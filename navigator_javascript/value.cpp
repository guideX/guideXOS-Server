#include "value.h"

namespace gxos {
namespace javascript {

Value Value::undefined()
{
    return Value();
}

Value Value::nullValue()
{
    Value value;
    value.type_ = ValueType::Null;
    return value;
}

Value Value::boolean(bool boolean)
{
    Value value;
    value.type_ = ValueType::Boolean;
    value.booleanValue_ = boolean;
    return value;
}

Value Value::number(double number)
{
    Value value;
    value.type_ = ValueType::Number;
    value.numberValue_ = number;
    return value;
}

Value Value::string(RuntimeStringId id)
{
    Value value;
    value.type_ = ValueType::String;
    value.stringId_ = id;
    return value;
}

Value Value::function(RuntimeFunctionId id)
{
    Value value;
    value.type_ = ValueType::Function;
    value.functionId_ = id;
    return value;
}

Value Value::object(RuntimeObjectId id)
{
    Value value;
    value.type_ = ValueType::Object;
    value.objectId_ = id;
    return value;
}

Value Value::hostObject(RuntimeHostObjectId id)
{
    Value value;
    value.type_ = ValueType::HostObject;
    value.hostObjectId_ = id;
    return value;
}

const char* valueTypeName(ValueType type)
{
    switch (type) {
    case ValueType::Undefined: return "Undefined";
    case ValueType::Null: return "Null";
    case ValueType::Boolean: return "Boolean";
    case ValueType::Number: return "Number";
    case ValueType::String: return "String";
    case ValueType::Function: return "Function";
    case ValueType::Object: return "Object";
    case ValueType::HostObject: return "HostObject";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
