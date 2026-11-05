#include "guidexos_nativeaot_pal_contract.h"
#include "guidexos_nativeaot_gc_startup_platform_contract.h"
#include "guidexos_nativeaot_rh_initialize_contract.h"
#include "../platform/guidexos_nativeaot_init_trace.h"
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
#include "../platform/guidexos_nativeaot_threadstore_readiness.h"
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES)
#include "../platform/guidexos_nativeaot_gcinit_record.h"
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED)
#include "../platform/guidexos_nativeaot_semishared_record.h"
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
#include "../platform/guidexos_nativeaot_native_alloc_record.h"
#endif

/*
 * Minimal proof-image adapter for the existing kernel startup harness.
 * It installs the established PAL contracts and invokes the pinned
 * NativeAOT RhInitialize entry.  It deliberately supplies no Windows import
 * shims, allocator, runtime state, or collection implementation.
 */
extern "C" bool RhInitialize(bool isDll);

static unsigned int g_startupResult = 0xFFFFFFFFu;
static uint32_t g_rhInitializeRawResult = 0xFFFFFFFFu;

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
extern "C" uint32_t __cdecl GuideXosNativeAotCaptureInitSingletonBits();

namespace {
constexpr uint32_t kInitTraceCapacity = 64u;
struct InitTraceRecord {
    uint32_t invocation;
    uint32_t stage;
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    uint32_t eventCounts[4];
    uint32_t lastEvent;
    uint32_t lastResult;
    uint32_t lastSingletonBits;
#else
    uint32_t event;
    uint32_t result;
    uint32_t singletonBits;
#endif
};
InitTraceRecord g_initTrace[kInitTraceCapacity] = {};
uint32_t g_initTraceCount = 0u;
uint32_t g_initTraceOverflow = 0u;
uint32_t g_initInvocation = 0u;
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
uint32_t g_initTraceEventCount = 0u;
uint32_t g_initTraceCoalescedEventCount = 0u;
uint32_t g_initTraceFirstFailureStage = 0xFFFFFFFFu;
uint32_t g_initTraceFirstFailureResult = 0xFFFFFFFFu;
uint32_t g_initTraceFirstFailureSingletonBits = 0xFFFFFFFFu;
#endif
guidexos_nativeaot_init_trace_sink g_initTraceSink = nullptr;

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
void incrementSaturating(uint32_t* value) {
    if (*value == 0xFFFFFFFFu) {
        if (g_initTraceOverflow != 0xFFFFFFFFu) ++g_initTraceOverflow;
        return;
    }
    ++(*value);
}
#endif

uint32_t captureInitSingletonBits() {
    return GuideXosNativeAotCaptureInitSingletonBits();
}

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES)
void emitGcInitRecord() {
    if (g_initTraceSink == nullptr) return;
    const uint32_t invocation = GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_INVOCATION);
    for (uint32_t field = 0u; field < GX_GCINIT_FIELD_COUNT; ++field) {
        g_initTraceSink(invocation, GX_INIT_STAGE_GC_INTERNAL_RECORD,
            field, GuideXosNativeAotGcInitGetField(field), 0u);
    }
}
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED)
void emitSemiSharedRecord(uint32_t invocation) {
    if (g_initTraceSink == nullptr ||
        GuideXosNativeAotSemiSharedGetSummaryField(GX_SEMISHARED_SUMMARY_INVOCATION) != invocation) {
        return;
    }
    const uint32_t summaryPairs[][2] = {
        { GX_SEMISHARED_SUMMARY_MAGIC, GX_SEMISHARED_SUMMARY_VERSION },
        { GX_SEMISHARED_SUMMARY_OPERATION_COUNT, GX_SEMISHARED_SUMMARY_OVERFLOW },
        { GX_SEMISHARED_SUMMARY_FINAL_RESULT, GX_SEMISHARED_SUMMARY_FIRST_FAILED_OPERATION },
        { GX_SEMISHARED_SUMMARY_FIRST_FAILURE_BRANCH, GX_SEMISHARED_SUMMARY_FIRST_FAILURE_STATUS },
        { GX_SEMISHARED_SUMMARY_FIRST_FAILURE_STATUS_SOURCE, GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_OPERATION },
        { GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_STATUS, GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_STATUS_SOURCE },
        { GX_SEMISHARED_SUMMARY_CLEANUP_ENTERED_MASK, GX_SEMISHARED_SUMMARY_CLEANUP_COMPLETED_MASK },
    };
    for (uint32_t field = 0u; field < sizeof(summaryPairs) / sizeof(summaryPairs[0]); ++field) {
        g_initTraceSink(invocation, GX_INIT_STAGE_GC_SEMISHARED_RECORD,
            0x80000000u | field,
            GuideXosNativeAotSemiSharedGetSummaryField(summaryPairs[field][0]),
            GuideXosNativeAotSemiSharedGetSummaryField(summaryPairs[field][1]));
    }

    const uint32_t operationCount = GuideXosNativeAotSemiSharedGetOperationCount();
    for (uint32_t index = 0u; index < operationCount; ++index) {
        const uint32_t operationPairs[][2] = {
            { GX_SEMISHARED_OPERATION_FIELD_ID, GX_SEMISHARED_OPERATION_FIELD_STATE },
            { GX_SEMISHARED_OPERATION_FIELD_RESULT, GX_SEMISHARED_OPERATION_FIELD_DETAIL },
            { GX_SEMISHARED_OPERATION_FIELD_RESOURCE, GX_SEMISHARED_OPERATION_FIELD_REQUEST_RESOURCE },
            { GX_SEMISHARED_OPERATION_FIELD_SIZE_LOW, GX_SEMISHARED_OPERATION_FIELD_SIZE_HIGH },
            { GX_SEMISHARED_OPERATION_FIELD_ALIGNMENT_LOW, GX_SEMISHARED_OPERATION_FIELD_ALIGNMENT_HIGH },
            { GX_SEMISHARED_OPERATION_FIELD_NATIVE_STATUS, GX_SEMISHARED_OPERATION_FIELD_NATIVE_STATUS_SOURCE },
        };
        for (uint32_t field = 0u; field < sizeof(operationPairs) / sizeof(operationPairs[0]); ++field) {
            const uint32_t event = (index << 8) | field;
            const uint32_t value = field == 2u
                ? (GuideXosNativeAotSemiSharedGetOperationField(index, operationPairs[field][0]) & 0xFFFFu) |
                  ((GuideXosNativeAotSemiSharedGetOperationField(index, operationPairs[field][1]) & 0xFFFFu) << 16)
                : GuideXosNativeAotSemiSharedGetOperationField(index, operationPairs[field][0]);
            const uint32_t auxiliary = field == 2u
                ? GuideXosNativeAotSemiSharedGetOperationField(
                    index, GX_SEMISHARED_OPERATION_FIELD_RESOURCE_RESULT)
                : GuideXosNativeAotSemiSharedGetOperationField(index, operationPairs[field][1]);
            g_initTraceSink(invocation, GX_INIT_STAGE_GC_SEMISHARED_RECORD, event,
                value, auxiliary);
        }
    }
}
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
void emitNativeAllocationRecord(uint32_t invocation) {
    if (g_initTraceSink == nullptr) return;
    for (uint32_t field = 0u; field < GX_NATIVE_ALLOC_SUMMARY_FIELD_COUNT; ++field) {
        g_initTraceSink(invocation, GX_INIT_STAGE_GC_NATIVE_ALLOC_RECORD,
            0x80000000u | field,
            GuideXosNativeAotNativeAllocGetSummaryField(field), 0u);
    }
    const uint32_t count = GuideXosNativeAotNativeAllocGetAttemptCount();
    for (uint32_t attempt = 0u; attempt < count; ++attempt) {
        for (uint32_t field = 0u; field < GX_NATIVE_ALLOC_ATTEMPT_FIELD_COUNT; ++field) {
            const uint32_t event = (attempt << 8) | field;
            g_initTraceSink(invocation, GX_INIT_STAGE_GC_NATIVE_ALLOC_RECORD,
                event, GuideXosNativeAotNativeAllocGetAttemptField(attempt, field), 0u);
        }
    }
}
#endif
}

extern "C" void __cdecl guideXosNativeAotInitTraceSetInvocation(uint32_t invocation) {
    g_initInvocation = invocation;
    g_initTraceCount = 0u;
    g_initTraceOverflow = 0u;
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    g_initTraceEventCount = 0u;
    g_initTraceCoalescedEventCount = 0u;
    g_initTraceFirstFailureStage = 0xFFFFFFFFu;
    g_initTraceFirstFailureResult = 0xFFFFFFFFu;
    g_initTraceFirstFailureSingletonBits = 0xFFFFFFFFu;
    for (uint32_t index = 0u; index < kInitTraceCapacity; ++index) {
        g_initTrace[index] = InitTraceRecord{};
    }
#endif
}

extern "C" __declspec(dllexport) void __cdecl guideXosNativeAotInitTraceSetSink(
    guidexos_nativeaot_init_trace_sink sink) {
    g_initTraceSink = sink;
}

extern "C" void __cdecl guideXosNativeAotInitTraceRecord(
    uint32_t stage, uint32_t event, uint32_t result, uint32_t singletonBits) {
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    incrementSaturating(&g_initTraceEventCount);
    const uint32_t capturedSingletonBits = singletonBits == 0xFFFFFFFFu
        ? captureInitSingletonBits() : singletonBits;
    if (event == GX_INIT_EVENT_FAIL && g_initTraceFirstFailureStage == 0xFFFFFFFFu) {
        g_initTraceFirstFailureStage = stage;
        g_initTraceFirstFailureResult = result;
        g_initTraceFirstFailureSingletonBits = capturedSingletonBits;
    }

    uint32_t index = 0u;
    while (index < g_initTraceCount && g_initTrace[index].stage != stage) ++index;
    if (index == g_initTraceCount) {
        if (g_initTraceCount >= kInitTraceCapacity) {
            incrementSaturating(&g_initTraceOverflow);
            return;
        }
        ++g_initTraceCount;
        InitTraceRecord& fresh = g_initTrace[index];
        fresh = InitTraceRecord{};
        fresh.invocation = g_initInvocation;
        fresh.stage = stage;
    } else {
        incrementSaturating(&g_initTraceCoalescedEventCount);
    }

    InitTraceRecord& record = g_initTrace[index];
    uint32_t counterIndex = event == GX_INIT_EVENT_BEGIN ? 0u :
        event == GX_INIT_EVENT_PASS ? 1u :
        event == GX_INIT_EVENT_FAIL ? 2u :
        event == GX_INIT_EVENT_OBSERVE ? 3u : 4u;
    if (counterIndex >= 4u || record.eventCounts[counterIndex] >= 0x7Fu) {
        incrementSaturating(&g_initTraceOverflow);
    } else {
        ++record.eventCounts[counterIndex];
    }
    record.lastEvent = event;
    record.lastResult = result;
    record.lastSingletonBits = capturedSingletonBits;
#else
    const uint32_t index = g_initTraceCount;
    if (index >= kInitTraceCapacity) {
        ++g_initTraceOverflow;
        return;
    }
    InitTraceRecord& record = g_initTrace[index];
    record.invocation = g_initInvocation;
    record.stage = stage;
    record.event = event;
    record.result = result;
    record.singletonBits = singletonBits == 0xFFFFFFFFu
        ? captureInitSingletonBits() : singletonBits;
    g_initTraceCount = index + 1u;
    if (g_initTraceSink != nullptr) {
        g_initTraceSink(record.invocation, record.stage, record.event,
                        record.result, record.singletonBits);
    }
#endif
}

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
extern "C" __declspec(dllexport) void __cdecl
GuideXosNativeAotInitTraceEmitSummary(void) {
    if (g_initTraceSink == nullptr) return;
    for (uint32_t index = 0u; index < g_initTraceCount; ++index) {
        const InitTraceRecord& record = g_initTrace[index];
        const uint32_t packedSummaryEvent = GX_INIT_EVENT_SUMMARY_COUNTS |
            ((record.lastEvent & 0x7u) << 28) |
            (record.eventCounts[0] & 0x7Fu) |
            ((record.eventCounts[1] & 0x7Fu) << 7) |
            ((record.eventCounts[2] & 0x7Fu) << 14) |
            ((record.eventCounts[3] & 0x7Fu) << 21);
        g_initTraceSink(record.invocation, record.stage,
            packedSummaryEvent, record.lastResult,
            record.lastSingletonBits);
    }
    if (g_initTraceFirstFailureStage != 0xFFFFFFFFu) {
        g_initTraceSink(g_initInvocation, g_initTraceFirstFailureStage,
            GX_INIT_EVENT_SUMMARY_FIRST_FAILURE,
            g_initTraceFirstFailureResult,
            g_initTraceFirstFailureSingletonBits);
    }
    g_initTraceSink(g_initInvocation, 0xFFFFFFFFu,
        GX_INIT_EVENT_SUMMARY_TOTALS, g_initTraceEventCount,
        g_initTraceCoalescedEventCount);
}
#endif

extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetCount(void) { return g_initTraceCount; }
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetOverflow(void) { return g_initTraceOverflow; }
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetInvocation(uint32_t index) {
    return index < g_initTraceCount ? g_initTrace[index].invocation : 0xFFFFFFFFu;
}
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetStage(uint32_t index) {
    return index < g_initTraceCount ? g_initTrace[index].stage : 0xFFFFFFFFu;
}
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetEvent(uint32_t index) {
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    if (index >= g_initTraceCount) return 0xFFFFFFFFu;
    const InitTraceRecord& record = g_initTrace[index];
    return (record.eventCounts[0] & 0x7Fu) |
        ((record.eventCounts[1] & 0x7Fu) << 7) |
        ((record.eventCounts[2] & 0x7Fu) << 14) |
        ((record.eventCounts[3] & 0x7Fu) << 21);
#else
    return index < g_initTraceCount ? g_initTrace[index].event : 0xFFFFFFFFu;
#endif
}
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetResult(uint32_t index) {
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    return index < g_initTraceCount ? g_initTrace[index].lastResult : 0xFFFFFFFFu;
#else
    return index < g_initTraceCount ? g_initTrace[index].result : 0xFFFFFFFFu;
#endif
}
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetSingletonBits(uint32_t index) {
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R2R2R4_SAFE_HANDOFF)
    return index < g_initTraceCount ? g_initTrace[index].lastSingletonBits : 0xFFFFFFFFu;
#else
    return index < g_initTraceCount ? g_initTrace[index].singletonBits : 0xFFFFFFFFu;
#endif
}
#endif

extern "C" __declspec(dllexport) int __cdecl
GuideXosNativeAotGcStartupInstallPalHooks(
    const guidexos_nativeaot_pal_hooks* hooks) {
    return guidexos_nativeaot_pal_install_hooks(hooks);
}

extern "C" __declspec(dllexport) int __cdecl
GuideXosNativeAotGcStartupInstallHookTable(
    const guidexos_nativeaot_pal_hook_table_v1* table) {
    return guidexos_nativeaot_pal_install_hook_table(table);
}

extern "C" __declspec(dllexport) int __cdecl
GuideXosNativeAotGcStartupInstallPlatformHooks(
    const guidexos_nativeaot_gc_startup_platform_table_v1* table) {
    return guidexos_nativeaot_gc_install_startup_platform_hooks(table);
}

extern "C" __declspec(dllexport) int __cdecl GuideXosNativeAotGcStartupMain(void) {
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
    uintptr_t entryLocal = 0u;
    GuideXosNativeAotThreadReadinessMark(
        GX_THREADSTORE_EVENT_ADAPTER_ENTER,
        reinterpret_cast<uintptr_t>(&entryLocal), 0u);
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
    guideXosNativeAotInitTraceSetInvocation(1u);
    guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_ADAPTER);
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_ADAPTER, true);
    guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_RH_INITIALIZE);
#endif
    const bool nativeResult = RhInitialize(false);
    const guidexos_nativeaot_rh_initialize_observation observation =
        guidexos_nativeaot_observe_rh_initialize(nativeResult);
    g_rhInitializeRawResult = observation.native_boolean;
    g_startupResult = static_cast<unsigned int>(observation.adapter_status);
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)
    GuideXosNativeAotThreadReadinessMark(
        GX_THREADSTORE_EVENT_RHINITIALIZE_RETURN,
        static_cast<uintptr_t>(g_rhInitializeRawResult),
        static_cast<uintptr_t>(g_startupResult));
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
    const uint32_t singletonBits = captureInitSingletonBits();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_RH_INITIALIZE,
        nativeResult, g_startupResult, singletonBits);
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES)
    GuideXosNativeAotGcInitFinalizeForRead();
    emitGcInitRecord();
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED)
    emitSemiSharedRecord(
        GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_INVOCATION));
#endif
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
    emitNativeAllocationRecord(
        GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_INVOCATION));
#endif
#endif
    return (int)g_startupResult;
}

extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotGcStartupGetRhInitializeRawResult(void) {
    return g_rhInitializeRawResult;
}

/* These are narrow scalar status values, not claims that GC completed a cycle. */
extern "C" __declspec(dllexport) unsigned int __cdecl
GuideXosNativeAotGcStartupGetState(void) {
    const unsigned long long generation =
        guidexos_nativeaot_gc_startup_platform_generation();
    return (unsigned int)(generation & 0xFFFFFFFFu);
}

extern "C" __declspec(dllexport) unsigned int __cdecl
GuideXosNativeAotGcStartupGetPreGcState(void) {
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
    return captureInitSingletonBits();
#else
    return g_startupResult == 0u ? 1u : 0u;
#endif
}

extern "C" __declspec(dllexport) unsigned int __cdecl
GuideXosNativeAotGcStartupGetAllocationCount(void) {
    return 0u;
}

extern "C" __declspec(dllexport) unsigned int __cdecl
GuideXosNativeAotGcStartupGetLastAllocationSize(void) {
    return 0u;
}

extern "C" __declspec(dllexport) unsigned int __cdecl
GuideXosNativeAotGcStartupGetDiagnosticStage(void) {
    return g_startupResult;
}
