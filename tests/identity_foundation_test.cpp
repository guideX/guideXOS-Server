#include "kernel/identity_runtime.h"

#include <iostream>
#include <string.h>

using namespace kernel::identity;

namespace {

struct FakeStore {
    bool available{true};
    bool writable{true};
    bool failNextWrite{false};
    uint32_t writeCount{0u};
    bool slotsPresent[2]{};
    size_t sizes[2]{};
    uint8_t bytes[2][kMaximumIdentityStoreBytes]{};
};

int failures = 0;
int checks = 0;

void check(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << "\n";
    }
}

StoreReadResult readSlot(void* context, uint8_t slot, uint8_t* output,
                         size_t capacity, size_t* outBytes)
{
    FakeStore& store = *static_cast<FakeStore*>(context);
    if (outBytes) *outBytes = 0u;
    if (!store.available) return StoreReadResult::Unavailable;
    if (slot > 1u || !output || !outBytes) return StoreReadResult::IoError;
    if (!store.slotsPresent[slot]) return StoreReadResult::Missing;
    if (store.sizes[slot] > capacity) return StoreReadResult::Corrupt;
    memcpy(output, store.bytes[slot], store.sizes[slot]);
    *outBytes = store.sizes[slot];
    return StoreReadResult::Ok;
}

StoreWriteResult writeSlot(void* context, uint8_t slot, const uint8_t* bytes,
                           size_t byteCount)
{
    FakeStore& store = *static_cast<FakeStore*>(context);
    if (!store.available) return StoreWriteResult::Unavailable;
    if (!store.writable) return StoreWriteResult::ReadOnly;
    if (slot > 1u || !bytes || byteCount > kMaximumIdentityStoreBytes)
        return StoreWriteResult::IoError;
    ++store.writeCount;
    if (store.failNextWrite) {
        store.failNextWrite = false;
        memcpy(store.bytes[slot], bytes, byteCount / 2u);
        store.sizes[slot] = byteCount / 2u;
        store.slotsPresent[slot] = true;
        return StoreWriteResult::IoError;
    }
    memcpy(store.bytes[slot], bytes, byteCount);
    store.sizes[slot] = byteCount;
    store.slotsPresent[slot] = true;
    return StoreWriteResult::Ok;
}

StorageAdapter adapterFor(FakeStore& store)
{
    StorageAdapter adapter{};
    adapter.context = &store;
    adapter.readSlot = readSlot;
    adapter.writeSlot = writeSlot;
    adapter.available = store.available ? 1u : 0u;
    adapter.writable = store.writable ? 1u : 0u;
    return adapter;
}

uint32_t crc32(const uint8_t* bytes, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(0u - (crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void put32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

void put64(uint8_t* out, uint64_t value)
{
    for (uint32_t i = 0; i < 8u; ++i) out[i] = static_cast<uint8_t>(value >> (8u * i));
}

void repairCrc(uint8_t* bytes, size_t length)
{
    put32(bytes + length - 4u, crc32(bytes, length - 4u));
}

} // namespace

int main()
{
    Registry registry;
    UserId id = 999u;
    check(!registry.initialized() && registry.provision("alice", "Alice", &id) == Result::NotInitialized,
        "registry requires explicit initialization");
    registry.initializeEmpty();
    size_t listed = 77u;
    check(registry.enumerate(nullptr, 0u, &listed) == Result::Ok && listed == 0u,
        "initialized registry starts empty");

    UserId alice = kInvalidUserId;
    UserId bob = kInvalidUserId;
    check(registry.provision("alice", "Alice Example", &alice) == Result::Ok &&
        alice == kFirstHumanUserId, "first account receives the reserved-range-safe ID");
    check(registry.provision("bob_2", "Bob Example", &bob) == Result::Ok && bob == alice + 1u,
        "distinct accounts receive distinct monotonically allocated IDs");
    UserRecordV1 found{};
    check(registry.find(alice, &found) == Result::Ok && found.id == alice &&
        strcmp(found.accountName, "alice") == 0 && found.version == kUserRecordVersion,
        "lookup copies the versioned record by numeric ID");
    check(registry.provision("ALICE", "Another Alice", &id) == Result::DuplicateAccountName,
        "account name uniqueness is ASCII case-insensitive");

    check(!validAccountName("") && !validAccountName("-alice") &&
        !validAccountName("two..dots") && !validAccountName("name/part") &&
        !validAccountName("this-account-name-is-far-too-long-for-the-limit"),
        "invalid, unsafe, and overlength account names are rejected");
    check(!validDisplayName("") && !validDisplayName(" Leading") &&
        !validDisplayName("line\nfeed") &&
        !validDisplayName("this display name exceeds the sixty-three byte limit................................"),
        "empty, control-containing, whitespace-padded, and overlength display names are rejected");
    check(registry.provision("valid", "Bad\tName", &id) == Result::InvalidArgument,
        "record creation validates both bounded names");

    check(registry.find(kInvalidUserId, &found) == Result::InvalidId &&
        registry.find(kSystemIdentityId, &found) == Result::InvalidId &&
        registry.find(UINT64_MAX, &found) == Result::InvalidId,
        "invalid, reserved system, and sentinel IDs cannot resolve as human accounts");
    UserRecordV1 oneRecord[1]{};
    check(registry.enumerate(oneRecord, 1u, &listed) == Result::BufferTooSmall && listed == 2u,
        "enumeration reports required bounded capacity");

    check(registry.setStatus(alice, AccountStatus::Disabled) == Result::Ok &&
        registry.find(alice, &found) == Result::Inactive && found.status == AccountStatus::Disabled,
        "disabled lifecycle state remains queryable but is not active");
    check(registry.setStatus(alice, AccountStatus::Active) == Result::Ok &&
        registry.erase(alice) == Result::Ok && registry.find(alice, &found) == Result::NotFound &&
        registry.erase(alice) == Result::NotFound,
        "reactivation and deletion make old references stale without recycling their ID");
    check(registry.provision("alice", "Alice Again", &id) == Result::Ok && id > bob,
        "deleted account names may be reused while deleted IDs are not reused");

    Registry full;
    full.initializeEmpty();
    bool filled = true;
    char account[24]{};
    char display[24]{};
    for (size_t i = 0; i < kMaximumUsers; ++i) {
        account[0] = 'u';
        account[1] = static_cast<char>('0' + (i / 10u));
        account[2] = static_cast<char>('0' + (i % 10u));
        account[3] = '\0';
        display[0] = 'U';
        display[1] = static_cast<char>('0' + (i / 10u));
        display[2] = static_cast<char>('0' + (i % 10u));
        display[3] = '\0';
        if (full.provision(account, display, &id) != Result::Ok) filled = false;
    }
    check(filled && full.count() == kMaximumUsers &&
        full.provision("overflow", "Overflow", &id) == Result::CapacityExceeded,
        "registry capacity is exact and exhaustion does not replace records");

    uint8_t encoded[kMaximumIdentityStoreBytes]{};
    size_t encodedBytes = 0u;
    check(registry.serialize(4u, encoded, sizeof(encoded), &encodedBytes) == Result::Ok &&
        encodedBytes == kIdentityHeaderBytes + registry.count() * kIdentityRecordBytes + 4u,
        "versioned serializer emits bounded records and checksum");
    Registry restored;
    uint64_t generation = 0u;
    check(restored.load(encoded, encodedBytes, &generation) == Result::Ok && generation == 4u &&
        restored.count() == registry.count() && restored.nextUserId() == registry.nextUserId(),
        "validated serialization restores IDs and allocation watermark");
    check(restored.provision("next", "Next User", &id) == Result::Ok &&
        id == registry.nextUserId(), "restart does not reuse a deleted persisted identifier");

    uint8_t corrupt[kMaximumIdentityStoreBytes]{};
    memcpy(corrupt, encoded, encodedBytes);
    corrupt[encodedBytes - 1u] ^= 0x80u;
    Registry badCrc;
    check(badCrc.load(corrupt, encodedBytes, &generation) == Result::CorruptRecord,
        "checksum damage is rejected");
    check(restored.load(encoded, encodedBytes - 1u, &generation) == Result::CorruptRecord,
        "truncated serialization is rejected");
    memcpy(corrupt, encoded, encodedBytes);
    put32(corrupt + 8u, 2u);
    repairCrc(corrupt, encodedBytes);
    check(restored.load(corrupt, encodedBytes, &generation) == Result::UnsupportedVersion,
        "unsupported store versions are rejected explicitly");

    Registry duplicateSource;
    duplicateSource.initializeEmpty();
    UserId first = 0u;
    UserId second = 0u;
    (void)duplicateSource.provision("first", "First", &first);
    (void)duplicateSource.provision("second", "Second", &second);
    (void)duplicateSource.serialize(1u, encoded, sizeof(encoded), &encodedBytes);
    put64(encoded + kIdentityHeaderBytes + kIdentityRecordBytes, first);
    repairCrc(encoded, encodedBytes);
    check(restored.load(encoded, encodedBytes, &generation) == Result::CorruptRecord,
        "duplicate IDs in a stored record set are rejected");
    (void)second;

    FakeStore durableStore;
    StorageAdapter durableAdapter = adapterFor(durableStore);
    Runtime durable;
    check(durable.initialize(&durableAdapter) == Result::Ok,
        "durable runtime initializes over empty writable storage");
    RuntimeSnapshotV1 snapshot{};
    check(durable.snapshot(&snapshot) == Result::Ok && snapshot.initialized &&
        snapshot.persistence == PersistenceState::Persistent && snapshot.accountCount == 0u,
        "empty persistent runtime reports infrastructure and no account records");
    UserId persistedId = 0u;
    check(durable.provision("persisted", "Persisted User", &persistedId) == Result::OkPersistent,
        "successful VFS-style write and read-back reports persisted success");
    Runtime restarted;
    check(restarted.initialize(&durableAdapter) == Result::Ok &&
        restarted.find(persistedId, &found) == Result::Ok &&
        restarted.snapshot(&snapshot) == Result::Ok && snapshot.allRecordsDurable,
        "new runtime restores the committed account and stable ID");

    durableStore.failNextWrite = true;
    check(restarted.provision("will-fail", "Not Committed", &id) == Result::IoError &&
        id == kInvalidUserId && restarted.find(id, &found) == Result::InvalidId,
        "partial write failure leaves the live registry unchanged and reports no ID");
    Runtime afterInterruptedWrite;
    check(afterInterruptedWrite.initialize(&durableAdapter) == Result::StorageCorrupt &&
        afterInterruptedWrite.find(persistedId, &found) == Result::Ok &&
        afterInterruptedWrite.find(kFirstHumanUserId + 1u, &found) == Result::NotFound,
        "valid prior slot survives an interrupted alternate-slot write");
    check(afterInterruptedWrite.snapshot(&snapshot) == Result::Ok &&
        snapshot.accountCountAvailable == 0u,
        "damaged alternate slot prevents a possibly stale fallback count from appearing authoritative");

    FakeStore ambiguousStore;
    Registry ambiguousA;
    Registry ambiguousB;
    ambiguousA.initializeEmpty();
    ambiguousB.initializeEmpty();
    (void)ambiguousA.provision("slot-a", "Slot A", &id);
    (void)ambiguousB.provision("slot-b", "Slot B", &id);
    size_t ambiguousBytes = 0u;
    (void)ambiguousA.serialize(9u, ambiguousStore.bytes[0],
        sizeof(ambiguousStore.bytes[0]), &ambiguousBytes);
    ambiguousStore.sizes[0] = ambiguousBytes;
    ambiguousStore.slotsPresent[0] = true;
    (void)ambiguousB.serialize(9u, ambiguousStore.bytes[1],
        sizeof(ambiguousStore.bytes[1]), &ambiguousBytes);
    ambiguousStore.sizes[1] = ambiguousBytes;
    ambiguousStore.slotsPresent[1] = true;
    StorageAdapter ambiguousAdapter = adapterFor(ambiguousStore);
    Runtime ambiguous;
    check(ambiguous.initialize(&ambiguousAdapter) == Result::StorageCorrupt &&
        ambiguous.snapshot(&snapshot) == Result::Ok && snapshot.accountCountAvailable == 0u &&
        snapshot.accountCount == 0u,
        "conflicting valid slots at one generation report no authoritative account count");

    FakeStore unrelatedStore;
    unrelatedStore.slotsPresent[0] = true;
    unrelatedStore.sizes[0] = 9u;
    memcpy(unrelatedStore.bytes[0], "unrelated", unrelatedStore.sizes[0]);
    StorageAdapter unrelatedAdapter = adapterFor(unrelatedStore);
    Runtime unrelated;
    check(unrelated.initialize(&unrelatedAdapter) == Result::StorageCorrupt &&
        unrelated.provision("replacement", "Replacement", &id) == Result::StorageCorrupt &&
        unrelatedStore.writeCount == 0u && unrelatedStore.sizes[0] == 9u &&
        memcmp(unrelatedStore.bytes[0], "unrelated", 9u) == 0,
        "unrecognized reserved-path data is preserved and never replaced by provisioning");

    FakeStore readOnlyStore;
    readOnlyStore.writable = false;
    StorageAdapter readOnlyAdapter = adapterFor(readOnlyStore);
    Runtime readOnly;
    check(readOnly.initialize(&readOnlyAdapter) == Result::Ok &&
        readOnly.provision("volatile", "Volatile User", &id) == Result::OkVolatile &&
        readOnly.snapshot(&snapshot) == Result::Ok &&
        snapshot.persistence == PersistenceState::ReadOnly && !snapshot.allRecordsDurable,
        "read-only storage permits explicitly volatile records without claiming persistence");
    Runtime readOnlyRestart;
    check(readOnlyRestart.initialize(&readOnlyAdapter) == Result::Ok &&
        readOnlyRestart.find(id, &found) == Result::NotFound,
        "volatile account records do not appear after restart");

    FakeStore unavailableStore;
    unavailableStore.available = false;
    StorageAdapter unavailableAdapter = adapterFor(unavailableStore);
    Runtime unavailable;
    check(unavailable.initialize(&unavailableAdapter) == Result::Ok &&
        unavailable.provision("temporary", "Temporary User", &id) == Result::OkVolatile &&
        unavailable.snapshot(&snapshot) == Result::Ok &&
        snapshot.persistence == PersistenceState::Unavailable && !snapshot.allRecordsDurable,
        "unavailable storage keeps boot and in-memory provisioning available with honest status");

    Runtime contexts;
    check(contexts.initialize(nullptr) == Result::Ok,
        "registry initializes when persistent storage is absent");
    IdentityContextV1 context{};
    check(contexts.queryContext(ExecutionDomain::Kernel, &context) == Result::Ok &&
        context.kind == IdentityKind::KernelSystem && context.userId == kSystemIdentityId,
        "kernel system context is a reserved nonhuman identity");
    check(contexts.queryContext(ExecutionDomain::Interactive, &context) == Result::Ok &&
        context.kind == IdentityKind::InteractiveUnassigned && context.userId == kInvalidUserId,
        "interactive execution reports no assigned human identity without authentication");
    check(contexts.queryContext(static_cast<ExecutionDomain>(0), &context) == Result::InvalidArgument,
        "unsupported execution domains return a clear error");

    std::cout << "Identity foundation checks: " << (checks - failures) << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
