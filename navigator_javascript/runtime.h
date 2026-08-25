#pragma once

#include "ast.h"
#include "environment.h"
#include "parser.h"
#include "value.h"

#include <cstddef>
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
    BindingLimitExceeded,
    BindingNameTooLong,
    StringLimitExceeded,
    ExecutionBudgetExceeded,
    IllegalBreak,
    IllegalContinue,
    IllegalReturn,
    InvalidAstState,
    AllocationFailure,
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

struct RuntimeLimits {
    LexerLimits lexer;
    ParserLimits parser;

    std::size_t maxBindings = kDefaultMaxJavaScriptRuntimeBindings;
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
};

// A context is an independent, resettable JS3 realm.  execute() copies a
// bounded source view before lexing, so the resulting AST and diagnostics do
// not borrow the caller's source lifetime.  AST nodes retain views into that
// context-owned copy.  Environment bindings and runtime strings are cleared
// by reset().
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

private:
    class Evaluator;
    friend class Evaluator;

    bool createString(SourceView text, Value& value, RuntimeErrorCode& error);
    const std::string* stringData(const Value& value) const;
    void setRuntimeError(RuntimeErrorCode code, SourceLocation location);
    bool consumeStep(SourceLocation location);

    RuntimeLimits limits_;
    std::string sourceStorage_;
    Ast ast_;
    Environment environment_;
    std::vector<std::string> strings_;
    std::size_t totalStringBytes_ = 0;
    std::size_t executionSteps_ = 0;
    ScriptResult result_;
    Value finalValue_;
};

ScriptResult executeScript(SourceView source, RuntimeContext& context);

const char* runtimeErrorCodeName(RuntimeErrorCode code);
const char* scriptStatusName(ScriptStatus status);

} // namespace javascript
} // namespace gxos
