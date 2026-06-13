#pragma once
#include "app_launch_target.h"
#include "app_manifest.h"

#include <string>
#include <vector>
#include <cstdint>

namespace gxos { namespace gui {
    // Pinned item types matching C# implementation
    enum class PinnedKind : uint8_t {
        App = 0,        // Application name (e.g., "Calculator")
        File = 1,       // File path (e.g., "data/test.gxm")
        Special = 2     // Special launcher (e.g., "Computer Files")
    };

    struct PinnedItem {
        std::string name;           // Display name
        std::string path;           // Full path (for File kind) or empty
        PinnedKind kind;
        std::string iconName;       // Icon identifier (optional)
    };

    struct RecentProgramEntry {
        std::string name;
        uint64_t lastUsedTicks;
        std::string iconName;
    };

    struct RecentDocumentEntry {
        std::string path;
        uint64_t lastUsedTicks;
        std::string iconName;
    };

    struct RegisteredDesktopApp {
        std::string id;
        std::string displayName;
        std::string icon;
        apps::AppKind kind = apps::AppKind::Unknown;
        std::string launchName;
        std::string source;
    };

    struct LaunchTargetShadowCounters {
        uint64_t totalObservations = 0;
        uint64_t unresolvedObservations = 0;
        uint64_t aliasFallbackObservations = 0;
        uint64_t adapterMatches = 0;
        uint64_t adapterAcceptedMismatches = 0;
        uint64_t adapterUnexpectedMismatches = 0;
        uint64_t typedDispatchCandidateMatches = 0;
        uint64_t typedDispatchCandidateAcceptedMismatches = 0;
        uint64_t typedDispatchCandidateUnexpectedMismatches = 0;
        uint64_t startMenuObservations = 0;
        uint64_t startMenuUnresolved = 0;
        uint64_t startMenuAliasFallback = 0;
        uint64_t startMenuAdapterMatches = 0;
        uint64_t startMenuAdapterAcceptedMismatches = 0;
        uint64_t startMenuAdapterUnexpectedMismatches = 0;
        uint64_t startMenuTypedDispatchCandidateMatches = 0;
        uint64_t startMenuTypedDispatchCandidateAcceptedMismatches = 0;
        uint64_t startMenuTypedDispatchCandidateUnexpectedMismatches = 0;
        uint64_t desktopShortcutObservations = 0;
        uint64_t desktopShortcutUnresolved = 0;
        uint64_t desktopShortcutAliasFallback = 0;
        uint64_t desktopShortcutAdapterMatches = 0;
        uint64_t desktopShortcutAdapterAcceptedMismatches = 0;
        uint64_t desktopShortcutAdapterUnexpectedMismatches = 0;
        uint64_t desktopShortcutTypedDispatchCandidateMatches = 0;
        uint64_t desktopShortcutTypedDispatchCandidateAcceptedMismatches = 0;
        uint64_t desktopShortcutTypedDispatchCandidateUnexpectedMismatches = 0;
        uint64_t otherObservations = 0;
        uint64_t otherUnresolved = 0;
        uint64_t otherAliasFallback = 0;
        uint64_t otherAdapterMatches = 0;
        uint64_t otherAdapterAcceptedMismatches = 0;
        uint64_t otherAdapterUnexpectedMismatches = 0;
        uint64_t otherTypedDispatchCandidateMatches = 0;
        uint64_t otherTypedDispatchCandidateAcceptedMismatches = 0;
        uint64_t otherTypedDispatchCandidateUnexpectedMismatches = 0;
    };

    struct TypedDispatchCandidateResult {
        apps::LaunchTarget target;
        std::string resolutionInput;
        std::string typedDispatchCandidate;
        std::string typedDispatchCandidateStatus;
        std::string typedDispatchCandidateReason;
        std::string typedDispatchCandidateComparison;
        bool typedDispatchCandidateMatchesActual = false;
    };

    struct LaunchDispatchDecision {
        apps::LaunchTarget target;
        apps::LaunchDispatchUsage usage = apps::LaunchDispatchUsage::LegacyFallback;
        std::string originalDispatch;
        std::string selectedDispatch;
        std::string reason;
    };

    struct LaunchDispatchUsageCounters {
        uint64_t total = 0;
        uint64_t typedDispatch = 0;
        uint64_t legacyFallback = 0;
        uint64_t blockedUnknownFallback = 0;
        uint64_t specialCaseFallback = 0;
    };

    class DesktopService {
    public:
        // Pinned management
        static void PinApp(const std::string& name);
        static void PinFile(const std::string& displayName, const std::string& absolutePath);
        static void PinSpecial(const std::string& name); // Computer Files, etc.
        static void Unpin(const std::string& name);
        static bool IsPinned(const std::string& name);
        static const std::vector<PinnedItem>& GetPinned() { return s_pinned; }

        // Recent tracking
        static void AddRecentProgram(const std::string& name);
        static void AddRecentDocument(const std::string& path);
        static const std::vector<RecentProgramEntry>& GetRecentPrograms() { return s_recentPrograms; }
        static const std::vector<RecentDocumentEntry>& GetRecentDocuments() { return s_recentDocuments; }

        // App registry
        static void RegisterApp(const std::string& name, const std::string& iconName = "");
        static void RegisterApp(const std::string& id, const std::string& displayName, const std::string& icon, apps::AppKind kind, const std::string& launchName);
        static void RegisterApp(const std::string& id, const std::string& displayName, const std::string& icon, apps::AppKind kind, const std::string& launchName, const std::string& source);
        static bool LaunchApp(const std::string& name, std::string& error);
        static bool OpenFilesystemEntry(const std::string& path, bool isDirectory, std::string& error);
        static const std::vector<RegisteredDesktopApp>& GetRegisteredApps() { return s_apps; }
        static std::string GetRegisteredAppsVerboseDiagnostic();
        static std::string GetRegisteredAppsDiagnostic();
        static std::string AppModelSummaryDiagnostic();
        static std::string BuiltInAppMetadataCoverageDiagnostic();
        static apps::LaunchTarget ResolveLaunchTarget(const std::string& label);
        static std::string ResolveLaunchTargetDiagnostic(const std::string& label);
        static std::string LegacyDispatchStringForLaunchTarget(const apps::LaunchTarget& target, std::string& status, std::string& reason);
        static TypedDispatchCandidateResult ComputeTypedDispatchCandidateForUiLaunch(const std::string& source, const std::string& uiLabel, const std::string& shortcutTarget, const std::string& actualDispatch);
        static LaunchDispatchDecision SelectLaunchDispatch(const std::string& originalDispatch);
        static void RecordLaunchDispatchDecision(const std::string& source, const LaunchDispatchDecision& decision);
        static LaunchDispatchUsageCounters GetLaunchDispatchUsageCounters();
        static std::string LaunchDispatchUsageDiagnostic();
        static std::string LaunchTargetAdapterDiagnostic(const std::string& label);
        static std::string LaunchTargetComparisonDiagnostic();
        static std::string RecordLaunchTargetShadowObservation(const std::string& source, const apps::LaunchTarget& target, const std::string& actualDispatch, const std::string& adapterLegacyDispatch);
        static LaunchTargetShadowCounters GetLaunchTargetShadowCounters();
        static std::string LaunchTargetShadowDiagnostic();
        static bool WriteTypedDispatchHostedSmokeEvidence(std::string& error);
        static std::string LaunchStorageDiagnostic();
        static std::string LaunchStoragePreviewDiagnostic();
        static std::string LaunchStoragePreviewComparisonDiagnostic();
        static std::string LaunchTargetTypeCoverageDiagnostic();
        static std::string TypedDispatchGateDiagnostic();
        static std::string NativeAppCapabilitiesDiagnostic();
        static std::string InspectNativeAppPipeline(const std::string& appIdOrDisplayName);
        static std::string NativeAppPipelineSmokeTest(const std::string& appIdOrDisplayName);
        static std::vector<RegisteredDesktopApp> GetAppModelDemoApps();

        // Persistence
        static void LoadState();
        static void SaveState();

    private:
        static const int kMaxRecentPrograms = 32;
        static const int kMaxRecentDocuments = 64;

        static std::vector<PinnedItem> s_pinned;
        static std::vector<RecentProgramEntry> s_recentPrograms;
        static std::vector<RecentDocumentEntry> s_recentDocuments;
        static std::vector<RegisteredDesktopApp> s_apps;
    };
} }
