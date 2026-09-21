#pragma once

/* guideXOS App Model audio output (MC5): the smallest reusable application
 * sound capability. One appended host call (gx_host_calls.play_pcm) plays a
 * complete short decoded PCM sound effect without giving applications any
 * audio hardware access.
 *
 * Wire format contract (all hosts, all backends):
 *   channels       == GX_AUDIO_CHANNELS (mono only in v1)
 *   bitsPerSample  == 8 or 16 (8-bit is unsigned, 16-bit is signed LE)
 *   sampleRateHz   in [GX_AUDIO_MIN_RATE_HZ, GX_AUDIO_MAX_RATE_HZ]
 *   pcmBytes       > 0, <= GX_AUDIO_MAX_PCM_BYTES, and a whole number of
 *                  frames (pcmBytes % (bitsPerSample / 8) == 0)
 *   resampled voice length is additionally bounded by the host
 *   (GX_AUDIO_MAX_VOICE_FRAMES at the mixer rate) so one noisy app cannot
 *   create unbounded allocations.
 *
 * Semantics: fire-and-forget (matches the VB6 SND_ASYNC usage the first
 * client mirrors). Overlapping calls mix when the backend supports it;
 * GX_ERROR_BUSY means every mixer voice is in use (drop or retry later).
 * Hosts with no audible backend still validate arguments and permissions
 * first, then report GX_ERROR_NOT_IMPLEMENTED / GX_ERROR_UNSUPPORTED.
 * Playback completion or timing never feeds back into the caller: there is
 * no completion callback and no query. Audio must always be a side effect;
 * applications stay fully functional when it is unavailable or denied.
 *
 * Permission: the calling package must hold "audio.output"
 * (GX_AUDIO_PERMISSION), otherwise GX_ERROR_PERMISSION_DENIED.
 */

#include "abi.h"
#include "types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_AUDIO_PERMISSION "audio.output"

enum {
    GX_AUDIO_CHANNELS = 1,
    GX_AUDIO_BITS_8 = 8,
    GX_AUDIO_BITS_16 = 16,
    GX_AUDIO_MIN_RATE_HZ = 8000,
    GX_AUDIO_MAX_RATE_HZ = 48000,
    /* Mixer output rate all hosts converge to (covers the 11025/22050 Hz
     * heritage assets without ultrasonic waste). */
    GX_AUDIO_MIX_RATE_HZ = 22050
};

enum {
    GX_AUDIO_MAX_PCM_BYTES = 262144u,
    GX_AUDIO_MAX_VOICES = 16u,
    GX_AUDIO_MAX_VOICE_SECONDS = 4u,
    GX_AUDIO_MAX_VOICE_FRAMES = (GX_AUDIO_MIX_RATE_HZ * GX_AUDIO_MAX_VOICE_SECONDS)
};

/* Bounds-safe play_pcm dispatch. Older hosts publish a smaller
 * gx_host_calls table without the appended audio slot; calling through a
 * missing slot would read out of bounds, so check the table size and the
 * slot pointer first and report "not implemented" explicitly. */
static inline gx_result gx_play_pcm(gx_app_context* ctx, const void* pcmData,
                                    uint32_t pcmBytes, uint32_t sampleRateHz,
                                    uint32_t channels, uint32_t bitsPerSample) {
    const gx_host_calls* host;
    size_t need;
    if (!ctx || !pcmData) return GX_ERROR_INVALID_ARGUMENT;
    host = ctx->host;
    if (!host) return GX_ERROR_INVALID_ARGUMENT;
    need = offsetof(gx_host_calls, play_pcm) + sizeof(host->play_pcm);
    if (host->size < need) return GX_ERROR_NOT_IMPLEMENTED;
    if (!host->play_pcm) return GX_ERROR_NOT_IMPLEMENTED;
    return host->play_pcm(ctx, pcmData, pcmBytes, sampleRateHz, channels, bitsPerSample);
}

#ifdef __cplusplus
}
#endif
