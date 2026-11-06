#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_transition_attribution.h"

#include <cstdio>
#include <cstring>

namespace {
uint32_t g_assertions = 0u;

bool check(bool condition, const char* name) {
    ++g_assertions;
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", name);
        return false;
    }
    std::printf("PASS %s\n", name);
    return true;
}

GuideXosNativeAotTransitionAttributionRecord readyRecord() {
    GuideXosNativeAotTransitionAttributionRecord record = {};
    record.tlsReady = 1u;
    record.runtimeStartupReady = 1u;
    record.runtimeFlsCellReady = 1u;
    record.rhInitializeReady = 1u;
    record.runtimeInstanceReady = 1u;
    record.threadStoreReady = 1u;
    record.rawCurrentThread = 0x1000u;
    record.observedOsThreadId = 0x42u;
    record.flsIndexReady = 1u;
    record.flsCurrentThreadState = GX_TRANSITION_PAL_FLS_EMPTY;
    record.currentThreadAttachment = GX_TRANSITION_ATTACHMENT_ABSENT;
    record.cooperativeState = GX_TRANSITION_COOP_STOCK_ATTACH_WILL_INITIALIZE;
    record.transitionFrameWillBeCreatedByStock = 1u;
    record.palStackBoundsReady = 1u;
    record.currentPointerInStackBounds = 1u;
    record.gcHeapReady = 1u;
    record.osModuleRegistered = 1u;
    record.codeManagerRegistered = 1u;
    record.modulesNotInitialized = 1u;
    return record;
}

bool expectGate(GuideXosNativeAotTransitionAttributionRecord record,
                uint32_t expected, const char* name) {
    return check(GuideXosTransitionEvaluateGate(&record) == expected, name);
}
}

int main() {
    bool ok = true;
    auto record = readyRecord();
    ok &= expectGate(record, GX_TRANSITION_GATE_NONE,
        "synthetic stock attachment path passes with zeroed TLS Thread");

    record.threadStoreReady = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_THREADSTORE,
        "missing ThreadStore publication blocks transition");

    record = readyRecord();
    record.currentThreadAttachment = GX_TRANSITION_ATTACHMENT_PRESENT;
    record.threadStateFlags = 1u;
    record.nativeThreadId = record.observedOsThreadId;
    record.flsCurrentThreadState = GX_TRANSITION_PAL_FLS_MATCHES_CURRENT_THREAD;
    record.transitionFrame = ~static_cast<uintptr_t>(0);
    record.transitionFrameReady = 1u;
    record.transitionFrameAligned = 1u;
    record.transitionFrameWillBeCreatedByStock = 0u;
    record.cooperativeState = GX_TRANSITION_COOP_PREEMPTIVE;
    ok &= expectGate(record, GX_TRANSITION_GATE_NONE,
        "synthetic attached preemptive thread with top-of-stack marker passes");

    record.transitionFrame = 0x3000u;
    record.transitionFrameInStackBounds = 1u;
    ok &= expectGate(record, GX_TRANSITION_GATE_NONE,
        "synthetic attached thread with in-bounds aligned frame passes");

    record.transitionFrame = 0x5000u;
    record.transitionFrameInStackBounds = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_CURRENT_THREAD_STATE,
        "attached transition frame outside PAL stack bounds blocks");

    record.transitionFrame = ~static_cast<uintptr_t>(0);
    record.transitionFrameInStackBounds = 0u;
    record.transitionFrame = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_CURRENT_THREAD_STATE,
        "missing transition frame blocks attached thread");

    record.transitionFrame = ~static_cast<uintptr_t>(0);
    record.cooperativeState = GX_TRANSITION_COOP_COOPERATIVE;
    ok &= expectGate(record, GX_TRANSITION_GATE_CURRENT_THREAD_STATE,
        "cooperative attached thread blocks reverse PInvoke");

    record = readyRecord();
    record.threadStateFlags = 8u;
    record.transitionFrameWillBeCreatedByStock = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_CURRENT_THREAD_STATE,
        "nonzero unattached thread flags block stock AttachCurrentThread early return");

    record = readyRecord();
    record.rhInitializeReady = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_RHINITIALIZE,
        "RhInitialize stage is an explicit transition prerequisite");

    record = readyRecord();
    record.tlsReady = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_TLS,
        "missing current-thread TLS blocks transition");

    record = readyRecord();
    record.observedOsThreadId = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_NATIVE_THREAD_ID,
        "missing native OS thread identity blocks transition");

    record = readyRecord();
    record.flsIndexReady = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_PAL_FLS,
        "PAL FLS index outside capacity blocks transition");

    record = readyRecord();
    record.flsCurrentThreadState = GX_TRANSITION_PAL_FLS_FOREIGN_THREAD;
    ok &= expectGate(record, GX_TRANSITION_GATE_PAL_FLS,
        "foreign PAL FLS value blocks unattached transition");

    record = readyRecord();
    record.palStackBoundsReady = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_STACK_BOUNDS,
        "missing PAL stack bounds blocks transition");

    record = readyRecord();
    record.currentPointerInStackBounds = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_STACK_BOUNDS,
        "current stack pointer outside PAL stack bounds blocks transition");

    record = readyRecord();
    record.gcHeapReady = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_GC_HEAP,
        "GC heap initialization stage is required");

    record = readyRecord();
    record.codeManagerRegistered = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_MODULE_REGISTRATION,
        "code manager registration is required");

    record = readyRecord();
    record.modulesNotInitialized = 0u;
    ok &= expectGate(record, GX_TRANSITION_GATE_MODULE_REGISTRATION,
        "already initialized module state blocks duplicate transition");

    const GuideXosTransitionCallSiteEntry sites[] = {
        { 0x1001u, 1u }, { 0x2002u, 2u }, { 0x3003u, 3u },
    };
    ok &= check(GuideXosTransitionResolveCallSite(0x2002u, sites, 3u) == 2u,
        "synthetic candidate RIP resolves to stable source call-site ID");
    ok &= check(GuideXosTransitionResolveCallSite(0x9999u, sites, 3u) ==
        kGuideXosTransitionCallSiteUnresolved,
        "unmapped caller RIP remains explicitly unresolved");

    record = readyRecord();
    record.fatalReason = 0x50414C06u;
    record.originalStatusAvailable = 0u;
    record.messageCaptureStatus = GX_TRANSITION_MESSAGE_UNAVAILABLE;
    record.messageBytes = 0u;
    record.messageTruncated = 0u;
    record.sourceCallSiteId = 1u;
    GuideXosTransitionAttributionSeal(&record);
    ok &= check(GuideXosTransitionAttributionRecordValid(&record),
        "fixed-size fatal record seals with stable call-site reason and unavailable text");

    record.messageCaptureStatus = GX_TRANSITION_MESSAGE_BOUNDED;
    record.messageBytes = 12u;
    record.messageTruncated = 0u;
    GuideXosTransitionAttributionSeal(&record);
    ok &= check(GuideXosTransitionAttributionRecordValid(&record),
        "synthetic bounded-message record is representable");

    record.messageCaptureStatus = GX_TRANSITION_MESSAGE_TRUNCATED;
    record.messageBytes = 64u;
    record.messageTruncated = 1u;
    GuideXosTransitionAttributionSeal(&record);
    ok &= check(GuideXosTransitionAttributionRecordValid(&record),
        "synthetic truncated-message record is representable");

    record.firstFailedStage = 0x0030u;
    record.firstFailureStatusAvailable = 1u;
    record.firstFailureStatus = 0x80004005u;
    record.fatalReason = 0x50414C06u;
    GuideXosTransitionAttributionSeal(&record);
    ok &= check(GuideXosTransitionAttributionRecordValid(&record) &&
        record.firstFailureStatus == 0x80004005u && record.fatalReason == 0x50414C06u,
        "first initialization failure remains separate from later fatal reason");

    record.initTraceOverflow = 1u;
    GuideXosTransitionAttributionSeal(&record);
    ok &= check(GuideXosTransitionAttributionRecordValid(&record) &&
        record.initTraceOverflow == 1u,
        "bounded-record overflow remains explicit in sealed fatal evidence");

    record.integrity ^= 1u;
    ok &= check(!GuideXosTransitionAttributionRecordValid(&record),
        "record integrity rejects post-seal mutation");

    std::printf("assertions=%u result=%s synthetic=1\n",
        g_assertions, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
