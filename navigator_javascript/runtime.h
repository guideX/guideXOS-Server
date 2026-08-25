#pragma once

#include "ast.h"
#include "environment.h"
#include "parser.h"
#include "value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace javascript {

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
    bool builtInsInitialized() const { return builtInsInitialized_; }

private:
    class Evaluator;
    friend class Evaluator;

    struct FunctionRecord {
        enum class Kind : std::uint8_t {
            User = 0,
            Native,
        };

        Kind kind = Kind::User;
        AstNodeId declaration = kInvalidAstNodeId;
        EnvironmentId closureEnvironment = kInvalidEnvironmentId;
        std::uint8_t nativeFunction = 0;
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
    };

    struct RuntimeProperty {
        std::string key;
        Value value;
    };

    struct RuntimeObject {
        bool array = false;
        RuntimeObjectId prototype = kInvalidRuntimeObjectId;
        std::vector<RuntimeProperty> properties;
        std::vector<Value> elements;
    };

    bool createString(SourceView text, Value& value, RuntimeErrorCode& error);
    const std::string* stringData(const Value& value) const;
    bool createEnvironment(EnvironmentId parent, EnvironmentId& result,
        RuntimeErrorCode& error);
    Environment* environmentAt(EnvironmentId id);
    const Environment* environmentAt(EnvironmentId id) const;
    bool createFunction(AstNodeId declaration, EnvironmentId closure,
        RuntimeFunctionId& result, RuntimeErrorCode& error);
    bool createNativeFunction(NativeFunctionId native,
        RuntimeFunctionId& result, RuntimeErrorCode& error);
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
        Value value, RuntimeErrorCode& error);
    bool initializeBuiltIns(RuntimeErrorCode& error);
    void clearRuntimeState();
    void setRuntimeError(RuntimeErrorCode code, SourceLocation location);
    bool consumeStep(SourceLocation location);

    RuntimeLimits limits_;
    std::string sourceStorage_;
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
    std::size_t executionSteps_ = 0;
    ScriptResult result_;
    Value finalValue_;
    std::size_t activeCallFrames_ = 0;
    RuntimeObjectId objectPrototype_ = kInvalidRuntimeObjectId;
    RuntimeObjectId arrayPrototype_ = kInvalidRuntimeObjectId;
    RuntimeObjectId mathObject_ = kInvalidRuntimeObjectId;
    bool builtInsInitialized_ = false;
};

ScriptResult executeScript(SourceView source, RuntimeContext& context);

const char* runtimeErrorCodeName(RuntimeErrorCode code);
const char* scriptStatusName(ScriptStatus status);

} // namespace javascript
} // namespace gxos
