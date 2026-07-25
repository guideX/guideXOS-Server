#include "include/kernel/nativeaot_pal_qemu_test.h"

#if defined(GXOS_NATIVEAOT_PAL_QEMU_TEST)

#include "include/kernel/address_space.h"
#include "include/kernel/arch.h"
#include "include/kernel/process.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/pit.h"

#include "runtime/local_storage/guidexos_local_storage.h"
#include "runtime/thread/guidexos_native_thread.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_abi_bridge.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.h"

extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_end[];

namespace kernel {
namespace nativeaot_pal_qemu_test {
namespace {

constexpr uintptr_t kPageSize = 0x1000u;
constexpr uint32_t kPtLoad = 1u;
constexpr uint16_t kElfExec = 2u;
constexpr uint32_t kMaxMappedPages = 1024u;
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

} // namespace nativeaot_pal_qemu_test
} // namespace kernel

#endif
