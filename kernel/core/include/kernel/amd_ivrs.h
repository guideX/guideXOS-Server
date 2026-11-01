#pragma once

// Bounded, allocation-free IVRS interpretation shared by the UEFI loader,
// AMD64 kernel, and hosted diagnostics tests. Layout constants follow AMD
// IOMMU Specification 48882 Rev 3.09-PUB (Oct 2023), Section 5.2.2.

#include <stdint.h>

namespace kernel {
namespace amd_ivrs {

static const uint32_t ACPI_HEADER_BYTES = 36u;
static const uint32_t IVRS_HEADER_BYTES = 48u;
static const uint32_t MAX_TABLE_BYTES = 1024u * 1024u;
static const uint32_t MAX_IOMMU_UNITS = 8u;
static const uint32_t MAX_DEVICE_RULES_PER_UNIT = 96u;
static const uint32_t MAX_IVMD_RANGES = 64u;
static const uint32_t IVHD_MMIO_MIN_BYTES = 0x4000u;
static const uint32_t IOMMU_CONTROL_OFFSET = 0x0018u;
static const uint32_t IOMMU_STATUS_OFFSET = 0x2020u;
static const uint32_t IOMMU_CONTROL_IOMMU_ENABLE = 1u << 0;
static const uint8_t PCI_IOMMU_CAPABILITY_ID = 0x0Fu;

enum class ParseStatus : uint8_t {
    NotPresent = 0,
    Valid,
    ValidWithUnsupported,
    Malformed,
    CapacityExceeded,
    AmbiguousTables
};

enum class RuleKind : uint8_t {
    Pad = 0,
    All,
    Select,
    Range,
    AliasSelect,
    AliasRange,
    ExtendedSelect,
    ExtendedRange,
    SpecialDevice,
    AcpiHid
};

enum class RouteStatus : uint8_t {
    Unmatched = 0,
    Resolved,
    Ambiguous,
    Unsupported
};

enum class TranslationState : uint8_t {
    Unknown = 0,
    EnabledObserved,
    DisabledObserved
};

enum class InspectionStatus : uint8_t {
    NotAttempted = 0,
    Readable,
    InvalidIdentity,
    InvalidResource,
    UnsupportedRevision,
    MappingUnavailable,
    ReadUnavailable
};

struct DeviceRule {
    RuleKind kind;
    uint16_t firstId;
    uint16_t lastId;
    uint16_t aliasFirstId;
    uint16_t aliasLastId;
    uint8_t dataSetting;
    uint32_t extendedData;
    uint64_t acpiHid;
    uint64_t acpiCid;
    uint8_t uidType;
    uint8_t uidLength;
    uint8_t uid[32];
};

struct IommuUnit {
    uint8_t ivhdType;
    uint8_t flags;
    uint8_t revision;
    uint16_t deviceId;
    uint16_t capabilityOffset;
    uint16_t segment;
    uint16_t info;
    uint32_t featureReporting;
    uint32_t attributes;
    uint64_t mmioBase;
    uint64_t efrImage;
    uint64_t efrImage2;
    uint32_t ruleCount;
    uint32_t unsupportedEntryCount;
    bool hasUnsupportedEntry;
    DeviceRule rules[MAX_DEVICE_RULES_PER_UNIT];
};

struct IvmdRange {
    uint8_t type;
    uint8_t flags;
    uint16_t segment;
    uint16_t deviceId;
    uint16_t auxiliary;
    uint64_t start;
    uint64_t length;
};

struct Table {
    ParseStatus status;
    uint8_t acpiRevision;
    uint32_t length;
    uint32_t ivInfo;
    uint32_t unitCount;
    uint32_t ivmdCount;
    uint32_t unsupportedStructureCount;
    bool hasUnsupportedTopology;
    IommuUnit units[MAX_IOMMU_UNITS];
    IvmdRange ivmd[MAX_IVMD_RANGES];
};

struct Route {
    RouteStatus status;
    uint16_t segment;
    uint16_t requesterId;
    uint16_t effectiveRequesterId;
    int32_t unitIndex;
};

struct RegisterSnapshot {
    InspectionStatus status;
    TranslationState translation;
    uint8_t capabilityRevision;
    uint64_t control;
    uint64_t iommuStatus;
    bool controlValid;
    bool statusValid;
};

inline uint16_t read_u16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8u);
}

inline uint32_t read_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

inline uint64_t read_u64(const uint8_t* p)
{
    return static_cast<uint64_t>(read_u32(p)) |
           (static_cast<uint64_t>(read_u32(p + 4u)) << 32u);
}

inline bool range_valid(uint64_t base, uint64_t length, uint64_t span)
{
    return length != 0u && span != 0u && base <= UINT64_MAX - length &&
           span <= length;
}

inline bool range_overflows(uint64_t base, uint64_t length)
{
    return length == 0u || base > UINT64_MAX - length;
}

inline bool checksum_valid(const uint8_t* bytes, uint32_t length)
{
    if (bytes == nullptr || length == 0u) return false;
    uint8_t sum = 0u;
    for (uint32_t i = 0u; i < length; ++i) {
        sum = static_cast<uint8_t>(sum + bytes[i]);
    }
    return sum == 0u;
}

inline void clear(Table* out)
{
    if (out == nullptr) return;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(out);
    for (uint32_t i = 0u; i < sizeof(Table); ++i) bytes[i] = 0u;
    out->status = ParseStatus::NotPresent;
}

inline bool signature_is_ivrs(const uint8_t* bytes, uint32_t length)
{
    return bytes != nullptr && length >= 4u && bytes[0] == 'I' &&
           bytes[1] == 'V' && bytes[2] == 'R' && bytes[3] == 'S';
}

inline bool append_rule(IommuUnit* unit, RuleKind kind, uint16_t first,
                        uint16_t last, uint16_t aliasFirst,
                        uint16_t aliasLast, uint8_t setting, uint32_t ext)
{
    if (unit == nullptr || unit->ruleCount >= MAX_DEVICE_RULES_PER_UNIT) {
        return false;
    }
    DeviceRule& rule = unit->rules[unit->ruleCount++];
    rule.kind = kind;
    rule.firstId = first;
    rule.lastId = last;
    rule.aliasFirstId = aliasFirst;
    rule.aliasLastId = aliasLast;
    rule.dataSetting = setting;
    rule.extendedData = ext;
    return true;
}

inline bool parse_ivhd_entries(const uint8_t* bytes, uint32_t begin,
                               uint32_t end, IommuUnit* unit,
                               bool* unsupported)
{
    if (bytes == nullptr || unit == nullptr || unsupported == nullptr ||
        begin > end) return false;
    bool rangeOpen = false;
    bool aliasRangeOpen = false;
    bool extendedRangeOpen = false;
    uint16_t rangeFirst = 0u;
    uint16_t aliasFirst = 0u;
    uint16_t extendedFirst = 0u;
    uint16_t aliasUsedFirst = 0u;
    uint8_t rangeSetting = 0u;
    uint8_t aliasSetting = 0u;
    uint8_t extendedSetting = 0u;
    uint32_t extendedValue = 0u;

    while (begin < end) {
        if (end - begin < 1u) return false;
        const uint8_t type = bytes[begin];
        uint32_t entryBytes = 0u;
        if (type == 0xF0u) {
            if (end - begin < 22u) return false;
            const uint8_t uidLength = bytes[begin + 21u];
            entryBytes = 22u + static_cast<uint32_t>(uidLength);
            if (entryBytes > end - begin) return false;
            const uint8_t uidType = bytes[begin + 20u];
            if (uidType > 2u || uidLength > 32u ||
                (uidType == 0u && uidLength != 0u)) {
                *unsupported = true;
                ++unit->unsupportedEntryCount;
                unit->hasUnsupportedEntry = true;
                begin += entryBytes;
                continue;
            }
            if (!append_rule(unit, RuleKind::AcpiHid,
                             read_u16(bytes + begin + 1u),
                             read_u16(bytes + begin + 1u), 0u, 0u,
                             bytes[begin + 3u], 0u)) return false;
            DeviceRule& hidRule = unit->rules[unit->ruleCount - 1u];
            hidRule.acpiHid = read_u64(bytes + begin + 4u);
            hidRule.acpiCid = read_u64(bytes + begin + 12u);
            hidRule.uidType = uidType;
            hidRule.uidLength = uidLength;
            for (uint32_t i = 0u; i < uidLength; ++i) {
                hidRule.uid[i] = bytes[begin + 22u + i];
            }
            begin += entryBytes;
            continue;
        }

        switch (type >> 6u) {
            case 0u: entryBytes = 4u; break;
            case 1u: entryBytes = 8u; break;
            default:
                // Keep the encoded span bounded, but never infer semantics for
                // reserved 16/32-byte entries.
                *unsupported = true;
                ++unit->unsupportedEntryCount;
                unit->hasUnsupportedEntry = true;
                entryBytes = (type >> 6u) == 2u ? 16u : 32u;
                if (entryBytes > end - begin) return false;
                begin += entryBytes;
                continue;
        }
        if (entryBytes > end - begin) return false;
        const uint16_t id = read_u16(bytes + begin + 1u);
        const uint8_t setting = bytes[begin + 3u];
        uint32_t ext = 0u;

        switch (type) {
            case 0x00u: // PAD4
                if (!append_rule(unit, RuleKind::Pad, id, id, 0u, 0u, setting, 0u)) return false;
                break;
            case 0x01u: // ALL
                if (!append_rule(unit, RuleKind::All, 0u, 0xFFFFu, 0u, 0u, setting, 0u)) return false;
                break;
            case 0x02u: // SELECT
                if (!append_rule(unit, RuleKind::Select, id, id, 0u, 0u, setting, 0u)) return false;
                break;
            case 0x03u: // START RANGE
                if (rangeOpen || aliasRangeOpen || extendedRangeOpen) return false;
                rangeOpen = true;
                rangeFirst = id;
                rangeSetting = setting;
                break;
            case 0x04u: // END RANGE
                if (rangeOpen) {
                    if (id < rangeFirst || aliasRangeOpen || extendedRangeOpen) return false;
                    if (!append_rule(unit, RuleKind::Range, rangeFirst, id,
                                     0u, 0u, rangeSetting, 0u)) return false;
                    rangeOpen = false;
                } else if (aliasRangeOpen) {
                    if (id < aliasFirst) return false;
                    const uint32_t aliasLast32 = static_cast<uint32_t>(aliasUsedFirst) +
                        static_cast<uint32_t>(id - aliasFirst);
                    if (aliasLast32 > 0xFFFFu ||
                        !append_rule(unit, RuleKind::AliasRange, aliasFirst, id,
                                     aliasUsedFirst, static_cast<uint16_t>(aliasLast32),
                                     aliasSetting, 0u)) return false;
                    aliasRangeOpen = false;
                } else if (extendedRangeOpen) {
                    if (id < extendedFirst ||
                        !append_rule(unit, RuleKind::ExtendedRange, extendedFirst, id,
                                     0u, 0u, extendedSetting, extendedValue)) return false;
                    extendedRangeOpen = false;
                } else {
                    return false;
                }
                break;
            case 0x40u: // PAD8
                if (!append_rule(unit, RuleKind::Pad, id, id, 0u, 0u, setting, 0u)) return false;
                break;
            case 0x42u: // ALIAS SELECT
                if (!append_rule(unit, RuleKind::AliasSelect, id, id,
                                 read_u16(bytes + begin + 4u),
                                 read_u16(bytes + begin + 4u), setting, 0u)) return false;
                break;
            case 0x43u: // ALIAS START RANGE
                if (rangeOpen || aliasRangeOpen || extendedRangeOpen) return false;
                aliasRangeOpen = true;
                aliasFirst = id;
                aliasUsedFirst = read_u16(bytes + begin + 4u);
                aliasSetting = setting;
                break;
            case 0x46u: // EXTENDED SELECT
                ext = read_u32(bytes + begin + 4u);
                if (!append_rule(unit, RuleKind::ExtendedSelect, id, id,
                                 0u, 0u, setting, ext)) return false;
                break;
            case 0x47u: // EXTENDED START RANGE
                if (rangeOpen || aliasRangeOpen || extendedRangeOpen) return false;
                extendedRangeOpen = true;
                extendedFirst = id;
                extendedSetting = setting;
                extendedValue = read_u32(bytes + begin + 4u);
                break;
            case 0x48u: // SPECIAL DEVICE (IOAPIC/HPET class entries)
                ext = static_cast<uint32_t>(bytes[begin + 4u]);
                if (!append_rule(unit, RuleKind::SpecialDevice, id, id,
                                 read_u16(bytes + begin + 5u),
                                 read_u16(bytes + begin + 5u), setting, ext)) return false;
                break;
            default:
                // Unknown four/eight-byte entry: its length is bounded, but
                // coverage semantics are not known and cannot safely route.
                *unsupported = true;
                ++unit->unsupportedEntryCount;
                unit->hasUnsupportedEntry = true;
                begin += entryBytes;
                continue;
        }

        begin += entryBytes;
    }
    return !rangeOpen && !aliasRangeOpen && !extendedRangeOpen;
}

inline ParseStatus parse(const uint8_t* bytes, uint32_t availableBytes,
                         Table* out)
{
    clear(out);
    if (bytes == nullptr || out == nullptr) return ParseStatus::Malformed;
    if (availableBytes < IVRS_HEADER_BYTES || !signature_is_ivrs(bytes, availableBytes)) {
        out->status = ParseStatus::Malformed;
        return out->status;
    }
    const uint32_t tableLength = read_u32(bytes + 4u);
    if (tableLength < IVRS_HEADER_BYTES || tableLength > MAX_TABLE_BYTES ||
        tableLength > availableBytes || !checksum_valid(bytes, tableLength)) {
        out->status = ParseStatus::Malformed;
        return out->status;
    }
    out->acpiRevision = bytes[8u];
    out->length = tableLength;
    out->ivInfo = read_u32(bytes + ACPI_HEADER_BYTES);
    uint32_t cursor = IVRS_HEADER_BYTES;
    bool unsupported = false;
    bool capacityExceeded = false;

    while (cursor < tableLength) {
        if (tableLength - cursor < 4u) {
            out->status = ParseStatus::Malformed;
            return out->status;
        }
        const uint8_t type = bytes[cursor];
        const uint8_t flags = bytes[cursor + 1u];
        const uint16_t blockLength = read_u16(bytes + cursor + 2u);
        if (blockLength < 4u || blockLength > tableLength - cursor) {
            out->status = ParseStatus::Malformed;
            return out->status;
        }
        if (type == 0x10u || type == 0x11u || type == 0x40u) {
            const uint32_t minimum = type == 0x10u ? 24u : 40u;
            if (blockLength < minimum) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if (out->unitCount >= MAX_IOMMU_UNITS) {
                capacityExceeded = true;
                break;
            }
            IommuUnit& unit = out->units[out->unitCount];
            uint8_t* unitBytes = reinterpret_cast<uint8_t*>(&unit);
            for (uint32_t i = 0u; i < sizeof(IommuUnit); ++i) unitBytes[i] = 0u;
            unit.ivhdType = type;
            unit.flags = flags;
            unit.revision = out->acpiRevision;
            unit.deviceId = read_u16(bytes + cursor + 4u);
            unit.capabilityOffset = read_u16(bytes + cursor + 6u);
            unit.mmioBase = read_u64(bytes + cursor + 8u);
            unit.segment = read_u16(bytes + cursor + 16u);
            unit.info = read_u16(bytes + cursor + 18u);
            if (type == 0x10u) {
                unit.featureReporting = read_u32(bytes + cursor + 20u);
            } else {
                unit.attributes = read_u32(bytes + cursor + 20u);
                unit.efrImage = read_u64(bytes + cursor + 24u);
                unit.efrImage2 = read_u64(bytes + cursor + 32u);
            }
            bool unitUnsupported = false;
            const bool entriesValid = parse_ivhd_entries(
                bytes, cursor + minimum, cursor + blockLength, &unit,
                &unitUnsupported);
            if (!entriesValid && !unitUnsupported) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if (unitUnsupported) {
                unsupported = true;
                out->hasUnsupportedTopology = true;
            }
            if (unit.unsupportedEntryCount <=
                UINT32_MAX - out->unsupportedStructureCount) {
                out->unsupportedStructureCount += unit.unsupportedEntryCount;
            } else {
                capacityExceeded = true;
            }
            out->unitCount++;
        } else if (type == 0x20u || type == 0x21u || type == 0x22u) {
            if (blockLength < 32u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if (out->ivmdCount >= MAX_IVMD_RANGES) {
                capacityExceeded = true;
                break;
            }
            IvmdRange& range = out->ivmd[out->ivmdCount++];
            range.type = type;
            range.flags = flags;
            range.deviceId = read_u16(bytes + cursor + 4u);
            range.auxiliary = read_u16(bytes + cursor + 6u);
            range.segment = read_u16(bytes + cursor + 8u);
            range.start = read_u64(bytes + cursor + 16u);
            range.length = read_u64(bytes + cursor + 24u);
            if (range.length == 0u || range_overflows(range.start, range.length)) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
        } else {
            ++out->unsupportedStructureCount;
            unsupported = true;
            out->hasUnsupportedTopology = true;
        }
        cursor += blockLength;
    }

    if (capacityExceeded) out->status = ParseStatus::CapacityExceeded;
    else if (unsupported) out->status = ParseStatus::ValidWithUnsupported;
    else out->status = ParseStatus::Valid;
    return out->status;
}

inline bool id_in_rule(const DeviceRule& rule, uint16_t id)
{
    return id >= rule.firstId && id <= rule.lastId;
}

inline Route route_device(const Table& table, uint16_t segment,
                          uint16_t requesterId)
{
    Route result{};
    result.status = RouteStatus::Unmatched;
    result.segment = segment;
    result.requesterId = requesterId;
    result.effectiveRequesterId = requesterId;
    result.unitIndex = -1;
    if (table.status == ParseStatus::Malformed ||
        table.status == ParseStatus::CapacityExceeded ||
        table.status == ParseStatus::AmbiguousTables ||
        table.status == ParseStatus::NotPresent || table.hasUnsupportedTopology) {
        result.status = RouteStatus::Unsupported;
        return result;
    }
    int32_t foundUnit = -1;
    uint16_t foundAlias = requesterId;
    bool foundAliasValue = false;
    uint32_t matches = 0u;
    for (uint32_t unitIndex = 0u; unitIndex < table.unitCount; ++unitIndex) {
        const IommuUnit& unit = table.units[unitIndex];
        if (unit.segment != segment) continue;
        bool unitMatch = false;
        uint16_t unitAlias = requesterId;
        bool unitAliasFound = false;
        for (uint32_t ruleIndex = 0u; ruleIndex < unit.ruleCount; ++ruleIndex) {
            const DeviceRule& rule = unit.rules[ruleIndex];
            switch (rule.kind) {
                case RuleKind::All:
                    unitMatch = true;
                    break;
                case RuleKind::Select:
                case RuleKind::ExtendedSelect:
                case RuleKind::Range:
                case RuleKind::ExtendedRange:
                    if (id_in_rule(rule, requesterId)) unitMatch = true;
                    break;
                case RuleKind::AliasSelect:
                case RuleKind::AliasRange:
                case RuleKind::SpecialDevice:
                    if (id_in_rule(rule, requesterId)) {
                        const uint16_t alias = static_cast<uint16_t>(
                            rule.aliasFirstId + (requesterId - rule.firstId));
                        if (unitAliasFound && unitAlias != alias) {
                            result.status = RouteStatus::Ambiguous;
                            return result;
                        }
                        unitAlias = alias;
                        unitAliasFound = true;
                        unitMatch = true;
                    }
                    break;
                default:
                    break;
            }
        }
        if (unitMatch) {
            ++matches;
            foundUnit = static_cast<int32_t>(unitIndex);
            if (unitAliasFound) {
                if (foundAliasValue && foundAlias != unitAlias) {
                    result.status = RouteStatus::Ambiguous;
                    return result;
                }
                foundAlias = unitAlias;
                foundAliasValue = true;
            }
        }
    }
    if (matches == 1u) {
        result.status = RouteStatus::Resolved;
        result.unitIndex = foundUnit;
        result.effectiveRequesterId = foundAliasValue ? foundAlias : requesterId;
    } else if (matches > 1u) {
        result.status = RouteStatus::Ambiguous;
    }
    return result;
}

inline bool valid_mmio_resource(const IommuUnit& unit, uint64_t span)
{
    return unit.mmioBase != 0u && (unit.mmioBase & 0x3FFFu) == 0u &&
           span >= IVHD_MMIO_MIN_BYTES && !range_overflows(unit.mmioBase, span);
}

inline TranslationState classify_translation(bool controlValid,
                                               uint8_t capabilityRevision,
                                               uint64_t control)
{
    if (!controlValid || capabilityRevision != 1u) return TranslationState::Unknown;
    return (control & IOMMU_CONTROL_IOMMU_ENABLE) != 0u
        ? TranslationState::EnabledObserved
        : TranslationState::DisabledObserved;
}

} // namespace amd_ivrs
} // namespace kernel
