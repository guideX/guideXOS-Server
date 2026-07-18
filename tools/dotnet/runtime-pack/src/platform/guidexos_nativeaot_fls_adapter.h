#pragma once

#include "../../../../../runtime/local_storage/guidexos_local_storage.h"

#if defined(_MSC_VER)
#define GUIDEXOS_NATIVEAOT_FLS_CALL __stdcall
#elif defined(__GNUC__) && defined(__i386__)
#define GUIDEXOS_NATIVEAOT_FLS_CALL __attribute__((stdcall))
#else
#define GUIDEXOS_NATIVEAOT_FLS_CALL
#endif

namespace guidexos {
namespace nativeaot {
namespace fls {

using Callback = void (GUIDEXOS_NATIVEAOT_FLS_CALL *)(void* value);
using Index = gxos_local_storage_uint32;

constexpr Index kOutOfIndexes = 0xFFFFFFFFu;

void initialize();
void shutdown();
void installPlatformHooks(const gxos::runtime::LocalStoragePlatformHooks* hooks);

bool attachCurrentThread();
bool detachCurrentThread();

Index alloc(Callback callback);
bool free(Index index);
void* get(Index index);
bool set(Index index, void* value);

} // namespace fls
} // namespace nativeaot
} // namespace guidexos

// C-callable inactive adapter entry points used by the probe.  They mirror
// FlsAlloc/FlsFree/FlsGetValue/FlsSetValue without returning live Windows
// imports from the proof runtime pack.
extern "C" {
void guidexos_nativeaot_fls_initialize();
void guidexos_nativeaot_fls_shutdown();
int guidexos_nativeaot_fls_attach_current_thread();
int guidexos_nativeaot_fls_detach_current_thread();
unsigned long guidexos_nativeaot_fls_alloc(guidexos::nativeaot::fls::Callback callback);
int guidexos_nativeaot_fls_free(unsigned long index);
void* guidexos_nativeaot_fls_get(unsigned long index);
int guidexos_nativeaot_fls_set(unsigned long index, void* value);
}
