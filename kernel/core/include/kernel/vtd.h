// Intel VT-d / DMA-remapping read-only audit helpers.
//
// Phase 21 deliberately separates bounded ACPI/register decoding from any
// IOMMU programming.  The parser and decoders are freestanding-safe and are
// also consumed by hosted deterministic tests.

#ifndef KERNEL_VTD_H
#define KERNEL_VTD_H

#include "kernel/types.h"

namespace kernel {
namespace vtd {

static const uint8_t VTD_MAX_DRHD = 16u;
static const uint8_t VTD_MAX_RMRR = 16u;
static const uint8_t VTD_INVALID_INDEX = 0xFFu;

static const uint32_t ACPI_SDT_HEADER_BYTES = 36u;
static const uint32_t DMAR_FIXED_HEADER_BYTES = 48u;
static const uint32_t ACPI_MAX_TABLE_BYTES = 1024u * 1024u;

static const uint32_t VTD_VER_REG = 0x0000u;
static const uint32_t VTD_CAP_REG = 0x0008u;
static const uint32_t VTD_ECAP_REG = 0x0010u;
static const uint32_t VTD_GCMD_REG = 0x0018u;
static const uint32_t VTD_GSTS_REG = 0x001Cu;
static const uint32_t VTD_RTADDR_REG = 0x0020u;
static const uint32_t VTD_FSTS_REG = 0x0034u;
static const uint32_t VTD_FAULT_RECORDS_REG = 0x0040u;
static const uint32_t VTD_REGISTER_MAP_BYTES = 0x10000u;

static const uint32_t VTD_GSTS_TES = (1u << 31);
static const uint32_t VTD_FSTS_PPF = (1u << 1);
static const uint32_t VTD_FSTS_FRI_SHIFT = 8u;
static const uint32_t VTD_FSTS_FRI_MASK = 0xFF00u;
static const uint32_t VTD_FAULT_RECORD_F = (1u << 31);
static const uint32_t VTD_RTADDR_TTM_SHIFT = 10u;
static const uint64_t VTD_RTADDR_ADDRESS_MASK = 0x0000FFFFFFFFF000ULL;

struct PciBdf {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

inline uint16_t pci_source_id(const PciBdf& bdf)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(bdf.bus) << 8) |
        (static_cast<uint16_t>(bdf.device & 0x1Fu) << 3) |
        static_cast<uint16_t>(bdf.function & 0x07u));
}

inline uint16_t pci_source_id(uint8_t bus, uint8_t device, uint8_t function)
{
    PciBdf bdf = { 0u, bus, device, function };
    return pci_source_id(bdf);
}

inline bool pci_bdf_equal(const PciBdf& left, const PciBdf& right)
{
    return left.segment == right.segment && left.bus == right.bus &&
           left.device == right.device && left.function == right.function;
}

struct DmarDrhd {
    uint8_t flags;
    uint16_t segment;
    uint64_t registerBase;
    bool includeAll;
    bool explicitScopeMatch;
};

struct DmarRmrr {
    uint16_t segment;
    uint64_t base;
    uint64_t limit;
    bool applicable;
};

struct DmarAudit {
    bool valid;
    bool dmarPresent;
    uint8_t revision;
    uint8_t hostAddressWidth;
    uint8_t flags;
    uint8_t drhdCount;
    uint8_t rmrrCount;
    uint8_t matchingDrhdIndex;
    uint8_t matchingRmrrCount;
    bool matchingDrhdFound;
    bool matchingDrhdIncludeAll;
    bool matchingDrhdExplicitScope;
    bool rmrrApplicable;
    uint64_t matchingRegisterBase;
    DmarDrhd drhds[VTD_MAX_DRHD];
    DmarRmrr rmrrs[VTD_MAX_RMRR];
    const char* failure;
};

enum class FaultAddressClass : uint8_t {
    Unknown = 0,
    Other,
    Ring,
    Buffer,
    RingAndBuffer,
};

inline const char* fault_address_class_name(FaultAddressClass value)
{
    switch (value) {
        case FaultAddressClass::Ring:        return "ring";
        case FaultAddressClass::Buffer:      return "buffer";
        case FaultAddressClass::RingAndBuffer:return "ring+buffer";
        case FaultAddressClass::Other:       return "other";
        default:                             return "unknown";
    }
}

inline bool range_contains(uint64_t base, uint64_t length, uint64_t address)
{
    return length != 0u && base <= address &&
           address - base < length;
}

inline FaultAddressClass classify_fault_address(uint64_t address,
                                                uint64_t ringBase,
                                                uint64_t ringLength,
                                                uint64_t bufferBase,
                                                uint64_t bufferLength)
{
    if (address == 0u) return FaultAddressClass::Unknown;
    const bool ring = range_contains(ringBase, ringLength, address);
    const bool buffer = range_contains(bufferBase, bufferLength, address);
    if (ring && buffer) return FaultAddressClass::RingAndBuffer;
    if (ring) return FaultAddressClass::Ring;
    if (buffer) return FaultAddressClass::Buffer;
    return FaultAddressClass::Other;
}

inline bool translation_enabled(uint32_t gsts)
{
    return (gsts & VTD_GSTS_TES) != 0u;
}

inline bool primary_fault_pending(uint32_t fsts)
{
    return (fsts & VTD_FSTS_PPF) != 0u;
}

inline uint8_t fault_record_index(uint32_t fsts)
{
    return static_cast<uint8_t>((fsts & VTD_FSTS_FRI_MASK) >>
                                VTD_FSTS_FRI_SHIFT);
}

inline uint8_t root_table_mode(uint64_t rtaddr)
{
    return static_cast<uint8_t>((rtaddr >> VTD_RTADDR_TTM_SHIFT) & 0x3u);
}

inline const char* root_table_mode_name(uint8_t mode)
{
    switch (mode) {
        case 0u: return "legacy";
        case 1u: return "scalable";
        case 3u: return "abort-dma";
        default: return "reserved";
    }
}

inline uint8_t cap_fault_record_count(uint64_t cap)
{
    return static_cast<uint8_t>(((cap >> 40) & 0xFFu) + 1u);
}

inline uint32_t cap_fault_record_offset(uint64_t cap)
{
    return static_cast<uint32_t>(((cap >> 24) & 0x3FFu) * 16u);
}

inline uint16_t fault_source_id(uint64_t faultLow)
{
    return static_cast<uint16_t>(faultLow & 0xFFFFu);
}

inline uint8_t fault_reason(uint64_t faultLow)
{
    return static_cast<uint8_t>(faultLow & 0xFFu);
}

inline bool fault_record_present(uint64_t faultLow)
{
    return (static_cast<uint32_t>(faultLow) & VTD_FAULT_RECORD_F) != 0u;
}

inline uint64_t fault_page_address(uint64_t faultHigh)
{
    return faultHigh & ~0xFFFULL;
}

struct VtdRegisterSnapshot {
    bool valid;
    bool faultRecordSupported;
    uint64_t registerBase;
    uint32_t version;
    uint64_t cap;
    uint64_t ecap;
    uint32_t gcmd;
    uint32_t gsts;
    uint64_t rootTableAddress;
    uint32_t faultStatus;
    uint8_t faultRecordCount;
    uint8_t selectedFaultRecord;
    bool translationEnabled;
    bool primaryFaultPending;
    bool faultPresent;
    uint16_t faultSourceId;
    uint8_t faultReason;
    uint64_t faultAddress;
};

struct VtdRegisterValues {
    uint32_t version;
    uint64_t cap;
    uint64_t ecap;
    uint32_t gcmd;
    uint32_t gsts;
    uint64_t rootTableAddress;
    uint32_t faultStatus;
    bool faultRecordReadable;
    uint64_t faultLow;
    uint64_t faultHigh;
};

inline bool decode_register_values(uint64_t registerBase,
                                   const VtdRegisterValues& values,
                                   VtdRegisterSnapshot* output)
{
    if (!output || registerBase == 0u) return false;
    VtdRegisterSnapshot decoded = {};
    decoded.valid = true;
    decoded.faultRecordSupported = values.faultRecordReadable;
    decoded.registerBase = registerBase;
    decoded.version = values.version;
    decoded.cap = values.cap;
    decoded.ecap = values.ecap;
    decoded.gcmd = values.gcmd;
    decoded.gsts = values.gsts;
    decoded.rootTableAddress = values.rootTableAddress;
    decoded.faultStatus = values.faultStatus;
    decoded.faultRecordCount = cap_fault_record_count(values.cap);
    decoded.selectedFaultRecord = primary_fault_pending(values.faultStatus)
        ? fault_record_index(values.faultStatus) : VTD_INVALID_INDEX;
    decoded.translationEnabled = translation_enabled(values.gsts);
    decoded.primaryFaultPending = primary_fault_pending(values.faultStatus);
    decoded.faultPresent = values.faultRecordReadable &&
                           fault_record_present(values.faultLow);
    decoded.faultSourceId = fault_source_id(values.faultLow);
    decoded.faultReason = fault_reason(values.faultLow);
    decoded.faultAddress = fault_page_address(values.faultHigh);
    *output = decoded;
    return true;
}

enum class Classification : uint8_t {
    Unavailable = 0,
    NoDmar,
    NoMatchingDrhd,
    TranslationDisabled,
    TranslationActive,
    DmaFault,
    SourceMatch,
    RingAddressFault,
    BufferAddressFault,
};

inline const char* classification_name(Classification value)
{
    switch (value) {
        case Classification::NoDmar:             return "TX_IOMMU_DIAGNOSTIC_UNAVAILABLE";
        case Classification::NoMatchingDrhd:     return "TX_IOMMU_DIAGNOSTIC_UNAVAILABLE";
        case Classification::TranslationDisabled: return "TX_IOMMU_TRANSLATION_DISABLED";
        case Classification::TranslationActive:  return "TX_IOMMU_TRANSLATION_ACTIVE";
        case Classification::DmaFault:            return "TX_IOMMU_DMA_FAULT";
        case Classification::SourceMatch:        return "TX_IOMMU_SOURCE_MATCH";
        case Classification::RingAddressFault:  return "TX_IOMMU_RING_ADDRESS_FAULT";
        case Classification::BufferAddressFault:return "TX_IOMMU_BUFFER_ADDRESS_FAULT";
        default:                                 return "TX_IOMMU_DIAGNOSTIC_UNAVAILABLE";
    }
}

struct Audit {
    bool acpiAvailable;
    bool dmarTablePresent;
    uint8_t dmarRevision;
    uint8_t drhdCount;
    uint8_t rmrrCount;
    PciBdf target;
    DmarAudit dmar;
    bool matchingDrhdFound;
    bool matchingDrhdIncludeAll;
    bool matchingDrhdExplicitScope;
    uint64_t matchingRegisterBase;
    bool registersReadable;
    VtdRegisterSnapshot registers;
    bool sourceIdMatchesTarget;
    FaultAddressClass faultAddressClass;
    bool rmrrApplicable;
    Classification classification;
    const char* failure;
};

inline uint16_t read_le16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
}

inline uint32_t read_le32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

inline uint64_t read_le64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(read_le32(bytes)) |
           (static_cast<uint64_t>(read_le32(bytes + 4)) << 32);
}

inline bool signature_equals(const uint8_t* bytes, const char* signature)
{
    if (!bytes || !signature) return false;
    for (uint8_t i = 0; i < 4u; ++i) {
        if (bytes[i] != static_cast<uint8_t>(signature[i])) return false;
    }
    return true;
}

inline bool checksum_valid(const uint8_t* bytes, uint32_t length)
{
    if (!bytes || length == 0u) return false;
    uint8_t sum = 0u;
    for (uint32_t i = 0; i < length; ++i) {
        sum = static_cast<uint8_t>(sum + bytes[i]);
    }
    return sum == 0u;
}

inline bool scope_matches_bdf(const uint8_t* scope, uint16_t scopeLength,
                              const PciBdf& target)
{
    if (!scope || scopeLength < 8u) return false;
    if (static_cast<uint16_t>(scopeLength - 6u) % 2u != 0u) return false;
    const uint8_t startBus = scope[5];
    const uint8_t pathCount = static_cast<uint8_t>((scopeLength - 6u) / 2u);
    if (pathCount != 1u) return false;
    const uint8_t device = static_cast<uint8_t>(scope[6] >> 3);
    const uint8_t function = static_cast<uint8_t>(scope[6] & 0x07u);
    return startBus == target.bus && device == target.device &&
           function == target.function;
}

inline bool scopes_match_bdf(const uint8_t* bytes, uint32_t begin,
                             uint32_t end, const PciBdf& target)
{
    uint32_t cursor = begin;
    bool matched = false;
    while (cursor < end) {
        if (end - cursor < 6u) return false;
        const uint16_t length = bytes[cursor + 1u];
        if (length < 6u || cursor + length > end) return false;
        matched = matched || scope_matches_bdf(bytes + cursor, length, target);
        cursor += length;
    }
    return cursor == end && matched;
}

inline bool parse_dmar_table(const uint8_t* table, uint32_t availableLength,
                             const PciBdf& target, DmarAudit* output)
{
    if (!table || !output || availableLength < DMAR_FIXED_HEADER_BYTES ||
        !signature_equals(table, "DMAR")) {
        return false;
    }
    DmarAudit parsed = {};
    parsed.matchingDrhdIndex = VTD_INVALID_INDEX;
    parsed.dmarPresent = true;
    parsed.revision = table[8];
    parsed.hostAddressWidth = table[36];
    parsed.flags = table[37];
    const uint32_t tableLength = read_le32(table + 4u);
    if (tableLength < DMAR_FIXED_HEADER_BYTES ||
        tableLength > availableLength || tableLength > ACPI_MAX_TABLE_BYTES ||
        !checksum_valid(table, tableLength)) {
        parsed.failure = "DMAR length or checksum is invalid";
        *output = parsed;
        return false;
    }

    uint32_t cursor = DMAR_FIXED_HEADER_BYTES;
    while (cursor < tableLength) {
        if (tableLength - cursor < 4u) {
            parsed.failure = "DMAR structure header is truncated";
            *output = parsed;
            return false;
        }
        const uint16_t type = read_le16(table + cursor);
        const uint16_t length = read_le16(table + cursor + 2u);
        if (length < 4u || cursor + length > tableLength) {
            parsed.failure = "DMAR structure length is malformed";
            *output = parsed;
            return false;
        }

        if (type == 0u) {
            if (length < 16u || parsed.drhdCount >= VTD_MAX_DRHD) {
                parsed.failure = "DRHD structure is unsupported or over capacity";
                *output = parsed;
                return false;
            }
            DmarDrhd& drhd = parsed.drhds[parsed.drhdCount++];
            drhd.flags = table[cursor + 4u];
            drhd.segment = read_le16(table + cursor + 6u);
            drhd.registerBase = read_le64(table + cursor + 8u);
            drhd.includeAll = (drhd.flags & 0x01u) != 0u;
            drhd.explicitScopeMatch = drhd.segment == target.segment &&
                scopes_match_bdf(table, cursor + 16u, cursor + length, target);
            const bool matches = drhd.segment == target.segment &&
                                 (drhd.includeAll || drhd.explicitScopeMatch);
            if (matches && !parsed.matchingDrhdFound) {
                parsed.matchingDrhdFound = true;
                parsed.matchingDrhdIndex = static_cast<uint8_t>(parsed.drhdCount - 1u);
                parsed.matchingDrhdIncludeAll = drhd.includeAll;
                parsed.matchingDrhdExplicitScope = drhd.explicitScopeMatch;
                parsed.matchingRegisterBase = drhd.registerBase;
            }
        } else if (type == 1u) {
            if (length < 24u || parsed.rmrrCount >= VTD_MAX_RMRR) {
                parsed.failure = "RMRR structure is unsupported or over capacity";
                *output = parsed;
                return false;
            }
            DmarRmrr& rmrr = parsed.rmrrs[parsed.rmrrCount++];
            rmrr.segment = read_le16(table + cursor + 6u);
            rmrr.base = read_le64(table + cursor + 8u);
            rmrr.limit = read_le64(table + cursor + 16u);
            rmrr.applicable = rmrr.segment == target.segment &&
                scopes_match_bdf(table, cursor + 24u, cursor + length, target);
            if (rmrr.applicable) {
                parsed.rmrrApplicable = true;
                ++parsed.matchingRmrrCount;
            }
        }
        cursor += length;
    }

    parsed.valid = true;
    parsed.failure = nullptr;
    *output = parsed;
    return true;
}

// Configure the physical ACPI RSDP address retained by the UEFI handoff.
// The value is only consumed by the read-only implementation in vtd.cpp.
void set_acpi_rsdp(uint64_t physicalAddress);

// Perform a bounded ACPI DMAR parse and capture the matching DRHD registers.
// This function performs reads only; it never writes GCMD, RTADDR, FSTS, or
// any table/context memory.
bool discover(const PciBdf& target);

const Audit* get_audit();

// Perform one caller-controlled, read-only register capture after discovery.
bool capture_registers(VtdRegisterSnapshot* output);

} // namespace vtd
} // namespace kernel

#endif
