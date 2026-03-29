// USB Audio Class Driver
//
// Supports:
//   - Audio Control interface (feature unit: volume, mute)
//   - Audio Streaming interface (PCM playback and capture)
//   - Sample rate selection
//   - USB speakers, headsets, and microphones
//
// Reference: USB Audio Class Definition 1.0 (audio10.pdf)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_AUDIO_H
#define KERNEL_USB_AUDIO_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_audio {

// ================================================================
// Audio class-specific descriptor subtypes
// ================================================================

enum AudioCSDescSubtype : uint8_t {
    // Audio Control subtypes
    AC_HEADER          = 0x01,
    AC_INPUT_TERMINAL  = 0x02,
    AC_OUTPUT_TERMINAL = 0x03,
    AC_MIXER_UNIT      = 0x04,
    AC_SELECTOR_UNIT   = 0x05,
    AC_FEATURE_UNIT    = 0x06,
    AC_PROCESSING_UNIT = 0x07,
    AC_EXTENSION_UNIT  = 0x08,

    // Audio Streaming subtypes
    AS_GENERAL         = 0x01,
    AS_FORMAT_TYPE     = 0x02,
    AS_FORMAT_SPECIFIC = 0x03,
};

// ================================================================
// Audio terminal types (common)
// ================================================================

enum AudioTerminalType : uint16_t {
    TT_USB_STREAMING   = 0x0101,
    TT_SPEAKER         = 0x0301,
    TT_HEADPHONES      = 0x0302,
    TT_DESKTOP_SPEAKER = 0x0304,
    TT_MICROPHONE      = 0x0201,
    TT_DESKTOP_MIC     = 0x0202,
    TT_HEADSET         = 0x0402,
};

// ================================================================
// Audio class-specific requests
// ================================================================

enum AudioRequest : uint8_t {
    AUDIO_REQ_SET_CUR  = 0x01,
    AUDIO_REQ_GET_CUR  = 0x81,
    AUDIO_REQ_SET_MIN  = 0x02,
    AUDIO_REQ_GET_MIN  = 0x82,
    AUDIO_REQ_SET_MAX  = 0x03,
    AUDIO_REQ_GET_MAX  = 0x83,
    AUDIO_REQ_SET_RES  = 0x04,
    AUDIO_REQ_GET_RES  = 0x84,
};

// Feature Unit control selectors
enum FeatureUnitCS : uint8_t {
    FU_MUTE_CONTROL   = 0x01,
    FU_VOLUME_CONTROL = 0x02,
    FU_BASS_CONTROL   = 0x03,
    FU_TREBLE_CONTROL = 0x05,
};

// ================================================================
// Audio format info
// ================================================================

struct AudioFormat {
    uint8_t  formatTag;       // 1 = PCM
    uint8_t  channels;
    uint8_t  bitResolution;
    uint8_t  subFrameSize;    // bytes per sample per channel
    uint32_t sampleRates[8];  // supported sample rates (0-terminated)
    uint8_t  numSampleRates;
};

// ================================================================
// Audio streaming endpoint
// ================================================================

enum AudioStreamDir : uint8_t {
    STREAM_PLAYBACK = 0,
    STREAM_CAPTURE  = 1,
};

struct AudioStream {
    bool           active;
    AudioStreamDir direction;
    uint8_t        interfaceNum;
    uint8_t        altSetting;
    uint8_t        endpointAddr;
    uint16_t       maxPacketSize;
    AudioFormat    format;
    uint32_t       currentSampleRate;
    bool           streaming;
};

// ================================================================
// Audio device instance
// ================================================================

struct AudioDevice {
    bool        active;
    uint8_t     usbAddress;
    uint8_t     controlInterface;
    uint8_t     featureUnitId;     // for volume/mute control
    AudioStream playback;
    AudioStream capture;
    int16_t     volumeMin;         // in 1/256 dB
    int16_t     volumeMax;
    int16_t     volumeRes;
    int16_t     volumeCurrent;
    bool        muted;
};

static const uint8_t MAX_AUDIO_DEVICES = 4;

// ================================================================
// Public API
// ================================================================

// Initialise the audio class driver.
void init();

// Probe a USB device for audio interfaces. Returns true if claimed.
bool probe(uint8_t usbAddress);

// Release audio interfaces on a device (on detach).
void release(uint8_t usbAddress);

// Return number of active audio devices.
uint8_t device_count();

// Get device info by index.
const AudioDevice* get_device(uint8_t index);

// ----------------------------------------------------------------
// Volume / mute controls
// ----------------------------------------------------------------

// Get current volume (1/256 dB units).
int16_t get_volume(uint8_t devIndex);

// Set volume (clamped to min/max).
usb::TransferStatus set_volume(uint8_t devIndex, int16_t volume);

// Get/set mute state.
bool get_mute(uint8_t devIndex);
usb::TransferStatus set_mute(uint8_t devIndex, bool mute);

// ----------------------------------------------------------------
// Streaming control
// ----------------------------------------------------------------

// Set sample rate for playback or capture stream.
usb::TransferStatus set_sample_rate(uint8_t devIndex,
                                    AudioStreamDir dir,
                                    uint32_t sampleRate);

// Start streaming (selects the alternate setting with audio data).
usb::TransferStatus start_stream(uint8_t devIndex, AudioStreamDir dir);

// Stop streaming (selects alternate setting 0 = zero-bandwidth).
usb::TransferStatus stop_stream(uint8_t devIndex, AudioStreamDir dir);

// Write PCM data to the playback stream (isochronous transfer).
// Returns number of bytes actually sent.
usb::TransferStatus playback_write(uint8_t devIndex,
                                   const void* pcmData,
                                   uint16_t len,
                                   uint16_t* written);

// Read PCM data from the capture stream (isochronous transfer).
usb::TransferStatus capture_read(uint8_t devIndex,
                                 void* pcmData,
                                 uint16_t maxLen,
                                 uint16_t* bytesRead);

} // namespace usb_audio
} // namespace kernel

#endif // KERNEL_USB_AUDIO_H
