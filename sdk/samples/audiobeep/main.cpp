// AudioBeep: minimal App Model audio sample (MC5), independent of Missile
// Command. Requests the audio.output permission, synthesizes a short 440 Hz
// S16 mono PCM beep in code (no resources), plays it twice with overlap via
// the appended play_pcm host call, then exits. Proves the audio API without
// any game logic. Stays silent (but functional) when the host predates the
// audio slot, denies the permission, or has no backend.

#include <guidexos/ui.h>
#include <guidexos/audio.h>

extern "C" void* memset(void* destination, int value, uint64_t bytes) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < bytes; ++i) output[i] = static_cast<uint8_t>(value);
    return destination;
}

// 0.30 s of 440 Hz at 22050 Hz, S16 mono: 6615 frames, static (no heap).
static const uint32_t kBeepRate = 22050u;
static const uint32_t kBeepFrames = 6615u;
static int16_t g_beep[kBeepFrames];

static void synth_beep() {
    // Integer 440 Hz square-ish sine: phase advances 440/22050 cycles per
    // frame; amplitude 12000. Deterministic, no libm.
    uint32_t phase = 0;
    const uint32_t step = 440u * 65536u / kBeepRate;
    for (uint32_t i = 0; i < kBeepFrames; ++i) {
        // phase in [0, 65536): first half positive, second negative.
        int32_t sample = (phase < 32768u) ? 12000 : -12000;
        // Simple 8-frame linear edges to avoid clicks.
        if (i < 8u) sample = sample * (int32_t)(i + 1u) / 8;
        if (i + 8u >= kBeepFrames) sample = sample * (int32_t)(kBeepFrames - i) / 8;
        g_beep[i] = (int16_t)sample;
        phase += step;
        if (phase >= 65536u) phase -= 65536u;
    }
}

static void log_result(gx_app_context* ctx, const char* label, gx_result result) {
    if (!ctx || !ctx->host || !ctx->host->log) return;
    ctx->host->log(ctx, label);
    if (result == GX_OK) {
        ctx->host->log(ctx, "AudioBeep request accepted");
    } else if (result == GX_ERROR_NOT_IMPLEMENTED) {
        ctx->host->log(ctx, "AudioBeep host predates audio (silent)");
    } else if (result == GX_ERROR_PERMISSION_DENIED) {
        ctx->host->log(ctx, "AudioBeep permission denied (silent)");
    } else if (result == GX_ERROR_BUSY) {
        ctx->host->log(ctx, "AudioBeep mixer busy (silent)");
    } else {
        ctx->host->log(ctx, "AudioBeep backend unavailable (silent)");
    }
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host) return GX_ERROR_INVALID_ARGUMENT;
    if (!ctx->host->log) return GX_ERROR_INVALID_ARGUMENT;
    ctx->host->log(ctx, "AudioBeep starting");

    synth_beep();
    // Two overlapping requests: the second fires immediately after the
    // first, exercising mixer overlap on supporting backends.
    gx_result first = gx_play_pcm(ctx, g_beep, sizeof(g_beep), kBeepRate, 1u, 16u);
    log_result(ctx, "AudioBeep first beep", first);
    gx_result second = gx_play_pcm(ctx, g_beep, sizeof(g_beep), kBeepRate, 1u, 16u);
    log_result(ctx, "AudioBeep second beep", second);

    ctx->host->log(ctx, "AudioBeep exiting");
    if (ctx->host->exit) return ctx->host->exit(ctx, GX_OK);
    return GX_OK;
}
