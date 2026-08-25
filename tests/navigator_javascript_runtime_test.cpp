#include "navigator_javascript/runtime.h"

#include <cmath>
#include <iostream>
#include <string>

using gxos::javascript::RuntimeContext;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeLimits;
using gxos::javascript::ScriptResult;
using gxos::javascript::ScriptStatus;
using gxos::javascript::SourceView;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

ScriptResult execute(RuntimeContext& context, const std::string& source)
{
    return context.execute(SourceView(source.data(), source.size()));
}

const Value* binding(const RuntimeContext& context, const char* name)
{
    const std::string spelling(name);
    return context.lookup(SourceView(spelling.data(), spelling.size()));
}

void expectType(const RuntimeContext& context, const char* name,
    ValueType type, const std::string& label)
{
    const Value* value = binding(context, name);
    expect(value != nullptr, label + ": binding exists");
    if (value != nullptr) expect(value->type() == type, label + ": type matches");
}

void expectNumber(const RuntimeContext& context, const char* name, double number,
    const std::string& label)
{
    const Value* value = binding(context, name);
    expect(value != nullptr, label + ": binding exists");
    if (value != nullptr) {
        expect(value->type() == ValueType::Number, label + ": Number type");
        if (value->type() == ValueType::Number) {
            expect(value->numberValue() == number, label + ": number matches");
        }
    }
}

void expectString(const RuntimeContext& context, const char* name,
    const std::string& text, const std::string& label)
{
    const Value* value = binding(context, name);
    expect(value != nullptr, label + ": binding exists");
    if (value != nullptr) {
        expect(value->type() == ValueType::String, label + ": String type");
        if (value->type() == ValueType::String) {
            expect(context.stringValue(*value) == text, label + ": string matches");
        }
    }
}

void expectError(const std::string& source, RuntimeErrorCode code,
    const std::string& label, RuntimeLimits limits = RuntimeLimits())
{
    RuntimeContext context(limits);
    const ScriptResult result = execute(context, source);
    expect(!result.succeeded(), label + ": execution fails");
    expect(result.status == (code == RuntimeErrorCode::ExecutionBudgetExceeded
        ? ScriptStatus::ExecutionBudgetExceeded : ScriptStatus::RuntimeFailure),
        label + ": status matches");
    expect(result.runtimeError.code == code, label + ": error code matches");
    expect(result.runtimeError.location.line >= 1 &&
        result.runtimeError.location.column >= 1, label + ": location exists");
}

void testVariablesArithmeticAndReset()
{
    RuntimeContext context;
    const std::string source = "var x = 10; var missing; var result = 2 + 3 * 4; x += 5;";
    const ScriptResult result = execute(context, source);
    expect(result.succeeded(), "variables/arithmetic: succeeds");
    expectNumber(context, "x", 15.0, "variables/arithmetic: x");
    expectType(context, "missing", ValueType::Undefined, "variables: undefined");
    expectNumber(context, "result", 14.0, "arithmetic precedence: result");

    context.reset();
    const std::string second = "var y = 7;";
    expect(execute(context, second).succeeded(), "reset: second script succeeds");
    expect(binding(context, "x") == nullptr, "reset: first binding is gone");
    expectNumber(context, "y", 7.0, "reset: second binding remains");
}

void testPrimitivesAndStrings()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var a = true; var b = false; var c = null;"
        "var x = \"hello\"; var y = ' world'; var z = x + y;"
        "var mixed = \"answer=\" + 42;");
    expect(result.succeeded(), "primitives/strings: succeeds");
    expectType(context, "a", ValueType::Boolean, "primitive: true");
    expectType(context, "b", ValueType::Boolean, "primitive: false");
    expectType(context, "c", ValueType::Null, "primitive: null");
    expect(binding(context, "a")->booleanValue(), "primitive: true value");
    expect(!binding(context, "b")->booleanValue(), "primitive: false value");
    expectString(context, "x", "hello", "string literal x");
    expectString(context, "y", " world", "string literal y");
    expectString(context, "z", "hello world", "string concatenation");
    expectString(context, "mixed", "answer=42", "mixed concatenation");
}

void testUnaryTruthinessAndNumbers()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var a = !0; var b = !\"\"; var c = !!\"x\";"
        "var d = -0; var e = +\"2\"; var f = !null;"
        "var positiveInfinity = 1 / 0; var nan = 0 / 0;");
    expect(result.succeeded(), "unary/truthiness: succeeds");
    expect(binding(context, "a")->booleanValue(), "truthiness: zero is false");
    expect(binding(context, "b")->booleanValue(), "truthiness: empty string is false");
    expect(binding(context, "c")->booleanValue(), "truthiness: non-empty string is true");
    expect(binding(context, "f")->booleanValue(), "truthiness: null is false");
    expect(std::signbit(binding(context, "d")->numberValue()),
        "number: negative zero is preserved");
    expectNumber(context, "e", 2.0, "unary plus string conversion");
    expect(std::isinf(binding(context, "positiveInfinity")->numberValue()),
        "number: division by zero produces infinity");
    expect(std::isnan(binding(context, "nan")->numberValue()),
        "number: zero divided by zero produces NaN");
}

void testEqualityAndShortCircuit()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var a = 1 === 1; var b = 1 === \"1\"; var c = null === null;"
        "var d = null === undefined; var e = 1 == \"1\";"
        "var f = null == undefined; var x = 0;"
        "false && (x = 10); true || (x = 20);"
        "var g = 0 || 5; var h = \"hello\" && 7;");
    expect(result.succeeded(), "equality/short-circuit: succeeds");
    expect(binding(context, "a")->booleanValue(), "strict equality true");
    expect(!binding(context, "b")->booleanValue(), "strict equality is type-sensitive");
    expect(binding(context, "c")->booleanValue(), "strict null equality");
    expect(!binding(context, "d")->booleanValue(), "null and undefined differ strictly");
    expect(binding(context, "e")->booleanValue(), "loose number/string equality");
    expect(binding(context, "f")->booleanValue(), "loose null/undefined equality");
    expectNumber(context, "x", 0.0, "logical operators short-circuit RHS");
    expectNumber(context, "g", 5.0, "logical OR returns right operand");
    expectNumber(context, "h", 7.0, "logical AND returns right operand");
}

void testAssignmentAndUpdates()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var x = 5; var y = x++; var z = ++x;"
        "x -= 1; x *= 2; x /= 2; x %= 3;");
    expect(result.succeeded(), "assignment/update: succeeds");
    expectNumber(context, "y", 5.0, "postfix update returns old value");
    expectNumber(context, "z", 7.0, "prefix update returns new value");
    expectNumber(context, "x", 0.0, "compound assignments execute");
}

void testControlFlow()
{
    RuntimeContext context;
    const ScriptResult ifResult = execute(context,
        "var selected = 0; if (true) { selected = 5; } else { selected = 10; }");
    expect(ifResult.succeeded(), "if/else: succeeds");
    expectNumber(context, "selected", 5.0, "if selects only true branch");

    const ScriptResult whileResult = execute(context,
        "var x = 0; while (x < 5) { x++; }");
    expect(whileResult.succeeded(), "while: succeeds");
    expectNumber(context, "x", 5.0, "while reaches five");

    const ScriptResult forResult = execute(context,
        "var sum = 0; for (var i = 0; i < 5; i++) { sum += i; }");
    expect(forResult.succeeded(), "for: succeeds");
    expectNumber(context, "sum", 10.0, "for sums zero through four");
    expectNumber(context, "i", 5.0, "for uses global var environment");

    const ScriptResult breakResult = execute(context,
        "var stopped = 0; while (true) { stopped++; if (stopped === 3) { break; } }");
    expect(breakResult.succeeded(), "break: succeeds");
    expectNumber(context, "stopped", 3.0, "break exits loop");

    const ScriptResult continueResult = execute(context,
        "var total = 0; var n = 0; while (n < 5) { n++;"
        "if (n === 3) { continue; } total += n; }");
    expect(continueResult.succeeded(), "continue: succeeds");
    expectNumber(context, "n", 5.0, "continue loop counter");
    expectNumber(context, "total", 12.0, "continue skips selected mutation");

    RuntimeLimits omitted;
    omitted.maxExecutionSteps = 32;
    RuntimeContext omittedContext(omitted);
    const ScriptResult omittedResult = execute(omittedContext, "for (;;) { }");
    expect(omittedResult.status == ScriptStatus::ExecutionBudgetExceeded,
        "omitted for components: budget still bounds loop");
}

void testNegativeRuntimeCases()
{
    expectError("x + 1;", RuntimeErrorCode::UnknownIdentifier,
        "unknown identifier");
    expectError("break;", RuntimeErrorCode::IllegalBreak, "illegal break");
    expectError("continue;", RuntimeErrorCode::IllegalContinue,
        "illegal continue");
    expectError("return 1;", RuntimeErrorCode::IllegalReturn, "illegal return");
    expectError("foo();", RuntimeErrorCode::UnsupportedFeature,
        "function call before JS4");
    expectError("foo.bar;", RuntimeErrorCode::UnsupportedFeature,
        "member access before object support");
    expectError("new Foo();", RuntimeErrorCode::UnsupportedFeature,
        "new before object support");
    expectError("function add(a, b) { return a + b; }",
        RuntimeErrorCode::UnsupportedFeature, "function declaration before JS4");
    expectError("this;", RuntimeErrorCode::UnsupportedFeature,
        "this before host policy");
    expectError("var foo = 1; foo.bar = 2;",
        RuntimeErrorCode::InvalidAssignmentTarget, "member assignment target");

    RuntimeContext lexical;
    const ScriptResult lexicalResult = execute(lexical, "@");
    expect(lexicalResult.status == ScriptStatus::LexicalFailure,
        "lexical failure is distinct");

    RuntimeContext parse;
    const ScriptResult parseResult = execute(parse, "var = 1;");
    expect(parseResult.status == ScriptStatus::ParseFailure,
        "parse failure is distinct");
}

void testLimitsAndBudget()
{
    RuntimeLimits bindingLimits;
    bindingLimits.maxBindings = 1;
    expectError("var a = 1; var b = 2;", RuntimeErrorCode::BindingLimitExceeded,
        "binding limit", bindingLimits);

    RuntimeLimits nameLimits;
    nameLimits.maxBindingNameLength = 2;
    expectError("var abc = 1;", RuntimeErrorCode::BindingNameTooLong,
        "binding name limit", nameLimits);

    RuntimeLimits stringLimits;
    stringLimits.maxTotalRuntimeStringBytes = 1;
    expectError("var x = \"a\"; var y = \"b\";",
        RuntimeErrorCode::StringLimitExceeded, "string byte limit", stringLimits);

    RuntimeLimits lengthLimits;
    lengthLimits.maxRuntimeStringLength = 2;
    expectError("var x = \"abc\";", RuntimeErrorCode::StringLimitExceeded,
        "string length limit", lengthLimits);

    RuntimeLimits budget;
    budget.maxExecutionSteps = 24;
    RuntimeContext first(budget);
    const ScriptResult firstResult = execute(first, "while (true) { }");
    expect(firstResult.status == ScriptStatus::ExecutionBudgetExceeded,
        "infinite while is bounded");
    expect(firstResult.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded,
        "infinite while has budget error");
    expect(firstResult.executionSteps == budget.maxExecutionSteps,
        "budget step count is deterministic");

    RuntimeContext second(budget);
    const ScriptResult secondResult = execute(second, "while (true) { }");
    expect(secondResult.executionSteps == firstResult.executionSteps &&
        secondResult.runtimeError.location.offset ==
            firstResult.runtimeError.location.offset,
        "budget failure is repeatably located");
}

void testContextOwnsSource()
{
    RuntimeContext context;
    {
        std::string temporary = "var message = \"owned\";";
        expect(execute(context, temporary).succeeded(), "owned source executes");
    }
    expectString(context, "message", "owned", "context owns source-dependent AST");
}

} // namespace

int main()
{
    testVariablesArithmeticAndReset();
    testPrimitivesAndStrings();
    testUnaryTruthinessAndNumbers();
    testEqualityAndShortCircuit();
    testAssignmentAndUpdates();
    testControlFlow();
    testNegativeRuntimeCases();
    testLimitsAndBudget();
    testContextOwnsSource();

    if (failures != 0) {
        std::cerr << "Navigator JavaScript runtime tests FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "Navigator JavaScript runtime tests PASS\n";
    return 0;
}
