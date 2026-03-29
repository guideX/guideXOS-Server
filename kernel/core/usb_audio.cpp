// USB Audio Class Driver — Implementation
//
// Audio Control (volume/mute) and Audio Streaming (playback/capture).
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_audio.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_audio {

// ================================================================
// Internal state
// ================================================================

static AudioDevice s_devices[MAX_AUDIO_DEVICES];
static uint8_t s_deviceCount = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static int16_t clamp16(int16_t v, int16_t lo, int16_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ================================================================
// Audio class-specific control requests
// ================================================================

static usb::TransferStatus audio_get_cur(uint8_t addr, uint8_t iface,
                                         uint8_t unitId, uint8_t cs,
                                         uint8_t channel,
                                         void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1; // Device-to-host, class, interface
    setup.bRequest      = AUDIO_REQ_GET_CUR;
    setup.wValue        = static_cast<uint16_t>((cs << 8) | channel);
    setup.wIndex        = static_cast<uint16_t>((unitId << 8) | iface);
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

static usb::TransferStatus audio_set_cur(uint8_t addr, uint8_t iface,
                                         uint8_t unitId, uint8_t cs,
                                         uint8_t channel,
                                         const void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = AUDIO_REQ_SET_CUR;
    setup.wValue        = static_cast<uint16_t>((cs << 8) | channel);
    setup.wIndex        = static_cast<uint16_t>((unitId << 8) | iface);
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup,
                                 const_cast<void*>(data), len);
}

static usb::TransferStatus audio_get_min_max_res(uint8_t addr, uint8_t iface,
                                                  uint8_t unitId, uint8_t cs,
                                                  int16_t* minVal,
                                                  int16_t* maxVal,
                                                  int16_t* resVal)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.wValue        = static_cast<uint16_t>((cs << 8) | 0);
    setup.wIndex        = static_cast<uint16_t>((unitId << 8) | iface);
    setup.wLength       = 2;

    // GET_MIN
    setup.bRequest = AUDIO_REQ_GET_MIN;
    usb::TransferStatus st = usb::control_transfer(addr, &setup, minVal, 2);
    if (st != usb::XFER_SUCCESS) return st;

    // GET_MAX
    setup.bRequest = AUDIO_REQ_GET_MAX;
    st = usb::control_transfer(addr, &setup, maxVal, 2);
    if (st != usb::XFER_SUCCESS) return st;

    // GET_RES
    setup.bRequest = AUDIO_REQ_GET_RES;
    st = usb::control_transfer(addr, &setup, resVal, 2);
    return st;
}

// ================================================================
// Set interface alternate setting
// ================================================================

static usb::TransferStatus set_interface(uint8_t addr,
                                         uint8_t iface,
                                         uint8_t altSetting)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x01; // Host-to-device, standard, interface
    setup.bRequest      = usb::REQ_SET_INTERFACE;
    setup.wValue        = altSetting;
    setup.wIndex        = iface;
    setup.wLength       = 0;
    return usb::control_transfer(addr, &setup, nullptr, 0);
}

// ================================================================
// Set sampling frequency on an isochronous endpoint
// (Audio 1.0: SET_CUR on the endpoint with 3-byte sample rate)
// ================================================================

static usb::TransferStatus set_endpoint_sample_rate(uint8_t addr,
                                                    uint8_t endpoint,
                                                    uint32_t rate)
{
    uint8_t rateBytes[3];
    rateBytes[0] = static_cast<uint8_t>(rate & 0xFF);
    rateBytes[1] = static_cast<uint8_t>((rate >> 8) & 0xFF);
    rateBytes[2] = static_cast<uint8_t>((rate >> 16) & 0xFF);

    usb::SetupPacket setup;
    setup.bmRequestType = 0x22; // Host-to-device, class, endpoint
    setup.bRequest      = AUDIO_REQ_SET_CUR;
    setup.wValue        = 0x0100; // Sampling Frequency Control
    setup.wIndex        = endpoint;
    setup.wLength       = 3;
    return usb::control_transfer(addr, &setup, rateBytes, 3);
}

// ================================================================
// Parse audio control and streaming descriptors
// ================================================================

static void parse_audio_descriptors(AudioDevice* dev,
                                    const usb::Device* usbDev)
{
    // Scan interfaces for Audio Control and Audio Streaming
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] != usb::CLASS_AUDIO) continue;

        uint8_t subclass = usbDev->interfaceSubClass[i];

        if (subclass == 0x01) {
            // Audio Control
            dev->controlInterface = i;
            dev->featureUnitId = 2; // typical default; real parser would read descriptors
        }
        else if (subclass == 0x02) {
            // Audio Streaming
            // Determine direction from endpoint
            for (uint8_t e = 0; e < usb::MAX_ENDPOINTS * 2; ++e) {
                const usb::Endpoint& ep = usbDev->endpoints[e];
                if (!ep.active || ep.type != usb::TRANSFER_ISOCHRONOUS) continue;

                AudioStream* stream = nullptr;
                if (ep.dir == usb::DIR_HOST_TO_DEVICE) {
                    stream = &dev->playback;
                    stream->direction = STREAM_PLAYBACK;
                } else {
                    stream = &dev->capture;
                    stream->direction = STREAM_CAPTURE;
                }

                stream->interfaceNum  = i;
                stream->altSetting    = 1; // typical non-zero-bandwidth alt
                stream->endpointAddr  = ep.address;
                stream->maxPacketSize = ep.maxPacketSize;
                stream->active        = true;
                stream->streaming     = false;

                // Default format: 16-bit stereo PCM @ 44100 Hz
                stream->format.formatTag      = 1; // PCM
                stream->format.channels        = 2;
                stream->format.bitResolution   = 16;
                stream->format.subFrameSize    = 2;
                stream->format.sampleRates[0]  = 44100;
                stream->format.sampleRates[1]  = 48000;
                stream->format.sampleRates[2]  = 0;
                stream->format.numSampleRates  = 2;
                stream->currentSampleRate      = 44100;
            }
        }
    }
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;
}

bool probe(uint8_t usbAddress)
{
    const usb::Device* usbDev = usb::get_device(usbAddress);
    if (!usbDev) return false;

    // Check if any interface is Audio class
    bool hasAudio = false;
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] == usb::CLASS_AUDIO) {
            hasAudio = true;
            break;
        }
    }
    if (!hasAudio) return false;

    if (s_deviceCount >= MAX_AUDIO_DEVICES) return false;

    AudioDevice* dev = &s_devices[s_deviceCount];
    memzero(dev, sizeof(AudioDevice));
    dev->usbAddress = usbAddress;

    parse_audio_descriptors(dev, usbDev);

    // Query volume range from feature unit
    if (dev->featureUnitId != 0) {
        audio_get_min_max_res(usbAddress, dev->controlInterface,
                              dev->featureUnitId, FU_VOLUME_CONTROL,
                              &dev->volumeMin, &dev->volumeMax,
                              &dev->volumeRes);

        audio_get_cur(usbAddress, dev->controlInterface,
                      dev->featureUnitId, FU_VOLUME_CONTROL, 0,
                      &dev->volumeCurrent, 2);

        uint8_t muteVal = 0;
        audio_get_cur(usbAddress, dev->controlInterface,
                      dev->featureUnitId, FU_MUTE_CONTROL, 0,
                      &muteVal, 1);
        dev->muted = (muteVal != 0);
    }

    dev->active = true;
    s_deviceCount++;
    return true;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_AUDIO_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            // Stop any active streams
            if (s_devices[i].playback.streaming) {
                set_interface(usbAddress, s_devices[i].playback.interfaceNum, 0);
            }
            if (s_devices[i].capture.streaming) {
                set_interface(usbAddress, s_devices[i].capture.interfaceNum, 0);
            }
            s_devices[i].active = false;
            s_deviceCount--;
        }
    }
}

uint8_t device_count() { return s_deviceCount; }

const AudioDevice* get_device(uint8_t index)
{
    if (index >= MAX_AUDIO_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

// ----------------------------------------------------------------
// Volume / mute controls
// ----------------------------------------------------------------

int16_t get_volume(uint8_t devIndex)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return 0;
    return s_devices[devIndex].volumeCurrent;
}

usb::TransferStatus set_volume(uint8_t devIndex, int16_t volume)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return usb::XFER_ERROR;
    AudioDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    volume = clamp16(volume, dev->volumeMin, dev->volumeMax);

    usb::TransferStatus st = audio_set_cur(dev->usbAddress,
                                           dev->controlInterface,
                                           dev->featureUnitId,
                                           FU_VOLUME_CONTROL, 0,
                                           &volume, 2);
    if (st == usb::XFER_SUCCESS) {
        dev->volumeCurrent = volume;
    }
    return st;
}

bool get_mute(uint8_t devIndex)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return false;
    return s_devices[devIndex].muted;
}

usb::TransferStatus set_mute(uint8_t devIndex, bool mute)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return usb::XFER_ERROR;
    AudioDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint8_t val = mute ? 1 : 0;
    usb::TransferStatus st = audio_set_cur(dev->usbAddress,
                                           dev->controlInterface,
                                           dev->featureUnitId,
                                           FU_MUTE_CONTROL, 0,
                                           &val, 1);
    if (st == usb::XFER_SUCCESS) {
        dev->muted = mute;
    }
    return st;
}

// ----------------------------------------------------------------
// Streaming control
// ----------------------------------------------------------------

usb::TransferStatus set_sample_rate(uint8_t devIndex,
                                    AudioStreamDir dir,
                                    uint32_t sampleRate)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return usb::XFER_ERROR;
    AudioDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    AudioStream* stream = (dir == STREAM_PLAYBACK) ? &dev->playback : &dev->capture;
    if (!stream->active) return usb::XFER_NOT_SUPPORTED;

    usb::TransferStatus st = set_endpoint_sample_rate(
        dev->usbAddress, stream->endpointAddr, sampleRate);

    if (st == usb::XFER_SUCCESS) {
        stream->currentSampleRate = sampleRate;
    }
    return st;
}

usb::TransferStatus start_stream(uint8_t devIndex, AudioStreamDir dir)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return usb::XFER_ERROR;
    AudioDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    AudioStream* stream = (dir == STREAM_PLAYBACK) ? &dev->playback : &dev->capture;
    if (!stream->active) return usb::XFER_NOT_SUPPORTED;

    usb::TransferStatus st = set_interface(dev->usbAddress,
                                           stream->interfaceNum,
                                           stream->altSetting);
    if (st == usb::XFER_SUCCESS) {
        // Set sample rate on the endpoint
        set_endpoint_sample_rate(dev->usbAddress, stream->endpointAddr,
                                 stream->currentSampleRate);
        stream->streaming = true;
    }
    return st;
}

usb::TransferStatus stop_stream(uint8_t devIndex, AudioStreamDir dir)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return usb::XFER_ERROR;
    AudioDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    AudioStream* stream = (dir == STREAM_PLAYBACK) ? &dev->playback : &dev->capture;
    if (!stream->active) return usb::XFER_NOT_SUPPORTED;

    usb::TransferStatus st = set_interface(dev->usbAddress,
                                           stream->interfaceNum, 0);
    if (st == usb::XFER_SUCCESS) {
        stream->streaming = false;
    }
    return st;
}

usb::TransferStatus playback_write(uint8_t devIndex,
                                   const void* pcmData,
                                   uint16_t len,
                                   uint16_t* written)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return usb::XFER_ERROR;
    AudioDevice* dev = &s_devices[devIndex];
    if (!dev->active || !dev->playback.streaming) return usb::XFER_ERROR;

    // Isochronous transfers go through the bulk path for now;
    // a full isochronous scheduler would use a dedicated HCI API.
    return usb::hci::bulk_transfer(dev->usbAddress,
                                   dev->playback.endpointAddr,
                                   const_cast<void*>(pcmData),
                                   len, written);
}

usb::TransferStatus capture_read(uint8_t devIndex,
                                 void* pcmData,
                                 uint16_t maxLen,
                                 uint16_t* bytesRead)
{
    if (devIndex >= MAX_AUDIO_DEVICES) return usb::XFER_ERROR;
    AudioDevice* dev = &s_devices[devIndex];
    if (!dev->active || !dev->capture.streaming) return usb::XFER_ERROR;

    return usb::hci::bulk_transfer(dev->usbAddress,
                                   dev->capture.endpointAddr,
                                   pcmData, maxLen, bytesRead);
}

} // namespace usb_audio
} // namespace kernel
