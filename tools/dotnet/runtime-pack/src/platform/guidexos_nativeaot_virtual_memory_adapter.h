#pragma once

#if defined(GXOS_BARE_METAL)
#include <stddef.h>
#include <stdint.h>
using nativeaot_vm_size = size_t;
using nativeaot_vm_uint16 = uint16_t;
using nativeaot_vm_uint32 = uint32_t;
using nativeaot_vm_uint64 = uint64_t;
using nativeaot_vm_int64 = int64_t;
#else
#include <cstddef>
#include <cstdint>
using nativeaot_vm_size = std::size_t;
using nativeaot_vm_uint16 = std::uint16_t;
using nativeaot_vm_uint32 = std::uint32_t;
using nativeaot_vm_uint64 = std::uint64_t;
using nativeaot_vm_int64 = std::int64_t;
#endif

#include "../../../../../runtime/memory/guidexos_virtual_memory_region.h"

namespace guidexos {
namespace nativeaot {
namespace virtual_memory {

using gxos::runtime::virtual_memory::MemoryProtection;
using gxos::runtime::virtual_memory::VmResult;

constexpr nativeaot_vm_uint32 kVirtualReserveWriteWatch = 1u;
constexpr nativeaot_vm_uint16 kNumaNodeUndefined = 0xFFFFu;

struct VirtualMemoryHandle;

struct MemoryStatus {
    nativeaot_vm_uint32 memoryLoad = 0;
    nativeaot_vm_uint64 availablePhysical = 0;
    nativeaot_vm_uint64 availablePageFile = 0;
    nativeaot_vm_uint64 physicalLimit = 0;
    nativeaot_vm_size virtualMemoryLimit = 0;
    bool physicalLimitRestricted = false;
};

struct RegistryStats {
    nativeaot_vm_size capacity = 0;
    nativeaot_vm_size liveReservations = 0;
    nativeaot_vm_size traceEntries = 0;
    nativeaot_vm_uint64 adapterInstance = 0;
    nativeaot_vm_uint64 nextReservationGeneration = 0;
};

struct ReservationDiagnostics {
    void* base = nullptr;
    nativeaot_vm_size reservedSize = 0;
    nativeaot_vm_uint64 reservationGeneration = 0;
    nativeaot_vm_uint64 regionGeneration = 0;
    nativeaot_vm_uint64 reserveCalls = 0;
    nativeaot_vm_uint64 commitCalls = 0;
    nativeaot_vm_uint64 decommitCalls = 0;
    nativeaot_vm_uint64 protectCalls = 0;
    nativeaot_vm_uint64 releaseCalls = 0;
    bool active = false;
    bool failureCleanup = false;
};

enum class TraceOperation : nativeaot_vm_uint32 {
    Initialize,
    Shutdown,
    Reserve,
    Commit,
    Decommit,
    Protect,
    Release,
    Reset,
    Query
};

struct TraceEvent {
    nativeaot_vm_uint64 sequence = 0;
    TraceOperation operation = TraceOperation::Initialize;
    void* address = nullptr;
    nativeaot_vm_size size = 0;
    MemoryProtection protection = MemoryProtection::NoAccess;
    nativeaot_vm_size slot = 0;
    nativeaot_vm_uint64 reservationGeneration = 0;
    nativeaot_vm_uint64 regionGeneration = 0;
    nativeaot_vm_int64 frameDelta = 0;
    VmResult result = VmResult::InvalidArgument;
    bool rollback = false;
    nativeaot_vm_size liveReservations = 0;
};

// The adapter is application-scoped and deliberately bounded. A successful
// shutdown requires an empty registry. The failure-cleanup argument is only
// for a failed collector startup path and is never implicit.
VmResult initializeVirtualMemoryAdapter();
VmResult shutdownVirtualMemoryAdapter(bool failureCleanup = false);
bool virtualMemoryAdapterInitialized();
RegistryStats registryStats();
bool queryReservationDiagnostics(void* base, ReservationDiagnostics* diagnostics);
bool reservationIdentityIsActive(void* base,
                                 nativeaot_vm_uint64 reservationGeneration);
nativeaot_vm_size traceEventCount();
bool traceEventAt(nativeaot_vm_size index, TraceEvent* event);

// Raw-address PAL-shaped operations. The result-returning reserve form is used
// by probes; gcVirtual* mirrors the source GCToOSInterface signatures.
VmResult reserveVirtualMemoryRaw(nativeaot_vm_size size,
                                 nativeaot_vm_size alignment,
                                 nativeaot_vm_uint32 flags,
                                 nativeaot_vm_uint16 node,
                                 void** base);
VmResult reserveVirtualMemoryAt(nativeaot_vm_size size,
                                nativeaot_vm_size alignment,
                                void* preferredBase,
                                nativeaot_vm_uint32 flags,
                                nativeaot_vm_uint16 node,
                                void** base);
bool commitVirtualMemoryRaw(void* address, nativeaot_vm_size size,
                            nativeaot_vm_uint16 node = kNumaNodeUndefined);
bool decommitVirtualMemoryRaw(void* address, nativeaot_vm_size size);
bool protectVirtualMemoryRaw(void* address, nativeaot_vm_size size,
                             MemoryProtection protection);
bool releaseVirtualMemoryRaw(void* address, nativeaot_vm_size size);
VmResult resetVirtualMemoryRaw(void* address, nativeaot_vm_size size,
                               bool unlock);
VmResult queryVirtualMemoryRaw(void* address,
                               gxos::runtime::virtual_memory::VirtualMemoryInfo* information);

void* gcVirtualReserve(nativeaot_vm_size size, nativeaot_vm_size alignment,
                       nativeaot_vm_uint32 flags,
                       nativeaot_vm_uint16 node = kNumaNodeUndefined);
bool gcVirtualCommit(void* address, nativeaot_vm_size size,
                     nativeaot_vm_uint16 node = kNumaNodeUndefined);
bool gcVirtualDecommit(void* address, nativeaot_vm_size size);
bool gcVirtualRelease(void* address, nativeaot_vm_size size);
bool gcVirtualReset(void* address, nativeaot_vm_size size, bool unlock);

// The selected one-node Workstation configuration deliberately does not enter
// large pages, NUMA, or write-watch paths.
bool supportsWriteWatch();
void resetWriteWatch(void* address, nativeaot_vm_size size);
bool getWriteWatch(bool resetState, void* address, nativeaot_vm_size size,
                   void** pageAddresses, nativeaot_vm_size* pageAddressesCount);
void* reserveAndCommitLargePages(nativeaot_vm_size size,
                                 nativeaot_vm_uint16 node = kNumaNodeUndefined);
nativeaot_vm_size getLargePageSize();
bool getMemoryStatus(nativeaot_vm_uint64 restrictedLimit,
                      MemoryStatus* status);
nativeaot_vm_size gcGetVirtualMemoryLimit();
nativeaot_vm_uint64 gcGetPhysicalMemoryLimit(bool* restricted = nullptr);
void gcGetMemoryStatus(nativeaot_vm_uint64 restrictedLimit,
                       nativeaot_vm_uint32* memoryLoad,
                       nativeaot_vm_uint64* availablePhysical,
                       nativeaot_vm_uint64* availablePageFile);
nativeaot_vm_size gcGetPageSize();

// Legacy handle-shaped probe API. It is backed by the same registry and is
// retained only for the inactive adapter tests.
VmResult reserveVirtualMemory(nativeaot_vm_size size,
                              nativeaot_vm_size alignment,
                              void* preferredBase,
                              VirtualMemoryHandle** handle,
                              bool requireTrueReservation = false);

VmResult commitVirtualMemory(VirtualMemoryHandle* handle,
                             nativeaot_vm_size offset,
                             nativeaot_vm_size size,
                             MemoryProtection protection);

VmResult decommitVirtualMemory(VirtualMemoryHandle* handle,
                               nativeaot_vm_size offset,
                               nativeaot_vm_size size);

VmResult releaseVirtualMemory(VirtualMemoryHandle** handle);

VmResult resetVirtualMemory(VirtualMemoryHandle* handle,
                            nativeaot_vm_size offset,
                            nativeaot_vm_size size,
                            bool unlock);

void* baseAddress(VirtualMemoryHandle* handle);

nativeaot_vm_size getPageSize();
nativeaot_vm_size getAllocationGranularity();
bool memoryAvailable(nativeaot_vm_size size);
bool supportsLargePages();
bool supportsNumaPlacement();
bool trueReservationSemantics();
const char* backendModeName();
const char* traceOperationName(TraceOperation operation);

} // namespace virtual_memory
} // namespace nativeaot
} // namespace guidexos
