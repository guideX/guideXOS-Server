#include "navigator_javascript/execution_core.h"
#include "navigator_javascript/frontend_storage.h"
#include "navigator_javascript/lexer.h"
#include "navigator_javascript/parser.h"
#include "navigator_javascript/runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace gxos {
namespace javascript {

struct NavigatorScriptRuntimeTestAccess {
    static bool functionReferenceIsLive(const RuntimeContext& context,
        Value function)
    {
        return function.isFunction() &&
            context.functionAt(function.functionId()) != nullptr;
    }
};

} // namespace javascript
} // namespace gxos

namespace {

using namespace gxos::javascript;

int checks = 0;
int failures = 0;

void expect(bool condition, const char* label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
}

SourceView view(const char* text)
{
    std::size_t length = 0u;
    while (text[length] != '\0') ++length;
    return SourceView(text, length);
}

bool sameNumber(Value value, double number)
{
    return value.isNumber() && value.numberValue() == number;
}

class PortableFixture {
public:
    explicit PortableFixture(std::string program,
        std::size_t environmentCapacity = 256u)
        : source_(std::move(program)), tokens_(4096u), nodes_(4096u),
          children_(8192u), parserScratch_(65536u),
          ast_(SourceView(source_.data(), source_.size()),
              AstStorageView{nodes_.data(), nodes_.size(), children_.data(),
                  children_.size()}),
          environments_( ), environmentRecords_(environmentCapacity), bindings_(1024u),
          bindingNames_(65536u), stringRecords_(128u), stringBytes_(16384u),
          functions_(), functionsStorage_(128u), objects_(),
          objectRecords_(128u), properties_(512u), arrayElements_(512u),
          propertyNames_(16384u), frames_(64u), arguments_(512u),
          executionFrames_(kDefaultPortableExecutionFrameCapacity),
          core_(ast_, strings_, environments_,
              functions_, objects_,
              PortableExecutionStorageView{frames_.data(), frames_.size(),
                  arguments_.data(), arguments_.size(),
                  executionFrames_.data(), executionFrames_.size()},
              limits_, steps_)
    {
    }

    bool initialize()
    {
        TokenBuffer tokenBuffer{tokens_.data(), tokens_.size()};
        Lexer lexer;
        const LexerResult lexed = lexer.tokenizeInto(
            SourceView(source_.data(), source_.size()), tokenBuffer);
        if (!lexed.succeeded()) return false;
        FrontendArena scratch(parserScratch_.data(), parserScratch_.size());
        Parser parser;
        ParserError parserError;
        if (!parser.parseInto(makeTokenView(
                SourceView(source_.data(), source_.size()), tokenBuffer),
                ast_, scratch, parserError)) return false;

        EnvironmentError environmentError;
        if (!environments_.configure(EnvironmentStorageView{
                environmentRecords_.data(), environmentRecords_.size(),
                bindings_.data(), bindings_.size(), bindingNames_.data(),
                bindingNames_.size()}, 1u, environmentError) ||
            !environments_.createRoot(EnvironmentLimits(), root_,
                environmentError)) return false;
        RuntimeStringError stringError;
        if (!strings_.configure(RuntimeStringStorageView{
                stringRecords_.data(), stringRecords_.size(),
                stringBytes_.data(), stringBytes_.size()}, 1u, stringError))
            return false;
        FunctionStoreError functionError;
        if (!functions_.configure(FunctionStorageView{functionsStorage_.data(),
                functionsStorage_.size()}, 1u, 0u, functionError)) return false;
        PortableObjectLimits objectLimits;
        objectLimits.maxObjects = objectRecords_.size();
        objectLimits.maxProperties = properties_.size();
        objectLimits.maxPropertiesPerObject = 128u;
        objectLimits.maxPropertyNameLength = 256u;
        objectLimits.maxPropertyNameBytes = propertyNames_.size();
        objectLimits.maxArrayElements = 128u;
        objectLimits.maxTotalArrayElements = arrayElements_.size();
        objectLimits.maxArrayIndex = 127u;
        objectLimits.maxArrayLength = 128u;
        objectLimits.maxPrototypeDepth = 16u;
        PortableObjectError objectError;
        if (!objects_.configure(PortableObjectStorageView{
                objectRecords_.data(), objectRecords_.size(), properties_.data(),
                properties_.size(), arrayElements_.data(),
                arrayElements_.size(), propertyNames_.data(),
                propertyNames_.size()}, objectLimits, 1u, objectError))
            return false;
        return true;
    }

    bool run()
    {
        Value finalValue;
        return core_.run(ast_.root(), root_, finalValue, runResult_);
    }

    bool global(const char* name, Value& value) const
    {
        EnvironmentError error;
        return environments_.readChain(root_, view(name), value, error);
    }

    bool call(const char* name, Value receiver, const Value* arguments,
        std::size_t argumentCount, PortableExecutionResult& result)
    {
        Value function;
        return global(name, function) && core_.invokeCallable(function,
            receiver, arguments, argumentCount, result);
    }

    std::string source_;
    std::vector<Token> tokens_;
    std::vector<AstNode> nodes_;
    std::vector<AstNodeId> children_;
    std::vector<unsigned char> parserScratch_;
    Ast ast_;
    EnvironmentStore environments_;
    std::vector<EnvironmentRecord> environmentRecords_;
    std::vector<Binding> bindings_;
    std::vector<char> bindingNames_;
    RuntimeStringStore strings_;
    std::vector<RuntimeStringRecord> stringRecords_;
    std::vector<char> stringBytes_;
    FunctionStore functions_;
    std::vector<FunctionRecord> functionsStorage_;
    PortableObjectStore objects_;
    std::vector<PortableObjectRecord> objectRecords_;
    std::vector<PortablePropertyRecord> properties_;
    std::vector<PortableArrayElementRecord> arrayElements_;
    std::vector<char> propertyNames_;
    PortableExecutionLimits limits_;
    std::vector<PortableCallFrame> frames_;
    std::vector<Value> arguments_;
    std::vector<PortableExecutionFrame> executionFrames_;
    std::size_t steps_ = 0u;
    PortableObjectExecutionCore core_;
    EnvironmentId root_ = kInvalidEnvironmentId;
    PortableExecutionResult runResult_;
};

std::string callbackProgram()
{
    std::string source =
        "let holder={};let box={};let array=[];"
        "function identity(value){return value;}"
        "function second(first,secondValue){return secondValue;}"
        "function owner(){return this;}"
        "function makeArrow(){return ()=>this;}"
        "function makeClosure(){let captured=17;return function(){return captured;};}"
        "function returnNull(){return null;}"
        "function returnBoolean(){return true;}"
        "function returnNumber(){return 23;}"
        "function returnString(){return 'portable';}"
        "function returnObject(){return holder;}"
        "function returnArray(){return array;}"
        "function returnFunction(){return identity;}"
        "function returnUndefined(){}"
        "function mutate(){box.beta=80;box.gamma=3;}"
        "function failAfterMutation(){box.keep=99;missingName;}"
        "function failImmutable(){const locked=1;locked=2;}"
        "function recurse(){return recurse();}"
        "function nested(value){return identity(value);}"
        "function maxArgs(";
    for (std::size_t index = 0u; index < 64u; ++index) {
        if (index != 0u) source.push_back(',');
        source += "a" + std::to_string(index);
    }
    source += "){return a63;}";
    return source;
}

void testCallableAndInvocation()
{
    PortableFixture fixture(callbackProgram());
    const bool initialized = fixture.initialize();
    expect(initialized, "portable callback fixture parses and configures");
    if (!initialized) return;
    FunctionRecord nativeRecord;
    nativeRecord.kind = FunctionRecord::Kind::Native;
    FunctionStoreError functionError;
    RuntimeFunctionId nativeFunction = kInvalidRuntimeFunctionId;
    expect(fixture.functions_.append(nativeRecord, nativeFunction,
            functionError), "native callable record is accepted by shared store");
    FunctionRecord hostRecord;
    hostRecord.kind = FunctionRecord::Kind::Host;
    RuntimeFunctionId hostFunction = kInvalidRuntimeFunctionId;
    expect(fixture.functions_.append(hostRecord, hostFunction, functionError),
        "host callable record is recognized without a fake implementation");
    expect(fixture.run(), "portable callback declarations execute");

    Value identity;
    Value holder;
    Value array;
    expect(fixture.global("identity", identity) && identity.isFunction(),
        "ordinary user function is retained as a callable Value");
    expect(fixture.global("holder", holder) && holder.isObject(),
        "holder object has stable shared-store identity");
    expect(fixture.global("array", array) && array.isObject(),
        "Array value is a shared object reference");

    PortableExecutionError callableError;
    bool callable = false;
    PortableExecutionResult result;
    expect(fixture.core_.isCallable(identity, callable, callableError) &&
        callable && callableError.code == PortableExecutionErrorCode::None,
        "IsCallable accepts an ordinary function");
    expect(fixture.core_.isCallable(Value::undefined(), callable,
            callableError) && !callable,
        "IsCallable rejects undefined without invoking it");
    expect(fixture.core_.isCallable(holder, callable, callableError) &&
        !callable, "IsCallable rejects noncallable objects");
    expect(!fixture.core_.invokeCallable(holder, Value::undefined(), nullptr,
            0u, result) && result.error.code == PortableExecutionErrorCode::TypeError,
        "invoking a noncallable object returns a TypeError completion");
    const Value invalidFunction = Value::function(makeRuntimeHandle(1u, 500u));
    expect(!fixture.core_.isCallable(invalidFunction, callable, callableError) &&
        !callable && callableError.code ==
            PortableExecutionErrorCode::InvalidFunctionReference,
        "invalid same-generation function handle is rejected");
    expect(fixture.core_.isCallable(Value::function(nativeFunction), callable,
            callableError) && callable,
        "IsCallable recognizes a real native-function record");
    expect(!fixture.core_.invokeCallable(Value::function(nativeFunction),
            holder, nullptr, 0u, result) &&
        result.error.code == PortableExecutionErrorCode::UnsupportedOperation,
        "native callback dispatch returns explicit unsupported capability");
    expect(fixture.core_.isCallable(Value::function(hostFunction), callable,
            callableError) && callable,
        "IsCallable recognizes a host-function record");
    expect(!fixture.core_.invokeCallable(Value::function(hostFunction),
            holder, nullptr, 0u, result) &&
        result.error.code == PortableExecutionErrorCode::UnsupportedOperation,
        "host callback dispatch is explicit and does not fabricate success");

    Value objectReceiver;
    PortableObjectError objectError;
    RuntimeObjectId receiverId = kInvalidRuntimeObjectId;
    expect(fixture.objects_.createObject(kInvalidRuntimeObjectId, receiverId,
            objectError), "ordinary receiver object is allocated");
    objectReceiver = Value::object(receiverId);
    expect(fixture.objects_.setOwnProperty(receiverId, view("method"),
            identity, objectError), "function is stored as an own property");
    Value method;
    bool found = false;
    expect(fixture.objects_.getProperty(receiverId, view("method"), method,
            found, objectError) && found && method.isFunction(),
        "own callable property lookup returns its actual Value");
    Value argument = Value::number(41.0);
    expect(fixture.core_.invokeCallable(method, objectReceiver, &argument, 1u,
            result) && sameNumber(result.finalValue, 41.0),
        "one argument is passed and returned in order");
    expect(fixture.core_.invokeCallable(identity, Value::undefined(), nullptr,
            0u, result) && result.finalValue.isUndefined(),
        "zero-argument invocation binds omitted parameters as undefined");
    std::array<Value, 2u> twoArguments{{Value::number(3.0), Value::number(9.0)}};
    Value second;
    expect(fixture.global("second", second) &&
        fixture.core_.invokeCallable(second, Value::undefined(),
            twoArguments.data(), twoArguments.size(), result) &&
        sameNumber(result.finalValue, 9.0),
        "two arguments retain their source order");
    for (Value reference : {holder, array, identity}) {
        expect(fixture.core_.invokeCallable(identity, Value::undefined(),
                &reference, 1u, result) && result.finalValue.type() ==
                    reference.type(),
            "object, Array, and function argument Values pass by identity");
    }

    Value owner;
    expect(fixture.global("owner", owner) && owner.isFunction(),
        "ordinary receiver callback is retained");
    Value receiverFunction;
    fixture.objects_.setOwnProperty(receiverId, view("callback"), owner,
        objectError);
    expect(fixture.objects_.getProperty(receiverId, view("callback"),
            receiverFunction, found, objectError) && found &&
        fixture.core_.invokeCallable(receiverFunction, objectReceiver,
            nullptr, 0u, result) && result.finalValue.isObject() &&
        result.finalValue.objectId() == receiverId,
        "property-derived invocation preserves the explicit receiver identity");

    RuntimeObjectId prototypeId = kInvalidRuntimeObjectId;
    RuntimeObjectId inheritedHolderId = kInvalidRuntimeObjectId;
    expect(fixture.objects_.createObject(kInvalidRuntimeObjectId, prototypeId,
            objectError) &&
        fixture.objects_.setOwnProperty(prototypeId, view("callback"), owner,
            objectError) &&
        fixture.objects_.createObject(prototypeId, inheritedHolderId,
            objectError), "inherited callable property is constructed");
    Value inherited;
    expect(fixture.objects_.getProperty(inheritedHolderId, view("callback"),
            inherited, found, objectError) && found && inherited.isFunction() &&
        fixture.core_.invokeCallable(inherited, Value::object(inheritedHolderId),
            nullptr, 0u, result) && result.finalValue.isObject() &&
        result.finalValue.objectId() == inheritedHolderId,
        "inherited callable invocation uses the child holder as receiver");

    Value arrowFactory;
    expect(fixture.global("makeArrow", arrowFactory),
        "arrow factory is available");
    expect(fixture.core_.invokeCallable(arrowFactory, objectReceiver,
            nullptr, 0u, result) && result.finalValue.isFunction(),
        "arrow function retains its creating closure");
    const Value arrow = result.finalValue;
    expect(fixture.core_.isCallable(arrow, callable, callableError) && callable,
        "IsCallable accepts an arrow function");
    expect(fixture.core_.invokeCallable(arrow, Value::object(inheritedHolderId),
            nullptr, 0u, result) && result.finalValue.isObject() &&
        result.finalValue.objectId() == receiverId,
        "arrow lexical this ignores the supplied invocation receiver");

    Value closureFactory;
    expect(fixture.global("makeClosure", closureFactory) &&
        fixture.core_.invokeCallable(closureFactory, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.isFunction(),
        "returned closure keeps its environment after factory return");
    const Value closure = result.finalValue;
    expect(fixture.core_.invokeCallable(closure, Value::undefined(), nullptr,
            0u, result) && sameNumber(result.finalValue, 17.0),
        "captured binding survives the invocation frame lifetime");
    expect(fixture.core_.invokeCallable(closure, Value::undefined(), nullptr,
            0u, result) && sameNumber(result.finalValue, 17.0),
        "same callable can be invoked repeatedly in one realm");

    Value identityTwo;
    expect(fixture.global("nested", identityTwo) &&
        fixture.core_.invokeCallable(identityTwo, Value::undefined(),
            twoArguments.data(), twoArguments.size(), result) &&
        result.finalValue.isNumber() && result.callDepthHighWater >= 2u,
        "nested call path shares the call-depth and execution budgets");

    Value undefinedReturn;
    expect(fixture.global("returnUndefined", undefinedReturn) &&
        fixture.core_.invokeCallable(undefinedReturn, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.isUndefined(),
        "undefined is an ordinary successful callback return value");
    bool deleted = true;
    fixture.objects_.setOwnProperty(receiverId, view("kept"),
        result.finalValue, objectError);
    Value keptValue;
    expect(fixture.objects_.getOwnProperty(receiverId, view("kept"),
            keptValue, found, objectError) && found && keptValue.isUndefined(),
        "undefined callback result does not implicitly delete a property");
    expect(fixture.objects_.deleteOwnProperty(receiverId, view("kept"),
            deleted, objectError) && deleted,
        "property deletion is an explicit separate ObjectStore operation");

    const char* returnNames[] = {"returnNull", "returnBoolean", "returnNumber",
        "returnString", "returnObject", "returnArray", "returnFunction"};
    for (const char* name : returnNames) {
        Value function;
        const bool invoked = fixture.global(name, function) &&
            fixture.core_.invokeCallable(function, Value::undefined(), nullptr,
                0u, result);
        expect(invoked && result.completed,
            "callback return category completes through shared invocation");
    }
    Value returnNull;
    Value returnBoolean;
    Value returnNumber;
    Value returnObject;
    Value returnArray;
    Value returnFunction;
    fixture.global("returnNull", returnNull);
    fixture.global("returnBoolean", returnBoolean);
    fixture.global("returnNumber", returnNumber);
    fixture.global("returnObject", returnObject);
    fixture.global("returnArray", returnArray);
    fixture.global("returnFunction", returnFunction);
    bool returnedArrayIsArray = false;
    expect(fixture.core_.invokeCallable(returnNull, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.isNull(),
        "callback null return remains null");
    expect(fixture.core_.invokeCallable(returnBoolean, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.isBoolean() &&
        result.finalValue.booleanValue(), "callback Boolean return is preserved");
    expect(fixture.core_.invokeCallable(returnNumber, Value::undefined(),
            nullptr, 0u, result) && sameNumber(result.finalValue, 23.0),
        "callback Number return is preserved");
    expect(fixture.core_.invokeCallable(returnObject, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.objectId() ==
                holder.objectId(), "callback object return preserves identity");
    expect(fixture.core_.invokeCallable(returnArray, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.objectId() ==
                array.objectId() && fixture.objects_.isArray(
                    result.finalValue.objectId(), returnedArrayIsArray,
                    objectError) && returnedArrayIsArray,
        "callback Array return preserves identity and kind");
    expect(fixture.core_.invokeCallable(returnFunction, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.isFunction() &&
        result.finalValue.functionId() == identity.functionId(),
        "callback function return preserves function identity");
    Value stringReturn;
    fixture.global("returnString", stringReturn);
    expect(fixture.core_.invokeCallable(stringReturn, Value::undefined(),
            nullptr, 0u, result) && result.finalValue.isString(),
        "callback returns a shared RuntimeStringStore reference");
    SourceView returnedText;
    RuntimeStringError returnedTextError;
    expect(result.finalValue.isString() && fixture.strings_.view(
            result.finalValue.stringId(), returnedText, returnedTextError) &&
        std::string(returnedText.data, returnedText.length) == "portable",
        "callback string return keeps its decoded text");
    Value stringArgument;
    RuntimeStringError stringError;
    RuntimeStringId stringId = kInvalidRuntimeStringId;
    expect(fixture.strings_.create(view("argument"), 64u, stringId,
            stringError), "string argument is placed in the shared string store");
    stringArgument = Value::string(stringId);
    expect(fixture.core_.invokeCallable(identity, Value::undefined(),
            &stringArgument, 1u, result) &&
        result.finalValue.stringId() == stringId,
        "string argument identity is preserved");

    Value maxArgsFunction;
    expect(fixture.global("maxArgs", maxArgsFunction),
        "maximum-argument function is retained");
    std::array<Value, 64u> maxArguments;
    for (std::size_t index = 0u; index < maxArguments.size(); ++index)
        maxArguments[index] = Value::number(static_cast<double>(index));
    expect(fixture.core_.invokeCallable(maxArgsFunction, Value::undefined(),
            maxArguments.data(), maxArguments.size(), result) &&
        sameNumber(result.finalValue, 63.0) &&
        result.argumentValuesHighWater == maxArguments.size(),
        "maximum supported argument span preserves order");
    expect(!fixture.core_.invokeCallable(maxArgsFunction, Value::undefined(),
            maxArguments.data(), maxArguments.size() + 1u, result) &&
        result.error.code == PortableExecutionErrorCode::InvalidInvocationTarget,
        "one argument beyond the configured limit fails before invocation");
    expect(!fixture.core_.invokeCallable(identity, Value::undefined(), nullptr,
            1u, result) &&
        result.error.code == PortableExecutionErrorCode::InternalInvariantFailure,
        "nonempty null argument span is rejected explicitly");
}

void testErrorsAndSideEffects()
{
    PortableFixture fixture(callbackProgram());
    const bool initialized = fixture.initialize();
    expect(initialized && fixture.run(),
        "error and side-effect fixture initializes");
    if (!initialized) return;
    if (!fixture.runResult_.completed) return;
    Value failFunction;
    Value box;
    expect(fixture.global("failAfterMutation", failFunction) &&
        fixture.global("box", box), "failing callback and target are retained");
    PortableExecutionResult result;
    expect(!fixture.core_.invokeCallable(failFunction, Value::undefined(),
            nullptr, 0u, result) &&
        result.error.code == PortableExecutionErrorCode::MissingBinding,
        "ReferenceError-class missing binding propagates as an explicit error");
    Value stored;
    bool found = false;
    PortableObjectError objectError;
    expect(fixture.objects_.getOwnProperty(box.objectId(), view("keep"),
            stored, found, objectError) && found && sameNumber(stored, 99.0),
        "completed property mutation remains after a later callback error");
    Value immutableFailure;
    expect(fixture.global("failImmutable", immutableFailure) &&
        !fixture.core_.invokeCallable(immutableFailure, Value::undefined(),
            nullptr, 0u, result) && result.error.code ==
                PortableExecutionErrorCode::ImmutableAssignment,
        "immutable binding failure is propagated without an undefined success");
    Value identity;
    expect(fixture.global("identity", identity) &&
        fixture.core_.invokeCallable(identity, Value::undefined(), nullptr,
            0u, result) && result.completed,
        "runtime recovers for a later independent callback after an error");

    Value recurse;
    expect(fixture.global("recurse", recurse),
        "recursive function is retained");
    expect(!fixture.core_.invokeCallable(recurse, Value::undefined(), nullptr,
            0u, result) &&
        result.error.code == PortableExecutionErrorCode::CallDepthExhausted &&
        result.callDepthHighWater == fixture.limits_.maxCallDepth,
        "recursive invocation stops at the shared call-depth limit");
    expect(fixture.core_.invokeCallable(identity, Value::undefined(), nullptr,
            0u, result) && result.completed,
        "call frames unwind safely after depth exhaustion");

    fixture.steps_ = fixture.limits_.maxExecutionSteps;
    expect(!fixture.core_.invokeCallable(identity, Value::undefined(), nullptr,
            0u, result) &&
        result.error.code == PortableExecutionErrorCode::StepBudgetExhausted,
        "callback execution observes the shared step budget");

    PortableFixture lowEnvironment("function one(){return 1;}", 1u);
    const bool lowReady = lowEnvironment.initialize() && lowEnvironment.run();
    Value one;
    expect(lowReady && lowEnvironment.global("one", one) &&
        !lowEnvironment.core_.invokeCallable(one, Value::undefined(), nullptr,
            0u, result) && result.error.code ==
                PortableExecutionErrorCode::EnvironmentCapacityExhausted,
        "environment exhaustion is reported through shared invocation");
}

void testPropertyOperationsAndSnapshots()
{
    PortableFixture fixture(callbackProgram());
    const bool initialized = fixture.initialize();
    expect(initialized && fixture.run(),
        "property-operation fixture initializes");
    if (!initialized) return;
    if (!fixture.runResult_.completed) return;
    PortableObjectError error;
    RuntimeObjectId prototype = kInvalidRuntimeObjectId;
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    expect(fixture.objects_.createObject(kInvalidRuntimeObjectId, prototype,
            error) && fixture.objects_.createObject(prototype, object, error),
        "prototype and child object use the shared store");
    expect(fixture.objects_.setOwnProperty(prototype, view("inherited"),
            Value::number(5.0), error) &&
        fixture.objects_.setOwnProperty(prototype, view("inheritedUndefined"),
            Value::undefined(), error) &&
        fixture.objects_.setOwnProperty(object, view("ownUndefined"),
            Value::undefined(), error) &&
        fixture.objects_.setOwnProperty(object, view("own"),
            Value::number(7.0), error),
        "own and inherited data properties are stored");
    Value value;
    bool found = false;
    expect(fixture.objects_.getOwnProperty(object, view("own"), value,
            found, error) && found && sameNumber(value, 7.0),
        "own lookup returns the own value");
    expect(fixture.objects_.getProperty(object, view("inherited"), value,
            found, error) && found && sameNumber(value, 5.0),
        "prototype lookup returns an inherited value");
    expect(fixture.objects_.getProperty(object, view("ownUndefined"), value,
            found, error) && found && value.isUndefined(),
        "own undefined remains distinct from a missing property");
    expect(fixture.objects_.getProperty(object, view("inheritedUndefined"),
            value, found, error) && found && value.isUndefined(),
        "inherited undefined remains distinct from a missing property");
    expect(fixture.objects_.getProperty(object, view("missing"), value,
            found, error) && !found && value.isUndefined(),
        "missing property returns undefined with found false");
    expect(!fixture.objects_.getProperty(makeRuntimeHandle(1u, 200u),
            view("missing"), value, found, error) &&
        error.code == PortableObjectErrorCode::InvalidObject,
        "invalid object handle returns an explicit lookup error");
    const RuntimeObjectId staleObject = makeRuntimeHandle(2u,
        runtimeHandleSlot(object));
    expect(!fixture.objects_.getProperty(staleObject, view("own"), value,
            found, error) &&
        error.code == PortableObjectErrorCode::StaleGeneration,
        "stale generation is distinguished during lookup");
    expect(!fixture.objects_.setPrototype(object, 0xffff0001u, error) &&
        error.code == PortableObjectErrorCode::InvalidPrototype,
        "invalid prototype reference is rejected before mutation");
    expect(!fixture.objects_.setPrototype(prototype, object, error) &&
        error.code == PortableObjectErrorCode::PrototypeCycle,
        "prototype cycle is rejected");

    RuntimeObjectId ordered = kInvalidRuntimeObjectId;
    expect(fixture.objects_.createObject(kInvalidRuntimeObjectId, ordered,
            error), "ordered object is allocated");
    fixture.objects_.setOwnProperty(ordered, view("b"), Value::number(1.0), error);
    fixture.objects_.setOwnProperty(ordered, view("10"), Value::number(10.0), error);
    fixture.objects_.setOwnProperty(ordered, view("2"), Value::number(2.0), error);
    fixture.objects_.setOwnProperty(ordered, view("a"), Value::number(4.0), error);
    fixture.objects_.setOwnProperty(ordered, view("hidden"), Value::number(9.0),
        error, 0x05u);
    fixture.objects_.setOwnProperty(ordered, WellKnownSymbol::Iterator,
        Value::number(11.0), error);
    std::array<PortablePropertyKeySnapshotEntry, 8u> keys{};
    std::array<char, 128u> keyBytes{};
    std::size_t keyCount = 0u;
    std::size_t keyBytesUsed = 0u;
    expect(fixture.objects_.snapshotOwnEnumerableStringProperties(ordered,
            keys.data(), keys.size(), keyBytes.data(), keyBytes.size(),
            keyCount, keyBytesUsed, error) && keyCount == 4u,
        "own-key snapshot filters symbols and non-enumerable properties");
    auto keyAt = [&](std::size_t index) {
        const auto& key = keys[index];
        return std::string(keyBytes.data() + key.byteOffset, key.byteLength);
    };
    expect(keyAt(0u) == "2" && keyAt(1u) == "10" && keyAt(2u) == "b" &&
        keyAt(3u) == "a",
        "snapshot uses numeric ordering then string insertion order");

    Value box;
    fixture.global("box", box);
    fixture.objects_.setOwnProperty(box.objectId(), view("alpha"),
        Value::number(1.0), error);
    fixture.objects_.setOwnProperty(box.objectId(), view("beta"),
        Value::number(2.0), error);
    std::array<PortablePropertyKeySnapshotEntry, 4u> boxKeys{};
    std::array<char, 64u> boxKeyBytes{};
    expect(fixture.objects_.snapshotOwnEnumerableStringProperties(box.objectId(),
            boxKeys.data(), boxKeys.size(), boxKeyBytes.data(),
            boxKeyBytes.size(), keyCount, keyBytesUsed, error) && keyCount == 2u,
        "live-read snapshot captures the original key set");
    Value mutate;
    fixture.global("mutate", mutate);
    PortableExecutionResult callbackResult;
    expect(fixture.core_.invokeCallable(mutate, Value::undefined(), nullptr,
            0u, callbackResult),
        "callback updates a later property and adds a new property");
    Value alpha;
    Value beta;
    bool boxDeleted = false;
    fixture.objects_.deleteOwnProperty(box.objectId(), view("alpha"),
        boxDeleted, error);
    fixture.objects_.setOwnProperty(box.objectId(), view("newKey"),
        Value::number(30.0), error);
    expect(fixture.objects_.getProperty(box.objectId(), view("alpha"), alpha,
            found, error) && !found,
        "live read distinguishes a property removed after the snapshot");
    expect(fixture.objects_.getProperty(box.objectId(), view("beta"), beta,
            found, error) && found && sameNumber(beta, 80.0),
        "live read observes a value changed after the snapshot");
    expect(keyCount == 2u &&
        std::string(boxKeyBytes.data() + boxKeys[0].byteOffset,
            boxKeys[0].byteLength) == "alpha" &&
        std::string(boxKeyBytes.data() + boxKeys[1].byteOffset,
            boxKeys[1].byteLength) == "beta",
        "copied key snapshot remains stable across delete and insertion");
    expect(fixture.objects_.snapshotOwnEnumerableStringProperties(ordered,
            keys.data(), 1u, keyBytes.data(), keyBytes.size(), keyCount,
            keyBytesUsed, error) == false && keyCount == 0u &&
        error.code == PortableObjectErrorCode::PropertyCapacityExceeded,
        "undersized key snapshot reports bounded capacity failure");

    const std::size_t propertyCountBefore = fixture.objects_.propertyCount();
    bool deleted = false;
    expect(fixture.objects_.deleteOwnProperty(ordered, view("b"), deleted,
            error) && deleted && fixture.objects_.propertyCount() + 1u ==
                propertyCountBefore,
        "own deletion releases the authoritative property record");
    expect(fixture.objects_.deleteOwnProperty(ordered, view("b"), deleted,
            error) && !deleted,
        "repeated deletion is safe and reports no second removal");
    expect(fixture.objects_.deleteOwnProperty(ordered, view("absent"), deleted,
            error) && !deleted,
        "missing-property deletion succeeds without mutation");
    fixture.objects_.setOwnProperty(ordered, view("b"), Value::number(2.0), error);
    expect(fixture.objects_.snapshotOwnEnumerableStringProperties(ordered,
            keys.data(), keys.size(), keyBytes.data(), keyBytes.size(),
            keyCount, keyBytesUsed, error) && keyAt(0u) == "2" &&
        keyAt(1u) == "10" && keyAt(2u) == "a" && keyAt(3u) == "b",
        "deleted then reinserted key moves to insertion-order tail");

    RuntimeObjectId arrayId = kInvalidRuntimeObjectId;
    expect(fixture.objects_.createArray(3u, kInvalidRuntimeObjectId, arrayId,
            error), "sparse Array is allocated with captured length");
    std::size_t arrayLength = 0u;
    fixture.objects_.arrayLength(arrayId, arrayLength, error);
    const std::size_t capturedLength = arrayLength;
    expect(capturedLength == 3u, "Array length can be snapshotted as a scalar");
    fixture.objects_.setArrayElement(arrayId, 1u, Value::undefined(), error);
    fixture.objects_.setArrayElement(arrayId, 4u, Value::number(44.0), error);
    std::array<PortablePropertyKeySnapshotEntry, 4u> arrayKeys{};
    std::array<char, 32u> arrayKeyBytes{};
    expect(fixture.objects_.snapshotOwnEnumerableStringProperties(arrayId,
            arrayKeys.data(), arrayKeys.size(), arrayKeyBytes.data(),
            arrayKeyBytes.size(), keyCount, keyBytesUsed, error) &&
        keyCount == 2u &&
        std::string(arrayKeyBytes.data() + arrayKeys[0].byteOffset,
            arrayKeys[0].byteLength) == "1" &&
        std::string(arrayKeyBytes.data() + arrayKeys[1].byteOffset,
            arrayKeys[1].byteLength) == "4",
        "Array key snapshot orders present indices and omits holes and length");
    bool hasElement = false;
    fixture.objects_.hasOwnArrayElement(arrayId, 0u, hasElement, error);
    expect(!hasElement, "Array hole is not an own undefined-valued element");
    fixture.objects_.hasOwnArrayElement(arrayId, 1u, hasElement, error);
    expect(hasElement && fixture.objects_.getOwnArrayElement(arrayId, 1u,
            value, found, error) && found && value.isUndefined(),
        "explicit undefined Array element remains an own property");
    fixture.objects_.arrayLength(arrayId, arrayLength, error);
    expect(arrayLength == 5u, "indexed assignment extends Array length");
    expect(fixture.objects_.deleteArrayElement(arrayId, 4u, deleted, error) &&
        deleted && fixture.objects_.arrayLength(arrayId, arrayLength, error) &&
        arrayLength == 5u,
        "indexed deletion leaves Array length unchanged and releases the slot");
    expect(fixture.objects_.setArrayLength(arrayId, 1u, error) &&
        fixture.objects_.arrayLength(arrayId, arrayLength, error) &&
        arrayLength == 1u && fixture.objects_.arrayElementCount() == 0u,
        "Array shrink releases elements beyond the new length");
    expect(capturedLength == 3u && arrayLength == 1u,
        "captured Array length remains stable after later length mutation");
    expect(!fixture.objects_.setArrayLength(arrayId, 129u, error) &&
        error.code == PortableObjectErrorCode::ArrayCapacityExceeded &&
        fixture.objects_.arrayLength(arrayId, arrayLength, error) &&
        arrayLength == 1u,
        "failed Array length mutation preserves the prior store state");
}

void testPropertyCapacityFailureSafety()
{
    PortableObjectRecord objectRecords[1]{};
    PortablePropertyRecord properties[1]{};
    PortableArrayElementRecord elements[1]{};
    char nameBytes[8]{};
    PortableObjectLimits limits;
    limits.maxObjects = 1u;
    limits.maxProperties = 1u;
    limits.maxPropertiesPerObject = 1u;
    limits.maxPropertyNameLength = 8u;
    limits.maxPropertyNameBytes = sizeof(nameBytes);
    limits.maxArrayElements = 1u;
    limits.maxTotalArrayElements = 1u;
    limits.maxArrayIndex = 0u;
    limits.maxArrayLength = 1u;
    limits.maxPrototypeDepth = 1u;
    PortableObjectStore store;
    PortableObjectError error;
    expect(store.configure(PortableObjectStorageView{objectRecords, 1u,
            properties, 1u, elements, 1u, nameBytes, sizeof(nameBytes)},
            limits, 1u, error), "small property store configures");
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    expect(store.createObject(kInvalidRuntimeObjectId, object, error) &&
        store.setOwnProperty(object, view("a"), Value::number(1.0), error),
        "small store accepts its configured property capacity");
    const std::size_t beforeCount = store.propertyCount();
    expect(!store.setOwnProperty(object, view("b"), Value::number(2.0), error) &&
        error.code == PortableObjectErrorCode::PropertyCapacityExceeded &&
        store.propertyCount() == beforeCount,
        "failed property creation leaves store counts unchanged");
    Value value;
    bool found = false;
    expect(store.getOwnProperty(object, view("a"), value, found, error) &&
        found && sameNumber(value, 1.0),
        "failed property creation preserves existing data");
    bool deleted = false;
    expect(store.deleteOwnProperty(object, view("a"), deleted, error) &&
        deleted && store.setOwnProperty(object, view("b"), Value::number(2.0),
            error) && store.propertyCount() == 1u,
        "deletion releases property quota for a subsequent creation");
}

void testHostedParityAndReset()
{
    RuntimeContext hosted;
    const char* source =
        "function hostOwner(){return this;}var hostHolder={};"
        "hostHolder.callback=hostOwner;var hostObserved=hostHolder.callback();";
    const ScriptResult hostResult = hosted.execute(view(source));
    const Value* hostHolder = hosted.lookup(view("hostHolder"));
    const Value* hostObserved = hosted.lookup(view("hostObserved"));
    expect(hostResult.succeeded() && hostHolder != nullptr &&
        hostObserved != nullptr && hostHolder->isObject() &&
        hostObserved->isObject() &&
        hostHolder->objectId() == hostObserved->objectId(),
        "hosted evaluator matches shared explicit method receiver identity");

    RuntimeContext retainedRealm;
    const ScriptResult firstScript = retainedRealm.execute(
        view("function retainedCallback(){return 6;}"));
    const Value* retainedPointer = retainedRealm.lookup(
        view("retainedCallback"));
    const Value retainedFunction = retainedPointer == nullptr
        ? Value::undefined() : *retainedPointer;
    const ScriptResult laterScript = retainedRealm.executeInSameRealm(
        view("var laterScriptRan=1;"));
    expect(firstScript.succeeded(),
        "first same-realm script installs the callback");
    expect(retainedPointer != nullptr && retainedFunction.isFunction(),
        "same-realm callback remains reachable after its first script");
    expect(laterScript.succeeded(),
        "later same-realm script completes beside the retained callback");
    expect(gxos::javascript::NavigatorScriptRuntimeTestAccess::
            functionReferenceIsLive(retainedRealm, retainedFunction),
        "same-realm later script retains the earlier callback handle");
    retainedRealm.reset();
    expect(!gxos::javascript::NavigatorScriptRuntimeTestAccess::
            functionReferenceIsLive(retainedRealm, retainedFunction),
        "full realm reset invalidates the retained callback generation");

    PortableFixture fixture(callbackProgram());
    const bool initialized = fixture.initialize();
    expect(initialized && fixture.run(),
        "realm reset fixture initializes");
    if (!initialized) return;
    if (!fixture.runResult_.completed) return;
    Value retained;
    fixture.global("identity", retained);
    const RuntimeFunctionId oldId = retained.functionId();
    FunctionStoreError functionError;
    fixture.functions_.reset(2u);
    bool callable = true;
    PortableExecutionError callableError;
    expect(!fixture.core_.isCallable(retained, callable, callableError) &&
        !callable && callableError.code == PortableExecutionErrorCode::StaleReference,
        "realm reset invalidates a retained callable generation");
    PortableExecutionResult invocation;
    expect(!fixture.core_.invokeCallable(retained, Value::undefined(), nullptr,
            0u, invocation) &&
        invocation.error.code == PortableExecutionErrorCode::StaleReference,
        "stale callback invocation is rejected after realm reset");
    (void)oldId;
    (void)functionError;
}

} // namespace

int main()
{
    testCallableAndInvocation();
    testErrorsAndSideEffects();
    testPropertyOperationsAndSnapshots();
    testPropertyCapacityFailureSafety();
    testHostedParityAndReset();
    std::cout << "Navigator JavaScript JS125 callback-foundation checks: "
        << (checks - failures) << '/' << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
