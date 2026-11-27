#include "navigator_javascript/execution_core.h"
#include "navigator_javascript/frontend_storage.h"
#include "navigator_javascript/lexer.h"
#include "navigator_javascript/parser.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace gxos::javascript;

int checks = 0;
int failures = 0;
std::size_t peakExecutionFrames = 0u;
std::size_t peakPreflightFrames = 0u;
std::size_t peakCallDepth = 0u;
std::size_t peakArgumentValues = 0u;

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

bool numberIs(Value value, double expected)
{
    return value.isNumber() && value.numberValue() == expected;
}

void recordHighWater(const PortableExecutionResult& result)
{
    if (result.executionFrameHighWater > peakExecutionFrames)
        peakExecutionFrames = result.executionFrameHighWater;
    if (result.preflightFrameHighWater > peakPreflightFrames)
        peakPreflightFrames = result.preflightFrameHighWater;
    if (result.callDepthHighWater > peakCallDepth)
        peakCallDepth = result.callDepthHighWater;
    if (result.argumentValuesHighWater > peakArgumentValues)
        peakArgumentValues = result.argumentValuesHighWater;
}

class Fixture {
public:
    explicit Fixture(std::string source,
        std::size_t callCapacity = 64u,
        std::size_t argumentCapacity = 512u,
        std::size_t executionCapacity = kDefaultPortableExecutionFrameCapacity,
        std::size_t stepLimit = 100000u,
        std::size_t astDepthLimit = 256u,
        std::size_t preflightWorkLimit = 65536u)
        : source_(std::move(source)), tokens_(8192u), nodes_(8192u),
          children_(16384u), parserScratch_(128u * 1024u),
          ast_(SourceView(source_.data(), source_.size()),
              AstStorageView{nodes_.data(), nodes_.size(), children_.data(),
                  children_.size()}),
          environments_(), environmentRecords_(512u), bindings_(4096u),
          bindingNames_(256u * 1024u), stringsRecords_(512u),
          stringBytes_(64u * 1024u), functionsStorage_(512u),
          objectsRecords_(512u), properties_(4096u), arrayElements_(2048u),
          propertyNames_(128u * 1024u), frames_(callCapacity),
          arguments_(argumentCapacity), executionFrames_(executionCapacity),
          core_(ast_, strings_, environments_, functions_, objects_,
              PortableExecutionStorageView{
                  frames_.empty() ? nullptr : frames_.data(), frames_.size(),
                  arguments_.empty() ? nullptr : arguments_.data(),
                  arguments_.size(),
                  executionFrames_.empty() ? nullptr : executionFrames_.data(),
                  executionFrames_.size()},
              PortableExecutionLimits{64u, 64u, 4096u, stepLimit, 256u,
                  256u, 64u * 1024u, astDepthLimit, executionCapacity,
                  preflightWorkLimit}, steps_)
    {
    }

    bool initialize()
    {
        ast_.reset(SourceView(source_.data(), source_.size()));
        TokenBuffer tokenBuffer{tokens_.data(), tokens_.size()};
        Lexer lexer;
        const LexerResult lexed = lexer.tokenizeInto(
            SourceView(source_.data(), source_.size()), tokenBuffer);
        if (!lexed.succeeded()) return false;
        FrontendArena scratch(parserScratch_.data(), parserScratch_.size());
        Parser parser;
        ParserError parserError;
        if (!parser.parseInto(makeTokenView(
                SourceView(source_.data(), source_.size()), tokenBuffer), ast_,
                scratch, parserError)) return false;

        EnvironmentError environmentError;
        if (!environments_.configure(EnvironmentStorageView{
                environmentRecords_.data(), environmentRecords_.size(),
                bindings_.data(), bindings_.size(), bindingNames_.data(),
                bindingNames_.size()}, 1u, environmentError) ||
            !environments_.createRoot(EnvironmentLimits(), root_,
                environmentError)) return false;
        RuntimeStringError stringError;
        if (!strings_.configure(RuntimeStringStorageView{
                stringsRecords_.data(), stringsRecords_.size(),
                stringBytes_.data(), stringBytes_.size()}, 1u, stringError))
            return false;
        FunctionStoreError functionError;
        if (!functions_.configure(FunctionStorageView{
                functionsStorage_.data(), functionsStorage_.size()},
                1u, 0u, functionError)) return false;
        PortableObjectLimits objectLimits;
        objectLimits.maxObjects = objectsRecords_.size();
        objectLimits.maxProperties = properties_.size();
        objectLimits.maxPropertiesPerObject = 256u;
        objectLimits.maxPropertyNameLength = 256u;
        objectLimits.maxPropertyNameBytes = propertyNames_.size();
        objectLimits.maxArrayElements = 256u;
        objectLimits.maxTotalArrayElements = arrayElements_.size();
        objectLimits.maxArrayIndex = 255u;
        objectLimits.maxArrayLength = 256u;
        objectLimits.maxPrototypeDepth = 16u;
        PortableObjectError objectError;
        return objects_.configure(PortableObjectStorageView{
                objectsRecords_.data(), objectsRecords_.size(),
                properties_.data(), properties_.size(), arrayElements_.data(),
                arrayElements_.size(), propertyNames_.data(),
                propertyNames_.size()}, objectLimits, 1u, objectError);
    }

    bool run()
    {
        const bool succeeded = core_.run(ast_.root(), root_, finalValue_, result_);
        recordHighWater(result_);
        return succeeded;
    }

    bool read(const char* name, Value& value) const
    {
        EnvironmentError error;
        return environments_.readChain(root_, view(name), value, error);
    }

    bool property(Value object, const char* name, Value& value) const
    {
        if (!object.isObject()) return false;
        PortableObjectError error;
        bool found = false;
        return objects_.getProperty(object.objectId(), view(name), value,
                found, error) && found;
    }

    bool invoke(Value function, Value receiver, const Value* arguments,
        std::size_t count, Value& returned, PortableExecutionResult& result)
    {
        const bool succeeded = core_.invokeCallable(function, receiver,
            arguments, count, result);
        recordHighWater(result);
        if (succeeded) returned = result.finalValue;
        return succeeded;
    }

    bool framesReleased() const
    {
        for (const PortableExecutionFrame& frame : executionFrames_)
            if (frame.kind != PortableExecutionFrameKind::Empty) return false;
        for (const PortableCallFrame& frame : frames_)
            if (frame.continuationNode != kInvalidAstNodeId) return false;
        return true;
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
    std::vector<RuntimeStringRecord> stringsRecords_;
    std::vector<char> stringBytes_;
    FunctionStore functions_;
    std::vector<FunctionRecord> functionsStorage_;
    PortableObjectStore objects_;
    std::vector<PortableObjectRecord> objectsRecords_;
    std::vector<PortablePropertyRecord> properties_;
    std::vector<PortableArrayElementRecord> arrayElements_;
    std::vector<char> propertyNames_;
    std::vector<PortableCallFrame> frames_;
    std::vector<Value> arguments_;
    std::vector<PortableExecutionFrame> executionFrames_;
    std::size_t steps_ = 0u;
    PortableObjectExecutionCore core_;
    EnvironmentId root_ = kInvalidEnvironmentId;
    Value finalValue_ = Value::undefined();
    PortableExecutionResult result_;
};

void testUnifiedExternalActivation()
{
    Fixture fixture(
        "let state={sum:0};let receiver={value:30};"
        "function collect(a,b){state.sum=state.sum+a+b;"
            "return this.value+state.sum;}"
        "let owner={value:11,make:function(){"
            "return (delta)=>this.value+delta;}};"
        "let arrow=owner.make();"
        "function makeClosure(){let base=40;"
            "return function(delta){return base+delta;};}"
        "let closure=makeClosure();");
    expect(fixture.initialize() && fixture.run(),
        "script entry uses the resumable execution machine");

    Value collect;
    Value receiver;
    Value state;
    Value arrow;
    Value closure;
    expect(fixture.read("collect", collect) && collect.isFunction() &&
        fixture.read("receiver", receiver) && receiver.isObject() &&
        fixture.read("state", state) && state.isObject() &&
        fixture.read("arrow", arrow) && arrow.isFunction() &&
        fixture.read("closure", closure) && closure.isFunction(),
        "run entry retains generation-safe callback values and closures");

    const Value collectArguments[] = {Value::number(2.0), Value::number(5.0)};
    Value returned;
    PortableExecutionResult result;
    expect(fixture.invoke(collect, receiver, collectArguments, 2u, returned,
            result) && numberIs(returned, 37.0) &&
        result.preflightCompleted && result.executionFrameHighWater != 0u,
        "external ordinary call binds arguments and explicit receiver");
    Value sum;
    expect(fixture.property(state, "sum", sum) && numberIs(sum, 7.0),
        "external callback preserves its object mutation");

    const Value arrowArgument = Value::number(8.0);
    expect(fixture.invoke(arrow, Value::undefined(), &arrowArgument, 1u,
            returned, result) && numberIs(returned, 19.0),
        "external arrow activation keeps lexical this despite its receiver");
    const Value closureArgument = Value::number(2.0);
    expect(fixture.invoke(closure, Value::undefined(), &closureArgument, 1u,
            returned, result) && numberIs(returned, 42.0),
        "external closure activation reads its captured environment");
    expect(fixture.framesReleased(),
        "both machine arenas and continuation records release after callbacks");
}

void testDepthBoundAndErrorRecovery()
{
    Fixture fixture(
        "function count(n){return n&&count(n-1)+1;}");
    expect(fixture.initialize() && fixture.run(),
        "recursive callback declaration passes iterative preflight");
    Value count;
    expect(fixture.read("count", count) && count.isFunction(),
        "recursive callback is retained as a callable value");

    const Value atLimit = Value::number(63.0);
    Value returned;
    PortableExecutionResult result;
    expect(fixture.invoke(count, Value::undefined(), &atLimit, 1u,
            returned, result) && numberIs(returned, 63.0) &&
        result.callDepthHighWater == 64u && fixture.framesReleased(),
        "external entry reaches all 64 logical JavaScript activations");

    const Value beyondLimit = Value::number(64.0);
    expect(!fixture.invoke(count, Value::undefined(), &beyondLimit, 1u,
            returned, result) &&
        result.error.code == PortableExecutionErrorCode::CallDepthExhausted &&
        result.callDepthHighWater == 64u && fixture.framesReleased(),
        "one activation beyond the limit aborts and releases machine state");

    const Value shallow = Value::number(0.0);
    expect(fixture.invoke(count, Value::undefined(), &shallow, 1u,
            returned, result) && numberIs(returned, 0.0) &&
        fixture.framesReleased(),
        "a later callback succeeds after the failed nested invocation");
}

void testIterativePreflightLimits()
{
    std::string source = "let result=";
    for (std::size_t index = 0u; index < 80u; ++index) source += '!';
    source += "true;";
    Fixture deep(std::move(source));
    expect(deep.initialize() && deep.run() &&
        deep.result_.preflightFrameHighWater == 83u &&
        deep.result_.preflightWorkUnits >= 83u &&
        deep.result_.preflightWorkUnits < 256u,
        "depth-80 syntax preflight bounds records and counted work");

    std::string tooDeep = "let result=";
    for (std::size_t index = 0u; index < 80u; ++index) tooDeep += '!';
    tooDeep += "true;";
    Fixture boundedDepth(std::move(tooDeep), 64u, 512u,
        kDefaultPortableExecutionFrameCapacity, 100000u, 24u);
    expect(boundedDepth.initialize() && !boundedDepth.run() &&
        boundedDepth.result_.error.code ==
            PortableExecutionErrorCode::PreflightLimitExceeded &&
        boundedDepth.result_.preflightFrameHighWater <= 25u &&
        boundedDepth.framesReleased(),
        "preflight depth limit fails closed and clears its shared arena");

    Fixture boundedWork("let first=1;let second=2;", 64u, 512u,
        kDefaultPortableExecutionFrameCapacity, 100000u, 256u, 2u);
    expect(boundedWork.initialize() && !boundedWork.run() &&
        boundedWork.result_.error.code ==
            PortableExecutionErrorCode::PreflightLimitExceeded &&
        !boundedWork.result_.preflightCompleted &&
        boundedWork.framesReleased(),
        "preflight work budget rejects excess traversal before execution");

    std::string lookupWork = "function target(){return 1;} let result=target();";
    for (std::size_t index = 0u; index < 48u; ++index) {
        lookupWork += "let spare";
        lookupWork += std::to_string(index);
        lookupWork += "=0;";
    }
    Fixture boundedLookup(std::move(lookupWork), 64u, 512u,
        kDefaultPortableExecutionFrameCapacity, 100000u, 256u, 64u);
    expect(boundedLookup.initialize() && !boundedLookup.run() &&
        boundedLookup.result_.error.code ==
            PortableExecutionErrorCode::PreflightLimitExceeded &&
        boundedLookup.result_.preflightWorkUnits == 64u &&
        !boundedLookup.result_.preflightCompleted &&
        boundedLookup.framesReleased(),
        "declaration lookup scans stop at the shared preflight work budget");
}

} // namespace

int main()
{
    testUnifiedExternalActivation();
    testDepthBoundAndErrorRecovery();
    testIterativePreflightLimits();
    std::cout << "JS129 unified portable call entry checks: " << checks - failures
        << '/' << checks << " passed\n";
    std::cout << "JS129 machine high-water: executionFrames="
        << peakExecutionFrames << " (" << peakExecutionFrames *
            sizeof(PortableExecutionFrame) << " bytes), preflightFrames="
        << peakPreflightFrames << " (" << peakPreflightFrames *
            sizeof(PortableExecutionFrame) << " bytes), callDepth="
        << peakCallDepth << ", argumentValues=" << peakArgumentValues << '\n';
    return failures == 0 ? 0 : 1;
}
