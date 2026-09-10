#include <stddef.h>

// This is the application-owned NativeAOT bootstrap seam.  The production
// guideXOS launcher passes the neutral startup tables through this stable
// application-owned seam.  The wrapper installs them into the runtime before
// performing the normal RhInitialize step and entering authentic managed code.
typedef struct C102NativeAotStartupContext {
    void* legacyPalHooks;
    void* palHookTable;
    void* gcPlatformTable;
    void* managedContext;
    int (*installTls)(void);
    void (*startupMarker)(unsigned int stage);
} C102NativeAotStartupContext;

extern int guidexos_nativeaot_pal_install_hooks(const void* hooks);
extern void guidexos_nativeaot_pal_set_startup_marker(void (*marker)(unsigned int stage));
extern int guidexos_nativeaot_pal_install_hook_table(const void* table);
extern int guidexos_nativeaot_gc_install_startup_platform_hooks(const void* table);
// RhInitialize is a C++ bool-returning NativeAOT ABI.  Keep the C wrapper's
// declaration one byte wide so a failed startup cannot be widened from stale
// upper return-register bits into an apparent success.
extern unsigned char RhInitialize(int isDll);
extern int ManagedMain(void* context);

enum {
    kStartupUninitialized = 0,
    kStartupInitializing = 1,
    kStartupReady = 2,
    kStartupFailed = 3,
};

// NativeAOT has no supported in-process shutdown boundary. The application
// image keeps runtime/module registration resident and later launches only
// re-enter ManagedMain. A partial startup is terminal for this image; retrying
// hooks or RhInitialize would create a mixed runtime state.
static unsigned int g_startupState = kStartupUninitialized;

__declspec(dllexport) int __cdecl
GuideXosNativeAotApplicationEntry(void* rawContext)
{
    C102NativeAotStartupContext* context =
        (C102NativeAotStartupContext*)rawContext;
    if (context == NULL || context->legacyPalHooks == NULL ||
        context->palHookTable == NULL || context->gcPlatformTable == NULL ||
        context->managedContext == NULL) {
        return -101;
    }

    if (g_startupState == kStartupReady) {
        if (context->installTls == NULL || context->installTls() != 0) {
            return -103;
        }
        return ManagedMain(context->managedContext);
    }
    if (g_startupState != kStartupUninitialized) {
        return -104;
    }
    g_startupState = kStartupInitializing;
    guidexos_nativeaot_pal_set_startup_marker(context->startupMarker);
    if (context->startupMarker != NULL) context->startupMarker(1u);
    if (guidexos_nativeaot_pal_install_hooks(context->legacyPalHooks) != 0) {
        g_startupState = kStartupFailed;
        return -102;
    }
    if (context->startupMarker != NULL) context->startupMarker(2u);
    if (guidexos_nativeaot_pal_install_hook_table(context->palHookTable) != 0) {
        g_startupState = kStartupFailed;
        return -102;
    }
    if (context->startupMarker != NULL) context->startupMarker(3u);
    if (guidexos_nativeaot_gc_install_startup_platform_hooks(
            context->gcPlatformTable) != 0) {
        g_startupState = kStartupFailed;
        return -102;
    }
    if (context->startupMarker != NULL) context->startupMarker(4u);
    if (context->startupMarker != NULL) context->startupMarker(5u);
    if (!RhInitialize(0)) {
        g_startupState = kStartupFailed;
        return -100;
    }
    if (context->startupMarker != NULL) context->startupMarker(6u);
    if (context->installTls == NULL || context->installTls() != 0) {
        g_startupState = kStartupFailed;
        return -103;
    }
    if (context->startupMarker != NULL) context->startupMarker(7u);
    g_startupState = kStartupReady;
    int managedResult = ManagedMain(context->managedContext);
    return managedResult;
}
