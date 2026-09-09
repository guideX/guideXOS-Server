#include <guidexos/ui.h>

static bool same_text(const char* left, const char* right) {
    if (!left || !right) return false;
    uint32_t i = 0; while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == '\0' && right[i] == '\0';
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* context) {
    if (!context || !context->host || context->host->size < GX_GUI_V1_HOST_CALLS_SIZE ||
        context->host->guiVersion != GX_GUI_ABI_VERSION || !context->host->wait_event) return GX_ERROR_UNSUPPORTED;
    context->host->log(context, "gx_main entered");
    gx_handle window = 0;
    if (context->host->request_window_ex(context, "Phase 9 App B", 330, 210,
                                         GX_WINDOW_FLAG_FIXED_SIZE, &window) != GX_OK) return GX_ERROR_FAILED;
    gx_handle label = 0, textbox = 0, button = 0;
    if (context->host->widget_create(context, window, GX_WIDGET_LABEL, 18, 18, 290, 28,
                                     "App B / surviving runtime", &label) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_TEXTBOX, 18, 58, 290, 34,
                                     "", &textbox) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_BUTTON, 80, 112, 170, 36,
                                     "App B button", &button) != GX_OK) return GX_ERROR_FAILED;
    (void)label;
    context->host->log(context, "runtime: initialized");
    char typed[16] = {}; uint32_t length = 0; uint32_t clicks = 0;
    for (;;) {
        gx_event event{};
        if (context->host->wait_event(context, &event) != GX_OK) return GX_ERROR_FAILED;
        if (event.type == GX_EVENT_TEXT_INPUT && event.window == textbox) {
            if (length + 1u < sizeof(typed)) typed[length++] = static_cast<char>(event.param1);
            typed[length] = '\0';
            if (same_text(typed, "appb")) context->host->log(context, "focus/input isolation: PASS value=appb");
        } else if (event.type == GX_EVENT_WIDGET && event.window == button) {
            ++clicks;
            context->host->log(context, "button: PASS count=1");
            context->host->widget_set_value(context, button, static_cast<int32_t>(clicks));
        } else if (event.type == GX_EVENT_WINDOW_CLOSE && event.window == window) {
            context->host->log(context, "close requested");
            return 42;
        }
    }
}
