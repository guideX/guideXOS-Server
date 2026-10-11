#ifndef KERNEL_DISK_MANAGER_WHOLE_DISK_ERASE_UI_H
#define KERNEL_DISK_MANAGER_WHOLE_DISK_ERASE_UI_H

#include "kernel/whole_disk_erase.h"

namespace kernel {
namespace apps {
namespace disk_manager_whole_disk_erase_ui {

enum WholeDiskEraseUiBlockReason : uint8_t {
    WHOLE_ERASE_UI_READY = 0,
    WHOLE_ERASE_UI_DISK_ROW_REQUIRED,
    WHOLE_ERASE_UI_ENGINE_REJECTED,
    WHOLE_ERASE_UI_STABLE_SERIAL_REQUIRED,
    WHOLE_ERASE_UI_DUPLICATE_SERIAL,
};

inline bool stable_serial_is_usable(const char* serial)
{
    if (!serial) return false;
    size_t length = 0;
    bool hasAlphaNumeric = false;
    while (serial[length]) {
        const unsigned char c = static_cast<unsigned char>(serial[length]);
        if (length >= 23 || c < 0x21 || c > 0x7E) return false;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) hasAlphaNumeric = true;
        ++length;
    }
    return length >= 4 && hasAlphaNumeric;
}

inline char uppercase_ascii(char c)
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
}

inline bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    size_t i = 0;
    while (left[i] && right[i]) {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

inline bool stable_serials_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    size_t i = 0;
    while (left[i] && right[i]) {
        if (uppercase_ascii(left[i]) != uppercase_ascii(right[i])) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

inline WholeDiskEraseUiBlockReason whole_erase_action_block_reason(
    bool diskRowSelected, const storage::WholeDiskEraseProbe& probe,
    bool serialUnique)
{
    if (!diskRowSelected) return WHOLE_ERASE_UI_DISK_ROW_REQUIRED;
    if (probe.reason != storage::WHOLE_DISK_ERASE_REASON_NONE)
        return WHOLE_ERASE_UI_ENGINE_REJECTED;
    if (!stable_serial_is_usable(probe.targetIdentity.serial))
        return WHOLE_ERASE_UI_STABLE_SERIAL_REQUIRED;
    if (!serialUnique) return WHOLE_ERASE_UI_DUPLICATE_SERIAL;
    return WHOLE_ERASE_UI_READY;
}

inline const char* whole_erase_ui_block_reason_text(
    WholeDiskEraseUiBlockReason reason,
    storage::WholeDiskEraseReason engineReason)
{
    switch (reason) {
        case WHOLE_ERASE_UI_READY:
            return "Eligible for explicit whole-device erase confirmation";
        case WHOLE_ERASE_UI_DISK_ROW_REQUIRED:
            return "Select the parent disk row; partition and free-space rows cannot authorize a whole-disk erase";
        case WHOLE_ERASE_UI_ENGINE_REJECTED:
            return storage::whole_disk_erase_reason_name(engineReason);
        case WHOLE_ERASE_UI_STABLE_SERIAL_REQUIRED:
            return "A trustworthy device serial is unavailable; whole-disk erase is disabled";
        case WHOLE_ERASE_UI_DUPLICATE_SERIAL:
            return "This serial is shared by another disk; whole-disk erase is disabled";
        default:
            return "Whole-disk erase is unavailable";
    }
}

inline bool build_confirmation_phrase(const char* serial, char* output,
                                      size_t capacity)
{
    if (!output || capacity == 0) return false;
    output[0] = '\0';
    if (!stable_serial_is_usable(serial)) return false;
    static const char prefix[] = "ERASE ";
    size_t length = 0;
    while (prefix[length]) {
        if (length + 1 >= capacity) {
            output[0] = '\0';
            return false;
        }
        output[length] = prefix[length];
        ++length;
    }
    for (size_t i = 0; serial[i]; ++i) {
        if (length + 1 >= capacity) {
            output[0] = '\0';
            return false;
        }
        output[length++] = uppercase_ascii(serial[i]);
    }
    output[length] = '\0';
    return true;
}

inline bool confirmation_phrase_matches(const char* typed,
                                       const char* capturedPhrase)
{
    if (!typed || !capturedPhrase || !capturedPhrase[0]) return false;
    size_t i = 0;
    while (typed[i] && capturedPhrase[i]) {
        if (typed[i] != capturedPhrase[i]) return false;
        ++i;
    }
    return typed[i] == '\0' && capturedPhrase[i] == '\0';
}

inline bool capabilities_match(
    const storage::DeviceCapabilities& a,
    const storage::DeviceCapabilities& b)
{
    return a.globalIndex == b.globalIndex && a.transport == b.transport &&
        a.driverIndex == b.driverIndex && a.readable == b.readable &&
        a.writable == b.writable && a.flushSupported == b.flushSupported &&
        a.flushSemanticsKnown == b.flushSemanticsKnown &&
        a.removableKnown == b.removableKnown && a.removable == b.removable &&
        a.usbIdentityValid == b.usbIdentityValid &&
        a.usbVendorId == b.usbVendorId && a.usbProductId == b.usbProductId &&
        a.usbPort == b.usbPort && a.usbInterface == b.usbInterface &&
        a.usbLun == b.usbLun &&
        a.usbSyncCacheState == b.usbSyncCacheState &&
        a.controllerPciLocationValid == b.controllerPciLocationValid &&
        a.controllerPciSegment == b.controllerPciSegment &&
        a.controllerPciBus == b.controllerPciBus &&
        a.controllerPciDevice == b.controllerPciDevice &&
        a.controllerPciFunction == b.controllerPciFunction &&
        a.ahciPortValid == b.ahciPortValid && a.ahciPort == b.ahciPort &&
        a.geometryValid == b.geometryValid &&
        a.capacityValid == b.capacityValid &&
        a.logicalSectorSize == b.logicalSectorSize &&
        a.totalLogicalSectors == b.totalLogicalSectors &&
        a.totalCapacityBytes == b.totalCapacityBytes &&
        a.requiredBufferAlignment == b.requiredBufferAlignment &&
        a.maxTransferBytes == b.maxTransferBytes &&
        a.persistence == b.persistence &&
        text_equal(a.name, b.name) && text_equal(a.model, b.model) &&
        text_equal(a.serial, b.serial);
}

inline bool confirmation_snapshot_matches(
    const storage::WholeDiskEraseProbe& captured,
    const storage::WholeDiskEraseProbe& current)
{
    return captured.reason == storage::WHOLE_DISK_ERASE_REASON_NONE &&
        current.reason == storage::WHOLE_DISK_ERASE_REASON_NONE &&
        storage::target_identities_equal(captured.targetIdentity,
                                         current.targetIdentity) &&
        capabilities_match(captured.capabilities, current.capabilities) &&
        captured.mediaClassification == current.mediaClassification &&
        captured.partitionScheme == current.partitionScheme &&
        captured.partitionCount == current.partitionCount &&
        captured.layoutFingerprint[0] == current.layoutFingerprint[0] &&
        captured.layoutFingerprint[1] == current.layoutFingerprint[1];
}

inline bool confirmation_can_start(
    bool exactDiskRowSelected,
    const storage::WholeDiskEraseProbe& captured,
    const storage::WholeDiskEraseProbe& current,
    const char* typed, const char* capturedPhrase)
{
    return exactDiskRowSelected &&
        confirmation_snapshot_matches(captured, current) &&
        confirmation_phrase_matches(typed, capturedPhrase);
}

inline bool whole_erase_job_running(const storage::WholeDiskEraseJob& job)
{
    return job.result.state == storage::WHOLE_DISK_ERASE_RUNNING;
}

inline bool request_stop_before_close(storage::WholeDiskEraseJob& job)
{
    if (!whole_erase_job_running(job)) return true;
    storage::request_whole_disk_erase_stop(job);
    return false;
}

inline const char* terminal_message(storage::WholeDiskEraseState state,
                                    bool writeAttempted)
{
    switch (state) {
        case storage::WHOLE_DISK_ERASE_COMPLETED:
            return "All addressable logical sectors were overwritten with zeros.";
        case storage::WHOLE_DISK_ERASE_STOPPED:
            return writeAttempted
                ? "Erasure stopped. The disk may be partially overwritten and may no longer contain a usable partition table."
                : "Erase stopped before any write was attempted. No sectors were changed.";
        case storage::WHOLE_DISK_ERASE_FAILED:
            return "Erasure failed. The disk may be partially overwritten.";
        default:
            return "Whole-disk erase is still in progress.";
    }
}

inline int progress_value(const storage::WholeDiskEraseResult& result)
{
    return result.progressPercent;
}

inline uint32_t stop_button_background(bool focused)
{
    return focused ? 0xFF9A3A32 : 0xFF6F3030;
}

inline bool running_tab_navigation_enabled(bool regularRunningDialog,
                                           bool wholeEraseRunning)
{
    return regularRunningDialog || wholeEraseRunning;
}

inline bool running_tab_focuses_stop_button(bool wipeRunning,
                                            bool wholeEraseRunning,
                                            bool formatScanCancelable)
{
    return wipeRunning || wholeEraseRunning || formatScanCancelable;
}

} // namespace disk_manager_whole_disk_erase_ui
} // namespace apps
} // namespace kernel

#endif
