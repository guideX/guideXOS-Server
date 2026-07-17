#include "guidexos_nativeaot_virtual_memory_adapter.h"

#include <new>

namespace guidexos {
namespace nativeaot {
namespace virtual_memory {

struct VirtualMemoryHandle {
    gxos::runtime::virtual_memory::VirtualMemoryRegion region;
};

VmResult reserveVirtualMemory(std::size_t size, std::size_t alignment,
                              void* preferredBase, VirtualMemoryHandle** handle,
                              bool requireTrueReservation) {
    if (handle == nullptr) return VmResult::InvalidArgument;
    if (*handle != nullptr) return VmResult::AlreadyReserved;
    if (requireTrueReservation && !trueReservationSemantics()) {
        return VmResult::Unsupported;
    }
    VirtualMemoryHandle* result = new (std::nothrow) VirtualMemoryHandle();
    if (result == nullptr) return VmResult::OutOfMemory;
    const VmResult status = gxos::runtime::virtual_memory::reserve(
        size, alignment, preferredBase, &result->region);
    if (status != VmResult::Ok) {
        delete result;
        return status;
    }
    *handle = result;
    return VmResult::Ok;
}

VmResult commitVirtualMemory(VirtualMemoryHandle* handle, std::size_t offset,
                             std::size_t size, MemoryProtection protection) {
    if (handle == nullptr) return VmResult::InvalidArgument;
    return gxos::runtime::virtual_memory::commit(handle->region, offset, size, protection);
}

VmResult decommitVirtualMemory(VirtualMemoryHandle* handle, std::size_t offset,
                               std::size_t size) {
    if (handle == nullptr) return VmResult::InvalidArgument;
    return gxos::runtime::virtual_memory::decommit(handle->region, offset, size);
}

VmResult releaseVirtualMemory(VirtualMemoryHandle** handle) {
    if (handle == nullptr) return VmResult::InvalidArgument;
    if (*handle == nullptr) return VmResult::AlreadyReleased;
    const VmResult status = gxos::runtime::virtual_memory::release((*handle)->region);
    if (status == VmResult::Ok) {
        delete *handle;
        *handle = nullptr;
    }
    return status;
}

VmResult resetVirtualMemory(VirtualMemoryHandle* handle, std::size_t offset,
                            std::size_t size, bool unlock) {
    (void)handle;
    (void)offset;
    (void)size;
    (void)unlock;
    // The matching collector source defines reset as discard-while-committed.
    // The generic API intentionally has no discard operation yet; returning a
    // precise unsupported result avoids silently changing it into decommit.
    return VmResult::Unsupported;
}

void* baseAddress(VirtualMemoryHandle* handle) {
    return handle == nullptr ? nullptr : handle->region.base;
}

std::size_t getPageSize() {
    return gxos::runtime::virtual_memory::pageSize();
}

std::size_t getAllocationGranularity() {
    return gxos::runtime::virtual_memory::allocationGranularity();
}

bool memoryAvailable(std::size_t size) {
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

} // namespace virtual_memory
} // namespace nativeaot
} // namespace guidexos
