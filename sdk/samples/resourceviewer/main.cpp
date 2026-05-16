#include <guidexos/ui.h>

static gx_rect kPanelRect = { 10, 10, 500, 180 };
static gx_rect kPreviewRect = { 20, 95, 480, 70 };

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

static void copy_preview(char* destination, uint32_t destinationSize, const char* source, uint32_t sourceSize) {
    if (!destination || destinationSize == 0) return;
    uint32_t limit = destinationSize - 1;
    uint32_t count = sourceSize < limit ? sourceSize : limit;
    for (uint32_t i = 0; i < count; ++i) {
        char c = source[i];
        destination[i] = (c == '\r' || c == '\n') ? ' ' : c;
    }
    destination[count] = '\0';
}

static void draw_content(gx_app_context* ctx, gx_handle window, int resourceLoaded, const char* preview) {
    gx_draw_panel(ctx, window, kPanelRect, 0x202830u);
    gx_draw_label(ctx, window, 24, 42, "Resource Viewer");
    gx_draw_label(ctx, window, 24, 72, resourceLoaded ? "Resource loaded" : "Resource missing");
    gx_draw_panel(ctx, window, kPreviewRect, 0x303840u);
    if (resourceLoaded && preview && preview[0]) {
        gx_draw_label(ctx, window, 30, 132, preview);
    } else {
        gx_draw_label(ctx, window, 30, 132, "No preview available");
    }
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host) return GX_ERROR_INVALID_ARGUMENT;
    if (!ctx->host->get_api_version || !ctx->host->log) return GX_ERROR_UNSUPPORTED;

    uint32_t apiVersion = ctx->host->get_api_version(ctx);
    (void)apiVersion;

    ctx->host->log(ctx, "ResourceViewer Native ELF starting");

    int resourceLoaded = 0;
    char preview[96];
    preview[0] = '\0';

    uint32_t resourceExists = 0;
    if (file_exists(ctx, "resources/about.txt", &resourceExists) == GX_OK && resourceExists) {
        char resourceBuffer[192];
        uint32_t bytesRead = 0;
        gx_result readResult = file_read_all(ctx, "resources/about.txt", resourceBuffer, sizeof(resourceBuffer) - 1, &bytesRead);
        if (readResult == GX_OK) {
            resourceBuffer[bytesRead] = '\0';
            copy_preview(preview, sizeof(preview), resourceBuffer, bytesRead);
            resourceLoaded = 1;
            ctx->host->log(ctx, "ResourceViewer resource loaded");
        } else {
            ctx->host->log(ctx, "ResourceViewer resource read failed");
        }
    } else {
        ctx->host->log(ctx, "ResourceViewer resource missing");
    }

    if (!ctx->host->request_window) return GX_ERROR_UNSUPPORTED;

    gx_handle window = 0;
    gx_result windowResult = ctx->host->request_window(ctx, "Resource Viewer", 520, 320, &window);
    ctx->host->log(ctx, windowResult == GX_OK ? "ResourceViewer window created" : "ResourceViewer window failed");
    if (windowResult != GX_OK) return windowResult;

    draw_content(ctx, window, resourceLoaded, preview);

    if (ctx->host->poll_event) {
        while (1) {
            gx_event event;
            clear_event(&event);
            gx_result eventResult = ctx->host->poll_event(ctx, &event, 500);
            if (eventResult == GX_OK && event.window == window) {
                if (gx_event_is_paint(&event)) {
                    draw_content(ctx, window, resourceLoaded, preview);
                } else if (gx_event_is_close(&event)) {
                    ctx->host->log(ctx, "ResourceViewer close event received");
                    break;
                } else if (gx_event_is_escape_down(&event)) {
                    ctx->host->log(ctx, "ResourceViewer Escape pressed");
                    break;
                }
            } else if (eventResult != GX_OK && eventResult != GX_ERROR_TIMEOUT) {
                ctx->host->log(ctx, "ResourceViewer poll_event failed");
                break;
            }
        }
    } else if (ctx->host->wait_for_close) {
        ctx->host->wait_for_close(ctx, window, 30000);
    }

    if (ctx->host->exit) return ctx->host->exit(ctx, GX_OK);
    return GX_OK;
}
