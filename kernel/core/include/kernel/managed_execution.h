#ifndef KERNEL_MANAGED_EXECUTION_H
#define KERNEL_MANAGED_EXECUTION_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/identity.h"

namespace kernel {
namespace authentication { class Runtime; }
namespace managed_execution {

static const uint32_t kMaximumRecords = 16u;
static const uint32_t kApplicationIdCapacity = 80u;
static const uint16_t kSnapshotVersion = 1u;

enum class Kind : uint8_t {
    NativeElfManagedInstance = 1u
};

enum class State : uint8_t {
    Empty,
    Created,
    Running,
    Exited,
    Faulted,
    Yielded
};

enum class OwnerKind : uint8_t {
    KernelSystem,
    AuthenticatedAdministrator,
    AuthenticatedStandardUser,
    InteractiveUnassigned,
    InvalidOrUnavailable
};

enum class Result : uint8_t {
    Ok,
    InvalidArgument,
    CapacityExhausted,
    IdentifierExhausted,
    NotFound,
    StaleHandle,
    InvalidTransition,
    Unauthorized,
    ProtectedExecution,
    TerminationUnsupported,
    NotRunning,
    BufferTooSmall,
    NotInitialized
};

// Slot and generation locate the current bounded record; runtimeId is the
// monotonic identity that prevents a wrapped slot generation from reviving an
// old reference.
struct Handle {
    uint64_t runtimeId;
    uint32_t generation;
    uint8_t slot;
    uint8_t reserved[3];
};

struct SnapshotV1 {
    uint16_t version;
    Kind kind;
    State state;
    OwnerKind ownerKind;
    uint8_t hasExitStatus;
    uint8_t faultObserved;
    uint8_t resourcesReleased;
    uint8_t reserved;
    Handle handle;
    identity::UserId ownerUserId;
    uint64_t sessionGeneration;
    uint64_t startSequence;
    uint32_t exitStatus;
    uint32_t faultVector;
    uint32_t faultErrorCode;
    char appId[kApplicationIdCapacity];
};

// This is a bounded lifecycle registry for actual Native ELF execution,
// including cooperative scheduler-managed applications. It is not an isolation
// boundary. No application ABI exposes record creation or management calls.
class Runtime {
public:
    explicit Runtime(authentication::Runtime* authority = nullptr);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Freestanding kernel startup does not run C++ static constructors. The
    // global runtime therefore needs an explicit boot-time initialization.
    void initialize();

    // Called by the trusted Native ELF launcher only after image and policy
    // validation and immediately before control is transferred to app code.
    Result beginNativeElf(const char* canonicalAppId, Handle* output);
    Result markRunning(const Handle& handle);
    Result markYielded(const Handle& handle);
    Result discardBeforeEntry(const Handle& handle);
    Result complete(const Handle& handle, bool faultObserved,
                    bool hasExitStatus, uint32_t exitStatus,
                    uint32_t faultVector, uint32_t faultErrorCode,
                    bool resourcesReleased);

    // Query returns a value snapshot, never a pointer into registry storage.
    // Standard users see only records owned by the authenticated account;
    // administrators may inspect nonsecret snapshots from all owners.
    Result query(const Handle& handle, SnapshotV1* output) const;
    Result enumerate(SnapshotV1* output, size_t outputCapacity,
                     size_t* outputCount) const;

    // Authorization is enforced at this boundary. Since the runtime cannot
    // safely stop synchronous kernel-privileged Native ELF code, an authorized
    // request is reported as unsupported and never changes lifecycle state.
    Result requestTermination(const Handle& handle);

    uint32_t activeCount() const;

private:
    struct Record {
        uint8_t occupied;
        uint32_t generation;
        uint64_t runtimeId;
        uint64_t startSequence;
        Kind kind;
        State state;
        OwnerKind ownerKind;
        uint8_t hasExitStatus;
        uint8_t faultObserved;
        uint8_t resourcesReleased;
        identity::UserId ownerUserId;
        uint64_t sessionGeneration;
        uint32_t exitStatus;
        uint32_t faultVector;
        uint32_t faultErrorCode;
        char appId[kApplicationIdCapacity];
    };

    authentication::Runtime& authority() const;
    Result find(const Handle& handle, Record** output);
    Result find(const Handle& handle, const Record** output) const;
    Result copySnapshot(const Record& record, SnapshotV1* output) const;
    int selectSlot() const;

    authentication::Runtime* m_authority;
    Record m_records[kMaximumRecords];
    uint64_t m_nextRuntimeId;
    uint64_t m_nextStartSequence;
    uint32_t m_nextSlot;
    bool m_initialized;
};

const char* resultName(Result result);
Runtime& runtime();

} // namespace managed_execution
} // namespace kernel

#endif
