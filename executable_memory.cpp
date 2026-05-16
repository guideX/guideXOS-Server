#include "executable_memory.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace gxos {
namespace apps {
namespace {

size_t pageSize() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<size_t>(info.dwPageSize);
#else
    long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<size_t>(value) : 4096;
#endif
}

size_t alignDown(size_t value, size_t alignment) {
    return value & ~(alignment - 1);
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

#ifdef _WIN32
DWORD toNativeProtection(ExecutableMemoryProtection protection) {
    switch (protection) {
    case ExecutableMemoryProtection::ReadWrite: return PAGE_READWRITE;
    case ExecutableMemoryProtection::ReadExecute: return PAGE_EXECUTE_READ;
    case ExecutableMemoryProtection::Read:
    default: return PAGE_READONLY;
    }
}
#else
int toNativeProtection(ExecutableMemoryProtection protection) {
    switch (protection) {
    case ExecutableMemoryProtection::ReadWrite: return PROT_READ | PROT_WRITE;
    case ExecutableMemoryProtection::ReadExecute: return PROT_READ | PROT_EXEC;
    case ExecutableMemoryProtection::Read:
    default: return PROT_READ;
    }
}
#endif

} // namespace

bool ExecutableMemory::Allocate(size_t size, ExecutableMemoryBlock& block, std::string& error) {
    return AllocateAt(nullptr, size, block, error);
}

bool ExecutableMemory::AllocateAt(void* preferredBase, size_t size, ExecutableMemoryBlock& block, std::string& error) {
    block = ExecutableMemoryBlock();
    if (size == 0) {
        error = "Executable memory allocation size is zero";
        return false;
    }

    size_t alignedSize = alignUp(size, pageSize());
#ifdef _WIN32
    void* memory = VirtualAlloc(preferredBase, alignedSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) {
        error = "VirtualAlloc failed";
        return false;
    }
    if (preferredBase && memory != preferredBase) {
        VirtualFree(memory, 0, MEM_RELEASE);
        error = "VirtualAlloc did not return the requested preferred base";
        return false;
    }
#else
    if (preferredBase) {
        error = "preferred-base executable mapping is not supported on this host";
        return false;
    }
    void* memory = mmap(nullptr, alignedSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        error = "mmap failed";
        return false;
    }
#endif

    block.base = memory;
    block.size = alignedSize;
    return true;
}

bool ExecutableMemory::Protect(ExecutableMemoryBlock& block, size_t offset, size_t size, ExecutableMemoryProtection protection, std::string& error) {
    if (!block.base || block.size == 0) {
        error = "Executable memory block is empty";
        return false;
    }
    if (offset > block.size || size > block.size - offset) {
        error = "Executable memory protection range is out of bounds";
        return false;
    }

    size_t page = pageSize();
    size_t alignedOffset = alignDown(offset, page);
    size_t alignedEnd = alignUp(offset + size, page);
    if (alignedEnd > block.size) alignedEnd = block.size;
    size_t alignedSize = alignedEnd - alignedOffset;
    char* address = static_cast<char*>(block.base) + alignedOffset;

#ifdef _WIN32
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, alignedSize, toNativeProtection(protection), &oldProtect)) {
        error = "VirtualProtect failed";
        return false;
    }
#else
    if (mprotect(address, alignedSize, toNativeProtection(protection)) != 0) {
        error = "mprotect failed";
        return false;
    }
#endif
    return true;
}

void ExecutableMemory::Free(ExecutableMemoryBlock& block) {
    if (!block.base) return;
#ifdef _WIN32
    VirtualFree(block.base, 0, MEM_RELEASE);
#else
    munmap(block.base, block.size);
#endif
    block = ExecutableMemoryBlock();
}

} // namespace apps
} // namespace gxos
