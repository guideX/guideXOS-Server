#include "guidexos_nativeaot_virtual_memory_adapter.h"

namespace guidexos {
namespace nativeaot {
namespace virtual_memory {

using gxos::runtime::virtual_memory::VirtualMemoryInfo;
using gxos::runtime::virtual_memory::VirtualMemoryRegion;
using gxos::runtime::virtual_memory::VirtualMemoryStats;

namespace {

constexpr nativeaot_vm_size kRegistryCapacity = 32;
constexpr nativeaot_vm_size kTraceCapacity = 128;

struct RegistryRecord;

struct VirtualMemoryHandleState {
    RegistryRecord* record;
    nativeaot_vm_uint64 reservationGeneration;
};

struct RegistryRecord {
    VirtualMemoryRegion region;
    VirtualMemoryHandleState handle;
    void* base;
    nativeaot_vm_size reservedSize;
    nativeaot_vm_uint64 reservationGeneration;
    nativeaot_vm_uint64 regionGeneration;
    nativeaot_vm_uint64 ownerInstance;
    nativeaot_vm_uint64 reserveCalls;
    nativeaot_vm_uint64 commitCalls;
    nativeaot_vm_uint64 decommitCalls;
    nativeaot_vm_uint64 protectCalls;
    nativeaot_vm_uint64 releaseCalls;
    bool active;
    bool failureCleanup;
    const char* purpose;

    RegistryRecord()
        : region(), handle{nullptr, 0}, base(nullptr), reservedSize(0),
          reservationGeneration(0), regionGeneration(0), ownerInstance(0),
          reserveCalls(0), commitCalls(0), decommitCalls(0), protectCalls(0),
          releaseCalls(0), active(false), failureCleanup(false),
          purpose(nullptr) {
    }
};

// The adapter is intentionally single-owner in this experimental
// configuration. The locked runtime identity has one application thread and
// no concurrent/background GC; keeping the registry static also makes
// exhaustion deterministic in the freestanding build.
RegistryRecord g_records[kRegistryCapacity];
TraceEvent g_trace[kTraceCapacity];
nativeaot_vm_size g_traceCount = 0;
nativeaot_vm_uint64 g_traceSequence = 0;
nativeaot_vm_uint64 g_nextReservationGeneration = 1;
nativeaot_vm_uint64 g_nextAdapterInstance = 1;
nativeaot_vm_uint64 g_adapterInstance = 0;
bool g_initialized = false;

bool checkedAdd(nativeaot_vm_uint64 left, nativeaot_vm_size right,
                nativeaot_vm_uint64* result) {
    const nativeaot_vm_uint64 maximum = ~static_cast<nativeaot_vm_uint64>(0);
    if (result == nullptr || static_cast<nativeaot_vm_uint64>(right) > maximum - left) {
        return false;
    }
    *result = left + static_cast<nativeaot_vm_uint64>(right);
    return true;
}

bool checkedAddAddress(const void* address, nativeaot_vm_size size,
                       nativeaot_vm_uint64* result) {
    return checkedAdd(static_cast<nativeaot_vm_uint64>(
                          reinterpret_cast<uintptr_t>(address)), size, result);
}

bool isAligned(const void* address, nativeaot_vm_size alignment) {
    return alignment != 0 &&
        (reinterpret_cast<uintptr_t>(address) % alignment) == 0;
}

bool validRawRange(void* address, nativeaot_vm_size size) {
    const nativeaot_vm_size page = getPageSize();
    if (address == nullptr || size == 0 || page == 0) return false;
    return isAligned(address, page) && (size % page) == 0;
}

void clearRecordAfterRelease(RegistryRecord& record) {
    record.base = nullptr;
    record.reservedSize = 0;
    record.regionGeneration = 0;
    record.ownerInstance = 0;
    record.reserveCalls = 0;
    record.commitCalls = 0;
    record.decommitCalls = 0;
    record.protectCalls = 0;
    record.releaseCalls = 0;
    record.active = false;
    record.failureCleanup = false;
    record.purpose = nullptr;
    record.handle.record = nullptr;
    record.handle.reservationGeneration = 0;
}

nativeaot_vm_size liveRecordCount() {
    nativeaot_vm_size count = 0;
    for (nativeaot_vm_size index = 0; index < kRegistryCapacity; ++index) {
        if (g_records[index].active) ++count;
    }
    return count;
}

nativeaot_vm_size recordIndex(const RegistryRecord* record) {
    if (record == nullptr) return kRegistryCapacity;
    for (nativeaot_vm_size index = 0; index < kRegistryCapacity; ++index) {
        if (&g_records[index] == record) return index;
    }
    return kRegistryCapacity;
}

RegistryRecord* freeRecord() {
    for (nativeaot_vm_size index = 0; index < kRegistryCapacity; ++index) {
        if (!g_records[index].active) return &g_records[index];
    }
    return nullptr;
}

RegistryRecord* findContaining(void* address, nativeaot_vm_size size) {
    nativeaot_vm_uint64 end = 0;
    if (!validRawRange(address, size) || !checkedAddAddress(address, size, &end)) {
        return nullptr;
    }
    const nativeaot_vm_uint64 start = static_cast<nativeaot_vm_uint64>(
        reinterpret_cast<uintptr_t>(address));
    for (nativeaot_vm_size index = 0; index < kRegistryCapacity; ++index) {
        RegistryRecord& record = g_records[index];
        if (!record.active) continue;
        nativeaot_vm_uint64 recordEnd = 0;
        if (!checkedAddAddress(record.base, record.reservedSize, &recordEnd)) continue;
        const nativeaot_vm_uint64 recordStart = static_cast<nativeaot_vm_uint64>(
            reinterpret_cast<uintptr_t>(record.base));
        if (start >= recordStart && end <= recordEnd) return &record;
    }
    return nullptr;
}

RegistryRecord* findExact(void* base) {
    for (nativeaot_vm_size index = 0; index < kRegistryCapacity; ++index) {
        if (g_records[index].active && g_records[index].base == base) {
            return &g_records[index];
        }
    }
    return nullptr;
}

RegistryRecord* recordForHandle(VirtualMemoryHandle* handle) {
    if (handle == nullptr) return nullptr;
    VirtualMemoryHandleState* state =
        reinterpret_cast<VirtualMemoryHandleState*>(handle);
    const nativeaot_vm_size index = recordIndex(state->record);
    if (index == kRegistryCapacity) return nullptr;
    RegistryRecord& record = g_records[index];
    if (!record.active || state->record != &record ||
        state->reservationGeneration != record.reservationGeneration) {
        return nullptr;
    }
    return &record;
}

void trace(TraceOperation operation, void* address, nativeaot_vm_size size,
           MemoryProtection protection, RegistryRecord* record, VmResult result,
           const VirtualMemoryStats& before,
           const VirtualMemoryStats& after, bool rollback) {
    TraceEvent event{};
    event.sequence = ++g_traceSequence;
    event.operation = operation;
    event.address = address;
    event.size = size;
    event.protection = protection;
    event.slot = recordIndex(record);
    event.reservationGeneration = record == nullptr ? 0 : record->reservationGeneration;
    event.regionGeneration = record == nullptr ? 0 : record->regionGeneration;
    event.frameDelta = static_cast<nativeaot_vm_int64>(after.regionOwnedFrames) -
        static_cast<nativeaot_vm_int64>(before.regionOwnedFrames);
    event.result = result;
    event.rollback = rollback;
    event.liveReservations = liveRecordCount();
    if (g_traceCount < kTraceCapacity) {
        g_trace[g_traceCount++] = event;
    } else {
        const nativeaot_vm_size slot =
            static_cast<nativeaot_vm_size>(event.sequence % kTraceCapacity);
        g_trace[slot] = event;
    }
}

void traceSimple(TraceOperation operation, void* address, nativeaot_vm_size size,
                 MemoryProtection protection, RegistryRecord* record,
                 VmResult result, bool rollback = false) {
    const VirtualMemoryStats stats = gxos::runtime::virtual_memory::stats();
    trace(operation, address, size, protection, record, result, stats, stats,
          rollback);
}

VmResult reserveInternal(nativeaot_vm_size size, nativeaot_vm_size alignment,
                         void* preferredBase, nativeaot_vm_uint32 flags,
                         nativeaot_vm_uint16 node, void** base) {
    if (base == nullptr) return VmResult::InvalidArgument;
    *base = nullptr;
    if (!g_initialized) return VmResult::AlreadyReleased;
    if (flags != 0 && flags != kVirtualReserveWriteWatch) {
        return VmResult::Unsupported;
    }
    if ((flags & kVirtualReserveWriteWatch) != 0 ||
        node != kNumaNodeUndefined) {
        traceSimple(TraceOperation::Reserve, preferredBase, size,
                    MemoryProtection::NoAccess, nullptr, VmResult::Unsupported);
        return VmResult::Unsupported;
    }

    RegistryRecord* record = freeRecord();
    if (record == nullptr) {
        traceSimple(TraceOperation::Reserve, preferredBase, size,
                    MemoryProtection::NoAccess, nullptr, VmResult::OutOfMemory);
        return VmResult::OutOfMemory;
    }
    const VirtualMemoryStats before = gxos::runtime::virtual_memory::stats();
    const VmResult reserved = gxos::runtime::virtual_memory::reserve(
        size, alignment == 0 ? getAllocationGranularity() : alignment,
        preferredBase, &record->region);
    const VirtualMemoryStats after = gxos::runtime::virtual_memory::stats();
    if (reserved != VmResult::Ok) {
        trace(TraceOperation::Reserve, preferredBase, size,
              MemoryProtection::NoAccess, record, reserved, before, after, false);
        return reserved;
    }

    VirtualMemoryInfo info{};
    const VmResult queried = gxos::runtime::virtual_memory::query(
        record->region.base, &info);
    if (queried != VmResult::Ok || !info.reserved ||
        info.regionBase != record->region.base || info.reservedSize == 0 ||
        info.generation == 0) {
        (void)gxos::runtime::virtual_memory::release(record->region);
        trace(TraceOperation::Reserve, preferredBase, size,
              MemoryProtection::NoAccess, record,
              queried == VmResult::Ok ? VmResult::HostFailure : queried,
              before, gxos::runtime::virtual_memory::stats(), true);
        return queried == VmResult::Ok ? VmResult::HostFailure : queried;
    }

    if (g_nextReservationGeneration == 0) ++g_nextReservationGeneration;
    record->reservationGeneration = g_nextReservationGeneration++;
    record->regionGeneration = info.generation;
    record->ownerInstance = g_adapterInstance;
    record->base = info.regionBase;
    record->reservedSize = info.reservedSize;
    record->active = true;
    record->failureCleanup = false;
    record->purpose = "gc-reservation";
    record->handle.record = record;
    record->handle.reservationGeneration = record->reservationGeneration;
    *base = record->base;
    trace(TraceOperation::Reserve, record->base, record->reservedSize,
          MemoryProtection::NoAccess, record, VmResult::Ok, before, after, false);
    return VmResult::Ok;
}

VmResult rangeOperation(TraceOperation operation, void* address,
                        nativeaot_vm_size size, MemoryProtection protection,
                        nativeaot_vm_uint16 node,
                        VmResult (*operationCall)(RegistryRecord&, nativeaot_vm_size,
                                                  nativeaot_vm_size, MemoryProtection),
                        nativeaot_vm_uint64 RegistryRecord::*counter) {
    if (!g_initialized) return VmResult::AlreadyReleased;
    if (node != kNumaNodeUndefined) return VmResult::Unsupported;
    RegistryRecord* record = findContaining(address, size);
    if (record == nullptr) {
        traceSimple(operation, address, size, protection, nullptr,
                    VmResult::NotOwned);
        return VmResult::NotOwned;
    }
    ++(record->*counter);
    const nativeaot_vm_size offset = static_cast<nativeaot_vm_size>(
        reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(record->base));
    const VirtualMemoryStats before = gxos::runtime::virtual_memory::stats();
    const VmResult result = operationCall(*record, offset, size, protection);
    const VirtualMemoryStats after = gxos::runtime::virtual_memory::stats();
    trace(operation, address, size, protection, record, result, before, after,
          result != VmResult::Ok && operation == TraceOperation::Commit);
    return result;
}

VmResult commitCall(RegistryRecord& record, nativeaot_vm_size offset,
                    nativeaot_vm_size size, MemoryProtection protection) {
    return gxos::runtime::virtual_memory::commit(record.region, offset, size,
                                                  protection);
}

VmResult decommitCall(RegistryRecord& record, nativeaot_vm_size offset,
                      nativeaot_vm_size size, MemoryProtection) {
    return gxos::runtime::virtual_memory::decommit(record.region, offset, size);
}

VmResult protectCall(RegistryRecord& record, nativeaot_vm_size offset,
                     nativeaot_vm_size size, MemoryProtection protection) {
    return gxos::runtime::virtual_memory::protect(record.region, offset, size,
                                                  protection);
}

} // namespace

// Keep the public handle opaque while using the bounded record as its storage.
struct VirtualMemoryHandle {
    RegistryRecord* record;
    nativeaot_vm_uint64 reservationGeneration;
};

VmResult initializeVirtualMemoryAdapter() {
    if (g_initialized) {
        traceSimple(TraceOperation::Initialize, nullptr, 0,
                    MemoryProtection::NoAccess, nullptr,
                    VmResult::AlreadyReserved);
        return VmResult::AlreadyReserved;
    }
    const nativeaot_vm_size page = getPageSize();
    const nativeaot_vm_size granularity = getAllocationGranularity();
    const bool available = page != 0 && granularity >= page &&
        gxos::runtime::virtual_memory::maximumRegionSize() >= page;
    if (!available || !trueReservationSemantics()) {
        traceSimple(TraceOperation::Initialize, nullptr, 0,
                    MemoryProtection::NoAccess, nullptr, VmResult::Unsupported);
        return VmResult::Unsupported;
    }
    if (g_nextAdapterInstance == 0) ++g_nextAdapterInstance;
    g_adapterInstance = g_nextAdapterInstance++;
    g_initialized = true;
    traceSimple(TraceOperation::Initialize, nullptr, 0,
                MemoryProtection::NoAccess, nullptr, VmResult::Ok);
    return VmResult::Ok;
}

VmResult shutdownVirtualMemoryAdapter(bool failureCleanup) {
    if (!g_initialized) return VmResult::AlreadyReleased;
    if (liveRecordCount() != 0 && !failureCleanup) {
        traceSimple(TraceOperation::Shutdown, nullptr, 0,
                    MemoryProtection::NoAccess, nullptr, VmResult::NotOwned);
        return VmResult::NotOwned;
    }
    if (failureCleanup) {
        for (nativeaot_vm_size index = 0; index < kRegistryCapacity; ++index) {
            RegistryRecord& record = g_records[index];
            if (!record.active) continue;
            record.failureCleanup = true;
            ++record.releaseCalls;
            const VmResult released = gxos::runtime::virtual_memory::release(
                record.region);
            if (released != VmResult::Ok) {
                traceSimple(TraceOperation::Shutdown, record.base,
                            record.reservedSize, MemoryProtection::NoAccess,
                            &record, released, true);
                return released;
            }
            clearRecordAfterRelease(record);
        }
    }
    if (liveRecordCount() != 0) return VmResult::NotOwned;
    g_initialized = false;
    g_adapterInstance = 0;
    traceSimple(TraceOperation::Shutdown, nullptr, 0,
                MemoryProtection::NoAccess, nullptr, VmResult::Ok);
    return VmResult::Ok;
}

bool virtualMemoryAdapterInitialized() { return g_initialized; }

RegistryStats registryStats() {
    RegistryStats result{};
    result.capacity = kRegistryCapacity;
    result.liveReservations = liveRecordCount();
    result.traceEntries = g_traceCount;
    result.adapterInstance = g_adapterInstance;
    result.nextReservationGeneration = g_nextReservationGeneration;
    return result;
}

bool queryReservationDiagnostics(void* base, ReservationDiagnostics* diagnostics) {
    if (diagnostics == nullptr) return false;
    *diagnostics = ReservationDiagnostics{};
    RegistryRecord* record = findExact(base);
    if (record == nullptr) return false;
    diagnostics->base = record->base;
    diagnostics->reservedSize = record->reservedSize;
    diagnostics->reservationGeneration = record->reservationGeneration;
    diagnostics->regionGeneration = record->regionGeneration;
    diagnostics->reserveCalls = record->reserveCalls;
    diagnostics->commitCalls = record->commitCalls;
    diagnostics->decommitCalls = record->decommitCalls;
    diagnostics->protectCalls = record->protectCalls;
    diagnostics->releaseCalls = record->releaseCalls;
    diagnostics->active = record->active;
    diagnostics->failureCleanup = record->failureCleanup;
    return true;
}

bool reservationIdentityIsActive(void* base,
                                 nativeaot_vm_uint64 reservationGeneration) {
    RegistryRecord* record = findExact(base);
    return record != nullptr &&
        record->reservationGeneration == reservationGeneration &&
        record->ownerInstance == g_adapterInstance;
}

nativeaot_vm_size traceEventCount() { return g_traceCount; }

bool traceEventAt(nativeaot_vm_size index, TraceEvent* event) {
    if (event == nullptr || index >= g_traceCount) return false;
    *event = g_trace[index];
    return true;
}

VmResult reserveVirtualMemoryRaw(nativeaot_vm_size size, nativeaot_vm_size alignment,
                                 nativeaot_vm_uint32 flags,
                                 nativeaot_vm_uint16 node, void** base) {
    return reserveInternal(size, alignment, nullptr, flags, node, base);
}

VmResult reserveVirtualMemoryAt(nativeaot_vm_size size, nativeaot_vm_size alignment,
                                void* preferredBase, nativeaot_vm_uint32 flags,
                                nativeaot_vm_uint16 node, void** base) {
    return reserveInternal(size, alignment, preferredBase, flags, node, base);
}

bool commitVirtualMemoryRaw(void* address, nativeaot_vm_size size,
                            nativeaot_vm_uint16 node) {
    return rangeOperation(TraceOperation::Commit, address, size,
                          MemoryProtection::ReadWrite, node, commitCall,
                          &RegistryRecord::commitCalls) == VmResult::Ok;
}

bool decommitVirtualMemoryRaw(void* address, nativeaot_vm_size size) {
    return rangeOperation(TraceOperation::Decommit, address, size,
                          MemoryProtection::NoAccess, kNumaNodeUndefined,
                          decommitCall, &RegistryRecord::decommitCalls) == VmResult::Ok;
}

bool protectVirtualMemoryRaw(void* address, nativeaot_vm_size size,
                             MemoryProtection protection) {
    return rangeOperation(TraceOperation::Protect, address, size, protection,
                          kNumaNodeUndefined, protectCall,
                          &RegistryRecord::protectCalls) == VmResult::Ok;
}

bool releaseVirtualMemoryRaw(void* address, nativeaot_vm_size size) {
    if (!g_initialized) return false;
    RegistryRecord* record = findExact(address);
    if (record == nullptr || (size != 0 && size != record->reservedSize)) {
        traceSimple(TraceOperation::Release, address, size,
                    MemoryProtection::NoAccess, record,
                    record == nullptr ? VmResult::NotOwned : VmResult::OutOfRange);
        return false;
    }
    ++record->releaseCalls;
    const VirtualMemoryStats before = gxos::runtime::virtual_memory::stats();
    const VmResult result = gxos::runtime::virtual_memory::release(record->region);
    const VirtualMemoryStats after = gxos::runtime::virtual_memory::stats();
    trace(TraceOperation::Release, address, size, MemoryProtection::NoAccess,
          record, result, before, after, false);
    if (result == VmResult::Ok) clearRecordAfterRelease(*record);
    return result == VmResult::Ok;
}

VmResult resetVirtualMemoryRaw(void* address, nativeaot_vm_size size,
                               bool unlock) {
    (void)unlock;
    RegistryRecord* record = findContaining(address, size);
    if (record == nullptr) {
        traceSimple(TraceOperation::Reset, address, size,
                    MemoryProtection::NoAccess, nullptr, VmResult::NotOwned);
        return VmResult::NotOwned;
    }
    traceSimple(TraceOperation::Reset, address, size,
                MemoryProtection::ReadWrite, record, VmResult::Unsupported);
    return VmResult::Unsupported;
}

VmResult queryVirtualMemoryRaw(void* address, VirtualMemoryInfo* information) {
    if (!g_initialized || address == nullptr || information == nullptr) {
        return g_initialized ? VmResult::InvalidArgument : VmResult::AlreadyReleased;
    }
    RegistryRecord* record = findContaining(address, getPageSize());
    if (record == nullptr) return VmResult::NotOwned;
    *information = VirtualMemoryInfo{};
    const VmResult result = gxos::runtime::virtual_memory::query(address, information);
    traceSimple(TraceOperation::Query, address, getPageSize(),
                information->protection, record, result);
    if (result != VmResult::Ok || information->regionBase != record->base ||
        information->reservedSize != record->reservedSize ||
        information->generation != record->regionGeneration) {
        return VmResult::HostFailure;
    }
    return VmResult::Ok;
}

void* gcVirtualReserve(nativeaot_vm_size size, nativeaot_vm_size alignment,
                       nativeaot_vm_uint32 flags, nativeaot_vm_uint16 node) {
    void* base = nullptr;
    return reserveVirtualMemoryRaw(size, alignment, flags, node, &base) == VmResult::Ok
        ? base : nullptr;
}

bool gcVirtualCommit(void* address, nativeaot_vm_size size,
                     nativeaot_vm_uint16 node) {
    return commitVirtualMemoryRaw(address, size, node);
}

bool gcVirtualDecommit(void* address, nativeaot_vm_size size) {
    return decommitVirtualMemoryRaw(address, size);
}

bool gcVirtualRelease(void* address, nativeaot_vm_size size) {
    return releaseVirtualMemoryRaw(address, size);
}

bool gcVirtualReset(void* address, nativeaot_vm_size size, bool unlock) {
    return resetVirtualMemoryRaw(address, size, unlock) == VmResult::Ok;
}

bool supportsWriteWatch() { return false; }

void resetWriteWatch(void* address, nativeaot_vm_size size) {
    (void)address;
    (void)size;
}

bool getWriteWatch(bool resetState, void* address, nativeaot_vm_size size,
                   void** pageAddresses, nativeaot_vm_size* pageAddressesCount) {
    (void)resetState;
    (void)address;
    (void)size;
    (void)pageAddresses;
    (void)pageAddressesCount;
    return false;
}

void* reserveAndCommitLargePages(nativeaot_vm_size size,
                                 nativeaot_vm_uint16 node) {
    (void)size;
    (void)node;
    return nullptr;
}

nativeaot_vm_size getLargePageSize() { return 0; }

bool getMemoryStatus(nativeaot_vm_uint64 restrictedLimit, MemoryStatus* status) {
    if (status == nullptr) return false;
    *status = MemoryStatus{};
    const VirtualMemoryStats vm = gxos::runtime::virtual_memory::stats();
    status->virtualMemoryLimit = gxos::runtime::virtual_memory::maximumRegionSize();
    // The hosted generic backend intentionally has no physical-frame ledger.
    // Returning false there prevents the adapter from fabricating host memory.
    if (!vm.physicalBackingAccounting || vm.totalKnownFrames == 0) return false;
    const nativeaot_vm_uint64 maximum = ~static_cast<nativeaot_vm_uint64>(0);
    const nativeaot_vm_uint64 frameCount =
        static_cast<nativeaot_vm_uint64>(vm.totalKnownFrames);
    const nativeaot_vm_uint64 page = static_cast<nativeaot_vm_uint64>(getPageSize());
    if (page == 0 || frameCount > maximum / page) return false;
    nativeaot_vm_uint64 physicalLimit = frameCount * page;
    if (restrictedLimit != 0 && restrictedLimit < physicalLimit) {
        physicalLimit = restrictedLimit;
        status->physicalLimitRestricted = true;
    }
    status->physicalLimit = physicalLimit;
    const nativeaot_vm_uint64 freeFrames =
        static_cast<nativeaot_vm_uint64>(vm.freeFrames);
    status->availablePhysical = freeFrames > maximum / page
        ? physicalLimit : freeFrames * page;
    if (status->availablePhysical > physicalLimit) status->availablePhysical = physicalLimit;
    status->memoryLoad = physicalLimit == 0 ? 100u :
        static_cast<nativeaot_vm_uint32>(
            (static_cast<nativeaot_vm_uint64>(vm.allocatedFrames) * 100u) /
            static_cast<nativeaot_vm_uint64>(vm.totalKnownFrames));
    status->availablePageFile = status->availablePhysical;
    return true;
}

nativeaot_vm_size gcGetVirtualMemoryLimit() {
    return gxos::runtime::virtual_memory::maximumRegionSize();
}

nativeaot_vm_uint64 gcGetPhysicalMemoryLimit(bool* restricted) {
    MemoryStatus status{};
    const bool available = getMemoryStatus(0, &status);
    if (restricted != nullptr) *restricted = available && status.physicalLimitRestricted;
    return available ? status.physicalLimit : 0;
}

void gcGetMemoryStatus(nativeaot_vm_uint64 restrictedLimit,
                       nativeaot_vm_uint32* memoryLoad,
                       nativeaot_vm_uint64* availablePhysical,
                       nativeaot_vm_uint64* availablePageFile) {
    MemoryStatus status{};
    (void)getMemoryStatus(restrictedLimit, &status);
    if (memoryLoad != nullptr) *memoryLoad = status.memoryLoad;
    if (availablePhysical != nullptr) *availablePhysical = status.availablePhysical;
    if (availablePageFile != nullptr) *availablePageFile = status.availablePageFile;
}

nativeaot_vm_size gcGetPageSize() {
    return getPageSize();
}

VmResult reserveVirtualMemory(nativeaot_vm_size size, nativeaot_vm_size alignment,
                              void* preferredBase, VirtualMemoryHandle** handle,
                              bool requireTrueReservation) {
    if (handle == nullptr) return VmResult::InvalidArgument;
    if (*handle != nullptr) return VmResult::AlreadyReserved;
    if (requireTrueReservation && !trueReservationSemantics()) {
        return VmResult::Unsupported;
    }
    void* base = nullptr;
    const VmResult result = reserveVirtualMemoryAt(
        size, alignment, preferredBase, 0, kNumaNodeUndefined, &base);
    if (result != VmResult::Ok) return result;
    RegistryRecord* record = findExact(base);
    if (record == nullptr) {
        (void)releaseVirtualMemoryRaw(base, 0);
        return VmResult::HostFailure;
    }
    *handle = reinterpret_cast<VirtualMemoryHandle*>(&record->handle);
    return VmResult::Ok;
}

VmResult commitVirtualMemory(VirtualMemoryHandle* handle, nativeaot_vm_size offset,
                             nativeaot_vm_size size, MemoryProtection protection) {
    RegistryRecord* record = recordForHandle(handle);
    if (record == nullptr) return VmResult::AlreadyReleased;
    if (offset > record->reservedSize || size > record->reservedSize - offset) {
        return VmResult::OutOfRange;
    }
    const nativeaot_vm_uint64 addressValue =
        static_cast<nativeaot_vm_uint64>(reinterpret_cast<uintptr_t>(record->base)) + offset;
    return rangeOperation(TraceOperation::Commit,
                          reinterpret_cast<void*>(static_cast<uintptr_t>(addressValue)),
                          size, protection, kNumaNodeUndefined, commitCall,
                          &RegistryRecord::commitCalls);
}

VmResult decommitVirtualMemory(VirtualMemoryHandle* handle, nativeaot_vm_size offset,
                               nativeaot_vm_size size) {
    RegistryRecord* record = recordForHandle(handle);
    if (record == nullptr) return VmResult::AlreadyReleased;
    if (offset > record->reservedSize || size > record->reservedSize - offset) {
        return VmResult::OutOfRange;
    }
    const nativeaot_vm_uint64 addressValue =
        static_cast<nativeaot_vm_uint64>(reinterpret_cast<uintptr_t>(record->base)) + offset;
    return rangeOperation(TraceOperation::Decommit,
                          reinterpret_cast<void*>(static_cast<uintptr_t>(addressValue)),
                          size, MemoryProtection::NoAccess, kNumaNodeUndefined,
                          decommitCall, &RegistryRecord::decommitCalls);
}

VmResult releaseVirtualMemory(VirtualMemoryHandle** handle) {
    if (handle == nullptr) return VmResult::InvalidArgument;
    if (*handle == nullptr) return VmResult::AlreadyReleased;
    RegistryRecord* record = recordForHandle(*handle);
    if (record == nullptr) return VmResult::AlreadyReleased;
    const VmResult result = releaseVirtualMemoryRaw(record->base, record->reservedSize)
        ? VmResult::Ok : VmResult::HostFailure;
    if (result == VmResult::Ok) *handle = nullptr;
    return result;
}

VmResult resetVirtualMemory(VirtualMemoryHandle* handle, nativeaot_vm_size offset,
                            nativeaot_vm_size size, bool unlock) {
    RegistryRecord* record = recordForHandle(handle);
    if (record == nullptr) return VmResult::AlreadyReleased;
    if (offset > record->reservedSize || size > record->reservedSize - offset) {
        return VmResult::OutOfRange;
    }
    const nativeaot_vm_uint64 addressValue =
        static_cast<nativeaot_vm_uint64>(reinterpret_cast<uintptr_t>(record->base)) + offset;
    return resetVirtualMemoryRaw(
        reinterpret_cast<void*>(static_cast<uintptr_t>(addressValue)), size, unlock);
}

void* baseAddress(VirtualMemoryHandle* handle) {
    RegistryRecord* record = recordForHandle(handle);
    return record == nullptr ? nullptr : record->base;
}

nativeaot_vm_size getPageSize() {
    return gxos::runtime::virtual_memory::pageSize();
}

nativeaot_vm_size getAllocationGranularity() {
    return gxos::runtime::virtual_memory::allocationGranularity();
}

bool memoryAvailable(nativeaot_vm_size size) {
    return size != 0 && size <= gxos::runtime::virtual_memory::maximumRegionSize();
}

bool supportsLargePages() { return false; }
bool supportsNumaPlacement() { return false; }

bool trueReservationSemantics() {
    return gxos::runtime::virtual_memory::stats().trueReservation;
}

const char* backendModeName() {
    return trueReservationSemantics() ? "true-reservation" : "eager-compatibility";
}

const char* traceOperationName(TraceOperation operation) {
    switch (operation) {
    case TraceOperation::Initialize: return "initialize";
    case TraceOperation::Shutdown: return "shutdown";
    case TraceOperation::Reserve: return "reserve";
    case TraceOperation::Commit: return "commit";
    case TraceOperation::Decommit: return "decommit";
    case TraceOperation::Protect: return "protect";
    case TraceOperation::Release: return "release";
    case TraceOperation::Reset: return "reset";
    case TraceOperation::Query: return "query";
    }
    return "unknown";
}

} // namespace virtual_memory
} // namespace nativeaot
} // namespace guidexos
