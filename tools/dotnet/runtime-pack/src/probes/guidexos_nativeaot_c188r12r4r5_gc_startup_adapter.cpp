#include "guidexos_nativeaot_pal_contract.h"
#include "guidexos_nativeaot_gc_startup_platform_contract.h"

/*
 * Minimal proof-image adapter for the existing kernel startup harness.
 * It installs the established PAL contracts and invokes the pinned
 * NativeAOT RhInitialize entry.  It deliberately supplies no Windows import
 * shims, allocator, runtime state, or collection implementation.
 */
extern "C" unsigned char RhInitialize(int isDll);

static unsigned int g_startupResult = 0xFFFFFFFFu;

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
    g_startupResult = RhInitialize(0) ? 0u : 1u;
    return (int)g_startupResult;
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
    return g_startupResult == 0u ? 1u : 0u;
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
