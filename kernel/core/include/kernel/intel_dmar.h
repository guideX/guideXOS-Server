#pragma once

// Bounded, allocation-free Intel DMAR parsing and PCI scope routing.
// Structure layouts follow Intel VT-d Architecture Specification Rev. 4.0,
// Sections 8.1-8.6. Unsupported topology is always fail-closed for routing.

#include <stdint.h>

namespace kernel {
namespace intel_dmar {

static const uint32_t ACPI_HEADER_BYTES = 36u;
static const uint32_t DMAR_HEADER_BYTES = 48u;
static const uint32_t MAX_TABLE_BYTES = 1024u * 1024u;
static const uint32_t MAX_DRHD_UNITS = 16u;
static const uint32_t MAX_RMRR_REGIONS = 32u;
static const uint32_t MAX_ATSR_STRUCTURES = 16u;
static const uint32_t MAX_RHSA_STRUCTURES = 16u;
static const uint32_t MAX_SCOPES_PER_STRUCTURE = 32u;
static const uint32_t MAX_SCOPE_PATH = 16u;

static const uint16_t STRUCTURE_DRHD = 0u;
static const uint16_t STRUCTURE_RMRR = 1u;
static const uint16_t STRUCTURE_ATSR = 2u;
static const uint16_t STRUCTURE_RHSA = 3u;
static const uint16_t STRUCTURE_ANDD = 4u;
static const uint16_t STRUCTURE_SATC = 5u;
static const uint16_t STRUCTURE_SIDP = 6u;

static const uint8_t SCOPE_ENDPOINT = 1u;
static const uint8_t SCOPE_PCI_BRIDGE = 2u;
static const uint8_t SCOPE_IOAPIC = 3u;
static const uint8_t SCOPE_HPET = 4u;
static const uint8_t SCOPE_ACPI_NAMESPACE = 5u;

enum class ParseStatus : uint8_t {
    NotPresent = 0,
    Valid,
    ValidWithUnsupported,
    Malformed,
    CapacityExceeded,
    UnsupportedRevision,
    AmbiguousTables
};

enum class RouteStatus : uint8_t {
    Unknown = 0,
    Resolved,
    Ambiguous,
    NotCovered,
    Unsupported
};

enum class TranslationState : uint8_t {
    Unknown = 0,
    EnabledObserved,
    DisabledObserved
};

enum class RegisterStatus : uint8_t {
    NotAttempted = 0,
    Readable,
    InvalidResource,
    MappingUnavailable,
    UnsupportedVersion,
    UnsupportedCapabilities,
    ReadUnavailable
};

enum class RootTableState : uint8_t {
    Unknown = 0,
    NotConfigured,
    Configured,
    Invalid
};

struct ScopePathElement {
    uint8_t device;
    uint8_t function;
};

struct DeviceScope {
    uint8_t type;
    uint8_t enumerationId;
    uint8_t startBus;
    uint8_t pathCount;
    ScopePathElement path[MAX_SCOPE_PATH];
};

struct DrhdUnit {
    uint8_t flags;
    uint8_t registerSizeEncoding;
    uint16_t segment;
    uint64_t registerBase;
    uint64_t registerBytes;
    uint32_t scopeCount;
    DeviceScope scopes[MAX_SCOPES_PER_STRUCTURE];
};

struct RmrrRegion {
    uint16_t segment;
    uint64_t base;
    uint64_t limit;
    uint64_t length;
    uint32_t scopeCount;
    DeviceScope scopes[MAX_SCOPES_PER_STRUCTURE];
};

struct AtsrStructure {
    uint8_t flags;
    uint16_t segment;
    uint32_t scopeCount;
    DeviceScope scopes[MAX_SCOPES_PER_STRUCTURE];
};

struct RhsaStructure {
    uint64_t registerBase;
    uint32_t proximityDomain;
};

struct Table {
    ParseStatus status;
    uint8_t revision;
    uint8_t hostAddressWidth;
    uint8_t flags;
    uint32_t length;
    uint32_t drhdCount;
    uint32_t rmrrCount;
    uint32_t atsrCount;
    uint32_t rhsaCount;
    uint32_t unsupportedStructureCount;
    uint32_t unsupportedScopeCount;
    bool hasUnsupportedTopology;
    DrhdUnit drhd[MAX_DRHD_UNITS];
    RmrrRegion rmrr[MAX_RMRR_REGIONS];
    AtsrStructure atsr[MAX_ATSR_STRUCTURES];
    RhsaStructure rhsa[MAX_RHSA_STRUCTURES];
};

struct PciPathElement {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

// Path entries run from a PCI segment's root bus to the endpoint and include
// every bridge on the way. Each entry records its real bus number so scopes
// with different Start Bus Number values can be compared safely.
struct PciPath {
    uint16_t segment;
    uint8_t count;
    bool valid;
    PciPathElement entries[MAX_SCOPE_PATH];
};

struct Route {
    RouteStatus status;
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    int32_t unitIndex;
    uint64_t registerBase;
    bool rmrrRequired;
};

struct RegisterSnapshot {
    RegisterStatus status;
    TranslationState translation;
    RootTableState rootTable;
    uint8_t version;
    uint8_t maximumGuestAddressWidth;
    uint8_t supportedAdjustedGuestAddressWidths;
    uint32_t globalCommand;
    uint32_t globalStatus;
    uint32_t faultStatus;
    uint64_t capability;
    uint64_t extendedCapability;
    uint64_t rootTableAddress;
    bool capabilityValid;
    bool extendedCapabilityValid;
    bool commandStatusValid;
    bool rootTableValid;
    bool faultStatusValid;
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

inline bool checksum_valid(const uint8_t* bytes, uint32_t length)
{
    if (bytes == nullptr || length == 0u) return false;
    uint8_t sum = 0u;
    for (uint32_t i = 0u; i < length; ++i) {
        sum = static_cast<uint8_t>(sum + bytes[i]);
    }
    return sum == 0u;
}

inline bool range_overflows(uint64_t base, uint64_t length)
{
    return length == 0u || base > UINT64_MAX - length;
}

inline void clear(Table* out)
{
    if (out == nullptr) return;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(out);
    for (uint32_t i = 0u; i < sizeof(Table); ++i) bytes[i] = 0u;
    out->status = ParseStatus::NotPresent;
}

inline bool signature_is_dmar(const uint8_t* bytes, uint32_t length)
{
    return bytes != nullptr && length >= 4u && bytes[0] == 'D' &&
           bytes[1] == 'M' && bytes[2] == 'A' && bytes[3] == 'R';
}

inline bool parse_scopes(const uint8_t* bytes, uint32_t begin, uint32_t end,
                         DeviceScope* scopes, uint32_t capacity,
                         uint32_t* count, bool* unsupported,
                         uint32_t* unsupportedCount)
{
    if (bytes == nullptr || scopes == nullptr || count == nullptr ||
        unsupported == nullptr || unsupportedCount == nullptr || begin > end) {
        return false;
    }
    *count = 0u;
    while (begin < end) {
        if (end - begin < 6u) return false;
        const uint8_t scopeLength = bytes[begin + 1u];
        if (scopeLength < 6u || scopeLength > end - begin ||
            ((scopeLength - 6u) & 1u) != 0u || bytes[begin + 2u] != 0u) {
            return false;
        }
        const uint32_t pathCount = (scopeLength - 6u) / 2u;
        const uint8_t type = bytes[begin];
        if ((type == SCOPE_ENDPOINT || type == SCOPE_PCI_BRIDGE ||
             type == SCOPE_IOAPIC || type == SCOPE_HPET) && pathCount == 0u) {
            return false;
        }
        if (pathCount > MAX_SCOPE_PATH) return false;
        if (*count >= capacity) return false;

        DeviceScope& scope = scopes[(*count)++];
        scope.type = type;
        scope.enumerationId = bytes[begin + 3u];
        scope.startBus = bytes[begin + 4u];
        scope.pathCount = static_cast<uint8_t>(pathCount);
        for (uint32_t i = 0u; i < pathCount; ++i) {
            const uint8_t device = bytes[begin + 6u + i * 2u];
            const uint8_t function = bytes[begin + 7u + i * 2u];
            if (device > 31u || function > 7u) return false;
            scope.path[i].device = device;
            scope.path[i].function = function;
        }
        if (type < SCOPE_ENDPOINT || type > SCOPE_ACPI_NAMESPACE) {
            *unsupported = true;
            if (*unsupportedCount != UINT32_MAX) ++(*unsupportedCount);
        }
        begin += scopeLength;
    }
    return begin == end;
}

inline bool duplicate_drhd(const Table& table, uint16_t segment,
                           uint64_t registerBase)
{
    for (uint32_t i = 0u; i < table.drhdCount; ++i) {
        if (table.drhd[i].segment == segment &&
            table.drhd[i].registerBase == registerBase) return true;
    }
    return false;
}

inline bool duplicate_rhsa(const Table& table, uint64_t registerBase)
{
    for (uint32_t i = 0u; i < table.rhsaCount; ++i) {
        if (table.rhsa[i].registerBase == registerBase) return true;
    }
    return false;
}

inline ParseStatus parse(const uint8_t* bytes, uint32_t availableBytes,
                         Table* out)
{
    clear(out);
    if (bytes == nullptr || out == nullptr || availableBytes < DMAR_HEADER_BYTES ||
        !signature_is_dmar(bytes, availableBytes)) {
        if (out != nullptr) out->status = ParseStatus::Malformed;
        return ParseStatus::Malformed;
    }
    const uint32_t tableLength = read_u32(bytes + 4u);
    if (tableLength < DMAR_HEADER_BYTES || tableLength > MAX_TABLE_BYTES ||
        tableLength > availableBytes || !checksum_valid(bytes, tableLength)) {
        out->status = ParseStatus::Malformed;
        return out->status;
    }
    out->revision = bytes[8u];
    out->length = tableLength;
    out->hostAddressWidth = bytes[36u];
    out->flags = bytes[37u];
    if (out->revision != 1u) {
        out->status = ParseStatus::UnsupportedRevision;
        return out->status;
    }
    if (out->hostAddressWidth > 63u || (out->flags & ~0x07u) != 0u) {
        out->status = ParseStatus::Malformed;
        return out->status;
    }
    for (uint32_t i = 38u; i < DMAR_HEADER_BYTES; ++i) {
        if (bytes[i] != 0u) {
            out->status = ParseStatus::Malformed;
            return out->status;
        }
    }

    bool unsupported = false;
    bool capacityExceeded = false;
    uint16_t previousStructureType = STRUCTURE_DRHD;
    bool havePreviousStructure = false;
    uint32_t cursor = DMAR_HEADER_BYTES;
    while (cursor < tableLength) {
        if (tableLength - cursor < 4u) {
            out->status = ParseStatus::Malformed;
            return out->status;
        }
        const uint16_t type = read_u16(bytes + cursor);
        const uint16_t structureLength = read_u16(bytes + cursor + 2u);
        if (structureLength < 4u || structureLength > tableLength - cursor) {
            out->status = ParseStatus::Malformed;
            return out->status;
        }
        if (havePreviousStructure && type < previousStructureType) {
            out->status = ParseStatus::Malformed;
            return out->status;
        }
        previousStructureType = type;
        havePreviousStructure = true;
        const uint8_t* structure = bytes + cursor;
        if (type == STRUCTURE_DRHD) {
            if (structureLength < 16u || structure[5u] > 15u ||
                (structure[5u] & 0xF0u) != 0u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if ((structure[4u] & 0xFEu) != 0u) {
                unsupported = true;
                out->hasUnsupportedTopology = true;
            }
            const uint16_t segment = read_u16(structure + 6u);
            const uint64_t registerBase = read_u64(structure + 8u);
            const uint64_t registerBytes = 1ULL << (12u + (structure[5u] & 0x0Fu));
            if (registerBase == 0u || (registerBase & (registerBytes - 1u)) != 0u ||
                range_overflows(registerBase, registerBytes) ||
                duplicate_drhd(*out, segment, registerBase)) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if (out->drhdCount >= MAX_DRHD_UNITS) {
                capacityExceeded = true;
                break;
            }
            DrhdUnit& unit = out->drhd[out->drhdCount];
            unit.flags = structure[4u];
            unit.registerSizeEncoding = structure[5u] & 0x0Fu;
            unit.segment = segment;
            unit.registerBase = registerBase;
            unit.registerBytes = registerBytes;
            if (!parse_scopes(structure, 16u, structureLength,
                              unit.scopes, MAX_SCOPES_PER_STRUCTURE,
                              &unit.scopeCount, &unsupported,
                              &out->unsupportedScopeCount)) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            const bool includeAll = (unit.flags & 1u) != 0u;
            if (includeAll) {
                for (uint32_t i = 0u; i < unit.scopeCount; ++i) {
                    if (unit.scopes[i].type == SCOPE_ENDPOINT ||
                        unit.scopes[i].type == SCOPE_PCI_BRIDGE) {
                        out->status = ParseStatus::Malformed;
                        return out->status;
                    }
                }
            }
            ++out->drhdCount;
        } else if (type == STRUCTURE_RMRR) {
            if (structureLength < 24u || structure[4u] != 0u ||
                structure[5u] != 0u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if (out->rmrrCount >= MAX_RMRR_REGIONS) {
                capacityExceeded = true;
                break;
            }
            RmrrRegion& region = out->rmrr[out->rmrrCount];
            region.segment = read_u16(structure + 6u);
            region.base = read_u64(structure + 8u);
            region.limit = read_u64(structure + 16u);
            if (region.limit < region.base ||
                (region.base & 0xFFFu) != 0u ||
                region.limit == UINT64_MAX ||
                ((region.limit + 1u) & 0xFFFu) != 0u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            region.length = region.limit - region.base + 1u;
            if (region.length == 0u || out->rmrrCount != 0u) {
                // Duplicate RMRR entries are ambiguous ownership declarations.
                for (uint32_t i = 0u; i < out->rmrrCount; ++i) {
                    const RmrrRegion& prior = out->rmrr[i];
                    if (prior.segment == region.segment && prior.base == region.base &&
                        prior.limit == region.limit) {
                        out->status = ParseStatus::Malformed;
                        return out->status;
                    }
                }
            }
            if (!parse_scopes(structure, 24u, structureLength,
                              region.scopes, MAX_SCOPES_PER_STRUCTURE,
                              &region.scopeCount, &unsupported,
                              &out->unsupportedScopeCount) || region.scopeCount == 0u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            ++out->rmrrCount;
        } else if (type == STRUCTURE_ATSR) {
            if (structureLength < 8u || (structure[4u] & 0xFEu) != 0u ||
                structure[5u] != 0u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if (out->atsrCount >= MAX_ATSR_STRUCTURES) {
                capacityExceeded = true;
                break;
            }
            AtsrStructure& atsr = out->atsr[out->atsrCount];
            atsr.flags = structure[4u];
            atsr.segment = read_u16(structure + 6u);
            if (!parse_scopes(structure, 8u, structureLength,
                              atsr.scopes, MAX_SCOPES_PER_STRUCTURE,
                              &atsr.scopeCount, &unsupported,
                              &out->unsupportedScopeCount)) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if ((atsr.flags & 1u) != 0u) {
                if (atsr.scopeCount != 0u) {
                    out->status = ParseStatus::Malformed;
                    return out->status;
                }
            } else {
                for (uint32_t i = 0u; i < atsr.scopeCount; ++i) {
                    if (atsr.scopes[i].type != SCOPE_PCI_BRIDGE) {
                        out->status = ParseStatus::Malformed;
                        return out->status;
                    }
                }
            }
            ++out->atsrCount;
        } else if (type == STRUCTURE_RHSA) {
            if (structureLength != 20u || read_u32(structure + 4u) != 0u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            if (out->rhsaCount >= MAX_RHSA_STRUCTURES) {
                capacityExceeded = true;
                break;
            }
            RhsaStructure& rhsa = out->rhsa[out->rhsaCount];
            rhsa.registerBase = read_u64(structure + 8u);
            rhsa.proximityDomain = read_u32(structure + 16u);
            if (rhsa.registerBase == 0u || (rhsa.registerBase & 0xFFFu) != 0u ||
                duplicate_rhsa(*out, rhsa.registerBase)) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            ++out->rhsaCount;
        } else if (type == STRUCTURE_ANDD) {
            if (structureLength < 9u || structure[4u] != 0u ||
                structure[5u] != 0u || structure[6u] != 0u ||
                structure[structureLength - 1u] != 0u) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
            unsupported = true;
            out->hasUnsupportedTopology = true;
            ++out->unsupportedStructureCount;
        } else {
            // Known SATC/SIDP and future structures are length-skipped, but
            // their effects are not implemented, so they cannot authorize DMA.
            unsupported = true;
            out->hasUnsupportedTopology = true;
            if (out->unsupportedStructureCount != UINT32_MAX) {
                ++out->unsupportedStructureCount;
            }
        }
        cursor += structureLength;
    }

    if (capacityExceeded) {
        out->status = ParseStatus::CapacityExceeded;
        out->hasUnsupportedTopology = true;
        return out->status;
    }
    if (cursor != tableLength || out->drhdCount == 0u) {
        out->status = ParseStatus::Malformed;
        return out->status;
    }
    for (uint32_t i = 0u; i < out->rhsaCount; ++i) {
        bool hasDrhd = false;
        for (uint32_t j = 0u; j < out->drhdCount; ++j) {
            if (out->rhsa[i].registerBase == out->drhd[j].registerBase) {
                hasDrhd = true;
                break;
            }
        }
        if (!hasDrhd) {
            out->status = ParseStatus::Malformed;
            return out->status;
        }
    }
    for (uint32_t i = 0u; i < out->drhdCount; ++i) {
        if ((out->drhd[i].flags & 1u) == 0u) continue;
        for (uint32_t j = i + 1u; j < out->drhdCount; ++j) {
            if (out->drhd[j].segment == out->drhd[i].segment) {
                out->status = ParseStatus::Malformed;
                return out->status;
            }
        }
    }
    if (unsupported || out->hasUnsupportedTopology) {
        out->status = ParseStatus::ValidWithUnsupported;
    } else {
        out->status = ParseStatus::Valid;
    }
    return out->status;
}

inline bool scope_matches_path(const DeviceScope& scope, const PciPath& path)
{
    if (!path.valid || path.count == 0u || path.count > MAX_SCOPE_PATH ||
        (scope.type != SCOPE_ENDPOINT && scope.type != SCOPE_PCI_BRIDGE) ||
        scope.pathCount == 0u) return false;
    for (uint32_t start = 0u; start < path.count; ++start) {
        if (path.entries[start].bus != scope.startBus ||
            scope.pathCount > path.count - start) continue;
        bool match = true;
        for (uint32_t i = 0u; i < scope.pathCount; ++i) {
            const PciPathElement& element = path.entries[start + i];
            if (element.device != scope.path[i].device ||
                element.function != scope.path[i].function) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        if (scope.type == SCOPE_ENDPOINT && start + scope.pathCount != path.count) {
            continue;
        }
        return true;
    }
    return false;
}

inline bool path_valid(const PciPath& path)
{
    if (!path.valid || path.count == 0u || path.count > MAX_SCOPE_PATH) return false;
    for (uint32_t i = 0u; i < path.count; ++i) {
        if (path.entries[i].device > 31u || path.entries[i].function > 7u) return false;
    }
    return true;
}

inline Route route_device(const Table& table, const PciPath& path)
{
    Route result{};
    result.status = RouteStatus::Unknown;
    result.segment = path.segment;
    result.unitIndex = -1;
    if (path_valid(path)) {
        const PciPathElement& endpoint = path.entries[path.count - 1u];
        result.bus = endpoint.bus;
        result.device = endpoint.device;
        result.function = endpoint.function;
    }
    if (table.status == ParseStatus::Malformed ||
        table.status == ParseStatus::CapacityExceeded ||
        table.status == ParseStatus::UnsupportedRevision ||
        table.status == ParseStatus::AmbiguousTables ||
        table.status == ParseStatus::NotPresent) {
        result.status = RouteStatus::Unsupported;
        return result;
    }
    if (table.hasUnsupportedTopology || table.status == ParseStatus::ValidWithUnsupported ||
        !path_valid(path)) {
        result.status = RouteStatus::Unsupported;
        return result;
    }

    int32_t explicitUnit = -1;
    uint32_t explicitMatches = 0u;
    int32_t includeAllUnit = -1;
    uint32_t includeAllMatches = 0u;
    for (uint32_t i = 0u; i < table.drhdCount; ++i) {
        const DrhdUnit& unit = table.drhd[i];
        if (unit.segment != path.segment) continue;
        bool unitMatches = false;
        for (uint32_t j = 0u; j < unit.scopeCount; ++j) {
            if (scope_matches_path(unit.scopes[j], path)) {
                unitMatches = true;
                break;
            }
        }
        if (unitMatches) {
            explicitUnit = static_cast<int32_t>(i);
            ++explicitMatches;
        } else if ((unit.flags & 1u) != 0u) {
            includeAllUnit = static_cast<int32_t>(i);
            ++includeAllMatches;
        }
    }

    int32_t selectedUnit = -1;
    if (explicitMatches == 1u) {
        selectedUnit = explicitUnit;
    } else if (explicitMatches > 1u || includeAllMatches > 1u) {
        result.status = RouteStatus::Ambiguous;
        return result;
    } else if (explicitMatches == 0u && includeAllMatches == 1u) {
        selectedUnit = includeAllUnit;
    } else {
        result.status = RouteStatus::NotCovered;
        return result;
    }

    result.status = RouteStatus::Resolved;
    result.unitIndex = selectedUnit;
    result.registerBase = table.drhd[static_cast<uint32_t>(selectedUnit)].registerBase;
    for (uint32_t i = 0u; i < table.rmrrCount; ++i) {
        const RmrrRegion& region = table.rmrr[i];
        if (region.segment != path.segment) continue;
        for (uint32_t j = 0u; j < region.scopeCount; ++j) {
            if (scope_matches_path(region.scopes[j], path)) {
                result.rmrrRequired = true;
                break;
            }
        }
    }
    return result;
}

inline bool valid_register_resource(uint64_t base, uint64_t registerBytes,
                                    uint64_t requiredBytes)
{
    return base != 0u && registerBytes >= 0x1000u &&
           (registerBytes & (registerBytes - 1u)) == 0u &&
           (base & (registerBytes - 1u)) == 0u &&
           requiredBytes >= 0x38u && requiredBytes <= registerBytes &&
           !range_overflows(base, registerBytes);
}

inline RootTableState classify_root_table(bool valid, uint64_t rootTableAddress)
{
    if (!valid) return RootTableState::Unknown;
    // RTADDR[11:10] is TTM; only modes 0 and 1 are defined. RTADDR[9:0]
    // is reserved and must be zero. The page-aligned root base is [63:12].
    const uint32_t tableMode = static_cast<uint32_t>((rootTableAddress >> 10u) & 0x3u);
    if ((rootTableAddress & 0x3FFu) != 0u || tableMode > 1u) {
        return RootTableState::Invalid;
    }
    if ((rootTableAddress & ~0xFFFULL) == 0u) return RootTableState::NotConfigured;
    return RootTableState::Configured;
}

inline RegisterSnapshot classify_registers(
    bool readsValid, uint32_t versionRegister, uint64_t capability,
    uint64_t extendedCapability, uint32_t globalCommand,
    uint32_t globalStatus, uint64_t rootTableAddress, uint32_t faultStatus)
{
    RegisterSnapshot result{};
    result.status = RegisterStatus::ReadUnavailable;
    result.translation = TranslationState::Unknown;
    result.rootTable = RootTableState::Unknown;
    if (!readsValid || versionRegister == 0u || versionRegister == UINT32_MAX ||
        capability == UINT64_MAX || extendedCapability == UINT64_MAX ||
        globalCommand == UINT32_MAX || globalStatus == UINT32_MAX ||
        rootTableAddress == UINT64_MAX || faultStatus == UINT32_MAX) return result;
    result.version = static_cast<uint8_t>(versionRegister & 0xFFu);
    if ((result.version >> 4u) != 1u) {
        result.status = RegisterStatus::UnsupportedVersion;
        return result;
    }
    result.capability = capability;
    result.extendedCapability = extendedCapability;
    result.globalCommand = globalCommand;
    result.globalStatus = globalStatus;
    result.rootTableAddress = rootTableAddress;
    result.faultStatus = faultStatus;
    result.maximumGuestAddressWidth = static_cast<uint8_t>((capability >> 16u) & 0x3Fu);
    result.supportedAdjustedGuestAddressWidths = static_cast<uint8_t>((capability >> 8u) & 0x1Fu);
    result.capabilityValid = true;
    result.extendedCapabilityValid = true;
    result.commandStatusValid = true;
    result.rootTableValid = true;
    result.faultStatusValid = true;
    result.rootTable = classify_root_table(true, rootTableAddress);
    // GSTS.TES is bit 31. An enabled state is observed only; this snapshot
    // does not establish which devices or buffers are mapped.
    result.translation = (globalStatus & (1u << 31u)) != 0u
        ? TranslationState::EnabledObserved
        : TranslationState::DisabledObserved;
    result.status = result.maximumGuestAddressWidth == 0u ||
                    result.supportedAdjustedGuestAddressWidths == 0u
        ? RegisterStatus::UnsupportedCapabilities
        : RegisterStatus::Readable;
    return result;
}

inline bool dma_eligibility_allowed(RouteStatus route, RegisterStatus registers,
                                   TranslationState translation,
                                   RootTableState rootTable,
                                   bool bufferPhysicalValidated,
                                   bool deviceMappingEstablished,
                                   bool rmrrSatisfied)
{
    // R7 has no Intel DMA-domain implementation. The runtime cannot satisfy
    // the device-visible mapping proof, even if observation preconditions pass.
    const bool observedPreconditions =
        route == RouteStatus::Resolved && registers == RegisterStatus::Readable &&
        translation == TranslationState::EnabledObserved &&
        rootTable == RootTableState::Configured && bufferPhysicalValidated &&
        deviceMappingEstablished && rmrrSatisfied;
    return observedPreconditions && false;
}

} // namespace intel_dmar
} // namespace kernel
