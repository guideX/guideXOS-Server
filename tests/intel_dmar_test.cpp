// Hosted tests for bounded ACPI DMAR parsing, PCI-scope routing, and read-only
// VT-d register classification. No hardware access occurs in this test.

#include "kernel/intel_dmar.h"

#include <cstring>
#include <iostream>
#include <vector>

namespace {

int failures = 0;
uint32_t assertions = 0u;

void expect(bool value, const char* label)
{
    ++assertions;
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}

void put16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8u));
}

void put32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; ++i) {
        bytes.push_back(static_cast<uint8_t>(value >> (i * 8u)));
    }
}

void put64(std::vector<uint8_t>& bytes, uint64_t value)
{
    for (uint32_t i = 0u; i < 8u; ++i) {
        bytes.push_back(static_cast<uint8_t>(value >> (i * 8u)));
    }
}

void write16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1u] = static_cast<uint8_t>(value >> 8u);
}

void write32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; ++i) {
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8u));
    }
}

std::vector<uint8_t> scope(uint8_t type, uint8_t startBus,
                           const std::vector<std::pair<uint8_t, uint8_t>>& path)
{
    std::vector<uint8_t> bytes;
    bytes.push_back(type);
    bytes.push_back(static_cast<uint8_t>(6u + path.size() * 2u));
    bytes.push_back(0u); // reserved
    bytes.push_back(0u); // enumeration ID
    bytes.push_back(startBus);
    bytes.push_back(0u); // reserved
    for (const auto& element : path) {
        bytes.push_back(element.first);
        bytes.push_back(element.second);
    }
    return bytes;
}

void append_bytes(std::vector<uint8_t>& destination,
                  const std::vector<uint8_t>& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

void append_drhd(std::vector<uint8_t>& bytes, uint8_t flags,
                 uint8_t sizeEncoding, uint16_t segment,
                 uint64_t registerBase,
                 const std::vector<std::vector<uint8_t>>& scopes = {})
{
    const size_t start = bytes.size();
    put16(bytes, kernel::intel_dmar::STRUCTURE_DRHD);
    put16(bytes, 0u);
    bytes.push_back(flags);
    bytes.push_back(sizeEncoding);
    put16(bytes, segment);
    put64(bytes, registerBase);
    for (const auto& entry : scopes) append_bytes(bytes, entry);
    write16(bytes, start + 2u, static_cast<uint16_t>(bytes.size() - start));
}

void append_rmrr(std::vector<uint8_t>& bytes, uint16_t segment,
                 uint64_t base, uint64_t limit,
                 const std::vector<std::vector<uint8_t>>& scopes)
{
    const size_t start = bytes.size();
    put16(bytes, kernel::intel_dmar::STRUCTURE_RMRR);
    put16(bytes, 0u);
    bytes.push_back(0u);
    bytes.push_back(0u);
    put16(bytes, segment);
    put64(bytes, base);
    put64(bytes, limit);
    for (const auto& entry : scopes) append_bytes(bytes, entry);
    write16(bytes, start + 2u, static_cast<uint16_t>(bytes.size() - start));
}

void append_atsr(std::vector<uint8_t>& bytes, uint8_t flags, uint16_t segment,
                 const std::vector<std::vector<uint8_t>>& scopes = {})
{
    const size_t start = bytes.size();
    put16(bytes, kernel::intel_dmar::STRUCTURE_ATSR);
    put16(bytes, 0u);
    bytes.push_back(flags);
    bytes.push_back(0u);
    put16(bytes, segment);
    for (const auto& entry : scopes) append_bytes(bytes, entry);
    write16(bytes, start + 2u, static_cast<uint16_t>(bytes.size() - start));
}

void append_rhsa(std::vector<uint8_t>& bytes, uint64_t registerBase,
                 uint32_t proximityDomain)
{
    put16(bytes, kernel::intel_dmar::STRUCTURE_RHSA);
    put16(bytes, 20u);
    put32(bytes, 0u);
    put64(bytes, registerBase);
    put32(bytes, proximityDomain);
}

std::vector<uint8_t> finish_table(std::vector<uint8_t> bytes)
{
    if (bytes.empty()) bytes.resize(kernel::intel_dmar::DMAR_HEADER_BYTES, 0u);
    bytes[0] = 'D'; bytes[1] = 'M'; bytes[2] = 'A'; bytes[3] = 'R';
    write32(bytes, 4u, static_cast<uint32_t>(bytes.size()));
    bytes[8u] = 1u;
    bytes[9u] = 0u;
    bytes[36u] = 47u; // host address width minus one
    bytes[37u] = 0u;
    uint8_t sum = 0u;
    for (uint8_t value : bytes) sum = static_cast<uint8_t>(sum + value);
    bytes[9u] = static_cast<uint8_t>(0u - sum);
    return bytes;
}

void repair_checksum(std::vector<uint8_t>& bytes)
{
    bytes[9u] = 0u;
    uint8_t sum = 0u;
    for (uint8_t value : bytes) sum = static_cast<uint8_t>(sum + value);
    bytes[9u] = static_cast<uint8_t>(0u - sum);
}

void append_uefi_descriptor(std::vector<uint8_t>& bytes, uint32_t type,
                            uint64_t base, uint64_t pages,
                            uint64_t attributes = 0u)
{
    put32(bytes, type);
    put32(bytes, 0u);
    put64(bytes, base);
    put64(bytes, 0u);
    put64(bytes, pages);
    put64(bytes, attributes);
}

std::vector<uint8_t> table_with_drhd(
    uint8_t flags, const std::vector<std::vector<uint8_t>>& scopes = {})
{
    std::vector<uint8_t> bytes(kernel::intel_dmar::DMAR_HEADER_BYTES, 0u);
    append_drhd(bytes, flags, 0u, 0u, 0xFEE00000u, scopes);
    return finish_table(bytes);
}

kernel::intel_dmar::PciPath endpoint_path()
{
    kernel::intel_dmar::PciPath path = {};
    path.segment = 0u;
    path.count = 2u;
    path.valid = true;
    path.entries[0] = {0u, 20u, 0u};
    path.entries[1] = {1u, 0u, 0u};
    return path;
}

void test_parser_boundaries()
{
    using namespace kernel::intel_dmar;
    Table table = {};
    std::vector<uint8_t> good = table_with_drhd(1u);
    expect(parse(good.data(), static_cast<uint32_t>(good.size()), &table) ==
               ParseStatus::Valid && table.drhdCount == 1u,
           "valid INCLUDE_PCI_ALL DRHD parses");
    expect(parse(good.data(), DMAR_HEADER_BYTES - 1u, &table) == ParseStatus::Malformed,
           "truncated DMAR header is rejected");

    std::vector<uint8_t> badChecksum = good;
    badChecksum[47u] ^= 1u;
    expect(parse(badChecksum.data(), static_cast<uint32_t>(badChecksum.size()), &table) ==
               ParseStatus::Malformed,
           "bad ACPI checksum is rejected");

    std::vector<uint8_t> badLength = good;
    write32(badLength, 4u, static_cast<uint32_t>(badLength.size() + 1u));
    expect(parse(badLength.data(), static_cast<uint32_t>(badLength.size()), &table) ==
               ParseStatus::Malformed,
           "declared table length beyond copied bytes is rejected");

    std::vector<uint8_t> zeroStructure = good;
    write16(zeroStructure, DMAR_HEADER_BYTES + 2u, 0u);
    zeroStructure = finish_table(zeroStructure);
    expect(parse(zeroStructure.data(), static_cast<uint32_t>(zeroStructure.size()), &table) ==
               ParseStatus::Malformed,
           "zero-length DMAR structure is rejected");

    std::vector<uint8_t> truncatedStructure = good;
    write16(truncatedStructure, DMAR_HEADER_BYTES + 2u, 17u);
    truncatedStructure = finish_table(truncatedStructure);
    expect(parse(truncatedStructure.data(), static_cast<uint32_t>(truncatedStructure.size()), &table) ==
               ParseStatus::Malformed,
           "structure extending beyond the table is rejected");

    std::vector<uint8_t> oversizedStructure = good;
    write16(oversizedStructure, DMAR_HEADER_BYTES + 2u, 0xFFFFu);
    oversizedStructure = finish_table(oversizedStructure);
    expect(parse(oversizedStructure.data(), static_cast<uint32_t>(oversizedStructure.size()), &table) ==
               ParseStatus::Malformed,
           "oversized DMAR structure length is rejected");

    std::vector<uint8_t> oversizedTable = good;
    write32(oversizedTable, 4u, MAX_TABLE_BYTES + 1u);
    expect(parse(oversizedTable.data(), static_cast<uint32_t>(oversizedTable.size()), &table) ==
               ParseStatus::Malformed,
           "DMAR table exceeding the bounded copy limit is rejected");

    std::vector<uint8_t> future = good;
    future[8u] = 2u;
    repair_checksum(future);
    expect(parse(future.data(), static_cast<uint32_t>(future.size()), &table) ==
               ParseStatus::UnsupportedRevision,
           "unsupported DMAR ACPI revision is rejected");

    std::vector<uint8_t> zeroDrhd(kernel::intel_dmar::DMAR_HEADER_BYTES, 0u);
    expect(parse(finish_table(zeroDrhd).data(), DMAR_HEADER_BYTES, &table) ==
               ParseStatus::Malformed,
           "DMAR without a DRHD is rejected");

    std::vector<uint8_t> tooMany(DMAR_HEADER_BYTES, 0u);
    for (uint32_t i = 0u; i < MAX_DRHD_UNITS + 1u; ++i) {
        append_drhd(tooMany, 0u, 0u, 0u,
                    0xFEE00000u + static_cast<uint64_t>(i) * 0x1000u);
    }
    tooMany = finish_table(tooMany);
    expect(parse(tooMany.data(), static_cast<uint32_t>(tooMany.size()), &table) ==
               ParseStatus::CapacityExceeded,
           "bounded DRHD capacity rejects an oversized topology");

    std::vector<uint8_t> badReserved = good;
    badReserved[38u] = 1u;
    badReserved = finish_table(badReserved);
    expect(parse(badReserved.data(), static_cast<uint32_t>(badReserved.size()), &table) ==
               ParseStatus::Malformed,
           "nonzero DMAR reserved header bytes are rejected");

    std::vector<uint8_t> sizedDrhd(DMAR_HEADER_BYTES, 0u);
    append_drhd(sizedDrhd, 1u, 1u, 0u, 0xFEE02000u);
    sizedDrhd = finish_table(sizedDrhd);
    expect(parse(sizedDrhd.data(), static_cast<uint32_t>(sizedDrhd.size()), &table) ==
               ParseStatus::Valid && table.drhd[0].registerBytes == 0x2000u,
           "DRHD size field expands the validated register aperture");

    std::vector<uint8_t> misalignedDrhd(DMAR_HEADER_BYTES, 0u);
    append_drhd(misalignedDrhd, 1u, 1u, 0u, 0xFEE01000u);
    misalignedDrhd = finish_table(misalignedDrhd);
    expect(parse(misalignedDrhd.data(),
                 static_cast<uint32_t>(misalignedDrhd.size()), &table) ==
               ParseStatus::Malformed,
           "DRHD base alignment follows the reported register-set size");

    std::vector<uint8_t> reservedSizeBits(DMAR_HEADER_BYTES, 0u);
    append_drhd(reservedSizeBits, 1u, 0x10u, 0u, 0xFEE00000u);
    reservedSizeBits = finish_table(reservedSizeBits);
    expect(parse(reservedSizeBits.data(),
                 static_cast<uint32_t>(reservedSizeBits.size()), &table) ==
               ParseStatus::Malformed,
           "reserved high DRHD size bits are rejected");

}

void test_scope_routing_and_rmrr()
{
    using namespace kernel::intel_dmar;
    const auto endpoint = scope(SCOPE_ENDPOINT, 0u, {{20u, 0u}, {0u, 0u}});
    const auto bridge = scope(SCOPE_PCI_BRIDGE, 0u, {{20u, 0u}});
    const PciPath path = endpoint_path();
    Table table = {};

    std::vector<uint8_t> explicitTable(DMAR_HEADER_BYTES, 0u);
    append_drhd(explicitTable, 0u, 0u, 0u, 0xFEE00000u, {endpoint});
    append_rmrr(explicitTable, 0u, 0x200000u, 0x20FFFFu, {endpoint});
    explicitTable = finish_table(explicitTable);
    expect(parse(explicitTable.data(), static_cast<uint32_t>(explicitTable.size()), &table) ==
               ParseStatus::Valid,
           "explicit endpoint DRHD and page-aligned RMRR parse");
    Route route = route_device(table, path);
    expect(route.status == RouteStatus::Resolved && route.unitIndex == 0 &&
           route.registerBase == 0xFEE00000u && route.rmrrRequired,
           "endpoint scope routes through its explicit DRHD and carries RMRR requirement");

    std::vector<uint8_t> bridgeTable(DMAR_HEADER_BYTES, 0u);
    append_drhd(bridgeTable, 0u, 0u, 0u, 0xFEE00000u, {bridge});
    bridgeTable = finish_table(bridgeTable);
    expect(parse(bridgeTable.data(), static_cast<uint32_t>(bridgeTable.size()), &table) ==
               ParseStatus::Valid &&
           route_device(table, path).status == RouteStatus::Resolved,
           "PCI bridge scope covers a downstream endpoint");

    std::vector<uint8_t> allTable = table_with_drhd(1u);
    expect(parse(allTable.data(), static_cast<uint32_t>(allTable.size()), &table) ==
               ParseStatus::Valid &&
           route_device(table, path).status == RouteStatus::Resolved,
           "INCLUDE_PCI_ALL DRHD is the fallback route");

    std::vector<uint8_t> unmatched(DMAR_HEADER_BYTES, 0u);
    append_drhd(unmatched, 0u, 0u, 0u, 0xFEE00000u,
                {scope(SCOPE_ENDPOINT, 0u, {{21u, 0u}, {0u, 0u}})});
    unmatched = finish_table(unmatched);
    parse(unmatched.data(), static_cast<uint32_t>(unmatched.size()), &table);
    expect(route_device(table, path).status == RouteStatus::NotCovered,
           "uncovered PCI endpoint is not guessed into a unit");

    std::vector<uint8_t> ambiguous(DMAR_HEADER_BYTES, 0u);
    append_drhd(ambiguous, 0u, 0u, 0u, 0xFEE00000u, {endpoint});
    append_drhd(ambiguous, 0u, 0u, 0u, 0xFEE01000u, {endpoint});
    ambiguous = finish_table(ambiguous);
    parse(ambiguous.data(), static_cast<uint32_t>(ambiguous.size()), &table);
    expect(route_device(table, path).status == RouteStatus::Ambiguous,
           "overlapping explicit DRHD scopes remain ambiguous");

    std::vector<uint8_t> explicitAndAll(DMAR_HEADER_BYTES, 0u);
    append_drhd(explicitAndAll, 0u, 0u, 0u, 0xFEE00000u, {endpoint});
    append_drhd(explicitAndAll, 1u, 0u, 0u, 0xFEE01000u);
    explicitAndAll = finish_table(explicitAndAll);
    expect(parse(explicitAndAll.data(), static_cast<uint32_t>(explicitAndAll.size()), &table) ==
               ParseStatus::Valid &&
           route_device(table, path).status == RouteStatus::Resolved &&
           route_device(table, path).unitIndex == 0,
           "explicit DRHD routing takes precedence over its later INCLUDE_PCI_ALL unit");

    std::vector<uint8_t> includeAllWrongOrder(DMAR_HEADER_BYTES, 0u);
    append_drhd(includeAllWrongOrder, 1u, 0u, 0u, 0xFEE00000u);
    append_drhd(includeAllWrongOrder, 0u, 0u, 0u, 0xFEE01000u, {endpoint});
    includeAllWrongOrder = finish_table(includeAllWrongOrder);
    expect(parse(includeAllWrongOrder.data(),
                 static_cast<uint32_t>(includeAllWrongOrder.size()), &table) ==
               ParseStatus::Malformed,
           "INCLUDE_PCI_ALL must follow the other DRHDs for its segment");

    std::vector<uint8_t> wrongSegment = table_with_drhd(1u);
    parse(wrongSegment.data(), static_cast<uint32_t>(wrongSegment.size()), &table);
    kernel::intel_dmar::PciPath otherSegment = path;
    otherSegment.segment = 1u;
    expect(route_device(table, otherSegment).status == RouteStatus::NotCovered,
           "INCLUDE_PCI_ALL is limited to its PCI segment");

    std::vector<uint8_t> duplicateRmrr(DMAR_HEADER_BYTES, 0u);
    append_drhd(duplicateRmrr, 1u, 0u, 0u, 0xFEE00000u);
    append_rmrr(duplicateRmrr, 0u, 0x200000u, 0x20FFFFu, {endpoint});
    append_rmrr(duplicateRmrr, 0u, 0x200000u, 0x20FFFFu, {endpoint});
    duplicateRmrr = finish_table(duplicateRmrr);
    expect(parse(duplicateRmrr.data(), static_cast<uint32_t>(duplicateRmrr.size()), &table) ==
               ParseStatus::Malformed,
           "duplicate RMRR ownership declaration is rejected");

    std::vector<uint8_t> duplicateDrhd(DMAR_HEADER_BYTES, 0u);
    append_drhd(duplicateDrhd, 1u, 0u, 0u, 0xFEE00000u);
    append_drhd(duplicateDrhd, 0u, 0u, 0u, 0xFEE00000u, {endpoint});
    duplicateDrhd = finish_table(duplicateDrhd);
    expect(parse(duplicateDrhd.data(), static_cast<uint32_t>(duplicateDrhd.size()), &table) ==
               ParseStatus::Malformed,
           "duplicate DRHD register identity is rejected");

    std::vector<uint8_t> badRmrr(DMAR_HEADER_BYTES, 0u);
    append_drhd(badRmrr, 1u, 0u, 0u, 0xFEE00000u);
    append_rmrr(badRmrr, 0u, 0x200001u, 0x20FFFFu, {endpoint});
    badRmrr = finish_table(badRmrr);
    expect(parse(badRmrr.data(), static_cast<uint32_t>(badRmrr.size()), &table) ==
               ParseStatus::Malformed,
           "unaligned RMRR boundary is rejected");
}

void test_atsr_rhsa_and_unknown_structures()
{
    using namespace kernel::intel_dmar;
    Table table = {};
    std::vector<uint8_t> supported(DMAR_HEADER_BYTES, 0u);
    append_drhd(supported, 1u, 0u, 0u, 0xFEE00000u);
    append_atsr(supported, 1u, 0u);
    append_rhsa(supported, 0xFEE00000u, 3u);
    supported = finish_table(supported);
    expect(parse(supported.data(), static_cast<uint32_t>(supported.size()), &table) ==
               ParseStatus::Valid && table.atsrCount == 1u && table.rhsaCount == 1u,
           "all-ports ATSR and matching RHSA parse");

    std::vector<uint8_t> badAtsr(DMAR_HEADER_BYTES, 0u);
    append_drhd(badAtsr, 1u, 0u, 0u, 0xFEE00000u);
    append_atsr(badAtsr, 1u, 0u,
                {scope(SCOPE_PCI_BRIDGE, 0u, {{20u, 0u}})});
    badAtsr = finish_table(badAtsr);
    expect(parse(badAtsr.data(), static_cast<uint32_t>(badAtsr.size()), &table) ==
               ParseStatus::Malformed,
           "ATSR all-ports flag cannot carry a scope list");

    std::vector<uint8_t> badRhsa(DMAR_HEADER_BYTES, 0u);
    append_drhd(badRhsa, 1u, 0u, 0u, 0xFEE00000u);
    append_rhsa(badRhsa, 0xFEE01000u, 0u);
    badRhsa = finish_table(badRhsa);
    expect(parse(badRhsa.data(), static_cast<uint32_t>(badRhsa.size()), &table) ==
               ParseStatus::Malformed,
           "RHSA for an unknown DRHD register base is rejected");

    std::vector<uint8_t> future(DMAR_HEADER_BYTES, 0u);
    append_drhd(future, 1u, 0u, 0u, 0xFEE00000u);
    put16(future, 7u);
    put16(future, 4u);
    future = finish_table(future);
    expect(parse(future.data(), static_cast<uint32_t>(future.size()), &table) ==
               ParseStatus::ValidWithUnsupported && table.unsupportedStructureCount == 1u,
           "unknown length-delimited DMAR structure is retained as unsupported");
    expect(route_device(table, endpoint_path()).status == RouteStatus::Unsupported,
           "unsupported DMAR structure prevents an unsafe route claim");

    std::vector<uint8_t> wrongOrder(DMAR_HEADER_BYTES, 0u);
    append_atsr(wrongOrder, 1u, 0u);
    append_drhd(wrongOrder, 1u, 0u, 0u, 0xFEE00000u);
    wrongOrder = finish_table(wrongOrder);
    expect(parse(wrongOrder.data(), static_cast<uint32_t>(wrongOrder.size()), &table) ==
               ParseStatus::Malformed,
           "out-of-order DMAR structures are rejected");
}

void test_register_classification_and_dma_gate()
{
    using namespace kernel::intel_dmar;
    expect(valid_register_resource(0xFEE00000u, 0x1000u, 0x38u),
           "VT-d register resource validates its complete aperture");
    expect(!valid_register_resource(0xFEE00001u, 0x1000u, 0x38u),
           "misaligned VT-d register base is rejected");
    expect(!valid_register_resource(0xFEE00000u, 0x1000u, 0x1001u),
           "required VT-d span cannot exceed declared resource");
    expect(!valid_register_resource(UINT64_MAX - 0xFFFu, 0x2000u, 0x38u),
           "overflowing VT-d register resource is rejected");

    const uint64_t capability = (1ULL << 16u) | (1ULL << 8u);
    RegisterSnapshot registers = classify_registers(
        true, 0x10u, capability, 0u, 1u << 31u, 0x1000u, 0u);
    expect(registers.status == RegisterStatus::Readable &&
           registers.translation == TranslationState::EnabledObserved &&
           registers.rootTable == RootTableState::Configured &&
           registers.capabilityValid && registers.extendedCapabilityValid &&
            registers.translationStatusValid && registers.faultStatusValid,
           "supported VT-d snapshot is classified as read-only observed state");

    registers = classify_registers(true, 0x10u, capability, 0u, 0u, 0u, 0u);
    expect(registers.status == RegisterStatus::Readable &&
           registers.translation == TranslationState::DisabledObserved &&
           registers.rootTable == RootTableState::NotConfigured,
           "translation-disabled and absent root table states are visible");
    registers = classify_registers(true, 0x10u, capability, 0u,
                                   1u << 31u, 0x1000u, 0u);
    expect(registers.status == RegisterStatus::Readable &&
           registers.translation == TranslationState::EnabledObserved,
           "read-only GSTS.TES determines translation state");
    expect(classify_registers(true, 0x10u, capability, 0u, UINT32_MAX,
                              0u, 0u).status == RegisterStatus::ReadUnavailable,
           "unreadable GSTS prevents a translation-state conclusion");
    expect(classify_registers(true, 0x20u, capability, 0u, 0u, 0u, 0u).status ==
               RegisterStatus::UnsupportedVersion,
           "unsupported VT-d major version prevents further qualification");
    expect(classify_registers(true, 0x10u, 0u, 0u, 0u, 0u, 0u).status ==
               RegisterStatus::UnsupportedCapabilities,
           "missing MGAW or SAGAW support is reported as unsupported");
    expect(classify_registers(false, 0x10u, capability, 0u, 0u, 0u, 0u).status ==
               RegisterStatus::ReadUnavailable,
           "incomplete read snapshot is never treated as valid");
    expect(classify_root_table(true, 0x1001u) == RootTableState::Invalid &&
           classify_root_table(true, 0x1400u) == RootTableState::Configured &&
           classify_root_table(true, 0x1800u) == RootTableState::Invalid,
           "RTADDR reserved bits and unsupported translation table mode are checked");

    RegisterSnapshot protectedState{};
    expect(classify_protected_memory(0u, 0u, 0u, 0u, 0u, 0u,
                                     &protectedState) ==
               ProtectedMemoryState::Disabled,
           "unsupported and disabled PMR reports are classified without writes");
    expect(classify_protected_memory(1ULL << 5u, (1u << 31u) | 1u,
                                     0x10000000u, 0x20000000u, 0u, 0u,
                                     &protectedState) ==
               ProtectedMemoryState::Enabled &&
           protectedState.protectedLowActive &&
           protectedState.protectedLowBase == 0x10000000u &&
           protectedState.protectedLowLimit == 0xFFFFFFFFu,
           "active PLMR is conservatively bounded through the low 4 GiB");
    const kernel::dma_memory::PhysicalRange belowLowPmr = {0x08000000u, 4096u};
    const kernel::dma_memory::PhysicalRange insideLowPmr = {0x10000000u, 4096u};
    expect(!protected_memory_overlaps_workspace(
               protectedState, &belowLowPmr, 1u) &&
           protected_memory_overlaps_workspace(
               protectedState, &insideLowPmr, 1u),
           "PLMR overlap check blocks possible protected workspace addresses");
    expect(classify_protected_memory(1ULL << 6u, (1u << 31u) | 1u,
                                     0u, 0u, 0x0000000100000000ULL,
                                     0x0000000200000000ULL,
                                     &protectedState) ==
               ProtectedMemoryState::Enabled &&
           protectedState.protectedHighActive &&
           protectedState.protectedHighLimit == 0x000FFFFFFFFFFFFFULL,
           "active PHMR is conservatively bounded through the 52-bit limit");
    const kernel::dma_memory::PhysicalRange belowHighPmr = {0x1000u, 4096u};
    const kernel::dma_memory::PhysicalRange insideHighPmr = {0x100000000ULL, 4096u};
    expect(!protected_memory_overlaps_workspace(
               protectedState, &belowHighPmr, 1u) &&
           protected_memory_overlaps_workspace(
               protectedState, &insideHighPmr, 1u),
           "PHMR overlap check blocks workspace at or above its base");
    expect(classify_protected_memory(1ULL << 5u, 1u << 31u,
                                     0u, 0u, 0u, 0u,
                                     &protectedState) ==
               ProtectedMemoryState::Unknown,
           "PMEN transition without PRS readback blocks direct DMA");
    expect(classify_protected_memory(1ULL << 5u, (1u << 31u) | 1u,
                                     0x10000001u, 0x20000000u, 0u, 0u,
                                     &protectedState) ==
               ProtectedMemoryState::Invalid,
           "malformed PLMR reserved bits block direct DMA");

    expect(!dma_eligibility_allowed(RouteStatus::Resolved, RegisterStatus::Readable,
               TranslationState::EnabledObserved, RootTableState::Configured,
               true, false, true),
           "enabled VT-d and validated buffers do not authorize DMA without a device mapping");
    expect(!dma_eligibility_allowed(RouteStatus::Resolved, RegisterStatus::Readable,
               TranslationState::EnabledObserved, RootTableState::Configured,
               true, true, true),
           "R7 does not authorize Intel DMA before its domain-mapping implementation exists");
    expect(!dma_eligibility_allowed(RouteStatus::NotCovered, RegisterStatus::Readable,
               TranslationState::EnabledObserved, RootTableState::Configured,
               true, true, true),
           "unrouted device is never DMA eligible");
    expect(!dma_eligibility_allowed(RouteStatus::Resolved,
               RegisterStatus::UnsupportedCapabilities,
               TranslationState::EnabledObserved, RootTableState::Configured,
               true, true, true),
           "unsupported VT-d capabilities block DMA eligibility");
}

void test_rmrr_memory_ownership()
{
    using namespace kernel::intel_dmar;
    const std::vector<uint8_t> selectedScope = scope(
        SCOPE_ENDPOINT, 0u, {{20u, 0u}, {0u, 0u}});
    const std::vector<uint8_t> unrelatedScope = scope(
        SCOPE_ENDPOINT, 2u, {{4u, 0u}});
    std::vector<uint8_t> bytes(DMAR_HEADER_BYTES, 0u);
    append_drhd(bytes, 1u, 0u, 0u, 0xFEE00000u);
    append_rmrr(bytes, 0u, 0x200000u, 0x20FFFFu, {selectedScope});
    append_rmrr(bytes, 2u, 0x300000u, 0x30FFFFu, {unrelatedScope});
    bytes = finish_table(bytes);
    Table table = {};
    expect(parse(bytes.data(), static_cast<uint32_t>(bytes.size()), &table) ==
               ParseStatus::Valid && table.rmrrCount == 2u,
           "valid selected and unrelated RMRRs parse together");

    std::vector<uint8_t> memoryMap;
    append_uefi_descriptor(memoryMap, kernel::dma_memory::UEFI_MEMORY_RESERVED,
                           0x200000u, 16u);
    append_uefi_descriptor(memoryMap, kernel::dma_memory::UEFI_MEMORY_RESERVED,
                           0x300000u, 16u);
    append_uefi_descriptor(memoryMap, kernel::dma_memory::UEFI_MEMORY_LOADER_DATA,
                           0x400000u, 256u);
    const kernel::dma_memory::MemoryMapView map = {
        memoryMap.data(), 3u, 40u};
    using kernel::dma_memory::MemoryMapCheck;
    expect(kernel::dma_memory::range_has_ownership(
               map, 0x400000u, 0x2000u, false, 64u) == MemoryMapCheck::Valid,
           "DMA region receives complete loader-owned physical backing");
    expect(kernel::dma_memory::range_has_ownership(
               map, 0x400020u, 0x2000u, false, 64u) ==
               MemoryMapCheck::InvalidAlignment,
           "misaligned device-visible DMA base is rejected");
    expect(kernel::dma_memory::range_has_ownership(
               map, UINT64_MAX - 0xFFFu, 0x2000u, false, 64u) ==
               MemoryMapCheck::RangeOverflow,
           "device-visible physical range overflow is rejected");
    expect(kernel::dma_memory::range_has_ownership(
               map, 0x4FF000u, 0x2000u, false, 64u) ==
               MemoryMapCheck::RangeUncovered,
           "partially backed DMA allocation is rejected");
    std::vector<uint8_t> invalidDescriptor;
    append_uefi_descriptor(invalidDescriptor,
                           kernel::dma_memory::UEFI_MEMORY_LOADER_DATA,
                           0x600000u, 0u);
    const kernel::dma_memory::MemoryMapView invalidDescriptorMap = {
        invalidDescriptor.data(), 1u, 40u};
    expect(kernel::dma_memory::range_has_ownership(
               invalidDescriptorMap, 0x600000u, 0x1000u, false, 64u) ==
               MemoryMapCheck::InvalidDescriptor,
           "zero-sized UEFI backing descriptor is rejected");

    const kernel::dma_memory::PhysicalRange dmaRanges[] = {
        {0x400000u, 0x2000u}
    };
    const RmrrMemorySummary summary = validate_rmrr_memory(
        table, endpoint_path(), map, dmaRanges, 1u);
    expect(summary.status == RmrrMemoryStatus::Valid &&
           summary.regionCount == 2u && summary.relevantCount == 1u &&
           summary.unrelatedCount == 1u,
           "RMRR applicability and retained firmware memory are verified per scope");

    std::vector<uint8_t> runtimeMap;
    append_uefi_descriptor(runtimeMap, kernel::dma_memory::UEFI_MEMORY_RUNTIME_CODE,
                           0x200000u, 16u,
                           kernel::dma_memory::UEFI_MEMORY_RUNTIME_ATTRIBUTE);
    append_uefi_descriptor(runtimeMap, kernel::dma_memory::UEFI_MEMORY_RUNTIME_DATA,
                           0x300000u, 16u,
                           kernel::dma_memory::UEFI_MEMORY_RUNTIME_ATTRIBUTE);
    append_uefi_descriptor(runtimeMap, kernel::dma_memory::UEFI_MEMORY_LOADER_DATA,
                           0x400000u, 256u);
    const kernel::dma_memory::MemoryMapView runtimeMemoryMap = {
        runtimeMap.data(), 3u, 40u};
    expect(validate_rmrr_memory(table, endpoint_path(), runtimeMemoryMap,
                                dmaRanges, 1u).status == RmrrMemoryStatus::Valid,
           "UEFI runtime descriptors satisfy Intel RMRR reserved-memory reporting");

    std::vector<uint8_t> runtimeWithoutAttributeMap;
    append_uefi_descriptor(runtimeWithoutAttributeMap,
                           kernel::dma_memory::UEFI_MEMORY_RUNTIME_CODE,
                           0x200000u, 16u);
    append_uefi_descriptor(runtimeWithoutAttributeMap,
                           kernel::dma_memory::UEFI_MEMORY_RESERVED,
                           0x300000u, 16u);
    append_uefi_descriptor(runtimeWithoutAttributeMap,
                           kernel::dma_memory::UEFI_MEMORY_LOADER_DATA,
                           0x400000u, 256u);
    const kernel::dma_memory::MemoryMapView runtimeWithoutAttribute = {
        runtimeWithoutAttributeMap.data(), 3u, 40u};
    expect(validate_rmrr_memory(table, endpoint_path(), runtimeWithoutAttribute,
                                dmaRanges, 1u).status ==
               RmrrMemoryStatus::ReservedMemoryUnproven,
           "runtime memory type without EFI_MEMORY_RUNTIME is not retained as RMRR");

    std::vector<uint8_t> unsupportedRmrrMap;
    append_uefi_descriptor(unsupportedRmrrMap,
                           kernel::dma_memory::UEFI_MEMORY_ACPI_RECLAIM,
                           0x200000u, 16u);
    append_uefi_descriptor(unsupportedRmrrMap, kernel::dma_memory::UEFI_MEMORY_RESERVED,
                           0x300000u, 16u);
    append_uefi_descriptor(unsupportedRmrrMap,
                           kernel::dma_memory::UEFI_MEMORY_LOADER_DATA,
                           0x400000u, 256u);
    const kernel::dma_memory::MemoryMapView unsupportedRmrrMemoryMap = {
        unsupportedRmrrMap.data(), 3u, 40u};
    expect(validate_rmrr_memory(table, endpoint_path(), unsupportedRmrrMemoryMap,
                                dmaRanges, 1u).status ==
               RmrrMemoryStatus::ReservedMemoryUnproven,
           "ACPI reclaim memory does not satisfy Intel RMRR memory-map contract");

    const kernel::dma_memory::PhysicalRange overlappingDma[] = {
        {0x20F000u, 0x2000u}
    };
    expect(validate_rmrr_memory(table, endpoint_path(), map,
                                overlappingDma, 1u).status ==
               RmrrMemoryStatus::WorkspaceOverlap,
           "RMRR overlap with a kernel DMA workspace blocks use");

    std::vector<uint8_t> conventionalMap;
    append_uefi_descriptor(conventionalMap, 7u, 0x200000u, 16u);
    append_uefi_descriptor(conventionalMap, 0u, 0x300000u, 16u);
    append_uefi_descriptor(conventionalMap, 2u, 0x400000u, 256u);
    const kernel::dma_memory::MemoryMapView unreservedMap = {
        conventionalMap.data(), 3u, 40u};
    expect(validate_rmrr_memory(table, endpoint_path(), unreservedMap,
                                dmaRanges, 1u).status ==
               RmrrMemoryStatus::ReservedMemoryUnproven,
           "conventional memory cannot satisfy firmware RMRR ownership");

    const kernel::dma_memory::MemoryMapView missingMap = {nullptr, 0u, 0u};
    expect(validate_rmrr_memory(table, endpoint_path(), missingMap,
                                dmaRanges, 1u).status ==
               RmrrMemoryStatus::MemoryMapUnavailable,
           "missing UEFI ownership map blocks RMRR qualification");
    const kernel::dma_memory::PhysicalRange overflowingWorkspace[] = {
        {UINT64_MAX - 0xFu, 0x40u}
    };
    expect(validate_rmrr_memory(table, endpoint_path(), map,
                                overflowingWorkspace, 1u).status ==
                RmrrMemoryStatus::InvalidWorkspace,
            "overflowing DMA workspace range is rejected before RMRR comparison");
}

void test_direct_physical_dma_contract()
{
    using namespace kernel::intel_dmar;
    DirectDmaEvidence evidence{};
    evidence.pciIdentityValidated = true;
    evidence.route = RouteStatus::Resolved;
    evidence.registers = RegisterStatus::Readable;
    evidence.translation = TranslationState::DisabledObserved;
    evidence.rootTable = RootTableState::NotConfigured;
    evidence.physicalBackingValidated = true;
    evidence.deviceAddressabilityValidated = true;
    evidence.rmrrMemory = RmrrMemoryStatus::Valid;
    evidence.busMasterDisabled = true;
    evidence.workspaceExclusive = true;
    evidence.protectedMemory = ProtectedMemoryState::Disabled;
    expect(evaluate_direct_physical_dma(evidence) == DirectDmaDecision::Verified,
           "direct physical DMA passes only with all independent proofs");

    evidence.protectedMemory = ProtectedMemoryState::Unknown;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedProtectedMemoryUnknown,
           "unknown PMEN or protected-region state blocks direct physical DMA");
    evidence.protectedMemory = ProtectedMemoryState::Enabled;
    evidence.protectedMemoryOverlap = true;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedProtectedMemoryOverlap,
           "DMA workspace overlap with active PMR blocks direct physical DMA");
    evidence.protectedMemoryOverlap = false;
    expect(evaluate_direct_physical_dma(evidence) == DirectDmaDecision::Verified,
           "active PMR permits DMA only when workspace is proven disjoint");
    evidence.protectedMemory = ProtectedMemoryState::Disabled;

    evidence.translation = TranslationState::EnabledObserved;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedTranslationEnabled,
           "enabled VT-d blocks direct physical DMA without mappings");
    evidence.translation = TranslationState::Unknown;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedTranslationUnknown,
           "unknown or conflicting VT-d state blocks direct physical DMA");

    evidence.translation = TranslationState::DisabledObserved;
    evidence.route = RouteStatus::Ambiguous;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedRoutingAmbiguous,
           "ambiguous DRHD/requester routing blocks direct physical DMA");
    evidence.route = RouteStatus::Resolved;
    evidence.pciIdentityValidated = false;
    expect(evaluate_direct_physical_dma(evidence) == DirectDmaDecision::BlockedIdentity,
           "incorrect PCI requester identity blocks direct physical DMA");
    evidence.pciIdentityValidated = true;
    evidence.physicalBackingValidated = false;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedPhysicalBacking,
           "insufficient or unowned physical backing blocks direct DMA");
    evidence.physicalBackingValidated = true;
    evidence.deviceAddressabilityValidated = false;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedAddressability,
           "device address-width failure blocks direct DMA");
    evidence.deviceAddressabilityValidated = true;
    evidence.rmrrMemory = RmrrMemoryStatus::ReservedMemoryUnproven;
    expect(evaluate_direct_physical_dma(evidence) == DirectDmaDecision::BlockedRmrr,
           "RMRR memory without proven reservation blocks direct DMA");
    evidence.rmrrMemory = RmrrMemoryStatus::Valid;
    evidence.busMasterDisabled = false;
    expect(evaluate_direct_physical_dma(evidence) == DirectDmaDecision::BlockedBusMaster,
           "unowned preexisting PCI bus mastering blocks direct DMA");
    evidence.busMasterDisabled = true;
    evidence.workspaceExclusive = false;
    expect(evaluate_direct_physical_dma(evidence) ==
               DirectDmaDecision::BlockedWorkspaceOwnership,
           "shared controller DMA workspace blocks direct DMA");
    evidence.workspaceExclusive = true;
    evidence.rootTable = RootTableState::Invalid;
    expect(evaluate_direct_physical_dma(evidence) == DirectDmaDecision::BlockedRegisters,
           "invalid VT-d root-table register state blocks direct DMA");
}

} // namespace

int main()
{
    test_parser_boundaries();
    test_scope_routing_and_rmrr();
    test_atsr_rhsa_and_unknown_structures();
    test_register_classification_and_dma_gate();
    test_rmrr_memory_ownership();
    test_direct_physical_dma_contract();
    if (failures == 0) {
        std::cout << "Intel DMAR and VT-d tests PASS (" << assertions
                  << " assertions)\n";
        return 0;
    }
    std::cerr << failures << " Intel DMAR and VT-d test failure(s)\n";
    return 1;
}
