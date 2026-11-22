#include "acpi_processor_inventory.h"

namespace guideXOS {
namespace acpi {
namespace {

constexpr uint32_t kLegacyRsdpBytes = 20;
constexpr uint32_t kExtendedRsdpBytes = 36;
constexpr uint32_t kMaxRsdpBytes = 4096;
constexpr uint32_t kSdtHeaderBytes = 36;
constexpr uint32_t kMadtHeaderBytes = 44;

uint32_t read32(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

uint64_t read64(const uint8_t* bytes)
{
    return read32(bytes) | (uint64_t(read32(bytes + 4)) << 32);
}

bool signature_is(const uint8_t* bytes, const char* signature, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] != static_cast<uint8_t>(signature[i])) return false;
    }
    return true;
}

bool checksum_valid(const uint8_t* bytes, size_t size)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum = static_cast<uint8_t>(sum + bytes[i]);
    return sum == 0;
}

struct Discovery {
    PhysicalView view;
    void* context;
    ProcessorInventory& out;
    InterruptTopology* interrupts;

    const uint8_t* fail(InventoryStatus status)
    {
        out.status = status;
        return nullptr;
    }

    const uint8_t* read(uint64_t address, size_t bytes)
    {
        out.failure_address = address;
        if (address == 0 || bytes > UINT64_MAX - address) {
            return fail(InventoryStatus::InvalidAddress);
        }
        const uint8_t* span = view(context, address, bytes);
        return span ? span : fail(InventoryStatus::Unreadable);
    }

    bool read_rsdp()
    {
        out.failure_table = TableKind::Rsdp;
        const auto* rsdp = read(out.rsdp_address, kLegacyRsdpBytes);
        if (!rsdp) return false;
        if (!signature_is(rsdp, "RSD PTR ", 8)) return fail(InventoryStatus::InvalidSignature);
        if (!checksum_valid(rsdp, kLegacyRsdpBytes)) return fail(InventoryStatus::InvalidChecksum);
        out.rsdp_revision = rsdp[15];
        if (out.rsdp_revision == 1) return fail(InventoryStatus::UnsupportedRsdpRevision);

        const uint64_t rsdt = read32(rsdp + 16);
        uint64_t xsdt = 0;
        if (out.rsdp_revision >= 2) {
            rsdp = read(out.rsdp_address, kExtendedRsdpBytes);
            if (!rsdp) return false;
            const uint32_t length = read32(rsdp + 20);
            if (length < kExtendedRsdpBytes) return fail(InventoryStatus::InvalidLength);
            if (length > kMaxRsdpBytes) return fail(InventoryStatus::LimitExceeded);
            rsdp = read(out.rsdp_address, length);
            if (!rsdp) return false;
            if (!checksum_valid(rsdp, length)) return fail(InventoryStatus::InvalidChecksum);
            xsdt = read64(rsdp + 24);
        }

        out.root_kind = xsdt ? TableKind::Xsdt : TableKind::Rsdt;
        out.root_address = xsdt ? xsdt : rsdt;
        return true;
    }

    const uint8_t* read_sdt(uint64_t address, const char* signature, uint32_t minimum,
                            uint32_t maximum, uint32_t& length)
    {
        const auto* header = read(address, kSdtHeaderBytes);
        if (!header) return nullptr;
        if (!signature_is(header, signature, 4)) return fail(InventoryStatus::InvalidSignature);
        length = read32(header + 4);
        if (length < minimum) return fail(InventoryStatus::InvalidLength);
        if (length > maximum) return fail(InventoryStatus::LimitExceeded);
        const auto* table = read(address, length);
        if (!table) return nullptr;
        if (!checksum_valid(table, length)) return fail(InventoryStatus::InvalidChecksum);
        return table;
    }

    bool find_madt()
    {
        out.failure_table = out.root_kind;
        const bool extended = out.root_kind == TableKind::Xsdt;
        const uint32_t entry_size = extended ? 8 : 4;
        uint32_t length = 0;
        const auto* root = read_sdt(out.root_address, extended ? "XSDT" : "RSDT",
                                    kSdtHeaderBytes, kSdtHeaderBytes + kMaxRootEntries * entry_size,
                                    length);
        if (!root) return false;
        if ((length - kSdtHeaderBytes) % entry_size) return fail(InventoryStatus::InvalidLength);
        for (uint32_t offset = kSdtHeaderBytes; offset < length; offset += entry_size) {
            const uint64_t child = extended ? read64(root + offset) : read32(root + offset);
            out.failure_table = TableKind::RootEntry;
            out.failure_offset = offset;
            // The signature is enough to skip unrelated tables, including FACS.
            const auto* signature = read(child, 4);
            if (!signature) return false;
            if (!signature_is(signature, "APIC", 4)) continue;
            if (out.madt_address != 0) return fail(InventoryStatus::MultipleMadts);
            out.madt_address = child;
        }
        out.failure_table = out.root_kind;
        out.failure_address = out.root_address;
        out.failure_offset = 0;
        if (out.madt_address == 0) return fail(InventoryStatus::NoMadt);
        return true;
    }

    void read_madt();
};

void mark_identity_issues(ProcessorRecord* records, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        auto& current = records[i];
        if (!current.has_apic_id()) continue;
        for (uint32_t j = 0; j < i; ++j) {
            auto& prior = records[j];
            if (!prior.has_apic_id()) continue;
            const bool same_id = current.apic_id == prior.apic_id;
            const bool same_uid = current.acpi_uid == prior.acpi_uid;
            if (!same_id && !same_uid) continue;
            if (same_id && same_uid && current.kind == prior.kind && current.flags == prior.flags) {
                current.duplicate = prior.duplicate = true;
            } else {
                current.conflicting = prior.conflicting = true;
            }
        }
    }
}

// IOREGSEL and the last byte of the 32-bit IOWIN register.
constexpr uint32_t kIoApicWindowLastByte = 0x13;

void mark_pair(uint8_t& first, uint8_t& second, InterruptRecordIssue issue)
{
    first |= issue;
    second |= issue;
}

void capture_interrupt_record(InterruptTopology& topology, const uint8_t* record, uint32_t offset)
{
    // The enclosing walk already checked the generic two-byte header and
    // ensured that the complete indicated record is in the checksummed MADT.
    const uint8_t type = record[0];
    const uint8_t minimum = type == 1 || type == 5 ? 12 : type == 2 ? 10 : 0;
    if (minimum == 0) {
        ++topology.ignored_count;
        if (type == 3 || type == 4 || type == 10) ++topology.ignored_nmi_count;
        return;
    }
    if (record[1] < minimum) {
        if (topology.malformed_count++ == 0) topology.first_malformed_offset = offset;
        return;
    }

    if (type == 1) {
        const uint16_t index = topology.io_apic_count++;
        if (index >= kIoApicCapacity) return;
        auto& io = topology.io_apics[index];
        io = {read32(record + 4), read32(record + 8), offset, record[2], 0};
        if (record[3] != 0 || io.address == 0 || (io.address & 3u) != 0 ||
            io.address > UINT32_MAX - kIoApicWindowLastByte) io.issues |= InvalidRecordFields;
        for (uint16_t i = 0; i < index; ++i) {
            auto& prior = topology.io_apics[i];
            const bool same_id = io.id == prior.id;
            const bool same_address = io.address == prior.address;
            const bool same_base = io.gsi_base == prior.gsi_base;
            const bool overlap = io.address <= uint64_t(prior.address) + kIoApicWindowLastByte &&
                                 prior.address <= uint64_t(io.address) + kIoApicWindowLastByte;
            if (same_id || overlap || same_base) {
                mark_pair(io.issues, prior.issues, same_id && same_address && same_base
                             ? DuplicateRecord : ConflictingRecord);
            }
        }
    } else if (type == 2) {
        const uint16_t index = topology.source_override_count++;
        if (index >= kSourceOverrideCapacity) return;
        auto& route = topology.source_overrides[index];
        const auto flags = static_cast<uint16_t>(uint16_t(record[8]) | (uint16_t(record[9]) << 8));
        route = {read32(record + 4), offset, flags, record[2], record[3], 0};
        if (route.bus != 0 || route.source >= 16 || (route.flags & 0xFFF0u) != 0 ||
            route.polarity() == InterruptPolarity::Reserved || route.trigger() == InterruptTrigger::Reserved) {
            route.issues |= InvalidRecordFields;
        }
        for (uint16_t i = 0; i < index; ++i) {
            auto& prior = topology.source_overrides[i];
            if (route.bus == prior.bus && route.source == prior.source) {
                const bool valid = ((route.issues | prior.issues) & InvalidRecordFields) == 0;
                const bool same_mode = route.flags == prior.flags ||
                    (valid && route.polarity() == prior.polarity() && route.trigger() == prior.trigger());
                mark_pair(route.issues, prior.issues, route.gsi == prior.gsi && same_mode
                             ? DuplicateRecord : ConflictingRecord);
            } else if (route.gsi == prior.gsi) {
                mark_pair(route.issues, prior.issues, SharedOverrideGsi);
            }
        }
    } else {
        const uint16_t index = topology.address_override_count++;
        if (index >= kAddressOverrideCapacity) return;
        auto& local = topology.address_overrides[index];
        local = {read64(record + 4), offset, 0};
        if (record[2] != 0 || record[3] != 0 || local.address == 0 || (local.address & 0xFFFu) != 0) {
            local.issues |= InvalidRecordFields;
        }
        for (uint16_t i = 0; i < index; ++i) {
            auto& prior = topology.address_overrides[i];
            mark_pair(local.issues, prior.issues, local.address == prior.address
                         ? DuplicateRecord : ConflictingRecord);
        }
    }
}

void Discovery::read_madt()
{
    out.failure_table = TableKind::Madt;
    uint32_t length = 0;
    const auto* madt = read_sdt(out.madt_address, "APIC", kMadtHeaderBytes, kMaxMadtBytes, length);
    if (!madt) return;
    out.madt_length = length;
    out.madt_revision = madt[8];
    uint32_t advertised = 0;
    uint32_t enabled = 0;
    uint32_t online_capable = 0;
    uint32_t stored = 0;
    for (uint32_t offset = kMadtHeaderBytes; offset < length;) {
        const uint32_t remaining = length - offset;
        const uint8_t* record = madt + offset;
        out.failure_offset = offset;
        if (remaining < 2 || record[1] < 2 || record[1] > remaining) {
            fail(InventoryStatus::InvalidRecord);
            return;
        }
        const auto kind = static_cast<ProcessorKind>(record[0]);
        if (kind == ProcessorKind::LocalApic || kind == ProcessorKind::LocalX2Apic) {
            const bool extended = kind == ProcessorKind::LocalX2Apic;
            if (record[1] < (extended ? 16 : 8)) {
                fail(InventoryStatus::InvalidRecord);
                return;
            }
            const ProcessorRecord processor = {
                extended ? read32(record + 4) : uint32_t(record[3]),
                extended ? read32(record + 12) : uint32_t(record[2]),
                read32(record + (extended ? 8 : 4)), offset, kind, false, false
            };
            ++advertised;
            if (processor.enabled()) ++enabled;
            if (out.online_capable(processor)) ++online_capable;
            if (stored < kProcessorCapacity) out.records[stored++] = processor;
        } else if (interrupts) {
            capture_interrupt_record(*interrupts, record, offset);
        }
        offset += record[1];
    }
    if (interrupts) {
        interrupts->local_apic_address = read32(madt + 36);
        interrupts->madt_flags = read32(madt + 40);
        const uint64_t local = interrupts->effective_local_apic_address();
        for (uint16_t i = 0; local != 0 && i < interrupts->stored_io_apics(); ++i) {
            auto& io = interrupts->io_apics[i];
            if (io.address <= (local | 0xFFFu) && local <= uint64_t(io.address) + kIoApicWindowLastByte) {
                io.issues |= ConflictingRecord;
            }
        }
    }
    mark_identity_issues(out.records, stored);
    out.advertised_count = advertised;
    out.enabled_count = enabled;
    out.online_capable_count = online_capable;
    out.stored_count = stored;
    out.status = advertised == stored ? InventoryStatus::Complete : InventoryStatus::Truncated;
    out.failure_table = TableKind::None;
    out.failure_address = 0;
    out.failure_offset = 0;
}

struct UefiMap {
    const uint8_t* descriptors;
    size_t bytes;
    size_t stride;
};

const uint8_t* uefi_acpi_view(void* context, uint64_t address, size_t bytes)
{
    constexpr uint64_t kIdentityLimit = 1ull << 47;
    constexpr uint64_t kPageBytes = 4096;
    constexpr uint64_t kEfiMemoryReadProtected = 0x2000;
    constexpr uint32_t kEfiAcpiReclaimMemory = 9;
    constexpr uint32_t kEfiAcpiMemoryNvs = 10;

    if (address >= kIdentityLimit || bytes > kIdentityLimit - address) return nullptr;
    const auto& map = *static_cast<const UefiMap*>(context);
    for (size_t offset = 0; offset < map.bytes; offset += map.stride) {
        const auto* descriptor = map.descriptors + offset;
        const uint32_t type = read32(descriptor);
        if (type != kEfiAcpiReclaimMemory && type != kEfiAcpiMemoryNvs) continue;
        const uint64_t base = read64(descriptor + 8);
        const uint64_t pages = read64(descriptor + 24);
        const uint64_t attributes = read64(descriptor + 32);
        if ((attributes & kEfiMemoryReadProtected) != 0 || base > address ||
            (base & (kPageBytes - 1)) != 0 || pages > (kIdentityLimit - base) / kPageBytes) {
            continue;
        }
        const uint64_t end = base + pages * kPageBytes;
        if (address < end && bytes <= end - address) {
            // UEFI AMD64 identity-maps memory described by its boot map.
            return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(address));
        }
    }
    return nullptr;
}

} // namespace

void discover_processors(uint64_t rsdp, PhysicalView view, void* context, ProcessorInventory& out,
                         InterruptTopology* interrupts)
{
    out = {};
    if (interrupts) *interrupts = {};
    out.rsdp_address = rsdp;
    if (rsdp == 0) {
        out.status = InventoryStatus::NoRsdp;
    } else if (!view) {
        out.status = InventoryStatus::MemoryMapUnavailable;
    } else {
        Discovery discovery{view, context, out, interrupts};
        if (discovery.read_rsdp() && discovery.find_madt()) discovery.read_madt();
        if (interrupts && out.status == InventoryStatus::InvalidRecord) *interrupts = {};
    }
}

void capture_uefi_processors(uint64_t rsdp, const void* memory_map, size_t map_bytes,
                             size_t descriptor_size, ProcessorInventory& out,
                             InterruptTopology* interrupts)
{
    const bool valid_map = memory_map && map_bytes != 0 && map_bytes <= 256u * 1024u &&
                           descriptor_size >= 40 && descriptor_size <= 256 &&
                           descriptor_size % 8 == 0 && map_bytes % descriptor_size == 0;
    UefiMap map{static_cast<const uint8_t*>(memory_map), map_bytes, descriptor_size};
    discover_processors(rsdp, valid_map ? uefi_acpi_view : nullptr, &map, out, interrupts);
}

} // namespace acpi
} // namespace guideXOS
