// Intel VT-d / DMA-remapping read-only discovery.

#include "include/kernel/vtd.h"

namespace kernel {
namespace vtd {

static uint64_t s_acpiRsdpPhysical = 0u;
static Audit s_audit = {};

static uint32_t read32(uint64_t address)
{
    return *reinterpret_cast<volatile const uint32_t*>(address);
}

static uint64_t read64(uint64_t address)
{
    return *reinterpret_cast<volatile const uint64_t*>(address);
}

static bool physical_range_valid(uint64_t address, uint64_t length)
{
    return address != 0u && length != 0u &&
           address <= (~0ULL - (length - 1u));
}

static bool physical_table_header_valid(uint64_t address,
                                        const char* expectedSignature,
                                        uint32_t* lengthOut)
{
    if (!physical_range_valid(address, ACPI_SDT_HEADER_BYTES)) return false;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(address);
    if (expectedSignature && !signature_equals(bytes, expectedSignature)) {
        return false;
    }
    const uint32_t length = read_le32(bytes + 4u);
    if (length < ACPI_SDT_HEADER_BYTES || length > ACPI_MAX_TABLE_BYTES) {
        return false;
    }
    if (lengthOut) *lengthOut = length;
    return true;
}

static bool rsdp_valid(uint64_t address, uint64_t* rootAddressOut,
                       bool* xsdtOut)
{
    if (!physical_range_valid(address, 20u)) return false;
    const uint8_t* rsdp = reinterpret_cast<const uint8_t*>(address);
    static const char signature[] = "RSD PTR ";
    for (uint8_t i = 0; i < 8u; ++i) {
        if (rsdp[i] != static_cast<uint8_t>(signature[i])) return false;
    }
    if (!checksum_valid(rsdp, 20u)) return false;

    const uint8_t revision = rsdp[15];
    if (revision >= 2u) {
        if (!physical_range_valid(address, 36u)) return false;
        const uint32_t length = read_le32(rsdp + 20u);
        if (length < 36u || length > ACPI_MAX_TABLE_BYTES ||
            !checksum_valid(rsdp, length)) return false;
        const uint64_t xsdtAddress = read_le64(rsdp + 24u);
        if (xsdtAddress != 0u) {
            if (rootAddressOut) *rootAddressOut = xsdtAddress;
            if (xsdtOut) *xsdtOut = true;
            return true;
        }
    }

    const uint64_t rsdtAddress = read_le32(rsdp + 16u);
    if (rsdtAddress == 0u) return false;
    if (rootAddressOut) *rootAddressOut = rsdtAddress;
    if (xsdtOut) *xsdtOut = false;
    return true;
}

static bool capture_unit(uint64_t base, VtdRegisterSnapshot* output)
{
    if (!output || (base & 0xFFFu) != 0u ||
        !physical_range_valid(base, VTD_REGISTER_MAP_BYTES)) {
        return false;
    }

    VtdRegisterValues values = {};
    values.version = read32(base + VTD_VER_REG);
    values.cap = read64(base + VTD_CAP_REG);
    values.ecap = read64(base + VTD_ECAP_REG);
    values.gcmd = read32(base + VTD_GCMD_REG);
    values.gsts = read32(base + VTD_GSTS_REG);
    values.rootTableAddress = read64(base + VTD_RTADDR_REG);
    values.faultStatus = read32(base + VTD_FSTS_REG);

    if (values.version == 0xFFFFFFFFu || values.cap == ~0ULL ||
        values.ecap == ~0ULL || values.gsts == 0xFFFFFFFFu ||
        values.faultStatus == 0xFFFFFFFFu) {
        return false;
    }

    values.faultRecordReadable = false;
    values.faultLow = 0u;
    values.faultHigh = 0u;
    const uint32_t recordOffset = cap_fault_record_offset(values.cap);
    const uint8_t recordCount = cap_fault_record_count(values.cap);
    const uint8_t selected = primary_fault_pending(values.faultStatus)
        ? fault_record_index(values.faultStatus) : VTD_INVALID_INDEX;
    if (recordCount != 0u &&
        recordOffset >= VTD_FAULT_RECORDS_REG &&
        recordOffset <= (VTD_REGISTER_MAP_BYTES - 16u) &&
        selected != VTD_INVALID_INDEX && selected < recordCount &&
        recordOffset <= (VTD_REGISTER_MAP_BYTES - 16u -
                         static_cast<uint32_t>(selected) * 16u)) {
        const uint64_t record = base + recordOffset +
                                static_cast<uint32_t>(selected) * 16u;
        values.faultLow = read64(record);
        values.faultHigh = read64(record + 8u);
        values.faultRecordReadable = true;
    }

    return decode_register_values(base, values, output);
}

void set_acpi_rsdp(uint64_t physicalAddress)
{
    s_acpiRsdpPhysical = physicalAddress;
    s_audit = {};
}

bool discover(const PciBdf& target)
{
    s_audit = {};
    s_audit.target = target;
    s_audit.acpiAvailable = s_acpiRsdpPhysical != 0u;
    s_audit.classification = Classification::Unavailable;
    if (!s_audit.acpiAvailable) {
        s_audit.failure = "ACPI RSDP was not supplied by the boot handoff";
        return false;
    }

    uint64_t rootAddress = 0u;
    bool xsdt = false;
    if (!rsdp_valid(s_acpiRsdpPhysical, &rootAddress, &xsdt)) {
        s_audit.failure = "ACPI RSDP is unavailable or malformed";
        return false;
    }

    uint32_t rootLength = 0u;
    if (!physical_table_header_valid(rootAddress, xsdt ? "XSDT" : "RSDT",
                                    &rootLength)) {
        s_audit.failure = "ACPI root table is unavailable or malformed";
        return false;
    }
    if (!checksum_valid(reinterpret_cast<const uint8_t*>(rootAddress),
                        rootLength)) {
        s_audit.failure = "ACPI root table checksum is invalid";
        return false;
    }
    const uint32_t entryBytes = xsdt ? 8u : 4u;
    if (rootLength < ACPI_SDT_HEADER_BYTES ||
        ((rootLength - ACPI_SDT_HEADER_BYTES) % entryBytes) != 0u) {
        s_audit.failure = "ACPI root table entry area is malformed";
        return false;
    }

    const uint32_t entryCount =
        (rootLength - ACPI_SDT_HEADER_BYTES) / entryBytes;
    if (entryCount > 256u) {
        s_audit.failure = "ACPI root table exceeds the bounded audit limit";
        return false;
    }

    const uint8_t* root = reinterpret_cast<const uint8_t*>(rootAddress);
    for (uint32_t index = 0u; index < entryCount; ++index) {
        const uint64_t tableAddress = xsdt
            ? read_le64(root + ACPI_SDT_HEADER_BYTES + index * 8u)
            : static_cast<uint64_t>(read_le32(
                  root + ACPI_SDT_HEADER_BYTES + index * 4u));
        if (tableAddress == 0u) continue;
        uint32_t tableLength = 0u;
        if (!physical_table_header_valid(tableAddress, nullptr,
                                         &tableLength)) {
            s_audit.failure = "an ACPI table header is malformed";
            return false;
        }
        const uint8_t* table = reinterpret_cast<const uint8_t*>(tableAddress);
        if (!signature_equals(table, "DMAR")) continue;
        if (s_audit.dmarTablePresent) {
            s_audit.failure = "multiple DMAR tables are not supported by this bounded audit";
            return false;
        }
        s_audit.dmarTablePresent = true;
        if (!parse_dmar_table(table, tableLength, target, &s_audit.dmar)) {
            s_audit.failure = s_audit.dmar.failure != nullptr
                ? s_audit.dmar.failure : "DMAR parsing failed";
            return false;
        }
        s_audit.dmarRevision = s_audit.dmar.revision;
        s_audit.drhdCount = s_audit.dmar.drhdCount;
        s_audit.rmrrCount = s_audit.dmar.rmrrCount;
        s_audit.matchingDrhdFound = s_audit.dmar.matchingDrhdFound;
        s_audit.matchingDrhdIncludeAll = s_audit.dmar.matchingDrhdIncludeAll;
        s_audit.matchingDrhdExplicitScope = s_audit.dmar.matchingDrhdExplicitScope;
        s_audit.matchingRegisterBase = s_audit.dmar.matchingRegisterBase;
        s_audit.rmrrApplicable = s_audit.dmar.rmrrApplicable;
    }

    if (!s_audit.dmarTablePresent) {
        s_audit.dmar.valid = false;
        s_audit.failure = "ACPI DMAR table is not present";
        s_audit.classification = Classification::NoDmar;
        return false;
    }
    if (!s_audit.matchingDrhdFound || s_audit.matchingRegisterBase == 0u) {
        s_audit.failure = "no DRHD covers the I219 PCI segment/BDF";
        s_audit.classification = Classification::NoMatchingDrhd;
        return false;
    }

    s_audit.registersReadable = capture_unit(
        s_audit.matchingRegisterBase, &s_audit.registers);
    if (!s_audit.registersReadable) {
        s_audit.failure = "matching DRHD MMIO registers are unavailable";
        return false;
    }
    s_audit.sourceIdMatchesTarget = s_audit.registers.faultPresent &&
        s_audit.registers.faultSourceId == pci_source_id(target);
    s_audit.faultAddressClass = FaultAddressClass::Unknown;
    s_audit.failure = nullptr;
    s_audit.classification = s_audit.registers.translationEnabled
        ? Classification::TranslationActive
        : Classification::TranslationDisabled;
    return true;
}

const Audit* get_audit()
{
    return &s_audit;
}

bool capture_registers(VtdRegisterSnapshot* output)
{
    if (!output || !s_audit.matchingDrhdFound ||
        s_audit.matchingRegisterBase == 0u) return false;
    return capture_unit(s_audit.matchingRegisterBase, output);
}

} // namespace vtd
} // namespace kernel
