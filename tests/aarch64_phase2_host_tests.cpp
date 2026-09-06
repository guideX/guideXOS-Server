#include <stdint.h>
#include <stdio.h>

#include "../aarch64/phase2/phase2_platform.h"
#include "../aarch64/phase2/phase2_validation.h"

static void be32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static bool expect(bool condition, const char* name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    bool ok = true;
    uint8_t header[64] = {};
    be32(header + 0, 0xd00dfeed);
    be32(header + 4, 64);
    be32(header + 8, 40);
    be32(header + 12, 40);
    be32(header + 16, 40);
    be32(header + 20, 17);
    be32(header + 24, 16);
    be32(header + 32, 16);
    be32(header + 36, 0);
    ok &= expect(gxos_aarch64_fdt_header_bounds(header, sizeof(header), nullptr, nullptr, nullptr, nullptr, nullptr),
                 "valid FDT header accepted");

    header[0] = 0;
    ok &= expect(!gxos_aarch64_fdt_header_bounds(header, sizeof(header), nullptr, nullptr, nullptr, nullptr, nullptr),
                 "malformed FDT magic rejected");
    header[0] = 0xd0;
    be32(header + 12, 0x1000);
    ok &= expect(!gxos_aarch64_fdt_header_bounds(header, sizeof(header), nullptr, nullptr, nullptr, nullptr, nullptr),
                 "out-of-bounds FDT strings block rejected");

    ok &= expect(!gxos_aarch64_memory_map_layout_valid(0x1000, 80, 40, 3),
                 "inconsistent memory-map arithmetic rejected");
    ok &= expect(gxos_aarch64_memory_map_layout_valid(0x1000, 80, 40, 2),
                 "valid memory-map arithmetic accepted");

    uint64_t end = 0;
    ok &= expect(!gxos_aarch64_page_descriptor_valid(UINT64_C(0x123), 1, &end),
                 "unaligned page descriptor rejected");
    ok &= expect(!gxos_aarch64_page_descriptor_valid(UINT64_MAX - 0xfff, 2, &end),
                 "overflowing page descriptor rejected");
    ok &= expect(gxos_aarch64_range_overlaps({0x1000, 0x2000}, {0x1fff, 0x3000}),
                 "overlapping protected ranges detected");

    if (argc > 1) {
        FILE* file = fopen(argv[1], "rb");
        static uint8_t blob[2 * 1024 * 1024];
        size_t size = file ? fread(blob, 1, sizeof(blob), file) : 0;
        if (file) fclose(file);
        gxos_aarch64_phase2_platform platform = {};
        const bool parsed = file != nullptr && size != 0 &&
                            gxos_aarch64_phase2_parse_dtb(blob, size, &platform);
        printf("DTB parser result=%u valid=%u RAM=%u UART=0x%llx/%llx GICv%u D=%llx/%llx C=%llx/%llx timer=%u source=%u\n",
               parsed ? 1 : 0, platform.valid, platform.ram_count,
               (unsigned long long)platform.uart_base, (unsigned long long)platform.uart_size,
               platform.gic_version, (unsigned long long)platform.gicd_base,
               (unsigned long long)platform.gicd_size, (unsigned long long)platform.gicc_base,
               (unsigned long long)platform.gicc_size, platform.timer_irq, platform.timer_source);
        ok &= expect(parsed, "QEMU virt DTB parsed");
    }

    if (!ok) return 1;
    puts("AARCH64 Phase 2 host validation tests: PASS");
    return 0;
}
