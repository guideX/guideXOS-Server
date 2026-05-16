#pragma once
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
        static const std::vector<RegisteredDesktopApp>& GetRegisteredApps() { return s_apps; }
        static std::string GetRegisteredAppsVerboseDiagnostic();
        static std::string NativeAppCapabilitiesDiagnostic();
        static std::string InspectNativeAppPipeline(const std::string& appIdOrDisplayName);
        static std::string NativeAppPipelineSmokeTest(const std::string& appIdOrDisplayName);

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
