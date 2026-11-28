#pragma once

#include <stddef.h>

#include "abi.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gx_app_context {
    uint32_t size;
    uint32_t apiVersion;
    const gx_host_calls* host;
    void* userData;
};

typedef gx_result (*gx_main_fn)(gx_app_context* ctx);

gx_result GX_CALL gx_main(gx_app_context* ctx);

// Safe optional-extension probe. The host-table size is part of the original
// ABI prefix, so older runtimes are detected before the appended slot is read.
static inline gx_sql_status gx_sql_query_catalog(
    gx_app_context* ctx, const gx_sql_catalog_request* request,
    gx_sql_catalog_entry* entries, uint32_t entryCapacity,
    gx_sql_catalog_result* outResult) {
    if (!ctx || ctx->size < sizeof(gx_app_context) || !ctx->host) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    const size_t requiredBytes = offsetof(gx_host_calls, sql_catalog_query) +
        sizeof(ctx->host->sql_catalog_query);
    if (ctx->host->size < requiredBytes || !ctx->host->sql_catalog_query) {
        return GX_SQL_STATUS_UNSUPPORTED;
    }
    return ctx->host->sql_catalog_query(ctx, request, entries, entryCapacity,
                                        outResult);
}

#ifdef __cplusplus
}
#endif
