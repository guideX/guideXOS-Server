#pragma once

#include <stdint.h>

#define GXOS_AARCH64_STUDIO_ELF_MAX_FILE_BYTES UINT64_C(16384)
#define GXOS_AARCH64_STUDIO_ELF_BASE UINT64_C(0x51000000)
#define GXOS_AARCH64_STUDIO_ELF_WINDOW_BYTES UINT64_C(0x00200000)
#define GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA \
    (GXOS_AARCH64_STUDIO_ELF_BASE + GXOS_AARCH64_STUDIO_ELF_WINDOW_BYTES - UINT64_C(0x1000))
#define GXOS_AARCH64_STUDIO_ELF_STACK_BYTES UINT64_C(0x4000)

// This is deliberately the first compiler-output profile, not a general ELF
// loader contract. It accepts the resident compiler's single RX bootstrap
// segment and exactly one integer-return gx_main body (movz/movk/ret).
struct gxos_aarch64_studio_elf_image {
    uint64_t file_bytes;
    uint64_t image_base;
    uint64_t image_memory_bytes;
    uint64_t entry_point;
    uint64_t segment_file_offset;
    uint64_t segment_file_bytes;
    uint64_t segment_memory_bytes;
    uint64_t image_page_count;
    uint32_t return_bits;
    uint32_t reserved;
};

#ifdef __cplusplus
extern "C" {
#endif

uint8_t gxos_aarch64_studio_elf_validate(
    const uint8_t* bytes, uint64_t byte_count,
    gxos_aarch64_studio_elf_image* image);

#ifdef __cplusplus
}
#endif
