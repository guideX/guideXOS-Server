#include "navigator_javascript/environment.h"
#include "navigator_javascript/runtime_string_storage.h"
#include "navigator_javascript/value.h"
#include "navigator_javascript_js118_runtime_core_probe.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

using namespace gxos::javascript;

std::size_t checks = 0u;
std::size_t failures = 0u;
std::size_t valueChecks = 0u;
std::size_t environmentChecks = 0u;
bool environmentTestActive = false;

void expect(bool condition, const char* label, bool environment = false)
{
    ++checks;
    if (environment || environmentTestActive) ++environmentChecks;
    else ++valueChecks;
    if (condition) return;
    ++failures;
    std::cerr << "  FAIL: " << label << '\n';
}

void testValueRecords()
{
    static_assert(sizeof(Value) == 16u, "portable Value record size");
    static_assert(alignof(Value) <= alignof(double), "Value alignment bound");
    static_assert(std::is_trivially_copyable<Value>::value,
        "Value copies must not allocate or throw");

    expect(Value::undefined().type() == ValueType::Undefined &&
        Value::undefined().isUndefined(), "undefined tag and inspection");
    expect(Value::nullValue().type() == ValueType::Null &&
        Value::nullValue().isNull(), "null tag and inspection");
    expect(Value::boolean(true).type() == ValueType::Boolean &&
        Value::boolean(true).booleanValue() &&
        !Value::boolean(false).booleanValue(), "Boolean construction and copy");
    expect(Value::number(-0.0).isNumber() &&
        std::signbit(Value::number(-0.0).numberValue()),
        "binary64 negative zero is preserved");
    expect(std::isnan(Value::number(
            std::numeric_limits<double>::quiet_NaN()).numberValue()) &&
        std::isinf(Value::number(std::numeric_limits<double>::infinity())
            .numberValue()) &&
        std::isinf(Value::number(-std::numeric_limits<double>::infinity())
            .numberValue()), "binary64 NaN and infinities are preserved");

    const RuntimeStringId stringId = makeRuntimeHandle(7u, 3u);
    const RuntimeFunctionId functionId = makeRuntimeHandle(7u, 4u);
    const RuntimeObjectId objectId = makeRuntimeHandle(7u, 5u);
    const RuntimeHostObjectId hostId =
        (static_cast<std::uint64_t>(11u) << 32u) | 9u;
    Value string = Value::string(stringId);
    Value function = Value::function(functionId);
    Value object = Value::object(objectId);
    Value host = Value::hostObject(hostId);
    const Value copiedString = string;
    const Value copiedFunction = function;
    const Value copiedObject = object;
    const Value copiedHost = host;
    expect(string.isString() && copiedString.stringId() == stringId &&
        function.isFunction() && copiedFunction.functionId() == functionId &&
        object.isObject() && copiedObject.objectId() == objectId &&
        host.isHostObject() && copiedHost.hostObjectId() == hostId,
        "string and opaque reference handles retain copy identity");
    function = Value::undefined();
    object = Value::nullValue();
    expect(copiedFunction.functionId() == functionId &&
        copiedObject.objectId() == objectId &&
        Value::wellKnownSymbol(WellKnownSymbol::Iterator)
            .wellKnownSymbolValue() == WellKnownSymbol::Iterator &&
        Value::symbolNamespace().isSymbolNamespace() &&
        Value::objectNamespace().isObjectNamespace() &&
        Value::jsonNamespace().isJsonNamespace(),
        "assignment, symbols, and supported namespace tags");
    expect(!Value::number(1.0).isString() &&
        Value::number(1.0).stringId() == kInvalidRuntimeStringId &&
        Value::undefined().objectId() == kInvalidRuntimeObjectId &&
        Value::nullValue().functionId() == kInvalidRuntimeFunctionId,
        "wrong-tag access returns invalid reference sentinels");
    expect(runtimeHandleIsValid(stringId, 7u) &&
        !runtimeHandleIsValid(stringId, 8u) &&
        !runtimeHandleIsValid(kInvalidRuntimeStringId, 7u),
        "packed handles validate generation and invalid sentinel");
}

void testStringStore()
{
    std::array<RuntimeStringRecord, 3u> records{};
    std::array<char, 4u> bytes{};
    RuntimeStringStore store;
    RuntimeStringError error;
    const bool configured = store.configure(
        RuntimeStringStorageView{records.data(), records.size(), bytes.data(),
            bytes.size()}, 7u, error);
    expect(configured && error.code == RuntimeStringErrorCode::None,
        "caller-owned string arrays configure");

    RuntimeStringId emptyId = kInvalidRuntimeStringId;
    RuntimeStringId firstId = kInvalidRuntimeStringId;
    const bool emptyCreated = store.create(SourceView(), 4u, emptyId, error);
    const char firstText[] = {'A', '\0', 'B'};
    const bool firstCreated = store.create(SourceView(firstText, sizeof(firstText)),
        4u, firstId, error);
    SourceView empty;
    SourceView first;
    const bool emptyRead = store.view(emptyId, empty, error);
    const bool firstRead = store.view(firstId, first, error);
    expect(emptyCreated && firstCreated && emptyRead && firstRead &&
        empty.length == 0u && first.length == 3u &&
        first.data[0] == 'A' && first.data[1] == '\0' &&
        first.data[2] == 'B' && store.bytesUsed() == 3u,
        "empty and embedded-zero strings retain length-aware owned bytes");

    const RuntimeStringMark mark = store.mark();
    RuntimeStringId finalId = kInvalidRuntimeStringId;
    const bool thirdCreated = store.create(SourceView("z", 1u), 4u,
        finalId, error);
    RuntimeStringId exhaustedId = kInvalidRuntimeStringId;
    const bool exhausted = store.create(SourceView("q", 1u), 4u,
        exhaustedId, error);
    expect(thirdCreated && !exhausted &&
        error.code == RuntimeStringErrorCode::CapacityExceeded &&
        store.count() == 3u && store.bytesUsed() == 4u,
        "string byte and record bounds fail without partial publication");
    expect(store.restore(mark, error) && store.count() == 2u &&
        store.bytesUsed() == 3u && !store.view(finalId, first, error) &&
        error.code == RuntimeStringErrorCode::InvalidReference,
        "generation-checked string mark restores transactional allocations");

    const RuntimeStringId staleId = makeRuntimeHandle(6u, 1u);
    expect(!store.view(staleId, first, error) &&
        error.code == RuntimeStringErrorCode::StaleGeneration,
        "string lookup rejects a stale realm generation");
    expect(!store.view(kInvalidRuntimeStringId, first, error) &&
        error.code == RuntimeStringErrorCode::InvalidReference,
        "string lookup rejects the invalid handle sentinel");
    expect(!store.create(SourceView(nullptr, 1u), 4u, exhaustedId, error) &&
        error.code == RuntimeStringErrorCode::InvalidInput &&
        store.count() == 2u && store.bytesUsed() == 3u,
        "invalid source is rejected before storage changes");

    RuntimeStringMark staleMark = mark;
    store.reset(8u);
    expect(!store.restore(staleMark, error) &&
        error.code == RuntimeStringErrorCode::StaleGeneration,
        "string reset invalidates previously issued marks");
    RuntimeStringId reusedSlot = kInvalidRuntimeStringId;
    expect(store.create(SourceView("n", 1u), 4u, reusedSlot, error) &&
        reusedSlot == makeRuntimeHandle(8u, 0u) && reusedSlot != emptyId &&
        !store.view(emptyId, first, error) &&
        error.code == RuntimeStringErrorCode::StaleGeneration,
        "reusing a string slot cannot revive a prior realm handle");
    const RuntimeStringStorageView stringStorage{records.data(),
        records.size(), bytes.data(), bytes.size()};
    store.reset(8u);
    RuntimeStringId advancedId = kInvalidRuntimeStringId;
    const bool sameGenerationResetBlocked = !store.create(
        SourceView("x", 1u), 4u, advancedId, error) &&
        error.code == RuntimeStringErrorCode::InvalidStorage;
    const bool sameGenerationConfigureBlocked = !store.configure(
        stringStorage, 8u, error) &&
        error.code == RuntimeStringErrorCode::StaleGeneration;
    const bool nextGenerationAccepted = store.configure(stringStorage, 9u,
        error) && store.create(SourceView("y", 1u), 4u, advancedId, error) &&
        advancedId == makeRuntimeHandle(9u, 0u);
    expect(sameGenerationResetBlocked && sameGenerationConfigureBlocked &&
        nextGenerationAccepted,
        "store reset and reconfigure require a strictly newer generation");

    std::array<unsigned char, sizeof(RuntimeStringRecord) + 1u> unaligned{};
    RuntimeStringStore badAlignment;
    expect(!badAlignment.configure(RuntimeStringStorageView{
            reinterpret_cast<RuntimeStringRecord*>(unaligned.data() + 1u),
            1u, nullptr, 0u}, 1u, error) &&
        error.code == RuntimeStringErrorCode::InvalidAlignment,
        "string metadata rejects invalid alignment");
    RuntimeStringStore badStorage;
    expect(!badStorage.configure(RuntimeStringStorageView{
            nullptr, 1u, nullptr, 0u}, 1u, error) &&
        error.code == RuntimeStringErrorCode::InvalidStorage,
        "string storage rejects a missing caller-owned record array");
    const std::uintptr_t nearEnd =
        std::numeric_limits<std::uintptr_t>::max() &
        ~(static_cast<std::uintptr_t>(alignof(RuntimeStringRecord)) - 1u);
    RuntimeStringStore overflow;
    expect(!overflow.configure(RuntimeStringStorageView{
            reinterpret_cast<RuntimeStringRecord*>(nearEnd), 1u, nullptr, 0u},
            1u, error) && error.code == RuntimeStringErrorCode::SizeOverflow,
        "string storage rejects pointer-range overflow before dereference");
}

void testEnvironmentOperations()
{
    environmentTestActive = true;
    std::array<EnvironmentRecord, 12u> environmentRecords{};
    std::array<Binding, 24u> bindings{};
    std::array<char, 128u> names{};
    EnvironmentStore store;
    EnvironmentError error;
    const bool configured = store.configure(EnvironmentStorageView{
        environmentRecords.data(), environmentRecords.size(), bindings.data(),
        bindings.size(), names.data(), names.size()}, 3u, error);
    EnvironmentId root = kInvalidEnvironmentId;
    const bool rootCreated = configured && store.createRoot(
        EnvironmentLimits{4u, 8u}, root, error);
    EnvironmentId block = kInvalidEnvironmentId;
    const bool blockCreated = rootCreated && store.create(root,
        EnvironmentKind::Block, EnvironmentLimits{4u, 8u}, block, error);
    expect(configured && rootCreated && blockCreated &&
        store.environmentCount() == 2u && store.record(block, error)->kind ==
            EnvironmentKind::Block,
        "global and block environments receive stable typed handles");

    const bool outerDeclared = store.declare(root, SourceView("x", 1u),
        Value::number(1.0), error);
    const bool shadowDeclared = store.declareLexical(block,
        SourceView("x", 1u), false, error, Value::undefined(), false);
    Value observed;
    const bool tdzRead = store.readChain(block, SourceView("x", 1u),
        observed, error);
    const bool tdzAssignment = store.assignChain(block, SourceView("x", 1u),
        Value::number(9.0), error);
    expect(outerDeclared && shadowDeclared && !tdzRead &&
        error.code == EnvironmentErrorCode::UninitializedBinding &&
        !tdzAssignment && error.code ==
            EnvironmentErrorCode::UninitializedBinding,
        "uninitialized inner binding shadows the initialized outer binding");

    const bool initialized = store.initializeChain(block,
        SourceView("x", 1u), Value::number(2.0), error);
    const bool innerRead = store.readChain(block, SourceView("x", 1u),
        observed, error);
    const bool innerAssigned = store.assignChain(block, SourceView("x", 1u),
        Value::number(3.0), error);
    const bool assignedRead = store.readChain(block, SourceView("x", 1u),
        observed, error);
    Value outer;
    const bool outerRead = store.readChain(root, SourceView("x", 1u),
        outer, error);
    expect(initialized && innerRead && innerAssigned && assignedRead &&
        observed.isNumber() && observed.numberValue() == 3.0 && outerRead &&
        outer.numberValue() == 1.0,
        "initialization and mutable assignment stay in the shadowing scope");

    const bool uninitializedDeclared = store.declareLexical(root,
        SourceView("u", 1u), false, error, Value::undefined(), false);
    const bool undefinedInitialized = store.initialize(root,
        SourceView("u", 1u), Value::undefined(), error);
    const bool undefinedRead = store.readChain(root, SourceView("u", 1u),
        observed, error);
    expect(uninitializedDeclared && undefinedInitialized && undefinedRead &&
        observed.isUndefined() && error.code == EnvironmentErrorCode::None,
        "initialized undefined is distinct from the temporal dead zone");

    const bool constDeclared = store.declareLexical(block,
        SourceView("fixed", 5u), true, error, Value::number(4.0), true);
    const bool constAssigned = store.assignChain(block,
        SourceView("fixed", 5u), Value::number(5.0), error);
    expect(constDeclared && !constAssigned &&
        error.code == EnvironmentErrorCode::ImmutableAssignment,
        "immutable binding assignment has its own explicit error");

    const std::size_t beforeDuplicate = store.bindingCount();
    const bool repeatedVar = store.declare(root, SourceView("x", 1u),
        Value::number(99.0), error);
    const bool duplicateLexical = store.declareLexical(root,
        SourceView("x", 1u), false, error, Value::number(99.0), true);
    const bool duplicateLocalLexical = store.declareLexical(block,
        SourceView("x", 1u), false, error, Value::number(99.0), true);
    expect(repeatedVar && !duplicateLexical && !duplicateLocalLexical &&
        error.code == EnvironmentErrorCode::DuplicateLexicalBinding &&
        store.bindingCount() == beforeDuplicate,
        "var redeclaration remains idempotent and lexical duplicates reject");

    const bool missing = store.readChain(block, SourceView("absent", 6u),
        observed, error);
    expect(!missing && error.code == EnvironmentErrorCode::MissingBinding,
        "missing binding is distinct from an uninitialized binding");
    const bool invalidStart = store.readChain(kInvalidEnvironmentId,
        SourceView("x", 1u), observed, error);
    expect(!invalidStart &&
        error.code == EnvironmentErrorCode::InvalidEnvironment,
        "scope-chain read validates the starting environment handle");

    EnvironmentId parameters = kInvalidEnvironmentId;
    EnvironmentId body = kInvalidEnvironmentId;
    const bool parameterEnv = store.create(root, EnvironmentKind::Parameters,
        EnvironmentLimits{4u, 8u}, parameters, error);
    const bool bodyEnv = parameterEnv && store.create(parameters,
        EnvironmentKind::FunctionBody, EnvironmentLimits{4u, 8u}, body,
        error);
    const bool earlier = store.declareLexical(parameters,
        SourceView("earlier", 7u), false, error, Value::number(10.0), true);
    const bool later = store.declareLexical(parameters,
        SourceView("later", 5u), false, error, Value::undefined(), false);
    const bool bodyOnly = store.declareLexical(body,
        SourceView("bodyOnly", 8u), false, error, Value::number(20.0), true);
    const bool earlierRead = store.readChain(parameters,
        SourceView("earlier", 7u), observed, error);
    const bool earlierValue = observed.isNumber() &&
        observed.numberValue() == 10.0;
    const bool laterRead = store.readChain(parameters,
        SourceView("later", 5u), observed, error);
    const EnvironmentErrorCode laterError = error.code;
    const bool bodyInvisible = store.readChain(parameters,
        SourceView("bodyOnly", 8u), observed, error);
    const EnvironmentErrorCode bodyError = error.code;
    expect(bodyEnv && earlier && later && bodyOnly && earlierRead &&
        earlierValue && !laterRead &&
        laterError == EnvironmentErrorCode::UninitializedBinding &&
        !bodyInvisible && bodyError == EnvironmentErrorCode::MissingBinding &&
        store.record(parameters, error)->kind == EnvironmentKind::Parameters &&
        store.record(body, error)->parent == parameters &&
        store.record(body, error)->kind == EnvironmentKind::FunctionBody,
        "parameter/body environment links preserve defaults and TDZ visibility");

    EnvironmentId captured = kInvalidEnvironmentId;
    EnvironmentId closure = kInvalidEnvironmentId;
    const bool capturedCreated = store.create(root, EnvironmentKind::Block,
        EnvironmentLimits{4u, 8u}, captured, error);
    const bool capturedBinding = store.declare(captured,
        SourceView("kept", 4u), Value::number(42.0), error);
    const bool closureCreated = capturedCreated && store.create(captured,
        EnvironmentKind::Function, EnvironmentLimits{4u, 8u}, closure,
        error);
    EnvironmentId afterCapture = kInvalidEnvironmentId;
    const bool moreCreated = closureCreated && store.create(root,
        EnvironmentKind::Block, EnvironmentLimits{4u, 8u}, afterCapture,
        error);
    const bool capturedRead = store.readChain(closure,
        SourceView("kept", 4u), observed, error);
    expect(capturedBinding && moreCreated && capturedRead &&
        observed.numberValue() == 42.0 &&
        store.record(closure, error)->parent == captured,
        "retained closure environments survive later pool allocations");

    EnvironmentId firstChild = kInvalidEnvironmentId;
    EnvironmentId secondChild = kInvalidEnvironmentId;
    const bool firstChildCreated = store.create(root, EnvironmentKind::Block,
        EnvironmentLimits{4u, 8u}, firstChild, error);
    const bool secondChildCreated = firstChildCreated && store.create(
        firstChild, EnvironmentKind::Block, EnvironmentLimits{4u, 8u},
        secondChild, error);
    const bool cycleRejected = !store.setParent(firstChild, secondChild, error) &&
        error.code == EnvironmentErrorCode::ParentCycle &&
        store.record(firstChild, error)->parent == root;
    expect(secondChildCreated && cycleRejected,
        "parent assignment rejects cycles without changing the link");

    Environment facade(&store, parameters, EnvironmentLimits{4u, 8u});
    const bool facadeLookup = facade.resolve(SourceView("earlier", 7u)) !=
        nullptr && facade.kind() == EnvironmentKind::Parameters &&
        facade.parent() == root;
    expect(facadeLookup,
        "hosted Environment facade calls the shared environment store");

    const EnvironmentId staleRoot = root;
    const EnvironmentId staleClosure = closure;
    store.reset(4u);
    EnvironmentId newRoot = kInvalidEnvironmentId;
    const bool resetRoot = store.createRoot(EnvironmentLimits{4u, 8u},
        newRoot, error);
    const bool staleRejected = !store.readChain(staleRoot,
        SourceView("x", 1u), observed, error) &&
        error.code == EnvironmentErrorCode::StaleGeneration &&
        !store.valid(staleClosure, error) &&
        error.code == EnvironmentErrorCode::StaleGeneration &&
        resetRoot && newRoot != staleRoot;
    expect(staleRejected && store.bindingCount() == 0u &&
        store.environmentCount() == 1u,
        "realm reset invalidates environment and closure handles");
    const EnvironmentStorageView environmentStorage{
        environmentRecords.data(), environmentRecords.size(), bindings.data(),
        bindings.size(), names.data(), names.size()};
    store.reset(4u);
    const bool repeatedResetDisabled = !store.valid(newRoot, error) &&
        error.code == EnvironmentErrorCode::InvalidStorage;
    const bool repeatedGenerationRejected = !store.configure(
        environmentStorage, 4u, error) &&
        error.code == EnvironmentErrorCode::StaleGeneration;
    EnvironmentId nextRoot = kInvalidEnvironmentId;
    const bool nextGenerationRecovered = store.configure(environmentStorage,
        5u, error) && store.createRoot(EnvironmentLimits{4u, 8u}, nextRoot,
            error) && runtimeHandleGeneration(nextRoot) == 5u &&
        !store.valid(newRoot, error) &&
        error.code == EnvironmentErrorCode::StaleGeneration;
    expect(repeatedResetDisabled && repeatedGenerationRejected &&
        nextGenerationRecovered,
        "environment reset cannot reissue a prior realm generation");
}

void testEnvironmentFailures()
{
    std::array<EnvironmentRecord, 2u> environmentRecords{};
    std::array<Binding, 2u> bindings{};
    std::array<char, 1u> names{};
    EnvironmentStore store;
    EnvironmentError error;
    const bool configured = store.configure(EnvironmentStorageView{
        environmentRecords.data(), environmentRecords.size(), bindings.data(),
        bindings.size(), names.data(), names.size()}, 9u, error);
    EnvironmentId root = kInvalidEnvironmentId;
    const bool rootCreated = configured && store.createRoot(
        EnvironmentLimits{2u, 4u}, root, error);
    const bool first = store.declare(root, SourceView("a", 1u),
        Value::number(1.0), error);
    const std::size_t countBefore = store.bindingCount();
    const std::size_t namesBefore = store.nameBytesUsed();
    const bool nameExhausted = !store.declare(root, SourceView("bb", 2u),
        Value::number(2.0), error);
    const EnvironmentErrorCode nameError = error.code;
    const bool noPartialName = store.bindingCount() == countBefore &&
        store.nameBytesUsed() == namesBefore &&
        store.resolveLocal(root, SourceView("bb", 2u), error) == nullptr;
    expect(rootCreated && first && nameExhausted && noPartialName &&
        nameError == EnvironmentErrorCode::CapacityExceeded,
        "name-arena exhaustion publishes no binding or name bytes");

    std::array<EnvironmentRecord, 1u> oneEnvironment{};
    std::array<Binding, 2u> spareBindings{};
    std::array<char, 8u> spareNames{};
    EnvironmentStore envBounded;
    const bool envConfigured = envBounded.configure(EnvironmentStorageView{
        oneEnvironment.data(), oneEnvironment.size(), spareBindings.data(),
        spareBindings.size(), spareNames.data(), spareNames.size()}, 2u, error);
    EnvironmentId onlyRoot = kInvalidEnvironmentId;
    const bool onlyRootCreated = envConfigured && envBounded.createRoot(
        EnvironmentLimits{2u, 8u}, onlyRoot, error);
    EnvironmentId noChild = kInvalidEnvironmentId;
    const bool envExhausted = !envBounded.create(onlyRoot,
        EnvironmentKind::Block, EnvironmentLimits{2u, 8u}, noChild, error);
    expect(onlyRootCreated && envExhausted &&
        error.code == EnvironmentErrorCode::CapacityExceeded &&
        envBounded.environmentCount() == 1u,
        "environment-pool exhaustion leaves no partial environment");

    std::array<EnvironmentRecord, 3u> recordSlots{};
    std::array<Binding, 2u> oneBinding{};
    std::array<char, 8u> enoughNames{};
    EnvironmentStore bindingBounded;
    const bool bindingConfigured = bindingBounded.configure(
        EnvironmentStorageView{recordSlots.data(), recordSlots.size(),
            oneBinding.data(), oneBinding.size(), enoughNames.data(),
            enoughNames.size()}, 5u, error);
    EnvironmentId bindingRoot = kInvalidEnvironmentId;
    const bool bindingRootCreated = bindingConfigured &&
        bindingBounded.createRoot(EnvironmentLimits{1u, 8u}, bindingRoot,
            error);
    const bool bindingFirst = bindingBounded.declare(bindingRoot,
        SourceView("a", 1u), Value::undefined(), error);
    const std::size_t bindingNamesBefore = bindingBounded.nameBytesUsed();
    const bool bindingLimit = !bindingBounded.declare(bindingRoot,
        SourceView("b", 1u), Value::number(2.0), error);
    expect(bindingRootCreated && bindingFirst && bindingLimit &&
        error.code == EnvironmentErrorCode::BindingLimitExceeded &&
        bindingBounded.bindingCount() == 1u &&
        bindingBounded.nameBytesUsed() == bindingNamesBefore,
        "per-environment binding limit fails before storage publication");

    std::array<EnvironmentRecord, 2u> invalidRecords{};
    EnvironmentStore invalid;
    expect(!invalid.configure(EnvironmentStorageView{
            nullptr, 1u, nullptr, 0u, nullptr, 0u}, 1u, error) &&
        error.code == EnvironmentErrorCode::InvalidStorage,
        "environment store rejects missing environment records");
    std::array<unsigned char, sizeof(EnvironmentRecord) + 1u> raw{};
    expect(!invalid.configure(EnvironmentStorageView{
            reinterpret_cast<EnvironmentRecord*>(raw.data() + 1u), 1u,
            nullptr, 0u, nullptr, 0u}, 1u, error) &&
        error.code == EnvironmentErrorCode::InvalidAlignment,
        "environment store rejects invalid record alignment");
    const std::uintptr_t nearEnd =
        std::numeric_limits<std::uintptr_t>::max() &
        ~(static_cast<std::uintptr_t>(alignof(EnvironmentRecord)) - 1u);
    expect(!invalid.configure(EnvironmentStorageView{
            reinterpret_cast<EnvironmentRecord*>(nearEnd), 1u,
            nullptr, 0u, nullptr, 0u}, 1u, error) &&
        error.code == EnvironmentErrorCode::SizeOverflow,
        "environment store rejects pointer-range overflow");

    EnvironmentStore invalidGeneration;
    expect(!invalidGeneration.configure(EnvironmentStorageView{
            invalidRecords.data(), invalidRecords.size(), nullptr, 0u,
            nullptr, 0u}, kInvalidRuntimeGeneration, error) &&
        error.code == EnvironmentErrorCode::InvalidStorage,
        "environment store rejects generation zero");
}

void testCAbiIntegrationProbe()
{
    environmentTestActive = true;
    std::array<EnvironmentRecord, 4u> environmentRecords{};
    std::array<Binding, 8u> bindings{};
    std::array<char, 32u> names{};
    std::array<RuntimeStringRecord, 4u> stringRecords{};
    std::array<char, 32u> stringBytes{};
    Js118RuntimeCoreProbeResult result;
    const std::uint32_t status = gxos_js118_runtime_core_probe(
        environmentRecords.data(), environmentRecords.size(), bindings.data(),
        bindings.size(), names.data(), names.size(), stringRecords.data(),
        stringRecords.size(), stringBytes.data(), stringBytes.size(), 13u,
        &result);
    expect(status == 0u && result.status == 0u &&
        result.root == makeRuntimeHandle(13u, 0u) &&
        result.child == makeRuntimeHandle(13u, 1u) &&
        result.text == makeRuntimeHandle(13u, 0u) &&
        result.environmentCount == 2u && result.bindingCount == 2u &&
        result.observedNumber == 3.0 && result.valueType ==
            static_cast<std::uint8_t>(ValueType::Number) &&
        result.observedText[0] == 'o' && result.observedText[1] == 'k' &&
        result.nameBytesUsed == 2u && result.stringCount == 1u &&
        result.stringBytesUsed == 2u,
        "C ABI qualification probe runs shared binding, TDZ, and string operations");
}

} // namespace

int main()
{
    testValueRecords();
    testStringStore();
    testEnvironmentOperations();
    testEnvironmentFailures();
    testCAbiIntegrationProbe();

    std::cout << "Portable Value checks: " << valueChecks << " passed\n";
    std::cout << "Portable Environment checks: " << environmentChecks
              << " passed\n";
    std::cout << "JS118 storage layout: Value=" << sizeof(Value)
              << ", StringRecord=" << sizeof(RuntimeStringRecord)
              << ", EnvironmentRecord=" << sizeof(EnvironmentRecord)
              << ", Binding=" << sizeof(Binding)
              << ", EnvironmentStore=" << sizeof(EnvironmentStore)
              << ", RuntimeStringStore=" << sizeof(RuntimeStringStore)
              << ", EnvironmentAdapter=" << sizeof(Environment) << " bytes\n";
    if (failures != 0u) {
        std::cerr << "JS118 portable runtime core: " << checks - failures
                  << '/' << checks << " passed; " << failures
                  << " failed\n";
        return 1;
    }
    std::cout << "JS118 portable runtime core: " << checks << '/' << checks
              << " passed\n";
    return 0;
}
