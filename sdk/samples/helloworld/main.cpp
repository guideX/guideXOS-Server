#include <guidexos/app.h>

extern "C" gx_result gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host) return GX_ERROR_INVALID_ARGUMENT;
    if (!ctx->host->get_api_version || !ctx->host->log) return GX_ERROR_UNSUPPORTED;

    uint32_t apiVersion = ctx->host->get_api_version(ctx);
    (void)apiVersion;

    ctx->host->log(ctx, "Hello from guideXOS Native ELF HelloWorld");
    if (ctx->host->request_window) {
        gx_handle window = 0;
        gx_result windowResult = ctx->host->request_window(ctx, "HelloWorld Native ELF", 480, 240, &window);
        ctx->host->log(ctx, windowResult == GX_OK ? "request_window succeeded" : "request_window failed");
        if (windowResult == GX_OK && ctx->host->draw_text) {
            ctx->host->draw_text(ctx, window, 20, 40, "Hello from Native ELF");
        }
    }
    return GX_OK;
}
