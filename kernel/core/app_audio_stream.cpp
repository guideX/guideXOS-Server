// Bare-metal App Model audio streaming backend (MC6) — implementation.
//
// See include/kernel/app_audio_stream.h for the architecture. Freestanding
// constraints: no STL, no exceptions, no threads, no <string.h> (tiny local
// loops instead), MSVC+GCC compatible (alignas, no GNU extensions).
//
// DMA/memory model (audited, see MC6 report):
// - All audio memory is static zero-initialized storage inside the kernel
//   image. The kernel links at 0x100000 but loads at an arbitrary physical
//   base, so device-visible addresses are translated with
//   base + (virt - 0x100000) through the HdaOps.virt_to_phys hook (same
//   formula as nic/virtio/mmio). program_ring() verifies every translated
//   address is nonzero and below 4 GiB, failing closed otherwise. Raw
//   virtual addresses MUST NOT be handed to the device (MC6 root-cause
//   finding: QEMU reads back zeros for untranslated addresses).
// - No stack buffer is ever handed to the device; no app-owned PCM is ever
//   used as DMA memory (apps are copied through the staging/mixer/ring
//   buffers). Nothing is freed while the device may reference it: the ring
//   lives for the lifetime of the boot.
// - x86 DMA is cache-coherent; no flush is required. VT-d/IOMMU is absent
//   on the supported QEMU `-machine pc` path; a remapping IOMMU would need
//   an explicit DMA-window pass (documented limitation, fail-closed check
//   cannot detect it, so it is reported, not asserted).

#include "include/kernel/app_audio_stream.h"
#include "include/kernel/pci_audio.h"

namespace kernel {
namespace app_audio {

namespace {

// ---- HDA stream-descriptor / codec constants (local aliases) ----
// Register layout comes from kernel::pci_audio; only the stream-descriptor
// programming and codec path verbs needed by the ring backend are used.

uint32_t sd_base(uint8_t streamIndex) {
    return pci_audio::HDA_SD_BASE +
           static_cast<uint32_t>(streamIndex) * pci_audio::HDA_SD_INTERVAL;
}

// SD control bits we use directly.
static const uint32_t kSdCtlRun = 0x02u;
static const uint32_t kSdCtlReset = 0x01u;
static const uint32_t kSdCtlTagShift = 20u;
static const uint32_t kSdCtlTagMask = 0x00F00000u;
// SD status bits: [2] Buffer Completion (informational, no IRQ wired),
// [3] FIFO Error, [4] Descriptor Error.
static const uint8_t kSdStsErrors = 0x18u;
static const uint8_t kSdStsClear = 0x1Cu;

// Converter format for the fixed device format: 48 kHz base, 16-bit,
// stereo (channels-1). Same encoding as the SD FMT field.
uint16_t device_conv_format() {
    return static_cast<uint16_t>(0x0000u | 0x0010u | (kDeviceChannels - 1u));
}

int16_t saturate32(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
}

void zero16(int16_t* dst, uint32_t count) {
    if (!dst) return;
    for (uint32_t i = 0; i < count; ++i) dst[i] = 0;
}

void copy16(int16_t* dst, const int16_t* src, uint32_t count) {
    if (!dst || !src) return;
    for (uint32_t i = 0; i < count; ++i) dst[i] = src[i];
}

void string_copy_bounded(char* dst, uint32_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    uint32_t i = 0;
    while (i + 1u < dstSize && src && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

} // namespace

// ================================================================
// Validation: same contract as hosted ValidatePlayRequest, including
// reason strings, so host/bare-metal behavior cannot drift.
// ================================================================

ValidationResult validate_play_request(const void* pcmData, uint32_t pcmBytes,
                                       uint32_t sampleRateHz, uint32_t channels,
                                       uint32_t bitsPerSample) {
    ValidationResult result;
    result.error = ErrorOk;
    result.reason = "ok";
    if (!pcmData) {
        result.error = ErrorInvalidArgument;
        result.reason = "null-pcm-data";
        return result;
    }
    if (pcmBytes == 0) {
        result.error = ErrorInvalidArgument;
        result.reason = "empty-pcm";
        return result;
    }
    if (pcmBytes > kMaxPcmBytes) {
        result.error = ErrorUnsupported;
        result.reason = "pcm-oversized";
        return result;
    }
    if (channels != kChannels) {
        result.error = ErrorInvalidArgument;
        result.reason = "channels-must-be-mono";
        return result;
    }
    if (bitsPerSample != 8 && bitsPerSample != 16) {
        result.error = ErrorInvalidArgument;
        result.reason = "bits-must-be-8-or-16";
        return result;
    }
    if (sampleRateHz < kMinRateHz || sampleRateHz > kMaxRateHz) {
        result.error = ErrorInvalidArgument;
        result.reason = "sample-rate-out-of-range";
        return result;
    }
    const uint32_t bytesPerFrame = bitsPerSample / 8u;
    if (pcmBytes % bytesPerFrame != 0u) {
        result.error = ErrorInvalidArgument;
        result.reason = "pcm-not-whole-frames";
        return result;
    }
    const uint64_t frameCount = pcmBytes / bytesPerFrame;
    if (frameCount == 0 || frameCount > 0xFFFFFFFFull) {
        result.error = ErrorInvalidArgument;
        result.reason = "frame-count-invalid";
        return result;
    }
    // Resampled worst case: lowest input rate expands the most.
    const uint64_t worstFrames =
        (frameCount * kMixRateHz + kMinRateHz - 1u) / kMinRateHz + 1u;
    if (worstFrames > 0xFFFFFFFFull) {
        result.error = ErrorUnsupported;
        result.reason = "voice-too-long";
        return result;
    }
    return result;
}

bool convert_to_mix_format(const void* pcmData, uint32_t pcmBytes,
                           uint32_t sampleRateHz, uint32_t bitsPerSample,
                           int16_t* outFrames, uint32_t outCapacity,
                           uint32_t* outFrameCount) {
    if (outFrameCount) *outFrameCount = 0;
    if (!pcmData || pcmBytes == 0 || !outFrames || outCapacity == 0 ||
        (bitsPerSample != 8 && bitsPerSample != 16) ||
        sampleRateHz < kMinRateHz || sampleRateHz > kMaxRateHz) {
        return false;
    }
    const uint32_t bytesPerFrame = bitsPerSample / 8u;
    if (pcmBytes % bytesPerFrame != 0u) return false;
    const uint32_t inFrames = pcmBytes / bytesPerFrame;
    if (inFrames == 0) return false;
    const uint64_t outFrames64 =
        (static_cast<uint64_t>(inFrames) * kMixRateHz + sampleRateHz - 1u) /
        sampleRateHz;
    if (outFrames64 == 0 || outFrames64 > kMaxVoiceFrames) return false;
    if (outFrames64 > outCapacity) return false;
    const uint32_t outFrames32 = static_cast<uint32_t>(outFrames64);
    const uint8_t* bytes = static_cast<const uint8_t*>(pcmData);
    for (uint32_t i = 0; i < outFrames32; ++i) {
        uint32_t src =
            static_cast<uint32_t>((static_cast<uint64_t>(i) * inFrames) / outFrames32);
        if (src >= inFrames) src = inFrames - 1u;
        int16_t sample = 0;
        if (bitsPerSample == 8) {
            sample = static_cast<int16_t>((static_cast<int32_t>(bytes[src]) - 128) * 256);
        } else {
            const uint32_t lo = bytes[src * 2u];
            const uint32_t hi = bytes[src * 2u + 1u];
            sample = static_cast<int16_t>(lo | (hi << 8u));
        }
        outFrames[i] = sample;
    }
    if (outFrameCount) *outFrameCount = outFrames32;
    return true;
}

// ================================================================
// Mixer-format -> device-format adaptation (deterministic).
// ================================================================

uint32_t mix_source_start(uint64_t deviceBase) {
    // src = (deviceBase * 22050) / 48000 computed overflow-free:
    // (q*48000 + r) * 22050 / 48000 = q*22050 + (r*22050)/48000,
    // with r < 48000 so r*22050 < 2^32 always.
    const uint64_t q = deviceBase / kDeviceRateHz;
    const uint64_t r = deviceBase % kDeviceRateHz;
    const uint64_t start =
        q * kMixRateHz + (r * kMixRateHz) / kDeviceRateHz;
    return static_cast<uint32_t>(start);
}

uint32_t mix_needed_for_device(uint32_t deviceFrames) {
    return mix_source_start(deviceFrames);
}

void adapt_chunk(const int16_t* mixMono, uint64_t deviceBase,
                 int16_t* stereoOut, uint32_t deviceFrames) {
    if (!mixMono || !stereoOut) return;
    const uint32_t baseSrc = mix_source_start(deviceBase);
    for (uint32_t i = 0; i < deviceFrames; ++i) {
        const uint32_t src = mix_source_start(deviceBase + i) - baseSrc;
        const int16_t sample = mixMono[src];
        stereoOut[i * 2u] = sample;
        stereoOut[i * 2u + 1u] = sample;
    }
}

// ================================================================
// Mixer (MC5 semantics, static voices).
// ================================================================

Mixer::Mixer() {
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].active = false;
        voices_[i].owner = 0;
        voices_[i].position = 0;
        voices_[i].length = 0;
    }
}

bool Mixer::play(uint64_t owner, const int16_t* frames, uint32_t frameCount) {
    if (!frames || frameCount == 0 || frameCount > kMaxVoiceFrames) return false;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active) continue;
        voices_[i].active = true;
        voices_[i].owner = owner;
        voices_[i].position = 0;
        voices_[i].length = frameCount;
        copy16(voices_[i].samples, frames, frameCount);
        return true;
    }
    return false;
}

void Mixer::mix(int16_t* outFrames, uint32_t frameCount) {
    if (!outFrames) return;
    for (uint32_t i = 0; i < frameCount; ++i) {
        int32_t mixed = 0;
        for (uint32_t v = 0; v < kMaxVoices; ++v) {
            Voice& voice = voices_[v];
            if (!voice.active) continue;
            if (voice.position < voice.length) {
                mixed += voice.samples[voice.position++];
            }
            if (voice.position >= voice.length) {
                voice.active = false;
                voice.owner = 0;
                voice.position = 0;
                voice.length = 0;
            }
        }
        outFrames[i] = saturate32(mixed);
    }
}

uint32_t Mixer::active_voices() const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active) ++count;
    }
    return count;
}

uint32_t Mixer::active_voices_for(uint64_t owner) const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active && voices_[i].owner == owner) ++count;
    }
    return count;
}

void Mixer::stop_owner(uint64_t owner) {
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].active && voices_[i].owner == owner) {
            voices_[i].active = false;
            voices_[i].owner = 0;
            voices_[i].position = 0;
            voices_[i].length = 0;
        }
    }
}

void Mixer::stop_all() {
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].active = false;
        voices_[i].owner = 0;
        voices_[i].position = 0;
        voices_[i].length = 0;
    }
}

// ================================================================
// Backend.
// ================================================================

static const char kNameHda[] = "hda-48000-stereo-s16";
static const char kNameNone[] = "none";

Backend::Backend()
    : ops_bound_(false),
      codec_path_ok_(false),
      mmio_base_(0),
      ctrl_index_(0),
      state_(BackendState::Unavailable),
      stream_index_(0),
      stream_tag_(1),
      device_frame_(0),
      last_lpib_(0),
      lpib_valid_(false),
      consecutive_errors_(0),
      plays_accepted_(0),
      plays_busy_(0),
      chunks_mixed_(0),
      descs_refilled_(0),
      underruns_(0),
      recoveries_(0),
      device_errors_(0),
      play_log_count_(0) {
    string_copy_bounded(name_, sizeof(name_), kNameNone);
    for (uint32_t i = 0; i < kRingDescriptors; ++i) {
        bdl_[i].address = 0;
        bdl_[i].length = 0;
        bdl_[i].flags = 0;
    }
}

bool Backend::ops_bound() const {
    return ops_bound_ && ops_.read8 && ops_.read16 && ops_.read32 &&
           ops_.write8 && ops_.write16 && ops_.write32 && ops_.send_verb &&
           ops_.virt_to_phys && ops_.ticks_ms && ops_.log_puts &&
           ops_.log_hex32 && ops_.log_hex64;
}

void Backend::bind_ops(const HdaOps* ops, uint64_t mmioBase, uint8_t ctrlIndex) {
    if (!ops) {
        ops_bound_ = false;
        return;
    }
    ops_ = *ops;
    ops_bound_ = true;
    mmio_base_ = mmioBase;
    ctrl_index_ = ctrlIndex;
    if (!ops_bound()) ops_bound_ = false;
}

void Backend::log_state(const char* what) const {
    if (!ops_bound()) return;
    ops_.log_puts("[APP-AUDIO] ");
    ops_.log_puts(what);
    ops_.log_puts(" state=");
    ops_.log_hex32(static_cast<uint32_t>(state_));
    ops_.log_puts(" backend=");
    ops_.log_puts(name_);
    ops_.log_puts("\n");
}

void Backend::log_hex_labeled(const char* label, uint32_t value) const {
    if (!ops_bound()) return;
    ops_.log_puts("[APP-AUDIO] ");
    ops_.log_puts(label);
    ops_.log_puts("=0x");
    ops_.log_hex32(value);
    ops_.log_puts("\n");
}

uint32_t Backend::verb(uint8_t codecAddr, uint8_t nodeId, uint32_t verb,
                       uint32_t* response) {
    uint32_t value = 0;
    if (!ops_bound()) return 0;
    if (!ops_.send_verb(ctrl_index_, codecAddr, nodeId, verb, &value)) return 0;
    if (response) *response = value;
    return value;
}

bool Backend::ensure_ready(const OutputPath* path) {
    if (state_ == BackendState::Ready) return true;
    if (state_ == BackendState::Failed) return false; // sticky, fail-closed
    if (!ops_bound() || !path || !path->valid) {
        state_ = BackendState::Unavailable;
        string_copy_bounded(name_, sizeof(name_), kNameNone);
        return false;
    }
    if (mmio_base_ == 0) {
        state_ = BackendState::Unavailable;
        string_copy_bounded(name_, sizeof(name_), kNameNone);
        if (ops_bound()) ops_.log_puts("[APP-AUDIO] no MMIO base; degraded to mixer-only\n");
        return false;
    }
    // Full codec path only when enumeration found usable endpoints;
    // otherwise (or when verbs demonstrably fail) attempt a default-route
    // stream start verified by LPIB motion (see below).
    codec_path_ok_ = (path->dacNode != 0 && path->pinNode != 0);
    if (codec_path_ok_ && !bring_up_path(path)) {
        codec_path_ok_ = false;
        ops_.log_puts("[APP-AUDIO] codec verbs failed; attempting default-route stream\n");
    }
    if (!program_ring()) {
        state_ = BackendState::Unavailable;
        string_copy_bounded(name_, sizeof(name_), kNameNone);
        return false;
    }
    if (!start_stream()) {
        state_ = BackendState::Unavailable;
        string_copy_bounded(name_, sizeof(name_), kNameNone);
        return false;
    }
    if (!codec_path_ok_ && !verify_streaming()) {
        stop_stream();
        state_ = BackendState::Unavailable;
        string_copy_bounded(name_, sizeof(name_), kNameNone);
        ops_.log_puts("[APP-AUDIO] default-route stream shows no DMA motion; degraded\n");
        return false;
    }
    state_ = BackendState::Ready;
    string_copy_bounded(name_, sizeof(name_), kNameHda);
    // The ring is prefilled with silence; the rendered window covers the
    // whole ring so the idle pump treats it as flushed (see pump()).
    device_frame_ = kRingDescriptors * kChunkDeviceFrames;
    lpib_valid_ = false;
    consecutive_errors_ = 0;
    if (codec_path_ok_) {
        log_state("backend ready");
    } else {
        log_state("backend ready (default route, LPIB-verified)");
    }
    return true;
}

bool Backend::verify_streaming() {
    // Accept a default-route stream only on observed DMA motion: poll LPIB
    // for change with BOTH a time deadline and an iteration cap (the cap
    // covers wedged clocks, e.g. frozen mock time in unit tests).
    const uint32_t first = read_lpib();
    const uint64_t deadline = ops_.ticks_ms() + 200u;
    for (uint32_t i = 0; i < 100000u; ++i) {
        if (read_lpib() != first) return true;
        if (ops_.ticks_ms() >= deadline) break;
    }
    return read_lpib() != first;
}

bool Backend::bring_up_path(const OutputPath* path) {
    // Codec output-path bring-up, best-effort with explicit logging.
    // Order: AFG power -> DAC format/stream/power/volume -> pin routing /
    // enable / EAPD / power / volume. Any hard failure aborts (the stream
    // registers are untouched), soft failures (routing discovery) proceed
    // with firmware defaults and a log line.
    uint32_t verbsUsed = 0;
    uint32_t response = 0;

    ops_.log_puts("[APP-AUDIO] codec bring-up start\n");

    // 0. Audio function group to D0 first (when the enumerator found it);
    // children inherit no power state, so DAC/pin are powered explicitly
    // below regardless.
    if (path->afgNode != 0) {
        if (++verbsUsed > kMaxBringUpVerbs) return false;
        if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->afgNode,
                            pci_audio::HDA_VERB_SET_POWER_STATE | 0x00u, nullptr)) {
            ops_.log_puts("[APP-AUDIO] AFG power D0 failed (continuing)\n");
        }
    }

    // 0b. Capability note: log what the DAC claims (informational only; the
    // fixed 48 kHz stereo device format is HDA-mandatory so we proceed).
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    ops_.send_verb(ctrl_index_, path->codecAddr, path->dacNode,
                   pci_audio::HDA_VERB_GET_PARAM | pci_audio::HDA_PARAM_PCM_SIZES_RATES,
                   &response);
    log_hex_labeled("dac pcm sizes/rates", response);

    // 1. DAC converter format + stream/channel + power.
    const uint16_t fmt = device_conv_format();
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->dacNode,
                        pci_audio::HDA_VERB_SET_CONV_FMT | fmt, nullptr)) {
        ops_.log_puts("[APP-AUDIO] DAC SET_CONV_FMT failed\n");
        return false;
    }
    // SET_CONV_STREAM/CHANNEL is verb 0x706 with payload stream[7:4] |
    // channel[3:0] (the header keeps both historical names for 0x70600).
    const uint32_t streamChan =
        (static_cast<uint32_t>(stream_tag_) << 4u) | 0u;
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->dacNode,
                        pci_audio::HDA_VERB_SET_CONV_STREAM | streamChan, nullptr)) {
        ops_.log_puts("[APP-AUDIO] DAC SET_CONV_STREAM failed\n");
        return false;
    }
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->dacNode,
                        pci_audio::HDA_VERB_SET_POWER_STATE | 0x00u, nullptr)) {
        ops_.log_puts("[APP-AUDIO] DAC power D0 failed\n");
        return false;
    }
    // Unmute + full gain, output amp left+right. Same encoding as the
    // hosted set_master_volume()/set_mute() path (0xB000 selector).
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->dacNode,
                        pci_audio::HDA_VERB_SET_AMP_GAIN | 0xB07Fu, nullptr)) {
        ops_.log_puts("[APP-AUDIO] DAC unmute/volume failed\n");
        return false;
    }

    // 2. Pin routing: best-effort connection select toward our DAC.
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    uint32_t connLen = 0;
    if (ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                       pci_audio::HDA_VERB_GET_PARAM | pci_audio::HDA_PARAM_CONN_LIST_LEN,
                       &connLen)) {
        const uint32_t entries = connLen & 0x7Fu;
        log_hex_labeled("pin conn list len", entries);
        if (entries >= 1u && entries <= 32u) {
            if (++verbsUsed > kMaxBringUpVerbs) return false;
            uint32_t list = 0;
            if (ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                               pci_audio::HDA_VERB_GET_CONN_LIST, &list)) {
                // Short-form entries: one byte each, up to 4 per verb.
                // Find our DAC; select it when it is not already selected.
                uint32_t select = 0xFFFFFFFFu;
                for (uint32_t e = 0; e < entries && e < 4u; ++e) {
                    const uint32_t node =
                        (list >> (e * 8u)) & 0xFFu;
                    if ((node & 0x7Fu) == path->dacNode) {
                        select = e;
                        break;
                    }
                }
                if (select != 0xFFFFFFFFu && select != 0u) {
                    if (++verbsUsed > kMaxBringUpVerbs) return false;
                    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                                        pci_audio::HDA_VERB_SET_CONN_SELECT | select,
                                        nullptr)) {
                        ops_.log_puts("[APP-AUDIO] pin SET_CONN_SELECT failed; keeping default\n");
                    }
                }
            }
        }
    } else {
        ops_.log_puts("[APP-AUDIO] pin conn list unreadable; keeping firmware routing\n");
    }

    // 3. Pin enable + EAPD + power + unmute.
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                        pci_audio::HDA_VERB_SET_PIN_CTRL | 0xC0u, nullptr)) {
        ops_.log_puts("[APP-AUDIO] pin enable failed\n");
        return false;
    }
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                        pci_audio::HDA_VERB_SET_EAPD_EN | 0x02u, nullptr)) {
        ops_.log_puts("[APP-AUDIO] pin EAPD enable failed (continuing)\n");
    }
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                        pci_audio::HDA_VERB_SET_POWER_STATE | 0x00u, nullptr)) {
        ops_.log_puts("[APP-AUDIO] pin power D0 failed\n");
        return false;
    }
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    if (!ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                        pci_audio::HDA_VERB_SET_AMP_GAIN | 0xB07Fu, nullptr)) {
        ops_.log_puts("[APP-AUDIO] pin unmute/volume failed (continuing)\n");
    }

    // 4. Read back pin control as device-level proof of routing.
    if (++verbsUsed > kMaxBringUpVerbs) return false;
    response = 0;
    ops_.send_verb(ctrl_index_, path->codecAddr, path->pinNode,
                   pci_audio::HDA_VERB_GET_PIN_CTRL, &response);
    log_hex_labeled("pin ctrl readback", response);

    ops_.log_puts("[APP-AUDIO] codec bring-up complete\n");
    return true;
}

bool Backend::program_ring() {
    // Verify DMA-visible addresses before telling the device about them.
    const uint64_t bdlPhys = ops_.virt_to_phys(&bdl_[0]);
    if (bdlPhys == 0 || (bdlPhys & 0x7Fu) != 0u) {
        ops_.log_puts("[APP-AUDIO] BDL alignment check failed\n");
        return false;
    }
    if (bdlPhys >= 0x100000000ull) {
        ops_.log_puts("[APP-AUDIO] BDL above 4GiB; failing closed\n");
        return false;
    }
    for (uint32_t i = 0; i < kRingDescriptors; ++i) {
        const uint64_t bufPhys = ops_.virt_to_phys(&pcm_[i][0]);
        if (bufPhys == 0 || bufPhys >= 0x100000000ull) {
            ops_.log_puts("[APP-AUDIO] PCM buffer address check failed\n");
            return false;
        }
        if ((kDescBytes & 0x7Fu) != 0u) {
            ops_.log_puts("[APP-AUDIO] descriptor size misaligned\n");
            return false;
        }
        bdl_[i].address = bufPhys;
        bdl_[i].length = kDescBytes;
        bdl_[i].flags = 0x01u; // IOC
        fill_silence(static_cast<uint8_t>(i));
    }

    // GCAP tells us the output-stream base: descriptors are ordered
    // input-first, so output 0 lives at index ISS (bits 11:8). SD0 is only
    // the playback descriptor on controllers with zero input streams.
    const uint16_t gcap = ops_.read16(mmio_base_, pci_audio::HDA_GCAP);
    const uint8_t inputCount = static_cast<uint8_t>((gcap >> 8) & 0x0Fu);
    if (inputCount >= 30u) {
        ops_.log_puts("[APP-AUDIO] GCAP input count out of range\n");
        return false;
    }
    stream_index_ = inputCount;
    const uint32_t base = sd_base(stream_index_);
    ops_.log_puts("[APP-AUDIO] output SD index=0x");
    ops_.log_hex32(stream_index_);
    ops_.log_puts(" gcap=0x");
    ops_.log_hex32(gcap);
    ops_.log_puts("\n");

    // Stop + reset the stream descriptor.
    uint32_t ctl = ops_.read32(mmio_base_, base + pci_audio::HDA_SD_CTL);
    ctl &= ~kSdCtlRun;
    ops_.write32(mmio_base_, base + pci_audio::HDA_SD_CTL, ctl);
    ops_.write8(mmio_base_, base + pci_audio::HDA_SD_CTL,
                static_cast<uint8_t>(kSdCtlReset));
    // Bounded reset wait (~50 ms via the abstracted clock).
    if (ops_bound()) {
        const uint64_t deadline = ops_.ticks_ms() + 50u;
        for (;;) {
            const uint32_t c = ops_.read32(mmio_base_, base + pci_audio::HDA_SD_CTL);
            if ((c & kSdCtlReset) == 0u) break;
            if (ops_.ticks_ms() >= deadline) break;
        }
    }
    ops_.write8(mmio_base_, base + pci_audio::HDA_SD_CTL, 0u);

    // Format + tag.
    ops_.write16(mmio_base_, base + pci_audio::HDA_SD_FMT, device_conv_format());
    ctl = ops_.read32(mmio_base_, base + pci_audio::HDA_SD_CTL);
    ctl = (ctl & ~kSdCtlTagMask) |
          ((static_cast<uint32_t>(stream_tag_) << kSdCtlTagShift) & kSdCtlTagMask);
    ops_.write32(mmio_base_, base + pci_audio::HDA_SD_CTL, ctl);

    // Ring programming: BDL pointer, last valid index (cyclic), byte count.
    ops_.write32(mmio_base_, base + pci_audio::HDA_SD_BDPL,
                 static_cast<uint32_t>(bdlPhys & 0xFFFFFFFFull));
    ops_.write32(mmio_base_, base + pci_audio::HDA_SD_BDPU,
                 static_cast<uint32_t>(bdlPhys >> 32));
    ops_.write32(mmio_base_, base + pci_audio::HDA_SD_CBL, kRingBytes);
    ops_.write16(mmio_base_, base + pci_audio::HDA_SD_LVI,
                 static_cast<uint16_t>(kRingDescriptors - 1u));
    clear_status(kSdStsClear);

    ops_.log_puts("[APP-AUDIO] ring programmed descs=0x");
    ops_.log_hex32(kRingDescriptors);
    ops_.log_puts(" bytes=0x");
    ops_.log_hex32(kRingBytes);
    ops_.log_puts(" bdl=0x");
    ops_.log_hex64(bdlPhys);
    ops_.log_puts("\n");
    return true;
}

bool Backend::start_stream() {
    const uint32_t base = sd_base(stream_index_);
    uint32_t ctl = ops_.read32(mmio_base_, base + pci_audio::HDA_SD_CTL);
    ctl |= kSdCtlRun;
    ops_.write32(mmio_base_, base + pci_audio::HDA_SD_CTL, ctl);
    const uint32_t check = ops_.read32(mmio_base_, base + pci_audio::HDA_SD_CTL);
    if ((check & kSdCtlRun) == 0u) {
        ops_.log_puts("[APP-AUDIO] stream RUN bit did not stick\n");
        return false;
    }
    ops_.log_puts("[APP-AUDIO] stream running\n");
    return true;
}

bool Backend::stop_stream() {
    const uint32_t base = sd_base(stream_index_);
    const uint32_t ctl = ops_.read32(mmio_base_, base + pci_audio::HDA_SD_CTL);
    ops_.write32(mmio_base_, base + pci_audio::HDA_SD_CTL, ctl & ~kSdCtlRun);
    return true;
}

uint32_t Backend::read_lpib() {
    const uint32_t base = sd_base(stream_index_);
    return ops_.read32(mmio_base_, base + pci_audio::HDA_SD_LPIB);
}

uint32_t Backend::read_status() {
    const uint32_t base = sd_base(stream_index_);
    return ops_.read8(mmio_base_, base + pci_audio::HDA_SD_STS);
}

void Backend::clear_status(uint32_t bits) {
    const uint32_t base = sd_base(stream_index_);
    ops_.write8(mmio_base_, base + pci_audio::HDA_SD_STS,
                static_cast<uint8_t>(bits & 0xFFu));
}

bool Backend::play(uint64_t owner, const int16_t* frames, uint32_t frameCount) {
    if (!mixer_.play(owner, frames, frameCount)) {
        ++plays_busy_;
        return false;
    }
    ++plays_accepted_;
    if (play_log_count_ < 8u && ops_bound()) {
        ++play_log_count_;
        ops_.log_puts("[APP-AUDIO] play queued owner=0x");
        ops_.log_hex64(owner);
        ops_.log_puts(" frames=0x");
        ops_.log_hex32(frameCount);
        ops_.log_puts("\n");
    }
    return true;
}

void Backend::fill_silence(uint8_t index) {
    if (index >= kRingDescriptors) return;
    zero16(&pcm_[index][0], kChunkDeviceFrames * kDeviceChannels);
}

void Backend::refill_descriptor(uint8_t index) {
    // Render the next device chunk (device_frame_) into descriptor index.
    // The caller guarantees index == (device_frame_/kChunkDeviceFrames)%8
    // in steady state; on resync the caller sets device_frame_ first.
    if (index >= kRingDescriptors) return;
    static int16_t mixTmp[kChunkMixFramesMax];
    const uint64_t base = device_frame_;
    const uint32_t srcStart = mix_source_start(base);
    const uint32_t srcEnd = mix_source_start(base + kChunkDeviceFrames);
    uint32_t need = (srcEnd > srcStart) ? (srcEnd - srcStart) : 0u;
    if (need > kChunkMixFramesMax) need = kChunkMixFramesMax;
    if (mixer_.active_voices() > 0 && need > 0) {
        mixer_.mix(mixTmp, need);
        ++chunks_mixed_;
    } else {
        zero16(mixTmp, need);
    }
    adapt_chunk(mixTmp, base, &pcm_[index][0], kChunkDeviceFrames);
    device_frame_ += kChunkDeviceFrames;
    ++descs_refilled_;
}

void Backend::refill_due(bool force_ahead) {
    // Render chunks until the rendered window leads the hardware cursor by
    // at least 3 chunks (steady state) or force_ahead refills the whole
    // ring window from the cursor (start/resync path).
    const uint32_t lpib = read_lpib();
    const bool cursorMoved = !lpib_valid_ || (lpib != last_lpib_);
    last_lpib_ = lpib;
    lpib_valid_ = true;
    const uint64_t cursorChunk =
        (static_cast<uint64_t>(lpib % kRingBytes)) / kDescBytes;
    uint64_t renderedChunk = device_frame_ / kChunkDeviceFrames;

    if (force_ahead) {
        // Resync: render from cursor+1 (the descriptor after the one
        // currently playing) three chunks ahead.
        device_frame_ = (cursorChunk + 1u) * kChunkDeviceFrames;
        renderedChunk = cursorChunk + 1u;
    }

    const bool active = mixer_.active_voices() > 0;
    if (!active && !force_ahead) {
        // Idle and previously flushed: track the cursor silently so a
        // later resync starts from the right window. No underrun counted.
        if (renderedChunk >= cursorChunk + kRingDescriptors) {
            device_frame_ = (cursorChunk + kRingDescriptors) * kChunkDeviceFrames;
        }
        return;
    }

    // Full window with pending voices: on a live cursor this is the steady
    // state (nothing due). On a frozen cursor the device is stalled (or
    // pumps outrun wall-clock, as in tight self-test loops): keeping the
    // slot/chunk mapping exact forbids re-rendering, so voice lifetimes
    // stay real-time by mixing one bounded quantum to discard — the same
    // degrade philosophy as the Failed/Unavailable paths. Counted, so a
    // wedged device cannot silently truncate audio.
    if (!force_ahead && active && renderedChunk >= cursorChunk + 3u) {
        if (cursorMoved) return;
        static int16_t stallDiscard[kChunkMixFramesMax];
        mixer_.mix(stallDiscard, mix_needed_for_device(kChunkDeviceFrames));
        ++chunks_mixed_;
        ++underruns_;
        if (ops_bound()) {
            ops_.log_puts("[APP-AUDIO] stall: cursor frozen, voice time advanced\n");
        }
        return;
    }

    // Underrun: the cursor reached (or passed) the rendered window, so the
    // device replayed stale descriptors. Count once per pump, then resync.
    if (!force_ahead && cursorChunk >= renderedChunk) {
        ++underruns_;
        if (ops_bound()) {
            ops_.log_puts("[APP-AUDIO] underrun cursor=0x");
            ops_.log_hex32(static_cast<uint32_t>(cursorChunk));
            ops_.log_puts(" rendered=0x");
            ops_.log_hex32(static_cast<uint32_t>(renderedChunk));
            ops_.log_puts("\n");
        }
        device_frame_ = (cursorChunk + 1u) * kChunkDeviceFrames;
        renderedChunk = cursorChunk + 1u;
    }

    uint32_t refills = 0;
    while (renderedChunk < cursorChunk + 3u && refills < kRingDescriptors) {
        const uint8_t index =
            static_cast<uint8_t>(renderedChunk % kRingDescriptors);
        refill_descriptor(index);
        ++refills;
        renderedChunk = device_frame_ / kChunkDeviceFrames;
        if (!active && mixer_.active_voices() == 0 && refills >= 1u && !force_ahead) {
            // Voices drained mid-refill: one trailing silence chunk keeps
            // the tail bounded; the idle path takes over next pump.
            break;
        }
    }

    // Idle flush: keep rendering silence until the whole ring is silent so
    // the frozen window assumption holds (see pump()).
    if (!mixer_.active_voices() && !force_ahead) {
        while (renderedChunk < cursorChunk + kRingDescriptors &&
               refills < kRingDescriptors) {
            const uint8_t index =
                static_cast<uint8_t>(renderedChunk % kRingDescriptors);
            refill_descriptor(index);
            ++refills;
            renderedChunk = device_frame_ / kChunkDeviceFrames;
        }
    }
}

void Backend::handle_stream_error(uint32_t status) {
    ++device_errors_;
    ++consecutive_errors_;
    log_hex_labeled("stream error sts", status & 0xFFu);
    if (consecutive_errors_ > kMaxRecoveryAttempts) {
        state_ = BackendState::Failed;
        string_copy_bounded(name_, sizeof(name_), kNameNone);
        stop_stream();
        ops_.log_puts("[APP-AUDIO] recovery exhausted; backend FAILED (mixer-only degrade)\n");
        return;
    }
    ++recoveries_;
    ops_.log_puts("[APP-AUDIO] recovering stream\n");
    stop_stream();
    program_ring();
    clear_status(kSdStsClear);
    if (!start_stream()) {
        ++consecutive_errors_;
        if (consecutive_errors_ > kMaxRecoveryAttempts) {
            state_ = BackendState::Failed;
            string_copy_bounded(name_, sizeof(name_), kNameNone);
            ops_.log_puts("[APP-AUDIO] restart failed; backend FAILED\n");
        }
        return;
    }
    consecutive_errors_ = 0;
    device_frame_ = kChunkDeviceFrames; // cursor ~0 after reset; lead by one
    lpib_valid_ = false;
    refill_due(true);
}

void Backend::pump() {
    if (state_ == BackendState::Failed) {
        // Degraded: keep mixer lifetimes real-time so voices still
        // complete and reclaim (hosted null-sink parity).
        if (mixer_.active_voices() > 0) {
            static int16_t discard[kChunkMixFramesMax];
            mixer_.mix(discard, mix_needed_for_device(kChunkDeviceFrames));
            ++chunks_mixed_;
        }
        return;
    }
    if (state_ != BackendState::Ready || !ops_bound()) {
        // Unavailable: same null-sink degrade (no hardware traffic).
        if (mixer_.active_voices() > 0) {
            static int16_t discard[kChunkMixFramesMax];
            mixer_.mix(discard, mix_needed_for_device(kChunkDeviceFrames));
            ++chunks_mixed_;
        }
        return;
    }

    const uint32_t status = read_status();
    if ((status & kSdStsErrors) != 0u) {
        clear_status(kSdStsClear);
        handle_stream_error(status);
        if (state_ != BackendState::Ready) return;
        return;
    }
    consecutive_errors_ = 0;

    const bool wasActive = mixer_.active_voices() > 0;
    const uint32_t lpib = read_lpib();
    const uint64_t cursorChunk =
        (static_cast<uint64_t>(lpib % kRingBytes)) / kDescBytes;
    const uint64_t renderedChunk = device_frame_ / kChunkDeviceFrames;

    // Idle->active transition (or stale window after a long idle): resync
    // the render window to the live cursor so sound starts within ~2
    // chunks instead of up to a full ring later.
    if (wasActive && renderedChunk > cursorChunk + 3u) {
        refill_due(true);
        return;
    }
    refill_due(false);
}

void Backend::stop_owner(uint64_t owner) {
    mixer_.stop_owner(owner);
}

void Backend::stop_all() {
    mixer_.stop_all();
}

void Backend::reset_stream_silence() {
    if (state_ != BackendState::Ready || !ops_bound()) return;
    mixer_.stop_all();
    stop_stream();
    program_ring();
    clear_status(kSdStsClear);
    if (!start_stream()) {
        ++device_errors_;
        return;
    }
    device_frame_ = kRingDescriptors * kChunkDeviceFrames;
    lpib_valid_ = false;
    consecutive_errors_ = 0;
    ops_.log_puts("[APP-AUDIO] stream reset to silence\n");
}

BackendState Backend::state() const { return state_; }

const char* Backend::name() const { return name_; }

bool Backend::codec_path_ok() const { return codec_path_ok_; }

uint64_t Backend::plays_accepted() const { return plays_accepted_; }
uint64_t Backend::plays_busy() const { return plays_busy_; }
uint64_t Backend::chunks_mixed() const { return chunks_mixed_; }
uint64_t Backend::descs_refilled() const { return descs_refilled_; }
uint64_t Backend::underruns() const { return underruns_; }
uint64_t Backend::recoveries() const { return recoveries_; }
uint64_t Backend::device_errors() const { return device_errors_; }

uint32_t Backend::active_voices() const { return mixer_.active_voices(); }

uint32_t Backend::active_voices_for(uint64_t owner) const {
    return mixer_.active_voices_for(owner);
}

uint32_t Backend::last_lpib() const { return last_lpib_; }

void Backend::report_status() const {
    if (!ops_bound()) return;
    ops_.log_puts("[APP-AUDIO] status backend=");
    ops_.log_puts(name_);
    ops_.log_puts(" state=0x");
    ops_.log_hex32(static_cast<uint32_t>(state_));
    ops_.log_puts(" codecPath=0x");
    ops_.log_hex32(codec_path_ok_ ? 1u : 0u);
    ops_.log_puts(" plays=0x");
    ops_.log_hex64(plays_accepted_);
    ops_.log_puts(" busy=0x");
    ops_.log_hex64(plays_busy_);
    ops_.log_puts(" chunks=0x");
    ops_.log_hex64(chunks_mixed_);
    ops_.log_puts(" refills=0x");
    ops_.log_hex64(descs_refilled_);
    ops_.log_puts(" underruns=0x");
    ops_.log_hex64(underruns_);
    ops_.log_puts(" recoveries=0x");
    ops_.log_hex64(recoveries_);
    ops_.log_puts(" devErr=0x");
    ops_.log_hex64(device_errors_);
    ops_.log_puts(" active=0x");
    ops_.log_hex32(mixer_.active_voices());
    ops_.log_puts(" lpib=0x");
    ops_.log_hex32(last_lpib_);
    ops_.log_puts("\n");
}

bool Backend::self_test(bool expect_dma_progress, SelfTestReport* report) {
    SelfTestReport local;
    local.validation_ok = false;
    local.conversion_ok = false;
    local.overlap_ok = false;
    local.drain_ok = false;
    local.reclaim_ok = false;
    local.exhaustion_ok = false;
    local.dma_progress = false;
    local.pass = false;

    stop_all();

    // 1. Validation matrix (mirrors the MC5 hosted pins).
    {
        static uint8_t pcm[64];
        for (uint32_t i = 0; i < sizeof(pcm); ++i) pcm[i] = 0x80;
        bool ok = true;
        ok = ok && (validate_play_request(pcm, 64, 22050, 1, 16).error == ErrorOk);
        ok = ok && (validate_play_request(pcm, 32, 8000, 1, 8).error == ErrorOk);
        ok = ok && (validate_play_request(nullptr, 64, 22050, 1, 16).error == ErrorInvalidArgument);
        ok = ok && (validate_play_request(pcm, 0, 22050, 1, 16).error == ErrorInvalidArgument);
        ok = ok && (validate_play_request(pcm, kMaxPcmBytes + 2u, 22050, 1, 16).error == ErrorUnsupported);
        ok = ok && (validate_play_request(pcm, 64, 22050, 2, 16).error == ErrorInvalidArgument);
        ok = ok && (validate_play_request(pcm, 64, 48001, 1, 16).error == ErrorInvalidArgument);
        ok = ok && (validate_play_request(pcm, 63, 22050, 1, 16).error == ErrorInvalidArgument);
        local.validation_ok = ok;
    }

    // 2. Conversion pins (8-bit widen + 16-bit passthrough + resample).
    {
        static int16_t out[kMaxVoiceFrames];
        uint32_t count = 0;
        bool ok = true;
        const uint8_t src8[4] = {0x00, 0x80, 0xFF, 0x80};
        ok = ok && convert_to_mix_format(src8, 4, 22050, 8, out, kMaxVoiceFrames, &count);
        ok = ok && (count == 4 && out[0] == -32768 && out[1] == 0 && out[2] == 32512);
        const uint8_t src16[4] = {0x00, 0x80, 0xFF, 0x7F};
        ok = ok && convert_to_mix_format(src16, 4, 22050, 16, out, kMaxVoiceFrames, &count);
        ok = ok && (count == 2 && out[0] == -32768 && out[1] == 32767);
        local.conversion_ok = ok;
    }

    // 3. Overlap: two synthesized voices through play()+pump().
    {
        static int16_t beep[2205]; // 0.1 s @ 22050 Hz
        for (uint32_t i = 0; i < 2205; ++i) {
            const int32_t phase = static_cast<int32_t>((i * 440u) / 22050u);
            beep[i] = (phase & 1) ? 12000 : -12000;
        }
        const uint64_t refillsBefore = descs_refilled_;
        const uint64_t chunksBefore = chunks_mixed_;
        const bool a = play(0xBE0Bu, beep, 2205);
        const bool b = play(0xBE0Bu, beep, 2205);
        bool ok = a && b && (active_voices() == 2u);
        for (uint32_t i = 0; i < 64u && active_voices() > 0; ++i) pump();
        ok = ok && (descs_refilled_ > refillsBefore) && (chunks_mixed_ > chunksBefore);
        local.overlap_ok = ok;
    }

    // 4. Drain: pumps advance voices to completion, then silence.
    {
        for (uint32_t i = 0; i < 512u && active_voices() > 0; ++i) pump();
        local.drain_ok = (active_voices() == 0u);
    }

    // 5. Reclaim: owner cleanup frees voices, mixer reusable.
    {
        static int16_t tiny[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        bool ok = true;
        for (uint32_t i = 0; i < 4u; ++i) ok = ok && play(0x01u, tiny, 8);
        ok = ok && (active_voices_for(0x01u) == 4u);
        stop_owner(0x01u);
        ok = ok && (active_voices_for(0x01u) == 0u) && (active_voices() == 0u);
        ok = ok && play(0x02u, tiny, 8);
        stop_all();
        ok = ok && (active_voices() == 0u);
        local.reclaim_ok = ok;
    }

    // 6. Exhaustion stays bounded: 16 accept, 17th refuses, still usable.
    {
        static int16_t tiny[8] = {7, 7, 7, 7, 7, 7, 7, 7};
        bool ok = true;
        for (uint32_t i = 0; i < kMaxVoices; ++i) ok = ok && play(0x10u + i, tiny, 8);
        ok = ok && !play(0x99u, tiny, 8);
        stop_owner(0x10u);
        ok = ok && play(0x99u, tiny, 8);
        stop_all();
        local.exhaustion_ok = ok;
    }

    // 7. DMA progress (real hardware only): LPIB must advance across pumps
    // while the stream runs. With a mock that never moves LPIB this is
    // skipped via expect_dma_progress=false.
    if (expect_dma_progress && ops_bound() && state_ == BackendState::Ready) {
        const uint32_t before = read_lpib();
        const uint64_t deadline = ops_.ticks_ms() + 500u;
        bool moved = false;
        // Iteration cap alongside the time deadline (wedged-clock safety).
        for (uint32_t tries = 0; tries < 100000u; ++tries) {
            pump();
            if (read_lpib() != before) {
                moved = true;
                break;
            }
            if (ops_.ticks_ms() >= deadline) break;
        }
        local.dma_progress = moved;
    } else {
        local.dma_progress = !expect_dma_progress;
    }

    stop_all();
    local.pass = local.validation_ok && local.conversion_ok && local.overlap_ok &&
                 local.drain_ok && local.reclaim_ok && local.exhaustion_ok &&
                 local.dma_progress;

    if (ops_bound()) {
        ops_.log_puts("[APP-AUDIO] self-test ");
        ops_.log_puts(local.pass ? "PASS" : "FAIL");
        ops_.log_puts(" val=");
        ops_.log_hex32(local.validation_ok ? 1u : 0u);
        ops_.log_puts(" conv=");
        ops_.log_hex32(local.conversion_ok ? 1u : 0u);
        ops_.log_puts(" ovlp=");
        ops_.log_hex32(local.overlap_ok ? 1u : 0u);
        ops_.log_puts(" drain=");
        ops_.log_hex32(local.drain_ok ? 1u : 0u);
        ops_.log_puts(" reclaim=");
        ops_.log_hex32(local.reclaim_ok ? 1u : 0u);
        ops_.log_puts(" exh=");
        ops_.log_hex32(local.exhaustion_ok ? 1u : 0u);
        ops_.log_puts(" dma=");
        ops_.log_hex32(local.dma_progress ? 1u : 0u);
        ops_.log_puts("\n");
    }

    if (report) *report = local;
    return local.pass;
}

Backend& backend_instance() {
    static Backend instance;
    return instance;
}

} // namespace app_audio
} // namespace kernel
