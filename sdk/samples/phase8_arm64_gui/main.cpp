#include <guidexos/app.h>

static bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        if (*left++ != *right++) return false;
    }
    return *left == '\0' && *right == '\0';
}

static void app_log(gx_app_context* context, const char* message)
{
    context->host->log(context, message);
}

static void make_click_label(char* output, uint32_t capacity, uint32_t count)
{
    const char prefix[] = "Click count: ";
    uint32_t i = 0;
    while (prefix[i] && i + 1 < capacity) { output[i] = prefix[i]; ++i; }
    char digits[11];
    uint32_t digitCount = 0;
    do { digits[digitCount++] = static_cast<char>('0' + (count % 10u)); count /= 10u; } while (count != 0 && digitCount < sizeof(digits));
    while (digitCount != 0 && i + 1 < capacity) output[i++] = digits[--digitCount];
    if (capacity != 0) output[i < capacity ? i : capacity - 1] = '\0';
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* context)
{
    if (!context || !context->host || context->size < sizeof(gx_app_context) ||
        context->apiVersion != GX_API_VERSION || context->host->size < GX_GUI_HOST_CALLS_SIZE ||
        context->host->version != GX_API_VERSION || context->host->guiVersion != GX_GUI_ABI_VERSION ||
        !context->host->log ||
        !context->host->get_api_version || !context->host->request_window_ex ||
        !context->host->window_destroy || !context->host->widget_create ||
        !context->host->widget_set_text || !context->host->poll_event) {
        return GX_ERROR_INVALID_ARGUMENT;
    }

    app_log(context, "gx_main entered");
    if (context->host->get_api_version(context) != GX_API_VERSION) return GX_ERROR_UNSUPPORTED;
    app_log(context, "ABI: GUI services OK");

    gx_handle window = 0;
    if (context->host->request_window_ex(context, "ARM64 App Model Proof", 460, 300,
                                         GX_WINDOW_FLAG_FIXED_SIZE | GX_WINDOW_FLAG_CENTERED,
                                         &window) != GX_OK || window == 0) {
        return GX_ERROR_FAILED;
    }
    app_log(context, "window created");

    gx_handle title = 0;
    gx_handle prompt = 0;
    gx_handle input = 0;
    gx_handle button = 0;
    gx_handle countLabel = 0;
    if (context->host->widget_create(context, window, GX_WIDGET_LABEL, 20, 18, 410, 26,
                                     "guideXOS ARM64 NativeElf is running", &title) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_LABEL, 20, 62, 80, 26,
                                     "Input:", &prompt) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_TEXTBOX, 90, 58, 320, 32,
                                     "", &input) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_BUTTON, 165, 112, 130, 34,
                                     "Click Me", &button) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_LABEL, 20, 176, 250, 26,
                                     "Click count: 0", &countLabel) != GX_OK) {
        context->host->window_destroy(context, window);
        return GX_ERROR_FAILED;
    }
    app_log(context, "controls created");

    char entered[32] = {};
    uint32_t enteredLength = 0;
    uint32_t clickCount = 0;
    bool textReported = false;
    bool buttonReported = false;
    bool closeRequested = false;

    while (!closeRequested) {
        gx_event event{};
        const gx_result result = context->host->poll_event(context, &event, 100);
        if (result == GX_ERROR_TIMEOUT) continue;
        if (result != GX_OK) return GX_ERROR_FAILED;
        if (event.type == GX_EVENT_WINDOW_CLOSE) {
            app_log(context, "close requested");
            closeRequested = true;
        } else if (event.type == GX_EVENT_TEXT_INPUT && event.window == input &&
                   event.param1 >= 0 && event.param1 <= 255 && enteredLength + 1 < sizeof(entered)) {
            entered[enteredLength++] = static_cast<char>(event.param1);
            entered[enteredLength] = '\0';
            if (!textReported && enteredLength == 5 && text_equal(entered, "arm64")) {
                app_log(context, "text input: PASS value=arm64");
                textReported = true;
            }
        } else if (event.type == GX_EVENT_WIDGET && event.window == button) {
            ++clickCount;
            char label[32];
            make_click_label(label, sizeof(label), clickCount);
            if (context->host->widget_set_text(context, countLabel, label) != GX_OK) return GX_ERROR_FAILED;
            if (!buttonReported && clickCount == 1) {
                app_log(context, "button event: PASS count=1");
                buttonReported = true;
            }
        }
    }

    app_log(context, "returning 42");
    return 42;
}
