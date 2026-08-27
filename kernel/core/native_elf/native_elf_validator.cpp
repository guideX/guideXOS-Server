//
// Validator for the deliberately narrow NativeElf ET_EXEC subset.
//

#include "native_elf_validator.h"

namespace kernel {
namespace native_elf {
namespace {

static const uint16_t ELF_TYPE_EXEC = 2U;
static const uint16_t ELF_MACHINE_AMD64 = 62U;
static const uint32_t ELF_VERSION_CURRENT = 1U;
static const uint32_t PT_LOAD = 1U;
static const uint32_t ELF_HEADER_BYTES = 64U;
static const uint32_t PROGRAM_HEADER_BYTES = 56U;

static uint16_t get_u16(const uint8_t* bytes, uint64_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

static uint32_t get_u32(const uint8_t* bytes, uint64_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static uint64_t get_u64(const uint8_t* bytes, uint64_t offset)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    }
    return value;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || left > ~static_cast<uint64_t>(0) - right) return false;
    *result = left + right;
    return true;
}

static bool multiply_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || (left != 0 && right > ~static_cast<uint64_t>(0) / left)) return false;
    *result = left * right;
    return true;
}

static bool is_power_of_two(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static bool align_up(uint64_t value, uint64_t alignment, uint64_t* result)
{
    if (!result || alignment == 0) return false;
    const uint64_t remainder = value % alignment;
    if (remainder == 0) {
        *result = value;
        return true;
    }
    return add_u64(value, alignment - remainder, result);
}

static bool fail(NativeElfValidationResult* result, const char* error)
{
    if (result) {
        result->valid = false;
        result->error = error;
    }
    return false;
}

} // namespace

NativeElfValidationPolicy default_validation_policy()
{
    NativeElfValidationPolicy policy = {};
    policy.regionBase = guidexos::native_elf::IMAGE_BASE;
    policy.regionSize = guidexos::native_elf::REGION_SIZE;
    policy.maxFileBytes = guidexos::native_elf::MAX_ELF_FILE_BYTES;
    policy.maxMappedBytes = guidexos::native_elf::MAX_MAPPED_BYTES;
    policy.maxLoadSegments = guidexos::native_elf::MAX_LOAD_SEGMENTS;
    return policy;
}

bool validate_native_elf(const uint8_t* image,
                         uint32_t imageBytes,
                         const NativeElfValidationPolicy& policy,
                         NativeElfValidationResult* result)
{
    if (!result) return false;
    *result = {};
    result->error = "unknown NativeElf validation failure";
    result->entryLoadIndex = 0xFFFFFFFFU;

    if (!image) return fail(result, "ELF image pointer is null");
    if (policy.regionBase == 0 || policy.regionSize == 0 ||
        policy.maxFileBytes == 0 || policy.maxMappedBytes == 0 ||
        policy.maxLoadSegments == 0 ||
        policy.maxLoadSegments > guidexos::native_elf::MAX_LOAD_SEGMENTS) {
        return fail(result, "NativeElf validation policy is invalid");
    }
    if (imageBytes < ELF_HEADER_BYTES) return fail(result, "ELF image is smaller than ELF64 header");
    if (imageBytes > policy.maxFileBytes) return fail(result, "ELF image exceeds loader file-size limit");

    uint64_t regionEnd = 0;
    if (!add_u64(policy.regionBase, policy.regionSize, &regionEnd)) {
        return fail(result, "NativeElf region arithmetic overflows");
    }

    if (image[0] != 0x7F || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') {
        return fail(result, "ELF magic mismatch");
    }
    if (image[4] != 2) return fail(result, "ELF is not ELF64");
    if (image[5] != 1) return fail(result, "ELF is not little-endian");
    if (image[6] != 1) return fail(result, "ELF identification version is not current");
    for (uint32_t i = 7; i < 16; ++i) {
        if (image[i] != 0) return fail(result, "ELF identification uses unsupported ABI metadata");
    }

    if (get_u16(image, 16) != ELF_TYPE_EXEC) return fail(result, "ELF is not ET_EXEC");
    if (get_u16(image, 18) != ELF_MACHINE_AMD64) return fail(result, "ELF machine is not AMD64");
    if (get_u32(image, 20) != ELF_VERSION_CURRENT) return fail(result, "ELF version field is not current");
    if (get_u32(image, 48) != 0) return fail(result, "ELF processor flags are unsupported");
    if (get_u16(image, 52) != ELF_HEADER_BYTES) return fail(result, "ELF header size is unsupported");
    if (get_u16(image, 54) != PROGRAM_HEADER_BYTES) return fail(result, "ELF program-header size is unsupported");
    if (get_u64(image, 40) != 0 || get_u16(image, 58) != 0 ||
        get_u16(image, 60) != 0 || get_u16(image, 62) != 0) {
        return fail(result, "ELF section metadata is unsupported");
    }

    const uint64_t programHeaderOffset = get_u64(image, 32);
    const uint16_t programHeaderCount = get_u16(image, 56);
    if (programHeaderCount == 0 || programHeaderCount > policy.maxLoadSegments) {
        return fail(result, "ELF program-header count is outside the supported bound");
    }
    if (programHeaderOffset < ELF_HEADER_BYTES || programHeaderOffset > imageBytes) {
        return fail(result, "ELF program-header table is out of bounds");
    }
    uint64_t programHeaderBytes = 0;
    if (!multiply_u64(programHeaderCount, PROGRAM_HEADER_BYTES, &programHeaderBytes) ||
        programHeaderBytes > static_cast<uint64_t>(imageBytes) - programHeaderOffset) {
        return fail(result, "ELF program-header table exceeds file bounds");
    }

    result->entryPoint = get_u64(image, 24);
    result->imageBase = regionEnd;
    result->imageEnd = policy.regionBase;
    uint64_t minAddress = regionEnd;
    uint64_t maxAddress = policy.regionBase;

    for (uint16_t i = 0; i < programHeaderCount; ++i) {
        const uint64_t headerOffset = programHeaderOffset + static_cast<uint64_t>(i) * PROGRAM_HEADER_BYTES;
        const uint32_t type = get_u32(image, headerOffset + 0);
        if (type != PT_LOAD) {
            return fail(result, "ELF contains an unsupported program-header type");
        }
        if (result->loadCount >= policy.maxLoadSegments) {
            return fail(result, "ELF contains too many PT_LOAD segments");
        }

        const uint32_t flags = get_u32(image, headerOffset + 4);
        const uint64_t fileOffset = get_u64(image, headerOffset + 8);
        const uint64_t virtualAddress = get_u64(image, headerOffset + 16);
        const uint64_t physicalAddress = get_u64(image, headerOffset + 24);
        const uint64_t fileSize = get_u64(image, headerOffset + 32);
        const uint64_t memorySize = get_u64(image, headerOffset + 40);
        const uint64_t alignment = get_u64(image, headerOffset + 48);

        if ((flags & ~(PF_R | PF_W | PF_X)) != 0 || (flags & PF_R) == 0) {
            return fail(result, "PT_LOAD permissions are outside the supported R/W/X subset");
        }
        if ((flags & PF_W) != 0 && (flags & PF_X) != 0) {
            return fail(result, "writable and executable PT_LOAD permissions may not be combined");
        }
        if (fileOffset > imageBytes || fileSize > static_cast<uint64_t>(imageBytes) - fileOffset) {
            return fail(result, "PT_LOAD file range exceeds ELF bounds");
        }
        if (fileSize > memorySize || memorySize == 0) {
            return fail(result, "PT_LOAD has invalid file and memory sizes");
        }
        if (physicalAddress != virtualAddress) {
            return fail(result, "PT_LOAD physical address differs from fixed virtual address");
        }
        if ((virtualAddress & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0 ||
            (fileOffset & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0) {
            return fail(result, "PT_LOAD is not page aligned for the bootstrap mapper");
        }
        if (alignment > 1) {
            if (!is_power_of_two(alignment) ||
                alignment > guidexos::native_elf::PAGE_SIZE ||
                (fileOffset % alignment) != (virtualAddress % alignment)) {
                return fail(result, "PT_LOAD alignment is unsupported or inconsistent");
            }
        }

        uint64_t virtualEnd = 0;
        if (!add_u64(virtualAddress, memorySize, &virtualEnd) ||
            virtualAddress < policy.regionBase || virtualEnd > regionEnd ||
            virtualEnd <= virtualAddress) {
            return fail(result, "PT_LOAD virtual range is outside the NativeElf region");
        }

        for (uint32_t previous = 0; previous < result->loadCount; ++previous) {
            uint64_t previousEnd = 0;
            if (!add_u64(result->loads[previous].virtualAddress,
                         result->loads[previous].memorySize, &previousEnd)) {
                return fail(result, "PT_LOAD overlap arithmetic overflows");
            }
            if (virtualAddress < previousEnd &&
                result->loads[previous].virtualAddress < virtualEnd) {
                return fail(result, "PT_LOAD memory ranges overlap");
            }
        }

        NativeElfLoad& load = result->loads[result->loadCount++];
        load.fileOffset = fileOffset;
        load.virtualAddress = virtualAddress;
        load.fileSize = fileSize;
        load.memorySize = memorySize;
        load.flags = flags;
        if (virtualAddress < minAddress) minAddress = virtualAddress;
        if (virtualEnd > maxAddress) maxAddress = virtualEnd;
        if ((flags & PF_X) != 0) ++result->executableLoadCount;
    }

    if (result->loadCount == 0) return fail(result, "ELF contains no PT_LOAD segment");
    if (minAddress != policy.regionBase) return fail(result, "ELF image base does not match NativeElf contract");
    result->imageBase = minAddress;
    result->imageEnd = maxAddress;

    uint64_t mappedEnd = 0;
    if (!align_up(maxAddress, guidexos::native_elf::PAGE_SIZE, &mappedEnd) ||
        mappedEnd < minAddress) {
        return fail(result, "ELF mapped-size arithmetic overflows");
    }
    result->mappedBytes = mappedEnd - minAddress;
    if (result->mappedBytes == 0 || result->mappedBytes > policy.maxMappedBytes ||
        result->mappedBytes > policy.regionSize) {
        return fail(result, "ELF mapped image exceeds loader bounds");
    }

    for (uint32_t i = 0; i < result->loadCount; ++i) {
        const NativeElfLoad& load = result->loads[i];
        if ((load.flags & PF_X) == 0) continue;
        uint64_t loadEnd = 0;
        if (!add_u64(load.virtualAddress, load.memorySize, &loadEnd)) {
            return fail(result, "executable PT_LOAD range overflows");
        }
        if (result->entryPoint >= load.virtualAddress && result->entryPoint < loadEnd) {
            const uint64_t entryOffset = result->entryPoint - load.virtualAddress;
            if (entryOffset >= load.fileSize) {
                return fail(result, "ELF entry point is not file-backed");
            }
            result->entryLoadIndex = i;
        }
    }
    if (result->executableLoadCount == 0) return fail(result, "ELF contains no executable PT_LOAD segment");
    if (result->entryLoadIndex == 0xFFFFFFFFU) {
        return fail(result, "ELF entry point is outside executable PT_LOAD memory");
    }

    result->valid = true;
    result->error = "";
    return true;
}

} // namespace native_elf
} // namespace kernel
