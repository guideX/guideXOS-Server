#include "native_app_audio.h"

#include "logger.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

#if defined(_WIN32) && !defined(NATIVE_APP_AUDIO_NO_BACKEND)
#include <windows.h>
#include <mmsystem.h>
#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib")
#endif
#endif

namespace gxos {
namespace audio {
namespace {

const char* kBackendNull = "null-sink";

#if !defined(NATIVE_APP_AUDIO_NO_BACKEND)
const char* kBackendWaveOut = "waveout-22050-mono-s16";

std::string toLowerCopy(const char* value) {
    std::string out;
    if (!value) return out;
    for (const char* p = value; *p; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        out.push_back(c);
    }
    return out;
}

bool nullBackendForced() {
    const char* value = std::getenv("GXOS_AUDIO_BACKEND");
    if (!value || !*value) return false;
    const std::string lower = toLowerCopy(value);
    return lower == "null" || lower == "none" || lower == "silent" || lower == "0";
}
#endif

int16_t saturate32(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
}

}  // namespace

ValidationResult ValidatePlayRequest(const void* pcmData, uint32_t pcmBytes,
                                     uint32_t sampleRateHz, uint32_t channels,
                                     uint32_t bitsPerSample) {
    ValidationResult result;
    if (!pcmData) {
        result.error = RequestError::InvalidArgument;
        result.reason = "null-pcm-data";
        return result;
    }
    if (pcmBytes == 0) {
        result.error = RequestError::InvalidArgument;
        result.reason = "empty-pcm";
        return result;
    }
    if (pcmBytes > kMaxPcmBytes) {
        result.error = RequestError::Unsupported;
        result.reason = "pcm-oversized";
        return result;
    }
    if (channels != kChannels) {
        result.error = RequestError::InvalidArgument;
        result.reason = "channels-must-be-mono";
        return result;
    }
    if (bitsPerSample != 8 && bitsPerSample != 16) {
        result.error = RequestError::InvalidArgument;
        result.reason = "bits-must-be-8-or-16";
        return result;
    }
    if (sampleRateHz < kMinRateHz || sampleRateHz > kMaxRateHz) {
        result.error = RequestError::InvalidArgument;
        result.reason = "sample-rate-out-of-range";
        return result;
    }
    const uint32_t bytesPerFrame = bitsPerSample / 8u;
    if (pcmBytes % bytesPerFrame != 0u) {
        result.error = RequestError::InvalidArgument;
        result.reason = "pcm-not-whole-frames";
        return result;
    }
    const uint64_t frameCount = pcmBytes / bytesPerFrame;
    if (frameCount == 0 || frameCount > std::numeric_limits<uint32_t>::max()) {
        result.error = RequestError::InvalidArgument;
        result.reason = "frame-count-invalid";
        return result;
    }
    // Resampled worst case: lowest input rate expands the most.
    const uint64_t worstFrames = (frameCount * kMixRateHz + kMinRateHz - 1u) / kMinRateHz + 1u;
    if (worstFrames > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        result.error = RequestError::Unsupported;
        result.reason = "voice-too-long";
        return result;
    }
    return result;
}

bool HasAudioOutputPermission(const std::vector<std::string>& permissions) {
    for (const std::string& granted : permissions) {
        if (granted == kPermission) return true;
    }
    return false;
}

bool ConvertToMixFormat(const void* pcmData, uint32_t pcmBytes, uint32_t sampleRateHz,
                        uint32_t bitsPerSample, std::vector<int16_t>& outMonoFrames) {
    outMonoFrames.clear();
    if (!pcmData || pcmBytes == 0 || (bitsPerSample != 8 && bitsPerSample != 16) ||
        sampleRateHz < kMinRateHz || sampleRateHz > kMaxRateHz) {
        return false;
    }
    const uint32_t bytesPerFrame = bitsPerSample / 8u;
    if (pcmBytes % bytesPerFrame != 0u) return false;
    const uint32_t inFrames = pcmBytes / bytesPerFrame;
    if (inFrames == 0) return false;
    const uint64_t outFrames64 =
        (static_cast<uint64_t>(inFrames) * kMixRateHz + sampleRateHz - 1u) / sampleRateHz;
    if (outFrames64 == 0 || outFrames64 > kMaxVoiceFrames) return false;
    const uint32_t outFrames = static_cast<uint32_t>(outFrames64);
    outMonoFrames.resize(outFrames);
    const uint8_t* bytes = static_cast<const uint8_t*>(pcmData);
    for (uint32_t i = 0; i < outFrames; ++i) {
        uint32_t src = static_cast<uint32_t>((static_cast<uint64_t>(i) * inFrames) / outFrames);
        if (src >= inFrames) src = inFrames - 1u;
        int16_t sample = 0;
        if (bitsPerSample == 8) {
            sample = static_cast<int16_t>((static_cast<int32_t>(bytes[src]) - 128) * 256);
        } else {
            uint32_t lo = bytes[src * 2u];
            uint32_t hi = bytes[src * 2u + 1u];
            sample = static_cast<int16_t>(lo | (hi << 8u));
        }
        outMonoFrames[i] = sample;
    }
    return true;
}

Mixer::Mixer() = default;

bool Mixer::Play(uint64_t ownerId, const int16_t* frames, uint32_t frameCount) {
    if (!frames || frameCount == 0 || frameCount > kMaxVoiceFrames) return false;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active) continue;
        voices_[i].active = true;
        voices_[i].owner = ownerId;
        voices_[i].position = 0;
        voices_[i].samples.assign(frames, frames + frameCount);
        return true;
    }
    return false;
}

void Mixer::Mix(int16_t* outFrames, uint32_t frameCount) {
    if (!outFrames) return;
    for (uint32_t i = 0; i < frameCount; ++i) {
        int32_t mixed = 0;
        for (uint32_t v = 0; v < kMaxVoices; ++v) {
            Voice& voice = voices_[v];
            if (!voice.active) continue;
            if (voice.position < voice.samples.size()) {
                mixed += voice.samples[voice.position++];
            }
            if (voice.position >= voice.samples.size()) {
                voice.active = false;
                voice.owner = 0;
                voice.position = 0;
                voice.samples.clear();
                voice.samples.shrink_to_fit();
            }
        }
        outFrames[i] = saturate32(mixed);
    }
}

uint32_t Mixer::ActiveVoices() const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active) ++count;
    }
    return count;
}

uint32_t Mixer::ActiveVoicesFor(uint64_t ownerId) const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active && voices_[i].owner == ownerId) ++count;
    }
    return count;
}

void Mixer::StopOwner(uint64_t ownerId) {
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active && voices_[i].owner == ownerId) {
            voices_[i].active = false;
            voices_[i].owner = 0;
            voices_[i].position = 0;
            voices_[i].samples.clear();
            voices_[i].samples.shrink_to_fit();
        }
    }
}

void Mixer::StopAll() {
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].active = false;
        voices_[i].owner = 0;
        voices_[i].position = 0;
        voices_[i].samples.clear();
        voices_[i].samples.shrink_to_fit();
    }
}

#if defined(NATIVE_APP_AUDIO_NO_BACKEND)

// Test/header-only builds: no OS backend. Voices are still fully managed by
// the Mixer above; playback reports the null sink explicitly.
namespace {
Mixer g_testMixer;
std::mutex g_testMutex;
std::atomic<uint64_t> g_testAccepted{0};
std::atomic<uint64_t> g_testBusy{0};
}  // namespace

uint64_t BackendPlay(uint64_t ownerId, const int16_t* frames, uint32_t frameCount,
                     RequestError* outError, std::string* outBackend) {
    if (outError) *outError = RequestError::Ok;
    if (outBackend) *outBackend = kBackendNull;
    std::lock_guard<std::mutex> lock(g_testMutex);
    if (!g_testMixer.Play(ownerId, frames, frameCount)) {
        ++g_testBusy;
        if (outError) *outError = RequestError::Busy;
        return 0;
    }
    ++g_testAccepted;
    return g_testAccepted.load();
}

void BackendStopOwner(uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(g_testMutex);
    g_testMixer.StopOwner(ownerId);
}

uint32_t BackendActiveVoices() {
    std::lock_guard<std::mutex> lock(g_testMutex);
    return g_testMixer.ActiveVoices();
}

uint32_t BackendActiveVoicesFor(uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(g_testMutex);
    return g_testMixer.ActiveVoicesFor(ownerId);
}

const char* BackendName() { return kBackendNull; }
uint64_t BackendPlaysAccepted() { return g_testAccepted.load(); }
uint64_t BackendPlaysBusy() { return g_testBusy.load(); }
uint64_t BackendChunksWritten() { return 0; }

#else

// Hosted backend: one shared mixer, one render thread, waveOut when a device
// opens, otherwise an explicit null sink (same mixer semantics, output
// discarded). Short one-shot voices mean a simple chunk pump is enough; no
// streaming, no effects, no per-app graphs.
namespace {

constexpr uint32_t kChunkFrames = 1024;
constexpr uint32_t kWaveBuffers = 4;

Mixer g_mixer;
std::mutex g_mutex;
std::thread g_renderThread;
std::atomic<bool> g_renderRunning{false};
std::atomic<bool> g_backendStarted{false};
std::atomic<uint64_t> g_playsAccepted{0};
std::atomic<uint64_t> g_playsBusy{0};
std::atomic<uint64_t> g_chunksWritten{0};
std::atomic<bool> g_waveAvailable{false};
std::string g_backendName = kBackendNull;

#if defined(_WIN32)
HWAVEOUT g_waveOut = nullptr;
WAVEHDR g_waveHeaders[kWaveBuffers];
std::vector<int16_t> g_waveStorage;
uint32_t g_waveNext = 0;
// Freshly prepared headers carry WHDR_PREPARED but not WHDR_DONE (the
// driver sets DONE only after a buffer has played once), so free-buffer
// bookkeeping is kept here: never-used buffers are free, used ones are
// free again once the driver marks them DONE.
bool g_waveFree[kWaveBuffers] = {true, true, true, true};

void closeWaveOut() {
    if (!g_waveOut) return;
    waveOutReset(g_waveOut);
    for (uint32_t i = 0; i < kWaveBuffers; ++i) {
        if (g_waveHeaders[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_waveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
    }
    waveOutClose(g_waveOut);
    g_waveOut = nullptr;
    g_waveStorage.clear();
}

bool openWaveOut() {
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = kMixRateHz;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = kMixRateHz * 2u;
    format.cbSize = 0;
    if (waveOutOpen(&g_waveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_waveOut = nullptr;
        return false;
    }
    g_waveStorage.assign(kWaveBuffers * kChunkFrames, 0);
    for (uint32_t i = 0; i < kWaveBuffers; ++i) {
        g_waveHeaders[i] = {};
        g_waveHeaders[i].lpData = reinterpret_cast<LPSTR>(g_waveStorage.data() + i * kChunkFrames);
        g_waveHeaders[i].dwBufferLength = kChunkFrames * sizeof(int16_t);
        g_waveHeaders[i].dwFlags = 0;
        if (waveOutPrepareHeader(g_waveOut, &g_waveHeaders[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            closeWaveOut();
            return false;
        }
    }
    g_waveNext = 0;
    return true;
}

bool bufferReady(uint32_t index) {
    if (g_waveFree[index]) return true;
    return (g_waveHeaders[index].dwFlags & WHDR_DONE) != 0;
}

bool writeWaveChunk(const int16_t* frames) {
    if (!g_waveOut) return false;
    // Wait (bounded) for a finished buffer so a hung device cannot stall the
    // render thread forever; on timeout the chunk is dropped, never queued
    // unboundedly, and the device is kept (only a waveOutWrite error
    // degrades to the null sink).
    for (int wait = 0; wait < 200; ++wait) {
        const uint32_t index = g_waveNext;
        WAVEHDR& header = g_waveHeaders[index];
        if (bufferReady(index)) {
            std::memcpy(header.lpData, frames, kChunkFrames * sizeof(int16_t));
            header.dwFlags &= ~WHDR_DONE;
            if (waveOutWrite(g_waveOut, &header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
            g_waveFree[index] = false;
            g_waveNext = (g_waveNext + 1u) % kWaveBuffers;
            return true;
        }
        Sleep(5);
    }
    return true;  // timeout: chunk dropped, device retained
}
#endif  // defined(_WIN32)

void renderLoop() {
    std::vector<int16_t> chunk(kChunkFrames, 0);
    while (g_renderRunning.load()) {
        uint32_t active = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            active = g_mixer.ActiveVoices();
            if (active > 0) g_mixer.Mix(chunk.data(), kChunkFrames);
        }
        if (active == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        bool written = false;
#if defined(_WIN32)
        if (g_waveAvailable.load()) {
            std::lock_guard<std::mutex> lock(g_mutex);
            written = writeWaveChunk(chunk.data());
            if (!written) {
                // A broken device degrades to the null sink explicitly
                // rather than stalling playback or faking success.
                g_waveAvailable.store(false);
                closeWaveOut();
                g_backendName = kBackendNull;
                Logger::write(LogLevel::Warn, "[AppAudio] waveOut write failed; degraded to null-sink mixer");
            }
        }
#endif
        // Null sink still consumes in real time so voice lifetimes and
        // reclaim behave identically with or without hardware.
        ++g_chunksWritten;
        (void)written;
        std::this_thread::sleep_for(
            std::chrono::milliseconds((kChunkFrames * 1000u) / kMixRateHz));
    }
}

void ensureBackendLocked() {
    if (g_backendStarted.exchange(true)) return;
#if defined(_WIN32)
    if (!nullBackendForced() && openWaveOut()) {
        g_waveAvailable.store(true);
        g_backendName = kBackendWaveOut;
        Logger::write(LogLevel::Info, "[AppAudio] backend=waveout-22050-mono-s16 (WinMM)");
    } else {
        g_backendName = kBackendNull;
        Logger::write(LogLevel::Warn,
                      "[AppAudio] backend=null-sink (no waveOut device or GXOS_AUDIO_BACKEND=null); "
                      "mixer still queues, mixes, and reclaims");
    }
#else
    g_backendName = kBackendNull;
    Logger::write(LogLevel::Warn, "[AppAudio] backend=null-sink (non-Windows host)");
#endif
    g_renderRunning.store(true);
    g_renderThread = std::thread(renderLoop);
    std::atexit([]() {
        g_renderRunning.store(false);
        if (g_renderThread.joinable()) g_renderThread.join();
#if defined(_WIN32)
        closeWaveOut();
#endif
    });
}

}  // namespace

uint64_t BackendPlay(uint64_t ownerId, const int16_t* frames, uint32_t frameCount,
                     RequestError* outError, std::string* outBackend) {
    if (outError) *outError = RequestError::Ok;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureBackendLocked();
    if (outBackend) *outBackend = g_backendName;
    if (!g_mixer.Play(ownerId, frames, frameCount)) {
        ++g_playsBusy;
        if (outError) *outError = RequestError::Busy;
        return 0;
    }
    ++g_playsAccepted;
    return g_playsAccepted.load();
}

void BackendStopOwner(uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_mixer.StopOwner(ownerId);
}

uint32_t BackendActiveVoices() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_mixer.ActiveVoices();
}

uint32_t BackendActiveVoicesFor(uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_mixer.ActiveVoicesFor(ownerId);
}

const char* BackendName() { return g_backendName.c_str(); }
uint64_t BackendPlaysAccepted() { return g_playsAccepted.load(); }
uint64_t BackendPlaysBusy() { return g_playsBusy.load(); }
uint64_t BackendChunksWritten() { return g_chunksWritten.load(); }

#endif  // NATIVE_APP_AUDIO_NO_BACKEND

}  // namespace audio
}  // namespace gxos
