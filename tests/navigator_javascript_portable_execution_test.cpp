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
constexpr std::size_t kTestObjectCapacity = 1024u;
constexpr std::size_t kTestPropertyCapacity = 4096u;
constexpr std::size_t kTestArrayElementCapacity = 4096u;
constexpr std::size_t kTestPropertyNameBytes = 256u * 1024u;

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
PortableObjectRecord objectRecordStorage[kTestObjectCapacity];
PortablePropertyRecord portablePropertyStorage[kTestPropertyCapacity];
PortableArrayElementRecord portableArrayElementStorage[
    kTestArrayElementCapacity];
char portablePropertyNameStorage[kTestPropertyNameBytes];

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
    std::size_t objects = 0u;
    std::size_t properties = 0u;
    std::size_t propertyNameBytes = 0u;
    std::size_t arrayElements = 0u;
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
    PortableObjectLimits objectLimits;
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
    PortableObjectStore objects;
    EnvironmentId root = kInvalidEnvironmentId;
    PortableExecutionResult result;
    Value finalValue = Value::undefined();
    std::size_t steps = 0u;
    RuntimeGeneration generation = kInvalidRuntimeGeneration;
    bool configured = false;

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
        PortableObjectError objectError;
        if (!objects.configure(PortableObjectStorageView{
                options.objectLimits.maxObjects == 0u ? nullptr
                    : objectRecordStorage,
                options.objectLimits.maxObjects,
                options.objectLimits.maxProperties == 0u ? nullptr
                    : portablePropertyStorage,
                options.objectLimits.maxProperties,
                options.objectLimits.maxTotalArrayElements == 0u ? nullptr
                    : portableArrayElementStorage,
                options.objectLimits.maxTotalArrayElements,
                options.objectLimits.maxPropertyNameBytes == 0u ? nullptr
                    : portablePropertyNameStorage,
                options.objectLimits.maxPropertyNameBytes},
                options.objectLimits, generation, objectError)) return false;
        if (!environments.createRoot(EnvironmentLimits{256u, 256u}, root,
                environmentError)) return false;
        configured = true;
        return true;
    }

    bool execute(AstNodeId rootNode, const TestOptions& options,
        EnvironmentId environment = kInvalidEnvironmentId)
    {
        if (!configured && !configure(options)) return false;
        if (environment == kInvalidEnvironmentId) environment = root;
        const PortableExecutionStorageView storage{
            options.frameCapacity == 0u ? nullptr : callFrameStorage,
            options.frameCapacity,
            options.argumentCapacity == 0u ? nullptr : argumentStorage,
            options.argumentCapacity};
        PortableObjectExecutionCore core(ast, strings, environments, functions,
            objects, storage, options.limits, steps);
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
        if (objects.objectCount() > usageHighWater.objects)
            usageHighWater.objects = objects.objectCount();
        if (objects.propertyCount() > usageHighWater.properties)
            usageHighWater.properties = objects.propertyCount();
        if (objects.propertyNameBytesUsed() >
                usageHighWater.propertyNameBytes)
            usageHighWater.propertyNameBytes =
                objects.propertyNameBytesUsed();
        if (objects.arrayElementCount() > usageHighWater.arrayElements)
            usageHighWater.arrayElements = objects.arrayElementCount();
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

void testObjectsAndArrays()
{
    TestRun person;
    check(executeSource(person,
        "const person = { name: 'Navigator', version: 120 };"
        " const result = person.version; result;"),
        "object literal and named property: parse and execute");
    check(isNumber(person.finalValue, 120.0),
        "object literal and named property: reads version 120");
    Value personValue;
    bool found = false;
    PortableObjectError objectError;
    check(person.read("person", personValue) && personValue.isObject() &&
            person.objects.getOwnProperty(personValue.objectId(),
                SourceView("name", 4u), person.finalValue, found,
                objectError) && found && person.finalValue.isString(),
        "object property: copied property name and value handle retained");

    TestRun write;
    check(executeSource(write,
        "const state = {}; state.value = 15; state.value;"),
        "object property write: parse and execute");
    check(isNumber(write.finalValue, 15.0),
        "object property write: assignment result and later read");

    TestRun array;
    check(executeSource(array,
        "const numbers = [4, 8, 12]; numbers[1] = 10;"
        " numbers[0] + numbers[1] + numbers[2];"),
        "Array literal and indexed mutation: parse and execute");
    check(isNumber(array.finalValue, 26.0),
        "Array literal and indexed mutation: result is 26");

    TestRun interactions;
    check(executeSource(interactions,
        "const key = 'value'; const state = { value: 7 };"
        " function read(target) { return target[key]; }"
        " read(state);"),
        "computed key and object argument: parse and execute");
    check(isNumber(interactions.finalValue, 7.0),
        "computed key and object argument: function reads same object");

    TestRun closure;
    check(executeSource(closure,
        "function make() { const saved = { value: 9 };"
        " return () => saved.value; } const reader = make(); reader();"),
        "object captured by closure: parse and execute");
    check(isNumber(closure.finalValue, 9.0),
        "object captured by closure: identity survives creator return");

    TestRun arrayArgument;
    check(executeSource(arrayArgument,
        "function second(items) { return items[1]; }"
        " second([3, 5]);"),
        "Array passed to function: parse and execute");
    check(isNumber(arrayArgument.finalValue, 5.0),
        "Array passed to function: indexed value is retained");

    TestRun returnedObject;
    check(executeSource(returnedObject,
        "function makeObject() { return { value: 6 }; }"
        " makeObject().value;"),
        "function returning object: parse and execute");
    check(isNumber(returnedObject.finalValue, 6.0),
        "function returning object: member read uses returned identity");

    TestRun returnedArray;
    check(executeSource(returnedArray,
        "function makeArray() { return [4, 5]; } makeArray()[1];"),
        "function returning Array: parse and execute");
    check(isNumber(returnedArray.finalValue, 5.0),
        "function returning Array: computed read");

    TestRun identity;
    check(executeSource(identity,
        "const first = {}; const second = first; first === second;"),
        "object identity: parse and execute");
    check(identity.finalValue.isBoolean() && identity.finalValue.booleanValue(),
        "object identity: copied Values compare by handle");

    TestRun safeProto;
    check(executeSource(safeProto,
        "const safe = { '__proto__': 5 }; safe.__proto__;"),
        "__proto__ property: parse and execute as data");
    check(isNumber(safeProto.finalValue, 5.0),
        "__proto__ property: preserved as an own data property");

    TestRun assignmentOrder;
    check(executeSource(assignmentOrder,
        "let order = 0; const target = {};"
        " function receiver() { order = order * 10 + 1; return target; }"
        " function key() { order = order * 10 + 2; return 0; }"
        " function right() { order = order * 10 + 3; return 9; }"
        " receiver()[key()] = right(); order;"),
        "property assignment order: parse and execute");
    check(isNumber(assignmentOrder.finalValue, 123.0),
        "property assignment order: receiver and key evaluate once before RHS");

    TestRun sparse;
    check(executeSource(sparse,
        "const values = []; values[2] = 9; values[1] = undefined;"
        " const object = { '': 1, alpha: 2 }; object.alpha;"),
        "sparse Array and empty key: parse and execute");
    check(isNumber(sparse.finalValue, 2.0),
        "empty key and insertion order: named value read");
    Value valuesValue;
    Value objectValue;
    std::size_t arrayLength = 0u;
    bool own0 = true;
    bool own1 = false;
    Value element;
    check(sparse.read("values", valuesValue) && valuesValue.isObject() &&
            sparse.objects.arrayLength(valuesValue.objectId(), arrayLength,
                objectError) && arrayLength == 3u &&
            sparse.objects.hasOwnArrayElement(valuesValue.objectId(), 0u,
                own0, objectError) && !own0 &&
            sparse.objects.getOwnArrayElement(valuesValue.objectId(), 1u,
                element, own1, objectError) && own1 && element.isUndefined(),
        "sparse Array: hole differs from explicit undefined and length is three");
    PortablePropertyEntry arrayEntry;
    check(sparse.objects.getOwnPropertyAt(valuesValue.objectId(), 0u,
            arrayEntry, found, objectError) && found &&
            arrayEntry.isArrayElement && arrayEntry.arrayIndex == 1u &&
            sparse.objects.getOwnPropertyAt(valuesValue.objectId(), 1u,
                arrayEntry, found, objectError) && found &&
            arrayEntry.isArrayElement && arrayEntry.arrayIndex == 2u &&
            sparse.objects.getOwnPropertyAt(valuesValue.objectId(), 2u,
                arrayEntry, found, objectError) && found &&
            arrayEntry.isArrayLength && arrayEntry.attributes == 0x01u,
        "Array own-key enumeration: sorted indices precede non-enumerable length");
    bool deleted = false;
    check(sparse.objects.deleteArrayElement(valuesValue.objectId(), 2u,
            deleted, objectError) && deleted &&
            sparse.objects.arrayLength(valuesValue.objectId(), arrayLength,
                objectError) && arrayLength == 3u &&
            sparse.objects.hasOwnArrayElement(valuesValue.objectId(), 2u,
                own0, objectError) && !own0,
        "Array deletion: frees slot while preserving logical length");
    check(sparse.objects.setArrayElement(valuesValue.objectId(), 2u,
            Value::number(11.0), objectError) &&
            sparse.objects.getOwnArrayElement(valuesValue.objectId(), 2u,
                element, own0, objectError) && own0 &&
            isNumber(element, 11.0),
        "Array deletion: slot can be reassigned");
    check(sparse.objects.setOwnProperty(valuesValue.objectId(),
            SourceView("length", 6u), Value::number(1.0), objectError) &&
            sparse.objects.arrayLength(valuesValue.objectId(), arrayLength,
                objectError) && arrayLength == 1u &&
            sparse.objects.setArrayLength(valuesValue.objectId(), 3u,
                objectError) &&
            sparse.objects.arrayLength(valuesValue.objectId(), arrayLength,
                objectError) && arrayLength == 3u &&
            sparse.objects.hasOwnArrayElement(valuesValue.objectId(), 1u,
                own0, objectError) && !own0,
        "Array length mutation: shrink deletes slots and expansion creates holes");

    check(sparse.read("object", objectValue) && objectValue.isObject() &&
            sparse.objects.hasOwnProperty(objectValue.objectId(),
                SourceView("", 0u), found, objectError) && found &&
            sparse.objects.deleteOwnProperty(objectValue.objectId(),
                SourceView("", 0u), deleted, objectError) && deleted &&
            sparse.objects.hasOwnProperty(objectValue.objectId(),
                SourceView("", 0u), found, objectError) && !found,
        "own-property existence and deletion: empty property name");
    PortablePropertyEntry propertyEntry;
    check(sparse.objects.getOwnPropertyAt(objectValue.objectId(), 0u,
            propertyEntry, found, objectError) && found &&
            propertyEntry.key.length == 5u &&
            propertyEntry.key.data[0] == 'a' &&
            propertyEntry.key.data[4] == 'a',
        "property enumeration: deleting first entry preserves next name");

    const std::size_t nameBytesBefore =
        sparse.objects.propertyNameBytesUsed();
    char temporaryName[] = {'r', 'e', 't', 'a', 'i', 'n'};
    check(sparse.objects.setOwnProperty(objectValue.objectId(),
            SourceView(temporaryName, sizeof(temporaryName)),
            Value::number(13.0), objectError),
        "property name ownership: insert from mutable source");
    for (char& character : temporaryName) character = 'x';
    check(sparse.objects.getOwnProperty(objectValue.objectId(),
            SourceView("retain", 6u), element, found, objectError) && found &&
            isNumber(element, 13.0) &&
            sparse.objects.propertyNameBytesUsed() == nameBytesBefore + 6u,
        "property name ownership: bytes survive source-buffer replacement");
    char maximumName[256];
    for (char& character : maximumName) character = 'n';
    check(sparse.objects.setOwnProperty(objectValue.objectId(),
            SourceView(maximumName, sizeof(maximumName)),
            Value::number(1.0), objectError) &&
            sparse.objects.getOwnProperty(objectValue.objectId(),
                SourceView(maximumName, sizeof(maximumName)), element,
                found, objectError) && found && isNumber(element, 1.0),
        "property name limit: longest accepted name is copied and readable");

    check(sparse.objects.setOwnProperty(objectValue.objectId(),
            SourceView("locked", 6u), Value::number(1.0), objectError,
            0x03u) &&
            sparse.objects.deleteOwnProperty(objectValue.objectId(),
                SourceView("locked", 6u), deleted, objectError) && !deleted,
        "property deletion: non-configurable data stays own");
    check(sparse.objects.setOwnProperty(objectValue.objectId(),
            SourceView("retain", 6u), Value::number(14.0), objectError) &&
            sparse.objects.getOwnProperty(objectValue.objectId(),
                SourceView("retain", 6u), element, found, objectError) &&
            found && isNumber(element, 14.0),
        "duplicate property assignment: update preserves one owned key");
    check(sparse.objects.setOwnProperty(objectValue.objectId(),
            SourceView("explicitUndefined", 17u), Value::undefined(),
            objectError) &&
            sparse.objects.getOwnProperty(objectValue.objectId(),
                SourceView("explicitUndefined", 17u), element, found,
                objectError) && found && element.isUndefined() &&
            sparse.objects.getOwnProperty(objectValue.objectId(),
                SourceView("missing", 7u), element, found, objectError) &&
            !found && element.isUndefined(),
        "object lookup: missing differs from an own undefined value");

    RuntimeObjectId prototype = kInvalidRuntimeObjectId;
    RuntimeObjectId child = kInvalidRuntimeObjectId;
    check(sparse.objects.createObject(kInvalidRuntimeObjectId, prototype,
            objectError) &&
            sparse.objects.setOwnProperty(prototype, SourceView("4", 1u),
                Value::number(44.0), objectError) &&
            sparse.objects.createObject(prototype, child, objectError) &&
            sparse.objects.getProperty(child, SourceView("4", 1u), element,
                found, objectError) && found && isNumber(element, 44.0) &&
            sparse.objects.hasOwnProperty(child, SourceView("4", 1u), found,
                objectError) && !found,
        "prototype lookup: inherited numeric-looking key stays inherited");
    check(!sparse.objects.setPrototype(prototype, child, objectError) &&
            objectError.code == PortableObjectErrorCode::PrototypeCycle,
        "prototype mutation: cycle rejected");

    RuntimeObjectId ordered = kInvalidRuntimeObjectId;
    check(sparse.objects.createObject(kInvalidRuntimeObjectId, ordered,
            objectError) &&
            sparse.objects.setOwnProperty(ordered, SourceView("10", 2u),
                Value::number(10.0), objectError) &&
            sparse.objects.setOwnProperty(ordered, SourceView("2", 1u),
                Value::number(2.0), objectError) &&
            sparse.objects.setOwnProperty(ordered, SourceView("name", 4u),
                Value::number(1.0), objectError) &&
            sparse.objects.getOwnPropertyAt(ordered, 0u, propertyEntry,
                found, objectError) && found && propertyEntry.key.length == 1u &&
            propertyEntry.key.data[0] == '2' &&
            sparse.objects.getOwnPropertyAt(ordered, 1u, propertyEntry,
                found, objectError) && found && propertyEntry.key.length == 2u &&
            propertyEntry.key.data[0] == '1' &&
            sparse.objects.getOwnPropertyAt(ordered, 2u, propertyEntry,
                found, objectError) && found && propertyEntry.key.length == 4u &&
            propertyEntry.key.data[0] == 'n',
        "object own-key enumeration: integer keys sort before insertion-ordered names");

    TestOptions propertyLimit;
    propertyLimit.objectLimits.maxProperties = 1u;
    propertyLimit.objectLimits.maxPropertiesPerObject = 1u;
    TestRun exhaustedProperty;
    check(executeSource(exhaustedProperty,
            "let prior = 0; const limited = { first: (prior = 1), second: 2 };",
            propertyLimit) &&
            !exhaustedProperty.succeeded &&
            exhaustedProperty.result.error.code ==
                PortableExecutionErrorCode::PropertyCapacityExhausted,
        "property capacity: explicit failure without truncation");
    Value partialObject = Value::object(makeRuntimeHandle(
        exhaustedProperty.generation, 0u));
    Value partialFirst;
    Value partialSecond;
    bool partialFound = false;
    check(readNumber(exhaustedProperty, "prior", 1.0) &&
            exhaustedProperty.objects.getOwnProperty(partialObject.objectId(),
                SourceView("first", 5u), partialFirst, partialFound,
                objectError) && partialFound && isNumber(partialFirst, 1.0) &&
            exhaustedProperty.objects.getOwnProperty(partialObject.objectId(),
                SourceView("second", 6u), partialSecond, partialFound,
                objectError) && !partialFound &&
            exhaustedProperty.objects.propertyCount() == 1u,
        "partial object literal: completed side effects and first property remain valid");

    TestOptions nameLimit;
    nameLimit.objectLimits.maxPropertyNameLength = 3u;
    TestRun longPropertyName;
    check(executeSource(longPropertyName,
            "const named = { long: 1 };", nameLimit) &&
            !longPropertyName.succeeded &&
            longPropertyName.result.error.code ==
                PortableExecutionErrorCode::PropertyNameTooLong,
        "property-name limit: overlong key has an explicit error");

    TestOptions nameStorageLimit;
    nameStorageLimit.objectLimits.maxPropertyNameLength = 2u;
    nameStorageLimit.objectLimits.maxPropertyNameBytes = 2u;
    TestRun exhaustedNames;
    check(executeSource(exhaustedNames,
            "const named = { a: 1, bc: 2 };", nameStorageLimit) &&
            !exhaustedNames.succeeded &&
            exhaustedNames.result.error.code ==
                PortableExecutionErrorCode::PropertyCapacityExhausted,
        "property-name arena: total byte capacity is explicit");

    TestOptions objectLimit;
    objectLimit.objectLimits.maxObjects = 0u;
    TestRun exhaustedObject;
    check(executeSource(exhaustedObject,
            "let prior = 0; const limited = { value: (prior = 1) };",
            objectLimit) && !exhaustedObject.succeeded &&
            exhaustedObject.result.error.code ==
                PortableExecutionErrorCode::ObjectCapacityExhausted &&
            readNumber(exhaustedObject, "prior", 0.0) &&
            exhaustedObject.objects.objectCount() == 0u,
        "object capacity: creation fails before property side effects");

    TestOptions arrayLimit;
    arrayLimit.objectLimits.maxArrayElements = 0u;
    arrayLimit.objectLimits.maxTotalArrayElements = 0u;
    TestRun exhaustedArray;
    check(executeSource(exhaustedArray,
            "let prior = 0; const limited = [(prior = 1)];", arrayLimit) &&
            !exhaustedArray.succeeded &&
            exhaustedArray.result.error.code ==
                PortableExecutionErrorCode::ArrayCapacityExhausted &&
            readNumber(exhaustedArray, "prior", 1.0),
        "Array element capacity: explicit failure");
    const RuntimeObjectId partialArray = makeRuntimeHandle(
        exhaustedArray.generation, 0u);
    std::size_t partialLength = 0u;
    bool partialElement = true;
    check(exhaustedArray.objects.arrayLength(partialArray, partialLength,
            objectError) && partialLength == 1u &&
            exhaustedArray.objects.hasOwnArrayElement(partialArray, 0u,
                partialElement, objectError) && !partialElement,
        "partial Array literal: logical length remains, failed slot is a hole");

    TestRun badIndex;
    check(executeSource(badIndex,
            "const limited = []; limited[1024] = 1;"),
        "Array index boundary: parser and store configure");
    check(!badIndex.succeeded && badIndex.result.error.code ==
            PortableExecutionErrorCode::InvalidArrayIndex,
        "Array index boundary: out-of-range index rejected");

    const RuntimeObjectId oldObject = objectValue.objectId();
    check(sparse.objects.reset(static_cast<RuntimeGeneration>(
                sparse.generation + 1u), objectError) &&
            !sparse.objects.validObject(oldObject, objectError) &&
            objectError.code == PortableObjectErrorCode::StaleGeneration,
        "realm reset: stale object handle rejected");

    TestRun repeated;
    AstNodeId firstRoot = kInvalidAstNodeId;
    check(repeated.parse("const persistent = { value: 10 };", firstRoot),
        "same realm objects: first source parses");
    TestOptions defaults;
    check(repeated.execute(firstRoot, defaults) && repeated.succeeded,
        "same realm objects: first source executes");
    AstNodeId secondRoot = kInvalidAstNodeId;
    check(repeated.parse("persistent.value = 17; persistent.value;",
            secondRoot),
        "same realm objects: second source parses");
    check(repeated.execute(secondRoot, defaults) && repeated.succeeded &&
            isNumber(repeated.finalValue, 17.0),
        "same realm objects: mutation survives successive scripts");
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
    testObjectsAndArrays();
    testBudgetsAndRecovery();
    testRealmResetStaleHandles();
    if (failureCount != 0u) {
        std::printf("Portable execution tests FAIL (%zu/%zu)\n",
            failureCount, checkCount);
        return 1;
    }
    std::printf("Navigator JavaScript portable execution checks: %zu/%zu passed\n",
        checkCount, checkCount);
    std::printf("Value=%zu FunctionRecord=%zu EnvironmentRecord=%zu "
        "Binding=%zu ObjectRecord=%zu PropertyRecord=%zu "
        "ArrayElementRecord=%zu PortableObjectStore=%zu "
        "PortableCallFrame=%zu PortableObjectExecutionCore=%zu\n",
        sizeof(Value),
        sizeof(FunctionRecord), sizeof(EnvironmentRecord), sizeof(Binding),
        sizeof(PortableObjectRecord), sizeof(PortablePropertyRecord),
        sizeof(PortableArrayElementRecord), sizeof(PortableObjectStore),
        sizeof(PortableCallFrame),
        sizeof(PortableObjectExecutionCore));
    std::printf("Observed high-water: environments=%zu bindings=%zu strings=%zu "
        "stringBytes=%zu functions=%zu callDepth=%zu arguments=%zu\n",
        usageHighWater.environments, usageHighWater.bindings,
        usageHighWater.stringValues, usageHighWater.stringBytes,
        usageHighWater.functions, usageHighWater.callDepth,
        usageHighWater.argumentValues);
    std::printf("Observed object high-water: objects=%zu properties=%zu "
        "propertyNameBytes=%zu arrayElements=%zu\n",
        usageHighWater.objects, usageHighWater.properties,
        usageHighWater.propertyNameBytes, usageHighWater.arrayElements);
    return 0;
}
