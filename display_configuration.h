#pragma once

#include "display_model.h"
#include "display_mode_catalog.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace gxos {
namespace gui {

// A detected inventory is backend-owned state. It is never used as the
// persisted request and it intentionally carries resource/transport details.
struct DetectedDisplayInventory {
    std::string backend{"framebuffer"};
    bool qemuOnly{false};
    bool backendGateActive{false};
    std::vector<DisplayMonitorDescriptor> monitors;
    std::vector<DisplayRenderTarget> renderTargets;
    DisplayVirtualDesktop currentDesktop{};

    uint32_t detectedOutputCount() const
    {
        return static_cast<uint32_t>(monitors.size());
    }

    uint32_t operationalOutputCount() const
    {
        uint32_t count = 0;
        for (const auto& monitor : monitors) {
            if (monitor.operational && monitor.width > 0 && monitor.height > 0) {
                ++count;
            }
        }
        return count;
    }

    uint32_t connectorEnabledCount() const
    {
        uint32_t count = 0;
        for (const auto& monitor : monitors) {
            if (monitor.connectorEnabled) ++count;
        }
        return count;
    }

    uint32_t presentationConfirmedCount() const
    {
        uint32_t count = 0;
        for (const auto& monitor : monitors) {
            if (monitor.presentationConfirmed) ++count;
        }
        return count;
    }

    bool hasOperationalOutputs() const
    {
        return operationalOutputCount() > 0;
    }
};

inline std::vector<DisplayMode> displayModeCatalogForInventory(const DetectedDisplayInventory& inventory)
{
    if (inventory.backend == "virtio-gpu" && inventory.qemuOnly && inventory.backendGateActive) {
        return qemuVirtioGpuLogicalModeCatalog();
    }
    return {};
}

// This is the requested configuration, built from the existing persisted
// display mode, primary id, and arrangement fields. It contains no temporary
// resource ids, backing addresses, or scanout bindings.
struct RequestedDisplayConfiguration {
    DisplayModeKind mode{DisplayModeKind::Mirror};
    std::string primaryOutputId{"display-1"};
    std::vector<DisplayMonitorDescriptor> arrangement;
    std::vector<std::string> enabledOutputIds;

    // The arrangement carries the same stable identity and dimensions for
    // compatibility with older requests; modeId is the authoritative logical
    // mode selection for each requested output.

    std::string summary() const
    {
        std::ostringstream out;
        out << "mode=" << displayModeName(mode)
            << " primary=" << (primaryOutputId.empty() ? "(none)" : primaryOutputId)
            << " outputs=" << (enabledOutputIds.empty() ? arrangement.size() : enabledOutputIds.size());
        if (!arrangement.empty()) {
            out << " arrangement=" << serializeDisplayArrangement(arrangement);
        }
        return out.str();
    }
};

struct ActiveDisplayConfiguration {
    std::string backend{"framebuffer"};
    DisplayModeKind mode{DisplayModeKind::Mirror};
    std::string primaryOutputId{"display-1"};
    DisplayVirtualDesktop virtualDesktop{};
    std::vector<DisplayMonitorDescriptor> monitors;
    std::vector<DisplayViewport> viewports;
    std::vector<DisplayRenderTarget> renderTargets;
    std::string taskbarMonitorId;

    bool valid() const
    {
        return !monitors.empty() && virtualDesktop.width() > 0 && virtualDesktop.height() > 0;
    }

    std::string summary() const
    {
        std::ostringstream out;
        out << "backend=" << backend
            << " mode=" << displayModeName(mode)
            << " primary=" << primaryOutputId
            << " monitors=" << virtualDesktop.activeMonitorCount()
            << " targets=" << renderTargets.size()
            << " virtualDesktop=" << virtualDesktop.width() << 'x' << virtualDesktop.height()
            << " taskbarMonitor=" << (taskbarMonitorId.empty() ? "(none)" : taskbarMonitorId);
        return out.str();
    }
};

struct DisplayApplyResult {
    bool success{false};
    bool validationPassed{false};
    bool presentationPaused{false};
    bool presentationIdle{false};
    bool targetsRebuilt{false};
    bool validationFrameResult{false};
    bool persistenceCommitted{false};
    bool rollbackAttempted{false};
    bool rollbackSucceeded{false};
    bool fallbackActivated{false};
    bool reconciliationOccurred{false};
    std::string reason;
    std::string finalResult;

    std::string summary() const
    {
        std::ostringstream out;
        out << "paused=" << (presentationPaused ? "yes" : "no")
            << " validated=" << (validationFrameResult ? "yes" : "no")
            << " targetsRebuilt=" << (targetsRebuilt ? "yes" : "no")
            << " validationFrame=" << (validationFrameResult ? "ok" : "failed")
            << " persisted=" << (persistenceCommitted ? "yes" : "no")
            << " rollback=" << (rollbackAttempted ? "yes" : "no")
            << " result=" << (finalResult.empty() ? (success ? "success" : "failed") : finalResult);
        if (!reason.empty()) out << " reason=" << reason;
        return out.str();
    }
};

struct DisplayReconfigurationState {
    bool reconfigurationRequested{false};
    bool reconfigurationInProgress{false};
    bool presentationPaused{false};
    ActiveDisplayConfiguration oldConfiguration{};
    RequestedDisplayConfiguration requestedConfiguration{};
    ActiveDisplayConfiguration appliedConfiguration{};
    bool validationFrameResult{false};
    bool rollbackAttempted{false};
    bool rollbackSucceeded{false};
    DisplayApplyResult finalResult{};
};

inline std::string displayConfigurationBackendName(const DetectedDisplayInventory& inventory)
{
    return inventory.backend.empty() ? "framebuffer" : inventory.backend;
}

inline const DisplayMonitorDescriptor* findDetectedDisplayOutput(
    const DetectedDisplayInventory& inventory,
    const std::string& identity)
{
    if (identity.empty()) return nullptr;
    for (const auto& monitor : inventory.monitors) {
        if (monitor.id == identity || monitor.outputId == identity || monitor.backendId == identity) {
            return &monitor;
        }
    }

    // Older persisted arrangements used display-1/display-2 while the first
    // backend inventory used numeric ids. Reconcile by stable inventory order
    // only after identity matching has been attempted.
    if (identity.size() > 8 && identity.compare(0, 8, "display-") == 0) {
        try {
            const size_t ordinal = static_cast<size_t>(std::stoul(identity.substr(8)));
            if (ordinal > 0 && ordinal <= inventory.monitors.size()) {
                return &inventory.monitors[ordinal - 1];
            }
        } catch (...) {
        }
    }
    return nullptr;
}

inline DisplayMonitorDescriptor requestedMonitorForPersistence(const DisplayMonitorDescriptor& monitor)
{
    DisplayMonitorDescriptor requested = monitor;
    requested.framebufferBase = nullptr;
    requested.scanoutId = 0;
    requested.resourceId = 0;
    requested.resourceBound = false;
    requested.backingAttached = false;
    requested.transferReady = false;
    requested.presentReady = false;
    requested.presentationReady = false;
    requested.presentationConfirmed = false;
    requested.backingVirtualAddress = 0;
    requested.backingByteCount = 0;
    requested.backingMemEntryCount = 0;
    requested.patternChecksum = 0;
    requested.lastCommandStatus.clear();
    return requested;
}

inline RequestedDisplayConfiguration reconcileRequestedDisplayConfiguration(
    const RequestedDisplayConfiguration& requested,
    const DetectedDisplayInventory& inventory,
    bool* reconciled)
{
    RequestedDisplayConfiguration result = requested;
    bool changed = false;
    std::vector<DisplayMonitorDescriptor> reconciledArrangement;
    std::vector<bool> used(inventory.monitors.size(), false);

    for (const auto& entry : requested.arrangement) {
        const DisplayMonitorDescriptor* matched = findDetectedDisplayOutput(inventory, entry.id);
        if (matched == nullptr && !entry.outputId.empty()) {
            matched = findDetectedDisplayOutput(inventory, entry.outputId);
        }
        if (matched == nullptr) {
            changed = true;
            continue;
        }

        size_t matchedIndex = static_cast<size_t>(matched - inventory.monitors.data());
        if (matchedIndex < used.size() && used[matchedIndex]) {
            changed = true;
            continue;
        }
        if (matchedIndex < used.size()) used[matchedIndex] = true;

        DisplayMonitorDescriptor normalized = requestedMonitorForPersistence(*matched);
        normalized.id = matched->id;
        normalized.name = matched->name;
        normalized.source = matched->source;
        normalized.sourceType = matched->sourceType;
        normalized.backendId = matched->backendId;
        normalized.outputId = matched->outputId;
        normalized.modeId = entry.modeId.empty() ? matched->modeId : entry.modeId;
        normalized.virtualX = entry.virtualX;
        normalized.virtualY = entry.virtualY;
        normalized.width = entry.width > 0 ? entry.width : matched->assignedWidth;
        normalized.height = entry.height > 0 ? entry.height : matched->assignedHeight;
        normalized.enabled = entry.enabled;
        normalized.primary = entry.primary;
        // Preferred/detected geometry is never overwritten by a logical mode
        // selection. It remains connector-reported metadata.
        normalized.preferredX = matched->preferredX;
        normalized.preferredY = matched->preferredY;
        normalized.preferredWidth = matched->preferredWidth;
        normalized.preferredHeight = matched->preferredHeight;
        normalized.assignedX = entry.assignedX;
        normalized.assignedY = entry.assignedY;
        normalized.assignedWidth = entry.assignedWidth;
        normalized.assignedHeight = entry.assignedHeight;
        reconciledArrangement.push_back(normalized);
        if (entry.id != matched->id) changed = true;
    }

    if (reconciledArrangement.empty() || reconciledArrangement.size() < inventory.operationalOutputCount()) {
        for (const auto& monitor : inventory.monitors) {
            if (!monitor.operational || monitor.width <= 0 || monitor.height <= 0) continue;
            const bool alreadyIncluded = std::any_of(
                reconciledArrangement.begin(), reconciledArrangement.end(),
                [&](const DisplayMonitorDescriptor& current) { return current.id == monitor.id; });
            if (alreadyIncluded) continue;
            DisplayMonitorDescriptor fallback = requestedMonitorForPersistence(monitor);
            fallback.enabled = true;
            fallback.primary = reconciledArrangement.empty();
            fallback.virtualX = monitor.assignedX;
            fallback.virtualY = monitor.assignedY;
            fallback.width = monitor.assignedWidth > 0 ? monitor.assignedWidth : monitor.width;
            fallback.height = monitor.assignedHeight > 0 ? monitor.assignedHeight : monitor.height;
            fallback.modeId = monitor.modeId;
            fallback.assignedX = fallback.virtualX;
            fallback.assignedY = fallback.virtualY;
            fallback.assignedWidth = fallback.width;
            fallback.assignedHeight = fallback.height;
            reconciledArrangement.push_back(fallback);
            changed = true;
        }
    }

    result.arrangement = reconciledArrangement;
    if (findDetectedDisplayOutput(inventory, result.primaryOutputId) == nullptr && !inventory.monitors.empty()) {
        result.primaryOutputId = inventory.monitors.front().id;
        changed = true;
    } else if (const DisplayMonitorDescriptor* primary = findDetectedDisplayOutput(inventory, result.primaryOutputId)) {
        if (result.primaryOutputId != primary->id) {
            result.primaryOutputId = primary->id;
            changed = true;
        }
    }

    if (result.enabledOutputIds.empty()) {
        for (const auto& entry : result.arrangement) {
            if (entry.enabled) result.enabledOutputIds.push_back(entry.id);
        }
    } else {
        for (auto& id : result.enabledOutputIds) {
            if (const DisplayMonitorDescriptor* matched = findDetectedDisplayOutput(inventory, id)) {
                if (id != matched->id) {
                    id = matched->id;
                    changed = true;
                }
            }
        }
    }

    // Mirror has no disabled side-by-side region.  Every operational output
    // receives the same logical viewport, so stale single-output selections
    // from the older synthetic model must not silently hide a real output.
    if (result.mode == DisplayModeKind::Mirror) {
        std::vector<std::string> mirroredOutputIds;
        for (const auto& monitor : inventory.monitors) {
            if (!monitor.operational || monitor.width <= 0 || monitor.height <= 0) continue;
            mirroredOutputIds.push_back(monitor.id);
        }
        if (mirroredOutputIds != result.enabledOutputIds) {
            result.enabledOutputIds = mirroredOutputIds;
            changed = true;
        }
    }

    if (reconciled != nullptr) *reconciled = changed;
    return result;
}

inline bool validateRequestedDisplayConfiguration(
    const RequestedDisplayConfiguration& requested,
    const DetectedDisplayInventory& inventory,
    std::string& reason)
{
    reason.clear();
    if (!inventory.hasOperationalOutputs()) {
        reason = "no operational outputs";
        return false;
    }
    const DisplayMonitorDescriptor* primary = findDetectedDisplayOutput(inventory, requested.primaryOutputId);
    if (primary == nullptr || !primary->operational) {
        reason = "primary output unavailable";
        return false;
    }
    if (!primary->primaryCapable) {
        reason = "primary monitor selection unsupported";
        return false;
    }

    std::vector<const DisplayMonitorDescriptor*> selected;
    if (!requested.enabledOutputIds.empty()) {
        for (const auto& id : requested.enabledOutputIds) {
            const DisplayMonitorDescriptor* monitor = findDetectedDisplayOutput(inventory, id);
            if (monitor == nullptr || !monitor->operational) {
                reason = "output unavailable: " + id;
                return false;
            }
            selected.push_back(monitor);
        }
    } else {
        for (const auto& monitor : inventory.monitors) {
            if (monitor.operational && monitor.width > 0 && monitor.height > 0) selected.push_back(&monitor);
        }
    }
    if (selected.empty()) {
        reason = "no selected operational outputs";
        return false;
    }

    const std::vector<DisplayMode> catalog = displayModeCatalogForInventory(inventory);
    // Physical backends still reject arbitrary resolution changes; the
    // bounded QEMU catalog below is the only logical-resolution exception.
    // An arbitrary resolution change is not supported on physical backends.
    uint64_t totalBackingBytes = 0;
    int mirrorWidth = 0;
    int mirrorHeight = 0;
    for (const auto* monitor : selected) {
        if (requested.mode == DisplayModeKind::Extend && !monitor->extendCapable) {
            reason = "Extend unsupported by output " + monitor->id;
            return false;
        }
        if (requested.mode == DisplayModeKind::Mirror && !monitor->mirrorCapable) {
            reason = "Mirror unsupported by output " + monitor->id;
            return false;
        }

        const DisplayMonitorDescriptor* entry = nullptr;
        for (const auto& candidate : requested.arrangement) {
            if (candidate.id == monitor->id) {
                entry = &candidate;
                break;
            }
        }
        const std::string requestedModeId = entry != nullptr ? entry->modeId : monitor->modeId;
        const int requestedWidth = entry != nullptr && entry->width > 0
            ? entry->width : (monitor->assignedWidth > 0 ? monitor->assignedWidth : monitor->width);
        const int requestedHeight = entry != nullptr && entry->height > 0
            ? entry->height : (monitor->assignedHeight > 0 ? monitor->assignedHeight : monitor->height);
        const DisplayMode* mode = nullptr;
        if (!catalog.empty()) {
            mode = requestedModeId.empty() ? findDisplayModeByDimensions(catalog, requestedWidth, requestedHeight)
                                            : findDisplayModeById(catalog, requestedModeId);
            if (mode == nullptr || !mode->backendSupported || mode->width != requestedWidth || mode->height != requestedHeight) {
                reason = "unsupported QEMU logical resolution for output " + monitor->id;
                return false;
            }
        } else {
            const int detectedWidth = monitor->assignedWidth > 0 ? monitor->assignedWidth : monitor->width;
            const int detectedHeight = monitor->assignedHeight > 0 ? monitor->assignedHeight : monitor->height;
            if (requestedWidth != detectedWidth || requestedHeight != detectedHeight) {
                reason = "arbitrary resolution change is not supported on physical backends";
                return false;
            }
        }

        uint64_t backingBytes = 0;
        if (!checkedDisplayModeBackingBytes(requestedWidth, requestedHeight, 4u, backingBytes)) {
            reason = "requested resolution exceeds bounded backing allocation for output " + monitor->id;
            return false;
        }
        if (totalBackingBytes > kQemuLogicalModeTotalBackingLimit - backingBytes) {
            reason = "requested display layout exceeds total backing allocation limit";
            return false;
        }
        totalBackingBytes += backingBytes;
        if (requested.mode == DisplayModeKind::Mirror) {
            if (mirrorWidth == 0) {
                mirrorWidth = requestedWidth;
                mirrorHeight = requestedHeight;
            } else if (mirrorWidth != requestedWidth || mirrorHeight != requestedHeight) {
                reason = "Mirror dimensions incompatible: all outputs must use the same logical resolution";
                return false;
            }
        }
    }
    return true;
}

inline DisplayRenderTarget makeActiveDisplayTarget(
    int targetIndex,
    const DisplayMonitorDescriptor& monitor,
    DisplayModeKind mode)
{
    DisplayRenderTarget target = makeDisplayRenderTarget(targetIndex, monitor, true, false, false);
    target.targetId = (monitor.sourceType == "virtio-gpu" || monitor.source == "virtio-gpu")
        ? "virtio-gpu-target-" + std::to_string(targetIndex)
        : "display-target-" + std::to_string(targetIndex);
    target.source = monitor.source;
    target.scanoutId = monitor.scanoutId;
    target.resourceId = monitor.resourceId;
    target.backedByOutputResource = monitor.resourceBound && monitor.backingAttached && monitor.presentReady;
    target.connectorEnabled = monitor.connectorEnabled;
    target.resourceBound = monitor.resourceBound;
    target.backingAttached = monitor.backingAttached;
    target.transferReady = monitor.transferReady;
    target.presentReady = monitor.presentReady;
    target.presentationConfirmed = monitor.presentationConfirmed;
    target.syntheticHosted = false;
    target.backingVirtualAddress = monitor.backingVirtualAddress;
    target.backingByteCount = monitor.backingByteCount;
    target.backingMemEntryCount = monitor.backingMemEntryCount;
    target.patternChecksum = monitor.patternChecksum;
    target.lastCommandStatus = monitor.lastCommandStatus;
    if (mode == DisplayModeKind::Mirror) {
        target.viewportOriginX = 0;
        target.viewportOriginY = 0;
    }
    return target;
}

inline ActiveDisplayConfiguration buildActiveDisplayConfiguration(
    const DetectedDisplayInventory& inventory,
    const RequestedDisplayConfiguration& requested,
    std::string& reason)
{
    ActiveDisplayConfiguration active;
    active.backend = displayConfigurationBackendName(inventory);
    active.mode = requested.mode;
    active.primaryOutputId = requested.primaryOutputId;
    reason.clear();

    bool reconciled = false;
    const RequestedDisplayConfiguration reconciledRequest =
        reconcileRequestedDisplayConfiguration(requested, inventory, &reconciled);
    active.primaryOutputId = reconciledRequest.primaryOutputId;
    if (!validateRequestedDisplayConfiguration(reconciledRequest, inventory, reason)) {
        return active;
    }

    std::vector<const DisplayMonitorDescriptor*> selected;
    for (const auto& monitor : inventory.monitors) {
        bool enabled = monitor.operational && monitor.width > 0 && monitor.height > 0;
        if (!reconciledRequest.enabledOutputIds.empty()) {
            enabled = std::find(reconciledRequest.enabledOutputIds.begin(), reconciledRequest.enabledOutputIds.end(), monitor.id)
                != reconciledRequest.enabledOutputIds.end();
        }
        if (enabled) selected.push_back(&monitor);
    }

    auto arrangementEntry = [&](const std::string& id) -> const DisplayMonitorDescriptor* {
        for (const auto& entry : reconciledRequest.arrangement) {
            if (entry.id == id) return &entry;
        }
        return nullptr;
    };

    int nextVirtualX = 0;
    for (size_t index = 0; index < selected.size(); ++index) {
        const DisplayMonitorDescriptor& detected = *selected[index];
        DisplayMonitorDescriptor monitor = detected;
        const DisplayMonitorDescriptor* entry = arrangementEntry(detected.id);
        monitor.enabled = true;
        monitor.operational = true;
        monitor.primary = detected.id == reconciledRequest.primaryOutputId;
        monitor.width = entry != nullptr && entry->width > 0 ? entry->width
            : (detected.assignedWidth > 0 ? detected.assignedWidth : detected.width);
        monitor.height = entry != nullptr && entry->height > 0 ? entry->height
            : (detected.assignedHeight > 0 ? detected.assignedHeight : detected.height);
        monitor.modeId = entry != nullptr && !entry->modeId.empty() ? entry->modeId : detected.modeId;
        // Persisted requests historically carried monitor.virtualX = entry->virtualX
        // and monitor.virtualY = entry->virtualY. QEMU logical rebuilds normalize
        // horizontal Extend origins from the complete requested mode set so a
        // changed first output cannot leave a gap or overlap.
        monitor.virtualX = nextVirtualX;
        monitor.virtualY = 0;
        if (requested.mode == DisplayModeKind::Mirror) monitor.virtualX = 0;
        if (monitor.width <= 0 || monitor.height <= 0) {
            reason = "logical output geometry is invalid";
            return ActiveDisplayConfiguration{};
        }
        if (requested.mode == DisplayModeKind::Extend) {
            if (nextVirtualX > 2147483647 - monitor.width) {
                reason = "virtual desktop geometry overflow";
                return ActiveDisplayConfiguration{};
            }
            nextVirtualX += monitor.width;
        }
        active.monitors.push_back(monitor);
    }

    if (requested.mode == DisplayModeKind::Mirror && !active.monitors.empty()) {
        const DisplayMonitorDescriptor* primary = nullptr;
        for (const auto& monitor : active.monitors) {
            if (monitor.primary) {
                primary = &monitor;
                break;
            }
        }
        if (primary == nullptr) primary = &active.monitors.front();
        const int width = primary->width;
        const int height = primary->height;
        for (auto& monitor : active.monitors) {
            monitor.virtualX = 0;
            monitor.virtualY = 0;
            monitor.width = width;
            monitor.height = height;
        }
    }

    // The active inventory reports the committed logical assignment. Preferred
    // connector geometry remains separate in preferredWidth/preferredHeight.
    for (auto& monitor : active.monitors) {
        monitor.assignedX = monitor.virtualX;
        monitor.assignedY = monitor.virtualY;
        monitor.assignedWidth = monitor.width;
        monitor.assignedHeight = monitor.height;
    }

    active.virtualDesktop.mode = requested.mode;
    active.virtualDesktop.monitors = active.monitors;
    active.virtualDesktop.recomputeBounds();
    active.taskbarMonitorId = active.primaryOutputId;
    for (size_t index = 0; index < active.monitors.size(); ++index) {
        DisplayRenderTarget target = makeActiveDisplayTarget(static_cast<int>(index + 1), active.monitors[index], requested.mode);
        target.viewportOriginX = active.monitors[index].virtualX;
        target.viewportOriginY = active.monitors[index].virtualY;
        if (requested.mode == DisplayModeKind::Mirror) {
            target.viewportOriginX = 0;
            target.viewportOriginY = 0;
        }
        active.renderTargets.push_back(target);
        DisplayViewport viewport = target.viewportDescriptor();
        viewport.index = static_cast<int>(index + 1);
        active.viewports.push_back(viewport);
    }
    (void)reconciled;
    return active;
}

inline std::string activeDisplayConfigurationSummary(const ActiveDisplayConfiguration& active)
{
    return active.summary();
}

} // namespace gui
} // namespace gxos
