#ifndef KERNEL_IDENTITY_RUNTIME_H
#define KERNEL_IDENTITY_RUNTIME_H

#include "kernel/identity.h"

namespace kernel {
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

    Result initialize(const StorageAdapter* storage);
    bool initialized() const;
    Result provision(const char* accountName, const char* displayName, UserId* outId);
    Result find(UserId id, UserRecordV1* output) const;
    Result enumerate(UserRecordV1* output, size_t capacity, size_t* outCount) const;
    Result setStatus(UserId id, AccountStatus status);
    Result erase(UserId id);
    Result queryContext(ExecutionDomain domain, IdentityContextV1* output) const;
    Result snapshot(RuntimeSnapshotV1* output) const;

private:
    Result commit(const Registry& candidate);

    Registry m_registry;
    StorageAdapter m_storage;
    PersistenceState m_persistence;
    uint64_t m_generation;
    int8_t m_activeSlot;
    bool m_initialized;
    bool m_accountCountAvailable;
    bool m_allRecordsDurable;
};

// Native boot installs the VFS adapter after mounting storage. Settings reads
// only value snapshots from this owner.
Result initializeRuntime();
Runtime& runtime();

} // namespace identity
} // namespace kernel

#endif
