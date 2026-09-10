#include "include/kernel/nativeaot_application.h"

#include "include/kernel/address_space.h"
#include "include/kernel/arch.h"
#include "include/kernel/hugepages.h"
#include "include/kernel/pit.h"
#include "include/kernel/process.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

#include "runtime/local_storage/guidexos_local_storage.h"
#include "runtime/memory/guidexos_virtual_memory_region.h"
#include "runtime/synchronization/guidexos_event.h"
#include "runtime/thread/guidexos_native_stack_bounds.h"
#include "runtime/thread/guidexos_native_thread.h"

#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_gc_startup_platform_contract.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_abi_bridge.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.h"

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace nativeaot {
namespace {

constexpr uintptr_t kPageSize = 0x1000u;
constexpr uint32_t kMaxProgramHeaders = 32u;
constexpr uint32_t kMaxMappedPages = 4096u;
// The ELF image owns one VM registry slot per PT_LOAD mapping.  NativeAOT
// startup also needs ordinary PAL allocations, so the registry must leave
// bounded capacity beyond the image's seven current load segments.
constexpr uint32_t kMaxVmSlots = 256u;
constexpr uint32_t kMaxEventSlots = 8u;
constexpr uint32_t kMaxWorkerSlots = 4u;
constexpr uint32_t kMaxFlsSlots = gxos::runtime::kLocalStorageCapacity;
constexpr uint32_t kMaxFlsContexts = gxos::runtime::kLocalStorageMaximumContexts;
constexpr uint64_t kMaxArtifactBytes = 4u * 1024u * 1024u;
constexpr uint32_t kMaxApplicationPath = 128u;
constexpr uint32_t kElfClass64 = 2u;
constexpr uint32_t kElfDataLittle = 1u;
constexpr uint16_t kElfExecutable = 2u;
constexpr uint16_t kElfMachineAmd64 = 0x3Eu;
constexpr uint32_t kElfProgramLoad = 1u;
constexpr uint32_t kElfProgramNull = 0u;
constexpr uint32_t kElfProgramGnuStack = 0x6474E551u;
constexpr uint32_t kElfFlagExecute = 1u;
constexpr uint32_t kElfFlagWrite = 2u;
constexpr uint32_t kElfFlagRead = 4u;
constexpr uint32_t kVmemCommit = 0x1000u;
constexpr uint32_t kVmemRelease = 0x8000u;

#pragma pack(push, 1)
struct Elf64Header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t programHeaderOffset;
    uint64_t sectionHeaderOffset;
    uint32_t flags;
    uint16_t headerSize;
    uint16_t programHeaderSize;
    uint16_t programHeaderCount;
    uint16_t sectionHeaderSize;
    uint16_t sectionHeaderCount;
    uint16_t sectionNameIndex;
};

struct Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtualAddress;
    uint64_t physicalAddress;
    uint64_t fileSize;
    uint64_t memorySize;
    uint64_t alignment;
};
#pragma pack(pop)

struct MappedPage {
    uintptr_t address;
    uint64_t physical;
};

struct VmSlot {
    bool active;
    gxos::runtime::virtual_memory::VirtualMemoryRegion region;
};

struct EventSlot {
    bool active;
    gxos::runtime::Event event;
};

struct FlsSlot {
    bool active;
    gxos::runtime::LocalStorageIndex genericIndex;
    guidexos_nativeaot_pal_win64_detach_callback callback;
};

struct FlsContext {
    bool active;
    uint64_t threadId;
    void* values[kMaxFlsSlots];
};

struct WorkerSlot {
    bool active;
    bool joined;
    uint32_t generation;
    guidexos_nativeaot_pal_win64_worker_entry entry;
    void* context;
    gxos::runtime::ThreadHandle nativeHandle;
};

struct NativeAotTlsGsArea {
    uint8_t reserved[0x58];
    void** vector;
};

struct NativeHostCallTable {
    uint32_t size;
    uint32_t version;
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *log)(void* context, uint8_t* message);
};

struct NativeGxAppContext {
    uint32_t size;
    uint32_t apiVersion;
    NativeHostCallTable* host;
    void* userData;
};

struct NativeAotStartupContext {
    void* legacyPalHooks;
    void* palHookTable;
    void* gcPlatformTable;
    void* managedContext;
    int (*installTls)(void);
    void (*startupMarker)(uint32_t stage);
};

enum class ApplicationLifecycleState : uint32_t {
    Empty = 0,
    Loading = 1,
    Resident = 2,
};

struct ResidentApplication {
    ApplicationLifecycleState state;
    char path[kMaxApplicationPath];
    uint64_t artifactBytes;
    uintptr_t artifactBase;
    uintptr_t artifactSpan;
    uintptr_t entryPoint;
    uint16_t loadSegmentCount;
    uint32_t sequence;
};

static_assert(sizeof(NativeHostCallTable) == 16, "C102 host callback ABI drift");
static_assert(sizeof(NativeGxAppContext) == 24, "C102 application ABI drift");
static_assert(offsetof(NativeAotTlsGsArea, vector) == 0x58,
              "C102 TLS vector offset drift");

alignas(4096) uint8_t g_artifact[kMaxArtifactBytes] = {};
MappedPage g_pages[kMaxMappedPages] = {};
uint32_t g_pageCount = 0;
uintptr_t g_artifactBase = 0;
uintptr_t g_artifactSpan = 0;
uint64_t g_installationGeneration = 1;
VmSlot g_vmSlots[kMaxVmSlots] = {};
EventSlot g_eventSlots[kMaxEventSlots] = {};
FlsSlot g_flsSlots[kMaxFlsSlots] = {};
FlsContext g_flsContexts[kMaxFlsContexts] = {};
WorkerSlot g_workers[kMaxWorkerSlots] = {};
alignas(16) NativeAotTlsGsArea g_tlsArea = {};
alignas(16) uint8_t g_tlsBlock[0x110] = {};
void* g_tlsVector[1] = {};
bool g_c102ManagedEntryObserved = false;
bool g_c102ManagedPassObserved = false;
ResidentApplication g_application = {};

#if defined(GXOS_C103_PRODUCTION_LAUNCH) || defined(GXOS_C103_NEGATIVE_LAUNCH)
constexpr bool kC103LifecycleEnabled = true;
#else
constexpr bool kC103LifecycleEnabled = false;
#endif

bool boundedRange(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool artifactContains(uintptr_t value) {
    return g_artifactBase != 0 && value >= g_artifactBase &&
        value - g_artifactBase < g_artifactSpan;
}

void emitFrameAccounting(const char* phase, const memory::address_space::FrameAccounting& stats) {
    serial::puts("[C102-FRAMES] phase=");
    serial::puts(phase);
    serial::puts(" totalTracked=");
    serial::put_hex64(stats.totalKnownFrames);
    serial::puts(" free=");
    serial::put_hex64(stats.freeFrames);
    serial::puts(" allocated=");
    serial::put_hex64(stats.allocatedFrames);
    serial::puts(" vmRegion=");
    serial::put_hex64(stats.regionOwnedFrames);
    serial::puts(" pageTable=");
    serial::put_hex64(stats.pageTableFrames);
    serial::puts(" kernel=");
    serial::put_hex64(stats.kernelOwnedFrames);
    serial::puts(" residual=");
    serial::put_hex64(stats.totalKnownFrames >= stats.freeFrames + stats.allocatedFrames
        ? stats.totalKnownFrames - stats.freeFrames - stats.allocatedFrames : UINT64_MAX);
    serial::puts(" ownerResidual=");
    serial::put_hex64(stats.allocatedFrames >=
            stats.regionOwnedFrames + stats.pageTableFrames + stats.kernelOwnedFrames
        ? stats.allocatedFrames - stats.regionOwnedFrames -
              stats.pageTableFrames - stats.kernelOwnedFrames : UINT64_MAX);
    serial::puts("\n");
}

void emitC103Begin(uint32_t sequence, const char* path, bool reused) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-LAUNCH-BEGIN] sequence=");
    serial::put_hex32(sequence);
    serial::puts(" path=");
    serial::puts(path);
    serial::puts(" runtime=");
    serial::puts(reused ? "reused" : "initialized");
    serial::puts(" image=");
    serial::puts(reused ? "resident" : "mapped");
    serial::puts("\n");
}

void emitC103Frames(const char* phase, uint32_t sequence,
                    const memory::address_space::FrameAccounting& stats) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-FRAMES] phase=");
    serial::puts(phase);
    serial::puts(" sequence=");
    serial::put_hex32(sequence);
    serial::puts(" totalTracked=");
    serial::put_hex64(stats.totalKnownFrames);
    serial::puts(" free=");
    serial::put_hex64(stats.freeFrames);
    serial::puts(" allocated=");
    serial::put_hex64(stats.allocatedFrames);
    serial::puts(" vmRegion=");
    serial::put_hex64(stats.regionOwnedFrames);
    serial::puts(" pageTable=");
    serial::put_hex64(stats.pageTableFrames);
    serial::puts(" residual=");
    serial::put_hex64(stats.totalKnownFrames >= stats.freeFrames + stats.allocatedFrames
        ? stats.totalKnownFrames - stats.freeFrames - stats.allocatedFrames : UINT64_MAX);
    serial::puts(" ownerResidual=");
    serial::put_hex64(stats.allocatedFrames >=
            stats.regionOwnedFrames + stats.pageTableFrames + stats.kernelOwnedFrames
        ? stats.allocatedFrames - stats.regionOwnedFrames -
              stats.pageTableFrames - stats.kernelOwnedFrames : UINT64_MAX);
    serial::puts("\n");
}

void emitC103Lifecycle(uint32_t sequence, const char* state, bool runtimeReused) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-LIFECYCLE] sequence=");
    serial::put_hex32(sequence);
    serial::puts(" state=");
    serial::puts(state);
    serial::puts(" runtime=");
    serial::puts(runtimeReused ? "reused" : "initialized");
    serial::puts(" image=resident reusable=1\n");
}

void emitC103Return(uint32_t sequence, LaunchStatus status) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-LAUNCH-RETURN] sequence=");
    serial::put_hex32(sequence);
    serial::puts(" status=");
    serial::puts(launchStatusName(status));
    serial::puts("\n");
}

bool copyApplicationPath(const char* source, char* destination) {
    if (source == nullptr || destination == nullptr) return false;
    uint32_t index = 0;
    for (; index + 1u < kMaxApplicationPath && source[index] != 0; ++index) {
        destination[index] = source[index];
    }
    if (source[index] != 0) return false;
    destination[index] = 0;
    return true;
}

bool sameApplicationPath(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    for (uint32_t index = 0; index < kMaxApplicationPath; ++index) {
        if (left[index] != right[index]) return false;
        if (left[index] == 0) return true;
    }
    return false;
}

bool installTls() {
    g_tlsVector[0] = g_tlsBlock;
    g_tlsArea.vector = g_tlsVector;
#if defined(ARCH_AMD64)
    constexpr uint32_t kGsBaseMsr = 0xC0000101u;
    const uint64_t value = reinterpret_cast<uint64_t>(&g_tlsArea);
    arch::amd64::write_msr(kGsBaseMsr, value);
    return arch::amd64::read_msr(kGsBaseMsr) == value;
#else
    return false;
#endif
}

int startupInstallTls() {
    return installTls() ? 0 : -1;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL currentThreadId64() {
    return process::current_thread_id();
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL stackBounds(
    guidexos_nativeaot_pal_stack_bounds_value* result) {
    if (result == nullptr) return -1;
    gxos::runtime::NativeStackBounds bounds{};
    if (gxos::runtime::queryCurrentNativeStackBounds(&bounds) !=
        gxos::runtime::StackBoundsResult::Success) return -1;
    result->low = bounds.low;
    result->high = bounds.high;
    result->current = bounds.current;
    return 0;
}

FlsContext* currentFlsContext() {
    const uint64_t id = currentThreadId64();
    if (id == 0) return nullptr;
    for (FlsContext& context : g_flsContexts) {
        if (context.active && context.threadId == id) return &context;
    }
    for (FlsContext& context : g_flsContexts) {
        if (!context.active) {
            context = FlsContext{};
            context.active = true;
            context.threadId = id;
            return &context;
        }
    }
    return nullptr;
}

void GUIDEXOS_NATIVEAOT_PAL_CALL flsDetach(void*) {}

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsAllocate(
    guidexos_nativeaot_pal_win64_detach_callback callback) {
    for (uint32_t index = 0; index < kMaxFlsSlots; ++index) {
        if (g_flsSlots[index].active) continue;
        gxos::runtime::LocalStorageIndex generic{};
        if (gxos::runtime::allocateLocalStorageIndex(flsDetach, &generic) !=
            gxos::runtime::LocalStorageResult::Success) return 0xFFFFFFFFu;
        g_flsSlots[index].active = true;
        g_flsSlots[index].genericIndex = generic;
        g_flsSlots[index].callback = callback;
        return index;
    }
    return 0xFFFFFFFFu;
}

bool validFlsIndex(uint32_t index) {
    return index < kMaxFlsSlots && g_flsSlots[index].active;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsRelease(uint32_t index) {
    if (!validFlsIndex(index)) return -1;
    const gxos::runtime::LocalStorageResult result =
        gxos::runtime::releaseLocalStorageIndex(g_flsSlots[index].genericIndex);
    if (result != gxos::runtime::LocalStorageResult::Success &&
        result != gxos::runtime::LocalStorageResult::CallbackFailed) return -1;
    g_flsSlots[index] = FlsSlot{};
    return 0;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL flsGet(uint32_t index) {
    FlsContext* context = validFlsIndex(index) ? currentFlsContext() : nullptr;
    return context == nullptr ? nullptr : context->values[index];
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsSet(uint32_t index, void* value) {
    FlsContext* context = validFlsIndex(index) ? currentFlsContext() : nullptr;
    if (context == nullptr) return -1;
    context->values[index] = value;
    return 0;
}

uintptr_t workerEntry(void* raw) {
    WorkerSlot* worker = static_cast<WorkerSlot*>(raw);
    if (worker == nullptr || !worker->active || worker->entry == nullptr) return 0;
    const auto attached = guidexos::nativeaot::threadstore::attachCurrentThread();
    if (attached != guidexos::nativeaot::threadstore::Result::Success) return 0;
    const uintptr_t result = guidexos_nativeaot_pal_bridge_invoke_worker(
        worker->entry, worker->context);
    const auto detached = guidexos::nativeaot::threadstore::detachCurrentThread();
    (void)detached;
    return result;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL createWorker(
    guidexos_nativeaot_pal_win64_worker_entry entry,
    void* context,
    uintptr_t stackSize,
    guidexos_nativeaot_pal_worker_handle* handle) {
    if (entry == nullptr || handle == nullptr || !artifactContains(
            reinterpret_cast<uintptr_t>(entry)) ||
        stackSize < gxos::runtime::kNativeThreadMinimumStackSize ||
        stackSize > gxos::runtime::kNativeThreadMaximumStackSize ||
        (stackSize & 15u) != 0) return -1;
    for (uint32_t index = 0; index < kMaxWorkerSlots; ++index) {
        WorkerSlot& worker = g_workers[index];
        if (worker.active) continue;
        if (worker.generation == 0) worker.generation = 1;
        worker.active = true;
        worker.joined = false;
        worker.entry = entry;
        worker.context = context;
        gxos::runtime::ThreadCreateOptions options;
        options.stackSize = static_cast<size_t>(stackSize);
        options.debugName = "nativeaot-production-worker";
        const gxos::runtime::ThreadResult created = gxos::runtime::createThread(
            workerEntry, &worker, options, &worker.nativeHandle);
        if (created != gxos::runtime::ThreadResult::Ok) {
            worker = WorkerSlot{};
            return -2;
        }
        *handle = guidexos_nativeaot_pal_worker_handle{
            index, worker.generation,
            static_cast<uint32_t>(g_installationGeneration), 0};
        return 0;
    }
    return -3;
}

WorkerSlot* lookupWorker(guidexos_nativeaot_pal_worker_handle handle) {
    if (handle.slot >= kMaxWorkerSlots || handle.reserved != 0 ||
        handle.domain_generation != static_cast<uint32_t>(g_installationGeneration)) {
        return nullptr;
    }
    WorkerSlot& worker = g_workers[handle.slot];
    return worker.active && worker.generation == handle.generation ? &worker : nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL joinWorker(
    guidexos_nativeaot_pal_worker_handle handle,
    uint32_t timeoutMilliseconds,
    uintptr_t* result) {
    WorkerSlot* worker = lookupWorker(handle);
    if (worker == nullptr || worker->joined) return -1;
    const gxos::runtime::WaitTimeout timeout = timeoutMilliseconds == 0xFFFFFFFFu
        ? gxos::runtime::WaitTimeout::infinite()
        : gxos::runtime::WaitTimeout::finiteMilliseconds(timeoutMilliseconds);
    const gxos::runtime::WaitResult joined = gxos::runtime::joinThread(
        worker->nativeHandle, timeout, result);
    if (joined == gxos::runtime::WaitResult::Signaled) {
        worker->joined = true;
        return 0;
    }
    return joined == gxos::runtime::WaitResult::TimedOut ? 1 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL destroyWorker(
    guidexos_nativeaot_pal_worker_handle handle) {
    WorkerSlot* worker = lookupWorker(handle);
    if (worker == nullptr) return -1;
    if (!worker->joined && joinWorker(handle, 0xFFFFFFFFu, nullptr) != 0) return -2;
    worker->active = false;
    worker->entry = nullptr;
    worker->context = nullptr;
    worker->nativeHandle = gxos::runtime::ThreadHandle{};
    if (worker->generation != 0xFFFFFFFFu) ++worker->generation;
    return 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL counter() { return pit::ticks(); }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL frequency() { return 100u; }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL milliseconds() { return pit::ticks() * 10u + 1u; }

void GUIDEXOS_NATIVEAOT_PAL_CALL sleepMilliseconds(uint32_t millisecondsValue) {
    const uint64_t target = pit::ticks() +
        (static_cast<uint64_t>(millisecondsValue) + 9u) / 10u;
    while (pit::ticks() < target) {
        arch::enable_interrupts();
        arch::halt();
        arch::disable_interrupts();
    }
}

void GUIDEXOS_NATIVEAOT_PAL_CALL yieldThread() {
#if defined(__x86_64__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

[[noreturn]] void GUIDEXOS_NATIVEAOT_PAL_CALL failFast(uint32_t reason, uintptr_t detail) {
    serial::puts("[C102-NATIVEAOT] FAIL_FAST reason=");
    serial::put_hex32(reason);
    serial::puts(" detail=");
    serial::put_hex64(detail);
    serial::puts("\n");
    for (;;) {
        arch::disable_interrupts();
        arch::halt();
    }
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL moduleFromPointer(void*, const void*) {
    return reinterpret_cast<void*>(g_artifactBase);
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL resolveStatic(void*, void*, const char*) {
    return nullptr;
}

VmSlot* findVm(const void* address) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    for (VmSlot& slot : g_vmSlots) {
        if (!slot.active || slot.region.base == nullptr) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(slot.region.base);
        if (value >= base && value - base < slot.region.reservedSize) return &slot;
    }
    return nullptr;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL virtualAlloc(
    void*, void* preferred, uintptr_t size, uint32_t type, uint32_t) {
    if (size == 0) return nullptr;
    const uintptr_t roundedSize =
        (size + (kPageSize - 1u)) & ~(static_cast<uintptr_t>(kPageSize) - 1u);
    if (roundedSize < size) return nullptr;
    for (VmSlot& slot : g_vmSlots) {
        if (slot.active) continue;
        if (gxos::runtime::virtual_memory::reserve(
                size, kPageSize, preferred, &slot.region) !=
            gxos::runtime::virtual_memory::VmResult::Ok) return nullptr;
        slot.active = true;
        if ((type & kVmemCommit) != 0 &&
            gxos::runtime::virtual_memory::commit(
                slot.region, 0, roundedSize,
                gxos::runtime::virtual_memory::MemoryProtection::ReadWrite) !=
            gxos::runtime::virtual_memory::VmResult::Ok) {
            (void)gxos::runtime::virtual_memory::release(slot.region);
            slot.active = false;
            return nullptr;
        }
        return slot.region.base;
    }
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualFree(
    void*, void* address, uintptr_t size, uint32_t type) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    const gxos::runtime::virtual_memory::VmResult result =
        type == kVmemRelease
            ? gxos::runtime::virtual_memory::release(slot->region)
            : gxos::runtime::virtual_memory::decommit(slot->region, offset, size);
    if (type == kVmemRelease && result == gxos::runtime::virtual_memory::VmResult::Ok) {
        slot->active = false;
    }
    return result == gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualProtect(
    void* address, uintptr_t size, uint32_t, uint32_t* oldProtection) {
    if (oldProtection != nullptr) *oldProtection = 0x04u;
    return findVm(address) != nullptr && size != 0 ? 1 : 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyCurrentThreadId(void*) {
    return currentThreadId64();
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyStackBounds(
    void*, uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    if (low == nullptr || high == nullptr || current == nullptr) return -1;
    guidexos_nativeaot_pal_stack_bounds_value value{};
    const int32_t result = stackBounds(&value);
    if (result == 0) {
        *low = value.low;
        *high = value.high;
        *current = value.current;
    }
    return result;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyCounter(void*) {
    return counter();
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyFrequency(void*) {
    return frequency();
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacySleepMilliseconds(
    void*, uint32_t value) {
    sleepMilliseconds(value);
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyYield(void*) {
    yieldThread();
    return 0;
}

void GUIDEXOS_NATIVEAOT_PAL_CALL legacyFailFast(void*, uint32_t reason) {
    failFast(reason, 0);
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyVirtualProtect(
    void*, void* address, uintptr_t size, uint32_t protection,
    uint32_t* oldProtection) {
    return virtualProtect(address, size, protection, oldProtection);
}

void fillLegacyHooks(guidexos_nativeaot_pal_hooks* hooks) {
    *hooks = {};
    hooks->size = sizeof(*hooks);
    hooks->abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    hooks->current_thread_id = legacyCurrentThreadId;
    hooks->stack_bounds = legacyStackBounds;
    hooks->counter = legacyCounter;
    hooks->frequency = legacyFrequency;
    hooks->sleep_milliseconds = legacySleepMilliseconds;
    hooks->yield = legacyYield;
    hooks->static_module_from_pointer = moduleFromPointer;
    hooks->static_resolve = resolveStatic;
    hooks->fail_fast = legacyFailFast;
    hooks->virtual_alloc = virtualAlloc;
    hooks->virtual_free = virtualFree;
    hooks->virtual_protect = legacyVirtualProtect;
}

void fillPalTable(guidexos_nativeaot_pal_hook_table_v1* table,
                  uintptr_t base, uintptr_t span) {
    *table = {};
    table->magic = GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC;
    table->abi_version = GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION;
    table->structure_size = sizeof(*table);
    table->capability_bits = GUIDEXOS_NATIVEAOT_PAL_CAP_REQUIRED;
    table->installation_generation = g_installationGeneration;
    table->artifact_base = base;
    table->artifact_size = span;
    table->current_thread_id = currentThreadId64;
    table->query_current_stack_bounds = stackBounds;
    table->fls_allocate = flsAllocate;
    table->fls_release = flsRelease;
    table->fls_get = flsGet;
    table->fls_set = flsSet;
    table->create_worker = createWorker;
    table->join_worker = joinWorker;
    table->destroy_worker_handle = destroyWorker;
    table->query_counter = counter;
    table->query_counter_frequency = frequency;
    table->monotonic_milliseconds = milliseconds;
    table->sleep_milliseconds = sleepMilliseconds;
    table->yield_thread = yieldThread;
    table->fail_fast = [](uint32_t reason, uintptr_t detail) {
        failFast(reason, detail);
    };
}

EventSlot* findEvent(void* handle) {
    for (EventSlot& slot : g_eventSlots) {
        if (slot.active && &slot.event == handle) return &slot;
    }
    return nullptr;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL gcCreateEvent(uint32_t manualReset,
                                                 uint32_t initialState) {
    for (EventSlot& slot : g_eventSlots) {
        if (slot.active) continue;
        if (!slot.event.initialize(
                manualReset != 0 ? gxos::runtime::EventMode::ManualReset
                                 : gxos::runtime::EventMode::AutoReset,
                initialState != 0)) return nullptr;
        slot.active = true;
        return &slot.event;
    }
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcSetEvent(void* handle) {
    EventSlot* slot = findEvent(handle);
    return slot != nullptr && slot->event.signal() == gxos::runtime::EventStatus::Ok
        ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcResetEvent(void* handle) {
    EventSlot* slot = findEvent(handle);
    return slot != nullptr && slot->event.reset() == gxos::runtime::EventStatus::Ok
        ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcWaitEvent(void* handle, uint32_t timeout) {
    EventSlot* slot = findEvent(handle);
    if (slot == nullptr) return -1;
    const gxos::runtime::WaitResult result = timeout == 0xFFFFFFFFu
        ? slot->event.wait(gxos::runtime::WaitTimeout::infinite())
        : slot->event.wait(gxos::runtime::WaitTimeout::finiteMilliseconds(timeout));
    return result == gxos::runtime::WaitResult::Signaled ? 0
        : result == gxos::runtime::WaitResult::TimedOut ? 258 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcCloseEvent(void* handle) {
    EventSlot* slot = findEvent(handle);
    if (slot == nullptr || slot->event.close() != gxos::runtime::EventStatus::Ok) return -1;
    slot->active = false;
    return 0;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL gcReserve(uintptr_t size, uintptr_t alignment,
                                             uint32_t, uint16_t) {
    if (size == 0) return nullptr;
    for (VmSlot& slot : g_vmSlots) {
        if (slot.active) continue;
        if (gxos::runtime::virtual_memory::reserve(size, alignment, nullptr,
                                                   &slot.region) !=
            gxos::runtime::virtual_memory::VmResult::Ok) return nullptr;
        slot.active = true;
        return slot.region.base;
    }
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcCommit(void* address, uintptr_t size, uint16_t) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr || size == 0) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    return gxos::runtime::virtual_memory::commit(
               slot->region, offset, size,
               gxos::runtime::virtual_memory::MemoryProtection::ReadWrite) ==
            gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcDecommit(void* address, uintptr_t size, uint16_t) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr || size == 0) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    return gxos::runtime::virtual_memory::decommit(slot->region, offset, size) ==
        gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcRelease(void* address, uintptr_t, uint16_t) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr || address != slot->region.base) return -1;
    const gxos::runtime::virtual_memory::VmResult result =
        gxos::runtime::virtual_memory::release(slot->region);
    if (result == gxos::runtime::virtual_memory::VmResult::Ok) slot->active = false;
    return result == gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcReset(void* address, uintptr_t size, uint32_t) {
    return findVm(address) != nullptr && size != 0 ? 0 : -1;
}

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcPageSize() { return 4096u; }
uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcGranularity() { return 4096u; }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL gcVirtualLimit() {
    return gxos::runtime::virtual_memory::maximumRegionSize();
}
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL gcPhysicalLimit(uint32_t* restricted) {
    if (restricted != nullptr) *restricted = 0;
    const memory::address_space::FrameAccounting stats =
        memory::address_space::accounting();
    return stats.totalKnownFrames * kPageSize;
}
void GUIDEXOS_NATIVEAOT_PAL_CALL gcMemoryStatus(uint64_t, uint32_t* load,
                                                 uint64_t* physical,
                                                 uint64_t* pageFile) {
    if (load != nullptr) *load = 0;
    const uint64_t limit = gcPhysicalLimit(nullptr);
    if (physical != nullptr) *physical = limit;
    if (pageFile != nullptr) *pageFile = limit;
}

void fillGcTable(guidexos_nativeaot_gc_startup_platform_table_v1* table) {
    *table = {};
    table->magic = GUIDEXOS_NATIVEAOT_GC_PLATFORM_MAGIC;
    table->abi_version = GUIDEXOS_NATIVEAOT_GC_PLATFORM_ABI_VERSION;
    table->structure_size = sizeof(*table);
    table->capability_bits = GUIDEXOS_NATIVEAOT_GC_CAP_REQUIRED;
    table->installation_generation = g_installationGeneration;
    table->create_event = gcCreateEvent;
    table->set_event = gcSetEvent;
    table->reset_event = gcResetEvent;
    table->wait_event = gcWaitEvent;
    table->close_event = gcCloseEvent;
    table->reserve = gcReserve;
    table->commit = gcCommit;
    table->decommit = gcDecommit;
    table->release = gcRelease;
    table->reset = gcReset;
    table->page_size = gcPageSize;
    table->allocation_granularity = gcGranularity;
    table->virtual_memory_limit = gcVirtualLimit;
    table->physical_memory_limit = gcPhysicalLimit;
    table->memory_status = gcMemoryStatus;
}

bool managedMessageEquals(const uint8_t* message, const char* expected) {
    if (message == nullptr || expected == nullptr) return false;
    uint32_t index = 0;
    while (expected[index] != '\0') {
        if (message[index] != static_cast<uint8_t>(expected[index])) return false;
        ++index;
    }
    return message[index] == 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedLog(void*, uint8_t* message) {
    if (message == nullptr) return -1;
    if (managedMessageEquals(message, "C102-MANAGED-ENTRY")) {
        g_c102ManagedEntryObserved = true;
    } else if (managedMessageEquals(message, "C102-MANAGED-PASS")) {
        g_c102ManagedPassObserved = true;
    }
    serial::puts("[C102-MANAGED-OUTPUT] ");
    for (uint32_t index = 0; index < 128u && message[index] != 0; ++index) {
        serial::putc(static_cast<char>(message[index]));
    }
    serial::puts("\n");
    return 0;
}

void startupMarker(uint32_t stage) {
    serial::puts("[C102-STARTUP] stage=");
    serial::put_hex32(stage);
    serial::puts("\n");
}

void releaseMappedPages();

bool mapElf(const uint8_t* bytes, uint64_t size, LaunchReport* report) {
    if (bytes == nullptr || size < sizeof(Elf64Header)) return false;
    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(bytes);
    if (header->ident[0] != 0x7Fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != kElfClass64 || header->ident[5] != kElfDataLittle ||
        header->type != kElfExecutable || header->machine != kElfMachineAmd64 ||
        header->version != 1u || header->headerSize != sizeof(Elf64Header) ||
        header->programHeaderSize != sizeof(Elf64ProgramHeader) ||
        header->programHeaderCount == 0 ||
        header->programHeaderCount > kMaxProgramHeaders ||
        !boundedRange(header->programHeaderOffset,
                      static_cast<uint64_t>(header->programHeaderCount) *
                          sizeof(Elf64ProgramHeader), size)) return false;
    const Elf64ProgramHeader* programs = reinterpret_cast<const Elf64ProgramHeader*>(
        bytes + header->programHeaderOffset);
    uintptr_t imageLow = UINTPTR_MAX;
    uintptr_t imageHigh = 0;
    bool entryValid = false;
    uint16_t loadCount = 0;
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type == kElfProgramNull || program.type == kElfProgramGnuStack) continue;
        if (program.type != kElfProgramLoad || program.memorySize == 0 ||
            program.fileSize > program.memorySize ||
            !boundedRange(program.offset, program.fileSize, size) ||
            program.alignment != kPageSize ||
            (program.offset & (kPageSize - 1u)) !=
                (program.virtualAddress & (kPageSize - 1u)) ||
            (program.flags & (kElfFlagWrite | kElfFlagExecute)) ==
                (kElfFlagWrite | kElfFlagExecute)) return false;
        if (program.virtualAddress > UINTPTR_MAX - program.memorySize) return false;
        const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
        const uint64_t end = program.virtualAddress + program.memorySize;
        const uintptr_t high = static_cast<uintptr_t>((end + kPageSize - 1u) & ~(kPageSize - 1u));
        if (high <= low || high > UINTPTR_MAX - kPageSize) return false;
        if (low < imageLow) imageLow = low;
        if (high > imageHigh) imageHigh = high;
        if (header->entry >= program.virtualAddress && header->entry < end) {
            entryValid = (program.flags & kElfFlagExecute) != 0;
        }
        ++loadCount;
    }
    if (loadCount == 0 || imageLow == UINTPTR_MAX || imageHigh <= imageLow ||
        !entryValid || imageHigh - imageLow > kMaxArtifactBytes * 2u) return false;

    memory::address_space::AddressSpace* addressSpace = memory::address_space::current();
    if (addressSpace == nullptr) return false;
    g_pageCount = 0;
    g_artifactBase = imageLow;
    g_artifactSpan = imageHigh - imageLow;
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type != kElfProgramLoad) continue;
        const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
        const uintptr_t end = static_cast<uintptr_t>(program.virtualAddress + program.memorySize);
        const uintptr_t high = (end + kPageSize - 1u) & ~(kPageSize - 1u);
        for (uintptr_t address = low; address < high; address += kPageSize) {
            memory::address_space::MappingInfo existing{};
            if (memory::address_space::queryPage(addressSpace, address, &existing) &&
                existing.present) {
                releaseMappedPages();
                return false;
            }
            if (g_pageCount >= kMaxMappedPages) {
                releaseMappedPages();
                return false;
            }
            const uint64_t physical = memory::address_space::allocateFrame(
                memory::address_space::FrameOwner::VmRegion);
            if (physical == 0) {
                releaseMappedPages();
                return false;
            }
            if (!memory::address_space::zeroFrame(physical)) {
                releaseMappedPages();
                return false;
            }
            if (!memory::address_space::mapPage(addressSpace, address, physical,
                    memory::hugepages::x86_64::PTE_P |
                    memory::hugepages::x86_64::PTE_W)) {
                releaseMappedPages();
                return false;
            }
            g_pages[g_pageCount++] = { address, physical };
        }
    }
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type != kElfProgramLoad || program.fileSize == 0) continue;
        uint8_t* destination = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(program.virtualAddress));
        const uint8_t* source = bytes + program.offset;
        for (uint64_t offset = 0; offset < program.fileSize; ++offset) {
            destination[offset] = source[offset];
        }
    }
    for (uint32_t pageIndex = 0; pageIndex < g_pageCount; ++pageIndex) {
        const uintptr_t address = g_pages[pageIndex].address;
        uint32_t flags = 0;
        bool covered = false;
        for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
            const Elf64ProgramHeader& program = programs[index];
            if (program.type != kElfProgramLoad) continue;
            const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
            const uintptr_t end = static_cast<uintptr_t>(program.virtualAddress + program.memorySize);
            const uintptr_t high = (end + kPageSize - 1u) & ~(kPageSize - 1u);
            if (address >= low && address < high) {
                flags |= program.flags;
                covered = true;
            }
        }
        if (!covered) {
            releaseMappedPages();
            return false;
        }
        uint64_t pte = memory::hugepages::x86_64::PTE_P;
        if ((flags & kElfFlagWrite) != 0) pte |= memory::hugepages::x86_64::PTE_W;
        if ((flags & kElfFlagExecute) == 0) pte |= memory::hugepages::x86_64::PTE_NX;
        if (!memory::address_space::updatePageFlags(addressSpace, address, pte)) {
            releaseMappedPages();
            return false;
        }
    }
    if (!memory::address_space::rangeHasPresentMapping(
            addressSpace,
            static_cast<uintptr_t>(header->entry & ~(kPageSize - 1u)), 1u)) {
        releaseMappedPages();
        return false;
    }
    report->artifactBase = imageLow;
    report->artifactSpan = imageHigh - imageLow;
    report->loadSegmentCount = loadCount;
    report->entryPoint = header->entry;
    return true;
}

void emitManagedStatus(bool entry, bool pass) {
    serial::puts("[C102-MANAGED-STATUS] entry=");
    serial::put_hex32(entry ? 1u : 0u);
    serial::puts(" pass=");
    serial::put_hex32(pass ? 1u : 0u);
    serial::puts("\n");
}

void releaseMappedPages() {
    memory::address_space::AddressSpace* addressSpace =
        memory::address_space::current();
    for (uint32_t index = 0; index < g_pageCount; ++index) {
        memory::address_space::MappingInfo removed{};
        (void)memory::address_space::unmapPage(
            addressSpace, g_pages[index].address, &removed);
        (void)memory::address_space::releaseFrame(
            g_pages[index].physical,
            memory::address_space::FrameOwner::VmRegion,
            memory::address_space::FrameReleaseReason::Release);
    }
    g_pageCount = 0;
    g_artifactBase = 0;
    g_artifactSpan = 0;
}

} // namespace

const char* launchStatusName(LaunchStatus status) {
    switch (status) {
        case LaunchStatus::Success: return "success";
        case LaunchStatus::NotFound: return "not-found";
        case LaunchStatus::InvalidPath: return "invalid-path";
        case LaunchStatus::ReadFailed: return "read-failed";
        case LaunchStatus::ArtifactTooLarge: return "artifact-too-large";
        case LaunchStatus::InvalidElf: return "invalid-elf";
        case LaunchStatus::MappingFailed: return "mapping-failed";
        case LaunchStatus::RuntimeFoundationFailed: return "runtime-foundation-failed";
        case LaunchStatus::TlsFailed: return "tls-failed";
        case LaunchStatus::StartupFailed: return "startup-failed";
        case LaunchStatus::ManagedFailed: return "managed-failed";
        case LaunchStatus::Busy: return "busy";
    }
    return "unknown";
}

LaunchStatus launchResident(const char* path, LaunchReport* report) {
    const uint32_t sequence = g_application.sequence + 1u;
    emitC103Begin(sequence, path, true);
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFFu) {
        report->status = LaunchStatus::NotFound;
        serial::puts("[C102-LAUNCH] rejected status=not-found path=");
        serial::puts(path);
        serial::puts("\n");
        emitC103Return(g_application.sequence, report->status);
        return report->status;
    }
    (void)vfs::close(handle);
    if (!sameApplicationPath(path, g_application.path)) {
        report->status = LaunchStatus::Busy;
        emitC103Lifecycle(g_application.sequence, "resident-different-image", true);
        emitC103Return(g_application.sequence, report->status);
        return report->status;
    }

    report->artifactBytes = g_application.artifactBytes;
    report->artifactBase = g_application.artifactBase;
    report->artifactSpan = g_application.artifactSpan;
    report->loadSegmentCount = g_application.loadSegmentCount;
    report->entryPoint = g_application.entryPoint;
    report->sequence = sequence;
    report->runtimeInitialized = true;
    report->runtimeReused = true;
    report->residentImage = true;
    report->lifecycleReusable = true;
    const memory::address_space::FrameAccounting before =
        memory::address_space::accounting();
    report->preTotalTracked = before.totalKnownFrames;
    report->preFreeFrames = before.freeFrames;
    report->preAllocatedFrames = before.allocatedFrames;
    report->preVmRegionFrames = before.regionOwnedFrames;
    report->prePageTableFrames = before.pageTableFrames;
    report->mapTotalTracked = before.totalKnownFrames;
    report->mapFreeFrames = before.freeFrames;
    report->mapAllocatedFrames = before.allocatedFrames;
    report->mapVmRegionFrames = before.regionOwnedFrames;
    report->mapPageTableFrames = before.pageTableFrames;
    emitFrameAccounting("before", before);
    emitC103Frames("before", sequence, before);
    serial::puts("[C103-LOADER] reusing resident ELF64 AMD64 ET_EXEC entry=");
    serial::put_hex64(report->entryPoint);
    serial::puts(" base=");
    serial::put_hex64(report->artifactBase);
    serial::puts(" span=");
    serial::put_hex64(report->artifactSpan);
    serial::puts("\n");

    if (!gxos::runtime::isLocalStorageInitialized() &&
        gxos::runtime::initializeLocalStorage() != gxos::runtime::LocalStorageResult::Success) {
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    if (gxos::runtime::attachLocalStorage() != gxos::runtime::LocalStorageResult::Success ||
        (!guidexos::nativeaot::threadstore::isInitialized() &&
         guidexos::nativeaot::threadstore::initialize() !=
             guidexos::nativeaot::threadstore::Result::Success) ||
        (guidexos::nativeaot::threadstore::getCurrentThread() == nullptr &&
         guidexos::nativeaot::threadstore::attachCurrentThread() !=
             guidexos::nativeaot::threadstore::Result::Success)) {
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    serial::puts("[C103-RUNTIME] resident runtime reused TLS=0\n");

    guidexos_nativeaot_pal_hooks legacy{};
    fillLegacyHooks(&legacy);
    guidexos_nativeaot_pal_hook_table_v1 pal{};
    fillPalTable(&pal, report->artifactBase, report->artifactSpan);
    guidexos_nativeaot_gc_startup_platform_table_v1 gc{};
    fillGcTable(&gc);
    NativeHostCallTable host{ sizeof(NativeHostCallTable), 0u, managedLog };
    NativeGxAppContext app{ sizeof(NativeGxAppContext), 0u, &host, nullptr };
    NativeAotStartupContext startup{
        &legacy, &pal, &gc, &app, startupInstallTls, startupMarker };
    using Entry = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void*);
    g_c102ManagedEntryObserved = false;
    g_c102ManagedPassObserved = false;
    arch::amd64::disable_interrupts();
    const int32_t managedReturn = reinterpret_cast<Entry>(report->entryPoint)(&startup);
    arch::amd64::enable_interrupts();
    report->managedReturn = managedReturn;
    report->managedEntryReached = g_c102ManagedEntryObserved;
    report->managedPassReached = g_c102ManagedPassObserved && managedReturn == 0;
    report->launcherRegainedControl = true;
    report->mappingsPersistentByDesign = true;
    g_application.sequence = sequence;
    emitManagedStatus(report->managedEntryReached, report->managedPassReached);
    serial::puts("[C102-LAUNCH-RETURN] status=");
    serial::put_hex32(static_cast<uint32_t>(managedReturn));
    serial::puts("\n");
    const memory::address_space::FrameAccounting after = memory::address_space::accounting();
    report->postTotalTracked = after.totalKnownFrames;
    report->postFreeFrames = after.freeFrames;
    report->postAllocatedFrames = after.allocatedFrames;
    report->postVmRegionFrames = after.regionOwnedFrames;
    report->postPageTableFrames = after.pageTableFrames;
    report->accountingResidual = after.totalKnownFrames >= after.freeFrames + after.allocatedFrames
        ? after.totalKnownFrames - after.freeFrames - after.allocatedFrames : UINT64_MAX;
    report->ownerResidual = after.allocatedFrames >=
            after.regionOwnedFrames + after.pageTableFrames + after.kernelOwnedFrames
        ? after.allocatedFrames - after.regionOwnedFrames -
              after.pageTableFrames - after.kernelOwnedFrames : UINT64_MAX;
    emitFrameAccounting("after-return", after);
    emitC103Frames("after-return", sequence, after);
    emitC103Frames("after-lifecycle", sequence, after);
    emitC103Lifecycle(sequence, "resident", true);
    report->status = managedReturn == 0 ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    emitC103Return(sequence, report->status);
    return report->status;
}

LaunchStatus launch(const char* path, LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->status = LaunchStatus::InvalidPath;
    if (path == nullptr || path[0] != '/') return report->status;
    char requestedPath[kMaxApplicationPath] = {};
    if (!copyApplicationPath(path, requestedPath)) return report->status;
    if (g_application.state == ApplicationLifecycleState::Resident) {
        return launchResident(requestedPath, report);
    }
    g_application.state = ApplicationLifecycleState::Loading;
    const uint32_t sequence = 1u;
    emitC103Begin(sequence, requestedPath, false);

    const uint8_t handle = vfs::open(requestedPath, vfs::OPEN_READ);
    if (handle == 0xFFu) {
        report->status = LaunchStatus::NotFound;
        serial::puts("[C102-LAUNCH] rejected status=not-found path=");
        serial::puts(path);
        serial::puts("\n");
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    const int64_t fileSize = vfs::file_size(handle);
    if (fileSize <= 0 || static_cast<uint64_t>(fileSize) > kMaxArtifactBytes) {
        (void)vfs::close(handle);
        report->status = fileSize > static_cast<int64_t>(kMaxArtifactBytes)
            ? LaunchStatus::ArtifactTooLarge : LaunchStatus::ReadFailed;
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    report->artifactBytes = static_cast<uint64_t>(fileSize);
    uint64_t loaded = 0;
    while (loaded < report->artifactBytes) {
        const uint32_t chunk = static_cast<uint32_t>(
            report->artifactBytes - loaded > 64u * 1024u
                ? 64u * 1024u : report->artifactBytes - loaded);
        const int32_t read = vfs::read(handle, g_artifact + loaded, chunk);
        if (read <= 0 || static_cast<uint32_t>(read) != chunk) {
            (void)vfs::close(handle);
            report->status = LaunchStatus::ReadFailed;
            g_application.state = ApplicationLifecycleState::Empty;
            emitC103Return(sequence, report->status);
            return report->status;
        }
        loaded += static_cast<uint32_t>(read);
    }
    (void)vfs::close(handle);

    if (!memory::address_space::reserveCurrentKernelImage()) {
        report->status = LaunchStatus::RuntimeFoundationFailed;
        serial::puts("[C102-LOADER] rejected status=kernel-frame-claim-failed\n");
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }

    const memory::address_space::FrameAccounting before = memory::address_space::accounting();
    report->preTotalTracked = before.totalKnownFrames;
    report->preFreeFrames = before.freeFrames;
    report->preAllocatedFrames = before.allocatedFrames;
    report->preVmRegionFrames = before.regionOwnedFrames;
    report->prePageTableFrames = before.pageTableFrames;
    emitFrameAccounting("before", before);
    emitC103Frames("before", sequence, before);
    serial::puts("[C102-LOADER] validating path=");
    serial::puts(requestedPath);
    serial::puts(" bytes=");
    serial::put_hex64(report->artifactBytes);
    serial::puts("\n");
    arch::amd64::disable_interrupts();
    const bool mapped = mapElf(g_artifact, report->artifactBytes, report);
    arch::amd64::enable_interrupts();
    if (!mapped) {
        releaseMappedPages();
        report->status = LaunchStatus::InvalidElf;
        serial::puts("[C102-LOADER] rejected status=invalid-elf\n");
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    const memory::address_space::FrameAccounting afterMap =
        memory::address_space::accounting();
    report->mapTotalTracked = afterMap.totalKnownFrames;
    report->mapFreeFrames = afterMap.freeFrames;
    report->mapAllocatedFrames = afterMap.allocatedFrames;
    report->mapVmRegionFrames = afterMap.regionOwnedFrames;
    report->mapPageTableFrames = afterMap.pageTableFrames;
    emitC103Frames("after-map", sequence, afterMap);
    serial::puts("[C102-LOADER] mapped ELF64 AMD64 ET_EXEC segments=");
    serial::put_hex32(report->loadSegmentCount);
    serial::puts(" entry=");
    serial::put_hex64(report->entryPoint);
    serial::puts(" base=");
    serial::put_hex64(report->artifactBase);
    serial::puts(" span=");
    serial::put_hex64(report->artifactSpan);
    serial::puts("\n");

    if (!gxos::runtime::isLocalStorageInitialized() &&
        gxos::runtime::initializeLocalStorage() != gxos::runtime::LocalStorageResult::Success) {
        releaseMappedPages();
        g_application.state = ApplicationLifecycleState::Empty;
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    if (gxos::runtime::attachLocalStorage() != gxos::runtime::LocalStorageResult::Success ||
        (!guidexos::nativeaot::threadstore::isInitialized() &&
         guidexos::nativeaot::threadstore::initialize() !=
             guidexos::nativeaot::threadstore::Result::Success) ||
        guidexos::nativeaot::threadstore::attachCurrentThread() !=
            guidexos::nativeaot::threadstore::Result::Success) {
        releaseMappedPages();
        g_application.state = ApplicationLifecycleState::Empty;
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    serial::puts("[C102-RUNTIME] PAL/VM/GC startup seam ready TLS=0\n");

    guidexos_nativeaot_pal_hooks legacy{};
    fillLegacyHooks(&legacy);
    guidexos_nativeaot_pal_hook_table_v1 pal{};
    fillPalTable(&pal, report->artifactBase, report->artifactSpan);
    guidexos_nativeaot_gc_startup_platform_table_v1 gc{};
    fillGcTable(&gc);
    NativeHostCallTable host{ sizeof(NativeHostCallTable), 0u, managedLog };
    NativeGxAppContext app{ sizeof(NativeGxAppContext), 0u, &host, nullptr };
    NativeAotStartupContext startup{
        &legacy, &pal, &gc, &app, startupInstallTls, startupMarker };
    using Entry = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void*);
    // The current kernel IRQ stubs restore a same-ring interrupt frame on the
    // active stack.  Keep the first cross-image startup/managed transition
    // bounded and non-preemptible; the ordinary kernel interrupt state is
    // restored before launch() returns to the main loop.
    g_c102ManagedEntryObserved = false;
    g_c102ManagedPassObserved = false;
    arch::amd64::disable_interrupts();
    const int32_t managedReturn = reinterpret_cast<Entry>(report->entryPoint)(&startup);
    arch::amd64::enable_interrupts();
    report->managedReturn = managedReturn;
    report->managedEntryReached = g_c102ManagedEntryObserved;
    report->managedPassReached = g_c102ManagedPassObserved && managedReturn == 0;
    report->launcherRegainedControl = true;
    g_application.state = ApplicationLifecycleState::Resident;
    (void)copyApplicationPath(requestedPath, g_application.path);
    g_application.artifactBytes = report->artifactBytes;
    g_application.artifactBase = report->artifactBase;
    g_application.artifactSpan = report->artifactSpan;
    g_application.entryPoint = report->entryPoint;
    g_application.loadSegmentCount = report->loadSegmentCount;
    g_application.sequence = sequence;
    report->sequence = sequence;
    report->runtimeInitialized = true;
    report->runtimeReused = false;
    report->residentImage = true;
    report->lifecycleReusable = true;
    emitManagedStatus(report->managedEntryReached, report->managedPassReached);
    serial::puts("[C102-LAUNCH-RETURN] status=");
    serial::put_hex32(static_cast<uint32_t>(managedReturn));
    serial::puts("\n");

    const memory::address_space::FrameAccounting after = memory::address_space::accounting();
    report->postTotalTracked = after.totalKnownFrames;
    report->postFreeFrames = after.freeFrames;
    report->postAllocatedFrames = after.allocatedFrames;
    report->postVmRegionFrames = after.regionOwnedFrames;
    report->postPageTableFrames = after.pageTableFrames;
    report->accountingResidual = after.totalKnownFrames >= after.freeFrames + after.allocatedFrames
        ? after.totalKnownFrames - after.freeFrames - after.allocatedFrames : UINT64_MAX;
    report->ownerResidual = after.allocatedFrames >=
            after.regionOwnedFrames + after.pageTableFrames + after.kernelOwnedFrames
        ? after.allocatedFrames - after.regionOwnedFrames -
              after.pageTableFrames - after.kernelOwnedFrames : UINT64_MAX;
    report->mappingsPersistentByDesign = true;
    emitFrameAccounting("after-return", after);
    emitC103Frames("after-return", sequence, after);
    emitC103Frames("after-lifecycle", sequence, after);
    emitC103Lifecycle(sequence, "resident", false);
    report->status = managedReturn == 0 ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    emitC103Return(sequence, report->status);
    return report->status;
}

} // namespace nativeaot
} // namespace kernel
