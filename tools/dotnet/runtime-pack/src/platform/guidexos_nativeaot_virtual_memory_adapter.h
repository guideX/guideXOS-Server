#pragma once

#include <cstddef>

#include "../../../../../runtime/memory/guidexos_virtual_memory_region.h"

namespace guidexos {
namespace nativeaot {
namespace virtual_memory {

using gxos::runtime::virtual_memory::MemoryProtection;
using gxos::runtime::virtual_memory::VmResult;

struct VirtualMemoryHandle;

VmResult reserveVirtualMemory(std::size_t size,
                              std::size_t alignment,
                              void* preferredBase,
                              VirtualMemoryHandle** handle,
                              bool requireTrueReservation = false);

VmResult commitVirtualMemory(VirtualMemoryHandle* handle,
                             std::size_t offset,
                             std::size_t size,
                             MemoryProtection protection);

VmResult decommitVirtualMemory(VirtualMemoryHandle* handle,
                               std::size_t offset,
                               std::size_t size);

VmResult releaseVirtualMemory(VirtualMemoryHandle** handle);

VmResult resetVirtualMemory(VirtualMemoryHandle* handle,
                            std::size_t offset,
                            std::size_t size,
                            bool unlock);

void* baseAddress(VirtualMemoryHandle* handle);

std::size_t getPageSize();
std::size_t getAllocationGranularity();
bool memoryAvailable(std::size_t size);
bool supportsLargePages();
bool supportsNumaPlacement();
bool trueReservationSemantics();
const char* backendModeName();

} // namespace virtual_memory
} // namespace nativeaot
} // namespace guidexos
