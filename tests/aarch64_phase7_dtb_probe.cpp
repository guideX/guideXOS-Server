#include <stdint.h>
#include <stdio.h>

#include "../aarch64/phase2/phase2_platform.h"

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    FILE* file = fopen(argv[1], "rb");
    if (!file) return 3;
    static uint8_t blob[2 * 1024 * 1024];
    const size_t size = fread(blob, 1, sizeof(blob), file);
    fclose(file);
    gxos_aarch64_phase2_platform platform = {};
    if (!gxos_aarch64_phase2_parse_dtb(blob, size, &platform)) return 4;
    printf("virtio-mmio-count=%u\n", platform.virtio_mmio_count);
    for (uint32_t i = 0; i < platform.virtio_mmio_count; ++i) {
        printf("virtio[%u] base=0x%llx size=0x%llx irq=%u\n", i,
               (unsigned long long)platform.virtio_mmio[i].base,
               (unsigned long long)platform.virtio_mmio[i].size,
               platform.virtio_mmio[i].irq);
    }
    return 0;
}
