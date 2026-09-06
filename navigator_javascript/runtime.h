#pragma once

#include "ast.h"
#include "environment.h"
#include "host.h"
#include "parser.h"
#include "value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace javascript {

constexpr std::uint8_t kEventPhaseNone = 0u;
constexpr std::uint8_t kEventPhaseCapturing = 1u;
constexpr std::uint8_t kEventPhaseAtTarget = 2u;
constexpr std::uint8_t kEventPhaseBubbling = 3u;

enum class RuntimeErrorCode : std::uint8_t {
    None = 0,
    UnknownIdentifier,
    InvalidAssignmentTarget,
    InvalidOperandType,
    UnsupportedFeature,
    UnsupportedFunctionConstruct,
    NotCallable,
    CallDepthExceeded,
    EnvironmentLimitExceeded,
    FunctionLimitExceeded,
    InvalidFunction,
    BindingLimitExceeded,
    BindingNameTooLong,
    StringLimitExceeded,
    ExecutionBudgetExceeded,
    IllegalBreak,
    IllegalContinue,
    IllegalReturn,
    InvalidAstState,
    AllocationFailure,
    NotObject,
    CannotReadProperty,
    CannotWriteProperty,
    ObjectLimitExceeded,
    PropertyLimitExceeded,
    PropertyNameTooLong,
    ArrayLimitExceeded,
    ArrayIndexOutOfRange,
    InvalidPropertyKey,
    InvalidReceiver,
    NativeFunctionLimitExceeded,
    InvalidNativeFunction,
    PrototypeChainExceeded,
    BuiltInInitializationFailed,
    HostObjectLimitExceeded,
    InvalidHostObject,
    StaleHostObject,
    HostPropertyNotFound,
    HostPropertyReadOnly,
    HostPropertyWriteFailed,
    HostCallFailed,
    HostOperationBudgetExceeded,
    HostReentryUnsupported,
    HostGenerationLimitExceeded,
    InvalidHostReturn,
    HostMethodLimitExceeded,
    HostInvalidValue,
    DocumentLookupLimitExceeded,
    DocumentTextLimitExceeded,
    DocumentMutationLimitExceeded,
    RealmSourceLimitExceeded,
    HostCallbackLimitExceeded,
    PropagationPathLimitExceeded,
};

struct RuntimeError {
    RuntimeErrorCode code = RuntimeErrorCode::None;
    SourceLocation location;
};

enum class ScriptStatus : std::uint8_t {
    Success = 0,
    LexicalFailure,
    ParseFailure,
    RuntimeFailure,
    ExecutionBudgetExceeded,
};

struct ScriptResult {
    ScriptStatus status = ScriptStatus::Success;
    LexerError lexerError;
    ParserError parserError;
    RuntimeError runtimeError;
    std::size_t executionSteps = 0;

    bool succeeded() const { return status == ScriptStatus::Success; }
};

constexpr std::size_t kDefaultMaxJavaScriptRuntimeBindings = 256u;
constexpr std::size_t kDefaultMaxJavaScriptBindingNameLength = 256u;
constexpr std::size_t kDefaultMaxJavaScriptRuntimeStringLength = 64u * 1024u;
constexpr std::size_t kDefaultMaxJavaScriptRuntimeStringBytes = 256u * 1024u;
constexpr std::size_t kDefaultMaxJavaScriptRuntimeStringValues = 4096u;
constexpr std::size_t kDefaultJavaScriptExecutionSteps = 100000u;
constexpr std::size_t kDefaultMaxJavaScriptCallDepth = 64u;
constexpr std::size_t kDefaultMaxJavaScriptEnvironments = 256u;
constexpr std::size_t kDefaultMaxJavaScriptFunctionValues = 4096u;
constexpr std::size_t kDefaultMaxJavaScriptRuntimeObjects = 1024u;
constexpr std::size_t kDefaultMaxJavaScriptPropertiesPerObject = 256u;
constexpr std::size_t kDefaultMaxJavaScriptRuntimeProperties = 4096u;
constexpr std::size_t kDefaultMaxJavaScriptPropertyNameLength = 256u;
constexpr std::size_t kDefaultMaxJavaScriptPropertyKeyBytes = 256u * 1024u;
constexpr std::size_t kDefaultMaxJavaScriptArrayElements = 1024u;
constexpr std::size_t kDefaultMaxJavaScriptArrayElementCount = 4096u;
constexpr std::size_t kDefaultMaxJavaScriptArrayIndex = 1023u;
constexpr std::size_t kDefaultMaxJavaScriptNativeFunctionValues = 64u;
constexpr std::size_t kDefaultMaxJavaScriptPrototypeDepth = 32u;
constexpr std::size_t kDefaultMaxJavaScriptHostObjects = 1024u;
constexpr std::size_t kDefaultMaxJavaScriptHostPropertyNameLength = 256u;
constexpr std::size_t kDefaultMaxJavaScriptHostOperations = 10000u;
constexpr std::size_t kDefaultMaxJavaScriptHostGenerations = 4096u;
constexpr std::size_t kDefaultMaxJavaScriptHostMethodValues = 64u;
constexpr std::size_t kDefaultMaxJavaScriptRealmSourceBytes = 1u * 1024u * 1024u;

struct RuntimeLimits {
    LexerLimits lexer;
    ParserLimits parser;

    std::size_t maxBindings = kDefaultMaxJavaScriptRuntimeBindings;
    // Global bindings retain the JS3 limit.  Each function invocation uses
    // this separate per-environment bound.
    std::size_t maxFunctionEnvironmentBindings =
        kDefaultMaxJavaScriptRuntimeBindings;
    std::size_t maxBindingNameLength =
        kDefaultMaxJavaScriptBindingNameLength;
    std::size_t maxRuntimeStringLength =
        kDefaultMaxJavaScriptRuntimeStringLength;
    std::size_t maxTotalRuntimeStringBytes =
        kDefaultMaxJavaScriptRuntimeStringBytes;
    // This also bounds repeated empty-string allocations, which consume no
    // bytes but are still runtime-owned values.
    std::size_t maxRuntimeStringValues =
        kDefaultMaxJavaScriptRuntimeStringValues;
    std::size_t maxExecutionSteps = kDefaultJavaScriptExecutionSteps;
    std::size_t maxCallDepth = kDefaultMaxJavaScriptCallDepth;
    // Environment records are retained until reset so captured lexical
    // parents never dangle.  This is both the live-environment and total
    // environment bound for the context lifetime.
    std::size_t maxEnvironments = kDefaultMaxJavaScriptEnvironments;
    std::size_t maxFunctions = kDefaultMaxJavaScriptFunctionValues;
    std::size_t maxNativeFunctions =
        kDefaultMaxJavaScriptNativeFunctionValues;
    std::size_t maxObjects = kDefaultMaxJavaScriptRuntimeObjects;
    std::size_t maxPropertiesPerObject =
        kDefaultMaxJavaScriptPropertiesPerObject;
    std::size_t maxTotalProperties = kDefaultMaxJavaScriptRuntimeProperties;
    std::size_t maxPropertyNameLength =
        kDefaultMaxJavaScriptPropertyNameLength;
    std::size_t maxTotalPropertyKeyBytes =
        kDefaultMaxJavaScriptPropertyKeyBytes;
    std::size_t maxArrayElements = kDefaultMaxJavaScriptArrayElements;
    std::size_t maxTotalArrayElements =
        kDefaultMaxJavaScriptArrayElementCount;
    std::size_t maxArrayIndex = kDefaultMaxJavaScriptArrayIndex;
    std::size_t maxPrototypeDepth = kDefaultMaxJavaScriptPrototypeDepth;
    std::size_t maxHostObjects = kDefaultMaxJavaScriptHostObjects;
    std::size_t maxHostPropertyNameLength =
        kDefaultMaxJavaScriptHostPropertyNameLength;
    std::size_t maxHostOperations = kDefaultMaxJavaScriptHostOperations;
    std::size_t maxHostGenerations = kDefaultMaxJavaScriptHostGenerations;
    std::size_t maxHostMethodValues = kDefaultMaxJavaScriptHostMethodValues;
    // Cumulative explicit-script source retained by a same-document realm.
    std::size_t maxRealmSourceBytes = kDefaultMaxJavaScriptRealmSourceBytes;
};

// A context is an independent, resettable bounded JavaScript realm.  execute() copies a
// bounded source view before lexing, so the resulting AST and diagnostics do
// not borrow the caller's source lifetime.  AST nodes retain views into that
// context-owned copy. Global bindings, function values, lexical environments,
// call-frame state, and runtime strings are cleared by reset().
class RuntimeContext {
public:
    explicit RuntimeContext(RuntimeLimits limits = RuntimeLimits());

    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;
    RuntimeContext(RuntimeContext&&) = delete;
    RuntimeContext& operator=(RuntimeContext&&) = delete;

    ScriptResult execute(SourceView source);
    // Execute another explicit script without resetting globals, closures,
    // host handles, or the shared execution/host-operation budgets.
    ScriptResult executeInSameRealm(SourceView source);
    void reset();

    const RuntimeLimits& limits() const { return limits_; }
    const Environment& environment() const { return environment_; }
    const Ast& ast() const { return ast_; }
    const ScriptResult& lastResult() const { return result_; }
    const Value& finalValue() const { return finalValue_; }

    const Value* lookup(SourceView name) const { return environment_.lookup(name); }
    // Returns an empty string for a non-string or stale value.  The returned
    // reference remains valid until reset() or the next string allocation.
    const std::string& stringValue(const Value& value) const;
    std::size_t runtimeStringBytes() const { return totalStringBytes_; }
    std::size_t runtimeStringValueCount() const { return strings_.size(); }
    std::size_t environmentCount() const { return 1u + environments_.size(); }
    std::size_t functionValueCount() const { return functions_.size(); }
    std::size_t userFunctionCount() const { return userFunctionCount_; }
    std::size_t nativeFunctionCount() const { return nativeFunctionCount_; }
    std::size_t objectCount() const { return objects_.size(); }
    std::size_t propertyCount() const { return totalPropertyCount_; }
    std::size_t arrayElementCount() const { return totalArrayElements_; }
    std::size_t activeCallFrameCount() const { return activeCallFrames_; }

    RuntimeObjectId objectPrototypeId() const { return objectPrototype_; }
    RuntimeObjectId arrayPrototypeId() const { return arrayPrototype_; }
    RuntimeObjectId mathObjectId() const { return mathObject_; }
    RuntimeObjectId prototypeOf(RuntimeObjectId object) const;
    // Test/diagnostic hook for proving cycle protection.  Script code has no
    // prototype mutation API; production host code should not use this hook.
    bool setPrototypeForTesting(RuntimeObjectId object,
        RuntimeObjectId prototype);
    bool readPropertyForTesting(RuntimeObjectId object, const std::string& key,
        Value& value, RuntimeErrorCode& error);
    // Synchronous host adapters may inspect a bounded ordinary runtime object
    // while validating a host call. Missing properties return Undefined.
    bool readObjectPropertyForHost(RuntimeObjectId object,
        const std::string& key, Value& value, RuntimeErrorCode& error);
    bool readHostPropertyForTesting(RuntimeHostObjectId object,
        const std::string& key, Value& value, RuntimeErrorCode& error);
    bool writeHostPropertyForTesting(RuntimeHostObjectId object,
        const std::string& key, Value value, RuntimeErrorCode& error);
    bool invokeFunctionForTesting(const Value& function,
        const std::vector<Value>& arguments, Value& result,
        RuntimeErrorCode& error);
    // Invoke a retained same-realm function from a synchronous host event.
    // Diagnostics are reset for this event, so a failing callback cannot
    // poison a later independent click.
    bool invokeFunctionInSameRealm(const Value& function,
        const std::vector<Value>& arguments, Value& result,
        RuntimeErrorCode& error);

    // Create or update the one synchronous host-created Event object used by
    // Navigator's bounded synchronous event dispatch paths. The returned
    // value is an ordinary runtime object, so normal property lookup,
    // assignment, and equality semantics apply. The host-defined properties
    // are immutable from script; target handles remain generation-scoped host
    // values and are canonicalized by the existing host-object registry.
    bool createOrUpdateEventObject(SourceView type,
        const HostObjectReference& target,
        const HostObjectReference& currentTarget, SourceView key,
        SourceView code, Value& result, RuntimeErrorCode& error);

    // The Navigator click adapter brackets one synchronous dispatch with
    // these calls. Event propagation methods are methods on the cached Event
    // object; outside this active window they are harmless no-ops and cannot
    // affect a later event.
    void beginEventDispatch();
    void endEventDispatch();
    // The host updates one dispatch-scoped byte at stage boundaries. The
    // cached Event property mirrors this value and remains host-owned.
    void setEventPhase(std::uint8_t phase);
    std::uint8_t eventPhase() const { return eventPhase_; }
    bool eventPropagationStopped() const
    {
        return eventDispatchActive_ && eventPropagationStopped_;
    }
    bool eventImmediatePropagationStopped() const
    {
        return eventDispatchActive_ && eventImmediatePropagationStopped_;
    }
    bool eventDefaultPrevented() const
    {
        return eventDispatchActive_ && eventDefaultPrevented_;
    }

    void setHostAdapter(HostAdapter* adapter) { hostAdapter_ = adapter; }
    HostAdapter* hostAdapter() const { return hostAdapter_; }
    bool registerHostObject(HostInstanceId instance, HostObjectKind kind,
        RuntimeHostObjectId& result, RuntimeErrorCode& error);
    bool installHostGlobal(const std::string& name, HostInstanceId instance,
        HostObjectKind kind, RuntimeErrorCode& error);
    bool invalidateHostGeneration(RuntimeErrorCode& error);
    HostGenerationId hostGeneration() const { return hostGeneration_; }
    std::size_t hostObjectCount() const { return liveHostObjectCount_; }
    std::size_t hostOperationCount() const { return hostOperations_; }
    std::size_t hostMethodCount() const { return hostMethodCount_; }
    bool builtInsInitialized() const { return builtInsInitialized_; }

private:
    class Evaluator;
    friend class Evaluator;

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
    };

    enum class NativeFunctionId : std::uint8_t {
        ObjectHasOwnProperty = 0,
        ArrayPush,
        ArrayPop,
        MathAbs,
        MathMin,
        MathMax,
        MathFloor,
        MathCeil,
        MathRound,
        EventStopPropagation,
        EventStopImmediatePropagation,
        EventPreventDefault,
    };

    struct RuntimeProperty {
        std::string key;
        Value value;
        bool readOnly = false;
    };

    struct RuntimeObject {
        bool array = false;
        RuntimeObjectId prototype = kInvalidRuntimeObjectId;
        std::vector<RuntimeProperty> properties;
        std::vector<Value> elements;
    };

    struct HostObjectRecord {
        bool live = false;
        HostObjectReference reference;
    };

    struct HostMethodRecord {
        RuntimeHostObjectId hostObject = kInvalidRuntimeHostObjectId;
        std::uint32_t method = 0;
        bool requiresReceiver = false;
        RuntimeFunctionId function = kInvalidRuntimeFunctionId;
    };

    struct HostGlobalSpec {
        std::string name;
        HostInstanceId instance = 0;
        HostObjectKind kind = 0;
    };

    bool createString(SourceView text, Value& value, RuntimeErrorCode& error);
    bool createOrGetCachedString(SourceView text, Value& value,
        RuntimeErrorCode& error);
    const std::string* stringData(const Value& value) const;
    bool createEnvironment(EnvironmentId parent, EnvironmentId& result,
        RuntimeErrorCode& error);
    Environment* environmentAt(EnvironmentId id);
    const Environment* environmentAt(EnvironmentId id) const;
    bool createFunction(AstNodeId declaration, EnvironmentId closure,
        RuntimeFunctionId& result, RuntimeErrorCode& error);
    bool createNativeFunction(NativeFunctionId native,
        RuntimeFunctionId& result, RuntimeErrorCode& error);
    bool createHostMethod(RuntimeHostObjectId object, std::uint32_t method,
        bool requiresReceiver, bool sharedAcrossReceivers,
        RuntimeFunctionId& result,
        RuntimeErrorCode& error);
    const FunctionRecord* functionAt(RuntimeFunctionId id) const;
    bool createObject(bool array, const std::vector<Value>& initialElements,
        RuntimeObjectId& result, RuntimeErrorCode& error,
        RuntimeObjectId prototype = kInvalidRuntimeObjectId);
    RuntimeObject* objectAt(RuntimeObjectId id);
    const RuntimeObject* objectAt(RuntimeObjectId id) const;
    bool readProperty(RuntimeObjectId object, const std::string& key,
        Value& value, RuntimeErrorCode& error,
        SourceLocation location = SourceLocation());
    bool writeProperty(RuntimeObjectId object, const std::string& key,
        Value value, RuntimeErrorCode& error, bool readOnly = false);
    bool updateExistingProperty(RuntimeObjectId object, const std::string& key,
        Value value, RuntimeErrorCode& error);
    bool initializeBuiltIns(RuntimeErrorCode& error);
    bool installHostGlobalInternal(const HostGlobalSpec& spec,
        RuntimeErrorCode& error, bool recordSpec);
    bool createHostObject(const HostObjectReference& reference,
        RuntimeHostObjectId& result, RuntimeErrorCode& error,
        bool validateAdapter);
    bool resolveHostObject(RuntimeHostObjectId object,
        HostObjectReference& reference, RuntimeErrorCode& error) const;
    bool consumeHostOperation(SourceLocation location);
    bool readHostProperty(RuntimeHostObjectId object, const std::string& key,
        Value& value, RuntimeErrorCode& error, SourceLocation location);
    bool writeHostProperty(RuntimeHostObjectId object, const std::string& key,
        Value value, RuntimeErrorCode& error, SourceLocation location);
    bool invokeHostMethod(const FunctionRecord& function,
        const std::vector<Value>& arguments, Value receiver,
        SourceLocation location, Value& result);
    bool convertValueToHost(const Value& value, HostValue& result,
        RuntimeErrorCode& error, SourceLocation location);
    bool convertHostValue(const HostValue& value, RuntimeHostObjectId methodOwner,
        Value& result, RuntimeErrorCode& error, SourceLocation location);
    bool validateAdapterReference(const HostObjectReference& reference,
        RuntimeErrorCode& error);
    RuntimeErrorCode mapHostResult(HostResultCode code) const;
    bool advanceHostGeneration(RuntimeErrorCode& error);
    void clearRuntimeState();
    void setRuntimeError(RuntimeErrorCode code, SourceLocation location);
    bool consumeStep(SourceLocation location);

    RuntimeLimits limits_;
    std::string sourceStorage_;
    std::string realmSourceStorage_;
    std::size_t realmSourceLineCount_ = 0;
    Ast ast_;
    Environment environment_;
    std::vector<Environment> environments_;
    std::vector<FunctionRecord> functions_;
    std::vector<std::string> strings_;
    std::vector<RuntimeObject> objects_;
    std::size_t totalStringBytes_ = 0;
    std::size_t totalPropertyCount_ = 0;
    std::size_t totalPropertyKeyBytes_ = 0;
    std::size_t totalArrayElements_ = 0;
    std::size_t userFunctionCount_ = 0;
    std::size_t nativeFunctionCount_ = 0;
    std::size_t hostMethodCount_ = 0;
    std::size_t executionSteps_ = 0;
    ScriptResult result_;
    Value finalValue_;
    std::size_t activeCallFrames_ = 0;
    RuntimeObjectId objectPrototype_ = kInvalidRuntimeObjectId;
    RuntimeObjectId arrayPrototype_ = kInvalidRuntimeObjectId;
    RuntimeObjectId mathObject_ = kInvalidRuntimeObjectId;
    RuntimeObjectId eventConstantsObject_ = kInvalidRuntimeObjectId;
    RuntimeObjectId eventObject_ = kInvalidRuntimeObjectId;
    RuntimeFunctionId eventStopPropagationFunction_ =
        kInvalidRuntimeFunctionId;
    RuntimeFunctionId eventStopImmediatePropagationFunction_ =
        kInvalidRuntimeFunctionId;
    RuntimeFunctionId eventPreventDefaultFunction_ =
        kInvalidRuntimeFunctionId;
    bool builtInsInitialized_ = false;
    bool eventDispatchActive_ = false;
    std::uint8_t eventPhase_ = kEventPhaseNone;
    bool eventPropagationStopped_ = false;
    bool eventImmediatePropagationStopped_ = false;
    bool eventDefaultPrevented_ = false;
    HostAdapter* hostAdapter_ = nullptr;
    HostGenerationId hostGeneration_ = kInvalidHostGenerationId;
    std::vector<HostObjectRecord> hostObjects_;
    std::vector<HostMethodRecord> hostMethods_;
    std::vector<HostGlobalSpec> hostGlobalSpecs_;
    std::size_t liveHostObjectCount_ = 0;
    std::size_t hostOperations_ = 0;
    bool hostCallActive_ = false;
};

ScriptResult executeScript(SourceView source, RuntimeContext& context);

const char* runtimeErrorCodeName(RuntimeErrorCode code);
const char* scriptStatusName(ScriptStatus status);

} // namespace javascript
} // namespace gxos
