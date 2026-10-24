#pragma once

// JS86's bounded identity/root proof. This header is test-only; production
// RuntimeContext and Value do not include or use these types.

#include <array>
#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {
namespace js86 {

enum class Status : std::uint8_t {
    None = 0,
    InvalidHandle,
    StaleHandle,
    SlotTableFull,
    GenerationExhausted,
    InvalidOwner,
    StaleOwner,
    OwnerTableFull,
    RootTableFull,
    InvalidRootToken,
    StaleRootToken,
    InvalidRootHandle,
    GraphFull,
    InvalidGraphEdge,
    UnknownRoots,
    ObjectStillReachable,
};

constexpr std::uint32_t kHandleSlotBits = 10u;
constexpr std::uint32_t kHandleSlotCapacity = 1u << kHandleSlotBits;
constexpr std::uint32_t kHandleSlotMask = kHandleSlotCapacity - 1u;
constexpr std::uint32_t kHandleGenerationBits = 32u - kHandleSlotBits;
constexpr std::uint32_t kHandleReservedGeneration =
    (1u << kHandleGenerationBits) - 1u;
// Reserve generation zero and the all-ones packed sentinel for invalid.
constexpr std::uint32_t kMaximumHandleGeneration =
    kHandleReservedGeneration - 1u;
constexpr std::uint32_t kInvalidPackedHandle = 0xffffffffu;

class ObjectHandle {
public:
    constexpr ObjectHandle() = default;

    static constexpr ObjectHandle make(std::uint32_t slot,
        std::uint32_t generation)
    {
        return slot < kHandleSlotCapacity && generation != 0u &&
            generation <= kMaximumHandleGeneration
            ? ObjectHandle((generation << kHandleSlotBits) | slot)
            : ObjectHandle();
    }

    // Used only by failure-injection tests to model corrupted native bits.
    static constexpr ObjectHandle fromRawForTesting(std::uint32_t raw)
    {
        return ObjectHandle(raw);
    }

    constexpr bool isInvalid() const { return raw_ == kInvalidPackedHandle; }
    constexpr std::uint32_t raw() const { return raw_; }
    constexpr std::uint32_t slot() const { return raw_ & kHandleSlotMask; }
    constexpr std::uint32_t generation() const
    {
        return raw_ >> kHandleSlotBits;
    }
    constexpr bool hasValidEncoding() const
    {
        return !isInvalid() && generation() != 0u &&
            generation() <= kMaximumHandleGeneration;
    }

    friend constexpr bool operator==(ObjectHandle left, ObjectHandle right)
    {
        return left.raw_ == right.raw_;
    }
    friend constexpr bool operator!=(ObjectHandle left, ObjectHandle right)
    {
        return !(left == right);
    }

private:
    explicit constexpr ObjectHandle(std::uint32_t raw) : raw_(raw) {}
    std::uint32_t raw_ = kInvalidPackedHandle;
};

static_assert(sizeof(ObjectHandle) == sizeof(std::uint32_t),
    "the packed object handle must stay one 32-bit word");

template <std::size_t Capacity,
    std::uint32_t GenerationLimit = kMaximumHandleGeneration>
class IdentityTable {
public:
    struct SlotRecord {
        std::uint32_t generation = 0u;
        std::uint32_t payload = 0u;
        bool live = false;
        bool quarantined = false;
    };

    static_assert(Capacity > 0u, "identity table must have at least one slot");
    static_assert(Capacity <= kHandleSlotCapacity,
        "identity table exceeds the packed slot field");
    static_assert(GenerationLimit > 0u &&
        GenerationLimit <= kMaximumHandleGeneration,
        "generation limit must fit the packed field");

    bool allocate(std::uint32_t payload, ObjectHandle& result,
        Status& status)
    {
        result = ObjectHandle();
        status = Status::None;
        bool foundReusable = false;
        bool sawExhaustion = false;
        bool sawLive = false;
        for (std::size_t index = 0u; index < Capacity; ++index) {
            SlotRecord& slot = slots_[index];
            if (slot.live) {
                sawLive = true;
                continue;
            }
            if (slot.quarantined) continue;
            if (slot.generation >= GenerationLimit) {
                slot.quarantined = true;
                sawExhaustion = true;
                continue;
            }
            foundReusable = true;
            ++slot.generation;
            slot.payload = payload;
            slot.live = true;
            result = ObjectHandle::make(static_cast<std::uint32_t>(index),
                slot.generation);
            return true;
        }
        status = !foundReusable && !sawLive && sawExhaustion
            ? Status::GenerationExhausted : Status::SlotTableFull;
        return false;
    }

    bool resolve(ObjectHandle handle, std::uint32_t& payload,
        Status& status) const
    {
        const SlotRecord* slot = nullptr;
        if (!liveRecord(handle, slot, status)) return false;
        payload = slot->payload;
        return true;
    }

    bool retire(ObjectHandle handle, Status& status)
    {
        SlotRecord* slot = nullptr;
        if (!liveRecord(handle, slot, status)) return false;
        slot->live = false;
        slot->payload = 0u;
        return true;
    }

    void reset()
    {
        // Keep generations and quarantine bits so pre-reset handles can never
        // identify post-reset allocations in this model.
        for (SlotRecord& slot : slots_) {
            slot.live = false;
            slot.payload = 0u;
        }
    }

    std::size_t liveCount() const
    {
        std::size_t count = 0u;
        for (const SlotRecord& slot : slots_)
            if (slot.live) ++count;
        return count;
    }

    std::size_t quarantinedCount() const
    {
        std::size_t count = 0u;
        for (const SlotRecord& slot : slots_)
            if (slot.quarantined) ++count;
        return count;
    }

    static constexpr std::size_t capacity() { return Capacity; }
    const SlotRecord& slotAt(std::size_t index) const { return slots_[index]; }

private:
    bool liveRecord(ObjectHandle handle, const SlotRecord*& result,
        Status& status) const
    {
        result = nullptr;
        status = Status::None;
        if (!handle.hasValidEncoding() || handle.slot() >= Capacity ||
            handle.generation() > GenerationLimit) {
            status = Status::InvalidHandle;
            return false;
        }
        const SlotRecord& slot = slots_[handle.slot()];
        if (!slot.live || slot.generation != handle.generation()) {
            status = Status::StaleHandle;
            return false;
        }
        result = &slot;
        return true;
    }

    bool liveRecord(ObjectHandle handle, SlotRecord*& result, Status& status)
    {
        const SlotRecord* constRecord = nullptr;
        if (!static_cast<const IdentityTable&>(*this).liveRecord(handle,
                constRecord, status)) {
            result = nullptr;
            return false;
        }
        result = &slots_[handle.slot()];
        return true;
    }

    std::array<SlotRecord, Capacity> slots_{};
};

struct OwnerToken {
    std::uint32_t slot = 0xffffffffu;
    std::uint32_t generation = 0u;

    friend constexpr bool operator==(OwnerToken left, OwnerToken right)
    {
        return left.slot == right.slot && left.generation == right.generation;
    }
    friend constexpr bool operator!=(OwnerToken left, OwnerToken right)
    {
        return !(left == right);
    }
};

struct RootToken {
    std::uint32_t slot = 0xffffffffu;
    std::uint32_t generation = 0u;
    OwnerToken owner;
};

template <std::size_t RootCapacity, std::size_t OwnerCapacity,
    std::uint32_t GenerationLimit = kMaximumHandleGeneration>
class RootRegistry {
public:
    struct RootRecord {
        ObjectHandle handle;
        OwnerToken owner;
        std::uint32_t generation = 0u;
        bool active = false;
        bool quarantined = false;
    };

    struct OwnerRecord {
        std::uint32_t generation = 0u;
        bool active = false;
        bool quarantined = false;
    };

    static_assert(RootCapacity > 0u && OwnerCapacity > 0u,
        "root and owner tables must have at least one record");
    static_assert(GenerationLimit > 0u &&
        GenerationLimit <= kMaximumHandleGeneration,
        "registration generations must fit the selected bound");

    bool beginOwner(OwnerToken& result, Status& status)
    {
        result = OwnerToken();
        status = Status::None;
        bool sawReusable = false;
        bool sawExhaustion = false;
        bool sawActive = false;
        for (std::size_t index = 0u; index < OwnerCapacity; ++index) {
            OwnerRecord& owner = owners_[index];
            if (owner.active) {
                sawActive = true;
                continue;
            }
            if (owner.quarantined) continue;
            if (owner.generation >= GenerationLimit) {
                owner.quarantined = true;
                sawExhaustion = true;
                continue;
            }
            sawReusable = true;
            ++owner.generation;
            owner.active = true;
            result = OwnerToken{static_cast<std::uint32_t>(index),
                owner.generation};
            return true;
        }
        status = !sawReusable && !sawActive && sawExhaustion
            ? Status::GenerationExhausted : Status::OwnerTableFull;
        return false;
    }

    template <std::size_t ObjectCapacity, std::uint32_t ObjectGenerationLimit>
    bool registerRoot(ObjectHandle handle, OwnerToken owner,
        const IdentityTable<ObjectCapacity, ObjectGenerationLimit>& objects,
        RootToken& result, Status& status)
    {
        result = RootToken();
        if (!activeOwner(owner, status)) return false;
        std::uint32_t ignoredPayload = 0u;
        if (!objects.resolve(handle, ignoredPayload, status)) return false;

        bool sawReusable = false;
        bool sawExhaustion = false;
        bool sawActive = false;
        for (std::size_t index = 0u; index < RootCapacity; ++index) {
            RootRecord& record = roots_[index];
            if (record.active) {
                sawActive = true;
                continue;
            }
            if (record.quarantined) continue;
            if (record.generation >= GenerationLimit) {
                record.quarantined = true;
                sawExhaustion = true;
                continue;
            }
            sawReusable = true;
            const std::uint32_t nextGeneration = record.generation + 1u;
            // Publish only after every validation and capacity check succeeds.
            record.handle = handle;
            record.owner = owner;
            record.generation = nextGeneration;
            record.active = true;
            result = RootToken{static_cast<std::uint32_t>(index),
                nextGeneration, owner};
            return true;
        }
        status = !sawReusable && !sawActive && sawExhaustion
            ? Status::GenerationExhausted : Status::RootTableFull;
        return false;
    }

    bool releaseRoot(RootToken token, Status& status)
    {
        status = Status::None;
        if (!validRootTokenEncoding(token)) {
            status = Status::InvalidRootToken;
            return false;
        }
        if (token.slot >= RootCapacity) {
            status = Status::InvalidRootToken;
            return false;
        }
        RootRecord& record = roots_[token.slot];
        if (!record.active || record.generation != token.generation ||
            record.owner != token.owner || !ownerIsActive(token.owner)) {
            status = Status::StaleRootToken;
            return false;
        }
        record.active = false;
        record.handle = ObjectHandle();
        record.owner = OwnerToken();
        return true;
    }

    bool endOwner(OwnerToken token, Status& status)
    {
        status = Status::None;
        OwnerRecord* ownerRecord = nullptr;
        if (!activeOwner(token, status, &ownerRecord)) return false;
        for (RootRecord& root : roots_) {
            if (root.active && root.owner == token) {
                root.active = false;
                root.handle = ObjectHandle();
                root.owner = OwnerToken();
            }
        }
        ownerRecord->active = false;
        return true;
    }

    void reset()
    {
        // Registration generations survive reset to prevent token aliasing.
        for (RootRecord& root : roots_) {
            root.active = false;
            root.handle = ObjectHandle();
            root.owner = OwnerToken();
        }
        for (OwnerRecord& owner : owners_) owner.active = false;
    }

    std::size_t activeRootCount(ObjectHandle handle) const
    {
        std::size_t count = 0u;
        for (const RootRecord& root : roots_)
            if (root.active && root.handle == handle) ++count;
        return count;
    }

    std::size_t activeRootCount() const
    {
        std::size_t count = 0u;
        for (const RootRecord& root : roots_)
            if (root.active) ++count;
        return count;
    }

    std::size_t activeOwnerCount() const
    {
        std::size_t count = 0u;
        for (const OwnerRecord& owner : owners_)
            if (owner.active) ++count;
        return count;
    }

    bool rootHandleAt(std::size_t index, ObjectHandle& handle) const
    {
        if (index >= RootCapacity || !roots_[index].active) return false;
        handle = roots_[index].handle;
        return true;
    }

    bool hasActiveOwner(OwnerToken token) const
    {
        return ownerIsActive(token);
    }

    static constexpr std::size_t rootCapacity() { return RootCapacity; }
    static constexpr std::size_t ownerCapacity() { return OwnerCapacity; }

private:
    static bool validRootTokenEncoding(const RootToken& token)
    {
        return token.slot != 0xffffffffu && token.generation != 0u &&
            token.owner.slot != 0xffffffffu &&
            token.owner.generation != 0u;
    }

    bool ownerIsActive(OwnerToken token) const
    {
        return token.slot < OwnerCapacity && token.generation != 0u &&
            owners_[token.slot].active &&
            owners_[token.slot].generation == token.generation;
    }

    bool activeOwner(OwnerToken token, Status& status,
        OwnerRecord** record = nullptr)
    {
        status = Status::None;
        if (record != nullptr) *record = nullptr;
        if (token.slot == 0xffffffffu || token.generation == 0u ||
            token.slot >= OwnerCapacity) {
            status = Status::InvalidOwner;
            return false;
        }
        OwnerRecord& candidate = owners_[token.slot];
        if (!candidate.active || candidate.generation != token.generation) {
            status = Status::StaleOwner;
            return false;
        }
        if (record != nullptr) *record = &candidate;
        return true;
    }

    bool activeOwner(OwnerToken token, Status& status) const
    {
        status = Status::None;
        if (token.slot == 0xffffffffu || token.generation == 0u ||
            token.slot >= OwnerCapacity) {
            status = Status::InvalidOwner;
            return false;
        }
        if (!ownerIsActive(token)) {
            status = Status::StaleOwner;
            return false;
        }
        return true;
    }

    std::array<RootRecord, RootCapacity> roots_{};
    std::array<OwnerRecord, OwnerCapacity> owners_{};
};

template <typename Registry, typename Objects>
class NativeRootScope {
public:
    NativeRootScope(Registry& registry, const Objects& objects,
        Status& status)
        : registry_(&registry), objects_(&objects)
    {
        active_ = registry_->beginOwner(owner_, status);
    }

    NativeRootScope(const NativeRootScope&) = delete;
    NativeRootScope& operator=(const NativeRootScope&) = delete;
    NativeRootScope(NativeRootScope&&) = delete;
    NativeRootScope& operator=(NativeRootScope&&) = delete;

    ~NativeRootScope()
    {
        if (active_) {
            Status ignored = Status::None;
            registry_->endOwner(owner_, ignored);
        }
    }

    bool registerValue(ObjectHandle value, RootToken& token, Status& status)
    {
        token = RootToken();
        if (!active_) {
            status = Status::StaleOwner;
            return false;
        }
        return registry_->registerRoot(value, owner_, *objects_, token, status);
    }

    bool release(RootToken token, Status& status)
    {
        if (!active_) {
            status = Status::StaleOwner;
            return false;
        }
        return registry_->releaseRoot(token, status);
    }

    bool close(Status& status)
    {
        if (!active_) {
            status = Status::StaleOwner;
            return false;
        }
        const bool closed = registry_->endOwner(owner_, status);
        active_ = false;
        return closed;
    }

    bool active() const { return active_; }
    OwnerToken owner() const { return owner_; }

private:
    Registry* registry_ = nullptr;
    const Objects* objects_ = nullptr;
    OwnerToken owner_;
    bool active_ = false;
};

enum class EdgeKind : std::uint8_t {
    ObjectProperty = 0,
    ArrayElement,
    Prototype,
};

template <std::size_t SlotCapacity, std::size_t EdgeCapacity>
class ReferenceGraph {
public:
    struct EdgeRecord {
        ObjectHandle from;
        ObjectHandle to;
        EdgeKind kind = EdgeKind::ObjectProperty;
        bool active = false;
    };

    static_assert(SlotCapacity > 0u && EdgeCapacity > 0u,
        "graph must have bounded node and edge capacity");
    static_assert(SlotCapacity <= kHandleSlotCapacity,
        "graph exceeds the packed object handle slot field");

    template <std::uint32_t GenerationLimit>
    bool addEdge(ObjectHandle from, ObjectHandle to, EdgeKind kind,
        const IdentityTable<SlotCapacity, GenerationLimit>& objects,
        Status& status)
    {
        status = Status::None;
        std::uint32_t payload = 0u;
        if (!objects.resolve(from, payload, status) ||
            !objects.resolve(to, payload, status)) {
            status = Status::InvalidGraphEdge;
            return false;
        }
        for (EdgeRecord& edge : edges_) {
            if (edge.active) continue;
            edge.from = from;
            edge.to = to;
            edge.kind = kind;
            edge.active = true;
            return true;
        }
        status = Status::GraphFull;
        return false;
    }

    // The test-only corruption hook exercises fail-closed edge validation.
    bool injectInvalidEdgeForTesting(std::size_t index,
        ObjectHandle from, ObjectHandle to)
    {
        if (index >= EdgeCapacity) return false;
        edges_[index] = EdgeRecord{from, to, EdgeKind::ObjectProperty, true};
        return true;
    }

    template <std::uint32_t GenerationLimit, std::size_t RootCapacity,
        std::size_t OwnerCapacity, std::uint32_t RootGenerationLimit>
    bool reachesFromRoots(ObjectHandle target,
        const IdentityTable<SlotCapacity, GenerationLimit>& objects,
        const RootRegistry<RootCapacity, OwnerCapacity,
            RootGenerationLimit>& roots,
        bool& reachable, Status& status) const
    {
        reachable = false;
        status = Status::None;
        std::uint32_t payload = 0u;
        if (!objects.resolve(target, payload, status)) return false;

        for (const EdgeRecord& edge : edges_) {
            if (!edge.active) continue;
            if (!objects.resolve(edge.from, payload, status) ||
                !objects.resolve(edge.to, payload, status)) {
                status = Status::InvalidGraphEdge;
                return false;
            }
        }

        std::array<bool, SlotCapacity> visited{};
        std::array<ObjectHandle, SlotCapacity> queue{};
        std::size_t head = 0u;
        std::size_t tail = 0u;
        for (std::size_t index = 0u; index < RootCapacity; ++index) {
            ObjectHandle root;
            if (!roots.rootHandleAt(index, root)) continue;
            if (!objects.resolve(root, payload, status)) {
                status = Status::InvalidRootHandle;
                return false;
            }
            if (root == target) {
                reachable = true;
                return true;
            }
            if (!visited[root.slot()]) {
                if (tail >= SlotCapacity) {
                    status = Status::InvalidGraphEdge;
                    return false;
                }
                visited[root.slot()] = true;
                queue[tail++] = root;
            }
        }

        while (head < tail) {
            const ObjectHandle current = queue[head++];
            for (const EdgeRecord& edge : edges_) {
                if (!edge.active || edge.from != current) continue;
                if (edge.to == target) {
                    reachable = true;
                    return true;
                }
                if (!visited[edge.to.slot()]) {
                    if (tail >= SlotCapacity) {
                        status = Status::InvalidGraphEdge;
                        return false;
                    }
                    visited[edge.to.slot()] = true;
                    queue[tail++] = edge.to;
                }
            }
        }
        return true;
    }

    void removeObject(ObjectHandle handle)
    {
        for (EdgeRecord& edge : edges_)
            if (edge.active && (edge.from == handle || edge.to == handle))
                edge.active = false;
    }

    void reset()
    {
        for (EdgeRecord& edge : edges_) edge.active = false;
    }

    std::size_t activeEdgeCount() const
    {
        std::size_t count = 0u;
        for (const EdgeRecord& edge : edges_)
            if (edge.active) ++count;
        return count;
    }

    static constexpr std::size_t nodeCapacity() { return SlotCapacity; }
    static constexpr std::size_t edgeCapacity() { return EdgeCapacity; }

private:
    std::array<EdgeRecord, EdgeCapacity> edges_{};
};

enum class RootCoverage : std::uint8_t {
    CompleteForModel = 0,
    UnknownExternalRoots,
};

template <std::size_t SlotCapacity, std::size_t RootCapacity,
    std::size_t OwnerCapacity, std::size_t EdgeCapacity,
    std::uint32_t GenerationLimit = kMaximumHandleGeneration>
class BoundedIdentityModel {
public:
    using Objects = IdentityTable<SlotCapacity, GenerationLimit>;
    using Roots = RootRegistry<RootCapacity, OwnerCapacity, GenerationLimit>;
    using Graph = ReferenceGraph<SlotCapacity, EdgeCapacity>;

    bool allocate(std::uint32_t payload, ObjectHandle& handle, Status& status)
    {
        return objects_.allocate(payload, handle, status);
    }

    bool addEdge(ObjectHandle from, ObjectHandle to, EdgeKind kind,
        Status& status)
    {
        return graph_.addEdge(from, to, kind, objects_, status);
    }

    bool mayRetire(ObjectHandle handle, RootCoverage coverage,
        Status& status) const
    {
        std::uint32_t payload = 0u;
        if (!objects_.resolve(handle, payload, status)) return false;
        if (coverage != RootCoverage::CompleteForModel) {
            status = Status::UnknownRoots;
            return false;
        }
        bool reachable = false;
        if (!graph_.reachesFromRoots(handle, objects_, roots_, reachable,
                status)) return false;
        if (reachable) {
            status = Status::ObjectStillReachable;
            return false;
        }
        status = Status::None;
        return true;
    }

    bool retire(ObjectHandle handle, RootCoverage coverage, Status& status)
    {
        if (!mayRetire(handle, coverage, status)) return false;
        if (!objects_.retire(handle, status)) return false;
        graph_.removeObject(handle);
        return true;
    }

    void reset()
    {
        roots_.reset();
        graph_.reset();
        objects_.reset();
    }

    Objects& objects() { return objects_; }
    const Objects& objects() const { return objects_; }
    Roots& roots() { return roots_; }
    const Roots& roots() const { return roots_; }
    Graph& graph() { return graph_; }
    const Graph& graph() const { return graph_; }

private:
    Objects objects_;
    Roots roots_;
    Graph graph_;
};

constexpr const char* statusName(Status status)
{
    switch (status) {
    case Status::None: return "None";
    case Status::InvalidHandle: return "InvalidHandle";
    case Status::StaleHandle: return "StaleHandle";
    case Status::SlotTableFull: return "SlotTableFull";
    case Status::GenerationExhausted: return "GenerationExhausted";
    case Status::InvalidOwner: return "InvalidOwner";
    case Status::StaleOwner: return "StaleOwner";
    case Status::OwnerTableFull: return "OwnerTableFull";
    case Status::RootTableFull: return "RootTableFull";
    case Status::InvalidRootToken: return "InvalidRootToken";
    case Status::StaleRootToken: return "StaleRootToken";
    case Status::InvalidRootHandle: return "InvalidRootHandle";
    case Status::GraphFull: return "GraphFull";
    case Status::InvalidGraphEdge: return "InvalidGraphEdge";
    case Status::UnknownRoots: return "UnknownRoots";
    case Status::ObjectStillReachable: return "ObjectStillReachable";
    }
    return "UnknownStatus";
}

} // namespace js86
} // namespace javascript
} // namespace gxos
