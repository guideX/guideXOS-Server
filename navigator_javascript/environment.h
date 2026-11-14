#pragma once

#include "source.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

using EnvironmentId = std::uint32_t;
constexpr EnvironmentId kGlobalEnvironmentId = 0u; // Unbound generation-0 sentinel.
constexpr EnvironmentId kInvalidEnvironmentId = 0xffffffffu;
constexpr std::uint32_t kInvalidBindingIndex = 0xffffffffu;

enum class EnvironmentErrorCode : std::uint8_t {
    None = 0,
    InvalidStorage,
    InvalidAlignment,
    InvalidInput,
    InvalidReference,
    InvalidEnvironment,
    StaleGeneration,
    MissingBinding,
    UninitializedBinding,
    BindingAlreadyInitialized,
    ImmutableAssignment,
    DuplicateLexicalBinding,
    BindingLimitExceeded,
    BindingNameTooLong,
    CapacityExceeded,
    AllocationFailure,
    SizeOverflow,
    ParentCycle,
    InternalInvariantFailure,
};

struct EnvironmentError {
    EnvironmentErrorCode code = EnvironmentErrorCode::None;
};

struct EnvironmentLimits {
    std::size_t maxBindings = 256u;
    std::size_t maxBindingNameLength = 256u;
};

enum class EnvironmentKind : std::uint8_t {
    Global = 0,
    Function,
    Block,
    Parameters,
    FunctionBody,
    Iteration,
};

// Names point into the EnvironmentStore's caller-owned stable name arena.
// The link is an index rather than an address, so binding storage can be
// inspected and copied without retaining a temporary container pointer.
struct Binding {
    SourceView name;
    Value value;
    std::uint32_t nextIndex = kInvalidBindingIndex;
    bool immutable = false;
    bool initialized = false;
    bool lexical = false;
};

struct EnvironmentRecord {
    EnvironmentId parent = kInvalidEnvironmentId;
    std::uint32_t firstBinding = kInvalidBindingIndex;
    std::uint32_t lastBinding = kInvalidBindingIndex;
    std::uint32_t bindingCount = 0u;
    std::uint16_t bindingLimit = 0u;
    std::uint16_t nameLengthLimit = 0u;
    RuntimeGeneration generation = kInvalidRuntimeGeneration;
    EnvironmentKind kind = EnvironmentKind::Global;
    bool active = false;
};

struct EnvironmentStorageView {
    EnvironmentRecord* environments = nullptr;
    std::size_t environmentCapacity = 0u;
    Binding* bindings = nullptr;
    std::size_t bindingCapacity = 0u;
    char* nameBytes = nullptr;
    std::size_t nameByteCapacity = 0u;
};

// The portable shared lexical-environment implementation. All persistent
// records and copied names live in caller-owned bounded storage. Handles are
// generation checked, and the append-only pool retains closure environments
// until reset.
class EnvironmentStore {
public:
    bool configure(EnvironmentStorageView storage,
        RuntimeGeneration generation, EnvironmentError& error);
    void reset(RuntimeGeneration generation);

    bool createRoot(EnvironmentLimits limits, EnvironmentId& result,
        EnvironmentError& error);
    bool create(EnvironmentId parent, EnvironmentKind kind,
        EnvironmentLimits limits, EnvironmentId& result,
        EnvironmentError& error);
    bool setParent(EnvironmentId environment, EnvironmentId parent,
        EnvironmentError& error);

    bool declare(EnvironmentId environment, SourceView name, Value value,
        EnvironmentError& error, bool immutable = false);
    bool declareLexical(EnvironmentId environment, SourceView name,
        bool immutable, EnvironmentError& error,
        const Value& initialValue = Value::undefined(),
        bool initialized = false);
    bool initialize(EnvironmentId environment, SourceView name, Value value,
        EnvironmentError& error);
    bool initializeChain(EnvironmentId start, SourceView name, Value value,
        EnvironmentError& error);
    bool assign(EnvironmentId environment, SourceView name, Value value,
        EnvironmentError& error);

    bool resolveChain(EnvironmentId start, SourceView name,
        const Binding*& result, EnvironmentError& error) const;
    bool resolveChain(EnvironmentId start, SourceView name, Binding*& result,
        EnvironmentError& error);
    bool readChain(EnvironmentId start, SourceView name, Value& result,
        EnvironmentError& error) const;
    bool assignChain(EnvironmentId start, SourceView name, Value value,
        EnvironmentError& error);

    bool valid(EnvironmentId id, EnvironmentError& error) const;
    const EnvironmentRecord* record(EnvironmentId id,
        EnvironmentError& error) const;
    EnvironmentRecord* record(EnvironmentId id, EnvironmentError& error);
    const Binding* resolveLocal(EnvironmentId environment, SourceView name,
        EnvironmentError& error) const;
    Binding* resolveLocal(EnvironmentId environment, SourceView name,
        EnvironmentError& error);
    const Binding* bindingAt(EnvironmentId environment,
        std::size_t index) const;

    std::size_t environmentCount() const { return environmentCount_; }
    std::size_t bindingCount() const { return bindingCount_; }
    std::size_t nameBytesUsed() const { return nameBytesUsed_; }
    RuntimeGeneration generation() const { return generation_; }
    bool configured() const { return valid_; }

private:
    std::uint32_t findLocal(const EnvironmentRecord& environment,
        SourceView name, EnvironmentError& error) const;
    bool validateName(SourceView name, std::size_t maximumLength,
        EnvironmentError& error) const;
    bool declareImpl(EnvironmentId environment, SourceView name, Value value,
        bool immutable, bool lexical, bool initialized,
        EnvironmentError& error);

    EnvironmentStorageView storage_;
    RuntimeGeneration generation_ = kInvalidRuntimeGeneration;
    std::size_t environmentCount_ = 0u;
    std::size_t bindingCount_ = 0u;
    std::size_t nameBytesUsed_ = 0u;
    bool valid_ = false;
};

// A small hosted/runtime adapter to one record in the same portable store.
// It contains no binding or name ownership of its own.
class Environment {
public:
    Environment() = default;
    Environment(EnvironmentStore* store, EnvironmentId id,
        EnvironmentLimits limits)
        : store_(store), id_(id), limits_(limits) {}

    void bind(EnvironmentStore* store, EnvironmentId id,
        EnvironmentLimits limits)
    {
        store_ = store;
        id_ = id;
        limits_ = limits;
    }
    EnvironmentId id() const { return id_; }
    EnvironmentId parent() const;
    EnvironmentKind kind() const;
    bool setParent(EnvironmentId parent, EnvironmentError& error);
    void setParent(EnvironmentId parent);

    bool declare(SourceView name, Value value, EnvironmentError& error,
        bool immutable = false);
    bool declareLexical(SourceView name, bool immutable,
        EnvironmentError& error, const Value& initialValue = Value::undefined(),
        bool initialized = false);
    bool initialize(SourceView name, Value value, EnvironmentError& error);
    bool initialize(SourceView name, Value value);
    bool assign(SourceView name, Value value, EnvironmentError& error);
    bool assign(SourceView name, Value value);
    bool isImmutable(SourceView name) const;
    const Binding* resolve(SourceView name) const;
    Binding* resolve(SourceView name);
    const Value* lookup(SourceView name) const;

    std::size_t bindingCount() const;
    const Binding* bindingAt(std::size_t index) const;
    const EnvironmentLimits& limits() const { return limits_; }

private:
    EnvironmentStore* store_ = nullptr;
    EnvironmentId id_ = kInvalidEnvironmentId;
    EnvironmentLimits limits_;
};

const char* environmentErrorCodeName(EnvironmentErrorCode code);

} // namespace javascript
} // namespace gxos
