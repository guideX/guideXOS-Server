#include <stdint.h>
#include <stddef.h>

#include "phase2_el0.h"
#include "phase2_mmu.h"
#include "../../../kernel/core/include/kernel/common_physical_allocator.h"

extern "C" uint64_t phase3_el0_enter(gxos_aarch64_el0_context* context);
extern "C" void phase3_serial_print(const char* text);
extern "C" void phase3_serial_hex(uint64_t value);
#if defined(GXOS_AARCH64_EL0_HOST_TEST)
extern "C" void* phase2_el0_host_physical_pointer(uint64_t physical_address);
#endif

namespace {

static_assert(offsetof(gxos_aarch64_el0_context, sp_el0) == GXOS_AARCH64_EL0_CONTEXT_SP_OFFSET,
              "EL0 context SP offset changed; update phase3_entry.S");
static_assert(offsetof(gxos_aarch64_el0_context, entry_pc) == GXOS_AARCH64_EL0_CONTEXT_PC_OFFSET,
              "EL0 context PC offset changed; update phase3_entry.S");
static_assert(offsetof(gxos_aarch64_el0_context, pstate) == GXOS_AARCH64_EL0_CONTEXT_PSTATE_OFFSET,
              "EL0 context PSTATE offset changed; update phase3_entry.S");
static_assert(offsetof(gxos_aarch64_el0_context, state) == GXOS_AARCH64_EL0_CONTEXT_STATE_OFFSET,
              "EL0 context state offset changed");
static_assert(offsetof(gxos_aarch64_el0_context, exit_value) == GXOS_AARCH64_EL0_CONTEXT_EXIT_OFFSET,
              "EL0 context exit offset changed; update phase3_entry.S");
static_assert(offsetof(gxos_aarch64_el0_context, kernel_sp) == GXOS_AARCH64_EL0_CONTEXT_KERNEL_SP_OFFSET,
              "EL0 context kernel SP offset changed; update phase3_entry.S");
static_assert(sizeof(gxos_aarch64_el0_context) == 352,
              "EL0 context ABI changed; update the exception bridge");

static const uint64_t kPageMask = GXOS_AARCH64_EL0_PAGE_SIZE - 1;
static const uint64_t kPhysicalMask = UINT64_C(0x000000fffffff000);
static const uint64_t kPhysicalLimit = UINT64_C(1) << GXOS_AARCH64_PHASE2_MMU_PA_BITS;
static const uint64_t kAddressMask = (UINT64_C(1) << GXOS_AARCH64_PHASE2_MMU_VA_BITS) - 1;
static const uint64_t kTableDescriptor = 3;
static const uint64_t kPageDescriptor = 3;
static const uint64_t kAttrNormal = UINT64_C(1) << 2;
static const uint64_t kShareInner = UINT64_C(3) << 8;
static const uint64_t kAccessFlag = UINT64_C(1) << 10;
static const uint64_t kNotGlobal = UINT64_C(1) << 11;
static const uint64_t kApReadOnlyUser = UINT64_C(3) << 6;
static const uint64_t kApReadWriteUser = UINT64_C(1) << 6;
static const uint64_t kPxn = UINT64_C(1) << 53;
static const uint64_t kUxn = UINT64_C(1) << 54;
static const uint64_t kUserRootIndex =
    (GXOS_AARCH64_EL0_USER_BASE >> 39) & UINT64_C(0x1ff);
static const uint64_t kExpectedUserPstate = UINT64_C(0x3c0); // EL0t, D/A/I/F masked.
static const uint64_t kQualificationOwner = UINT64_C(0x454c305155414c31); // "EL0QUAL1"

static uint64_t g_next_space_identity = 1;
#if !defined(GXOS_AARCH64_EL0_HOST_TEST)
// A dedicated, immutable kernel-owned sentinel.  The negative test reads this
// inert rodata location so it proves a present EL1-only mapping without
// sampling mutable address-space control state.
static const volatile uint64_t g_kernel_data_probe_sentinel =
    UINT64_C(0x47584f53454c3030); // "GXOSEL00"
#endif
static gxos_aarch64_el0_address_space* g_active_space = nullptr;
static gxos_aarch64_el0_context* g_active_context = nullptr;
static gxos_aarch64_el0_address_space g_qualification_space{};
static gxos_aarch64_el0_context g_qualification_context{};

#if defined(GXOS_AARCH64_EL0_HOST_TEST)
static void* physical_pointer(uint64_t physical_address)
{
    return phase2_el0_host_physical_pointer(physical_address);
}
#else
static void* physical_pointer(uint64_t physical_address)
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(physical_address));
}
#endif

static bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return *result > left;
}

static bool aligned_page(uint64_t address)
{
    return (address & kPageMask) == 0;
}

static bool in_user_window(uint64_t virtual_address)
{
    return virtual_address >= GXOS_AARCH64_EL0_USER_BASE &&
           virtual_address < GXOS_AARCH64_EL0_USER_LIMIT &&
           (virtual_address & ~kAddressMask) == 0;
}

static bool valid_flags(uint64_t flags)
{
    const uint64_t known = GXOS_AARCH64_EL0_PAGE_READ |
                           GXOS_AARCH64_EL0_PAGE_WRITE |
                           GXOS_AARCH64_EL0_PAGE_EXECUTE |
                           GXOS_AARCH64_EL0_PAGE_STACK;
    return (flags & ~known) == 0 &&
           (flags & GXOS_AARCH64_EL0_PAGE_READ) != 0 &&
           (flags & (GXOS_AARCH64_EL0_PAGE_WRITE | GXOS_AARCH64_EL0_PAGE_EXECUTE)) !=
               (GXOS_AARCH64_EL0_PAGE_WRITE | GXOS_AARCH64_EL0_PAGE_EXECUTE) &&
           ((flags & GXOS_AARCH64_EL0_PAGE_STACK) == 0 ||
            ((flags & GXOS_AARCH64_EL0_PAGE_WRITE) != 0 &&
             (flags & GXOS_AARCH64_EL0_PAGE_EXECUTE) == 0));
}

static bool page_owned(const gxos_aarch64_el0_address_space* space,
                       uint64_t physical_address)
{
    if (!space) return false;
    for (uint64_t i = 0; i < space->page_count; ++i) {
        if (space->owned_pages[i] == physical_address) return true;
    }
    return false;
}

static bool table_owned(const gxos_aarch64_el0_address_space* space,
                        uint64_t physical_address)
{
    if (!space) return false;
    for (uint64_t i = 0; i < space->table_count; ++i) {
        if (space->table_pages[i] == physical_address) return true;
    }
    return false;
}

static bool allocate_owned_page(gxos_aarch64_el0_address_space* space,
                                uint64_t* physical_address, bool table)
{
    if (!space || !physical_address || space->state != GXOS_AARCH64_EL0_SPACE_CREATED) return false;
    if ((table && space->table_count >= 8) || (!table && space->page_count >= 16)) return false;
    uint64_t physical = 0;
    if (!kernel::memory::allocate_pages(1, &physical) || !aligned_page(physical) ||
        physical >= kPhysicalLimit || !physical_pointer(physical)) {
        if (physical && aligned_page(physical)) kernel::memory::release_pages(physical, 1);
        return false;
    }
    uint64_t* words = static_cast<uint64_t*>(physical_pointer(physical));
    for (uint32_t i = 0; i < GXOS_AARCH64_EL0_PAGE_SIZE / sizeof(uint64_t); ++i) words[i] = 0;
    if (table) space->table_pages[space->table_count++] = physical;
    else space->owned_pages[space->page_count++] = physical;
    *physical_address = physical;
    return true;
}

static bool allocate_owned_table(gxos_aarch64_el0_address_space* space,
                                 uint64_t** virtual_table)
{
    uint64_t physical = 0;
    if (!virtual_table || !allocate_owned_page(space, &physical, true)) return false;
    *virtual_table = static_cast<uint64_t*>(physical_pointer(physical));
    return *virtual_table != nullptr;
}

static uint64_t table_address(uint64_t descriptor)
{
    return descriptor & kPhysicalMask;
}

static bool owned_child_table(gxos_aarch64_el0_address_space* space,
                              uint64_t descriptor, uint64_t** table)
{
    if ((descriptor & 3) != kTableDescriptor) return false;
    const uint64_t physical = table_address(descriptor);
    if (!aligned_page(physical) || physical >= kPhysicalLimit ||
        descriptor != (physical | kTableDescriptor) || !table_owned(space, physical)) return false;
    *table = static_cast<uint64_t*>(physical_pointer(physical));
    return *table != nullptr;
}

static bool ensure_user_l3(gxos_aarch64_el0_address_space* space,
                           uint64_t virtual_address, uint64_t** l3)
{
    if (!space || !l3 || !in_user_window(virtual_address)) return false;
    uint64_t* root = static_cast<uint64_t*>(physical_pointer(space->root));
    if (!root) return false;

    const uint32_t l0_index = static_cast<uint32_t>((virtual_address >> 39) & 0x1ff);
    const uint32_t l1_index = static_cast<uint32_t>((virtual_address >> 30) & 0x1ff);
    const uint32_t l2_index = static_cast<uint32_t>((virtual_address >> 21) & 0x1ff);
    uint64_t* l1 = nullptr;
    uint64_t* l2 = nullptr;

    if (l0_index != kUserRootIndex) return false;
    if (root[l0_index] == 0) {
        if (!allocate_owned_table(space, &l1)) return false;
        root[l0_index] = ((uint64_t)(uintptr_t)space->table_pages[space->table_count - 1] & kPhysicalMask) |
                         kTableDescriptor;
    } else if (!owned_child_table(space, root[l0_index], &l1)) {
        return false;
    }

    if (l1[l1_index] == 0) {
        if (!allocate_owned_table(space, &l2)) return false;
        l1[l1_index] = ((uint64_t)(uintptr_t)space->table_pages[space->table_count - 1] & kPhysicalMask) |
                       kTableDescriptor;
    } else if (!owned_child_table(space, l1[l1_index], &l2)) {
        return false;
    }

    if (l2[l2_index] == 0) {
        if (!allocate_owned_table(space, l3)) return false;
        l2[l2_index] = ((uint64_t)(uintptr_t)space->table_pages[space->table_count - 1] & kPhysicalMask) |
                       kTableDescriptor;
    } else if (!owned_child_table(space, l2[l2_index], l3)) {
        return false;
    }
    return true;
}

static uint64_t make_user_descriptor(uint64_t physical_address, uint64_t flags);

static bool lookup_mapping(const gxos_aarch64_el0_address_space* space,
                           uint64_t virtual_address, uint64_t* descriptor)
{
    if (!space || !descriptor || !in_user_window(virtual_address) || !space->root) return false;
    uint64_t* root = static_cast<uint64_t*>(physical_pointer(space->root));
    if (!root) return false;
    uint64_t* l1 = nullptr;
    uint64_t* l2 = nullptr;
    uint64_t* l3 = nullptr;
    if (!owned_child_table(const_cast<gxos_aarch64_el0_address_space*>(space),
                           root[(virtual_address >> 39) & 0x1ff], &l1) ||
        !owned_child_table(const_cast<gxos_aarch64_el0_address_space*>(space),
                           l1[(virtual_address >> 30) & 0x1ff], &l2) ||
        !owned_child_table(const_cast<gxos_aarch64_el0_address_space*>(space),
                           l2[(virtual_address >> 21) & 0x1ff], &l3)) return false;
    *descriptor = l3[(virtual_address >> 12) & 0x1ff];
    if ((*descriptor & 3) != kPageDescriptor) return false;
    const uint64_t page = virtual_address & ~kPageMask;
    for (uint64_t i = 0; i < space->mapping_count; ++i) {
        const gxos_aarch64_el0_mapping& mapping = space->mappings[i];
        if (mapping.virtual_address == page) {
            return mapping.owner_id == space->owner_id &&
                   *descriptor == make_user_descriptor(mapping.physical_address, mapping.flags);
        }
    }
    return false;
}

static uint64_t make_user_descriptor(uint64_t physical_address, uint64_t flags)
{
    uint64_t descriptor = (physical_address & kPhysicalMask) | kPageDescriptor |
                          kAttrNormal | kShareInner | kAccessFlag | kNotGlobal;
    descriptor |= (flags & GXOS_AARCH64_EL0_PAGE_WRITE) ? kApReadWriteUser : kApReadOnlyUser;
    descriptor |= kPxn;
    if ((flags & GXOS_AARCH64_EL0_PAGE_EXECUTE) == 0) descriptor |= kUxn;
    return descriptor;
}

static bool mapping_for(const gxos_aarch64_el0_address_space* space,
                        uint64_t virtual_address,
                        const gxos_aarch64_el0_mapping** mapping)
{
    if (!space || !mapping) return false;
    const uint64_t page = virtual_address & ~kPageMask;
    for (uint64_t i = 0; i < space->mapping_count; ++i) {
        if (space->mappings[i].virtual_address == page) {
            *mapping = &space->mappings[i];
            return true;
        }
    }
    return false;
}

static bool mapping_executable(const gxos_aarch64_el0_address_space* space,
                               uint64_t virtual_address)
{
    const gxos_aarch64_el0_mapping* mapping = nullptr;
    uint64_t descriptor = 0;
    return (virtual_address & 3) == 0 && mapping_for(space, virtual_address, &mapping) &&
           (mapping->flags & GXOS_AARCH64_EL0_PAGE_EXECUTE) != 0 &&
           (mapping->flags & GXOS_AARCH64_EL0_PAGE_WRITE) == 0 &&
           lookup_mapping(space, virtual_address, &descriptor) &&
           ((descriptor >> 6) & 3) == 3 &&
           (descriptor & kUxn) == 0 && (descriptor & kPxn) != 0;
}

static bool mapping_stack(const gxos_aarch64_el0_address_space* space,
                          uint64_t stack_pointer)
{
    if (!space || stack_pointer <= GXOS_AARCH64_EL0_USER_BASE ||
        stack_pointer > GXOS_AARCH64_EL0_USER_LIMIT || (stack_pointer & 0xf) != 0) return false;
    const uint64_t probe = stack_pointer - 1;
    const gxos_aarch64_el0_mapping* mapping = nullptr;
    uint64_t descriptor = 0;
    return mapping_for(space, probe, &mapping) &&
           (mapping->flags & (GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_WRITE)) ==
               (GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_WRITE) &&
           (mapping->flags & GXOS_AARCH64_EL0_PAGE_STACK) != 0 &&
           (mapping->flags & GXOS_AARCH64_EL0_PAGE_EXECUTE) == 0 &&
           lookup_mapping(space, probe, &descriptor) &&
           ((descriptor >> 6) & 3) == 1 &&
           (descriptor & (kPxn | kUxn)) == (kPxn | kUxn);
}

static void clean_tables_for_activation(const gxos_aarch64_el0_address_space* space)
{
#if defined(GXOS_AARCH64_EL0_HOST_TEST)
    (void)space;
#else
    for (uint64_t i = 0; i < space->table_count; ++i) {
        const uint64_t base = space->table_pages[i];
        for (uint64_t offset = 0; offset < GXOS_AARCH64_EL0_PAGE_SIZE; offset += 64) {
            const uint64_t address = base + offset;
            __asm__ volatile("dc cvac, %0" : : "r"(address) : "memory");
        }
    }
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}

static void sync_user_instruction_page(uint64_t physical_address)
{
#if defined(GXOS_AARCH64_EL0_HOST_TEST)
    (void)physical_address;
#else
    for (uint64_t offset = 0; offset < GXOS_AARCH64_EL0_PAGE_SIZE; offset += 64) {
        const uint64_t address = physical_address + offset;
        __asm__ volatile("dc cvau, %0" : : "r"(address) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
    for (uint64_t offset = 0; offset < GXOS_AARCH64_EL0_PAGE_SIZE; offset += 64) {
        const uint64_t address = physical_address + offset;
        __asm__ volatile("ic ivau, %0" : : "r"(address) : "memory");
    }
    __asm__ volatile("dsb ish\n isb" ::: "memory");
#endif
}

static void zero_bytes(void* buffer, uint64_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(buffer);
    for (uint64_t i = 0; i < size; ++i) bytes[i] = 0;
}

static void mark_fault(gxos_aarch64_el0_context* context, uint64_t reason,
                       uint64_t esr, uint64_t elr, uint64_t spsr, uint64_t far)
{
    context->fault_reason = reason;
    context->fault_esr = esr;
    context->fault_elr = elr;
    context->fault_spsr = spsr;
    context->fault_far = far;
    context->state = GXOS_AARCH64_EL0_CONTEXT_FAULTED;
}

} // namespace

extern "C" uint8_t phase2_el0_address_space_create(
    gxos_aarch64_el0_address_space* space, uint64_t owner_id)
{
    if (!space || owner_id == 0 || g_active_space ||
        g_next_space_identity == 0 || g_next_space_identity == UINT64_MAX) return 0;
    zero_bytes(space, sizeof(*space));
    const uint64_t kernel_root = phase2_mmu_root();
    if (!aligned_page(kernel_root) || kernel_root >= kPhysicalLimit ||
        phase2_mmu_current_root() != kernel_root) return 0;
    uint64_t* source = static_cast<uint64_t*>(physical_pointer(kernel_root));
    if (!source) return 0;
    for (uint32_t i = 0; i < 512; ++i) {
        if (source[i] != 0 && ((source[i] & 3) != kTableDescriptor ||
            (source[i] & kPhysicalMask) >= kPhysicalLimit)) return 0;
    }
    if (source[kUserRootIndex] != 0) return 0;

    space->owner_id = owner_id;
    space->kernel_root = kernel_root;
    space->state = GXOS_AARCH64_EL0_SPACE_CREATED;
    space->identity = g_next_space_identity++;
    uint64_t root = 0;
    if (!allocate_owned_page(space, &root, true)) {
        zero_bytes(space, sizeof(*space));
        return 0;
    }
    uint64_t* destination = static_cast<uint64_t*>(physical_pointer(root));
    if (!destination) {
        kernel::memory::release_pages(root, 1);
        zero_bytes(space, sizeof(*space));
        return 0;
    }
    for (uint32_t i = 0; i < 512; ++i) destination[i] = source[i];
    space->root = root;
    return 1;
}

extern "C" uint8_t phase2_el0_map_owned_page(
    gxos_aarch64_el0_address_space* space, uint64_t owner_id,
    uint64_t virtual_address, uint64_t physical_address, uint64_t flags)
{
    if (!space || space->state != GXOS_AARCH64_EL0_SPACE_CREATED ||
        owner_id == 0 || owner_id != space->owner_id || !in_user_window(virtual_address) ||
        !aligned_page(virtual_address) || !aligned_page(physical_address) ||
        physical_address >= kPhysicalLimit || !valid_flags(flags) ||
        !page_owned(space, physical_address) || space->mapping_count >= 16) return 0;
    uint64_t virtual_end = 0;
    if (!add_u64(virtual_address, GXOS_AARCH64_EL0_PAGE_SIZE, &virtual_end) ||
        virtual_end > GXOS_AARCH64_EL0_USER_LIMIT) return 0;
    for (uint64_t i = 0; i < space->mapping_count; ++i) {
        const uint64_t existing = space->mappings[i].virtual_address;
        uint64_t existing_end = 0;
        if (!add_u64(existing, GXOS_AARCH64_EL0_PAGE_SIZE, &existing_end)) return 0;
        if (space->mappings[i].physical_address == physical_address ||
            existing == virtual_address ||
            (existing < virtual_end && virtual_address < existing_end)) return 0;
    }
    uint64_t* l3 = nullptr;
    if (!ensure_user_l3(space, virtual_address, &l3)) return 0;
    const uint32_t index = static_cast<uint32_t>((virtual_address >> 12) & 0x1ff);
    if (l3[index] != 0) return 0;
    const uint64_t descriptor = make_user_descriptor(physical_address, flags);
    if ((descriptor & 3) != kPageDescriptor ||
        ((descriptor >> 6) & 3) == 0 ||
        ((flags & GXOS_AARCH64_EL0_PAGE_WRITE) && (descriptor & kUxn) == 0) ||
        ((flags & GXOS_AARCH64_EL0_PAGE_EXECUTE) &&
         ((descriptor & kUxn) != 0 || (descriptor & kPxn) == 0))) return 0;
    l3[index] = descriptor;
    gxos_aarch64_el0_mapping& mapping = space->mappings[space->mapping_count++];
    mapping.virtual_address = virtual_address;
    mapping.physical_address = physical_address;
    mapping.flags = flags;
    mapping.owner_id = owner_id;
    return 1;
}

extern "C" uint8_t phase2_el0_allocate_page(
    gxos_aarch64_el0_address_space* space, uint64_t owner_id,
    uint64_t virtual_address, uint64_t flags, uint64_t* physical_address)
{
    if (!space || !physical_address || !valid_flags(flags) ||
        space->state != GXOS_AARCH64_EL0_SPACE_CREATED || space->page_count >= 16) return 0;
    uint64_t physical = 0;
    if (!allocate_owned_page(space, &physical, false)) return 0;
    if (!phase2_el0_map_owned_page(space, owner_id, virtual_address, physical, flags)) return 0;
    *physical_address = physical;
    return 1;
}

extern "C" uint8_t phase2_el0_query_descriptor(
    const gxos_aarch64_el0_address_space* space, uint64_t virtual_address,
    uint64_t* descriptor)
{
    return lookup_mapping(space, virtual_address, descriptor) ? 1 : 0;
}

extern "C" uint8_t phase2_el0_validate_entry(
    const gxos_aarch64_el0_address_space* space, uint64_t entry_pc)
{
    return mapping_executable(space, entry_pc) ? 1 : 0;
}

extern "C" uint8_t phase2_el0_validate_stack(
    const gxos_aarch64_el0_address_space* space, uint64_t stack_pointer)
{
    return mapping_stack(space, stack_pointer) ? 1 : 0;
}

extern "C" uint8_t phase2_el0_validate_context(
    const gxos_aarch64_el0_address_space* space,
    const gxos_aarch64_el0_context* context, uint64_t expected_owner_id)
{
    if (!space || !context || space->state != GXOS_AARCH64_EL0_SPACE_CREATED ||
        expected_owner_id == 0 || expected_owner_id != space->owner_id ||
        context->state != GXOS_AARCH64_EL0_CONTEXT_PREPARED ||
        context->address_space_id != space->identity ||
        context->owner_id != expected_owner_id || context->pstate != kExpectedUserPstate ||
        !phase2_el0_validate_entry(space, context->entry_pc) ||
        !phase2_el0_validate_stack(space, context->sp_el0)) return 0;
    return 1;
}

extern "C" uint8_t phase2_el0_begin_context(
    gxos_aarch64_el0_address_space* space,
    gxos_aarch64_el0_context* context, uint64_t expected_owner_id)
{
    if (g_active_space || g_active_context ||
        !phase2_el0_validate_context(space, context, expected_owner_id)) return 0;
    context->state = GXOS_AARCH64_EL0_CONTEXT_RUNNING;
    g_active_context = context;
    if (!phase2_el0_activate(space)) {
        g_active_context = nullptr;
        context->state = GXOS_AARCH64_EL0_CONTEXT_PREPARED;
        return 0;
    }
    return 1;
}

extern "C" uint8_t phase2_el0_activate(gxos_aarch64_el0_address_space* space)
{
    if (!space || space->state != GXOS_AARCH64_EL0_SPACE_CREATED || g_active_space ||
        !space->root || !space->kernel_root || phase2_mmu_current_root() != space->kernel_root) return 0;
    clean_tables_for_activation(space);
    if (!phase2_mmu_activate_root(space->root)) return 0;
    space->state = GXOS_AARCH64_EL0_SPACE_ACTIVE;
    g_active_space = space;
    return 1;
}

extern "C" uint8_t phase2_el0_deactivate(gxos_aarch64_el0_address_space* space)
{
    if (!space || space != g_active_space || space->state != GXOS_AARCH64_EL0_SPACE_ACTIVE ||
        phase2_mmu_current_root() != space->root || !phase2_mmu_activate_root(space->kernel_root)) return 0;
    space->state = GXOS_AARCH64_EL0_SPACE_CREATED;
    g_active_space = nullptr;
    g_active_context = nullptr;
    return 1;
}

extern "C" uint8_t phase2_el0_cleanup(gxos_aarch64_el0_address_space* space)
{
    if (!space || space->state == GXOS_AARCH64_EL0_SPACE_EMPTY ||
        space->state == GXOS_AARCH64_EL0_SPACE_ACTIVE || space == g_active_space ||
        phase2_mmu_current_root() == space->root) return 0;
    if (space->cleanup_state == 0) {
        space->cleanup_state = 1;
        space->state = GXOS_AARCH64_EL0_SPACE_CLEANING;
    }
    for (uint64_t i = 0; i < space->page_count; ++i) {
        if (space->owned_pages[i] != 0) {
            if (!kernel::memory::release_pages(space->owned_pages[i], 1)) return 0;
            space->owned_pages[i] = 0;
        }
    }
    for (uint64_t i = 0; i < space->table_count; ++i) {
        if (space->table_pages[i] != 0) {
            if (!kernel::memory::release_pages(space->table_pages[i], 1)) return 0;
            space->table_pages[i] = 0;
        }
    }
    space->root = 0;
    space->mapping_count = 0;
    space->page_count = 0;
    space->table_count = 0;
    space->state = GXOS_AARCH64_EL0_SPACE_CLEANED;
    space->cleanup_state = 2;
    return 1;
}

extern "C" uint8_t phase2_el0_dispatch_exception(
    uint64_t* register_frame, uint64_t vector_class, uint64_t esr,
    uint64_t elr, uint64_t spsr, uint64_t far,
    gxos_aarch64_el0_context** return_context)
{
    if (!register_frame || !return_context || !g_active_space || !g_active_context ||
        g_active_space->state != GXOS_AARCH64_EL0_SPACE_ACTIVE ||
        g_active_context->state != GXOS_AARCH64_EL0_CONTEXT_RUNNING ||
        g_active_context->address_space_id != g_active_space->identity ||
        g_active_context->owner_id != g_active_space->owner_id ||
        phase2_mmu_current_root() != g_active_space->root || (spsr & 0xf) != 0) return 0;

    gxos_aarch64_el0_context* context = g_active_context;
    uint64_t reason = GXOS_AARCH64_EL0_FAULT_NONE;
    if (vector_class == 6) {
        reason = GXOS_AARCH64_EL0_FAULT_UNEXPECTED_INTERRUPT;
    } else if (vector_class != 5) {
        reason = GXOS_AARCH64_EL0_FAULT_BAD_EXCEPTION_STATE;
    } else {
        const uint64_t exception_class = (esr >> 26) & 0x3f;
        if (exception_class == 0x15) { // SVC from AArch64 EL0.
            const uint64_t immediate = esr & 0xffff;
            const bool valid_call_site = elr >= 4 && mapping_executable(g_active_space, elr - 4);
            if (immediate != GXOS_AARCH64_EL0_SVC_IMMEDIATE || !valid_call_site ||
                register_frame[8] != GXOS_AARCH64_EL0_EXIT_SERVICE) {
                reason = GXOS_AARCH64_EL0_FAULT_UNSUPPORTED_SVC;
            } else {
                for (uint32_t i = 0; i < 31; ++i) context->x[i] = register_frame[i];
                context->exit_value = register_frame[0];
                context->fault_esr = esr;
                context->fault_elr = elr;
                context->fault_spsr = spsr;
                context->state = GXOS_AARCH64_EL0_CONTEXT_EXITED;
            }
        } else if (exception_class == 0x20) {
            reason = GXOS_AARCH64_EL0_FAULT_INSTRUCTION_ABORT;
        } else if (exception_class == 0x24) {
            reason = GXOS_AARCH64_EL0_FAULT_DATA_ABORT;
        } else {
            reason = GXOS_AARCH64_EL0_FAULT_UNEXPECTED_SYNC;
        }
    }

    if (reason != GXOS_AARCH64_EL0_FAULT_NONE) {
        mark_fault(context, reason, esr, elr, spsr, far);
    }
    g_active_space->state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
    if (!phase2_mmu_activate_root(g_active_space->kernel_root)) return 0;
    g_active_space = nullptr;
    g_active_context = nullptr;
    *return_context = context;
    return 1;
}

extern "C" uint8_t phase2_el0_run_qualification(
    uint64_t* observed_exit_value, uint64_t* fault_reason)
{
    static const uint8_t payload[] = {
        0x69, 0x02, 0x80, 0xd2, // mov x9, #19
        0xea, 0x02, 0x80, 0xd2, // mov x10, #23
        0x20, 0x01, 0x0a, 0x8b, // add x0, x9, x10
        0x28, 0x00, 0x80, 0xd2, // mov x8, #GXOS_AARCH64_EL0_EXIT_SERVICE
        0x01, 0x00, 0x00, 0xd4  // svc #GXOS_AARCH64_EL0_SVC_IMMEDIATE
    };
    static const uint64_t owner = kQualificationOwner;
    uint64_t code_physical = 0;
    uint64_t data_physical = 0;
    uint64_t stack_physical = 0;
    if (observed_exit_value) *observed_exit_value = 0;
    if (fault_reason) *fault_reason = GXOS_AARCH64_EL0_FAULT_NONE;
    if (g_active_space || g_active_context) return 0;
    if (g_qualification_space.state != GXOS_AARCH64_EL0_SPACE_EMPTY) {
        if (g_qualification_space.state != GXOS_AARCH64_EL0_SPACE_CLEANED &&
            !phase2_el0_cleanup(&g_qualification_space)) return 0;
        zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
    }
    if (!phase2_el0_address_space_create(&g_qualification_space, owner)) return 0;

    const uint64_t code_va = GXOS_AARCH64_EL0_USER_BASE;
    const uint64_t data_va = GXOS_AARCH64_EL0_USER_BASE + 0x2000;
    const uint64_t stack_va = GXOS_AARCH64_EL0_USER_BASE + 0x4000;
    const uint64_t rw_flags = GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_WRITE;
    const uint64_t rx_flags = GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_EXECUTE;
    const uint64_t stack_flags = rw_flags | GXOS_AARCH64_EL0_PAGE_STACK;
    bool ok = phase2_el0_allocate_page(&g_qualification_space, owner, code_va, rx_flags, &code_physical) &&
              phase2_el0_allocate_page(&g_qualification_space, owner, data_va, rw_flags, &data_physical) &&
              phase2_el0_allocate_page(&g_qualification_space, owner, stack_va, stack_flags, &stack_physical);
    if (!ok) {
        if (fault_reason) *fault_reason = GXOS_AARCH64_EL0_FAULT_BAD_EXCEPTION_STATE;
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }
    uint8_t* code = static_cast<uint8_t*>(physical_pointer(code_physical));
    for (uint32_t i = 0; i < sizeof(payload); ++i) code[i] = payload[i];
    sync_user_instruction_page(code_physical);

    zero_bytes(&g_qualification_context, sizeof(g_qualification_context));
    g_qualification_context.sp_el0 = stack_va + GXOS_AARCH64_EL0_PAGE_SIZE;
    g_qualification_context.entry_pc = code_va;
    g_qualification_context.pstate = kExpectedUserPstate;
    g_qualification_context.address_space_id = g_qualification_space.identity;
    g_qualification_context.owner_id = owner;
    g_qualification_context.state = GXOS_AARCH64_EL0_CONTEXT_PREPARED;
    if (!phase2_el0_validate_context(&g_qualification_space, &g_qualification_context, owner)) {
        if (fault_reason) *fault_reason = GXOS_AARCH64_EL0_FAULT_BAD_EXCEPTION_STATE;
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }

#if !defined(GXOS_AARCH64_EL0_HOST_TEST)
    uint64_t saved_daif = 0;
    __asm__ volatile("mrs %0, daif\n msr daifset, #0xf\n isb" : "=r"(saved_daif) : : "memory");
#endif
    if (!phase2_el0_begin_context(&g_qualification_space,
                                  &g_qualification_context, owner)) {
#if !defined(GXOS_AARCH64_EL0_HOST_TEST)
        __asm__ volatile("msr daif, %0\n isb" : : "r"(saved_daif) : "memory");
#endif
        if (fault_reason) *fault_reason = GXOS_AARCH64_EL0_FAULT_BAD_EXCEPTION_STATE;
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }
#if defined(GXOS_AARCH64_EL0_HOST_TEST)
    const uint64_t returned = 0;
#else
    const uint64_t returned = phase3_el0_enter(&g_qualification_context);
    __asm__ volatile("msr daif, %0\n isb" : : "r"(saved_daif) : "memory");
#endif

    const bool actual_exit = g_qualification_context.state == GXOS_AARCH64_EL0_CONTEXT_EXITED &&
        g_qualification_context.exit_value == returned && returned == 42 &&
        phase2_mmu_current_root() == g_qualification_space.kernel_root;
    if (observed_exit_value && g_qualification_context.state == GXOS_AARCH64_EL0_CONTEXT_EXITED)
        *observed_exit_value = g_qualification_context.exit_value;
    if (fault_reason) *fault_reason = g_qualification_context.fault_reason;
    g_qualification_space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
    const bool cleaned = phase2_el0_cleanup(&g_qualification_space) != 0;
    if (actual_exit && cleaned) {
        phase3_serial_print("[guideXOS] EL0 trusted payload bytes=20 sha256=9adb67ab60e0329266473bb0c419ff4167df6fb77c59fec2aff1de57421dd442 exit=");
        phase3_serial_hex(returned);
        phase3_serial_print(" cleanup=PASS\n");
    }
    if (cleaned) zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
    zero_bytes(&g_qualification_context, sizeof(g_qualification_context));
    return actual_exit && cleaned ? 1 : 0;
}

static uint32_t make_movz_x1(uint16_t immediate, uint32_t shift)
{
    return UINT32_C(0xd2800001) | (static_cast<uint32_t>(immediate) << 5) |
           ((shift / 16) << 21);
}

static uint32_t make_movk_x1(uint16_t immediate, uint32_t shift)
{
    return UINT32_C(0xf2800001) | (static_cast<uint32_t>(immediate) << 5) |
           ((shift / 16) << 21);
}

extern "C" uint8_t phase2_el0_run_negative_probe(
    uint64_t probe_kind, uint64_t device_address,
    gxos_aarch64_el0_negative_probe_result* result)
{
    if (!result) return 0;
    zero_bytes(result, sizeof(*result));
    if (g_active_space || g_active_context) return 0;

    uint64_t target_address = 0;
    if (probe_kind == GXOS_AARCH64_EL0_PROBE_KERNEL_DATA) {
#if defined(GXOS_AARCH64_EL0_HOST_TEST)
        target_address = UINT64_C(0x00300000);
#else
        target_address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
            &g_kernel_data_probe_sentinel)) & ~UINT64_C(7);
#endif
    } else if (probe_kind == GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO) {
        target_address = device_address;
    } else {
        return 0;
    }
    if (target_address == 0 || (target_address & ~kAddressMask) != 0 ||
        (probe_kind == GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO &&
         (target_address & kPageMask) != 0)) return 0;

    const uint64_t kernel_descriptor = phase2_mmu_descriptor_for(target_address);
    const uint64_t expected_attribute =
        probe_kind == GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO ? 0 : 1;
    const uint64_t expected_ap =
        probe_kind == GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO ? 0 : (UINT64_C(2) << 6);
    if ((kernel_descriptor & 3) != kPageDescriptor ||
        (kernel_descriptor & kPhysicalMask) != (target_address & ~kPageMask) ||
        ((kernel_descriptor >> 2) & 7) != expected_attribute ||
        (kernel_descriptor & (UINT64_C(3) << 6)) != expected_ap ||
        (kernel_descriptor & kAccessFlag) == 0 ||
        (kernel_descriptor & (kPxn | kUxn)) != (kPxn | kUxn)) return 0;
    result->target_address = target_address;
    result->kernel_descriptor = kernel_descriptor;

    if (g_qualification_space.state != GXOS_AARCH64_EL0_SPACE_EMPTY) {
        if (g_qualification_space.state != GXOS_AARCH64_EL0_SPACE_CLEANED &&
            !phase2_el0_cleanup(&g_qualification_space)) return 0;
        zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
    }
    const uint64_t owner = kQualificationOwner;
    if (!phase2_el0_address_space_create(&g_qualification_space, owner)) return 0;
    result->child_identity = g_qualification_space.identity;
    result->owner_id = owner;

    const uint64_t code_va = GXOS_AARCH64_EL0_USER_BASE;
    const uint64_t stack_va = GXOS_AARCH64_EL0_USER_BASE + 0x4000;
    const uint64_t rx_flags = GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_EXECUTE;
    const uint64_t stack_flags = GXOS_AARCH64_EL0_PAGE_READ |
                                 GXOS_AARCH64_EL0_PAGE_WRITE |
                                 GXOS_AARCH64_EL0_PAGE_STACK;
    uint64_t code_physical = 0;
    uint64_t stack_physical = 0;
    const bool mapped =
        phase2_el0_allocate_page(&g_qualification_space, owner, code_va, rx_flags, &code_physical) &&
        phase2_el0_allocate_page(&g_qualification_space, owner, stack_va, stack_flags, &stack_physical);
    if (!mapped) {
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }

    // Four MOV-wide instructions materialize only the kernel-approved target.
    // LDR should raise a level-3 permission fault; the SVC is a failure path if
    // an EL0 access unexpectedly succeeds.
    const uint32_t instructions[7] = {
        make_movz_x1(static_cast<uint16_t>(target_address), 0),
        make_movk_x1(static_cast<uint16_t>(target_address >> 16), 16),
        make_movk_x1(static_cast<uint16_t>(target_address >> 32), 32),
        make_movk_x1(static_cast<uint16_t>(target_address >> 48), 48),
        UINT32_C(0xf9400020), // ldr x0, [x1]
        UINT32_C(0xd2800028), // mov x8, #1
        UINT32_C(0xd4000001)  // svc #0
    };
    uint8_t* code = static_cast<uint8_t*>(physical_pointer(code_physical));
    if (!code) {
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }
    for (uint32_t i = 0; i < 7; ++i) {
        for (uint32_t byte = 0; byte < 4; ++byte)
            code[i * 4 + byte] = static_cast<uint8_t>(instructions[i] >> (byte * 8));
    }
    sync_user_instruction_page(code_physical);

    zero_bytes(&g_qualification_context, sizeof(g_qualification_context));
    g_qualification_context.sp_el0 = stack_va + GXOS_AARCH64_EL0_PAGE_SIZE;
    g_qualification_context.entry_pc = code_va;
    g_qualification_context.pstate = kExpectedUserPstate;
    g_qualification_context.address_space_id = g_qualification_space.identity;
    g_qualification_context.owner_id = owner;
    g_qualification_context.state = GXOS_AARCH64_EL0_CONTEXT_PREPARED;
    if (!phase2_el0_validate_context(&g_qualification_space,
                                     &g_qualification_context, owner)) {
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }

#if !defined(GXOS_AARCH64_EL0_HOST_TEST)
    uint64_t saved_daif = 0;
    __asm__ volatile("mrs %0, daif\n msr daifset, #0xf\n isb" : "=r"(saved_daif) : : "memory");
#endif
    if (!phase2_el0_begin_context(&g_qualification_space,
                                  &g_qualification_context, owner)) {
#if !defined(GXOS_AARCH64_EL0_HOST_TEST)
        __asm__ volatile("msr daif, %0\n isb" : : "r"(saved_daif) : "memory");
#endif
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }

#if defined(GXOS_AARCH64_EL0_HOST_TEST)
    uint64_t simulated_frame[31] = {};
    gxos_aarch64_el0_context* simulated_return = nullptr;
    const uint64_t simulated_esr = (UINT64_C(0x24) << 26) | UINT64_C(0x0f);
    if (!phase2_el0_dispatch_exception(simulated_frame,
                                       GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
                                       simulated_esr, code_va + 16, kExpectedUserPstate,
                                       target_address, &simulated_return)) {
        phase2_el0_deactivate(&g_qualification_space);
        if (phase2_el0_cleanup(&g_qualification_space))
            zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
        return 0;
    }
    (void)simulated_return;
    const uint64_t returned = 0;
#else
    const uint64_t returned = phase3_el0_enter(&g_qualification_context);
    __asm__ volatile("msr daif, %0\n isb" : : "r"(saved_daif) : "memory");
#endif

    const bool expected_fault =
        g_qualification_context.state == GXOS_AARCH64_EL0_CONTEXT_FAULTED &&
        g_qualification_context.fault_reason == GXOS_AARCH64_EL0_FAULT_DATA_ABORT &&
        ((g_qualification_context.fault_esr >> 26) & UINT64_C(0x3f)) == UINT64_C(0x24) &&
        (g_qualification_context.fault_esr & UINT64_C(0x7f)) == UINT64_C(0x0f) &&
        g_qualification_context.fault_elr == code_va + 16 &&
        g_qualification_context.fault_spsr == kExpectedUserPstate &&
        g_qualification_context.fault_far == target_address && returned == 0 &&
        phase2_mmu_current_root() == g_qualification_space.kernel_root;
    result->context_state = g_qualification_context.state;
    result->fault_reason = g_qualification_context.fault_reason;
    result->esr = g_qualification_context.fault_esr;
    result->elr = g_qualification_context.fault_elr;
    result->spsr = g_qualification_context.fault_spsr;
    result->far = g_qualification_context.fault_far;
    result->target_access_succeeded =
        g_qualification_context.state == GXOS_AARCH64_EL0_CONTEXT_EXITED;
    result->read_value = g_qualification_context.exit_value;
    result->kernel_root_restored =
        phase2_mmu_current_root() == g_qualification_space.kernel_root;
    result->data_pages_before_cleanup = g_qualification_space.page_count;
    result->table_pages_before_cleanup = g_qualification_space.table_count;
    g_qualification_space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
    const bool cleaned = phase2_el0_cleanup(&g_qualification_space) != 0;
    result->cleanup_succeeded = cleaned;
    if (expected_fault && cleaned) {
        phase3_serial_print("[guideXOS] EL0 negative permission fault ESR=");
        phase3_serial_hex(g_qualification_context.fault_esr);
        phase3_serial_print(" FAR=");
        phase3_serial_hex(g_qualification_context.fault_far);
        phase3_serial_print(" cleanup=PASS\n");
    }
    if (cleaned) zero_bytes(&g_qualification_space, sizeof(g_qualification_space));
    zero_bytes(&g_qualification_context, sizeof(g_qualification_context));
    return expected_fault && cleaned ? 1 : 0;
}
