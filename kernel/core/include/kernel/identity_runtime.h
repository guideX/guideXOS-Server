#ifndef KERNEL_IDENTITY_RUNTIME_H
#define KERNEL_IDENTITY_RUNTIME_H

#include "kernel/identity.h"

namespace kernel {
namespace authentication { class Runtime; }
namespace identity {

enum class StoreReadResult : uint8_t {
    Ok,
    Missing,
    Corrupt,
    Unavailable,
    IoError
};

enum class StoreWriteResult : uint8_t {
    Ok,
    ReadOnly,
    Unavailable,
    IoError
};

typedef StoreReadResult (*ReadIdentitySlotFn)(void* context, uint8_t slot,
                                               uint8_t* output, size_t capacity,
                                               size_t* outBytes);
typedef StoreWriteResult (*WriteIdentitySlotFn)(void* context, uint8_t slot,
                                                 const uint8_t* bytes, size_t byteCount);
typedef Result (*InteractiveContextProviderFn)(void* context,
                                               IdentityContextV1* output);

struct StorageAdapter {
    void* context;
    ReadIdentitySlotFn readSlot;
    WriteIdentitySlotFn writeSlot;
    uint8_t available;
    uint8_t writable;
};

// Runtime owns one registry and receives storage callbacks owned by the VFS
// adapter. No caller-supplied ID can create an identity context or session.
class Runtime {
public:
    Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    Result initialize(const StorageAdapter* storage);
    bool initialized() const;
    Result provision(const char* accountName, const char* displayName, UserId* outId);
    Result find(UserId id, UserRecordV1* output) const;
    Result findRecord(UserId id, UserRecordV1* output) const;
    Result findByAccountName(const char* accountName, UserRecordV1* output) const;
    Result enumerate(UserRecordV1* output, size_t capacity, size_t* outCount) const;
    Result setStatus(UserId id, AccountStatus status);
    Result setRole(UserId id, AccountRole role);
    Result getRole(UserId id, AccountRole* output) const;
    bool isAdministrator(UserId id) const;
    Result erase(UserId id);
    Result queryContext(ExecutionDomain domain, IdentityContextV1* output) const;
    Result snapshot(RuntimeSnapshotV1* output) const;

private:
    Result commit(const Registry& candidate);
    Result provisionForAuthentication(const authentication::Runtime* owner,
                                      const char* accountName,
                                      const char* displayName, UserId* outId);
    Result setStatusForAuthentication(const authentication::Runtime* owner,
                                      UserId id, AccountStatus status);
    Result setRoleForAuthentication(const authentication::Runtime* owner,
                                    UserId id, AccountRole role);
    Result eraseForAuthentication(const authentication::Runtime* owner, UserId id);
    bool bindAuthorizationManager(const authentication::Runtime* owner);
    void setInteractiveContextProvider(const authentication::Runtime* owner,
                                       void* context,
                                       InteractiveContextProviderFn provider);

    friend class ::kernel::authentication::Runtime;

    Registry m_registry;
    StorageAdapter m_storage;
    PersistenceState m_persistence;
    uint64_t m_generation;
    int8_t m_activeSlot;
    bool m_initialized;
    bool m_accountCountAvailable;
    bool m_allRecordsDurable;
    const authentication::Runtime* m_authorizationManagerOwner;
    void* m_contextProviderContext;
    InteractiveContextProviderFn m_contextProvider;
};

// Native boot installs the VFS adapter after mounting storage. Settings reads
// only value snapshots from this owner.
Result initializeRuntime();
Runtime& runtime();

} // namespace identity
} // namespace kernel

#endif
