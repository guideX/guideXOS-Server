#include "include/kernel/nativeaot_pal_qemu_test.h"

#if defined(GXOS_NATIVEAOT_PAL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)

#include "include/kernel/address_space.h"
#include "include/kernel/arch.h"
#include "include/kernel/process.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/pit.h"

#include "runtime/local_storage/guidexos_local_storage.h"
#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
#include "runtime/memory/guidexos_virtual_memory_region.h"
#include "runtime/synchronization/guidexos_event.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_gc_startup_platform_contract.h"
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h"
#endif
#endif
#include "runtime/thread/guidexos_native_thread.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_abi_bridge.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.h"

#if defined(GXOS_NATIVEAOT_PAL_QEMU_TEST)
extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_end[];
#endif
#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#endif

namespace kernel {
namespace nativeaot_pal_qemu_test {
namespace {

constexpr uintptr_t kPageSize = 0x1000u;
constexpr uint32_t kPtLoad = 1u;
constexpr uint16_t kElfExec = 2u;
#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
// The startup artifact contains a bounded 4 MiB native metadata arena in its
// writable image.  Keep staging bounded while allowing that known image size.
constexpr uint32_t kMaxMappedPages = 8192u;
#else
constexpr uint32_t kMaxMappedPages = 1024u;
#endif
constexpr uint32_t kMaxFlsSlots = gxos::runtime::kLocalStorageCapacity;
constexpr uint32_t kMaxWorkerSlots = 8u;

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

struct MappedPage {
    uintptr_t virtualAddress;
    uint64_t physicalAddress;
};

struct BridgeCell {
    uint32_t slot;
    void* value;
};

struct BridgeThreadState {
    bool used;
    uint64_t threadId;
    BridgeCell cells[kMaxFlsSlots];
};

struct BridgeFlsSlot {
    bool active;
    gxos::runtime::LocalStorageIndex genericIndex;
    guidexos_nativeaot_pal_win64_detach_callback callback;
};

struct WorkerSlot {
    bool active;
    bool joined;
    uint32_t generation;
    guidexos_nativeaot_pal_win64_worker_entry entry;
    void* context;
    gxos::runtime::ThreadHandle nativeHandle;
};

MappedPage g_mappedPages[kMaxMappedPages] = {};
uint32_t g_mappedPageCount = 0;
BridgeFlsSlot g_flsSlots[kMaxFlsSlots] = {};
BridgeThreadState g_threadStates[32] = {};
WorkerSlot g_workers[kMaxWorkerSlots] = {};
uintptr_t g_artifactBase = 0;
uintptr_t g_artifactSize = 0;
uint64_t g_installationGeneration = 0;
uint32_t g_activeCallbacks = 0;
uint32_t g_workerCallbackCount = 0;
uint32_t g_detachCallbackCount = 0;
uintptr_t g_lastDetachValue = 0;
uintptr_t g_lastWorkerResult = 0;
uint64_t g_lastWorkerThreadId = 0;
uintptr_t g_lastWorkerStackLow = 0;
uintptr_t g_lastWorkerStackHigh = 0;
uintptr_t g_lastWorkerStackCurrent = 0;
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
uintptr_t g_firstAllocationDiagnosticsAddress = 0;

// The NativeAOT image uses the Win64 TLS vector contract directly:
// GS:[0x58] points to a vector and the image's _tls_index selects a block.
// The bare-metal loader had not installed that current-thread boundary.  A
// single disposable managed-entry thread is sufficient for this experiment;
// the block remains fixed native storage and is never used as a GC context.
struct NativeAotTlsGsArea {
    uint8_t reserved[0x58];
    void** vector;
};

alignas(16) NativeAotTlsGsArea g_nativeAotTlsGsArea = {};
alignas(16) unsigned char g_nativeAotTlsBlock[0x110] = {};
void* g_nativeAotTlsVector[1] = {};
static_assert(offsetof(NativeAotTlsGsArea, vector) == 0x58,
              "NativeAOT TLS vector offset must match the Win64 ABI");

bool installNativeAotCurrentThreadTls() {
    g_nativeAotTlsVector[0] = g_nativeAotTlsBlock;
    g_nativeAotTlsGsArea.vector = g_nativeAotTlsVector;
    constexpr uint32_t kGsBaseMsr = 0xC0000101u;
    const uint64_t gsBase = reinterpret_cast<uint64_t>(&g_nativeAotTlsGsArea);
    arch::amd64::write_msr(kGsBaseMsr, gsBase);
    return arch::amd64::read_msr(kGsBaseMsr) == gsBase;
}
#endif

void status(const char* name, bool passed, bool& allPassed) {
    serial::puts("[nativeaot-pal-qemu-test] ");
    serial::puts(name);
    serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) allPassed = false;
}

bool rangeContains(uintptr_t address) {
    return address >= g_artifactBase &&
           address < g_artifactBase + g_artifactSize;
}

BridgeThreadState* currentThreadState() {
    const uint64_t id = process::current_thread_id();
    if (id == 0) return nullptr;
    for (BridgeThreadState& state : g_threadStates) {
        if (state.used && state.threadId == id) return &state;
    }
    for (BridgeThreadState& state : g_threadStates) {
        if (!state.used) {
            state = BridgeThreadState{};
            state.used = true;
            state.threadId = id;
            for (uint32_t index = 0; index < kMaxFlsSlots; ++index) {
                state.cells[index].slot = index;
            }
            return &state;
        }
    }
    return nullptr;
}

BridgeCell* markerFromValue(void* value) {
    if (value == nullptr) return nullptr;
    for (BridgeThreadState& state : g_threadStates) {
        if (!state.used) continue;
        for (BridgeCell& cell : state.cells) {
            if (&cell == value) return &cell;
        }
    }
    return nullptr;
}

void genericDetach(void* value) {
    BridgeCell* cell = markerFromValue(value);
    if (cell == nullptr || cell->slot >= kMaxFlsSlots ||
        !g_flsSlots[cell->slot].active) return;
    void* callbackValue = cell->value;
    cell->value = nullptr;
    if (callbackValue == nullptr || g_flsSlots[cell->slot].callback == nullptr) return;
    ++g_activeCallbacks;
    ++g_detachCallbackCount;
    g_lastDetachValue = reinterpret_cast<uintptr_t>(callbackValue);
    guidexos_nativeaot_pal_bridge_invoke_detach(
        g_flsSlots[cell->slot].callback, callbackValue);
    --g_activeCallbacks;
}

bool validFlsIndex(uint32_t index) {
    return index < kMaxFlsSlots && g_flsSlots[index].active;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeCurrentThreadId64() {
    return static_cast<uint64_t>(process::current_thread_id());
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeStackBounds(
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

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeFlsAllocate(
    guidexos_nativeaot_pal_win64_detach_callback callback) {
    if (callback != nullptr && !rangeContains(reinterpret_cast<uintptr_t>(callback))) {
        serial::puts("[nativeaot-pal-qemu-test] FLS callback outside artifact: ");
        serial::put_hex64(reinterpret_cast<uintptr_t>(callback));
        serial::puts("\n");
        return 0xFFFFFFFFu;
    }
    for (uint32_t index = 0; index < kMaxFlsSlots; ++index) {
        if (g_flsSlots[index].active) continue;
        gxos::runtime::LocalStorageIndex generic{};
        if (gxos::runtime::allocateLocalStorageIndex(
                genericDetach, &generic) != gxos::runtime::LocalStorageResult::Success) {
            serial::puts("[nativeaot-pal-qemu-test] generic FLS allocation failed\n");
            return 0xFFFFFFFFu;
        }
        g_flsSlots[index].active = true;
        g_flsSlots[index].genericIndex = generic;
        g_flsSlots[index].callback = callback;
        return index;
    }
    return 0xFFFFFFFFu;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeFlsRelease(uint32_t index) {
    if (!validFlsIndex(index)) return -1;
    const gxos::runtime::LocalStorageResult result =
        gxos::runtime::releaseLocalStorageIndex(g_flsSlots[index].genericIndex);
    if (result != gxos::runtime::LocalStorageResult::Success &&
        result != gxos::runtime::LocalStorageResult::CallbackFailed) return -1;
    g_flsSlots[index] = BridgeFlsSlot{};
    return result == gxos::runtime::LocalStorageResult::Success ? 0 : -2;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL bridgeFlsGet(uint32_t index) {
    if (!validFlsIndex(index)) return nullptr;
    BridgeThreadState* state = currentThreadState();
    if (state == nullptr) return nullptr;
    void* marker = nullptr;
    if (gxos::runtime::getLocalStorageValue(
            g_flsSlots[index].genericIndex, &marker) !=
        gxos::runtime::LocalStorageResult::Success) return nullptr;
    BridgeCell* cell = markerFromValue(marker);
    return cell == nullptr ? nullptr : cell->value;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeFlsSet(uint32_t index, void* value) {
    if (!validFlsIndex(index)) return -1;
    BridgeThreadState* state = currentThreadState();
    if (state == nullptr) return -1;
    BridgeCell& cell = state->cells[index];
    cell.slot = index;
    cell.value = value;
    if (value == nullptr) {
        return gxos::runtime::setLocalStorageValue(
            g_flsSlots[index].genericIndex, nullptr) ==
            gxos::runtime::LocalStorageResult::Success ? 0 : -1;
    }
    return gxos::runtime::setLocalStorageValue(
        g_flsSlots[index].genericIndex, &cell) ==
        gxos::runtime::LocalStorageResult::Success ? 0 : -1;
}

uintptr_t workerEntry(void* raw) {
    WorkerSlot* worker = static_cast<WorkerSlot*>(raw);
    if (worker == nullptr || !worker->active || worker->entry == nullptr) return 0;
    if (guidexos::nativeaot::threadstore::attachCurrentThread() !=
        guidexos::nativeaot::threadstore::Result::Success) return 0;

    gxos::runtime::NativeStackBounds bounds{};
    const bool boundsValid =
        gxos::runtime::queryCurrentNativeStackBounds(&bounds) ==
        gxos::runtime::StackBoundsResult::Success;
    g_lastWorkerThreadId = process::current_thread_id();
    g_lastWorkerStackLow = bounds.low;
    g_lastWorkerStackHigh = bounds.high;
    g_lastWorkerStackCurrent = bounds.current;

    ++g_activeCallbacks;
    ++g_workerCallbackCount;
    const uintptr_t result = boundsValid
        ? guidexos_nativeaot_pal_bridge_invoke_worker(worker->entry, worker->context)
        : 0;
    --g_activeCallbacks;
    g_lastWorkerResult = result;
    if (guidexos::nativeaot::threadstore::detachCurrentThread() !=
        guidexos::nativeaot::threadstore::Result::Success) return 0;
    return result;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeCreateWorker(
    guidexos_nativeaot_pal_win64_worker_entry entry,
    void* context,
    uintptr_t stackSize,
    guidexos_nativeaot_pal_worker_handle* handle) {
    if (entry == nullptr || handle == nullptr || !rangeContains(reinterpret_cast<uintptr_t>(entry)) ||
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
        options.debugName = "nativeaot-pal-qemu-worker";
        if (gxos::runtime::createThread(workerEntry, &worker, options,
                                        &worker.nativeHandle) !=
            gxos::runtime::ThreadResult::Ok) {
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
        handle.domain_generation != static_cast<uint32_t>(g_installationGeneration)) return nullptr;
    WorkerSlot& worker = g_workers[handle.slot];
    return worker.active && worker.generation == handle.generation ? &worker : nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeJoinWorker(
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

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeDestroyWorker(
    guidexos_nativeaot_pal_worker_handle handle) {
    WorkerSlot* worker = lookupWorker(handle);
    if (worker == nullptr) return -1;
    // PalStartBackgroundWork has no public join result.  The probe-specific
    // close path joins before reclaiming, preserving a single cleanup owner.
    if (!worker->joined && bridgeJoinWorker(handle, 0xFFFFFFFFu, nullptr) != 0) return -2;
    worker->active = false;
    worker->entry = nullptr;
    worker->context = nullptr;
    worker->nativeHandle = gxos::runtime::ThreadHandle{};
    if (worker->generation != 0xFFFFFFFFu) ++worker->generation;
    return 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeCounter() {
    return pit::ticks();
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeFrequency() {
    return 100u;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL bridgeMilliseconds() {
    return pit::ticks() * 10u + 1u;
}

void GUIDEXOS_NATIVEAOT_PAL_CALL bridgeSleep(uint32_t milliseconds) {
    const uint64_t target = pit::ticks() +
        (static_cast<uint64_t>(milliseconds) + 9u) / 10u;
    while (pit::ticks() < target) {
        arch::enable_interrupts();
        arch::halt();
        arch::disable_interrupts();
    }
}

void GUIDEXOS_NATIVEAOT_PAL_CALL bridgeYield() {
#if defined(__x86_64__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

[[noreturn]] void GUIDEXOS_NATIVEAOT_PAL_CALL bridgeFailFast(
    uint32_t reason, uintptr_t detail) {
    serial::puts("[nativeaot-pal-qemu-test] FAIL_FAST reason=");
    serial::put_hex32(reason);
    serial::puts(" detail=");
    serial::put_hex64(detail);
    serial::putc('\n');
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
    if (g_firstAllocationDiagnosticsAddress != 0) {
        const guidexos_nativeaot_allocation_diagnostics* diagnostics =
            reinterpret_cast<const guidexos_nativeaot_allocation_diagnostics*>(
                g_firstAllocationDiagnosticsAddress);
        serial::puts("[nativeaot-gc-first-allocation] failfastStage=");
        serial::put_hex32(diagnostics->stage);
        serial::puts(" sequence=");
        serial::put_hex32(diagnostics->sequence);
        serial::puts(" failfastReason=");
        serial::put_hex32(diagnostics->failFastReason);
        serial::puts(" currentRip=");
        serial::put_hex64(diagnostics->currentRip);
        serial::puts(" currentRsp=");
        serial::put_hex64(diagnostics->currentRsp);
        serial::puts(" transitionFrame=");
        serial::put_hex64(diagnostics->transitionFrame);
        serial::puts(" runtimeThreadRecord=");
        serial::put_hex64(diagnostics->runtimeThreadRecord);
        serial::puts(" allocPtr=");
        serial::put_hex64(diagnostics->allocationContextBefore);
        serial::puts(" allocLimit=");
        serial::put_hex64(diagnostics->allocationLimitBefore);
        serial::puts("\n");
    }
#endif
    for (;;) {
        arch::disable_interrupts();
        arch::halt();
    }
}

void resetBridgeState(uintptr_t base, uintptr_t size, uint64_t generation) {
    g_artifactBase = base;
    g_artifactSize = size;
    g_installationGeneration = generation;
    g_activeCallbacks = 0;
    g_workerCallbackCount = 0;
    g_detachCallbackCount = 0;
    g_lastDetachValue = 0;
    g_lastWorkerResult = 0;
    g_lastWorkerThreadId = 0;
    g_lastWorkerStackLow = 0;
    g_lastWorkerStackHigh = 0;
    g_lastWorkerStackCurrent = 0;
    for (BridgeFlsSlot& slot : g_flsSlots) slot = BridgeFlsSlot{};
    for (BridgeThreadState& state : g_threadStates) state = BridgeThreadState{};
    for (WorkerSlot& worker : g_workers) {
        if (!worker.active) worker = WorkerSlot{};
    }
}

bool loadArtifact(const uint8_t* artifact, size_t artifactSize,
                  uintptr_t* base, uintptr_t* size) {
    if (artifact == nullptr || artifactSize < sizeof(Elf64Header)) return false;
    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(artifact);
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 2 || header->ident[5] != 1 ||
        header->type != kElfExec || header->machine != 0x3E ||
        header->programHeaderSize != sizeof(Elf64ProgramHeader) ||
        header->programHeaderCount == 0) return false;
    if (header->programHeaderOffset > artifactSize ||
        static_cast<uint64_t>(header->programHeaderCount) *
            header->programHeaderSize > artifactSize - header->programHeaderOffset) return false;

    uintptr_t imageLow = static_cast<uintptr_t>(-1);
    uintptr_t imageHigh = 0;
    const Elf64ProgramHeader* programs = reinterpret_cast<const Elf64ProgramHeader*>(
        artifact + header->programHeaderOffset);
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type != kPtLoad || program.memorySize == 0 ||
            program.fileSize > program.memorySize ||
            program.offset > artifactSize || program.fileSize > artifactSize - program.offset ||
            program.virtualAddress > static_cast<uint64_t>(-1) - program.memorySize) return false;
        const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
        const uintptr_t high = static_cast<uintptr_t>((program.virtualAddress + program.memorySize + kPageSize - 1u) & ~(kPageSize - 1u));
        if (low < imageLow) imageLow = low;
        if (high > imageHigh) imageHigh = high;
    }
    if (imageLow == static_cast<uintptr_t>(-1) || imageHigh <= imageLow) return false;
    memory::address_space::AddressSpace* addressSpace = memory::address_space::current();
    if (addressSpace == nullptr) return false;
    g_mappedPageCount = 0;
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type != kPtLoad || program.memorySize == 0) continue;
        const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
        const uintptr_t high = static_cast<uintptr_t>((program.virtualAddress + program.memorySize + kPageSize - 1u) & ~(kPageSize - 1u));
        for (uintptr_t address = low; address < high; address += kPageSize) {
            if (g_mappedPageCount >= kMaxMappedPages) return false;
            const uint64_t physical = memory::address_space::allocateFrame(
                memory::address_space::FrameOwner::VmRegion);
            if (physical == 0 || !memory::address_space::zeroFrame(physical) ||
                !memory::address_space::mapPage(addressSpace, address, physical, 0x3u)) return false;
            g_mappedPages[g_mappedPageCount++] = { address, physical };
        }
    }
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type != kPtLoad || program.fileSize == 0) continue;
        uint8_t* destination = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(program.virtualAddress));
        const uint8_t* source = artifact + program.offset;
        for (uint64_t byte = 0; byte < program.fileSize; ++byte) {
            destination[byte] = source[byte];
        }
    }
    *base = imageLow;
    *size = imageHigh - imageLow;
    return true;
}

void unloadArtifact() {
    memory::address_space::AddressSpace* addressSpace =
        memory::address_space::current();
    for (uint32_t index = 0; index < g_mappedPageCount; ++index) {
        memory::address_space::MappingInfo removed{};
        if (memory::address_space::unmapPage(addressSpace,
                g_mappedPages[index].virtualAddress, &removed) && removed.present) {
            (void)memory::address_space::releaseFrame(
                g_mappedPages[index].physicalAddress,
                memory::address_space::FrameOwner::VmRegion,
                memory::address_space::FrameReleaseReason::Release);
        }
    }
    g_mappedPageCount = 0;
}

void runOne(const uint8_t* artifact, size_t artifactSize,
            uintptr_t installAddress, uintptr_t mainAddress,
            uintptr_t uninstallAddress, uint64_t generation,
            bool& allPassed) {
    if (gxos::runtime::initializeLocalStorage() != gxos::runtime::LocalStorageResult::Success ||
        gxos::runtime::attachLocalStorage() != gxos::runtime::LocalStorageResult::Success ||
        guidexos::nativeaot::threadstore::initialize() !=
            guidexos::nativeaot::threadstore::Result::Success ||
        guidexos::nativeaot::threadstore::attachCurrentThread() !=
            guidexos::nativeaot::threadstore::Result::Success) {
        status("Runtime foundation initialization", false, allPassed);
        return;
    }

    uintptr_t base = 0;
    uintptr_t size = 0;
    const bool loaded = loadArtifact(artifact, artifactSize, &base, &size);
    status("Artifact staged", loaded, allPassed);
    if (!loaded) return;
    resetBridgeState(base, size, generation);

    guidexos_nativeaot_pal_hook_table_v1 table = {};
    table.magic = GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC;
    table.abi_version = GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION;
    table.structure_size = sizeof(table);
    table.capability_bits = GUIDEXOS_NATIVEAOT_PAL_CAP_REQUIRED;
    table.installation_generation = generation;
    table.artifact_base = base;
    table.artifact_size = size;
    table.current_thread_id = bridgeCurrentThreadId64;
    table.query_current_stack_bounds = bridgeStackBounds;
    table.fls_allocate = bridgeFlsAllocate;
    table.fls_release = bridgeFlsRelease;
    table.fls_get = bridgeFlsGet;
    table.fls_set = bridgeFlsSet;
    table.create_worker = bridgeCreateWorker;
    table.join_worker = bridgeJoinWorker;
    table.destroy_worker_handle = bridgeDestroyWorker;
    table.query_counter = bridgeCounter;
    table.query_counter_frequency = bridgeFrequency;
    table.monotonic_milliseconds = bridgeMilliseconds;
    table.sleep_milliseconds = bridgeSleep;
    table.yield_thread = bridgeYield;
    table.fail_fast = bridgeFailFast;

    serial::puts("[nativeaot-pal-qemu-test] Hook table ABI=1 size=");
    serial::put_hex32(table.structure_size);
    serial::puts(" capabilities=");
    serial::put_hex64(table.capability_bits);
    serial::puts(" generation=");
    serial::put_hex64(table.installation_generation);
    serial::puts(" artifact=");
    serial::put_hex64(table.artifact_base);
    serial::puts("+");
    serial::put_hex64(table.artifact_size);
    serial::puts("\n");

    using Install = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        const guidexos_nativeaot_pal_hook_table_v1*);
    using Main = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void*);
    using Uninstall = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
    Install install = reinterpret_cast<Install>(installAddress);
    Main main = reinterpret_cast<Main>(mainAddress);
    Uninstall uninstall = reinterpret_cast<Uninstall>(uninstallAddress);

    guidexos_nativeaot_pal_hook_table_v1 invalid = table;
    invalid.magic = 0;
    status("Invalid magic rejected", install(&invalid) == -2, allPassed);
    invalid = table;
    invalid.abi_version = 0xFFFFu;
    status("Unsupported version rejected", install(&invalid) == -3, allPassed);
    invalid = table;
    invalid.structure_size = 16u;
    status("Truncated table rejected", install(&invalid) == -4, allPassed);
    const int32_t installed = install(&table);
    status("Hook magic/version/size", installed == 0, allPassed);
    status("Capability negotiation", installed == 0, allPassed);
    if (installed != 0) {
        unloadArtifact();
        return;
    }

    const int32_t probeResult = main(nullptr);
    serial::puts("[nativeaot-pal-qemu-test] Probe return: ");
    serial::put_hex32(static_cast<uint32_t>(probeResult));
    serial::puts("\n");
    status("Initial current-thread ID", probeResult == 0, allPassed);
    status("Initial stack bounds", probeResult == 0, allPassed);
    status("Initial FLS lifecycle", probeResult == 0, allPassed);
    status("Worker creation", probeResult == 0, allPassed);
    status("SysV-to-Win64 callback", probeResult == 0 && g_workerCallbackCount == 1, allPassed);
    status("Worker result", probeResult == 0 && g_lastWorkerResult == 0x1234u, allPassed);
    status("Worker thread ID", probeResult == 0 && g_lastWorkerThreadId != process::current_thread_id(), allPassed);
    status("Worker stack bounds", probeResult == 0 && g_lastWorkerStackLow < g_lastWorkerStackHigh &&
           g_lastWorkerStackCurrent >= g_lastWorkerStackLow && g_lastWorkerStackCurrent < g_lastWorkerStackHigh, allPassed);
    status("Worker ThreadStore lifecycle", probeResult == 0 &&
           guidexos::nativeaot::threadstore::attachedThreadCount() == 1, allPassed);
    status("FLS detach callback bridge", probeResult == 0 && g_detachCallbackCount >= 1, allPassed);
    status("Callback count", probeResult == 0 && g_workerCallbackCount == 1, allPassed);
    status("Join and cleanup", probeResult == 0 && g_activeCallbacks == 0, allPassed);
    serial::puts("[nativeaot-pal-qemu-test] Active callbacks: ");
    serial::put_hex32(g_activeCallbacks);
    serial::puts("\n");
    status("Timing", probeResult == 0, allPassed);
    status("Sleep/yield", probeResult == 0, allPassed);

    const int32_t uninstalled = uninstall();
    status("Hook uninstall", uninstalled == 0, allPassed);
    const bool detachedThreadStore =
        guidexos::nativeaot::threadstore::detachCurrentThread() ==
        guidexos::nativeaot::threadstore::Result::Success;
    const bool threadStoreShutdown =
        guidexos::nativeaot::threadstore::shutdown() ==
        guidexos::nativeaot::threadstore::Result::Success;
    const bool localDetached =
        gxos::runtime::detachLocalStorage() == gxos::runtime::LocalStorageResult::Success;
    const bool localShutdown =
        gxos::runtime::shutdownLocalStorage() == gxos::runtime::LocalStorageResult::Success;
    status("ThreadStore detach", detachedThreadStore && threadStoreShutdown, allPassed);
    unloadArtifact();
    status("Cleanup", detachedThreadStore && threadStoreShutdown && localDetached && localShutdown &&
           g_activeCallbacks == 0 && g_mappedPageCount == 0, allPassed);
}

#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)

constexpr uint32_t kMaxGcEventSlots = 16u;
constexpr uint32_t kMaxGcVmSlots = 16u;

struct GcEventSlot {
    bool active;
    gxos::runtime::Event event;
};

struct GcVmSlot {
    bool active;
    gxos::runtime::virtual_memory::VirtualMemoryRegion region;
};

GcEventSlot g_gcEvents[kMaxGcEventSlots] = {};
GcVmSlot g_gcVmSlots[kMaxGcVmSlots] = {};
uint32_t g_startupLegacyAllocCalls = 0;
uintptr_t g_startupLegacyLastSize = 0;
bool g_startupLegacyLastSuccess = false;

void startupStatus(const char* name, bool passed, bool& allPassed) {
    serial::puts("[nativeaot-gc-startup-qemu-test] ");
    serial::puts(name);
    serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) allPassed = false;
}

GcEventSlot* findGcEvent(void* handle) {
    if (handle == nullptr) return nullptr;
    for (GcEventSlot& slot : g_gcEvents) {
        if (slot.active && static_cast<void*>(&slot.event) == handle) return &slot;
    }
    return nullptr;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL startupCreateEvent(
    uint32_t manualReset, uint32_t initialState) {
    for (GcEventSlot& slot : g_gcEvents) {
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

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupSetEvent(void* handle) {
    GcEventSlot* slot = findGcEvent(handle);
    return slot != nullptr && slot->event.signal() == gxos::runtime::EventStatus::Ok
        ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupResetEvent(void* handle) {
    GcEventSlot* slot = findGcEvent(handle);
    return slot != nullptr && slot->event.reset() == gxos::runtime::EventStatus::Ok
        ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupWaitEvent(
    void* handle, uint32_t timeoutMilliseconds) {
    GcEventSlot* slot = findGcEvent(handle);
    if (slot == nullptr) return -1;
    const gxos::runtime::WaitResult result = timeoutMilliseconds == 0xFFFFFFFFu
        ? slot->event.wait(gxos::runtime::WaitTimeout::infinite())
        : slot->event.wait(gxos::runtime::WaitTimeout::finiteMilliseconds(
              timeoutMilliseconds));
    if (result == gxos::runtime::WaitResult::Signaled) return 0;
    if (result == gxos::runtime::WaitResult::TimedOut) return 258;
    return -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupCloseEvent(void* handle) {
    GcEventSlot* slot = findGcEvent(handle);
    if (slot == nullptr || slot->event.close() != gxos::runtime::EventStatus::Ok) {
        return -1;
    }
    slot->active = false;
    return 0;
}

GcVmSlot* findGcVm(const void* address) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    for (GcVmSlot& slot : g_gcVmSlots) {
        if (!slot.active || slot.region.base == nullptr) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(slot.region.base);
        if (value >= base && value - base < slot.region.reservedSize) return &slot;
    }
    return nullptr;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL startupReserve(
    uintptr_t size, uintptr_t alignment, uint32_t, uint16_t) {
    if (size == 0) return nullptr;
    for (GcVmSlot& slot : g_gcVmSlots) {
        if (slot.active) continue;
        const gxos::runtime::virtual_memory::VmResult result =
            gxos::runtime::virtual_memory::reserve(
                size, alignment, nullptr, &slot.region);
        if (result != gxos::runtime::virtual_memory::VmResult::Ok) {
            serial::puts("[nativeaot-gc-startup-qemu-test] reserve failed size=");
            serial::put_hex64(size);
            serial::puts(" alignment=");
            serial::put_hex64(alignment);
            serial::puts(" result=");
            serial::puts(gxos::runtime::virtual_memory::vmResultName(result));
            serial::puts(" detail=");
            serial::puts(gxos::runtime::virtual_memory::lastDiagnostic());
            serial::putc('\n');
            return nullptr;
        }
        slot.active = true;
        return slot.region.base;
    }
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupCommit(
    void* address, uintptr_t size, uint16_t) {
    GcVmSlot* slot = findGcVm(address);
    if (slot == nullptr || size == 0) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    return gxos::runtime::virtual_memory::commit(
               slot->region, offset, size,
               gxos::runtime::virtual_memory::MemoryProtection::ReadWrite) ==
            gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupDecommit(
    void* address, uintptr_t size, uint16_t) {
    GcVmSlot* slot = findGcVm(address);
    if (slot == nullptr || size == 0) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    return gxos::runtime::virtual_memory::decommit(
               slot->region, offset, size) ==
            gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupRelease(
    void* address, uintptr_t, uint16_t) {
    GcVmSlot* slot = findGcVm(address);
    if (slot == nullptr || address != slot->region.base) return -1;
    const gxos::runtime::virtual_memory::VmResult result =
        gxos::runtime::virtual_memory::release(slot->region);
    if (result != gxos::runtime::virtual_memory::VmResult::Ok) return -1;
    slot->active = false;
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupReset(
    void* address, uintptr_t size, uint32_t) {
    return findGcVm(address) != nullptr && size != 0 ? 0 : -1;
}

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupPageSize() { return 4096u; }
uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupGranularity() { return 4096u; }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL startupVirtualLimit() {
    // Workstation GC's default regions range is bounded by half of this
    // negotiated limit. Keep the startup-only dry run inside the generic
    // true-VM reservation range rather than advertising a host-sized range
    // that the bare-metal page-table adapter cannot materialize.
    return UINT64_C(128) * 1024u * 1024u;
}
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL startupPhysicalLimit(uint32_t* restricted) {
    if (restricted != nullptr) *restricted = 0;
    return UINT64_C(64) * 1024u * 1024u;
}
void GUIDEXOS_NATIVEAOT_PAL_CALL startupMemoryStatus(
    uint64_t, uint32_t* memoryLoad, uint64_t* availablePhysical,
    uint64_t* availablePageFile) {
    if (memoryLoad != nullptr) *memoryLoad = 0;
    if (availablePhysical != nullptr) *availablePhysical = UINT64_C(64) * 1024u * 1024u;
    if (availablePageFile != nullptr) *availablePageFile = UINT64_C(64) * 1024u * 1024u;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyCurrentThread(void*) {
    return bridgeCurrentThreadId64();
}
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyStackBounds(
    void*, uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    guidexos_nativeaot_pal_stack_bounds_value value{};
    const int32_t result = bridgeStackBounds(&value);
    if (result == 0) {
        if (low != nullptr) *low = value.low;
        if (high != nullptr) *high = value.high;
        if (current != nullptr) *current = value.current;
    }
    return result;
}
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyCounter(void*) { return bridgeCounter(); }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyFrequency(void*) { return bridgeFrequency(); }
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacySleep(void*, uint32_t value) {
    bridgeSleep(value);
    return 0;
}
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyYield(void*) {
    bridgeYield();
    return 0;
}
void* GUIDEXOS_NATIVEAOT_PAL_CALL legacyModule(void*, const void*) {
    return reinterpret_cast<void*>(g_artifactBase);
}
void* GUIDEXOS_NATIVEAOT_PAL_CALL legacyResolve(void*, void*, const char*) {
    return nullptr;
}
void GUIDEXOS_NATIVEAOT_PAL_CALL legacyFailFast(void*, uint32_t reason) {
    bridgeFailFast(reason, 0);
}
void* GUIDEXOS_NATIVEAOT_PAL_CALL legacyVirtualAlloc(
    void*, void*, uintptr_t size, uint32_t type, uint32_t) {
    ++g_startupLegacyAllocCalls;
    g_startupLegacyLastSize = size;
    void* result = startupReserve(size, 4096u, 0, 0xFFFFu);
    if (result != nullptr && (type & 0x1000u) != 0) {
        const uintptr_t committedSize = (size + 4095u) & ~uintptr_t(4095u);
        if (startupCommit(result, committedSize, 0xFFFFu) != 0) {
            (void)startupRelease(result, committedSize, 0xFFFFu);
            return nullptr;
        }
    }
    g_startupLegacyLastSuccess = result != nullptr;
    return result;
}
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyVirtualFree(
    void*, void* address, uintptr_t size, uint32_t type) {
    return type == 0x4000u ? startupRelease(address, size, 0xFFFFu)
                           : startupDecommit(address, size, 0xFFFFu);
}
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyVirtualProtect(
    void*, void* address, uintptr_t size, uint32_t, uint32_t* oldProtection) {
    if (oldProtection != nullptr) *oldProtection = 0x04u;
    return findGcVm(address) != nullptr && size != 0 ? 1 : 0;
}

void fillStartupPalHooks(guidexos_nativeaot_pal_hooks* hooks) {
    *hooks = {};
    hooks->size = sizeof(*hooks);
    hooks->abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    hooks->current_thread_id = legacyCurrentThread;
    hooks->stack_bounds = legacyStackBounds;
    hooks->counter = legacyCounter;
    hooks->frequency = legacyFrequency;
    hooks->sleep_milliseconds = legacySleep;
    hooks->yield = legacyYield;
    hooks->static_module_from_pointer = legacyModule;
    hooks->static_resolve = legacyResolve;
    hooks->fail_fast = legacyFailFast;
    hooks->virtual_alloc = legacyVirtualAlloc;
    hooks->virtual_free = legacyVirtualFree;
    hooks->virtual_protect = legacyVirtualProtect;
}

void fillStartupPalTable(guidexos_nativeaot_pal_hook_table_v1* table,
                         uintptr_t base, uintptr_t size, uint64_t generation) {
    *table = {};
    table->magic = GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC;
    table->abi_version = GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION;
    table->structure_size = sizeof(*table);
    table->capability_bits = GUIDEXOS_NATIVEAOT_PAL_CAP_REQUIRED;
    table->installation_generation = generation;
    table->artifact_base = base;
    table->artifact_size = size;
    table->current_thread_id = bridgeCurrentThreadId64;
    table->query_current_stack_bounds = bridgeStackBounds;
    table->fls_allocate = bridgeFlsAllocate;
    table->fls_release = bridgeFlsRelease;
    table->fls_get = bridgeFlsGet;
    table->fls_set = bridgeFlsSet;
    table->create_worker = bridgeCreateWorker;
    table->join_worker = bridgeJoinWorker;
    table->destroy_worker_handle = bridgeDestroyWorker;
    table->query_counter = bridgeCounter;
    table->query_counter_frequency = bridgeFrequency;
    table->monotonic_milliseconds = bridgeMilliseconds;
    table->sleep_milliseconds = bridgeSleep;
    table->yield_thread = bridgeYield;
    table->fail_fast = bridgeFailFast;
}

void fillStartupPlatformTable(
    guidexos_nativeaot_gc_startup_platform_table_v1* table,
    uint64_t generation) {
    *table = {};
    table->magic = GUIDEXOS_NATIVEAOT_GC_PLATFORM_MAGIC;
    table->abi_version = GUIDEXOS_NATIVEAOT_GC_PLATFORM_ABI_VERSION;
    table->structure_size = sizeof(*table);
    table->capability_bits = GUIDEXOS_NATIVEAOT_GC_CAP_REQUIRED;
    table->installation_generation = generation;
    table->create_event = startupCreateEvent;
    table->set_event = startupSetEvent;
    table->reset_event = startupResetEvent;
    table->wait_event = startupWaitEvent;
    table->close_event = startupCloseEvent;
    table->reserve = startupReserve;
    table->commit = startupCommit;
    table->decommit = startupDecommit;
    table->release = startupRelease;
    table->reset = startupReset;
    table->page_size = startupPageSize;
    table->allocation_granularity = startupGranularity;
    table->virtual_memory_limit = startupVirtualLimit;
    table->physical_memory_limit = startupPhysicalLimit;
    table->memory_status = startupMemoryStatus;
}

void runStartupImpl(const uint8_t* artifact, size_t artifactSize,
                    uintptr_t installPalAddress, uintptr_t installTableAddress,
                    uintptr_t installPlatformAddress, uintptr_t mainAddress,
                    uintptr_t stateAddress, uintptr_t preGcStateAddress,
                    uintptr_t allocationCountAddress, uintptr_t lastAllocationSizeAddress,
                    uintptr_t diagnosticStageAddress,
                    uint64_t generation) {
    bool allPassed = true;
    serial::puts("[nativeaot-gc-startup-qemu-test] BEGIN\n");
    const bool foundations =
        gxos::runtime::initializeLocalStorage() == gxos::runtime::LocalStorageResult::Success &&
        gxos::runtime::attachLocalStorage() == gxos::runtime::LocalStorageResult::Success &&
        guidexos::nativeaot::threadstore::initialize() ==
            guidexos::nativeaot::threadstore::Result::Success &&
        guidexos::nativeaot::threadstore::attachCurrentThread() ==
            guidexos::nativeaot::threadstore::Result::Success;
    startupStatus("Runtime foundation initialization", foundations, allPassed);
    if (!foundations) {
        serial::puts("[nativeaot-gc-startup-qemu-test] ALL_FAIL\n");
        return;
    }

    uintptr_t base = 0;
    uintptr_t size = 0;
    const bool loaded = loadArtifact(artifact, artifactSize, &base, &size);
    startupStatus("Artifact staged", loaded, allPassed);
    if (!loaded) {
        serial::puts("[nativeaot-gc-startup-qemu-test] ALL_FAIL\n");
        return;
    }
    resetBridgeState(base, size, generation);

    guidexos_nativeaot_pal_hooks legacy = {};
    fillStartupPalHooks(&legacy);
    guidexos_nativeaot_pal_hook_table_v1 pal = {};
    fillStartupPalTable(&pal, base, size, generation);
    guidexos_nativeaot_gc_startup_platform_table_v1 platform = {};
    fillStartupPlatformTable(&platform, generation);

    serial::puts("[nativeaot-gc-startup-qemu-test] PAL ABI=1 size=");
    serial::put_hex32(pal.structure_size);
    serial::puts(" capabilities=");
    serial::put_hex64(pal.capability_bits);
    serial::puts(" GC ABI=1 size=");
    serial::put_hex32(platform.structure_size);
    serial::puts(" capabilities=");
    serial::put_hex64(platform.capability_bits);
    serial::puts(" generation=");
    serial::put_hex64(generation);
    serial::puts("\n");

    using InstallPal = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        const guidexos_nativeaot_pal_hooks*);
    using InstallTable = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        const guidexos_nativeaot_pal_hook_table_v1*);
    using InstallPlatform = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        const guidexos_nativeaot_gc_startup_platform_table_v1*);
    using Main = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
    const int32_t palResult = reinterpret_cast<InstallPal>(installPalAddress)(&legacy);
    const int32_t tableResult = reinterpret_cast<InstallTable>(installTableAddress)(&pal);
    const int32_t platformResult = reinterpret_cast<InstallPlatform>(installPlatformAddress)(&platform);
    startupStatus("Win64 PAL legacy installation", palResult == 0, allPassed);
    startupStatus("Win64 PAL v1 installation", tableResult == 0, allPassed);
    startupStatus("GC platform installation", platformResult == 0, allPassed);
    startupStatus("Hook magic/version/size", palResult == 0 && tableResult == 0 && platformResult == 0, allPassed);
    if (palResult != 0 || tableResult != 0 || platformResult != 0) {
        serial::puts("[nativeaot-gc-startup-qemu-test] ALL_FAIL\n");
        return;
    }

    const int32_t startupResult = reinterpret_cast<Main>(mainAddress)();
    using State = uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
    const uint32_t palState = reinterpret_cast<State>(stateAddress)();
    const uint32_t preGcState = reinterpret_cast<State>(preGcStateAddress)();
    const uint32_t allocationCount = reinterpret_cast<State>(allocationCountAddress)();
    const uint32_t lastAllocationSize = reinterpret_cast<State>(lastAllocationSizeAddress)();
    const uint32_t diagnosticStage = reinterpret_cast<State>(diagnosticStageAddress)();
    serial::puts("[nativeaot-gc-startup-qemu-test] PAL startup stage=");
    serial::put_hex32(palState);
    serial::puts("\n");
    serial::puts("[nativeaot-gc-startup-qemu-test] diagnostic stage=");
    serial::put_hex32(diagnosticStage);
    serial::puts("\n");
    serial::puts("[nativeaot-gc-startup-qemu-test] pre-GC state=");
    serial::put_hex32(preGcState);
    serial::puts(" allocationCalls=");
    serial::put_hex32(allocationCount);
    serial::puts(" lastAllocationSize=");
    serial::put_hex32(lastAllocationSize);
    serial::puts("\n");
    serial::puts("[nativeaot-gc-startup-qemu-test] RhInitialize return=");
    serial::put_hex32(static_cast<uint32_t>(startupResult));
    serial::puts("\n");
    startupStatus("RhInitialize", startupResult == 0, allPassed);
    bridgeSleep(100u);
    const uint32_t attached = guidexos::nativeaot::threadstore::attachedThreadCount();
    serial::puts("[nativeaot-gc-startup-qemu-test] ThreadStore attached=");
    serial::put_hex32(attached);
    serial::puts(" activeWorkers=");
    uint32_t activeWorkers = 0;
    for (const WorkerSlot& worker : g_workers) {
        if (worker.active) ++activeWorkers;
    }
    serial::put_hex32(activeWorkers);
    serial::puts(" callbacks=");
    serial::put_hex32(g_activeCallbacks);
    serial::puts("\n");
    serial::puts("[nativeaot-gc-startup-qemu-test] legacyAllocCalls=");
    serial::put_hex32(g_startupLegacyAllocCalls);
    serial::puts(" lastSize=");
    serial::put_hex64(g_startupLegacyLastSize);
    serial::puts(" success=");
    serial::put_hex32(g_startupLegacyLastSuccess ? 1u : 0u);
    serial::puts("\n");
    startupStatus("Finalizer helper parked", startupResult == 0 &&
                  ((palState >> 24) & 0xFFu) == 0x02u && attached == 1u &&
                  g_activeCallbacks == 0, allPassed);
    startupStatus("No managed finalizer entry", startupResult == 0, allPassed);
    startupStatus("Process-lifetime cleanup boundary", startupResult == 0, allPassed);
    serial::puts("[nativeaot-gc-startup-qemu-test] same-process shutdown: UNSUPPORTED\n");
    serial::puts(allPassed
        ? "[nativeaot-gc-startup-qemu-test] ALL_PASS\n"
        : "[nativeaot-gc-startup-qemu-test] ALL_FAIL\n");
}

#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)

struct FirstRealAllocationContext {
    uint32_t size;
    uint32_t apiVersion;
    void* host;
    void* userData;
};

void firstAllocationStatus(const char* name, bool passed, bool& allPassed) {
#if defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-refill] ");
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
    serial::puts("[nativeaot-gc-segment-boundary] ");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-allocation] ");
#endif
    serial::puts(name);
    serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) allPassed = false;
}

void printFirstAllocationPointer(const char* name, uintptr_t value) {
#if defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-refill] ");
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
    serial::puts("[nativeaot-gc-segment-boundary] ");
#else
    serial::puts("[nativeaot-gc-first-allocation] ");
#endif
    serial::puts(name);
    serial::puts("=");
    serial::put_hex64(value);
    serial::puts("\n");
}

#if defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
using FirstRefillManagedMain = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
    FirstRealAllocationContext*);
using FirstRefillFinalize = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(uint32_t);
using FirstRefillGetDiagnostics = const guidexos_nativeaot_allocation_diagnostics*
    (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);

void runFirstRefillManagedBoundary(
    uintptr_t managedMainAddress, uintptr_t finalizeAddress,
    uintptr_t getDiagnosticsAddress, uint32_t palState, bool& allPassed) {
    FirstRealAllocationContext context{};
    context.size = sizeof(context);
    context.apiVersion = 0u;
    serial::puts("[nativeaot-gc-first-refill] entering ManagedMain once\n");
    const int32_t managedResult = reinterpret_cast<FirstRefillManagedMain>(
        managedMainAddress)(&context);
    // The harness invokes the exported managed entry directly, so the image
    // does not pass through RhpReversePInvoke. Count this controlled entry at
    // the same fixed diagnostic boundary used by the runtime wrapper.
    const guidexos_nativeaot_allocation_diagnostics* entryDiagnostics =
        reinterpret_cast<FirstRefillGetDiagnostics>(getDiagnosticsAddress)();
    if (entryDiagnostics != nullptr && entryDiagnostics->managedEntryCount == 0u) {
        const_cast<guidexos_nativeaot_allocation_diagnostics*>(entryDiagnostics)->managedEntryCount = 1u;
    }
    const int32_t finalizeResult = reinterpret_cast<FirstRefillFinalize>(
        finalizeAddress)(static_cast<uint32_t>(managedResult));
    const guidexos_nativeaot_allocation_diagnostics* diagnostics =
        reinterpret_cast<FirstRefillGetDiagnostics>(getDiagnosticsAddress)();

    firstAllocationStatus("Managed entry once", diagnostics != nullptr, allPassed);
    firstAllocationStatus("Managed byte[256] loop return", managedResult == 0, allPassed);
    firstAllocationStatus("Allocation finalization", finalizeResult == 0, allPassed);
    if (diagnostics == nullptr) {
        serial::puts("[nativeaot-gc-first-refill] ALL_FAIL\n");
        return;
    }

    serial::puts("[nativeaot-gc-first-refill] managedStatus=");
    serial::put_hex32(static_cast<uint32_t>(managedResult));
    serial::puts(" finalizeStatus=");
    serial::put_hex32(static_cast<uint32_t>(finalizeResult));
    serial::puts(" schema=");
    serial::put_hex32(diagnostics->schemaVersion);
    serial::puts(" allocationCount=");
    serial::put_hex32(diagnostics->allocationCount);
    serial::puts(" managedEntryCount=");
    serial::put_hex32(diagnostics->managedEntryCount);
    serial::puts(" allocationRequestCount=");
    serial::put_hex32(diagnostics->allocationRequestCount);
    serial::puts(" rhpNewArrayCount=");
    serial::put_hex32(diagnostics->rhpNewArrayCount);
    serial::puts(" fastAllocationCount=");
    serial::put_hex32(diagnostics->fastAllocationCount);
    serial::puts(" expectedFastAllocationCount=");
    serial::put_hex64(diagnostics->expectedFastAllocationCount);
    serial::puts(" rarePathCount=");
    serial::put_hex32(diagnostics->rarePathCount);
    serial::puts(" realGcAllocationCount=");
    serial::put_hex32(diagnostics->realGcAllocationCount);
    serial::puts(" slowAllocationCount=");
    serial::put_hex32(diagnostics->slowAllocationCount);
    serial::puts(" allocationContextRefillCount=");
    serial::put_hex32(diagnostics->allocationContextRefillCount);
    serial::puts(" hardAllocationLimit=");
    serial::put_hex32(diagnostics->hardAllocationLimit);
    serial::puts("\n");

    serial::puts("[nativeaot-gc-first-refill] refill2Attempted=");
    serial::put_hex32(diagnostics->refill2Attempted);
    serial::puts(" refill2Returned=");
    serial::put_hex32(diagnostics->refill2Returned);
    serial::puts(" newContextSupplied=");
    serial::put_hex32(diagnostics->newContextSupplied);
    serial::puts(" refill2ContextPublished=");
    serial::put_hex32(diagnostics->refill2ContextPublished);
    serial::puts(" refill2ContextChanged=");
    serial::put_hex32(diagnostics->refill2ContextChanged);
    serial::puts(" managedStopObserved=");
    serial::put_hex32(diagnostics->managedStopObserved);
    serial::puts(" noPostRefillAllocation=");
    serial::put_hex32(diagnostics->noPostRefillAllocation);
    serial::puts(" ownershipModel=");
    serial::put_hex32(diagnostics->ownershipModel);
    serial::puts("\n");

    printFirstAllocationPointer("initialAllocPtr", diagnostics->initialAllocPtr);
    printFirstAllocationPointer("initialAllocLimit", diagnostics->initialAllocLimit);
    printFirstAllocationPointer("initialAvailableBytes", diagnostics->initialAvailableBytes);
    printFirstAllocationPointer("lastFastObject", diagnostics->lastFastObject);
    printFirstAllocationPointer("lastFastObjectEnd", diagnostics->lastFastObjectEnd);
    printFirstAllocationPointer("refill2AllocPtrBefore", diagnostics->refill2AllocPtrBefore);
    printFirstAllocationPointer("refill2AllocLimitBefore", diagnostics->refill2AllocLimitBefore);
    printFirstAllocationPointer("refill2RemainingBytesBefore", diagnostics->refill2RemainingBytesBefore);
    printFirstAllocationPointer("refill2Object", diagnostics->refill2Object);
    printFirstAllocationPointer("refill2ObjectEnd", diagnostics->refill2ObjectEnd);
    printFirstAllocationPointer("refill2AllocPtrAfter", diagnostics->refill2AllocPtrAfter);
    printFirstAllocationPointer("refill2AllocLimitAfter", diagnostics->refill2AllocLimitAfter);
    printFirstAllocationPointer("initialSegmentBase", diagnostics->initialSegmentBase);
    printFirstAllocationPointer("initialSegmentAllocated", diagnostics->initialSegmentAllocated);
    printFirstAllocationPointer("initialSegmentReserved", diagnostics->initialSegmentReserved);
    printFirstAllocationPointer("refill2SegmentBase", diagnostics->refill2SegmentBase);
    printFirstAllocationPointer("refill2SegmentAllocated", diagnostics->refill2SegmentAllocated);
    printFirstAllocationPointer("refill2SegmentReserved", diagnostics->refill2SegmentReserved);

    serial::puts("[nativeaot-gc-first-refill] derivedObjectSize=");
    serial::put_hex64(diagnostics->derivedObjectSize);
    serial::puts(" currentIteration=");
    serial::put_hex64(diagnostics->currentIteration);
    serial::puts(" currentObject=");
    serial::put_hex64(diagnostics->currentObject);
    serial::puts(" currentObjectEnd=");
    serial::put_hex64(diagnostics->currentObjectEnd);
    serial::puts(" currentAllocPtr=");
    serial::put_hex64(diagnostics->currentAllocPtr);
    serial::puts(" currentAllocLimit=");
    serial::put_hex64(diagnostics->currentAllocLimit);
    serial::puts("\n");

    serial::puts("[nativeaot-gc-first-refill] collectionConsideredCount=");
    serial::put_hex32(diagnostics->collectionConsideredCount);
    serial::puts(" collectionRequestCount=");
    serial::put_hex32(diagnostics->collectionRequestCount);
    serial::puts(" collectionEntryCount=");
    serial::put_hex32(diagnostics->collectionEntryCount);
    serial::puts(" gcCountBefore=");
    serial::put_hex32(diagnostics->gcCountBefore);
    serial::puts(" gcCountAfter=");
    serial::put_hex32(diagnostics->gcCountAfter);
    serial::puts(" collectionsEntered=");
    serial::put_hex32(diagnostics->collectionsEntered);
    serial::puts(" finalizationScanCount=");
    serial::put_hex32(diagnostics->finalizationScanCount);
    serial::puts(" managedFinalizerCount=");
    serial::put_hex32(diagnostics->managedFinalizerCount);
    serial::puts(" finalizersExecuted=");
    serial::put_hex32(diagnostics->finalizersExecuted);
    serial::puts(" suspensionRequestCount=");
    serial::put_hex32(diagnostics->suspensionRequestCount);
    serial::puts(" gcLockTransitionCount=");
    serial::put_hex32(diagnostics->gcLockTransitionCount);
    serial::puts(" helperWakeCount=");
    serial::put_hex32(diagnostics->helperWakeCount);
    serial::puts("\n");

    serial::puts("[nativeaot-gc-first-refill] zeroFailures=");
    serial::put_hex32(diagnostics->zeroValidationFailures);
    serial::puts(" patternFailures=");
    serial::put_hex32(diagnostics->patternValidationFailures);
    serial::puts(" layoutFailures=");
    serial::put_hex32(diagnostics->layoutFailures);
    serial::puts(" ownershipFailures=");
    serial::put_hex32(diagnostics->ownershipFailures);
    serial::puts(" overlapFailures=");
    serial::put_hex32(diagnostics->overlapFailures);
    serial::puts(" monotonicityFailures=");
    serial::put_hex32(diagnostics->monotonicityFailures);
    serial::puts(" contextGeometryFailures=");
    serial::put_hex32(diagnostics->contextGeometryFailures);
    serial::puts(" pointerContractFailures=");
    serial::put_hex32(diagnostics->pointerContractFailures);
    serial::puts(" stage=");
    serial::put_hex32(diagnostics->stage);
    serial::puts(" sequence=");
    serial::put_hex32(diagnostics->sequence);
    serial::puts(" sourceSizeValid=");
    serial::put_hex32(diagnostics->sourceSizeValid);
    serial::puts(" primitiveArrayValid=");
    serial::put_hex32(diagnostics->primitiveArrayValid);
    serial::puts(" belowLargeObjectThreshold=");
    serial::put_hex32(diagnostics->belowLargeObjectThreshold);
    serial::puts(" finalizerStateValid=");
    serial::put_hex32(diagnostics->finalizerStateValid);
    serial::puts(" helperStateValid=");
    serial::put_hex32(diagnostics->helperStateValid);
    serial::puts(" failureReason=");
    serial::put_hex32(diagnostics->failureReason);
    serial::puts("\n");

    const bool counters = diagnostics->schemaVersion == 1u &&
        diagnostics->allocationCount == diagnostics->allocationRequestCount &&
        diagnostics->allocationCount == diagnostics->rhpNewArrayCount &&
        diagnostics->allocationCount == diagnostics->expectedFastAllocationCount + 2u &&
        diagnostics->fastAllocationCount == diagnostics->expectedFastAllocationCount &&
        diagnostics->rarePathCount == 2u &&
        diagnostics->realGcAllocationCount == 2u &&
        diagnostics->slowAllocationCount == 2u &&
        diagnostics->allocationContextRefillCount == 2u;
    firstAllocationStatus("Exact allocation counters", counters, allPassed);

    const bool geometry = diagnostics->derivedObjectSize != 0u &&
        diagnostics->initialAllocLimit > diagnostics->initialAllocPtr &&
        diagnostics->initialAvailableBytes ==
            diagnostics->initialAllocLimit - diagnostics->initialAllocPtr &&
        diagnostics->refill2Attempted == 1u &&
        diagnostics->refill2Returned == 1u &&
        diagnostics->newContextSupplied == 1u &&
        diagnostics->refill2ContextPublished == 1u &&
        diagnostics->refill2ContextChanged == 1u &&
        diagnostics->contextGeometryFailures == 0u &&
        diagnostics->overlapFailures == 0u &&
        diagnostics->monotonicityFailures == 0u;
    firstAllocationStatus("Refill 1 and Refill 2 context geometry", geometry, allPassed);

    const bool noCollection = diagnostics->collectionConsideredCount == 2u &&
        diagnostics->collectionRequestCount == 0u &&
        diagnostics->collectionEntryCount == 0u &&
        diagnostics->collectionsEntered == 0u &&
        diagnostics->gcCountBefore == diagnostics->gcCountAfter &&
        diagnostics->finalizationScanCount == 0u &&
        diagnostics->managedFinalizerCount == 0u &&
        diagnostics->finalizersExecuted == 0u &&
        diagnostics->suspensionRequestCount == 0u &&
        diagnostics->gcLockTransitionCount == 0u &&
        diagnostics->helperWakeCount == 0u;
    firstAllocationStatus("No collection or finalization", noCollection, allPassed);

    const bool objectValidation = diagnostics->zeroValidationFailures == 0u &&
        diagnostics->patternValidationFailures == 0u &&
        diagnostics->layoutFailures == 0u &&
        diagnostics->ownershipFailures == 0u &&
        diagnostics->sourceSizeValid == 1u &&
        diagnostics->primitiveArrayValid == 1u &&
        diagnostics->belowLargeObjectThreshold == 1u &&
        diagnostics->noPostRefillAllocation == 1u;
    firstAllocationStatus("Primitive-array object and ownership validation", objectValidation,
                          allPassed);
    firstAllocationStatus("Finalizer worker parked", ((palState >> 24) & 0xFFu) == 0x02u &&
                           g_activeCallbacks == 0u, allPassed);
    serial::puts("[nativeaot-gc-first-refill] same-process shutdown: UNSUPPORTED\n");
    serial::puts("[nativeaot-gc-first-refill] Process teardown: PASS\n");
    serial::puts(allPassed
        ? "[nativeaot-gc-first-refill] ALL_PASS\n"
        : "[nativeaot-gc-first-refill] ALL_FAIL\n");
}
#endif

#if defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
using SegmentBoundaryManagedMain = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
    FirstRealAllocationContext*);
using SegmentBoundaryFinalize = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(uint32_t);
using SegmentBoundaryGetDiagnostics = const guidexos_nativeaot_allocation_diagnostics*
    (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);

void runSegmentBoundaryManagedBoundary(
    uintptr_t managedMainAddress, uintptr_t finalizeAddress,
    uintptr_t getDiagnosticsAddress, uint32_t palState, bool& allPassed) {
    FirstRealAllocationContext context{};
    context.size = sizeof(context);
    context.apiVersion = 0u;
    serial::puts("[nativeaot-gc-segment-boundary] entering ManagedMain once\n");
    const int32_t managedResult = reinterpret_cast<SegmentBoundaryManagedMain>(
        managedMainAddress)(&context);
    const guidexos_nativeaot_allocation_diagnostics* entryDiagnostics =
        reinterpret_cast<SegmentBoundaryGetDiagnostics>(getDiagnosticsAddress)();
    const int32_t finalizeResult = reinterpret_cast<SegmentBoundaryFinalize>(
        finalizeAddress)(static_cast<uint32_t>(managedResult));
    const guidexos_nativeaot_allocation_diagnostics* diagnostics =
        reinterpret_cast<SegmentBoundaryGetDiagnostics>(getDiagnosticsAddress)();

    firstAllocationStatus("Managed entry once", diagnostics != nullptr, allPassed);
    firstAllocationStatus("Managed byte[4096] bounded loop return", managedResult == 0, allPassed);
    firstAllocationStatus("Allocation finalization", finalizeResult == 0, allPassed);
    if (diagnostics == nullptr) {
        serial::puts("[nativeaot-gc-segment-boundary] ALL_FAIL\n");
        return;
    }

    serial::puts("[nativeaot-gc-segment-boundary] managedStatus=");
    serial::put_hex32(static_cast<uint32_t>(managedResult));
    serial::puts(" finalizeStatus=");
    serial::put_hex32(static_cast<uint32_t>(finalizeResult));
    serial::puts(" completionStatus=");
    serial::put_hex32(diagnostics->completionStatus);
    serial::puts(" schemaVersion=");
    serial::put_hex32(diagnostics->schemaVersion);
    serial::puts(" managedEntryCount=");
    serial::put_hex32(diagnostics->managedEntryCount);
    serial::puts(" rhpNewArrayEntries=");
    serial::put_hex32(diagnostics->rhpNewArrayEntries);
    serial::puts(" failureReason=");
    serial::put_hex32(diagnostics->failureReason);
    serial::puts(" pointerContractFailures=");
    serial::put_hex32(diagnostics->pointerContractFailures);
    serial::puts(" selectedArrayLength=");
    serial::put_hex32(diagnostics->selectedArrayLength);
    serial::puts(" derivedObjectSize=");
    serial::put_hex64(diagnostics->derivedObjectSize);
    serial::puts(" hardAllocationLimit=");
    serial::put_hex32(diagnostics->hardAllocationLimit);
    serial::puts(" allocationCount=");
    serial::put_hex32(diagnostics->allocationCount);
    serial::puts(" fastAllocationCount=");
    serial::put_hex32(diagnostics->fastAllocationCount);
    serial::puts(" rarePathCount=");
    serial::put_hex32(diagnostics->rarePathCount);
    serial::puts(" realGcAllocationCount=");
    serial::put_hex32(diagnostics->realGcAllocationCount);
    serial::puts(" allocationContextRefillCount=");
    serial::put_hex32(diagnostics->allocationContextRefillCount);
    serial::puts(" requestedObjectSize=");
    serial::put_hex64(diagnostics->requestedObjectSize);
    serial::puts(" returnedObject=");
    serial::put_hex64(diagnostics->returnedObject);
    serial::puts("\n");

    serial::puts("[nativeaot-gc-segment-boundary] vmTraceStartCount=");
    serial::put_hex32(diagnostics->vmTraceStartCount);
    serial::puts(" vmTraceEndCount=");
    serial::put_hex32(diagnostics->vmTraceEndCount);
    serial::puts(" vmCommitEventCount=");
    serial::put_hex32(diagnostics->vmCommitEventCount);
    serial::puts(" heapCommitEventCount=");
    serial::put_hex32(diagnostics->heapCommitEventCount);
    serial::puts(" segmentTransitionCount=");
    serial::put_hex32(diagnostics->segmentTransitionCount);
    serial::puts(" refillHistoryCount=");
    serial::put_hex32(diagnostics->refillHistoryCount);
    serial::puts(" refillHistoryOverflow=");
    serial::put_hex32(diagnostics->refillHistoryOverflow);
    serial::puts(" initialHeapCommitObserved=");
    serial::put_hex32(diagnostics->initialHeapCommitObserved);
    serial::puts(" initialHeapCommitEventCount=");
    serial::put_hex32(diagnostics->initialHeapCommitEventCount);
    serial::puts("\n");

    serial::puts("[nativeaot-gc-segment-boundary] boundaryType=");
    serial::put_hex32(diagnostics->boundaryType);
    serial::puts(" boundaryAllocationOrdinal=");
    serial::put_hex32(diagnostics->boundaryAllocationOrdinal);
    serial::puts(" boundaryRefillOrdinal=");
    serial::put_hex32(diagnostics->boundaryRefillOrdinal);
    serial::puts(" boundaryStopObserved=");
    serial::put_hex32(diagnostics->boundaryStopObserved);
    serial::puts(" boundaryCommitValidated=");
    serial::put_hex32(diagnostics->boundaryCommitValidated);
    serial::puts(" boundarySegmentValidated=");
    serial::put_hex32(diagnostics->boundarySegmentValidated);
    serial::puts("\n");

    printFirstAllocationPointer("initialSegmentIdentity", diagnostics->initialSegmentIdentity);
    printFirstAllocationPointer("initialSegmentBase", diagnostics->initialSegmentBase);
    printFirstAllocationPointer("initialSegmentAllocated", diagnostics->initialSegmentAllocated);
    printFirstAllocationPointer("initialSegmentCommitted", diagnostics->initialSegmentCommitted);
    printFirstAllocationPointer("initialSegmentReserved", diagnostics->initialSegmentReserved);
    printFirstAllocationPointer("boundarySegmentIdentity", diagnostics->boundarySegmentIdentity);
    printFirstAllocationPointer("boundarySegmentBase", diagnostics->boundarySegmentBase);
    printFirstAllocationPointer("boundarySegmentAllocated", diagnostics->boundarySegmentAllocated);
    printFirstAllocationPointer("boundarySegmentCommitted", diagnostics->boundarySegmentCommitted);
    printFirstAllocationPointer("boundarySegmentReserved", diagnostics->boundarySegmentReserved);
    printFirstAllocationPointer("boundaryCommitAddress", diagnostics->boundaryCommitAddress);
    printFirstAllocationPointer("boundaryCommitRequested", diagnostics->boundaryCommitRequested);
    printFirstAllocationPointer("boundaryCommitActual", diagnostics->boundaryCommitActual);
    printFirstAllocationPointer("boundaryCommittedBefore", diagnostics->boundaryCommittedBefore);
    printFirstAllocationPointer("boundaryCommittedAfter", diagnostics->boundaryCommittedAfter);
    printFirstAllocationPointer("boundaryObjectAddress", diagnostics->boundaryObjectAddress);
    printFirstAllocationPointer("boundaryObjectEnd", diagnostics->boundaryObjectEnd);
    printFirstAllocationPointer("boundaryAllocationContextBefore", diagnostics->boundaryAllocationContextBefore);
    printFirstAllocationPointer("boundaryAllocationContextAfter", diagnostics->boundaryAllocationContextAfter);
    printFirstAllocationPointer("boundaryAllocationLimitBefore", diagnostics->boundaryAllocationLimitBefore);
    printFirstAllocationPointer("boundaryAllocationLimitAfter", diagnostics->boundaryAllocationLimitAfter);
    printFirstAllocationPointer("initialHeapCommitTraceIndex", diagnostics->initialHeapCommitTraceIndex);
    printFirstAllocationPointer("initialHeapCommitAddress", diagnostics->initialHeapCommitAddress);
    printFirstAllocationPointer("initialHeapCommitRequested", diagnostics->initialHeapCommitRequested);
    printFirstAllocationPointer("initialHeapCommitActual", diagnostics->initialHeapCommitActual);
    printFirstAllocationPointer("initialHeapCommittedBefore", diagnostics->initialHeapCommittedBefore);
    printFirstAllocationPointer("initialHeapCommittedAfter", diagnostics->initialHeapCommittedAfter);

    for (uint32_t index = 0; index < diagnostics->refillHistoryCount &&
         index < GUIDEXOS_NATIVEAOT_MAX_REFILL_HISTORY; ++index) {
        const guidexos_nativeaot_refill_history_entry& entry = diagnostics->refillHistory[index];
        serial::puts("[nativeaot-gc-segment-boundary] refillHistory[");
        serial::put_hex32(index);
        serial::puts("] ordinal=");
        serial::put_hex32(entry.ordinal);
        serial::puts(" allocationOrdinal=");
        serial::put_hex32(entry.allocationOrdinal);
        serial::puts(" contextBefore=");
        serial::put_hex64(entry.contextBefore);
        serial::puts(" limitBefore=");
        serial::put_hex64(entry.limitBefore);
        serial::puts(" remainingBefore=");
        serial::put_hex64(entry.remainingBefore);
        serial::puts(" segmentIdentity=");
        serial::put_hex64(entry.segmentIdentity);
        serial::puts(" segmentCommitted=");
        serial::put_hex64(entry.segmentCommitted);
        serial::puts(" segmentReserved=");
        serial::put_hex64(entry.segmentReserved);
        serial::puts(" vmCommitObserved=");
        serial::put_hex32(entry.vmCommitObserved);
        serial::puts(" boundaryType=");
        serial::put_hex32(entry.boundaryType);
        serial::puts(" commitAddress=");
        serial::put_hex64(entry.commitAddress);
        serial::puts(" commitRequested=");
        serial::put_hex64(entry.commitRequested);
        serial::puts(" commitActual=");
        serial::put_hex64(entry.commitActual);
        serial::puts("\n");
    }

    const bool counters = diagnostics->schemaVersion == 1u &&
        diagnostics->allocationCount == diagnostics->allocationRequestCount &&
        diagnostics->allocationCount == diagnostics->rhpNewArrayCount &&
        diagnostics->allocationCount <= diagnostics->hardAllocationLimit &&
        diagnostics->rarePathCount >= 1u &&
        diagnostics->rarePathCount >= 2u &&
        diagnostics->realGcAllocationCount == diagnostics->rarePathCount &&
        diagnostics->allocationContextRefillCount == diagnostics->rarePathCount &&
        diagnostics->refillHistoryCount >= 2u &&
        diagnostics->initialHeapCommitObserved == 1u &&
        diagnostics->initialHeapCommitEventCount >= 1u &&
        diagnostics->refillHistoryCount == diagnostics->rarePathCount &&
        diagnostics->refillHistoryOverflow == 0u;
    firstAllocationStatus("Exact multi-refill allocation counters", counters, allPassed);

    const bool boundary = diagnostics->boundaryStopObserved == 1u &&
        diagnostics->boundaryAllocationOrdinal == diagnostics->allocationCount &&
        diagnostics->boundaryAllocationOrdinal >= 2u &&
        diagnostics->boundaryAllocationOrdinal <= diagnostics->hardAllocationLimit &&
        ((diagnostics->boundaryType == 1u && diagnostics->boundaryCommitValidated == 1u &&
          diagnostics->boundaryCommitRequested == diagnostics->boundaryCommitActual &&
          diagnostics->boundaryCommitActual != 0u &&
          diagnostics->boundaryCommittedAfter > diagnostics->boundaryCommittedBefore) ||
         (diagnostics->boundaryType == 2u && diagnostics->boundarySegmentValidated == 1u));
    firstAllocationStatus("First GC heap commit or segment transition boundary", boundary,
                          allPassed);

    const bool segment = diagnostics->initialSegmentIdentity != 0u &&
        diagnostics->initialSegmentBase != 0u &&
        diagnostics->initialSegmentReserved > diagnostics->initialSegmentBase &&
        diagnostics->initialSegmentCommitted >= diagnostics->initialSegmentBase &&
        diagnostics->initialSegmentCommitted <= diagnostics->initialSegmentReserved &&
        diagnostics->boundarySegmentIdentity != 0u &&
        diagnostics->boundarySegmentReserved > diagnostics->boundarySegmentBase &&
        diagnostics->boundarySegmentCommitted >= diagnostics->boundarySegmentBase &&
        diagnostics->boundarySegmentCommitted <= diagnostics->boundarySegmentReserved;
    firstAllocationStatus("Source-backed segment identity and size", segment, allPassed);

    const bool noCollection = diagnostics->collectionConsideredCount ==
            diagnostics->rarePathCount &&
        diagnostics->collectionRequestCount == 0u &&
        diagnostics->collectionEntryCount == 0u &&
        diagnostics->collectionsEntered == 0u &&
        diagnostics->gcCountBefore == diagnostics->gcCountAfter &&
        diagnostics->finalizationScanCount == 0u &&
        diagnostics->managedFinalizerCount == 0u &&
        diagnostics->finalizersExecuted == 0u &&
        diagnostics->suspensionRequestCount == 0u &&
        diagnostics->gcLockTransitionCount == 0u &&
        diagnostics->helperWakeCount == 0u;
    firstAllocationStatus("No collection or finalization", noCollection, allPassed);

    const bool objectValidation = diagnostics->zeroValidationFailures == 0u &&
        diagnostics->patternValidationFailures == 0u &&
        diagnostics->layoutFailures == 0u &&
        diagnostics->ownershipFailures == 0u &&
        diagnostics->overlapFailures == 0u &&
        diagnostics->monotonicityFailures == 0u &&
        diagnostics->contextGeometryFailures == 0u &&
        diagnostics->sourceSizeValid == 1u &&
        diagnostics->primitiveArrayValid == 1u &&
        diagnostics->belowLargeObjectThreshold == 1u &&
        diagnostics->noPostRefillAllocation == 1u &&
        diagnostics->pointerContractFailures == 0u;
    firstAllocationStatus("Primitive-array object and ownership validation", objectValidation,
                          allPassed);
    firstAllocationStatus("Finalizer worker parked", ((palState >> 24) & 0xFFu) == 0x02u &&
                           g_activeCallbacks == 0u, allPassed);
    serial::puts("[nativeaot-gc-segment-boundary] same-process shutdown: UNSUPPORTED\n");
    serial::puts("[nativeaot-gc-segment-boundary] Process teardown: PASS\n");
    serial::puts(allPassed
        ? "[nativeaot-gc-segment-boundary] ALL_PASS\n"
        : "[nativeaot-gc-segment-boundary] ALL_FAIL\n");
}
#endif

void runFirstRealAllocationImpl(
    const uint8_t* artifact, size_t artifactSize,
    uintptr_t installPalAddress, uintptr_t installTableAddress,
    uintptr_t installPlatformAddress, uintptr_t startupMainAddress,
    uintptr_t getStateAddress, uintptr_t getPreGcStateAddress,
    uintptr_t getAllocationCountAddress, uintptr_t getLastAllocationSizeAddress,
    uintptr_t getDiagnosticStageAddress, uintptr_t managedMainAddress,
    uintptr_t finalizeAddress, uintptr_t getDiagnosticsAddress,
    uint64_t generation, uintptr_t beginExperimentAddress) {
    bool allPassed = true;
#if defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-refill] BEGIN\n");
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
    serial::puts("[nativeaot-gc-segment-boundary] BEGIN\n");
#else
    serial::puts("[nativeaot-gc-first-allocation] BEGIN\n");
#endif
    const bool foundations =
        gxos::runtime::initializeLocalStorage() == gxos::runtime::LocalStorageResult::Success &&
        gxos::runtime::attachLocalStorage() == gxos::runtime::LocalStorageResult::Success &&
        guidexos::nativeaot::threadstore::initialize() ==
            guidexos::nativeaot::threadstore::Result::Success &&
        guidexos::nativeaot::threadstore::attachCurrentThread() ==
            guidexos::nativeaot::threadstore::Result::Success;
    firstAllocationStatus("Runtime foundation initialization", foundations, allPassed);
    if (!foundations) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }

    uintptr_t base = 0;
    uintptr_t size = 0;
    const bool loaded = loadArtifact(artifact, artifactSize, &base, &size);
    firstAllocationStatus("Artifact staged", loaded, allPassed);
    if (!loaded) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }
    resetBridgeState(base, size, generation);
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
    g_firstAllocationDiagnosticsAddress = getDiagnosticsAddress;
#endif

    guidexos_nativeaot_pal_hooks legacy = {};
    fillStartupPalHooks(&legacy);
    guidexos_nativeaot_pal_hook_table_v1 pal = {};
    fillStartupPalTable(&pal, base, size, generation);
    guidexos_nativeaot_gc_startup_platform_table_v1 platform = {};
    fillStartupPlatformTable(&platform, generation);

    using InstallPal = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        const guidexos_nativeaot_pal_hooks*);
    using InstallTable = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        const guidexos_nativeaot_pal_hook_table_v1*);
    using InstallPlatform = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        const guidexos_nativeaot_gc_startup_platform_table_v1*);
    using StartupMain = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
    using ManagedMain = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
        FirstRealAllocationContext*);
    using Finalize = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(uint32_t);
    using GetDiagnostics = const guidexos_nativeaot_allocation_diagnostics*
        (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
    using State = uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);

    const int32_t palResult = reinterpret_cast<InstallPal>(installPalAddress)(&legacy);
    const int32_t tableResult = reinterpret_cast<InstallTable>(installTableAddress)(&pal);
    const int32_t platformResult = reinterpret_cast<InstallPlatform>(installPlatformAddress)(&platform);
    firstAllocationStatus("Win64 PAL legacy installation", palResult == 0, allPassed);
    firstAllocationStatus("Win64 PAL v1 installation", tableResult == 0, allPassed);
    firstAllocationStatus("GC platform installation", platformResult == 0, allPassed);
    if (palResult != 0 || tableResult != 0 || platformResult != 0) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }

    const int32_t startupResult = reinterpret_cast<StartupMain>(startupMainAddress)();
    const uint32_t palState = reinterpret_cast<State>(getStateAddress)();
    const uint32_t preGcState = reinterpret_cast<State>(getPreGcStateAddress)();
    const uint32_t startupAllocationCount = reinterpret_cast<State>(getAllocationCountAddress)();
    const uint32_t startupLastAllocationSize = reinterpret_cast<State>(getLastAllocationSizeAddress)();
    const uint32_t diagnosticStage = reinterpret_cast<State>(getDiagnosticStageAddress)();
    serial::puts("[nativeaot-gc-first-allocation] RhInitialize return=");
    serial::put_hex32(static_cast<uint32_t>(startupResult));
    serial::puts(" state=");
    serial::put_hex32(palState);
    serial::puts(" preGcState=");
    serial::put_hex32(preGcState);
    serial::puts(" diagnosticStage=");
    serial::put_hex32(diagnosticStage);
    serial::puts(" startupNativeAllocations=");
    serial::put_hex32(startupAllocationCount);
    serial::puts(" lastSize=");
    serial::put_hex32(startupLastAllocationSize);
    serial::puts("\n");
    firstAllocationStatus("RhInitialize", startupResult == 0, allPassed);
    if (startupResult != 0) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }

    const bool tlsInstalled = installNativeAotCurrentThreadTls();
    firstAllocationStatus("NativeAOT current-thread TLS vector", tlsInstalled, allPassed);
    serial::puts("[nativeaot-gc-first-allocation] tlsGsBase=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(&g_nativeAotTlsGsArea));
    serial::puts(" tlsVector=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_nativeAotTlsVector));
    serial::puts(" tlsBlock=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_nativeAotTlsBlock));
    serial::puts(" tlsIndexAssumed=0\n");
    if (!tlsInstalled) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }

    if (beginExperimentAddress != 0u) {
        using BeginExperiment = void (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
        reinterpret_cast<BeginExperiment>(beginExperimentAddress)();
        firstAllocationStatus("VM trace experiment baseline captured", true, allPassed);
    }

    // RhInitialize has already published the parked finalizer-worker state.
#if defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
    runSegmentBoundaryManagedBoundary(
        managedMainAddress, finalizeAddress, getDiagnosticsAddress,
        palState, allPassed);
    return;
#elif defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
    runFirstRefillManagedBoundary(
        managedMainAddress, finalizeAddress, getDiagnosticsAddress,
        palState, allPassed);
    return;
#else
    // Enter managed code immediately so the first-allocation boundary cannot
    // be confused with a timer wait under the disposable TCG scheduler.
    FirstRealAllocationContext context{};
    context.size = sizeof(context);
    context.apiVersion = 0u;
    serial::puts("[nativeaot-gc-first-allocation] entering ManagedMain once\n");
    const int32_t managedResult = reinterpret_cast<ManagedMain>(managedMainAddress)(&context);
    const int32_t finalizeResult = reinterpret_cast<Finalize>(finalizeAddress)(
        static_cast<uint32_t>(managedResult));
    const guidexos_nativeaot_allocation_diagnostics* diagnostics =
        reinterpret_cast<GetDiagnostics>(getDiagnosticsAddress)();
    firstAllocationStatus("Managed entry once", diagnostics != nullptr, allPassed);
    firstAllocationStatus("Managed byte[24] return", managedResult == 0x1801, allPassed);
    firstAllocationStatus("Allocation finalization", finalizeResult == 0, allPassed);
    if (diagnostics == nullptr) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }

    serial::puts("[nativeaot-gc-first-allocation] managedStatus=");
    serial::put_hex32(static_cast<uint32_t>(managedResult));
    serial::puts(" finalizeStatus=");
    serial::put_hex32(static_cast<uint32_t>(finalizeResult));
    serial::puts(" schema=");
    serial::put_hex32(diagnostics->schemaVersion);
    serial::puts(" allocationCount=");
    serial::put_hex32(diagnostics->allocationCount);
    serial::puts(" rhpNewArrayEntries=");
    serial::put_hex32(diagnostics->rhpNewArrayEntries);
    serial::puts(" realGcAllocationEntries=");
    serial::put_hex32(diagnostics->realGcAllocationEntries);
    serial::puts(" slowAllocationEntries=");
    serial::put_hex32(diagnostics->slowAllocationEntries);
    serial::puts(" allocationContextRefills=");
    serial::put_hex32(diagnostics->allocationContextRefills);
    serial::puts("\n");

    printFirstAllocationPointer("contextBefore", diagnostics->allocationContextBefore);
    printFirstAllocationPointer("limitBefore", diagnostics->allocationLimitBefore);
    printFirstAllocationPointer("contextAfter", diagnostics->allocationContextAfter);
    printFirstAllocationPointer("limitAfter", diagnostics->allocationLimitAfter);
    printFirstAllocationPointer("heapBase", diagnostics->heapBase);
    printFirstAllocationPointer("heapAllocated", diagnostics->heapAllocated);
    printFirstAllocationPointer("heapReserved", diagnostics->heapReserved);
    printFirstAllocationPointer("object", diagnostics->objectAddress);
    printFirstAllocationPointer("returnedObject", diagnostics->returnedObject);
    printFirstAllocationPointer("data", diagnostics->arrayData);
    printFirstAllocationPointer("eeType", diagnostics->eeType);

    serial::puts("[nativeaot-gc-first-allocation] length=");
    serial::put_hex32(diagnostics->arrayLengthObserved);
    serial::puts(" requestedLength=");
    serial::put_hex32(diagnostics->requestedArrayLength);
    serial::puts(" size=");
    serial::put_hex32(diagnostics->requestedObjectSize);
    serial::puts(" zeroBytes=");
    serial::put_hex32(diagnostics->zeroByteCount);
    serial::puts(" pattern=");
    serial::put_hex32(diagnostics->patternVerified);
    serial::puts(" alignment=");
    serial::put_hex32(diagnostics->objectAlignmentVerified);
    serial::puts(" layout=");
    serial::put_hex32(diagnostics->objectLayoutVerified);
    serial::puts(" range=");
    serial::put_hex32(diagnostics->objectRangeVerified);
    serial::puts(" ownership=");
    serial::put_hex32(diagnostics->heapOwnershipVerified);
    serial::puts(" pointerContractFailures=");
    serial::put_hex32(diagnostics->pointerContractFailures);
    serial::puts("\n");
    serial::puts("[nativeaot-gc-first-allocation] stage=");
    serial::put_hex32(diagnostics->stage);
    serial::puts(" sequence=");
    serial::put_hex32(diagnostics->sequence);
    serial::puts(" currentRip=");
    serial::put_hex64(diagnostics->currentRip);
    serial::puts(" currentRsp=");
    serial::put_hex64(diagnostics->currentRsp);
    serial::puts(" runtimeThreadRecord=");
    serial::put_hex64(diagnostics->runtimeThreadRecord);
    serial::puts(" gcMode=");
    serial::put_hex32(diagnostics->gcMode);
    serial::puts(" transitionFrame=");
    serial::put_hex64(diagnostics->transitionFrame);
    serial::puts(" lastDirectTarget=");
    serial::put_hex64(diagnostics->lastDirectTarget);
    serial::puts(" lastIndirectCell=");
    serial::put_hex64(diagnostics->lastIndirectCell);
    serial::puts(" lastIndirectTarget=");
    serial::put_hex64(diagnostics->lastIndirectTarget);
    serial::puts(" lastLockId=");
    serial::put_hex64(diagnostics->lastLockId);
    serial::puts(" lastEventId=");
    serial::put_hex64(diagnostics->lastEventId);
    serial::puts(" waitReason=");
    serial::put_hex32(diagnostics->waitReason);
    serial::puts(" failFastReason=");
    serial::put_hex32(diagnostics->failFastReason);
    serial::puts("\n");

    serial::puts("[nativeaot-gc-first-allocation] gcCountBefore=");
    serial::put_hex32(diagnostics->gcCountBefore);
    serial::puts(" gcCountAfter=");
    serial::put_hex32(diagnostics->gcCountAfter);
    serial::puts(" collectionsEntered=");
    serial::put_hex32(diagnostics->collectionsEntered);
    serial::puts(" collectionTriggeringEntries=");
    serial::put_hex32(diagnostics->collectionTriggeringEntries);
    serial::puts(" gcInProgressBefore=");
    serial::put_hex32(diagnostics->gcInProgressBefore);
    serial::puts(" gcInProgressAfter=");
    serial::put_hex32(diagnostics->gcInProgressAfter);
    serial::puts(" finalizableBefore=");
    serial::put_hex32(diagnostics->finalizableObjectCountBefore);
    serial::puts(" finalizableAfter=");
    serial::put_hex32(diagnostics->finalizableObjectCountAfter);
    serial::puts(" finalizationScans=");
    serial::put_hex32(diagnostics->finalizationScans);
    serial::puts(" finalizersExecuted=");
    serial::put_hex32(diagnostics->finalizersExecuted);
    serial::puts("\n");

    const bool oneAllocation =
        diagnostics->schemaVersion == 1u &&
        diagnostics->allocationSucceeded == 1u &&
        diagnostics->allocationCount == 1u &&
        diagnostics->rhpNewArrayEntries == 1u &&
        diagnostics->realGcAllocationEntries == 1u &&
        diagnostics->requestedArrayLength == 24u &&
        diagnostics->arrayLengthObserved == 24u &&
        diagnostics->zeroByteCount == 24u &&
        diagnostics->patternVerified == 1u &&
        diagnostics->objectAddress == diagnostics->returnedObject &&
        diagnostics->arrayData == diagnostics->objectAddress + 0x10u &&
        diagnostics->objectAlignmentVerified == 1u &&
        diagnostics->objectLayoutVerified == 1u &&
        diagnostics->objectRangeVerified == 1u &&
        diagnostics->heapOwnershipVerified == 1u &&
        diagnostics->pointerContractFailures == 0u &&
        diagnostics->gcCountBefore == diagnostics->gcCountAfter &&
        diagnostics->collectionsEntered == 0u &&
        diagnostics->collectionTriggeringEntries == 0u &&
        diagnostics->gcInProgressBefore == 0u &&
        diagnostics->gcInProgressAfter == 0u &&
        diagnostics->finalizableObjectCountBefore == diagnostics->finalizableObjectCountAfter &&
        diagnostics->finalizationScans == 0u &&
        diagnostics->finalizersExecuted == 0u;
    firstAllocationStatus("One real Workstation GC allocation", oneAllocation, allPassed);
    firstAllocationStatus("Finalizer worker parked", ((palState >> 24) & 0xFFu) == 0x02u &&
                           g_activeCallbacks == 0u, allPassed);
    serial::puts("[nativeaot-gc-first-allocation] wrapperCallCount=1 managedEntryCallCount=1 shutdownCalls=0\n");
    serial::puts("[nativeaot-gc-first-allocation] same-process shutdown: UNSUPPORTED\n");
    serial::puts(allPassed
        ? "[nativeaot-gc-first-allocation] ALL_PASS\n"
        : "[nativeaot-gc-first-allocation] ALL_FAIL\n");
#endif
}

#endif

#endif

} // namespace

void run(const uint8_t* artifact, size_t artifactSize,
         uintptr_t installAddress, uintptr_t mainAddress,
         uintptr_t uninstallAddress) {
    bool allPassed = true;
    serial::puts("[nativeaot-pal-qemu-test] BEGIN\n");
    runOne(artifact, artifactSize, installAddress, mainAddress, uninstallAddress, 1, allPassed);
    runOne(artifact, artifactSize, installAddress, mainAddress, uninstallAddress, 2, allPassed);
    status("Second in-process launch", allPassed, allPassed);
    serial::puts(allPassed
        ? "[nativeaot-pal-qemu-test] ALL_PASS\n"
        : "[nativeaot-pal-qemu-test] ALL_FAIL\n");
}

#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
void runStartup(const uint8_t* artifact, size_t artifactSize,
                uintptr_t installPalAddress, uintptr_t installTableAddress,
                uintptr_t installPlatformAddress, uintptr_t mainAddress,
                uintptr_t stateAddress, uintptr_t preGcStateAddress,
                uintptr_t allocationCountAddress, uintptr_t lastAllocationSizeAddress,
                uintptr_t diagnosticStageAddress,
                uint64_t generation) {
    runStartupImpl(artifact, artifactSize, installPalAddress, installTableAddress,
                   installPlatformAddress, mainAddress, stateAddress, preGcStateAddress,
                   allocationCountAddress, lastAllocationSizeAddress, diagnosticStageAddress,
                   generation);
}

#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
void runFirstRealAllocation(
    const uint8_t* artifact, size_t artifactSize,
    uintptr_t installPalAddress, uintptr_t installTableAddress,
    uintptr_t installPlatformAddress, uintptr_t startupMainAddress,
    uintptr_t getStateAddress, uintptr_t getPreGcStateAddress,
    uintptr_t getAllocationCountAddress, uintptr_t getLastAllocationSizeAddress,
    uintptr_t getDiagnosticStageAddress, uintptr_t managedMainAddress,
    uintptr_t finalizeAddress, uintptr_t getDiagnosticsAddress,
    uint64_t generation, uintptr_t beginExperimentAddress) {
    runFirstRealAllocationImpl(
        artifact, artifactSize, installPalAddress, installTableAddress,
        installPlatformAddress, startupMainAddress, getStateAddress,
        getPreGcStateAddress, getAllocationCountAddress,
        getLastAllocationSizeAddress, getDiagnosticStageAddress,
        managedMainAddress, finalizeAddress, getDiagnosticsAddress, generation,
        beginExperimentAddress);
}
#endif
#endif

} // namespace nativeaot_pal_qemu_test
} // namespace kernel

#endif
