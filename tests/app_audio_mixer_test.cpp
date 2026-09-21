// MC5 App Model audio platform tests: validation, conversion, mixer,
// permission policy, and backend ownership. The Mixer and its helpers are
// backend-independent and deterministic, so the whole suite runs with no
// audio hardware (NATIVE_APP_AUDIO_NO_BACKEND null sink).
//
// Build: g++ -std=c++17 -DNATIVE_APP_AUDIO_NO_BACKEND tests/... (see
// scripts/run-app-audio-mixer-test.ps1).

#include "../native_app_audio.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        ++g_failures;
        std::cout << "FAIL: " << name << "\n";
    }
}

using gxos::audio::Mixer;
using gxos::audio::RequestError;

bool isOk(gxos::audio::ValidationResult r) { return r.error == RequestError::Ok; }

void testValidation() {
    static uint8_t pcm[64];
    std::memset(pcm, 0x80, sizeof(pcm));
    // Valid matrix: 8/16-bit mono at heritage and common rates.
    check(isOk(gxos::audio::ValidatePlayRequest(pcm, 11025, 11025, 1, 8)), "valid pcm8 11025");
    check(isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 22050, 1, 16)), "valid pcm16 22050");
    check(isOk(gxos::audio::ValidatePlayRequest(pcm, 48, 44100, 1, 16)), "valid pcm16 44100");
    check(isOk(gxos::audio::ValidatePlayRequest(pcm, 32, 8000, 1, 8)), "valid min rate");
    check(isOk(gxos::audio::ValidatePlayRequest(pcm, 32, 48000, 1, 8)), "valid max rate");
    // Malformed / unsupported.
    check(!isOk(gxos::audio::ValidatePlayRequest(nullptr, 64, 22050, 1, 16)), "null data");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 0, 22050, 1, 16)), "empty data");
    check(gxos::audio::ValidatePlayRequest(pcm, gxos::audio::kMaxPcmBytes + 2, 22050, 1, 16).error ==
              RequestError::Unsupported,
          "oversized unsupported");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 22050, 0, 16)), "zero channels");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 22050, 2, 16)), "stereo rejected");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 22050, 1, 4)), "adpcm bits rejected");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 22050, 1, 24)), "24-bit rejected");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 0, 1, 16)), "zero rate");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 7999, 1, 16)), "rate below min");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 64, 48001, 1, 16)), "rate above max");
    check(!isOk(gxos::audio::ValidatePlayRequest(pcm, 63, 22050, 1, 16)), "non-whole 16-bit frames");
}

void testConversion() {
    // 8-bit unsigned mapping: 0x00 -> -32768, 0x80 -> 0, 0xFF -> +32512.
    uint8_t src8[4] = {0x00, 0x80, 0xFF, 0x80};
    std::vector<int16_t> out;
    check(gxos::audio::ConvertToMixFormat(src8, 4, 22050, 8, out), "conv8 ok");
    check(out.size() == 4 && out[0] == -32768 && out[1] == 0 && out[2] == 32512 && out[3] == 0,
          "conv8 values");
    // 16-bit LE passthrough at the mix rate.
    uint8_t src16[4] = {0x00, 0x80, 0xFF, 0x7F};  // -32768, +32767
    check(gxos::audio::ConvertToMixFormat(src16, 4, 22050, 16, out), "conv16 ok");
    check(out.size() == 2 && out[0] == -32768 && out[1] == 32767, "conv16 values");
    // 11025 -> 22050 resample doubles frames (nearest neighbor).
    uint8_t src11[4] = {0x80, 0xFF, 0x00, 0x80};
    check(gxos::audio::ConvertToMixFormat(src11, 4, 11025, 8, out), "upsample ok");
    check(out.size() == 8, "upsample length");
    check(out[0] == 0 && out[1] == 0 && out[2] == 32512 && out[3] == 32512 &&
              out[4] == -32768 && out[5] == -32768 && out[6] == 0 && out[7] == 0,
          "upsample nearest");
    // Overlong voices are refused (no unbounded allocation).
    std::vector<uint8_t> huge(60000, 0x80);
    check(!gxos::audio::ConvertToMixFormat(huge.data(), 60000, 8000, 8, out), "voice-too-long");
}

void testMixerSingle() {
    Mixer mixer;
    int16_t voice[4] = {1000, 2000, -3000, 4000};
    check(mixer.Play(7, voice, 4), "play one");
    check(mixer.ActiveVoices() == 1, "one active");
    check(mixer.ActiveVoicesFor(7) == 1, "one active for owner");
    check(mixer.ActiveVoicesFor(8) == 0, "none for other owner");
    int16_t rendered[4] = {0};
    mixer.Mix(rendered, 4);
    check(rendered[0] == 1000 && rendered[1] == 2000 && rendered[2] == -3000 &&
              rendered[3] == 4000,
          "single voice exact");
    check(mixer.ActiveVoices() == 0, "reclaimed after completion");
    int16_t silence[4] = {9, 9, 9, 9};
    mixer.Mix(silence, 4);
    check(silence[0] == 0 && silence[3] == 0, "idle renders silence");
}

void testMixerOverlapAndClip() {
    Mixer mixer;
    int16_t a[4] = {1000, 1000, 1000, 1000};
    int16_t b[4] = {2000, -500, 30000, -30000};
    check(mixer.Play(1, a, 4), "overlap a");
    check(mixer.Play(2, b, 4), "overlap b");
    int16_t rendered[4] = {0};
    mixer.Mix(rendered, 4);
    check(rendered[0] == 3000 && rendered[1] == 500 && rendered[2] == 31000 &&
              rendered[3] == -29000,
          "two voices sum");
    // Saturation both rails.
    Mixer clip;
    int16_t hot[2] = {32767, -32768};
    check(clip.Play(1, hot, 2), "clip a");
    check(clip.Play(2, hot, 2), "clip b");
    int16_t clipped[2] = {0};
    clip.Mix(clipped, 2);
    check(clipped[0] == 32767 && clipped[1] == -32768, "saturating mix");
}

void testMixerDeterminism() {
    // Identical voice sequences render identical bytes, twice.
    int16_t a[8] = {1, -2, 300, -4000, 5000, -6000, 7000, -8000};
    int16_t b[5] = {100, 200, 300, 400, 500};
    int16_t first[8] = {0};
    int16_t second[8] = {0};
    {
        Mixer mixer;
        mixer.Play(3, a, 8);
        mixer.Play(3, b, 5);
        mixer.Mix(first, 8);
    }
    {
        Mixer mixer;
        mixer.Play(3, a, 8);
        mixer.Play(3, b, 5);
        mixer.Mix(second, 8);
    }
    check(std::memcmp(first, second, sizeof(first)) == 0, "mixer deterministic");
    check(first[0] == 101 && first[4] == 5500 && first[5] == -6000, "mixer overlap shape");
}

void testMixerExhaustionAndOwnership() {
    Mixer mixer;
    int16_t tiny[2] = {7, 7};
    for (uint32_t i = 0; i < gxos::audio::kMaxVoices; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "fill voice %u", i);
        check(mixer.Play(1000 + i, tiny, 2), name);
    }
    check(!mixer.Play(9999, tiny, 2), "voice exhaustion refuses");
    check(mixer.ActiveVoices() == gxos::audio::kMaxVoices, "full house");
    // One noisy owner cannot pin the mixer: stopping it frees its voices.
    mixer.StopOwner(1000);
    check(mixer.ActiveVoices() == gxos::audio::kMaxVoices - 1, "owner stop frees");
    check(mixer.Play(9999, tiny, 2), "slot reusable after owner stop");
    mixer.StopAll();
    check(mixer.ActiveVoices() == 0, "stop-all reclaims everything");
    check(mixer.Play(5, nullptr, 2) == false, "null frames refused");
    check(mixer.Play(5, tiny, 0) == false, "empty voice refused");
}

void testPolicy() {
    check(gxos::audio::HasAudioOutputPermission({"audio.output"}), "allowed exact");
    check(gxos::audio::HasAudioOutputPermission({"log", "window", "audio.output", "file.read"}),
          "allowed among others");
    check(!gxos::audio::HasAudioOutputPermission({}), "denied empty");
    check(!gxos::audio::HasAudioOutputPermission({"log", "window"}), "denied missing");
    check(!gxos::audio::HasAudioOutputPermission({""}), "denied blank entry");
    check(!gxos::audio::HasAudioOutputPermission({"audio.outputx"}), "denied prefix-lookalike");
    check(!gxos::audio::HasAudioOutputPermission({"Audio.Output"}), "denied case variant");
    check(!gxos::audio::HasAudioOutputPermission({"audio"}), "denied short token");
}

void testBackendOwnership() {
    // Test-double backend: accept, busy, and per-owner reclaim behavior.
    gxos::audio::RequestError error = RequestError::Ok;
    std::string backend;
    int16_t tiny[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint64_t before = gxos::audio::BackendPlaysAccepted();
    const uint64_t owner = 4242;
    for (uint32_t i = 0; i < gxos::audio::kMaxVoices; ++i) {
        uint64_t id = gxos::audio::BackendPlay(owner, tiny, 8, &error, &backend);
        check(id != 0 && error == RequestError::Ok, "backend accept");
    }
    check(gxos::audio::BackendActiveVoicesFor(owner) == gxos::audio::kMaxVoices,
          "backend voices owned");
    uint64_t refused = gxos::audio::BackendPlay(owner, tiny, 8, &error, &backend);
    check(refused == 0 && error == RequestError::Busy, "backend busy at limit");
    check(!backend.empty(), "backend identity reported");
    // App exit reclaims its voices; a relaunch starts cleanly.
    gxos::audio::BackendStopOwner(owner);
    check(gxos::audio::BackendActiveVoicesFor(owner) == 0, "backend owner reclaim");
    uint64_t relaunched = gxos::audio::BackendPlay(owner, tiny, 8, &error, &backend);
    check(relaunched != 0 && error == RequestError::Ok, "backend relaunch clean");
    gxos::audio::BackendStopOwner(owner);
    check(gxos::audio::BackendPlaysAccepted() == before + gxos::audio::kMaxVoices + 1,
          "backend accept counter");
    check(gxos::audio::BackendPlaysBusy() >= 1, "backend busy counter");
}

}  // namespace

int main() {
    testValidation();
    testConversion();
    testMixerSingle();
    testMixerOverlapAndClip();
    testMixerDeterminism();
    testMixerExhaustionAndOwnership();
    testPolicy();
    testBackendOwnership();
    if (g_failures == 0) {
        std::cout << "App audio mixer test PASS\n";
        return 0;
    }
    std::cout << "App audio mixer test FAIL (" << g_failures << ")\n";
    return 1;
}
