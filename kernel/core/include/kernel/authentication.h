#ifndef KERNEL_AUTHENTICATION_H
#define KERNEL_AUTHENTICATION_H

#include "kernel/identity_runtime.h"

namespace kernel {
namespace authentication {

static const size_t kCredentialSaltBytes = 16u;
static const size_t kCredentialVerifierBytes = 32u;
static const size_t kMaximumPasswordBytes = 128u;
static const size_t kMaximumSessions = 16u;
static const size_t kMaximumAuthenticationAttempts = 32u;
static const uint32_t kPbkdf2HmacSha256Iterations = 600000u;
static const uint16_t kCredentialRecordFormatVersion = 1u;

enum class Algorithm : uint16_t {
    Pbkdf2HmacSha256 = 1u
};

enum class Result : uint8_t {
    Ok,
    OkPersistent,
    OkVolatile,
    NotInitialized,
    InvalidArgument,
    AccountUnavailable,
    InvalidCredentials,
    CredentialExists,
    CredentialMissing,
    StorageUnavailable,
    StorageCorrupt,
    UnsupportedIdentityFormat,
    UnsupportedCredentialFormat,
    EntropyUnavailable,
    CryptoUnavailable,
    Throttled,
    SessionUnavailable,
    LastAdministrator,
    Unauthorized,
    CapacityExceeded,
    IoError
};

enum class StoreReadResult : uint8_t { Ok, Missing, Corrupt, Unavailable, IoError };
enum class StoreWriteResult : uint8_t { Ok, ReadOnly, Unavailable, IoError };

typedef StoreReadResult (*ReadCredentialSlotFn)(void* context, uint8_t slot,
    uint8_t* output, size_t capacity, size_t* outBytes);
typedef StoreWriteResult (*WriteCredentialSlotFn)(void* context, uint8_t slot,
    const uint8_t* bytes, size_t byteCount);

struct StorageAdapter {
    void* context;
    ReadCredentialSlotFn readSlot;
    WriteCredentialSlotFn writeSlot;
    uint8_t available;
    uint8_t writable;
};

struct CryptoProvider {
    void* context;
    bool (*randomBytes)(void* context, uint8_t* output, size_t byteCount);
    bool (*deriveVerifier)(void* context, Algorithm algorithm, uint32_t iterations,
        const uint8_t* password, size_t passwordBytes,
        const uint8_t* salt, size_t saltBytes,
        uint8_t* verifier, size_t verifierBytes);
    uint64_t (*clockTicks)(void* context);
};

struct SessionReference {
    uint8_t bytes[16];
};

enum class SessionState : uint8_t { None, Active, Invalidated };

struct SessionSnapshotV1 {
    uint16_t version;
    SessionState state;
    identity::UserId userId;
    identity::AccountRole role;
    char displayName[identity::kDisplayNameCapacity];
};

struct RuntimeSnapshotV1 {
    uint16_t version;
    uint8_t initialized;
    uint8_t credentialStoreAvailable;
    uint8_t credentialCount;
    uint8_t allCredentialsDurable;
    uint8_t bootstrapRequired;
    identity::PersistenceState persistence;
};

// The service is owned by the trusted native runtime. It is not exposed over
// a syscall, COM2, or hosted Settings transport. Authentication always
// resolves eligibility through the identity runtime before using a verifier.
class Runtime {
public:
    // Fixed-layout record type used by the service implementation and its
    // persistence tests. Callers receive no API for reading stored records.
    struct CredentialRecord {
        uint64_t userId;
        uint16_t formatVersion;
        Algorithm algorithm;
        uint32_t iterations;
        uint8_t salt[kCredentialSaltBytes];
        uint8_t verifier[kCredentialVerifierBytes];
        uint8_t durable;
    };

    Runtime();

    Result initialize(identity::Runtime* identities,
                      const StorageAdapter* storage,
                      const CryptoProvider* crypto,
                      bool bindInteractiveContext);
    bool initialized() const;
    Result enrollCredential(identity::UserId userId,
                            const char* password, size_t passwordBytes);
    Result authenticate(const char* accountName,
                       const char* password, size_t passwordBytes,
                       SessionReference* outputSession);
    Result changeCredential(identity::UserId userId,
                            const char* currentPassword, size_t currentBytes,
                            const char* newPassword, size_t newBytes);
    Result revokeCredential(identity::UserId userId);
    Result bootstrapAdministrator(const char* accountName,
                                  const char* displayName,
                                  const char* password, size_t passwordBytes,
                                  SessionReference* outputSession);
    Result setAccountRole(identity::UserId userId, identity::AccountRole role);
    Result setAccountStatus(identity::UserId userId, identity::AccountStatus status);
    Result eraseAccount(identity::UserId userId);
    bool credentialEligible(identity::UserId userId) const;
    Result queryCurrentSession(SessionSnapshotV1* output);
    Result queryContext(identity::ExecutionDomain domain,
                        identity::IdentityContextV1* output);
    Result signOutCurrent();
    Result snapshot(RuntimeSnapshotV1* output) const;
    void invalidateUser(identity::UserId userId);

private:
    struct SessionRecord {
        SessionReference reference;
        identity::UserId userId;
        SessionState state;
    };

    struct AttemptRecord {
        uint64_t nameKey;
        uint64_t blockedUntil;
        uint32_t consecutiveFailures;
        uint8_t used;
    };

    Result prepareCredential(const char* password, size_t passwordBytes,
                             CredentialRecord* output);
    Result commitCredentials(const CredentialRecord* candidate, size_t count);
    Result openSession(identity::UserId userId, SessionReference* outputSession);
    Result findCredential(identity::UserId userId, size_t* index) const;
    Result validatePassword(const CredentialRecord& credential,
                            const char* password, size_t passwordBytes) const;
    bool credentialRecordValid(const CredentialRecord& credential) const;
    void clearSession(size_t index, SessionState finalState);
    bool currentAdministrator();
    bool currentUserIs(identity::UserId userId);
    void clearAttempt(uint64_t nameKey);
    uint64_t accountNameKey(const char* accountName) const;
    bool isThrottled(uint64_t nameKey, uint64_t now) const;
    void recordFailure(uint64_t nameKey, uint64_t now);
    Result dummyPasswordWork(const char* password, size_t passwordBytes) const;

    identity::Runtime* m_identities;
    StorageAdapter m_storage;
    CryptoProvider m_crypto;
    CredentialRecord m_credentials[identity::kMaximumUsers];
    size_t m_credentialCount;
    uint64_t m_generation;
    int8_t m_activeSlot;
    identity::PersistenceState m_persistence;
    bool m_initialized;
    bool m_identityRegistryReadable;
    bool m_identityRecordsDurable;
    bool m_storeReadable;
    bool m_storeWritable;
    bool m_bootstrapRequired;
    SessionRecord m_sessions[kMaximumSessions];
    int8_t m_currentSession;
    AttemptRecord m_attempts[kMaximumAuthenticationAttempts];
    size_t m_nextAttemptSlot;
    uint8_t m_slotBytes[2][32u + identity::kMaximumUsers * 64u + 4u];
    uint8_t m_writeBytes[32u + identity::kMaximumUsers * 64u + 4u];
    uint8_t m_verifyBytes[32u + identity::kMaximumUsers * 64u + 4u];
};

const char* resultName(Result result);
bool validSessionSnapshot(const SessionSnapshotV1& snapshot);

CryptoProvider platformCryptoProvider();
StorageAdapter createVfsCredentialStorageAdapter();
Result initializeRuntime();
Runtime& runtime();

} // namespace authentication
} // namespace kernel

#endif
