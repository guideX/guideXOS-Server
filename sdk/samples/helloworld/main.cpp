#include <guidexos/app.h>

#include <stdio.h>

static void draw_content(gx_app_context* ctx, gx_handle window, int clickCount) {
    if (ctx->host->draw_rect) {
        ctx->host->draw_rect(ctx, window, 10, 10, 460, 80, 0x202020);
        ctx->host->draw_rect(ctx, window, 20, 90, 160, 40, 0x404040);
    }
    if (ctx->host->draw_text) {
        ctx->host->draw_text(ctx, window, 20, 40, "Hello from Native ELF");
        ctx->host->draw_text(ctx, window, 35, 115, "Click Me");
        char message[64];
        snprintf(message, sizeof(message), "Clicked: %d", clickCount);
        ctx->host->draw_text(ctx, window, 20, 150, message);
    }
}

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
        int clickCount = 0;
        if (windowResult == GX_OK) {
            draw_content(ctx, window, clickCount);
        }
        if (windowResult == GX_OK && ctx->host->poll_event) {
            int elapsedMs = 0;
            while (elapsedMs < 30000) {
                gx_event event = {};
                gx_result eventResult = ctx->host->poll_event(ctx, &event, 500);
                if (eventResult == GX_OK && event.window == window) {
                    if (event.type == GX_EVENT_WINDOW_PAINT) {
                        draw_content(ctx, window, clickCount);
                    } else if (event.type == GX_EVENT_WINDOW_CLOSE) {
                        ctx->host->log(ctx, "close event received");
                        break;
                    } else if (event.type == GX_EVENT_KEY) {
                        if (event.param1 == 27 && event.param2 == GX_KEY_ACTION_DOWN) {
                            ctx->host->log(ctx, "Escape pressed");
                            break;
                        }
                        char message[64];
                        snprintf(message, sizeof(message), "key code %d", event.param1);
                        ctx->host->log(ctx, message);
                    } else if (event.type == GX_EVENT_MOUSE) {
                        int action = GX_MOUSE_ACTION(event.param3);
                        int button = GX_MOUSE_BUTTON(event.param3);
                        if (button == GX_MOUSE_BUTTON_LEFT && action == GX_MOUSE_ACTION_DOWN &&
                            event.param1 >= 20 && event.param1 < 180 && event.param2 >= 90 && event.param2 < 130) {
                            ++clickCount;
                            ctx->host->log(ctx, "Native button clicked");
                            draw_content(ctx, window, clickCount);
                        }
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
