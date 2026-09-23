// MC6 bare-metal audio streaming backend tests (host-side).
//
// Compiles the freestanding kernel module (kernel/core/app_audio_stream.cpp)
// with a scripted mock HDA controller: mock MMIO register file, mock codec
// verbs, scripted LPIB motion and clock. Covers validation/conversion/mixer
// parity with MC5, mono->stereo adaptation, ring bookkeeping, wraparound,
// refill, underrun, recovery, lifecycle, and the end-to-end self-test.
//
// Build: g++ -std=c++17 tests/app_audio_stream_test.cpp
//        kernel/core/app_audio_stream.cpp -I kernel/core/include
//        (see scripts/run-app-audio-stream-test.ps1).

// Quoted relative include (repo convention, cf. app_audio_mixer_test.cpp):
// avoids putting the freestanding kernel include directory on the host
// include path (its stdio.h/stdlib.h stubs are hosted-incompatible).
#include "../kernel/core/include/kernel/app_audio_stream.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

using kernel::app_audio::Backend;
using kernel::app_audio::HdaOps;
using kernel::app_audio::OutputPath;

// ================================================================
// Mock HDA controller.
// ================================================================

struct MockHda {
    static const int kMaxSd = 8;
    // Per-SD register files (SDn base = 0x80 + n*0x20).
    uint32_t ctl[kMaxSd];
    uint8_t sts[kMaxSd];
    uint32_t lpib[kMaxSd];
    uint32_t cbl[kMaxSd];
    uint16_t lvi[kMaxSd];
    uint16_t fmt[kMaxSd];
    uint32_t bdpl[kMaxSd];
    uint32_t bdpu[kMaxSd];
    uint16_t gcap = 0x0000; // ISS=0: SD0 is the output descriptor

    bool running() const {
        for (int i = 0; i < kMaxSd; ++i) {
            if (ctl[i] & 0x02u) return true;
        }
        return false;
    }

    // Codec script.
    uint8_t dacNode = 2;
    uint8_t pinNode = 5;
    bool failSetConvFmt = false;
    bool failAllVerbs = false;
    bool failRun = false; // RUN bit never sticks (broken stream start)
    std::vector<uint32_t> verbs; // recorded verb payloads (verb & 0xFFFFF)

    // Clock + log.
    uint64_t nowMs = 1000;
    std::string log;
    bool autoAdvance = false; // advance LPIB on every read while running

    void reset() {
        for (int i = 0; i < kMaxSd; ++i) {
            ctl[i] = 0;
            sts[i] = 0;
            lpib[i] = 0;
            cbl[i] = 0;
            lvi[i] = 0;
            fmt[i] = 0;
            bdpl[i] = 0;
            bdpu[i] = 0;
        }
        gcap = 0x0000;
        verbs.clear();
        nowMs = 1000;
        log.clear();
        autoAdvance = false;
        failSetConvFmt = false;
        failAllVerbs = false;
        failRun = false;
    }

    void advance(uint32_t bytes) {
        for (int i = 0; i < kMaxSd; ++i) {
            if (!(ctl[i] & 0x02u) || cbl[i] == 0) continue;
            lpib[i] = (lpib[i] + bytes) % cbl[i];
        }
    }

    // Advance one descriptor (scripted cursor placement for wrap tests).
    void set_lpib(int sd, uint32_t value) {
        if (sd >= 0 && sd < kMaxSd) lpib[sd] = value;
    }
};

MockHda g_mock;

// Fake DMA-visible addresses (low, stable, 128B-aligned BDL).
// Keyed by pointer so repeated program_ring() calls (recovery path) and
// distinct Backend instances each get self-consistent mappings.
struct VirtMapEntry {
    const void* virt;
    uint64_t phys;
};

VirtMapEntry g_virtMap[16];
int g_virtCount = 0;

uint64_t mock_virt_to_phys(const void* ptr) {
    for (int i = 0; i < g_virtCount; ++i) {
        if (g_virtMap[i].virt == ptr) return g_virtMap[i].phys;
    }
    uint64_t addr = (g_virtCount == 0) ? 0x00200000ull
                                       : (0x00210000ull + (uint64_t)(g_virtCount - 1) * 0x1000ull);
    if (g_virtCount < 16) {
        g_virtMap[g_virtCount].virt = ptr;
        g_virtMap[g_virtCount].phys = addr;
        ++g_virtCount;
    }
    return addr;
}

void mock_reset_all() {
    g_mock.reset();
    g_virtCount = 0;
    for (int i = 0; i < 16; ++i) {
        g_virtMap[i].virt = nullptr;
        g_virtMap[i].phys = 0;
    }
}

uint64_t g_mmioBase = 0xFEBF0000ull;

// SD index from a stream-descriptor register offset, or -1.
int mock_sd(uint32_t off) {
    if (off < 0x80u) return -1;
    const uint32_t rel = off - 0x80u;
    const int sd = (int)(rel / 0x20u);
    if (sd < 0 || sd >= MockHda::kMaxSd) return -1;
    return sd;
}

uint32_t mock_sd_rel(uint32_t off) { return (off - 0x80u) % 0x20u; }

uint8_t mock_r8(uint64_t base, uint32_t off) {
    (void)base;
    const int sd = mock_sd(off);
    if (sd < 0) return 0;
    if (mock_sd_rel(off) == 0x03) return g_mock.sts[sd];
    return 0;
}

uint16_t mock_r16(uint64_t base, uint32_t off) {
    (void)base;
    if (off == 0x00) return g_mock.gcap;
    const int sd = mock_sd(off);
    if (sd < 0) return 0;
    if (mock_sd_rel(off) == 0x0C) return g_mock.lvi[sd];
    if (mock_sd_rel(off) == 0x12) return g_mock.fmt[sd];
    return 0;
}

uint32_t mock_r32(uint64_t base, uint32_t off) {
    (void)base;
    const int sd = mock_sd(off);
    if (sd < 0) return 0;
    if (mock_sd_rel(off) == 0x00) return g_mock.ctl[sd];
    if (mock_sd_rel(off) == 0x04) {
        if (g_mock.autoAdvance) g_mock.advance(512);
        return g_mock.lpib[sd];
    }
    if (mock_sd_rel(off) == 0x08) return g_mock.cbl[sd];
    if (mock_sd_rel(off) == 0x18) return g_mock.bdpl[sd];
    if (mock_sd_rel(off) == 0x1C) return g_mock.bdpu[sd];
    return 0;
}

void mock_w8(uint64_t base, uint32_t off, uint8_t v) {
    (void)base;
    const int sd = mock_sd(off);
    if (sd < 0) return;
    if (mock_sd_rel(off) == 0x00) {
        if (v & 0x01u) {
            g_mock.ctl[sd] &= ~0x01u;
            g_mock.lpib[sd] = 0;
        }
        return;
    }
    if (mock_sd_rel(off) == 0x03) {
        g_mock.sts[sd] &= (uint8_t)~v;
        return;
    }
}

void mock_w16(uint64_t base, uint32_t off, uint16_t v) {
    (void)base;
    const int sd = mock_sd(off);
    if (sd < 0) return;
    if (mock_sd_rel(off) == 0x0C) {
        g_mock.lvi[sd] = v;
        return;
    }
    if (mock_sd_rel(off) == 0x12) {
        g_mock.fmt[sd] = v;
        return;
    }
}

void mock_w32(uint64_t base, uint32_t off, uint32_t v) {
    (void)base;
    const int sd = mock_sd(off);
    if (sd < 0) return;
    if (mock_sd_rel(off) == 0x00) {
        const bool wasReset = (v & 0x01u) != 0u;
        g_mock.ctl[sd] = v;
        if (wasReset) {
            g_mock.ctl[sd] &= ~0x01u;
            g_mock.lpib[sd] = 0;
        }
        if (g_mock.failRun) g_mock.ctl[sd] &= ~0x02u; // RUN never sticks
        return;
    }
    if (mock_sd_rel(off) == 0x04) {
        g_mock.lpib[sd] = v;
        return;
    }
    if (mock_sd_rel(off) == 0x08) {
        g_mock.cbl[sd] = v;
        return;
    }
    if (mock_sd_rel(off) == 0x18) {
        g_mock.bdpl[sd] = v;
        return;
    }
    if (mock_sd_rel(off) == 0x1C) {
        g_mock.bdpu[sd] = v;
        return;
    }
}

bool mock_send_verb(uint8_t ctrl, uint8_t codec, uint8_t node, uint32_t verb,
                    uint32_t* resp) {
    (void)ctrl;
    (void)codec;
    (void)node;
    if (g_mock.failAllVerbs) return false;
    const uint32_t payload = verb & 0xFFFFFu;
    const uint32_t id = payload & 0xFFF00u;
    // Record node-packed verbs so path selection is assertable per node.
    g_mock.verbs.push_back(((uint32_t)node << 20) | payload);
    if (id == 0x20000u && g_mock.failSetConvFmt) return false; // SET_CONV_FMT
    if (resp) {
        if (id == 0xF0000u) {
            const uint32_t param = payload & 0xFFu;
            if (param == 0x0Au) *resp = 0x00000057u; // fake PCM sizes/rates
            else if (param == 0x0Eu) *resp = 0x00000001u; // 1 conn entry
            else *resp = 0;
        } else if (id == 0xF0200u) {
            *resp = (uint32_t)g_mock.dacNode; // conn list: our DAC first
        } else if (id == 0xF0700u) {
            *resp = 0xC0u; // pin ctrl readback: out enabled
        } else {
            *resp = 0;
        }
    }
    return true;
}

uint64_t mock_ticks() { return g_mock.nowMs; }

void mock_log(const char* s) { g_mock.log += s; }
void mock_hex32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", v);
    g_mock.log += buf;
}
void mock_hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)v);
    g_mock.log += buf;
}

HdaOps make_ops() {
    HdaOps ops;
    ops.read8 = mock_r8;
    ops.read16 = mock_r16;
    ops.read32 = mock_r32;
    ops.write8 = mock_w8;
    ops.write16 = mock_w16;
    ops.write32 = mock_w32;
    ops.send_verb = mock_send_verb;
    ops.virt_to_phys = mock_virt_to_phys;
    ops.ticks_ms = mock_ticks;
    ops.log_puts = mock_log;
    ops.log_hex32 = mock_hex32;
    ops.log_hex64 = mock_hex64;
    return ops;
}

OutputPath make_path() {
    OutputPath p;
    p.codecAddr = 0;
    p.afgNode = 1;
    p.dacNode = 2;
    p.pinNode = 5;
    p.valid = true;
    return p;
}

bool verb_seen(uint32_t fullPayload) {
    for (uint32_t v : g_mock.verbs) {
        if (v == fullPayload) return true;
    }
    return false;
}

// ================================================================
// Tests.
// ================================================================

using kernel::app_audio::RequestError;
using kernel::app_audio::ValidationResult;

void test_abi_pins() {
    check(kernel::app_audio::kMixRateHz == 22050u, "mix rate 22050");
    check(kernel::app_audio::kMaxVoices == 16u, "16 voices");
    check(kernel::app_audio::kMaxVoiceFrames == 88200u, "88200 max frames");
    check(kernel::app_audio::kMaxPcmBytes == 262144u, "256KiB max");
    check(kernel::app_audio::kDeviceRateHz == 48000u, "device 48k");
    check(kernel::app_audio::kDeviceChannels == 2u, "device stereo");
    check(kernel::app_audio::kDeviceBits == 16u, "device s16");
    check(kernel::app_audio::kRingDescriptors == 8u, "8 descs");
    check(kernel::app_audio::kDescBytes == 4096u, "4096 desc bytes");
    check(kernel::app_audio::kRingBytes == 32768u, "32768 ring bytes");
    check(sizeof(kernel::app_audio::BdlEntry) == 16u, "bdl entry 16 bytes");
    check(offsetof(kernel::app_audio::BdlEntry, address) == 0u, "bdl addr off");
    check(offsetof(kernel::app_audio::BdlEntry, length) == 8u, "bdl len off");
    check(offsetof(kernel::app_audio::BdlEntry, flags) == 12u, "bdl flags off");
}

void test_validation() {
    static uint8_t pcm[64];
    std::memset(pcm, 0x80, sizeof(pcm));
    using kernel::app_audio::validate_play_request;
    using kernel::app_audio::kMaxPcmBytes;
    check(validate_play_request(pcm, 11025, 11025, 1, 8).error == RequestError::ErrorOk, "valid 8bit 11025");
    check(validate_play_request(pcm, 64, 22050, 1, 16).error == RequestError::ErrorOk, "valid 16bit 22050");
    check(validate_play_request(pcm, 48, 44100, 1, 16).error == RequestError::ErrorOk, "valid 44100");
    check(validate_play_request(pcm, 32, 8000, 1, 8).error == RequestError::ErrorOk, "valid min rate");
    check(validate_play_request(pcm, 32, 48000, 1, 8).error == RequestError::ErrorOk, "valid max rate");
    check(validate_play_request(nullptr, 64, 22050, 1, 16).error == RequestError::ErrorInvalidArgument, "null data");
    check(validate_play_request(pcm, 0, 22050, 1, 16).error == RequestError::ErrorInvalidArgument, "empty");
    check(validate_play_request(pcm, kMaxPcmBytes + 2u, 22050, 1, 16).error == RequestError::ErrorUnsupported, "oversize unsupported");
    check(std::strcmp(validate_play_request(pcm, kMaxPcmBytes + 2u, 22050, 1, 16).reason, "pcm-oversized") == 0, "oversize reason");
    check(validate_play_request(pcm, 64, 22050, 2, 16).error == RequestError::ErrorInvalidArgument, "stereo rejected");
    check(validate_play_request(pcm, 64, 22050, 1, 24).error == RequestError::ErrorInvalidArgument, "24bit rejected");
    check(validate_play_request(pcm, 64, 7999, 1, 16).error == RequestError::ErrorInvalidArgument, "rate low");
    check(validate_play_request(pcm, 64, 48001, 1, 16).error == RequestError::ErrorInvalidArgument, "rate high");
    check(validate_play_request(pcm, 63, 22050, 1, 16).error == RequestError::ErrorInvalidArgument, "non-whole frames");
}

void test_conversion() {
    using kernel::app_audio::convert_to_mix_format;
    static int16_t out[88200];
    uint32_t count = 0;
    const uint8_t src8[4] = {0x00, 0x80, 0xFF, 0x80};
    check(convert_to_mix_format(src8, 4, 22050, 8, out, 88200, &count), "conv8 ok");
    check(count == 4 && out[0] == -32768 && out[1] == 0 && out[2] == 32512 && out[3] == 0, "conv8 values");
    const uint8_t src16[4] = {0x00, 0x80, 0xFF, 0x7F};
    check(convert_to_mix_format(src16, 4, 22050, 16, out, 88200, &count), "conv16 ok");
    check(count == 2 && out[0] == -32768 && out[1] == 32767, "conv16 values");
    const uint8_t src11[4] = {0x80, 0xFF, 0x00, 0x80};
    check(convert_to_mix_format(src11, 4, 11025, 8, out, 88200, &count), "upsample ok");
    check(count == 8, "upsample length");
    check(out[0] == 0 && out[1] == 0 && out[2] == 32512 && out[3] == 32512 &&
              out[4] == -32768 && out[5] == -32768 && out[6] == 0 && out[7] == 0,
          "upsample nearest");
    std::vector<uint8_t> huge(60000, 0x80);
    check(!convert_to_mix_format(huge.data(), 60000, 8000, 8, out, 88200, &count), "voice-too-long");
    check(!convert_to_mix_format(src8, 4, 22050, 8, out, 2, &count), "capacity guard");
    check(!convert_to_mix_format(nullptr, 4, 22050, 8, out, 88200, &count), "conv null guard");
}

void test_mixer() {
    using kernel::app_audio::Mixer;
    {
        static Mixer m;
        int16_t voice[4] = {1000, 2000, -3000, 4000};
        check(m.play(7, voice, 4), "mixer play one");
        check(m.active_voices() == 1, "mixer one active");
        check(m.active_voices_for(7) == 1, "mixer owner count");
        check(m.active_voices_for(8) == 0, "mixer other owner");
        int16_t rendered[4] = {0};
        m.mix(rendered, 4);
        check(rendered[0] == 1000 && rendered[1] == 2000 && rendered[2] == -3000 && rendered[3] == 4000, "mixer exact");
        check(m.active_voices() == 0, "mixer reclaimed");
        int16_t silence[4] = {9, 9, 9, 9};
        m.mix(silence, 4);
        check(silence[0] == 0 && silence[3] == 0, "mixer idle silence");
    }
    {
        static Mixer m;
        int16_t a[4] = {1000, 1000, 1000, 1000};
        int16_t b[4] = {2000, -500, 30000, -30000};
        check(m.play(1, a, 4), "overlap a");
        check(m.play(2, b, 4), "overlap b");
        int16_t rendered[4] = {0};
        m.mix(rendered, 4);
        check(rendered[0] == 3000 && rendered[1] == 500 && rendered[2] == 31000 && rendered[3] == -29000, "overlap sum");
        static Mixer clip;
        int16_t hot[2] = {32767, -32768};
        check(clip.play(1, hot, 2), "clip a");
        check(clip.play(2, hot, 2), "clip b");
        int16_t clipped[2] = {0};
        clip.mix(clipped, 2);
        check(clipped[0] == 32767 && clipped[1] == -32768, "saturating");
    }
    {
        static Mixer m;
        int16_t tiny[2] = {7, 7};
        for (uint32_t i = 0; i < 16; ++i) {
            char name[64];
            std::snprintf(name, sizeof(name), "fill %u", i);
            check(m.play(1000 + i, tiny, 2), name);
        }
        check(!m.play(9999, tiny, 2), "exhaustion refuses");
        m.stop_owner(1000);
        check(m.active_voices() == 15, "owner stop frees");
        check(m.play(9999, tiny, 2), "slot reusable");
        m.stop_all();
        check(m.active_voices() == 0, "stop-all");
        check(!m.play(5, nullptr, 2), "null refused");
        check(!m.play(5, tiny, 0), "empty refused");
    }
}

void test_adapt() {
    using kernel::app_audio::adapt_chunk;
    using kernel::app_audio::mix_needed_for_device;
    using kernel::app_audio::mix_source_start;
    check(mix_needed_for_device(1024) == 470, "first chunk needs 470");
    // Phase continuity: consecutive chunks need 470 or 471 frames.
    uint64_t base = 0;
    for (uint32_t c = 0; c < 64; ++c) {
        const uint32_t s0 = mix_source_start(base);
        const uint32_t s1 = mix_source_start(base + 1024);
        check(s1 > s0 && (s1 - s0 == 470 || s1 - s0 == 471), "chunk count 470/471");
        base += 1024;
    }
    // Monotonic source mapping.
    for (uint64_t i = 0; i < 4096; ++i) {
        check(mix_source_start(i + 1) >= mix_source_start(i), "src monotonic");
    }
    // Deterministic: same input twice -> identical bytes.
    static int16_t mix[960];
    for (uint32_t i = 0; i < 960; ++i) mix[i] = (int16_t)(i * 37 - 8000);
    static int16_t first[2048], second[2048];
    adapt_chunk(mix, 0, first, 1024);
    adapt_chunk(mix, 0, second, 1024);
    check(std::memcmp(first, second, sizeof(first)) == 0, "adapt deterministic");
    // Stereo duplication + mapping spot-checks.
    check(first[0] == mix[0] && first[1] == mix[0], "dup frame0");
    // device frame 3 -> src (3*22050)/48000 = 1.
    check(first[3 * 2] == mix[1] && first[3 * 2 + 1] == mix[1], "dup frame3 src1");
    // Silence in -> silence out.
    static int16_t zeros[960] = {0};
    static int16_t silent[2048];
    for (uint32_t i = 0; i < 2048; ++i) silent[i] = 9;
    adapt_chunk(zeros, 4096, silent, 1024);
    bool allZero = true;
    for (uint32_t i = 0; i < 2048; ++i) allZero = allZero && (silent[i] == 0);
    check(allZero, "silence adaptation");
    // DC value duplicated everywhere.
    static int16_t dc[960];
    for (uint32_t i = 0; i < 960; ++i) dc[i] = 1234;
    static int16_t dcOut[2048];
    adapt_chunk(dc, 2048, dcOut, 1024);
    bool allDc = true;
    for (uint32_t i = 0; i < 2048; ++i) allDc = allDc && (dcOut[i] == 1234);
    check(allDc, "dc duplication");
}

void test_bring_up_success() {
    mock_reset_all();
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "bring-up ok");
    check(backend.state() == kernel::app_audio::BackendState::Ready, "state ready");
    check(std::strcmp(backend.name(), "hda-48000-stereo-s16") == 0, "backend name");
    // Codec verbs programmed the fixed device format + path.
    check(verb_seen((1u << 20) | 0x70500u), "afg power d0");
    check(verb_seen((2u << 20) | 0x20011u), "dac conv fmt 48k stereo s16");
    check(verb_seen((2u << 20) | 0x70610u), "dac stream tag 1");
    check(verb_seen((5u << 20) | 0x707C0u), "pin out enabled");
    check(verb_seen((5u << 20) | 0x70C02u), "eapd enabled");
    check(verb_seen((2u << 20) | 0x70500u), "dac power d0");
    check(verb_seen((5u << 20) | 0x70500u), "pin power d0");
    // Stream descriptor programmed.
    check(g_mock.fmt[0] == 0x0011u, "sd fmt 0x0011");
    check(g_mock.cbl[0] == 32768u, "sd cbl ring bytes");
    check(g_mock.lvi[0] == 7u, "sd lvi 7");
    check(g_mock.bdpl[0] == 0x00200000u, "bdpl mock bdl");
    check(g_mock.bdpu[0] == 0u, "bdpu zero");
    check((g_mock.ctl[0] & 0x02u) != 0u, "run bit set");
    check((g_mock.ctl[0] & 0x00F00000u) == 0x00100000u, "tag 1 in ctl");
    check(g_mock.log.find("backend ready") != std::string::npos, "ready logged");
}

void test_output_sd_selection() {
    // HDA descriptors are input-first: with ISS=4 (ICH6/QEMU layout,
    // GCAP=0x4401) the first OUTPUT descriptor is SD4 at 0x100, not SD0.
    // Programming SD0 would arm a capture descriptor whose LPIB never
    // moves for playback (MC6 audit finding, proven on QEMU).
    mock_reset_all();
    g_mock.gcap = 0x4401u; // OSS=4, ISS=4
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "sd4 bring-up ok");
    check(backend.state() == kernel::app_audio::BackendState::Ready, "sd4 ready");
    check(g_mock.fmt[4] == 0x0011u, "sd4 fmt");
    check(g_mock.cbl[4] == 32768u, "sd4 cbl");
    check(g_mock.lvi[4] == 7u, "sd4 lvi");
    check(g_mock.bdpl[4] == 0x00200000u, "sd4 bdpl");
    check((g_mock.ctl[4] & 0x02u) != 0u, "sd4 run set");
    check((g_mock.ctl[4] & 0x00F00000u) == 0x00100000u, "sd4 tag 1");
    // SD0 (capture) untouched.
    check(g_mock.fmt[0] == 0u, "sd0 fmt untouched");
    check((g_mock.ctl[0] & 0x02u) == 0u, "sd0 run clear");
    check(g_mock.cbl[0] == 0u, "sd0 cbl untouched");
    // Playback through SD4 refills and tracks LPIB there.
    static int16_t beep[1102];
    for (uint32_t i = 0; i < 1102; ++i) beep[i] = 3000;
    check(backend.play(21, beep, 1102), "sd4 play");
    backend.pump();
    check(backend.descs_refilled() > 0u, "sd4 refills");
    g_mock.set_lpib(4, 8192);
    backend.pump();
    check(backend.last_lpib() == 8192u, "sd4 lpib tracked");
    backend.stop_all();
}

void test_bring_up_failures() {
    // No ops bound.
    {
        static Backend backend;
        OutputPath path = make_path();
        check(!backend.ensure_ready(&path), "no-ops not ready");
        check(backend.state() == kernel::app_audio::BackendState::Unavailable, "no-ops unavailable");
        check(std::strcmp(backend.name(), "none") == 0, "no-ops name none");
        // Degraded play still accepted (mixer-only null sink).
        static int16_t tiny[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        check(backend.play(7, tiny, 8), "degraded play accepted");
        check(backend.active_voices() == 1, "degraded voice live");
        backend.pump(); // advances without hardware
        backend.stop_owner(7);
        check(backend.active_voices() == 0, "degraded reclaim");
    }
    // Invalid path.
    {
        mock_reset_all();
        HdaOps ops = make_ops();
        static Backend backend;
        backend.bind_ops(&ops, g_mmioBase, 0);
        OutputPath bad = make_path();
        bad.valid = false;
        check(!backend.ensure_ready(&bad), "bad path not ready");
    }
    // Codec verb failure -> Unavailable, stream never started.
    {
        mock_reset_all();
        g_mock.failSetConvFmt = true;
        HdaOps ops = make_ops();
        static Backend backend;
        backend.bind_ops(&ops, g_mmioBase, 0);
        OutputPath path = make_path();
        check(!backend.ensure_ready(&path), "verb fail not ready");
        check(backend.state() == kernel::app_audio::BackendState::Unavailable, "verb fail unavailable");
        check((g_mock.ctl[0] & 0x02u) == 0u, "run never set");
        // Retryable (Unavailable, not Failed).
        g_mock.failSetConvFmt = false;
        // Note: mock virt_to_phys sequence continues; bring-up only needs verbs.
        check(backend.ensure_ready(&path), "retry after transient fail");
    }
    // Total verb failure.
    {
        mock_reset_all();
        g_mock.failAllVerbs = true;
        HdaOps ops = make_ops();
        static Backend backend;
        backend.bind_ops(&ops, g_mmioBase, 0);
        OutputPath path = make_path();
        check(!backend.ensure_ready(&path), "all-verb fail not ready");
    }
}

void test_fallback_default_route() {
    // Codec verbs all fail (QEMU hda-duplex behavior: zero responses) but
    // the stream engine moves: default-route start accepted after LPIB
    // verification, full codec path flagged off.
    mock_reset_all();
    g_mock.failAllVerbs = true;
    g_mock.autoAdvance = true;
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "fallback ready");
    check(backend.state() == kernel::app_audio::BackendState::Ready, "fallback state ready");
    check(!backend.codec_path_ok(), "fallback codec path off");
    check(std::strcmp(backend.name(), "hda-48000-stereo-s16") == 0, "fallback name");
    static int16_t beep[1102];
    for (uint32_t i = 0; i < 1102; ++i) beep[i] = 5000;
    check(backend.play(11, beep, 1102), "fallback play");
    for (uint32_t i = 0; i < 20; ++i) backend.pump();
    check(backend.descs_refilled() > 0u, "fallback refills");
    backend.stop_all();
}

void test_fallback_no_motion() {
    // Verbs fail AND the stream engine shows no DMA motion: honest
    // Unavailable (mixer-only degrade), never a fake Ready.
    mock_reset_all();
    g_mock.failAllVerbs = true;
    g_mock.autoAdvance = false;
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(!backend.ensure_ready(&path), "no-motion not ready");
    check(backend.state() == kernel::app_audio::BackendState::Unavailable, "no-motion unavailable");
    // Missing endpoints entirely also take the verified path.
    mock_reset_all();
    g_mock.autoAdvance = true;
    static Backend backend2;
    backend2.bind_ops(&ops, g_mmioBase, 0);
    OutputPath noEp = make_path();
    noEp.dacNode = 0;
    noEp.pinNode = 0;
    check(backend2.ensure_ready(&noEp), "no-endpoint default route ready");
    check(!backend2.codec_path_ok(), "no-endpoint codec path off");
}

void test_streaming_refill() {
    mock_reset_all();
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "stream ready");

    static int16_t beep[2205];
    for (uint32_t i = 0; i < 2205; ++i) beep[i] = (i & 1) ? 8000 : -8000;
    const uint64_t refillsBefore = backend.descs_refilled();
    check(backend.play(1, beep, 2205), "stream play 1");
    check(backend.play(1, beep, 2205), "stream play 2 overlap");
    check(backend.active_voices() == 2u, "two voices live");

    // Immediate pump resyncs to the live cursor (no full-ring wait).
    backend.pump();
    check(backend.descs_refilled() > refillsBefore, "refill after play");

    // Steady-state: advance LPIB in small steps, pump each time.
    for (uint32_t i = 0; i < 40; ++i) {
        g_mock.advance(1024);
        g_mock.nowMs += 5;
        backend.pump();
    }
    check(backend.underruns() == 0u, "no underruns at cadence");
    check(backend.state() == kernel::app_audio::BackendState::Ready, "still ready");
    // Voices (2205 mix frames ~2.4 device chunks each... short) drain.
    for (uint32_t i = 0; i < 200 && backend.active_voices() > 0; ++i) {
        g_mock.advance(2048);
        g_mock.nowMs += 10;
        backend.pump();
    }
    check(backend.active_voices() == 0u, "voices drained");
    const uint64_t refillsIdle = backend.descs_refilled();
    for (uint32_t i = 0; i < 10; ++i) {
        g_mock.advance(2048);
        g_mock.nowMs += 10;
        backend.pump();
    }
    // Idle keeps the ring valid with bounded silence refills, no underruns.
    check(backend.underruns() == 0u, "no idle underruns");
    check(backend.descs_refilled() >= refillsIdle, "idle refills bounded");
    check(backend.descs_refilled() <= refillsIdle + 16u, "idle refills small");
}

void test_wraparound() {
    mock_reset_all();
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "wrap ready");
    static int16_t tone[4410];
    for (uint32_t i = 0; i < 4410; ++i) tone[i] = 4000;
    check(backend.play(3, tone, 4410), "wrap play");
    // Park the cursor near the end of the ring, then wrap it.
    g_mock.set_lpib(0, 30000);
    backend.pump();
    g_mock.advance(4096); // wraps past 32768
    check(g_mock.lpib[0] < 30000u, "lpib wrapped");
    backend.pump();
    g_mock.advance(4096);
    backend.pump();
    check(backend.state() == kernel::app_audio::BackendState::Ready, "ready across wrap");
    check(backend.underruns() == 0u, "no underrun across orderly wrap");
}

void test_underrun_recovery() {
    mock_reset_all();
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "ur ready");
    static int16_t tone[8820];
    for (uint32_t i = 0; i < 8820; ++i) tone[i] = 2000;
    check(backend.play(5, tone, 8820), "ur play");
    backend.pump();
    // Starve the pump: jump the cursor most of a ring ahead at once
    // (a full-ring advance would alias to no motion mod CBL).
    g_mock.advance(20000);
    backend.pump();
    check(backend.underruns() >= 1u, "underrun counted");
    check(backend.state() == kernel::app_audio::BackendState::Ready, "ready after underrun");
    // Stream continues: further orderly pumps make progress without errors.
    for (uint32_t i = 0; i < 10; ++i) {
        g_mock.advance(1024);
        backend.pump();
    }
    check(backend.state() == kernel::app_audio::BackendState::Ready, "stable after resync");
}

void test_stream_error_recovery() {
    mock_reset_all();
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "err ready");
    static int16_t tone[2205];
    for (uint32_t i = 0; i < 2205; ++i) tone[i] = 1000;
    check(backend.play(6, tone, 2205), "err play");
    // Single FIFO error -> bounded recovery, stream restarts.
    g_mock.sts[0] = 0x08u;
    backend.pump();
    check(backend.recoveries() == 1u, "one recovery");
    check(backend.device_errors() == 1u, "one device error");
    check(backend.state() == kernel::app_audio::BackendState::Ready, "ready after recovery");
    check((g_mock.ctl[0] & 0x02u) != 0u, "run restored");
    // Exhaust recovery: repeated errors with a stream that can no longer
    // start -> Failed (fail-closed). A mock whose RUN bit always sticks
    // would recover forever, so the failure is scripted via failRun.
    g_mock.failRun = true;
    for (uint32_t i = 0; i < 5; ++i) {
        g_mock.sts[0] = 0x18u;
        backend.pump();
    }
    check(backend.state() == kernel::app_audio::BackendState::Failed, "failed after exhaustion");
    check(std::strcmp(backend.name(), "none") == 0, "failed name none");
    // Failed backend still drains the mixer (null-sink degrade).
    for (uint32_t i = 0; i < 100 && backend.active_voices() > 0; ++i) backend.pump();
    check(backend.active_voices() == 0u, "failed backend drains voices");
    backend.report_status();
    check(g_mock.log.find("FAILED") != std::string::npos, "failure logged");
}

void test_lifecycle_relaunch() {
    mock_reset_all();
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "cycle ready");
    static int16_t beep[1102];
    for (uint32_t i = 0; i < 1102; ++i) beep[i] = 6000;
    // Launch/exit cycles: play, pump, owner cleanup, relaunch.
    for (uint32_t cycle = 0; cycle < 4; ++cycle) {
        const uint64_t owner = 100 + cycle;
        check(backend.play(owner, beep, 1102), "cycle play");
        for (uint32_t i = 0; i < 8; ++i) {
            g_mock.advance(1024);
            backend.pump();
        }
        backend.stop_owner(owner); // app exit
        check(backend.active_voices_for(owner) == 0u, "cycle reclaim");
    }
    check(backend.state() == kernel::app_audio::BackendState::Ready, "ready after cycles");
    // One app exiting does not disturb another owner's voice.
    check(backend.play(201, beep, 1102), "owner A play");
    check(backend.play(202, beep, 1102), "owner B play");
    backend.stop_owner(201);
    check(backend.active_voices_for(202) == 1u, "owner B survives");
    backend.stop_all();
    // Denied/failed requests never reach the mixer: malformed conversions fail.
    static int16_t out[88200];
    uint32_t count = 0;
    static uint8_t bad[63];
    check(!kernel::app_audio::convert_to_mix_format(bad, 63, 22050, 16, out, 88200, &count),
          "malformed convert fails safely");
}

void test_self_test_mock() {
    mock_reset_all();
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "selftest ready");
    kernel::app_audio::Backend::SelfTestReport report;
    check(backend.self_test(false, &report), "self-test pass (no dma)");
    check(report.validation_ok && report.conversion_ok && report.overlap_ok, "selftest stages");
    check(report.drain_ok && report.reclaim_ok && report.exhaustion_ok, "selftest lifecycle");
    check(report.pass, "selftest pass flag");
    check(g_mock.log.find("self-test PASS") != std::string::npos, "selftest logged");
}

void test_self_test_dma() {
    mock_reset_all();
    g_mock.autoAdvance = true; // LPIB moves on every read while running
    HdaOps ops = make_ops();
    static Backend backend;
    backend.bind_ops(&ops, g_mmioBase, 0);
    OutputPath path = make_path();
    check(backend.ensure_ready(&path), "selftest-dma ready");
    kernel::app_audio::Backend::SelfTestReport report;
    check(backend.self_test(true, &report), "self-test pass (dma)");
    check(report.dma_progress, "dma progress observed");
}

} // namespace

int main() {
    test_abi_pins();
    test_validation();
    test_conversion();
    test_mixer();
    test_adapt();
    test_bring_up_success();
    test_output_sd_selection();
    test_bring_up_failures();
    test_fallback_default_route();
    test_fallback_no_motion();
    test_streaming_refill();
    test_wraparound();
    test_underrun_recovery();
    test_stream_error_recovery();
    test_lifecycle_relaunch();
    test_self_test_mock();
    test_self_test_dma();
    if (g_failures == 0) {
        std::printf("App audio stream test PASS\n");
        return 0;
    }
    std::printf("App audio stream test FAIL (%d)\n", g_failures);
    return 1;
}
