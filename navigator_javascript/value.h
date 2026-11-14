#pragma once

#include <cstdint>

namespace gxos {
namespace javascript {

enum class ValueType : std::uint8_t {
    Undefined = 0,
    Null,
    Boolean,
    Number,
    String,
    Function,
    Object,
    HostObject,
    Symbol,
    SymbolNamespace,
    ObjectNamespace,
    JsonNamespace,
};

// Only symbols defined by the runtime itself are currently representable.
// None is the string-key discriminator used by bounded property records.
enum class WellKnownSymbol : std::uint8_t {
    None = 0,
    Iterator = 1,
};

// Realm-owned handles use the upper 16 bits for a non-zero generation and the
// lower 16 bits for a slot. Slot 0xffff and generation 0 are reserved, which
// leaves stale handles distinguishable after a realm reset.
using RuntimeGeneration = std::uint16_t;
constexpr RuntimeGeneration kInvalidRuntimeGeneration = 0u;
constexpr std::uint32_t kRuntimeHandleInvalidPart = 0xffffu;
constexpr std::uint32_t kRuntimeHandleSlotMask = 0x0000ffffu;

constexpr std::uint32_t makeRuntimeHandle(RuntimeGeneration generation,
    std::uint32_t slot)
{
    return generation == kInvalidRuntimeGeneration ||
        slot >= kRuntimeHandleInvalidPart
        ? 0xffffffffu
        : (static_cast<std::uint32_t>(generation) << 16u) | slot;
}

constexpr RuntimeGeneration runtimeHandleGeneration(std::uint32_t handle)
{
    return static_cast<RuntimeGeneration>(handle >> 16u);
}

constexpr std::uint32_t runtimeHandleSlot(std::uint32_t handle)
{
    return handle & kRuntimeHandleSlotMask;
}

constexpr bool runtimeHandleIsValid(std::uint32_t handle,
    RuntimeGeneration generation)
{
    return handle != 0xffffffffu &&
        generation != kInvalidRuntimeGeneration &&
        runtimeHandleGeneration(handle) == generation &&
        runtimeHandleSlot(handle) != kRuntimeHandleInvalidPart;
}

using RuntimeStringId = std::uint32_t;
constexpr RuntimeStringId kInvalidRuntimeStringId = 0xffffffffu;

using RuntimeFunctionId = std::uint32_t;
constexpr RuntimeFunctionId kInvalidRuntimeFunctionId = 0xffffffffu;

using RuntimeObjectId = std::uint32_t;
constexpr RuntimeObjectId kInvalidRuntimeObjectId = 0xffffffffu;

// Host handles are opaque, context-owned tokens. The upper 32 bits carry a
// bounded host generation and the lower 32 bits carry a registry slot. No
// external pointer is ever stored in a JavaScript Value.
using RuntimeHostObjectId = std::uint64_t;
constexpr RuntimeHostObjectId kInvalidRuntimeHostObjectId =
    0xffffffffffffffffull;

// Values are non-owning tagged scalars. Strings and runtime references are
// generation-checked handles resolved by their owning RuntimeContext. Copying
// a Value preserves the handle identity without allocating or retaining a
// pointer into a moving container.
class Value {
public:
    Value() = default;

    static Value undefined();
    static Value nullValue();
    static Value boolean(bool value);
    static Value number(double value);
    static Value string(RuntimeStringId id);
    static Value function(RuntimeFunctionId id);
    static Value object(RuntimeObjectId id);
    static Value hostObject(RuntimeHostObjectId id);
    static Value wellKnownSymbol(WellKnownSymbol symbol);
    static Value objectNamespace();
    // The minimal global Symbol intrinsic is a namespace for the supported
    // well-known identities, not a callable Symbol() constructor.
    static Value symbolNamespace();
    static Value jsonNamespace();

    ValueType type() const { return type_; }
    bool isUndefined() const { return type_ == ValueType::Undefined; }
    bool isNull() const { return type_ == ValueType::Null; }
    bool isBoolean() const { return type_ == ValueType::Boolean; }
    bool isNumber() const { return type_ == ValueType::Number; }
    bool isString() const { return type_ == ValueType::String; }
    bool isFunction() const { return type_ == ValueType::Function; }
    bool isObject() const { return type_ == ValueType::Object; }
    bool isHostObject() const { return type_ == ValueType::HostObject; }
    bool isSymbol() const { return type_ == ValueType::Symbol; }
    bool isSymbolNamespace() const
    {
        return type_ == ValueType::SymbolNamespace;
    }
    bool isObjectNamespace() const
    {
        return type_ == ValueType::ObjectNamespace;
    }
    bool isJsonNamespace() const { return type_ == ValueType::JsonNamespace; }

    bool booleanValue() const;
    double numberValue() const;
    RuntimeStringId stringId() const;
    RuntimeFunctionId functionId() const;
    RuntimeObjectId objectId() const;
    RuntimeHostObjectId hostObjectId() const;
    WellKnownSymbol wellKnownSymbolValue() const;

private:
    union Payload {
        bool booleanValue;
        double numberValue;
        RuntimeStringId stringId;
        RuntimeFunctionId functionId;
        RuntimeObjectId objectId;
        RuntimeHostObjectId hostObjectId;
        std::uint64_t bits;

        constexpr Payload() : bits(0u) {}
    } payload_;

    ValueType type_ = ValueType::Undefined;
};

const char* valueTypeName(ValueType type);

} // namespace javascript
} // namespace gxos
