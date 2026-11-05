#if defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
#define GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST 1
#endif
#if defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
#define GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST 1
#endif
#if defined(GXOS_NATIVEAOT_GC_SEGMENT_TRANSITION_QEMU_TEST)
#define GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST 1
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_QEMU_TEST)
#define GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST 1
#endif

#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "include/kernel/native_unwind_provider.h"

#if defined(GXOS_NATIVEAOT_PAL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)

#include "include/kernel/address_space.h"
#include "include/kernel/arch.h"
#include "include/kernel/process.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/pit.h"

#include "runtime/local_storage/guidexos_local_storage.h"
#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
#include "runtime/memory/guidexos_virtual_memory_region.h"
#include "nativeaot_gc_vm_slots.h"
#include "runtime/synchronization/guidexos_event.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_gc_startup_platform_contract.h"
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST) || defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h"
#endif
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_thread_static_diagnostics.h"
#endif
#endif
#include "runtime/thread/guidexos_native_thread.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_abi_bridge.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.h"
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_init_trace.h"
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_native_alloc_record.h"
#include "include/kernel/nativeaot_native_alloc_backend_summary.h"
#endif
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.h"

#if defined(GXOS_NATIVEAOT_PAL_QEMU_TEST)
extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_end[];
#endif
#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#endif

namespace kernel {
namespace nativeaot_pal_qemu_test {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
namespace {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED)
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
constexpr uint32_t kInitTraceSerialLineLimit = 768u;
#else
constexpr uint32_t kInitTraceSerialLineLimit = 384u;
#endif
#else
constexpr uint32_t kInitTraceSerialLineLimit = 96u;
#endif
uint32_t g_initTraceSerialLines = 0u;
uint32_t g_initTraceSerialDropped = 0u;

void __cdecl nativeAotInitTraceSink(
    uint32_t invocation, uint32_t stage, uint32_t event,
    uint32_t result, uint32_t singletonBits) {
    if (g_initTraceSerialLines >= kInitTraceSerialLineLimit) {
        ++g_initTraceSerialDropped;
        return;
    }
    ++g_initTraceSerialLines;
    if (stage == GX_INIT_STAGE_GC_INTERNAL_RECORD) {
        serial::puts("[C188R12R4R5R4R5_GCINIT] invocation=");
        serial::put_hex32(invocation);
        serial::puts(" field=");
        serial::put_hex32(event);
        serial::puts(" value=");
        serial::put_hex32(result);
        serial::puts("\n");
        return;
    }
    if (stage == GX_INIT_STAGE_GC_SEMISHARED_RECORD) {
        const bool summary = (event & 0x80000000u) != 0u;
        const uint32_t sequence = summary ? 0xFFFFFFFFu : (event >> 8);
        const uint32_t field = summary ? (event & 0x7FFFFFFFu) : (event & 0xFFu);
        serial::puts("[C188R12R4R5R4R5R2_SEMISHARED] invocation=");
        serial::put_hex32(invocation);
        serial::puts(" sequence=");
        serial::put_hex32(sequence);
        serial::puts(" field=");
        serial::put_hex32(field);
        serial::puts(" value=");
        serial::put_hex32(result);
        serial::puts(" auxiliary=");
        serial::put_hex32(singletonBits);
        serial::puts("\n");
        return;
    }
    if (stage == GX_INIT_STAGE_GC_NATIVE_ALLOC_RECORD) {
        const bool summary = (event & 0x80000000u) != 0u;
        const uint32_t sequence = summary ? 0xFFFFFFFFu : (event >> 8);
        const uint32_t field = summary ? (event & 0x7FFFFFFFu) : (event & 0xFFu);
        serial::puts("[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=runtime invocation=");
        serial::put_hex32(invocation);
        serial::puts(" sequence=");
        serial::put_hex32(sequence);
        serial::puts(" field=");
        serial::put_hex32(field);
        serial::puts(" value=");
        serial::put_hex32(result);
        serial::puts(" auxiliary=");
        serial::put_hex32(singletonBits);
        serial::puts("\n");
        return;
    }
    serial::puts("[C188R12R4R5R1_INIT] invocation=");
    serial::put_hex32(invocation);
    serial::puts(" stage=");
    serial::put_hex32(stage);
    serial::puts(" event=");
    serial::put_hex32(event);
    serial::puts(" code=");
    serial::put_hex32(result);
    serial::puts(" singleton=");
    serial::put_hex32(singletonBits);
    serial::puts("\n");
}
}
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
extern "C" void __cdecl guideXosNativeAotC011EC21DescribeNativeCaller(
    uintptr_t recoveredRip, uintptr_t recoveredRsp, uintptr_t recoveredRbp);
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC96_PHYSICAL_FRAME_PROVENANCE)
void emitC96FrameStats(const char* marker, const char* checkpoint,
                       uint64_t address, uint64_t size, uint32_t result,
                       const gxos::runtime::virtual_memory::VirtualMemoryStats& stats) {
    serial::puts("[nativeaot-gc-c96-frame] ");
    serial::puts(marker);
    serial::puts(" marker=C011EC96-");
    serial::puts(marker);
    serial::puts(" checkpoint=");
    serial::puts(checkpoint);
    serial::puts(" address=");
    serial::put_hex64(address);
    serial::puts(" size=");
    serial::put_hex64(size);
    serial::puts(" result=");
    serial::put_hex32(result);
    serial::puts(" totalKnownFrames=");
    serial::put_hex64(stats.totalKnownFrames);
    serial::puts(" freeFrames=");
    serial::put_hex64(stats.freeFrames);
    serial::puts(" allocatedFrames=");
    serial::put_hex64(stats.allocatedFrames);
    serial::puts(" regionOwnedFrames=");
    serial::put_hex64(stats.regionOwnedFrames);
    serial::puts(" pageTableFrames=");
    serial::put_hex64(stats.pageTableFrames);
    serial::puts(" mappingCount=");
    serial::put_hex64(stats.mappingCount);
    serial::puts(" framesReleasedByDecommit=");
    serial::put_hex64(stats.framesReleasedByDecommit);
    serial::puts(" framesReleasedByRelease=");
    serial::put_hex64(stats.framesReleasedByRelease);
    serial::puts("\n");
}

void emitC96Checkpoint(const char* checkpoint) {
    emitC96FrameStats("FRAME", checkpoint, 0, 0, 0,
        gxos::runtime::virtual_memory::stats());
}

extern "C" void guideXosNativeAotC011EC96RollbackObserved(
    uint32_t stage, uint64_t firstPage, uint64_t pageCount,
    uint64_t newlyAllocatedPages) {
    const gxos::runtime::virtual_memory::VirtualMemoryStats stats =
        gxos::runtime::virtual_memory::stats();
    serial::puts("[nativeaot-gc-c96-frame] ROLLBACK marker=C011EC96-ROLLBACK stage=");
    serial::put_hex32(stage);
    serial::puts(" firstPage=");
    serial::put_hex64(firstPage);
    serial::puts(" pageCount=");
    serial::put_hex64(pageCount);
    serial::puts(" newlyAllocatedPages=");
    serial::put_hex64(newlyAllocatedPages);
    serial::puts(" freeFrames=");
    serial::put_hex64(stats.freeFrames);
    serial::puts(" allocatedFrames=");
    serial::put_hex64(stats.allocatedFrames);
    serial::puts(" regionOwnedFrames=");
    serial::put_hex64(stats.regionOwnedFrames);
    serial::puts(" pageTableFrames=");
    serial::put_hex64(stats.pageTableFrames);
    serial::puts("\n");
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
uint32_t g_c97TailSelector = 0u;

const char* c97CheckpointName(uint32_t checkpoint) {
    switch (checkpoint) {
        case 1u: return "tail-001";
        case 79u: return "tail-079";
        case 80u: return "tail-080";
        case 143u: return "tail-143";
        case 203u: return "tail-203";
        case 216u: return "tail-216";
        case 0x217u: return "tail-217-pre";
        case 217u: return "tail-217";
        case 218u: return "tail-complete";
        default: return "invalid";
    }
}

void emitC97FrameStatsNamed(
    const char* checkpointName, uint32_t checkpoint,
    uint32_t allocationPresent) {
    const gxos::runtime::virtual_memory::VirtualMemoryStats stats =
        gxos::runtime::virtual_memory::stats();
    serial::puts("[nativeaot-gc-c97-frame] marker=C011EC97-FRAME checkpoint=");
    serial::puts(checkpointName);
    serial::puts(" checkpointOrdinal=");
    serial::put_hex32(checkpoint);
    serial::puts(" allocationPresent=");
    serial::put_hex32(allocationPresent);
    serial::puts(" selector=");
    serial::put_hex32(g_c97TailSelector);
    serial::puts(" totalKnownFrames=");
    serial::put_hex64(stats.totalKnownFrames);
    serial::puts(" freeFrames=");
    serial::put_hex64(stats.freeFrames);
    serial::puts(" allocatedFrames=");
    serial::put_hex64(stats.allocatedFrames);
    serial::puts(" regionOwnedFrames=");
    serial::put_hex64(stats.regionOwnedFrames);
    serial::puts(" pageTableFrames=");
    serial::put_hex64(stats.pageTableFrames);
    serial::puts(" mappingCount=");
    serial::put_hex64(stats.mappingCount);
    serial::puts("\n");
}

void emitC97FrameStats(uint32_t checkpoint, uint32_t allocationPresent) {
    emitC97FrameStatsNamed(c97CheckpointName(checkpoint), checkpoint,
                           allocationPresent);
}

void emitC97StageStats(const char* checkpointName) {
    emitC97FrameStatsNamed(checkpointName, 0u, 0u);
}

uint64_t c97ArtifactId(const uint8_t* artifact, size_t artifactSize) {
    // A bounded, allocation-free identity for the exact staged NativeAOT
    // bytes.  The host records the SHA-256 alongside this marker; the runtime
    // marker proves that both selector boots hashed the bytes they actually
    // staged, rather than merely reusing a host-side filename.
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t index = 0; index < artifactSize; ++index) {
        hash ^= artifact[index];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

void emitC97SelectorArtifact(const uint8_t* artifact, size_t artifactSize) {
    serial::puts("[nativeaot-gc-c97] marker=C011EC97-SELECTOR selector=");
    serial::put_hex32(g_c97TailSelector);
    serial::puts(" artifactId=");
    serial::put_hex64(c97ArtifactId(artifact, artifactSize));
    serial::puts(" artifactBytes=");
    serial::put_hex64(static_cast<uint64_t>(artifactSize));
    serial::puts("\n");
}

extern "C" int guideXosNativeAotC011EC97Checkpoint(
    uint32_t checkpoint, uint32_t allocationPresent) {
    if ((g_c97TailSelector != 216u && g_c97TailSelector != 320u) ||
        c97CheckpointName(checkpoint)[0] == 'i' ||
        allocationPresent > 1u) {
        return -1;
    }
    emitC97FrameStats(checkpoint, allocationPresent);
    return 0;
}
#endif
namespace {

constexpr uintptr_t kPageSize = 0x1000u;
constexpr uint32_t kPtLoad = 1u;
constexpr uint16_t kElfExec = 2u;
#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
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
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST) || defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
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

#if defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
void threadStoreReadinessKernelMarker(
    const char* event, uintptr_t raw0, uintptr_t raw1) {
    serial::puts("[C188R12R4R5R4_THREAD] event=");
    serial::puts(event);
    serial::puts(" raw0=");
    serial::put_hex64(raw0);
    serial::puts(" raw1=");
    serial::put_hex64(raw1);
    serial::puts("\n");
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
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
    serial::puts("[nativeaot-thread-static] BEGIN\n");
#elif defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
    serial::puts("[nativeaot-gc-single-thread-suspend-ee] ");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
    if (g_firstAllocationDiagnosticsAddress != 0) {
        const guidexos_nativeaot_allocation_diagnostics* diagnostics =
            reinterpret_cast<const guidexos_nativeaot_allocation_diagnostics*>(
                g_firstAllocationDiagnosticsAddress);
        if (diagnostics->safeStopObserved != 0u &&
            diagnostics->firstCollectionBoundaryMarker ==
                GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_SAFE_STOP_MARKER) {
            serial::puts("[nativeaot-gc-first-collection-boundary] SAFE_STOP marker=");
            serial::put_hex32(diagnostics->firstCollectionBoundaryMarker);
            serial::puts(" callback=GCToEEInterface::SuspendEE requestCount=");
            serial::put_hex32(diagnostics->firstCollectionRequestCount);
            serial::puts(" entryCount=");
            serial::put_hex32(diagnostics->firstCollectionEntryCount);
            serial::puts(" requestedGeneration=");
            serial::put_hex32(diagnostics->requestedGeneration);
            serial::puts(" reason=");
            serial::put_hex32(diagnostics->collectionReason);
            serial::puts(" blocking=");
            serial::put_hex32(diagnostics->collectionBlockingMode);
            serial::puts(" compacting=");
            serial::put_hex32(diagnostics->collectionCompactingMode);
            serial::puts(" suspensionRequestCount=");
            serial::put_hex32(diagnostics->suspensionRequestCount);
            serial::puts(" suspensionEntryCount=");
            serial::put_hex32(diagnostics->suspensionEntryCount);
            serial::puts(" restartResumeCount=");
            serial::put_hex32(diagnostics->restartResumeCount);
            serial::puts(" heapMutationStarted=");
            serial::put_hex32(diagnostics->heapMutationStarted);
            serial::puts(" managedExecutionResumed=");
            serial::put_hex32(diagnostics->managedExecutionResumed);
            serial::puts(" unsupportedContract=");
            serial::put_hex32(diagnostics->firstUnsupportedContract);
            serial::puts(" allocations=");
            serial::put_hex32(diagnostics->allocationCount);
            serial::puts(" fast=");
            serial::put_hex32(diagnostics->fastAllocationCount);
            serial::puts(" rare=");
            serial::put_hex32(diagnostics->rarePathCount);
            serial::puts(" allocationRequests=");
            serial::put_hex32(diagnostics->allocationRequestCount);
            serial::puts(" collectionConsidered=");
            serial::put_hex32(diagnostics->collectionConsideredCount);
            serial::puts(" allocationOrdinal=");
            serial::put_hex32(diagnostics->collectionRequestAllocationOrdinal);
            serial::puts(" lastObject=");
            serial::put_hex64(diagnostics->currentObject);
            serial::puts(" objectAddress=0 requestedObjectSize=");
            serial::put_hex32(diagnostics->requestedObjectSize);
            serial::puts(" actualAlignedSize=");
            serial::put_hex64(diagnostics->derivedObjectSize);
            serial::puts(" allocPtr=");
            serial::put_hex64(diagnostics->collectionEntryAllocPtr);
            serial::puts(" allocLimit=");
            serial::put_hex64(diagnostics->collectionEntryAllocLimit);
            serial::puts(" committed=");
            serial::put_hex64(diagnostics->currentSegmentCommitted);
            serial::puts(" reserved=");
            serial::put_hex64(diagnostics->currentSegmentReserved);
            serial::puts(" segment=");
            serial::put_hex64(diagnostics->currentSegmentIdentity);
            serial::puts(" refills=");
            serial::put_hex32(diagnostics->allocationContextRefillCount);
            serial::puts(" sameSegmentCommits=");
            serial::put_hex32(diagnostics->heapCommitEventCount);
            serial::puts(" segmentTransitions=");
            serial::put_hex32(diagnostics->segmentTransitionCount);
            serial::puts(" sentinelChecks=");
            serial::put_hex32(diagnostics->sentinelValidationCount);
            serial::puts(" sentinelFailures=");
            serial::put_hex32(diagnostics->sentinelValidationFailures);
            serial::puts(" liveSentinels=");
            serial::put_hex32(diagnostics->liveSentinelCount);
            serial::puts("\n");
        }
    }
#endif
    serial::puts("[nativeaot-pal-qemu-test] FAIL_FAST reason=");
    serial::put_hex32(reason);
    serial::puts(" detail=");
    serial::put_hex64(detail);
    serial::putc('\n');
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
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
#if defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
        serial::puts("[nativeaot-gc-first-allocation] c14-predecision queueInsertions=");
        serial::put_hex32(diagnostics->firstNonNullOldOQueueInsertionCount);
        serial::puts(" callbacks=");
        serial::put_hex32(diagnostics->firstNonNullOldOCallbackCount);
        serial::puts(" candidateLoads=");
        serial::put_hex32(diagnostics->firstNonNullOldOCandidateCount);
        serial::puts(" markHelpers=");
        serial::put_hex32(diagnostics->firstNonNullOldOMarkHelperCount);
        serial::puts(" old_o=");
        serial::put_hex64(diagnostics->firstNonNullOldOOldObject);
        serial::puts(" rawMarkReads=");
        serial::put_hex32(diagnostics->firstNonNullOldORawMarkWordReadCount);
        serial::puts(" rootSlot=");
        serial::put_hex64(diagnostics->firstNonNullOldORootSlot);
        serial::puts(" rawRoot=");
        serial::put_hex64(diagnostics->firstNonNullOldORawRoot);
        serial::puts(" storageObject=");
        serial::put_hex64(diagnostics->runtimeThreadStaticStorageObjectAddress);
        serial::puts("\n");
#endif
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

#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)

constexpr uint32_t kMaxGcEventSlots = 16u;

struct GcEventSlot {
    bool active;
    gxos::runtime::Event event;
};

using GcVmSlotTable = kernel::nativeaot::detail::GcVmSlotTable<
    gxos::runtime::virtual_memory::VirtualMemoryRegion,
    kernel::nativeaot::detail::kMaxGcVmSlots>;

GcEventSlot g_gcEvents[kMaxGcEventSlots] = {};
GcVmSlotTable g_gcVmSlots;
uint32_t g_startupLegacyAllocCalls = 0;
uintptr_t g_startupLegacyLastSize = 0;
bool g_startupLegacyLastSuccess = false;

#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
struct NativeAllocBackendAttempt {
    uintptr_t requestSize;
    uint32_t requestType;
    uintptr_t roundedSize;
    uint32_t slotCapacity;
    uint32_t activeSlots;
    uint32_t freeSlotIndex;
    uint32_t addressSpaceReady;
    uint32_t vmRegionCapacity;
    uint32_t reserveResult;
    uintptr_t commitSize;
    uint32_t commitResult;
    uint32_t rollbackReleaseResult;
    uint32_t finalPointerNonNull;
    uint32_t failureOperation;
    uint32_t failureBranch;
    uint32_t failureStatus;
    uint32_t failureStatusSource;
    gxos::runtime::virtual_memory::VirtualMemoryStats before;
    gxos::runtime::virtual_memory::VirtualMemoryStats after;
};

struct NativeAllocBackendRecord {
    uint32_t count;
    uint32_t activeAttempt;
    uint32_t activeAttemptOrdinal;
    NativeAllocBackendAttempt attempts[GX_NATIVE_ALLOC_BACKEND_CAPACITY];
    kernel::nativeaot::diagnostics::NativeAllocBackendSummary summary;
};

NativeAllocBackendRecord g_nativeAllocBackend = {
    0u, GX_NATIVE_ALLOC_VALUE_UNAVAILABLE, GX_NATIVE_ALLOC_VALUE_UNAVAILABLE, {}, {},
};
bool g_nativeAllocBackendCaptureActive = false;

uint32_t nativeAllocBackendActiveVmSlots() {
    return g_gcVmSlots.activeCount();
}

uint32_t nativeAllocBackendFreeVmSlot() {
    return g_gcVmSlots.firstFreeIndex();
}

NativeAllocBackendAttempt* nativeAllocBackendActiveAttempt() {
    const uint32_t index = g_nativeAllocBackend.activeAttempt;
    return index < g_nativeAllocBackend.count &&
           index < GX_NATIVE_ALLOC_BACKEND_CAPACITY
        ? &g_nativeAllocBackend.attempts[index] : nullptr;
}

void nativeAllocBackendRecordFailure(uint32_t operation, uint32_t branch,
                                     uint32_t status, uint32_t statusSource) {
    kernel::nativeaot::diagnostics::recordNativeAllocBackendFailure(
        g_nativeAllocBackend.summary, g_nativeAllocBackend.activeAttemptOrdinal,
        operation, branch, status, statusSource);
    NativeAllocBackendAttempt* attempt = nativeAllocBackendActiveAttempt();
    if (attempt == nullptr ||
        attempt->failureOperation != GX_NATIVE_ALLOC_VALUE_UNAVAILABLE) return;
    attempt->failureOperation = operation;
    attempt->failureBranch = branch;
    attempt->failureStatus = status;
    attempt->failureStatusSource = statusSource;
}

void nativeAllocBackendBegin(uintptr_t size, uint32_t type) {
    g_nativeAllocBackendCaptureActive = false;
    if (size != 24u) return;
    const bool retainDetails = g_nativeAllocBackend.count < GX_NATIVE_ALLOC_BACKEND_CAPACITY;
    const uint32_t ordinal = kernel::nativeaot::diagnostics::
        beginNativeAllocBackendAttempt(g_nativeAllocBackend.summary, retainDetails);
    g_nativeAllocBackend.activeAttemptOrdinal = ordinal;
    if (retainDetails) {
        const uint32_t index = g_nativeAllocBackend.count++;
        NativeAllocBackendAttempt& attempt = g_nativeAllocBackend.attempts[index];
        attempt = {};
        attempt.requestSize = size;
        attempt.requestType = type;
        attempt.roundedSize = size <= UINTPTR_MAX - 4095u
            ? (size + 4095u) & ~uintptr_t(4095u)
            : GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
        attempt.slotCapacity = kernel::nativeaot::detail::kMaxGcVmSlots;
        attempt.activeSlots = nativeAllocBackendActiveVmSlots();
        attempt.freeSlotIndex = nativeAllocBackendFreeVmSlot();
        attempt.addressSpaceReady = kernel::memory::address_space::isInitialized() &&
            kernel::memory::address_space::current() != nullptr ? 1u : 0u;
        attempt.before = gxos::runtime::virtual_memory::stats();
        attempt.after = attempt.before;
        attempt.vmRegionCapacity = static_cast<uint32_t>(attempt.before.metadataCapacity);
        attempt.reserveResult = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
        attempt.commitSize = 0u;
        attempt.commitResult = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
        attempt.rollbackReleaseResult = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
        attempt.failureOperation = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
        attempt.failureBranch = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
        attempt.failureStatus = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
        attempt.failureStatusSource = GX_NATIVE_ALLOC_STATUS_UNAVAILABLE;
        g_nativeAllocBackend.activeAttempt = index;
    } else {
        g_nativeAllocBackend.activeAttempt = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    }
    g_nativeAllocBackendCaptureActive = true;
}

void nativeAllocBackendObserveReserve(uint32_t result) {
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        g_nativeAllocBackend.summary, kernel::nativeaot::diagnostics::NativeAllocBackendReserve);
    NativeAllocBackendAttempt* attempt = nativeAllocBackendActiveAttempt();
    if (attempt != nullptr) attempt->reserveResult = result;
}

void nativeAllocBackendObserveCommit(uintptr_t size, uint32_t result) {
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        g_nativeAllocBackend.summary, kernel::nativeaot::diagnostics::NativeAllocBackendCommit);
    NativeAllocBackendAttempt* attempt = nativeAllocBackendActiveAttempt();
    if (attempt == nullptr) return;
    attempt->commitSize = size;
    attempt->commitResult = result;
}

void nativeAllocBackendObserveRollbackRelease(uint32_t result) {
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        g_nativeAllocBackend.summary,
        kernel::nativeaot::diagnostics::NativeAllocBackendRollbackRelease);
    NativeAllocBackendAttempt* attempt = nativeAllocBackendActiveAttempt();
    if (attempt != nullptr) attempt->rollbackReleaseResult = result;
}

void nativeAllocBackendFinish(void* result) {
    NativeAllocBackendAttempt* attempt = nativeAllocBackendActiveAttempt();
    if (result == nullptr && (attempt == nullptr ||
        attempt->failureOperation == GX_NATIVE_ALLOC_VALUE_UNAVAILABLE)) {
        nativeAllocBackendRecordFailure(
            GX_NATIVE_ALLOC_OP_KERNEL_VIRTUAL_ALLOC,
            GX_NATIVE_ALLOC_FAILURE_PAL_RETURNED_NULL,
            GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
            GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
        attempt = nativeAllocBackendActiveAttempt();
    }
    if (attempt != nullptr) {
        attempt->finalPointerNonNull = result != nullptr ? 1u : 0u;
        attempt->after = gxos::runtime::virtual_memory::stats();
    }
    kernel::nativeaot::diagnostics::finishNativeAllocBackendAttempt(
        g_nativeAllocBackend.summary, result != nullptr);
    g_nativeAllocBackendCaptureActive = false;
    g_nativeAllocBackend.activeAttempt = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    g_nativeAllocBackend.activeAttemptOrdinal = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
}

} // namespace

extern "C" void guideXosNativeAotNativeAllocBackendFailure(
    uint32_t operation, uint32_t branch, uint32_t status,
    uint32_t statusSource) {
    if (g_nativeAllocBackendCaptureActive) {
        nativeAllocBackendRecordFailure(operation, branch, status, statusSource);
    }
}

namespace {

uint32_t nativeAllocBackendField(const NativeAllocBackendAttempt& attempt,
                                 uint32_t field, uint32_t sequence) {
    const auto low = [](uintptr_t value) { return static_cast<uint32_t>(value); };
    const auto high = [](uint64_t value) { return static_cast<uint32_t>(value >> 32); };
    switch (field) {
    case GX_NATIVE_ALLOC_BACKEND_FIELD_SEQUENCE: return sequence;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_REQUEST_SIZE: return low(attempt.requestSize);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_REQUEST_TYPE: return attempt.requestType;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_ROUNDED_SIZE: return low(attempt.roundedSize);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_SLOT_CAPACITY: return attempt.slotCapacity;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_ACTIVE_SLOTS: return attempt.activeSlots;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_FREE_SLOT_INDEX: return attempt.freeSlotIndex;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_ADDRESS_SPACE_READY: return attempt.addressSpaceReady;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_VM_REGION_CAPACITY: return attempt.vmRegionCapacity;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_RESERVE_RESULT: return attempt.reserveResult;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_COMMIT_SIZE: return low(attempt.commitSize);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_COMMIT_RESULT: return attempt.commitResult;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_ROLLBACK_RELEASE_RESULT: return attempt.rollbackReleaseResult;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_FINAL_POINTER_NON_NULL: return attempt.finalPointerNonNull;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_FIRST_FAILURE_OPERATION: return attempt.failureOperation;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_FIRST_FAILURE_BRANCH: return attempt.failureBranch;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_FIRST_FAILURE_STATUS: return attempt.failureStatus;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_STATUS_SOURCE: return attempt.failureStatusSource;
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_TOTAL_FRAMES_LOW: return low(attempt.before.totalKnownFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_TOTAL_FRAMES_HIGH: return high(attempt.before.totalKnownFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_FREE_FRAMES_LOW: return low(attempt.before.freeFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_FREE_FRAMES_HIGH: return high(attempt.before.freeFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_ALLOCATED_FRAMES_LOW: return low(attempt.before.allocatedFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_ALLOCATED_FRAMES_HIGH: return high(attempt.before.allocatedFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_VM_REGIONS: return low(attempt.before.activeRegions);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_PRE_VM_METADATA_CAPACITY: return low(attempt.before.metadataCapacity);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_FREE_FRAMES_LOW: return low(attempt.after.freeFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_FREE_FRAMES_HIGH: return high(attempt.after.freeFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_ALLOCATED_FRAMES_LOW: return low(attempt.after.allocatedFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_ALLOCATED_FRAMES_HIGH: return high(attempt.after.allocatedFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_REGION_OWNED_LOW: return low(attempt.after.regionOwnedFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_REGION_OWNED_HIGH: return high(attempt.after.regionOwnedFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_PAGE_TABLE_LOW: return low(attempt.after.pageTableFrames);
    case GX_NATIVE_ALLOC_BACKEND_FIELD_POST_PAGE_TABLE_HIGH: return high(attempt.after.pageTableFrames);
    default: return GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    }
}

void emitNativeAllocBackendRecord() {
    using kernel::nativeaot::diagnostics::NativeAllocBackendSummary;
    const NativeAllocBackendSummary& summary = g_nativeAllocBackend.summary;
    const auto emitSummary = [](uint32_t field, uint32_t value, uint32_t auxiliary) {
        serial::puts("[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation=00000001 sequence=FFFFFFFF field=");
        serial::put_hex32(0x80000000u | field);
        serial::puts(" value=");
        serial::put_hex32(value);
        serial::puts(" auxiliary=");
        serial::put_hex32(auxiliary);
        serial::putc('\n');
    };
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_STORED_COUNT,
        g_nativeAllocBackend.count, summary.droppedDetailCount);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_FIRST_FAILURE_ATTEMPT,
        summary.firstFailureAttempt, 0u);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_ATTEMPT_COUNTS,
        summary.attemptedCount, summary.completedCount);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_OUTCOME_COUNTS,
        summary.successfulCount, summary.failedCount);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_OPERATION_COUNTS,
        summary.operationCounts[kernel::nativeaot::diagnostics::NativeAllocBackendReserve],
        summary.operationCounts[kernel::nativeaot::diagnostics::NativeAllocBackendCommit]);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_ROLLBACK_AND_FIRST_DROPPED,
        summary.operationCounts[kernel::nativeaot::diagnostics::NativeAllocBackendRollbackRelease],
        summary.firstDroppedAttempt);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_TERMINAL_STATUS,
        kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(summary),
        kernel::nativeaot::diagnostics::nativeAllocBackendSummaryComplete(summary) ? 1u : 0u);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_FIRST_FAILURE_CODE,
        summary.firstFailureOperation, summary.firstFailureBranch);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_DISTINCT_FAILURE_COUNTS,
        summary.distinctFailureCount, summary.distinctFailureOverflow);
    emitSummary(GX_NATIVE_ALLOC_BACKEND_SUMMARY_COUNTER_SATURATION,
        summary.counterSaturated, 0u);
    for (uint32_t index = 0u; index < summary.distinctFailureCount; ++index) {
        const kernel::nativeaot::diagnostics::NativeAllocBackendFailureCode& code =
            summary.distinctFailures[index];
        const uint32_t field = GX_NATIVE_ALLOC_BACKEND_SUMMARY_DISTINCT_FAILURE_BASE +
            (index * 2u);
        emitSummary(field, code.operation, code.branch);
        emitSummary(field + 1u, code.status, code.statusSource);
    }
    for (uint32_t attempt = 0u; attempt < g_nativeAllocBackend.count; ++attempt) {
        const NativeAllocBackendAttempt& row = g_nativeAllocBackend.attempts[attempt];
        for (uint32_t field = 0u; field < GX_NATIVE_ALLOC_BACKEND_FIELD_COUNT; ++field) {
            serial::puts("[C188R12R4R5R4R5R2R2_NATIVE_ALLOC] source=backend invocation=00000001 sequence=");
            serial::put_hex32(attempt);
            serial::puts(" field=");
            serial::put_hex32(field);
            serial::puts(" value=");
            serial::put_hex32(nativeAllocBackendField(row, field, attempt));
            serial::puts(" auxiliary=00000000\n");
        }
    }
}
#endif

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

GcVmSlotTable::Slot* findGcVm(const void* address) {
    return g_gcVmSlots.findContaining(address);
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL startupReserve(
    uintptr_t size, uintptr_t alignment, uint32_t, uint16_t) {
    if (size == 0) {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
        nativeAllocBackendObserveReserve(static_cast<uint32_t>(gxos::runtime::virtual_memory::VmResult::InvalidArgument));
#endif
        return nullptr;
    }
    for (uint32_t slotIndex = 0u;
         slotIndex < kernel::nativeaot::detail::kMaxGcVmSlots; ++slotIndex) {
        GcVmSlotTable::Slot* slotPointer = g_gcVmSlots.slotAt(slotIndex);
        if (slotPointer == nullptr) return nullptr;
        GcVmSlotTable::Slot& slot = *slotPointer;
        if (slot.active) continue;
        const gxos::runtime::virtual_memory::VmResult result =
            gxos::runtime::virtual_memory::reserve(
                size, alignment, nullptr, &slot.region);
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
        nativeAllocBackendObserveReserve(static_cast<uint32_t>(result));
#endif
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
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
    nativeAllocBackendObserveReserve(GX_NATIVE_ALLOC_VALUE_UNAVAILABLE);
    nativeAllocBackendRecordFailure(
        GX_NATIVE_ALLOC_OP_KERNEL_VIRTUAL_ALLOC,
        GX_NATIVE_ALLOC_FAILURE_KERNEL_SLOT_CAPACITY, 0u,
        GX_NATIVE_ALLOC_STATUS_KERNEL_BOOLEAN);
#endif
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupCommit(
    void* address, uintptr_t size, uint16_t) {
    GcVmSlotTable::Slot* slot = findGcVm(address);
#if defined(GUIDEXOS_NATIVEAOT_C011EC95_VM_COMMIT_STATUS)
    if (slot == nullptr) {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
        nativeAllocBackendObserveCommit(size, static_cast<uint32_t>(gxos::runtime::virtual_memory::VmResult::NotOwned));
#endif
        return -static_cast<int32_t>(
            gxos::runtime::virtual_memory::VmResult::NotOwned);
    }
    if (size == 0) {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
        nativeAllocBackendObserveCommit(size, static_cast<uint32_t>(gxos::runtime::virtual_memory::VmResult::InvalidArgument));
#endif
        return -static_cast<int32_t>(
            gxos::runtime::virtual_memory::VmResult::InvalidArgument);
    }
#else
    if (slot == nullptr || size == 0) {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
        nativeAllocBackendObserveCommit(
            size, static_cast<uint32_t>(slot == nullptr
                ? gxos::runtime::virtual_memory::VmResult::NotOwned
                : gxos::runtime::virtual_memory::VmResult::InvalidArgument));
#endif
        return -1;
    }
#endif
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    const uintptr_t committedBefore = slot->region.committedSize;
#if defined(GUIDEXOS_NATIVEAOT_C011EC96_PHYSICAL_FRAME_PROVENANCE)
    const gxos::runtime::virtual_memory::VirtualMemoryStats c96Before =
        gxos::runtime::virtual_memory::stats();
#endif
    const gxos::runtime::virtual_memory::VmResult result =
        gxos::runtime::virtual_memory::commit(
            slot->region, offset, size,
            gxos::runtime::virtual_memory::MemoryProtection::ReadWrite);
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
    nativeAllocBackendObserveCommit(size, static_cast<uint32_t>(result));
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC96_PHYSICAL_FRAME_PROVENANCE)
    const gxos::runtime::virtual_memory::VirtualMemoryStats c96After =
        gxos::runtime::virtual_memory::stats();
    emitC96FrameStats("COMMIT-BEFORE", "vm-commit-before",
        reinterpret_cast<uintptr_t>(address), size, 0, c96Before);
    emitC96FrameStats("COMMIT-AFTER", "vm-commit-after",
        reinterpret_cast<uintptr_t>(address), size,
        static_cast<uint32_t>(result), c96After);
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC95_VM_COMMIT_STATUS)
    if (result != gxos::runtime::virtual_memory::VmResult::Ok &&
        size == UINT64_C(0x10000) &&
        (g_startupLegacyAllocCalls & 0x80000000u) == 0u) {
        // Reuse the existing startup counter's high bit as a per-boot
        // publication guard; the printed counter masks the observer bit.
        g_startupLegacyAllocCalls |= 0x80000000u;
        const char* resultDetail =
            gxos::runtime::virtual_memory::lastDiagnostic();
        const gxos::runtime::virtual_memory::VirtualMemoryStats vmStats =
            gxos::runtime::virtual_memory::stats();
        serial::puts("[nativeaot-gc-c95-vm] C95-VM-PRIMITIVE marker=C011EC95-VM-PRIMITIVE");
        serial::puts(" address=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(address));
        serial::puts(" offset=");
        serial::put_hex64(offset);
        serial::puts(" size=");
        serial::put_hex64(size);
        serial::puts(" pageSize=");
        serial::put_hex64(kPageSize);
        serial::puts(" committedBefore=");
        serial::put_hex64(committedBefore);
        serial::puts(" committedAfter=");
        serial::put_hex64(slot->region.committedSize);
        serial::puts(" reservedSize=");
        serial::put_hex64(slot->region.reservedSize);
        serial::puts(" status=");
        serial::put_hex32(static_cast<uint32_t>(result));
        serial::puts(" statusName=");
        serial::puts(gxos::runtime::virtual_memory::vmResultName(result));
        serial::puts(" detail=");
        serial::puts(resultDetail);
        serial::puts(" freeFrames=");
        serial::put_hex64(vmStats.freeFrames);
        serial::puts(" allocatedFrames=");
        serial::put_hex64(vmStats.allocatedFrames);
        serial::puts(" regionOwnedFrames=");
        serial::put_hex64(vmStats.regionOwnedFrames);
        serial::puts("\n");
    }
    return result == gxos::runtime::virtual_memory::VmResult::Ok
        ? 0 : -static_cast<int32_t>(result);
#else
    return result == gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
#endif
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupDecommit(
    void* address, uintptr_t size, uint16_t) {
    GcVmSlotTable::Slot* slot = findGcVm(address);
    if (slot == nullptr || size == 0) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    return gxos::runtime::virtual_memory::decommit(
               slot->region, offset, size) ==
            gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL startupRelease(
    void* address, uintptr_t, uint16_t) {
    GcVmSlotTable::Slot* slot = findGcVm(address);
    if (slot == nullptr || address != slot->region.base) {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
        nativeAllocBackendObserveRollbackRelease(
            static_cast<uint32_t>(gxos::runtime::virtual_memory::VmResult::NotOwned));
#endif
        return -1;
    }
    const gxos::runtime::virtual_memory::VmResult result =
        gxos::runtime::virtual_memory::release(slot->region);
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
    nativeAllocBackendObserveRollbackRelease(static_cast<uint32_t>(result));
    if (result != gxos::runtime::virtual_memory::VmResult::Ok) {
        nativeAllocBackendRecordFailure(
            GX_NATIVE_ALLOC_OP_VM_RELEASE_ROLLBACK,
            GX_NATIVE_ALLOC_FAILURE_VM_ROLLBACK_RELEASE,
            static_cast<uint32_t>(result), GX_NATIVE_ALLOC_STATUS_VM_RESULT);
    }
#endif
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
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
    nativeAllocBackendBegin(size, type);
#endif
    ++g_startupLegacyAllocCalls;
    g_startupLegacyLastSize = size;
    void* result = startupReserve(size, 4096u, 0, 0xFFFFu);
    if (result != nullptr && (type & 0x1000u) != 0) {
        const uintptr_t committedSize = (size + 4095u) & ~uintptr_t(4095u);
        if (startupCommit(result, committedSize, 0xFFFFu) != 0) {
            (void)startupRelease(result, committedSize, 0xFFFFu);
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
            nativeAllocBackendFinish(nullptr);
#endif
            return nullptr;
        }
    }
    g_startupLegacyLastSuccess = result != nullptr;
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
    nativeAllocBackendFinish(result);
#endif
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
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
    table->reserved[0] = reinterpret_cast<uintptr_t>(
        &guideXosNativeAotC011EC21DescribeNativeCaller);
    table->reserved[GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_PLATFORM_RESERVED_INDEX] =
        reinterpret_cast<uintptr_t>(&guideXosNativeUnwindLookup);
    table->reserved[GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_CLASSIFY_PLATFORM_RESERVED_INDEX] =
        reinterpret_cast<uintptr_t>(&guideXosNativeUnwindClassify);
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
    table->reserved[7] = reinterpret_cast<uintptr_t>(
        &guideXosNativeAotC011EC97Checkpoint);
#endif
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
    serial::put_hex32(g_startupLegacyAllocCalls & 0x7FFFFFFFu);
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

#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)

struct FirstRealAllocationContext {
    uint32_t size;
    uint32_t apiVersion;
    void* host;
    void* userData;
};

#if defined(GXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_QEMU_TEST)
struct C188R12R4R5HostCallTable {
    uint32_t size;
    uint32_t version;
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *log)(
        FirstRealAllocationContext*, const uint8_t*);
};

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL c188R12R4R5HostLog(
    FirstRealAllocationContext*, const uint8_t* message) {
    if (message == nullptr) return -1;
    char bounded[512]{};
    size_t length = 0u;
    while (length + 1u < sizeof(bounded) && message[length] != 0u) {
        bounded[length] = static_cast<char>(message[length]);
        ++length;
    }
    bounded[length] = '\0';
    arch::disable_interrupts();
    serial::puts(bounded);
    serial::puts("\n");
    return 0;
}
using C188R12R4R5ManagedMain = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
    FirstRealAllocationContext*);
#endif

void firstAllocationStatus(const char* name, bool passed, bool& allPassed) {
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
    serial::puts("[nativeaot-thread-static] ");
#elif defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
    serial::puts("[nativeaot-gc-single-thread-suspend-ee] ");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-collection-boundary] ");
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_TRANSITION_QEMU_TEST)
    serial::puts("[nativeaot-gc-segment-transition] ");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
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
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
    serial::puts("[nativeaot-thread-static] ");
#elif defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
    serial::puts("[nativeaot-gc-single-thread-suspend-ee] ");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-collection-boundary] ");
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_TRANSITION_QEMU_TEST)
    serial::puts("[nativeaot-gc-segment-transition] ");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
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
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
    context.userData = reinterpret_cast<void*>(
        static_cast<uintptr_t>(g_c97TailSelector));
#endif
#if defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
    #if defined(GUIDEXOS_NATIVEAOT_C011EC96_PHYSICAL_FRAME_PROVENANCE)
    emitC96Checkpoint("pre-tail");
    #endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
    emitC97StageStats("pre-tail");
#endif
    serial::puts("[nativeaot-gc-single-thread-suspend-ee] entering ManagedMain once\n");
    reinterpret_cast<SegmentBoundaryManagedMain>(managedMainAddress)(&context);
    return;
}
#elif defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-collection-boundary] entering ManagedMain once\n");
    reinterpret_cast<SegmentBoundaryManagedMain>(managedMainAddress)(&context);
    return;
}
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_TRANSITION_QEMU_TEST)
    serial::puts("[nativeaot-gc-segment-transition] entering ManagedMain once\n");
    const int32_t managedResult = reinterpret_cast<SegmentBoundaryManagedMain>(
        managedMainAddress)(&context);
    const int32_t finalizeResult = reinterpret_cast<SegmentBoundaryFinalize>(
        finalizeAddress)(static_cast<uint32_t>(managedResult));
    const guidexos_nativeaot_allocation_diagnostics* diagnostics =
        reinterpret_cast<SegmentBoundaryGetDiagnostics>(getDiagnosticsAddress)();

    firstAllocationStatus("Managed entry once", diagnostics != nullptr, allPassed);
    firstAllocationStatus("Managed byte[4096] bounded loop return", managedResult == 0, allPassed);
    firstAllocationStatus("Allocation finalization", finalizeResult == 0, allPassed);
    if (diagnostics == nullptr) {
        serial::puts("[nativeaot-gc-segment-transition] ALL_FAIL\n");
        return;
    }

    serial::puts("[nativeaot-gc-segment-transition] transitionModel=collection-first\n");
    serial::puts("[nativeaot-gc-segment-transition] collectionDecisionPath=soh_try_fit->a_fit_segment_end_p->trigger_ephemeral_gc\n");
    serial::puts("[nativeaot-gc-segment-transition] managedStatus=");
    serial::put_hex32(static_cast<uint32_t>(managedResult));
    serial::puts(" finalizeStatus=");
    serial::put_hex32(static_cast<uint32_t>(finalizeResult));
    serial::puts(" completionStatus=");
    serial::put_hex32(diagnostics->completionStatus);
    serial::puts(" selectedArrayLength=");
    serial::put_hex32(diagnostics->selectedArrayLength);
    serial::puts(" derivedObjectSize=");
    serial::put_hex64(diagnostics->derivedObjectSize);
    serial::puts(" hardAllocationLimit=");
    serial::put_hex32(diagnostics->hardAllocationLimit);
    serial::puts(" hardRefillLimit=");
    serial::put_hex32(diagnostics->hardRefillLimit);
    serial::puts(" hardCommitLimit=");
    serial::put_hex32(diagnostics->hardCommitLimit);
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
    serial::puts(" postStartupCommitCount=");
    serial::put_hex32(diagnostics->vmCommitEventCount);
    serial::puts(" segmentTransitionCount=");
    serial::put_hex32(diagnostics->segmentTransitionCount);
    serial::puts(" boundaryType=");
    serial::put_hex32(diagnostics->boundaryType);
    serial::puts(" boundaryStopObserved=");
    serial::put_hex32(diagnostics->boundaryStopObserved);
    serial::puts(" collectionConsidered=");
    serial::put_hex32(diagnostics->collectionConsideredCount);
    serial::puts(" collectionRequested=");
    serial::put_hex32(diagnostics->collectionRequestCount);
    serial::puts(" collectionEntered=");
    serial::put_hex32(diagnostics->collectionEntryCount);
    serial::puts(" suspensionRequested=");
    serial::put_hex32(diagnostics->suspensionRequestCount);
    serial::puts(" stopReason=");
    serial::put_hex32(diagnostics->stopReason);
    serial::puts("\n");

    printFirstAllocationPointer("initialSegmentIdentity", diagnostics->initialSegmentIdentity);
    printFirstAllocationPointer("initialSegmentBase", diagnostics->initialSegmentBase);
    printFirstAllocationPointer("initialSegmentAllocated", diagnostics->initialSegmentAllocated);
    printFirstAllocationPointer("initialSegmentCommitted", diagnostics->initialSegmentCommitted);
    printFirstAllocationPointer("initialSegmentReserved", diagnostics->initialSegmentReserved);
    printFirstAllocationPointer("currentSegmentIdentity", diagnostics->currentSegmentIdentity);
    printFirstAllocationPointer("currentSegmentBase", diagnostics->currentSegmentBase);
    printFirstAllocationPointer("currentSegmentAllocated", diagnostics->currentSegmentAllocated);
    printFirstAllocationPointer("currentSegmentCommitted", diagnostics->currentSegmentCommitted);
    printFirstAllocationPointer("currentSegmentReserved", diagnostics->currentSegmentReserved);
    printFirstAllocationPointer("stopObject", diagnostics->currentObject);
    printFirstAllocationPointer("stopObjectEnd", diagnostics->currentObjectEnd);
    printFirstAllocationPointer("currentAllocPtr", diagnostics->currentAllocPtr);
    printFirstAllocationPointer("currentAllocLimit", diagnostics->currentAllocLimit);

    const bool counters = diagnostics->schemaVersion == 1u &&
        diagnostics->allocationCount == diagnostics->allocationRequestCount &&
        diagnostics->allocationCount == diagnostics->rhpNewArrayCount &&
        diagnostics->allocationCount == diagnostics->hardAllocationLimit &&
        diagnostics->allocationCount > 1u &&
        diagnostics->realGcAllocationCount == diagnostics->rarePathCount &&
        diagnostics->allocationContextRefillCount == diagnostics->rarePathCount &&
        diagnostics->refillHistoryCount <= diagnostics->hardRefillLimit &&
        diagnostics->refillHistoryOverflow == 0u &&
        diagnostics->vmCommitEventCount <= diagnostics->hardCommitLimit;
    firstAllocationStatus("Fixed hard limits and allocation history", counters, allPassed);

    const bool sourceModel = diagnostics->collectionDecisionObserved == 1u &&
        diagnostics->collectionDecisionPath == 2u &&
        diagnostics->segmentTransitionCount == 0u;
    firstAllocationStatus("Source-backed collection-first decision", sourceModel, allPassed);

    const bool geometry = diagnostics->initialSegmentIdentity != 0u &&
        diagnostics->currentSegmentIdentity == diagnostics->initialSegmentIdentity &&
        diagnostics->initialSegmentReserved > diagnostics->initialSegmentBase &&
        diagnostics->currentSegmentCommitted >= diagnostics->currentSegmentBase &&
        diagnostics->currentSegmentCommitted <= diagnostics->currentSegmentReserved;
    firstAllocationStatus("Current segment geometry and ownership", geometry, allPassed);

    const bool noCollection = diagnostics->collectionRequestCount == 0u &&
        diagnostics->collectionEntryCount == 0u &&
        diagnostics->collectionsEntered == 0u &&
        diagnostics->gcCountBefore == diagnostics->gcCountAfter &&
        diagnostics->suspensionRequestCount == 0u &&
        diagnostics->finalizationScanCount == 0u &&
        diagnostics->managedFinalizerCount == 0u &&
        diagnostics->finalizersExecuted == 0u;
    firstAllocationStatus("No collection or collection suspension", noCollection, allPassed);

    const bool objects = diagnostics->zeroValidationFailures == 0u &&
        diagnostics->patternValidationFailures == 0u &&
        diagnostics->layoutFailures == 0u &&
        diagnostics->ownershipFailures == 0u &&
        diagnostics->overlapFailures == 0u &&
        diagnostics->contextGeometryFailures == 0u &&
        diagnostics->sourceSizeValid == 1u &&
        diagnostics->primitiveArrayValid == 1u &&
        diagnostics->belowLargeObjectThreshold == 1u &&
        diagnostics->managedStopObserved == 1u &&
        diagnostics->noPostRefillAllocation == 1u &&
        diagnostics->pointerContractFailures == 0u;
    firstAllocationStatus("Primitive-array stop-object validation", objects, allPassed);
    firstAllocationStatus("Finalizer worker parked", ((palState >> 24) & 0xFFu) == 0x02u &&
                           g_activeCallbacks == 0u, allPassed);
    serial::puts("[nativeaot-gc-segment-transition] same-process shutdown: UNSUPPORTED\n");
    serial::puts("[nativeaot-gc-segment-transition] Process teardown: PASS\n");
    serial::puts(allPassed
        ? "[nativeaot-gc-segment-transition] ALL_PASS\n"
        : "[nativeaot-gc-segment-transition] ALL_FAIL\n");
    return;
}
#else
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
#if defined(GUIDEXOS_NATIVEAOT_C011EC95_VM_COMMIT_STATUS)
        serial::puts(" commitAttemptObserved=");
        serial::put_hex32(entry.reserved1);
        serial::puts(" commitStatus=");
        serial::put_hex32(entry.reserved0);
#endif
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
#endif

void runFirstRealAllocationImpl(
    const uint8_t* artifact, size_t artifactSize,
    uintptr_t installPalAddress, uintptr_t installTableAddress,
    uintptr_t installPlatformAddress, uintptr_t startupMainAddress,
    uintptr_t getStateAddress, uintptr_t getPreGcStateAddress,
    uintptr_t getAllocationCountAddress, uintptr_t getLastAllocationSizeAddress,
    uintptr_t getDiagnosticStageAddress, uintptr_t managedMainAddress,
    uintptr_t finalizeAddress, uintptr_t getDiagnosticsAddress,
    uint64_t generation, uintptr_t beginExperimentAddress,
    uintptr_t standaloneNativeUnwindAddress,
    uintptr_t initTraceSetSinkAddress,
    uintptr_t initTraceGetCountAddress,
    uintptr_t initTraceGetOverflowAddress,
    uintptr_t initTraceRhInitializeRawResultAddress,
    uintptr_t safeHandoffPrepareAddress) {
    bool allPassed = true;
#if !defined(GXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    (void)safeHandoffPrepareAddress;
#endif
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
    serial::puts("[nativeaot-thread-static] BEGIN\n");
#elif defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
    serial::puts("[nativeaot-gc-single-thread-suspend-ee] BEGIN\n");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
    serial::puts("[nativeaot-gc-first-collection-boundary] BEGIN\n");
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_TRANSITION_QEMU_TEST)
    serial::puts("[nativeaot-gc-segment-transition] BEGIN\n");
#elif defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
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
#if defined(GUIDEXOS_NATIVEAOT_C011EC96_PHYSICAL_FRAME_PROVENANCE)
    emitC96Checkpoint("pre-managed");
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
    emitC97StageStats("pre-managed");
#endif
    const bool loaded = loadArtifact(artifact, artifactSize, &base, &size);
    firstAllocationStatus("Artifact staged", loaded, allPassed);
    if (!loaded) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }
#if defined(GUIDEXOS_NATIVEAOT_C011EC96_PHYSICAL_FRAME_PROVENANCE)
    emitC96Checkpoint("post-image");
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
    emitC97StageStats("post-image");
    emitC97SelectorArtifact(artifact, artifactSize);
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
    // The kernel native module is registered before NativeAOT initialization
    // and well before any EE-suspended path.  Preflight uses the actual
    // relinked helper address and the same generic PC lookup callback that the
    // iterator will use; it does not special-case the helper in the provider.
    const int32_t nativeModuleRegistration =
        guideXosNativeUnwindRegisterKernelModule();
    firstAllocationStatus("Kernel native unwind module registration",
                          nativeModuleRegistration == 0, allPassed);
    serial::puts("[nativeaot-gc-single-thread-suspend-ee] native unwind registry capacity=");
    serial::put_hex32(guideXosNativeUnwindRegistryCapacity());
    serial::puts(" count=");
    serial::put_hex32(guideXosNativeUnwindRegistryCount());
    serial::puts("\n");
    guidexos_nativeaot_native_unwind_lookup_result preflight = {};
    const uintptr_t actualHelperPc = reinterpret_cast<uintptr_t>(
        &runFirstRealAllocationImpl);
    const int32_t preflightLookup = guideXosNativeUnwindLookup(
        actualHelperPc, &preflight);
    uint32_t secondNativeFunctionIndex = 0xFFFFFFFFu;
    const int32_t focusedCoverage = guideXosNativeUnwindValidateFocusedCoverage(
        &secondNativeFunctionIndex);
    firstAllocationStatus("Kernel native unwind helper lookup",
                          preflightLookup == 0, allPassed);
    firstAllocationStatus("Kernel native unwind focused coverage",
                          focusedCoverage == 0, allPassed);
    if (nativeModuleRegistration == 0 && preflightLookup == 0 &&
        focusedCoverage == 0) {
        serial::puts("[nativeaot-gc-native-unwind] preflight marker=C011EC23-PREFLIGHT");
        serial::puts(" helperPC=");
        serial::put_hex64(actualHelperPc);
        serial::puts(" moduleBase=");
        serial::put_hex64(preflight.module_base);
        serial::puts(" pdataStart=");
        serial::put_hex64(preflight.pdata_start);
        serial::puts(" pdataEnd=");
        serial::put_hex64(preflight.pdata_end);
        serial::puts(" runtimeFunction=");
        serial::put_hex64(preflight.runtime_function);
        serial::puts(" unwindInfo=");
        serial::put_hex64(preflight.unwind_info);
        serial::puts(" beginRVA=");
        serial::put_hex64(preflight.begin_address);
        serial::puts(" endRVA=");
        serial::put_hex64(preflight.end_address);
        serial::puts(" unwindRVA=");
        serial::put_hex64(preflight.unwind_data);
        serial::puts(" entries=");
        serial::put_hex32(static_cast<uint32_t>(
            (preflight.pdata_end - preflight.pdata_start) / 12u));
        serial::puts(" focusedCoverage=1 secondFunctionIndex=");
        serial::put_hex32(secondNativeFunctionIndex);
        serial::puts("\n");
    }
#endif
    resetBridgeState(base, size, generation);
#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST) || defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
    g_firstAllocationDiagnosticsAddress = 0u;
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

#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
    g_initTraceSerialLines = 0u;
    g_initTraceSerialDropped = 0u;
    if (initTraceSetSinkAddress != 0u) {
        using SetInitTraceSink = void (__cdecl *)(guidexos_nativeaot_init_trace_sink);
        reinterpret_cast<SetInitTraceSink>(initTraceSetSinkAddress)(
            &nativeAotInitTraceSink);
    }
#else
    (void)initTraceSetSinkAddress;
    (void)initTraceGetCountAddress;
    (void)initTraceGetOverflowAddress;
#endif
#if !defined(GXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT)
    (void)initTraceRhInitializeRawResultAddress;
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
    threadStoreReadinessKernelMarker(
        "kernel_adapter_dispatch",
        static_cast<uintptr_t>(process::current_thread_id()),
        static_cast<uintptr_t>(startupMainAddress));
#endif
    const int32_t startupResult = reinterpret_cast<StartupMain>(startupMainAddress)();
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
    emitNativeAllocBackendRecord();
#endif
    g_firstAllocationDiagnosticsAddress = reinterpret_cast<uintptr_t>(
        reinterpret_cast<GetDiagnostics>(getDiagnosticsAddress)());
    const uint32_t palState = reinterpret_cast<State>(getStateAddress)();
    const uint32_t preGcState = reinterpret_cast<State>(getPreGcStateAddress)();
    const uint32_t startupAllocationCount = reinterpret_cast<State>(getAllocationCountAddress)();
    const uint32_t startupLastAllocationSize = reinterpret_cast<State>(getLastAllocationSizeAddress)();
    const uint32_t diagnosticStage = reinterpret_cast<State>(getDiagnosticStageAddress)();
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
    using InitTraceValue = uint32_t (__cdecl *)(void);
    uint32_t initTraceCount = initTraceGetCountAddress != 0u
        ? reinterpret_cast<InitTraceValue>(initTraceGetCountAddress)() : 0xFFFFFFFFu;
    uint32_t initTraceOverflow = initTraceGetOverflowAddress != 0u
        ? reinterpret_cast<InitTraceValue>(initTraceGetOverflowAddress)() : 0xFFFFFFFFu;
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT)
    using InitRhResultValue = uint32_t (__cdecl *)(void);
    const uint32_t nativeRhInitializeResult =
        initTraceRhInitializeRawResultAddress != 0u
            ? reinterpret_cast<InitRhResultValue>(
                  initTraceRhInitializeRawResultAddress)()
            : 0xFFFFFFFFu;
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
    threadStoreReadinessKernelMarker(
        "kernel_init_result",
        static_cast<uintptr_t>(nativeRhInitializeResult),
        static_cast<uintptr_t>(static_cast<uint32_t>(startupResult)));
#endif
    serial::puts(
#if defined(GXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT)
        "[nativeaot-gc-first-allocation] startup adapter status="
#else
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
        "[nativeaot-thread-static] RhInitialize return="
#else
        "[nativeaot-gc-first-allocation] RhInitialize return="
#endif
#endif
    );
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
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS) && \
    !defined(GXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    serial::puts("[C188R12R4R5R1_INIT] trace_end invocation=00000001 count=");
    serial::put_hex32(initTraceCount);
    serial::puts(" overflow=");
    serial::put_hex32(initTraceOverflow);
    serial::puts(" serialDropped=");
    serial::put_hex32(g_initTraceSerialDropped);
    serial::puts(" adapterStatus=");
    serial::put_hex32(static_cast<uint32_t>(startupResult));
    serial::puts("\n");
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT)
    serial::puts("[C188R12R4R5R3_INIT] init_result nativeBool=");
    serial::put_hex32(nativeRhInitializeResult);
    serial::puts(" adapterStatus=");
    serial::put_hex32(static_cast<uint32_t>(startupResult));
    serial::puts(" nativeOutcome=");
    if (nativeRhInitializeResult == 0u) serial::puts("failure");
    else if (nativeRhInitializeResult == 1u) serial::puts("success");
    else serial::puts("unknown");
    serial::puts(" adapterOutcome=");
    serial::puts(startupResult == 0 ? "success\n" : "failure\n");
#endif
    firstAllocationStatus("RhInitialize adapter status (zero is success)",
                          startupResult == 0, allPassed);
    if (startupResult != 0) {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
        threadStoreReadinessKernelMarker(
            "managed_entry_blocked_init_failure",
            static_cast<uintptr_t>(nativeRhInitializeResult),
            static_cast<uintptr_t>(static_cast<uint32_t>(startupResult)));
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
#if defined(GXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT)
        serial::puts("[C188R12R4R5R1_INIT] managed_main_gate=blocked adapterStatus=");
#else
        serial::puts("[C188R12R4R5R1_INIT] managed_main_gate=blocked rhResult=");
#endif
        serial::put_hex32(static_cast<uint32_t>(startupResult));
#if defined(GXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT)
        serial::puts(" nativeBool=");
        serial::put_hex32(nativeRhInitializeResult);
#endif
        serial::puts("\n");
#endif
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
#if defined(GXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    threadStoreReadinessKernelMarker(
        "managed_entry_gate_closed",
        static_cast<uintptr_t>(nativeRhInitializeResult),
        static_cast<uintptr_t>(static_cast<uint32_t>(startupResult)));
#else
    threadStoreReadinessKernelMarker(
        "managed_entry_gate_open",
        static_cast<uintptr_t>(nativeRhInitializeResult),
        static_cast<uintptr_t>(static_cast<uint32_t>(startupResult)));
#endif
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS) && !defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
    serial::puts("[C188R12R4R5R1_INIT] managed_main_gate=blocked reason=runtime-threadstore-attachment-unqualified singleton=");
    serial::put_hex32(preGcState);
    serial::puts(" gcProof=not-entered\n");
    serial::puts("[nativeaot-gc-first-allocation] initialization diagnostics complete; GC proof suppressed\n");
    return;
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC96_PHYSICAL_FRAME_PROVENANCE)
    emitC96Checkpoint("post-startup");
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
    emitC97StageStats("post-startup");
#endif

    const bool tlsInstalled = installNativeAotCurrentThreadTls();
    firstAllocationStatus("NativeAOT current-thread TLS vector", tlsInstalled, allPassed);
    serial::puts(
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
        "[nativeaot-thread-static] tlsGsBase="
#else
        "[nativeaot-gc-first-allocation] tlsGsBase="
#endif
    );
    serial::put_hex64(reinterpret_cast<uintptr_t>(&g_nativeAotTlsGsArea));
    serial::puts(" tlsVector=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_nativeAotTlsVector));
    serial::puts(" tlsBlock=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_nativeAotTlsBlock));
    serial::puts(" tlsIndexAssumed=0\n");
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
    threadStoreReadinessKernelMarker(
        "tls_installed",
        reinterpret_cast<uintptr_t>(g_nativeAotTlsVector),
        reinterpret_cast<uintptr_t>(g_nativeAotTlsBlock));
    threadStoreReadinessKernelMarker(
        "tls_gsbase",
        reinterpret_cast<uintptr_t>(&g_nativeAotTlsGsArea),
        static_cast<uintptr_t>(arch::amd64::read_msr(0xC0000101u)));
#endif
    if (!tlsInstalled) {
        serial::puts("[nativeaot-gc-first-allocation] ALL_FAIL\n");
        return;
    }

#if defined(GXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    if (safeHandoffPrepareAddress == 0u) {
        serial::puts("[nativeaot-safe-handoff] diagnosticError=missing_prepare_export runtimeError=none\n");
        threadStoreReadinessKernelMarker("diagnostic_safe_stop_incomplete", 1u, 0u);
        return;
    }
    using SafeHandoffPrepare = uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
    const uint32_t safeHandoffStatus =
        reinterpret_cast<SafeHandoffPrepare>(safeHandoffPrepareAddress)();
    if (safeHandoffStatus != 0u) {
        using InitTraceValue = uint32_t (__cdecl *)(void);
        initTraceCount = initTraceGetCountAddress != 0u
            ? reinterpret_cast<InitTraceValue>(initTraceGetCountAddress)() : 0xFFFFFFFFu;
        initTraceOverflow = initTraceGetOverflowAddress != 0u
            ? reinterpret_cast<InitTraceValue>(initTraceGetOverflowAddress)() : 0xFFFFFFFFu;
        serial::puts("[C188R12R4R5R1_INIT] trace_end invocation=00000001 count=");
        serial::put_hex32(initTraceCount);
        serial::puts(" overflow=");
        serial::put_hex32(initTraceOverflow);
        serial::puts(" serialDropped=");
        serial::put_hex32(g_initTraceSerialDropped);
        serial::puts(" adapterStatus=");
        serial::put_hex32(static_cast<uint32_t>(startupResult));
        serial::puts("\n");
        serial::puts("[nativeaot-safe-handoff] diagnosticError=prepare_failed status=");
        serial::put_hex32(safeHandoffStatus);
        serial::puts(" runtimeError=none\n");
        threadStoreReadinessKernelMarker(
            "diagnostic_safe_stop_incomplete", safeHandoffStatus, 0u);
        return;
    }
    using InitTraceValue = uint32_t (__cdecl *)(void);
    initTraceCount = initTraceGetCountAddress != 0u
        ? reinterpret_cast<InitTraceValue>(initTraceGetCountAddress)() : 0xFFFFFFFFu;
    initTraceOverflow = initTraceGetOverflowAddress != 0u
        ? reinterpret_cast<InitTraceValue>(initTraceGetOverflowAddress)() : 0xFFFFFFFFu;
    serial::puts("[C188R12R4R5R1_INIT] trace_end invocation=00000001 count=");
    serial::put_hex32(initTraceCount);
    serial::puts(" overflow=");
    serial::put_hex32(initTraceOverflow);
    serial::puts(" serialDropped=");
    serial::put_hex32(g_initTraceSerialDropped);
    serial::puts(" adapterStatus=");
    serial::put_hex32(static_cast<uint32_t>(startupResult));
    serial::puts("\n");
    threadStoreReadinessKernelMarker("diagnostic_safe_stop", 1u, 0u);
    serial::puts("[nativeaot-safe-handoff] status=intentionally_stopped initialization=accepted moduleRegistration=accepted managedTransition=not_attempted managedEntry=not_reached runtimeError=none\n");
    serial::puts("[nativeaot-gc-first-allocation] SAFE_HANDOFF_STOP\n");
    return;
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
    // InitializeModules clears the real-GC diagnostics record.  Run the
    // standalone native check after that initialization, but still before
    // ManagedMain can request a collection or enter the suspended walk.
    if (standaloneNativeUnwindAddress != 0u) {
#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
        uintptr_t c011ec24LiveRsp = 0u;
#if defined(__GNUC__) || defined(__clang__)
        __asm__ volatile("mov %%rsp, %0" : "=r"(c011ec24LiveRsp));
#endif
        using StandaloneNativeUnwind = uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
            uintptr_t, uintptr_t);
        const uint32_t standaloneResult =
            reinterpret_cast<StandaloneNativeUnwind>(standaloneNativeUnwindAddress)(
                reinterpret_cast<uintptr_t>(&runFirstRealAllocationImpl),
                c011ec24LiveRsp);
#else
        using StandaloneNativeUnwind = uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(
            uintptr_t);
        const uint32_t standaloneResult =
            reinterpret_cast<StandaloneNativeUnwind>(standaloneNativeUnwindAddress)(
                reinterpret_cast<uintptr_t>(&runFirstRealAllocationImpl));
#endif
        firstAllocationStatus("Second genuine native unwind",
                              standaloneResult == 1u, allPassed);
    }
#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
    // The C24 standalone entry also emits C011EC24-PREFLIGHT after its two
    // bounded synthetic checks and direct live return-slot derivation.
#endif
#endif

    if (beginExperimentAddress != 0u) {
        using BeginExperiment = void (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
        reinterpret_cast<BeginExperiment>(beginExperimentAddress)();
        firstAllocationStatus("VM trace experiment baseline captured", true, allPassed);
    }

    // RhInitialize has already published the parked finalizer-worker state.
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
    FirstRealAllocationContext context{};
    context.size = sizeof(context);
    context.apiVersion = 0u;
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
    context.userData = reinterpret_cast<void*>(
        static_cast<uintptr_t>(g_c97TailSelector));
#endif
    using GetThreadStaticDiagnostics = const guidexos_nativeaot_thread_static_diagnostics*
        (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void);
    serial::puts("[nativeaot-thread-static] entering ManagedMain once\n");
    const int32_t managedResult = reinterpret_cast<ManagedMain>(managedMainAddress)(&context);
    const guidexos_nativeaot_thread_static_diagnostics* diagnostics =
        reinterpret_cast<GetThreadStaticDiagnostics>(getDiagnosticsAddress)();
    const bool primitivePass = diagnostics != nullptr &&
        (diagnostics->primitiveStartCount == 0u ||
        (diagnostics->primitiveStartCount == 1u &&
        diagnostics->primitiveSuccessCount == 1u &&
        diagnostics->primitiveInitialValue == 0u &&
        diagnostics->primitiveAssignedValue == 0x13572468u &&
        diagnostics->primitiveReadbackValue == 0x13572468u &&
        diagnostics->primitiveMismatchCount == 0u));
    const bool referencePass = diagnostics != nullptr &&
        diagnostics->referenceStartCount <= 1u &&
        diagnostics->referenceSuccessCount <= 1u &&
        (diagnostics->referenceSuccessCount == 0u ||
         (diagnostics->referenceAssigned != 0u &&
          diagnostics->referenceAssigned == diagnostics->referenceReadback &&
          diagnostics->referenceIdentityMatch == 1u &&
          diagnostics->referenceObjectValid == 1u));
    const bool noGc = diagnostics != nullptr &&
        diagnostics->unexpectedGcRequests == 0u &&
        diagnostics->collectionEntries == 0u &&
        diagnostics->suspensionRequests == 0u;
    const bool oneThread = diagnostics != nullptr &&
        diagnostics->runtimeThread != 0u &&
        diagnostics->registeredThreadCount == 1u &&
        diagnostics->tlsBlock != 0u &&
        diagnostics->flsRuntimeIdentity != 0u &&
        diagnostics->inlinedRootList != 0u &&
        diagnostics->inlinedStorageBase != 0u &&
        diagnostics->duplicateStorageCount == 0u;
    const bool managedPass = managedResult == 0x7A510002 || managedResult == 0x7A510004;
    firstAllocationStatus("Managed thread-static entry", managedPass, allPassed);
    firstAllocationStatus("Primitive thread-static proof", primitivePass, allPassed);
    firstAllocationStatus("Reference thread-static proof", referencePass, allPassed);
    firstAllocationStatus("Single-thread storage ownership", oneThread, allPassed);
    firstAllocationStatus("No collection during proof", noGc, allPassed);
    // Keep the bounded diagnostics record contiguous. The PIT diagnostic IRQ
    // is unrelated to the managed proof and can otherwise interleave between
    // a field name and its hexadecimal value on the serial stream.
    arch::disable_interrupts();
    if (diagnostics != nullptr) {
        serial::puts("[nativeaot-thread-static] managedResult=");
        serial::put_hex32(static_cast<uint32_t>(managedResult));
        serial::puts(" primitiveStart=");
        serial::put_hex32(diagnostics->primitiveStartCount);
        serial::puts(" primitiveSuccess=");
        serial::put_hex32(diagnostics->primitiveSuccessCount);
        serial::puts(" referenceStart=");
        serial::put_hex32(diagnostics->referenceStartCount);
        serial::puts(" referenceSuccess=");
        serial::put_hex32(diagnostics->referenceSuccessCount);
        serial::puts(" primitiveInitial=");
        serial::put_hex32(diagnostics->primitiveInitialValue);
        serial::puts(" primitiveAssigned=");
        serial::put_hex32(diagnostics->primitiveAssignedValue);
        serial::puts(" primitiveReadback=");
        serial::put_hex32(diagnostics->primitiveReadbackValue);
        serial::puts(" referenceAssigned=");
        serial::put_hex64(diagnostics->referenceAssigned);
        serial::puts(" referenceReadback=");
        serial::put_hex64(diagnostics->referenceReadback);
        serial::puts(" identityMatch=");
        serial::put_hex32(diagnostics->referenceIdentityMatch);
        serial::puts(" objectValid=");
        serial::put_hex32(diagnostics->referenceObjectValid);
        serial::puts(" runtimeThread=");
        serial::put_hex64(diagnostics->runtimeThread);
        serial::puts(" nativeThreadId=");
        serial::put_hex64(diagnostics->nativeThreadId);
        serial::puts(" tlsBlock=");
        serial::put_hex64(diagnostics->tlsBlock);
        serial::puts(" flsIdentity=");
        serial::put_hex64(diagnostics->flsRuntimeIdentity);
        serial::puts(" threadStaticStorage=");
        serial::put_hex64(diagnostics->threadStaticStorage);
        serial::puts(" inlinedRoot=");
        serial::put_hex64(diagnostics->inlinedRootList);
        serial::puts(" storageBase=");
        serial::put_hex64(diagnostics->inlinedStorageBase);
        serial::puts(" storageSize=");
        serial::put_hex32(diagnostics->inlinedStorageSize);
        serial::puts(" inlinedTypeManager=");
        serial::put_hex64(diagnostics->inlinedTypeManager);
        serial::puts(" storageInitRequests=");
        serial::put_hex32(diagnostics->storageInitializationRequests);
        serial::puts(" storageInitEntries=");
        serial::put_hex32(diagnostics->storageInitializationEntries);
        serial::puts(" storageInitCompletions=");
        serial::put_hex32(diagnostics->storageInitializationCompletions);
        serial::puts(" storageAllocations=");
        serial::put_hex32(diagnostics->storageAllocationCount);
        serial::puts(" repeatedLookups=");
        serial::put_hex32(diagnostics->repeatedLookupCount);
        serial::puts(" registeredThreads=");
        serial::put_hex32(diagnostics->registeredThreadCount);
        serial::puts(" unexpectedGcRequests=");
        serial::put_hex32(diagnostics->unexpectedGcRequests);
        serial::puts(" collectionEntries=");
        serial::put_hex32(diagnostics->collectionEntries);
        serial::puts(" suspensionRequests=");
        serial::put_hex32(diagnostics->suspensionRequests);
        serial::puts(" writeBarrierRequests=");
        serial::put_hex32(diagnostics->writeBarrierRequests);
        serial::puts(" moduleInitRequests=");
        serial::put_hex32(diagnostics->moduleInitializationRequests);
        serial::puts(" moduleInitEntries=");
        serial::put_hex32(diagnostics->moduleInitializationEntries);
        serial::puts(" moduleInitCompletions=");
        serial::put_hex32(diagnostics->moduleInitializationCompletions);
        serial::puts(" finalMarker=");
        serial::put_hex32(diagnostics->finalMarker);
        serial::puts("\n");
    }
    serial::puts(allPassed
        ? "[nativeaot-thread-static] ALL_PASS\n"
        : "[nativeaot-thread-static] ALL_FAIL\n");
    return;
#elif defined(GXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_QEMU_TEST)
    C188R12R4R5HostCallTable host{};
    host.size = sizeof(host);
    host.version = 1u;
    host.log = &c188R12R4R5HostLog;
    FirstRealAllocationContext context{};
    context.size = sizeof(context);
    context.apiVersion = 0u;
    context.host = &host;
    serial::puts("[C188R12R4R5-GC-OWNERSHIP] callbacks=3 begin\n");
    uint32_t callbacks = 0u;
    int32_t results[3] = {};
    for (uint32_t index = 0u; index < 3u; ++index) {
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
        serial::puts("[C188R12R4R5R1_INIT] managed_main_call ordinal=");
        serial::put_hex32(index + 1u);
        serial::puts("\n");
#endif
#if defined(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
        threadStoreReadinessKernelMarker(
            "managed_dispatch",
            static_cast<uintptr_t>(managedMainAddress),
            static_cast<uintptr_t>(index + 1u));
#endif
        results[index] = reinterpret_cast<C188R12R4R5ManagedMain>(
            managedMainAddress)(&context);
#if defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
        serial::puts("[C188R12R4R5R1_INIT] managed_main_return ordinal=");
        serial::put_hex32(index + 1u);
        serial::puts(" result=");
        serial::put_hex32(static_cast<uint32_t>(results[index]));
        serial::puts("\n");
#endif
        ++callbacks;
        if (results[index] != 0) break;
    }
    const bool passed = callbacks == 3u && results[0] == 0 &&
        results[1] == 0 && results[2] == 0;
    serial::puts("[C188R12R4R5-GC-OWNERSHIP] outcome=");
    serial::puts(passed ? "PASS" : "FAIL");
    serial::puts(" callbacks=");
    serial::put_hex32(callbacks);
    serial::puts(" result1=");
    serial::put_hex32(static_cast<uint32_t>(results[0]));
    serial::puts(" result2=");
    serial::put_hex32(static_cast<uint32_t>(results[1]));
    serial::puts(" result3=");
    serial::put_hex32(static_cast<uint32_t>(results[2]));
    serial::puts("\n");
    return;
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
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

#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
void setC011EC97TailSelector(uint32_t selector) {
    g_c97TailSelector = selector;
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
extern "C" void __cdecl guideXosNativeAotC011EC21DescribeNativeCaller(
    uintptr_t recoveredRip, uintptr_t recoveredRsp, uintptr_t recoveredRbp) {
    /*
     * This callback is deliberately image-local and scalar-only. It obtains
     * the helper symbol from the same linked translation unit, records the
     * return-site instruction identified by the audited x64 call shape, and
     * does not inspect stack memory or fabricate native unwind state.
     */
    const uintptr_t helperStart = reinterpret_cast<uintptr_t>(
        &runFirstRealAllocationImpl);
    /* The exact linked symbol interval is captured by the static audit. */
    const uintptr_t helperEnd = 0u;
    const uintptr_t callSite = recoveredRip >= 7u ? recoveredRip - 7u : 0u;
    guidexos_nativeaot_allocation_diagnostics* diagnostics =
        reinterpret_cast<guidexos_nativeaot_allocation_diagnostics*>(
            g_firstAllocationDiagnosticsAddress);
    if (diagnostics != nullptr) {
        diagnostics->c011ec21NativeRip = recoveredRip;
        diagnostics->c011ec21NativeRsp = recoveredRsp;
        diagnostics->c011ec21NativeRbp = recoveredRbp;
        diagnostics->c011ec21NativeHelperStart = helperStart;
        diagnostics->c011ec21NativeHelperEnd = helperEnd;
        diagnostics->c011ec21NativeFunctionOffset =
            recoveredRip >= helperStart ? recoveredRip - helperStart : 0u;
        diagnostics->c011ec21NativeCallSite = callSite;
        diagnostics->c011ec21NativeModuleIdentity = 1u;
        diagnostics->c011ec21NativeSectionIdentity = 1u;
        diagnostics->c011ec21NativeRuntimeFunction = 0u;
        diagnostics->c011ec21NativeUnwindInfo = 0u;
    }
}
#endif

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

#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
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

#if defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
void runFirstRealAllocation(
    const uint8_t* artifact, size_t artifactSize,
    uintptr_t installPalAddress, uintptr_t installTableAddress,
    uintptr_t installPlatformAddress, uintptr_t startupMainAddress,
    uintptr_t getStateAddress, uintptr_t getPreGcStateAddress,
    uintptr_t getAllocationCountAddress, uintptr_t getLastAllocationSizeAddress,
    uintptr_t getDiagnosticStageAddress, uintptr_t managedMainAddress,
    uintptr_t finalizeAddress, uintptr_t getDiagnosticsAddress,
    uint64_t generation, uintptr_t beginExperimentAddress,
    uintptr_t standaloneNativeUnwindAddress,
    uintptr_t initTraceSetSinkAddress,
    uintptr_t initTraceGetCountAddress,
    uintptr_t initTraceGetOverflowAddress,
    uintptr_t initTraceRhInitializeRawResultAddress,
    uintptr_t safeHandoffPrepareAddress) {
    runFirstRealAllocationImpl(
        artifact, artifactSize, installPalAddress, installTableAddress,
        installPlatformAddress, startupMainAddress, getStateAddress,
        getPreGcStateAddress, getAllocationCountAddress,
        getLastAllocationSizeAddress, getDiagnosticStageAddress,
        managedMainAddress, finalizeAddress, getDiagnosticsAddress, generation,
        beginExperimentAddress, standaloneNativeUnwindAddress,
        initTraceSetSinkAddress, initTraceGetCountAddress,
        initTraceGetOverflowAddress, initTraceRhInitializeRawResultAddress,
        safeHandoffPrepareAddress);
}
#endif
#endif

} // namespace nativeaot_pal_qemu_test
} // namespace kernel

#endif
