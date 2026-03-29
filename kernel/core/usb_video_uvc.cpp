// USB Video Class (UVC) Enhanced Driver — Implementation
//
// Camera terminal controls, processing unit controls, still image
// capture, and H.264 payload format support.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_video_uvc.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_video_uvc {

// ================================================================
// Internal state
// ================================================================

static UVCDevice s_devices[MAX_UVC_DEVICES];
static uint8_t   s_deviceCount = 0;

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

static usb::TransferStatus uvc_get_cur(uint8_t addr, uint16_t iface_entity,
                                        uint8_t cs,
                                        void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = usb_video::UVC_GET_CUR;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface_entity;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

static usb::TransferStatus uvc_set_cur(uint8_t addr, uint16_t iface_entity,
                                        uint8_t cs,
                                        const void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21;
    setup.bRequest      = usb_video::UVC_SET_CUR;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface_entity;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup,
                                 const_cast<void*>(data), len);
}

static usb::TransferStatus uvc_get_min(uint8_t addr, uint16_t iface_entity,
                                        uint8_t cs,
                                        void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = usb_video::UVC_GET_MIN;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface_entity;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

static usb::TransferStatus uvc_get_max(uint8_t addr, uint16_t iface_entity,
                                        uint8_t cs,
                                        void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = usb_video::UVC_GET_MAX;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface_entity;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

static usb::TransferStatus uvc_get_res(uint8_t addr, uint16_t iface_entity,
                                        uint8_t cs,
                                        void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = usb_video::UVC_GET_RES;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface_entity;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

static usb::TransferStatus uvc_get_def(uint8_t addr, uint16_t iface_entity,
                                        uint8_t cs,
                                        void* data, uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = usb_video::UVC_GET_DEF;
    setup.wValue        = static_cast<uint16_t>(cs << 8);
    setup.wIndex        = iface_entity;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, data, len);
}

// Build wIndex for entity requests: (entityId << 8) | interfaceNum
static uint16_t make_entity_index(uint8_t entityId, uint8_t iface)
{
    return static_cast<uint16_t>((entityId << 8) | iface);
}

// ================================================================
// Query a full control range (min/max/res/def/cur)
// ================================================================

static bool query_control_range_16(uint8_t addr, uint16_t wIndex,
                                    uint8_t cs, ControlRange* range)
{
    int16_t val16 = 0;
    usb::TransferStatus st;

    st = uvc_get_min(addr, wIndex, cs, &val16, 2);
    if (st != usb::XFER_SUCCESS) return false;
    range->min = val16;

    st = uvc_get_max(addr, wIndex, cs, &val16, 2);
    if (st != usb::XFER_SUCCESS) return false;
    range->max = val16;

    st = uvc_get_res(addr, wIndex, cs, &val16, 2);
    if (st != usb::XFER_SUCCESS) return false;
    range->res = val16;

    st = uvc_get_def(addr, wIndex, cs, &val16, 2);
    if (st != usb::XFER_SUCCESS) return false;
    range->def = val16;

    st = uvc_get_cur(addr, wIndex, cs, &val16, 2);
    if (st != usb::XFER_SUCCESS) return false;
    range->current = val16;

    return true;
}

// ================================================================
// Generic control set (2-byte value)
// ================================================================

static usb::TransferStatus set_control_16(uint8_t addr, uint16_t wIndex,
                                           uint8_t cs, int16_t value)
{
    return uvc_set_cur(addr, wIndex, cs, &value, 2);
}

// ================================================================
// Parse UVC descriptors for enhanced features
// ================================================================

static void parse_uvc_descriptors(UVCDevice* dev,
                                   const usb::Device* usbDev)
{
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] != usb::CLASS_VIDEO) continue;

        uint8_t subclass = usbDev->interfaceSubClass[i];

        if (subclass == 0x01) {
            // Video Control
            dev->controlInterface = i;
            // Typical entity IDs (would be parsed from CS descriptors)
            dev->cameraTerminalId = 1;
            dev->processingUnitId = 2;
            dev->encodingUnitId   = 0; // only present on H.264 devices
        }
        else if (subclass == 0x02) {
            // Video Streaming
            dev->streamInterface = i;
        }
    }
}

// ================================================================
// Read initial camera control values
// ================================================================

static void read_camera_controls(UVCDevice* dev)
{
    uint16_t ctIdx = make_entity_index(dev->cameraTerminalId,
                                        dev->controlInterface);
    uint16_t puIdx = make_entity_index(dev->processingUnitId,
                                        dev->controlInterface);

    query_control_range_16(dev->usbAddress, puIdx,
                           PU_BRIGHTNESS_CONTROL, &dev->camera.brightness);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_CONTRAST_CONTROL, &dev->camera.contrast);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_SATURATION_CONTROL, &dev->camera.saturation);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_SHARPNESS_CONTROL, &dev->camera.sharpness);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_GAMMA_CONTROL, &dev->camera.gamma);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_HUE_CONTROL, &dev->camera.hue);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_WHITE_BALANCE_TEMP_CONTROL,
                           &dev->camera.whiteBalanceTemp);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_BACKLIGHT_COMP_CONTROL, &dev->camera.backlightComp);
    query_control_range_16(dev->usbAddress, puIdx,
                           PU_GAIN_CONTROL, &dev->camera.gain);

    query_control_range_16(dev->usbAddress, ctIdx,
                           CT_EXPOSURE_TIME_ABS_CONTROL,
                           &dev->camera.exposureTimeAbs);
    query_control_range_16(dev->usbAddress, ctIdx,
                           CT_FOCUS_ABS_CONTROL, &dev->camera.focusAbs);
    query_control_range_16(dev->usbAddress, ctIdx,
                           CT_ZOOM_ABS_CONTROL, &dev->camera.zoomAbs);

    // Auto-exposure mode
    uint8_t aeMode = 0;
    uvc_get_cur(dev->usbAddress, ctIdx, CT_AE_MODE_CONTROL, &aeMode, 1);
    dev->camera.autoExposureMode = aeMode;

    // Auto-focus
    uint8_t afVal = 0;
    uvc_get_cur(dev->usbAddress, ctIdx, CT_FOCUS_AUTO_CONTROL, &afVal, 1);
    dev->camera.autoFocus = (afVal != 0);

    // Auto white balance
    uint8_t awbVal = 0;
    uvc_get_cur(dev->usbAddress, puIdx, PU_WHITE_BALANCE_TEMP_AUTO, &awbVal, 1);
    dev->camera.autoWhiteBalance = (awbVal != 0);

    // Power line frequency
    uint8_t plf = 0;
    uvc_get_cur(dev->usbAddress, puIdx, PU_POWER_LINE_FREQ_CONTROL, &plf, 1);
    dev->camera.powerLineFreq = plf;
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
    if (s_deviceCount >= MAX_UVC_DEVICES) return false;

    UVCDevice* dev = &s_devices[s_deviceCount];
    memzero(dev, sizeof(UVCDevice));
    dev->usbAddress = usbAddress;

    parse_uvc_descriptors(dev, usbDev);
    read_camera_controls(dev);

    dev->active = true;
    ++s_deviceCount;
    return true;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_UVC_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            s_devices[i].active = false;
            --s_deviceCount;
        }
    }
}

uint8_t device_count() { return s_deviceCount; }

const UVCDevice* get_device(uint8_t index)
{
    if (index >= MAX_UVC_DEVICES || !s_devices[index].active) return nullptr;
    return &s_devices[index];
}

// ----------------------------------------------------------------
// Camera Terminal controls
// ----------------------------------------------------------------

usb::TransferStatus set_auto_exposure(uint8_t devIndex, AutoExposureMode mode)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint8_t val = static_cast<uint8_t>(mode);
    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    usb::TransferStatus st = uvc_set_cur(dev->usbAddress, wIdx,
                                          CT_AE_MODE_CONTROL, &val, 1);
    if (st == usb::XFER_SUCCESS) dev->camera.autoExposureMode = val;
    return st;
}

usb::TransferStatus set_exposure_time(uint8_t devIndex, int32_t time100ns)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    uint32_t val = static_cast<uint32_t>(time100ns);
    usb::TransferStatus st = uvc_set_cur(dev->usbAddress, wIdx,
                                          CT_EXPOSURE_TIME_ABS_CONTROL, &val, 4);
    if (st == usb::XFER_SUCCESS) dev->camera.exposureTimeAbs.current = time100ns;
    return st;
}

usb::TransferStatus set_focus(uint8_t devIndex, int32_t focusVal)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          CT_FOCUS_ABS_CONTROL, static_cast<int16_t>(focusVal));
}

usb::TransferStatus set_auto_focus(uint8_t devIndex, bool enable)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint8_t val = enable ? 1 : 0;
    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    usb::TransferStatus st = uvc_set_cur(dev->usbAddress, wIdx,
                                          CT_FOCUS_AUTO_CONTROL, &val, 1);
    if (st == usb::XFER_SUCCESS) dev->camera.autoFocus = enable;
    return st;
}

usb::TransferStatus set_zoom(uint8_t devIndex, int32_t zoomVal)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          CT_ZOOM_ABS_CONTROL, static_cast<int16_t>(zoomVal));
}

usb::TransferStatus set_pan_tilt(uint8_t devIndex, int32_t pan, int32_t tilt)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    // Pan/tilt is an 8-byte control: pan(4) + tilt(4)
    uint8_t data[8];
    data[0] = static_cast<uint8_t>(pan);
    data[1] = static_cast<uint8_t>(pan >> 8);
    data[2] = static_cast<uint8_t>(pan >> 16);
    data[3] = static_cast<uint8_t>(pan >> 24);
    data[4] = static_cast<uint8_t>(tilt);
    data[5] = static_cast<uint8_t>(tilt >> 8);
    data[6] = static_cast<uint8_t>(tilt >> 16);
    data[7] = static_cast<uint8_t>(tilt >> 24);

    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    usb::TransferStatus st = uvc_set_cur(dev->usbAddress, wIdx,
                                          CT_PANTILT_ABS_CONTROL, data, 8);
    if (st == usb::XFER_SUCCESS) {
        dev->camera.panCurrent  = pan;
        dev->camera.tiltCurrent = tilt;
    }
    return st;
}

usb::TransferStatus set_roll(uint8_t devIndex, int32_t rollVal)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          CT_ROLL_ABS_CONTROL, static_cast<int16_t>(rollVal));
}

// ----------------------------------------------------------------
// Processing Unit controls
// ----------------------------------------------------------------

usb::TransferStatus set_brightness(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_BRIGHTNESS_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_contrast(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_CONTRAST_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_saturation(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_SATURATION_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_sharpness(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_SHARPNESS_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_gamma(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_GAMMA_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_hue(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_HUE_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_white_balance_temp(uint8_t devIndex, int32_t kelvin)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_WHITE_BALANCE_TEMP_CONTROL,
                          static_cast<int16_t>(kelvin));
}

usb::TransferStatus set_white_balance_auto(uint8_t devIndex, bool enable)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint8_t val = enable ? 1 : 0;
    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    usb::TransferStatus st = uvc_set_cur(dev->usbAddress, wIdx,
                                          PU_WHITE_BALANCE_TEMP_AUTO, &val, 1);
    if (st == usb::XFER_SUCCESS) dev->camera.autoWhiteBalance = enable;
    return st;
}

usb::TransferStatus set_backlight_comp(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_BACKLIGHT_COMP_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_gain(uint8_t devIndex, int32_t value)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return set_control_16(dev->usbAddress, wIdx,
                          PU_GAIN_CONTROL, static_cast<int16_t>(value));
}

usb::TransferStatus set_power_line_freq(uint8_t devIndex, uint8_t mode)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    usb::TransferStatus st = uvc_set_cur(dev->usbAddress, wIdx,
                                          PU_POWER_LINE_FREQ_CONTROL, &mode, 1);
    if (st == usb::XFER_SUCCESS) dev->camera.powerLineFreq = mode;
    return st;
}

// ----------------------------------------------------------------
// Control range queries
// ----------------------------------------------------------------

bool get_control_range(uint8_t devIndex, ProcessingUnitCS control,
                       ControlRange* range)
{
    if (devIndex >= MAX_UVC_DEVICES || !range) return false;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return false;

    uint16_t wIdx = make_entity_index(dev->processingUnitId, dev->controlInterface);
    return query_control_range_16(dev->usbAddress, wIdx,
                                   static_cast<uint8_t>(control), range);
}

bool get_camera_control_range(uint8_t devIndex, CameraTerminalCS control,
                              ControlRange* range)
{
    if (devIndex >= MAX_UVC_DEVICES || !range) return false;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return false;

    uint16_t wIdx = make_entity_index(dev->cameraTerminalId, dev->controlInterface);
    return query_control_range_16(dev->usbAddress, wIdx,
                                   static_cast<uint8_t>(control), range);
}

// ----------------------------------------------------------------
// Still image capture
// ----------------------------------------------------------------

usb::TransferStatus trigger_still_capture(uint8_t devIndex)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;
    if (dev->stillMethod == STILL_NONE) return usb::XFER_NOT_SUPPORTED;

    // Send VS_STILL_IMAGE_TRIGGER to the streaming interface
    uint8_t trigger = 0x01; // trigger normal still
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21;
    setup.bRequest      = usb_video::UVC_SET_CUR;
    setup.wValue        = 0x0500; // Still Image Trigger Control
    setup.wIndex        = dev->streamInterface;
    setup.wLength       = 1;
    return usb::control_transfer(dev->usbAddress, &setup, &trigger, 1);
}

usb::TransferStatus read_still_image(uint8_t devIndex,
                                      void* buffer,
                                      uint32_t maxLen,
                                      uint32_t* bytesRead)
{
    if (devIndex >= MAX_UVC_DEVICES) return usb::XFER_ERROR;
    UVCDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    // Read from the bulk or isochronous still pipe
    // (Implementation depends on still method; use bulk path as placeholder)
    uint16_t chunk = (maxLen > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(maxLen);
    uint16_t recvd = 0;
    usb::TransferStatus st = usb::hci::bulk_transfer(
        dev->usbAddress, 0x82, // typical still endpoint
        buffer, chunk, &recvd);

    if (bytesRead) *bytesRead = recvd;
    return st;
}

} // namespace usb_video_uvc
} // namespace kernel
