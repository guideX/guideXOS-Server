#pragma once

#include "native_app_runtime.h"

namespace gxos {
namespace apps {

#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
#if defined(_WIN32) && defined(__x86_64__)
using NativeElfWin64Entry = gx_result (*)(NativeGxAppContext* ctx);
gx_result CallNativeElfWin64Entry(NativeElfWin64Entry entry, NativeGxAppContext* context);
#endif
#endif

} // namespace apps
} // namespace gxos
