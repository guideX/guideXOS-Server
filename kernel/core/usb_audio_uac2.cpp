// USB Audio Class 2.0 (UAC2) Driver — Implementation
//
// Handles UAC2 clock source management, high-bandwidth isochronous
// endpoints, extended feature unit controls, and streaming.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_audio_uac2.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_audio_uac2 {

// ================================================================
// Internal state
// ================================================================

static UAC2Device s_devices[MAX_UAC2_DEVICES];
static uint8_t    s_deviceCount = 0;

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
// UAC2 class-specific control requests
//
// UAC2 uses CUR (current) and RANGE requests instead of the
// UAC1 GET_CUR/SET_CUR/GET_MIN/GET_MAX/GET_RES family.
// ================================================================

static usb::TransferStatus uac2_get_cur(uint8_t addr, uint8_t iface,
                                         uint8_t entityId, uint8_t cs,
                                         void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1; // Device-to-host, class, interface
    setup.bRequest      = UAC2_CUR;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = static_cast<uint16_t>((entityId << 8) | iface);
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

static usb::TransferStatus uac2_set_cur(uint8_t addr, uint8_t iface,
                                         uint8_t entityId, uint8_t cs,
                                         const void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = UAC2_CUR;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = static_cast<uint16_t>((entityId << 8) | iface);
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup,
                                 const_cast<void*>(data), len);
}

static usb::TransferStatus uac2_get_range(uint8_t addr, uint8_t iface,
                                           uint8_t entityId, uint8_t cs,
                                           void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = UAC2_RANGE;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = static_cast<uint16_t>((entityId << 8) | iface);
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

// ================================================================
// Set interface alternate setting
// ================================================================

static usb::TransferStatus set_interface(uint8_t addr,
                                         uint8_t iface,
                                         uint8_t altSetting)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x01;
    setup.bRequest      = usb::REQ_SET_INTERFACE;
    setup.wValue        = altSetting;
    setup.wIndex        = iface;
    setup.wLength       = 0;
    return usb::control_transfer(addr, &setup, nullptr, 0);
}

// ================================================================
// Clock source management
// ================================================================

static usb::TransferStatus read_clock_frequency(uint8_t addr,
                                                 uint8_t iface,
                                                 uint8_t clockId,
                                                 uint32_t* freq)
{
    return uac2_get_cur(addr, iface, clockId,
                        CS_SAM_FREQ_CONTROL, freq, 4);
}

static usb::TransferStatus write_clock_frequency(uint8_t addr,
                                                  uint8_t iface,
                                                  uint8_t clockId,
                                                  uint32_t freq)
{
    return uac2_set_cur(addr, iface, clockId,
                        CS_SAM_FREQ_CONTROL, &freq, 4);
}

static usb::TransferStatus read_clock_valid(uint8_t addr,
                                             uint8_t iface,
                                             uint8_t clockId,
                                             bool* valid)
{
    uint8_t val = 0;
    usb::TransferStatus st = uac2_get_cur(addr, iface, clockId,
                                           CS_CLOCK_VALID_CONTROL, &val, 1);
    if (st == usb::XFER_SUCCESS) *valid = (val != 0);
    return st;
}

static usb::TransferStatus read_clock_ranges(uint8_t addr,
                                              uint8_t iface,
                                              uint8_t clockId,
                                              ClockSource* clk)
{
    // UAC2 RANGE response for SAM_FREQ_CONTROL:
    //   wNumSubRanges (2 bytes) + N * {dMIN(4), dMAX(4), dRES(4)}
    uint8_t buf[2 + MAX_RATE_RANGES * 12];
    uint16_t maxLen = sizeof(buf);

    usb::TransferStatus st = uac2_get_range(addr, iface, clockId,
                                             CS_SAM_FREQ_CONTROL,
                                             buf, maxLen);
    if (st != usb::XFER_SUCCESS) return st;

    uint16_t numRanges = static_cast<uint16_t>(buf[0]) |
                         (static_cast<uint16_t>(buf[1]) << 8);
    if (numRanges > MAX_RATE_RANGES) numRanges = MAX_RATE_RANGES;

    clk->numRanges = static_cast<uint8_t>(numRanges);
    for (uint8_t i = 0; i < numRanges; ++i) {
        uint32_t off = 2 + i * 12;
        clk->rates[i].min = static_cast<uint32_t>(buf[off])     |
                            (static_cast<uint32_t>(buf[off+1]) << 8) |
                            (static_cast<uint32_t>(buf[off+2]) << 16)|
                            (static_cast<uint32_t>(buf[off+3]) << 24);
        clk->rates[i].max = static_cast<uint32_t>(buf[off+4])   |
                            (static_cast<uint32_t>(buf[off+5]) << 8) |
                            (static_cast<uint32_t>(buf[off+6]) << 16)|
                            (static_cast<uint32_t>(buf[off+7]) << 24);
        clk->rates[i].res = static_cast<uint32_t>(buf[off+8])   |
                            (static_cast<uint32_t>(buf[off+9]) << 8) |
                            (static_cast<uint32_t>(buf[off+10]) << 16)|
                            (static_cast<uint32_t>(buf[off+11]) << 24);
    }

    return usb::XFER_SUCCESS;
}

// ================================================================
// Volume range query (UAC2 uses RANGE request)
// ================================================================

static usb::TransferStatus read_volume_range(uint8_t addr, uint8_t iface,
                                              uint8_t featureId,
                                              int16_t* minVal,
                                              int16_t* maxVal,
                                              int16_t* resVal)
{
    // RANGE response: wNumSubRanges(2) + N * {wMIN(2), wMAX(2), wRES(2)}
    uint8_t buf[2 + 6]; // just one sub-range
    usb::TransferStatus st = uac2_get_range(addr, iface, featureId,
                                             UAC2_FU_VOLUME_CONTROL,
                                             buf, sizeof(buf));
    if (st != usb::XFER_SUCCESS) return st;

    *minVal = static_cast<int16_t>(buf[2] | (buf[3] << 8));
    *maxVal = static_cast<int16_t>(buf[4] | (buf[5] << 8));
    *resVal = static_cast<int16_t>(buf[6] | (buf[7] << 8));
    return usb::XFER_SUCCESS;
}

// ================================================================
// Parse UAC2 descriptors
// ================================================================

static void parse_uac2_descriptors(UAC2Device* dev,
                                    const usb::Device* usbDev)
{
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] != usb::CLASS_AUDIO) continue;

        uint8_t subclass = usbDev->interfaceSubClass[i];

        if (subclass == UAC2_SC_AUDIOCONTROL) {
            dev->controlInterface = i;
            dev->featureUnitId = 2; // typical; real parser reads descriptors

            // Default clock source ID (typically entity 1)
            dev->clockSource.clockId   = 1;
            dev->clockSource.attributes = CLOCK_ATTR_INT_FIXED;
            dev->clockSource.valid     = true;
        }
        else if (subclass == UAC2_SC_AUDIOSTREAMING) {
            for (uint8_t e = 0; e < usb::MAX_ENDPOINTS * 2; ++e) {
                const usb::Endpoint& ep = usbDev->endpoints[e];
                if (!ep.active || ep.type != usb::TRANSFER_ISOCHRONOUS) continue;

                UAC2Stream* stream = nullptr;
                if (ep.dir == usb::DIR_HOST_TO_DEVICE) {
                    stream = &dev->playback;
                    stream->direction = 0;
                } else {
                    stream = &dev->capture;
                    stream->direction = 1;
                }

                stream->interfaceNum  = i;
                stream->altSetting    = 1;
                stream->endpointAddr  = ep.address;
                stream->maxPacketSize = ep.maxPacketSize;
                stream->clockSourceId = dev->clockSource.clockId;
                stream->active        = true;
                stream->streaming     = false;

                // Default: 24-bit stereo PCM, 48 kHz
                stream->formatType     = UAC2_FORMAT_TYPE_I;
                stream->formats        = UAC2_PCM;
                stream->channels       = 2;
                stream->bitResolution  = 24;
                stream->subSlotSize    = 3;
                stream->currentSampleRate = 48000;
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

    // Check for UAC2: look for Audio class with bcdADC >= 0x0200
    // Simplified: just check for Audio class interfaces
    bool hasAudio = false;
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] == usb::CLASS_AUDIO) {
            hasAudio = true;
            break;
        }
    }
    if (!hasAudio) return false;
    if (s_deviceCount >= MAX_UAC2_DEVICES) return false;

    UAC2Device* dev = &s_devices[s_deviceCount];
    memzero(dev, sizeof(UAC2Device));
    dev->usbAddress = usbAddress;

    parse_uac2_descriptors(dev, usbDev);

    // Read clock source info
    if (dev->clockSource.valid) {
        read_clock_frequency(usbAddress, dev->controlInterface,
                             dev->clockSource.clockId,
                             &dev->clockSource.currentFreq);

        read_clock_valid(usbAddress, dev->controlInterface,
                         dev->clockSource.clockId,
                         &dev->clockSource.clockValid);

        read_clock_ranges(usbAddress, dev->controlInterface,
                          dev->clockSource.clockId,
                          &dev->clockSource);
    }

    // Read volume range
    if (dev->featureUnitId != 0) {
        read_volume_range(usbAddress, dev->controlInterface,
                          dev->featureUnitId,
                          &dev->volumeMin, &dev->volumeMax,
                          &dev->volumeRes);

        uac2_get_cur(usbAddress, dev->controlInterface,
                     dev->featureUnitId, UAC2_FU_VOLUME_CONTROL,
                     &dev->volumeCurrent, 2);

        uint8_t muteVal = 0;
        uac2_get_cur(usbAddress, dev->controlInterface,
                     dev->featureUnitId, UAC2_FU_MUTE_CONTROL,
                     &muteVal, 1);
        dev->muted = (muteVal != 0);
    }

    dev->active = true;
    ++s_deviceCount;
    return true;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_UAC2_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            if (s_devices[i].playback.streaming) {
                set_interface(usbAddress, s_devices[i].playback.interfaceNum, 0);
            }
            if (s_devices[i].capture.streaming) {
                set_interface(usbAddress, s_devices[i].capture.interfaceNum, 0);
            }
            s_devices[i].active = false;
            --s_deviceCount;
        }
    }
}

uint8_t device_count() { return s_deviceCount; }

const UAC2Device* get_device(uint8_t index)
{
    if (index >= MAX_UAC2_DEVICES || !s_devices[index].active) return nullptr;
    return &s_devices[index];
}

// ----------------------------------------------------------------
// Clock management
// ----------------------------------------------------------------

uint32_t get_clock_frequency(uint8_t devIndex)
{
    if (devIndex >= MAX_UAC2_DEVICES) return 0;
    return s_devices[devIndex].clockSource.currentFreq;
}

usb::TransferStatus set_clock_frequency(uint8_t devIndex, uint32_t freq)
{
    if (devIndex >= MAX_UAC2_DEVICES) return usb::XFER_ERROR;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = write_clock_frequency(
        dev->usbAddress, dev->controlInterface,
        dev->clockSource.clockId, freq);

    if (st == usb::XFER_SUCCESS) {
        dev->clockSource.currentFreq = freq;
        // Update stream sample rates
        if (dev->playback.active) dev->playback.currentSampleRate = freq;
        if (dev->capture.active)  dev->capture.currentSampleRate  = freq;
    }
    return st;
}

bool is_clock_valid(uint8_t devIndex)
{
    if (devIndex >= MAX_UAC2_DEVICES) return false;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active) return false;

    bool valid = false;
    read_clock_valid(dev->usbAddress, dev->controlInterface,
                     dev->clockSource.clockId, &valid);
    dev->clockSource.clockValid = valid;
    return valid;
}

uint8_t get_rate_ranges(uint8_t devIndex, const SampleRateRange** ranges)
{
    if (devIndex >= MAX_UAC2_DEVICES || !ranges) return 0;
    *ranges = s_devices[devIndex].clockSource.rates;
    return s_devices[devIndex].clockSource.numRanges;
}

// ----------------------------------------------------------------
// Volume / mute controls
// ----------------------------------------------------------------

int16_t get_volume(uint8_t devIndex)
{
    if (devIndex >= MAX_UAC2_DEVICES) return 0;
    return s_devices[devIndex].volumeCurrent;
}

usb::TransferStatus set_volume(uint8_t devIndex, int16_t volume)
{
    if (devIndex >= MAX_UAC2_DEVICES) return usb::XFER_ERROR;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    volume = clamp16(volume, dev->volumeMin, dev->volumeMax);

    usb::TransferStatus st = uac2_set_cur(dev->usbAddress,
                                           dev->controlInterface,
                                           dev->featureUnitId,
                                           UAC2_FU_VOLUME_CONTROL,
                                           &volume, 2);
    if (st == usb::XFER_SUCCESS) dev->volumeCurrent = volume;
    return st;
}

bool get_mute(uint8_t devIndex)
{
    if (devIndex >= MAX_UAC2_DEVICES) return false;
    return s_devices[devIndex].muted;
}

usb::TransferStatus set_mute(uint8_t devIndex, bool mute)
{
    if (devIndex >= MAX_UAC2_DEVICES) return usb::XFER_ERROR;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint8_t val = mute ? 1 : 0;
    usb::TransferStatus st = uac2_set_cur(dev->usbAddress,
                                           dev->controlInterface,
                                           dev->featureUnitId,
                                           UAC2_FU_MUTE_CONTROL,
                                           &val, 1);
    if (st == usb::XFER_SUCCESS) dev->muted = mute;
    return st;
}

// ----------------------------------------------------------------
// Streaming control
// ----------------------------------------------------------------

usb::TransferStatus start_stream(uint8_t devIndex, uint8_t direction)
{
    if (devIndex >= MAX_UAC2_DEVICES) return usb::XFER_ERROR;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    UAC2Stream* stream = (direction == 0) ? &dev->playback : &dev->capture;
    if (!stream->active) return usb::XFER_NOT_SUPPORTED;

    usb::TransferStatus st = set_interface(dev->usbAddress,
                                           stream->interfaceNum,
                                           stream->altSetting);
    if (st == usb::XFER_SUCCESS) {
        stream->streaming = true;
    }
    return st;
}

usb::TransferStatus stop_stream(uint8_t devIndex, uint8_t direction)
{
    if (devIndex >= MAX_UAC2_DEVICES) return usb::XFER_ERROR;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    UAC2Stream* stream = (direction == 0) ? &dev->playback : &dev->capture;
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
    if (devIndex >= MAX_UAC2_DEVICES) return usb::XFER_ERROR;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active || !dev->playback.streaming) return usb::XFER_ERROR;

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
    if (devIndex >= MAX_UAC2_DEVICES) return usb::XFER_ERROR;
    UAC2Device* dev = &s_devices[devIndex];
    if (!dev->active || !dev->capture.streaming) return usb::XFER_ERROR;

    return usb::hci::bulk_transfer(dev->usbAddress,
                                   dev->capture.endpointAddr,
                                   pcmData, maxLen, bytesRead);
}

} // namespace usb_audio_uac2
} // namespace kernel
