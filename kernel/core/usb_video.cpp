// USB Video Class (UVC) Driver — Implementation
//
// Probe/commit negotiation, format parsing, and frame capture.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_video.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_video {

// ================================================================
// Internal state
// ================================================================

static VideoDevice s_devices[MAX_VIDEO_DEVICES];
static uint8_t s_deviceCount = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// ================================================================
// UVC class-specific control requests
// ================================================================

static usb::TransferStatus uvc_get_cur(uint8_t addr, uint8_t iface,
                                       uint8_t cs,
                                       void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1; // Device-to-host, class, interface
    setup.bRequest      = UVC_GET_CUR;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

static usb::TransferStatus uvc_set_cur(uint8_t addr, uint8_t iface,
                                       uint8_t cs,
                                       const void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = UVC_SET_CUR;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup,
                                 const_cast<void*>(data), len);
}

static usb::TransferStatus uvc_get_max(uint8_t addr, uint8_t iface,
                                       uint8_t cs,
                                       void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = UVC_GET_MAX;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface;
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
// Parse video streaming descriptors
//
// This is a simplified parser that looks at the interface class
// and creates default format entries.  A full implementation would
// parse the class-specific VS descriptors from the raw config.
// ================================================================

static void parse_video_descriptors(VideoDevice* dev,
                                    const usb::Device* usbDev)
{
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] != usb::CLASS_VIDEO) continue;

        uint8_t subclass = usbDev->interfaceSubClass[i];

        if (subclass == 0x01) {
            // Video Control
            dev->controlInterface = i;
        }
        else if (subclass == 0x02) {
            // Video Streaming
            dev->streamInterface = i;
            dev->streamAltSetting = 1;

            // Find isochronous IN endpoint
            for (uint8_t e = 0; e < usb::MAX_ENDPOINTS * 2; ++e) {
                const usb::Endpoint& ep = usbDev->endpoints[e];
                if (ep.active &&
                    ep.type == usb::TRANSFER_ISOCHRONOUS &&
                    ep.dir == usb::DIR_DEVICE_TO_HOST) {
                    dev->isoEndpoint  = ep.address;
                    dev->isoMaxPacket = ep.maxPacketSize;
                    break;
                }
            }

            // Create default format descriptors
            if (dev->formatCount < MAX_FORMAT_DESCS) {
                FormatDescriptor& fmt = dev->formats[dev->formatCount];
                fmt.type        = FORMAT_MJPEG;
                fmt.formatIndex = 1;
                fmt.bitsPerPixel = 0; // compressed
                fmt.numFrames   = 3;

                // Common webcam resolutions
                if (fmt.numFrames > 0) {
                    fmt.frames[0].width  = 640;
                    fmt.frames[0].height = 480;
                    fmt.frames[0].frameIndex = 1;
                    fmt.frames[0].defaultFrameInterval = 333333; // 30 fps
                    fmt.frames[0].minFrameInterval = 333333;
                    fmt.frames[0].maxFrameInterval = 2000000; // 5 fps
                }
                if (fmt.numFrames > 1) {
                    fmt.frames[1].width  = 320;
                    fmt.frames[1].height = 240;
                    fmt.frames[1].frameIndex = 2;
                    fmt.frames[1].defaultFrameInterval = 333333;
                    fmt.frames[1].minFrameInterval = 333333;
                    fmt.frames[1].maxFrameInterval = 2000000;
                }
                if (fmt.numFrames > 2) {
                    fmt.frames[2].width  = 1280;
                    fmt.frames[2].height = 720;
                    fmt.frames[2].frameIndex = 3;
                    fmt.frames[2].defaultFrameInterval = 333333;
                    fmt.frames[2].minFrameInterval = 333333;
                    fmt.frames[2].maxFrameInterval = 2000000;
                }
                dev->formatCount++;
            }

            // Uncompressed YUY2 format
            if (dev->formatCount < MAX_FORMAT_DESCS) {
                FormatDescriptor& fmt = dev->formats[dev->formatCount];
                fmt.type        = FORMAT_UNCOMPRESSED;
                fmt.formatIndex = 2;
                fmt.bitsPerPixel = 16; // YUY2
                fmt.numFrames   = 2;

                if (fmt.numFrames > 0) {
                    fmt.frames[0].width  = 640;
                    fmt.frames[0].height = 480;
                    fmt.frames[0].frameIndex = 1;
                    fmt.frames[0].defaultFrameInterval = 333333;
                    fmt.frames[0].minFrameInterval = 333333;
                    fmt.frames[0].maxFrameInterval = 2000000;
                }
                if (fmt.numFrames > 1) {
                    fmt.frames[1].width  = 320;
                    fmt.frames[1].height = 240;
                    fmt.frames[1].frameIndex = 2;
                    fmt.frames[1].defaultFrameInterval = 333333;
                    fmt.frames[1].minFrameInterval = 333333;
                    fmt.frames[1].maxFrameInterval = 2000000;
                }
                dev->formatCount++;
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

    bool hasVideo = false;
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] == usb::CLASS_VIDEO) {
            hasVideo = true;
            break;
        }
    }
    if (!hasVideo) return false;
    if (s_deviceCount >= MAX_VIDEO_DEVICES) return false;

    VideoDevice* dev = &s_devices[s_deviceCount];
    memzero(dev, sizeof(VideoDevice));
    dev->usbAddress = usbAddress;

    parse_video_descriptors(dev, usbDev);

    dev->active = true;
    s_deviceCount++;
    return true;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_VIDEO_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            if (s_devices[i].streaming) {
                set_interface(usbAddress, s_devices[i].streamInterface, 0);
            }
            s_devices[i].active = false;
            s_deviceCount--;
        }
    }
}

uint8_t device_count() { return s_deviceCount; }

const VideoDevice* get_device(uint8_t index)
{
    if (index >= MAX_VIDEO_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

// ----------------------------------------------------------------
// Format negotiation (Probe / Commit)
// ----------------------------------------------------------------

usb::TransferStatus negotiate(uint8_t devIndex,
                               uint8_t formatIndex,
                               uint8_t frameIndex,
                               uint32_t frameInterval)
{
    if (devIndex >= MAX_VIDEO_DEVICES) return usb::XFER_ERROR;
    VideoDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    ProbeCommitControl probe;
    memzero(&probe, sizeof(probe));
    probe.bmHint         = 0x0001; // dwFrameInterval field is valid
    probe.bFormatIndex   = formatIndex;
    probe.bFrameIndex    = frameIndex;
    probe.dwFrameInterval = frameInterval;

    // SET_CUR on VS_PROBE_CONTROL
    usb::TransferStatus st = uvc_set_cur(dev->usbAddress,
                                          dev->streamInterface,
                                          VS_PROBE_CONTROL,
                                          &probe, sizeof(probe));
    if (st != usb::XFER_SUCCESS) return st;

    // GET_CUR on VS_PROBE_CONTROL to get device's negotiated values
    st = uvc_get_cur(dev->usbAddress, dev->streamInterface,
                     VS_PROBE_CONTROL,
                     &probe, sizeof(probe));
    if (st != usb::XFER_SUCCESS) return st;

    // SET_CUR on VS_COMMIT_CONTROL to accept
    st = uvc_set_cur(dev->usbAddress, dev->streamInterface,
                     VS_COMMIT_CONTROL,
                     &probe, sizeof(probe));
    if (st != usb::XFER_SUCCESS) return st;

    dev->negotiated = probe;

    // Extract resolution from the format/frame descriptors
    for (uint8_t f = 0; f < dev->formatCount; ++f) {
        if (dev->formats[f].formatIndex == formatIndex) {
            for (uint8_t fr = 0; fr < dev->formats[f].numFrames; ++fr) {
                if (dev->formats[f].frames[fr].frameIndex == frameIndex) {
                    dev->frameWidth  = dev->formats[f].frames[fr].width;
                    dev->frameHeight = dev->formats[f].frames[fr].height;
                    break;
                }
            }
            break;
        }
    }

    // Calculate FPS from frame interval (100ns units)
    if (frameInterval > 0) {
        dev->fps = 10000000u / frameInterval;
    }

    return usb::XFER_SUCCESS;
}

// ----------------------------------------------------------------
// Streaming control
// ----------------------------------------------------------------

usb::TransferStatus start_capture(uint8_t devIndex)
{
    if (devIndex >= MAX_VIDEO_DEVICES) return usb::XFER_ERROR;
    VideoDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = set_interface(dev->usbAddress,
                                           dev->streamInterface,
                                           dev->streamAltSetting);
    if (st == usb::XFER_SUCCESS) {
        dev->streaming = true;
    }
    return st;
}

usb::TransferStatus stop_capture(uint8_t devIndex)
{
    if (devIndex >= MAX_VIDEO_DEVICES) return usb::XFER_ERROR;
    VideoDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = set_interface(dev->usbAddress,
                                           dev->streamInterface, 0);
    if (st == usb::XFER_SUCCESS) {
        dev->streaming = false;
    }
    return st;
}

usb::TransferStatus read_frame(uint8_t devIndex,
                                void* buffer,
                                uint32_t maxLen,
                                uint32_t* bytesRead)
{
    if (devIndex >= MAX_VIDEO_DEVICES) return usb::XFER_ERROR;
    VideoDevice* dev = &s_devices[devIndex];
    if (!dev->active || !dev->streaming) return usb::XFER_ERROR;

    // Read from isochronous endpoint (using bulk path as placeholder)
    uint16_t chunk = (maxLen > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(maxLen);
    uint16_t recvd = 0;

    usb::TransferStatus st = usb::hci::bulk_transfer(
        dev->usbAddress, dev->isoEndpoint,
        buffer, chunk, &recvd);

    if (bytesRead) *bytesRead = recvd;
    return st;
}

} // namespace usb_video
} // namespace kernel
