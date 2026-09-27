#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

static void put32(uint8_t* bytes, uint32_t value)
{
    memcpy(bytes, &value, sizeof(value));
}

static void put64(uint8_t* bytes, uint64_t value)
{
    memcpy(bytes, &value, sizeof(value));
}

static void test_efi_memory_attributes()
{
    uint8_t descriptor[48] = {};
    put32(descriptor, nic::TX_DMA_EFI_LOADER_DATA_TYPE);
    put64(descriptor + 8u, 0x08000000ULL);
    put64(descriptor + 24u, 4u);
    put64(descriptor + 32u, nic::TX_DMA_EFI_MEMORY_WB | 0x4000ULL);

    uint64_t attributes = 0u;
    assert(nic::tx_dma_region_memory_map_attributes(
        descriptor, 1u, sizeof(descriptor), 0x08001000ULL, 0x2000ULL,
        &attributes));
    assert(attributes == (nic::TX_DMA_EFI_MEMORY_WB | 0x4000ULL));
    assert(!nic::tx_dma_region_memory_map_attributes(
        descriptor, 1u, 32u, 0x08001000ULL, 0x2000ULL, &attributes));
    assert(!nic::tx_dma_region_memory_map_attributes(
        descriptor, 1u, sizeof(descriptor), 0x08004000ULL, 0x1000ULL,
        &attributes));
}

static void test_bounded_output_and_mode()
{
    assert(shell::nicinfo_mode_from_args("tx", "iommu", nullptr) ==
           shell::NICINFO_MODE_TX_IOMMU);
    assert(shell::NICINFO_TX_IOMMU_EXPECTED_LINES <=
           shell::NICINFO_TX_IOMMU_MAX_LINES);
}

int main()
{
    test_efi_memory_attributes();
    test_bounded_output_and_mode();
    return 0;
}
