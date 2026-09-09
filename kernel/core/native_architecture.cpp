#include "include/kernel/native_architecture.h"

namespace gxos {
namespace apps {

const char* NativeArchitectureToString(NativeArchitecture architecture)
{
    switch (architecture) {
    case NativeArchitecture::AMD64: return "amd64";
    case NativeArchitecture::ARM64: return "arm64";
    case NativeArchitecture::Unknown:
    default: return "unknown";
    }
}

static bool text_equals(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

NativeArchitecture NativeArchitectureFromString(const char* architecture)
{
    if (text_equals(architecture, "amd64") || text_equals(architecture, "x64") ||
        text_equals(architecture, "x86_64")) return NativeArchitecture::AMD64;
    if (text_equals(architecture, "arm64") || text_equals(architecture, "aarch64") ||
        text_equals(architecture, "AARCH64")) return NativeArchitecture::ARM64;
    return NativeArchitecture::Unknown;
}

NativeArchitecture CurrentNativeArchitecture()
{
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64)
    return NativeArchitecture::AMD64;
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64) || \
      defined(__ARM64_ARCH_8__) || defined(__ARM_ARCH_ISA_A64)
    return NativeArchitecture::ARM64;
#else
    return NativeArchitecture::Unknown;
#endif
}

uint16_t NativeArchitectureElfMachine(NativeArchitecture architecture)
{
    switch (architecture) {
    case NativeArchitecture::AMD64: return 62;
    case NativeArchitecture::ARM64: return 183;
    case NativeArchitecture::Unknown:
    default: return 0;
    }
}

} // namespace apps
} // namespace gxos
