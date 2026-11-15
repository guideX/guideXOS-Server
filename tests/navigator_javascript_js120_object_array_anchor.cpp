#include "navigator_javascript_js120_object_array_probe.h"

#include <cstddef>

using namespace gxos::javascript;

static_assert(sizeof(Value) == 16u, "JS120 AMD64 Value size");
static_assert(sizeof(EnvironmentRecord) == 24u,
    "JS120 AMD64 environment record size");
static_assert(sizeof(Binding) == 40u, "JS120 AMD64 binding size");
static_assert(sizeof(PortableCallFrame) == 72u,
    "JS120 AMD64 call frame size");
static_assert(sizeof(PortableObjectRecord) == 28u,
    "JS120 AMD64 object record size");
static_assert(sizeof(PortablePropertyRecord) == 40u,
    "JS120 AMD64 property record size");
static_assert(sizeof(PortableArrayElementRecord) == 32u,
    "JS120 AMD64 Array element record size");
static_assert(sizeof(PortableObjectStore) == 200u,
    "JS120 AMD64 object store metadata size");
static_assert(sizeof(PortableExecutionCore) == 304u,
    "JS120 AMD64 portable execution context size");
static_assert(sizeof(PortableObjectExecutionCore) == 304u,
    "JS120 AMD64 object-enabled execution context size");
static_assert(alignof(Value) == 8u, "JS120 AMD64 Value alignment");
static_assert(alignof(PortableObjectRecord) == 4u,
    "JS120 AMD64 object record alignment");
static_assert(alignof(PortablePropertyRecord) == 8u,
    "JS120 AMD64 property record alignment");
static_assert(alignof(PortableArrayElementRecord) == 8u,
    "JS120 AMD64 Array element record alignment");
static_assert(alignof(PortableExecutionCore) == 8u,
    "JS120 AMD64 execution context alignment");
static_assert(alignof(PortableObjectExecutionCore) == 8u,
    "JS120 AMD64 object-enabled execution alignment");

extern "C" std::uint32_t gxos_js120_portable_object_array_probe(
    const char* sourceBytes, std::size_t sourceLength,
    const Js120PortableObjectArrayStorage* supplied,
    std::uint16_t generation, Js120PortableObjectArrayResult* output)
{
    if (output == nullptr) return 1u;
    *output = Js120PortableObjectArrayResult();
    if (supplied == nullptr || (sourceBytes == nullptr && sourceLength != 0u) ||
        supplied->tokens == nullptr || supplied->nodes == nullptr ||
        supplied->children == nullptr || supplied->parserScratch == nullptr ||
        supplied->environments == nullptr || supplied->bindings == nullptr ||
        supplied->bindingNames == nullptr || supplied->stringRecords == nullptr ||
        supplied->stringBytes == nullptr || supplied->functions == nullptr ||
        supplied->frames == nullptr || supplied->argumentValues == nullptr ||
        supplied->objects == nullptr || supplied->properties == nullptr ||
        supplied->arrayElements == nullptr ||
        supplied->propertyNameBytes == nullptr ||
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
    if (!environments.createRoot(EnvironmentLimits{256u, 256u}, root,
            environmentError)) {
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
    PortableObjectStore objects;
    PortableObjectError objectError;
    if (!objects.configure(PortableObjectStorageView{
            supplied->objects, supplied->objectCapacity,
            supplied->properties, supplied->propertyCapacity,
            supplied->arrayElements, supplied->arrayElementCapacity,
            supplied->propertyNameBytes, supplied->propertyNameCapacity},
            PortableObjectLimits(), generation, objectError)) {
        output->status = 4u;
        return output->status;
    }

    const PortableExecutionStorageView executionStorage{
        supplied->frames, supplied->frameCapacity,
        supplied->argumentValues, supplied->argumentCapacity};
    const PortableExecutionLimits limits;
    std::size_t steps = 0u;
    Value finalValue = Value::undefined();
    PortableExecutionResult executionResult;
    PortableObjectExecutionCore core(ast, strings, environments, functions,
        objects, executionStorage, limits, steps);
    const bool succeeded = core.run(ast.root(), root, finalValue,
        executionResult);
    output->status = succeeded ? 0u : 5u;
    output->executionError = static_cast<std::uint8_t>(
        executionResult.error.code);
    output->executionSteps = executionResult.executionSteps;
    output->objectCount = objects.objectCount();
    output->propertyCount = objects.propertyCount();
    output->propertyNameBytes = objects.propertyNameBytesUsed();
    output->arrayElementCount = objects.arrayElementCount();
    output->finalValueType = static_cast<std::uint8_t>(finalValue.type());
    if (finalValue.isNumber()) output->finalNumber = finalValue.numberValue();

    Value values;
    if (!succeeded || finalValue.type() != ValueType::Number ||
        !environments.readChain(root, SourceView("values", 6u), values,
            environmentError) || !values.isObject() ||
        !objects.arrayLength(values.objectId(), output->arrayLength,
            objectError)) {
        output->status = 6u;
        return output->status;
    }
    bool deleted = false;
    if (!objects.deleteArrayElement(values.objectId(), 1u, deleted,
            objectError) || !deleted || output->arrayLength != 3u ||
        objects.arrayElementCount() + 1u != output->arrayElementCount) {
        output->status = 6u;
        return output->status;
    }

    Value state;
    if (!environments.readChain(root, SourceView("state", 5u), state,
            environmentError) || !state.isObject()) {
        output->status = 6u;
        return output->status;
    }
    RuntimeObjectId originalPrototype = kInvalidRuntimeObjectId;
    RuntimeObjectId currentPrototype = kInvalidRuntimeObjectId;
    if (!objects.prototypeOf(state.objectId(), originalPrototype,
            objectError) ||
        !objects.setOwnProperty(state.objectId(), SourceView("__proto__", 9u),
            Value::number(1.0), objectError) ||
        !objects.prototypeOf(state.objectId(), currentPrototype,
            objectError) || originalPrototype != currentPrototype) {
        output->status = 6u;
        return output->status;
    }

    output->objectRecordBytes = sizeof(PortableObjectRecord);
    output->propertyRecordBytes = sizeof(PortablePropertyRecord);
    output->arrayElementRecordBytes = sizeof(PortableArrayElementRecord);
    output->objectStoreBytes = sizeof(PortableObjectStore);
    output->executionCoreBytes = sizeof(PortableObjectExecutionCore);
    return output->status;
}
