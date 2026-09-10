#include <guidexos/app.h>

extern "C" gx_result GX_CALL gx_main(gx_app_context* context)
{
    if (!context || !context->host || !context->host->log) return GX_ERROR_INVALID_ARGUMENT;
    return context->host->log(context, "Phase 11 AMD64 companion payload") == GX_OK ? GX_OK : GX_ERROR_FAILED;
}
