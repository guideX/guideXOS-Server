#include <guidexos/app.h>

static void app_log(gx_app_context* context, const char* message)
{
    context->host->log(context, message);
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* context)
{
    if (!context || !context->host || context->size < sizeof(gx_app_context) ||
        context->apiVersion != GX_API_VERSION || !context->host->log ||
        !context->host->get_api_version) return GX_ERROR_INVALID_ARGUMENT;

    app_log(context, "gx_main entered");
    if (context->host->get_api_version(context) != GX_API_VERSION) return GX_ERROR_UNSUPPORTED;
    app_log(context, "architecture: arm64");
    app_log(context, "ABI: OK");

    uint32_t checksum = 0;
    for (uint32_t i = 1; i <= 64; ++i) checksum += i * (i + 3u);
    if (checksum != 95680u) return GX_ERROR_FAILED;
    app_log(context, "computation: PASS");
    app_log(context, "returning 42");
    return 42;
}
