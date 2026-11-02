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

    static void failNextArrayCreation(RuntimeContext& runtime)
    {
        runtime.failNextArrayCreationForTesting_ = true;
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
    const LexResult lexed = Lexer().tokenize(view);
    return Parser().parse(view, lexed);
}

void testLexerAndGrammar()
{
    const char* syntax =
        "function add(a,b=10,...rest){return b;}"
        "const arrow=(value=1,...items)=>value;";
    const SourceView view(syntax, std::char_traits<char>::length(syntax));
    const LexResult lexed = Lexer().tokenize(view);
    std::size_t ellipses = 0u;
    for (const Token& token : lexed.tokens)
        if (token.type == TokenType::Ellipsis) ++ellipses;
    expect(lexed.succeeded() && ellipses == 2u && parse(syntax).succeeded(),
        "lexer and parser recognize bounded defaults and final rest parameters");
    expect(parse("function legacy(a,a){return a;}").succeeded(),
        "ordinary duplicate simple parameters preserve their legacy grammar");

    const char* invalid[] = {
        "function f(...values,next){}",
        "function f(...values=[]){}",
        "function f(...values,){}",
        "function f(...a,...b){}",
        "function f(a=1,a){}",
        "const f=(a=1,a)=>a;",
        "const f=(...a,)=>a;",
        "const f=(...a=[])=>a;",
        "const f=(...a,b)=>a;",
        "var f=function(...a,...b){};",
        "function f(a=1){return a;} var x=f(...[1]);",
        "var values=[...source];",
        "var value={...source};"
    };
    bool rejected = true;
    for (const char* source : invalid)
        rejected = rejected && !parse(source).succeeded();
    expect(rejected,
        "malformed rest, prohibited duplicate non-simple names, and spread forms are rejected");

    std::string maximum = "const f=(";
    for (std::size_t index = 0;
        index < kDefaultMaxJavaScriptFunctionParameters; ++index) {
        if (index != 0u) maximum += ',';
        maximum += "p" + std::to_string(index);
    }
    maximum += "=0)=>0;";
    expect(parse(maximum.c_str()).succeeded(),
        "arrow parameter lookahead accepts the configured parameter limit");
    maximum.insert(maximum.find("=0"), ",extra");
    expect(!parse(maximum.c_str()).succeeded(),
        "parameter lists beyond the configured limit fail during parsing");

    RuntimeContext noPartialExecution;
    const ScriptResult invalidScript = run(noPartialExecution,
        "var shouldNotRun=0;function bad(...items,){}shouldNotRun=1;");
    expect(invalidScript.status == ScriptStatus::ParseFailure &&
        binding(noPartialExecution, "shouldNotRun") == nullptr,
        "invalid parameter syntax is rejected before script execution");
}

void testDefaultsAndEvaluationOrder()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "function add(a,b=10){return a+b;}"
        "const multiply=(a,b=2)=>a*b;"
        "const increment=(value=1)=>value+1;"
        "function valueOrDefault(x=10){return x;}"
        "function calculate(a=2,b=a*3){return b;}"
        "var addOmitted=add(5);var addGiven=add(5,20);"
        "var multiplied=multiply(3);var incremented=increment();"
        "var omitted=valueOrDefault();"
        "var falsy=valueOrDefault(undefined)===10&&"
        "valueOrDefault(null)===null&&valueOrDefault(false)===false&&"
        "valueOrDefault(0)===0&&valueOrDefault('')==='';"
        "var earlier=calculate()===6&&calculate(4)===12&&"
        "calculate(4,20)===20;"
        "var expression=(function(x=8){return x+1;})();");
    expect(result.succeeded() && numberIs(runtime, "addOmitted", 15.0) &&
        numberIs(runtime, "addGiven", 25.0) &&
        numberIs(runtime, "multiplied", 6.0) &&
        numberIs(runtime, "incremented", 2.0) &&
        numberIs(runtime, "omitted", 10.0) && booleanIs(runtime, "falsy") &&
        booleanIs(runtime, "earlier") &&
        numberIs(runtime, "expression", 9.0),
        "ordinary, arrow, anonymous, earlier-parameter, and falsy-value defaults work");

    RuntimeContext lazy;
    const ScriptResult lazyResult = run(lazy,
        "let calls=0;function next(){calls=calls+1;return calls;}"
        "function choose(value=next()){return value;}"
        "var provided=choose(100);var afterProvided=calls;"
        "var first=choose();var second=choose();"
        "function sequence(a=next(),b=next()){return a*10+b;}"
        "var ordered=sequence();var afterOrdered=calls;"
        "var partlyGiven=sequence(9);"
        "var correct=provided===100&&afterProvided===0&&first===1&&"
        "second===2&&ordered===34&&afterOrdered===4&&partlyGiven===95&&calls===5;");
    expect(lazyResult.succeeded() && booleanIs(lazy, "correct"),
        "defaults run lazily at invocation time in left-to-right order");

    RuntimeContext fresh;
    const ScriptResult freshResult = run(fresh,
        "function makeObject(value={count:0}){return value;}"
        "function makeArray(value=[]){return value;}"
        "var objectA=makeObject();var objectB=makeObject();objectA.count=9;"
        "var arrayA=makeArray();var arrayB=makeArray();arrayA.push(7);"
        "var independent=objectA!==objectB&&objectB.count===0&&"
        "arrayA!==arrayB&&arrayA.length===1&&arrayB.length===0;");
    expect(freshResult.succeeded() && booleanIs(fresh, "independent"),
        "object and Array initializers allocate fresh values on every call");
}

void testParameterScopesAndTdz()
{
    RuntimeContext later;
    const ScriptResult laterResult = run(later,
        "var b=100;function invalid(a=b,b=10){return a;}invalid();");
    expect(!laterResult.succeeded() &&
        laterResult.runtimeError.code == RuntimeErrorCode::UninitializedBinding &&
        later.activeCallFrameCount() == 0u,
        "later parameters shadow outer names in the TDZ and unwind the frame");

    RuntimeContext bodyHidden;
    const ScriptResult bodyResult = run(bodyHidden,
        "function test(value=hidden){var hidden=20;return value;}test();");
    expect(!bodyResult.succeeded() &&
        bodyResult.runtimeError.code == RuntimeErrorCode::UnknownIdentifier &&
        bodyHidden.activeCallFrameCount() == 0u,
        "default initializers cannot see function-body var bindings");

    RuntimeContext outerVisible;
    const ScriptResult outerResult = run(outerVisible,
        "var hidden=7;function test(value=hidden){var hidden=20;return value;}"
        "var result=test();");
    expect(outerResult.succeeded() && numberIs(outerVisible, "result", 7.0),
        "parameter initializers can resolve a valid outer binding past body var scope");

    RuntimeContext bodyParameter;
    const ScriptResult parameterVar = run(bodyParameter,
        "function f(value=1){var value;value=value+1;return value;}"
        "var result=f();");
    expect(parameterVar.succeeded() && numberIs(bodyParameter, "result", 2.0),
        "body var declarations with parameter names keep the parameter binding");

    RuntimeContext closureScopes;
    const ScriptResult closureResult = run(closureScopes,
        "function make(value=1,read=()=>value){var hidden=20;value=9;return read;}"
        "var saved=make();var observed=saved();"
        "function makeBody(value=()=>hidden){var hidden=30;return value;}"
        "var bodyClosure=makeBody();");
    expect(closureResult.succeeded() && numberIs(closureScopes, "observed", 9.0),
        "closures from default expressions retain parameter bindings after return");
    const ScriptResult bodyClosureResult = runSameRealm(closureScopes,
        "bodyClosure();");
    expect(!bodyClosureResult.succeeded() &&
        bodyClosureResult.runtimeError.code == RuntimeErrorCode::UnknownIdentifier &&
        closureScopes.activeCallFrameCount() == 0u,
        "closures created by defaults do not capture body-only declarations");
}

void testRestAndCombinedParameters()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "function collect(...values){return values;}"
        "function select(first,...remaining){return remaining;}"
        "function process(multiplier=2,...values){"
        "return values.map(value=>value*multiplier);}"
        "const collectArrow=(...values)=>values;"
        "const pair=(first,...others)=>{return others;};"
        "var empty=collect();var one=collectArrow(5);"
        "var many=select(10,20,30,40);"
        "var mapped=process(3,4,5);var defaulted=process(undefined,6,7);"
        "var noRemaining=pair(1);"
        "var identity={value:4};var references=collect(identity,[8]);"
        "var preserve=references[0]===identity&&references[1][0]===8;"
        "var changed=collect(1,2);changed[0]=99;"
        "var unaffected=changed[0]===99&&changed[1]===2;"
        "var nested=(base=2)=>((value=base)=>value);"
        "var nestedResult=nested()();"
        "var callbackDefault=[undefined].map((value=11)=>value)[0];"
        "function makeRest(...values){return ()=>values;}"
        "var restClosure=makeRest(identity,9);var retained=restClosure();"
        "var retainedValues=retained[0]===identity&&retained[1]===9;");
    expect(result.succeeded() && binding(runtime, "empty") != nullptr &&
        binding(runtime, "empty")->isObject() &&
        binding(runtime, "one") != nullptr &&
        binding(runtime, "many") != nullptr &&
        binding(runtime, "noRemaining") != nullptr &&
        binding(runtime, "noRemaining")->isObject() &&
        booleanIs(runtime, "preserve") && booleanIs(runtime, "unaffected") &&
        numberIs(runtime, "nestedResult", 2.0) &&
        numberIs(runtime, "callbackDefault", 11.0) &&
        booleanIs(runtime, "retainedValues"),
        "native rest Arrays, defaults before rest, nested callbacks, and closures work");
    Value length;
    RuntimeErrorCode propertyError = RuntimeErrorCode::None;
    const Value* many = binding(runtime, "many");
    const bool lengthRead = many != nullptr && many->isObject() &&
        runtime.readPropertyForTesting(many->objectId(), "length", length,
            propertyError);
    expect(lengthRead && length.isNumber() && length.numberValue() == 3.0 &&
        propertyError == RuntimeErrorCode::None &&
        runtime.arrayElementCount() >= 10u,
        "rest Array lengths and bounded element storage reflect supplied arguments");
    const Value* mapped = binding(runtime, "mapped");
    const Value* defaulted = binding(runtime, "defaulted");
    Value firstMapped;
    Value secondMapped;
    Value firstDefaulted;
    const bool mapValues = mapped != nullptr && mapped->isObject() &&
        runtime.readPropertyForTesting(mapped->objectId(), "0", firstMapped,
            propertyError) &&
        runtime.readPropertyForTesting(mapped->objectId(), "1", secondMapped,
            propertyError) && defaulted != nullptr && defaulted->isObject() &&
        runtime.readPropertyForTesting(defaulted->objectId(), "0",
            firstDefaulted, propertyError);
    expect(mapValues && firstMapped.isNumber() && firstMapped.numberValue() == 12.0 &&
        secondMapped.isNumber() && secondMapped.numberValue() == 15.0 &&
        firstDefaulted.isNumber() && firstDefaulted.numberValue() == 12.0,
        "rest values pass through the existing Array.map method with defaulted prefixes");
}

void testThisAndClosures()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var object={base:10,make:function(){return (value=this.base)=>value;}};"
        "var arrow=object.make();"
        "var arrowResult=[undefined].map(arrow,{base:200})[0];"
        "var ordinaryResult=[undefined].map(function(value=this.base){return value;},"
        "{base:20})[0];"
        "function makeMutable(value=1){const read=()=>value;value=9;return read;}"
        "var mutableClosure=makeMutable();var mutableResult=mutableClosure();"
        "function makeObjectClosure(value={score:3}){return ()=>value;}"
        "var objectClosure=makeObjectClosure();var capturedObject=objectClosure();"
        "var closureIdentity=capturedObject.score===3;");
    expect(result.succeeded() && numberIs(runtime, "arrowResult", 10.0) &&
        numberIs(runtime, "ordinaryResult", 20.0) &&
        numberIs(runtime, "mutableResult", 9.0) &&
        booleanIs(runtime, "closureIdentity"),
        "arrow defaults retain lexical this while ordinary defaults use invocation this");
}

void testCapacityAndUnwinding()
{
    RuntimeContext allocation;
    const ScriptResult declaration = run(allocation,
        "function collect(...values){return values;}");
    const std::size_t objectsBefore = allocation.objectCount();
    const std::size_t slotsBefore = allocation.arrayElementCount();
    NavigatorScriptRuntimeTestAccess::failNextArrayCreation(allocation);
    const ScriptResult allocationFailure = runSameRealm(allocation,
        "var unpublished=collect(1,2);");
    expect(declaration.succeeded() && !allocationFailure.succeeded() &&
        allocationFailure.runtimeError.code == RuntimeErrorCode::AllocationFailure &&
        allocation.objectCount() == objectsBefore &&
        allocation.arrayElementCount() == slotsBefore &&
        binding(allocation, "unpublished") != nullptr &&
        binding(allocation, "unpublished")->isUndefined() &&
        allocation.activeCallFrameCount() == 0u,
        "failed rest Array creation publishes no object, slots, or binding");
    RuntimeLimits arrayLimit;
    arrayLimit.maxTotalArrayElements = 1u;
    RuntimeContext boundedArray(arrayLimit);
    const ScriptResult arrayDeclaration = run(boundedArray,
        "function collect(...values){return values;}");
    const std::size_t boundedObjects = boundedArray.objectCount();
    const std::size_t boundedSlots = boundedArray.arrayElementCount();
    const ScriptResult arrayFailure = runSameRealm(boundedArray,
        "var unpublished=collect(1,2);");
    expect(arrayDeclaration.succeeded() && !arrayFailure.succeeded() &&
        arrayFailure.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        boundedArray.objectCount() == boundedObjects &&
        boundedArray.arrayElementCount() == boundedSlots &&
        boundedArray.activeCallFrameCount() == 0u,
        "rest allocation respects the existing aggregate Array element bound");

    RuntimeLimits argumentLimit;
    argumentLimit.parser.maxCallArguments = 2u;
    RuntimeContext argumentBounded(argumentLimit);
    const ScriptResult argumentDeclaration = run(argumentBounded,
        "function collect(...values){return values;}");
    const std::size_t argumentObjectsBefore = argumentBounded.objectCount();
    const Value* collectFunction = binding(argumentBounded, "collect");
    std::vector<Value> tooMany{Value::number(1), Value::number(2),
        Value::number(3)};
    Value ignored;
    RuntimeErrorCode argumentError = RuntimeErrorCode::None;
    const bool callSucceeded = collectFunction != nullptr &&
        argumentBounded.invokeFunctionForTesting(*collectFunction, tooMany,
            ignored, argumentError);
    expect(argumentDeclaration.succeeded() && !callSucceeded &&
        argumentError == RuntimeErrorCode::TooManyArguments &&
        argumentBounded.objectCount() == argumentObjectsBefore &&
        argumentBounded.activeCallFrameCount() == 0u,
        "host-side invocation enforces the configured call argument cap without truncation");

    RuntimeLimits environmentLimit;
    environmentLimit.maxEnvironments = 2u;
    RuntimeContext noBodyEnvironment(environmentLimit);
    const ScriptResult setup = run(noBodyEnvironment,
        "let calls=0;function needsBody(value=(calls=calls+1)){return value;}");
    const ScriptResult environmentFailure = runSameRealm(noBodyEnvironment,
        "needsBody();");
    expect(setup.succeeded() && !environmentFailure.succeeded() &&
        environmentFailure.runtimeError.code ==
            RuntimeErrorCode::EnvironmentLimitExceeded &&
        numberIs(noBodyEnvironment, "calls", 1.0) &&
        noBodyEnvironment.activeCallFrameCount() == 0u,
        "separate parameter/body environments respect the retained environment limit and unwind");
    RuntimeLimits bindingLimit;
    bindingLimit.maxFunctionEnvironmentBindings = 1u;
    RuntimeContext parameterBindings(bindingLimit);
    const ScriptResult bindingSetup = run(parameterBindings,
        "function two(a=1,b=2){return a;}");
    const ScriptResult bindingFailure = runSameRealm(parameterBindings,
        "two();");
    expect(bindingSetup.succeeded() && !bindingFailure.succeeded() &&
        bindingFailure.runtimeError.code == RuntimeErrorCode::BindingLimitExceeded &&
        parameterBindings.activeCallFrameCount() == 0u,
        "parameter TDZ binding creation respects per-environment capacity");
    RuntimeLimits shallow;
    shallow.maxCallDepth = 1u;
    RuntimeContext nestedDefault(shallow);
    const ScriptResult nestedFailure = run(nestedDefault,
        "function inner(value=1){return value;}"
        "function outer(value=inner()){return value;}outer();");
    expect(!nestedFailure.succeeded() &&
        nestedFailure.runtimeError.code == RuntimeErrorCode::CallDepthExceeded &&
        nestedDefault.activeCallFrameCount() == 0u,
        "nested default invocation obeys call-depth limits and unwinds both frames");

    RuntimeContext initializerError;
    const ScriptResult defaultError = run(initializerError,
        "function fails(value=missing()){return value;}fails();");
    expect(!defaultError.succeeded() &&
        defaultError.runtimeError.code == RuntimeErrorCode::UnknownIdentifier &&
        initializerError.activeCallFrameCount() == 0u,
        "runtime errors raised by a default initializer unwind the call frame");

    RuntimeLimits shortBudget;
    shortBudget.maxExecutionSteps = 80u;
    RuntimeContext budget(shortBudget);
    const ScriptResult budgetFailure = run(budget,
        "function spin(){while(true){;}}"
        "function apply(value=spin()){return value;}apply();");
    expect(budgetFailure.status == ScriptStatus::ExecutionBudgetExceeded &&
        budget.activeCallFrameCount() == 0u,
        "default evaluation cannot bypass the shared execution-step budget");

    RuntimeContext callBoundary;
    const ScriptResult boundaryDefinition = run(callBoundary,
        "function count(...values){return values.length;}");
    std::string call = "var countResult=count(";
    for (std::size_t index = 0; index < 64u; ++index) {
        if (index != 0u) call += ',';
        call += std::to_string(index);
    }
    call += ");";
    const ScriptResult boundaryResult = runSameRealm(callBoundary, call.c_str());
    expect(boundaryDefinition.succeeded() && boundaryResult.succeeded() &&
        numberIs(callBoundary, "countResult", 64.0),
        "the configured 64-argument boundary is fully collected");
    call.insert(call.find(");"), ",64");
    const ScriptResult overBoundary = run(callBoundary, call.c_str());
    expect(overBoundary.status == ScriptStatus::ParseFailure &&
        overBoundary.parserError.code == ParserErrorCode::TooManyArguments,
        "the first argument beyond the configured bound fails instead of truncating");
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
    std::ifstream input("navigator-smoke/javascript-js105.html",
        std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///javascript-js105.html", html, loadError);
    const auto& scripts = harness.document().scriptSources;
    const ScriptResult executed = loaded && scripts.size() == 1u
        ? harness.execute(scripts[0]) : ScriptResult();
    settleMutation(harness);
    expect(loaded && loadError == RuntimeErrorCode::None &&
        executed.succeeded() &&
        textById(harness, serialById(harness, "result")) == "6, 12, 18",
        "hosted JS105 default/rest map renders the requested result");
    expect(textById(harness, serialById(harness, "captured-result")) == "7" &&
        textById(harness, serialById(harness, "this-result")) == "10, 20" &&
        textById(harness, serialById(harness, "independent-result")) ==
            "2:9:1:3",
        "hosted defaults use captured state and correct this while rest Arrays stay independent");

    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = harness.dispatchClick(
        serialById(harness, "event-button"), eventError);
    settleMutation(harness);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        textById(harness, serialById(harness, "event-result")) == "1:click",
        "hosted Event callback receives a native rest Array with the Event value");
}

} // namespace

int main()
{
    testLexerAndGrammar();
    testDefaultsAndEvaluationOrder();
    testParameterScopesAndTdz();
    testRestAndCombinedParameters();
    testThisAndClosures();
    testCapacityAndUnwinding();
    testHostedFixture();

    std::cout << "JS105 record sizes: AstNode=" << sizeof(AstNode)
        << " Binding=" << sizeof(Binding) << " Environment="
        << sizeof(Environment) << " FunctionRecord="
        << NavigatorScriptRuntimeTestAccess::functionRecordSize()
        << " RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::runtimeObjectSize()
        << " RuntimeArrayElement="
        << NavigatorScriptRuntimeTestAccess::arrayElementSize() << '\n';
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS105 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS105 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
