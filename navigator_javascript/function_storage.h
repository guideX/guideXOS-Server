#pragma once

#include "ast.h"
#include "environment.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

enum class FunctionStoreErrorCode : std::uint8_t {
    None = 0,
    InvalidStorage,
    InvalidAlignment,
    InvalidInput,
    InvalidReference,
    StaleGeneration,
    CapacityExceeded,
    SizeOverflow,
    InternalInvariantFailure,
};

struct FunctionStoreError {
    FunctionStoreErrorCode code = FunctionStoreErrorCode::None;
};

// The function record is shared by the hosted evaluator and the portable
// execution core. AST IDs and closure environments remain stable handles;
// records never point into a movable FunctionStore backing array.
struct FunctionRecord {
    enum class Kind : std::uint8_t {
        User = 0,
        Native,
        Host,
    };

    Kind kind = Kind::User;
    AstNodeId declaration = kInvalidAstNodeId;
    EnvironmentId closureEnvironment = kInvalidEnvironmentId;
    std::uint8_t nativeFunction = 0;
    RuntimeHostObjectId hostObject = kInvalidRuntimeHostObjectId;
    std::uint32_t hostMethod = 0;
    bool hostMethodRequiresReceiver = false;
    bool arrowFunction = false;
    Value lexicalThis = Value::undefined();
};

struct FunctionStorageView {
    FunctionRecord* records = nullptr;
    std::size_t capacity = 0u;
};

// Caller-backed, append-only records with generation checked stable handles.
// A non-zero slot base lets a hosted RuntimeContext keep its established
// native/host prefix while the shared core appends into portable storage.
class FunctionStore {
public:
    bool configure(FunctionStorageView storage,
        RuntimeGeneration generation, std::uint32_t slotBase,
        FunctionStoreError& error)
    {
        const RuntimeGeneration previousGeneration = generation_;
        error.code = FunctionStoreErrorCode::None;
        storage_ = FunctionStorageView();
        generation_ = kInvalidRuntimeGeneration;
        slotBase_ = 0u;
        count_ = 0u;
        valid_ = false;
        if ((storage.records == nullptr && storage.capacity != 0u) ||
            (storage.records != nullptr && storage.capacity == 0u)) {
            error.code = FunctionStoreErrorCode::InvalidStorage;
            return false;
        }
        if (storage.records != nullptr &&
            reinterpret_cast<std::uintptr_t>(storage.records) %
                alignof(FunctionRecord) != 0u) {
            error.code = FunctionStoreErrorCode::InvalidAlignment;
            return false;
        }
        if (generation == kInvalidRuntimeGeneration ||
            slotBase >= kRuntimeHandleInvalidPart) {
            error.code = FunctionStoreErrorCode::InvalidInput;
            return false;
        }
        if (previousGeneration != kInvalidRuntimeGeneration &&
            generation <= previousGeneration) {
            error.code = FunctionStoreErrorCode::StaleGeneration;
            return false;
        }
        if (storage.capacity >
            static_cast<std::size_t>(kRuntimeHandleInvalidPart - slotBase)) {
            error.code = FunctionStoreErrorCode::SizeOverflow;
            return false;
        }
        storage_ = storage;
        generation_ = generation;
        slotBase_ = slotBase;
        valid_ = true;
        return true;
    }

    void reset(RuntimeGeneration generation)
    {
        count_ = 0u;
        if (generation == kInvalidRuntimeGeneration) {
            valid_ = false;
            return;
        }
        if (!valid_ || generation <= generation_) {
            valid_ = false;
            return;
        }
        generation_ = generation;
    }

    bool setSlotBase(std::uint32_t slotBase, FunctionStoreError& error)
    {
        error.code = FunctionStoreErrorCode::None;
        if (!valid_ || count_ != 0u) {
            error.code = FunctionStoreErrorCode::InvalidInput;
            return false;
        }
        if (slotBase >= kRuntimeHandleInvalidPart) {
            error.code = FunctionStoreErrorCode::InvalidInput;
            return false;
        }
        slotBase_ = slotBase;
        return true;
    }

    bool append(const FunctionRecord& value, RuntimeFunctionId& result,
        FunctionStoreError& error)
    {
        result = kInvalidRuntimeFunctionId;
        error.code = FunctionStoreErrorCode::None;
        if (!valid_) {
            error.code = FunctionStoreErrorCode::InvalidStorage;
            return false;
        }
        if (generation_ == kInvalidRuntimeGeneration) {
            error.code = FunctionStoreErrorCode::StaleGeneration;
            return false;
        }
        if (count_ >= storage_.capacity ||
            count_ >= static_cast<std::size_t>(kRuntimeHandleInvalidPart -
                slotBase_)) {
            error.code = FunctionStoreErrorCode::CapacityExceeded;
            return false;
        }
        if (storage_.records == nullptr) {
            error.code = FunctionStoreErrorCode::InvalidStorage;
            return false;
        }
        storage_.records[count_] = value;
        result = makeRuntimeHandle(generation_, slotBase_ +
            static_cast<std::uint32_t>(count_));
        if (result == kInvalidRuntimeFunctionId) {
            error.code = FunctionStoreErrorCode::InternalInvariantFailure;
            return false;
        }
        ++count_;
        return true;
    }

    // Remove only the newest record when its handle has not been published.
    // This supports failure-safe declaration setup without permitting an
    // earlier stable function handle to move or become invalid.
    bool discardLast(RuntimeFunctionId id, FunctionStoreError& error)
    {
        error.code = FunctionStoreErrorCode::None;
        if (!valid_) {
            error.code = FunctionStoreErrorCode::InvalidStorage;
            return false;
        }
        if (runtimeHandleGeneration(id) != generation_) {
            error.code = FunctionStoreErrorCode::StaleGeneration;
            return false;
        }
        if (!runtimeHandleIsValid(id, generation_) || count_ == 0u ||
            runtimeHandleSlot(id) != slotBase_ +
                static_cast<std::uint32_t>(count_ - 1u)) {
            error.code = FunctionStoreErrorCode::InvalidReference;
            return false;
        }
        --count_;
        return true;
    }

    const FunctionRecord* get(RuntimeFunctionId id,
        FunctionStoreError& error) const
    {
        error.code = FunctionStoreErrorCode::None;
        if (!valid_) {
            error.code = FunctionStoreErrorCode::InvalidStorage;
            return nullptr;
        }
        if (runtimeHandleGeneration(id) != generation_) {
            error.code = FunctionStoreErrorCode::StaleGeneration;
            return nullptr;
        }
        if (!runtimeHandleIsValid(id, generation_)) {
            error.code = FunctionStoreErrorCode::InvalidReference;
            return nullptr;
        }
        const std::uint32_t slot = runtimeHandleSlot(id);
        if (slot < slotBase_) {
            error.code = FunctionStoreErrorCode::InvalidReference;
            return nullptr;
        }
        const std::size_t index = static_cast<std::size_t>(slot - slotBase_);
        if (index >= count_) {
            error.code = FunctionStoreErrorCode::InvalidReference;
            return nullptr;
        }
        return &storage_.records[index];
    }

    std::size_t count() const { return count_; }
    std::size_t capacity() const { return storage_.capacity; }
    std::uint32_t slotBase() const { return slotBase_; }
    RuntimeGeneration generation() const { return generation_; }
    bool configured() const { return valid_; }

private:
    FunctionStorageView storage_;
    RuntimeGeneration generation_ = kInvalidRuntimeGeneration;
    std::uint32_t slotBase_ = 0u;
    std::size_t count_ = 0u;
    bool valid_ = false;
};

} // namespace javascript
} // namespace gxos
