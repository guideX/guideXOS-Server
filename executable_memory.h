#pragma once

#include <cstddef>
#include <string>

namespace gxos {
namespace apps {

enum class ExecutableMemoryProtection {
    Read = 0,
    ReadWrite,
    ReadExecute
};

struct ExecutableMemoryBlock {
    void* base = nullptr;
    size_t size = 0;
};

class ExecutableMemory {
public:
    static bool Allocate(size_t size, ExecutableMemoryBlock& block, std::string& error);
    static bool AllocateAt(void* preferredBase, size_t size, ExecutableMemoryBlock& block, std::string& error);
    static bool Protect(ExecutableMemoryBlock& block, size_t offset, size_t size, ExecutableMemoryProtection protection, std::string& error);
    static void Free(ExecutableMemoryBlock& block);
};

} // namespace apps
} // namespace gxos
