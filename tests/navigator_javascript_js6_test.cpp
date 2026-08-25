#include "navigator_javascript/runtime.h"

#include <iostream>
#include <string>

using gxos::javascript::RuntimeContext;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeLimits;
using gxos::javascript::RuntimeObjectId;
using gxos::javascript::ScriptResult;
using gxos::javascript::ScriptStatus;
using gxos::javascript::SourceView;
using gxos::javascript::Value;
using gxos::javascript::ValueType;
using gxos::javascript::kInvalidRuntimeObjectId;

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

void expectNumber(const RuntimeContext& context, const char* name,
    double expected, const std::string& label)
{
    const Value* value = binding(context, name);
    expect(value != nullptr, label + ": binding exists");
    if (value != nullptr) {
        expect(value->type() == ValueType::Number, label + ": Number type");
        if (value->isNumber()) expect(value->numberValue() == expected,
            label + ": value matches");
    }
}

void expectError(RuntimeLimits limits, const std::string& source,
    RuntimeErrorCode code, const std::string& label)
{
    RuntimeContext context(limits);
    const ScriptResult result = execute(context, source);
    expect(!result.succeeded(), label + ": fails");
    expect(result.status == (code == RuntimeErrorCode::ExecutionBudgetExceeded
        ? ScriptStatus::ExecutionBudgetExceeded : ScriptStatus::RuntimeFailure),
        label + ": status");
    expect(result.runtimeError.code == code, label + ": error");
}

void testBuiltInsAndMath()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var a = Math.abs; var b = Math.abs; var same = a === b;"
        "var abs = a(-7); var min = Math.min(5, 2, 9);"
        "var max = Math.max(5, 2, 9); var floor = Math.floor(3.8);"
        "var ceil = Math.ceil(3.2); var round = Math.round(3.6);"
        "var noMin = Math.min(); var noMax = Math.max();"
        "var stringLength = \"hello\".length;");
    expect(result.succeeded(), "built-ins/math: succeeds");
    expect(binding(context, "same")->booleanValue(),
        "built-ins: native identity is stable");
    expectNumber(context, "abs", 7.0, "Math.abs as value");
    expectNumber(context, "min", 2.0, "Math.min");
    expectNumber(context, "max", 9.0, "Math.max");
    expectNumber(context, "floor", 3.0, "Math.floor");
    expectNumber(context, "ceil", 4.0, "Math.ceil");
    expectNumber(context, "round", 4.0, "Math.round");
    expectNumber(context, "stringLength", 5.0, "primitive string length");
}

void testNativeInteroperabilityAndThis()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "function apply(f, x) { return f(x); }"
        "function getValue() { return this.value; }"
        "var obj = { value: 12, getValue: getValue };"
        "var applied = apply(Math.abs, -6); var method = obj.getValue();"
        "var stored = {}; stored.f = Math.abs; var storedResult = stored.f(-3);"
        "var detached = obj.getValue; var detachedIsFunction = detached !== undefined;");
    expect(result.succeeded(), "native/user interoperability: succeeds");
    expectNumber(context, "applied", 6.0, "native higher-order call");
    expectNumber(context, "method", 12.0, "user method receiver");
    expectNumber(context, "storedResult", 3.0, "native stored in object");
    expect(binding(context, "detachedIsFunction")->booleanValue(),
        "detached method remains a function value");

    const ScriptResult detached = execute(context,
        "function getValue() { return this.value; }"
        "var obj = { value: 12, getValue: getValue };"
        "var f = obj.getValue; var result = f();");
    expect(!detached.succeeded(), "detached method: fails safely");
    expect(detached.runtimeError.code == RuntimeErrorCode::CannotReadProperty,
        "detached method: Undefined this is not retained");
}

void testArrayPrototypeAndShadowing()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var values = [1, 2]; var push = values.push;"
        "var length = values.push(3, 4); var last = values.pop();"
        "var alias = values; alias.push(8); var aliasValue = values[3];"
        "var prototypeValue = values.doesNotExist;"
        "var inherited = values.hasOwnProperty(\"push\");");
    expect(result.succeeded(), "array prototype: succeeds");
    expectNumber(context, "length", 4.0, "push returns length");
    expectNumber(context, "last", 4.0, "pop returns last element");
    expectNumber(context, "aliasValue", 8.0, "array aliases share mutation");
    expect(binding(context, "prototypeValue")->isUndefined(),
        "missing prototype property is Undefined");
    expect(!binding(context, "inherited")->booleanValue(),
        "hasOwnProperty excludes inherited push");
    const Value* push = binding(context, "push");
    expect(push != nullptr && push->isFunction(),
        "array method is a callable inherited value");

    const ScriptResult shadow = execute(context,
        "var a = []; var b = []; var original = a.push; a.push = 9;"
        "var shadowed = a.push; var isolated = b.push !== undefined;");
    expect(shadow.succeeded(), "array shadowing: succeeds");
    expectNumber(context, "shadowed", 9.0, "own push shadows prototype");
    expect(binding(context, "isolated")->booleanValue(),
        "prototype mutation is isolated to own property");
    expect(binding(context, "original")->isFunction(),
        "shadowing does not mutate original native value");

    const ScriptResult notCallable = execute(context,
        "var a = []; a.push = 5; a.push(1);");
    expect(!notCallable.succeeded(), "shadowed method call: fails");
    expect(notCallable.runtimeError.code == RuntimeErrorCode::NotCallable,
        "shadowed method call: NotCallable");

    const ScriptResult invalidReceiver = execute(context,
        "var push = [].push; push(1);");
    expect(!invalidReceiver.succeeded(), "detached array method: fails");
    expect(invalidReceiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "detached array method: InvalidReceiver");

    RuntimeLimits elementLimit;
    elementLimit.maxArrayElements = 2;
    RuntimeContext limited(elementLimit);
    const ScriptResult atomic = execute(limited,
        "var a = [1]; a.push(2, 3);");
    expect(!atomic.succeeded(), "push element limit: fails");
    expect(atomic.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded,
        "push element limit: all-or-nothing error");
    const Value* limitedArray = binding(limited, "a");
    expect(limitedArray != nullptr && limitedArray->isObject(),
        "push element limit: array remains usable");
    if (limitedArray != nullptr && limitedArray->isObject()) {
        Value length;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        expect(limited.readPropertyForTesting(limitedArray->objectId(),
            "length", length, error) && length.isNumber() &&
            length.numberValue() == 1.0,
            "push element limit: no partial append");
    }
}

void testIntegrationAndPrototypeStructure()
{
    RuntimeContext context;
    const ScriptResult result = execute(context,
        "var values = [-3, 4, -8]; var sum = 0;"
        "for (var i = 0; i < values.length; i++) { sum += Math.abs(values[i]); }"
        "var result = sum;");
    expect(result.succeeded(), "Math/array integration: succeeds");
    expectNumber(context, "result", 15.0, "Math/array integration result");

    const Value* array = binding(context, "values");
    expect(array != nullptr && array->isObject(), "prototype structure: array exists");
    if (array != nullptr && array->isObject()) {
        const auto arrayPrototype = context.prototypeOf(array->objectId());
        expect(arrayPrototype == context.arrayPrototypeId(),
            "array -> Array.prototype");
        expect(context.prototypeOf(arrayPrototype) == context.objectPrototypeId(),
            "Array.prototype -> Object.prototype");
        expect(context.prototypeOf(context.objectPrototypeId()) ==
            kInvalidRuntimeObjectId, "Object.prototype -> null");
    }
    expect(context.objectCount() == 4, "built-in and script object accounting");
    expect(context.nativeFunctionCount() == 9,
        "native function accounting");
}

void testPrototypeDepthAndInitializationLimits()
{
    RuntimeLimits depth;
    depth.maxPrototypeDepth = 1;
    expectError(depth, "var a = []; var f = a.push;",
        RuntimeErrorCode::PrototypeChainExceeded, "prototype depth");

    RuntimeContext cycle;
    expect(execute(cycle, "var a = [];").succeeded(),
        "prototype cycle setup");
    const Value* cycleArray = binding(cycle, "a");
    expect(cycleArray != nullptr && cycleArray->isObject(),
        "prototype cycle array exists");
    if (cycleArray != nullptr && cycleArray->isObject()) {
        const RuntimeObjectId object = cycleArray->objectId();
        expect(cycle.setPrototypeForTesting(object, object),
            "prototype cycle hook");
        Value ignored;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        expect(!cycle.readPropertyForTesting(object, "missing", ignored, error),
            "prototype cycle stops");
        expect(error == RuntimeErrorCode::PrototypeChainExceeded,
            "prototype cycle reports bounded error");
    }

    RuntimeLimits nativeFunctions;
    nativeFunctions.maxNativeFunctions = 8;
    expectError(nativeFunctions, "var x = 1;",
        RuntimeErrorCode::BuiltInInitializationFailed,
        "native initialization limit");

    RuntimeLimits objects;
    objects.maxObjects = 2;
    expectError(objects, "var x = 1;",
        RuntimeErrorCode::BuiltInInitializationFailed,
        "object initialization limit");

    RuntimeLimits properties;
    properties.maxTotalProperties = 8;
    expectError(properties, "var x = 1;",
        RuntimeErrorCode::BuiltInInitializationFailed,
        "property initialization limit");
}

void testResetLifetime()
{
    RuntimeContext context;
    const ScriptResult first = execute(context,
        "var a = []; a.push(1); var first = a[0];");
    expect(first.succeeded(), "reset lifetime: first script");
    expectNumber(context, "first", 1.0, "reset lifetime: first value");
    expect(context.builtInsInitialized(), "reset lifetime: built-ins active");
    context.reset();
    expect(context.builtInsInitialized(), "reset lifetime: built-ins recreated");
    expect(context.objectPrototypeId() == 0 && context.arrayPrototypeId() == 1 &&
        context.mathObjectId() == 2, "reset lifetime: deterministic built-in IDs");
    expect(binding(context, "a") == nullptr, "reset lifetime: old binding gone");
    const ScriptResult second = execute(context,
        "var result = Math.abs(-4); var a = []; a.push(2);");
    expect(second.succeeded(), "reset lifetime: second script");
    expectNumber(context, "result", 4.0, "reset lifetime: new native call");
}

} // namespace

int main()
{
    testBuiltInsAndMath();
    testNativeInteroperabilityAndThis();
    testArrayPrototypeAndShadowing();
    testIntegrationAndPrototypeStructure();
    testPrototypeDepthAndInitializationLimits();
    testResetLifetime();

    if (failures != 0) {
        std::cerr << "Navigator JavaScript JS6 tests FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS6 tests PASS\n";
    return 0;
}
