#include <guidexos/abi.h>
#include <guidexos/scheduler.h>

namespace {

static const uint64_t kInitialState = 0x534543335F534348ull;
static const uint32_t kMaximumFrames = 80u;

static void clear_event(gx_event* event)
{
    if (!event) return;
    event->size = sizeof(*event);
    event->type = GX_EVENT_NONE;
    event->window = 0u;
    event->param1 = 0;
    event->param2 = 0;
    event->param3 = 0;
    event->param4 = 0;
}

static uint64_t advance_state(uint64_t value, uint32_t frame)
{
    value ^= 0x9E3779B97F4A7C15ull + frame;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 29u;
    return value;
}

} // namespace

extern "C" gx_result GX_CALL gx_main(gx_app_context* context)
{
    if (!context || !context->host) return GX_ERROR_INVALID_ARGUMENT;
    const gx_host_calls* host = context->host;
    if (!host->request_window || !host->draw_text || !host->poll_event ||
        !host->log) return GX_ERROR_UNSUPPORTED;

    gx_handle window = 0u;
    const gx_result windowResult = host->request_window(
        context, "SECURITY3F Scheduler Pilot", 560, 260, &window);
    if (windowResult != GX_OK) return windowResult;

    host->log(context, "SECURITY3F PILOT entered scheduler-owned Native ELF context");
    host->log(context, "SECURITY3F PILOT yielding; desktop and service contexts can progress");
    (void)host->draw_text(context, window, 18, 36,
        "Authentic Native ELF on a dedicated scheduler stack");
    (void)host->draw_text(context, window, 18, 72,
        "Each frame yields, resumes, and checks stack-local state");
    (void)host->draw_text(context, window, 18, 108,
        "Press Escape or close this window to finish early");
    (void)host->draw_text(context, window, 18, 144,
        "Automatic completion after 80 cooperative frames");

    volatile uint64_t localState = kInitialState;
    uint64_t expectedState = kInitialState;
    uint32_t completedFrames = 0u;
    for (uint32_t frame = 0u; frame < kMaximumFrames; ++frame) {
        expectedState = advance_state(expectedState, frame);
        localState = advance_state(localState, frame);

        const gx_result yieldResult = gx_app_yield(context);
        if (yieldResult != GX_OK) {
            host->log(context, "SECURITY3F PILOT yield callback failed");
            return yieldResult;
        }
        if (localState != expectedState) {
            host->log(context, "SECURITY3F PILOT stack-local state mismatch after resume");
            return GX_ERROR_INTERNAL;
        }
        completedFrames = frame + 1u;

        gx_event event;
        clear_event(&event);
        const gx_result pollResult = host->poll_event(context, &event, 100);
        if (pollResult == GX_OK && event.window == window &&
            (event.type == GX_EVENT_WINDOW_CLOSE ||
             (event.type == GX_EVENT_KEY && event.param1 == 27 &&
              event.param2 == GX_KEY_ACTION_DOWN))) {
            break;
        }
        if (pollResult != GX_OK && pollResult != GX_ERROR_TIMEOUT) {
            host->log(context, "SECURITY3F PILOT event polling failed");
            return pollResult;
        }
    }

    if (completedFrames == 0u || localState != expectedState) {
        host->log(context, "SECURITY3F PILOT completed without a valid yield/resume proof");
        return GX_ERROR_INTERNAL;
    }

    host->log(context, "SECURITY3F PILOT normal return after yield/resume and local-state checks");
    return GX_OK;
}
