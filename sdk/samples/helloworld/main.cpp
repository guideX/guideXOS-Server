#include <guidexos/ui.h>

static gx_rect kPanelRect = { 10, 10, 460, 80 };
static gx_rect kButtonRect = { 20, 90, 160, 40 };

static void clear_event(gx_event* event) {
    if (!event) return;
    event->size = 0;
    event->type = GX_EVENT_NONE;
    event->window = 0;
    event->param1 = 0;
    event->param2 = 0;
    event->param3 = 0;
    event->param4 = 0;
}

static void draw_content(gx_app_context* ctx, gx_handle window, int clickCount, int pressed, int resourceLoaded) {
    gx_draw_panel(ctx, window, kPanelRect, 0x202020u);
    gx_draw_label(ctx, window, 20, 40, "Hello from Native ELF");
    gx_draw_label(ctx, window, 20, 65, resourceLoaded ? "Resource loaded" : "Resource missing");
    gx_draw_button(ctx, window, kButtonRect, "Click Me", pressed);
    if (clickCount == 1) {
        gx_draw_label(ctx, window, 20, 150, "Clicked once");
    } else if (clickCount > 1) {
        gx_draw_label(ctx, window, 20, 150, "Clicked many");
    }
}

extern "C" gx_result gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host) return GX_ERROR_INVALID_ARGUMENT;
    if (!ctx->host->get_api_version || !ctx->host->log) return GX_ERROR_UNSUPPORTED;

    uint32_t apiVersion = ctx->host->get_api_version(ctx);
    (void)apiVersion;

    ctx->host->log(ctx, "Hello from guideXOS Native ELF HelloWorld");
    int resourceLoaded = 0;
    uint32_t resourceExists = 0;
    if (file_exists(ctx, "resources/message.txt", &resourceExists) == GX_OK && resourceExists) {
        char resourceBuffer[128];
        uint32_t bytesRead = 0;
        gx_result readResult = file_read_all(ctx, "resources/message.txt", resourceBuffer, sizeof(resourceBuffer) - 1, &bytesRead);
        if (readResult == GX_OK) {
            resourceBuffer[bytesRead] = '\0';
            resourceLoaded = 1;
            ctx->host->log(ctx, "resource read succeeded");
            ctx->host->log(ctx, resourceBuffer);
        } else {
            ctx->host->log(ctx, "resource read failed");
        }
    } else {
        ctx->host->log(ctx, "resource missing");
    }

    if (ctx->host->request_window) {
        gx_handle window = 0;
        gx_result windowResult = ctx->host->request_window(ctx, "HelloWorld Native ELF", 480, 240, &window);
        ctx->host->log(ctx, windowResult == GX_OK ? "request_window succeeded" : "request_window failed");
        int clickCount = 0;
        int buttonPressed = 0;
        if (windowResult == GX_OK) {
            draw_content(ctx, window, clickCount, buttonPressed, resourceLoaded);
        }
        if (windowResult == GX_OK && ctx->host->poll_event) {
            int elapsedMs = 0;
            while (elapsedMs < 30000) {
                gx_event event;
                clear_event(&event);
                gx_result eventResult = ctx->host->poll_event(ctx, &event, 500);
                if (eventResult == GX_OK && event.window == window) {
                    if (gx_event_is_paint(&event)) {
                        draw_content(ctx, window, clickCount, buttonPressed, resourceLoaded);
                    } else if (gx_event_is_close(&event)) {
                        ctx->host->log(ctx, "close event received");
                        break;
                    } else if (gx_event_is_escape_down(&event)) {
                        ctx->host->log(ctx, "Escape pressed");
                        break;
                    } else if (gx_mouse_is_left_down(&event) && gx_rect_contains(kButtonRect, event.param1, event.param2)) {
                        ++clickCount;
                        buttonPressed = 1;
                        ctx->host->log(ctx, "Native button clicked");
                        draw_content(ctx, window, clickCount, buttonPressed, resourceLoaded);
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
