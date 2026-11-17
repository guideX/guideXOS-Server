#include "include/kernel/managed_execution.h"

#include "include/kernel/application_execution_policy.h"
#include "include/kernel/authentication.h"

namespace kernel {
namespace managed_execution {
namespace {

Runtime s_runtime;

static bool copy_app_id(char* destination, size_t capacity, const char* source)
{
    if (!destination || capacity == 0u ||
        !application_policy::validCanonicalAppId(source)) return false;
    size_t i = 0u;
    for (; i + 1u < capacity && source[i] != '\0'; ++i)
        destination[i] = source[i];
    if (source[i] != '\0') return false;
    destination[i] = '\0';
    return true;
}

static OwnerKind owner_kind(authentication::ExecutionAuthorityKind kind)
{
    switch (kind) {
        case authentication::ExecutionAuthorityKind::AuthenticatedAdministrator:
            return OwnerKind::AuthenticatedAdministrator;
        case authentication::ExecutionAuthorityKind::AuthenticatedStandardUser:
            return OwnerKind::AuthenticatedStandardUser;
        case authentication::ExecutionAuthorityKind::InteractiveUnassigned:
            return OwnerKind::InteractiveUnassigned;
        case authentication::ExecutionAuthorityKind::Invalidated:
        case authentication::ExecutionAuthorityKind::Unavailable:
        default:
            return OwnerKind::InvalidOrUnavailable;
    }
}

static bool terminal(State state)
{
    return state == State::Exited || state == State::Faulted;
}

} // namespace

Runtime::Runtime(authentication::Runtime* authorityRuntime)
    : m_authority(authorityRuntime), m_records{}, m_nextRuntimeId(1u),
      m_nextStartSequence(1u), m_nextSlot(0u), m_initialized(true)
{
}

void Runtime::initialize()
{
    if (m_initialized) return;
    for (uint32_t i = 0u; i < kMaximumRecords; ++i)
        m_records[i] = Record{};
    m_nextRuntimeId = 1u;
    m_nextStartSequence = 1u;
    m_nextSlot = 0u;
    m_initialized = true;
}

authentication::Runtime& Runtime::authority() const
{
    return m_authority ? *m_authority : authentication::runtime();
}

int Runtime::selectSlot() const
{
    for (uint32_t offset = 0u; offset < kMaximumRecords; ++offset) {
        const uint32_t slot = (m_nextSlot + offset) % kMaximumRecords;
        if (!m_records[slot].occupied) return static_cast<int>(slot);
    }

    int oldestSlot = -1;
    uint64_t oldestSequence = UINT64_MAX;
    for (uint32_t offset = 0u; offset < kMaximumRecords; ++offset) {
        const uint32_t slot = (m_nextSlot + offset) % kMaximumRecords;
        const Record& record = m_records[slot];
        if (terminal(record.state) && record.startSequence < oldestSequence) {
            oldestSlot = static_cast<int>(slot);
            oldestSequence = record.startSequence;
        }
    }
    return oldestSlot;
}

Result Runtime::beginNativeElf(const char* canonicalAppId, Handle* output)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output || !application_policy::validCanonicalAppId(canonicalAppId))
        return Result::InvalidArgument;
    if (m_nextRuntimeId == 0u || m_nextStartSequence == 0u)
        return Result::IdentifierExhausted;
    if (activeCount() != 0u) return Result::CapacityExhausted;
    const int selectedSlot = selectSlot();
    if (selectedSlot < 0) return Result::CapacityExhausted;

    authentication::ExecutionAuthorityV1 authoritySnapshot{};
    const authentication::Result authorityResult =
        authority().queryExecutionAuthority(&authoritySnapshot);
    OwnerKind capturedOwner = OwnerKind::InvalidOrUnavailable;
    identity::UserId capturedUser = identity::kInvalidUserId;
    uint64_t capturedSessionGeneration = 0u;
    if (authorityResult == authentication::Result::Ok &&
        authoritySnapshot.version == 1u) {
        capturedOwner = owner_kind(authoritySnapshot.kind);
        if (capturedOwner == OwnerKind::AuthenticatedAdministrator ||
            capturedOwner == OwnerKind::AuthenticatedStandardUser) {
            capturedUser = authoritySnapshot.userId;
            capturedSessionGeneration = authoritySnapshot.sessionGeneration;
            if (capturedUser == identity::kInvalidUserId ||
                capturedSessionGeneration == 0u) {
                capturedOwner = OwnerKind::InvalidOrUnavailable;
                capturedUser = identity::kInvalidUserId;
                capturedSessionGeneration = 0u;
            }
        }
    }

    Record& record = m_records[static_cast<uint32_t>(selectedSlot)];
    uint32_t generation = record.generation + 1u;
    if (generation == 0u) generation = 1u;
    record = Record{};
    record.occupied = 1u;
    record.generation = generation;
    record.runtimeId = m_nextRuntimeId;
    record.startSequence = m_nextStartSequence;
    record.kind = Kind::NativeElfManagedInstance;
    record.state = State::Created;
    record.ownerKind = capturedOwner;
    record.ownerUserId = capturedUser;
    record.sessionGeneration = capturedSessionGeneration;
    if (!copy_app_id(record.appId, sizeof(record.appId), canonicalAppId)) {
        record = Record{};
        return Result::InvalidArgument;
    }
    output->runtimeId = record.runtimeId;
    output->generation = record.generation;
    output->slot = static_cast<uint8_t>(selectedSlot);
    output->reserved[0] = output->reserved[1] = output->reserved[2] = 0u;

    m_nextRuntimeId = m_nextRuntimeId == UINT64_MAX ? 0u : m_nextRuntimeId + 1u;
    m_nextStartSequence = m_nextStartSequence == UINT64_MAX ? 0u : m_nextStartSequence + 1u;
    m_nextSlot = (static_cast<uint32_t>(selectedSlot) + 1u) % kMaximumRecords;
    return Result::Ok;
}

Result Runtime::find(const Handle& handle, Record** output)
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output || handle.slot >= kMaximumRecords || handle.runtimeId == 0u ||
        handle.generation == 0u) return Result::InvalidArgument;
    Record& record = m_records[handle.slot];
    if (!record.occupied || record.runtimeId != handle.runtimeId ||
        record.generation != handle.generation) return Result::StaleHandle;
    *output = &record;
    return Result::Ok;
}

Result Runtime::find(const Handle& handle, const Record** output) const
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output || handle.slot >= kMaximumRecords || handle.runtimeId == 0u ||
        handle.generation == 0u) return Result::InvalidArgument;
    const Record& record = m_records[handle.slot];
    if (!record.occupied || record.runtimeId != handle.runtimeId ||
        record.generation != handle.generation) return Result::StaleHandle;
    *output = &record;
    return Result::Ok;
}

Result Runtime::markRunning(const Handle& handle)
{
    Record* record = nullptr;
    const Result found = find(handle, &record);
    if (found != Result::Ok) return found;
    if (record->state != State::Created && record->state != State::Yielded)
        return Result::InvalidTransition;
    record->state = State::Running;
    return Result::Ok;
}

Result Runtime::markYielded(const Handle& handle)
{
    Record* record = nullptr;
    const Result found = find(handle, &record);
    if (found != Result::Ok) return found;
    if (record->state != State::Running) return Result::InvalidTransition;
    record->state = State::Yielded;
    return Result::Ok;
}

Result Runtime::discardBeforeEntry(const Handle& handle)
{
    Record* record = nullptr;
    const Result found = find(handle, &record);
    if (found != Result::Ok) return found;
    if (record->state != State::Created) return Result::InvalidTransition;
    record->occupied = 0u;
    record->state = State::Empty;
    return Result::Ok;
}

Result Runtime::complete(const Handle& handle, bool faultObserved,
                         bool hasExitStatus, uint32_t exitStatus,
                         uint32_t faultVector, uint32_t faultErrorCode,
                         bool resourcesReleased)
{
    Record* record = nullptr;
    const Result found = find(handle, &record);
    if (found != Result::Ok) return found;
    if (record->state != State::Running && record->state != State::Yielded)
        return Result::InvalidTransition;
    record->state = faultObserved ? State::Faulted : State::Exited;
    record->faultObserved = faultObserved ? 1u : 0u;
    record->hasExitStatus = (!faultObserved && hasExitStatus) ? 1u : 0u;
    record->exitStatus = record->hasExitStatus ? exitStatus : 0u;
    record->faultVector = faultObserved ? faultVector : 0u;
    record->faultErrorCode = faultObserved ? faultErrorCode : 0u;
    record->resourcesReleased = resourcesReleased ? 1u : 0u;
    return Result::Ok;
}

Result Runtime::copySnapshot(const Record& record, SnapshotV1* output) const
{
    if (!output || !record.occupied) return Result::InvalidArgument;
    SnapshotV1 snapshot{};
    snapshot.version = kSnapshotVersion;
    snapshot.kind = record.kind;
    snapshot.state = record.state;
    snapshot.ownerKind = record.ownerKind;
    snapshot.hasExitStatus = record.hasExitStatus;
    snapshot.faultObserved = record.faultObserved;
    snapshot.resourcesReleased = record.resourcesReleased;
    snapshot.handle.runtimeId = record.runtimeId;
    snapshot.handle.generation = record.generation;
    snapshot.handle.slot = static_cast<uint8_t>(&record - m_records);
    snapshot.ownerUserId = record.ownerUserId;
    snapshot.sessionGeneration = record.sessionGeneration;
    snapshot.startSequence = record.startSequence;
    snapshot.exitStatus = record.exitStatus;
    snapshot.faultVector = record.faultVector;
    snapshot.faultErrorCode = record.faultErrorCode;
    for (uint32_t i = 0u; i < kApplicationIdCapacity; ++i)
        snapshot.appId[i] = record.appId[i];
    *output = snapshot;
    return Result::Ok;
}

Result Runtime::query(const Handle& handle, SnapshotV1* output) const
{
    if (!output) return Result::InvalidArgument;
    const Record* record = nullptr;
    const Result found = find(handle, &record);
    if (found != Result::Ok) return found;
    const identity::UserId target = record->ownerKind == OwnerKind::KernelSystem
        ? identity::kSystemIdentityId : record->ownerUserId;
    if (!authority().authorizeCurrent(authorization::Operation::ProcessQuery,
            target).allowed()) return Result::Unauthorized;
    return copySnapshot(*record, output);
}

Result Runtime::enumerate(SnapshotV1* output, size_t outputCapacity,
                          size_t* outputCount) const
{
    if (!m_initialized) return Result::NotInitialized;
    if (!outputCount || (outputCapacity != 0u && !output))
        return Result::InvalidArgument;
    SnapshotV1 staged[kMaximumRecords]{};
    size_t stagedCount = 0u;
    for (uint32_t i = 0u; i < kMaximumRecords; ++i) {
        const Record& record = m_records[i];
        if (!record.occupied) continue;
        const identity::UserId target = record.ownerKind == OwnerKind::KernelSystem
            ? identity::kSystemIdentityId : record.ownerUserId;
        if (!authority().authorizeCurrent(authorization::Operation::ProcessQuery,
                target).allowed()) continue;
        if (copySnapshot(record, &staged[stagedCount]) == Result::Ok)
            ++stagedCount;
    }
    *outputCount = stagedCount;
    if (stagedCount > outputCapacity) return Result::BufferTooSmall;
    for (size_t i = 0u; i < stagedCount; ++i) output[i] = staged[i];
    return Result::Ok;
}

Result Runtime::requestTermination(const Handle& handle)
{
    const Record* record = nullptr;
    const Result found = find(handle, &record);
    if (found != Result::Ok) return found;

    authorization::Operation operation = authorization::Operation::ProcessTerminateOther;
    identity::UserId target = record->ownerUserId;
    if (record->ownerKind == OwnerKind::KernelSystem) {
        operation = authorization::Operation::ProcessPrivilegedManagement;
        target = identity::kSystemIdentityId;
    } else {
        authentication::ExecutionAuthorityV1 caller{};
        if (authority().queryExecutionAuthority(&caller) == authentication::Result::Ok &&
            (caller.kind == authentication::ExecutionAuthorityKind::AuthenticatedAdministrator ||
             caller.kind == authentication::ExecutionAuthorityKind::AuthenticatedStandardUser) &&
            caller.userId == record->ownerUserId) {
            operation = authorization::Operation::ProcessTerminateOwn;
        }
    }

    if (!authority().authorizeCurrent(operation, target).allowed())
        return Result::Unauthorized;
    if (record->ownerKind == OwnerKind::KernelSystem)
        return Result::ProtectedExecution;
    if (record->state != State::Running && record->state != State::Yielded)
        return Result::NotRunning;
    return Result::TerminationUnsupported;
}

uint32_t Runtime::activeCount() const
{
    if (!m_initialized) return 0u;
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < kMaximumRecords; ++i) {
        if (m_records[i].occupied &&
            (m_records[i].state == State::Created ||
             m_records[i].state == State::Running ||
             m_records[i].state == State::Yielded)) ++count;
    }
    return count;
}

const char* resultName(Result result)
{
    switch (result) {
        case Result::Ok: return "Ok";
        case Result::InvalidArgument: return "InvalidArgument";
        case Result::CapacityExhausted: return "CapacityExhausted";
        case Result::IdentifierExhausted: return "IdentifierExhausted";
        case Result::NotFound: return "NotFound";
        case Result::StaleHandle: return "StaleHandle";
        case Result::InvalidTransition: return "InvalidTransition";
        case Result::Unauthorized: return "Unauthorized";
        case Result::ProtectedExecution: return "ProtectedExecution";
        case Result::TerminationUnsupported: return "TerminationUnsupported";
        case Result::NotRunning: return "NotRunning";
        case Result::BufferTooSmall: return "BufferTooSmall";
        case Result::NotInitialized: return "NotInitialized";
        default: return "Unknown";
    }
}

Runtime& runtime() { return s_runtime; }

} // namespace managed_execution
} // namespace kernel
