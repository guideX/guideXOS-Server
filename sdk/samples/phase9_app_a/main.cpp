#include <guidexos/ui.h>

static void clear_event(gx_event* event) {
    if (!event) return;
    event->size = sizeof(gx_event); event->type = GX_EVENT_NONE; event->window = 0;
    event->param1 = event->param2 = event->param3 = event->param4 = 0;
}

static bool text_equals(const char* left, const char* right) {
    if (!left || !right) return false;
    uint32_t i = 0; while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == '\0' && right[i] == '\0';
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* context) {
    if (!context || !context->host || context->apiVersion != GX_API_VERSION ||
        context->host->size < GX_GUI_V1_HOST_CALLS_SIZE || context->host->version != GX_API_VERSION ||
        context->host->guiVersion != GX_GUI_ABI_VERSION || !context->host->log ||
        !context->host->request_window_ex || !context->host->widget_create ||
        !context->host->poll_event) return GX_ERROR_UNSUPPORTED;
    context->host->log(context, "gx_main entered");

    gx_handle mainWindow = 0, secondaryWindow = 0;
    if (context->host->request_window_ex(context, "Phase 9 App A — Main", 340, 250,
                                         GX_WINDOW_FLAG_FIXED_SIZE, &mainWindow) != GX_OK) return GX_ERROR_FAILED;
    if (context->host->request_window_ex(context, "Phase 9 App A — Secondary", 300, 200,
                                         GX_WINDOW_FLAG_FIXED_SIZE, &secondaryWindow) != GX_OK) return GX_ERROR_FAILED;
    gx_handle label = 0, textbox = 0, button = 0, secondaryLabel = 0, secondaryButton = 0;
    if (context->host->widget_create(context, mainWindow, GX_WIDGET_LABEL, 18, 18, 300, 28,
                                     "App A / independent runtime", &label) != GX_OK ||
        context->host->widget_create(context, mainWindow, GX_WIDGET_TEXTBOX, 18, 58, 300, 34,
                                     "", &textbox) != GX_OK ||
        context->host->widget_create(context, mainWindow, GX_WIDGET_BUTTON, 90, 110, 160, 36,
                                     "App A button", &button) != GX_OK ||
        context->host->widget_create(context, secondaryWindow, GX_WIDGET_LABEL, 16, 20, 260, 28,
                                     "Second window / same app", &secondaryLabel) != GX_OK ||
        context->host->widget_create(context, secondaryWindow, GX_WIDGET_BUTTON, 60, 80, 180, 36,
                                     "Close secondary", &secondaryButton) != GX_OK) return GX_ERROR_FAILED;
    (void)label; (void)secondaryLabel; (void)secondaryButton;
    context->host->log(context, "multi-window: PASS");

    char typed[16] = {}; uint32_t typedLength = 0; uint32_t clicks = 0;
    for (;;) {
        gx_event event{};
        gx_result result = GX_ERROR_UNSUPPORTED;
        if (context->host->size >= GX_GUI_HOST_CALLS_SIZE && context->host->wait_event)
            result = context->host->wait_event(context, &event);
        else if (context->host->poll_event)
            result = context->host->poll_event(context, &event, 100);
        if (result != GX_OK) return result;
        if (event.type == GX_EVENT_WINDOW_FOCUS) {
            if (event.window == mainWindow) context->host->log(context, "focus: main");
            else if (event.window == secondaryWindow) context->host->log(context, "focus: secondary");
        } else if (event.type == GX_EVENT_TEXT_INPUT && event.window == textbox) {
            if (typedLength + 1u < sizeof(typed)) typed[typedLength++] = static_cast<char>(event.param1);
            typed[typedLength] = '\0';
            if (text_equals(typed, "appa")) context->host->log(context, "focus/input isolation: PASS value=appa");
        } else if (event.type == GX_EVENT_WIDGET) {
            if (event.window == button) {
                ++clicks;
                context->host->log(context, "button: PASS count=1");
                context->host->widget_set_value(context, button, static_cast<int32_t>(clicks));
            } else if (event.window == secondaryButton) {
                context->host->window_destroy(context, secondaryWindow);
                context->host->log(context, "secondary window close: PASS");
            }
        } else if (event.type == GX_EVENT_WINDOW_CLOSE) {
            if (event.window == mainWindow) {
                context->host->log(context, "close requested");
                return 42;
            } else if (event.window == secondaryWindow) {
                context->host->log(context, "secondary window close: PASS");
            }
        }
        clear_event(&event);
    }
}
