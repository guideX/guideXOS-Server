#include "navigator_script_host.h"

namespace gxos {
namespace javascript {

HostResult NavigatorScriptHostAdapter::validate(
    const HostObjectReference& object)
{
    if (object.valid() && object.generation == generation_) {
        return HostResult();
    }
    return HostResult{object.generation == generation_
        ? HostResultCode::InvalidObject : HostResultCode::StaleObject};
}

HostResult NavigatorScriptHostAdapter::getProperty(
    const HostObjectReference& object, SourceView property, HostValue& result)
{
    (void)object;
    (void)property;
    result = HostValue::undefined();
    return HostResult{HostResultCode::PropertyNotFound};
}

HostResult NavigatorScriptHostAdapter::setProperty(
    const HostObjectReference& object, SourceView property,
    const HostValue& value)
{
    (void)object;
    (void)property;
    (void)value;
    return HostResult{HostResultCode::PropertyWriteFailed};
}

HostResult NavigatorScriptHostAdapter::call(const HostObjectReference* receiver,
    std::uint32_t methodId, const HostValue* arguments,
    std::size_t argumentCount, HostValue& result)
{
    (void)receiver;
    (void)methodId;
    (void)arguments;
    (void)argumentCount;
    result = HostValue::undefined();
    return HostResult{HostResultCode::CallFailed};
}

} // namespace javascript
} // namespace gxos
