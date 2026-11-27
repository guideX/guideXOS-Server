#include "navigator_javascript_js129_unified_call_entry_probe.h"

#include <cstddef>
#include <cstdint>

using namespace gxos::javascript;

static_assert(sizeof(Value) == 16u, "JS129 AMD64 Value size");
static_assert(sizeof(PortableCallFrame) == 80u,
    "JS129 AMD64 invocation frame size");
static_assert(sizeof(PortableExecutionFrame) == 112u,
    "JS129 AMD64 execution/preflight frame size");
static_assert(alignof(PortableCallFrame) == 8u,
    "JS129 AMD64 invocation frame alignment");
static_assert(alignof(PortableExecutionFrame) == 8u,
    "JS129 AMD64 execution/preflight frame alignment");

extern "C" __attribute__((noinline)) std::uint32_t
gxos_js129_invoke_callable_entry(PortableExecutionCore* core,
    Value callable, Value receiver, const Value* arguments,
    std::size_t argumentCount, PortableExecutionResult* output)
{
    if (core == nullptr || output == nullptr) return 1u;
    *output = PortableExecutionResult();
    return core->invokeCallable(callable, receiver, arguments, argumentCount,
        *output) ? 0u : 2u;
}

extern "C" std::uint32_t gxos_js129_unified_call_entry_probe(
    const char* sourceBytes, std::size_t sourceLength,
    const Js129UnifiedCallEntryStorage* supplied,
    std::uint16_t generation, Js129UnifiedCallEntryResult* output)
{
    if (output == nullptr) return 1u;
    *output = Js129UnifiedCallEntryResult();
    if (supplied == nullptr ||
        (sourceBytes == nullptr && sourceLength != 0u) ||
        supplied->tokens == nullptr || supplied->nodes == nullptr ||
        supplied->children == nullptr || supplied->parserScratch == nullptr ||
        supplied->environments == nullptr || supplied->bindings == nullptr ||
        supplied->bindingNames == nullptr || supplied->stringRecords == nullptr ||
        supplied->stringBytes == nullptr || supplied->functions == nullptr ||
        supplied->callFrames == nullptr || supplied->argumentValues == nullptr ||
        supplied->executionFrames == nullptr ||
        supplied->executionFrameCapacity == 0u ||
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

    const PortableExecutionStorageView executionStorage{
        supplied->callFrames, supplied->callFrameCapacity,
        supplied->argumentValues, supplied->argumentCapacity,
        supplied->executionFrames, supplied->executionFrameCapacity};
    PortableExecutionLimits limits;
    limits.maxExecutionFrames = supplied->executionFrameCapacity;
    std::size_t steps = 0u;
    Value finalValue = Value::undefined();
    PortableExecutionResult executionResult;
    PortableExecutionCore core(ast, strings, environments, functions,
        executionStorage, limits, steps);
    if (!core.run(ast.root(), root, finalValue, executionResult)) {
        output->status = 5u;
        output->executionError = static_cast<std::uint8_t>(
            executionResult.error.code);
        output->executionSteps = executionResult.executionSteps;
        output->callDepthHighWater = executionResult.callDepthHighWater;
        output->executionFrameHighWater =
            executionResult.executionFrameHighWater;
        output->preflightFrameHighWater =
            executionResult.preflightFrameHighWater;
        output->preflightWorkUnits = executionResult.preflightWorkUnits;
        return output->status;
    }

    Value callback = Value::undefined();
    if (!environments.readChain(root, SourceView("callback", 8u), callback,
            environmentError)) {
        output->status = 6u;
        return output->status;
    }
    PortableExecutionResult callbackResult;
    if (gxos_js129_invoke_callable_entry(&core, callback,
            Value::undefined(), nullptr, 0u, &callbackResult) != 0u) {
        output->status = 7u;
        output->callbackError = static_cast<std::uint8_t>(
            callbackResult.error.code);
        output->executionSteps = callbackResult.executionSteps;
        output->callDepthHighWater = callbackResult.callDepthHighWater;
        output->executionFrameHighWater =
            callbackResult.executionFrameHighWater;
        output->preflightFrameHighWater =
            callbackResult.preflightFrameHighWater;
        output->preflightWorkUnits = callbackResult.preflightWorkUnits;
        return output->status;
    }
    if (!callbackResult.finalValue.isNumber()) {
        output->status = 8u;
        return output->status;
    }

    output->status = 0u;
    output->callbackNumber = callbackResult.finalValue.numberValue();
    output->executionSteps = callbackResult.executionSteps;
    output->callDepthHighWater = callbackResult.callDepthHighWater;
    output->argumentValuesHighWater = callbackResult.argumentValuesHighWater;
    output->executionFrameHighWater = callbackResult.executionFrameHighWater;
    output->preflightFrameHighWater = callbackResult.preflightFrameHighWater;
    output->preflightWorkUnits = callbackResult.preflightWorkUnits;
    output->portableCallFrameBytes = sizeof(PortableCallFrame);
    output->portableExecutionFrameBytes = sizeof(PortableExecutionFrame);
    return output->status;
}
