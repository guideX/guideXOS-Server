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

// Return a read-only span for the entire range, or nullptr. A returned span
// must remain accessible and stable for the duration of discovery.
using PhysicalView = const uint8_t* (*)(void*, uint64_t address, size_t bytes);

void discover_processors(uint64_t rsdp, PhysicalView view, void* context,
                         ProcessorInventory& out);

// Capture while the AMD64 UEFI firmware mappings are active. The descriptor
// buffer and its size come from a successful GetMemoryMap call.
void capture_uefi_processors(uint64_t rsdp, const void* memory_map, size_t map_bytes,
                             size_t descriptor_size, ProcessorInventory& out);

} // namespace acpi
} // namespace guideXOS
