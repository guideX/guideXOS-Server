#include "task_manager.h"
#include "gui_protocol.h"
#include "logger.h"
#include "allocator.h"
#include "scheduler.h"
#include "compositor.h"
#include "desktop_service.h"
#include "built_in_app_metadata.h"
#include "native_app_process_table.h"
#include "fs.h"
#include "network_telemetry.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cctype>
#include <unordered_map>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t TaskManager::s_windowId = 0;
    TaskManagerSnapshot TaskManager::s_snapshot{};
    std::vector<ProcessInfo> TaskManager::s_processes;
    int TaskManager::s_selectedIndex = 0;
    int TaskManager::s_scrollOffset = 0;
    uint64_t TaskManager::s_lastRefreshTicks = 0;
    int TaskManager::s_lastKeyCode = 0;
    bool TaskManager::s_keyDown = false;
    
    uint64_t TaskManager::s_totalMemory = 0;
    uint64_t TaskManager::s_usedMemory = 0;
    uint64_t TaskManager::s_peakMemory = 0;
    uint64_t TaskManager::s_tasksExecuted = 0;
    
    int TaskManager::s_currentTab = 0;
    int TaskManager::s_selectedTombIndex = -1;
    int TaskManager::s_cpuPct = 0;
    uint64_t TaskManager::s_cpuHistory[TaskManager::kCpuHistoryMax] = {};
    int TaskManager::s_cpuHistoryCount = 0;
    int TaskManager::s_cpuHistoryHead = 0;
    int TaskManager::s_memPct = 0;
    int TaskManager::s_diskPct = 0;
    uint64_t TaskManager::s_diskHistory[TaskManager::kDiskHistoryMax] = {};
    int TaskManager::s_diskHistoryCount = 0;
    int TaskManager::s_diskHistoryHead = 0;
    int TaskManager::s_netPct = 0;
    int TaskManager::s_perfCategoryIndex = 0;
    uint64_t TaskManager::s_memoryHistory[TaskManager::kMemoryHistoryMax] = {};
    int TaskManager::s_memoryHistoryCount = 0;
    int TaskManager::s_memoryHistoryHead = 0;
    
    // Memory Details tab
    uint64_t TaskManager::s_cumulativeAllocated = 0;
    uint64_t TaskManager::s_cumulativeFreed = 0;
    uint64_t TaskManager::s_lastMemDetailUpdate = 0;
    bool TaskManager::s_leakExists = false;
    int TaskManager::s_leakGrowthCounter = 0;
    std::vector<uint64_t> TaskManager::s_leakHistory;

    namespace {
        constexpr uint64_t kProcessCpuTelemetryDisplayWindowMicros = 1000ULL * 1000ULL;

        struct ProcessCpuSampleState {
            uint64_t lastWallMicros = 0;
            uint64_t lastCpuMicros = 0;
            uint64_t pendingWindowMicros = 0;
            uint64_t pendingCpuMicros = 0;
            bool hasLastSample = false;
            bool hasStableSample = false;
            int stablePct = 0;
            uint64_t stableWindowMs = 0;
            std::string stableSource = "processThreadCpuTimeWarmup";
        };

        static std::unordered_map<uint64_t, ProcessCpuSampleState> s_processCpuSamples;

        static uint64_t steadyClockMicros() {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        static uint64_t microsToMillisCeil(uint64_t micros) {
            return micros == 0 ? 0 : ((micros + 999ULL) / 1000ULL);
        }

        static std::string processCpuPctOrNa(bool available, int pct) {
            if (!available) return "N/A";
            return std::to_string(pct) + "%";
        }

        static std::string diskRateSummary(const gxos::apps::PerformanceSnapshot& perf) {
            if (!perf.diskAvailable) return "N/A";
            if (perf.diskActivePctAvailable) return std::to_string(perf.diskPct) + "%";
            return std::to_string(perf.diskReadKBps) + "/" + std::to_string(perf.diskWriteKBps) + " KB/s";
        }

        static std::string diskRateDetail(const gxos::apps::PerformanceSnapshot& perf, bool readRate) {
            if (!perf.diskAvailable) return "N/A";
            const uint64_t kbps = readRate ? perf.diskReadKBps : perf.diskWriteKBps;
            return std::to_string(kbps) + " KB/s";
        }

        static std::string networkRateSummary(const gxos::apps::PerformanceSnapshot& perf) {
            if (!perf.networkAvailable) return "N/A";
            if (perf.networkUtilizationAvailable) return std::to_string(perf.networkPct) + "%";
            return std::to_string(perf.networkSendKBps) + "/" + std::to_string(perf.networkReceiveKBps) + " KB/s";
        }

        static std::string networkRateDetail(const gxos::apps::PerformanceSnapshot& perf, bool sendRate) {
            if (!perf.networkAvailable) return "N/A";
            const uint64_t kbps = sendRate ? perf.networkSendKBps : perf.networkReceiveKBps;
            return std::to_string(kbps) + " KB/s";
        }

        static void updateProcessCpuSample(uint64_t pid,
                                           const gxos::ProcessCpuTelemetry& telemetry,
                                           ProcessSnapshot& out,
                                           uint64_t nowMicros) {
            ProcessCpuSampleState& state = s_processCpuSamples[pid];

            if (!telemetry.available) {
                out.cpuPctAvailable = false;
                out.cpuPct = 0;
                out.cpuSampleWindowMs = 0;
                out.cpuSource = "N/A";
                if (state.hasStableSample) {
                    out.cpuPctAvailable = true;
                    out.cpuPct = state.stablePct;
                    out.cpuSampleWindowMs = state.stableWindowMs;
                    out.cpuSource = state.stableSource;
                }
                return;
            }

            if (!state.hasLastSample) {
                state.hasLastSample = true;
                state.lastWallMicros = telemetry.startWallMicros ? telemetry.startWallMicros : nowMicros;
                state.lastCpuMicros = 0;
            }

            const uint64_t wallDeltaMicros = nowMicros > state.lastWallMicros ? nowMicros - state.lastWallMicros : 0;
            const uint64_t cpuDeltaMicros = telemetry.cpuMicros > state.lastCpuMicros ? telemetry.cpuMicros - state.lastCpuMicros : 0;
            state.lastWallMicros = nowMicros;
            state.lastCpuMicros = telemetry.cpuMicros;
            state.pendingWindowMicros += wallDeltaMicros;
            state.pendingCpuMicros += cpuDeltaMicros;

            if (state.pendingWindowMicros >= kProcessCpuTelemetryDisplayWindowMicros) {
                const uint64_t displayWindowMicros = state.pendingWindowMicros;
                const uint64_t displayCpuMicros = state.pendingCpuMicros > displayWindowMicros ? displayWindowMicros : state.pendingCpuMicros;
                const uint64_t rawPct = displayWindowMicros == 0 ? 0 : ((displayCpuMicros * 100ULL) / displayWindowMicros);
                state.stablePct = static_cast<int>(std::min<uint64_t>(100ULL, rawPct));
                state.stableWindowMs = microsToMillisCeil(displayWindowMicros);
                state.stableSource = telemetry.source ? telemetry.source : "processThreadCpuTime";
                state.hasStableSample = true;
                state.pendingWindowMicros = 0;
                state.pendingCpuMicros = 0;

                out.cpuPctAvailable = true;
                out.cpuPct = state.stablePct;
                out.cpuSampleWindowMs = state.stableWindowMs;
                out.cpuSource = state.stableSource;
                return;
            }

            if (state.hasStableSample) {
                out.cpuPctAvailable = true;
                out.cpuPct = state.stablePct;
                out.cpuSampleWindowMs = state.stableWindowMs;
                out.cpuSource = state.stableSource;
                return;
            }

            out.cpuPctAvailable = false;
            out.cpuPct = 0;
            out.cpuSampleWindowMs = microsToMillisCeil(state.pendingWindowMicros);
            out.cpuSource = "processThreadCpuTimeWarmup";
        }
    } // namespace

    uint64_t TaskManager::Launch() {
        ProcessSpec spec{"task_manager", TaskManager::main};
        spec.appId = "gxos.builtin.taskmanager";
        return ProcessTable::spawn(spec, {"task_manager"});
    }

    namespace {
        static void publishTextAt(uint64_t windowId, int x, int y, const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawTextAt;
            std::ostringstream oss;
            oss << windowId << "|" << x << "|" << y << "|" << text;
            const std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        }

        static void publishTextAtColor(uint64_t windowId, int x, int y, uint8_t r, uint8_t g, uint8_t b, const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawTextAtColor;
            std::ostringstream oss;
            oss << windowId << "|" << x << "|" << y << "|" << static_cast<int>(r) << "|" << static_cast<int>(g) << "|" << static_cast<int>(b) << "|" << text;
            const std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        }

        static void publishRect(uint64_t windowId, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawRect;
            std::ostringstream oss;
            oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|"
                << static_cast<int>(r) << "|" << static_cast<int>(g) << "|" << static_cast<int>(b);
            const std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        }

        static void clearWindow(uint64_t windowId) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            const std::string payload = std::to_string(windowId) + "|\f";
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        }

        static std::string pctOrNa(bool available, int pct) {
            if (!available) return "N/A";
            return std::to_string(pct) + "%";
        }

        static void appendHistorySample(uint64_t* history, int historyMax, int& count, int& head, int value) {
            if (historyMax <= 0) return;
            history[head] = static_cast<uint64_t>(std::max(0, std::min(100, value)));
            head = (head + 1) % historyMax;
            if (count < historyMax) {
                ++count;
            }
        }

        static std::string trimDisplayName(const std::string& text, size_t maxChars) {
            if (text.size() <= maxChars) return text;
            if (maxChars <= 3) return text.substr(0, maxChars);
            return text.substr(0, maxChars - 3) + "...";
        }

        static void drawGraphBox(uint64_t windowId,
                                 int x,
                                 int y,
                                 int w,
                                 int h,
                                 const std::string& title,
                                 const std::string& valueText,
                                 bool hasData,
                                 const uint64_t* history,
                                 int historyCount,
                                 int historyHead,
                                 uint8_t accentR,
                                 uint8_t accentG,
                                 uint8_t accentB) {
            publishRect(windowId, x, y, w, h, 0x22, 0x22, 0x22);
            publishRect(windowId, x, y, w, 1, 0x44, 0x44, 0x44);
            publishRect(windowId, x, y + h - 1, w, 1, 0x44, 0x44, 0x44);
            publishRect(windowId, x, y, 1, h, 0x44, 0x44, 0x44);
            publishRect(windowId, x + w - 1, y, 1, h, 0x44, 0x44, 0x44);
            publishTextAtColor(windowId, x + 8, y + 6, accentR, accentG, accentB, title);
            publishTextAt(windowId, x + w - 72, y + 6, valueText);

            const int plotX = x + 8;
            const int plotY = y + 22;
            const int plotW = w - 16;
            const int plotH = h - 30;

            if (!hasData || historyCount <= 0 || plotW <= 0 || plotH <= 0) {
                publishRect(windowId, plotX, plotY, plotW, plotH, 0x1B, 0x1B, 0x1B);
                publishTextAtColor(windowId, plotX + 12, plotY + plotH / 2 - 4, 160, 160, 160, "N/A");
                return;
            }

            publishRect(windowId, plotX, plotY, plotW, plotH, 0x19, 0x19, 0x19);
            if (historyCount < 2) {
                publishTextAtColor(windowId, plotX + 12, plotY + plotH / 2 - 4, accentR, accentG, accentB, valueText);
                return;
            }

            const int barCount = historyCount < plotW ? historyCount : plotW;
            const int columnWidth = barCount > 0 ? std::max(1, plotW / barCount) : 1;
            const int start = historyCount < barCount ? 0 : historyCount - barCount;
            constexpr int kHistoryMax = 48;
            for (int i = 0; i < barCount; ++i) {
                const int idx = (historyHead - historyCount + start + i + kHistoryMax) % kHistoryMax;
                const int valuePct = static_cast<int>(history[idx]);
                const int barHeight = std::max(1, (plotH * valuePct) / 100);
                const int barX = plotX + i * columnWidth;
                const int barY = plotY + plotH - barHeight;
                publishRect(windowId, barX, barY, std::max(1, columnWidth - 1), barHeight, accentR, accentG, accentB);
            }
        }

        struct TombstoneCapabilityStatus {
            bool known = false;
            bool capable = false;
            std::string source = "N/A";
        };

        static const apps::BuiltInAppMetadata* findBuiltInMetadataForTombstone(const gxos::ProcessTombstoneRecord& tombstone) {
            if (tombstone.appId.empty()) return nullptr;
            return apps::FindBuiltInAppMetadataByAppId(tombstone.appId.c_str());
        }

        static TombstoneCapabilityStatus resolveTombstoneCapability(const gxos::ProcessTombstoneRecord& tombstone) {
            TombstoneCapabilityStatus status;
            const apps::BuiltInAppMetadata* metadata = findBuiltInMetadataForTombstone(tombstone);
            if (!metadata) {
                status.source = tombstone.appId.empty() ? "N/A" : "UnknownApp";
                return status;
            }

            status.known = true;
            status.capable = apps::CanBuiltInAppTombstone(*metadata);
            status.source = "appModelMetadata";
            return status;
        }

        static std::string formatTombstoneCapabilityText(const gxos::ProcessTombstoneRecord& tombstone) {
            if (!tombstone.appTombstoneCapabilityKnown) return "N/A";
            return tombstone.appTombstoneCapable ? "Supported" : "Unsupported";
        }

        static std::string formatTombstoneCapabilityFlag(const gxos::ProcessTombstoneRecord& tombstone) {
            if (!tombstone.appTombstoneCapabilityKnown) return "N/A";
            return tombstone.appTombstoneCapable ? "true" : "false";
        }

        static std::string allocTagName(gxos::AllocTag tag) {
            switch (tag) {
                case gxos::AllocTag::Unknown: return "Unknown";
                case gxos::AllocTag::ThreadMeta: return "ThreadMeta";
                case gxos::AllocTag::ThreadStack: return "ThreadStack";
                case gxos::AllocTag::ExecImage: return "ExecImage";
                case gxos::AllocTag::Image: return "Image";
                case gxos::AllocTag::FileBuffer: return "FileBuffer";
                case gxos::AllocTag::Temp: return "Temp";
                case gxos::AllocTag::Count: break;
            }
            return "Unknown";
        }
    }

    TaskManagerSnapshot TaskManager::BuildTaskManagerSnapshot() {
        TaskManagerSnapshot snapshot;

        const std::vector<uint64_t> pidList = ProcessTable::list();
        const std::vector<gxos::apps::NativeAppProcessInfo> nativeProcesses = NativeAppProcessTable::List();
        const std::vector<gxos::gui::WindowDebugInfo> windows = gxos::gui::Compositor::debugWindowsSnapshot();
        const std::vector<std::pair<uint64_t, uint64_t>> pidBytes = Allocator::listPidBytes();

        std::unordered_map<uint64_t, uint64_t> bytesByPid;
        for (const auto& entry : pidBytes) {
            bytesByPid[entry.first] = entry.second;
        }

        std::unordered_map<uint64_t, const gxos::apps::NativeAppProcessInfo*> nativeByPid;
        for (const auto& process : nativeProcesses) {
            if (process.nativePid != 0) nativeByPid[process.nativePid] = &process;
            if (process.runtimeId != 0) nativeByPid[process.runtimeId] = &process;
        }

        std::unordered_map<uint64_t, std::string> windowTitleByPid;
        for (const auto& window : windows) {
            if (window.ownerPid == 0 || window.title.empty()) continue;
            auto it = windowTitleByPid.find(window.ownerPid);
            if (it == windowTitleByPid.end() || it->second.empty()) {
                windowTitleByPid[window.ownerPid] = window.title;
            }
        }

        snapshot.memory.totalBytes = Allocator::totalSize();
        snapshot.memory.totalAvailable = snapshot.memory.totalBytes > 0;
        snapshot.memory.usedBytes = Allocator::bytesInUse();
        snapshot.memory.peakBytes = Allocator::peakBytes();
        snapshot.memory.freeBytes = snapshot.memory.totalBytes > snapshot.memory.usedBytes
            ? snapshot.memory.totalBytes - snapshot.memory.usedBytes
            : 0;
        snapshot.memory.totalFreedBytes = Allocator::totalFreed();
        snapshot.memory.totalFreedAvailable = true;
        snapshot.memory.heapUtilPctAvailable = snapshot.memory.totalAvailable && snapshot.memory.totalBytes > 0;
        if (snapshot.memory.heapUtilPctAvailable) {
            snapshot.memory.heapUtilPct = static_cast<int>((snapshot.memory.usedBytes * 100) / snapshot.memory.totalBytes);
        }
        snapshot.memory.leakStateAvailable = true;
        snapshot.memory.leakState = s_leakExists;
        if (snapshot.memory.totalFreedAvailable) {
            const uint64_t allocatedBytes = snapshot.memory.usedBytes + snapshot.memory.totalFreedBytes;
            if (allocatedBytes > 0) {
                snapshot.memory.freeAllocRatioAvailable = true;
                snapshot.memory.freeAllocRatioPct = static_cast<int>((snapshot.memory.totalFreedBytes * 100) / allocatedBytes);
            }
        }

        uint64_t maxTagBytes = 0;
        gxos::AllocTag maxTag = gxos::AllocTag::Unknown;
        for (int tagIndex = 0; tagIndex < static_cast<int>(gxos::AllocTag::Count); ++tagIndex) {
            gxos::AllocTag tag = static_cast<gxos::AllocTag>(tagIndex);
            uint64_t tagBytes = Allocator::tagBytes(tag);
            if (tagBytes > maxTagBytes) {
                maxTagBytes = tagBytes;
                maxTag = tag;
            }
        }
        if (maxTagBytes > 0) {
            snapshot.memory.topTagAvailable = true;
            snapshot.memory.topTagName = allocTagName(maxTag);
            snapshot.memory.topTagBytes = maxTagBytes;
        }

        uint64_t topOwnerPid = 0;
        uint64_t topOwnerBytes = 0;
        for (const auto& entry : pidBytes) {
            if (entry.second > topOwnerBytes) {
                topOwnerPid = entry.first;
                topOwnerBytes = entry.second;
            }
        }
        if (topOwnerBytes > 0) {
            snapshot.memory.topOwnerAvailable = true;
            snapshot.memory.topOwnerPid = topOwnerPid;
            snapshot.memory.topOwnerBytes = topOwnerBytes;
        }

        snapshot.performance.processCount = pidList.size();
        snapshot.performance.nativeProcessCount = nativeProcesses.size();
        snapshot.performance.windowCount = windows.size();
        snapshot.performance.schedulerTasksExecuted = Scheduler::tasksExecuted();
        snapshot.performance.memoryAvailable = snapshot.memory.totalAvailable;
        snapshot.performance.memoryPct = snapshot.memory.heapUtilPctAvailable ? snapshot.memory.heapUtilPct : 0;
        const CpuTelemetrySnapshot cpuTelemetry = Scheduler::cpuTelemetrySnapshot();
        snapshot.performance.cpuAvailable = cpuTelemetry.available;
        snapshot.performance.cpuPct = cpuTelemetry.utilizationPct;
        snapshot.performance.cpuSource = cpuTelemetry.source;
        snapshot.performance.cpuSampleWindowMs = cpuTelemetry.sampleWindowMs;
        snapshot.performance.cpuBusyTimeMs = cpuTelemetry.busyTimeMs;
        snapshot.performance.cpuIdleTimeMs = cpuTelemetry.idleTimeMs;
        snapshot.performance.processCpuAvailable = false;
        snapshot.performance.processCpuSource = "N/A";
        snapshot.performance.processCpuSampleWindowMs = 0;
        snapshot.performance.processCpuRowsWithCpu = 0;
        const gxos::DiskTelemetrySnapshot diskTelemetry = gxos::FS::telemetrySnapshot();
        snapshot.performance.diskAvailable = diskTelemetry.available;
        snapshot.performance.diskSource = diskTelemetry.available ? diskTelemetry.source : "N/A";
        snapshot.performance.diskSampleWindowMs = diskTelemetry.available ? diskTelemetry.sampleWindowMs : 0;
        snapshot.performance.diskReadBytesTotal = diskTelemetry.readBytesTotal;
        snapshot.performance.diskWriteBytesTotal = diskTelemetry.writeBytesTotal;
        snapshot.performance.diskReadKBps = diskTelemetry.available ? diskTelemetry.readKBps : 0;
        snapshot.performance.diskWriteKBps = diskTelemetry.available ? diskTelemetry.writeKBps : 0;
        snapshot.performance.diskActivePctAvailable = diskTelemetry.activePctAvailable;
        snapshot.performance.diskPct = diskTelemetry.activePct;
        const gxos::net::NetworkTelemetrySnapshot networkTelemetry = gxos::net::networkTelemetrySnapshot();
        snapshot.performance.networkAvailable = networkTelemetry.available;
        snapshot.performance.networkSource = networkTelemetry.available ? networkTelemetry.source : "N/A";
        snapshot.performance.networkSampleWindowMs = networkTelemetry.available ? networkTelemetry.sampleWindowMs : 0;
        snapshot.performance.networkBytesSentTotal = networkTelemetry.bytesSentTotal;
        snapshot.performance.networkBytesReceivedTotal = networkTelemetry.bytesReceivedTotal;
        snapshot.performance.networkSendKBps = networkTelemetry.available ? networkTelemetry.sendKBps : 0;
        snapshot.performance.networkReceiveKBps = networkTelemetry.available ? networkTelemetry.receiveKBps : 0;
        snapshot.performance.networkUtilizationAvailable = networkTelemetry.available && networkTelemetry.utilizationPctAvailable;
        snapshot.performance.networkPct = networkTelemetry.utilizationPct;
        snapshot.performance.processDiskAvailable = false;
        snapshot.performance.processNetworkAvailable = false;
        snapshot.performance.syntheticCounters = false;
        const uint64_t nowMicros = steadyClockMicros();
        snapshot.tombstoneAppCapabilityKnown = 0;
        snapshot.tombstoneRowsWithAppId = 0;
        snapshot.tombstoneRowsWithPolicy = 0;

        std::unordered_map<uint64_t, bool> livePids;
        for (uint64_t pid : pidList) {
            livePids[pid] = true;
        }

        for (uint64_t pid : pidList) {
            ProcessSnapshot info;
            info.pid = pid;

            bool running = false;
            int exitCode = 0;
            if (ProcessTable::getStatus(pid, running, exitCode)) {
                info.running = running;
                info.exitCode = exitCode;
            }

            auto bytesIt = bytesByPid.find(pid);
            if (bytesIt != bytesByPid.end()) {
                info.memoryBytes = bytesIt->second;
            }

            std::string processName;
            std::string processAppId;
            if (ProcessTable::getIdentity(pid, processName, processAppId)) {
                if (!processName.empty()) info.displayName = processName;
                if (!processAppId.empty()) info.appId = processAppId;
            }

            auto nativeIt = nativeByPid.find(pid);
            if (nativeIt != nativeByPid.end() && nativeIt->second) {
                const auto& native = *nativeIt->second;
                info.appId = native.appId;
                info.displayName = !native.displayName.empty() ? native.displayName :
                                   (!native.appId.empty() ? native.appId : std::string());
                auto windowTitleIt = windowTitleByPid.find(pid);
                if (windowTitleIt != windowTitleByPid.end()) {
                    info.windowTitle = windowTitleIt->second;
                }
            } else {
                auto windowTitleIt = windowTitleByPid.find(pid);
                if (windowTitleIt != windowTitleByPid.end()) {
                    info.windowTitle = windowTitleIt->second;
                    if (info.displayName.empty()) info.displayName = windowTitleIt->second;
                }
            }

            if (!info.appId.empty()) {
                if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByAppId(info.appId.c_str())) {
                    if (metadata->displayName && metadata->displayName[0] != '\0') {
                        info.displayName = metadata->displayName;
                    }
                }
            }

            if (info.displayName.empty()) {
                if (!info.windowTitle.empty()) {
                    info.displayName = info.windowTitle;
                } else if (!info.appId.empty()) {
                    info.displayName = info.appId;
                } else {
                    info.displayName = std::string("Process-") + std::to_string(pid);
                }
            }

            gxos::ProcessCpuTelemetry processCpuTelemetry;
            if (ProcessTable::cpuTelemetry(pid, processCpuTelemetry)) {
                updateProcessCpuSample(pid, processCpuTelemetry, info, nowMicros);
            } else {
                info.cpuPctAvailable = false;
                info.cpuPct = 0;
                info.cpuSampleWindowMs = 0;
                info.cpuSource = "N/A";
            }

            if (info.cpuPctAvailable) {
                ++snapshot.performance.processCpuRowsWithCpu;
                snapshot.performance.processCpuAvailable = true;
                snapshot.performance.processCpuSource = info.cpuSource;
                snapshot.performance.processCpuSampleWindowMs = info.cpuSampleWindowMs;
            } else if (snapshot.performance.processCpuSource == "N/A" && info.cpuSource != "N/A") {
                snapshot.performance.processCpuSource = info.cpuSource;
                snapshot.performance.processCpuSampleWindowMs = info.cpuSampleWindowMs;
            }

            info.diskPctAvailable = false;
            info.networkPctAvailable = false;
            snapshot.processes.push_back(info);
        }

        for (auto it = s_processCpuSamples.begin(); it != s_processCpuSamples.end();) {
            if (livePids.find(it->first) == livePids.end()) {
                it = s_processCpuSamples.erase(it);
            } else {
                ++it;
            }
        }

        std::sort(snapshot.processes.begin(), snapshot.processes.end(),
            [](const ProcessSnapshot& a, const ProcessSnapshot& b) {
                return a.pid < b.pid;
            });

        std::unordered_map<uint64_t, const ProcessSnapshot*> processByPid;
        for (const auto& process : snapshot.processes) {
            processByPid[process.pid] = &process;
        }

        snapshot.tombstoneDetailsAvailable = true;
        snapshot.tombstoneDiagnosticHistoryAvailable = true;
        snapshot.appTombstonePolicyAvailable = apps::kBuiltInAppMetadataCount > 0;
        snapshot.tombstoneCapabilitySource = snapshot.appTombstonePolicyAvailable ? "appModelMetadata" : "N/A";
        snapshot.tombstoneRestoreImplemented = false;
        snapshot.tombstoneRestoreSupported = false;
        snapshot.tombstoneHistoryCapacity = ProcessTable::kTombstoneHistoryMax;
        snapshot.tombstoned = ProcessTable::tombstones();
        for (auto& tombstone : snapshot.tombstoned) {
            auto processIt = processByPid.find(tombstone.pid);
            if (processIt != processByPid.end() && processIt->second) {
                const ProcessSnapshot& process = *processIt->second;
                if (tombstone.displayName.empty()) tombstone.displayName = process.displayName;
                if (tombstone.appId.empty()) tombstone.appId = process.appId;
                if (tombstone.windowTitle.empty()) tombstone.windowTitle = process.windowTitle;
                tombstone.finalMemoryBytesAvailable = true;
                tombstone.finalMemoryBytes = process.memoryBytes;
                if (process.cpuPctAvailable) {
                    tombstone.finalCpuPctAvailable = true;
                    tombstone.finalCpuPct = process.cpuPct;
                }
                if (!tombstone.exitCodeAvailable && !process.running) {
                    tombstone.exitCodeAvailable = true;
                    tombstone.exitCode = process.exitCode;
                }
            }

            if (tombstone.windowTitle.empty()) {
                auto windowTitleIt = windowTitleByPid.find(tombstone.pid);
                if (windowTitleIt != windowTitleByPid.end()) {
                    tombstone.windowTitle = windowTitleIt->second;
                }
            }

            if (tombstone.displayName.empty()) {
                if (!tombstone.windowTitle.empty()) {
                    tombstone.displayName = tombstone.windowTitle;
                } else if (!tombstone.appId.empty()) {
                    tombstone.displayName = tombstone.appId;
                } else {
                    tombstone.displayName = std::string("Process-") + std::to_string(tombstone.pid);
                }
            }

            if (tombstone.reason.empty()) {
                tombstone.reason = "Unknown";
            }

            const TombstoneCapabilityStatus capability = resolveTombstoneCapability(tombstone);
            tombstone.appTombstoneCapabilityKnown = capability.known;
            tombstone.appTombstoneCapable = capability.capable;
            tombstone.appTombstoneCapabilitySource = capability.source;
            if (capability.known) {
                ++snapshot.tombstoneAppCapabilityKnown;
                ++snapshot.tombstoneRowsWithPolicy;
            }
            if (!tombstone.appId.empty()) {
                ++snapshot.tombstoneRowsWithAppId;
            }
        }

        snapshot.syntheticCounters = false;
        return snapshot;
    }

    std::string TaskManager::SnapshotDiagnostic() {
        TaskManagerSnapshot snapshot = BuildTaskManagerSnapshot();
        std::ostringstream oss;
        oss << "tabs=Processes,Performance,Tombstoned,Memory Details\n";
        oss << "title=Task Manager\n";
        oss << "processColumns=Name,CPU%,Memory,Disk%,Network%\n";
        oss << "performanceCategories=CPU,Memory,Disk,Network\n";
        oss << "memoryDetailsSections=Memory Allocator Details;Free() Call Statistics;Heap Allocator\n";
        oss << "taskManagerPolishCheckpoint=true\n";
        oss << "tombstoneDetailsAvailable=" << (snapshot.tombstoneDetailsAvailable ? "true" : "false") << "\n";
        oss << "tombstoneDiagnosticHistoryAvailable=" << (snapshot.tombstoneDiagnosticHistoryAvailable ? "true" : "false") << "\n";
        oss << "appTombstonePolicyAvailable=" << (snapshot.appTombstonePolicyAvailable ? "true" : "false") << "\n";
        oss << "tombstoneCapabilitySource=" << snapshot.tombstoneCapabilitySource << "\n";
        oss << "tombstoneRestoreImplemented=" << (snapshot.tombstoneRestoreImplemented ? "true" : "false") << "\n";
        oss << "tombstoneHistoryCapacity=" << snapshot.tombstoneHistoryCapacity << "\n";
        oss << "tombstoneAppCapabilityKnown=" << snapshot.tombstoneAppCapabilityKnown << "\n";
        oss << "tombstoneRowsWithAppId=" << snapshot.tombstoneRowsWithAppId << "\n";
        oss << "tombstoneRowsWithPolicy=" << snapshot.tombstoneRowsWithPolicy << "\n";
        oss << "tombstoneColumns=Name,PID,App ID,Reason,Exit,Runtime,Restore,End\n";
        oss << "tombstoneDetailsPane=true\n";
        oss << "tombstoneReasonValues=NormalExit,Terminated,Crashed,Unknown\n";
        oss << "processes=" << snapshot.performance.processCount << "\n";
        oss << "memoryUsed=" << snapshot.memory.usedBytes << "\n";
        if (snapshot.memory.totalAvailable) {
            oss << "memoryTotal=" << snapshot.memory.totalBytes << "\n";
            oss << "memoryTotalSource=allocatorHeap\n";
            oss << "memoryTotalDerived=true\n";
        } else {
            oss << "memoryTotal=N/A\n";
            oss << "memoryTotalDerived=false\n";
        }
        oss << "processCpuAvailable=" << (snapshot.performance.processCpuAvailable ? "true" : "false") << "\n";
        oss << "processCpuSource=" << snapshot.performance.processCpuSource << "\n";
        if (snapshot.performance.processCpuAvailable) {
            oss << "processCpuSampleWindowMs=" << snapshot.performance.processCpuSampleWindowMs << "\n";
        } else {
            oss << "processCpuSampleWindowMs=N/A\n";
        }
        oss << "processCpuRowsWithCpu=" << snapshot.performance.processCpuRowsWithCpu << "\n";
        if (snapshot.performance.cpuAvailable) {
            oss << "cpu=" << snapshot.performance.cpuPct << "%\n";
            oss << "cpuAvailable=true\n";
            oss << "cpuSource=" << snapshot.performance.cpuSource << "\n";
            oss << "cpuSampleWindowMs=" << snapshot.performance.cpuSampleWindowMs << "\n";
            oss << "cpuBusyTimeMs=" << snapshot.performance.cpuBusyTimeMs << "\n";
            oss << "cpuIdleTimeMs=" << snapshot.performance.cpuIdleTimeMs << "\n";
        } else {
            oss << "cpu=N/A\n";
            oss << "cpuAvailable=false\n";
            oss << "cpuSource=" << snapshot.performance.cpuSource << "\n";
            oss << "cpuSampleWindowMs=" << snapshot.performance.cpuSampleWindowMs << "\n";
            oss << "cpuBusyTimeMs=" << snapshot.performance.cpuBusyTimeMs << "\n";
            oss << "cpuIdleTimeMs=" << snapshot.performance.cpuIdleTimeMs << "\n";
        }
        oss << "diskAvailable=" << (snapshot.performance.diskAvailable ? "true" : "false") << "\n";
        if (snapshot.performance.diskAvailable) {
            oss << "disk=" << (snapshot.performance.diskActivePctAvailable ? std::to_string(snapshot.performance.diskPct) + "%" : std::string("N/A")) << "\n";
            oss << "diskSource=" << snapshot.performance.diskSource << "\n";
            oss << "diskSampleWindowMs=" << snapshot.performance.diskSampleWindowMs << "\n";
            oss << "diskReadKBps=" << snapshot.performance.diskReadKBps << "\n";
            oss << "diskWriteKBps=" << snapshot.performance.diskWriteKBps << "\n";
            oss << "diskActivePctAvailable=" << (snapshot.performance.diskActivePctAvailable ? "true" : "false") << "\n";
        } else {
            oss << "disk=N/A\n";
            oss << "diskSource=N/A\n";
            oss << "diskSampleWindowMs=N/A\n";
            oss << "diskReadKBps=N/A\n";
            oss << "diskWriteKBps=N/A\n";
            oss << "diskActivePctAvailable=false\n";
        }
        oss << "processDiskAvailable=" << (snapshot.performance.processDiskAvailable ? "true" : "false") << "\n";
        oss << "networkAvailable=" << (snapshot.performance.networkAvailable ? "true" : "false") << "\n";
        oss << "networkUtilizationAvailable=" << (snapshot.performance.networkUtilizationAvailable ? "true" : "false") << "\n";
        if (snapshot.performance.networkAvailable) {
            oss << "network=" << (snapshot.performance.networkUtilizationAvailable ? std::to_string(snapshot.performance.networkPct) + "%" : std::string("N/A")) << "\n";
            oss << "networkSource=" << snapshot.performance.networkSource << "\n";
            oss << "networkSampleWindowMs=" << snapshot.performance.networkSampleWindowMs << "\n";
            oss << "networkSendKBps=" << snapshot.performance.networkSendKBps << "\n";
            oss << "networkReceiveKBps=" << snapshot.performance.networkReceiveKBps << "\n";
        } else {
            oss << "network=N/A\n";
            oss << "networkSource=N/A\n";
            oss << "networkSampleWindowMs=N/A\n";
            oss << "networkSendKBps=N/A\n";
            oss << "networkReceiveKBps=N/A\n";
        }
        oss << "processNetworkAvailable=" << (snapshot.performance.processNetworkAvailable ? "true" : "false") << "\n";
        oss << "unavailableGraphLabel=N/A\n";
        oss << "tombstoned=" << snapshot.tombstoned.size() << "\n";
        oss << "tombstoneRestoreSupported=" << (snapshot.tombstoneRestoreSupported ? "true" : "false") << "\n";
        oss << "tombstoneEndSupported=false\n";
        oss << "tombstonedColumns=Name,PID,App ID,Reason,Exit,Runtime,Restore,End\n";
        oss << "tombstonedRestoreSupported=false\n";
        oss << "tombstonedEndSupported=false\n";
        for (const auto& tomb : snapshot.tombstoned) {
            oss << "tombstoneRow pid=" << tomb.pid
                << " displayName=" << tomb.displayName
                << " appId=" << (tomb.appId.empty() ? std::string("N/A") : tomb.appId)
                << " reason=" << (tomb.reason.empty() ? std::string("Unknown") : tomb.reason)
                << " exitCode=" << (tomb.exitCodeAvailable ? std::to_string(tomb.exitCode) : std::string("N/A"))
                << " runtimeMs=" << (tomb.runtimeMsAvailable ? std::to_string(tomb.runtimeMs) : std::string("N/A"))
                << " appTombstoneCapable=" << formatTombstoneCapabilityFlag(tomb)
                << " appTombstoneCapabilitySource=" << tomb.appTombstoneCapabilitySource
                << " restoreSupported=" << (tomb.restoreSupported ? "true" : "false")
                << " windowTitle=" << (tomb.windowTitle.empty() ? std::string("N/A") : tomb.windowTitle)
                << " lastMessage=" << (tomb.lastMessage.empty() ? std::string("N/A") : tomb.lastMessage)
                << "\n";
        }
        for (const auto& process : snapshot.processes) {
            oss << "processRow pid=" << process.pid
                << " appId=" << (process.appId.empty() ? std::string("N/A") : process.appId)
                << " cpuPctAvailable=" << (process.cpuPctAvailable ? "true" : "false")
                << " cpuPct=" << processCpuPctOrNa(process.cpuPctAvailable, process.cpuPct)
                << " cpuSampleWindowMs=" << (process.cpuPctAvailable ? std::to_string(process.cpuSampleWindowMs) : std::string("N/A"))
                << " cpuSource=" << (process.cpuPctAvailable ? process.cpuSource : std::string("N/A"))
                << " running=" << (process.running ? "true" : "false")
                << "\n";
        }
        oss << "syntheticCounters=" << (snapshot.syntheticCounters ? "true" : "false") << "\n";
        return oss.str();
    }

    int TaskManager::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "TaskManager starting...");
            
            // Initialize state
            s_windowId = 0;
            s_snapshot = TaskManagerSnapshot{};
            s_processes.clear();
            s_selectedIndex = 0;
            s_scrollOffset = 0;
            s_lastRefreshTicks = 0;
            s_lastKeyCode = 0;
            s_keyDown = false;
            s_currentTab = 0;
            s_selectedTombIndex = -1;
            s_cpuPct = 0;
            s_cpuHistoryCount = 0;
            s_cpuHistoryHead = 0;
            for (int i = 0; i < kCpuHistoryMax; ++i) {
                s_cpuHistory[i] = 0;
            }
            s_memPct = 0;
            s_diskPct = 0;
            s_diskHistoryCount = 0;
            s_diskHistoryHead = 0;
            for (int i = 0; i < kDiskHistoryMax; ++i) {
                s_diskHistory[i] = 0;
            }
            s_netPct = 0;
            s_perfCategoryIndex = 0;
            s_memoryHistoryCount = 0;
            s_memoryHistoryHead = 0;
            for (int i = 0; i < kMemoryHistoryMax; ++i) {
                s_memoryHistory[i] = 0;
            }
            s_cumulativeAllocated = 0;
            s_cumulativeFreed = 0;
            s_lastMemDetailUpdate = 0;
            s_leakExists = false;
            s_leakGrowthCounter = 0;
            s_leakHistory.clear();
            s_processCpuSamples.clear();
            
            refreshProcessList();
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window (760x520 matching Legacy)
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Task Manager|760|520";
            std::string payload = oss.str();
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
            
            // Main event loop
            bool running = true;
            while (running) {
                ipc::Message msg;
                if (ipc::Bus::pop(kGuiChanOut, msg, 50)) {  // 50ms timeout
                    MsgType msgType = (MsgType)msg.type;
                    
                    switch (msgType) {
                        case MsgType::MT_Create: {
                            // Window created - extract window ID
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    std::string idStr = payload.substr(0, sep);
                                    s_windowId = std::stoull(idStr);
                                    Logger::write(LogLevel::Info, std::string("TaskManager window created: ") + std::to_string(s_windowId));
                                    
                                    // Add buttons
                                    auto addButton = [](int id, int x, int y, int w, int h, const std::string& text) {
                                        ipc::Message msg;
                                        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
                                        std::ostringstream oss;
                                        oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
                                        std::string payload = oss.str();
                                        msg.data.assign(payload.begin(), payload.end());
                                        ipc::Bus::publish("gui.input", std::move(msg), false);
                                    };
                                    
                                    // Control buttons
                                    addButton(1, 4, 4, 80, 20, "Refresh");
                                    addButton(2, 88, 4, 100, 20, "End Process");
                                    
                                    // Tab buttons (4 tabs matching Legacy)
                                    addButton(10, 4, 28, 110, 22, "Processes");
                                    addButton(11, 118, 28, 110, 22, "Performance");
                                    addButton(12, 232, 28, 110, 22, "Tombstoned");
                                    addButton(13, 346, 28, 130, 22, "Memory Details");
                                    
                                    // Tombstoned tab action buttons
                                    addButton(20, 560, 440, 150, 24, "Restore");
                                    addButton(21, 560, 468, 150, 24, "End Tombstoned");
                                    
                                    // Draw initial content
                                    updateHeader();
                                    updateDisplay();
                                    updateStatusBar();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse window ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_Close: {
                            // Window closed
                            std::string payload(msg.data.begin(), msg.data.end());
                            if (!payload.empty()) {
                                try {
                                    uint64_t closedId = std::stoull(payload);
                                    if (closedId == s_windowId) {
                                        Logger::write(LogLevel::Info, "TaskManager closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            // Keyboard input
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    int keyCode = std::stoi(payload.substr(0, sep));
                                    std::string action = payload.substr(sep + 1);
                                    
                                    // Key debouncing
                                    if (action == "down") {
                                        if (s_keyDown && keyCode == s_lastKeyCode) {
                                            break;
                                        }
                                        s_keyDown = true;
                                        s_lastKeyCode = keyCode;
                                        handleKeyPress(keyCode);
                                    } else {
                                        s_keyDown = false;
                                        s_lastKeyCode = 0;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse key code: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_WidgetEvt: {
                            // Button click
                            std::string payload(msg.data.begin(), msg.data.end());
                            std::istringstream iss(payload);
                            std::string winIdStr, widgetIdStr, event, value;
                            std::getline(iss, winIdStr, '|');
                            std::getline(iss, widgetIdStr, '|');
                            std::getline(iss, event, '|');
                            std::getline(iss, value);
                            
                            if (!winIdStr.empty() && !widgetIdStr.empty()) {
                                try {
                                    uint64_t winId = std::stoull(winIdStr);
                                    if (winId == s_windowId && event == "click") {
                                        int widgetId = std::stoi(widgetIdStr);
                                        
                                        switch (widgetId) {
                                            case 1: // Refresh
                                                refreshProcessList();
                                                updateDisplay();
                                                updateStatusBar();
                                                break;
                                            case 2: // End Process
                                                endSelectedProcess();
                                                break;
                                            case 10: // Processes tab
                                                s_currentTab = 0;
                                                updateDisplay();
                                                break;
                                            case 11: // Performance tab
                                                s_currentTab = 1;
                                                updateDisplay();
                                                break;
                                            case 12: // Tombstoned tab
                                                s_currentTab = 2;
                                                updateDisplay();
                                                break;
                                            case 13: // Memory Details tab
                                                s_currentTab = 3;
                                                updateDisplay();
                                                break;
                                            case 20: // Restore tombstoned
                                                restoreTombstoned();
                                                break;
                                            case 21: // End tombstoned
                                                endTombstoned();
                                                break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
                
                // Auto-refresh every 2 seconds
                auto now = std::chrono::steady_clock::now();
                uint64_t nowTicks = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                
                if (s_windowId != 0 && (nowTicks - s_lastRefreshTicks >= 2000)) {
                    refreshProcessList();
                    updateDisplay();
                    updateStatusBar();
                    s_lastRefreshTicks = nowTicks;
                }
            }
            
            Logger::write(LogLevel::Info, "TaskManager stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("TaskManager EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "TaskManager UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void TaskManager::refreshProcessList() {
        s_snapshot = BuildTaskManagerSnapshot();
        recordPerformanceSnapshot(s_snapshot);
        s_processes.clear();
        s_processes.reserve(s_snapshot.processes.size());
        for (const auto& process : s_snapshot.processes) {
            ProcessInfo info;
            info.pid = process.pid;
            info.name = process.displayName;
            info.running = process.running;
            info.exitCode = process.exitCode;
            info.memoryBytes = process.memoryBytes;
            s_processes.push_back(info);
        }
        s_totalMemory = s_snapshot.memory.totalAvailable ? s_snapshot.memory.totalBytes : 0;
        s_usedMemory = s_snapshot.memory.usedBytes;
        s_peakMemory = s_snapshot.memory.peakBytes;
        s_tasksExecuted = s_snapshot.performance.schedulerTasksExecuted;
        
        // Ensure selected index is valid
        if (s_selectedIndex >= (int)s_processes.size()) {
            s_selectedIndex = (int)s_processes.size() - 1;
        }
        if (s_selectedIndex < 0) {
            s_selectedIndex = 0;
        }
        
        Logger::write(LogLevel::Info, std::string("TaskManager: Refreshed ") + std::to_string(s_processes.size()) + " processes");
    }

    void TaskManager::recordPerformanceSnapshot(const TaskManagerSnapshot& snapshot) {
        if (snapshot.performance.cpuAvailable) {
            appendHistorySample(s_cpuHistory, kCpuHistoryMax, s_cpuHistoryCount, s_cpuHistoryHead, snapshot.performance.cpuPct);
        }

        if (snapshot.memory.heapUtilPctAvailable) {
            appendHistorySample(s_memoryHistory, kMemoryHistoryMax, s_memoryHistoryCount, s_memoryHistoryHead, snapshot.memory.heapUtilPct);
        }

        if (snapshot.performance.diskAvailable && snapshot.performance.diskActivePctAvailable) {
            appendHistorySample(s_diskHistory, kDiskHistoryMax, s_diskHistoryCount, s_diskHistoryHead, snapshot.performance.diskPct);
        }
    }
    
    void TaskManager::endSelectedProcess() {
        if (s_selectedIndex < 0 || s_selectedIndex >= (int)s_processes.size()) {
            return;
        }
        
        const ProcessInfo& proc = s_processes[s_selectedIndex];
        
        // Don't allow ending process 0 (system)
        if (proc.pid == 0) {
            Logger::write(LogLevel::Warn, "TaskManager: Cannot end system process");
            return;
        }
        
        // Don't allow ending own process
        // (We can't get our own PID easily, but we can prevent ending the TaskManager window)
        
        Logger::write(LogLevel::Info, std::string("TaskManager: Ending process ") + std::to_string(proc.pid));
        
        if (ProcessTable::terminate(proc.pid)) {
            Logger::write(LogLevel::Info, std::string("TaskManager: Successfully ended process ") + std::to_string(proc.pid));
            // Refresh to show updated list
            refreshProcessList();
            updateDisplay();
            updateStatusBar();
        } else {
            Logger::write(LogLevel::Error, std::string("TaskManager: Failed to end process ") + std::to_string(proc.pid));
        }
    }
    
    void TaskManager::handleKeyPress(int keyCode) {
        // Tab key - cycle tabs
        if (keyCode == 9) {
            s_currentTab = (s_currentTab + 1) % kTabCount;
            updateDisplay();
            return;
        }
        
        // Tab-specific handling
        if (s_currentTab == 1) {
            // Performance tab: Left/Right to change category (4 categories)
            if (keyCode == 37) { // Left
                if (s_perfCategoryIndex > 0) s_perfCategoryIndex--;
                updateDisplay();
                return;
            }
            if (keyCode == 39) { // Right
                if (s_perfCategoryIndex < 3) s_perfCategoryIndex++;
                updateDisplay();
                return;
            }
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        if (s_currentTab == 2) {
            // Tombstoned tab: Up/Down to select, R=Restore, E/Del=End
            if (keyCode == 38) { // Up
                if (s_selectedTombIndex > 0) s_selectedTombIndex--;
                updateDisplay();
                return;
            }
            if (keyCode == 40) { // Down
                int count = countTombstoned();
                if (s_selectedTombIndex < count - 1) s_selectedTombIndex++;
                updateDisplay();
                return;
            }
            if (keyCode == 82 || keyCode == 114) { // R - Restore
                restoreTombstoned();
                return;
            }
            if (keyCode == 46 || keyCode == 69 || keyCode == 101) { // Del/E - End
                endTombstoned();
                return;
            }
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        if (s_currentTab == 3) {
            // Memory Details tab: F5 refreshes
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        // Processes tab (s_currentTab == 0)
        if (keyCode == 38) { // Up
            if (s_selectedIndex > 0) {
                s_selectedIndex--;
                if (s_selectedIndex < s_scrollOffset) {
                    s_scrollOffset = s_selectedIndex;
                }
                updateDisplay();
            }
        }
        else if (keyCode == 40) { // Down
            if (s_selectedIndex < (int)s_processes.size() - 1) {
                s_selectedIndex++;
                if (s_selectedIndex >= s_scrollOffset + 11) {
                    s_scrollOffset = s_selectedIndex - 10;
                }
                updateDisplay();
            }
        }
        else if (keyCode == 46 || keyCode == 69 || keyCode == 101) { // Del/E
            endSelectedProcess();
        }
        else if (keyCode == 116 || keyCode == 82 || keyCode == 114) { // F5/R
            refreshProcessList();
            updateDisplay();
            updateStatusBar();
        }
        else if (keyCode == 33) { // Page Up
            if (s_scrollOffset > 0) {
                s_scrollOffset -= 10;
                if (s_scrollOffset < 0) s_scrollOffset = 0;
                updateDisplay();
            }
        }
        else if (keyCode == 34) { // Page Down
            int maxScroll = (int)s_processes.size() - 11;
            if (maxScroll < 0) maxScroll = 0;
            s_scrollOffset += 10;
            if (s_scrollOffset > maxScroll) s_scrollOffset = maxScroll;
            updateDisplay();
        }
    }
    
    void TaskManager::updateHeader() {
        const MemorySnapshot& mem = s_snapshot.memory;
        const int x = 12;
        const int y = 58;
        const int w = 736;
        const int h = 92;

        publishRect(s_windowId, x, y, w, h, 0x1A, 0x1A, 0x1A);
        publishRect(s_windowId, x, y, w, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y + h - 1, w, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y, 1, h, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x + w - 1, y, 1, h, 0x44, 0x44, 0x44);

        publishTextAtColor(s_windowId, x + 12, y + 10, 240, 244, 248, "Task Manager");

        std::ostringstream memoryLine;
        memoryLine << "Memory: " << formatMemory(mem.usedBytes);
        if (mem.totalAvailable) {
            memoryLine << " / " << formatMemory(mem.totalBytes) << " (allocator heap total)";
        } else {
            memoryLine << " / N/A";
        }
        memoryLine << "  Peak: " << formatMemory(mem.peakBytes);
        publishTextAt(s_windowId, x + 12, y + 30, memoryLine.str());

        std::ostringstream statsLine;
        statsLine << "Processes: " << s_snapshot.performance.processCount
                  << "  Windows: " << s_snapshot.performance.windowCount
                  << "  Tasks Executed: " << s_tasksExecuted;
        publishTextAt(s_windowId, x + 12, y + 48, statsLine.str());

        std::ostringstream footerLine;
        footerLine << "CPU: ";
        footerLine << (s_snapshot.performance.cpuAvailable ? std::to_string(s_snapshot.performance.cpuPct) + "%" : std::string("N/A"));
        footerLine << "  Disk: " << diskRateSummary(s_snapshot.performance);
        footerLine << "  Network: " << networkRateSummary(s_snapshot.performance);
        publishTextAtColor(s_windowId, x + 12, y + 66, 170, 176, 186, footerLine.str());
    }
    
    void TaskManager::updateDisplay() {
        clearWindow(s_windowId);

        if (s_currentTab == 0) {
            updateHeader();
            const int tableX = 12;
            const int tableY = 160;
            const int tableW = 736;
            const int headerH = 22;
            const int rowH = 24;
            const int rowY = tableY + headerH;
            const int visibleRows = 11;
            const int rowCount = static_cast<int>(s_snapshot.processes.size());
            const int startIndex = std::max(0, std::min(s_scrollOffset, std::max(0, rowCount - visibleRows)));
            const int endIndex = std::min(rowCount, startIndex + visibleRows);

            publishRect(s_windowId, tableX, tableY, tableW, headerH, 0x24, 0x24, 0x24);
            publishRect(s_windowId, tableX, tableY, tableW, 1, 0x44, 0x44, 0x44);
            publishRect(s_windowId, tableX, tableY + headerH - 1, tableW, 1, 0x44, 0x44, 0x44);
            publishRect(s_windowId, tableX, tableY, 1, headerH, 0x44, 0x44, 0x44);
            publishRect(s_windowId, tableX + tableW - 1, tableY, 1, headerH, 0x44, 0x44, 0x44);

            publishTextAtColor(s_windowId, tableX + 12, tableY + 3, 236, 240, 248, "Name");
            publishTextAtColor(s_windowId, tableX + 330, tableY + 3, 236, 240, 248, "CPU%");
            publishTextAtColor(s_windowId, tableX + 394, tableY + 3, 236, 240, 248, "Memory");
            publishTextAtColor(s_windowId, tableX + 534, tableY + 3, 236, 240, 248, "Disk%");
            publishTextAtColor(s_windowId, tableX + 606, tableY + 3, 236, 240, 248, "Network%");

            publishRect(s_windowId, tableX, rowY - 2, tableW, visibleRows * rowH + 4, 0x18, 0x18, 0x18);

            for (int i = startIndex; i < endIndex; ++i) {
                const ProcessSnapshot& proc = s_snapshot.processes[i];
                const int y = rowY + (i - startIndex) * rowH;
                if (i == s_selectedIndex) {
                    publishRect(s_windowId, tableX + 1, y, tableW - 2, rowH - 1, 0x2A, 0x3B, 0x52);
                }

                publishTextAt(s_windowId, tableX + 8, y + 5, i == s_selectedIndex ? "> " : "  ");
                publishTextAt(s_windowId, tableX + 24, y + 5, trimDisplayName(proc.displayName, 30));
                publishTextAtColor(s_windowId, tableX + 330, y + 5, 224, 228, 238, pctOrNa(proc.cpuPctAvailable, proc.cpuPct));
                publishTextAt(s_windowId, tableX + 394, y + 5, formatMemory(proc.memoryBytes));
                publishTextAtColor(s_windowId, tableX + 534, y + 5, 224, 228, 238, proc.diskPctAvailable ? std::to_string(proc.diskPct) + "%" : "N/A");
                publishTextAtColor(s_windowId, tableX + 606, y + 5, 224, 228, 238, proc.networkPctAvailable ? std::to_string(proc.networkPct) + "%" : "N/A");
            }

            publishRect(s_windowId, tableX, 456, tableW, 1, 0x44, 0x44, 0x44);
            std::ostringstream selectedLine;
            if (s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_snapshot.processes.size())) {
                const ProcessSnapshot& proc = s_snapshot.processes[s_selectedIndex];
                selectedLine << "Selected: " << trimDisplayName(proc.displayName, 28)
                             << " | PID " << proc.pid
                             << " | Status " << (proc.running ? "Running" : "Stopped");
                if (!proc.appId.empty()) {
                    selectedLine << " | App ID " << proc.appId;
                }
            } else {
                selectedLine << "Selected: N/A";
            }
            publishTextAt(s_windowId, tableX + 12, 462, selectedLine.str());
            updateStatusBar();
        } else if (s_currentTab == 1) {
            updatePerformanceTab();
        } else if (s_currentTab == 2) {
            updateTombstonedTab();
        } else if (s_currentTab == 3) {
            updateMemoryDetailsTab();
        }
    }
    
    void TaskManager::updatePerformanceTab() {
        const PerformanceSnapshot& perf = s_snapshot.performance;
        const MemorySnapshot& mem = s_snapshot.memory;
        s_tasksExecuted = perf.schedulerTasksExecuted;
        s_cpuPct = perf.cpuAvailable ? perf.cpuPct : 0;
        s_memPct = perf.memoryAvailable ? perf.memoryPct : 0;
        s_diskPct = perf.diskActivePctAvailable ? perf.diskPct : 0;
        s_netPct = perf.networkUtilizationAvailable ? perf.networkPct : 0;

        const int x = 12;
        const int y = 58;
        const int w = 736;
        const int h = 452;
        const int navW = 210;
        const int gap = 12;
        const int detailX = x + navW + gap;
        const int detailW = w - navW - gap;
        const int navItemH = 88;
        const int navStartY = y + 34;

        publishRect(s_windowId, x, y, navW, h, 0x1A, 0x1A, 0x1A);
        publishRect(s_windowId, detailX, y, detailW, h, 0x1A, 0x1A, 0x1A);
        publishRect(s_windowId, x, y, navW, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, detailX, y, detailW, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y + h - 1, navW, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, detailX, y + h - 1, detailW, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y, 1, h, 0x44, 0x44, 0x44);
        publishRect(s_windowId, detailX, y, 1, h, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x + navW - 1, y, 1, h, 0x44, 0x44, 0x44);
        publishRect(s_windowId, detailX + detailW - 1, y, 1, h, 0x44, 0x44, 0x44);

        publishTextAtColor(s_windowId, x + 12, y + 10, 240, 244, 248, "Performance");
        publishTextAtColor(s_windowId, detailX + 12, y + 10, 240, 244, 248, s_perfCategoryIndex == 0 ? "CPU" : s_perfCategoryIndex == 1 ? "Memory" : s_perfCategoryIndex == 2 ? "Disk" : "Network");

        const char* navLabels[] = { "CPU", "Memory", "Disk", "Network" };
        const bool navAvailable[] = { perf.cpuAvailable, perf.memoryAvailable, perf.diskAvailable, perf.networkAvailable };
        const std::string navValueTexts[] = {
            pctOrNa(perf.cpuAvailable, perf.cpuPct),
            pctOrNa(perf.memoryAvailable, perf.memoryPct),
            diskRateSummary(perf),
            networkRateSummary(perf)
        };
        const uint64_t* navHistories[] = { s_cpuHistory, s_memoryHistory, s_diskHistory, nullptr };
        const int navHistoryCounts[] = { s_cpuHistoryCount, s_memoryHistoryCount, s_diskHistoryCount, 0 };
        const int navHistoryHeads[] = { s_cpuHistoryHead, s_memoryHistoryHead, s_diskHistoryHead, 0 };
        const uint8_t navColors[4][3] = {
            { 93, 173, 226 },
            { 88, 214, 141 },
            { 230, 126, 34 },
            { 155, 89, 182 }
        };

        for (int i = 0; i < 4; ++i) {
            const int itemY = navStartY + i * 96;
            if (i == s_perfCategoryIndex) {
                publishRect(s_windowId, x + 8, itemY - 2, navW - 16, navItemH + 2, 0x25, 0x32, 0x44);
            }
            const bool showHistory = navAvailable[i] && navHistoryCounts[i] > 0;
            drawGraphBox(
                s_windowId,
                x + 10,
                itemY,
                navW - 20,
                navItemH,
                navLabels[i],
                navValueTexts[i],
                showHistory,
                navHistories[i],
                navHistoryCounts[i],
                navHistoryHeads[i],
                navColors[i][0],
                navColors[i][1],
                navColors[i][2]
            );
        }

        const bool detailHasHistory = (s_perfCategoryIndex == 0 && perf.cpuAvailable && s_cpuHistoryCount > 0) ||
                                      (s_perfCategoryIndex == 1 && mem.heapUtilPctAvailable && s_memoryHistoryCount > 0) ||
                                      (s_perfCategoryIndex == 2 && perf.diskAvailable && perf.diskActivePctAvailable && s_diskHistoryCount > 0);
        const char* detailLabels[] = { "CPU", "Memory", "Disk", "Network" };
        const uint8_t detailColors[4][3] = {
            { 93, 173, 226 },
            { 88, 214, 141 },
            { 230, 126, 34 },
            { 155, 89, 182 }
        };
        const uint64_t* detailHistory = s_perfCategoryIndex == 0 ? s_cpuHistory
                                      : s_perfCategoryIndex == 1 ? s_memoryHistory
                                      : s_perfCategoryIndex == 2 ? s_diskHistory
                                      : nullptr;
        const int detailHistoryCount = s_perfCategoryIndex == 0 ? s_cpuHistoryCount
                                     : s_perfCategoryIndex == 1 ? s_memoryHistoryCount
                                     : s_perfCategoryIndex == 2 ? s_diskHistoryCount
                                     : 0;
        const int detailHistoryHead = s_perfCategoryIndex == 0 ? s_cpuHistoryHead
                                    : s_perfCategoryIndex == 1 ? s_memoryHistoryHead
                                    : s_perfCategoryIndex == 2 ? s_diskHistoryHead
                                    : 0;
        const std::string detailValue =
            s_perfCategoryIndex == 0 ? pctOrNa(perf.cpuAvailable, perf.cpuPct)
            : s_perfCategoryIndex == 1 ? pctOrNa(mem.heapUtilPctAvailable, mem.heapUtilPct)
            : s_perfCategoryIndex == 2 ? diskRateSummary(perf)
            : networkRateSummary(perf);
        drawGraphBox(
            s_windowId,
            detailX + 10,
            y + 34,
            detailW - 20,
            210,
            detailLabels[s_perfCategoryIndex],
            detailValue,
            detailHasHistory,
            detailHistory,
            detailHistoryCount,
            detailHistoryHead,
            detailColors[s_perfCategoryIndex][0],
            detailColors[s_perfCategoryIndex][1],
            detailColors[s_perfCategoryIndex][2]
        );

        const int detailTop = y + 256;
        const int labelX = detailX + 14;
        const int valueX = detailX + detailW / 2;
        const int lineH = 18;
        auto detailRow = [&](int row, const std::string& label, const std::string& value) {
            const int rowY = detailTop + row * lineH;
            publishTextAt(s_windowId, labelX, rowY, label);
            publishTextAt(s_windowId, valueX, rowY, value);
        };

        if (s_perfCategoryIndex == 0) {
            detailRow(0, "Utilization:", pctOrNa(perf.cpuAvailable, perf.cpuPct));
            detailRow(1, "Sample window:", perf.cpuAvailable ? std::to_string(perf.cpuSampleWindowMs) + " ms" : std::string("N/A"));
            detailRow(2, "Source:", perf.cpuAvailable ? perf.cpuSource : std::string("N/A"));
            detailRow(3, "Busy time:", perf.cpuAvailable ? std::to_string(perf.cpuBusyTimeMs) + " ms" : std::string("N/A"));
            detailRow(4, "Idle time:", perf.cpuAvailable ? std::to_string(perf.cpuIdleTimeMs) + " ms" : std::string("N/A"));
        } else if (s_perfCategoryIndex == 1) {
            detailRow(0, "In Use:", formatMemory(mem.usedBytes) + " (" + std::to_string(mem.heapUtilPctAvailable ? mem.heapUtilPct : 0) + "%)");
            detailRow(1, "Available:", mem.totalAvailable ? formatMemory(mem.freeBytes) : std::string("N/A"));
            detailRow(2, "Total:", mem.totalAvailable ? formatMemory(mem.totalBytes) + " (allocator heap)" : std::string("N/A"));
            detailRow(3, "Peak:", formatMemory(mem.peakBytes));
            detailRow(4, "Free/Alloc Ratio:", mem.freeAllocRatioAvailable ? std::to_string(mem.freeAllocRatioPct) + "%" : std::string("N/A"));
            detailRow(5, "Top Tag:", mem.topTagAvailable ? mem.topTagName + " (" + formatMemory(mem.topTagBytes) + ")" : std::string("N/A"));
            detailRow(6, "Top Owner:", mem.topOwnerAvailable ? std::string("PID ") + std::to_string(mem.topOwnerPid) + " (" + formatMemory(mem.topOwnerBytes) + ")" : std::string("N/A"));
        } else if (s_perfCategoryIndex == 2) {
            detailRow(0, "Utilization:", perf.diskActivePctAvailable ? std::to_string(perf.diskPct) + "%" : std::string("N/A"));
            detailRow(1, "Read speed:", diskRateDetail(perf, true));
            detailRow(2, "Write speed:", diskRateDetail(perf, false));
            detailRow(3, "Sample window:", perf.diskAvailable ? std::to_string(perf.diskSampleWindowMs) + " ms" : std::string("N/A"));
            detailRow(4, "Source:", perf.diskAvailable ? perf.diskSource : std::string("N/A"));
            detailRow(5, "Read bytes:", perf.diskAvailable ? std::to_string(perf.diskReadBytesTotal) : std::string("N/A"));
            detailRow(6, "Write bytes:", perf.diskAvailable ? std::to_string(perf.diskWriteBytesTotal) : std::string("N/A"));
        } else if (s_perfCategoryIndex == 3) {
            detailRow(0, "Utilization:", perf.networkUtilizationAvailable ? std::to_string(perf.networkPct) + "%" : std::string("N/A"));
            detailRow(1, "Send speed:", networkRateDetail(perf, true));
            detailRow(2, "Receive speed:", networkRateDetail(perf, false));
            detailRow(3, "Sample window:", perf.networkAvailable ? std::to_string(perf.networkSampleWindowMs) + " ms" : std::string("N/A"));
            detailRow(4, "Source:", perf.networkAvailable ? perf.networkSource : std::string("N/A"));
            detailRow(5, "Sent bytes:", perf.networkAvailable ? std::to_string(perf.networkBytesSentTotal) : std::string("N/A"));
            detailRow(6, "Received bytes:", perf.networkAvailable ? std::to_string(perf.networkBytesReceivedTotal) : std::string("N/A"));
        }

        publishTextAtColor(s_windowId, detailX + 12, y + h - 22, 160, 166, 176, "Left/Right switches categories");
    }
    
    void TaskManager::updateTombstonedTab() {
        const int x = 12;
        const int y = 58;
        const int w = 736;
        const int h = 452;
        const int headerY = y + 34;
        const int rowY = headerY + 24;
        const int rowH = 22;
        const int visibleRows = 9;
        const int rowCount = static_cast<int>(s_snapshot.tombstoned.size());

        if (rowCount > 0 && s_selectedTombIndex < 0) {
            s_selectedTombIndex = 0;
        }
        if (s_selectedTombIndex >= rowCount) {
            s_selectedTombIndex = rowCount - 1;
        }
        if (s_selectedTombIndex < -1) {
            s_selectedTombIndex = -1;
        }

        publishRect(s_windowId, x, y, w, h, 0x1A, 0x1A, 0x1A);
        publishRect(s_windowId, x, y, w, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y + h - 1, w, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y, 1, h, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x + w - 1, y, 1, h, 0x44, 0x44, 0x44);
        publishTextAtColor(s_windowId, x + 12, y + 10, 240, 244, 248, "Tombstoned");

        publishRect(s_windowId, x + 10, headerY, w - 20, 22, 0x24, 0x24, 0x24);
        publishTextAtColor(s_windowId, x + 22, headerY + 3, 236, 240, 248, "Name");
        publishTextAtColor(s_windowId, x + 168, headerY + 3, 236, 240, 248, "PID");
        publishTextAtColor(s_windowId, x + 238, headerY + 3, 236, 240, 248, "App ID");
        publishTextAtColor(s_windowId, x + 352, headerY + 3, 236, 240, 248, "Reason");
        publishTextAtColor(s_windowId, x + 488, headerY + 3, 236, 240, 248, "Exit");
        publishTextAtColor(s_windowId, x + 552, headerY + 3, 236, 240, 248, "Runtime");
        publishTextAtColor(s_windowId, x + 624, headerY + 3, 236, 240, 248, "Restore");
        publishTextAtColor(s_windowId, x + 688, headerY + 3, 236, 240, 248, "End");

        publishRect(s_windowId, x + 10, rowY - 2, w - 20, visibleRows * rowH + 4, 0x18, 0x18, 0x18);

        for (int i = 0; i < rowCount && i < visibleRows; ++i) {
            const TombstoneSnapshot& tomb = s_snapshot.tombstoned[i];
            const int row = rowY + i * rowH;
            if (i == s_selectedTombIndex) {
                publishRect(s_windowId, x + 11, row, w - 22, rowH - 1, 0x30, 0x30, 0x46);
            }
            publishTextAt(s_windowId, x + 20, row + 5, i == s_selectedTombIndex ? "> " : "  ");
            publishTextAt(s_windowId, x + 36, row + 5, trimDisplayName(tomb.displayName, 20));
            publishTextAt(s_windowId, x + 168, row + 5, std::to_string(tomb.pid));
            publishTextAt(s_windowId, x + 238, row + 5, trimDisplayName(tomb.appId.empty() ? std::string("N/A") : tomb.appId, 17));
            publishTextAt(s_windowId, x + 352, row + 5, trimDisplayName(tomb.reason.empty() ? std::string("Unknown") : tomb.reason, 18));
            publishTextAt(s_windowId, x + 488, row + 5, tomb.exitCodeAvailable ? std::to_string(tomb.exitCode) : std::string("N/A"));
            publishTextAt(s_windowId, x + 552, row + 5, tomb.runtimeMsAvailable ? formatUptime(tomb.runtimeMs) : std::string("N/A"));
            publishTextAtColor(s_windowId, x + 624, row + 5, 224, 228, 238, tomb.restoreSupported ? "Yes" : "No");
            publishTextAtColor(s_windowId, x + 688, row + 5, 224, 228, 238, tomb.endSupported ? "Yes" : "No");
        }

        const int detailY = rowY + visibleRows * rowH + 10;
        publishRect(s_windowId, x + 10, detailY - 6, w - 20, h - (detailY - 6) - 28, 0x16, 0x16, 0x16);
        publishRect(s_windowId, x + 10, detailY - 6, w - 20, 1, 0x44, 0x44, 0x44);
        publishTextAtColor(s_windowId, x + 22, detailY + 2, 236, 240, 248, "Selected Tombstone Details");

        const TombstoneSnapshot* selected = (rowCount > 0 && s_selectedTombIndex >= 0 && s_selectedTombIndex < rowCount)
            ? &s_snapshot.tombstoned[s_selectedTombIndex]
            : nullptr;
        const int detailLeftLabelX = x + 22;
        const int detailLeftValueX = x + 136;
        const int detailRightLabelX = x + 364;
        const int detailRightValueX = x + 470;
        const int detailRowH = 14;
        int detailRowY = detailY + 18;
        auto detailPairRow = [&](const std::string& leftLabel,
                                 const std::string& leftValue,
                                 const std::string& rightLabel,
                                 const std::string& rightValue) {
            publishTextAt(s_windowId, detailLeftLabelX, detailRowY, leftLabel);
            publishTextAt(s_windowId, detailLeftValueX, detailRowY, leftValue);
            publishTextAt(s_windowId, detailRightLabelX, detailRowY, rightLabel);
            publishTextAt(s_windowId, detailRightValueX, detailRowY, rightValue);
            detailRowY += detailRowH;
        };
        if (selected) {
            detailPairRow(
                "Name:",
                trimDisplayName(selected->displayName, 28) + " (PID " + std::to_string(selected->pid) + ")",
                "App ID:",
                trimDisplayName(selected->appId.empty() ? std::string("N/A") : selected->appId, 28)
            );
            detailPairRow(
                "Window Title:",
                trimDisplayName(selected->windowTitle.empty() ? std::string("N/A") : selected->windowTitle, 28),
                "Reason:",
                trimDisplayName(selected->reason.empty() ? std::string("Unknown") : selected->reason, 20)
            );
            detailPairRow(
                "Exit Code:",
                selected->exitCodeAvailable ? std::to_string(selected->exitCode) : std::string("N/A"),
                "Runtime:",
                selected->runtimeMsAvailable ? formatUptime(selected->runtimeMs) : std::string("N/A")
            );
            detailPairRow(
                "Final Memory:",
                selected->finalMemoryBytesAvailable ? formatMemory(selected->finalMemoryBytes) : std::string("N/A"),
                "Final CPU:",
                selected->finalCpuPctAvailable ? std::to_string(selected->finalCpuPct) + "%" : std::string("N/A")
            );
            detailPairRow(
                "App Tombstone:",
                formatTombstoneCapabilityText(*selected),
                "Policy Source:",
                selected->appTombstoneCapabilityKnown ? trimDisplayName(selected->appTombstoneCapabilitySource, 24) : std::string("N/A")
            );
            detailPairRow(
                "Restore:",
                selected->restoreSupported ? "Supported" : "Unsupported",
                "Last Message:",
                selected->lastMessage.empty() ? std::string("N/A") : trimDisplayName(selected->lastMessage, 30)
            );
        } else {
            detailPairRow("Name:", "N/A", "App ID:", "N/A");
            detailPairRow("Window Title:", "N/A", "Reason:", "N/A");
            detailPairRow("Exit Code:", "N/A", "Runtime:", "N/A");
            detailPairRow("Final Memory:", "N/A", "Final CPU:", "N/A");
            detailPairRow("App Tombstone:", "N/A", "Policy Source:", "N/A");
            detailPairRow("Restore:", "N/A", "Last Message:", "N/A");
        }

        const int footerY = y + h - 20;
        std::ostringstream footer;
        footer << rowCount << " tombstoned | Up/Down=Select | Restore=No | Del/E=End";
        publishTextAtColor(s_windowId, x + 12, footerY, 160, 166, 176, footer.str());
    }
    
    void TaskManager::updateStatusBar() {
        std::ostringstream oss;
        oss << s_snapshot.performance.processCount << " processes";
        if (s_selectedIndex >= 0 && s_selectedIndex < (int)s_snapshot.processes.size()) {
            const ProcessSnapshot& proc = s_snapshot.processes[s_selectedIndex];
            oss << " | Selected: " << trimDisplayName(proc.displayName, 24) << " (PID " << proc.pid << ")";
        }
        oss << " | F5=Refresh | Del/E=End";
        std::string payload = oss.str();
        publishTextAtColor(s_windowId, 12, 496, 160, 166, 176, payload);
    }
    
    // --- Tombstoned management (matching Legacy TaskManager.cs) ---
    
    int TaskManager::countTombstoned() {
        return (int)s_snapshot.tombstoned.size();
    }
    
    void TaskManager::restoreTombstoned() {
        if (s_selectedTombIndex < 0) return;
        if (s_selectedTombIndex >= (int)s_snapshot.tombstoned.size()) return;
        const TombstoneSnapshot& tomb = s_snapshot.tombstoned[s_selectedTombIndex];
        if (!tomb.restoreSupported) {
            Logger::write(LogLevel::Info, std::string("TaskManager: Restore unsupported for tombstoned PID ") + std::to_string(tomb.pid));
            updateDisplay();
            updateStatusBar();
            return;
        }
        Logger::write(LogLevel::Info, std::string("TaskManager: Restoring tombstoned PID ") + std::to_string(tomb.pid));
        Logger::write(LogLevel::Info, "TaskManager: Process restore requested (server-side restart)");
        refreshProcessList();
        updateDisplay();
        updateStatusBar();
        return;
    }
    
    void TaskManager::endTombstoned() {
        if (s_selectedTombIndex < 0) return;
        if (s_selectedTombIndex >= (int)s_snapshot.tombstoned.size()) return;
        const TombstoneSnapshot& tomb = s_snapshot.tombstoned[s_selectedTombIndex];
        if (!tomb.endSupported) {
            Logger::write(LogLevel::Info, std::string("TaskManager: End unsupported for tombstoned PID ") + std::to_string(tomb.pid));
            updateDisplay();
            updateStatusBar();
            return;
        }
        Logger::write(LogLevel::Info, std::string("TaskManager: Ending tombstoned PID ") + std::to_string(tomb.pid));
        ProcessTable::terminate(tomb.pid);
        s_selectedTombIndex = -1;
        refreshProcessList();
        updateDisplay();
        updateStatusBar();
        return;
    }
    
    // --- Memory Details tab (matching Legacy TaskManager.cs DrawMemoryDetails) ---
    
    void TaskManager::updateMemoryDetailsTab() {
        auto now = std::chrono::steady_clock::now();
        uint64_t nowTicks = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        if (nowTicks - s_lastMemDetailUpdate >= 1000) {
            s_lastMemDetailUpdate = nowTicks;
            refreshProcessList();
        }

        const MemorySnapshot& mem = s_snapshot.memory;
        const uint64_t usedBytes = mem.usedBytes;
        const uint64_t freedBytes = mem.totalFreedBytes;
        const uint64_t allocatedBytes = usedBytes + freedBytes;
        const uint64_t currentPages = usedBytes / gxos::PageSize;
        const uint64_t freedPages = freedBytes / gxos::PageSize;
        const uint64_t allocatedPages = currentPages + freedPages;
        const uint64_t heapTotal = mem.totalAvailable ? mem.totalBytes : 0;
        const uint64_t heapFree = heapTotal > usedBytes ? heapTotal - usedBytes : 0;
        const int heapUtilPct = mem.heapUtilPctAvailable ? mem.heapUtilPct : 0;

        s_usedMemory = usedBytes;
        s_peakMemory = mem.peakBytes;
        s_cumulativeFreed = freedBytes;
        s_cumulativeAllocated = allocatedBytes;

        s_leakHistory.push_back(usedBytes);
        if ((int)s_leakHistory.size() > kLeakHistoryMax) {
            s_leakHistory.erase(s_leakHistory.begin());
        }
        if (s_leakHistory.size() >= 2) {
            const uint64_t prev = s_leakHistory[s_leakHistory.size() - 2];
            const uint64_t curr = s_leakHistory[s_leakHistory.size() - 1];
            if (curr > prev) {
                ++s_leakGrowthCounter;
                if (s_leakGrowthCounter >= kLeakThreshold) {
                    s_leakExists = true;
                }
            } else {
                s_leakGrowthCounter = 0;
                if ((int)s_leakHistory.size() >= kLeakHistoryMax) {
                    const uint64_t first = s_leakHistory.front();
                    const uint64_t last = s_leakHistory.back();
                    if (last <= first + (100 * 1024ULL)) {
                        s_leakExists = false;
                    }
                }
            }
        }

        const int x = 12;
        const int y = 58;
        const int w = 736;
        const int h = 452;

        publishRect(s_windowId, x, y, w, h, 0x1A, 0x1A, 0x1A);
        publishRect(s_windowId, x, y, w, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y + h - 1, w, 1, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x, y, 1, h, 0x44, 0x44, 0x44);
        publishRect(s_windowId, x + w - 1, y, 1, h, 0x44, 0x44, 0x44);
        publishTextAtColor(s_windowId, x + 12, y + 10, 240, 244, 248, "Memory Details");

        publishTextAtColor(s_windowId, x + 12, y + 34, 236, 240, 248, "=== Memory Allocator Details ===");
        publishTextAtColor(s_windowId, x + 12, y + 52, 236, 240, 248, "=== Free() Call Statistics ===");

        const int labelX = x + 12;
        const int valueX = x + 300;
        const int rowH = 18;
        int rowY = y + 70;
        auto row = [&](const std::string& label, const std::string& value, bool highlight = false) {
            publishTextAt(s_windowId, labelX, rowY, label);
            if (highlight) {
                publishTextAtColor(s_windowId, valueX, rowY, 208, 92, 92, value);
            } else {
                publishTextAt(s_windowId, valueX, rowY, value);
            }
            rowY += rowH;
        };

        row("Total Free() Calls:", "N/A");
        row("Successful Frees:", "N/A");
        row("Failed Invalid Ptr:", "N/A");
        row("Failed No Pages:", "N/A");

        publishRect(s_windowId, x + 12, rowY + 2, w - 24, 1, 0x44, 0x44, 0x44);
        rowY += 12;

        row("Allocated Pages:", std::to_string(allocatedPages));
        row("Freed Pages:", std::to_string(freedPages));
        row("Current Pages in Use:", std::to_string(currentPages));
        row("Net Growth:", formatMemory(usedBytes) + " (" + std::to_string(currentPages) + " pages)");
        row("Leak Exists:", mem.leakStateAvailable ? (mem.leakState ? std::string("Yes") : std::string("No")) : std::string("N/A"), mem.leakStateAvailable && mem.leakState);
        row("Free/Alloc Ratio:", allocatedPages > 0 ? std::to_string((freedPages * 100ULL) / allocatedPages) + "%" : std::string("N/A"));

        publishRect(s_windowId, x + 12, rowY + 2, w - 24, 1, 0x44, 0x44, 0x44);
        rowY += 12;

        publishTextAtColor(s_windowId, x + 12, rowY, 236, 240, 248, "=== Heap Allocator ===");
        rowY += 18;
        row("Total Heap Size:", heapTotal > 0 ? formatMemory(heapTotal) + " (allocator heap)" : std::string("N/A"));
        row("Heap In Use:", formatMemory(usedBytes));
        row("Heap Free:", heapTotal > 0 ? formatMemory(heapFree) : std::string("N/A"));
        row("Heap Utilization:", heapTotal > 0 ? std::to_string(heapUtilPct) + "%" : std::string("N/A"));
        row("Peak Memory:", formatMemory(mem.peakBytes));
        row("Top Tag:", mem.topTagAvailable ? mem.topTagName + " (" + formatMemory(mem.topTagBytes) + ")" : std::string("N/A"));
        row("Top Owner:", mem.topOwnerAvailable ? std::string("PID ") + std::to_string(mem.topOwnerPid) + " (" + formatMemory(mem.topOwnerBytes) + ")" : std::string("N/A"));

        publishTextAtColor(s_windowId, x + 12, y + h - 20, 160, 166, 176, "F5=Refresh | Tab=Switch Tab");
    }
    
    // --- Helper functions ---

    std::string TaskManager::formatMemory(uint64_t bytes) {
        if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
            double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << gb << " GB";
            return oss.str();
        } else if (bytes >= 1024ULL * 1024ULL) {
            double mb = (double)bytes / (1024.0 * 1024.0);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << mb << " MB";
            return oss.str();
        } else if (bytes >= 1024ULL) {
            return std::to_string(bytes / 1024) + " KB";
        } else {
            return std::to_string(bytes) + " B";
        }
    }
    
    std::string TaskManager::formatUptime(uint64_t ticks) {
        uint64_t sec = ticks / 1000;
        uint64_t min = sec / 60;
        uint64_t hrs = min / 60;
        uint64_t days = hrs / 24;
        sec %= 60;
        min %= 60;
        hrs %= 24;
        
        std::ostringstream oss;
        if (days > 0) oss << days << "d ";
        oss << std::setfill('0') << std::setw(2) << hrs << ":"
            << std::setw(2) << min << ":"
            << std::setw(2) << sec;
        return oss.str();
    }
    
}} // namespace gxos::apps
