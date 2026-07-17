#pragma once

#if defined(GXOS_BARE_METAL)
#include <stddef.h>
#include <stdint.h>
using gxos_vm_size = size_t;
using gxos_vm_uint8 = uint8_t;
using gxos_vm_uint64 = uint64_t;
#else
#include <cstddef>
#include <cstdint>
using gxos_vm_size = std::size_t;
using gxos_vm_uint8 = std::uint8_t;
using gxos_vm_uint64 = std::uint64_t;
#endif

namespace gxos {
namespace runtime {
namespace virtual_memory {

namespace detail {
struct RegionAccess;
}

// Results deliberately describe the guideXOS contract rather than exposing
// host-specific error codes.
enum class VmResult : gxos_vm_uint8 {
    Ok = 0,
    InvalidArgument,
    AlreadyReserved,
    AlreadyReleased,
    RangeOverflow,
    AlignmentError,
    AddressUnavailable,
    OutOfMemory,
    OutOfRange,
    NotOwned,
    NotFound,
    NotCommitted,
    ProtectionUnsupported,
    Unsupported,
    HostFailure
};

enum class MemoryProtection : gxos_vm_uint8 {
    NoAccess = 0,
    ReadOnly,
    ReadWrite,
    ReadExecute,
    ReadWriteExecute
};

struct VirtualMemoryInfo {
    void* regionBase = nullptr;
    void* pageBase = nullptr;
    gxos_vm_size pageSize = 0;
    gxos_vm_size regionSize = 0;
    gxos_vm_size reservedSize = 0;
    gxos_vm_size committedSize = 0;
    gxos_vm_uint64 generation = 0;
    bool reserved = false;
    bool committed = false;
    bool mappingPresent = false;
    gxos_vm_uint64 physicalFrame = 0;
    MemoryProtection protection = MemoryProtection::NoAccess;
};

struct VirtualMemoryStats {
    gxos_vm_size activeRegions = 0;
    gxos_vm_size committedPages = 0;
    gxos_vm_size totalKnownFrames = 0;
    gxos_vm_size freeFrames = 0;
    gxos_vm_size allocatedFrames = 0;
    gxos_vm_size regionOwnedFrames = 0;
    gxos_vm_size pageTableFrames = 0;
    gxos_vm_size framesReleasedByDecommit = 0;
    gxos_vm_size framesReleasedByRelease = 0;
    gxos_vm_size tlbInvalidations = 0;
    gxos_vm_size activeMetadataEntries = 0;
    gxos_vm_size metadataCapacity = 0;
    gxos_vm_size mappingCount = 0;
    bool trueReservation = false;
    bool physicalBackingAccounting = false;
    bool protectionEnforced = false;
};

// A region is an owning, non-copyable capability.  The opaque state and
// generation are intentionally private so callers cannot manufacture a valid
// ownership token or reuse one after release.
class VirtualMemoryRegion {
public:
    void* base;
    gxos_vm_size reservedSize;
    gxos_vm_size committedSize;

    VirtualMemoryRegion();
    ~VirtualMemoryRegion();

    VirtualMemoryRegion(const VirtualMemoryRegion&) = delete;
    VirtualMemoryRegion& operator=(const VirtualMemoryRegion&) = delete;
    VirtualMemoryRegion(VirtualMemoryRegion&&) = delete;
    VirtualMemoryRegion& operator=(VirtualMemoryRegion&&) = delete;

private:
    void* opaqueState;
    gxos_vm_uint64 generation;

    friend VmResult reserve(gxos_vm_size, gxos_vm_size, void*, VirtualMemoryRegion*);
    friend VmResult commit(VirtualMemoryRegion&, gxos_vm_size, gxos_vm_size, MemoryProtection);
    friend VmResult decommit(VirtualMemoryRegion&, gxos_vm_size, gxos_vm_size);
    friend VmResult protect(VirtualMemoryRegion&, gxos_vm_size, gxos_vm_size, MemoryProtection);
    friend VmResult release(VirtualMemoryRegion&);
    friend VmResult query(const void*, VirtualMemoryInfo*);
    friend struct detail::RegionAccess;
};

VmResult reserve(gxos_vm_size size,
                 gxos_vm_size alignment,
                 void* preferredBase,
                 VirtualMemoryRegion* region);

VmResult commit(VirtualMemoryRegion& region,
                gxos_vm_size offset,
                gxos_vm_size size,
                MemoryProtection protection);

VmResult decommit(VirtualMemoryRegion& region,
                  gxos_vm_size offset,
                  gxos_vm_size size);

VmResult protect(VirtualMemoryRegion& region,
                 gxos_vm_size offset,
                 gxos_vm_size size,
                 MemoryProtection protection);

VmResult release(VirtualMemoryRegion& region);

VmResult query(const void* address, VirtualMemoryInfo* information);

// Narrow current-address-space teardown hook used by the kernel smoke and
// future process teardown integration. It invalidates all active region state
// and releases committed backing before destroying the current owner.
VmResult teardownAddressSpace();

gxos_vm_size pageSize();
gxos_vm_size allocationGranularity();
gxos_vm_size maximumRegionSize();
VirtualMemoryStats stats();
bool protectionIsEnforced();

const char* vmResultName(VmResult result);
const char* memoryProtectionName(MemoryProtection protection);
const char* lastDiagnostic();

} // namespace virtual_memory
} // namespace runtime
} // namespace gxos
