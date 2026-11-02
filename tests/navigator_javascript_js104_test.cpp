#include "navigator_javascript/navigator_script_host.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptRuntimeTestAccess {
    static std::size_t functionRecordSize()
    {
        return sizeof(RuntimeContext::FunctionRecord);
    }

    static std::size_t runtimeObjectSize()
    {
        return sizeof(RuntimeContext::RuntimeObject);
    }

    static std::size_t arrayElementSize()
    {
        return sizeof(RuntimeContext::RuntimeArrayElement);
    }
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

std::size_t checks = 0u;
std::size_t failures = 0u;

void expect(bool condition, const char* label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "  FAIL: " << label << '\n';
}

const Value* binding(const RuntimeContext& runtime, const char* name)
{
    return runtime.lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

bool numberIs(const RuntimeContext& runtime, const char* name, double expected)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isNumber() &&
        value->numberValue() == expected;
}

bool booleanIs(const RuntimeContext& runtime, const char* name,
    bool expected = true)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isBoolean() &&
        value->booleanValue() == expected;
}

bool stringIs(const RuntimeContext& runtime, const char* name,
    const std::string& expected)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isString() &&
        runtime.stringValue(*value) == expected;
}

ScriptResult run(RuntimeContext& runtime, const char* source)
{
    return runtime.execute(SourceView(source,
        std::char_traits<char>::length(source)));
}

ScriptResult runSameRealm(RuntimeContext& runtime, const char* source)
{
    return runtime.executeInSameRealm(SourceView(source,
        std::char_traits<char>::length(source)));
}

ParseResult parse(const char* source)
{
    const SourceView view(source, std::char_traits<char>::length(source));
    Lexer lexer;
    const LexResult lexed = lexer.tokenize(view);
    Parser parser;
    return parser.parse(view, lexed);
}

void testLexerAndParser()
{
    const char* syntax = "let x=1;const y=2;x=>x;(a,b)=>a+b;";
    const LexResult lexed = Lexer().tokenize(SourceView(syntax,
        std::char_traits<char>::length(syntax)));
    bool hasLet = false;
    bool hasConst = false;
    bool hasArrow = false;
    for (const Token& token : lexed.tokens) {
        hasLet = hasLet || token.type == TokenType::KeywordLet;
        hasConst = hasConst || token.type == TokenType::KeywordConst;
        hasArrow = hasArrow || token.type == TokenType::Arrow;
    }
    expect(lexed.succeeded() && hasLet && hasConst && hasArrow,
        "lexer exposes let, const, and one explicit arrow token");
    expect(parse(syntax).succeeded(),
        "parser accepts bounded declarations and both arrow parameter forms");
    expect(parse("var grouped=(2+3)*4;var call=(function(){return 7;})();")
        .succeeded(),
        "ordinary parenthesized expressions remain distinct from arrow parameters");
    std::string maximumParameterArrow = "const bounded=(";
    for (std::size_t index = 0; index < kDefaultMaxJavaScriptFunctionParameters;
        ++index) {
        if (index != 0u) maximumParameterArrow += ',';
        maximumParameterArrow += "p" + std::to_string(index);
    }
    maximumParameterArrow += ")=>0;";
    expect(parse(maximumParameterArrow.c_str()).succeeded(),
        "bounded arrow lookahead accepts the configured maximum parameter count");

    const char* invalid[] = {
        "const value;",
        "let value;let value;",
        "const value=1;const value=2;",
        "let value=1;const value=2;",
        "let value=1;var value=2;",
        "let value=1;{var value=2;}",
        "{let value=1;var value=2;}",
        "if(true)let value=1;",
        "const f=(x,x)=>x;",
        "const f=(x,)=>x;",
        "const f=x=>;",
        "for(let i=0;i<2;i++){;}",
    };
    bool rejected = true;
    for (const char* source : invalid)
        rejected = rejected && !parse(source).succeeded();
    expect(rejected && parse("const f=(x=1)=>x;").succeeded() &&
        parse("const f=(...x)=>x;").succeeded(),
        "early declaration, duplicate-parameter, malformed-arrow, and excluded-loop errors remain rejected while JS105 arrow defaults and rest are accepted");
    expect(parse("var value=1;{let value=2;value=3;}").succeeded() &&
        parse("var value=1;for(const value of [2]){} ").succeeded(),
        "nested lexical shadowing and for-of shadowing of an outer var remain valid");

    RuntimeContext noPartialExecution;
    const ScriptResult invalidScript = run(noPartialExecution,
        "var shouldNotRun=0;const missing;shouldNotRun=1;");
    expect(invalidScript.status == ScriptStatus::ParseFailure &&
        binding(noPartialExecution, "shouldNotRun") == nullptr,
        "early syntax failure occurs before any script binding is executed");
}

void testLexicalDeclarationsAndTdz()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "let count=0;let title='Navigator';let pending;count=count+1;"
        "const limit=100;const object={value:10};object.value=20;"
        "let outer=10;{let outer=20;outer=30;}var after=outer;"
        "function varScope(){{var nestedVar=7;}return nestedVar;}"
        "var varResult=varScope();");
    expect(result.succeeded() && numberIs(runtime, "count", 1.0) &&
        stringIs(runtime, "title", "Navigator") &&
        binding(runtime, "pending") != nullptr &&
        binding(runtime, "pending")->isUndefined() &&
        numberIs(runtime, "limit", 100.0) && numberIs(runtime, "after", 10.0) &&
        numberIs(runtime, "varResult", 7.0),
        "let and const initialize, mutate, shadow, and retain existing var scope");
    const Value* object = binding(runtime, "object");
    Value objectValue;
    RuntimeErrorCode propertyError = RuntimeErrorCode::None;
    const bool objectMutation = object != nullptr && object->isObject() &&
        runtime.readPropertyForTesting(object->objectId(), "value",
            objectValue, propertyError) && objectValue.isNumber() &&
        objectValue.numberValue() == 20.0 &&
        propertyError == RuntimeErrorCode::None;
    expect(objectMutation,
        "const protects its binding while allowing object properties to change");

    RuntimeContext constWrite;
    const ScriptResult immutable = run(constWrite,
        "const fixed=1;fixed=2;");
    expect(!immutable.succeeded() &&
        immutable.runtimeError.code == RuntimeErrorCode::TypeError,
        "assignment to an initialized const uses the established TypeError");

    RuntimeContext temporalDeadZone;
    const ScriptResult tdz = run(temporalDeadZone,
        "var value=5;{var result=value;let value=10;}");
    expect(!tdz.succeeded() &&
        tdz.runtimeError.code == RuntimeErrorCode::UninitializedBinding,
        "block bindings are instantiated before statements and shadow outer vars in the TDZ");
    RuntimeContext selfInitialization;
    const ScriptResult self = run(selfInitialization,
        "let value=value;");
    expect(!self.succeeded() &&
        self.runtimeError.code == RuntimeErrorCode::UninitializedBinding,
        "a let initializer cannot read its own uninitialized binding");
    RuntimeContext tdzAssignment;
    const ScriptResult assignment = run(tdzAssignment,
        "{value=1;let value=2;}");
    expect(!assignment.succeeded() &&
        assignment.runtimeError.code == RuntimeErrorCode::UninitializedBinding,
        "assignment to a declared but uninitialized lexical binding fails");

    RuntimeContext noGlobalCreation;
    const ScriptResult unknown = run(noGlobalCreation,
        "unrelated=1;");
    expect(!unknown.succeeded() &&
        unknown.runtimeError.code == RuntimeErrorCode::UnknownIdentifier &&
        binding(noGlobalCreation, "unrelated") == nullptr,
        "unknown assignment does not create a global binding");

    RuntimeContext lexicalThenVar;
    const ScriptResult lexicalFirst = run(lexicalThenVar,
        "let existing=3;");
    const ScriptResult varConflict = runSameRealm(lexicalThenVar,
        "var unpublished=9;var existing=4;");
    expect(lexicalFirst.succeeded() && !varConflict.succeeded() &&
        varConflict.runtimeError.code == RuntimeErrorCode::DuplicateBinding &&
        numberIs(lexicalThenVar, "existing", 3.0) &&
        binding(lexicalThenVar, "unpublished") == nullptr,
        "a later same-realm var cannot overwrite a lexical binding or partially hoist other names");

    RuntimeContext varThenLexical;
    const ScriptResult varFirst = run(varThenLexical, "var existing=5;");
    const ScriptResult lexicalConflict = runSameRealm(varThenLexical,
        "let unpublished=8;const existing=6;");
    expect(varFirst.succeeded() && !lexicalConflict.succeeded() &&
        lexicalConflict.runtimeError.code == RuntimeErrorCode::DuplicateBinding &&
        numberIs(varThenLexical, "existing", 5.0) &&
        binding(varThenLexical, "unpublished") == nullptr,
        "a later same-realm lexical declaration cannot collide with var or publish other bindings");
}

void testArrowFunctionsAndThis()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "const double=x=>x*2;const add=(a,b)=>a+b;const fortyTwo=()=>42;"
        "const sum=(a,b)=>{return a+b;};const noReturn=()=>{let local=1;};"
        "const make=outer=>inner=>outer+inner;"
        "var implicit=double(4);var multi=add(3,4);var zero=fortyTwo();"
        "var explicit=sum(8,9);var absent=noReturn()===undefined;"
        "var nested=make(10)(5);var grouped=(2+3)*4;"
        "var mapped=[1,2,3].map(x=>x*3);var callbackResult=mapped[2];"
        "var object={offset:10,makeAdder:function(){return x=>this.offset+x;}};"
        "var adder=object.makeAdder();var holder={offset:200,adder:adder};"
        "var arrowResult=holder.adder(5);var callbackContext={offset:100};"
        "var arrowCallback=[1].map(adder,callbackContext)[0];"
        "var ordinaryCallback=[1].map(function(x){return this.offset+x;},"
        "callbackContext)[0];");
    expect(result.succeeded() && numberIs(runtime, "implicit", 8.0) &&
        numberIs(runtime, "multi", 7.0) && numberIs(runtime, "zero", 42.0) &&
        numberIs(runtime, "explicit", 17.0) && booleanIs(runtime, "absent") &&
        numberIs(runtime, "nested", 15.0) && numberIs(runtime, "grouped", 20.0) &&
        numberIs(runtime, "callbackResult", 9.0),
        "arrow arity, implicit/block return, nesting, callback use, and precedence work");
    expect(numberIs(runtime, "arrowResult", 15.0) &&
        numberIs(runtime, "arrowCallback", 11.0) &&
        numberIs(runtime, "ordinaryCallback", 101.0),
        "arrows retain lexical this while traditional callbacks keep dynamic thisArg");
}

void testClosuresAndForOf()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var getShared;var setShared;var getConstant;var readOuter;"
        "var readInner;var oldFunction;{let shared=1;"
        "getShared=()=>shared;setShared=value=>shared=value;"
        "const fixed=8;getConstant=()=>fixed;"
        "readOuter=()=>shared;{let shared=3;readInner=()=>shared;}"
        "oldFunction=function(){return shared;};}"
        "var before=getShared();setShared(9);var after=getShared();"
        "var fixedValue=getConstant();var outerValue=readOuter();"
        "var innerValue=readInner();var traditionalValue=oldFunction();"
        "var callbacks=[];for(let value of [1,2,3]){"
        "callbacks.push(()=>value);}"
        "var first=callbacks[0]();var second=callbacks[1]();"
        "var third=callbacks[2]();"
        "var constCallbacks=[];for(const item of [4,5]){"
        "constCallbacks.push(()=>item);}"
        "var fourth=constCallbacks[0]();var fifth=constCallbacks[1]();");
    expect(result.succeeded() && numberIs(runtime, "before", 1.0) &&
        numberIs(runtime, "after", 9.0) && numberIs(runtime, "fixedValue", 8.0) &&
        numberIs(runtime, "outerValue", 9.0) &&
        numberIs(runtime, "innerValue", 3.0) &&
        numberIs(runtime, "traditionalValue", 9.0),
        "arrows and ordinary functions keep live lexical bindings beyond block exit");
    expect(numberIs(runtime, "first", 1.0) &&
        numberIs(runtime, "second", 2.0) && numberIs(runtime, "third", 3.0) &&
        numberIs(runtime, "fourth", 4.0) && numberIs(runtime, "fifth", 5.0),
        "let and const for-of closures retain distinct initialized iteration environments");

    RuntimeContext constIteration;
    const ScriptResult rejected = run(constIteration,
        "for(const value of [1]){value=2;}");
    expect(!rejected.succeeded() &&
        rejected.runtimeError.code == RuntimeErrorCode::TypeError,
        "for-of const iteration bindings remain immutable");
}

void testCapacityAndFailureBounds()
{
    RuntimeLimits sparseBlocks;
    sparseBlocks.maxEnvironments = 2u;
    RuntimeContext noLexicalEnvironments(sparseBlocks);
    const ScriptResult emptyBlocks = run(noLexicalEnvironments,
        "var i=0;while(i<3){{;}i++;}");
    expect(emptyBlocks.succeeded() &&
        noLexicalEnvironments.environmentCount() == 1u,
        "blocks without lexical declarations do not consume environment records");

    RuntimeContext exhaustedBlockEnvironment(sparseBlocks);
    const ScriptResult blockFailure = run(exhaustedBlockEnvironment,
        "{let first=1;}{let second=2;}");
    expect(!blockFailure.succeeded() &&
        blockFailure.runtimeError.code == RuntimeErrorCode::EnvironmentLimitExceeded &&
        exhaustedBlockEnvironment.environmentCount() == 2u,
        "sequential lexical blocks account for retained environments and fail at the configured limit");

    RuntimeLimits manyBlocks;
    manyBlocks.maxEnvironments = 16u;
    RuntimeContext exhaustedSequentialBlocks(manyBlocks);
    std::string sequentialSource;
    for (std::size_t index = 0; index < 32u; ++index)
        sequentialSource += "{let retained" + std::to_string(index) + "=" +
            std::to_string(index) + ";}";
    const ScriptResult sequentialFailure = run(exhaustedSequentialBlocks,
        sequentialSource.c_str());
    expect(!sequentialFailure.succeeded() &&
        sequentialFailure.runtimeError.code ==
            RuntimeErrorCode::EnvironmentLimitExceeded &&
        exhaustedSequentialBlocks.environmentCount() == 16u,
        "many sequential lexical blocks consume retained environments deterministically");

    RuntimeLimits nestedBlocks;
    nestedBlocks.maxEnvironments = 3u;
    RuntimeContext exhaustedNestedBlocks(nestedBlocks);
    const ScriptResult nestedFailure = run(exhaustedNestedBlocks,
        "{let outer=1;{let middle=2;{let inner=3;}}}");
    expect(!nestedFailure.succeeded() &&
        nestedFailure.runtimeError.code ==
            RuntimeErrorCode::EnvironmentLimitExceeded &&
        exhaustedNestedBlocks.environmentCount() == 3u,
        "nested lexical blocks stop at the configured environment bound");

    RuntimeLimits noFunctions;
    noFunctions.maxFunctions = 0u;
    RuntimeContext exhaustedFunctionValues(noFunctions);
    const std::size_t functionsBefore =
        exhaustedFunctionValues.functionValueCount();
    const ScriptResult functionFailure = run(exhaustedFunctionValues,
        "const callback=()=>1;");
    expect(!functionFailure.succeeded() &&
        functionFailure.runtimeError.code == RuntimeErrorCode::FunctionLimitExceeded &&
        exhaustedFunctionValues.functionValueCount() == functionsBefore,
        "function-pool exhaustion is reported before publishing an arrow closure");

    RuntimeLimits shallowCalls;
    shallowCalls.maxCallDepth = 1u;
    RuntimeContext depth(shallowCalls);
    const ScriptResult depthFailure = run(depth,
        "const recurse=n=>{if(n>0){return recurse(n-1);}return 0;};recurse(2);");
    expect(!depthFailure.succeeded() &&
        depthFailure.runtimeError.code == RuntimeErrorCode::CallDepthExceeded &&
        depth.activeCallFrameCount() == 0u,
        "recursive arrow calls preserve the bounded call-depth guard and unwind frames");

    RuntimeLimits shortExecution;
    shortExecution.maxExecutionSteps = 80u;
    RuntimeContext budget(shortExecution);
    const ScriptResult budgetFailure = run(budget,
        "const spin=()=>{while(true){;}};spin();");
    expect(budgetFailure.status == ScriptStatus::ExecutionBudgetExceeded &&
        budgetFailure.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded &&
        budget.activeCallFrameCount() == 0u,
        "arrow execution cannot bypass the shared step budget");
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const auto& element : harness.document().structuralElements)
        if (element.id == id) return element.serial;
    return 0u;
}

std::string textById(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    std::string text;
    if (!navigatorScriptElementTextContent(harness.document(), serial, text))
        return std::string();
    return text;
}

void settleMutation(NavigatorScriptExecutionHarness& harness)
{
    if (harness.documentDirty()) (void)harness.relayout();
}

void testHostedFixture()
{
    std::ifstream input("navigator-smoke/javascript-js104.html",
        std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///javascript-js104.html", html, loadError);
    const auto& scripts = harness.document().scriptSources;
    const ScriptResult executed = loaded && scripts.size() == 1u
        ? harness.execute(scripts[0]) : ScriptResult();
    settleMutation(harness);
    const std::uint64_t result = serialById(harness, "result");
    expect(loaded && loadError == RuntimeErrorCode::None &&
        executed.succeeded() && textById(harness, result) == "6, 12, 18",
        "hosted JS104 map fixture renders its result through Navigator text layout");

    const std::uint64_t thisButton = serialById(harness, "this-button");
    RuntimeErrorCode thisError = RuntimeErrorCode::None;
    const bool thisClicked = thisButton != 0u &&
        harness.dispatchClick(thisButton, thisError);
    settleMutation(harness);
    expect(thisClicked && thisError == RuntimeErrorCode::None &&
        textById(harness, serialById(harness, "this-result")) == "guideXOS",
        "hosted Event arrow retains method lexical this and renders the mutation");

    const std::uint64_t stateButton = serialById(harness, "state-button");
    RuntimeErrorCode stateError = RuntimeErrorCode::None;
    const bool firstClick = stateButton != 0u &&
        harness.dispatchClick(stateButton, stateError);
    settleMutation(harness);
    const bool secondClick = stateButton != 0u &&
        harness.dispatchClick(stateButton, stateError);
    settleMutation(harness);
    expect(firstClick && secondClick && stateError == RuntimeErrorCode::None &&
        textById(harness, serialById(harness, "state-result")) == "clicks=2",
        "hosted Event arrow observes captured mutable let state across clicks");
}

} // namespace

int main()
{
    testLexerAndParser();
    testLexicalDeclarationsAndTdz();
    testArrowFunctionsAndThis();
    testClosuresAndForOf();
    testCapacityAndFailureBounds();
    testHostedFixture();
    std::cout << "JS104 record sizes: AstNode=" << sizeof(AstNode)
        << " Binding=" << sizeof(Binding) << " Environment="
        << sizeof(Environment) << " FunctionRecord="
        << NavigatorScriptRuntimeTestAccess::functionRecordSize()
        << " RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::runtimeObjectSize()
        << " RuntimeArrayElement="
        << NavigatorScriptRuntimeTestAccess::arrayElementSize() << '\n';
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS104 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS104 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
