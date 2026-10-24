#include "navigator_javascript/runtime.h"
#include "tests/navigator_javascript_js86_model.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace gxos {
namespace javascript {

struct NavigatorScriptRuntimeTestAccess {
    static std::size_t runtimeObjectSize()
    {
        return sizeof(RuntimeContext::RuntimeObject);
    }
};

} // namespace javascript
} // namespace gxos

namespace {

using namespace gxos::javascript;
using namespace gxos::javascript::js86;
using MeasurementProfile =
    BoundedIdentityModel<1024u, 128u, 16u, 4096u>;

std::size_t checks = 0u;
std::size_t failures = 0u;

void expect(bool condition, const char* label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
}

void testPackedIdentityAndReuse()
{
    IdentityTable<2u, 3u> table;
    Status status = Status::None;
    ObjectHandle first;
    ObjectHandle second;
    expect(table.allocate(101u, first, status) && status == Status::None &&
        first.slot() == 0u && first.generation() == 1u,
        "first allocation packs slot zero and generation one");

    std::uint32_t payload = 0u;
    expect(table.resolve(first, payload, status) && payload == 101u &&
        status == Status::None,
        "valid handle resolves its original payload");

    const ObjectHandle copied = first;
    expect(copied == first && copied.raw() == first.raw(),
        "copied aliases retain exact object identity");

    expect(table.allocate(202u, second, status) && second.slot() == 1u &&
        second.generation() == 1u && second != first,
        "different live objects have distinct packed identities");

    ObjectHandle invalid = ObjectHandle::fromRawForTesting(0xffffffffu);
    expect(!table.resolve(invalid, payload, status) &&
        status == Status::InvalidHandle,
        "reserved all-ones encoding is rejected");

    invalid = ObjectHandle::fromRawForTesting(0u);
    expect(!table.resolve(invalid, payload, status) &&
        status == Status::InvalidHandle,
        "zero generation encoding is rejected");

    invalid = ObjectHandle::make(2u, 1u);
    expect(!table.resolve(invalid, payload, status) &&
        status == Status::InvalidHandle,
        "handle slot outside the bounded table is rejected");

    invalid = ObjectHandle::make(0u, 4u);
    expect(!table.resolve(invalid, payload, status) &&
        status == Status::InvalidHandle,
        "generation above the configured model limit is rejected");

    expect(table.retire(first, status) && status == Status::None,
        "live object retires once");
    expect(!table.retire(first, status) && status == Status::StaleHandle,
        "double retirement is rejected as stale");

    ObjectHandle replacement;
    expect(table.allocate(303u, replacement, status) &&
        replacement.slot() == first.slot() &&
        replacement.generation() == first.generation() + 1u &&
        replacement != first,
        "reused slot receives a new generation");
    expect(!table.resolve(first, payload, status) &&
        status == Status::StaleHandle,
        "retired handle cannot resolve its replacement");
    expect(!table.resolve(copied, payload, status) &&
        status == Status::StaleHandle,
        "every copied alias becomes stale after retirement");
    expect(table.resolve(replacement, payload, status) && payload == 303u,
        "replacement handle resolves only its replacement payload");

    ObjectHandle failed;
    expect(!table.allocate(404u, failed, status) &&
        status == Status::SlotTableFull && failed.isInvalid(),
        "failed full-table allocation publishes no handle");

    table.reset();
    expect(!table.resolve(second, payload, status) &&
        status == Status::StaleHandle && table.liveCount() == 0u,
        "model reset invalidates live handles");
    ObjectHandle afterReset;
    expect(table.allocate(505u, afterReset, status) &&
        afterReset.slot() == 0u && afterReset.generation() == 3u &&
        afterReset != replacement,
        "post-reset allocation advances rather than aliases a prior generation");
    expect(!table.resolve(replacement, payload, status) &&
        status == Status::StaleHandle,
        "pre-reset handle cannot resolve the post-reset allocation");
}

void testGenerationExhaustion()
{
    IdentityTable<1u, 2u> table;
    Status status = Status::None;
    ObjectHandle generationOne;
    ObjectHandle generationTwo;
    std::uint32_t payload = 0u;
    expect(table.allocate(1u, generationOne, status) &&
        generationOne.generation() == 1u,
        "generation exhaustion model starts at one");
    expect(table.retire(generationOne, status),
        "first generation retires cleanly");
    expect(table.allocate(2u, generationTwo, status) &&
        generationTwo.generation() == 2u,
        "maximum configured generation is allocatable");
    expect(table.retire(generationTwo, status),
        "maximum generation retires cleanly");

    ObjectHandle failed;
    expect(!table.allocate(3u, failed, status) &&
        status == Status::GenerationExhausted && failed.isInvalid() &&
        table.quarantinedCount() == 1u,
        "exhausted slot is quarantined instead of wrapping");
    expect(!table.resolve(generationOne, payload, status) &&
        status == Status::StaleHandle,
        "generation one remains stale after exhaustion");
    expect(!table.resolve(generationTwo, payload, status) &&
        status == Status::StaleHandle,
        "maximum-generation handle remains stale after exhaustion");
}

using RootTestObjects = IdentityTable<2u, 3u>;
using RootTestRegistry = RootRegistry<2u, 2u, 3u>;
using RootTestScope = NativeRootScope<RootTestRegistry, RootTestObjects>;

void testRootCopiesTokensAndOwners()
{
    RootTestObjects objects;
    Status status = Status::None;
    ObjectHandle value;
    expect(objects.allocate(71u, value, status),
        "root test allocates a stable object handle");

    RootTestRegistry roots;
    expect(sizeof(RootTestRegistry::RootRecord) == 20u &&
        sizeof(OwnerToken) == 8u && sizeof(RootToken) == 16u,
        "root record, owner token, and registration token layouts are bounded");
    RootTestScope ownerA(roots, objects, status);
    RootToken rootA;
    expect(ownerA.active() && ownerA.registerValue(value, rootA, status),
        "native owner A explicitly registers its Value copy");

    const ObjectHandle alias = value;
    RootTestScope ownerB(roots, objects, status);
    RootToken rootB;
    expect(ownerB.active() && ownerB.registerValue(alias, rootB, status) &&
        roots.activeRootCount(value) == 2u,
        "independent copied alias receives an independent root record");

    expect(ownerA.release(rootA, status) &&
        roots.activeRootCount(value) == 1u,
        "releasing owner A leaves owner B registered");
    expect(!ownerA.release(rootA, status) &&
        status == Status::StaleRootToken &&
        roots.activeRootCount(value) == 1u,
        "duplicate release is detected without changing owner B");
    expect(roots.releaseRoot(RootToken(), status) == false &&
        status == Status::InvalidRootToken,
        "zero or reserved root token is rejected");

    const OwnerToken closedOwner = ownerA.owner();
    expect(ownerA.close(status) && !roots.hasActiveOwner(closedOwner),
        "native frame close invalidates its owner token");

    RootToken invalidRoot;
    expect(!roots.registerRoot(value, OwnerToken{7u, 1u}, objects,
            invalidRoot, status) && status == Status::InvalidOwner,
        "out-of-range owner token is rejected");
    expect(!roots.registerRoot(value, closedOwner, objects, invalidRoot,
            status) && status == Status::StaleOwner,
        "closed owner cannot register a late Value");

    expect(ownerB.release(rootB, status) &&
        roots.activeRootCount(value) == 0u,
        "final independent root release removes the model root");
    expect(ownerB.close(status),
        "second native owner closes after its independent release");

    RootTestScope ownerC(roots, objects, status);
    RootToken rootC;
    expect(ownerC.registerValue(value, rootC, status) &&
        rootC.slot == rootA.slot && rootC.generation != rootA.generation,
        "reused root record receives a fresh registration generation");
    expect(!roots.releaseRoot(rootA, status) &&
        status == Status::StaleRootToken &&
        roots.activeRootCount(value) == 1u,
        "stale token cannot release a later owner's root");
    expect(ownerC.release(rootC, status) && ownerC.close(status) &&
        roots.activeRootCount() == 0u && roots.activeOwnerCount() == 0u,
        "controlled owner scopes leave no active roots or owners");
}

void testRootSaturationAndGenerationFailure()
{
    IdentityTable<1u, 3u> objects;
    RootRegistry<2u, 2u, 3u> roots;
    Status status = Status::None;
    ObjectHandle value;
    expect(objects.allocate(8u, value, status),
        "saturation test allocates an object");
    NativeRootScope<decltype(roots), decltype(objects)> owner(roots, objects,
        status);
    RootToken first;
    RootToken second;
    RootToken overflow;
    expect(owner.registerValue(value, first, status) &&
        owner.registerValue(value, second, status),
        "root registry fills to its declared capacity");
    expect(!owner.registerValue(value, overflow, status) &&
        status == Status::RootTableFull && overflow.slot == 0xffffffffu &&
        roots.activeRootCount(value) == 2u,
        "saturation rejects without overwriting active roots");
    expect(owner.release(first, status) && owner.release(second, status) &&
        owner.close(status) && roots.activeRootCount() == 0u,
        "root capacity is reusable after explicit release");

    RootRegistry<2u, 1u, 3u> ownerLimited;
    NativeRootScope<decltype(ownerLimited), decltype(objects)> onlyOwner(
        ownerLimited, objects, status);
    NativeRootScope<decltype(ownerLimited), decltype(objects)> overflowOwner(
        ownerLimited, objects, status);
    expect(onlyOwner.active() && !overflowOwner.active() &&
        status == Status::OwnerTableFull &&
        ownerLimited.activeOwnerCount() == 1u,
        "owner saturation is deterministic and preserves the active owner");
    expect(onlyOwner.close(status) &&
        ownerLimited.activeOwnerCount() == 0u,
        "owner scope releases its owner slot");

    IdentityTable<1u, 3u> tinyObjects;
    ObjectHandle tinyValue;
    expect(tinyObjects.allocate(9u, tinyValue, status),
        "token exhaustion test allocates an object");
    RootRegistry<1u, 1u, 2u> tinyRoots;
    NativeRootScope<decltype(tinyRoots), decltype(tinyObjects)> tinyOwner(
        tinyRoots, tinyObjects, status);
    RootToken tokenOne;
    RootToken tokenTwo;
    RootToken tokenThree;
    expect(tinyOwner.registerValue(tinyValue, tokenOne, status) &&
        tinyOwner.release(tokenOne, status) &&
        tinyOwner.registerValue(tinyValue, tokenTwo, status) &&
        tokenTwo.generation == 2u && tinyOwner.release(tokenTwo, status),
        "root token reaches its maximum generation without wrapping");
    expect(!tinyOwner.registerValue(tinyValue, tokenThree, status) &&
        status == Status::GenerationExhausted &&
        tokenThree.slot == 0xffffffffu &&
        tinyRoots.activeRootCount() == 0u,
        "exhausted root record is quarantined and fails closed");
    expect(tinyOwner.close(status), "root generation test closes its owner");
    NativeRootScope<decltype(tinyRoots), decltype(tinyObjects)> ownerTwo(
        tinyRoots, tinyObjects, status);
    expect(ownerTwo.active() && ownerTwo.close(status),
        "owner slot can advance to its maximum configured generation");
    NativeRootScope<decltype(tinyRoots), decltype(tinyObjects)> ownerThree(
        tinyRoots, tinyObjects, status);
    expect(!ownerThree.active() && status == Status::GenerationExhausted &&
        tinyRoots.activeOwnerCount() == 0u,
        "owner generation exhaustion refuses token reuse");
}

using CallbackModel = BoundedIdentityModel<4u, 8u, 8u, 4u, 3u>;
using CallbackScope =
    NativeRootScope<CallbackModel::Roots, CallbackModel::Objects>;

bool simulateCallbackErrorUnwind(CallbackModel& model, ObjectHandle value)
{
    Status status = Status::None;
    CallbackScope nativeFrame(model.roots(), model.objects(), status);
    RootToken nativeRoot;
    if (!nativeFrame.registerValue(value, nativeRoot, status)) return false;
    CallbackScope callbackFrame(model.roots(), model.objects(), status);
    RootToken callbackRoot;
    if (!callbackFrame.registerValue(value, callbackRoot, status)) return false;
    CallbackScope nestedEvent(model.roots(), model.objects(), status);
    RootToken eventRoot;
    if (!nestedEvent.registerValue(value, eventRoot, status)) return false;
    return false; // Every nested scope unwinds as if callback execution failed.
}

void testNestedCallbacksEventsAndEscapingValues()
{
    CallbackModel model;
    Status status = Status::None;
    ObjectHandle value;
    expect(model.allocate(18u, value, status),
        "callback/Event model allocates a retained object");

    CallbackScope eventA(model.roots(), model.objects(), status);
    RootToken rootA;
    expect(eventA.registerValue(value, rootA, status),
        "outer Event A owns its registered root");
    CallbackScope callback(model.roots(), model.objects(), status);
    RootToken rootB;
    expect(callback.registerValue(value, rootB, status),
        "callback owns a nested native root");
    CallbackScope eventB(model.roots(), model.objects(), status);
    RootToken rootC;
    expect(eventB.registerValue(value, rootC, status) &&
        model.roots().activeRootCount(value) == 3u,
        "nested Event B owns a third independent root");
    expect(eventB.close(status) &&
        model.roots().activeRootCount(value) == 2u,
        "Event B exit releases only its own root");
    expect(callback.close(status) &&
        model.roots().activeRootCount(value) == 1u,
        "callback exit preserves outer Event A");
    expect(eventA.active() && model.roots().hasActiveOwner(eventA.owner()),
        "outer Event owner remains active through inner exits");
    expect(eventA.close(status) && model.roots().activeRootCount() == 0u,
        "outer Event exit releases the final nested root");

    CallbackScope caller(model.roots(), model.objects(), status);
    RootToken borrowedRoot;
    expect(caller.registerValue(value, borrowedRoot, status),
        "native caller roots a temporary callback argument");
    const ObjectHandle borrowedArgument = value;
    expect(borrowedArgument == value &&
        model.mayRetire(borrowedArgument, RootCoverage::CompleteForModel,
            status) == false &&
        status == Status::ObjectStillReachable,
        "synchronous borrowed argument remains protected by its caller");

    CallbackScope embedding(model.roots(), model.objects(), status);
    RootToken escapedResult;
    expect(embedding.registerValue(value, escapedResult, status),
        "embedding registers a Value before it escapes the evaluator frame");
    expect(caller.close(status) &&
        model.roots().activeRootCount(value) == 1u &&
        !model.mayRetire(value, RootCoverage::CompleteForModel, status) &&
        status == Status::ObjectStillReachable,
        "embedding-held result outlives the native caller registration");
    expect(embedding.release(escapedResult, status) &&
        embedding.close(status) && model.roots().activeRootCount() == 0u,
        "embedding releases its root at its declared ownership boundary");

    expect(!simulateCallbackErrorUnwind(model, value) &&
        model.roots().activeRootCount() == 0u &&
        model.roots().activeOwnerCount() == 0u,
        "callback/Event error unwind releases every nested registration");
}

void testCombinedRetirementAndReset()
{
    BoundedIdentityModel<2u, 4u, 4u, 4u, 3u> model;
    Status status = Status::None;
    ObjectHandle oldHandle;
    expect(model.allocate(111u, oldHandle, status) &&
        oldHandle.slot() == 0u && oldHandle.generation() == 1u,
        "combined proof allocates X with a generation-tagged handle");

    using ModelType = decltype(model);
    NativeRootScope<typename ModelType::Roots, typename ModelType::Objects> rootA(
        model.roots(), model.objects(), status);
    NativeRootScope<typename ModelType::Roots, typename ModelType::Objects> rootB(
        model.roots(), model.objects(), status);
    RootToken rootTokenA;
    RootToken rootTokenB;
    expect(rootA.registerValue(oldHandle, rootTokenA, status) &&
        rootB.registerValue(ObjectHandle(oldHandle), rootTokenB, status),
        "two independent owners register aliases for X");
    expect(!model.retire(oldHandle, RootCoverage::CompleteForModel, status) &&
        status == Status::ObjectStillReachable,
        "active root registrations deny simulated retirement");

    expect(rootA.close(status) &&
        !model.retire(oldHandle, RootCoverage::CompleteForModel, status) &&
        status == Status::ObjectStillReachable,
        "releasing one alias leaves the other root authoritative");
    expect(rootB.close(status) &&
        !model.mayRetire(oldHandle, RootCoverage::UnknownExternalRoots,
            status) && status == Status::UnknownRoots,
        "unknown external roots fail closed after registered roots end");
    expect(model.retire(oldHandle, RootCoverage::CompleteForModel, status) &&
        status == Status::None,
        "complete bounded model proof permits only simulated retirement");

    ObjectHandle replacement;
    std::uint32_t payload = 0u;
    expect(model.allocate(222u, replacement, status) &&
        replacement.slot() == oldHandle.slot() &&
        replacement.generation() == oldHandle.generation() + 1u,
        "slot replacement Y gets a distinct generation");
    expect(!model.objects().resolve(oldHandle, payload, status) &&
        status == Status::StaleHandle,
        "old H1 cannot resolve replacement Y");
    expect(model.objects().resolve(replacement, payload, status) &&
        payload == 222u,
        "new handle resolves replacement Y");

    using ResetModel = BoundedIdentityModel<2u, 2u, 2u, 2u, 3u>;
    ResetModel resetModel;
    ObjectHandle beforeReset;
    expect(resetModel.allocate(1u, beforeReset, status),
        "reset model allocates a pre-reset object");
    using ResetScope =
        NativeRootScope<ResetModel::Roots, ResetModel::Objects>;
    ResetScope oldScope(resetModel.roots(), resetModel.objects(), status);
    RootToken beforeToken;
    expect(oldScope.registerValue(beforeReset, beforeToken, status),
        "reset model registers a pre-reset owner");
    resetModel.reset();
    expect(resetModel.roots().activeRootCount() == 0u &&
        resetModel.roots().activeOwnerCount() == 0u &&
        resetModel.objects().liveCount() == 0u,
        "model reset clears live roots, owners, graph, and allocations");
    ObjectHandle afterReset;
    expect(resetModel.allocate(2u, afterReset, status) &&
        afterReset.slot() == beforeReset.slot() &&
        afterReset.generation() == beforeReset.generation() + 1u &&
        !resetModel.objects().resolve(beforeReset, payload, status) &&
        status == Status::StaleHandle,
        "reset keeps the per-slot generation history");
    RootToken lateRegistration;
    expect(!oldScope.registerValue(afterReset, lateRegistration, status) &&
        status == Status::StaleOwner,
        "pre-reset owner cannot register against the new model epoch");
    expect(!resetModel.roots().releaseRoot(beforeToken, status) &&
        status == Status::StaleRootToken,
        "pre-reset root token remains invalid");
    expect(!oldScope.close(status) && status == Status::StaleOwner,
        "stale scope closes with a detectable reset error");
}

void testBoundedReferenceGraph()
{
    BoundedIdentityModel<7u, 4u, 4u, 8u, 3u> model;
    Status status = Status::None;
    std::array<ObjectHandle, 7u> objects{};
    bool allocated = true;
    for (std::size_t index = 0u; index < objects.size(); ++index)
        allocated = model.allocate(static_cast<std::uint32_t>(index + 1u),
            objects[index], status) && allocated;
    expect(allocated, "bounded graph allocates every configured node");

    using ModelType = decltype(model);
    NativeRootScope<ModelType::Roots, ModelType::Objects> root(
        model.roots(), model.objects(), status);
    RootToken rootToken;
    expect(root.registerValue(objects[0], rootToken, status),
        "graph root explicitly registers node A");
    expect(model.addEdge(objects[0], objects[1], EdgeKind::ObjectProperty,
            status) &&
        model.addEdge(objects[0], objects[2], EdgeKind::ArrayElement,
            status) &&
        model.addEdge(objects[1], objects[3], EdgeKind::ObjectProperty,
            status) &&
        model.addEdge(objects[3], objects[1], EdgeKind::ObjectProperty,
            status) &&
        model.addEdge(objects[2], objects[4], EdgeKind::ArrayElement,
            status),
        "property and Array edges form a rooted chain with a reachable cycle");
    expect(model.addEdge(objects[0], objects[1], EdgeKind::ObjectProperty,
            status) && model.graph().activeEdgeCount() == 6u,
        "duplicate reference edges are bounded and accepted");
    expect(!model.mayRetire(objects[4], RootCoverage::CompleteForModel,
            status) && status == Status::ObjectStillReachable,
        "bounded traversal follows roots through chains and duplicate edges");

    expect(model.addEdge(objects[5], objects[6], EdgeKind::ObjectProperty,
            status) &&
        model.addEdge(objects[6], objects[5], EdgeKind::ArrayElement,
            status),
        "unrooted cyclic component is represented with bounded edges");
    expect(model.retire(objects[5], RootCoverage::CompleteForModel, status) &&
        model.graph().activeEdgeCount() == 6u,
        "unrooted simulated cycle can retire after complete model proof");
    expect(model.retire(objects[6], RootCoverage::CompleteForModel, status),
        "remaining unrooted cycle member is independently reclaimable in model");
    expect(!model.retire(objects[4], RootCoverage::UnknownExternalRoots,
            status) && status == Status::UnknownRoots,
        "unknown external root blocks an otherwise unreachable graph node");
    expect(root.close(status) && model.roots().activeRootCount() == 0u,
        "graph test releases its root registration");

    BoundedIdentityModel<2u, 2u, 2u, 2u, 3u> corrupt;
    ObjectHandle one;
    ObjectHandle two;
    expect(corrupt.allocate(1u, one, status) &&
        corrupt.allocate(2u, two, status),
        "invalid-edge model allocates bounded endpoints");
    expect(corrupt.graph().injectInvalidEdgeForTesting(0u, one,
            ObjectHandle::make(7u, 1u)),
        "test-only corruption hook installs one invalid graph edge");
    expect(!corrupt.mayRetire(two, RootCoverage::CompleteForModel, status) &&
        status == Status::InvalidGraphEdge &&
        corrupt.objects().liveCount() == 2u,
        "invalid graph edge prevents retirement without changing pool state");

    IdentityTable<3u, 3u> capacityObjects;
    ReferenceGraph<3u, 2u> capacityGraph;
    ObjectHandle a;
    ObjectHandle b;
    ObjectHandle c;
    expect(capacityObjects.allocate(1u, a, status) &&
        capacityObjects.allocate(2u, b, status) &&
        capacityObjects.allocate(3u, c, status),
        "graph-capacity test allocates three nodes");
    expect(capacityGraph.addEdge(a, b, EdgeKind::ObjectProperty,
            capacityObjects, status) &&
        capacityGraph.addEdge(b, c, EdgeKind::ArrayElement,
            capacityObjects, status),
        "graph edge table fills to its exact bound");
    expect(!capacityGraph.addEdge(c, a, EdgeKind::Prototype,
            capacityObjects, status) && status == Status::GraphFull &&
        capacityGraph.activeEdgeCount() == 2u,
        "graph saturation fails without publishing a partial edge");
    RootRegistry<3u, 3u, 3u> capacityRoots;
    OwnerToken capacityOwner;
    expect(capacityRoots.beginOwner(capacityOwner, status),
        "bounded graph obtains a root owner");
    RootToken capacityToken;
    expect(capacityRoots.registerRoot(a, capacityOwner, capacityObjects,
            capacityToken, status),
        "bounded graph registers its root");
    bool reachable = false;
    expect(capacityGraph.reachesFromRoots(c, capacityObjects, capacityRoots,
            reachable, status) && reachable,
        "bounded traversal resolves the full two-edge path without overflow");
    expect(capacityRoots.endOwner(capacityOwner, status),
        "bounded graph closes the owner scope");
}

void testMaximumProfileTraversalBound()
{
    static MeasurementProfile model;
    Status status = Status::None;
    ObjectHandle object;
    expect(model.allocate(1u, object, status),
        "full-capacity profile allocates a bounded graph node");
    NativeRootScope<MeasurementProfile::Roots, MeasurementProfile::Objects>
        root(model.roots(), model.objects(), status);
    RootToken token;
    expect(root.registerValue(object, token, status),
        "full-capacity profile registers its root");
    expect(!model.mayRetire(object, RootCoverage::CompleteForModel, status) &&
        status == Status::ObjectStillReachable,
        "1,024-node profile traversal fails safely when the target is rooted");
    expect(root.close(status) && model.roots().activeRootCount() == 0u,
        "full-capacity profile closes without leaked registrations");
}

template <typename Operation>
double nanosecondsPerOperation(std::size_t iterations, Operation operation)
{
    volatile std::uint64_t sink = 0u;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t index = 0u; index < iterations; ++index)
        sink += static_cast<std::uint64_t>(operation(index));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    (void)sink;
    const double nanoseconds =
        std::chrono::duration<double, std::nano>(elapsed).count();
    return nanoseconds / static_cast<double>(iterations);
}

void reportMeasurements()
{
    using ProfileObjects = MeasurementProfile::Objects;
    using ProfileRoots = MeasurementProfile::Roots;
    using ProfileGraph = MeasurementProfile::Graph;
    using BenchObjects = IdentityTable<1024u>;
    using BenchRoots = RootRegistry<128u, 16u>;

    std::cout << "JS86 handle layout: width=" << sizeof(ObjectHandle) * 8u
        << " bits slotBits=" << kHandleSlotBits
        << " generationBits=" << kHandleGenerationBits
        << " maxSlots=" << kHandleSlotCapacity
        << " maxGeneration=" << kMaximumHandleGeneration << '\n';
    std::cout << "JS86 model capacities: slots="
        << ProfileObjects::capacity() << " roots="
        << ProfileRoots::rootCapacity() << " owners="
        << ProfileRoots::ownerCapacity() << " graphNodes="
        << ProfileGraph::nodeCapacity() << " graphEdges="
        << ProfileGraph::edgeCapacity() << '\n';
    std::cout << "JS86 model bytes: slotRecord="
        << sizeof(ProfileObjects::SlotRecord)
        << " generationMetadataAt1024=" <<
            ProfileObjects::capacity() * sizeof(std::uint32_t)
        << " identityTable=" << sizeof(ProfileObjects)
        << " rootRecord=" << sizeof(ProfileRoots::RootRecord)
        << " rootTable=" << sizeof(ProfileRoots)
        << " graphEdge=" << sizeof(ProfileGraph::EdgeRecord)
        << " graph=" << sizeof(ProfileGraph)
        << " completeModel=" << sizeof(MeasurementProfile) << '\n';
    std::cout << "JS86 production layouts (host compiler): RuntimeObjectId="
        << sizeof(RuntimeObjectId) << " Value=" << sizeof(Value)
        << " RuntimeObject=" << NavigatorScriptRuntimeTestAccess::
            runtimeObjectSize()
        << " bytes; production pool capacities: objects="
        << kDefaultMaxJavaScriptRuntimeObjects << " properties="
        << kDefaultMaxJavaScriptRuntimeProperties << " arrays="
        << kDefaultMaxJavaScriptArrayElementCount << " environments="
        << kDefaultMaxJavaScriptEnvironments << " hostHandles="
        << kDefaultMaxJavaScriptHostObjects << '\n';

    static BenchObjects objects;
    Status status = Status::None;
    static std::array<ObjectHandle, 1024u> handles{};
    bool filled = true;
    for (std::size_t index = 0u; index < handles.size(); ++index)
        filled = objects.allocate(static_cast<std::uint32_t>(index),
            handles[index], status) && filled;
    expect(filled && objects.liveCount() == handles.size(),
        "performance profile fills the exact object-table capacity");
    const ObjectHandle last = handles.back();
    const ObjectHandle stale =
        ObjectHandle::make(last.slot(), last.generation() - 1u);
    const ObjectHandle malformed = ObjectHandle::fromRawForTesting(0u);
    const std::size_t iterations = 50000u;
    const double constructNs = nanosecondsPerOperation(iterations,
        [](std::size_t index) {
            return ObjectHandle::make(
                static_cast<std::uint32_t>(index) & kHandleSlotMask, 1u).raw();
        });
    const double resolveNs = nanosecondsPerOperation(iterations,
        [last](std::size_t) {
            std::uint32_t payload = 0u;
            Status local = Status::None;
            return objects.resolve(last, payload, local) ? payload + 1u : 0u;
        });
    const double rejectNs = nanosecondsPerOperation(iterations,
        [stale](std::size_t) {
            std::uint32_t payload = 0u;
            Status local = Status::None;
            return objects.resolve(stale, payload, local) ? 0u : 1u;
        });
    const double invalidRejectNs = nanosecondsPerOperation(iterations,
        [malformed](std::size_t) {
            std::uint32_t payload = 0u;
            Status local = Status::None;
            return objects.resolve(malformed, payload, local) ? 0u : 1u;
        });

    static BenchRoots roots;
    NativeRootScope<BenchRoots, BenchObjects> owner(roots, objects, status);
    const double registerReleaseNs = nanosecondsPerOperation(iterations,
        [&owner, last](std::size_t) {
            RootToken token;
            Status local = Status::None;
            if (!owner.registerValue(last, token, local)) return 0u;
            return owner.release(token, local) ? 1u : 0u;
        });

    static BenchRoots fullRoots;
    NativeRootScope<BenchRoots, BenchObjects> fullOwner(
        fullRoots, objects, status);
    static std::array<RootToken, BenchRoots::rootCapacity()> rootTokens{};
    bool rootsFilled = true;
    for (RootToken& token : rootTokens)
        rootsFilled = fullOwner.registerValue(last, token, status) &&
            rootsFilled;
    expect(rootsFilled &&
        fullRoots.activeRootCount(last) == BenchRoots::rootCapacity(),
        "performance profile fills the exact root registry capacity");
    const double maximumLookupNs = nanosecondsPerOperation(iterations,
        [last](std::size_t) {
            return fullRoots.activeRootCount(last);
        });

    static BenchRoots nestedRoots;
    const double nestedRegistrationNs = nanosecondsPerOperation(10000u,
        [last](std::size_t) {
            Status local = Status::None;
            NativeRootScope<BenchRoots, BenchObjects> outer(
                nestedRoots, objects, local);
            // Each nested owner models a synchronous callback/Event frame.
            RootToken one;
            if (!outer.registerValue(last, one, local)) return 0u;
            NativeRootScope<BenchRoots, BenchObjects> middle(
                nestedRoots, objects, local);
            RootToken two;
            if (!middle.registerValue(last, two, local)) return 0u;
            NativeRootScope<BenchRoots, BenchObjects> inner(
                nestedRoots, objects, local);
            RootToken three;
            return inner.registerValue(last, three, local) ? 1u : 0u;
        });
    expect(nestedRoots.activeRootCount() == 0u &&
        nestedRoots.activeOwnerCount() == 0u,
        "nested registration benchmark leaves no active roots or owners");

    double exhaustionNs = 0.0;
    constexpr std::size_t exhaustionTrials = 2000u;
    volatile std::uint64_t exhaustionSink = 0u;
    const auto exhaustionStart = std::chrono::steady_clock::now();
    for (std::size_t trial = 0u; trial < exhaustionTrials; ++trial) {
        IdentityTable<1u, 8u> tiny;
        ObjectHandle handle;
        Status local = Status::None;
        for (std::uint32_t generation = 0u; generation < 8u; ++generation) {
            const std::uint32_t payload = static_cast<std::uint32_t>(trial) +
                generation;
            if (!tiny.allocate(payload, handle, local)) break;
            exhaustionSink += handle.raw();
            if (!tiny.retire(handle, local)) break;
        }
        ObjectHandle denied;
        (void)tiny.allocate(9u, denied, local);
        exhaustionSink += static_cast<std::uint64_t>(
            tiny.quarantinedCount() + (denied.isInvalid() ? 1u : 0u));
    }
    (void)exhaustionSink;
    exhaustionNs = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - exhaustionStart).count() /
        static_cast<double>(exhaustionTrials);

    std::cout << std::fixed << std::setprecision(1)
        << "JS86 model benchmark (ns/op, 50k iterations): handleConstruct="
        << constructNs << " resolveAt1024=" << resolveNs
        << " invalidReject=" << invalidRejectNs
        << " staleReject=" << rejectNs
        << " registerReleasePair=" << registerReleaseNs
        << " nestedThreeScopes=" << nestedRegistrationNs
        << " full128RootLookup=" << maximumLookupNs
        << " generationExhaustionProbe=" << exhaustionNs << '\n';
    (void)owner.close(status);
    for (RootToken token : rootTokens) (void)fullOwner.release(token, status);
    (void)fullOwner.close(status);
}

} // namespace

int main()
{
    testPackedIdentityAndReuse();
    testGenerationExhaustion();
    testRootCopiesTokensAndOwners();
    testRootSaturationAndGenerationFailure();
    testNestedCallbacksEventsAndEscapingValues();
    testCombinedRetirementAndReset();
    testBoundedReferenceGraph();
    testMaximumProfileTraversalBound();
    reportMeasurements();

    std::cout << "Navigator JavaScript JS86 checks: " << checks - failures
        << '/' << checks << " passed.\n";
    return failures == 0u ? 0 : 1;
}
