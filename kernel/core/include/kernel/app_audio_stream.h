// Bare-metal App Model audio streaming backend (MC6).
//
// Freestanding companion to the hosted MC5 path (native_app_audio.h/.cpp).
// The hosted mixer remains the source of truth for voice semantics; this
// module mirrors those semantics exactly in freestanding C++ (no STL, no
// exceptions, no threads, no <string.h>) and adds the missing piece MC5
// left out: a real HDA DMA streaming backend that continuously consumes
// mixer output.
//
// Layers (mirrors native_app_audio.h where possible, same names/constants):
//   1. Policy   : unchanged, enforced by the caller (bare-metal runtime
//      checks PackageInfo.hasAudioOutput, exactly like hosted checks the
//      permission list). This module performs no permission checks.
//   2. Validate (validate_play_request): same bounds/format contract as
//      hosted ValidatePlayRequest, including reason strings.
//   3. Convert  (convert_to_mix_format): same 8/16-bit mono 8-48 kHz to
//      S16 mono @ kMixRateHz nearest-neighbor rules, into a caller buffer.
//   4. Mixer    (class Mixer): 16 bounded voices, saturating mix,
//      deterministic advance, per-owner stop. Static storage only.
//   5. Backend  (class Backend): fixed-format HDA streaming layer. Mixer
//      output (22050 Hz mono S16) is adapted deterministically to the
//      device format (48000 Hz stereo S16, the HDA-mandatory base rate)
//      and fed to a bounded cyclic DMA ring. No unbounded queue exists
//      between mixer and hardware.
//
// Hardware access is fully abstracted through HdaOps so the whole backend
// (bring-up sequence, ring bookkeeping, refill, underrun recovery,
// lifecycle) is unit-testable on the host against a scripted mock
// controller. The kernel wires real MMIO/verb/tick/serial functions;
// nothing here touches port I/O or PCI config space directly (discovery
// stays in kernel::pci_audio, whose init/enumeration/verbs are reused).
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_APP_AUDIO_STREAM_H
#define KERNEL_APP_AUDIO_STREAM_H

// Quoted same-directory include (not <kernel/types.h>): keeps this header
// usable from host unit tests without putting the freestanding kernel
// include directory (which ships hosted-incompatible stdio.h/stdlib.h
// stubs) on the host include path.
#include "types.h"

namespace kernel {
namespace app_audio {

// ================================================================
// Canonical mixer contract (MC5, unchanged).
// Must stay in sync with native_app_audio.h and sdk/include/guidexos/audio.h.
// ================================================================

static const uint32_t kChannels = 1u;
static const uint32_t kMinRateHz = 8000u;
static const uint32_t kMaxRateHz = 48000u;
static const uint32_t kMixRateHz = 22050u;
static const uint32_t kMaxPcmBytes = 262144u;
static const uint32_t kMaxVoices = 16u;
static const uint32_t kMaxVoiceSeconds = 4u;
static const uint32_t kMaxVoiceFrames = kMixRateHz * kMaxVoiceSeconds; // 88200

// ================================================================
// Device (HDA stream) format.
//
// Fixed 48 kHz stereo S16LE: the HDA-mandatory base rate combo, supported
// by every compliant codec (including QEMU's hda-duplex) without format
// negotiation. The mixer format is adapted backend-side (see adapt_chunk).
// ================================================================

static const uint32_t kDeviceRateHz = 48000u;
static const uint32_t kDeviceChannels = 2u;
static const uint32_t kDeviceBits = 16u;

// ================================================================
// DMA ring geometry (all bounded, all static storage).
//
// 8 descriptors x 1024 device frames x stereo S16 = 4096 bytes each,
// 32 KiB of DMA audio + one 128-byte-aligned BDL. At 48 kHz stereo the
// ring holds ~170 ms of audio; the device always sees valid data (mixed
// audio or silence), so a late pump replays bounded stale audio instead
// of faulting or DMAing garbage.
// ================================================================

static const uint32_t kRingDescriptors = 8u;
static const uint32_t kChunkDeviceFrames = 1024u;
static const uint32_t kDescBytes =
    kChunkDeviceFrames * kDeviceChannels * (kDeviceBits / 8u); // 4096
static const uint32_t kRingBytes = kRingDescriptors * kDescBytes; // 32768

// Mixer frames backing one device chunk (exact, no drift):
// ceil(1024 * 22050 / 48000) = 471. Per-chunk counts vary 470/471 by
// phase; mix_needed_for_device() computes the exact count.
static const uint32_t kChunkMixFramesMax = 512u;

static const uint32_t kMaxBringUpVerbs = 64u;
static const uint32_t kMaxRecoveryAttempts = 3u;

// ================================================================
// Validation (mirrors hosted RequestError, minus the hosted-only
// PermissionDenied which the runtime reports before reaching here).
// ================================================================

enum RequestError : uint32_t {
    ErrorOk = 0,
    ErrorInvalidArgument = 1,
    ErrorUnsupported = 2,
    ErrorBusy = 3,
    ErrorBackendUnavailable = 4,
};

struct ValidationResult {
    RequestError error;
    const char* reason;
};

ValidationResult validate_play_request(const void* pcmData, uint32_t pcmBytes,
                                       uint32_t sampleRateHz, uint32_t channels,
                                       uint32_t bitsPerSample);

// Convert a validated request to mixer-native frames. outFrames must hold
// at least outCapacity frames; outFrameCount receives the converted length.
// Returns false when the resampled voice would exceed kMaxVoiceFrames
// (caller reports Unsupported). Same mapping as hosted ConvertToMixFormat:
// 8-bit (b-128)*256, 16-bit LE verbatim, nearest-neighbor resample.
bool convert_to_mix_format(const void* pcmData, uint32_t pcmBytes,
                           uint32_t sampleRateHz, uint32_t bitsPerSample,
                           int16_t* outFrames, uint32_t outCapacity,
                           uint32_t* outFrameCount);

// ================================================================
// Mixer format -> device format adaptation.
//
// Deterministic nearest-neighbor 22050 mono -> 48000 stereo with channel
// duplication. deviceBase is the global device-frame index of stereoOut[0]
// (keeps resample phase continuous across chunks and across idle gaps).
// mixMono must contain mix_needed_for_device(deviceFrames) frames starting
// at the phase-implied source offset (see mix_source_start()).
// ================================================================

uint32_t mix_needed_for_device(uint32_t deviceFrames);
uint32_t mix_source_start(uint64_t deviceBase);
void adapt_chunk(const int16_t* mixMono, uint64_t deviceBase,
                 int16_t* stereoOut, uint32_t deviceFrames);

// ================================================================
// Freestanding mixer: MC5 semantics, static storage, no allocation.
// ================================================================

class Mixer {
public:
    Mixer();

    // Queue mixer-native frames for an owner. Copies into a static voice
    // slot. Returns false when every voice is busy.
    bool play(uint64_t owner, const int16_t* frames, uint32_t frameCount);

    // Render frameCount mixed frames (saturating add), advancing every
    // live voice and reclaiming finished ones. Silent (zeros) when idle.
    void mix(int16_t* outFrames, uint32_t frameCount);

    uint32_t active_voices() const;
    uint32_t active_voices_for(uint64_t owner) const;

    void stop_owner(uint64_t owner);
    void stop_all();

private:
    struct Voice {
        bool active;
        uint64_t owner;
        uint32_t position;
        uint32_t length;
        int16_t samples[kMaxVoiceFrames];
    };
    Voice voices_[kMaxVoices];
};

// ================================================================
// Hardware abstraction (wired by the kernel, mocked by host tests).
// ================================================================

struct HdaOps {
    uint8_t (*read8)(uint64_t mmioBase, uint32_t offset);
    uint16_t (*read16)(uint64_t mmioBase, uint32_t offset);
    uint32_t (*read32)(uint64_t mmioBase, uint32_t offset);
    void (*write8)(uint64_t mmioBase, uint32_t offset, uint8_t value);
    void (*write16)(uint64_t mmioBase, uint32_t offset, uint16_t value);
    void (*write32)(uint64_t mmioBase, uint32_t offset, uint32_t value);
    // Codec verb transport (pci_audio::hda_send_verb in the kernel).
    bool (*send_verb)(uint8_t ctrlIndex, uint8_t codecAddr, uint8_t nodeId,
                      uint32_t verb, uint32_t* response);
    // Device-visible address translation. The kernel links at 0x100000 but
    // loads at BootInfo.KernelPhysicalBase, so the kernel wires
    // base + (virt - 0x100000) (same formula as nic/virtio/mmio); the
    // backend validates nonzero and < 4 GiB and fails closed otherwise.
    // A future IOMMU pass would hook translation here.
    uint64_t (*virt_to_phys)(const void* ptr);
    // Monotonic milliseconds (pit::ticks()*10 in the kernel).
    uint64_t (*ticks_ms)();
    // Bounded diagnostic sink (serial in the kernel).
    void (*log_puts)(const char* text);
    void (*log_hex32)(uint32_t value);
    void (*log_hex64)(uint64_t value);
};

// Codec output-path endpoints, discovered by kernel::pci_audio enumeration
// (HDACodec.afgNode/dacNode/pinOutNode) and passed in by the glue layer.
struct OutputPath {
    uint8_t codecAddr;
    uint8_t afgNode; // 0 = unknown, skip AFG power step
    uint8_t dacNode;
    uint8_t pinNode;
    bool valid;
};

enum class BackendState : uint8_t {
    Unavailable = 0, // no usable controller/path (degraded: mixer still runs)
    Ready = 1,       // DMA silence-or-audio loop running
    Failed = 2,      // explicit failure after bounded recovery (degraded)
};

// HDA Buffer Descriptor List entry (16 bytes, base must be 128-byte
// aligned). Matches the layout used by kernel::pci_audio::BDLEntry.
struct BdlEntry {
    uint64_t address; // device-visible (physical) address of the buffer
    uint32_t length;  // length in bytes
    uint32_t flags;   // bit 0 = IOC (Interrupt On Completion)
};

// ================================================================
// Streaming backend: owns one Mixer, one HDA output stream, one ring.
// Single instance; not reentrant (the kernel has no audio IRQ path and
// all entry points run in task context).
// ================================================================

class Backend {
public:
    Backend();

    // Wire hardware access. Must precede ensure_ready(). The ops struct
    // is copied; the caller retains no obligations.
    void bind_ops(const HdaOps* ops, uint64_t mmioBase, uint8_t ctrlIndex);

    // Discover-free bring-up: programs the codec output path (DAC format /
    // stream / power, pin enable / EAPD / unmute) when the enumerator found
    // usable DAC/pin nodes, then starts the cyclic DMA ring prefilled with
    // silence. When codec verbs demonstrably fail (no usable path), falls
    // back to a default-route stream start that is only accepted after
    // observed LPIB motion (verify_streaming), so "Ready" always means
    // proven DMA motion, never blind optimism. Bounded verbs, bounded waits.
    // Returns true when the stream is running (state Ready).
    bool ensure_ready(const OutputPath* path);

    // Queue mixer-native frames for an owner. Always safe to call: when
    // the backend is Unavailable/Failed the mixer still queues, mixes,
    // and reclaims (explicit null-sink degrade, like hosted), so
    // applications never block on audio. Returns false only when all
    // mixer voices are busy.
    bool play(uint64_t owner, const int16_t* frames, uint32_t frameCount);

    // Non-blocking, bounded pump: advances/reclaims voices, refills DMA
    // descriptors due from the hardware cursor (LPIB), detects underruns,
    // performs bounded recovery. Call from task context at any natural
    // cadence (app frame present / event poll / play). Never sleeps.
    void pump();

    void stop_owner(uint64_t owner);
    void stop_all();

    // Return a Ready backend to a clean silence loop (post self-test):
    // stops voices, reprefills the ring with silence, restarts the
    // stream. No-op unless Ready.
    void reset_stream_silence();

    BackendState state() const;
    const char* name() const; // "hda-48000-stereo-s16" or "none"

    // True when the full codec verb path (DAC/pin programming) succeeded.
    // False after a default-route fallback start (streaming verified via
    // LPIB motion, codec left at firmware defaults).
    bool codec_path_ok() const;

    uint64_t plays_accepted() const;
    uint64_t plays_busy() const;
    uint64_t chunks_mixed() const;
    uint64_t descs_refilled() const;
    uint64_t underruns() const;
    uint64_t recoveries() const;
    uint64_t device_errors() const;
    uint32_t active_voices() const;
    uint32_t active_voices_for(uint64_t owner) const;
    uint32_t last_lpib() const;

    // Bounded end-to-end self-test used by host unit tests and by the
    // opt-in kernel boot probe (GXOS_AUDIO_BOOT_SELFTEST). Synthesizes
    // frames through validate/convert/play/pump, checks overlap, drain,
    // reclaim, exhaustion bounds, and (with real hardware) DMA progress.
    // Returns true on PASS. expect_dma_progress=false runs the full logic
    // test without requiring LPIB advancement (mock without DMA motion).
    struct SelfTestReport {
        bool validation_ok;
        bool conversion_ok;
        bool overlap_ok;
        bool drain_ok;
        bool reclaim_ok;
        bool exhaustion_ok;
        bool dma_progress;
        bool pass;
    };
    bool self_test(bool expect_dma_progress, SelfTestReport* report);

    // Serial-friendly one-line status report (bounded output).
    void report_status() const;

private:
    bool ops_bound() const;
    void log_state(const char* what) const;
    void log_hex_labeled(const char* label, uint32_t value) const;
    bool bring_up_path(const OutputPath* path);
    bool program_ring();
    bool start_stream();
    bool verify_streaming();
    bool stop_stream();
    uint32_t read_lpib();
    uint32_t read_status();
    void clear_status(uint32_t bits);
    void refill_due(bool force_ahead);
    void refill_descriptor(uint8_t index);
    void fill_silence(uint8_t index);
    void handle_stream_error(uint32_t status);
    uint32_t verb(uint8_t codecAddr, uint8_t nodeId, uint32_t verb,
                  uint32_t* response);

    HdaOps ops_;
    bool ops_bound_;
    bool codec_path_ok_;
    uint64_t mmio_base_;
    uint8_t ctrl_index_;

    BackendState state_;
    char name_[32];

    Mixer mixer_;

    alignas(128) BdlEntry bdl_[kRingDescriptors];
    alignas(64) int16_t pcm_[kRingDescriptors][kChunkDeviceFrames * kDeviceChannels];

    uint8_t stream_index_; // HDA SD index (0 = first output stream)
    uint8_t stream_tag_;   // HDA stream tag (1..15)
    uint64_t device_frame_;// next device frame to render (resample phase)
    uint32_t last_lpib_;
    bool lpib_valid_;
    uint32_t consecutive_errors_;

    uint64_t plays_accepted_;
    uint64_t plays_busy_;
    uint64_t chunks_mixed_;
    uint64_t descs_refilled_;
    uint64_t underruns_;
    uint64_t recoveries_;
    uint64_t device_errors_;
    uint32_t play_log_count_;
};

// Process-wide backend instance (single HDA output stream per boot).
Backend& backend_instance();

} // namespace app_audio
} // namespace kernel

#endif // KERNEL_APP_AUDIO_STREAM_H
