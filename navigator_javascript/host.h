#pragma once

#include "source.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

class RuntimeContext;
enum class RuntimeErrorCode : std::uint8_t;

using HostGenerationId = std::uint32_t;
constexpr HostGenerationId kInvalidHostGenerationId = 0u;
using HostInstanceId = std::uint64_t;
using HostObjectKind = std::uint32_t;

// This descriptor is owned by the embedding host. It is copied into the
// RuntimeContext registry and is never placed directly in a JavaScript Value.
// generation is supplied by RuntimeContext and prevents a reused instance ID
// from becoming visible through a stale value.
struct HostObjectReference {
    HostInstanceId instanceId = 0;
    HostGenerationId generation = kInvalidHostGenerationId;
    HostObjectKind kind = 0;

    bool valid() const
    {
        return instanceId != 0 && generation != kInvalidHostGenerationId;
    }
};

inline bool operator==(const HostObjectReference& left,
    const HostObjectReference& right)
{
    return left.instanceId == right.instanceId &&
        left.generation == right.generation && left.kind == right.kind;
}

inline bool operator!=(const HostObjectReference& left,
    const HostObjectReference& right)
{
    return !(left == right);
}

enum class HostValueType : std::uint8_t {
    Undefined = 0,
    Null,
    Boolean,
    Number,
    String,
    Function,
    Object,
    HostObject,
    Method,
};

// Strings are borrowed only for the duration of the adapter call. The
// runtime copies successful returned strings into its bounded string store.
// Object and Function are runtime-owned IDs. A callback-capable host may
// retain a Function ID only under an explicit bounded same-realm contract.
struct HostValue {
    HostValueType type = HostValueType::Undefined;
    bool booleanValue = false;
    double numberValue = 0.0;
    SourceView stringValue;
    RuntimeFunctionId functionId = kInvalidRuntimeFunctionId;
    RuntimeObjectId objectId = kInvalidRuntimeObjectId;
    HostObjectReference hostObject;
    std::uint32_t methodId = 0;
    bool methodRequiresReceiver = false;
    // A bounded receiver-aware method may use one runtime wrapper for all
    // host objects of that method. The receiver is still checked at call
    // time; this only avoids one cached wrapper per element.
    bool methodSharedAcrossReceivers = false;

    static HostValue undefined() { return HostValue(); }

    static HostValue nullValue()
    {
        HostValue value;
        value.type = HostValueType::Null;
        return value;
    }

    static HostValue boolean(bool value)
    {
        HostValue result;
        result.type = HostValueType::Boolean;
        result.booleanValue = value;
        return result;
    }

    static HostValue number(double value)
    {
        HostValue result;
        result.type = HostValueType::Number;
        result.numberValue = value;
        return result;
    }

    static HostValue string(SourceView value)
    {
        HostValue result;
        result.type = HostValueType::String;
        result.stringValue = value;
        return result;
    }

    static HostValue function(RuntimeFunctionId value)
    {
        HostValue result;
        result.type = HostValueType::Function;
        result.functionId = value;
        return result;
    }

    static HostValue object(RuntimeObjectId value)
    {
        HostValue result;
        result.type = HostValueType::Object;
        result.objectId = value;
        return result;
    }

    static HostValue fromHostObject(const HostObjectReference& value)
    {
        HostValue result;
        result.type = HostValueType::HostObject;
        result.hostObject = value;
        return result;
    }

    static HostValue method(std::uint32_t value, bool requiresReceiver,
        bool sharedAcrossReceivers = false)
    {
        HostValue result;
        result.type = HostValueType::Method;
        result.methodId = value;
        result.methodRequiresReceiver = requiresReceiver;
        result.methodSharedAcrossReceivers = sharedAcrossReceivers;
        return result;
    }
};

// A synchronous, same-realm callback capability supplied only for the
// duration of a runtime-aware host call. The embedding host may invoke a
// JavaScript function without reparsing source or creating another evaluator.
// Host values are converted by the active evaluator before callback entry.
class HostFunctionInvoker {
public:
    virtual ~HostFunctionInvoker() = default;

    virtual bool invoke(const HostValue& function,
        const HostValue* arguments, std::size_t argumentCount,
        const HostValue& thisArgument, Value& result,
        RuntimeErrorCode& error) = 0;
    // Callback-capable hosts must use the active evaluator's JavaScript
    // truthiness rules when a host operation consumes a callback result.
    virtual bool isTruthy(const Value& value) const = 0;
};

enum class HostResultCode : std::uint8_t {
    Success = 0,
    PropertyNotFound,
    PropertyReadOnly,
    PropertyWriteFailed,
    InvalidObject,
    StaleObject,
    CallFailed,
    TypeError,
    ReentryUnsupported,
    InvalidReturn,
    InvalidValue,
    DocumentLookupLimitExceeded,
    DocumentTextLimitExceeded,
    DocumentMutationLimitExceeded,
    CallbackLimitExceeded,
};

struct HostResult {
    HostResultCode code = HostResultCode::Success;

    bool succeeded() const { return code == HostResultCode::Success; }
};

// The adapter is externally owned and must outlive the RuntimeContext using
// it. Calls are synchronous. An adapter must not retain SourceView spans,
// HostValue strings or argument arrays after a call returns. A callback host
// may retain Function IDs only while its bounded same-realm contract is active.
class HostAdapter {
public:
    virtual ~HostAdapter() = default;

    virtual HostResult validate(const HostObjectReference& object) = 0;

    virtual HostResult getProperty(const HostObjectReference& object,
        SourceView property, HostValue& result) = 0;

    virtual HostResult setProperty(const HostObjectReference& object,
        SourceView property, const HostValue& value) = 0;

    // receiver is null for receiver-independent methods. method IDs are
    // adapter-owned stable IDs, not JavaScript property-name special cases.
    virtual HostResult call(const HostObjectReference* receiver,
        std::uint32_t methodId, const HostValue* arguments,
        std::size_t argumentCount, HostValue& result) = 0;

    // Runtime-aware host calls may inspect bounded ordinary runtime objects
    // synchronously through RuntimeContext. Existing adapters retain the
    // original call contract by default.
    virtual HostResult callWithRuntime(RuntimeContext& runtime,
        HostFunctionInvoker& functionInvoker,
        RuntimeErrorCode& callbackError,
        const HostObjectReference* receiver, std::uint32_t methodId,
        const HostValue* arguments, std::size_t argumentCount,
        HostValue& result)
    {
        (void)runtime;
        (void)functionInvoker;
        (void)callbackError;
        return call(receiver, methodId, arguments, argumentCount, result);
    }

    // Most hosts intentionally reject a host call made while another host
    // call is active. A bounded host method may opt into re-entry when its
    // own synchronous callback contract is safe. The default remains the
    // historical fail-closed behavior.
    virtual bool allowsReentrantCall(std::uint32_t methodId) const
    {
        (void)methodId;
        return false;
    }

    // A very small set of pure host predicates may choose to fail closed on
    // an already-held stale receiver. The default remains strict stale-host
    // rejection for every other property and method.
    virtual bool allowsStaleHostProperty(const HostObjectReference& object,
        SourceView property) const
    {
        (void)object;
        (void)property;
        return false;
    }

    virtual bool allowsStaleHostMethod(std::uint32_t methodId) const
    {
        (void)methodId;
        return false;
    }

    // A fail-closed predicate may accept an already-held stale host argument
    // so the adapter can return its safe sentinel instead of surfacing a
    // stale-object exception from runtime argument conversion.
    virtual bool allowsStaleHostMethodArgument(std::uint32_t methodId) const
    {
        (void)methodId;
        return false;
    }
};

const char* hostValueTypeName(HostValueType type);
const char* hostResultCodeName(HostResultCode code);

} // namespace javascript
} // namespace gxos
