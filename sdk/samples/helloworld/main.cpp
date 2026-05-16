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
        if (windowResult == GX_OK) {
            if (ctx->host->draw_rect) {
                ctx->host->draw_rect(ctx, window, 10, 10, 460, 80, 0x202020);
            }
            if (ctx->host->draw_text) {
                ctx->host->draw_text(ctx, window, 20, 40, "Hello from Native ELF");
            }
        }
        if (windowResult == GX_OK && ctx->host->poll_event) {
            int elapsedMs = 0;
            while (elapsedMs < 30000) {
                gx_event event = {};
                gx_result eventResult = ctx->host->poll_event(ctx, &event, 500);
                if (eventResult == GX_OK && event.window == window) {
                    if (event.type == GX_EVENT_WINDOW_PAINT) {
                        if (ctx->host->draw_rect) {
                            ctx->host->draw_rect(ctx, window, 10, 10, 460, 80, 0x202020);
                        }
                        if (ctx->host->draw_text) {
                            ctx->host->draw_text(ctx, window, 20, 40, "Hello from Native ELF");
                        }
                    } else if (event.type == GX_EVENT_WINDOW_CLOSE) {
                        ctx->host->log(ctx, "close event received");
                        break;
                    }
                }
                if (eventResult != GX_OK && eventResult != GX_ERROR_TIMEOUT) {
                    ctx->host->log(ctx, "poll_event failed");
                    break;
                }
                elapsedMs += 500;
            }
        } else if (windowResult == GX_OK && ctx->host->wait_for_close) {
            gx_result waitResult = ctx->host->wait_for_close(ctx, window, 30000);
            ctx->host->log(ctx, waitResult == GX_OK ? "wait_for_close succeeded" : "wait_for_close timed out or failed");
        }
    }
    return GX_OK;
}
