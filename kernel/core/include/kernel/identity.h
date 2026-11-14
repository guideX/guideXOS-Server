#ifndef KERNEL_IDENTITY_H
#define KERNEL_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace identity {

typedef uint64_t UserId;

static const UserId kInvalidUserId = 0u;
static const UserId kSystemIdentityId = 1u;
static const UserId kFirstHumanUserId = 0x1000u;
static const UserId kMaximumUserId = UINT64_MAX - 1u;
static const uint16_t kUserRecordVersion = 1u;
static const size_t kMaximumUsers = 32u;
static const size_t kAccountNameCapacity = 32u;
static const size_t kDisplayNameCapacity = 64u;
static const size_t kIdentityHeaderBytes = 40u;
static const size_t kIdentityRecordBytes = 8u + 1u + kAccountNameCapacity + kDisplayNameCapacity;
static const size_t kMaximumIdentityStoreBytes =
    kIdentityHeaderBytes + kMaximumUsers * kIdentityRecordBytes + 4u;

enum class AccountStatus : uint8_t {
    Active = 1u,
    Disabled = 2u
};

enum class Result : uint8_t {
    Ok,
    OkPersistent,
    OkVolatile,
    NotInitialized,
    InvalidArgument,
    InvalidId,
    DuplicateId,
    DuplicateAccountName,
    NotFound,
    Inactive,
    CapacityExceeded,
    IdExhausted,
    BufferTooSmall,
    CorruptRecord,
    UnsupportedVersion,
    StorageUnavailable,
    StorageCorrupt,
    IoError,
    GenerationExhausted
};

enum class IdentityKind : uint8_t {
    KernelSystem = 1u,
    InteractiveUnassigned = 2u,
    AuthenticatedHuman = 3u
};

enum class ExecutionDomain : uint8_t {
    Kernel = 1u,
    Interactive = 2u
};

struct UserRecordV1 {
    uint16_t version;
    UserId id;
    AccountStatus status;
    char accountName[kAccountNameCapacity];
    char displayName[kDisplayNameCapacity];
};

struct IdentityContextV1 {
    uint16_t version;
    IdentityKind kind;
    UserId userId;
};

enum class PersistenceState : uint8_t {
    Persistent,
    Volatile,
    ReadOnly,
    Unavailable,
    Corrupt,
    Unsupported
};

struct RuntimeSnapshotV1 {
    uint16_t version;
    uint8_t initialized;
    uint8_t accountCountAvailable;
    PersistenceState persistence;
    uint32_t accountCount;
    uint64_t generation;
    uint8_t allRecordsDurable;
};

// Fixed-size registry. All returned records are copies, so callers never
// retain pointers into registry storage. Account names are unique using ASCII
// case-insensitive comparison; IDs are the only internal account keys.
class Registry {
public:
    Registry();

    void initializeEmpty();
    bool initialized() const;
    size_t count() const;
    UserId nextUserId() const;

    Result provision(const char* accountName, const char* displayName, UserId* outId);
    Result find(UserId id, UserRecordV1* output) const;
    Result enumerate(UserRecordV1* output, size_t capacity, size_t* outCount) const;
    Result setStatus(UserId id, AccountStatus status);
    Result erase(UserId id);

    Result serialize(uint64_t generation, uint8_t* output, size_t capacity,
                     size_t* outBytes) const;
    Result load(const uint8_t* bytes, size_t byteCount, uint64_t* outGeneration);

private:
    UserRecordV1 m_records[kMaximumUsers];
    size_t m_count;
    UserId m_nextUserId;
    bool m_initialized;
};

bool validAccountName(const char* value);
bool validDisplayName(const char* value);
const char* resultName(Result result);
const char* persistenceStateName(PersistenceState state);

} // namespace identity
} // namespace kernel

#endif
