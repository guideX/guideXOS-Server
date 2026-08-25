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
};

using RuntimeStringId = std::uint32_t;
constexpr RuntimeStringId kInvalidRuntimeStringId = 0xffffffffu;

using RuntimeFunctionId = std::uint32_t;
constexpr RuntimeFunctionId kInvalidRuntimeFunctionId = 0xffffffffu;

// A Value is a small tagged runtime value. String values contain an index into
// the owning RuntimeContext string store and function values contain a stable
// index into that context's function table. Neither points at lexer/parser
// buffers. Handles are valid only while their owning context remains alive (or
// until that context is reset).
class Value {
public:
    Value() = default;

    static Value undefined();
    static Value nullValue();
    static Value boolean(bool value);
    static Value number(double value);
    static Value string(RuntimeStringId id);
    static Value function(RuntimeFunctionId id);

    ValueType type() const { return type_; }
    bool isUndefined() const { return type_ == ValueType::Undefined; }
    bool isNull() const { return type_ == ValueType::Null; }
    bool isBoolean() const { return type_ == ValueType::Boolean; }
    bool isNumber() const { return type_ == ValueType::Number; }
    bool isString() const { return type_ == ValueType::String; }
    bool isFunction() const { return type_ == ValueType::Function; }

    bool booleanValue() const { return booleanValue_; }
    double numberValue() const { return numberValue_; }
    RuntimeStringId stringId() const { return stringId_; }
    RuntimeFunctionId functionId() const { return functionId_; }

private:
    ValueType type_ = ValueType::Undefined;
    bool booleanValue_ = false;
    double numberValue_ = 0.0;
    RuntimeStringId stringId_ = kInvalidRuntimeStringId;
    RuntimeFunctionId functionId_ = kInvalidRuntimeFunctionId;
};

const char* valueTypeName(ValueType type);

} // namespace javascript
} // namespace gxos
