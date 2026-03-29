// USB Audio Class 2.0 (UAC2) Driver
//
// Extends the existing UAC1 driver with USB Audio Class 2.0 support:
//
//   - Clock Source / Clock Selector / Clock Multiplier entities
//   - High-bandwidth isochronous endpoints (up to 3x 1024 bytes)
//   - Explicit clock domain management
//   - Extended Feature Unit controls (loudness, AGC, delay)
//   - Class-specific interrupt endpoint for status notifications
//   - 24-bit / 32-bit sample depths and high sample rates (96/192 kHz)
//   - Type III (IEC61937) format support for compressed passthrough
//
// Reference: USB Audio Device Class Specification 2.0 (audio20-final.pdf)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_AUDIO_UAC2_H
#define KERNEL_USB_AUDIO_UAC2_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_audio_uac2 {

// ================================================================
// UAC2 interface subclass codes
// ================================================================

static const uint8_t UAC2_SC_AUDIOCONTROL   = 0x01;
static const uint8_t UAC2_SC_AUDIOSTREAMING  = 0x02;
static const uint8_t UAC2_SC_MIDISTREAMING   = 0x03;

// ================================================================
// UAC2 Audio Function Category codes
// ================================================================

enum AudioFunctionCategory : uint8_t {
    AFC_DESKTOP_SPEAKER   = 0x01,
    AFC_HOME_THEATER      = 0x02,
    AFC_MICROPHONE        = 0x03,
    AFC_HEADSET           = 0x04,
    AFC_TELEPHONE         = 0x05,
    AFC_CONVERTER         = 0x06,
    AFC_SOUND_RECORDER    = 0x07,
    AFC_IO_BOX            = 0x08,
    AFC_MUSICAL_INSTRUMENT= 0x09,
    AFC_PRO_AUDIO         = 0x0A,
    AFC_AUDIO_VIDEO       = 0x0B,
    AFC_CONTROL_PANEL     = 0x0C,
    AFC_OTHER             = 0xFF,
};

// ================================================================
// UAC2 Audio Control descriptor subtypes
// ================================================================

enum UAC2ACDescSubtype : uint8_t {
    UAC2_AC_HEADER            = 0x01,
    UAC2_AC_INPUT_TERMINAL    = 0x02,
    UAC2_AC_OUTPUT_TERMINAL   = 0x03,
    UAC2_AC_MIXER_UNIT        = 0x04,
    UAC2_AC_SELECTOR_UNIT     = 0x05,
    UAC2_AC_FEATURE_UNIT      = 0x06,
    UAC2_AC_EFFECT_UNIT       = 0x07,
    UAC2_AC_PROCESSING_UNIT   = 0x08,
    UAC2_AC_EXTENSION_UNIT    = 0x09,
    UAC2_AC_CLOCK_SOURCE      = 0x0A,
    UAC2_AC_CLOCK_SELECTOR    = 0x0B,
    UAC2_AC_CLOCK_MULTIPLIER  = 0x0C,
    UAC2_AC_SAMPLE_RATE_CONV  = 0x0D,
};

// ================================================================
// UAC2 Audio Streaming descriptor subtypes
// ================================================================

enum UAC2ASDescSubtype : uint8_t {
    UAC2_AS_GENERAL           = 0x01,
    UAC2_AS_FORMAT_TYPE       = 0x02,
    UAC2_AS_ENCODER           = 0x03,
    UAC2_AS_DECODER           = 0x04,
};

// ================================================================
// UAC2 class-specific request codes
// ================================================================

enum UAC2Request : uint8_t {
    UAC2_CUR   = 0x01,  // Current setting
    UAC2_RANGE = 0x02,  // Range of settings (min/max/res triplets)
    UAC2_MEM   = 0x03,  // Memory access
};

// ================================================================
// UAC2 Clock Source control selectors
// ================================================================

enum ClockSourceCS : uint8_t {
    CS_SAM_FREQ_CONTROL     = 0x01,
    CS_CLOCK_VALID_CONTROL  = 0x02,
};

// ================================================================
// UAC2 Clock Selector control selectors
// ================================================================

enum ClockSelectorCS : uint8_t {
    CX_CLOCK_SELECTOR_CONTROL = 0x01,
};

// ================================================================
// UAC2 Feature Unit control selectors (extended from UAC1)
// ================================================================

enum UAC2FeatureUnitCS : uint8_t {
    UAC2_FU_MUTE_CONTROL          = 0x01,
    UAC2_FU_VOLUME_CONTROL        = 0x02,
    UAC2_FU_BASS_CONTROL          = 0x03,
    UAC2_FU_MID_CONTROL           = 0x04,
    UAC2_FU_TREBLE_CONTROL        = 0x05,
    UAC2_FU_GRAPHIC_EQ_CONTROL    = 0x06,
    UAC2_FU_AGC_CONTROL           = 0x07,
    UAC2_FU_DELAY_CONTROL         = 0x08,
    UAC2_FU_BASS_BOOST_CONTROL    = 0x09,
    UAC2_FU_LOUDNESS_CONTROL      = 0x0A,
    UAC2_FU_INPUT_GAIN_CONTROL    = 0x0B,
    UAC2_FU_INPUT_GAIN_PAD_CONTROL= 0x0C,
    UAC2_FU_PHASE_INVERTER_CONTROL= 0x0D,
    UAC2_FU_UNDERFLOW_CONTROL     = 0x0E,
    UAC2_FU_OVERFLOW_CONTROL      = 0x0F,
    UAC2_FU_LATENCY_CONTROL       = 0x10,
};

// ================================================================
// UAC2 Clock Source attributes
// ================================================================

enum ClockSourceAttr : uint8_t {
    CLOCK_ATTR_EXTERNAL       = 0x00,
    CLOCK_ATTR_INT_FIXED      = 0x01,
    CLOCK_ATTR_INT_VARIABLE   = 0x02,
    CLOCK_ATTR_INT_PROGRAMMABLE = 0x03,
    CLOCK_ATTR_SYNCED_TO_SOF  = 0x04,
};

// ================================================================
// UAC2 Format Type codes
// ================================================================

enum UAC2FormatType : uint8_t {
    UAC2_FORMAT_TYPE_UNDEFINED = 0x00,
    UAC2_FORMAT_TYPE_I         = 0x01,  // PCM
    UAC2_FORMAT_TYPE_II        = 0x02,  // compressed (IEC61937)
    UAC2_FORMAT_TYPE_III       = 0x03,  // IEC61937 passthrough
    UAC2_FORMAT_TYPE_IV        = 0x04,  // extended
    UAC2_EXT_FORMAT_TYPE_I     = 0x81,
    UAC2_EXT_FORMAT_TYPE_II    = 0x82,
    UAC2_EXT_FORMAT_TYPE_III   = 0x83,
};

// ================================================================
// UAC2 Audio Data Format codes (Type I)
// ================================================================

enum UAC2AudioDataFormat : uint32_t {
    UAC2_PCM        = 0x00000001,
    UAC2_PCM8       = 0x00000002,
    UAC2_IEEE_FLOAT = 0x00000004,
    UAC2_ALAW       = 0x00000008,
    UAC2_MULAW      = 0x00000010,
    UAC2_RAW_DATA   = 0x80000000,
};

// ================================================================
// UAC2 sample rate range (returned by RANGE request on clock source)
// ================================================================

struct SampleRateRange {
    uint32_t min;
    uint32_t max;
    uint32_t res;  // 0 = continuous between min and max
};

static const uint8_t MAX_RATE_RANGES = 16;

// ================================================================
// UAC2 Clock Source descriptor (parsed)
// ================================================================

struct ClockSource {
    bool     valid;
    uint8_t  clockId;
    uint8_t  attributes;     // ClockSourceAttr
    uint8_t  assocTerminal;
    uint32_t currentFreq;
    bool     clockValid;
    SampleRateRange rates[MAX_RATE_RANGES];
    uint8_t  numRanges;
};

// ================================================================
// UAC2 Audio Stream info
// ================================================================

struct UAC2Stream {
    bool     active;
    uint8_t  direction;       // 0 = playback, 1 = capture
    uint8_t  interfaceNum;
    uint8_t  altSetting;
    uint8_t  endpointAddr;
    uint16_t maxPacketSize;
    uint8_t  clockSourceId;
    uint8_t  formatType;      // UAC2FormatType
    uint32_t formats;         // bitmask of UAC2AudioDataFormat
    uint8_t  channels;
    uint8_t  bitResolution;
    uint8_t  subSlotSize;     // bytes per subslot (sample container)
    uint32_t currentSampleRate;
    bool     streaming;
};

// ================================================================
// UAC2 Audio Device instance
// ================================================================

struct UAC2Device {
    bool         active;
    uint8_t      usbAddress;
    uint8_t      controlInterface;
    uint8_t      featureUnitId;
    ClockSource  clockSource;
    UAC2Stream   playback;
    UAC2Stream   capture;
    int16_t      volumeMin;      // 1/256 dB
    int16_t      volumeMax;
    int16_t      volumeRes;
    int16_t      volumeCurrent;
    bool         muted;
    uint8_t      audioFuncCategory;
};

static const uint8_t MAX_UAC2_DEVICES = 4;

// ================================================================
// Public API
// ================================================================

void init();

// Probe a USB device for UAC2 interfaces. Returns true if claimed.
bool probe(uint8_t usbAddress);

// Release on detach.
void release(uint8_t usbAddress);

uint8_t device_count();
const UAC2Device* get_device(uint8_t index);

// ----------------------------------------------------------------
// Clock management
// ----------------------------------------------------------------

// Get the current sample rate from the clock source.
uint32_t get_clock_frequency(uint8_t devIndex);

// Set the sample rate on the clock source.
usb::TransferStatus set_clock_frequency(uint8_t devIndex, uint32_t freq);

// Check if the clock source is valid (locked).
bool is_clock_valid(uint8_t devIndex);

// Get supported sample rate ranges.
uint8_t get_rate_ranges(uint8_t devIndex, const SampleRateRange** ranges);

// ----------------------------------------------------------------
// Volume / mute controls
// ----------------------------------------------------------------

int16_t get_volume(uint8_t devIndex);
usb::TransferStatus set_volume(uint8_t devIndex, int16_t volume);
bool get_mute(uint8_t devIndex);
usb::TransferStatus set_mute(uint8_t devIndex, bool mute);

// ----------------------------------------------------------------
// Streaming control
// ----------------------------------------------------------------

usb::TransferStatus start_stream(uint8_t devIndex, uint8_t direction);
usb::TransferStatus stop_stream(uint8_t devIndex, uint8_t direction);

usb::TransferStatus playback_write(uint8_t devIndex,
                                   const void* pcmData,
                                   uint16_t len,
                                   uint16_t* written);

usb::TransferStatus capture_read(uint8_t devIndex,
                                 void* pcmData,
                                 uint16_t maxLen,
                                 uint16_t* bytesRead);

} // namespace usb_audio_uac2
} // namespace kernel

#endif // KERNEL_USB_AUDIO_UAC2_H
