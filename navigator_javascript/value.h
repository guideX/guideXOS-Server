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
};

// Only symbols defined by the runtime itself are currently representable.
// None is the string-key discriminator used by bounded property records.
enum class WellKnownSymbol : std::uint8_t {
    None = 0,
    Iterator = 1,
};

using RuntimeStringId = std::uint32_t;
constexpr RuntimeStringId kInvalidRuntimeStringId = 0xffffffffu;

using RuntimeFunctionId = std::uint32_t;
constexpr RuntimeFunctionId kInvalidRuntimeFunctionId = 0xffffffffu;

using RuntimeObjectId = std::uint32_t;
constexpr RuntimeObjectId kInvalidRuntimeObjectId = 0xffffffffu;

// Host handles are opaque, context-owned tokens. The upper 32 bits carry a
// bounded realm generation and the lower 32 bits carry a registry slot. No
// external pointer is ever stored in a JavaScript Value.
using RuntimeHostObjectId = std::uint64_t;
constexpr RuntimeHostObjectId kInvalidRuntimeHostObjectId =
    0xffffffffffffffffull;

// A Value is a small tagged runtime value. String values contain an index into
// the owning RuntimeContext string store, well-known Symbols carry their
// bounded identity in that same scalar slot under a distinct type tag, and
// function values contain an index into that context's function table. None
// points at lexer/parser buffers. Handles are valid only while their owning
// context remains alive (or until that context is reset).
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

    bool booleanValue() const { return booleanValue_; }
    double numberValue() const { return numberValue_; }
    RuntimeStringId stringId() const { return stringId_; }
    RuntimeFunctionId functionId() const { return functionId_; }
    RuntimeObjectId objectId() const { return objectId_; }
    RuntimeHostObjectId hostObjectId() const { return hostObjectId_; }
    WellKnownSymbol wellKnownSymbolValue() const
    {
        return static_cast<WellKnownSymbol>(stringId_);
    }

private:
    ValueType type_ = ValueType::Undefined;
    bool booleanValue_ = false;
    double numberValue_ = 0.0;
    RuntimeStringId stringId_ = kInvalidRuntimeStringId;
    RuntimeFunctionId functionId_ = kInvalidRuntimeFunctionId;
    RuntimeObjectId objectId_ = kInvalidRuntimeObjectId;
    RuntimeHostObjectId hostObjectId_ = kInvalidRuntimeHostObjectId;
};

const char* valueTypeName(ValueType type);

} // namespace javascript
} // namespace gxos
