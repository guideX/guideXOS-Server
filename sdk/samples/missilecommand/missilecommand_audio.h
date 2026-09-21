#pragma once

// Missile Command MC5 audio mapping: verified VB6 sndPlaySound events as
// side-effect sound requests that never touch simulation state.
//
// Source evidence (D:\dev\bkup\inactive\missilecommand, Module1.bas,
// flags=1 SND_ASYNC everywhere, gated by SoundFX):
//
//   Alarm() per DoIt level ............ Alarm.wav   (PCM8 mono 11025)
//   FireFX() per LaunchM success ...... Swoosh.wav  (ADPCM stereo 22050 ->
//                                          build-time converted to PCM16
//                                          mono 22050, see below)
//   Intercept kill .................... Thunder.wav (MISSING upstream: silence)
//   LaunchM refused (pool/quota out) .. Empty.wav   (PCM8 mono 11025)
//   MyShow missile arm (detonation) ... Explode.wav (PCM8 mono 11025)
//   MyShow bomb arm (ground impact) ... Thunder.wav (MISSING upstream: silence)
//   MyShow split arm .................. Split.wav   (PCM8 mono 11025)
//   DoIt tail blnLost ................. OhNo.wav    (PCM16 mono 22050)
//   DoIt tail blnQuit ................. Error.wav   (MISSING upstream: silence)
//   DoIt tail blnWon .................. OnNo.wav    (commented out upstream:
//                                          never compiled in: silence)
//
// sndPlaySound with a missing file is a silent no-op upstream, so every
// missing/commented-out mapping above stays silent here too. No substitute
// assets are ever synthesized.
//
// Determinism boundary: game logic emits tick events; the platform layer
// (main.cpp) turns them into play_pcm requests. Playback completion/timing
// never feeds back. This header only DETECTS events by diffing simulation
// snapshots around mc_fixed_update plus DECODES staged WAVs; it adds no
// fields to McState and changes no existing function, so identical seed +
// input stream + tick sequence fingerprints identically with audio enabled,
// unavailable, or permission-denied.
//
// Pure logic only (same contract as missilecommand_state.h): no guideXOS
// includes, no allocation, no libc. Shared by main.cpp and the host test.

#include "missilecommand_state.h"

// ---------------------------------------------------------------------------
// Sound identities (verified events only; missing sources have no identity)
// ---------------------------------------------------------------------------

static const int kMcSoundNone = 0;
static const int kMcSoundAlarm = 1;
static const int kMcSoundSwoosh = 2;
static const int kMcSoundEmpty = 3;
static const int kMcSoundExplode = 4;
static const int kMcSoundSplit = 5;
static const int kMcSoundOhNo = 6;
static const int kMcSoundCount = 7;

struct McSoundResource {
    int id;
    const char* path;
    uint32_t expectRate;
    uint32_t expectBits;
};

// Staged filenames are lowercase; the mapping to the original case-
// insensitive VB loads is documented in docs/MISSILE_COMMAND_MC5_*.
static const McSoundResource kMcSoundResources[] = {
    { kMcSoundAlarm, "resources/audio/alarm.wav", 11025u, 8u },
    { kMcSoundSwoosh, "resources/audio/swoosh.wav", 22050u, 16u },
    { kMcSoundEmpty, "resources/audio/empty.wav", 11025u, 8u },
    { kMcSoundExplode, "resources/audio/explode.wav", 11025u, 8u },
    { kMcSoundSplit, "resources/audio/split.wav", 11025u, 8u },
    { kMcSoundOhNo, "resources/audio/ohno.wav", 22050u, 16u },
};
static const uint32_t kMcSoundResourceCount = 6u;

// Largest staged voice in frames (OhNo: 73984 bytes / 2). Loaders size one
// static S16 buffer per sound from the exact counts below; no heap.
static const uint32_t kMcSoundFramesAlarm = 7498u;
static const uint32_t kMcSoundFramesSwoosh = 6656u;
static const uint32_t kMcSoundFramesEmpty = 2862u;
static const uint32_t kMcSoundFramesExplode = 23540u;
static const uint32_t kMcSoundFramesSplit = 1380u;
static const uint32_t kMcSoundFramesOhNo = 36992u;

inline uint32_t mc_sound_frame_capacity(int sound) {
    switch (sound) {
        case kMcSoundAlarm: return kMcSoundFramesAlarm;
        case kMcSoundSwoosh: return kMcSoundFramesSwoosh;
        case kMcSoundEmpty: return kMcSoundFramesEmpty;
        case kMcSoundExplode: return kMcSoundFramesExplode;
        case kMcSoundSplit: return kMcSoundFramesSplit;
        case kMcSoundOhNo: return kMcSoundFramesOhNo;
        default: return 0u;
    }
}

// ---------------------------------------------------------------------------
// Minimal WAV decoder (PCM tag 1, mono, 8/16-bit only)
//
// The staged tree holds PCM only: the one compressed heritage asset
// (Swoosh ADPCM stereo) is converted at build time by
// scripts/convert-missilecommand-swoosh.py, so neither the app nor the host
// needs an ADPCM decoder. Anything else (ADPCM tag, stereo, odd rates,
// truncated chunks) is rejected and the caller stays silent.
// ---------------------------------------------------------------------------

struct McWavPcm {
    uint32_t sampleRate;
    uint32_t bitsPerSample;
    uint32_t frameCount;
};

inline uint32_t mc_read_u32le(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

inline uint32_t mc_read_u16le(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u);
}

// Decode a whole staged WAV into caller-owned S16 frames (8-bit unsigned is
// widened to (b-128)*256; 16-bit LE is copied verbatim). Returns false on
// any malformed/unsupported input; out is untouched on failure.
inline bool mc_wav_decode(const unsigned char* bytes, uint32_t size, int16_t* outFrames,
                           uint32_t outCapacityFrames, McWavPcm* out) {
    if (!bytes || !outFrames || !out || size < 44u || outCapacityFrames == 0u) return false;
    if (bytes[0] != 'R' || bytes[1] != 'I' || bytes[2] != 'F' || bytes[3] != 'F') return false;
    if (bytes[8] != 'W' || bytes[9] != 'A' || bytes[10] != 'V' || bytes[11] != 'E') return false;
    uint32_t riffBytes = mc_read_u32le(bytes + 4);
    if (riffBytes + 8u < size) {
        // Tolerate the 188-byte GIF-style tails some tools append: the RIFF
        // size is authoritative, but a short buffer is still rejected below.
    }
    uint32_t pos = 12u;
    bool haveFmt = false;
    uint32_t fmtTag = 0;
    uint32_t channels = 0;
    uint32_t rate = 0;
    uint32_t bits = 0;
    const unsigned char* dataPtr = 0;
    uint32_t dataBytes = 0;
    while (pos + 8u <= size) {
        char c0 = (char)bytes[pos];
        char c1 = (char)bytes[pos + 1];
        char c2 = (char)bytes[pos + 2];
        char c3 = (char)bytes[pos + 3];
        uint32_t chunkBytes = mc_read_u32le(bytes + pos + 4);
        uint32_t body = pos + 8u;
        if (chunkBytes > size || body > size - chunkBytes) break;
        if (c0 == 'f' && c1 == 'm' && c2 == 't' && c3 == ' ') {
            if (chunkBytes < 16u) return false;
            fmtTag = mc_read_u16le(bytes + body);
            channels = mc_read_u16le(bytes + body + 2);
            rate = mc_read_u32le(bytes + body + 4);
            bits = mc_read_u16le(bytes + body + 14);
            haveFmt = true;
        } else if (c0 == 'd' && c1 == 'a' && c2 == 't' && c3 == 'a') {
            if (dataPtr == 0) {
                dataPtr = bytes + body;
                dataBytes = chunkBytes;
            }
        }
        uint32_t advance = 8u + chunkBytes + (chunkBytes & 1u);
        if (pos > size - advance) break;
        pos += advance;
    }
    if (!haveFmt || dataPtr == 0) return false;
    // v1 contract: decoded PCM, mono, 8/16-bit, sane rate. ADPCM (tag 2),
    // stereo heritage data, and absurd rates are all rejected.
    if (fmtTag != 1u || channels != 1u) return false;
    if (bits != 8u && bits != 16u) return false;
    if (rate < 8000u || rate > 48000u) return false;
    uint32_t bytesPerFrame = bits / 8u;
    if (dataBytes == 0u || dataBytes % bytesPerFrame != 0u) return false;
    uint32_t frames = dataBytes / bytesPerFrame;
    if (frames == 0u || frames > outCapacityFrames) return false;
    if ((uint32_t)(dataPtr - bytes) > size - dataBytes) return false;
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t sample = 0;
        if (bits == 8u) {
            sample = (int16_t)(((int32_t)dataPtr[i] - 128) * 256);
        } else {
            uint32_t lo = dataPtr[i * 2u];
            uint32_t hi = dataPtr[i * 2u + 1u];
            sample = (int16_t)(lo | (hi << 8u));
        }
        outFrames[i] = sample;
    }
    out->sampleRate = rate;
    out->bitsPerSample = bits;
    out->frameCount = frames;
    return true;
}

// ---------------------------------------------------------------------------
// Tick-event detection (snapshot before mc_fixed_update, diff after)
//
// Every detector mirrors the exact VB gate so sounds fire exactly when the
// original's sndPlaySound would (minus the missing-file no-ops, which stay
// silent by having no sound identity at all).
// ---------------------------------------------------------------------------

struct McTickPre {
    int mFired;
    int mPool;
    int levelMMax;
    int levelIndex;
    bool pendingFire;
    bool running;
    bool won;
    bool lost;
    bool gameComplete;
};

inline McTickPre mc_tick_pre(const McState* state) {
    McTickPre pre;
    pre.mFired = 0;
    pre.mPool = 0;
    pre.levelMMax = 0;
    pre.levelIndex = 1;
    pre.pendingFire = false;
    pre.running = false;
    pre.won = false;
    pre.lost = false;
    pre.gameComplete = false;
    if (!state) return pre;
    pre.mFired = state->mFired;
    pre.mPool = state->mPool;
    pre.levelMMax = state->level.mMax;
    pre.levelIndex = state->levelIndex;
    pre.pendingFire = state->pendingFire;
    pre.running = state->running;
    pre.won = state->won;
    pre.lost = state->lost;
    pre.gameComplete = state->gameComplete;
    return pre;
}

// LaunchM gate, exactly like mc_launch_m: latched fire + free pool slot +
// remaining quota while the campaign runs. Success played Swoosh via
// FireFX(); the else arm played Empty.
inline int mc_detect_launch(const McTickPre& pre) {
    if (!pre.pendingFire) return 0;
    if (!pre.running || pre.won || pre.lost || pre.gameComplete) return 0;
    if (pre.mPool == 0) return -1;
    if (!(pre.mFired < pre.levelMMax)) return -1;
    return 1;
}

// Fresh defensive detonations this tick (MyShow missile arm -> Explode).
// mc_myshow_defense moves 1 -> 2 on detonation and grows every later tick,
// so status == 2 after the tick is exactly the fresh set.
inline int mc_count_fresh_bursts(const McState* state) {
    if (!state) return 0;
    int n = 0;
    int link = state->mHead;
    int guard = 0;
    while (link >= 1 && link <= kMcPoolCap && guard <= kMcPoolCap) {
        ++guard;
        if (state->m[link].status == 2) ++n;
        link = state->m[link].link;
    }
    return n;
}

struct McTickSounds {
    int launches;  // -> Swoosh (FireFX)
    int refused;   // -> Empty (LaunchM else)
    int bursts;    // -> Explode (MyShow missile arm)
    int splits;    // -> Split (MyShow split arm)
    int alarm;     // -> Alarm (new campaign level started)
    int ohno;      // -> OhNo (campaign lost)
};

inline McTickSounds mc_tick_sounds(const McTickPre& pre, const McState* post) {
    McTickSounds sounds;
    sounds.launches = 0;
    sounds.refused = 0;
    sounds.bursts = 0;
    sounds.splits = 0;
    sounds.alarm = 0;
    sounds.ohno = 0;
    if (!post) return sounds;
    int launch = mc_detect_launch(pre);
    if (launch > 0) {
        sounds.launches = 1;
    } else if (launch < 0) {
        sounds.refused = 1;
    }
    sounds.bursts = mc_count_fresh_bursts(post);
    // VB quirk preserved in mc_myshow_hostiles: a split child increments
    // mFired (the missile counter), not bDropped. Within one fixed tick the
    // only mFired writers are the defensive launch (+1) and split children,
    // so children = delta - launches (clamped; level resets zero it).
    int delta = post->mFired - pre.mFired;
    int splits = delta - sounds.launches;
    sounds.splits = splits > 0 ? splits : 0;
    if (post->levelIndex > pre.levelIndex) sounds.alarm = 1;
    if (!pre.lost && post->lost) sounds.ohno = 1;
    // Everything else is upstream silence: intercept kills and ground
    // impacts (missing Thunder.wav), campaign quit (missing Error.wav),
    // campaign won (OnNo.wav commented out). No identities, no requests.
    return sounds;
}
