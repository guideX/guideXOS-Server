#include <stdint.h>
#include <stdio.h>

#include "kernel/arch/arm64/phase2_el0.h"
#include "kernel/arch/arm64/phase2_mmu.h"
#include "kernel/arch/arm64/phase3_timer.h"
#include "kernel/core/include/kernel/common_physical_allocator.h"
#include "kernel/core/include/kernel/common_scheduler.h"

extern "C" void* phase2_el0_host_physical_pointer(uint64_t physical_address);

namespace {

static const uint64_t kPhysicalBase = UINT64_C(0x00100000);
static const uint32_t kArenaPages = 128;
alignas(4096) static uint8_t g_physical_pages[kArenaPages][4096];
static bool g_page_allocated[kArenaPages];
static uint64_t g_current_root = kPhysicalBase;
static uint64_t g_assertions = 0;
static uint64_t g_failures = 0;
static uint64_t g_host_counter = 0;
static uint64_t g_timer_start_count = 0;
static bool g_host_irq_enabled = false;
alignas(4096) static uint8_t g_scheduler_stacks[16][0x2000];
static bool g_scheduler_stack_used[16];
static uint32_t g_scheduler_context_count = 0;
static uint64_t g_scheduler_switch_count = 0;
static uint64_t g_interrupt_frame[40];

struct SchedulerMockContext {
    uint64_t stack_base;
    uint64_t stack_size;
    kernel::arch::thread_entry_t entry;
    void* argument;
};
static SchedulerMockContext g_scheduler_contexts[32];
static uint64_t g_context_switch_marker = 0;

static void kernel_owner_entry(void*) {}

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
    gxos_aarch64_el0_negative_probe_result kernel_result{};
    expect(phase2_el0_run_negative_probe(GXOS_AARCH64_EL0_PROBE_KERNEL_DATA,
                                         0, &kernel_result),
           "kernel-data probe observes the simulated lower-EL permission fault");
    expect(kernel_result.far == UINT64_C(0x00300000),
           "kernel-data negative probe records its protected address");
    expect(((kernel_result.esr >> 26) & UINT64_C(0x3f)) == UINT64_C(0x24) &&
           (kernel_result.esr & UINT64_C(0x7f)) == UINT64_C(0x0f) &&
           kernel_result.elr == GXOS_AARCH64_EL0_USER_BASE + 16 &&
           kernel_result.spsr == UINT64_C(0x3c0),
           "kernel-data probe requires an EL0 read permission fault at the L3 leaf");
    expect(kernel_result.context_state == GXOS_AARCH64_EL0_CONTEXT_FAULTED &&
           kernel_result.fault_reason == GXOS_AARCH64_EL0_FAULT_DATA_ABORT &&
           kernel_result.child_identity != 0 && kernel_result.owner_id != 0 &&
           !kernel_result.target_access_succeeded,
           "kernel-data fault is attributed to the actual child and terminal fault state");
    expect(((kernel_result.kernel_descriptor >> 6) & UINT64_C(3)) == 2 &&
           ((kernel_result.kernel_descriptor >> 2) & UINT64_C(7)) == 1 &&
           (kernel_result.kernel_descriptor & (UINT64_C(1) << 10)) != 0 &&
           (kernel_result.kernel_descriptor & (UINT64_C(1) << 54)) != 0,
           "kernel-data target is present, accessible EL1-only normal read-only memory");
    expect(kernel_result.kernel_root_restored && kernel_result.cleanup_succeeded &&
           kernel_result.data_pages_before_cleanup == 2 &&
           kernel_result.table_pages_before_cleanup > 0 &&
           phase2_mmu_current_root() == phase2_mmu_root() &&
           kernel::memory::free_pages() == initial_free,
           "kernel-data probe restores the kernel root and releases every page");

    reset_fake_memory();
    const uint64_t mmio_free = kernel::memory::free_pages();
    gxos_aarch64_el0_negative_probe_result mmio_result{};
    expect(phase2_el0_run_negative_probe(GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO,
                                         UINT64_C(0x08000000), &mmio_result),
           "GIC-distributor probe observes the simulated lower-EL permission fault");
    expect(mmio_result.far == UINT64_C(0x08000000),
           "MMIO negative probe records the protected device address");
    expect(((mmio_result.esr >> 26) & UINT64_C(0x3f)) == UINT64_C(0x24) &&
           (mmio_result.esr & UINT64_C(0x7f)) == UINT64_C(0x0f) &&
           mmio_result.elr == GXOS_AARCH64_EL0_USER_BASE + 16 &&
           mmio_result.spsr == UINT64_C(0x3c0),
           "MMIO probe requires an EL0 read permission fault at the L3 leaf");
    expect(((mmio_result.kernel_descriptor >> 6) & UINT64_C(3)) == 0 &&
           ((mmio_result.kernel_descriptor >> 2) & UINT64_C(7)) == 0 &&
           (mmio_result.kernel_descriptor & (UINT64_C(1) << 10)) != 0 &&
           (mmio_result.kernel_descriptor & (UINT64_C(1) << 54)) != 0,
           "MMIO target is present, accessible EL1-only device memory");
    expect(mmio_result.kernel_root_restored && mmio_result.cleanup_succeeded &&
           !mmio_result.target_access_succeeded &&
           phase2_mmu_current_root() == phase2_mmu_root() &&
           kernel::memory::free_pages() == mmio_free,
           "MMIO probe restores the kernel root and releases every page");
}

static void allocate_scheduler_pages(uint64_t pages, uint64_t* base)
{
    if (!base || pages != 2) { if (base) *base = 0; return; }
    for (uint32_t i = 0; i < 16; ++i) {
        if (g_scheduler_stack_used[i]) continue;
        g_scheduler_stack_used[i] = true;
        for (uint32_t j = 0; j < sizeof(g_scheduler_stacks[i]); ++j)
            g_scheduler_stacks[i][j] = 0;
        *base = reinterpret_cast<uint64_t>(g_scheduler_stacks[i]);
        return;
    }
    *base = 0;
}

static bool release_scheduler_pages(uint64_t base, uint64_t pages)
{
    if (pages != 2) return false;
    for (uint32_t i = 0; i < 16; ++i) {
        if (reinterpret_cast<uint64_t>(g_scheduler_stacks[i]) != base ||
            !g_scheduler_stack_used[i]) continue;
        g_scheduler_stack_used[i] = false;
        return true;
    }
    return false;
}

static void* find_execution_task(uint64_t execution_id)
{
    for (uint32_t i = 0; i < 16; ++i) {
        kernel::scheduler::Task* task = kernel::scheduler::task_at(i);
        if (task && task->execution_id == execution_id) return task;
    }
    return nullptr;
}

static void test_scheduled_el0_runtime_host_simulation()
{
    reset_fake_memory();
    for (uint32_t i = 0; i < 16; ++i) g_scheduler_stack_used[i] = false;
    g_scheduler_context_count = 0;
    g_scheduler_switch_count = 0;
    g_host_counter = 0;
    g_timer_start_count = 0;
    kernel::scheduler::Config config{
        allocate_scheduler_pages, 2, 1, UINT64_C(1000000), release_scheduler_pages
    };
    expect(kernel::scheduler::initialize(config),
           "common scheduler accepts the hosted EL0 runtime fixture configuration");
    kernel::scheduler::Task* owner = kernel::scheduler::create_task(
        501, "hosted-owner", kernel_owner_entry, nullptr, true);
    expect(owner != nullptr, "create a kernel task as child launch owner");
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_SCHEDULED);
    expect(kernel::scheduler::prepare_interrupt_contexts(),
           "prepare the owner's common scheduler exception context");
    kernel::scheduler::start();
    expect(kernel::scheduler::current_task() == owner,
           "common scheduler owns the current launch task");
    expect(phase2_el0_task_runtime_ready(),
           "EL0 runtime reports its timer and scheduler dependencies ready");

    uint64_t finite_id = 0;
    uint64_t loop_id = 0;
    uint64_t cancel_before_id = 0;
    uint64_t cancel_running_id = 0;
    uint64_t fault_id = 0;
    expect(!phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_FINITE_PREEMPTION, 502, 10000, &finite_id),
           "reject child creation from an unowned launch task");
    expect(!phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_FINITE_PREEMPTION, owner->id, 999, &finite_id),
           "reject a child budget below the configured minimum");
    const uint8_t simd_payload[] = { 0x00, 0x28, 0x20, 0x1e }; // fadd s0, s0, s0
    expect(!phase2_el0_task_create(simd_payload, sizeof(simd_payload), owner->id,
                                   10000, &finite_id),
           "reject SIMD payloads until the child context owns FP/SIMD state");
    expect(phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_FINITE_PREEMPTION, owner->id, 10000, &finite_id) &&
           phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_NONCOOPERATIVE, owner->id, 1000, &loop_id) &&
           phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_CANCELLATION, owner->id, 5000, &cancel_before_id) &&
           phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_CANCELLATION, owner->id, 5000, &cancel_running_id) &&
           phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_PROTECTED_MEMORY_FAULT, owner->id, 5000, &fault_id),
           "create five trusted EL0 child contexts without making them runnable");

    kernel::scheduler::Task* finite = static_cast<kernel::scheduler::Task*>(
        find_execution_task(finite_id));
    kernel::scheduler::Task* loop = static_cast<kernel::scheduler::Task*>(
        find_execution_task(loop_id));
    kernel::scheduler::Task* cancel_before = static_cast<kernel::scheduler::Task*>(
        find_execution_task(cancel_before_id));
    kernel::scheduler::Task* cancel_running = static_cast<kernel::scheduler::Task*>(
        find_execution_task(cancel_running_id));
    kernel::scheduler::Task* fault = static_cast<kernel::scheduler::Task*>(
        find_execution_task(fault_id));
    expect(finite && loop && cancel_before && cancel_running && fault &&
           finite->execution_address_space != loop->execution_address_space &&
           finite->execution_context != loop->execution_context &&
           finite->owner_id == owner->id && loop->owner_id == owner->id,
           "each child owns a distinct context and address space under its parent identity");

    gxos_aarch64_el0_task_result result{};
    expect(!phase2_el0_task_start(UINT64_C(0xdead), owner->id),
           "reject start for a stale execution identity");
    expect(phase2_el0_task_query(finite_id, owner->id, &result) &&
           result.state == kernel::scheduler::EXECUTION_CREATED &&
           result.result_kind == kernel::scheduler::EXECUTION_RESULT_NONE,
           "created children remain non-runnable with no completion value");

    expect(phase2_el0_task_start(finite_id, owner->id),
           "make a fully validated child runnable through the common scheduler");
    g_host_counter = 10;
    expect(kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter) != nullptr &&
           kernel::scheduler::current_task() == finite &&
           finite->execution_state == kernel::scheduler::EXECUTION_READY,
           "host-simulated timer selection enters a ready EL0 task's kernel trampoline");
    gxos_aarch64_el0_context* finite_context =
        static_cast<gxos_aarch64_el0_context*>(finite->execution_context);
    gxos_aarch64_el0_address_space* finite_space =
        static_cast<gxos_aarch64_el0_address_space*>(finite->execution_address_space);
    expect(phase2_el0_begin_context(finite_space, finite_context, finite_context->owner_id),
           "validated child activation binds EL0 state to the selected scheduler task");

    bool repeated_preemptions = true;
    // The architectural counter is 64-bit and wraps. A short resident interval
    // that crosses UINT64_MAX must still be charged as modular elapsed ticks.
    g_host_counter = UINT64_MAX - UINT64_C(49);
    expect(kernel::scheduler::mark_el0_running(finite, g_host_counter),
           "record the initial EL0 entry immediately before counter wrap");
    uint64_t child_registers[31]{};
    child_registers[19] = UINT64_C(0x19191919);
    repeated_preemptions = phase2_el0_suspend_for_schedule(
        child_registers, finite_context->entry_pc, 0x340, finite_context->sp_el0) &&
        kernel::scheduler::timer_interrupt_at(g_interrupt_frame, UINT64_C(50)) != nullptr &&
        kernel::scheduler::current_task() == owner && finite->execution_consumed == 100;
    g_host_counter = 200;
    void* resumed = kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter);
    repeated_preemptions = repeated_preemptions &&
        kernel::scheduler::current_task() == finite && resumed &&
        (reinterpret_cast<uintptr_t>(resumed) & 1) != 0 &&
        phase2_el0_resume_for_schedule(finite_space, finite_context, finite_context->owner_id) &&
        kernel::scheduler::mark_el0_resumed(finite, g_host_counter) &&
        phase2_mmu_current_root() == finite_space->root;
    expect(repeated_preemptions && phase2_mmu_current_root() == finite_space->root,
           "host-simulated preemption restores the matching child root and GPR context");

    child_registers[19] = UINT64_C(0x20202020);
    repeated_preemptions = repeated_preemptions &&
        phase2_el0_suspend_for_schedule(child_registers, finite_context->entry_pc + 4,
                                        0x340, finite_context->sp_el0) &&
        kernel::scheduler::timer_interrupt_at(g_interrupt_frame, 500) != nullptr &&
        kernel::scheduler::current_task() == owner && finite->execution_consumed == 400;
    g_host_counter = 600;
    resumed = kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter);
    repeated_preemptions = repeated_preemptions && resumed &&
        (reinterpret_cast<uintptr_t>(resumed) & 1) != 0 &&
        phase2_el0_resume_for_schedule(finite_space, finite_context, finite_context->owner_id) &&
        kernel::scheduler::mark_el0_resumed(finite, g_host_counter);
    expect(repeated_preemptions && finite->execution_state == kernel::scheduler::EXECUTION_RUNNING,
           "a second independently saved EL0 frame resumes without losing ownership");

    uint64_t svc_frame[31]{};
    svc_frame[0] = 42;
    svc_frame[8] = GXOS_AARCH64_EL0_EXIT_SERVICE;
    gxos_aarch64_el0_context* returned_context = nullptr;
    expect(phase2_el0_dispatch_exception(
               svc_frame, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
               UINT64_C(0x15) << 26, finite_context->entry_pc + 24,
               0x340, 0, &returned_context) && returned_context == finite_context &&
           kernel::scheduler::mark_el0_returning(finite, 700) &&
           phase2_el0_task_finish_exception(finite),
           "normal SVC completion keeps the child's actual return value and cleans its space");
    g_host_counter = 700;
    expect(kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter) != nullptr &&
           kernel::scheduler::current_task() == owner,
           "finished child returns control through the normal scheduler timer path");
    expect(phase2_el0_task_query(finite_id, owner->id, &result) &&
           result.state == kernel::scheduler::EXECUTION_COMPLETED &&
           result.result_kind == kernel::scheduler::EXECUTION_RESULT_EXIT &&
           result.result_value == 42 && result.resources_cleaned &&
           result.scheduled_counter_ticks == 500 && result.preemption_count == 2 &&
           result.el0_timer_interrupt_count == 2 && result.el0_entry_count == 1 &&
           result.el0_resume_count == 2 && result.address_space_id != 0 &&
           result.user_page_count == 2 && result.kernel_root_restored,
           "completion preserves exit 42 and reports wrapped ticks, child preemptions, resumes, and root restoration");
    expect(!phase2_el0_task_cancel(finite_id, owner->id) &&
           !kernel::scheduler::complete_el0_task(
               finite, kernel::scheduler::EXECUTION_COMPLETED,
               kernel::scheduler::EXECUTION_RESULT_EXIT, 43),
           "cancellation after exit and duplicate completion cannot replace the real result");
    expect(phase2_el0_task_reap(finite_id, owner->id) &&
           !kernel::scheduler::request_el0_cancel(finite),
           "reaping releases the inactive kernel stack and rejects the stale task pointer");

    expect(phase2_el0_task_cancel(cancel_before_id, owner->id) &&
           phase2_el0_task_query(cancel_before_id, owner->id, &result) &&
           result.state == kernel::scheduler::EXECUTION_CANCELLED &&
           result.result_kind == kernel::scheduler::EXECUTION_RESULT_CANCEL &&
           result.result_value == 0 && result.resources_cleaned,
           "cancellation before launch is terminal only after address-space cleanup");
    expect(phase2_el0_task_reap(cancel_before_id, owner->id),
           "cancelled unlaunched task can be reaped once");

    expect(phase2_el0_task_start(loop_id, owner->id),
           "start the non-cooperative trusted loop through scheduler ownership");
    g_host_counter = 800;
    expect(kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter) != nullptr &&
           kernel::scheduler::current_task() == loop,
           "scheduler dispatches the non-cooperative child fixture");
    gxos_aarch64_el0_context* loop_context =
        static_cast<gxos_aarch64_el0_context*>(loop->execution_context);
    gxos_aarch64_el0_address_space* loop_space =
        static_cast<gxos_aarch64_el0_address_space*>(loop->execution_address_space);
    expect(phase2_el0_begin_context(loop_space, loop_context, loop_context->owner_id) &&
           kernel::scheduler::mark_el0_running(loop, g_host_counter) &&
           phase2_el0_suspend_for_schedule(child_registers, loop_context->entry_pc,
                                           0x340, loop_context->sp_el0) &&
           kernel::scheduler::timer_interrupt_at(g_interrupt_frame, 1801) != nullptr &&
           kernel::scheduler::current_task() == owner &&
           loop->execution_state == kernel::scheduler::EXECUTION_TIMED_OUT &&
           phase2_el0_task_finish_preempted(loop),
           "a non-cooperative loop is stopped at the child budget on a simulated timer IRQ");
    expect(phase2_el0_task_query(loop_id, owner->id, &result) &&
           result.state == kernel::scheduler::EXECUTION_TIMED_OUT &&
           result.result_kind == kernel::scheduler::EXECUTION_RESULT_TIMEOUT &&
           result.result_value == 0 && result.resources_cleaned,
           "timeout is distinct from exit and never fabricates an exit value");
    expect(!phase2_el0_task_cancel(loop_id, owner->id) &&
           phase2_el0_task_reap(loop_id, owner->id),
           "timeout wins over later cancellation and remains reapable");

    expect(phase2_el0_task_start(cancel_running_id, owner->id),
           "start the separate cancellation target");
    g_host_counter = 1900;
    expect(kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter) != nullptr &&
           kernel::scheduler::current_task() == cancel_running,
           "cancellation target becomes scheduler owned");
    gxos_aarch64_el0_context* cancel_context =
        static_cast<gxos_aarch64_el0_context*>(cancel_running->execution_context);
    gxos_aarch64_el0_address_space* cancel_space =
        static_cast<gxos_aarch64_el0_address_space*>(cancel_running->execution_address_space);
    expect(phase2_el0_begin_context(cancel_space, cancel_context, cancel_context->owner_id) &&
           kernel::scheduler::mark_el0_running(cancel_running, g_host_counter) &&
           phase2_el0_suspend_for_schedule(child_registers, cancel_context->entry_pc,
                                           0x340, cancel_context->sp_el0) &&
           kernel::scheduler::timer_interrupt_at(g_interrupt_frame, 2000) != nullptr &&
           kernel::scheduler::current_task() == owner &&
           phase2_el0_task_cancel(cancel_running_id, owner->id),
           "external cancellation terminates a preempted non-cooperative child");
    expect(phase2_el0_task_query(cancel_running_id, owner->id, &result) &&
           result.state == kernel::scheduler::EXECUTION_CANCELLED &&
           result.result_kind == kernel::scheduler::EXECUTION_RESULT_CANCEL &&
           result.result_value == 0 && result.resources_cleaned &&
           phase2_el0_task_reap(cancel_running_id, owner->id),
           "preempted cancellation records no synthetic child return and reaps cleanly");

    expect(phase2_el0_task_start(fault_id, owner->id), "start the protected-memory fault fixture");
    g_host_counter = 2100;
    expect(kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter) != nullptr &&
           kernel::scheduler::current_task() == fault,
           "fault fixture enters through the common scheduler");
    gxos_aarch64_el0_context* fault_context =
        static_cast<gxos_aarch64_el0_context*>(fault->execution_context);
    gxos_aarch64_el0_address_space* fault_space =
        static_cast<gxos_aarch64_el0_address_space*>(fault->execution_address_space);
    expect(phase2_el0_begin_context(fault_space, fault_context, fault_context->owner_id) &&
           kernel::scheduler::mark_el0_running(fault, g_host_counter),
           "fault fixture owns an active child root before its simulated access");
    gxos_aarch64_el0_context* fault_return = nullptr;
    expect(phase2_el0_dispatch_exception(
               child_registers, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
               (UINT64_C(0x24) << 26) | UINT64_C(0x0f),
               fault_context->entry_pc + 4, 0x340, UINT64_C(0x40000000), &fault_return) &&
           kernel::scheduler::mark_el0_returning(fault, 2150) &&
           fault_return == fault_context && phase2_el0_task_finish_exception(fault),
           "protected-memory exception becomes a fault result and restores the kernel root");
    g_host_counter = 2200;
    kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter);
    expect(kernel::scheduler::current_task() == owner &&
           phase2_el0_task_query(fault_id, owner->id, &result) &&
           result.state == kernel::scheduler::EXECUTION_FAULTED &&
           result.result_kind == kernel::scheduler::EXECUTION_RESULT_FAULT &&
           result.result_value == GXOS_AARCH64_EL0_FAULT_DATA_ABORT &&
           result.resources_cleaned && !phase2_el0_task_cancel(fault_id, owner->id) &&
           phase2_el0_task_reap(fault_id, owner->id),
           "fault wins over later cancellation and remains distinguishable after cleanup");

    uint64_t final_budget_id = 0;
    expect(phase2_el0_task_create_fixture(
               GXOS_AARCH64_EL0_FIXTURE_FINITE_PREEMPTION, owner->id, 1000,
               &final_budget_id) &&
           phase2_el0_task_start(final_budget_id, owner->id),
           "create and start a child with a finite final-return budget");
    kernel::scheduler::Task* final_budget = static_cast<kernel::scheduler::Task*>(
        find_execution_task(final_budget_id));
    g_host_counter = 2300;
    expect(final_budget &&
           kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter) != nullptr &&
           kernel::scheduler::current_task() == final_budget,
           "dispatch the final-return budget fixture");
    gxos_aarch64_el0_context* final_budget_context =
        static_cast<gxos_aarch64_el0_context*>(final_budget->execution_context);
    gxos_aarch64_el0_address_space* final_budget_space =
        static_cast<gxos_aarch64_el0_address_space*>(final_budget->execution_address_space);
    uint64_t final_budget_svc_frame[31]{};
    final_budget_svc_frame[0] = 42;
    final_budget_svc_frame[8] = GXOS_AARCH64_EL0_EXIT_SERVICE;
    gxos_aarch64_el0_context* final_budget_return = nullptr;
    g_host_counter = 2300;
    expect(phase2_el0_begin_context(final_budget_space, final_budget_context,
                                    final_budget_context->owner_id) &&
           kernel::scheduler::mark_el0_running(final_budget, g_host_counter) &&
           phase2_el0_dispatch_exception(
               final_budget_svc_frame, GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64,
               UINT64_C(0x15) << 26, final_budget_context->entry_pc + 24,
               0x340, 0, &final_budget_return) &&
           final_budget_return == final_budget_context,
           "capture an exit after the child budget without a preceding timer interrupt");
    g_host_counter = 3301;
    expect(kernel::scheduler::mark_el0_returning(final_budget, g_host_counter) &&
           phase2_el0_task_finish_exception(final_budget) &&
           final_budget->execution_state == kernel::scheduler::EXECUTION_TIMED_OUT &&
           final_budget->execution_result_kind == kernel::scheduler::EXECUTION_RESULT_TIMEOUT &&
           final_budget->execution_result == 0 && final_budget->resources_cleaned,
           "the final return-time counter closes the budget and suppresses a late exit value");
    g_host_counter = 3400;
    expect(kernel::scheduler::timer_interrupt_at(g_interrupt_frame, g_host_counter) != nullptr &&
           kernel::scheduler::current_task() == owner &&
           phase2_el0_task_query(final_budget_id, owner->id, &result) &&
           result.state == kernel::scheduler::EXECUTION_TIMED_OUT &&
           result.result_kind == kernel::scheduler::EXECUTION_RESULT_TIMEOUT &&
           result.result_value == 0 && result.resources_cleaned &&
           phase2_el0_task_reap(final_budget_id, owner->id),
           "reap the final-return timeout after scheduler handoff");

    expect(g_timer_start_count == 5,
           "each started execution arms the existing architectural timer path");
    printf("EL0 scheduled-task controls used hosted timer/exception simulation; no native proof.\n");
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
    const uint64_t ap = virtual_address == UINT64_C(0x00300000) ? (UINT64_C(2) << 6) : 0;
    return physical | UINT64_C(3) | (attribute << 2) | ap | (UINT64_C(3) << 8) |
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

void phase3_timer_start() { ++g_timer_start_count; }
uint32_t phase3_timer_irq() { return 30; }
uint64_t phase3_timer_frequency() { return UINT64_C(1000000); }
uint64_t phase3_timer_count() { return 0; }
uint64_t phase3_timer_counter() { return g_host_counter; }

namespace kernel {
namespace arch {

interrupt_state_t irq_save()
{
    const interrupt_state_t previous = g_host_irq_enabled ? 1 : 0;
    g_host_irq_enabled = false;
    return previous;
}
void irq_restore(interrupt_state_t state) { g_host_irq_enabled = state != 0; }
void irq_enable() { g_host_irq_enabled = true; }
void irq_disable() { g_host_irq_enabled = false; }
void idle() {}
void memory_barrier() {}
const char* interface_name() { return "host-simulated-arm64"; }

void* context_init(uint64_t stack_base, uint64_t stack_size,
                   thread_entry_t entry, void* argument)
{
    if (!entry || g_scheduler_context_count >= 32) return nullptr;
    SchedulerMockContext* context = &g_scheduler_contexts[g_scheduler_context_count++];
    context->stack_base = stack_base;
    context->stack_size = stack_size;
    context->entry = entry;
    context->argument = argument;
    return context;
}

void context_switch(void** old_context, void*)
{
    ++g_scheduler_switch_count;
    if (old_context) *old_context = &g_context_switch_marker;
}

bool interrupt_frame_save(void* frame, void* destination)
{
    if (!frame || !destination) return false;
    uint64_t* target = static_cast<uint64_t*>(destination);
    for (uint32_t i = 0; i < kInterruptContextWords; ++i) target[i] = 0;
    const uint64_t* source = static_cast<const uint64_t*>(frame);
    for (uint32_t i = 0; i < 31; ++i) target[i] = source[i];
    return true;
}

bool context_to_interrupt(void* context, void* destination)
{
    if (!context || !destination) return false;
    uint64_t* target = static_cast<uint64_t*>(destination);
    for (uint32_t i = 0; i < kInterruptContextWords; ++i) target[i] = 0;
    target[0] = reinterpret_cast<uint64_t>(context);
    return true;
}

} // namespace arch
} // namespace kernel

int main()
{
    reset_fake_memory();
    test_permissions_and_ownership();
    test_context_and_svc_exit();
    test_faults_and_owner_integrity();
    test_invalid_descriptors_and_repeated_cleanup();
    test_kernel_and_mmio_negative_probes();
    test_scheduled_el0_runtime_host_simulation();
    if (g_failures != 0) {
        printf("AARCH64_EL0_HOST_FAIL assertions=%llu failures=%llu\n",
               static_cast<unsigned long long>(g_assertions),
               static_cast<unsigned long long>(g_failures));
        return 1;
    }
    printf("AARCH64_EL0_HOST_PASS assertions=%llu scope=structural-and-hosted-scheduler-simulation\n",
           static_cast<unsigned long long>(g_assertions));
    return 0;
}

// Host-side MMU register stubs are structural models only.
