// Hosted check of the accepted TOOLS12 AMD64 ELF against the production
// NativeElf validator and runtime bounds from the supplied Server checkout.
// This validates image structure only; it does not execute the guest ELF.

#include "core/native_elf/native_elf_runtime.h"
#include "core/native_elf/native_elf_validator.h"

#include <cstdio>

using namespace kernel::native_elf;

static uint8_t s_image[NATIVE_APP_MAX_ELF_FILE_BYTES] = {};

namespace {

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

} // namespace

int main(int argc, char** argv)
{
    if (!require(argc == 2, "pass the accepted AMD64 ELF path")) return 1;

    std::FILE* file = std::fopen(argv[1], "rb");
    if (!require(file != nullptr, "accepted ELF opens")) return 1;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        require(false, "accepted ELF size is readable");
        return 1;
    }
    const long fileBytes = std::ftell(file);
    if (!require(fileBytes == 1262228L, "accepted ELF size is unchanged") ||
        std::fseek(file, 0, SEEK_SET) != 0 ||
        std::fread(s_image, 1, static_cast<size_t>(fileBytes), file) !=
            static_cast<size_t>(fileBytes)) {
        std::fclose(file);
        return 1;
    }
    std::fclose(file);

    // Match NativeElfLoader::load's policy: the configured NativeElf window,
    // 2 MiB file-buffer cap, and the runtime's PT_LOAD count bound.
    NativeElfValidationPolicy policy = default_validation_policy();
    policy.regionBase = guidexos::native_elf::IMAGE_BASE;
    policy.regionSize = guidexos::native_elf::REGION_SIZE;
    policy.maxFileBytes = NATIVE_APP_MAX_ELF_FILE_BYTES;
    policy.maxMappedBytes = policy.regionSize;

    NativeElfValidationResult result = {};
    if (!require(validate_native_elf(s_image,
                                    static_cast<uint32_t>(fileBytes),
                                    policy,
                                    &result),
                 result.error ? result.error : "production NativeElf validator accepts ELF")) {
        return 1;
    }
    if (!require(result.imageBase == 0x50000000ULL,
                 "ELF begins at the NativeElf fixed image base")) return 1;
    if (!require(result.entryPoint == 0x500D36F0ULL,
                 "ELF entry matches the accepted gx_main image")) return 1;
    if (!require(result.loadCount == 3U && result.executableLoadCount == 1U,
                 "ELF uses three PT_LOAD segments with one executable segment")) return 1;
    if (!require(result.mappedBytes <= NATIVE_APP_MAX_MAPPED_IMAGE_BYTES,
                 "mapped image fits the nested-runtime snapshot bound")) return 1;
    if (!require(APPLICATION_STACK_SIZE == 512U * 1024U &&
                 (APPLICATION_STACK_BASE + APPLICATION_STACK_SIZE) %
                     APPLICATION_STACK_ALIGNMENT == 0,
                 "runtime stack contract is 512 KiB and 16-byte aligned")) return 1;

    std::printf("TOOLS13R1C accepted ELF: PASS; bytes=%u mapped=%llu loads=%u entry=0x%llX\n",
                static_cast<unsigned>(fileBytes),
                static_cast<unsigned long long>(result.mappedBytes),
                static_cast<unsigned>(result.loadCount),
                static_cast<unsigned long long>(result.entryPoint));
    return 0;
}
