// USB Video Class (UVC) Driver
//
// Supports:
//   - Video Control interface (camera terminal, processing unit)
//   - Video Streaming interface (probe/commit, frame capture)
//   - Format negotiation (MJPEG, uncompressed YUY2)
//   - Webcams and basic video capture devices
//
// Reference: USB Video Class 1.1 (UVC 1.1)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_VIDEO_H
#define KERNEL_USB_VIDEO_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_video {

// ================================================================
// UVC descriptor subtypes — Video Control
// ================================================================

enum VCDescSubtype : uint8_t {
    VC_HEADER            = 0x01,
    VC_INPUT_TERMINAL    = 0x02,
    VC_OUTPUT_TERMINAL   = 0x03,
    VC_SELECTOR_UNIT     = 0x04,
    VC_PROCESSING_UNIT   = 0x05,
    VC_EXTENSION_UNIT    = 0x06,
};

// ================================================================
// UVC descriptor subtypes — Video Streaming
// ================================================================

enum VSDescSubtype : uint8_t {
    VS_INPUT_HEADER      = 0x01,
    VS_OUTPUT_HEADER     = 0x02,
    VS_STILL_IMAGE       = 0x03,
    VS_FORMAT_UNCOMPRESSED = 0x04,
    VS_FRAME_UNCOMPRESSED  = 0x05,
    VS_FORMAT_MJPEG      = 0x06,
    VS_FRAME_MJPEG       = 0x07,
    VS_COLOR_FORMAT      = 0x0D,
};

// ================================================================
// UVC class-specific requests
// ================================================================

enum UVCRequest : uint8_t {
    UVC_SET_CUR  = 0x01,
    UVC_GET_CUR  = 0x81,
    UVC_GET_MIN  = 0x82,
    UVC_GET_MAX  = 0x83,
    UVC_GET_RES  = 0x84,
    UVC_GET_DEF  = 0x87,
};

// Video Streaming interface control selectors
enum VSControlSelector : uint8_t {
    VS_PROBE_CONTROL  = 0x01,
    VS_COMMIT_CONTROL = 0x02,
};

// ================================================================
// Video format types
// ================================================================

enum VideoFormatType : uint8_t {
    FORMAT_UNCOMPRESSED = 0,   // YUY2, NV12, etc.
    FORMAT_MJPEG        = 1,
};

// ================================================================
// Probe / Commit control structure (26 bytes, UVC 1.1)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define UVC_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define UVC_PACKED
#endif

struct ProbeCommitControl {
    uint16_t bmHint;
    uint8_t  bFormatIndex;
    uint8_t  bFrameIndex;
    uint32_t dwFrameInterval;     // 100 ns units (e.g. 333333 = 30 fps)
    uint16_t wKeyFrameRate;
    uint16_t wPFrameRate;
    uint16_t wCompQuality;
    uint16_t wCompWindowSize;
    uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize;
    uint32_t dwMaxPayloadTransferSize;
} UVC_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef UVC_PACKED

// ================================================================
// Frame descriptor (parsed from device)
// ================================================================

struct FrameDescriptor {
    uint16_t width;
    uint16_t height;
    uint32_t minFrameInterval;    // 100 ns
    uint32_t maxFrameInterval;
    uint32_t defaultFrameInterval;
    uint8_t  frameIndex;
};

static const uint8_t MAX_FRAME_DESCS = 8;

// ================================================================
// Format descriptor (parsed from device)
// ================================================================

struct FormatDescriptor {
    VideoFormatType type;
    uint8_t         formatIndex;
    uint8_t         bitsPerPixel;
    uint8_t         numFrames;
    FrameDescriptor frames[MAX_FRAME_DESCS];
};

static const uint8_t MAX_FORMAT_DESCS = 4;

// ================================================================
// Video device instance
// ================================================================

struct VideoDevice {
    bool               active;
    uint8_t            usbAddress;
    uint8_t            controlInterface;
    uint8_t            streamInterface;
    uint8_t            streamAltSetting;
    uint8_t            isoEndpoint;
    uint16_t           isoMaxPacket;

    FormatDescriptor   formats[MAX_FORMAT_DESCS];
    uint8_t            formatCount;

    ProbeCommitControl negotiated;
    bool               streaming;

    uint32_t           frameWidth;
    uint32_t           frameHeight;
    uint32_t           fps;
};

static const uint8_t MAX_VIDEO_DEVICES = 2;

// ================================================================
// Public API
// ================================================================

// Initialise the video class driver.
void init();

// Probe a USB device for UVC interfaces. Returns true if claimed.
bool probe(uint8_t usbAddress);

// Release video interfaces on a device (on detach).
void release(uint8_t usbAddress);

// Return number of active video devices.
uint8_t device_count();

// Get device info by index.
const VideoDevice* get_device(uint8_t index);

// ----------------------------------------------------------------
// Format negotiation
// ----------------------------------------------------------------

// Negotiate format and frame size with the device.
// formatIndex / frameIndex come from the parsed descriptors.
usb::TransferStatus negotiate(uint8_t devIndex,
                               uint8_t formatIndex,
                               uint8_t frameIndex,
                               uint32_t frameInterval);

// ----------------------------------------------------------------
// Streaming control
// ----------------------------------------------------------------

// Start video capture.
usb::TransferStatus start_capture(uint8_t devIndex);

// Stop video capture.
usb::TransferStatus stop_capture(uint8_t devIndex);

// Read a frame payload (may require multiple calls for large frames).
// Returns XFER_SUCCESS when data is available.
usb::TransferStatus read_frame(uint8_t devIndex,
                                void* buffer,
                                uint32_t maxLen,
                                uint32_t* bytesRead);

} // namespace usb_video
} // namespace kernel

#endif // KERNEL_USB_VIDEO_H
