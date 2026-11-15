#include "navigator_javascript_js119_execution_probe.h"

#include <cstddef>

using namespace gxos::javascript;

static_assert(sizeof(Value) == 16u, "JS119 AMD64 Value size");
static_assert(sizeof(FunctionRecord) == 48u,
    "JS119 AMD64 function record size");
static_assert(sizeof(EnvironmentRecord) == 24u,
    "JS119 AMD64 environment record size");
static_assert(sizeof(Binding) == 40u, "JS119 AMD64 binding size");
static_assert(sizeof(PortableCallFrame) == 72u,
    "JS119 AMD64 portable call frame size");
static_assert(sizeof(PortableExecutionCore) == 304u,
    "JS119 AMD64 portable execution context size");
static_assert(alignof(Value) == 8u, "JS119 AMD64 Value alignment");
static_assert(alignof(FunctionRecord) == 8u,
    "JS119 AMD64 function record alignment");
static_assert(alignof(EnvironmentRecord) == 4u,
    "JS119 AMD64 environment record alignment");
static_assert(alignof(Binding) == 8u, "JS119 AMD64 binding alignment");
static_assert(alignof(PortableCallFrame) == 8u,
    "JS119 AMD64 portable call frame alignment");
static_assert(alignof(PortableExecutionCore) == 8u,
    "JS119 AMD64 portable execution context alignment");

extern "C" std::uint32_t gxos_js119_portable_execution_probe(
    const char* sourceBytes, std::size_t sourceLength,
    const Js119PortableExecutionProbeStorage* supplied,
    std::uint16_t generation, Js119PortableExecutionProbeResult* output)
{
    if (output == nullptr) return 1u;
    *output = Js119PortableExecutionProbeResult();
    if (supplied == nullptr || (sourceBytes == nullptr && sourceLength != 0u) ||
        supplied->tokens == nullptr || supplied->nodes == nullptr ||
        supplied->children == nullptr || supplied->parserScratch == nullptr ||
        supplied->environments == nullptr || supplied->bindings == nullptr ||
        supplied->bindingNames == nullptr || supplied->stringRecords == nullptr ||
        supplied->stringBytes == nullptr || supplied->functions == nullptr ||
        supplied->frames == nullptr || supplied->argumentValues == nullptr ||
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

    const PortableExecutionStorageView executionStorage{
        supplied->frames, supplied->frameCapacity,
        supplied->argumentValues, supplied->argumentCapacity};
    const PortableExecutionLimits limits;
    std::size_t steps = 0u;
    Value finalValue = Value::undefined();
    PortableExecutionResult executionResult;
    PortableExecutionCore core(ast, strings, environments, functions,
        executionStorage, limits, steps);
    const bool succeeded = core.run(ast.root(), root, finalValue,
        executionResult);
    output->status = succeeded ? 0u : 5u;
    output->executionError = static_cast<std::uint8_t>(
        executionResult.error.code);
    output->executionSteps = executionResult.executionSteps;
    output->environmentCount = environments.environmentCount();
    output->bindingCount = environments.bindingCount();
    output->functionCount = functions.count();
    output->callDepthHighWater = executionResult.callDepthHighWater;
    output->argumentValuesHighWater =
        executionResult.argumentValuesHighWater;
    output->finalValueType = static_cast<std::uint8_t>(finalValue.type());
    if (finalValue.isNumber()) output->finalNumber = finalValue.numberValue();
    return output->status;
}
