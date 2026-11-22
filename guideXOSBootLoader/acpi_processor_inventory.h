#pragma once

#include <stddef.h>
#include <stdint.h>

namespace guideXOS {
namespace acpi {

constexpr uint32_t kProcessorCapacity = 128;
constexpr uint32_t kMaxRootEntries = 256;
constexpr uint32_t kMaxMadtBytes = 64 * 1024;

enum class InventoryStatus : uint8_t {
    Unavailable, NoRsdp, MemoryMapUnavailable, InvalidAddress, Unreadable,
    InvalidSignature, InvalidChecksum, InvalidLength, LimitExceeded,
    UnsupportedRsdpRevision, NoMadt, MultipleMadts, InvalidRecord,
    Complete, Truncated
};

enum class TableKind : uint8_t { None, Rsdp, Rsdt, Xsdt, RootEntry, Madt };
enum class ProcessorKind : uint8_t { LocalApic = 0, LocalX2Apic = 9 };

struct ProcessorRecord {
    uint32_t apic_id;
    uint32_t acpi_uid;
    uint32_t flags;
    uint32_t madt_offset;
    ProcessorKind kind;
    bool duplicate;
    bool conflicting;

    bool enabled() const { return (flags & 1u) != 0; }
    bool has_apic_id() const
    {
        return apic_id != (kind == ProcessorKind::LocalApic ? 0xFFu : UINT32_MAX);
    }
};

struct ProcessorInventory {
    uint64_t rsdp_address;
    uint64_t root_address;
    uint64_t madt_address;
    uint64_t failure_address;
    uint32_t madt_length;
    uint32_t failure_offset;
    uint32_t advertised_count; // Type 0/9 records, including disabled entries.
    uint32_t enabled_count;
    uint32_t online_capable_count;
    uint32_t stored_count;
    InventoryStatus status;
    TableKind root_kind;
    TableKind failure_table;
    uint8_t rsdp_revision;
    uint8_t madt_revision;
    ProcessorRecord records[kProcessorCapacity];

    bool online_capable(const ProcessorRecord& record) const
    {
        return madt_revision >= 5 && (record.flags & 3u) == 2u;
    }
};

constexpr uint16_t kIoApicCapacity = 16;
constexpr uint16_t kSourceOverrideCapacity = 24;
constexpr uint16_t kAddressOverrideCapacity = 4;
constexpr uint32_t kMadtPcatCompat = 1u;

enum InterruptRecordIssue : uint8_t {
    InvalidRecordFields = 1u << 0,
    DuplicateRecord = 1u << 1,
    ConflictingRecord = 1u << 2,
    SharedOverrideGsi = 1u << 3
};

struct IoApicRecord {
    uint32_t address;
    uint32_t gsi_base;
    uint32_t madt_offset;
    uint8_t id;
    uint8_t issues;
};

enum class InterruptPolarity : uint8_t { Unresolved, ActiveHigh, ActiveLow, Reserved };
enum class InterruptTrigger : uint8_t { Unresolved, Edge, Level, Reserved };

struct SourceOverrideRecord {
    uint32_t gsi;
    uint32_t madt_offset;
    uint16_t flags;
    uint8_t bus;
    uint8_t source;
    uint8_t issues;

    // Zero encodes bus-conforming. ISA defaults are high and edge.
    InterruptPolarity polarity() const
    {
        switch (flags & 3u) {
        case 0: return bus == 0 ? InterruptPolarity::ActiveHigh : InterruptPolarity::Unresolved;
        case 1: return InterruptPolarity::ActiveHigh;
        case 3: return InterruptPolarity::ActiveLow;
        default: return InterruptPolarity::Reserved;
        }
    }

    InterruptTrigger trigger() const
    {
        switch ((flags >> 2) & 3u) {
        case 0: return bus == 0 ? InterruptTrigger::Edge : InterruptTrigger::Unresolved;
        case 1: return InterruptTrigger::Edge;
        case 3: return InterruptTrigger::Level;
        default: return InterruptTrigger::Reserved;
        }
    }
};

struct AddressOverrideRecord {
    uint64_t address;
    uint32_t madt_offset;
    uint8_t issues;
};

struct InterruptTopology {
    uint32_t local_apic_address;
    uint32_t madt_flags;
    uint32_t first_malformed_offset;
    // Totals include all records with a supported length, even beyond capacity.
    uint16_t io_apic_count;
    uint16_t source_override_count;
    uint16_t address_override_count;
    uint16_t malformed_count;
    uint16_t ignored_count;
    uint16_t ignored_nmi_count;
    IoApicRecord io_apics[kIoApicCapacity];
    SourceOverrideRecord source_overrides[kSourceOverrideCapacity];
    AddressOverrideRecord address_overrides[kAddressOverrideCapacity];

    uint16_t stored_io_apics() const
    {
        return io_apic_count < kIoApicCapacity ? io_apic_count : kIoApicCapacity;
    }

    uint16_t stored_source_overrides() const
    {
        return source_override_count < kSourceOverrideCapacity ? source_override_count : kSourceOverrideCapacity;
    }

    uint16_t stored_address_overrides() const
    {
        return address_override_count < kAddressOverrideCapacity ? address_override_count : kAddressOverrideCapacity;
    }

    bool truncated() const
    {
        return io_apic_count > kIoApicCapacity || source_override_count > kSourceOverrideCapacity ||
               address_override_count > kAddressOverrideCapacity;
    }

    // A firmware address candidate. Zero denotes invalid or ambiguous data.
    uint64_t effective_local_apic_address() const
    {
        if (malformed_count != 0) return 0;
        if (address_override_count == 1 && address_overrides[0].issues == 0) return address_overrides[0].address;
        if (address_override_count != 0 || (local_apic_address & 0xFFFu) != 0) return 0;
        return local_apic_address;
    }
};

// Return a read-only span for the entire range, or nullptr. A returned span
// must remain accessible and stable for the duration of discovery.
using PhysicalView = const uint8_t* (*)(void*, uint64_t address, size_t bytes);

void discover_processors(uint64_t rsdp, PhysicalView view, void* context,
                         ProcessorInventory& out, InterruptTopology* interrupts = nullptr);

// Capture while the AMD64 UEFI firmware mappings are active. The descriptor
// buffer and its size come from a successful GetMemoryMap call.
void capture_uefi_processors(uint64_t rsdp, const void* memory_map, size_t map_bytes,
                             size_t descriptor_size, ProcessorInventory& out,
                             InterruptTopology* interrupts = nullptr);

} // namespace acpi
} // namespace guideXOS
