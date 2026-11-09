#include <stdint.h>
#include <stdio.h>

#include "kernel/arch/arm64/phase2_el0.h"
#include "kernel/arch/arm64/phase2_mmu.h"
#include "kernel/core/include/kernel/common_physical_allocator.h"

extern "C" void* phase2_el0_host_physical_pointer(uint64_t physical_address);

namespace {

static const uint64_t kPhysicalBase = UINT64_C(0x00100000);
static const uint32_t kArenaPages = 128;
alignas(4096) static uint8_t g_physical_pages[kArenaPages][4096];
static bool g_page_allocated[kArenaPages];
static uint64_t g_current_root = kPhysicalBase;
static uint64_t g_assertions = 0;
static uint64_t g_failures = 0;

static void expect(bool condition, const char* label)
{
    ++g_assertions;
    if (!condition) {
        ++g_failures;
        printf("FAIL: %s\n", label);
    }
}

static void reset_fake_memory()
{
    for (uint32_t i = 0; i < kArenaPages; ++i) {
        g_page_allocated[i] = false;
        for (uint32_t j = 0; j < 4096; ++j) g_physical_pages[i][j] = 0;
    }
    g_page_allocated[0] = true; // Kernel root owned by the test fixture.
    g_page_allocated[1] = true; // Shared kernel L1 table.
    g_current_root = kPhysicalBase;
    uint64_t* root = reinterpret_cast<uint64_t*>(g_physical_pages[0]);
    root[0] = (kPhysicalBase + 4096) | 3;
}

static uint64_t* table_at(uint64_t physical)
{
    return static_cast<uint64_t*>(phase2_el0_host_physical_pointer(physical));
}

struct Fixture {
    gxos_aarch64_el0_address_space space;
    gxos_aarch64_el0_context context;
    uint64_t owner;
    uint64_t code_page;
    uint64_t data_page;
    uint64_t stack_page;
};

static bool make_fixture(Fixture* f, uint64_t owner)
{
    if (!f) return false;
    for (uint64_t i = 0; i < sizeof(*f); ++i) reinterpret_cast<uint8_t*>(f)[i] = 0;
    f->owner = owner;
    if (!phase2_el0_address_space_create(&f->space, owner)) return false;
    const uint64_t rw = GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_WRITE;
    const uint64_t rx = GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_EXECUTE;
    const uint64_t stack = rw | GXOS_AARCH64_EL0_PAGE_STACK;
    if (!phase2_el0_allocate_page(&f->space, owner, GXOS_AARCH64_EL0_USER_BASE,
                                  rx, &f->code_page) ||
        !phase2_el0_allocate_page(&f->space, owner, GXOS_AARCH64_EL0_USER_BASE + 0x2000,
                                  rw, &f->data_page) ||
        !phase2_el0_allocate_page(&f->space, owner, GXOS_AARCH64_EL0_USER_BASE + 0x4000,
                                  stack, &f->stack_page)) return false;
    f->context.sp_el0 = GXOS_AARCH64_EL0_USER_BASE + 0x5000;
    f->context.entry_pc = GXOS_AARCH64_EL0_USER_BASE;
    f->context.pstate = 0x3c0;
    f->context.address_space_id = f->space.identity;
    f->context.owner_id = owner;
    f->context.state = GXOS_AARCH64_EL0_CONTEXT_PREPARED;
    return true;
}

static void cleanup_fixture(Fixture* f)
{
    if (!f) return;
    if (f->space.state == GXOS_AARCH64_EL0_SPACE_ACTIVE)
        phase2_el0_deactivate(&f->space);
    phase2_el0_cleanup(&f->space);
}

static void test_permissions_and_ownership()
{
    reset_fake_memory();
    Fixture a{};
    Fixture b{};
    expect(make_fixture(&a, 11), "create the first bounded child address space");
    expect(make_fixture(&b, 12), "create an independent second child address space");
    expect(a.space.identity != b.space.identity && a.space.root != b.space.root,
           "each child receives a distinct address-space identity and root");

    uint64_t* root_a = table_at(a.space.root);
    uint64_t* root_b = table_at(b.space.root);
    expect(root_a[0] == table_at(kPhysicalBase)[0],
           "child root preserves the shared kernel EL1 mapping");
    expect(root_a[128] != 0 && root_b[128] != 0 && root_a[128] != root_b[128],
           "private user translation subtrees are not inherited by another child");
    expect(root_a[128] != table_at(kPhysicalBase)[128],
           "child user subtree is absent from the kernel root");

    uint64_t descriptor = 0;
    expect(phase2_el0_query_descriptor(&a.space, GXOS_AARCH64_EL0_USER_BASE, &descriptor) &&
           ((descriptor >> 6) & 3) == 3 && (descriptor & (UINT64_C(1) << 53)) != 0 &&
           (descriptor & (UINT64_C(1) << 54)) == 0,
           "EL0 code is readable, read-only, user-executable and EL1 execute-never");
    expect(phase2_el0_query_descriptor(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x2000, &descriptor) &&
           ((descriptor >> 6) & 3) == 1 && (descriptor & (UINT64_C(1) << 53)) != 0 &&
           (descriptor & (UINT64_C(1) << 54)) != 0,
           "EL0 data is writable and execute-never at EL0 and EL1");
    expect(phase2_el0_query_descriptor(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x4000, &descriptor) &&
           ((descriptor >> 6) & 3) == 1 && (descriptor & (UINT64_C(1) << 53)) != 0 &&
           (descriptor & (UINT64_C(1) << 54)) != 0,
           "EL0 stack is writable and execute-never at EL0 and EL1");

    expect(!phase2_el0_query_descriptor(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x1000, &descriptor) &&
           !phase2_el0_query_descriptor(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x3000, &descriptor) &&
           !phase2_el0_query_descriptor(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x5000, &descriptor),
           "guard pages remain unmapped");
    expect(!phase2_el0_query_descriptor(&a.space, UINT64_C(0x40000000), &descriptor) &&
           !phase2_el0_query_descriptor(&a.space, UINT64_C(0x08000000), &descriptor),
           "kernel and device virtual addresses are outside the child user window");
    expect(phase2_el0_validate_entry(&a.space, a.context.entry_pc) != 0 &&
           !phase2_el0_validate_entry(&a.space, a.context.entry_pc + 1) &&
           !phase2_el0_validate_entry(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x2000),
           "entry validation requires aligned EL0-executable code");
    expect(phase2_el0_validate_stack(&a.space, a.context.sp_el0) != 0 &&
           !phase2_el0_validate_stack(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x3000) &&
           !phase2_el0_validate_stack(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x3000 + 16) &&
           !phase2_el0_validate_stack(&a.space, GXOS_AARCH64_EL0_USER_BASE + 0x2000 + 16),
           "stack validation rejects guard and non-stack data pages");

    expect(!phase2_el0_allocate_page(&a.space, a.owner, GXOS_AARCH64_EL0_USER_BASE + 0x6000,
           GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_WRITE |
           GXOS_AARCH64_EL0_PAGE_EXECUTE, &descriptor),
           "user-writable executable mappings are rejected");
    expect(!phase2_el0_allocate_page(&a.space, a.owner, GXOS_AARCH64_EL0_USER_BASE + 0x6000,
           GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_STACK, &descriptor),
           "stack metadata cannot omit write permission");
    expect(!phase2_el0_map_owned_page(&a.space, a.owner,
           GXOS_AARCH64_EL0_USER_BASE + 0x6000, kPhysicalBase,
           GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_WRITE),
           "kernel-owned physical pages cannot be mapped as child-owned pages");
    expect(!phase2_el0_map_owned_page(&a.space, a.owner,
           GXOS_AARCH64_EL0_USER_BASE + 0x6000, UINT64_C(0x08000000),
           GXOS_AARCH64_EL0_PAGE_READ),
           "device MMIO cannot be mapped without child ownership");
    expect(!phase2_el0_map_owned_page(&a.space, a.owner + 1,
           GXOS_AARCH64_EL0_USER_BASE + 0x6000, a.data_page,
           GXOS_AARCH64_EL0_PAGE_READ),
           "mapping with the wrong child owner is rejected");
    expect(!phase2_el0_map_owned_page(&a.space, a.owner,
           GXOS_AARCH64_EL0_USER_BASE + 0x6000, a.code_page,
           GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_WRITE),
           "a physical page cannot gain a writable alias to executable code");
    expect(!phase2_el0_map_owned_page(&a.space, a.owner,
           GXOS_AARCH64_EL0_USER_BASE + 0x6000, a.data_page + 1,
           GXOS_AARCH64_EL0_PAGE_READ),
           "unaligned and non-owned physical page input is rejected");
    expect(!phase2_el0_map_owned_page(&a.space, a.owner,
           GXOS_AARCH64_EL0_USER_BASE, a.data_page,
           GXOS_AARCH64_EL0_PAGE_READ),
           "overlapping user virtual mappings are rejected");
    expect(!phase2_el0_allocate_page(&a.space, a.owner,
           GXOS_AARCH64_EL0_USER_BASE + 1, GXOS_AARCH64_EL0_PAGE_READ, &descriptor),
           "unaligned user mapping is rejected");
    expect(!phase2_el0_allocate_page(&a.space, a.owner, UINT64_MAX & ~UINT64_C(0xfff),
           GXOS_AARCH64_EL0_PAGE_READ, &descriptor),
           "out-of-window and overflowing user ranges are rejected");

    cleanup_fixture(&a);
    cleanup_fixture(&b);
    expect(a.space.state == GXOS_AARCH64_EL0_SPACE_CLEANED &&
           b.space.state == GXOS_AARCH64_EL0_SPACE_CLEANED,
           "both child address spaces release their private resources");
}

static void test_context_and_svc_exit()
{
    reset_fake_memory();
    Fixture f{};
    expect(make_fixture(&f, 21), "prepare SVC qualification fixture");
    expect(phase2_el0_validate_context(&f.space, &f.context, f.owner),
           "valid EL0 context passes entry, stack, owner and PSTATE checks");
    gxos_aarch64_el0_context changed = f.context;
    changed.address_space_id++;
    expect(!phase2_el0_validate_context(&f.space, &changed, f.owner),
           "context with the wrong address-space identity is rejected");
    changed = f.context;
    changed.owner_id++;
    expect(!phase2_el0_validate_context(&f.space, &changed, f.owner),
           "context with the wrong child owner is rejected");
    changed = f.context;
    changed.pstate = 0x5; // EL1h must never be used for a child return.
    expect(!phase2_el0_validate_context(&f.space, &changed, f.owner),
           "EL1 PSTATE is rejected for an EL0 return");
    changed = f.context;
    changed.entry_pc = GXOS_AARCH64_EL0_USER_BASE + 0x2000;
    expect(!phase2_el0_validate_context(&f.space, &changed, f.owner),
           "data address cannot be used as an EL0 entry point");
    changed = f.context;
    changed.sp_el0 = GXOS_AARCH64_EL0_USER_BASE + 0x3000;
    expect(!phase2_el0_validate_context(&f.space, &changed, f.owner),
           "guard address cannot be used as an EL0 stack pointer");
    expect(!phase2_el0_validate_context(&f.space, &f.context, f.owner + 1),
           "lifecycle owner must match the address-space owner");

    expect(phase2_el0_begin_context(&f.space, &f.context, f.owner),
           "validated context activates its child-owned root");
    expect(g_current_root == f.space.root && f.space.state == GXOS_AARCH64_EL0_SPACE_ACTIVE,
           "TTBR0 selects the child root before EL0 entry");
    expect(!phase2_el0_cleanup(&f.space),
           "active address-space resources cannot be freed");

    uint64_t frame[31] = {};
    frame[0] = 42;
    frame[8] = GXOS_AARCH64_EL0_EXIT_SERVICE;
    gxos_aarch64_el0_context* returned = nullptr;
    const uint64_t svc_esr = UINT64_C(0x15) << 26;
    expect(phase2_el0_dispatch_exception(frame, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
           svc_esr, f.context.entry_pc + 20, 0x3c0, 0, &returned),
           "expected SVC is accepted from an active EL0 child");
    expect(returned == &f.context && f.context.state == GXOS_AARCH64_EL0_CONTEXT_EXITED &&
           f.context.exit_value == 42 && f.context.fault_esr == svc_esr &&
           f.context.fault_elr == f.context.entry_pc + 20 &&
           f.context.fault_spsr == 0x3c0,
           "exit service preserves the child-provided result and exception state");
    expect(g_current_root == f.space.kernel_root &&
           f.space.state == GXOS_AARCH64_EL0_SPACE_TERMINAL,
           "SVC return restores the kernel root before lifecycle cleanup");
    expect(phase2_el0_cleanup(&f.space), "completed child resources are reclaimed");
    expect(phase2_el0_cleanup(&f.space), "cleanup remains idempotent");
}

static void run_fault_case(uint64_t owner, uint64_t vector, uint64_t esr,
                           uint64_t expected_reason, const char* label)
{
    reset_fake_memory();
    Fixture f{};
    expect(make_fixture(&f, owner), "prepare lower-EL fault fixture");
    expect(phase2_el0_begin_context(&f.space, &f.context, owner),
           "fault fixture activates its child address space");
    uint64_t frame[31] = {};
    frame[0] = 77;
    frame[8] = 99;
    gxos_aarch64_el0_context* returned = nullptr;
    expect(phase2_el0_dispatch_exception(frame, vector, esr,
           f.context.entry_pc, 0x3c0, UINT64_C(0xdead0000), &returned), label);
    expect(returned == &f.context && f.context.state == GXOS_AARCH64_EL0_CONTEXT_FAULTED &&
           f.context.fault_reason == expected_reason && f.context.fault_esr == esr &&
           f.context.fault_elr == f.context.entry_pc && f.context.fault_spsr == 0x3c0 &&
           f.context.fault_far == UINT64_C(0xdead0000),
           "lower-EL fault records ESR, ELR, SPSR, FAR and terminal state");
    expect(g_current_root == f.space.kernel_root,
           "fault path restores kernel mappings before returning");
    expect(phase2_el0_cleanup(&f.space), "faulted child resources are reclaimed");
}

static void test_faults_and_owner_integrity()
{
    run_fault_case(31, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
                   UINT64_C(0x15) << 26, GXOS_AARCH64_EL0_FAULT_UNSUPPORTED_SVC,
                   "unsupported SVC is rejected and terminates the child");
    run_fault_case(32, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
                   UINT64_C(0x20) << 26, GXOS_AARCH64_EL0_FAULT_INSTRUCTION_ABORT,
                   "EL0 instruction abort becomes a child fault");
    run_fault_case(33, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
                   UINT64_C(0x24) << 26, GXOS_AARCH64_EL0_FAULT_DATA_ABORT,
                   "EL0 data abort becomes a child fault");
    run_fault_case(34, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
                   UINT64_C(0x00) << 26, GXOS_AARCH64_EL0_FAULT_UNEXPECTED_SYNC,
                   "unexpected lower-EL synchronous exception becomes a child fault");
    run_fault_case(35, GXOS_AARCH64_EL0_VECTOR_LOWER_IRQ_A64,
                   0, GXOS_AARCH64_EL0_FAULT_UNEXPECTED_INTERRUPT,
                   "unexpected lower-EL IRQ is contained as a child fault");

    reset_fake_memory();
    Fixture f{};
    expect(make_fixture(&f, 36), "prepare exit ownership fixture");
    expect(phase2_el0_begin_context(&f.space, &f.context, f.owner),
           "exit ownership fixture enters active state");
    uint64_t frame[31] = {};
    frame[0] = 42;
    frame[8] = GXOS_AARCH64_EL0_EXIT_SERVICE;
    gxos_aarch64_el0_context* returned = nullptr;
    f.context.owner_id++;
    expect(!phase2_el0_dispatch_exception(frame, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
           UINT64_C(0x15) << 26, f.context.entry_pc + 20, 0x3c0, 0, &returned),
           "exit from a context with mismatched child ownership is rejected");
    f.context.owner_id = f.owner;
    expect(phase2_el0_dispatch_exception(frame, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
           UINT64_C(0x15) << 26, f.context.entry_pc + 20, 0x3c0, 0, &returned),
           "restored owner can deliver the real SVC result");
    expect(f.context.exit_value == 42 && f.context.state == GXOS_AARCH64_EL0_CONTEXT_EXITED,
           "correct owner retains the actual child exit result");
    expect(phase2_el0_cleanup(&f.space), "owned child cleanup succeeds after exit");
}

static void test_invalid_descriptors_and_repeated_cleanup()
{
    reset_fake_memory();
    Fixture malformed{};
    expect(phase2_el0_address_space_create(&malformed.space, 41),
           "create address space for malformed descriptor control");
    uint64_t* root = table_at(malformed.space.root);
    root[128] = (kPhysicalBase + 0x7000) | 1; // Block descriptor at an L0 table slot.
    uint64_t pa = 0;
    expect(!phase2_el0_allocate_page(&malformed.space, 41, GXOS_AARCH64_EL0_USER_BASE,
           GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_EXECUTE, &pa),
           "invalid block descriptor is rejected where a table is required");
    root[128] = (kPhysicalBase + 0x7000) | 3; // Table-shaped, but not owned by this child.
    expect(!phase2_el0_allocate_page(&malformed.space, 41, GXOS_AARCH64_EL0_USER_BASE,
           GXOS_AARCH64_EL0_PAGE_READ | GXOS_AARCH64_EL0_PAGE_EXECUTE, &pa),
           "unowned page-table descriptor is rejected without dereferencing it");
    root[128] = 0;
    expect(phase2_el0_cleanup(&malformed.space),
           "malformed-tree address-space cleanup remains bounded");

    reset_fake_memory();
    Fixture corrupted{};
    expect(make_fixture(&corrupted, 42), "create mapping for invalid leaf descriptor test");
    uint64_t* root_table = table_at(corrupted.space.root);
    uint64_t* l1 = table_at(root_table[128] & UINT64_C(0x000000fffffff000));
    uint64_t* l2 = table_at(l1[0] & UINT64_C(0x000000fffffff000));
    uint64_t* l3 = table_at(l2[0] & UINT64_C(0x000000fffffff000));
    l3[0] ^= UINT64_C(1) << 53; // Corrupt the EL1 execute-never code descriptor.
    expect(!phase2_el0_query_descriptor(&corrupted.space,
           GXOS_AARCH64_EL0_USER_BASE, &pa),
           "descriptor mismatch is rejected by the page-table walker");
    expect(phase2_el0_cleanup(&corrupted.space),
           "corrupted child page tables remain owned and cleanable");

    reset_fake_memory();
    gxos_aarch64_el0_address_space bounded{};
    expect(phase2_el0_address_space_create(&bounded, 43),
           "create address space for resource ceiling control");
    bool all_pages_allocated = true;
    for (uint32_t i = 0; i < 16; ++i) {
        uint64_t physical = 0;
        if (!phase2_el0_allocate_page(&bounded, 43,
                GXOS_AARCH64_EL0_USER_BASE + static_cast<uint64_t>(i) * 4096,
                GXOS_AARCH64_EL0_PAGE_READ, &physical)) all_pages_allocated = false;
    }
    uint64_t seventeenth = 0;
    expect(all_pages_allocated && bounded.page_count == 16 && bounded.mapping_count == 16 &&
           !phase2_el0_allocate_page(&bounded, 43,
               GXOS_AARCH64_EL0_USER_BASE + UINT64_C(0x10000),
               GXOS_AARCH64_EL0_PAGE_READ, &seventeenth),
           "bounded child page and mapping limits fail closed");
    expect(phase2_el0_cleanup(&bounded), "resource ceiling fixture releases all owned pages");

    reset_fake_memory();
    const uint64_t initial_free = kernel::memory::free_pages();
    for (uint32_t iteration = 0; iteration < 24; ++iteration) {
        Fixture f{};
        expect(make_fixture(&f, 100 + iteration), "repeated create and map cycle succeeds");
        expect(phase2_el0_cleanup(&f.space), "repeated cleanup cycle succeeds");
        expect(kernel::memory::free_pages() == initial_free,
               "repeated child cleanup returns every allocated page");
    }
}

static void test_kernel_and_mmio_negative_probes()
{
    reset_fake_memory();
    const uint64_t initial_free = kernel::memory::free_pages();
    uint64_t esr = 0;
    uint64_t far = 0;
    expect(phase2_el0_run_negative_probe(GXOS_AARCH64_EL0_PROBE_KERNEL_DATA,
                                         0, &esr, &far),
           "kernel-data probe observes the simulated lower-EL permission fault");
    expect(far == UINT64_C(0x00300000),
           "kernel-data negative probe records its protected address");
    expect(((esr >> 26) & UINT64_C(0x3f)) == UINT64_C(0x24) &&
           (esr & UINT64_C(0x3f)) == UINT64_C(0x0f),
           "kernel-data probe requires a level-3 data permission fault");
    expect(phase2_mmu_current_root() == phase2_mmu_root() &&
           kernel::memory::free_pages() == initial_free,
           "kernel-data probe restores the kernel root and releases every page");

    reset_fake_memory();
    const uint64_t mmio_free = kernel::memory::free_pages();
    esr = 0;
    far = 0;
    expect(phase2_el0_run_negative_probe(GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO,
                                         UINT64_C(0x08000000), &esr, &far),
           "GIC-distributor probe observes the simulated lower-EL permission fault");
    expect(far == UINT64_C(0x08000000),
           "MMIO negative probe records the protected device address");
    expect(((esr >> 26) & UINT64_C(0x3f)) == UINT64_C(0x24) &&
           (esr & UINT64_C(0x3f)) == UINT64_C(0x0f),
           "MMIO probe requires a level-3 data permission fault");
    expect(phase2_mmu_current_root() == phase2_mmu_root() &&
           kernel::memory::free_pages() == mmio_free,
           "MMIO probe restores the kernel root and releases every page");
}

} // namespace

extern "C" void* phase2_el0_host_physical_pointer(uint64_t physical_address)
{
    if (physical_address < kPhysicalBase || (physical_address & 0xfff) != 0) return nullptr;
    const uint64_t offset = physical_address - kPhysicalBase;
    if (offset >= static_cast<uint64_t>(kArenaPages) * 4096) return nullptr;
    return g_physical_pages[offset / 4096];
}

namespace kernel {
namespace memory {

bool allocate_pages(uint64_t pages, uint64_t* base)
{
    if (!base || pages == 0 || pages > kArenaPages) return false;
    for (uint32_t i = 2; i + pages <= kArenaPages; ++i) {
        bool available = true;
        for (uint64_t j = 0; j < pages; ++j) if (g_page_allocated[i + j]) available = false;
        if (!available) continue;
        for (uint64_t j = 0; j < pages; ++j) g_page_allocated[i + j] = true;
        *base = kPhysicalBase + static_cast<uint64_t>(i) * 4096;
        return true;
    }
    return false;
}

bool allocate_pages_at(uint64_t, uint64_t) { return false; }
bool release_pages(uint64_t base, uint64_t pages)
{
    if (base < kPhysicalBase || (base & 0xfff) || pages == 0) return false;
    const uint64_t first = (base - kPhysicalBase) / 4096;
    if (first + pages > kArenaPages) return false;
    for (uint64_t i = 0; i < pages; ++i) {
        if (!g_page_allocated[first + i] || first + i < 2) return false;
    }
    for (uint64_t i = 0; i < pages; ++i) g_page_allocated[first + i] = false;
    return true;
}
bool is_reserved(uint64_t, uint64_t) { return false; }
bool ready() { return true; }
uint64_t total_pages() { return kArenaPages - 2; }
uint64_t free_pages()
{
    uint64_t result = 0;
    for (uint32_t i = 2; i < kArenaPages; ++i) if (!g_page_allocated[i]) ++result;
    return result;
}
uint64_t allocated_pages() { return total_pages() - free_pages(); }
uint64_t allocation_count() { return allocated_pages(); }

} // namespace memory
} // namespace kernel

uint64_t phase2_mmu_root() { return kPhysicalBase; }
uint64_t phase2_mmu_current_root() { return g_current_root; }
uint64_t phase2_mmu_descriptor_for(uint64_t virtual_address)
{
    const uint64_t physical = virtual_address & UINT64_C(0x000000fffffff000);
    const uint64_t attribute = virtual_address == UINT64_C(0x08000000) ? 0 : 1;
    return physical | UINT64_C(3) | (attribute << 2) | (UINT64_C(3) << 8) |
           (UINT64_C(1) << 10) | (UINT64_C(1) << 53) | (UINT64_C(1) << 54);
}
uint8_t phase2_mmu_activate_root(uint64_t root)
{
    if ((root & 0xfff) != 0 || !phase2_el0_host_physical_pointer(root)) return 0;
    g_current_root = root;
    return 1;
}
extern "C" void phase3_serial_print(const char*) {}
extern "C" void phase3_serial_hex(uint64_t) {}
extern "C" uint64_t phase3_el0_enter(gxos_aarch64_el0_context*) { return 0; }

int main()
{
    reset_fake_memory();
    test_permissions_and_ownership();
    test_context_and_svc_exit();
    test_faults_and_owner_integrity();
    test_invalid_descriptors_and_repeated_cleanup();
    test_kernel_and_mmio_negative_probes();
    if (g_failures != 0) {
        printf("AARCH64_EL0_HOST_FAIL assertions=%llu failures=%llu\n",
               static_cast<unsigned long long>(g_assertions),
               static_cast<unsigned long long>(g_failures));
        return 1;
    }
    printf("AARCH64_EL0_HOST_PASS assertions=%llu scope=structural-simulation\n",
           static_cast<unsigned long long>(g_assertions));
    return 0;
}

// Host-side MMU register stubs are structural models only.
