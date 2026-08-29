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
    expectError("foo();", RuntimeErrorCode::UnknownIdentifier,
        "unknown function identifier");
    expectError("var foo = null; foo.bar;", RuntimeErrorCode::CannotReadProperty,
        "null member access");
    expectError("new Foo();", RuntimeErrorCode::UnsupportedFeature,
        "new before object support");
    RuntimeContext thisContext;
    const ScriptResult thisResult = execute(thisContext, "var value = this;");
    expect(thisResult.succeeded(), "standalone this policy: succeeds");
    expectType(thisContext, "value", ValueType::Undefined,
        "standalone this policy: Undefined");
    expectError("var foo = 1; foo.bar = 2;",
        RuntimeErrorCode::NotObject, "non-object member assignment");

    RuntimeContext lexical;
    const ScriptResult lexicalResult = execute(lexical, "@");
    expect(lexicalResult.status == ScriptStatus::LexicalFailure,
        "lexical failure is distinct");

    RuntimeContext parse;
    const ScriptResult parseResult = execute(parse, "var = 1;");
    expect(parseResult.status == ScriptStatus::ParseFailure,
        "parse failure is distinct");
}

void testObjectsAndProperties()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var obj = { x: 10, y: 20 }; obj.x += 5;"
        "var result = obj.x + obj.y;"
        "var key = \"x\"; var computed = obj[key];"
        "obj[1] = 8; var numeric = obj[\"1\"];"
        "var missing = obj.missing;");
    expect(result.succeeded(), "objects: basic properties succeed");
    expectType(context, "obj", ValueType::Object, "objects: object value");
    expectNumber(context, "result", 35.0, "objects: member arithmetic");
    expectNumber(context, "computed", 15.0, "objects: computed read");
    expectNumber(context, "numeric", 8.0, "objects: numeric key coercion");
    expectType(context, "missing", ValueType::Undefined,
        "objects: missing property is Undefined");
    expect(context.objectCount() == 5 && context.propertyCount() == 16,
        "objects: bounded pool and duplicate-free properties");

    const ScriptResult duplicate = execute(context,
        "var order = 0; function record(v) { order = order * 10 + v; return v; }"
        "var duplicate = { x: record(1), x: record(2) };"
        "var duplicateValue = duplicate.x;");
    expect(duplicate.succeeded(), "objects: literal evaluation order succeeds");
    expectNumber(context, "order", 12.0, "objects: properties initialize left-to-right");
    expectNumber(context, "duplicateValue", 2.0,
        "objects: latest duplicate property wins");

    const ScriptResult aliases = execute(context,
        "var a = { x: 1 }; var b = a; b.x = 5; var aliasResult = a.x;"
        "var c = {}; var distinct = a === c; var same = a === b;");
    expect(aliases.succeeded(), "objects: aliases succeed");
    expectNumber(context, "aliasResult", 5.0, "objects: alias mutation");
    expect(binding(context, "same")->booleanValue(),
        "objects: aliases compare by identity");
    expect(!binding(context, "distinct")->booleanValue(),
        "objects: distinct objects compare unequal");

    const ScriptResult nested = execute(context,
        "var nested = { inner: { value: 1 } };"
        "nested.inner.value = 8; var nestedResult = nested[\"inner\"][\"value\"];"
        "var post = nested.inner.value++; var prefix = ++nested.inner.value;");
    expect(nested.succeeded(), "objects: nested members and updates succeed");
    expectNumber(context, "nestedResult", 8.0, "objects: nested read");
    expectNumber(context, "post", 8.0, "objects: postfix member update");
    expectNumber(context, "prefix", 10.0, "objects: prefix member update");
}

void testArrays()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var values = [10, 20, 30]; values[1] = 99;"
        "var result = values[1]; var len = values.length;"
        "values[5] = 7; var grown = values.length;"
        "var hole = values[3]; var same = values === values;");
    expect(result.succeeded(), "arrays: indexing and assignment succeed");
    expectType(context, "values", ValueType::Object, "arrays: array is object value");
    expectNumber(context, "result", 99.0, "arrays: indexed read");
    expectNumber(context, "len", 3.0, "arrays: length read");
    expectNumber(context, "grown", 6.0, "arrays: bounded dense growth");
    expectType(context, "hole", ValueType::Undefined,
        "arrays: grown holes are Undefined");
    expect(binding(context, "same")->booleanValue(),
        "arrays: identity equality");

    const ScriptResult loop = execute(context,
        "var a = [1, 2, 3, 4]; var sum = 0;"
        "for (var i = 0; i < a.length; i++) { sum += a[i]; }");
    expect(loop.succeeded(), "arrays: indexed loop succeeds");
    expectNumber(context, "sum", 10.0, "arrays: indexed loop result");

    const ScriptResult ordinary = execute(context,
        "var a = []; a.name = \"test\"; var name = a.name;"
        "var independent = []; var distinct = a === independent;");
    expect(ordinary.succeeded(), "arrays: ordinary properties succeed");
    expectString(context, "name", "test", "arrays: ordinary property");
    expect(!binding(context, "distinct")->booleanValue(),
        "arrays: distinct arrays compare unequal");
}

void testObjectFunctionInteraction()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "function update(obj) { obj.value = obj.value + 1; }"
        "var target = { value: 4 }; update(target); var result = target.value;"
        "function make() { return { value: 12 }; }"
        "var returned = make(); var returnedValue = returned.value;"
        "var directReturnedValue = make().value;"
        "function makeArray() { return [4, 5, 6]; }"
        "var values = makeArray(); var arrayValue = values[2];");
    expect(result.succeeded(), "objects: function interaction succeeds");
    expectNumber(context, "result", 5.0, "objects: passed object is aliased");
    expectNumber(context, "returnedValue", 12.0, "objects: returned object survives");
    expectNumber(context, "directReturnedValue", 12.0,
        "objects: direct returned member read");
    expectNumber(context, "arrayValue", 6.0, "arrays: returned array survives");

    const ScriptResult closure = execute(context,
        "function makeBox() { var box = { value: 0 };"
        " function next() { box.value++; return box.value; } return next; }"
        "var next = makeBox(); var first = next(); var second = next();");
    expect(closure.succeeded(), "objects: closure interaction succeeds");
    expectNumber(context, "first", 1.0, "objects: first closure mutation");
    expectNumber(context, "second", 2.0, "objects: retained object mutation");

    const ScriptResult functionProperty = execute(context,
        "function getValue() { return 8; } var obj = {}; obj.f = getValue;"
        "var f = obj.f; var result = f(); var same = obj.f === getValue;");
    expect(functionProperty.succeeded(), "objects: function-valued property succeeds");
    expectNumber(context, "result", 8.0, "objects: function-valued property call");
    expect(binding(context, "same")->booleanValue(),
        "objects: function property preserves identity");
    expectError("var obj = {}; obj.f();", RuntimeErrorCode::NotCallable,
        "objects: missing member call is not callable");
}

void testObjectArrayLimitsAndReset()
{
    RuntimeLimits objects;
    objects.maxObjects = 4;
    expectError("var a = {}; var b = {};", RuntimeErrorCode::ObjectLimitExceeded,
        "objects: object limit", objects);

    RuntimeLimits properties;
    properties.maxPropertiesPerObject = 6;
    expectError("var a = {}; a.a = 1; a.b = 2; a.c = 3; a.d = 4;"
        "a.e = 5; a.f = 6; a.g = 7;",
        RuntimeErrorCode::PropertyLimitExceeded, "objects: property limit", properties);

    RuntimeLimits totalProperties;
    totalProperties.maxTotalProperties = 14;
    expectError("var a = {}; a.x = 1; var b = {}; b.y = 2;",
        RuntimeErrorCode::PropertyLimitExceeded,
        "objects: total property limit", totalProperties);

    RuntimeLimits propertyName;
    propertyName.maxPropertyNameLength = 2;
    expectError("var a = {}; a.long = 1;",
        RuntimeErrorCode::BuiltInInitializationFailed,
        "objects: property name limit", propertyName);

    RuntimeLimits arrays;
    arrays.maxArrayElements = 2;
    expectError("var a = []; a[2] = 1;", RuntimeErrorCode::ArrayLimitExceeded,
        "arrays: element limit", arrays);

    RuntimeLimits totalArrays;
    totalArrays.maxTotalArrayElements = 2;
    expectError("var a = [1, 2]; var b = [3];",
        RuntimeErrorCode::ArrayLimitExceeded, "arrays: total element limit", totalArrays);

    RuntimeLimits index;
    index.maxArrayIndex = 2;
    expectError("var a = []; a[3] = 1;", RuntimeErrorCode::ArrayIndexOutOfRange,
        "arrays: index limit", index);

    RuntimeLimits budget;
    budget.maxExecutionSteps = 3;
    expectError("var a = {}; a.x = 1;", RuntimeErrorCode::ExecutionBudgetExceeded,
        "objects: operations share execution budget", budget);

    RuntimeContext context;
    expect(execute(context, "var a = { x: 1 }; a.x = 2; var b = [3];").succeeded(),
        "reset: objects and arrays setup succeeds");
    expect(context.objectCount() == 6, "reset: objects are retained before reset");
    context.reset();
    expect(context.objectCount() == 4 && context.propertyCount() == 13 &&
        context.arrayElementCount() == 0, "reset: object pools are cleared");
    expect(execute(context, "var result = 7;").succeeded(),
        "reset: new script after object reset succeeds");
    expectNumber(context, "result", 7.0, "reset: new script result");
}

void testFunctionsAndLexicalScope()
{
    RuntimeContext context;
    const ScriptResult basic = execute(context,
        "function add(a, b) { return a + b; }"
        "var result = add(2, 3);");
    expect(basic.succeeded(), "functions: basic call succeeds");
    expectType(context, "add", ValueType::Function,
        "functions: declaration creates Function value");
    expectNumber(context, "result", 5.0, "functions: add result");

    const ScriptResult hoisted = execute(context,
        "var result = add(2, 3);"
        "function add(a, b) { return a + b; }");
    expect(hoisted.succeeded(), "functions: declaration hoisting succeeds");
    expectNumber(context, "result", 5.0, "functions: hoisted call result");

    const ScriptResult valueCall = execute(context,
        "function foo() { return 9; }"
        "var f = foo; var result = f(); var same = f === foo;");
    expect(valueCall.succeeded(), "functions: value call succeeds");
    expectNumber(context, "result", 9.0, "functions: value call result");
    expect(binding(context, "same")->booleanValue(),
        "functions: stable identity equality");

    const ScriptResult argumentsResult = execute(context,
        "function foo(a, b) { return b; }"
        "var missing = foo(1);"
        "function first(a) { return a; }"
        "var extra = first(5, 6, 7);");
    expect(argumentsResult.succeeded(), "functions: argument bounds succeed");
    expectType(context, "missing", ValueType::Undefined,
        "functions: missing argument is Undefined");
    expectNumber(context, "extra", 5.0,
        "functions: extra arguments are ignored after evaluation");

    const ScriptResult order = execute(context,
        "var x = 0;"
        "function value(v) { x = x * 10 + v; return v; }"
        "function pair(a, b) { return a + b; }"
        "var result = pair(value(1), value(2));");
    expect(order.succeeded(), "functions: argument order succeeds");
    expectNumber(context, "x", 12.0, "functions: arguments evaluate left-to-right");
    expectNumber(context, "result", 3.0, "functions: ordered argument result");

    const ScriptResult scope = execute(context,
        "var global = 1;"
        "function outer() {"
        "  var local = 2;"
        "  function inner() { return global + local; }"
        "  return inner();"
        "}"
        "var result = outer();");
    expect(scope.succeeded(), "functions: nested lexical call succeeds");
    expectNumber(context, "result", 3.0,
        "functions: nested function sees lexical parents");

    const ScriptResult shadow = execute(context,
        "var x = 1;"
        "function foo() { var x = 2; return x; }"
        "var y = foo();");
    expect(shadow.succeeded(), "functions: shadowing succeeds");
    expectNumber(context, "x", 1.0, "functions: local shadow preserves global");
    expectNumber(context, "y", 2.0, "functions: local shadow result");

    const ScriptResult outerAssignment = execute(context,
        "var x = 1; function foo() { x = 2; } foo();");
    expect(outerAssignment.succeeded(), "functions: outer assignment succeeds");
    expectNumber(context, "x", 2.0, "functions: assignment finds outer binding");

    const ScriptResult fresh = execute(context,
        "function foo(v) { var x = v; x++; return x; }"
        "var a = foo(1); var b = foo(10);");
    expect(fresh.succeeded(), "functions: fresh invocation locals succeed");
    expectNumber(context, "a", 2.0, "functions: first local is fresh");
    expectNumber(context, "b", 11.0, "functions: second local is fresh");
    expect(binding(context, "x") == nullptr,
        "functions: function var does not leak globally");

    const ScriptResult returnResult = execute(context,
        "function explicit() { return 42; }"
        "function empty() { return; }"
        "function falloff() { var x = 1; }"
        "var a = explicit(); var b = empty(); var c = falloff();");
    expect(returnResult.succeeded(), "functions: return forms succeed");
    expectNumber(context, "a", 42.0, "functions: explicit return");
    expectType(context, "b", ValueType::Undefined,
        "functions: bare return is Undefined");
    expectType(context, "c", ValueType::Undefined,
        "functions: falloff is Undefined");

    const ScriptResult propagation = execute(context,
        "function find() { var x = 0; while (true) {"
        "  if (x === 4) { return x; } x++;"
        "} } var result = find();");
    expect(propagation.succeeded(), "functions: return propagation succeeds");
    expectNumber(context, "result", 4.0,
        "functions: return exits nested loop and conditional");

    const ScriptResult recursion = execute(context,
        "function factorial(n) { if (n <= 1) { return 1; }"
        " return n * factorial(n - 1); }"
        "var result = factorial(5);");
    expect(recursion.succeeded(), "functions: recursion succeeds");
    expectNumber(context, "result", 120.0, "functions: factorial result");

    const ScriptResult expressionCalls = execute(context,
        "function add(a, b) { return a + b; }"
        "function double(x) { return x * 2; }"
        "function addOne(x) { return x + 1; }"
        "var x = 1 + add(2, 3) * 4;"
        "var result = double(addOne(4));");
    expect(expressionCalls.succeeded(), "functions: expression calls succeed");
    expectNumber(context, "x", 21.0, "functions: call precedence in expression");
    expectNumber(context, "result", 10.0, "functions: nested call result");

    const ScriptResult primitives = execute(context,
        "function u() { return; } function n() { return null; }"
        "function b() { return true; } function z() { return 0; }"
        "function s() { return \"hello\"; }"
        "var uResult = u(); var nResult = n(); var bResult = b();"
        "var zResult = z(); var sResult = s();");
    expect(primitives.succeeded(), "functions: primitive returns succeed");
    expectType(context, "uResult", ValueType::Undefined, "functions: Undefined return");
    expectType(context, "nResult", ValueType::Null, "functions: Null return");
    expectType(context, "bResult", ValueType::Boolean, "functions: Boolean return");
    expectNumber(context, "zResult", 0.0, "functions: Number return");
    expectString(context, "sResult", "hello", "functions: String return");

    const ScriptResult redeclaration = execute(context,
        "function foo() { return 1; } function foo() { return 2; }"
        "var result = foo(); var x = 1; var x;");
    expect(redeclaration.succeeded(), "functions: redeclaration succeeds");
    expectNumber(context, "result", 2.0,
        "functions: latest declaration is applicable");
    expectNumber(context, "x", 1.0, "functions: var redeclaration preserves value");

    const ScriptResult assignmentHoist = execute(context,
        "x = 5; var x;");
    expect(assignmentHoist.succeeded(), "functions: var hoisting assignment succeeds");
    expectNumber(context, "x", 5.0, "functions: hoisted var accepts early assignment");
}

void testFunctionLimitsAndFailures()
{
    expectError("var x = 123; x();", RuntimeErrorCode::NotCallable,
        "functions: non-callable value");
    expectError("return 5;", RuntimeErrorCode::IllegalReturn,
        "functions: top-level return remains illegal");
    expectError("if (true) { function nested() { return 1; } }",
        RuntimeErrorCode::UnsupportedFunctionConstruct,
        "functions: block declaration policy");

    RuntimeLimits depth;
    depth.maxCallDepth = 4;
    expectError("function recurse() { return recurse(); } recurse();",
        RuntimeErrorCode::CallDepthExceeded, "functions: call depth", depth);

    RuntimeLimits budget;
    budget.maxExecutionSteps = 48;
    expectError("function spin() { while (true) { } } spin();",
        RuntimeErrorCode::ExecutionBudgetExceeded,
        "functions: shared execution budget", budget);

    RuntimeLimits environments;
    environments.maxEnvironments = 2;
    expectError("function foo() { return 1; } foo(); foo();",
        RuntimeErrorCode::EnvironmentLimitExceeded,
        "functions: environment limit", environments);

    RuntimeLimits functions;
    functions.maxFunctions = 0;
    expectError("function first() { return 1; } function second() { return 2; }",
        RuntimeErrorCode::FunctionLimitExceeded,
        "functions: function value limit", functions);
}

void testFunctionClosureAndResetLifetime()
{
    RuntimeContext context;
    const ScriptResult closure = execute(context,
        "function makeCounter() { var x = 0;"
        " function next() { x++; return x; } return next; }"
        "var counter = makeCounter(); var a = counter(); var b = counter();");
    expect(closure.succeeded(), "functions: escaped closure succeeds safely");
    expectNumber(context, "a", 1.0, "functions: first closure call");
    expectNumber(context, "b", 2.0, "functions: closure retains lexical environment");
    expect(context.activeCallFrameCount() == 0,
        "functions: calls unwind all active frames");
    expect(context.environmentCount() >= 4,
        "functions: retained environment identities are bounded and stable");

    const ScriptResult failed = execute(context,
        "function fail() { return missing; } fail();");
    expect(!failed.succeeded(), "functions: failed call reports failure");
    expect(context.activeCallFrameCount() == 0,
        "functions: failed call unwinds active frames");

    context.reset();
    expect(context.activeCallFrameCount() == 0,
        "functions: reset clears active frames");
    expect(context.environmentCount() == 1,
        "functions: reset clears function environments");
    expect(context.functionValueCount() == 9 && context.nativeFunctionCount() == 9,
        "functions: reset recreates native identities");
    expect(execute(context, "var result = 7;").succeeded(),
        "functions: unrelated script after reset succeeds");
    expectNumber(context, "result", 7.0, "functions: post-reset result");
    expect(binding(context, "counter") == nullptr,
        "functions: old closure binding does not survive reset");
}

void testLimitsAndBudget()
{
    RuntimeLimits bindingLimits;
    bindingLimits.maxBindings = 1;
    expectError("var a = 1; var b = 2;", RuntimeErrorCode::BindingLimitExceeded,
        "binding limit", bindingLimits);

    RuntimeLimits nameLimits;
    nameLimits.maxBindingNameLength = 4;
    expectError("var abcde = 1;", RuntimeErrorCode::BindingNameTooLong,
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
    testFunctionsAndLexicalScope();
    testFunctionLimitsAndFailures();
    testFunctionClosureAndResetLifetime();
    testObjectsAndProperties();
    testArrays();
    testObjectFunctionInteraction();
    testObjectArrayLimitsAndReset();
    testLimitsAndBudget();
    testContextOwnsSource();

    if (failures != 0) {
        std::cerr << "Navigator JavaScript runtime tests FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "Navigator JavaScript runtime tests PASS\n";
    return 0;
}
