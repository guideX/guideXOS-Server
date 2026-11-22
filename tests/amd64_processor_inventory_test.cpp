#include "../guideXOSBootLoader/bootinfo_acpi.h"
#include "../kernel/arch/amd64/include/arch/boot_processor_inventory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <vector>

using namespace guideXOS;
using namespace guideXOS::acpi;
using namespace kernel::arch::amd64;

namespace {

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
        std::exit(1); \
    } \
} while (false)

using Bytes = std::vector<uint8_t>;
constexpr uint64_t kRsdp = 0x1023;
constexpr uint64_t kRoot = 0x3081;
constexpr uint64_t kMadt = 0x100006FFDu;
constexpr uint64_t kOther = 0x9041;

void write_le(Bytes& bytes, size_t offset, uint64_t value, unsigned width = 4)
{
    for (unsigned i = 0; i < width; ++i) bytes.at(offset + i) = uint8_t(value >> (8 * i));
}

void checksum(Bytes& bytes, size_t field, size_t size)
{
    bytes.at(field) = 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum = uint8_t(sum + bytes.at(i));
    bytes.at(field) = uint8_t(0 - sum);
}

Bytes rsdp(bool extended, uint64_t xsdt = kRoot)
{
    Bytes bytes(extended ? 36 : 20);
    std::memcpy(bytes.data(), "RSD PTR ", 8);
    bytes[15] = extended ? 2 : 0;
    write_le(bytes, 16, kRoot);
    if (extended) {
        write_le(bytes, 20, bytes.size());
        write_le(bytes, 24, xsdt, 8);
    }
    checksum(bytes, 8, 20);
    if (extended) checksum(bytes, 32, bytes.size());
    return bytes;
}

Bytes sdt(const char* signature, const Bytes& payload = {}, uint8_t revision = 1)
{
    Bytes bytes(36);
    std::memcpy(bytes.data(), signature, 4);
    bytes[8] = revision;
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    write_le(bytes, 4, bytes.size());
    checksum(bytes, 9, bytes.size());
    return bytes;
}

Bytes root(bool extended, std::initializer_list<uint64_t> addresses)
{
    const unsigned width = extended ? 8 : 4;
    Bytes payload(addresses.size() * width);
    size_t offset = 0;
    for (const auto address : addresses) {
        write_le(payload, offset, address, width);
        offset += width;
    }
    return sdt(extended ? "XSDT" : "RSDT", payload);
}

Bytes lapic(uint8_t uid, uint8_t id, uint32_t flags)
{
    Bytes data{0, 8, uid, id, 0, 0, 0, 0};
    write_le(data, 4, flags);
    return data;
}

Bytes x2apic(uint32_t uid, uint32_t id, uint32_t flags)
{
    Bytes data(16);
    data[0] = 9;
    data[1] = 16;
    write_le(data, 4, id);
    write_le(data, 8, flags);
    write_le(data, 12, uid);
    return data;
}

Bytes io_apic(uint8_t id, uint32_t address, uint32_t gsi)
{
    Bytes record{1, 12, id, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    write_le(record, 4, address);
    write_le(record, 8, gsi);
    return record;
}

Bytes source_override(uint8_t bus, uint8_t source, uint32_t gsi, uint16_t flags)
{
    Bytes record{2, 10, bus, source, 0, 0, 0, 0, 0, 0};
    write_le(record, 4, gsi);
    write_le(record, 8, flags, 2);
    return record;
}

Bytes address_override(uint64_t address)
{
    Bytes record{5, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    write_le(record, 4, address, 8);
    return record;
}

Bytes madt(std::initializer_list<Bytes> records, uint8_t revision = 5,
           uint32_t local = 0, uint32_t flags = 0)
{
    Bytes payload(8);
    write_le(payload, 0, local);
    write_le(payload, 4, flags);
    for (const auto& record : records) payload.insert(payload.end(), record.begin(), record.end());
    return sdt("APIC", payload, revision);
}

struct Tables {
    std::map<uint64_t, Bytes> bytes{
        {kRsdp, rsdp(true)}, {kRoot, root(true, {kOther, kMadt})},
        {kOther, {'F', 'A', 'C', 'S'}}, {kMadt, madt({lapic(2, 3, 1)})}
    };
    std::vector<uint64_t> views;

    ProcessorInventory read(InterruptTopology* interrupts = nullptr)
    {
        views.clear();
        ProcessorInventory result{};
        discover_processors(kRsdp, [](void* context, uint64_t address, size_t size) -> const uint8_t* {
            auto& tables = *static_cast<Tables*>(context);
            tables.views.push_back(address);
            const auto found = tables.bytes.find(address);
            if (found == tables.bytes.end() || size > found->second.size()) return nullptr;
            return found->second.data();
        }, this, result, interrupts);
        return result;
    }
};

void single_and_mixed()
{
    Tables tables;
    tables.bytes[kRsdp] = rsdp(false);
    tables.bytes[kRoot] = root(false, {kOther, 0x7FFDu});
    tables.bytes[0x7FFDu] = tables.bytes[kMadt];
    auto result = tables.read();
    CHECK(result.status == InventoryStatus::Complete && result.root_kind == TableKind::Rsdt);
    CHECK(result.rsdp_address == kRsdp && result.root_address == kRoot && result.madt_address == 0x7FFDu);
    CHECK(result.advertised_count == 1 && result.enabled_count == 1 && result.online_capable_count == 0);
    CHECK(result.records[0].apic_id == 3 && result.records[0].acpi_uid == 2);

    tables.bytes[kRsdp] = rsdp(true, 0);
    CHECK(tables.read().status == InventoryStatus::Complete);
    tables = Tables{};
    tables.bytes[kMadt] = madt({lapic(2, 3, 1), Bytes{1, 12, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                               Bytes{2, 10, 0, 0, 0, 0, 0, 0, 0, 0}, Bytes{254, 2},
                               x2apic(0x10000000u, 0x10203u, 2), lapic(4, 4, 0),
                               x2apic(8, 0x304u, 3)});
    result = tables.read();
    CHECK(result.root_kind == TableKind::Xsdt && result.madt_address == kMadt);
    CHECK(result.status == InventoryStatus::Complete && result.stored_count == 4);
    CHECK(result.advertised_count == 4 && result.enabled_count == 2 && result.online_capable_count == 1);
    CHECK(result.records[1].kind == ProcessorKind::LocalX2Apic && result.records[1].apic_id == 0x10203u);
    CHECK(result.records[1].acpi_uid == 0x10000000u && result.records[1].madt_offset == 76);
    CHECK(!result.records[2].enabled() && !result.online_capable(result.records[2]));
    CHECK(result.records[3].enabled() && !result.online_capable(result.records[3]));
    tables.bytes[kMadt] = madt({lapic(4, 4, 2)}, 4);
    result = tables.read();
    CHECK(result.online_capable_count == 0 && result.records[0].flags == 2);
}

void missing_and_invalid_tables()
{
    ProcessorInventory result{};
    discover_processors(0, nullptr, nullptr, result);
    CHECK(result.status == InventoryStatus::NoRsdp);
    discover_processors(kRsdp, nullptr, nullptr, result);
    CHECK(result.status == InventoryStatus::MemoryMapUnavailable && result.rsdp_address == kRsdp);
    Tables tables;
    tables.bytes[kRoot] = root(true, {kOther});
    result = tables.read();
    CHECK(result.status == InventoryStatus::NoMadt && result.failure_table == TableKind::Xsdt);
    tables.bytes[kRoot] = root(true, {kMadt, kMadt});
    CHECK(tables.read().status == InventoryStatus::MultipleMadts);
    tables.bytes.erase(kOther);
    tables.bytes[kRoot] = root(true, {kMadt, kOther});
    result = tables.read();
    CHECK(result.status == InventoryStatus::Unreadable && result.failure_table == TableKind::RootEntry);
    CHECK(result.failure_address == kOther && result.failure_offset == 44 && result.stored_count == 0);
    tables.bytes[kRoot] = root(true, {UINT64_MAX - 2});
    CHECK(tables.read().status == InventoryStatus::InvalidAddress);

    tables = Tables{};
    tables.bytes[kRsdp][0] = 'X';
    CHECK(tables.read().status == InventoryStatus::InvalidSignature);
    tables.bytes[kRsdp] = rsdp(true);
    ++tables.bytes[kRsdp][8];
    CHECK(tables.read().status == InventoryStatus::InvalidChecksum);
    tables.bytes[kRsdp] = rsdp(true);
    ++tables.bytes[kRsdp][32];
    CHECK(tables.read().status == InventoryStatus::InvalidChecksum);
    write_le(tables.bytes[kRsdp], 20, 35);
    CHECK(tables.read().status == InventoryStatus::InvalidLength);
    write_le(tables.bytes[kRsdp], 20, 4097);
    CHECK(tables.read().status == InventoryStatus::LimitExceeded);
    tables.bytes[kRsdp] = rsdp(true);
    tables.bytes[kRsdp].resize(20);
    CHECK(tables.read().status == InventoryStatus::Unreadable);
    tables.bytes[kRsdp] = rsdp(false);
    tables.bytes[kRsdp][15] = 1;
    checksum(tables.bytes[kRsdp], 8, 20);
    CHECK(tables.read().status == InventoryStatus::UnsupportedRsdpRevision);

    tables = Tables{};
    tables.bytes[kRoot] = root(false, {});
    CHECK(tables.read().status == InventoryStatus::InvalidSignature);
    tables.bytes[kRoot] = sdt("XSDT", {0});
    CHECK(tables.read().status == InventoryStatus::InvalidLength);
    tables.bytes[kRoot] = sdt("XSDT", Bytes(8 * (kMaxRootEntries + 1)));
    CHECK(tables.read().status == InventoryStatus::LimitExceeded);
    tables.bytes[kRoot] = root(true, {kMadt});
    ++tables.bytes[kRoot][9];
    CHECK(tables.read().status == InventoryStatus::InvalidChecksum);
    tables.bytes[kRoot] = root(true, {kMadt});
    ++tables.bytes[kMadt][9];
    CHECK(tables.read().status == InventoryStatus::InvalidChecksum);
    tables.bytes[kMadt] = sdt("APIC");
    CHECK(tables.read().status == InventoryStatus::InvalidLength);
    write_le(tables.bytes[kMadt], 4, kMaxMadtBytes + 1);
    CHECK(tables.read().status == InventoryStatus::LimitExceeded);
    write_le(tables.bytes[kMadt], 4, 44);
    CHECK(tables.read().status == InventoryStatus::Unreadable);
}

void records_and_capacity()
{
    Tables tables;
    for (const auto& bad : {Bytes{0}, Bytes{0, 0}, Bytes{99, 1}, Bytes{99, 3},
                            Bytes{0, 4, 0, 0}, Bytes{9, 2}}) {
        tables.bytes[kMadt] = madt({lapic(1, 2, 1), bad});
        const auto result = tables.read();
        CHECK(result.status == InventoryStatus::InvalidRecord && result.failure_table == TableKind::Madt);
        CHECK(result.failure_offset == 52 && result.stored_count == 0 && result.advertised_count == 0);
    }
    Bytes payload(8);
    for (uint32_t i = 0; i <= kProcessorCapacity; ++i) {
        auto record = x2apic(i, i + 256, i == kProcessorCapacity ? 2 : 1);
        payload.insert(payload.end(), record.begin(), record.end());
    }
    tables.bytes[kMadt] = sdt("APIC", payload, 5);
    auto result = tables.read();
    CHECK(result.status == InventoryStatus::Truncated && result.stored_count == kProcessorCapacity);
    CHECK(result.advertised_count == kProcessorCapacity + 1 && result.enabled_count == kProcessorCapacity);
    CHECK(result.online_capable_count == 1);
    CpuTopologyHint hint{CpuTopologySource::Leaf1F, 256, 0, 0, 0};
    CHECK(match_boot_processor(hint, &result).status == BootCpuMatchStatus::InventoryTruncated);
    payload.push_back(99);
    tables.bytes[kMadt] = sdt("APIC", payload);
    CHECK(tables.read().status == InventoryStatus::InvalidRecord);
}

void boot_identity()
{
    Tables tables;
    tables.bytes[kMadt] = madt({lapic(1, 3, 1), x2apic(2, 0x103, 2)});
    auto result = tables.read();
    CpuTopologyHint hint{CpuTopologySource::Leaf1F, 3, 1, 1, 99};
    auto match = match_boot_processor(hint, &result);
    CHECK(match.status == BootCpuMatchStatus::MatchedEnabled && match.record_index == 0);
    hint.initial_apic_id = 0x103;
    match = match_boot_processor(hint, &result);
    CHECK(match.status == BootCpuMatchStatus::MatchedDisabled && match.record_index == 1);
    hint.initial_apic_id = 0x203;
    CHECK(match_boot_processor(hint, &result).status == BootCpuMatchStatus::Mismatch);
    hint = {};
    CHECK(match_boot_processor(hint, &result).status == BootCpuMatchStatus::IdentityUnavailable);
    CHECK(match_boot_processor(hint, nullptr).status == BootCpuMatchStatus::InventoryUnavailable);
    hint = {CpuTopologySource::LegacyLeaf1, 3, 0, 0, 0};
    CHECK(match_boot_processor(hint, &result).status == BootCpuMatchStatus::IdentityInsufficient);
    tables.bytes[kMadt] = madt({lapic(1, 3, 1), x2apic(2, 0x104, 2)});
    result = tables.read();
    CHECK(match_boot_processor(hint, &result).status == BootCpuMatchStatus::MatchedEnabled);

    tables.bytes[kMadt] = madt({lapic(1, 3, 1), lapic(1, 3, 1), x2apic(1, 0x103, 0),
                               x2apic(4, 3, 1), lapic(1, 0xFF, 0)});
    result = tables.read();
    CHECK(result.status == InventoryStatus::Complete && result.advertised_count == 5);
    CHECK(result.records[0].duplicate && result.records[1].duplicate && result.records[0].conflicting);
    CHECK(result.records[2].conflicting && result.records[3].conflicting);
    CHECK(!result.records[4].conflicting && !result.records[4].has_apic_id());
    hint.source = CpuTopologySource::Leaf0B;
    CHECK(match_boot_processor(hint, &result).status == BootCpuMatchStatus::Ambiguous);
    hint.initial_apic_id = 0x103;
    CHECK(match_boot_processor(hint, &result).status == BootCpuMatchStatus::Ambiguous);
}

void interrupt_inventory()
{
    Tables tables;
    InterruptTopology interrupts{};
    const uint32_t header = 0xFEE00000;
    const uint64_t local = 0x1FEE00000ull;
    tables.bytes[kMadt] = madt({lapic(1, 3, 1), io_apic(7, 0xFEC00000, 0),
        io_apic(8, 0xFEC00100, 32), source_override(0, 0, 2, 0),
        source_override(0, 12, 44, 5), source_override(0, 9, 41, 15),
        x2apic(2, 0x104, 2), address_override(local), Bytes{4, 6, 0xFF, 0, 0, 1}, Bytes{254, 2}},
        5, header, kMadtPcatCompat);
    const auto processors = tables.read(&interrupts);
    CHECK(processors.status == InventoryStatus::Complete && processors.stored_count == 2);
    CHECK(processors.records[1].apic_id == 0x104 && processors.online_capable_count == 1);
    CHECK((tables.views == std::vector<uint64_t>{kRsdp, kRsdp, kRsdp, kRoot, kRoot,
                                               kOther, kMadt, kMadt, kMadt}));
    CHECK(interrupts.local_apic_address == header && interrupts.madt_flags == kMadtPcatCompat);
    CHECK(interrupts.address_override_count == 1 && interrupts.effective_local_apic_address() == local);
    CHECK(interrupts.address_overrides[0].madt_offset == 122 && interrupts.address_overrides[0].issues == 0);
    CHECK(interrupts.io_apic_count == 2 && interrupts.io_apics[0].madt_offset == 52);
    CHECK(interrupts.io_apics[1].id == 8 && interrupts.io_apics[1].address == 0xFEC00100 &&
          interrupts.io_apics[1].gsi_base == 32 && interrupts.io_apics[1].issues == 0);
    CHECK(interrupts.source_override_count == 3 && interrupts.ignored_count == 2 &&
          interrupts.ignored_nmi_count == 1 && interrupts.malformed_count == 0 && !interrupts.truncated());
    const auto& pit = interrupts.source_overrides[0];
    CHECK(pit.bus == 0 && pit.source == 0 && pit.gsi == 2 && pit.flags == 0 && pit.issues == 0);
    CHECK(pit.polarity() == InterruptPolarity::ActiveHigh && pit.trigger() == InterruptTrigger::Edge);
    CHECK(interrupts.source_overrides[1].polarity() == InterruptPolarity::ActiveHigh &&
          interrupts.source_overrides[1].trigger() == InterruptTrigger::Edge);
    CHECK(interrupts.source_overrides[2].polarity() == InterruptPolarity::ActiveLow &&
          interrupts.source_overrides[2].trigger() == InterruptTrigger::Level);

    tables.bytes[kMadt] = madt({}, 3, header, 0);
    CHECK(tables.read(&interrupts).status == InventoryStatus::Complete);
    CHECK(interrupts.effective_local_apic_address() == header && interrupts.madt_flags == 0);
    CHECK(interrupts.address_override_count == 0 && interrupts.ignored_count == 0);
}

void invalid_interrupt_fields_and_lengths()
{
    Tables tables;
    InterruptTopology interrupts{};
    auto reserved_io = io_apic(2, 0xFEC00000, UINT32_MAX);
    reserved_io[3] = 1;
    auto reserved_address = address_override(0x1FEE00000ull);
    reserved_address[3] = 1;
    tables.bytes[kMadt] = madt({io_apic(1, 0, 0), reserved_io, io_apic(3, 0xFEC01001, 16),
        io_apic(4, 0xFFFFFFF0u, 32),
        source_override(1, 0, 2, 0), source_override(0, 16, 4, 0),
        source_override(0, 5, 5, 2), source_override(0, 6, 6, 8),
        source_override(0, 7, 7, 0x8000), source_override(0, 8, UINT32_MAX, 0),
        reserved_address}, 5, 0xFEE00000);
    CHECK(tables.read(&interrupts).status == InventoryStatus::Complete);
    for (uint16_t i = 0; i < interrupts.io_apic_count; ++i) {
        CHECK(interrupts.io_apics[i].issues == InvalidRecordFields);
    }
    CHECK(interrupts.io_apics[1].gsi_base == UINT32_MAX);
    for (uint16_t i = 0; i < 5; ++i) {
        CHECK(interrupts.source_overrides[i].issues == InvalidRecordFields);
    }
    CHECK(interrupts.source_overrides[0].polarity() == InterruptPolarity::Unresolved &&
          interrupts.source_overrides[0].trigger() == InterruptTrigger::Unresolved);
    CHECK(interrupts.source_overrides[2].polarity() == InterruptPolarity::Reserved &&
          interrupts.source_overrides[3].trigger() == InterruptTrigger::Reserved);
    CHECK(interrupts.source_overrides[5].gsi == UINT32_MAX && interrupts.source_overrides[5].issues == 0);
    CHECK(interrupts.address_overrides[0].issues == InvalidRecordFields &&
          interrupts.effective_local_apic_address() == 0);
    for (const auto address : {0ull, 0xFEE00001ull}) {
        tables.bytes[kMadt] = madt({address_override(address)}, 5, 0xFEE00000);
        CHECK(tables.read(&interrupts).status == InventoryStatus::Complete);
        CHECK(interrupts.address_overrides[0].issues == InvalidRecordFields &&
              interrupts.effective_local_apic_address() == 0);
    }
    tables.bytes[kMadt] = madt({}, 5, 0xFEE00001u);
    CHECK(tables.read(&interrupts).status == InventoryStatus::Complete &&
          interrupts.effective_local_apic_address() == 0);

    auto extended = io_apic(4, 0xFEC01000, 0);
    extended.push_back(0xAA);
    ++extended[1];
    tables.bytes[kMadt] = madt({Bytes{1, 2}, Bytes{2, 2}, Bytes{5, 2}, extended, lapic(1, 3, 1)}, 5, 0xFEE00000);
    CHECK(tables.read(&interrupts).stored_count == 1);
    CHECK(interrupts.malformed_count == 3 && interrupts.first_malformed_offset == 44 &&
          interrupts.io_apic_count == 1 && interrupts.io_apics[0].madt_offset == 50 &&
          interrupts.effective_local_apic_address() == 0);
    for (const auto& bad : {Bytes{1}, Bytes{2, 0}, Bytes{5, 12}, Bytes{9, 2}, Bytes{254, 3}}) {
        tables.bytes[kMadt] = madt({io_apic(1, 0xFEC00000, 0), bad});
        CHECK(tables.read(&interrupts).status == InventoryStatus::InvalidRecord);
        CHECK(interrupts.io_apic_count == 0 && interrupts.malformed_count == 0);
    }
    ++tables.bytes[kMadt][9];
    CHECK(tables.read(&interrupts).status == InventoryStatus::InvalidChecksum && interrupts.io_apic_count == 0);
}

void contradictory_interrupt_records()
{
    Tables tables;
    InterruptTopology interrupts{};
    const auto io = io_apic(2, 0xFEC00000, 0);
    const auto route = source_override(0, 0, 2, 0);
    const auto local = address_override(0xFEE00000);
    tables.bytes[kMadt] = madt({io, io, io_apic(2, 0xFEC01000, 16),
        io_apic(3, 0xFEC00000, 32), io_apic(4, 0xFEC03000, 0),
        route, source_override(0, 0, 2, 5), source_override(0, 0, 2, 15),
        source_override(0, 0, 9, 0), source_override(0, 9, 2, 0),
        local, local, address_override(0xFEE01000)});
    CHECK(tables.read(&interrupts).status == InventoryStatus::Complete);
    CHECK(interrupts.io_apics[0].issues == (DuplicateRecord | ConflictingRecord));
    CHECK(interrupts.io_apics[1].issues == interrupts.io_apics[0].issues);
    for (unsigned i = 2; i < 5; ++i) CHECK(interrupts.io_apics[i].issues == ConflictingRecord);
    CHECK(interrupts.source_overrides[0].issues == (DuplicateRecord | ConflictingRecord | SharedOverrideGsi));
    CHECK(interrupts.source_overrides[1].issues == interrupts.source_overrides[0].issues);
    CHECK(interrupts.source_overrides[2].issues == (ConflictingRecord | SharedOverrideGsi));
    CHECK(interrupts.source_overrides[3].issues == ConflictingRecord);
    CHECK(interrupts.source_overrides[4].issues == SharedOverrideGsi);
    CHECK(interrupts.address_overrides[0].issues == (DuplicateRecord | ConflictingRecord));
    CHECK(interrupts.address_overrides[1].issues == interrupts.address_overrides[0].issues);
    CHECK(interrupts.address_overrides[2].issues == ConflictingRecord);
    CHECK(interrupts.effective_local_apic_address() == 0);

    tables.bytes[kMadt] = madt({local, local});
    CHECK(tables.read(&interrupts).status == InventoryStatus::Complete);
    CHECK(interrupts.address_overrides[0].issues == DuplicateRecord && interrupts.effective_local_apic_address() == 0);
    tables.bytes[kMadt] = madt({io_apic(1, 0xFEC00100, 0), io_apic(2, 0xFEC00110, 32)});
    CHECK(tables.read(&interrupts).status == InventoryStatus::Complete);
    CHECK(interrupts.io_apics[0].issues == ConflictingRecord && interrupts.io_apics[1].issues == ConflictingRecord);
    tables.bytes[kMadt] = madt({local, io_apic(1, 0xFEE00100, 0)});
    CHECK(tables.read(&interrupts).status == InventoryStatus::Complete);
    CHECK(interrupts.io_apics[0].issues == ConflictingRecord);
}

void interrupt_capacity()
{
    Tables tables;
    InterruptTopology interrupts{};
    Bytes payload(8);
    const auto append = [&](const Bytes& record, uint16_t count) {
        for (uint16_t i = 0; i < count; ++i) payload.insert(payload.end(), record.begin(), record.end());
    };
    append(io_apic(2, 0xFEC00000, 0), kIoApicCapacity + 1);
    append(source_override(0, 0, 2, 0), kSourceOverrideCapacity + 1);
    append(address_override(0xFEE00000), kAddressOverrideCapacity + 1);
    append(lapic(1, 3, 1), 1);
    tables.bytes[kMadt] = sdt("APIC", payload);
    const auto processors = tables.read(&interrupts);
    CHECK(processors.status == InventoryStatus::Complete && processors.stored_count == 1);
    CHECK(interrupts.io_apic_count == kIoApicCapacity + 1 && interrupts.stored_io_apics() == kIoApicCapacity);
    CHECK(interrupts.source_override_count == kSourceOverrideCapacity + 1 &&
          interrupts.stored_source_overrides() == kSourceOverrideCapacity);
    CHECK(interrupts.address_override_count == kAddressOverrideCapacity + 1 &&
          interrupts.stored_address_overrides() == kAddressOverrideCapacity);
    CHECK(interrupts.truncated() && interrupts.effective_local_apic_address() == 0);
    payload.push_back(2);
    tables.bytes[kMadt] = sdt("APIC", payload);
    CHECK(tables.read(&interrupts).status == InventoryStatus::InvalidRecord && !interrupts.truncated());
}

void vendor_topology()
{
    struct Leaves {
        std::map<uint32_t, CpuidRegisters> words;
        std::vector<uint32_t> queried;
        CpuCapabilities decode()
        {
            queried.clear();
            return decode_cpu_capabilities([](void* context, uint32_t leaf, uint32_t subleaf,
                                              CpuidRegisters& out) {
                auto& fixture = *static_cast<Leaves*>(context);
                fixture.queried.push_back(leaf);
                auto found = fixture.words.find(leaf);
                if (subleaf || found == fixture.words.end()) return false;
                out = found->second;
                return true;
            }, this);
        }
        bool read(uint32_t leaf)
        {
            for (auto requested : queried) if (requested == leaf) return true;
            return false;
        }
    } leaves;
    leaves.words = {
        {0, {0x1F, 0x68747541, 0x444D4163, 0x69746E65}}, // AuthenticAMD
        {1, {0, 5u << 24, 0, 1u << 9}},
        {0x1F, {1, 2, 0x100, 7}},
        {0x0B, {1, 2, 0x100, 0x204}},
        {0x80000000u, {0x80000026u, 0, 0, 0}},
        {0x80000001u, {0, 0, 1u << 22, 0}},
        {0x8000001Eu, {0x103, 0, 0, 0}},
        {0x80000026u, {1, 2, 0x100, 0x1203}}
    };
    Tables tables;
    tables.bytes[kMadt] = madt({x2apic(1, 0x1203, 1)});
    auto inventory = tables.read();
    auto cpu = leaves.decode();
    CHECK(cpu.topology.source == CpuTopologySource::AmdLeaf80000026 && cpu.topology.initial_apic_id == 0x1203);
    CHECK(!leaves.read(0x1F) && !leaves.read(0x0B) && !leaves.read(0x8000001E));
    CHECK(match_boot_processor(cpu.topology, &inventory).status == BootCpuMatchStatus::MatchedEnabled);
    leaves.words[0x80000026u].ebx = 0;
    cpu = leaves.decode();
    CHECK(cpu.topology.source == CpuTopologySource::Leaf0B && cpu.topology.initial_apic_id == 0x204);
    CHECK(match_boot_processor(cpu.topology, &inventory).status == BootCpuMatchStatus::Mismatch);
    leaves.words.erase(0x0B);
    cpu = leaves.decode();
    CHECK(cpu.topology.source == CpuTopologySource::AmdLeaf8000001E && cpu.topology.initial_apic_id == 0x103);
    CHECK(cpu.topology.logical_count_hint == 0);
    leaves.words[0x80000001u].ecx = 0;
    CHECK(leaves.decode().topology.source == CpuTopologySource::LegacyLeaf1 && !leaves.read(0x8000001E));
    leaves.words[0x80000001u].ecx = 1u << 22;
    leaves.words[0x80000000u].eax = 0x80000008u;
    CHECK(leaves.decode().topology.source == CpuTopologySource::LegacyLeaf1 && !leaves.read(0x80000026));
    CHECK(!leaves.read(0x8000001E));
    leaves.words[0] = {0x1F, 0x756E6547, 0x6C65746E, 0x49656E69}; // GenuineIntel
    leaves.words[0x80000000u].eax = 0x80000026u;
    CHECK(leaves.decode().topology.source == CpuTopologySource::Leaf1F && !leaves.read(0x80000026));
    leaves.words[0] = {0x1F, 0x6F677948, 0x656E6975, 0x6E65476E}; // HygonGenuine
    CHECK(leaves.decode().topology.source == CpuTopologySource::AmdLeaf8000001E);
    CHECK(!leaves.read(0x80000026) && !leaves.read(0x1F));
}

void uefi_mapping_and_handoff()
{
    alignas(4096) static uint8_t firmware[8192]{};
    alignas(4096) static BootInfoWithAcpi handoff{};
    const auto base = reinterpret_cast<uintptr_t>(firmware);
    const Bytes rsdp_bytes = rsdp(true, base + 128);
    const Bytes root_bytes = root(true, {base + 4093});
    const Bytes madt_bytes = madt({lapic(1, 3, 1), io_apic(2, 0xFEC00000, 0)}, 3, 0xFEE00000);
    std::memcpy(firmware, rsdp_bytes.data(), rsdp_bytes.size());
    std::memcpy(firmware + 128, root_bytes.data(), root_bytes.size());
    std::memcpy(firmware + 4093, madt_bytes.data(), madt_bytes.size());

    Bytes map(48);
    write_le(map, 0, 9);
    write_le(map, 8, base, 8);
    write_le(map, 24, 2, 8);
    ProcessorInventory& inventory = handoff.acpi.inventory;
    capture_uefi_processors(base, map.data(), map.size(), 48, inventory);
    CHECK(inventory.status == InventoryStatus::Complete && inventory.advertised_count == 1);
    write_le(map, 24, 1, 8);
    capture_uefi_processors(base, map.data(), map.size(), 48, inventory);
    CHECK(inventory.status == InventoryStatus::Unreadable && inventory.failure_table == TableKind::RootEntry);
    write_le(map, 24, UINT64_MAX, 8);
    capture_uefi_processors(base, map.data(), map.size(), 48, inventory);
    CHECK(inventory.status == InventoryStatus::Unreadable && inventory.failure_table == TableKind::Rsdp);
    write_le(map, 24, 2, 8);
    write_le(map, 0, 11); // EfiMemoryMappedIO
    capture_uefi_processors(base, map.data(), map.size(), 48, inventory);
    CHECK(inventory.status == InventoryStatus::Unreadable);
    write_le(map, 0, 10); // EfiACPIMemoryNVS
    write_le(map, 32, 0x2000, 8); // EFI_MEMORY_RP
    capture_uefi_processors(base, map.data(), map.size(), 48, inventory);
    CHECK(inventory.status == InventoryStatus::Unreadable);
    write_le(map, 32, 0, 8);
    capture_uefi_processors(base, map.data(), map.size() - 1, 48, inventory);
    CHECK(inventory.status == InventoryStatus::MemoryMapUnavailable);
    capture_uefi_processors(base, map.data(), map.size(), 48, inventory);
    CHECK(inventory.status == InventoryStatus::Complete);

    auto& boot = handoff.base;
    boot.Magic = GUIDEXOS_BOOTINFO_MAGIC;
    boot.Version = GUIDEXOS_BOOTINFO_VERSION;
    boot.Size = sizeof(BootInfo);
    boot.BootMode = BootMode::Uefi;
    boot.AcpiRsdp = base;
    CHECK(bootinfo_acpi_inventory(&boot) == nullptr);
    seal_bootinfo_acpi(handoff);
    CHECK(bootinfo_acpi_inventory(&boot) == nullptr);
    uint32_t sum = 0;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&boot);
    for (size_t i = 0; i < sizeof(boot) / 4; ++i) {
        uint32_t word;
        std::memcpy(&word, bytes + 4 * i, sizeof(word));
        sum += word;
    }
    boot.HeaderChecksum = 0u - sum;
    CHECK(bootinfo_acpi_inventory(&boot) == &inventory);
    ++inventory.records[0].apic_id;
    CHECK(bootinfo_acpi_inventory(&boot) == nullptr);
    seal_bootinfo_acpi(handoff);
    CHECK(bootinfo_acpi_inventory(&boot) == &inventory);
    ++handoff.acpi.version;
    CHECK(bootinfo_acpi_inventory(&boot) == nullptr);
    seal_bootinfo_acpi(handoff);
    ++inventory.rsdp_address;
    seal_bootinfo_acpi(handoff);
    CHECK(bootinfo_acpi_inventory(&boot) == nullptr);

    // Old loaders clear the optional tail. Its absence or corruption is
    // independent from the processor handoff used by CPU3.
    --inventory.rsdp_address;
    seal_bootinfo_acpi(handoff);
    CHECK(bootinfo_acpi_inventory(&boot) == &inventory);
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == nullptr);
    auto& topology = handoff.interrupts.topology;
    capture_uefi_processors(base, map.data(), map.size(), 48, inventory, &topology);
    seal_bootinfo_acpi(handoff);
    seal_bootinfo_interrupt_topology(handoff);
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == &topology);
    CHECK(topology.io_apic_count == 1 && topology.effective_local_apic_address() == 0xFEE00000);
    ++topology.io_apics[0].id;
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == nullptr && bootinfo_acpi_inventory(&boot) == &inventory);
    seal_bootinfo_interrupt_topology(handoff);
    ++handoff.interrupts.version;
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == nullptr);
    seal_bootinfo_interrupt_topology(handoff);
    --handoff.interrupts.size;
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == nullptr);
    seal_bootinfo_interrupt_topology(handoff);
    handoff.interrupts.signature = 0;
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == nullptr);
    seal_bootinfo_interrupt_topology(handoff);
    inventory.status = InventoryStatus::Truncated;
    seal_bootinfo_acpi(handoff);
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == &topology);
    inventory.status = InventoryStatus::NoRsdp;
    seal_bootinfo_acpi(handoff);
    CHECK(bootinfo_acpi_interrupt_topology(&boot) == nullptr);
    seal_bootinfo_interrupt_topology(handoff);
    CHECK(handoff.interrupts.signature == 0);
    ++boot.HeaderChecksum;
    CHECK(bootinfo_acpi_inventory(&boot) == nullptr && bootinfo_acpi_interrupt_topology(&boot) == nullptr);
}

} // namespace

int main()
{
    single_and_mixed();
    missing_and_invalid_tables();
    records_and_capacity();
    boot_identity();
    interrupt_inventory();
    invalid_interrupt_fields_and_lengths();
    contradictory_interrupt_records();
    interrupt_capacity();
    vendor_topology();
    uefi_mapping_and_handoff();
    std::puts("PASS AMD64 processor and interrupt inventory (10 fixture groups)");
}
