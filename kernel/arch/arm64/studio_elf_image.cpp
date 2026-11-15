#include "studio_elf_image.h"

namespace {

static const uint64_t kPageSize = UINT64_C(4096);
static const uint64_t kMaxImagePages = 8;
static const uint64_t kElfHeaderBytes = 64;
static const uint64_t kProgramHeaderBytes = 56;

static uint16_t read_u16(const uint8_t* bytes, uint64_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

static uint32_t read_u32(const uint8_t* bytes, uint64_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static uint64_t read_u64(const uint8_t* bytes, uint64_t offset)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool align_up(uint64_t value, uint64_t* result)
{
    if (!result || value > UINT64_MAX - (kPageSize - 1)) return false;
    *result = (value + kPageSize - 1) & ~(kPageSize - 1);
    return true;
}

static bool compiler_integer_return(const uint8_t* bytes, uint64_t offset,
                                    uint64_t available, uint32_t* result)
{
    if (!bytes || !result || available < 12 || offset > UINT64_MAX - 12) return false;
    const uint32_t movz = read_u32(bytes, offset);
    const uint32_t movk = read_u32(bytes, offset + 4);
    const uint32_t ret = read_u32(bytes, offset + 8);
    if ((movz & UINT32_C(0xffe0001f)) != UINT32_C(0x52800000) ||
        (movk & UINT32_C(0xffe0001f)) != UINT32_C(0x72a00000) ||
        (movz & UINT32_C(0x1f)) != 0 || (movk & UINT32_C(0x1f)) != 0 ||
        ret != UINT32_C(0xd65f03c0)) return false;
    const uint32_t low = (movz >> 5) & UINT32_C(0xffff);
    const uint32_t high = (movk >> 5) & UINT32_C(0xffff);
    *result = low | (high << 16);
    return true;
}

} // namespace

extern "C" uint8_t gxos_aarch64_studio_elf_validate(
    const uint8_t* bytes, uint64_t byte_count,
    gxos_aarch64_studio_elf_image* image)
{
    if (image) {
        const uint8_t* clear = reinterpret_cast<const uint8_t*>(image);
        for (uint32_t i = 0; i < sizeof(*image); ++i)
            const_cast<uint8_t*>(clear)[i] = 0;
    }
    if (!bytes || !image || byte_count < kElfHeaderBytes + kProgramHeaderBytes ||
        byte_count > GXOS_AARCH64_STUDIO_ELF_MAX_FILE_BYTES) return 0;

    if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F' ||
        bytes[4] != 2 || bytes[5] != 1 || bytes[6] != 1 ||
        read_u16(bytes, 16) != 2 || read_u16(bytes, 18) != 183 ||
        read_u32(bytes, 20) != 1 || read_u64(bytes, 24) != GXOS_AARCH64_STUDIO_ELF_BASE + 0x1000 ||
        read_u64(bytes, 32) != kElfHeaderBytes || read_u64(bytes, 40) != 0 ||
        read_u32(bytes, 48) != 0 || read_u16(bytes, 52) != kElfHeaderBytes ||
        read_u16(bytes, 54) != kProgramHeaderBytes || read_u16(bytes, 56) != 1 ||
        read_u16(bytes, 58) != 0 || read_u16(bytes, 60) != 0 || read_u16(bytes, 62) != 0)
        return 0;

    const uint64_t ph = kElfHeaderBytes;
    const uint32_t type = read_u32(bytes, ph);
    const uint32_t flags = read_u32(bytes, ph + 4);
    const uint64_t file_offset = read_u64(bytes, ph + 8);
    const uint64_t virtual_address = read_u64(bytes, ph + 16);
    const uint64_t physical_address = read_u64(bytes, ph + 24);
    const uint64_t file_size = read_u64(bytes, ph + 32);
    const uint64_t memory_size = read_u64(bytes, ph + 40);
    const uint64_t alignment = read_u64(bytes, ph + 48);
    uint64_t file_end = 0;
    uint64_t memory_end = 0;
    uint64_t rounded_end = 0;
    if (type != 1 || flags != 5 || file_offset != 0 ||
        virtual_address != GXOS_AARCH64_STUDIO_ELF_BASE ||
        physical_address != virtual_address || file_size != byte_count ||
        memory_size < file_size || memory_size < UINT64_C(0x100c) ||
        memory_size > kMaxImagePages * kPageSize || alignment != kPageSize ||
        (file_offset & (alignment - 1)) != (virtual_address & (alignment - 1)) ||
        !add_u64(file_offset, file_size, &file_end) || file_end > byte_count ||
        !add_u64(virtual_address, memory_size, &memory_end) ||
        memory_end > GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA ||
        !align_up(memory_end, &rounded_end)) return 0;

    const uint64_t code_file_offset = file_offset + (read_u64(bytes, 24) - virtual_address);
    uint32_t return_bits = 0;
    if (code_file_offset != 0x1000 || code_file_offset > file_size ||
        !compiler_integer_return(bytes, code_file_offset, file_size - code_file_offset,
                                 &return_bits)) return 0;

    const uint64_t image_bytes = rounded_end - virtual_address;
    const uint64_t image_pages = image_bytes / kPageSize;
    if (image_pages == 0 || image_pages > kMaxImagePages) return 0;
    image->file_bytes = byte_count;
    image->image_base = virtual_address;
    image->image_memory_bytes = memory_size;
    image->entry_point = read_u64(bytes, 24);
    image->segment_file_offset = file_offset;
    image->segment_file_bytes = file_size;
    image->segment_memory_bytes = memory_size;
    image->image_page_count = image_pages;
    image->return_bits = return_bits;
    return 1;
}
