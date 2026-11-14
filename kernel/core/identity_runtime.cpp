#include "include/kernel/identity_runtime.h"

#include <string.h>

namespace kernel {
namespace identity {
namespace {

uint8_t s_slotBytes[2][kMaximumIdentityStoreBytes];
uint8_t s_writeBytes[kMaximumIdentityStoreBytes];
uint8_t s_verifyBytes[kMaximumIdentityStoreBytes];
Runtime s_runtime;

struct SlotRead {
    Result result;
    uint64_t generation;
    size_t byteCount;
};

bool successfulMutation(Result result)
{
    return result == Result::Ok || result == Result::OkPersistent ||
        result == Result::OkVolatile;
}

} // namespace

Runtime::Runtime()
    : m_registry(), m_storage{}, m_persistence(PersistenceState::Unavailable),
      m_generation(0u), m_activeSlot(-1), m_initialized(false),
      m_accountCountAvailable(false),
      m_allRecordsDurable(false)
{
}

Result Runtime::initialize(const StorageAdapter* storage)
{
    m_registry.initializeEmpty();
    m_storage = storage ? *storage : StorageAdapter{};
    m_persistence = PersistenceState::Unavailable;
    m_generation = 0u;
    m_activeSlot = -1;
    m_initialized = true;
    m_accountCountAvailable = true;
    m_allRecordsDurable = true;

    if (!m_storage.available || !m_storage.readSlot) {
        m_persistence = PersistenceState::Unavailable;
        return Result::Ok;
    }

    SlotRead slots[2] = {
        { Result::StorageUnavailable, 0u, 0u },
        { Result::StorageUnavailable, 0u, 0u }
    };
    bool sawIoError = false;
    bool sawUnavailable = false;
    bool sawUnsupported = false;
    bool sawCorrupt = false;
    for (uint8_t slot = 0; slot < 2u; ++slot) {
        size_t bytes = 0;
        const StoreReadResult readResult = m_storage.readSlot(
            m_storage.context, slot, s_slotBytes[slot],
            sizeof(s_slotBytes[slot]), &bytes);
        slots[slot].byteCount = bytes;
        if (readResult == StoreReadResult::Missing) {
            slots[slot].result = Result::NotFound;
        } else if (readResult == StoreReadResult::Unavailable) {
            slots[slot].result = Result::StorageUnavailable;
            sawUnavailable = true;
        } else if (readResult == StoreReadResult::Corrupt) {
            slots[slot].result = Result::CorruptRecord;
            sawCorrupt = true;
        } else if (readResult == StoreReadResult::IoError || bytes == 0u ||
                   bytes > sizeof(s_slotBytes[slot])) {
            slots[slot].result = Result::IoError;
            sawIoError = true;
        } else {
            uint64_t generation = 0u;
            Registry decoded;
            const Result decodedResult = decoded.load(
                s_slotBytes[slot], bytes, &generation);
            slots[slot].result = decodedResult;
            slots[slot].generation = generation;
            if (decodedResult == Result::Ok) {
                // Keep the decoded registry in a stable scratch copy only long
                // enough to select one complete slot below.
                if (slot == 0u) m_registry = decoded;
            } else if (decodedResult == Result::UnsupportedVersion) {
                sawUnsupported = true;
            } else {
                sawCorrupt = true;
            }
        }
    }

    const bool valid0 = slots[0].result == Result::Ok;
    const bool valid1 = slots[1].result == Result::Ok;
    if (valid0 && valid1 && slots[0].generation == slots[1].generation) {
        if (slots[0].byteCount != slots[1].byteCount ||
            memcmp(s_slotBytes[0], s_slotBytes[1], slots[0].byteCount) != 0) {
            m_persistence = PersistenceState::Corrupt;
            m_registry.initializeEmpty();
            m_accountCountAvailable = false;
            m_allRecordsDurable = false;
            return Result::StorageCorrupt;
        }
        m_activeSlot = 0;
        m_generation = slots[0].generation;
    } else if (valid0 && (!valid1 || slots[0].generation > slots[1].generation)) {
        uint64_t ignoredGeneration = 0u;
        const Result loaded = m_registry.load(
            s_slotBytes[0], slots[0].byteCount, &ignoredGeneration);
        if (loaded != Result::Ok) return loaded;
        m_activeSlot = 0;
        m_generation = slots[0].generation;
    } else if (valid1) {
        uint64_t ignoredGeneration = 0u;
        const Result loaded = m_registry.load(
            s_slotBytes[1], slots[1].byteCount, &ignoredGeneration);
        if (loaded != Result::Ok) return loaded;
        m_activeSlot = 1;
        m_generation = slots[1].generation;
    }

    const bool anyPresent = slots[0].result != Result::NotFound ||
        slots[1].result != Result::NotFound;
    if (sawUnsupported) {
        m_persistence = PersistenceState::Unsupported;
        m_accountCountAvailable = false;
        m_allRecordsDurable = m_activeSlot >= 0;
        return Result::UnsupportedVersion;
    }
    if (sawCorrupt) {
        // A valid older slot can still serve reads after an interrupted write,
        // but writes stop until the damaged slot is handled explicitly.
        m_persistence = PersistenceState::Corrupt;
        m_accountCountAvailable = false;
        m_allRecordsDurable = m_activeSlot >= 0 || !anyPresent;
        return Result::StorageCorrupt;
    }
    if (sawIoError) {
        m_persistence = PersistenceState::Unavailable;
        m_accountCountAvailable = false;
        m_allRecordsDurable = m_activeSlot >= 0 || !anyPresent;
        return Result::IoError;
    }
    if (sawUnavailable) {
        m_persistence = PersistenceState::Unavailable;
        m_accountCountAvailable = false;
        m_allRecordsDurable = m_activeSlot >= 0 || !anyPresent;
        return Result::StorageUnavailable;
    }

    m_persistence = m_storage.writable
        ? PersistenceState::Persistent : PersistenceState::ReadOnly;
    m_allRecordsDurable = true;
    return Result::Ok;
}

bool Runtime::initialized() const { return m_initialized; }

Result Runtime::commit(const Registry& candidate)
{
    if (!m_initialized) return Result::NotInitialized;
    if (m_persistence == PersistenceState::Corrupt) return Result::StorageCorrupt;
    if (m_persistence == PersistenceState::Unsupported) return Result::UnsupportedVersion;
    if (!m_storage.available || !m_storage.writable || !m_storage.writeSlot ||
        !m_storage.readSlot) return Result::StorageUnavailable;
    if (m_generation == UINT64_MAX) return Result::GenerationExhausted;

    const uint64_t nextGeneration = m_generation + 1u;
    size_t writeCount = 0u;
    const Result serialized = candidate.serialize(nextGeneration, s_writeBytes,
        sizeof(s_writeBytes), &writeCount);
    if (serialized != Result::Ok) return serialized;
    const uint8_t targetSlot = m_activeSlot < 0
        ? 0u : static_cast<uint8_t>(1 - m_activeSlot);
    const StoreWriteResult writeResult = m_storage.writeSlot(
        m_storage.context, targetSlot, s_writeBytes, writeCount);
    if (writeResult != StoreWriteResult::Ok) {
        return writeResult == StoreWriteResult::ReadOnly ||
            writeResult == StoreWriteResult::Unavailable
            ? Result::StorageUnavailable : Result::IoError;
    }

    size_t verifyCount = 0u;
    if (m_storage.readSlot(m_storage.context, targetSlot, s_verifyBytes,
            sizeof(s_verifyBytes), &verifyCount) != StoreReadResult::Ok ||
        verifyCount != writeCount || memcmp(s_verifyBytes, s_writeBytes, writeCount) != 0) {
        return Result::IoError;
    }
    Registry verified;
    uint64_t verifiedGeneration = 0u;
    if (verified.load(s_verifyBytes, verifyCount, &verifiedGeneration) != Result::Ok ||
        verifiedGeneration != nextGeneration) return Result::IoError;

    m_registry = candidate;
    m_generation = nextGeneration;
    m_activeSlot = static_cast<int8_t>(targetSlot);
    m_persistence = PersistenceState::Persistent;
    m_accountCountAvailable = true;
    m_allRecordsDurable = true;
    return Result::OkPersistent;
}

Result Runtime::provision(const char* accountName, const char* displayName, UserId* outId)
{
    if (outId) *outId = kInvalidUserId;
    if (!m_initialized) return Result::NotInitialized;
    Registry candidate = m_registry;
    const Result created = candidate.provision(accountName, displayName, outId);
    if (created != Result::Ok) return created;

    if (m_persistence == PersistenceState::ReadOnly ||
        m_persistence == PersistenceState::Unavailable) {
        m_registry = candidate;
        m_allRecordsDurable = false;
        return Result::OkVolatile;
    }
    const Result committed = commit(candidate);
    if (!successfulMutation(committed)) *outId = kInvalidUserId;
    return committed;
}

Result Runtime::find(UserId id, UserRecordV1* output) const
{
    if (!m_initialized) return Result::NotInitialized;
    return m_registry.find(id, output);
}

Result Runtime::enumerate(UserRecordV1* output, size_t capacity, size_t* outCount) const
{
    if (!m_initialized) return Result::NotInitialized;
    return m_registry.enumerate(output, capacity, outCount);
}

Result Runtime::setStatus(UserId id, AccountStatus status)
{
    if (!m_initialized) return Result::NotInitialized;
    Registry candidate = m_registry;
    const Result changed = candidate.setStatus(id, status);
    if (changed != Result::Ok) return changed;
    if (m_persistence == PersistenceState::ReadOnly ||
        m_persistence == PersistenceState::Unavailable) {
        m_registry = candidate;
        m_allRecordsDurable = false;
        return Result::OkVolatile;
    }
    return commit(candidate);
}

Result Runtime::erase(UserId id)
{
    if (!m_initialized) return Result::NotInitialized;
    Registry candidate = m_registry;
    const Result removed = candidate.erase(id);
    if (removed != Result::Ok) return removed;
    if (m_persistence == PersistenceState::ReadOnly ||
        m_persistence == PersistenceState::Unavailable) {
        m_registry = candidate;
        m_allRecordsDurable = false;
        return Result::OkVolatile;
    }
    return commit(candidate);
}

Result Runtime::queryContext(ExecutionDomain domain, IdentityContextV1* output) const
{
    if (!m_initialized) return Result::NotInitialized;
    if (!output) return Result::InvalidArgument;
    IdentityContextV1 context{};
    context.version = 1u;
    if (domain == ExecutionDomain::Kernel) {
        context.kind = IdentityKind::KernelSystem;
        context.userId = kSystemIdentityId;
    } else if (domain == ExecutionDomain::Interactive) {
        context.kind = IdentityKind::InteractiveUnassigned;
        context.userId = kInvalidUserId;
    } else {
        return Result::InvalidArgument;
    }
    *output = context;
    return Result::Ok;
}

Result Runtime::snapshot(RuntimeSnapshotV1* output) const
{
    if (!output) return Result::InvalidArgument;
    RuntimeSnapshotV1 snapshot{};
    snapshot.version = 1u;
    snapshot.initialized = m_initialized ? 1u : 0u;
    snapshot.accountCountAvailable = m_accountCountAvailable ? 1u : 0u;
    snapshot.persistence = m_persistence;
    snapshot.accountCount = static_cast<uint32_t>(m_registry.count());
    snapshot.generation = m_generation;
    snapshot.allRecordsDurable = m_allRecordsDurable ? 1u : 0u;
    *output = snapshot;
    return m_initialized ? Result::Ok : Result::NotInitialized;
}

Runtime& runtime() { return s_runtime; }

} // namespace identity
} // namespace kernel
