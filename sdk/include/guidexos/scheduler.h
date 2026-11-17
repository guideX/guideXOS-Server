#pragma once

#include "app.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Capability-negotiated cooperative yield. Hosts that expose the appended
 * callback suspend only a scheduler-managed application context. A legacy or
 * synchronous host reports GX_ERROR_UNSUPPORTED and keeps its old behavior.
 */
static inline gx_result gx_app_yield(gx_app_context* context) {
    if (!context || !context->host) return GX_ERROR_INVALID_ARGUMENT;
    if (context->host->size < offsetof(gx_host_calls, yield) +
                                  sizeof(context->host->yield) ||
        !context->host->yield) {
        return GX_ERROR_UNSUPPORTED;
    }
    return context->host->yield(context);
}

#ifdef __cplusplus
}
#endif
