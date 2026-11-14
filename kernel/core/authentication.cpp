#include "include/kernel/authentication.h"

#include <string.h>

namespace kernel {
namespace authentication {
namespace {

static const uint8_t kStoreMagic[8] = { 'G', 'X', 'O', 'S', 'C', 'R', 'D', '1' };
static const size_t kStoreHeaderBytes = 32u;
static const size_t kStoreRecordBytes = 64u;
static const size_t kStoreCrcBytes = 4u;
static const uint32_t kStoreVersion = 1u;
static const uint32_t kMinimumIterations = 600000u;
static const uint32_t kMaximumIterations = 1200000u;
static const uint64_t kMaximumBackoffTicks = 3000u;
static const uint64_t kInitialBackoffTicks = 500u;
static const uint32_t kFailuresBeforeBackoff = 5u;

Runtime s_runtime;

void secureZero(void* buffer, size_t bytes)
{
    volatile uint8_t* output = static_cast<volatile uint8_t*>(buffer);
    while (bytes-- != 0u) *output++ = 0u;
}

void put16(uint8_t* output, uint16_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8u);
}

void put32(uint8_t* output, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; ++i)
        output[i] = static_cast<uint8_t>(value >> (8u * i));
}

void put64(uint8_t* output, uint64_t value)
{
    for (uint32_t i = 0u; i < 8u; ++i)
        output[i] = static_cast<uint8_t>(value >> (8u * i));
}

uint16_t get16(const uint8_t* input)
{
    return static_cast<uint16_t>(input[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(input[1]) << 8u);
}

uint32_t get32(const uint8_t* input)
{
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8u) |
        (static_cast<uint32_t>(input[2]) << 16u) |
        (static_cast<uint32_t>(input[3]) << 24u);
}

uint64_t get64(const uint8_t* input)
{
    uint64_t value = 0u;
    for (uint32_t i = 0u; i < 8u; ++i)
        value |= static_cast<uint64_t>(input[i]) << (8u * i);
    return value;
}

uint32_t crc32(const uint8_t* bytes, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(0u - (crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool timingSafeEqual(const uint8_t* left, const uint8_t* right, size_t length)
{
    uint8_t difference = 0u;
    for (size_t i = 0u; i < length; ++i) difference |= left[i] ^ right[i];
    return difference == 0u;
}

bool validPasswordInput(const char* password, size_t passwordBytes)
{
    if (!password || passwordBytes < 12u || passwordBytes > kMaximumPasswordBytes)
        return false;
    for (size_t i = 0u; i < passwordBytes; ++i) {
        const uint8_t c = static_cast<uint8_t>(password[i]);
        if (c < 0x20u || c > 0x7Eu) return false;
    }
    return true;
}

bool sameCredential(const Runtime::CredentialRecord& left,
                    const Runtime::CredentialRecord& right)
{
    return left.userId == right.userId && left.formatVersion == right.formatVersion &&
        left.algorithm == right.algorithm && left.iterations == right.iterations &&
        timingSafeEqual(left.salt, right.salt, sizeof(left.salt)) &&
        timingSafeEqual(left.verifier, right.verifier, sizeof(left.verifier));
}

Result mapIdentityResult(identity::Result result)
{
    switch (result) {
    case identity::Result::Ok: return Result::Ok;
    case identity::Result::OkPersistent: return Result::OkPersistent;
    case identity::Result::OkVolatile: return Result::OkVolatile;
    case identity::Result::InvalidArgument: return Result::InvalidArgument;
    case identity::Result::LastAdministrator: return Result::LastAdministrator;
    case identity::Result::CapacityExceeded:
    case identity::Result::IdExhausted:
    case identity::Result::BufferTooSmall: return Result::CapacityExceeded;
    case identity::Result::StorageUnavailable: return Result::StorageUnavailable;
    case identity::Result::StorageCorrupt:
    case identity::Result::CorruptRecord: return Result::StorageCorrupt;
    case identity::Result::UnsupportedVersion: return Result::UnsupportedIdentityFormat;
    case identity::Result::IoError:
    case identity::Result::GenerationExhausted: return Result::IoError;
    default: return Result::AccountUnavailable;
    }
}

} // namespace

Runtime::Runtime()
    : m_identities(nullptr), m_storage{}, m_crypto{}, m_credentials{},
      m_credentialCount(0u), m_generation(0u), m_activeSlot(-1),
      m_persistence(identity::PersistenceState::Unavailable), m_initialized(false),
      m_identityRegistryReadable(false), m_identityRecordsDurable(false),
      m_storeReadable(false), m_storeWritable(false), m_bootstrapRequired(true),
      m_sessions{}, m_currentSession(-1), m_attempts{}, m_nextAttemptSlot(0u),
      m_slotBytes{}, m_writeBytes{}, m_verifyBytes{}
{
}

Result Runtime::initialize(identity::Runtime* identities,
                            const StorageAdapter* storage,
                            const CryptoProvider* crypto,
                            bool bindInteractiveContext)
{
    if (!identities || !crypto || !crypto->randomBytes ||
        !crypto->deriveVerifier || !crypto->clockTicks) return Result::InvalidArgument;
    m_identities = identities;
    m_storage = storage ? *storage : StorageAdapter{};
    m_crypto = *crypto;
    m_credentialCount = 0u;
    memset(m_credentials, 0, sizeof(m_credentials));
    memset(m_sessions, 0, sizeof(m_sessions));
    memset(m_attempts, 0, sizeof(m_attempts));
    m_currentSession = -1;
    m_nextAttemptSlot = 0u;
    m_generation = 0u;
    m_activeSlot = -1;
    m_initialized = true;
    identity::RuntimeSnapshotV1 identitySnapshot{};
    m_identityRegistryReadable = m_identities->snapshot(&identitySnapshot) == identity::Result::Ok &&
        identitySnapshot.initialized != 0u && identitySnapshot.accountCountAvailable != 0u;
    m_identityRecordsDurable = identitySnapshot.allRecordsDurable != 0u;
    m_storeReadable = true;
    m_storeWritable = m_storage.available != 0u && m_storage.writable != 0u &&
        m_storage.readSlot && m_storage.writeSlot;
    m_persistence = identity::PersistenceState::Unavailable;

    if (!m_storage.available || !m_storage.readSlot) {
        m_storeWritable = false;
        m_persistence = identity::PersistenceState::Unavailable;
        m_bootstrapRequired = true;
        if (bindInteractiveContext)
            m_identities->setInteractiveContextProvider(this,
                [](void* context, identity::IdentityContextV1* output) -> identity::Result {
                    return static_cast<Runtime*>(context)->queryContext(
                        identity::ExecutionDomain::Interactive, output) == Result::Ok
                        ? identity::Result::Ok : identity::Result::NotInitialized;
                });
        return Result::Ok;
    }

    struct DecodedSlot {
        Result result;
        uint64_t generation;
        size_t byteCount;
    } slots[2] = {
        { Result::StorageUnavailable, 0u, 0u },
        { Result::StorageUnavailable, 0u, 0u }
    };
    bool sawCorrupt = false;
    bool sawUnsupported = false;
    bool sawIo = false;
    bool sawUnavailable = false;
    for (uint8_t slot = 0u; slot < 2u; ++slot) {
        size_t byteCount = 0u;
        const StoreReadResult read = m_storage.readSlot(m_storage.context, slot,
            m_slotBytes[slot], sizeof(m_slotBytes[slot]), &byteCount);
        slots[slot].byteCount = byteCount;
        if (read == StoreReadResult::Missing) {
            slots[slot].result = Result::CredentialMissing;
            continue;
        }
        if (read == StoreReadResult::Unavailable) {
            slots[slot].result = Result::StorageUnavailable;
            sawUnavailable = true;
            continue;
        }
        if (read == StoreReadResult::IoError || byteCount == 0u ||
            byteCount > sizeof(m_slotBytes[slot])) {
            slots[slot].result = Result::IoError;
            sawIo = true;
            continue;
        }
        if (read == StoreReadResult::Corrupt || byteCount < kStoreHeaderBytes + kStoreCrcBytes ||
            memcmp(m_slotBytes[slot], kStoreMagic, sizeof(kStoreMagic)) != 0) {
            slots[slot].result = Result::StorageCorrupt;
            sawCorrupt = true;
            continue;
        }
        const uint8_t* bytes = m_slotBytes[slot];
        if (get32(bytes + 8u) != kStoreVersion) {
            slots[slot].result = Result::UnsupportedCredentialFormat;
            sawUnsupported = true;
            continue;
        }
        const uint32_t count = get32(bytes + 24u);
        const size_t expectedBytes = kStoreHeaderBytes +
            static_cast<size_t>(count) * kStoreRecordBytes + kStoreCrcBytes;
        if (get32(bytes + 12u) != kStoreHeaderBytes ||
            get32(bytes + 28u) != kStoreRecordBytes ||
            count > identity::kMaximumUsers || expectedBytes != byteCount ||
            get32(bytes + byteCount - kStoreCrcBytes) !=
                crc32(bytes, byteCount - kStoreCrcBytes)) {
            slots[slot].result = Result::StorageCorrupt;
            sawCorrupt = true;
            continue;
        }
        const uint64_t generation = get64(bytes + 16u);
        if (generation == 0u) {
            slots[slot].result = Result::StorageCorrupt;
            sawCorrupt = true;
            continue;
        }
        CredentialRecord decoded[identity::kMaximumUsers]{};
        bool malformed = false;
        bool unsupported = false;
        size_t offset = kStoreHeaderBytes;
        for (uint32_t i = 0u; i < count; ++i) {
            CredentialRecord& record = decoded[i];
            record.userId = get64(bytes + offset);
            record.formatVersion = get16(bytes + offset + 8u);
            record.algorithm = static_cast<Algorithm>(get16(bytes + offset + 10u));
            record.iterations = get32(bytes + offset + 12u);
            memcpy(record.salt, bytes + offset + 16u, sizeof(record.salt));
            memcpy(record.verifier, bytes + offset + 32u, sizeof(record.verifier));
            record.durable = 1u;
            if (record.formatVersion != kCredentialRecordFormatVersion ||
                record.algorithm != Algorithm::Pbkdf2HmacSha256 ||
                record.iterations < kMinimumIterations ||
                record.iterations > kMaximumIterations) unsupported = true;
            if (record.userId < identity::kFirstHumanUserId ||
                record.userId > identity::kMaximumUserId ||
                (i != 0u && decoded[i - 1u].userId >= record.userId)) malformed = true;
            offset += kStoreRecordBytes;
        }
        if (unsupported) {
            slots[slot].result = Result::UnsupportedCredentialFormat;
            sawUnsupported = true;
            continue;
        }
        if (malformed) {
            slots[slot].result = Result::StorageCorrupt;
            sawCorrupt = true;
            continue;
        }
        slots[slot].result = Result::Ok;
        slots[slot].generation = generation;
    }

    const bool valid0 = slots[0].result == Result::Ok;
    const bool valid1 = slots[1].result == Result::Ok;
    if (valid0 && valid1 && slots[0].generation == slots[1].generation) {
        if (slots[0].byteCount != slots[1].byteCount ||
            memcmp(m_slotBytes[0], m_slotBytes[1], slots[0].byteCount) != 0) {
            sawCorrupt = true;
        } else {
            m_activeSlot = 0;
            m_generation = slots[0].generation;
            const uint32_t count = get32(m_slotBytes[0] + 24u);
            m_credentialCount = count;
            size_t offset = kStoreHeaderBytes;
            for (size_t i = 0u; i < m_credentialCount; ++i) {
                CredentialRecord& record = m_credentials[i];
                record.userId = get64(m_slotBytes[0] + offset);
                record.formatVersion = get16(m_slotBytes[0] + offset + 8u);
                record.algorithm = static_cast<Algorithm>(get16(m_slotBytes[0] + offset + 10u));
                record.iterations = get32(m_slotBytes[0] + offset + 12u);
                memcpy(record.salt, m_slotBytes[0] + offset + 16u, sizeof(record.salt));
                memcpy(record.verifier, m_slotBytes[0] + offset + 32u, sizeof(record.verifier));
                record.durable = 1u;
                offset += kStoreRecordBytes;
            }
        }
    } else if (valid0 && (!valid1 || slots[0].generation > slots[1].generation)) {
        m_activeSlot = 0;
        m_generation = slots[0].generation;
        const uint32_t count = get32(m_slotBytes[0] + 24u);
        m_credentialCount = count;
        size_t offset = kStoreHeaderBytes;
        for (size_t i = 0u; i < m_credentialCount; ++i) {
            CredentialRecord& record = m_credentials[i];
            record.userId = get64(m_slotBytes[0] + offset);
            record.formatVersion = get16(m_slotBytes[0] + offset + 8u);
            record.algorithm = static_cast<Algorithm>(get16(m_slotBytes[0] + offset + 10u));
            record.iterations = get32(m_slotBytes[0] + offset + 12u);
            memcpy(record.salt, m_slotBytes[0] + offset + 16u, sizeof(record.salt));
            memcpy(record.verifier, m_slotBytes[0] + offset + 32u, sizeof(record.verifier));
            record.durable = 1u;
            offset += kStoreRecordBytes;
        }
    } else if (valid1) {
        m_activeSlot = 1;
        m_generation = slots[1].generation;
        const uint32_t count = get32(m_slotBytes[1] + 24u);
        m_credentialCount = count;
        size_t offset = kStoreHeaderBytes;
        for (size_t i = 0u; i < m_credentialCount; ++i) {
            CredentialRecord& record = m_credentials[i];
            record.userId = get64(m_slotBytes[1] + offset);
            record.formatVersion = get16(m_slotBytes[1] + offset + 8u);
            record.algorithm = static_cast<Algorithm>(get16(m_slotBytes[1] + offset + 10u));
            record.iterations = get32(m_slotBytes[1] + offset + 12u);
            memcpy(record.salt, m_slotBytes[1] + offset + 16u, sizeof(record.salt));
            memcpy(record.verifier, m_slotBytes[1] + offset + 32u, sizeof(record.verifier));
            record.durable = 1u;
            offset += kStoreRecordBytes;
        }
    }

    if (sawUnsupported) {
        m_storeReadable = false;
        m_persistence = identity::PersistenceState::Unsupported;
    } else if (sawCorrupt) {
        // Credential fallback can revive an old password after a replacement
        // or revocation. Keep any decoded bytes for diagnostics only and fail
        // closed for every authentication and credential mutation.
        m_storeReadable = false;
        m_persistence = identity::PersistenceState::Corrupt;
    } else if (sawIo || sawUnavailable) {
        m_storeReadable = false;
        m_persistence = identity::PersistenceState::Unavailable;
    } else {
        m_persistence = m_storage.writable
            ? identity::PersistenceState::Persistent : identity::PersistenceState::ReadOnly;
    }
    m_storeWritable = m_storeReadable && m_storage.writable && m_storage.writeSlot;

    identity::UserRecordV1 accounts[identity::kMaximumUsers]{};
    size_t accountCount = 0u;
    bool activeAdministrator = false;
    if (m_identities->enumerate(accounts, identity::kMaximumUsers, &accountCount) ==
        identity::Result::Ok) {
        for (size_t i = 0u; i < accountCount; ++i) {
            if (m_storeReadable && accounts[i].status == identity::AccountStatus::Active &&
                accounts[i].role == identity::AccountRole::Administrator &&
                findCredential(accounts[i].id, nullptr) == Result::Ok) {
                activeAdministrator = true;
                break;
            }
        }
    }
    m_bootstrapRequired = !activeAdministrator;
    if (bindInteractiveContext)
        m_identities->setInteractiveContextProvider(this,
            [](void* context, identity::IdentityContextV1* output) -> identity::Result {
                return static_cast<Runtime*>(context)->queryContext(
                    identity::ExecutionDomain::Interactive, output) == Result::Ok
                    ? identity::Result::Ok : identity::Result::NotInitialized;
            });
    secureZero(m_slotBytes, sizeof(m_slotBytes));
    if (!m_storeReadable) {
        secureZero(m_credentials, sizeof(m_credentials));
        m_credentialCount = 0u;
    }
    return m_storeReadable ? Result::Ok :
        (m_persistence == identity::PersistenceState::Unsupported
            ? Result::UnsupportedCredentialFormat
            : (m_persistence == identity::PersistenceState::Corrupt
                ? Result::StorageCorrupt : Result::StorageUnavailable));
}

bool Runtime::initialized() const { return m_initialized; }

bool Runtime::credentialRecordValid(const CredentialRecord& credential) const
{
    return credential.userId >= identity::kFirstHumanUserId &&
        credential.userId <= identity::kMaximumUserId &&
        credential.formatVersion == kCredentialRecordFormatVersion &&
        credential.algorithm == Algorithm::Pbkdf2HmacSha256 &&
        credential.iterations >= kMinimumIterations &&
        credential.iterations <= kMaximumIterations;
}

Result Runtime::findCredential(identity::UserId userId, size_t* index) const
{
    if (index) *index = 0u;
    for (size_t i = 0u; i < m_credentialCount; ++i) {
        if (m_credentials[i].userId == userId) {
            if (index) *index = i;
            return Result::Ok;
        }
    }
    return Result::CredentialMissing;
}

Result Runtime::prepareCredential(const char* password, size_t passwordBytes,
                                  CredentialRecord* output)
{
    if (!output || !validPasswordInput(password, passwordBytes)) return Result::InvalidArgument;
    CredentialRecord candidate{};
    candidate.formatVersion = kCredentialRecordFormatVersion;
    candidate.algorithm = Algorithm::Pbkdf2HmacSha256;
    candidate.iterations = kPbkdf2HmacSha256Iterations;
    bool uniqueSalt = false;
    for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        if (!m_crypto.randomBytes(m_crypto.context, candidate.salt, sizeof(candidate.salt))) {
            secureZero(&candidate, sizeof(candidate));
            return Result::EntropyUnavailable;
        }
        uniqueSalt = true;
        for (size_t i = 0u; i < m_credentialCount; ++i)
            if (timingSafeEqual(candidate.salt, m_credentials[i].salt,
                    sizeof(candidate.salt))) uniqueSalt = false;
        if (uniqueSalt) break;
    }
    if (!uniqueSalt) {
        secureZero(&candidate, sizeof(candidate));
        return Result::EntropyUnavailable;
    }
    if (!m_crypto.deriveVerifier(m_crypto.context, candidate.algorithm, candidate.iterations,
            reinterpret_cast<const uint8_t*>(password), passwordBytes,
            candidate.salt, sizeof(candidate.salt), candidate.verifier,
            sizeof(candidate.verifier))) {
        secureZero(&candidate, sizeof(candidate));
        return Result::CryptoUnavailable;
    }
    *output = candidate;
    secureZero(&candidate, sizeof(candidate));
    return Result::Ok;
}

Result Runtime::validatePassword(const CredentialRecord& credential,
                                 const char* password, size_t passwordBytes) const
{
    if (!credentialRecordValid(credential)) return Result::UnsupportedCredentialFormat;
    if (!validPasswordInput(password, passwordBytes)) return Result::InvalidCredentials;
    uint8_t verifier[kCredentialVerifierBytes]{};
    const bool derived = m_crypto.deriveVerifier(m_crypto.context, credential.algorithm,
        credential.iterations, reinterpret_cast<const uint8_t*>(password), passwordBytes,
        credential.salt, sizeof(credential.salt), verifier, sizeof(verifier));
    const bool matches = derived && timingSafeEqual(verifier, credential.verifier,
        sizeof(verifier));
    secureZero(verifier, sizeof(verifier));
    return !derived ? Result::CryptoUnavailable :
        (matches ? Result::Ok : Result::InvalidCredentials);
}

Result Runtime::commitCredentials(const CredentialRecord* candidate, size_t count)
{
    if (!m_initialized) return Result::NotInitialized;
    if ((!candidate && count != 0u) || count > identity::kMaximumUsers)
        return Result::InvalidArgument;
    if (!m_storeReadable) {
        return m_persistence == identity::PersistenceState::Unsupported
            ? Result::UnsupportedCredentialFormat : Result::StorageCorrupt;
    }
    identity::RuntimeSnapshotV1 identitySnapshot{};
    m_identityRecordsDurable = m_identities &&
        m_identities->snapshot(&identitySnapshot) == identity::Result::Ok &&
        identitySnapshot.initialized != 0u && identitySnapshot.accountCountAvailable != 0u &&
        identitySnapshot.allRecordsDurable != 0u;
    for (size_t i = 0u; i < count; ++i) {
        if (!credentialRecordValid(candidate[i]) ||
            (i != 0u && candidate[i - 1u].userId >= candidate[i].userId))
            return Result::InvalidArgument;
    }

    if (!m_storeWritable || !m_identityRecordsDurable) {
        for (size_t oldIndex = 0u; oldIndex < m_credentialCount; ++oldIndex) {
            if (m_credentials[oldIndex].durable == 0u) continue;
            bool retained = false;
            for (size_t newIndex = 0u; newIndex < count; ++newIndex) {
                if (sameCredential(m_credentials[oldIndex], candidate[newIndex])) {
                    retained = true;
                    break;
                }
            }
            if (!retained) return Result::StorageUnavailable;
        }
        memcpy(m_credentials, candidate, count * sizeof(CredentialRecord));
        m_credentialCount = count;
        return Result::OkVolatile;
    }
    if (!m_storage.readSlot || !m_storage.writeSlot || m_generation == UINT64_MAX)
        return Result::StorageUnavailable;

    const uint64_t nextGeneration = m_generation + 1u;
    const size_t totalBytes = kStoreHeaderBytes + count * kStoreRecordBytes + kStoreCrcBytes;
    if (totalBytes > sizeof(m_writeBytes)) return Result::CapacityExceeded;
    memcpy(m_writeBytes, kStoreMagic, sizeof(kStoreMagic));
    put32(m_writeBytes + 8u, kStoreVersion);
    put32(m_writeBytes + 12u, static_cast<uint32_t>(kStoreHeaderBytes));
    put64(m_writeBytes + 16u, nextGeneration);
    put32(m_writeBytes + 24u, static_cast<uint32_t>(count));
    put32(m_writeBytes + 28u, static_cast<uint32_t>(kStoreRecordBytes));
    size_t offset = kStoreHeaderBytes;
    for (size_t i = 0u; i < count; ++i) {
        put64(m_writeBytes + offset, candidate[i].userId);
        put16(m_writeBytes + offset + 8u, candidate[i].formatVersion);
        put16(m_writeBytes + offset + 10u, static_cast<uint16_t>(candidate[i].algorithm));
        put32(m_writeBytes + offset + 12u, candidate[i].iterations);
        memcpy(m_writeBytes + offset + 16u, candidate[i].salt, sizeof(candidate[i].salt));
        memcpy(m_writeBytes + offset + 32u, candidate[i].verifier,
            sizeof(candidate[i].verifier));
        offset += kStoreRecordBytes;
    }
    put32(m_writeBytes + offset, crc32(m_writeBytes, offset));
    const uint8_t targetSlot = m_activeSlot < 0
        ? 0u : static_cast<uint8_t>(1 - m_activeSlot);
    const StoreWriteResult write = m_storage.writeSlot(m_storage.context, targetSlot,
        m_writeBytes, totalBytes);
    if (write != StoreWriteResult::Ok) {
        secureZero(m_writeBytes, sizeof(m_writeBytes));
        return write == StoreWriteResult::IoError
            ? Result::IoError : Result::StorageUnavailable;
    }

    size_t verifyBytes = 0u;
    if (m_storage.readSlot(m_storage.context, targetSlot, m_verifyBytes,
            sizeof(m_verifyBytes), &verifyBytes) != StoreReadResult::Ok ||
        verifyBytes != totalBytes ||
        memcmp(m_writeBytes, m_verifyBytes, totalBytes) != 0) {
        secureZero(m_writeBytes, sizeof(m_writeBytes));
        secureZero(m_verifyBytes, sizeof(m_verifyBytes));
        return Result::IoError;
    }
    if (get32(m_verifyBytes + 8u) != kStoreVersion ||
        get64(m_verifyBytes + 16u) != nextGeneration ||
        get32(m_verifyBytes + 24u) != count ||
        get32(m_verifyBytes + verifyBytes - kStoreCrcBytes) !=
            crc32(m_verifyBytes, verifyBytes - kStoreCrcBytes)) {
        secureZero(m_writeBytes, sizeof(m_writeBytes));
        secureZero(m_verifyBytes, sizeof(m_verifyBytes));
        return Result::IoError;
    }

    memcpy(m_credentials, candidate, count * sizeof(CredentialRecord));
    for (size_t i = 0u; i < count; ++i) m_credentials[i].durable = 1u;
    m_credentialCount = count;
    m_generation = nextGeneration;
    m_activeSlot = static_cast<int8_t>(targetSlot);
    m_persistence = identity::PersistenceState::Persistent;
    secureZero(m_writeBytes, sizeof(m_writeBytes));
    secureZero(m_verifyBytes, sizeof(m_verifyBytes));
    return Result::OkPersistent;
}

Result Runtime::enrollCredential(identity::UserId userId,
                                const char* password, size_t passwordBytes)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!currentAdministrator() && !currentUserIs(userId)) return Result::Unauthorized;
    identity::UserRecordV1 account{};
    if (!m_identityRegistryReadable || !m_identities ||
        m_identities->find(userId, &account) != identity::Result::Ok)
        return Result::AccountUnavailable;
    if (findCredential(userId, nullptr) == Result::Ok) return Result::CredentialExists;
    if (m_credentialCount >= identity::kMaximumUsers) return Result::CapacityExceeded;
    CredentialRecord added{};
    const Result prepared = prepareCredential(password, passwordBytes, &added);
    if (prepared != Result::Ok) return prepared;
    added.userId = userId;
    CredentialRecord candidate[identity::kMaximumUsers]{};
    size_t insert = 0u;
    while (insert < m_credentialCount && m_credentials[insert].userId < userId) ++insert;
    for (size_t i = 0u; i < insert; ++i) candidate[i] = m_credentials[i];
    candidate[insert] = added;
    for (size_t i = insert; i < m_credentialCount; ++i) candidate[i + 1u] = m_credentials[i];
    const Result committed = commitCredentials(candidate, m_credentialCount + 1u);
    secureZero(&added, sizeof(added));
    secureZero(candidate, sizeof(candidate));
    if (committed == Result::Ok || committed == Result::OkPersistent ||
        committed == Result::OkVolatile) {
        m_bootstrapRequired = true;
        identity::UserRecordV1 accounts[identity::kMaximumUsers]{};
        size_t count = 0u;
        if (m_identities->enumerate(accounts, identity::kMaximumUsers, &count) == identity::Result::Ok)
            for (size_t i = 0u; i < count; ++i)
                if (accounts[i].status == identity::AccountStatus::Active &&
                    accounts[i].role == identity::AccountRole::Administrator &&
                    findCredential(accounts[i].id, nullptr) == Result::Ok)
                    m_bootstrapRequired = false;
    }
    return committed;
}

uint64_t Runtime::accountNameKey(const char* accountName) const
{
    uint64_t hash = 1469598103934665603ull;
    if (!accountName) return hash;
    for (size_t i = 0u; i < identity::kAccountNameCapacity && accountName[i]; ++i) {
        uint8_t value = static_cast<uint8_t>(accountName[i]);
        if (value >= 'A' && value <= 'Z') value = static_cast<uint8_t>(value + ('a' - 'A'));
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool Runtime::isThrottled(uint64_t nameKey, uint64_t now) const
{
    for (size_t i = 0u; i < kMaximumAuthenticationAttempts; ++i)
        if (m_attempts[i].used && m_attempts[i].nameKey == nameKey &&
            now < m_attempts[i].blockedUntil) return true;
    return false;
}

void Runtime::recordFailure(uint64_t nameKey, uint64_t now)
{
    AttemptRecord* record = nullptr;
    for (size_t i = 0u; i < kMaximumAuthenticationAttempts; ++i) {
        if (m_attempts[i].used && m_attempts[i].nameKey == nameKey) {
            record = &m_attempts[i];
            break;
        }
    }
    if (!record) {
        record = &m_attempts[m_nextAttemptSlot++ % kMaximumAuthenticationAttempts];
        *record = AttemptRecord{};
        record->used = 1u;
        record->nameKey = nameKey;
    }
    if (record->blockedUntil != 0u && now >= record->blockedUntil)
        record->blockedUntil = 0u;
    if (record->consecutiveFailures < UINT32_MAX) ++record->consecutiveFailures;
    if (record->consecutiveFailures >= kFailuresBeforeBackoff) {
        const uint32_t exponent = record->consecutiveFailures - kFailuresBeforeBackoff;
        uint64_t delay = kInitialBackoffTicks;
        for (uint32_t i = 0u; i < exponent && delay < kMaximumBackoffTicks; ++i)
            delay = delay > kMaximumBackoffTicks / 2u
                ? kMaximumBackoffTicks : delay * 2u;
        if (delay > kMaximumBackoffTicks) delay = kMaximumBackoffTicks;
        record->blockedUntil = now > UINT64_MAX - delay ? UINT64_MAX : now + delay;
    }
}

void Runtime::clearAttempt(uint64_t nameKey)
{
    for (size_t i = 0u; i < kMaximumAuthenticationAttempts; ++i)
        if (m_attempts[i].used && m_attempts[i].nameKey == nameKey)
            m_attempts[i] = AttemptRecord{};
}

Result Runtime::dummyPasswordWork(const char* password, size_t passwordBytes) const
{
    if (!validPasswordInput(password, passwordBytes)) return Result::InvalidCredentials;
    static const uint8_t kDummySalt[kCredentialSaltBytes] = {
        0x47u,0x58u,0x4Fu,0x53u,0x2Du,0x41u,0x55u,0x54u,
        0x48u,0x2Du,0x44u,0x55u,0x4Du,0x4Du,0x59u,0x31u
    };
    uint8_t verifier[kCredentialVerifierBytes]{};
    const bool derived = m_crypto.deriveVerifier(m_crypto.context,
        Algorithm::Pbkdf2HmacSha256, kPbkdf2HmacSha256Iterations,
        reinterpret_cast<const uint8_t*>(password), passwordBytes,
        kDummySalt, sizeof(kDummySalt), verifier, sizeof(verifier));
    secureZero(verifier, sizeof(verifier));
    return derived ? Result::Ok : Result::CryptoUnavailable;
}

Result Runtime::authenticate(const char* accountName,
                             const char* password, size_t passwordBytes,
                             SessionReference* outputSession)
{
    if (outputSession) memset(outputSession, 0, sizeof(*outputSession));
    if (!m_initialized) return Result::NotInitialized;
    if (!outputSession || !validPasswordInput(password, passwordBytes))
        return Result::InvalidCredentials;
    if (!m_identityRegistryReadable) return Result::AccountUnavailable;
    if (!m_storeReadable) return m_persistence == identity::PersistenceState::Unsupported
        ? Result::UnsupportedCredentialFormat : Result::StorageUnavailable;
    const uint64_t nameKey = accountNameKey(accountName);
    const uint64_t now = m_crypto.clockTicks(m_crypto.context);
    if (isThrottled(nameKey, now)) return Result::Throttled;
    identity::UserRecordV1 account{};
    const identity::Result lookup = m_identities
        ? m_identities->findByAccountName(accountName, &account)
        : identity::Result::NotInitialized;
    if (lookup != identity::Result::Ok) {
        const Result work = dummyPasswordWork(password, passwordBytes);
        if (work != Result::Ok) return work;
        if (lookup != identity::Result::NotFound && lookup != identity::Result::Inactive &&
            lookup != identity::Result::InvalidArgument) return Result::AccountUnavailable;
        recordFailure(nameKey, now);
        return Result::InvalidCredentials;
    }
    size_t index = 0u;
    if (findCredential(account.id, &index) != Result::Ok) {
        const Result work = dummyPasswordWork(password, passwordBytes);
        if (work != Result::Ok) return work;
        recordFailure(nameKey, now);
        return Result::InvalidCredentials;
    }
    const Result validated = validatePassword(m_credentials[index], password, passwordBytes);
    if (validated != Result::Ok) {
        if (validated == Result::CryptoUnavailable ||
            validated == Result::UnsupportedCredentialFormat) return validated;
        recordFailure(nameKey, now);
        return Result::InvalidCredentials;
    }
    clearAttempt(nameKey);
    return openSession(account.id, outputSession);
}

Result Runtime::openSession(identity::UserId userId, SessionReference* outputSession)
{
    if (!outputSession || !m_crypto.randomBytes) return Result::InvalidArgument;
    if (!m_storeReadable || !m_identityRegistryReadable) return Result::AccountUnavailable;
    if (m_currentSession >= 0) return Result::SessionUnavailable;
    identity::UserRecordV1 account{};
    if (!m_identities || m_identities->find(userId, &account) != identity::Result::Ok ||
        findCredential(userId, nullptr) != Result::Ok) return Result::AccountUnavailable;
    size_t slot = kMaximumSessions;
    for (size_t i = 0u; i < kMaximumSessions; ++i) {
        if (m_sessions[i].state != SessionState::Active) {
            slot = i;
            break;
        }
    }
    if (slot == kMaximumSessions) return Result::CapacityExceeded;
    SessionReference reference{};
    bool unique = false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        if (!m_crypto.randomBytes(m_crypto.context, reference.bytes, sizeof(reference.bytes))) {
            secureZero(&reference, sizeof(reference));
            return Result::EntropyUnavailable;
        }
        uint8_t aggregate = 0u;
        for (size_t i = 0u; i < sizeof(reference.bytes); ++i) aggregate |= reference.bytes[i];
        unique = aggregate != 0u;
        for (size_t i = 0u; unique && i < kMaximumSessions; ++i)
            if (m_sessions[i].state != SessionState::None &&
                timingSafeEqual(m_sessions[i].reference.bytes, reference.bytes,
                    sizeof(reference.bytes))) unique = false;
        if (unique) break;
    }
    if (!unique) {
        secureZero(&reference, sizeof(reference));
        return Result::EntropyUnavailable;
    }
    m_sessions[slot].reference = reference;
    m_sessions[slot].userId = userId;
    m_sessions[slot].state = SessionState::Active;
    m_currentSession = static_cast<int8_t>(slot);
    *outputSession = reference;
    secureZero(&reference, sizeof(reference));
    return Result::Ok;
}

Result Runtime::changeCredential(identity::UserId userId,
                                 const char* currentPassword, size_t currentBytes,
                                 const char* newPassword, size_t newBytes)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!m_identityRegistryReadable) return Result::AccountUnavailable;
    if (!currentAdministrator() && !currentUserIs(userId)) return Result::Unauthorized;
    identity::UserRecordV1 account{};
    if (!m_identities || m_identities->find(userId, &account) != identity::Result::Ok)
        return Result::AccountUnavailable;
    size_t index = 0u;
    if (findCredential(userId, &index) != Result::Ok) return Result::CredentialMissing;
    const Result current = validatePassword(m_credentials[index], currentPassword, currentBytes);
    if (current != Result::Ok) return current == Result::CryptoUnavailable
        ? current : Result::InvalidCredentials;
    CredentialRecord replacement{};
    const Result prepared = prepareCredential(newPassword, newBytes, &replacement);
    if (prepared != Result::Ok) return prepared;
    replacement.userId = userId;
    CredentialRecord candidate[identity::kMaximumUsers]{};
    memcpy(candidate, m_credentials, m_credentialCount * sizeof(CredentialRecord));
    candidate[index] = replacement;
    const Result committed = commitCredentials(candidate, m_credentialCount);
    secureZero(&replacement, sizeof(replacement));
    secureZero(candidate, sizeof(candidate));
    if (committed == Result::OkPersistent || committed == Result::OkVolatile ||
        committed == Result::Ok) invalidateUser(userId);
    return committed;
}

Result Runtime::revokeCredential(identity::UserId userId)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!currentAdministrator() && !currentUserIs(userId)) return Result::Unauthorized;
    size_t index = 0u;
    if (findCredential(userId, &index) != Result::Ok) return Result::CredentialMissing;
    identity::UserRecordV1 account{};
    const bool activeAdministrator = m_identities &&
        m_identities->findRecord(userId, &account) == identity::Result::Ok &&
        account.status == identity::AccountStatus::Active &&
        account.role == identity::AccountRole::Administrator;
    if (activeAdministrator) {
        size_t usableAdministrators = 0u;
        identity::UserRecordV1 accounts[identity::kMaximumUsers]{};
        size_t count = 0u;
        if (m_identities->enumerate(accounts, identity::kMaximumUsers, &count) == identity::Result::Ok) {
            for (size_t i = 0u; i < count; ++i)
                if (accounts[i].status == identity::AccountStatus::Active &&
                    accounts[i].role == identity::AccountRole::Administrator &&
                    findCredential(accounts[i].id, nullptr) == Result::Ok)
                    ++usableAdministrators;
        }
        if (usableAdministrators <= 1u) return Result::LastAdministrator;
    }
    CredentialRecord candidate[identity::kMaximumUsers]{};
    size_t outputCount = 0u;
    for (size_t i = 0u; i < m_credentialCount; ++i)
        if (i != index) candidate[outputCount++] = m_credentials[i];
    const Result committed = commitCredentials(candidate, outputCount);
    secureZero(candidate, sizeof(candidate));
    if (committed == Result::Ok || committed == Result::OkPersistent ||
        committed == Result::OkVolatile) invalidateUser(userId);
    return committed;
}

Result Runtime::bootstrapAdministrator(const char* accountName,
                                       const char* displayName,
                                       const char* password, size_t passwordBytes,
                                       SessionReference* outputSession)
{
    if (outputSession) memset(outputSession, 0, sizeof(*outputSession));
    if (!m_initialized) return Result::NotInitialized;
    if (!m_identityRegistryReadable) return Result::AccountUnavailable;
    if (!outputSession || !identity::validAccountName(accountName) ||
        !identity::validDisplayName(displayName) || !validPasswordInput(password, passwordBytes))
        return Result::InvalidArgument;
    if (!m_storeReadable) return m_persistence == identity::PersistenceState::Unsupported
        ? Result::UnsupportedCredentialFormat : Result::StorageUnavailable;

    identity::UserRecordV1 accounts[identity::kMaximumUsers]{};
    size_t accountCount = 0u;
    if (!m_identities || m_identities->enumerate(accounts, identity::kMaximumUsers,
            &accountCount) != identity::Result::Ok) return Result::AccountUnavailable;
    size_t usableAdministrators = 0u;
    for (size_t i = 0u; i < accountCount; ++i)
        if (accounts[i].status == identity::AccountStatus::Active &&
            accounts[i].role == identity::AccountRole::Administrator &&
            findCredential(accounts[i].id, nullptr) == Result::Ok)
            ++usableAdministrators;
    if (usableAdministrators != 0u) return Result::Unauthorized;

    identity::UserRecordV1 account{};
    const identity::Result lookup = m_identities->findByAccountName(accountName, &account);
    if (lookup == identity::Result::NotFound && accountCount != 0u)
        return Result::AccountUnavailable;
    if (lookup != identity::Result::NotFound && lookup != identity::Result::Ok)
        return Result::AccountUnavailable;

    // If the selected account already has a credential, prove it before
    // granting the first administrator role. With no credential, this is the
    // explicit local bootstrap/recovery path and the user enrolls one.
    if (lookup == identity::Result::Ok && findCredential(account.id, nullptr) == Result::Ok) {
        SessionReference authenticated{};
        const Result authentication = authenticate(accountName, password,
            passwordBytes, &authenticated);
        secureZero(&authenticated, sizeof(authenticated));
        if (authentication != Result::Ok) return authentication;
        const identity::Result promoted = m_identities->setRole(account.id,
            identity::AccountRole::Administrator);
        if (promoted != identity::Result::Ok &&
            promoted != identity::Result::OkPersistent &&
            promoted != identity::Result::OkVolatile)
            return mapIdentityResult(promoted);
        identity::AccountRole role{};
        if (m_identities->getRole(account.id, &role) != identity::Result::Ok ||
            role != identity::AccountRole::Administrator) return Result::IoError;
        m_bootstrapRequired = false;
        SessionReference current{};
        if (m_currentSession >= 0) current = m_sessions[m_currentSession].reference;
        *outputSession = current;
        secureZero(&current, sizeof(current));
        return promoted == identity::Result::OkPersistent ? Result::OkPersistent :
            (promoted == identity::Result::OkVolatile ? Result::OkVolatile : Result::Ok);
    }

    CredentialRecord prepared{};
    const Result preparedResult = prepareCredential(password, passwordBytes, &prepared);
    if (preparedResult != Result::Ok) return preparedResult;
    if (lookup == identity::Result::NotFound) {
        identity::UserId userId = identity::kInvalidUserId;
        const identity::Result created = m_identities->provision(accountName,
            displayName, &userId);
        if (created != identity::Result::Ok && created != identity::Result::OkPersistent &&
            created != identity::Result::OkVolatile) {
            secureZero(&prepared, sizeof(prepared));
            return mapIdentityResult(created);
        }
        account.id = userId;
    }
    prepared.userId = account.id;
    CredentialRecord candidate[identity::kMaximumUsers]{};
    size_t insert = 0u;
    while (insert < m_credentialCount && m_credentials[insert].userId < account.id) ++insert;
    if (m_credentialCount >= identity::kMaximumUsers) {
        secureZero(&prepared, sizeof(prepared));
        return Result::CapacityExceeded;
    }
    for (size_t i = 0u; i < insert; ++i) candidate[i] = m_credentials[i];
    candidate[insert] = prepared;
    for (size_t i = insert; i < m_credentialCount; ++i) candidate[i + 1u] = m_credentials[i];
    const Result enrolled = commitCredentials(candidate, m_credentialCount + 1u);
    secureZero(&prepared, sizeof(prepared));
    secureZero(candidate, sizeof(candidate));
    if (enrolled != Result::Ok && enrolled != Result::OkPersistent &&
        enrolled != Result::OkVolatile) return enrolled;
    const identity::Result roleSet = m_identities->setRole(account.id,
        identity::AccountRole::Administrator);
    if (roleSet != identity::Result::Ok && roleSet != identity::Result::OkPersistent &&
        roleSet != identity::Result::OkVolatile) return mapIdentityResult(roleSet);
    m_bootstrapRequired = false;
    const Result authenticated = authenticate(accountName, password, passwordBytes,
        outputSession);
    if (authenticated != Result::Ok) return authenticated;
    if (enrolled == Result::OkVolatile || roleSet == identity::Result::OkVolatile)
        return Result::OkVolatile;
    if (enrolled == Result::OkPersistent && roleSet == identity::Result::OkPersistent)
        return Result::OkPersistent;
    return Result::Ok;
}

Result Runtime::setAccountRole(identity::UserId userId, identity::AccountRole role)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!currentAdministrator()) return Result::Unauthorized;
    if (!identity::validAccountRole(role)) return Result::InvalidArgument;
    identity::UserRecordV1 target{};
    if (!m_identities || m_identities->findRecord(userId, &target) == identity::Result::NotFound)
        return Result::AccountUnavailable;
    if (target.id == identity::kInvalidUserId) return Result::AccountUnavailable;
    if (target.status == identity::AccountStatus::Active &&
        target.role == identity::AccountRole::Administrator &&
        role != identity::AccountRole::Administrator &&
        findCredential(userId, nullptr) == Result::Ok) {
        size_t others = 0u;
        identity::UserRecordV1 accounts[identity::kMaximumUsers]{};
        size_t count = 0u;
        if (m_identities->enumerate(accounts, identity::kMaximumUsers, &count) == identity::Result::Ok)
            for (size_t i = 0u; i < count; ++i)
                if (accounts[i].id != userId && accounts[i].status == identity::AccountStatus::Active &&
                    accounts[i].role == identity::AccountRole::Administrator &&
                    findCredential(accounts[i].id, nullptr) == Result::Ok) ++others;
        if (others == 0u) return Result::LastAdministrator;
    }
    const identity::Result changed = m_identities->setRole(userId, role);
    m_bootstrapRequired = changed != identity::Result::Ok &&
        changed != identity::Result::OkPersistent && changed != identity::Result::OkVolatile;
    return mapIdentityResult(changed);
}

Result Runtime::setAccountStatus(identity::UserId userId, identity::AccountStatus status)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!currentAdministrator()) return Result::Unauthorized;
    identity::UserRecordV1 target{};
    if (!m_identities || m_identities->findRecord(userId, &target) == identity::Result::NotFound)
        return Result::AccountUnavailable;
    if (target.id == identity::kInvalidUserId) return Result::AccountUnavailable;
    if (target.status == identity::AccountStatus::Active &&
        status == identity::AccountStatus::Disabled &&
        target.role == identity::AccountRole::Administrator &&
        findCredential(userId, nullptr) == Result::Ok) {
        size_t others = 0u;
        identity::UserRecordV1 accounts[identity::kMaximumUsers]{};
        size_t count = 0u;
        if (m_identities->enumerate(accounts, identity::kMaximumUsers, &count) == identity::Result::Ok)
            for (size_t i = 0u; i < count; ++i)
                if (accounts[i].id != userId && accounts[i].status == identity::AccountStatus::Active &&
                    accounts[i].role == identity::AccountRole::Administrator &&
                    findCredential(accounts[i].id, nullptr) == Result::Ok) ++others;
        if (others == 0u) return Result::LastAdministrator;
    }
    const identity::Result changed = m_identities->setStatus(userId, status);
    if (changed != identity::Result::Ok && changed != identity::Result::OkPersistent &&
        changed != identity::Result::OkVolatile) return mapIdentityResult(changed);
    if (status == identity::AccountStatus::Disabled) invalidateUser(userId);
    return mapIdentityResult(changed);
}

Result Runtime::eraseAccount(identity::UserId userId)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!currentAdministrator()) return Result::Unauthorized;
    identity::UserRecordV1 target{};
    if (!m_identities || m_identities->findRecord(userId, &target) == identity::Result::NotFound)
        return Result::AccountUnavailable;
    if (target.id == identity::kInvalidUserId) return Result::AccountUnavailable;
    if (target.status == identity::AccountStatus::Active &&
        target.role == identity::AccountRole::Administrator &&
        findCredential(userId, nullptr) == Result::Ok) {
        size_t others = 0u;
        identity::UserRecordV1 accounts[identity::kMaximumUsers]{};
        size_t count = 0u;
        if (m_identities->enumerate(accounts, identity::kMaximumUsers, &count) == identity::Result::Ok)
            for (size_t i = 0u; i < count; ++i)
                if (accounts[i].id != userId && accounts[i].status == identity::AccountStatus::Active &&
                    accounts[i].role == identity::AccountRole::Administrator &&
                    findCredential(accounts[i].id, nullptr) == Result::Ok) ++others;
        if (others == 0u) return Result::LastAdministrator;
    }
    const identity::Result erased = m_identities->erase(userId);
    if (erased != identity::Result::Ok && erased != identity::Result::OkPersistent &&
        erased != identity::Result::OkVolatile) return mapIdentityResult(erased);
    invalidateUser(userId);
    size_t index = 0u;
    if (findCredential(userId, &index) == Result::Ok) {
        CredentialRecord candidate[identity::kMaximumUsers]{};
        size_t outputCount = 0u;
        for (size_t i = 0u; i < m_credentialCount; ++i)
            if (i != index) candidate[outputCount++] = m_credentials[i];
        const Result removed = commitCredentials(candidate, outputCount);
        secureZero(candidate, sizeof(candidate));
        if (removed != Result::Ok && removed != Result::OkPersistent &&
            removed != Result::OkVolatile) return removed;
    }
    m_bootstrapRequired = true;
    return erased == identity::Result::OkPersistent ? Result::OkPersistent :
        (erased == identity::Result::OkVolatile ? Result::OkVolatile : Result::Ok);
}

bool Runtime::credentialEligible(identity::UserId userId) const
{
    identity::UserRecordV1 account{};
    return m_initialized && m_identityRegistryReadable && m_storeReadable && m_identities &&
        m_identities->find(userId, &account) == identity::Result::Ok &&
        findCredential(userId, nullptr) == Result::Ok;
}

bool Runtime::currentAdministrator()
{
    SessionSnapshotV1 current{};
    return queryCurrentSession(&current) == Result::Ok &&
        current.state == SessionState::Active &&
        current.role == identity::AccountRole::Administrator;
}

bool Runtime::currentUserIs(identity::UserId userId)
{
    SessionSnapshotV1 current{};
    return queryCurrentSession(&current) == Result::Ok &&
        current.state == SessionState::Active && current.userId == userId;
}

void Runtime::clearSession(size_t index, SessionState finalState)
{
    if (index >= kMaximumSessions) return;
    secureZero(&m_sessions[index].reference, sizeof(m_sessions[index].reference));
    m_sessions[index].userId = identity::kInvalidUserId;
    m_sessions[index].state = finalState;
    if (m_currentSession == static_cast<int8_t>(index)) m_currentSession = -1;
}

void Runtime::invalidateUser(identity::UserId userId)
{
    for (size_t i = 0u; i < kMaximumSessions; ++i)
        if (m_sessions[i].state == SessionState::Active && m_sessions[i].userId == userId)
            clearSession(i, SessionState::Invalidated);
}

Result Runtime::queryCurrentSession(SessionSnapshotV1* output)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output) return Result::InvalidArgument;
    SessionSnapshotV1 snapshot{};
    snapshot.version = 1u;
    snapshot.state = SessionState::None;
    snapshot.userId = identity::kInvalidUserId;
    if (m_currentSession >= 0) {
        const size_t index = static_cast<size_t>(m_currentSession);
        identity::UserRecordV1 account{};
        if (m_sessions[index].state != SessionState::Active || !m_storeReadable ||
            !m_identityRegistryReadable || !m_identities ||
            m_identities->find(m_sessions[index].userId, &account) != identity::Result::Ok ||
            findCredential(m_sessions[index].userId, nullptr) != Result::Ok) {
            clearSession(index, SessionState::Invalidated);
        } else {
            snapshot.state = SessionState::Active;
            snapshot.userId = account.id;
            snapshot.role = account.role;
            memcpy(snapshot.displayName, account.displayName, sizeof(snapshot.displayName));
        }
    }
    *output = snapshot;
    return Result::Ok;
}

Result Runtime::queryContext(identity::ExecutionDomain domain,
                             identity::IdentityContextV1* output)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output) return Result::InvalidArgument;
    identity::IdentityContextV1 context{};
    context.version = 1u;
    if (domain == identity::ExecutionDomain::Kernel) {
        context.kind = identity::IdentityKind::KernelSystem;
        context.userId = identity::kSystemIdentityId;
    } else if (domain == identity::ExecutionDomain::Interactive) {
        SessionSnapshotV1 current{};
        const Result result = queryCurrentSession(&current);
        if (result != Result::Ok) return result;
        if (current.state == SessionState::Active) {
            context.kind = identity::IdentityKind::AuthenticatedHuman;
            context.userId = current.userId;
        } else {
            context.kind = identity::IdentityKind::InteractiveUnassigned;
            context.userId = identity::kInvalidUserId;
        }
    } else return Result::InvalidArgument;
    *output = context;
    return Result::Ok;
}

Result Runtime::signOutCurrent()
{
    if (!m_initialized) return Result::NotInitialized;
    if (m_currentSession < 0) return Result::SessionUnavailable;
    clearSession(static_cast<size_t>(m_currentSession), SessionState::Invalidated);
    return Result::Ok;
}

Result Runtime::snapshot(RuntimeSnapshotV1* output) const
{
    if (!output) return Result::InvalidArgument;
    RuntimeSnapshotV1 snapshot{};
    snapshot.version = 1u;
    snapshot.initialized = m_initialized ? 1u : 0u;
    snapshot.credentialStoreAvailable = m_storeReadable ? 1u : 0u;
    snapshot.credentialCount = static_cast<uint8_t>(m_credentialCount);
    snapshot.allCredentialsDurable = 1u;
    for (size_t i = 0u; i < m_credentialCount; ++i)
        if (m_credentials[i].durable == 0u) snapshot.allCredentialsDurable = 0u;
    snapshot.bootstrapRequired = m_bootstrapRequired ? 1u : 0u;
    snapshot.persistence = m_persistence;
    *output = snapshot;
    return m_initialized ? Result::Ok : Result::NotInitialized;
}

bool validSessionSnapshot(const SessionSnapshotV1& snapshot)
{
    if (snapshot.version != 1u) return false;
    if (snapshot.state == SessionState::None)
        return snapshot.userId == identity::kInvalidUserId && snapshot.displayName[0] == '\0';
    return snapshot.state == SessionState::Active &&
        snapshot.userId >= identity::kFirstHumanUserId &&
        snapshot.userId <= identity::kMaximumUserId &&
        identity::validAccountRole(snapshot.role) && identity::validDisplayName(snapshot.displayName);
}

const char* resultName(Result result)
{
    switch (result) {
    case Result::Ok: return "OK";
    case Result::OkPersistent: return "OK_PERSISTENT";
    case Result::OkVolatile: return "OK_VOLATILE";
    case Result::NotInitialized: return "NOT_INITIALIZED";
    case Result::InvalidArgument: return "INVALID_ARGUMENT";
    case Result::AccountUnavailable: return "ACCOUNT_UNAVAILABLE";
    case Result::InvalidCredentials: return "INVALID_CREDENTIALS";
    case Result::CredentialExists: return "CREDENTIAL_EXISTS";
    case Result::CredentialMissing: return "CREDENTIAL_MISSING";
    case Result::StorageUnavailable: return "STORAGE_UNAVAILABLE";
    case Result::StorageCorrupt: return "STORAGE_CORRUPT";
    case Result::UnsupportedIdentityFormat: return "UNSUPPORTED_IDENTITY_FORMAT";
    case Result::UnsupportedCredentialFormat: return "UNSUPPORTED_CREDENTIAL_FORMAT";
    case Result::EntropyUnavailable: return "ENTROPY_UNAVAILABLE";
    case Result::CryptoUnavailable: return "CRYPTO_UNAVAILABLE";
    case Result::Throttled: return "THROTTLED";
    case Result::SessionUnavailable: return "SESSION_UNAVAILABLE";
    case Result::LastAdministrator: return "LAST_ADMINISTRATOR";
    case Result::Unauthorized: return "UNAUTHORIZED";
    case Result::CapacityExceeded: return "CAPACITY_EXCEEDED";
    case Result::IoError: return "IO_ERROR";
    default: return "UNKNOWN";
    }
}

Runtime& runtime() { return s_runtime; }

} // namespace authentication
} // namespace kernel
