#include "host.h"

namespace gxos {
namespace javascript {

const char* hostValueTypeName(HostValueType type)
{
    switch (type) {
    case HostValueType::Undefined: return "Undefined";
    case HostValueType::Null: return "Null";
    case HostValueType::Boolean: return "Boolean";
    case HostValueType::Number: return "Number";
    case HostValueType::String: return "String";
    case HostValueType::Object: return "Object";
    case HostValueType::HostObject: return "HostObject";
    case HostValueType::Method: return "Method";
    }
    return "Invalid";
}

const char* hostResultCodeName(HostResultCode code)
{
    switch (code) {
    case HostResultCode::Success: return "Success";
    case HostResultCode::PropertyNotFound: return "PropertyNotFound";
    case HostResultCode::PropertyReadOnly: return "PropertyReadOnly";
    case HostResultCode::PropertyWriteFailed: return "PropertyWriteFailed";
    case HostResultCode::InvalidObject: return "InvalidObject";
    case HostResultCode::StaleObject: return "StaleObject";
    case HostResultCode::CallFailed: return "CallFailed";
    case HostResultCode::ReentryUnsupported: return "ReentryUnsupported";
    case HostResultCode::InvalidReturn: return "InvalidReturn";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
