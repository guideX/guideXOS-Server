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
        true, 0x10u, capability, 0u, 0u, 1u << 31u,
        0x1000u, 0u);
    expect(registers.status == RegisterStatus::Readable &&
           registers.translation == TranslationState::EnabledObserved &&
           registers.rootTable == RootTableState::Configured &&
           registers.capabilityValid && registers.extendedCapabilityValid &&
           registers.commandStatusValid && registers.faultStatusValid,
           "supported VT-d snapshot is classified as read-only observed state");

    registers = classify_registers(true, 0x10u, capability, 0u, 0u, 0u, 0u, 0u);
    expect(registers.status == RegisterStatus::Readable &&
           registers.translation == TranslationState::DisabledObserved &&
           registers.rootTable == RootTableState::NotConfigured,
           "translation-disabled and absent root table states are visible");
    expect(classify_registers(true, 0x20u, capability, 0u, 0u, 0u, 0u, 0u).status ==
               RegisterStatus::UnsupportedVersion,
           "unsupported VT-d major version prevents further qualification");
    expect(classify_registers(true, 0x10u, 0u, 0u, 0u, 0u, 0u, 0u).status ==
               RegisterStatus::UnsupportedCapabilities,
           "missing MGAW or SAGAW support is reported as unsupported");
    expect(classify_registers(false, 0x10u, capability, 0u, 0u, 0u, 0u, 0u).status ==
               RegisterStatus::ReadUnavailable,
           "incomplete read snapshot is never treated as valid");
    expect(classify_root_table(true, 0x1001u) == RootTableState::Invalid &&
           classify_root_table(true, 0x1400u) == RootTableState::Configured &&
           classify_root_table(true, 0x1800u) == RootTableState::Invalid,
           "RTADDR reserved bits and unsupported translation table mode are checked");

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

} // namespace

int main()
{
    test_parser_boundaries();
    test_scope_routing_and_rmrr();
    test_atsr_rhsa_and_unknown_structures();
    test_register_classification_and_dma_gate();
    if (failures == 0) {
        std::cout << "Intel DMAR and VT-d tests PASS (" << assertions
                  << " assertions)\n";
        return 0;
    }
    std::cerr << failures << " Intel DMAR and VT-d test failure(s)\n";
    return 1;
}
