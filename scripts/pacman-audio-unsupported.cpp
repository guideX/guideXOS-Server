#include "pacman_audio_runtime.h"

/* This Server v0.3 SDK predates App Model audio output. Keep PacMan's audio
 * hooks silent until this branch exposes the audio.output host call. */
uint32_t pacman_audio_load_resources(gx_app_context*) {
    return 0;
}

PacManAudioSubmitResult pacman_audio_submit(void*, PacManSoundId) {
    return kPacManAudioSilent;
}
