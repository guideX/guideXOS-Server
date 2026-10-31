#include "guidexos_nativeaot_pal_contract.h"
#include "guidexos_nativeaot_gc_startup_platform_contract.h"
#include "guidexos_nativeaot_rh_initialize_contract.h"
#include "../platform/guidexos_nativeaot_init_trace.h"

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
    uint32_t event;
    uint32_t result;
    uint32_t singletonBits;
};
InitTraceRecord g_initTrace[kInitTraceCapacity] = {};
uint32_t g_initTraceCount = 0u;
uint32_t g_initTraceOverflow = 0u;
uint32_t g_initInvocation = 0u;
guidexos_nativeaot_init_trace_sink g_initTraceSink = nullptr;

uint32_t captureInitSingletonBits() {
    return GuideXosNativeAotCaptureInitSingletonBits();
}
}

extern "C" void __cdecl guideXosNativeAotInitTraceSetInvocation(uint32_t invocation) {
    g_initInvocation = invocation;
    g_initTraceCount = 0u;
    g_initTraceOverflow = 0u;
}

extern "C" __declspec(dllexport) void __cdecl guideXosNativeAotInitTraceSetSink(
    guidexos_nativeaot_init_trace_sink sink) {
    g_initTraceSink = sink;
}

extern "C" void __cdecl guideXosNativeAotInitTraceRecord(
    uint32_t stage, uint32_t event, uint32_t result, uint32_t singletonBits) {
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
}

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
    return index < g_initTraceCount ? g_initTrace[index].event : 0xFFFFFFFFu;
}
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetResult(uint32_t index) {
    return index < g_initTraceCount ? g_initTrace[index].result : 0xFFFFFFFFu;
}
extern "C" __declspec(dllexport) uint32_t __cdecl
GuideXosNativeAotInitTraceGetSingletonBits(uint32_t index) {
    return index < g_initTraceCount ? g_initTrace[index].singletonBits : 0xFFFFFFFFu;
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
#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
    const uint32_t singletonBits = captureInitSingletonBits();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_RH_INITIALIZE,
        nativeResult, g_startupResult, singletonBits);
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
