#include "navigator_javascript_js125_callback_probe.h"

#include <cstddef>
#include <cstdint>

using namespace gxos::javascript;

static_assert(sizeof(Value) == 16u, "JS125 AMD64 Value size");
static_assert(sizeof(FunctionRecord) == 48u, "JS125 AMD64 function record size");
static_assert(sizeof(EnvironmentRecord) == 24u,
    "JS125 AMD64 environment record size");
static_assert(sizeof(Binding) == 40u, "JS125 AMD64 binding size");
static_assert(sizeof(PortableCallFrame) == 72u,
    "JS125 AMD64 invocation frame size");
static_assert(sizeof(PortablePropertyKeySnapshotEntry) == 8u,
    "JS125 AMD64 property snapshot entry size");
static_assert(alignof(Value) == 8u, "JS125 AMD64 Value alignment");
static_assert(alignof(PortableCallFrame) == 8u,
    "JS125 AMD64 invocation frame alignment");

extern "C" std::uint32_t gxos_js125_portable_callback_probe(
    const char* sourceBytes, std::size_t sourceLength,
    const Js125PortableCallbackStorage* supplied,
    std::uint16_t generation, Js125PortableCallbackResult* output)
{
    if (output == nullptr) return 1u;
    *output = Js125PortableCallbackResult();
    if (supplied == nullptr ||
        (sourceBytes == nullptr && sourceLength != 0u) ||
        supplied->tokens == nullptr || supplied->nodes == nullptr ||
        supplied->children == nullptr || supplied->parserScratch == nullptr ||
        supplied->environments == nullptr || supplied->bindings == nullptr ||
        supplied->bindingNames == nullptr || supplied->stringRecords == nullptr ||
        supplied->stringBytes == nullptr || supplied->functions == nullptr ||
        supplied->frames == nullptr || supplied->argumentValues == nullptr ||
        supplied->objects == nullptr || supplied->properties == nullptr ||
        supplied->arrayElements == nullptr ||
        supplied->propertyNameBytes == nullptr ||
        supplied->snapshotEntries == nullptr ||
        supplied->snapshotKeyBytes == nullptr ||
        generation == kInvalidRuntimeGeneration) {
        output->status = 1u;
        return output->status;
    }

    const SourceView source(sourceBytes, sourceLength);
    TokenBuffer tokenBuffer{supplied->tokens, supplied->tokenCapacity};
    Lexer lexer;
    const LexerResult lexed = lexer.tokenizeInto(source, tokenBuffer);
    if (!lexed.succeeded()) {
        output->status = 2u;
        output->lexerError = static_cast<std::uint8_t>(lexed.error.code);
        return output->status;
    }
    Ast ast(source, AstStorageView{supplied->nodes, supplied->nodeCapacity,
        supplied->children, supplied->childCapacity});
    FrontendArena scratch(supplied->parserScratch,
        supplied->parserScratchCapacity);
    Parser parser;
    ParserError parserError;
    if (!parser.parseInto(makeTokenView(source, tokenBuffer), ast, scratch,
            parserError)) {
        output->status = 3u;
        output->parserError = static_cast<std::uint8_t>(parserError.code);
        return output->status;
    }

    EnvironmentStore environments;
    EnvironmentError environmentError;
    if (!environments.configure(EnvironmentStorageView{
            supplied->environments, supplied->environmentCapacity,
            supplied->bindings, supplied->bindingCapacity,
            supplied->bindingNames, supplied->bindingNameCapacity}, generation,
            environmentError)) {
        output->status = 4u;
        return output->status;
    }
    EnvironmentId root = kInvalidEnvironmentId;
    if (!environments.createRoot(EnvironmentLimits(), root, environmentError)) {
        output->status = 4u;
        return output->status;
    }
    RuntimeStringStore strings;
    RuntimeStringError stringError;
    if (!strings.configure(RuntimeStringStorageView{
            supplied->stringRecords, supplied->stringRecordCapacity,
            supplied->stringBytes, supplied->stringByteCapacity}, generation,
            stringError)) {
        output->status = 4u;
        return output->status;
    }
    FunctionStore functions;
    FunctionStoreError functionError;
    if (!functions.configure(FunctionStorageView{supplied->functions,
            supplied->functionCapacity}, generation, 0u, functionError)) {
        output->status = 4u;
        return output->status;
    }
    PortableObjectLimits objectLimits;
    objectLimits.maxObjects = supplied->objectCapacity;
    objectLimits.maxProperties = supplied->propertyCapacity;
    objectLimits.maxPropertiesPerObject = objectLimits.maxProperties;
    objectLimits.maxPropertyNameBytes = supplied->propertyNameCapacity;
    objectLimits.maxArrayElements = supplied->arrayElementCapacity;
    objectLimits.maxTotalArrayElements = supplied->arrayElementCapacity;
    objectLimits.maxArrayIndex = objectLimits.maxArrayElements == 0u
        ? 0u : objectLimits.maxArrayElements - 1u;
    objectLimits.maxArrayLength = objectLimits.maxArrayElements;
    PortableObjectStore objects;
    PortableObjectError objectError;
    if (!objects.configure(PortableObjectStorageView{
            supplied->objects, supplied->objectCapacity,
            supplied->properties, supplied->propertyCapacity,
            supplied->arrayElements, supplied->arrayElementCapacity,
            supplied->propertyNameBytes, supplied->propertyNameCapacity},
            objectLimits, generation, objectError)) {
        output->status = 4u;
        return output->status;
    }

    PortableExecutionLimits limits;
    PortableExecutionStorageView executionStorage{
        supplied->frames, supplied->frameCapacity,
        supplied->argumentValues, supplied->argumentCapacity};
    std::size_t steps = 0u;
    Value finalValue = Value::undefined();
    PortableExecutionResult executionResult;
    PortableObjectExecutionCore core(ast, strings, environments, functions,
        objects, executionStorage, limits, steps);
    if (!core.run(ast.root(), root, finalValue, executionResult)) {
        output->status = 5u;
        output->executionError = static_cast<std::uint8_t>(
            executionResult.error.code);
        output->executionSteps = executionResult.executionSteps;
        return output->status;
    }

    Value callback;
    if (!environments.readChain(root, SourceView("callback", 8u), callback,
            environmentError)) {
        output->status = 6u;
        return output->status;
    }
    bool callable = false;
    PortableExecutionError callableError;
    if (!core.isCallable(callback, callable, callableError) || !callable) {
        output->status = 6u;
        output->invocationError = static_cast<std::uint8_t>(
            callableError.code);
        return output->status;
    }

    RuntimeObjectId receiverId = kInvalidRuntimeObjectId;
    if (!objects.createObject(kInvalidRuntimeObjectId, receiverId,
            objectError) ||
        !objects.setOwnProperty(receiverId, SourceView("callback", 8u),
            callback, objectError)) {
        output->status = 7u;
        return output->status;
    }
    Value propertyCallback;
    bool found = false;
    if (!objects.getProperty(receiverId, SourceView("callback", 8u),
            propertyCallback, found, objectError) || !found) {
        output->status = 7u;
        return output->status;
    }
    PortableExecutionResult invocation;
    if (!core.invokeCallable(propertyCallback, Value::object(receiverId),
            nullptr, 0u, invocation)) {
        output->status = 8u;
        output->invocationError = static_cast<std::uint8_t>(
            invocation.error.code);
        output->executionSteps = invocation.executionSteps;
        return output->status;
    }
    if (!invocation.finalValue.isObject() ||
        invocation.finalValue.objectId() != receiverId ||
        !objects.snapshotOwnEnumerableStringProperties(receiverId,
            supplied->snapshotEntries, supplied->snapshotEntryCapacity,
            supplied->snapshotKeyBytes, supplied->snapshotKeyByteCapacity,
            output->snapshotEntryCount, output->snapshotKeyBytes, objectError)) {
        output->status = 9u;
        return output->status;
    }

    output->status = 0u;
    output->executionSteps = invocation.executionSteps;
    output->callDepthHighWater = invocation.callDepthHighWater;
    output->argumentValuesHighWater = invocation.argumentValuesHighWater;
    output->finalValueType = static_cast<std::uint8_t>(
        invocation.finalValue.type());
    output->portableExecutionCoreBytes = sizeof(PortableObjectExecutionCore);
    output->portableObjectStoreBytes = sizeof(PortableObjectStore);
    output->callFrameBytes = sizeof(PortableCallFrame);
    output->functionRecordBytes = sizeof(FunctionRecord);
    return output->status;
}
