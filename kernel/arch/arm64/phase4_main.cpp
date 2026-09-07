#include <stdint.h>

#include "../../../aarch64/phase4/phase4_contract.h"
#include "../../../aarch64/phase2/phase2_platform.h"
#include "../../../aarch64/phase2/phase2_validation.h"
#include "../../../kernel/core/include/kernel/arch_interface.h"
#include "../../../kernel/core/include/kernel/boot_info.h"
#include "../../../kernel/core/include/kernel/common_kernel_entry.h"
#include "../../../kernel/core/include/kernel/common_physical_allocator.h"
#include "../../../kernel/core/include/kernel/common_scheduler.h"
#include "../../../kernel/core/include/kernel/irq_registry.h"
#include "../../../kernel/core/include/kernel/block_device.h"
#include "../../../kernel/core/include/kernel/fs_fat.h"
#include "../../../kernel/core/include/kernel/ramdisk.h"
#include "../../../kernel/core/include/kernel/vfs.h"
#include "phase2_mmu.h"
#include "phase3_timer.h"

extern "C" void phase3_serial_init();
extern "C" void phase3_serial_set_base(uint64_t base);
extern "C" void phase3_serial_print(const char* text);
extern "C" void phase3_serial_hex(uint64_t value);
extern "C" void phase3_serial_dec(uint64_t value);
extern "C" void phase4_serial_set_base(uint64_t base);
extern "C" uint8_t phase3_register_failure_active();
extern "C" void phase3_register_probe(uint64_t task_id, uint64_t register_base);
extern "C" void phase3_preemptive_worker(void* argument);
extern "C" uint8_t phase3_irq_controller_init(
    const gxos_aarch64_phase2_platform* platform, uint32_t timer_irq);
extern "C" uint32_t phase3_exception_count();
extern "C" uint8_t phase3_vectors[];
extern "C" void* gxos_kernel_heap_alloc_aligned(size_t, size_t);
extern "C" void gxos_kernel_heap_free_aligned(void*);
extern "C" size_t gxos_kernel_heap_used_bytes();

static const uint64_t kCooperativeTarget = UINT64_C(10000);
static const uint64_t kPreemptionTarget = UINT64_C(10000);
static const char kFixtureText[] = "guideXOS AARCH64 Phase 4 filesystem proof";
static const char kNestedText[] = "guideXOS AARCH64 Phase 4 nested filesystem proof";
static const char kScratchText[] = "guideXOS AARCH64 Phase 4 writable proof";

namespace {

struct Work {
    volatile uint64_t counter;
    uint64_t private_magic;
    uint64_t register_base;
    uint32_t id;
    uint32_t reserved;
};

struct FilesystemWork {
    volatile uint64_t reads;
    volatile uint64_t enumerations;
    volatile uint64_t failures;
};

static Work g_work[3] = {
    { 0, UINT64_C(0x47584f5350524956) ^ 1, UINT64_C(0xa100000000000000), 1, 0 },
    { 0, UINT64_C(0x47584f5350524956) ^ 2, UINT64_C(0xb200000000000000), 2, 0 },
    { 0, UINT64_C(0x47584f5350524956) ^ 3, UINT64_C(0xc300000000000000), 3, 0 }
};
static FilesystemWork g_fs_work{};
static volatile uint8_t g_first_task_entered = 0;
static volatile uint8_t g_register_failure = 0;
static volatile uint8_t g_fs_failure = 0;
static kernel::boot::CommonBootInfo g_boot_info{};

static void allocate_pages(uint64_t pages, uint64_t* base)
{
    if (!base || !kernel::memory::allocate_pages(pages, base)) {
        if (base) *base = 0;
    }
}

static void print(const char* value) { phase3_serial_print(value); }

static void fail(const char* reason)
{
    print("[guideXOS] ");
    print(reason);
    print("\n[guideXOS] AARCH64_PHASE4_ERROR\n");
    for (;;) __asm__ volatile("wfi");
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t* result)
{
    if (!result || b > UINT64_MAX - a) return false;
    *result = a + b;
    return *result > a;
}

static bool stack_is_owned(const gxos_aarch64_phase4_handoff* handoff, uint64_t sp)
{
    uint64_t end = 0;
    return handoff && handoff->stack_size != 0 &&
           add_u64(handoff->stack_base, handoff->stack_size, &end) &&
           handoff->stack_top == end && sp >= handoff->stack_base && sp <= end &&
           (sp & 0xf) == 0;
}

static uint64_t read_current_el()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    return (value >> 2) & 3;
}

static uint64_t read_sp()
{
    uint64_t value = 0;
    __asm__ volatile("mov %0, sp" : "=r"(value));
    return value;
}

static bool valid_handoff(const gxos_aarch64_phase4_handoff* handoff)
{
    if (!handoff || handoff->magic != GXOS_AARCH64_PHASE4_HANDOFF_MAGIC ||
        handoff->version != GXOS_AARCH64_PHASE4_HANDOFF_VERSION ||
        handoff->size != sizeof(*handoff)) return false;
    const uint32_t required = GXOS_AARCH64_PHASE4_FLAG_EBS_COMPLETE |
        GXOS_AARCH64_PHASE4_FLAG_IDENTITY_LOAD |
        GXOS_AARCH64_PHASE4_FLAG_MMU_OFF_ON_ENTRY |
        GXOS_AARCH64_PHASE4_FLAG_STACK_ALLOCATED |
        GXOS_AARCH64_PHASE4_FLAG_MEMORY_MAP_VALID |
        GXOS_AARCH64_PHASE4_FLAG_DTB_VALID |
        GXOS_AARCH64_PHASE4_FLAG_DTB_COPIED |
        GXOS_AARCH64_PHASE4_FLAG_RAMDISK_VALID;
    if ((handoff->flags & required) != required || handoff->kernel_base != GXOS_AARCH64_PHASE4_KERNEL_LOAD_ADDRESS ||
        handoff->kernel_size == 0 || handoff->stack_size == 0 || handoff->ramdisk_size == 0 ||
        !gxos_aarch64_memory_map_layout_valid(handoff->memory_map, handoff->memory_map_size,
                                               handoff->memory_map_descriptor_size,
                                               handoff->memory_map_entry_count) ||
        handoff->dtb_base == 0 || handoff->dtb_size == 0) return false;
    uint64_t end = 0;
    uint64_t kernel_end = 0;
    return add_u64(handoff->kernel_base, handoff->kernel_size, &kernel_end) &&
           handoff->kernel_entry >= handoff->kernel_base && handoff->kernel_entry < kernel_end &&
           add_u64(handoff->stack_base, handoff->stack_size, &end) && handoff->stack_top == end &&
           add_u64(handoff->memory_map, handoff->memory_map_size, &end) &&
           add_u64(handoff->dtb_base, handoff->dtb_size, &end) &&
           add_u64(handoff->ramdisk_base, handoff->ramdisk_size, &end);
}

static bool work_valid(const Work* work)
{
    return work && work->id >= 1 && work->id <= 3 &&
           work->private_magic == (UINT64_C(0x47584f5350524956) ^ work->id);
}

static bool all_work_progressed()
{
    for (uint32_t i = 0; i < 3; ++i) {
        if (!work_valid(&g_work[i]) || g_work[i].counter == 0) return false;
    }
    return true;
}

static void cooperative_task(void* argument)
{
    Work* work = static_cast<Work*>(argument);
    g_first_task_entered = 1;
    for (;;) {
        if (!work_valid(work)) {
            g_register_failure = 1;
            kernel::scheduler::mark_cooperative_complete();
            kernel::scheduler::yield();
            for (;;) kernel::arch::idle();
        }
        if (kernel::scheduler::cooperative_switches() >= kCooperativeTarget) {
            kernel::scheduler::mark_cooperative_complete();
            kernel::scheduler::yield();
            for (;;) kernel::arch::idle();
        }
        ++work->counter;
        kernel::scheduler::note_execution();
        phase3_register_probe(work->id, work->register_base);
        if (phase3_register_failure_active()) g_register_failure = 1;
        kernel::scheduler::yield();
    }
}

static bool text_matches(const uint8_t* bytes, int32_t length, const char* expected)
{
    if (!bytes || length < 0) return false;
    uint32_t expected_length = 0;
    while (expected[expected_length]) ++expected_length;
    if (static_cast<uint32_t>(length) != expected_length) return false;
    for (uint32_t i = 0; i < expected_length; ++i) if (bytes[i] != (uint8_t)expected[i]) return false;
    return true;
}

static bool name_equals(const char* left, const char* right)
{
    uint32_t i = 0;
    for (;;) {
        char a = left[i];
        char b = right[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
        if (!a) return true;
        ++i;
    }
}

static bool enumerate_phase4(bool* saw_hello, bool* saw_nested)
{
    if (saw_hello) *saw_hello = false;
    if (saw_nested) *saw_nested = false;
    const uint8_t iterator = kernel::vfs::opendir("/phase4");
    if (iterator == 0xff) return false;
    kernel::vfs::DirEntry entry{};
    uint32_t entries = 0;
    bool ok = true;
    while (kernel::vfs::readdir(iterator, &entry)) {
        ++entries;
        if (name_equals(entry.name, "HELLO.TXT") && entry.type == kernel::vfs::FILE_TYPE_REGULAR && saw_hello) *saw_hello = true;
        if (name_equals(entry.name, "NESTED") && entry.type == kernel::vfs::FILE_TYPE_DIRECTORY && saw_nested) *saw_nested = true;
        if (entries > 16) { ok = false; break; }
    }
    kernel::vfs::closedir(iterator);
    return ok && entries != 0;
}

static void filesystem_task(void*)
{
    uint8_t handle = kernel::vfs::open("/phase4/hello.txt", kernel::vfs::OPEN_READ);
    uint8_t buffer[128];
    if (handle == 0xff) { g_fs_failure = 1; for (;;) kernel::arch::idle(); }
    for (;;) {
        if (kernel::vfs::seek(handle, 0, kernel::vfs::SEEK_SET) != kernel::vfs::VFS_OK) {
            ++g_fs_work.failures; g_fs_failure = 1;
        }
        const int32_t bytes = kernel::vfs::read(handle, buffer, sizeof(buffer));
        const int32_t eof = kernel::vfs::read(handle, buffer, 1);
        if (!text_matches(buffer, bytes, kFixtureText) || eof != 0) {
            ++g_fs_work.failures; g_fs_failure = 1;
        }
        ++g_fs_work.reads;
        // Keep the read path hot on every slice while periodically exercising
        // the directory cursor as well.  This preserves a sustained mixed
        // VFS workload without making the bounded QEMU proof dominated by
        // repeated metadata scans.
        if ((g_fs_work.reads & 0xfu) == 0) {
            bool saw_hello = false;
            bool saw_nested = false;
            if (!enumerate_phase4(&saw_hello, &saw_nested) || !saw_hello || !saw_nested) {
                ++g_fs_work.failures; g_fs_failure = 1;
            }
            ++g_fs_work.enumerations;
        }
        kernel::scheduler::note_execution();
    }
}

static void completion_task(void*) { kernel::scheduler::return_to_bootstrap(); }

static void* timer_handler(uint32_t, void* frame, void*)
{
    phase3_timer_ack_and_rearm();
    return kernel::scheduler::timer_interrupt(frame);
}

static bool heap_stress()
{
    void* first = gxos_kernel_heap_alloc_aligned(37, 8);
    void* second = gxos_kernel_heap_alloc_aligned(4096, 16);
    void* third = gxos_kernel_heap_alloc_aligned(777, 64);
    if (!first || !second || !third || ((uintptr_t)second & 0xf) != 0 || ((uintptr_t)third & 0x3f) != 0) return false;
    uint8_t* a = static_cast<uint8_t*>(first);
    uint8_t* b = static_cast<uint8_t*>(second);
    uint8_t* c = static_cast<uint8_t*>(third);
    for (uint32_t i = 0; i < 37; ++i) a[i] = (uint8_t)(i ^ 0x5a);
    for (uint32_t i = 0; i < 4096; ++i) b[i] = (uint8_t)(i ^ 0xa5);
    for (uint32_t i = 0; i < 777; ++i) c[i] = (uint8_t)(i ^ 0x3c);
    for (uint32_t i = 0; i < 37; ++i) if (a[i] != (uint8_t)(i ^ 0x5a)) return false;
    for (uint32_t i = 0; i < 4096; ++i) if (b[i] != (uint8_t)(i ^ 0xa5)) return false;
    for (uint32_t i = 0; i < 777; ++i) if (c[i] != (uint8_t)(i ^ 0x3c)) return false;
    gxos_kernel_heap_free_aligned(second);
    gxos_kernel_heap_free_aligned(first);
    gxos_kernel_heap_free_aligned(third);
    for (uint32_t cycle = 0; cycle < 1000; ++cycle) {
        void* p = gxos_kernel_heap_alloc_aligned(32 + (cycle & 127), (cycle & 1) ? 16 : 8);
        if (!p) return false;
        static_cast<uint8_t*>(p)[0] = (uint8_t)cycle;
        gxos_kernel_heap_free_aligned(p);
    }
    return true;
}

static bool vfs_boot_proof()
{
    kernel::block::init();
    kernel::ramdisk::init();
    const uint8_t disk = kernel::ramdisk::create_at(
        (void*)(uintptr_t)g_boot_info.ramdisk_base, (size_t)g_boot_info.ramdisk_size, "bootramdisk");
    if (disk == 0xff) return false;
    kernel::fs_fat::init();
    kernel::vfs::init();
    const uint8_t mount = kernel::vfs::mount("/", disk);
    if (mount == 0xff || !kernel::vfs::get_mount("/")) return false;
    print("[guideXOS] ramdisk: mounted\n");
    print("[guideXOS] VFS root: OK\n");

    bool app_visible = false;
    const uint8_t root_iter = kernel::vfs::opendir("/");
    if (root_iter != 0xff) {
        kernel::vfs::DirEntry entry{};
        while (kernel::vfs::readdir(root_iter, &entry)) {
            if (name_equals(entry.name, "APPS") && entry.type == kernel::vfs::FILE_TYPE_DIRECTORY) app_visible = true;
        }
        kernel::vfs::closedir(root_iter);
    }
    if (app_visible) print("[guideXOS] application directory: visible\n");

    kernel::vfs::FileInfo info{};
    uint32_t fixture_size = 0;
    while (kFixtureText[fixture_size]) ++fixture_size;
    if (kernel::vfs::stat("/phase4/hello.txt", &info) != kernel::vfs::VFS_OK ||
        info.size != fixture_size) return false;
    uint8_t buffer[128]{};
    if (kernel::vfs::read_file("/phase4/hello.txt", buffer, sizeof(buffer)) != (int32_t)fixture_size ||
        !text_matches(buffer, (int32_t)fixture_size, kFixtureText)) return false;
    const uint8_t handle = kernel::vfs::open("/phase4/hello.txt", kernel::vfs::OPEN_READ);
    if (handle == 0xff || kernel::vfs::read(handle, buffer, sizeof(buffer)) != (int32_t)fixture_size ||
        kernel::vfs::read(handle, buffer, 1) != 0 || kernel::vfs::close(handle) != kernel::vfs::VFS_OK) return false;
    uint32_t nested_size = 0;
    while (kNestedText[nested_size]) ++nested_size;
    if (kernel::vfs::read_file("/phase4/nested/proof.txt", buffer, sizeof(buffer)) != (int32_t)nested_size ||
        !text_matches(buffer, (int32_t)nested_size, kNestedText)) return false;
    bool saw_hello = false;
    bool saw_nested = false;
    if (!enumerate_phase4(&saw_hello, &saw_nested) || !saw_hello || !saw_nested) return false;
    print("[guideXOS] VFS directory enumeration: PASS\n");
    print("[guideXOS] VFS file read: PASS\n");

    uint32_t scratch_size = 0;
    while (kScratchText[scratch_size]) ++scratch_size;
    if (kernel::vfs::write_file("/phase4/scratch.txt", kScratchText, scratch_size) != (int32_t)scratch_size ||
        kernel::vfs::read_file("/phase4/scratch.txt", buffer, sizeof(buffer)) != (int32_t)scratch_size ||
        !text_matches(buffer, (int32_t)scratch_size, kScratchText)) return false;
    print("[guideXOS] VFS write/read: PASS\n");
    return true;
}

} // namespace

extern "C" uint8_t phase3_register_failure_active()
{
    return g_register_failure;
}

extern "C" void phase3_register_failure(uint64_t task_id, uint64_t register_id,
                                         uint64_t expected, uint64_t observed)
{
    g_register_failure = 1;
    print("[guideXOS] register integrity: FAIL task=");
    phase3_serial_dec(task_id);
    print(" reg=x");
    phase3_serial_dec(register_id);
    print(" expected=");
    phase3_serial_hex(expected);
    print(" observed=");
    phase3_serial_hex(observed);
    print("\n");
}

extern "C" void phase3_preemptive_step(void* argument)
{
    Work* work = static_cast<Work*>(argument);
    if (!work_valid(work)) {
        phase3_register_failure(work ? work->id : 0, 0,
                                UINT64_C(0x47584f5350524956), 0);
        return;
    }
    ++work->counter;
    kernel::scheduler::note_execution();
}

extern "C" void* phase4_irq_dispatch(uint32_t irq, void* frame)
{
    return kernel::irq::dispatch(irq, frame);
}

extern "C" void phase3_main(const gxos_aarch64_phase4_handoff* handoff, uint64_t initial_el)
{
    phase3_serial_init();
    print("[guideXOS] AARCH64 kernel entry\n");
    if (read_current_el() != 1) fail("execution level: unsupported");
    print("[guideXOS] execution level: EL1\n");
    if (!stack_is_owned(handoff, read_sp())) fail("stack: FAIL");
    print("[guideXOS] stack: OK\n");
    if (!valid_handoff(handoff)) fail("firmware handoff: FAIL");
    print("[guideXOS] ExitBootServices: OK\n");
    print("[guideXOS] firmware handoff: OK\n");
    (void)initial_el;

    gxos_aarch64_phase2_platform platform{};
    if (!gxos_aarch64_phase2_parse_dtb((const void*)(uintptr_t)handoff->dtb_base,
                                       handoff->dtb_size, &platform) ||
        platform.timer_source != 2 || platform.gic_version != 2) fail("DTB: FAIL");
    print("[guideXOS] DTB: OK\n");
    phase3_serial_set_base(platform.uart_base);
    phase4_serial_set_base(platform.uart_base);
    phase3_serial_init();
    print("[guideXOS] PL011: active console validated\n");

    if (!phase2_mmu_build(&platform, handoff->kernel_base, handoff->kernel_size)) fail("MMU tables: FAIL");
    print("[guideXOS] MMU tables: built\n");
    phase2_mmu_enable();
    print("[guideXOS] MMU: guideXOS tables active\n");
    uint64_t vbar = 0;
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
    if (vbar != (uint64_t)(uintptr_t)phase3_vectors || (vbar & 0x7ff) != 0) fail("exception vectors: FAIL");
    print("[guideXOS] exception vectors: OK\n");

    g_boot_info.magic = kernel::boot::kCommonBootInfoMagic;
    g_boot_info.version = kernel::boot::kCommonBootInfoVersion;
    g_boot_info.size = (uint16_t)sizeof(g_boot_info);
    g_boot_info.architecture = kernel::boot::kArchitectureArm64;
    g_boot_info.kernel_base = handoff->kernel_base;
    g_boot_info.kernel_size = handoff->kernel_size;
    g_boot_info.bootstrap_stack_base = handoff->stack_base;
    g_boot_info.bootstrap_stack_size = handoff->stack_size;
    g_boot_info.memory_map = handoff->memory_map;
    g_boot_info.memory_map_size = handoff->memory_map_size;
    g_boot_info.memory_map_descriptor_size = handoff->memory_map_descriptor_size;
    g_boot_info.memory_map_entry_count = handoff->memory_map_entry_count;
    g_boot_info.handoff_base = (uint64_t)(uintptr_t)handoff;
    g_boot_info.handoff_size = handoff->size;
    g_boot_info.ramdisk_base = handoff->ramdisk_base;
    g_boot_info.ramdisk_size = handoff->ramdisk_size;
    g_boot_info.dtb_base = handoff->dtb_base;
    g_boot_info.dtb_size = handoff->dtb_size;
    g_boot_info.flags = kernel::boot::kBootFlagMemoryMap |
                        kernel::boot::kBootFlagRamdisk |
                        kernel::boot::kBootFlagDtb;
    g_boot_info.memory_range_count = platform.ram_count;
    for (uint32_t i = 0; i < platform.ram_count; ++i) {
        g_boot_info.memory_ranges[i] = { platform.ram[i].base, platform.ram[i].size };
    }
    g_boot_info.mmio_ranges[0] = { platform.uart_base, platform.uart_size };
    g_boot_info.mmio_ranges[1] = { platform.gicd_base, platform.gicd_size };
    g_boot_info.mmio_ranges[2] = { platform.gicc_base, platform.gicc_size };
    g_boot_info.mmio_range_count = 3;
    if (!kernel::boot::validate(&g_boot_info)) fail("common boot resources: FAIL");
    print("[guideXOS] common boot resources: OK\n");

    if (!kernel::memory::initialize(g_boot_info)) fail("common physical allocator: FAIL");
    print("[guideXOS] common physical allocator: PASS pages-total=");
    phase3_serial_dec(kernel::memory::total_pages());
    print(" pages-free=");
    phase3_serial_dec(kernel::memory::free_pages());
    print("\n");
    if (!heap_stress()) fail("kernel heap: FAIL");
    print("[guideXOS] kernel heap: PASS\n");

    if (!vfs_boot_proof()) fail("ramdisk/VFS proof: FAIL");
    if (!kernel::irq::initialize() ||
        !phase3_irq_controller_init(&platform, platform.timer_irq) ||
        !phase3_timer_configure(platform.timer_irq, 100) ||
        !kernel::irq::register_handler(platform.timer_irq, timer_handler, nullptr)) {
        fail("IRQ/timer setup: FAIL");
    }
    print("[guideXOS] GIC: OK version=2\n");

    kernel::common::KernelEntryConfig config = {
        allocate_pages, 16, kCooperativeTarget, kPreemptionTarget,
        kernel::arch::interface_name(), print
    };
    if (!kernel::common::kernel_entry_init(config)) fail("common kernel entry: FAIL");
    print("[guideXOS] scheduler: initialized\n");

    kernel::scheduler::Task* workers[3] = {};
    for (uint32_t i = 0; i < 3; ++i) {
        workers[i] = kernel::scheduler::create_task(i + 1, i == 0 ? "task-A" : (i == 1 ? "task-B" : "task-C"),
                                                    cooperative_task, &g_work[i], true);
        if (!workers[i]) fail("thread context: FAIL");
    }
    kernel::scheduler::Task* fs_task = kernel::scheduler::create_task(4, "vfs-worker", filesystem_task, nullptr, false);
    kernel::scheduler::Task* completion = kernel::scheduler::create_task(99, "scheduler-report", completion_task, nullptr, false);
    if (!fs_task || !completion || kernel::scheduler::task_count() != 5) fail("thread context: FAIL");
    print("[guideXOS] ARM64 thread context: OK\n");
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_COOPERATIVE);
    kernel::scheduler::start();
    if (!g_first_task_entered || g_register_failure || kernel::scheduler::failed() ||
        kernel::scheduler::cooperative_switches() < kCooperativeTarget || !all_work_progressed() ||
        !kernel::scheduler::stack_integrity()) fail("cooperative scheduling: FAIL");
    print("[guideXOS] first kernel task: entered\n");
    print("[guideXOS] cooperative scheduling: PASS switches=");
    phase3_serial_dec(kernel::scheduler::cooperative_switches());
    print("\n");

    for (uint32_t i = 0; i < 3; ++i) {
        if (!kernel::scheduler::reset_task(workers[i], phase3_preemptive_worker, &g_work[i], true)) fail("preemption setup: FAIL");
        g_work[i].counter = 0;
    }
    if (!kernel::scheduler::reset_task(fs_task, filesystem_task, nullptr, true) ||
        !kernel::scheduler::reset_task(completion, completion_task, nullptr, true)) fail("preemption setup: FAIL");
    kernel::scheduler::set_completion_task(completion);
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_PREEMPTIVE);
    if (!kernel::scheduler::prepare_interrupt_contexts()) fail("preemption setup: FAIL");
    phase3_timer_start();
    kernel::arch::irq_enable();
    kernel::scheduler::start();
    phase3_timer_stop();
    if (!kernel::scheduler::preemptive_complete() || kernel::scheduler::failed() ||
        kernel::scheduler::preemptions() < kPreemptionTarget || g_fs_failure || g_fs_work.failures != 0 ||
        g_fs_work.reads == 0 || g_fs_work.enumerations == 0 || !kernel::scheduler::stack_integrity()) {
        fail("scheduler/VFS integration: FAIL");
    }
    print("[guideXOS] scheduler/VFS integration: PASS reads=");
    phase3_serial_dec(g_fs_work.reads);
    print(" enumerations=");
    phase3_serial_dec(g_fs_work.enumerations);
    print("\n");

    kernel::scheduler::Stats stats{};
    kernel::scheduler::get_stats(&stats);
    print("[guideXOS] memory/VFS durability: PASS pages-used=");
    phase3_serial_dec(kernel::memory::allocated_pages());
    print(" pages-free=");
    phase3_serial_dec(kernel::memory::free_pages());
    print(" heap-used=");
    phase3_serial_dec(gxos_kernel_heap_used_bytes());
    print(" heap-alloc-cycles=1000 ticks=");
    phase3_serial_dec(stats.timer_ticks);
    print(" context-switches=");
    phase3_serial_dec(stats.context_switches);
    print(" preemptions=");
    phase3_serial_dec(stats.preemptions);
    print(" vfs-reads=");
    phase3_serial_dec(g_fs_work.reads);
    print(" vfs-enumerations=");
    phase3_serial_dec(g_fs_work.enumerations);
    print(" unexpected-irq=");
    phase3_serial_dec(stats.unexpected_irqs);
    print(" exceptions=");
    phase3_serial_dec(phase3_exception_count());
    print("\nAARCH64_PHASE4_PASS\n");
    for (;;) __asm__ volatile("wfi");
}
