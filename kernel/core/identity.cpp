#include "include/kernel/identity.h"

#include <string.h>

namespace kernel {
namespace identity {
namespace {

const uint8_t kStoreMagic[8] = { 'G', 'X', 'O', 'S', 'I', 'D', '0', '1' };
const uint32_t kStoreVersion = 2u;
const uint32_t kLegacyStoreVersion = 1u;
const uint32_t kStoreHeaderBytes = static_cast<uint32_t>(kIdentityHeaderBytes);
const uint32_t kStoreRecordBytes = static_cast<uint32_t>(kIdentityRecordBytes);
const uint32_t kStoreCrcBytes = 4u;

size_t boundedLength(const char* value, size_t capacity)
{
    if (!value) return capacity;
    size_t length = 0;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

uint8_t asciiLower(uint8_t value)
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<uint8_t>(value + ('a' - 'A')) : value;
}

bool accountNamesEqual(const char* left, const char* right)
{
    size_t index = 0;
    while (left[index] && right[index]) {
        if (asciiLower(static_cast<uint8_t>(left[index])) !=
            asciiLower(static_cast<uint8_t>(right[index]))) return false;
        ++index;
    }
    return left[index] == right[index];
}

bool validAccountArray(const char* value)
{
    const size_t length = boundedLength(value, kAccountNameCapacity);
    if (length == 0 || length >= kAccountNameCapacity ||
        !validAccountName(value)) return false;
    for (size_t i = length + 1; i < kAccountNameCapacity; ++i) {
        if (value[i] != '\0') return false;
    }
    return true;
}

bool validDisplayArray(const char* value)
{
    const size_t length = boundedLength(value, kDisplayNameCapacity);
    if (length == 0 || length >= kDisplayNameCapacity ||
        !validDisplayName(value)) return false;
    for (size_t i = length + 1; i < kDisplayNameCapacity; ++i) {
        if (value[i] != '\0') return false;
    }
    return true;
}

void put32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

void put64(uint8_t* output, uint64_t value)
{
    for (uint32_t i = 0; i < 8u; ++i)
        output[i] = static_cast<uint8_t>(value >> (i * 8u));
}

uint32_t get32(const uint8_t* input)
{
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

uint64_t get64(const uint8_t* input)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8u; ++i)
        value |= static_cast<uint64_t>(input[i]) << (i * 8u);
    return value;
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

bool validStatus(AccountStatus status)
{
    return status == AccountStatus::Active || status == AccountStatus::Disabled;
}

bool validRole(AccountRole role)
{
    return role == AccountRole::StandardUser || role == AccountRole::Administrator;
}

size_t activeAdministratorCount(const UserRecordV1* records, size_t count)
{
    size_t administrators = 0u;
    for (size_t i = 0u; i < count; ++i) {
        if (records[i].status == AccountStatus::Active &&
            records[i].role == AccountRole::Administrator) ++administrators;
    }
    return administrators;
}

} // namespace

bool validAccountName(const char* value)
{
    if (!value) return false;
    const size_t length = boundedLength(value, kAccountNameCapacity);
    if (length == 0 || length >= kAccountNameCapacity) return false;
    const uint8_t first = static_cast<uint8_t>(value[0]);
    const uint8_t last = static_cast<uint8_t>(value[length - 1]);
    const bool firstAlphaNumeric = (first >= 'a' && first <= 'z') ||
        (first >= 'A' && first <= 'Z') || (first >= '0' && first <= '9');
    const bool lastAlphaNumeric = (last >= 'a' && last <= 'z') ||
        (last >= 'A' && last <= 'Z') || (last >= '0' && last <= '9');
    if (!firstAlphaNumeric || !lastAlphaNumeric) return false;

    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        const bool valid = (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-';
        if (!valid || (c == '.' && i > 0 && value[i - 1] == '.')) return false;
    }
    return true;
}

bool validDisplayName(const char* value)
{
    if (!value) return false;
    const size_t length = boundedLength(value, kDisplayNameCapacity);
    if (length == 0 || length >= kDisplayNameCapacity ||
        value[0] == ' ' || value[length - 1] == ' ') return false;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        if (c < 0x20u || c > 0x7Eu) return false;
    }
    return true;
}

bool validAccountRole(AccountRole role) { return validRole(role); }

Registry::Registry()
    : m_records{}, m_count(0), m_nextUserId(kFirstHumanUserId), m_initialized(false)
{
}

void Registry::initializeEmpty()
{
    memset(m_records, 0, sizeof(m_records));
    m_count = 0;
    m_nextUserId = kFirstHumanUserId;
    m_initialized = true;
}

bool Registry::initialized() const { return m_initialized; }
size_t Registry::count() const { return m_count; }
UserId Registry::nextUserId() const { return m_nextUserId; }

Result Registry::provision(const char* accountName, const char* displayName, UserId* outId)
{
    if (outId) *outId = kInvalidUserId;
    if (!m_initialized) return Result::NotInitialized;
    if (!outId || !validAccountName(accountName) || !validDisplayName(displayName))
        return Result::InvalidArgument;
    if (m_count >= kMaximumUsers) return Result::CapacityExceeded;
    if (m_nextUserId < kFirstHumanUserId || m_nextUserId > kMaximumUserId)
        return Result::IdExhausted;

    for (size_t i = 0; i < m_count; ++i) {
        if (accountNamesEqual(m_records[i].accountName, accountName))
            return Result::DuplicateAccountName;
    }

    UserRecordV1& record = m_records[m_count];
    memset(&record, 0, sizeof(record));
    record.version = kUserRecordVersion;
    record.id = m_nextUserId;
    record.status = AccountStatus::Active;
    record.role = AccountRole::StandardUser;
    const size_t accountLength = boundedLength(accountName, kAccountNameCapacity);
    const size_t displayLength = boundedLength(displayName, kDisplayNameCapacity);
    memcpy(record.accountName, accountName, accountLength);
    memcpy(record.displayName, displayName, displayLength);
    ++m_count;
    ++m_nextUserId;
    *outId = record.id;
    return Result::Ok;
}

Result Registry::find(UserId id, UserRecordV1* output) const
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output) return Result::InvalidArgument;
    if (id == kInvalidUserId || id == kSystemIdentityId || id < kFirstHumanUserId ||
        id > kMaximumUserId) return Result::InvalidId;
    for (size_t i = 0; i < m_count; ++i) {
        if (m_records[i].id == id) {
            *output = m_records[i];
            return m_records[i].status == AccountStatus::Active
                ? Result::Ok : Result::Inactive;
        }
    }
    return Result::NotFound;
}

Result Registry::findRecord(UserId id, UserRecordV1* output) const
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output) return Result::InvalidArgument;
    if (id == kInvalidUserId || id == kSystemIdentityId || id < kFirstHumanUserId ||
        id > kMaximumUserId) return Result::InvalidId;
    for (size_t i = 0u; i < m_count; ++i) {
        if (m_records[i].id == id) {
            *output = m_records[i];
            return m_records[i].status == AccountStatus::Active
                ? Result::Ok : Result::Inactive;
        }
    }
    return Result::NotFound;
}

Result Registry::findByAccountName(const char* accountName, UserRecordV1* output) const
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output || !validAccountName(accountName)) return Result::InvalidArgument;
    for (size_t i = 0u; i < m_count; ++i) {
        if (accountNamesEqual(m_records[i].accountName, accountName)) {
            *output = m_records[i];
            return m_records[i].status == AccountStatus::Active
                ? Result::Ok : Result::Inactive;
        }
    }
    return Result::NotFound;
}

Result Registry::enumerate(UserRecordV1* output, size_t capacity, size_t* outCount) const
{
    if (outCount) *outCount = 0;
    if (!m_initialized) return Result::NotInitialized;
    if (!outCount || (capacity != 0 && !output)) return Result::InvalidArgument;
    *outCount = m_count;
    if (capacity < m_count) return Result::BufferTooSmall;
    for (size_t i = 0; i < m_count; ++i) output[i] = m_records[i];
    return Result::Ok;
}

Result Registry::setStatus(UserId id, AccountStatus status)
{
    if (!m_initialized) return Result::NotInitialized;
    if (id == kInvalidUserId || id == kSystemIdentityId || id < kFirstHumanUserId ||
        id > kMaximumUserId) return Result::InvalidId;
    if (!validStatus(status)) return Result::InvalidArgument;
    for (size_t i = 0; i < m_count; ++i) {
        if (m_records[i].id == id) {
            if (m_records[i].role == AccountRole::Administrator &&
                status != AccountStatus::Active &&
                activeAdministratorCount(m_records, m_count) <= 1u)
                return Result::LastAdministrator;
            m_records[i].status = status;
            return Result::Ok;
        }
    }
    return Result::NotFound;
}

Result Registry::setRole(UserId id, AccountRole role)
{
    if (!m_initialized) return Result::NotInitialized;
    if (id == kInvalidUserId || id == kSystemIdentityId || id < kFirstHumanUserId ||
        id > kMaximumUserId) return Result::InvalidId;
    if (!validRole(role)) return Result::InvalidRole;
    for (size_t i = 0u; i < m_count; ++i) {
        if (m_records[i].id != id) continue;
        if (m_records[i].status == AccountStatus::Active &&
            m_records[i].role == AccountRole::Administrator &&
            role != AccountRole::Administrator &&
            activeAdministratorCount(m_records, m_count) <= 1u)
            return Result::LastAdministrator;
        m_records[i].role = role;
        return Result::Ok;
    }
    return Result::NotFound;
}

Result Registry::getRole(UserId id, AccountRole* output) const
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output) return Result::InvalidArgument;
    if (id == kInvalidUserId || id == kSystemIdentityId || id < kFirstHumanUserId ||
        id > kMaximumUserId) return Result::InvalidId;
    for (size_t i = 0u; i < m_count; ++i) {
        if (m_records[i].id != id) continue;
        *output = m_records[i].role;
        return m_records[i].status == AccountStatus::Active
            ? Result::Ok : Result::Inactive;
    }
    return Result::NotFound;
}

bool Registry::isAdministrator(UserId id) const
{
    if (!m_initialized) return false;
    for (size_t i = 0u; i < m_count; ++i)
        if (m_records[i].id == id) return m_records[i].role == AccountRole::Administrator;
    return false;
}

Result Registry::erase(UserId id)
{
    if (!m_initialized) return Result::NotInitialized;
    if (id == kInvalidUserId || id == kSystemIdentityId || id < kFirstHumanUserId ||
        id > kMaximumUserId) return Result::InvalidId;
    for (size_t i = 0; i < m_count; ++i) {
        if (m_records[i].id != id) continue;
        if (m_records[i].status == AccountStatus::Active &&
            m_records[i].role == AccountRole::Administrator &&
            activeAdministratorCount(m_records, m_count) <= 1u)
            return Result::LastAdministrator;
        for (size_t move = i + 1; move < m_count; ++move)
            m_records[move - 1] = m_records[move];
        memset(&m_records[m_count - 1], 0, sizeof(m_records[m_count - 1]));
        --m_count;
        return Result::Ok;
    }
    return Result::NotFound;
}

Result Registry::serialize(uint64_t generation, uint8_t* output, size_t capacity,
                           size_t* outBytes) const
{
    if (outBytes) *outBytes = 0;
    if (!m_initialized) return Result::NotInitialized;
    if (!output || !outBytes || generation == 0u) return Result::InvalidArgument;
    const size_t total = kIdentityHeaderBytes + m_count * kIdentityRecordBytes + kStoreCrcBytes;
    if (capacity < total) return Result::BufferTooSmall;

    memcpy(output, kStoreMagic, sizeof(kStoreMagic));
    put32(output + 8, kStoreVersion);
    put32(output + 12, kStoreHeaderBytes);
    put64(output + 16, generation);
    put64(output + 24, m_nextUserId);
    put32(output + 32, static_cast<uint32_t>(m_count));
    put32(output + 36, kStoreRecordBytes);

    size_t offset = kIdentityHeaderBytes;
    for (size_t i = 0; i < m_count; ++i) {
        const UserRecordV1& record = m_records[i];
        put64(output + offset, record.id);
        output[offset + 8] = static_cast<uint8_t>(record.status);
        output[offset + 9] = static_cast<uint8_t>(record.role);
        memcpy(output + offset + 10, record.accountName, kAccountNameCapacity);
        memcpy(output + offset + 10 + kAccountNameCapacity,
               record.displayName, kDisplayNameCapacity);
        offset += kIdentityRecordBytes;
    }
    put32(output + offset, crc32(output, offset));
    *outBytes = total;
    return Result::Ok;
}

Result Registry::load(const uint8_t* bytes, size_t byteCount, uint64_t* outGeneration)
{
    if (outGeneration) *outGeneration = 0;
    if (!bytes || !outGeneration || byteCount < kIdentityHeaderBytes + kStoreCrcBytes)
        return Result::CorruptRecord;
    if (memcmp(bytes, kStoreMagic, sizeof(kStoreMagic)) != 0) return Result::CorruptRecord;
    const uint32_t version = get32(bytes + 8);
    if (version != kStoreVersion && version != kLegacyStoreVersion)
        return Result::UnsupportedVersion;
    const uint32_t expectedRecordBytes = version == kLegacyStoreVersion
        ? static_cast<uint32_t>(kLegacyIdentityRecordBytes) : kStoreRecordBytes;
    if (get32(bytes + 12) != kStoreHeaderBytes || get32(bytes + 36) != expectedRecordBytes)
        return Result::CorruptRecord;

    const uint64_t generation = get64(bytes + 16);
    const UserId nextId = get64(bytes + 24);
    const uint32_t count = get32(bytes + 32);
    if (generation == 0u || count > kMaximumUsers || nextId < kFirstHumanUserId)
        return Result::CorruptRecord;
    const size_t recordBytes = version == kLegacyStoreVersion
        ? kLegacyIdentityRecordBytes : kIdentityRecordBytes;
    const size_t expected = kIdentityHeaderBytes +
        static_cast<size_t>(count) * recordBytes + kStoreCrcBytes;
    if (expected != byteCount || get32(bytes + byteCount - kStoreCrcBytes) !=
        crc32(bytes, byteCount - kStoreCrcBytes)) return Result::CorruptRecord;

    Registry candidate;
    candidate.initializeEmpty();
    candidate.m_count = count;
    candidate.m_nextUserId = nextId;
    size_t offset = kIdentityHeaderBytes;
    UserId previousId = kInvalidUserId;
    for (uint32_t i = 0; i < count; ++i) {
        UserRecordV1& record = candidate.m_records[i];
        memset(&record, 0, sizeof(record));
        record.version = kUserRecordVersion;
        record.id = get64(bytes + offset);
        record.status = static_cast<AccountStatus>(bytes[offset + 8]);
        record.role = version == kLegacyStoreVersion
            ? AccountRole::StandardUser
            : static_cast<AccountRole>(bytes[offset + 9]);
        const size_t nameOffset = version == kLegacyStoreVersion ? 9u : 10u;
        memcpy(record.accountName, bytes + offset + nameOffset, kAccountNameCapacity);
        memcpy(record.displayName, bytes + offset + nameOffset + kAccountNameCapacity,
               kDisplayNameCapacity);
        if (record.id < kFirstHumanUserId || record.id > kMaximumUserId ||
            record.id <= previousId || !validStatus(record.status) || !validRole(record.role) ||
            !validAccountArray(record.accountName) || !validDisplayArray(record.displayName))
            return Result::CorruptRecord;
        for (uint32_t prior = 0; prior < i; ++prior) {
            if (accountNamesEqual(candidate.m_records[prior].accountName, record.accountName))
                return Result::CorruptRecord;
        }
        previousId = record.id;
        offset += recordBytes;
    }
    if (count != 0u && nextId <= previousId) return Result::CorruptRecord;

    *this = candidate;
    *outGeneration = generation;
    return Result::Ok;
}

const char* resultName(Result result)
{
    switch (result) {
    case Result::Ok: return "OK";
    case Result::OkPersistent: return "OK_PERSISTENT";
    case Result::OkVolatile: return "OK_VOLATILE";
    case Result::NotInitialized: return "NOT_INITIALIZED";
    case Result::InvalidArgument: return "INVALID_ARGUMENT";
    case Result::InvalidId: return "INVALID_ID";
    case Result::InvalidRole: return "INVALID_ROLE";
    case Result::LastAdministrator: return "LAST_ADMINISTRATOR";
    case Result::DuplicateId: return "DUPLICATE_ID";
    case Result::DuplicateAccountName: return "DUPLICATE_ACCOUNT_NAME";
    case Result::NotFound: return "NOT_FOUND";
    case Result::Inactive: return "INACTIVE";
    case Result::CapacityExceeded: return "CAPACITY_EXCEEDED";
    case Result::IdExhausted: return "ID_EXHAUSTED";
    case Result::BufferTooSmall: return "BUFFER_TOO_SMALL";
    case Result::CorruptRecord: return "CORRUPT_RECORD";
    case Result::UnsupportedVersion: return "UNSUPPORTED_VERSION";
    case Result::StorageUnavailable: return "STORAGE_UNAVAILABLE";
    case Result::StorageCorrupt: return "STORAGE_CORRUPT";
    case Result::IoError: return "IO_ERROR";
    case Result::GenerationExhausted: return "GENERATION_EXHAUSTED";
    case Result::Unauthorized: return "UNAUTHORIZED";
    default: return "UNKNOWN";
    }
}

const char* persistenceStateName(PersistenceState state)
{
    switch (state) {
    case PersistenceState::Persistent: return "persistent";
    case PersistenceState::Volatile: return "volatile";
    case PersistenceState::ReadOnly: return "read-only";
    case PersistenceState::Unavailable: return "unavailable";
    case PersistenceState::Corrupt: return "corrupt";
    case PersistenceState::Unsupported: return "unsupported-version";
    default: return "unknown";
    }
}

} // namespace identity
} // namespace kernel
