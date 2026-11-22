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
std::size_t peakCallDepth = 0u;
std::size_t peakArgumentValues = 0u;

void recordHighWater(const PortableExecutionResult& result)
{
    if (result.executionFrameHighWater > peakExecutionFrames)
        peakExecutionFrames = result.executionFrameHighWater;
    if (result.callDepthHighWater > peakCallDepth)
        peakCallDepth = result.callDepthHighWater;
    if (result.argumentValuesHighWater > peakArgumentValues)
        peakArgumentValues = result.argumentValuesHighWater;
}

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

class Fixture {
public:
    explicit Fixture(std::string source,
        std::size_t callCapacity = 64u,
        std::size_t argumentCapacity = 512u,
        std::size_t executionCapacity = kDefaultPortableExecutionFrameCapacity,
        std::size_t stepLimit = 100000u)
        : source_(std::move(source)), tokens_(8192u), nodes_(8192u),
          children_(16384u), parserScratch_(128u * 1024u),
          ast_(SourceView(source_.data(), source_.size()),
              AstStorageView{nodes_.data(), nodes_.size(), children_.data(),
                  children_.size()}),
          environments_(), environmentRecords_(512u), bindings_(4096u),
          bindingNames_(256u * 1024u),
          stringsRecords_(512u), stringBytes_(64u * 1024u),
          functionsStorage_(512u), objectsRecords_(512u),
          properties_(4096u), arrayElements_(2048u), propertyNames_(128u * 1024u),
          frames_(callCapacity), arguments_(argumentCapacity),
          executionFrames_(executionCapacity),
          core_(ast_, strings_, environments_, functions_, objects_,
              PortableExecutionStorageView{
                  frames_.empty() ? nullptr : frames_.data(), frames_.size(),
                  arguments_.empty() ? nullptr : arguments_.data(),
                  arguments_.size(),
                  executionFrames_.empty() ? nullptr : executionFrames_.data(),
                  executionFrames_.size()},
              PortableExecutionLimits{64u, 64u, 4096u, stepLimit, 256u,
                  256u, 64u * 1024u, 256u, executionCapacity}, steps_)
    {
    }

    bool parse()
    {
        ast_.reset(SourceView(source_.data(), source_.size()));
        TokenBuffer tokenBuffer{tokens_.data(), tokens_.size()};
        Lexer lexer;
        const LexerResult lexed = lexer.tokenizeInto(
            SourceView(source_.data(), source_.size()), tokenBuffer);
        if (!lexed.succeeded()) return false;
        FrontendArena scratch(parserScratch_.data(), parserScratch_.size());
        Parser parser;
        ParserError error;
        return parser.parseInto(makeTokenView(
            SourceView(source_.data(), source_.size()), tokenBuffer), ast_,
            scratch, error);
    }

    bool run()
    {
        const bool succeeded = core_.run(ast_.root(), root_, finalValue_, result_);
        recordHighWater(result_);
        return succeeded;
    }

    bool appendAndRun(const std::string& source)
    {
        const std::size_t sourceOffset = source_.size();
        std::size_t lineOffset = 0u;
        for (char ch : source_)
            if (ch == '\n') ++lineOffset;
        source_ += source;
        ast_.setSource(SourceView(source_.data(), source_.size()));
        const SourceView appendedSource(source.data(), source.size());
        Lexer lexer;
        const LexResult lexed = lexer.tokenize(appendedSource);
        if (!lexed.succeeded()) return false;
        Parser parser;
        ParseResult appended = parser.parse(appendedSource, lexed);
        if (!appended.succeeded()) return false;
        AstNodeId appendedRoot = kInvalidAstNodeId;
        if (!ast_.appendFrom(appended.ast, sourceOffset, lineOffset,
                appendedRoot)) return false;
        ast_.setSource(SourceView(source_.data(), source_.size()));
        const bool succeeded = core_.run(appendedRoot, root_, finalValue_, result_);
        recordHighWater(result_);
        return succeeded;
    }

    bool initialize()
    {
        if (!parse()) return false;
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

void testExpressionAndStatementResumption()
{
    Fixture fixture(
        "let trace=0;let state={};"
        "function first(){trace=trace*10+1;return 4;}"
        "function second(){trace=trace*10+2;return 7;}"
        "function combine(a,b){return a*10+b;}"
        "function calculate(n){return n+2;}"
        "function work(n){let result=calculate(n);state.value=result;return result+1;}"
        "state.a=1;let args=combine(first(),second());"
        "let workResult=work(5);state.b=calculate(6);"
        "let sequence=state.a+state.b;");
    expect(fixture.initialize(), "generic execution fixture parses and configures");
    expect(fixture.run(), "initializer, assignment, args, and sequence execute");
    Value trace, state, field, args, workResult, sequence;
    const bool observed = fixture.read("trace", trace) &&
        fixture.read("state", state) && fixture.property(state, "value", field) &&
        fixture.read("args", args) && fixture.read("workResult", workResult) &&
        fixture.read("sequence", sequence);
    expect(observed && numberIs(trace, 12.0) && numberIs(args, 47.0) &&
        numberIs(field, 7.0) && numberIs(workResult, 8.0) &&
        numberIs(sequence, 9.0),
        "left-to-right arguments and resumed sequential statements run once");
    expect(fixture.result_.executionFrameHighWater > 1u &&
        fixture.framesReleased(),
        "execution frames and call records release after normal completion");
}

void testGenericRecursionAndDepths()
{
    const std::size_t depths[] = {0u, 7u, 15u, 31u, 62u};
    for (const std::size_t depth : depths) {
        Fixture fixture(
            "function count(n){return (n>0&&count(n-1)+1)||0;}"
            "let result=count(" + std::to_string(depth) + ");");
        const bool success = fixture.initialize() && fixture.run();
        Value result;
        expect(success && fixture.read("result", result) &&
            numberIs(result, static_cast<double>(depth)) &&
            fixture.result_.callDepthHighWater == depth + 1u &&
            fixture.framesReleased(),
            "generic arithmetic recursion resumes at the correct depth");
    }

    Fixture maximum(
        "function count(n){return (n>0&&count(n-1)+1)||0;}let result=count(63);");
    const bool maxSuccess = maximum.initialize() && maximum.run();
    Value maxResult;
    expect(maxSuccess && maximum.read("result", maxResult) &&
        numberIs(maxResult, 63.0) &&
        maximum.result_.callDepthHighWater == 64u,
        "generic non-tail calls use the full accepted depth of 64");

    Fixture beyond(
        "function count(n){return (n>0&&count(n-1)+1)||0;}let result=count(64);");
    expect(beyond.initialize() && !beyond.run() &&
        beyond.result_.error.code == PortableExecutionErrorCode::CallDepthExhausted &&
        beyond.result_.callDepthHighWater == 64u && beyond.framesReleased(),
        "one call beyond the logical depth limit unwinds cleanly");

    Fixture mutual(
        "function even(n){return n&&odd(n-1)+1;}"
        "function odd(n){return n&&even(n-1)+1;}let result=even(16);");
    Value mutualResult;
    expect(mutual.initialize() && mutual.run() &&
        mutual.read("result", mutualResult) && numberIs(mutualResult, 16.0) &&
        mutual.result_.callDepthHighWater == 17u,
        "mutual recursion uses the same generic call frames");

    std::string tailSource = "function f0(){return 9;}";
    for (std::size_t index = 1u; index <= 40u; ++index) {
        tailSource += "function f" + std::to_string(index) +
            "(){return f" + std::to_string(index - 1u) + "();}";
    }
    tailSource += "let result=f40();";
    Fixture tail(std::move(tailSource));
    Value tailResult;
    expect(tail.initialize() && tail.run() && tail.read("result", tailResult) &&
        numberIs(tailResult, 9.0) &&
        tail.result_.tailCallsExecuted == 40u &&
        tail.result_.callDepthHighWater == 41u,
        "direct tail dispatch remains iterative and depth-accounted");
}

void testReceiverClosuresAndPropertyCalls()
{
    Fixture fixture(
        "let receiver={value:20,read:function(){return this.value;},"
        "owner:function(){let arrow=()=>this.value;return arrow();}};"
        "let methodValue=receiver.read();let arrowValue=receiver.owner();"
        "function make(){let captured=17;return function(){return captured;};}"
        "let closure=make();let closureValue=closure();"
        "let fn=function(value){return value*2;};let variableCall=fn(6);");
    Value methodValue, arrowValue, closureValue, variableCall;
    expect(fixture.initialize() && fixture.run() &&
        fixture.read("methodValue", methodValue) &&
        fixture.read("arrowValue", arrowValue) &&
        fixture.read("closureValue", closureValue) &&
        fixture.read("variableCall", variableCall) &&
        numberIs(methodValue, 20.0) && numberIs(arrowValue, 20.0) &&
        numberIs(closureValue, 17.0) && numberIs(variableCall, 12.0),
        "property receivers, arrow lexical this, closures, and variable calls resume");
}

void testCapacityErrorsAndRecovery()
{
    Fixture frames(
        "function count(n){return n&&count(n-1)+1;}let result=count(8);",
        64u, 512u, 8u);
    expect(frames.initialize() && !frames.run() &&
        frames.result_.error.code ==
            PortableExecutionErrorCode::ExecutionFrameCapacityExhausted &&
        frames.framesReleased(),
        "execution-frame exhaustion is explicit and unwinds active calls");

    Fixture arguments(
        "let trace=0;function first(){trace=trace+1;return 1;}"
        "function second(){trace=trace+2;return 2;}"
        "function combine(a,b){return a+b;}let result=combine(first(),second());",
        64u, 1u);
    Value trace;
    expect(arguments.initialize() && !arguments.run() &&
        arguments.result_.error.code ==
            PortableExecutionErrorCode::ArgumentCapacityExhausted &&
        arguments.read("trace", trace) && numberIs(trace, 0.0),
        "argument capacity fails before starting observable argument work");

    Fixture steps("let state={value:0};state.value=9;1+2;",
        64u, 512u, kDefaultPortableExecutionFrameCapacity, 9u);
    Value state, value;
    expect(steps.initialize() && !steps.run() &&
        steps.result_.error.code == PortableExecutionErrorCode::StepBudgetExhausted &&
        steps.read("state", state) && steps.property(state, "value", value) &&
        numberIs(value, 9.0) && steps.framesReleased(),
        "step exhaustion preserves earlier mutations and releases machine state");

    Fixture failed(
        "let state={value:0};function bad(){state.value=7;return missing;}"
        "function caller(){let value=bad();return value+1;}let result=caller();");
    Value failedState, priorMutation;
    const bool failedRun = failed.initialize() && !failed.run();
    expect(failedRun && failed.result_.error.code ==
            PortableExecutionErrorCode::MissingBinding &&
        failed.read("state", failedState) &&
        failed.property(failedState, "value", priorMutation) &&
        numberIs(priorMutation, 7.0) && failed.framesReleased(),
        "errors unwind suspended calls without rolling back prior mutations");

    const bool reparsed = true;
    Value recovered;
    expect(reparsed && failed.appendAndRun(
            "function recovered(){return 77;}let value=recovered();") &&
        failed.read("value", recovered) &&
        numberIs(recovered, 77.0),
        "fresh supported execution succeeds after machine error cleanup");
}

} // namespace

int main()
{
    testExpressionAndStatementResumption();
    testGenericRecursionAndDepths();
    testReceiverClosuresAndPropertyCalls();
    testCapacityErrorsAndRecovery();
    std::cout << "Navigator JavaScript JS128 general resumable checks: "
        << (checks - failures) << '/' << checks << " passed\n"
        << "JS128 machine high-water: executionFrames="
        << peakExecutionFrames << " (" << peakExecutionFrames *
            sizeof(PortableExecutionFrame) << " bytes), callDepth="
        << peakCallDepth << ", argumentValues=" << peakArgumentValues << '\n';
    return failures == 0 ? 0 : 1;
}
