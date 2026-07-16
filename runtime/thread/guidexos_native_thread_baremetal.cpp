#include "guidexos_native_thread.h"

#if defined(GXOS_BARE_METAL)

namespace gxos {
namespace runtime {
namespace {
    NativeThreadPlatformHooks g_hooks = { nullptr, nullptr, nullptr, nullptr };
}

void installNativeThreadPlatformHooks(const NativeThreadPlatformHooks* hooks) {
    if (hooks == nullptr) {
        g_hooks = { nullptr, nullptr, nullptr, nullptr };
        return;
    }
    g_hooks = *hooks;
}

ThreadResult createThread(NativeThreadEntry entry,
                          void* context,
                          const ThreadCreateOptions& options,
                          ThreadHandle* result) {
    if (g_hooks.create == nullptr) {
        if (result != nullptr) {
            *result = ThreadHandle{};
        }
        return ThreadResult::NotSupported;
    }
    return g_hooks.create(g_hooks.context, entry, context, options, result);
}

WaitResult joinThread(ThreadHandle thread,
                      const WaitTimeout& timeout,
                      gxos_thread_uintptr* exitResult) {
    if (g_hooks.join == nullptr) {
        if (exitResult != nullptr) {
            *exitResult = 0;
        }
        return WaitResult::Invalid;
    }
    return g_hooks.join(g_hooks.context, thread, timeout, exitResult);
}

ThreadResult detachThread(ThreadHandle thread) {
    return g_hooks.detach == nullptr
        ? ThreadResult::NotSupported
        : g_hooks.detach(g_hooks.context, thread);
}

} // namespace runtime
} // namespace gxos

#endif // GXOS_BARE_METAL

