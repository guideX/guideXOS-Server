#pragma once

// guideXOS App Model audio output (MC5): the smallest reusable application
// mixer/output path. Missile Command is the first client; nothing here is
// game-specific.
//
// Layers:
//   1. Policy   (HasAudioOutputPermission): pure "audio.output" gating. No
//      identity special-casing: any package holding the permission may play.
//   2. Validate (ValidatePlayRequest): bounds/format checks for one-shot PCM.
//      Contract mirrors sdk/include/guidexos/audio.h (GX_AUDIO_*).
//   3. Convert  (ConvertToMixFormat): 8/16-bit mono at 8-48 kHz becomes
//      mixer-native S16 mono at kMixRateHz (nearest-neighbor resample).
//      The host copies, so the app may free its buffer on return.
//   4. Mixer    (class Mixer): bounded voices (kMaxVoices), saturating mix,
//      deterministic Mix() advance, per-owner stop for app-exit reclaim.
//      No threads, no OS calls: unit-testable and backend-independent.
//   5. Backend  (EnsureBackend/Play/StopOwner/... below): hosted output.
//      waveOut (WinMM) when a device opens, otherwise an explicit null sink
//      that still mixes, advances, and reclaims (gameplay never blocks on
//      audio). Backend identity is logged and queryable so "reaches the
//      mixer" is never confused with "audibly rendered".
//
// Bare metal has no PCM submit path wired to DMA yet (HDA/USB drivers expose
// enumeration/volume/stream primitives but no mixer), so the bare-metal host
// call validates + permission-checks, then returns NOT_IMPLEMENTED
// explicitly. See kernel/core/native_elf_baremetal.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace audio {

// Must stay in sync with sdk/include/guidexos/audio.h.
constexpr uint32_t kChannels = 1;
constexpr uint32_t kMinRateHz = 8000;
constexpr uint32_t kMaxRateHz = 48000;
constexpr uint32_t kMixRateHz = 22050;
constexpr uint32_t kMaxPcmBytes = 262144;
constexpr uint32_t kMaxVoices = 16;
constexpr uint32_t kMaxVoiceSeconds = 4;
constexpr uint32_t kMaxVoiceFrames = kMixRateHz * kMaxVoiceSeconds;
constexpr const char* kPermission = "audio.output";

enum class RequestError {
    Ok = 0,
    InvalidArgument,
    Unsupported,
    PermissionDenied,
    Busy,
    BackendUnavailable
};

struct ValidationResult {
    RequestError error = RequestError::Ok;
    const char* reason = "ok";
};

ValidationResult ValidatePlayRequest(const void* pcmData, uint32_t pcmBytes,
                                     uint32_t sampleRateHz, uint32_t channels,
                                     uint32_t bitsPerSample);

// Any-owner permission gate: true when the granted-permission list contains
// exactly "audio.output". Case-sensitive, no prefixes, no identity checks.
bool HasAudioOutputPermission(const std::vector<std::string>& permissions);

// Convert a validated request to mixer-native frames. Returns false when the
// resampled voice would exceed kMaxVoiceFrames (caller reports Unsupported).
bool ConvertToMixFormat(const void* pcmData, uint32_t pcmBytes, uint32_t sampleRateHz,
                        uint32_t bitsPerSample, std::vector<int16_t>& outMonoFrames);

class Mixer {
public:
    Mixer();

    // Queue mixer-native frames for an owner (hosted: runtime id). Copies.
    // Returns false when every voice is busy (caller reports Busy).
    bool Play(uint64_t ownerId, const int16_t* frames, uint32_t frameCount);

    // Render frameCount mixed frames (saturating add), advancing every live
    // voice and reclaiming finished ones. Deterministic: identical voice
    // sequences produce identical output. Silent (zeros) when idle.
    void Mix(int16_t* outFrames, uint32_t frameCount);

    uint32_t ActiveVoices() const;
    uint32_t ActiveVoicesFor(uint64_t ownerId) const;

    // App-exit reclaim: drop every voice owned by ownerId.
    void StopOwner(uint64_t ownerId);
    void StopAll();

private:
    struct Voice {
        bool active = false;
        uint64_t owner = 0;
        std::vector<int16_t> samples;
        uint32_t position = 0;
    };
    Voice voices_[kMaxVoices];
};

// Hosted backend (implemented in native_app_audio.cpp; WinMM waveOut with a
// null-sink fallback). All functions are thread-safe. The mixer above stays
// backend-independent so tests never need audio hardware.
uint64_t BackendPlay(uint64_t ownerId, const int16_t* frames, uint32_t frameCount,
                     RequestError* outError, std::string* outBackend);
void BackendStopOwner(uint64_t ownerId);
uint32_t BackendActiveVoices();
uint32_t BackendActiveVoicesFor(uint64_t ownerId);
const char* BackendName();
uint64_t BackendPlaysAccepted();
uint64_t BackendPlaysBusy();
uint64_t BackendChunksWritten();

}  // namespace audio
}  // namespace gxos
