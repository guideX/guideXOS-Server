// USB Video Class (UVC) Enhanced Driver
//
// Extends the existing UVC 1.1 driver with:
//
//   - UVC 1.5 extension controls
//   - H.264 payload format support (payload type 0x0C)
//   - Still image capture (method 1: VS_STILL_IMAGE)
//   - Camera Terminal controls (pan, tilt, zoom, focus, exposure,
//     iris, roll, auto-exposure modes, auto-focus)
//   - Processing Unit controls (brightness, contrast, hue,
//     saturation, sharpness, gamma, white balance, backlight comp,
//     gain, power line frequency, digital multiplier)
//   - Encoding Unit controls (for H.264 streams)
//
// Reference: USB Video Class 1.5 Specification
//            USB Video Payload H.264 Specification
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_VIDEO_UVC_H
#define KERNEL_USB_VIDEO_UVC_H

#include "kernel/types.h"
#include "kernel/usb.h"
#include "kernel/usb_video.h"

namespace kernel {
namespace usb_video_uvc {

// ================================================================
// UVC 1.5 additional descriptor subtypes
// ================================================================

enum UVC15DescSubtype : uint8_t {
    VC_ENCODING_UNIT     = 0x07,
};

// ================================================================
// Camera Terminal Control Selectors
// ================================================================

enum CameraTerminalCS : uint8_t {
    CT_SCANNING_MODE_CONTROL       = 0x01,
    CT_AE_MODE_CONTROL             = 0x02,
    CT_AE_PRIORITY_CONTROL         = 0x03,
    CT_EXPOSURE_TIME_ABS_CONTROL   = 0x04,
    CT_EXPOSURE_TIME_REL_CONTROL   = 0x05,
    CT_FOCUS_ABS_CONTROL           = 0x06,
    CT_FOCUS_REL_CONTROL           = 0x07,
    CT_IRIS_ABS_CONTROL            = 0x09,
    CT_IRIS_REL_CONTROL            = 0x0A,
    CT_ZOOM_ABS_CONTROL            = 0x0B,
    CT_ZOOM_REL_CONTROL            = 0x0C,
    CT_PANTILT_ABS_CONTROL         = 0x0D,
    CT_PANTILT_REL_CONTROL         = 0x0E,
    CT_ROLL_ABS_CONTROL            = 0x0F,
    CT_ROLL_REL_CONTROL            = 0x10,
    CT_FOCUS_AUTO_CONTROL          = 0x12,
    CT_PRIVACY_CONTROL             = 0x13,
    CT_FOCUS_SIMPLE_CONTROL        = 0x14,  // UVC 1.5
    CT_WINDOW_CONTROL              = 0x15,  // UVC 1.5
    CT_REGION_OF_INTEREST_CONTROL  = 0x16,  // UVC 1.5
};

// ================================================================
// Processing Unit Control Selectors
// ================================================================

enum ProcessingUnitCS : uint8_t {
    PU_BACKLIGHT_COMP_CONTROL      = 0x01,
    PU_BRIGHTNESS_CONTROL          = 0x02,
    PU_CONTRAST_CONTROL            = 0x03,
    PU_GAIN_CONTROL                = 0x04,
    PU_POWER_LINE_FREQ_CONTROL     = 0x05,
    PU_HUE_CONTROL                 = 0x06,
    PU_SATURATION_CONTROL          = 0x07,
    PU_SHARPNESS_CONTROL           = 0x08,
    PU_GAMMA_CONTROL               = 0x09,
    PU_WHITE_BALANCE_TEMP_CONTROL  = 0x0A,
    PU_WHITE_BALANCE_TEMP_AUTO     = 0x0B,
    PU_WHITE_BALANCE_COMP_CONTROL  = 0x0C,
    PU_WHITE_BALANCE_COMP_AUTO     = 0x0D,
    PU_DIGITAL_MULT_CONTROL        = 0x0E,
    PU_DIGITAL_MULT_LIMIT_CONTROL  = 0x0F,
    PU_HUE_AUTO_CONTROL            = 0x10,
    PU_ANALOG_VIDEO_STANDARD       = 0x11,
    PU_ANALOG_LOCK_STATUS          = 0x12,
    PU_CONTRAST_AUTO_CONTROL       = 0x13, // UVC 1.5
};

// ================================================================
// Auto-Exposure modes (CT_AE_MODE_CONTROL)
// ================================================================

enum AutoExposureMode : uint8_t {
    AE_MODE_MANUAL         = 0x01,
    AE_MODE_AUTO           = 0x02,
    AE_MODE_SHUTTER_PRIO   = 0x04,
    AE_MODE_APERTURE_PRIO  = 0x08,
};

// ================================================================
// H.264 payload format types
// ================================================================

enum H264PayloadType : uint8_t {
    H264_BASELINE  = 0x42,
    H264_MAIN      = 0x4D,
    H264_HIGH      = 0x64,
};

// ================================================================
// Still image capture method
// ================================================================

enum StillCaptureMethod : uint8_t {
    STILL_NONE         = 0,
    STILL_METHOD_1     = 1,  // Device supports dedicated still pipe
    STILL_METHOD_2     = 2,  // Device sends still via bulk endpoint
    STILL_METHOD_3     = 3,  // Device sends still in video stream
};

// ================================================================
// UVC control value range
// ================================================================

struct ControlRange {
    int32_t min;
    int32_t max;
    int32_t res;      // step size
    int32_t def;      // default value
    int32_t current;
};

// ================================================================
// Camera controls state
// ================================================================

struct CameraControls {
    ControlRange brightness;
    ControlRange contrast;
    ControlRange saturation;
    ControlRange sharpness;
    ControlRange gamma;
    ControlRange hue;
    ControlRange whiteBalanceTemp;
    ControlRange backlightComp;
    ControlRange gain;
    ControlRange exposureTimeAbs;
    ControlRange focusAbs;
    ControlRange zoomAbs;
    int32_t      panCurrent;
    int32_t      tiltCurrent;
    int32_t      rollCurrent;
    uint8_t      autoExposureMode;
    bool         autoFocus;
    bool         autoWhiteBalance;
    uint8_t      powerLineFreq;  // 0=disabled, 1=50Hz, 2=60Hz, 3=auto
};

// ================================================================
// H.264 stream configuration
// ================================================================

struct H264Config {
    uint8_t  profile;         // H264PayloadType
    uint8_t  level;           // e.g. 31 = 3.1
    uint16_t width;
    uint16_t height;
    uint32_t maxBitrate;      // bits/sec
    uint32_t frameInterval;   // 100ns units
    uint8_t  sliceMode;       // 0=frame, 1=slice
    bool     iFrameOnly;
};

// ================================================================
// Enhanced UVC device instance
// ================================================================

struct UVCDevice {
    bool                active;
    uint8_t             usbAddress;
    uint8_t             controlInterface;
    uint8_t             streamInterface;
    uint8_t             cameraTerminalId;
    uint8_t             processingUnitId;
    uint8_t             encodingUnitId;    // 0 if none
    CameraControls      camera;
    StillCaptureMethod  stillMethod;
    bool                supportsH264;
    H264Config          h264Config;
    bool                streaming;
};

static const uint8_t MAX_UVC_DEVICES = 4;

// ================================================================
// Public API
// ================================================================

void init();

bool probe(uint8_t usbAddress);
void release(uint8_t usbAddress);
uint8_t device_count();
const UVCDevice* get_device(uint8_t index);

// ----------------------------------------------------------------
// Camera Terminal controls
// ----------------------------------------------------------------

usb::TransferStatus set_auto_exposure(uint8_t devIndex, AutoExposureMode mode);
usb::TransferStatus set_exposure_time(uint8_t devIndex, int32_t time100ns);
usb::TransferStatus set_focus(uint8_t devIndex, int32_t focusVal);
usb::TransferStatus set_auto_focus(uint8_t devIndex, bool enable);
usb::TransferStatus set_zoom(uint8_t devIndex, int32_t zoomVal);
usb::TransferStatus set_pan_tilt(uint8_t devIndex, int32_t pan, int32_t tilt);
usb::TransferStatus set_roll(uint8_t devIndex, int32_t rollVal);

// ----------------------------------------------------------------
// Processing Unit controls
// ----------------------------------------------------------------

usb::TransferStatus set_brightness(uint8_t devIndex, int32_t value);
usb::TransferStatus set_contrast(uint8_t devIndex, int32_t value);
usb::TransferStatus set_saturation(uint8_t devIndex, int32_t value);
usb::TransferStatus set_sharpness(uint8_t devIndex, int32_t value);
usb::TransferStatus set_gamma(uint8_t devIndex, int32_t value);
usb::TransferStatus set_hue(uint8_t devIndex, int32_t value);
usb::TransferStatus set_white_balance_temp(uint8_t devIndex, int32_t kelvin);
usb::TransferStatus set_white_balance_auto(uint8_t devIndex, bool enable);
usb::TransferStatus set_backlight_comp(uint8_t devIndex, int32_t value);
usb::TransferStatus set_gain(uint8_t devIndex, int32_t value);
usb::TransferStatus set_power_line_freq(uint8_t devIndex, uint8_t mode);

// ----------------------------------------------------------------
// Control range queries
// ----------------------------------------------------------------

bool get_control_range(uint8_t devIndex, ProcessingUnitCS control,
                       ControlRange* range);
bool get_camera_control_range(uint8_t devIndex, CameraTerminalCS control,
                              ControlRange* range);

// ----------------------------------------------------------------
// Still image capture
// ----------------------------------------------------------------

usb::TransferStatus trigger_still_capture(uint8_t devIndex);
usb::TransferStatus read_still_image(uint8_t devIndex,
                                      void* buffer,
                                      uint32_t maxLen,
                                      uint32_t* bytesRead);

} // namespace usb_video_uvc
} // namespace kernel

#endif // KERNEL_USB_VIDEO_UVC_H
