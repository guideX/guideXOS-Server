// Focused hosted checks for the bounded NativeElf validator.
// These tests do not claim to replace QEMU execution.

#include "core/compiler/elf_writer.h"
#include "native_elf/native_elf_validator.h"

#include <cstdio>
#include <cstring>

using namespace kernel::compiler;
using namespace kernel::native_elf;

namespace {

static bool require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

static void put_u16(uint8_t* bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

static void put_u64(uint8_t* bytes, uint32_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFULL);
    }
}

static bool validate(const uint8_t* image, uint32_t bytes, const char* message)
{
    NativeElfValidationResult result = {};
    const bool ok = validate_native_elf(image, bytes, default_validation_policy(), &result);
    return require(ok, message);
}

} // namespace

int main()
{
    const uint8_t code[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    uint8_t image[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout layout = {};
    if (!require(write_bootstrap_elf(code, sizeof(code), image, sizeof(image), &layout),
                 "writer creates NativeElf fixture")) return 1;
    if (!validate(image, layout.outputBytes, "valid generated ELF is accepted")) return 1;

    NativeElfValidationResult valid = {};
    if (!require(validate_native_elf(image, layout.outputBytes, default_validation_policy(), &valid) &&
                 valid.imageBase == guidexos::native_elf::IMAGE_BASE &&
                 valid.entryPoint == guidexos::native_elf::IMAGE_BASE + BOOTSTRAP_CODE_OFFSET &&
                 valid.loadCount == 1 && valid.executableLoadCount == 1 &&
                 valid.mappedBytes == 0x2000,
                 "valid layout fields are bounded")) return 1;

    uint8_t malformed[BOOTSTRAP_MAX_ELF_BYTES] = {};
    std::memcpy(malformed, image, layout.outputBytes);

    malformed[4] = 1;
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "wrong ELF class is rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    put_u16(malformed, 18, 3);
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "wrong architecture is rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    put_u16(malformed, 16, 3);
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "ET_DYN is rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    if (!require(!validate_native_elf(malformed, 64, default_validation_policy(), &valid),
                 "truncated program headers are rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    put_u64(malformed, 24, guidexos::native_elf::IMAGE_BASE +
                            guidexos::native_elf::REGION_SIZE);
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "entry outside executable segment is rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    put_u64(malformed, 96, ~0ULL);
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "invalid segment file bounds are rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    put_u64(malformed, 80, 0x00100000ULL);
    put_u64(malformed, 88, 0x00100000ULL);
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "protected mapping address is rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    put_u64(malformed, 80, ~0xFFFULL);
    put_u64(malformed, 88, ~0xFFFULL);
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "address arithmetic overflow/out-of-range is rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    put_u16(malformed, 56, 2);
    std::memcpy(malformed + 120, malformed + 64, 56);
    if (!require(!validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid),
                 "overlapping PT_LOAD segments are rejected")) return 1;
    std::memcpy(malformed, image, layout.outputBytes);

    // A larger memory size is accepted and records the page-rounded mapping;
    // the loader's zero-before-copy step supplies the zero-fill semantics.
    put_u64(malformed, 104, static_cast<uint64_t>(layout.outputBytes) + 0x100ULL);
    if (!require(validate_native_elf(malformed, layout.outputBytes, default_validation_policy(), &valid) &&
                 valid.imageEnd == guidexos::native_elf::IMAGE_BASE +
                                   layout.outputBytes + 0x100ULL &&
                 valid.mappedBytes == 0x2000,
                 "PT_LOAD zero-fill extent is bounded")) return 1;

    std::puts("native_elf_validator_host_test: PASS");
    return 0;
}
