#include "compiler_target.h"

#include "kernel/native_architecture.h"

namespace kernel {
namespace compiler {

namespace {

static uint32_t text_length(const char* value)
{
    uint32_t length = 0;
    if (value) while (value[length]) ++length;
    return length;
}

static bool ends_with(const char* value, const char* suffix)
{
    const uint32_t valueLength = text_length(value);
    const uint32_t suffixLength = text_length(suffix);
    if (!value || !suffix || valueLength < suffixLength) return false;
    for (uint32_t i = 0; i < suffixLength; ++i) {
        if (value[valueLength - suffixLength + i] != suffix[i]) return false;
    }
    return true;
}

static gxos::apps::NativeArchitecture canonical_target(CompilerTarget target)
{
    if (target == CompilerTarget::Current) target = current_target();
    return target == CompilerTarget::Arm64 ? gxos::apps::NativeArchitecture::ARM64 :
        gxos::apps::NativeArchitecture::AMD64;
}

} // namespace

const char* target_architecture(CompilerTarget target)
{
    return gxos::apps::NativeArchitectureToString(canonical_target(target));
}

uint16_t target_elf_machine(CompilerTarget target)
{
    return gxos::apps::NativeArchitectureElfMachine(canonical_target(target));
}

bool target_is_supported(CompilerTarget target)
{
    return target == CompilerTarget::Current || target == CompilerTarget::Amd64 || target == CompilerTarget::Arm64;
}

CompilerTarget target_from_profile(const char* profile)
{
    if (!profile) return CompilerTarget::Current;
    if (ends_with(profile, "arm64") || ends_with(profile, "aarch64")) return CompilerTarget::Arm64;
    if (ends_with(profile, "amd64") || ends_with(profile, "x86_64")) return CompilerTarget::Amd64;
    return CompilerTarget::Current;
}

CompilerTarget current_target()
{
    return gxos::apps::CurrentNativeArchitecture() == gxos::apps::NativeArchitecture::ARM64 ?
        CompilerTarget::Arm64 : CompilerTarget::Amd64;
}

} // namespace compiler
} // namespace kernel
