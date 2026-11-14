#include "navigator_javascript/execution_core.h"
#include "navigator_javascript/lexer.h"
#include "navigator_javascript/parser.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace gxos::javascript;

namespace {

constexpr std::size_t kTestEnvironmentCapacity = 2048u;
constexpr std::size_t kTestBindingCapacity = 16384u;
constexpr std::size_t kTestNameBytes = 1024u * 1024u;
constexpr std::size_t kTestStringRecords = 4096u;
constexpr std::size_t kTestStringBytes = 256u * 1024u;
constexpr std::size_t kTestFunctionCapacity = 4096u;
constexpr std::size_t kTestFrameCapacity = 64u;
constexpr std::size_t kTestArgumentCapacity = 512u;

Token tokenStorage[kDefaultMaxJavaScriptTokens];
AstNode astNodeStorage[kDefaultMaxJavaScriptAstNodes];
AstNodeId astChildStorage[kDefaultMaxJavaScriptAstNodes];
alignas(64) unsigned char parserScratch[
    kDefaultJavaScriptParserScratchBytes];
EnvironmentRecord environmentStorage[kTestEnvironmentCapacity];
Binding bindingStorage[kTestBindingCapacity];
char bindingNameStorage[kTestNameBytes];
RuntimeStringRecord stringRecordStorage[kTestStringRecords];
char stringStorage[kTestStringBytes];
FunctionRecord functionStorage[kTestFunctionCapacity];
PortableCallFrame callFrameStorage[kTestFrameCapacity];
Value argumentStorage[kTestArgumentCapacity];

Token secondaryTokenStorage[128u];
AstNode secondaryAstNodes[256u];
AstNodeId secondaryAstChildren[256u];
alignas(64) unsigned char secondaryParserScratch[8192u];

std::size_t checkCount = 0u;
std::size_t failureCount = 0u;
RuntimeGeneration nextGeneration = 1u;

struct UsageHighWater {
    std::size_t environments = 0u;
    std::size_t bindings = 0u;
    std::size_t stringValues = 0u;
    std::size_t stringBytes = 0u;
    std::size_t functions = 0u;
    std::size_t callDepth = 0u;
    std::size_t argumentValues = 0u;
} usageHighWater;

void check(bool condition, const char* label)
{
    ++checkCount;
    if (condition) return;
    ++failureCount;
    std::printf("FAIL: %s\n", label);
}

std::size_t textLength(const char* text)
{
    std::size_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

struct TestOptions {
    PortableExecutionLimits limits;
    std::size_t environmentCapacity = kTestEnvironmentCapacity;
    std::size_t bindingCapacity = kTestBindingCapacity;
    std::size_t bindingNameCapacity = kTestNameBytes;
    std::size_t stringRecordCapacity = kTestStringRecords;
    std::size_t stringByteCapacity = kTestStringBytes;
    std::size_t functionCapacity = kTestFunctionCapacity;
    std::size_t frameCapacity = kTestFrameCapacity;
    std::size_t argumentCapacity = kTestArgumentCapacity;
};

struct TestRun {
    bool parsed = false;
    bool succeeded = false;
    Ast ast;
    TokenBuffer tokens{tokenStorage, kDefaultMaxJavaScriptTokens};
    FrontendArena arena;
    EnvironmentStore environments;
    RuntimeStringStore strings;
    FunctionStore functions;
    EnvironmentId root = kInvalidEnvironmentId;
    PortableExecutionResult result;
    Value finalValue = Value::undefined();
    std::size_t steps = 0u;
    RuntimeGeneration generation = kInvalidRuntimeGeneration;

    TestRun() : ast(SourceView(), AstStorageView{astNodeStorage,
            kDefaultMaxJavaScriptAstNodes, astChildStorage,
            kDefaultMaxJavaScriptAstNodes})
    {
    }

    bool parse(const char* sourceText, AstNodeId& rootNode)
    {
        const SourceView source(sourceText, textLength(sourceText));
        ast.reset(source);
        arena.configure(parserScratch, sizeof(parserScratch));
        Lexer lexer;
        const LexerResult lexed = lexer.tokenizeInto(source, tokens);
        if (!lexed.succeeded()) return false;
        Parser parser;
        ParserError error;
        parsed = parser.parseInto(makeTokenView(source, tokens), ast, arena,
            error);
        rootNode = ast.root();
        return parsed;
    }

    bool configure(const TestOptions& options)
    {
        generation = nextGeneration++;
        EnvironmentError environmentError;
        if (!environments.configure(EnvironmentStorageView{
                environmentStorage, options.environmentCapacity,
                bindingStorage, options.bindingCapacity, bindingNameStorage,
                options.bindingNameCapacity}, generation, environmentError))
            return false;
        RuntimeStringError stringError;
        if (!strings.configure(RuntimeStringStorageView{
                options.stringRecordCapacity == 0u ? nullptr
                    : stringRecordStorage,
                options.stringRecordCapacity,
                options.stringByteCapacity == 0u ? nullptr : stringStorage,
                options.stringByteCapacity}, generation, stringError))
            return false;
        FunctionStoreError functionError;
        if (!functions.configure(FunctionStorageView{
                options.functionCapacity == 0u ? nullptr : functionStorage,
                options.functionCapacity}, generation, 0u, functionError))
            return false;
        return environments.createRoot(EnvironmentLimits{256u, 256u}, root,
            environmentError);
    }

    bool execute(AstNodeId rootNode, const TestOptions& options,
        EnvironmentId environment = kInvalidEnvironmentId)
    {
        if (!configure(options)) return false;
        if (environment == kInvalidEnvironmentId) environment = root;
        const PortableExecutionStorageView storage{
            options.frameCapacity == 0u ? nullptr : callFrameStorage,
            options.frameCapacity,
            options.argumentCapacity == 0u ? nullptr : argumentStorage,
            options.argumentCapacity};
        PortableExecutionCore core(ast, strings, environments, functions,
            storage, options.limits, steps);
        succeeded = core.run(rootNode, environment, finalValue, result);
        if (environments.environmentCount() > usageHighWater.environments)
            usageHighWater.environments = environments.environmentCount();
        if (environments.bindingCount() > usageHighWater.bindings)
            usageHighWater.bindings = environments.bindingCount();
        if (strings.count() > usageHighWater.stringValues)
            usageHighWater.stringValues = strings.count();
        if (strings.bytesUsed() > usageHighWater.stringBytes)
            usageHighWater.stringBytes = strings.bytesUsed();
        if (functions.count() > usageHighWater.functions)
            usageHighWater.functions = functions.count();
        if (result.callDepthHighWater > usageHighWater.callDepth)
            usageHighWater.callDepth = result.callDepthHighWater;
        if (result.argumentValuesHighWater > usageHighWater.argumentValues)
            usageHighWater.argumentValues = result.argumentValuesHighWater;
        return true;
    }

    bool read(const char* name, Value& value) const
    {
        EnvironmentError error;
        return environments.readChain(root, SourceView(name, textLength(name)),
            value, error);
    }
};

bool executeSource(TestRun& run, const char* source,
    const TestOptions& options = TestOptions())
{
    AstNodeId root = kInvalidAstNodeId;
    return run.parse(source, root) && run.execute(root, options);
}

bool isNumber(const Value& value, double expected)
{
    return value.isNumber() && value.numberValue() == expected;
}

bool readNumber(const TestRun& run, const char* name, double expected)
{
    Value actual;
    return run.read(name, actual) && isNumber(actual, expected);
}

void testBindingsAndPrimitiveExpressions()
{
    TestRun arithmetic;
    check(executeSource(arithmetic,
        "let a = 10; const b = 20; a = a + b; a;"),
        "binding arithmetic: parse and execute");
    check(arithmetic.succeeded && isNumber(arithmetic.finalValue, 30.0),
        "binding arithmetic: final value is 30");
    check(readNumber(arithmetic, "a", 30.0),
        "binding arithmetic: assignment updates shared environment");

    TestRun shadow;
    check(executeSource(shadow,
        "let value = 5; { let value = 12; } value;"),
        "block shadowing: parse and execute");
    check(readNumber(shadow, "value", 5.0),
        "block shadowing: outer binding remains 5");

    TestRun primitives;
    check(executeSource(primitives,
        "const n = (2 + 3) * 4; const yes = true && (n >= 20);"
        " const no = null == undefined; yes;"),
        "primitive operators: parse and execute");
    check(primitives.finalValue.isBoolean() &&
            primitives.finalValue.booleanValue(),
        "primitive operators: grouping comparison and logical result");
    check(readNumber(primitives, "n", 20.0),
        "primitive operators: arithmetic precedence");
    Value no;
    check(primitives.read("no", no) && no.isBoolean() && no.booleanValue(),
        "primitive operators: null and undefined loose equality");

    TestRun strings;
    check(executeSource(strings, "const text = 'a\\nb'; text;"),
        "string literal: parse and execute");
    SourceView decoded;
    RuntimeStringError stringError;
    check(strings.finalValue.isString() && strings.strings.view(
            strings.finalValue.stringId(), decoded, stringError) &&
            decoded.length == 3u && decoded.data[0] == 'a' &&
            decoded.data[1] == '\n' && decoded.data[2] == 'b',
        "string literal: shared store decodes accepted escape");
}

void testBindingErrorsAndCapacity()
{
    TestRun tdz;
    check(executeSource(tdz,
        "let value = 1; { value; let value = 2; }"),
        "TDZ: parser succeeds and core executes");
    check(!tdz.succeeded && tdz.result.error.code ==
            PortableExecutionErrorCode::UninitializedBinding,
        "TDZ: inner uninitialized binding hides outer binding");

    TestRun missing;
    check(executeSource(missing, "missing;"), "missing name: parse");
    check(!missing.succeeded && missing.result.error.code ==
            PortableExecutionErrorCode::MissingBinding,
        "missing name: explicit ReferenceError equivalent");

    TestRun immutable;
    check(executeSource(immutable, "const fixed = 1; fixed = 2;"),
        "const assignment: parse and execute");
    check(!immutable.succeeded && immutable.result.error.code ==
            PortableExecutionErrorCode::ImmutableAssignment,
        "const assignment: immutable binding rejected");

    TestOptions environmentLimit;
    environmentLimit.environmentCapacity = 1u;
    TestRun exhaustedEnvironment;
    check(executeSource(exhaustedEnvironment,
        "{ let local = 1; }", environmentLimit),
        "environment capacity: parse and configure");
    check(!exhaustedEnvironment.succeeded &&
            exhaustedEnvironment.result.error.code ==
                PortableExecutionErrorCode::EnvironmentCapacityExhausted,
        "environment capacity: explicit bounded failure");

    TestOptions functionLimit;
    functionLimit.limits.maxFunctions = 0u;
    TestRun exhaustedFunction;
    const bool functionProgramParsed = executeSource(exhaustedFunction,
        "function unavailable() { return 1; }", functionLimit);
    check(functionProgramParsed,
        "function capacity: parse and configure");

    TestOptions bindingCapacity;
    bindingCapacity.bindingCapacity = 0u;
    TestRun unpublishedFunction;
    const bool bindingProgramParsed = executeSource(unpublishedFunction,
        "function unpublished() { return 1; }", bindingCapacity);
    check(!unpublishedFunction.succeeded &&
            exhaustedFunction.result.error.code ==
                PortableExecutionErrorCode::FunctionCapacityExhausted &&
            bindingProgramParsed && !unpublishedFunction.succeeded &&
            unpublishedFunction.result.error.code ==
                PortableExecutionErrorCode::BindingCapacityExhausted &&
            unpublishedFunction.functions.count() == 0u &&
            unpublishedFunction.result.functionsCreated == 0u,
        "function and binding capacity: failures publish no partial function");

    TestOptions stringLimit;
    stringLimit.stringRecordCapacity = 0u;
    stringLimit.stringByteCapacity = 0u;
    TestRun exhaustedString;
    check(executeSource(exhaustedString, "'text';", stringLimit),
        "string capacity: parse and configure");
    check(!exhaustedString.succeeded && exhaustedString.result.error.code ==
            PortableExecutionErrorCode::StringCapacityExhausted,
        "string capacity: explicit bounded failure");
}

void testFunctionsAndClosures()
{
    TestRun ordinary;
    check(executeSource(ordinary,
        "function add(a, b) { return a + b; }"
        " let result = add(7, 8); result;"),
        "ordinary call: parse and execute");
    check(readNumber(ordinary, "result", 15.0),
        "ordinary call: argument order and return value");

    TestRun arrow;
    check(executeSource(arrow,
        "const multiply = (a, b) => a * b;"
        " let result = multiply(3, 4); result;"),
        "arrow call: parse and execute");
    check(readNumber(arrow, "result", 12.0),
        "arrow call: parameters and expression return");

    TestRun closures;
    check(executeSource(closures,
        "function makeAdder(x) { return function(y) { return x + y; }; }"
        " const addFive = makeAdder(5); const addEight = makeAdder(8);"
        " const fiveResult = addFive(9); const eightResult = addEight(9);"),
        "closure: parse and execute");
    check(readNumber(closures, "fiveResult", 14.0),
        "closure: first captured value survives creator return");
    check(readNumber(closures, "eightResult", 17.0),
        "closure: second closure retains independent capture");
    check(closures.result.functionsCreated == 3u &&
            closures.result.environmentsCreated >= 4u,
        "closure: bounded function and environment accounting");

    TestRun nested;
    check(executeSource(nested,
        "function inc(x) { return x + 1; }"
        " function twice(x) { return inc(inc(x)); } twice(4);"),
        "nested call: parse and execute");
    check(isNumber(nested.finalValue, 6.0),
        "nested call: return propagation through two frames");
    check(nested.result.callDepthHighWater == 2u,
        "nested call: call-depth accounting");

    TestOptions depth;
    depth.limits.maxCallDepth = 3u;
    TestRun recursive;
    check(executeSource(recursive,
        "function recurse() { return recurse(); } recurse();", depth),
        "recursive call: parse and execute");
    check(!recursive.succeeded && recursive.result.error.code ==
            PortableExecutionErrorCode::CallDepthExhausted &&
            recursive.result.callDepthHighWater == 3u,
        "recursive call: accepted depth enforced");

    TestOptions argumentCapacity;
    argumentCapacity.argumentCapacity = 1u;
    TestRun arguments;
    check(executeSource(arguments,
        "function add(a, b) { return a + b; } add(1, 2);",
        argumentCapacity), "argument scratch: parse and execute");
    check(!arguments.succeeded && arguments.result.error.code ==
            PortableExecutionErrorCode::ArgumentCapacityExhausted,
        "argument scratch: capacity reported without truncation");
}

void testBudgetsAndRecovery()
{
    TestOptions stepLimit;
    stepLimit.limits.maxExecutionSteps = 2u;
    TestRun steps;
    check(executeSource(steps, "let value = 1; value;", stepLimit),
        "step budget: parse and execute");
    check(!steps.succeeded && steps.result.error.code ==
            PortableExecutionErrorCode::StepBudgetExhausted &&
            steps.steps == 2u,
        "step budget: exact limit is enforced");

    TestRun recovery;
    AstNodeId failedRoot = kInvalidAstNodeId;
    check(recovery.parse("function fail() { return missing; } fail();",
            failedRoot), "recovery: initial parser output");
    TestOptions defaults;
    check(recovery.configure(defaults), "recovery: initial stores configure");
    PortableExecutionStorageView storage{callFrameStorage,
        kTestFrameCapacity, argumentStorage, kTestArgumentCapacity};
    PortableExecutionCore first(recovery.ast, recovery.strings,
        recovery.environments, recovery.functions, storage, defaults.limits,
        recovery.steps);
    recovery.succeeded = first.run(failedRoot, recovery.root,
        recovery.finalValue, recovery.result);
    check(!recovery.succeeded && recovery.result.error.code ==
            PortableExecutionErrorCode::MissingBinding,
        "recovery: function error propagates explicitly");

    static char combinedSource[256u];
    const char* firstText = "function fail() { return missing; } fail();";
    const char* secondText = "let recovered = 42; recovered;";
    const std::size_t firstLength = textLength(firstText);
    const std::size_t secondLength = textLength(secondText);
    for (std::size_t index = 0u; index < firstLength; ++index)
        combinedSource[index] = firstText[index];
    combinedSource[firstLength] = '\n';
    for (std::size_t index = 0u; index < secondLength; ++index)
        combinedSource[firstLength + 1u + index] = secondText[index];
    const std::size_t combinedLength = firstLength + 1u + secondLength;
    recovery.ast.setSource(SourceView(combinedSource, combinedLength));

    TokenBuffer secondTokens{secondaryTokenStorage, 128u};
    const SourceView secondSource(combinedSource + firstLength + 1u,
        secondLength);
    Lexer lexer;
    const LexerResult lexed = lexer.tokenizeInto(secondSource, secondTokens);
    check(lexed.succeeded(), "recovery: second lexer output");
    Ast secondAst(secondSource, AstStorageView{secondaryAstNodes, 256u,
        secondaryAstChildren, 256u});
    FrontendArena secondArena(secondaryParserScratch,
        sizeof(secondaryParserScratch));
    Parser parser;
    ParserError parserError;
    check(parser.parseInto(makeTokenView(secondSource, secondTokens),
            secondAst, secondArena, parserError),
        "recovery: second parser output");
    AstNodeId secondRoot = kInvalidAstNodeId;
    check(recovery.ast.appendFrom(secondAst, firstLength + 1u, 1u, secondRoot),
        "recovery: append source-stable AST");
    recovery.ast.setSource(SourceView(combinedSource, combinedLength));
    check(recovery.ast.setRoot(secondRoot), "recovery: select second program");
    PortableExecutionCore second(recovery.ast, recovery.strings,
        recovery.environments, recovery.functions, storage, defaults.limits,
        recovery.steps);
    recovery.succeeded = second.run(secondRoot, recovery.root,
        recovery.finalValue, recovery.result);
    check(recovery.succeeded && isNumber(recovery.finalValue, 42.0),
        "recovery: valid program follows runtime failure in same realm");
    check(readNumber(recovery, "recovered", 42.0),
        "recovery: second program uses same environment store");
}

void testRealmResetStaleHandles()
{
    TestRun realm;
    check(executeSource(realm,
        "function make(x) { return y => x + y; }"
        " const add = make(6); add;"),
        "realm reset: create retained closure");
    Value oldFunction = realm.finalValue;
    check(oldFunction.isFunction(), "realm reset: function handle observed");
    const RuntimeFunctionId oldId = oldFunction.functionId();

    TestRun nextRealm;
    check(executeSource(nextRealm, "1 + 2;"),
        "realm reset: repeated execution with reused buffers");
    FunctionStoreError error;
    check(nextRealm.functions.get(oldId, error) == nullptr &&
            error.code == FunctionStoreErrorCode::StaleGeneration,
        "realm reset: stale function handle rejected");
    check(isNumber(nextRealm.finalValue, 3.0),
        "realm reset: new realm execution succeeds");
}

} // namespace

int main()
{
    testBindingsAndPrimitiveExpressions();
    testBindingErrorsAndCapacity();
    testFunctionsAndClosures();
    testBudgetsAndRecovery();
    testRealmResetStaleHandles();
    if (failureCount != 0u) {
        std::printf("JS119 portable execution tests FAIL (%zu/%zu)\n",
            failureCount, checkCount);
        return 1;
    }
    std::printf("Navigator JavaScript portable execution checks: %zu/%zu passed\n",
        checkCount, checkCount);
    std::printf("Value=%zu FunctionRecord=%zu EnvironmentRecord=%zu "
        "Binding=%zu PortableCallFrame=%zu PortableExecutionCore=%zu\n",
        sizeof(Value),
        sizeof(FunctionRecord), sizeof(EnvironmentRecord), sizeof(Binding),
        sizeof(PortableCallFrame), sizeof(PortableExecutionCore));
    std::printf("Observed high-water: environments=%zu bindings=%zu strings=%zu "
        "stringBytes=%zu functions=%zu callDepth=%zu arguments=%zu\n",
        usageHighWater.environments, usageHighWater.bindings,
        usageHighWater.stringValues, usageHighWater.stringBytes,
        usageHighWater.functions, usageHighWater.callDepth,
        usageHighWater.argumentValues);
    return 0;
}
