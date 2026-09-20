#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/vtd.h"
#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

static void put16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

static void put32(uint8_t* bytes, uint32_t value)
{
    for (uint8_t i = 0; i < 4u; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8u));
    }
}

static void put64(uint8_t* bytes, uint64_t value)
{
    for (uint8_t i = 0; i < 8u; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8u));
    }
}

static void finalize_acpi_checksum(uint8_t* table, uint32_t length)
{
    table[9] = 0u;
    uint8_t sum = 0u;
    for (uint32_t i = 0; i < length; ++i) sum = static_cast<uint8_t>(sum + table[i]);
    table[9] = static_cast<uint8_t>(0u - sum);
}

static uint32_t make_dmar(uint8_t* table, bool includeAll, bool addRmrr)
{
    memset(table, 0, 512u);
    memcpy(table, "DMAR", 4u);
    table[8] = 1u;
    table[36] = 0x2Fu;

    uint32_t cursor = 48u;
    put16(table + cursor, 0u);
    put16(table + cursor + 2u, includeAll ? 16u : 24u);
    table[cursor + 4u] = includeAll ? 1u : 0u;
    put16(table + cursor + 6u, 0u);
    put64(table + cursor + 8u, 0x00000000FED90000ULL);
    if (!includeAll) {
        table[cursor + 16u] = 1u;
        table[cursor + 17u] = 8u;
        table[cursor + 21u] = 0u;
        table[cursor + 22u] = static_cast<uint8_t>((31u << 3) | 6u);
    }
    cursor += includeAll ? 16u : 24u;

    if (addRmrr) {
        put16(table + cursor, 1u);
        put16(table + cursor + 2u, 32u);
        put16(table + cursor + 6u, 0u);
        put64(table + cursor + 8u, 0x00100000ULL);
        put64(table + cursor + 16u, 0x00101FFFULL);
        table[cursor + 24u] = 1u;
        table[cursor + 25u] = 8u;
        table[cursor + 29u] = 0u;
        table[cursor + 30u] = static_cast<uint8_t>((31u << 3) | 6u);
        cursor += 32u;
    }

    put32(table + 4u, cursor);
    finalize_acpi_checksum(table, cursor);
    return cursor;
}

static void test_dmar_parser()
{
    uint8_t table[512] = {};
    const uint32_t length = make_dmar(table, false, true);
    const vtd::PciBdf target = { 0u, 0u, 31u, 6u };
    vtd::DmarAudit audit = {};
    assert(vtd::parse_dmar_table(table, length, target, &audit));
    assert(audit.valid);
    assert(audit.drhdCount == 1u);
    assert(audit.rmrrCount == 1u);
    assert(audit.matchingDrhdFound);
    assert(!audit.matchingDrhdIncludeAll);
    assert(audit.matchingDrhdExplicitScope);
    assert(audit.matchingRegisterBase == 0x00000000FED90000ULL);
    assert(audit.rmrrApplicable);
    assert(audit.matchingRmrrCount == 1u);

    const uint32_t includeAllLength = make_dmar(table, true, false);
    audit = {};
    assert(vtd::parse_dmar_table(table, includeAllLength, target, &audit));
    assert(audit.matchingDrhdFound && audit.matchingDrhdIncludeAll);

    const vtd::PciBdf otherSegment = { 1u, 0u, 31u, 6u };
    audit = {};
    assert(vtd::parse_dmar_table(table, includeAllLength, otherSegment, &audit));
    assert(!audit.matchingDrhdFound);

    table[4] = static_cast<uint8_t>(length - 1u);
    audit = {};
    assert(!vtd::parse_dmar_table(table, length, target, &audit));
    assert(audit.failure != nullptr);

    make_dmar(table, false, false);
    put16(table + 50u, 3u);
    finalize_acpi_checksum(table, length - 32u);
    audit = {};
    assert(!vtd::parse_dmar_table(table, length - 32u, target, &audit));
}

static void test_register_decoders()
{
    const vtd::PciBdf target = { 0u, 0u, 31u, 6u };
    const uint64_t sid = vtd::pci_source_id(target);
    vtd::VtdRegisterValues values = {};
    values.version = 0x10u;
    values.cap = (3ULL << 40) | (4ULL << 24);
    values.ecap = 0x1234ULL;
    values.gcmd = 1u;
    values.gsts = vtd::VTD_GSTS_TES;
    values.rootTableAddress = 0x0000000012345000ULL |
                              (1ULL << vtd::VTD_RTADDR_TTM_SHIFT);
    values.faultStatus = vtd::VTD_FSTS_PPF | (2u << vtd::VTD_FSTS_FRI_SHIFT);
    values.faultRecordReadable = true;
    values.faultLow = vtd::VTD_FAULT_RECORD_F | sid;
    values.faultHigh = 0x0000000012345678ULL;
    vtd::VtdRegisterSnapshot snapshot = {};
    assert(vtd::decode_register_values(0xFED90000ULL, values, &snapshot));
    assert(snapshot.valid);
    assert(snapshot.translationEnabled);
    assert(snapshot.primaryFaultPending);
    assert(snapshot.selectedFaultRecord == 2u);
    assert(snapshot.faultRecordCount == 4u);
    assert(snapshot.faultPresent);
    assert(snapshot.faultSourceId == sid);
    assert(snapshot.faultAddress == 0x0000000012345000ULL);
    assert(vtd::root_table_mode(snapshot.rootTableAddress) == 1u);
    assert(vtd::fault_reason(values.faultLow) == static_cast<uint8_t>(sid & 0xFFu));

    assert(vtd::classify_fault_address(0x1000u, 0x1000u, 0x100u,
                                       0x4000u, 0x100u) ==
           vtd::FaultAddressClass::Ring);
    assert(vtd::classify_fault_address(0x4000u, 0x1000u, 0x100u,
                                       0x4000u, 0x100u) ==
           vtd::FaultAddressClass::Buffer);
    assert(vtd::classify_fault_address(0x9000u, 0x1000u, 0x100u,
                                       0x4000u, 0x100u) ==
           vtd::FaultAddressClass::Other);
}

static void test_shell_contract()
{
    assert(shell::nicinfo_mode_from_arg("dma") == shell::NICINFO_MODE_DMA);
    assert(shell::nicinfo_mode_from_args("dma", "brief", nullptr) ==
           shell::NICINFO_MODE_DMA_BRIEF);
    assert(shell::nicinfo_mode_from_args("tx", "iommu", nullptr) ==
           shell::NICINFO_MODE_TX_IOMMU);
    assert(shell::nicinfo_mode_from_args("tx", "iommu", "run") ==
           shell::NICINFO_MODE_INVALID);
    static_assert(shell::NICINFO_DMA_EXPECTED_LINES <= shell::NICINFO_DMA_MAX_LINES,
                  "DMA output bound changed");
    static_assert(shell::NICINFO_DMA_BRIEF_EXPECTED_LINES <= shell::NICINFO_DMA_BRIEF_MAX_LINES,
                  "DMA brief output bound changed");
    static_assert(shell::NICINFO_TX_IOMMU_EXPECTED_LINES <= shell::NICINFO_TX_IOMMU_MAX_LINES,
                  "TX IOMMU output bound changed");
}

int main()
{
    test_dmar_parser();
    test_register_decoders();
    test_shell_contract();
    return 0;
}
