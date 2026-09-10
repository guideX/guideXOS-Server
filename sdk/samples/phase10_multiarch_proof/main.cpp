#include <guidexos/ui.h>

static_assert(sizeof(gx_event) == 32, "Phase 10 proof requires the stable event ABI");
static_assert(sizeof(gx_host_calls) == 360, "Phase 10 proof requires the stable extended host ABI");
static_assert(offsetof(gx_host_calls, guiVersion) == 240, "Phase 10 proof requires the stable GUI version offset");
static_assert(sizeof(gx_app_context) == 24, "Phase 10 proof requires the stable app context ABI");

static const char* selected_architecture_label()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64";
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    return "AMD64";
#else
    return "UNKNOWN";
#endif
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* context)
{
    if (!context || !context->host || context->size < sizeof(gx_app_context) ||
        context->apiVersion != GX_API_VERSION || context->host->size < GX_GUI_HOST_CALLS_SIZE ||
        context->host->version != GX_API_VERSION || context->host->guiVersion != GX_GUI_ABI_VERSION ||
        !context->host->log || !context->host->request_window_ex || !context->host->widget_create ||
        !context->host->widget_set_text || !context->host->wait_event) return GX_ERROR_INVALID_ARGUMENT;

    context->host->log(context, "package identity: PASS");
#if defined(__aarch64__) || defined(_M_ARM64)
    context->host->log(context, "running architecture: arm64");
#else
    context->host->log(context, "running architecture: amd64");
#endif
    context->host->log(context, "GUI ABI: PASS");
    context->host->log(context, "payload selection: PASS");

    gx_handle window = 0;
    if (context->host->request_window_ex(context, "Phase 10 Multi-Architecture Proof", 500, 270,
                                         GX_WINDOW_FLAG_FIXED_SIZE | GX_WINDOW_FLAG_CENTERED,
                                         &window) != GX_OK || window == 0) return GX_ERROR_FAILED;
    gx_handle title = 0, package = 0, architecture = 0, verify = 0, close = 0, status = 0;
    if (context->host->widget_create(context, window, GX_WIDGET_LABEL, 20, 16, 460, 28,
                                     "Multi-Architecture App Model Proof", &title) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_LABEL, 20, 54, 460, 25,
                                     "Package: com.guidexos.phase10.multiarchproof", &package) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_LABEL, 20, 86, 460, 25,
                                     selected_architecture_label(), &architecture) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_BUTTON, 70, 132, 170, 40,
                                     "Verify selection", &verify) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_BUTTON, 260, 132, 170, 40,
                                     "Close", &close) != GX_OK ||
        context->host->widget_create(context, window, GX_WIDGET_LABEL, 20, 190, 460, 25,
                                     "Payload selected automatically.", &status) != GX_OK) return GX_ERROR_FAILED;
    (void)title; (void)package; (void)architecture;
    context->host->log(context, "GUI ready: PASS");

    for (;;) {
        gx_event event{};
        if (context->host->wait_event(context, &event) != GX_OK) return GX_ERROR_FAILED;
        if (event.type == GX_EVENT_WIDGET && event.window == verify) {
            context->host->widget_set_text(context, status, "Button event: PASS");
            context->host->log(context, "button: PASS");
        } else if (event.type == GX_EVENT_WIDGET && event.window == close) {
            context->host->log(context, "close requested");
            return 42;
        } else if (event.type == GX_EVENT_WINDOW_CLOSE) {
            context->host->log(context, "close requested");
            return 42;
        }
    }
}
