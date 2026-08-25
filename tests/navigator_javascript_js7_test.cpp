#include "navigator_javascript/runtime.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using gxos::javascript::HostAdapter;
using gxos::javascript::HostInstanceId;
using gxos::javascript::HostObjectKind;
using gxos::javascript::HostObjectReference;
using gxos::javascript::HostResult;
using gxos::javascript::HostResultCode;
using gxos::javascript::HostValue;
using gxos::javascript::HostValueType;
using gxos::javascript::RuntimeContext;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeHostObjectId;
using gxos::javascript::RuntimeLimits;
using gxos::javascript::ScriptResult;
using gxos::javascript::SourceView;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

constexpr HostObjectKind kTestHostKind = 1u;
constexpr HostInstanceId kRoot = 1u;
constexpr HostInstanceId kChild = 2u;
constexpr std::uint32_t kIncrement = 1u;
constexpr std::uint32_t kAdd = 2u;
constexpr std::uint32_t kGetChild = 3u;
constexpr std::uint32_t kBadReturn = 4u;
constexpr std::uint32_t kInspect = 5u;

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
}

bool textEquals(SourceView text, const char* expected)
{
    const std::size_t length = std::string(expected).size();
    return text.data != nullptr && text.length == length &&
        std::string(text.data, text.length) == expected;
}

class TestHostAdapter final : public HostAdapter {
public:
    int counter = 1;
    int childValue = 1;
    std::string name = "guideXOS";

    HostResult validate(const HostObjectReference& object) override
    {
        if ((object.instanceId == kRoot || object.instanceId == kChild) &&
            object.generation != 0 && object.kind == kTestHostKind) {
            return HostResult();
        }
        return HostResult{HostResultCode::InvalidObject};
    }

    HostResult getProperty(const HostObjectReference& object,
        SourceView property, HostValue& result) override
    {
        if (object.instanceId == kRoot) {
            if (textEquals(property, "counter")) {
                result = HostValue::number(static_cast<double>(counter));
                return HostResult();
            }
            if (textEquals(property, "name")) {
                result = HostValue::string(SourceView(name.data(), name.size()));
                return HostResult();
            }
            if (textEquals(property, "readOnlyValue")) {
                result = HostValue::number(17.0);
                return HostResult();
            }
            if (textEquals(property, "child")) {
                result = HostValue::fromHostObject(
                    HostObjectReference{kChild, object.generation, kTestHostKind});
                return HostResult();
            }
            if (textEquals(property, "increment")) {
                result = HostValue::method(kIncrement, true);
                return HostResult();
            }
            if (textEquals(property, "add")) {
                result = HostValue::method(kAdd, false);
                return HostResult();
            }
            if (textEquals(property, "getChild")) {
                result = HostValue::method(kGetChild, true);
                return HostResult();
            }
            if (textEquals(property, "badReturn")) {
                result = HostValue::method(kBadReturn, false);
                return HostResult();
            }
            if (textEquals(property, "inspect")) {
                result = HostValue::method(kInspect, false);
                return HostResult();
            }
            return HostResult{HostResultCode::PropertyNotFound};
        }
        if (object.instanceId == kChild && textEquals(property, "value")) {
            result = HostValue::number(static_cast<double>(childValue));
            return HostResult();
        }
        return HostResult{HostResultCode::PropertyNotFound};
    }

    HostResult setProperty(const HostObjectReference& object,
        SourceView property, const HostValue& value) override
    {
        if (object.instanceId == kRoot && textEquals(property, "counter")) {
            if (value.type != HostValueType::Number) {
                return HostResult{HostResultCode::PropertyWriteFailed};
            }
            counter = static_cast<int>(value.numberValue);
            return HostResult();
        }
        if (object.instanceId == kRoot && textEquals(property, "name")) {
            if (value.type != HostValueType::String ||
                value.stringValue.data == nullptr) {
                return HostResult{HostResultCode::PropertyWriteFailed};
            }
            name.assign(value.stringValue.data, value.stringValue.length);
            return HostResult();
        }
        if (object.instanceId == kRoot && textEquals(property, "readOnlyValue")) {
            return HostResult{HostResultCode::PropertyReadOnly};
        }
        if (object.instanceId == kChild && textEquals(property, "value")) {
            if (value.type != HostValueType::Number) {
                return HostResult{HostResultCode::PropertyWriteFailed};
            }
            childValue = static_cast<int>(value.numberValue);
            return HostResult();
        }
        return HostResult{HostResultCode::PropertyWriteFailed};
    }

    HostResult call(const HostObjectReference* receiver,
        std::uint32_t methodId, const HostValue* arguments,
        std::size_t argumentCount, HostValue& result) override
    {
        if (methodId == kIncrement) {
            if (receiver == nullptr || receiver->instanceId != kRoot) {
                return HostResult{HostResultCode::InvalidObject};
            }
            ++counter;
            result = HostValue::number(static_cast<double>(counter));
            return HostResult();
        }
        if (methodId == kAdd) {
            if (receiver != nullptr || argumentCount != 2 || arguments == nullptr ||
                arguments[0].type != HostValueType::Number ||
                arguments[1].type != HostValueType::Number) {
                return HostResult{HostResultCode::CallFailed};
            }
            result = HostValue::number(arguments[0].numberValue +
                arguments[1].numberValue);
            return HostResult();
        }
        if (methodId == kGetChild) {
            if (receiver == nullptr || receiver->instanceId != kRoot) {
                return HostResult{HostResultCode::InvalidObject};
            }
            result = HostValue::fromHostObject(
                HostObjectReference{kChild, receiver->generation, kTestHostKind});
            return HostResult();
        }
        if (methodId == kBadReturn) {
            result = HostValue::fromHostObject(
                HostObjectReference{kChild, receiver == nullptr ? 0u
                    : receiver->generation + 1u, kTestHostKind});
            return HostResult();
        }
        if (methodId == kInspect) {
            if (receiver != nullptr || argumentCount != 1 || arguments == nullptr ||
                arguments[0].type != HostValueType::Object) {
                return HostResult{HostResultCode::CallFailed};
            }
            result = HostValue::number(5.0);
            return HostResult();
        }
        return HostResult{HostResultCode::CallFailed};
    }
};

ScriptResult execute(RuntimeContext& context, const std::string& source)
{
    return context.execute(SourceView(source.data(), source.size()));
}

const Value* binding(const RuntimeContext& context, const char* name)
{
    const std::string key(name);
    return context.lookup(SourceView(key.data(), key.size()));
}

void install(RuntimeContext& context, TestHostAdapter& adapter)
{
    context.setHostAdapter(&adapter);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(context.installHostGlobal("host", kRoot, kTestHostKind, error),
        "host global installs: " + std::string(
            gxos::javascript::runtimeErrorCodeName(error)));
}

void expectNumber(const RuntimeContext& context, const char* name,
    double expected, const std::string& label)
{
    const Value* value = binding(context, name);
    expect(value != nullptr, label + ": binding exists");
    if (value != nullptr) {
        expect(value->type() == ValueType::Number, label + ": Number");
        if (value->isNumber()) expect(value->numberValue() == expected,
            label + ": value");
    }
}

void expectRuntimeError(RuntimeContext& context, const std::string& source,
    RuntimeErrorCode expected, const std::string& label)
{
    const ScriptResult result = execute(context, source);
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error is " +
        gxos::javascript::runtimeErrorCodeName(result.runtimeError.code));
}

void testReadsWritesMethodsAndIdentity()
{
    TestHostAdapter adapter;
    RuntimeContext context;
    install(context, adapter);
    const ScriptResult result = execute(context,
        "host.counter = 5; host.increment();"
        "var result = host.counter; var name = host.name;"
        "var missing = host.doesNotExist; var truthy = !!host;"
        "var same = host === host; var a = host.child; var b = host.child;"
        "var childSame = a === b; var m1 = host.add; var m2 = host.add;"
        "var methodSame = m1 === m2; var sum = host.add(2, 7);"
        "var detachedSum = m1(4, 5); var inspect = host.inspect({x: 5});");
    expect(result.succeeded(), "host reads/writes/methods: succeeds");
    expectNumber(context, "result", 6.0, "host counter round trip");
    expect(adapter.counter == 6, "host backing counter mutated");
    expect(binding(context, "host") != nullptr &&
        binding(context, "host")->isHostObject(), "host global is HostObject");
    expect(binding(context, "name") != nullptr &&
        context.stringValue(*binding(context, "name")) == "guideXOS",
        "host returned string is runtime-owned");
    expect(binding(context, "missing") != nullptr &&
        binding(context, "missing")->isUndefined(), "missing host property is Undefined");
    expect(binding(context, "truthy") != nullptr &&
        binding(context, "truthy")->booleanValue(), "HostObject is truthy");
    expect(binding(context, "same") != nullptr &&
        binding(context, "same")->booleanValue(), "host identity is stable");
    expect(binding(context, "childSame") != nullptr &&
        binding(context, "childSame")->booleanValue(), "child identity is stable");
    expect(binding(context, "methodSame") != nullptr &&
        binding(context, "methodSame")->booleanValue(),
        "host method identity is stable");
    expectNumber(context, "sum", 9.0, "host receiver method result");
    expectNumber(context, "detachedSum", 9.0,
        "receiver-independent host method result");
    expectNumber(context, "inspect", 5.0, "JS object passed by runtime ID");
}

void testReceiverAndReadOnlyErrors()
{
    TestHostAdapter adapter;
    RuntimeContext context;
    install(context, adapter);
    expectRuntimeError(context, "host.readOnlyValue = 10;",
        RuntimeErrorCode::HostPropertyReadOnly, "read-only host property");
    expectRuntimeError(context, "var f = host.increment; f();",
        RuntimeErrorCode::InvalidReceiver, "detached receiver failure");
}

void testChildFunctionsAndClosures()
{
    TestHostAdapter adapter;
    RuntimeContext context;
    install(context, adapter);
    const ScriptResult result = execute(context,
        "var child = host.getChild(); child.value = 12;"
        "var alias = host.child; var childResult = alias.value;"
        "function update(h) { h.counter += 2; }"
        "host.counter = 3; update(host); var functionResult = host.counter;"
        "function capture(h) { function read() { return h.counter; } return read; }"
        "host.counter = 8; var reader = capture(host); var closureResult = reader();"
        "var obj = { counter: 1 }; obj.counter++; var ordinary = obj.counter;");
    expect(result.succeeded(), "host through child/function/closure: succeeds");
    expectNumber(context, "childResult", 12.0, "child host mutation");
    expect(adapter.childValue == 12, "child backing state mutated");
    expectNumber(context, "functionResult", 5.0, "host through function");
    expectNumber(context, "closureResult", 8.0, "host through closure");
    expectNumber(context, "ordinary", 2.0, "ordinary object remains independent");
}

void testGenerationInvalidationAndReuse()
{
    TestHostAdapter adapter;
    RuntimeContext context;
    install(context, adapter);
    const ScriptResult setup = execute(context,
        "var oldChild = host.child;"
        "function capture(h) { function read() { return h.value; } return read; }"
        "var reader = capture(oldChild);");
    expect(setup.succeeded(), "stale setup succeeds");
    const Value oldChild = *binding(context, "oldChild");
    const Value reader = *binding(context, "reader");
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(context.invalidateHostGeneration(error), "generation invalidates");
    RuntimeHostObjectId newChild = gxos::javascript::kInvalidRuntimeHostObjectId;
    expect(context.registerHostObject(kChild, kTestHostKind, newChild, error),
        "new generation child registers");
    expect(newChild != oldChild.hostObjectId(), "generation changes host identity");
    Value ignored;
    expect(!context.readHostPropertyForTesting(oldChild.hostObjectId(), "value",
        ignored, error) && error == RuntimeErrorCode::StaleHostObject,
        "old host handle is stale after slot reuse");
    Value newValue;
    expect(context.readHostPropertyForTesting(newChild, "value", newValue, error) &&
        newValue.isNumber() && newValue.numberValue() == adapter.childValue,
        "new generation sees current child only");
    Value closureValue;
    std::vector<Value> noArguments;
    expect(!context.invokeFunctionForTesting(reader, noArguments, closureValue,
        error) && error == RuntimeErrorCode::StaleHostObject,
        "closure retaining stale host fails safely");
}

void testLimitsResetAndIsolation()
{
    RuntimeLimits hostLimit;
    hostLimit.maxHostObjects = 1;
    TestHostAdapter limitedAdapter;
    RuntimeContext limited(hostLimit);
    limited.setHostAdapter(&limitedAdapter);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    RuntimeHostObjectId ignored = gxos::javascript::kInvalidRuntimeHostObjectId;
    expect(limited.registerHostObject(kRoot, kTestHostKind, ignored, error),
        "host limit: first object registers");
    expect(!limited.registerHostObject(kChild, kTestHostKind, ignored, error) &&
        error == RuntimeErrorCode::HostObjectLimitExceeded,
        "host limit: second object rejected");

    RuntimeLimits budget;
    budget.maxHostOperations = 2;
    TestHostAdapter budgetAdapter;
    RuntimeContext budgetContext(budget);
    install(budgetContext, budgetAdapter);
    expectRuntimeError(budgetContext,
        "var x = host.counter; x = host.counter;",
        RuntimeErrorCode::HostOperationBudgetExceeded, "host operation budget");

    TestHostAdapter adapter;
    RuntimeContext context;
    install(context, adapter);
    expect(execute(context,
        "var child = host.child;"
        "function capture(h) { function read() { return h.value; } return read; }"
        "var reader = capture(child);").succeeded(), "reset setup succeeds");
    const Value oldChild = *binding(context, "child");
    const Value oldReader = *binding(context, "reader");
    context.reset();
    expect(binding(context, "host") == nullptr && context.hostObjectCount() == 0,
        "reset clears host registry and globals");
    Value staleValue;
    expect(!context.readHostPropertyForTesting(oldChild.hostObjectId(), "value",
        staleValue, error) && error == RuntimeErrorCode::StaleHostObject,
        "reset invalidates retained host value");
    Value staleClosureValue;
    std::vector<Value> noArguments;
    expect(!context.invokeFunctionForTesting(oldReader, noArguments,
        staleClosureValue, error) && error == RuntimeErrorCode::InvalidFunction,
        "reset clears retained closure/function state");
    install(context, adapter);
    expect(execute(context, "var result = host.counter;").succeeded(),
        "new host installs after reset");
    expectNumber(context, "result", 1.0, "new host state after reset");

    TestHostAdapter adapterA;
    TestHostAdapter adapterB;
    RuntimeContext contextA;
    RuntimeContext contextB;
    install(contextA, adapterA);
    install(contextB, adapterB);
    adapterA.counter = 1;
    adapterB.counter = 9;
    expect(execute(contextA, "var result = host.counter;").succeeded(),
        "context A host succeeds");
    expect(execute(contextB, "var result = host.counter;").succeeded(),
        "context B host succeeds");
    expectNumber(contextA, "result", 1.0, "context A isolation");
    expectNumber(contextB, "result", 9.0, "context B isolation");
}

void testInvalidHostReturn()
{
    TestHostAdapter adapter;
    RuntimeContext context;
    install(context, adapter);
    expectRuntimeError(context, "var bad = host.badReturn();",
        RuntimeErrorCode::InvalidHostReturn, "invalid adapter host return");
}

} // namespace

int main()
{
    testReadsWritesMethodsAndIdentity();
    testReceiverAndReadOnlyErrors();
    testChildFunctionsAndClosures();
    testGenerationInvalidationAndReuse();
    testLimitsResetAndIsolation();
    testInvalidHostReturn();

    if (failures != 0) {
        std::cerr << "Navigator JavaScript JS7 tests FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS7 tests PASS\n";
    return 0;
}
