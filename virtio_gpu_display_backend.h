#pragma once

#ifdef GXOS_BARE_METAL
#include "virtio_gpu_display_backend_kernel.h"
#else
#include "display_model.h"
#include "display_configuration.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace gxos {
namespace gui {

struct VirtioGpuScanoutState {
    uint32_t scanoutId{0};
    uint32_t resourceId{0};
    bool connectorEnabled{false};
    int preferredX{0};
    int preferredY{0};
    int preferredWidth{0};
    int preferredHeight{0};
    int assignedX{0};
    int assignedY{0};
    int assignedWidth{0};
    int assignedHeight{0};
    bool resourceBound{false};
    bool backingAttached{false};
    bool transferReady{false};
    bool presentReady{false};
    bool presentationConfirmed{false};
    bool primary{false};
    bool active{false};
    uint64_t backingVirtualAddress{0};
    uint64_t backingByteCount{0};
    uint32_t backingMemEntryCount{0};
    uint64_t patternChecksum{0};
    std::string lastCommandStatus;
    std::string source{"virtio-gpu"};

    bool isOperational(uint32_t deviceConfigNumScanouts) const
    {
        return scanoutId < deviceConfigNumScanouts
            && resourceId != 0u
            && backingAttached
            && resourceBound
            && transferReady
            && presentReady;
    }

    DisplayRect preferredGeometry() const
    {
        return DisplayRect{
            preferredX,
            preferredY,
            preferredX + std::max(0, preferredWidth),
            preferredY + std::max(0, preferredHeight)
        };
    }

    DisplayRect assignedGeometry() const
    {
        return DisplayRect{
            assignedX,
            assignedY,
            assignedX + std::max(0, assignedWidth),
            assignedY + std::max(0, assignedHeight)
        };
    }
};

struct VirtioGpuOutputInventory {
    bool qemuOnly{true};
    uint32_t deviceConfigNumScanouts{0};
    uint32_t outputCount{0};
    uint32_t operationalOutputCount{0};
    uint32_t protocolConnectorEnabledCount{0};
    uint32_t presentationConfirmedCount{0};
    uint32_t primaryOutput{0};
    uint32_t targetCount{0};
    uint32_t backedTargetCount{0};
    DisplayVirtualDesktop virtualDesktop{};
    std::vector<VirtioGpuScanoutState> outputs;
    std::vector<DisplayMonitorDescriptor> monitors;
    std::vector<DisplayViewport> viewports;
    std::vector<DisplayRenderTarget> renderTargets;
};

inline std::string virtioGpuGeometrySummary(int x, int y, int width, int height)
{
    std::ostringstream out;
    out << x << ',' << y << ' ' << width << 'x' << height;
    return out.str();
}

inline std::string virtioGpuMonitorSummaryLine(const DisplayMonitorDescriptor& monitor)
{
    std::ostringstream out;
    out << "monitor[" << monitor.id << "]: source="
        << (monitor.source.empty() ? "virtio-gpu" : monitor.source)
        << " scanout=" << monitor.scanoutId
        << " resource=" << monitor.resourceId
        << " primary=" << (monitor.primary ? "yes" : "no")
        << " enabled=" << (monitor.enabled ? "yes" : "no")
        << " operational=" << ((monitor.operational || monitor.enabled) ? "yes" : "no")
        << " connectorEnabled=" << (monitor.connectorEnabled ? "yes" : "no")
        << " resourceBound=" << (monitor.resourceBound ? "yes" : "no")
        << " backingAttached=" << (monitor.backingAttached ? "yes" : "no")
        << " transferReady=" << (monitor.transferReady ? "yes" : "no")
        << " presentationReady=" << (monitor.presentationReady ? "yes" : "no")
        << " presentReady=" << (monitor.presentReady ? "yes" : "no")
        << " confirmed=" << (monitor.presentationConfirmed ? "yes" : "no")
        << " preferred=" << virtioGpuGeometrySummary(
            monitor.preferredX,
            monitor.preferredY,
            monitor.preferredWidth,
            monitor.preferredHeight)
        << " assigned=" << virtioGpuGeometrySummary(
            monitor.assignedX,
            monitor.assignedY,
            monitor.assignedWidth,
            monitor.assignedHeight)
        << " virtual=" << monitor.virtualX << ',' << monitor.virtualY;
    return out.str();
}

inline std::string virtioGpuRenderTargetSummaryLine(const DisplayRenderTarget& target)
{
    std::ostringstream out;
    out << "target[" << target.targetIndex << "]: source="
        << (target.source.empty() ? "virtio-gpu" : target.source)
        << " monitor=" << (target.monitorId.empty() ? "(none)" : target.monitorId)
        << " scanout=" << target.scanoutId
        << " resource=" << target.resourceId
        << " primary=" << (target.primary ? "yes" : "no")
        << " active=" << (target.active ? "yes" : "no")
        << " backed=" << ((target.backedByOutputResource || target.backedByHostedFramebuffer) ? "yes" : "no")
        << " connectorEnabled=" << (target.connectorEnabled ? "yes" : "no")
        << " resourceBound=" << (target.resourceBound ? "yes" : "no")
        << " backingAttached=" << (target.backingAttached ? "yes" : "no")
        << " transferReady=" << (target.transferReady ? "yes" : "no")
        << " presentReady=" << (target.presentReady ? "yes" : "no")
        << " confirmed=" << (target.presentationConfirmed ? "yes" : "no")
        << " viewport=" << virtioGpuGeometrySummary(
            target.viewportOriginX,
            target.viewportOriginY,
            target.width,
            target.height)
        << " preferred=" << virtioGpuGeometrySummary(
            target.preferredX,
            target.preferredY,
            target.preferredWidth,
            target.preferredHeight)
        << " assigned=" << virtioGpuGeometrySummary(
            target.assignedX,
            target.assignedY,
            target.assignedWidth,
            target.assignedHeight);
    return out.str();
}

inline std::string virtioGpuOutputSummaryLine(
    const VirtioGpuScanoutState& output,
    const DisplayMonitorDescriptor& monitor)
{
    std::ostringstream out;
    out << "output[" << output.scanoutId << "]: source="
        << (output.source.empty() ? "virtio-gpu" : output.source)
        << " scanout=" << output.scanoutId
        << " resource=" << output.resourceId
        << " connectorEnabled=" << (output.connectorEnabled ? "yes" : "no")
        << " resourceBound=" << (output.resourceBound ? "yes" : "no")
        << " backingAttached=" << (output.backingAttached ? "yes" : "no")
        << " transferReady=" << (output.transferReady ? "yes" : "no")
        << " presentReady=" << (output.presentReady ? "yes" : "no")
        << " confirmed=" << (output.presentationConfirmed ? "yes" : "no")
        << " operational=" << (monitor.operational ? "yes" : "no")
        << " preferred=" << virtioGpuGeometrySummary(
            output.preferredX,
            output.preferredY,
            output.preferredWidth,
            output.preferredHeight)
        << " assigned=" << virtioGpuGeometrySummary(
            output.assignedX,
            output.assignedY,
            output.assignedWidth,
            output.assignedHeight)
        << " virtual=" << monitor.virtualX << ',' << monitor.virtualY
        << " primary=" << (monitor.primary ? "yes" : "no")
        << " active=" << ((monitor.enabled && monitor.width > 0 && monitor.height > 0) ? "yes" : "no");
    return out.str();
}

inline std::string virtioGpuOutputInventorySummary(const VirtioGpuOutputInventory& inventory)
{
    std::ostringstream out;
    out << "VirtioGPU outputs: configured=" << inventory.outputCount
        << " operational=" << inventory.operationalOutputCount
        << " connectorEnabled=" << inventory.protocolConnectorEnabledCount
        << " presentationConfirmed=" << inventory.presentationConfirmedCount
        << " virtualDesktop=" << inventory.virtualDesktop.width() << 'x' << inventory.virtualDesktop.height()
        << " targets=" << inventory.targetCount
        << " backed=" << inventory.backedTargetCount
        << " primaryOutput=" << inventory.primaryOutput
        << " protocolConnectorEnabledCount=" << inventory.protocolConnectorEnabledCount
        << " operationalOutputCount=" << inventory.operationalOutputCount
        << " presentationConfirmedCount=" << inventory.presentationConfirmedCount;
    return out.str();
}

class VirtioGpuDisplayBackend {
public:
    // REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
    static bool isQemuOnly()
    {
        return true;
    }

    static VirtioGpuOutputInventory getVirtioGpuOutputInventory(
        const std::vector<VirtioGpuScanoutState>& scanouts,
        uint32_t deviceConfigNumScanouts)
    {
        VirtioGpuOutputInventory inventory;
        inventory.deviceConfigNumScanouts = deviceConfigNumScanouts;
        inventory.qemuOnly = true;

        std::vector<VirtioGpuScanoutState> activeScanouts;
        activeScanouts.reserve(scanouts.size());
        for (const auto& scanout : scanouts) {
            if (scanout.isOperational(deviceConfigNumScanouts)) {
                activeScanouts.push_back(scanout);
            }
        }

        std::sort(activeScanouts.begin(), activeScanouts.end(), [](const VirtioGpuScanoutState& a, const VirtioGpuScanoutState& b) {
            return a.scanoutId < b.scanoutId;
        });

        inventory.outputCount = static_cast<uint32_t>(activeScanouts.size());
        int virtualX = 0;
        bool havePrimary = false;
        for (size_t index = 0; index < activeScanouts.size(); ++index) {
            VirtioGpuScanoutState output = activeScanouts[index];
            output.active = true;
            if (!havePrimary && output.primary) {
                havePrimary = true;
                inventory.primaryOutput = output.scanoutId;
            }
            if (!havePrimary && index == 0) {
                output.primary = true;
                havePrimary = true;
                inventory.primaryOutput = output.scanoutId;
            }

            const int assignedWidth = std::max(1, output.assignedWidth);
            const int assignedHeight = std::max(1, output.assignedHeight);

            DisplayMonitorDescriptor monitor = makeDisplayMonitor(
                std::string("display-") + std::to_string(output.scanoutId + 1u),
                std::string("VirtIO-GPU Output ") + std::to_string(output.scanoutId + 1u),
                virtualX,
                0,
                assignedWidth,
                assignedHeight,
                nullptr,
                0,
                true,
                output.primary);
            monitor.source = output.source;
            monitor.sourceType = "virtio-gpu";
            monitor.backendId = "virtio-gpu";
            monitor.outputId = std::string("virtio-gpu-output-") + std::to_string(output.scanoutId);
            const std::vector<DisplayMode> modeCatalog = qemuVirtioGpuLogicalModeCatalog();
            if (const DisplayMode* currentMode = findDisplayModeByDimensions(modeCatalog, assignedWidth, assignedHeight)) {
                monitor.modeId = currentMode->id;
            }
            monitor.scanoutId = output.scanoutId;
            monitor.resourceId = output.resourceId;
            monitor.preferredX = output.preferredX;
            monitor.preferredY = output.preferredY;
            monitor.preferredWidth = output.preferredWidth;
            monitor.preferredHeight = output.preferredHeight;
            monitor.assignedX = output.assignedX;
            monitor.assignedY = output.assignedY;
            monitor.assignedWidth = output.assignedWidth;
            monitor.assignedHeight = output.assignedHeight;
            monitor.virtualX = virtualX;
            monitor.virtualY = 0;
            monitor.width = assignedWidth;
            monitor.height = assignedHeight;
            monitor.enabled = true;
            monitor.operational = true;
            monitor.connectorEnabled = output.connectorEnabled;
            monitor.resourceBound = output.resourceBound;
            monitor.backingAttached = output.backingAttached;
            monitor.transferReady = output.transferReady;
            monitor.presentReady = output.presentReady;
            monitor.presentationReady = output.presentReady;
            monitor.presentationConfirmed = output.presentationConfirmed;
            monitor.primary = output.primary;
            monitor.primaryCapable = true;
            monitor.mirrorCapable = true;
            monitor.extendCapable = true;
            monitor.backingVirtualAddress = output.backingVirtualAddress;
            monitor.backingByteCount = output.backingByteCount;
            monitor.backingMemEntryCount = output.backingMemEntryCount;
            monitor.patternChecksum = output.patternChecksum;
            monitor.lastCommandStatus = output.lastCommandStatus;
            inventory.monitors.push_back(monitor);

            DisplayViewport viewport;
            viewport.index = static_cast<int>(index + 1);
            viewport.originX = virtualX;
            viewport.originY = 0;
            viewport.width = assignedWidth;
            viewport.height = assignedHeight;
            viewport.syntheticHosted = false;
            viewport.source = output.source;
            viewport.scanoutId = output.scanoutId;
            viewport.resourceId = output.resourceId;
            viewport.connectorEnabled = output.connectorEnabled;
            viewport.resourceBound = output.resourceBound;
            viewport.backingAttached = output.backingAttached;
            viewport.transferReady = output.transferReady;
            viewport.presentReady = output.presentReady;
            viewport.presentationConfirmed = output.presentationConfirmed;
            viewport.monitorId = monitor.id;
            viewport.monitorName = monitor.name;
            viewport.modeId = monitor.modeId;
            viewport.preferredX = output.preferredX;
            viewport.preferredY = output.preferredY;
            viewport.preferredWidth = output.preferredWidth;
            viewport.preferredHeight = output.preferredHeight;
            viewport.assignedX = output.assignedX;
            viewport.assignedY = output.assignedY;
            viewport.assignedWidth = output.assignedWidth;
            viewport.assignedHeight = output.assignedHeight;
            viewport.backingVirtualAddress = output.backingVirtualAddress;
            viewport.backingByteCount = output.backingByteCount;
            viewport.backingMemEntryCount = output.backingMemEntryCount;
            viewport.patternChecksum = output.patternChecksum;
            viewport.lastCommandStatus = output.lastCommandStatus;
            inventory.viewports.push_back(viewport);

            DisplayRenderTarget target = makeDisplayRenderTarget(
                static_cast<int>(index + 1),
                monitor,
                true,
                false,
                false);
            target.targetId = std::string("virtio-gpu-target-") + std::to_string(index + 1);
            target.source = output.source;
            target.modeId = monitor.modeId;
            target.scanoutId = output.scanoutId;
            target.resourceId = output.resourceId;
            target.viewportOriginX = virtualX;
            target.viewportOriginY = 0;
            target.width = assignedWidth;
            target.height = assignedHeight;
            target.framebufferRect = DisplayRect{ 0, 0, assignedWidth, assignedHeight };
            target.preferredX = output.preferredX;
            target.preferredY = output.preferredY;
            target.preferredWidth = output.preferredWidth;
            target.preferredHeight = output.preferredHeight;
            target.assignedX = output.assignedX;
            target.assignedY = output.assignedY;
            target.assignedWidth = output.assignedWidth;
            target.assignedHeight = output.assignedHeight;
            target.primary = output.primary;
            target.active = true;
            target.backedByHostedFramebuffer = false;
            target.backedByOutputResource = true;
            target.connectorEnabled = output.connectorEnabled;
            target.resourceBound = output.resourceBound;
            target.backingAttached = output.backingAttached;
            target.transferReady = output.transferReady;
            target.presentReady = output.presentReady;
            target.presentationConfirmed = output.presentationConfirmed;
            target.syntheticHosted = false;
            target.backingVirtualAddress = output.backingVirtualAddress;
            target.backingByteCount = output.backingByteCount;
            target.backingMemEntryCount = output.backingMemEntryCount;
            target.patternChecksum = output.patternChecksum;
            target.lastCommandStatus = output.lastCommandStatus;
            inventory.renderTargets.push_back(target);

            if (output.connectorEnabled) {
                ++inventory.protocolConnectorEnabledCount;
            }
            if (output.presentationConfirmed) {
                ++inventory.presentationConfirmedCount;
            }

            virtualX += assignedWidth;
        }

        inventory.operationalOutputCount = static_cast<uint32_t>(inventory.monitors.size());
        inventory.targetCount = static_cast<uint32_t>(inventory.renderTargets.size());
        inventory.backedTargetCount = 0;
        for (const auto& target : inventory.renderTargets) {
            if (target.backedByHostedFramebuffer || target.backedByOutputResource) {
                ++inventory.backedTargetCount;
            }
        }

        if (!havePrimary && !inventory.monitors.empty()) {
            inventory.primaryOutput = inventory.monitors.front().scanoutId;
            inventory.monitors.front().primary = true;
            inventory.renderTargets.front().primary = true;
            inventory.viewports.front().monitorId = inventory.monitors.front().id;
            inventory.viewports.front().monitorName = inventory.monitors.front().name;
        }

        inventory.virtualDesktop.monitors = inventory.monitors;
        inventory.virtualDesktop.mode = inventory.monitors.size() > 1 ? DisplayModeKind::Extend : DisplayModeKind::Mirror;
        inventory.virtualDesktop.recomputeBounds();

        return inventory;
    }

    static std::vector<DisplayMonitorDescriptor> buildVirtioGpuDisplayMonitors(const VirtioGpuOutputInventory& inventory)
    {
        return inventory.monitors;
    }

    static std::vector<DisplayViewport> buildVirtioGpuDisplayViewports(const VirtioGpuOutputInventory& inventory)
    {
        return inventory.viewports;
    }

    static std::vector<DisplayRenderTarget> buildVirtioGpuDisplayTargets(const VirtioGpuOutputInventory& inventory)
    {
        return inventory.renderTargets;
    }

    static DetectedDisplayInventory makeDetectedDisplayInventory(const VirtioGpuOutputInventory& inventory)
    {
        DetectedDisplayInventory detected;
        detected.backend = "virtio-gpu";
        detected.qemuOnly = inventory.qemuOnly;
        detected.backendGateActive = inventory.qemuOnly;
        detected.monitors = inventory.monitors;
        detected.renderTargets = inventory.renderTargets;
        detected.currentDesktop = inventory.virtualDesktop;
        return detected;
    }

    static bool presentVirtioGpuTarget(DisplayRenderTarget& target, const VirtioGpuScanoutState& output)
    {
        target.source = output.source;
        target.scanoutId = output.scanoutId;
        target.resourceId = output.resourceId;
        target.connectorEnabled = output.connectorEnabled;
        target.resourceBound = output.resourceBound;
        target.backingAttached = output.backingAttached;
        target.transferReady = output.transferReady;
        target.presentReady = output.presentReady;
        target.presentationConfirmed = output.presentationConfirmed;
        target.backedByOutputResource = output.resourceBound;
        target.backingVirtualAddress = output.backingVirtualAddress;
        target.backingByteCount = output.backingByteCount;
        target.backingMemEntryCount = output.backingMemEntryCount;
        target.patternChecksum = output.patternChecksum;
        target.lastCommandStatus = output.lastCommandStatus;
        return target.active && target.backedByOutputResource;
    }

    static bool updateVirtioGpuTarget(DisplayRenderTarget& target, const VirtioGpuScanoutState& output)
    {
        return presentVirtioGpuTarget(target, output);
    }
};

} // namespace gui
} // namespace gxos
#endif
