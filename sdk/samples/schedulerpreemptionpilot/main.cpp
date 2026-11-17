#include <guidexos/abi.h>
#include <guidexos/scheduler.h>

namespace {

static const uint64_t kInitialState = 0x5333474E4F4E5949ull;
static const uint64_t kExpectedState = 0xE74DA1AC535EAC4Dull;
static const uint64_t kMarker = 0x5345435552495459ull;
static const uint32_t kMaximumEventPolls = 50u;

extern "C" void GX_CALL security3g_run_workload(uint64_t* checksum,
                                                 uint64_t* xmm6Observed,
                                                 uint32_t* xstateProofMask);

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

static void to_hex(uint64_t value, char* output, uint32_t capacity)
{
    static const char digits[] = "0123456789ABCDEF";
    if (!output || capacity < 19u) return;
    output[0] = '0';
    output[1] = 'x';
    for (uint32_t i = 0u; i < 16u; ++i) {
        const uint32_t shift = (15u - i) * 4u;
        output[i + 2u] = digits[(value >> shift) & 0xFu];
    }
    output[18] = '\0';
}

} // namespace

extern "C" gx_result GX_CALL gx_main(gx_app_context* context)
{
    if (!context || !context->host) return GX_ERROR_INVALID_ARGUMENT;
    const gx_host_calls* host = context->host;
    if (!host->request_window || !host->draw_text || !host->poll_event ||
        !host->log) return GX_ERROR_UNSUPPORTED;

    gx_handle window = 0u;
    const gx_result createResult = host->request_window(
        context, "SECURITY3G Timer Preemption Pilot", 600, 300, &window);
    if (createResult != GX_OK) return createResult;
    (void)host->draw_text(context, window, 18, 34,
        "Bounded Native ELF CPU workload; no yield during measurement");
    (void)host->draw_text(context, window, 18, 66,
        "A hardware timer should run the system service and resume this task");
    (void)host->draw_text(context, window, 18, 98,
        "After the checksum, use Escape or close this window");

    // The runtime recognizes these two host-log markers to bound the
    // measured interval. There are no host calls or cooperative yields in
    // the deterministic loop between them.
    if (host->log(context, "SECURITY3G-NONYIELD-BEGIN") != GX_OK)
        return GX_ERROR_INTERNAL;

    volatile uint64_t stackLocalProof = 0x533347535441434Bull;
    uint64_t checksum = 0u;
    uint64_t observedXmm6 = 0u;
    uint32_t xstateProofMask = 0u;
    security3g_run_workload(&checksum, &observedXmm6, &xstateProofMask);
    const bool workloadPassed = checksum == kExpectedState &&
        observedXmm6 == kMarker &&
        (xstateProofMask & 1u) != 0u &&
        stackLocalProof == 0x533347535441434Bull;
    if (host->log(context, "SECURITY3G-NONYIELD-END") != GX_OK)
        return GX_ERROR_INTERNAL;

    char checksumText[19] = {};
    to_hex(checksum, checksumText, sizeof(checksumText));
    (void)host->draw_text(context, window, 18, 142,
        workloadPassed ? ((xstateProofMask & 2u)
            ? "Checksum, XMM6 and YMM6 state preserved"
            : "Checksum and XMM6 state preserved; AVX unavailable")
            : "CPU state check failed");
    (void)host->draw_text(context, window, 18, 174, checksumText);
    char xstateText[19] = {};
    to_hex(xstateProofMask, xstateText, sizeof(xstateText));
    (void)host->draw_text(context, window, 18, 206, xstateText);
    host->log(context, workloadPassed
        ? ((xstateProofMask & 2u)
            ? "SECURITY3G bounded checksum, XMM6 and YMM6 proofs passed"
            : "SECURITY3G bounded checksum and XMM6 proofs passed; AVX unavailable")
        : "SECURITY3G bounded no-yield workload or SIMD proof failed");

    if (!workloadPassed) return GX_ERROR_INTERNAL;

    for (uint32_t poll = 0u; poll < kMaximumEventPolls; ++poll) {
        gx_event event;
        clear_event(&event);
        const gx_result pollResult = host->poll_event(context, &event, 100);
        if (pollResult != GX_OK && pollResult != GX_ERROR_TIMEOUT)
            return pollResult;
        if (pollResult == GX_OK && event.window == window &&
            (event.type == GX_EVENT_WINDOW_CLOSE ||
             (event.type == GX_EVENT_KEY && event.param1 == 27 &&
              event.param2 == GX_KEY_ACTION_DOWN))) break;
    }

    host->log(context, "SECURITY3G bounded preemption pilot returning normally");
    return GX_OK;
}
