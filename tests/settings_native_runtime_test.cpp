#include "kernel/app_launch_target_resolver.h"
#include "kernel/kernel_app.h"
#include "kernel/kernel_compositor.h"
#include "kernel/native_elf_baremetal.h"
#include "kernel/settings_native_provider.h"
#include "kernel/vfs.h"
#include "kernel/version.h"
#include "kernel/arch.h"
#include "app_launch_target.h"
#include "built_in_app_metadata.h"

#include <iostream>
#include <string>

namespace settings = gxos::apps::settings;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_activeWindows = 0;
int g_registeredWindows = 0;
int g_unregisteredWindows = 0;
uint32_t g_nextWindowId = 1;
uint32_t g_focusedWindowId = 0;
kernel::app::KernelWindow* g_focusedWindow = nullptr;
int g_appCreated = 0;
int g_appDestroyed = 0;
int g_appInitialized = 0;
int g_appShutdown = 0;
int g_runtimeFocusAcquired = 0;
int g_runtimeFocusLost = 0;
char g_lastParameter[64] = {};

void expect(bool condition, const char* name)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void copyText(char* destination, const char* source, size_t capacity)
{
    if (!destination || capacity == 0) return;
    size_t i = 0;
    if (source) {
        while (source[i] && i + 1 < capacity) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

class ProbeApp : public kernel::app::KernelApp {
public:
    explicit ProbeApp(const char* name)
    {
        copyText(m_name, name, sizeof(m_name));
        ++g_appCreated;
    }

    ~ProbeApp() override { ++g_appDestroyed; }

    bool init() override
    {
        ++g_appInitialized;
        m_window = new kernel::app::KernelWindow();
        if (!m_window) return false;
        m_window->owner = this;
        copyText(m_window->title, getName(), sizeof(m_window->title));
        if (!kernel::compositor::KernelCompositor::registerWindow(m_window)) {
            delete m_window;
            m_window = nullptr;
            return false;
        }
        m_state = kernel::app::AppState::Running;
        return true;
    }

    bool initWithParam(const char* parameter) override
    {
        copyText(g_lastParameter, parameter, sizeof(g_lastParameter));
        return init();
    }

    void shutdown() override
    {
        ++g_appShutdown;
        m_state = kernel::app::AppState::Terminated;
    }

    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}

    void onWindowFocus() override { ++g_runtimeFocusAcquired; }
    void onWindowBlur() override { ++g_runtimeFocusLost; }
};

class FailingProbeApp : public kernel::app::KernelApp {
public:
    explicit FailingProbeApp(const char* name)
    {
        copyText(m_name, name, sizeof(m_name));
    }
    bool init() override { return false; }
    void shutdown() override { m_state = kernel::app::AppState::Terminated; }
    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
};

kernel::app::KernelApp* createSettingsProbe() { return new ProbeApp("Settings"); }
kernel::app::KernelApp* createNotepadProbe() { return new ProbeApp("Notepad"); }
kernel::app::KernelApp* createFailingSettingsProbe() { return new FailingProbeApp("FailingSettingsProbe"); }

void copySnapshotFromBytes(const uint8_t* source,
                           kernel::settings_native::SystemAboutSnapshotV2* output)
{
    uint8_t* destination = reinterpret_cast<uint8_t*>(output);
    for (size_t i = 0; i < sizeof(*output); ++i) destination[i] = source[i];
}

} // namespace

namespace kernel {
namespace compositor {

bool KernelCompositor::registerWindow(app::KernelWindow* window)
{
    if (!window) return false;
    if (window->id == 0) window->id = g_nextWindowId++;
    ++g_activeWindows;
    ++g_registeredWindows;
    if (g_focusedWindow && g_focusedWindow->owner) g_focusedWindow->owner->onWindowBlur();
    if (g_focusedWindow) g_focusedWindow->flags &= ~app::WF_FOCUSED;
    g_focusedWindow = window;
    g_focusedWindowId = window->id;
    window->flags |= app::WF_FOCUSED;
    if (window->owner) window->owner->onWindowFocus();
    return true;
}

void KernelCompositor::unregisterWindow(app::KernelWindow* window)
{
    if (!window) return;
    if (g_focusedWindowId == window->id) {
        if (window->owner) window->owner->onWindowBlur();
        g_focusedWindowId = 0;
        g_focusedWindow = nullptr;
    }
    if (g_activeWindows > 0) --g_activeWindows;
    ++g_unregisteredWindows;
}

void KernelCompositor::setFocus(uint32_t windowId)
{
    if (g_focusedWindowId == windowId) return;
    if (g_focusedWindow && g_focusedWindow->owner) g_focusedWindow->owner->onWindowBlur();
    g_focusedWindowId = windowId;
    g_focusedWindow = nullptr;
}

void KernelCompositor::handleKeyChar(char) {}

} // namespace compositor

namespace desktop {
void record_recent_program(const char*) {}
} // namespace desktop

namespace vfs {
Status stat(const char*, FileInfo*) { return VFS_ERR_NOT_FOUND; }
int32_t read_file(const char*, void*, uint32_t) { return VFS_ERR_NOT_FOUND; }
} // namespace vfs

namespace native_elf {
bool launch(const char*) { return false; }
const PackageInfo* lookup_package(const char*) { return nullptr; }
} // namespace native_elf
} // namespace kernel

extern "C" void desktop_request_redraw() {}

int main()
{
    using namespace kernel;
    using namespace kernel::app;
    using namespace kernel::settings_native;

    AppManager::init();
    AppLogger::init();
    expect(AppManager::registerAppWithIdentity("Settings", kSettingsRuntimeAppId,
                                                0xFF8060D0u, createSettingsProbe),
           "register dedicated Settings identity");
    expect(AppManager::registerAppWithIdentity("Notepad", "gxos.builtin.notepad",
                                                0xFF78B450u, createNotepadProbe),
           "register distinct non-Settings identity");
    expect(AppManager::registerAppWithIdentity("FailingSettingsProbe",
                                                "gxos.test.settings-failure",
                                                0xFF8060D0u, createFailingSettingsProbe),
           "register a Settings launch target that fails initialization");
    expect(!AppManager::registerAppWithIdentity("Settings", "gxos.builtin.settings",
                                                 0xFF8060D0u, createSettingsProbe),
           "duplicate launch name rejected");
    expect(!AppManager::registerAppWithIdentity("SettingsIdentityImpostor",
                                                 kSettingsRuntimeAppId,
                                                 0xFF78B450u, createNotepadProbe),
           "a different application factory cannot claim Settings runtime identity");
    expect(!AppManager::launchApp("UnknownNativeApp"), "unknown app launch rejected");

    const gxos::apps::LaunchTarget settingsTarget =
        appmodel::resolveLaunchTarget(kSettingsRuntimeAppId);
    expect(settingsTarget.type == gxos::apps::LaunchTargetType::BuiltInApp,
           "canonical Settings identity resolves as built-in");
    expect(settingsTarget.bareMetalAvailable &&
               std::string(settingsTarget.dispatchLaunchName) == "Settings",
           "canonical Settings identity resolves to dedicated KernelApp launch name");
    expect(std::string(settingsTarget.appId) == kSettingsRuntimeAppId,
           "resolver preserves canonical Settings identity");

    const gxos::apps::LaunchTarget startSettingsTarget =
        appmodel::resolveLaunchTarget("Settings");
    expect(startSettingsTarget.type == gxos::apps::LaunchTargetType::BuiltInApp &&
               startSettingsTarget.bareMetalAvailable &&
               std::string(startSettingsTarget.appId) == kSettingsRuntimeAppId &&
               std::string(startSettingsTarget.dispatchLaunchName) == "Settings",
           "Start-menu Settings resolves to its native built-in app target");

    const gxos::apps::LaunchTarget controlPanelTarget =
        appmodel::resolveLaunchTarget("Control Panel");
    expect(controlPanelTarget.type == gxos::apps::LaunchTargetType::ShellAction &&
               std::string(controlPanelTarget.shellAction) == "Control Panel" &&
               std::string(controlPanelTarget.dispatchLaunchName) == "Control Panel" &&
               controlPanelTarget.bareMetalAvailable &&
               std::string(controlPanelTarget.appId) != kSettingsRuntimeAppId,
           "legacy Control Panel resolves as its production bare-metal shell action");
    expect(std::string(startSettingsTarget.dispatchLaunchName) !=
               std::string(controlPanelTarget.dispatchLaunchName) &&
               AppManager::isAppAvailable(startSettingsTarget.dispatchLaunchName) &&
               !AppManager::isAppAvailable(controlPanelTarget.dispatchLaunchName),
           "Start-menu Settings and Control Panel use distinct production dispatch targets");
    const gxos::apps::LaunchTarget controlPanelIdentity =
        appmodel::resolveLaunchTarget("gxos.builtin.controlpanel");
    expect(std::string(controlPanelIdentity.appId) == "gxos.builtin.controlpanel" &&
               !controlPanelIdentity.bareMetalAvailable &&
               !AppManager::isAppAvailable("ControlPanel"),
           "hosted-only Control Panel identity has no bare-metal KernelApp factory");
    expect(!AppManager::launchAppWithParam("ControlPanel", kSettingsRuntimeAppId),
           "hosted-only Control Panel identity cannot launch through the bare-metal manager");
    expect(!AppManager::launchAppWithParam("Control Panel", kSettingsRuntimeAppId),
           "legacy Control Panel shell action is not a parameterized KernelApp launch");
    expect(appmodel::resolveLaunchTarget("unknown.s11.target").type ==
               gxos::apps::LaunchTargetType::Unknown,
           "unknown resolver target rejected");
    const int launchLogCountBeforeFailure = AppLogger::getLogCount();
    expect(!AppManager::launchApp("FailingSettingsProbe"),
           "registered Settings target reports an initialization failure");
    const app::AppLaunchLog* failureLog = AppLogger::getLog(launchLogCountBeforeFailure);
    expect(failureLog && failureLog->result == LaunchResult::FailedToInit,
           "registered launch failure retains its accurate initialization result");
    const int launchLogCountBeforeUnavailable = AppLogger::getLogCount();
    expect(!AppManager::launchApp("UnknownNativeApp"),
           "unavailable Settings-like launch target is rejected");
    const app::AppLaunchLog* unavailableLog = AppLogger::getLog(launchLogCountBeforeUnavailable);
    expect(unavailableLog && unavailableLog->result == LaunchResult::NotAvailable,
           "unregistered launch target is reported as unavailable");

    const gxos::apps::BuiltInAppMetadata* settingsMetadata =
        gxos::apps::FindBuiltInAppMetadataByAppId(kSettingsRuntimeAppId);
    const gxos::apps::BuiltInAppMetadata* controlPanelMetadata =
        gxos::apps::FindBuiltInAppMetadataByAppId("gxos.builtin.controlpanel");
    expect(settingsMetadata &&
               gxos::apps::IsBuiltInAppAvailableInBareMetal(*settingsMetadata),
           "Settings metadata enables the bare-metal target");
    expect(settingsMetadata && controlPanelMetadata &&
               !gxos::apps::detail::builtInTextEquals(settingsMetadata->appId,
                                                        controlPanelMetadata->appId),
           "Settings and Control Panel metadata identities do not collide");

    expect(AppManager::launchApp("Settings"), "Settings app launches");
    expect(AppManager::getRunningAppCount() == 1, "one Settings instance is running");
    KernelApp* settingsApp = AppManager::getRunningApp(0);
    expect(settingsApp && std::string(settingsApp->getRuntimeAppId()) == kSettingsRuntimeAppId,
           "AppManager assigns the runtime identity before init");
    expect(settingsApp && settingsApp->getWindow() &&
               settingsApp->getWindow()->owner == settingsApp,
           "Settings window owner matches its running application");
    expect(AppManager::launchApp("Settings") && AppManager::getRunningAppCount() == 1,
           "duplicate launch focuses existing instance without creating another");
    expect(AppManager::launchAppWithParam("Settings", "gxos.builtin.controlpanel") &&
               AppManager::getRunningAppCount() == 1 &&
               AppManager::getRunningApp(0) == settingsApp &&
               std::string(settingsApp->getRuntimeAppId()) == kSettingsRuntimeAppId,
           "duplicate launch arguments cannot retarget Settings identity or instance");

    SystemAboutSnapshotRequestV2 request = {
        sizeof(SystemAboutSnapshotRequestV2), kSystemAboutSnapshotVersion, 0
    };
    uint8_t unalignedStorage[sizeof(SystemAboutSnapshotV2) + 1] = {};
    size_t snapshotSize = 0;
    uint64_t snapshotRevision = 0;
    expect(readSystemAboutSnapshot(settingsApp, &request, &unalignedStorage[1],
                                   sizeof(SystemAboutSnapshotV2), &snapshotSize,
                                   &snapshotRevision) == SnapshotStatus::Ok,
           "provider writes into caller-owned unaligned storage");
    expect(snapshotSize == sizeof(SystemAboutSnapshotV2) && snapshotRevision == 1,
           "provider reports bounded size and snapshot revision");
    SystemAboutSnapshotV2 snapshot{};
    copySnapshotFromBytes(&unalignedStorage[1], &snapshot);
    expect(validSystemAboutSnapshot(snapshot), "provider snapshot fields validate");
    expect(snapshot.productName == std::string(KERNEL_NAME) &&
               snapshot.osReleaseVersion[0] == '\0' &&
               snapshot.kernelVersion == std::string(KERNEL_VERSION_STRING) &&
               snapshot.architecture == std::string(arch::get_arch_name()),
           "snapshot values come from kernel-owned sources and leave the undefined OS release absent");
    expect((snapshot.flags & kSystemAboutProductNameAvailable) != 0 &&
               (snapshot.flags & kSystemAboutKernelVersionAvailable) != 0 &&
               (snapshot.flags & kSystemAboutArchitectureAvailable) != 0 &&
               (snapshot.flags & kSystemAboutOsReleaseVersionAvailable) == 0,
           "snapshot flags distinguish required values from unavailable OS release metadata");

    request.version = kSystemAboutSnapshotVersion + 1;
    expect(readSystemAboutSnapshot(settingsApp, &request, &snapshot, sizeof(snapshot),
                                   &snapshotSize, &snapshotRevision) ==
               SnapshotStatus::UnsupportedVersion,
           "unsupported provider request version rejected");
    request.version = kSystemAboutSnapshotVersion;
    request.structureSize = sizeof(request) - 1u;
    expect(readSystemAboutSnapshot(settingsApp, &request, &snapshot, sizeof(snapshot),
                                   &snapshotSize, &snapshotRevision) ==
               SnapshotStatus::InvalidArgument,
           "undersized provider request rejected");
    request.structureSize = sizeof(request);
    expect(readSystemAboutSnapshot(settingsApp, &request, &snapshot,
                                   sizeof(snapshot) - 1u, &snapshotSize,
                                   &snapshotRevision) == SnapshotStatus::BufferTooSmall,
           "undersized provider output rejected");
    request.knownRevision = snapshotRevision + 1u;
    expect(readSystemAboutSnapshot(settingsApp, &request, &snapshot, sizeof(snapshot),
                                   &snapshotSize, &snapshotRevision) ==
               SnapshotStatus::StaleRevision,
           "provider rejects stale requested revision");
    request.knownRevision = 0;

    expect(buildSystemAboutSnapshot(KERNEL_NAME, nullptr, KERNEL_VERSION_STRING, "", 1,
                                    &snapshot) == SnapshotStatus::Unavailable,
           "unavailable kernel source is represented explicitly");
    expect(!validSystemAboutSnapshot(snapshot),
           "unavailable snapshot is not presented as valid data");
    expect(buildSystemAboutSnapshot(KERNEL_NAME, nullptr, KERNEL_VERSION_STRING,
                                    arch::get_arch_name(), 1, &snapshot) == SnapshotStatus::Ok,
           "valid kernel values create a fresh snapshot");
    expect(buildSystemAboutSnapshot(KERNEL_NAME, "release-test", KERNEL_VERSION_STRING,
                                    arch::get_arch_name(), 2, &snapshot) == SnapshotStatus::Ok &&
               std::string(snapshot.osReleaseVersion) == "release-test" &&
               (snapshot.flags & kSystemAboutOsReleaseVersionAvailable) != 0,
           "an explicitly supplied OS release is represented as a separate optional field");
    expect(buildSystemAboutSnapshot(KERNEL_NAME, "1234567890123456", KERNEL_VERSION_STRING,
                                    arch::get_arch_name(), 2, &snapshot) == SnapshotStatus::Unavailable,
           "oversized optional version data is rejected rather than truncated or hidden");
    expect(buildSystemAboutSnapshot(KERNEL_NAME, nullptr, KERNEL_VERSION_STRING,
                                    arch::get_arch_name(), 1, &snapshot) == SnapshotStatus::Ok,
           "snapshot can be restored after rejected optional metadata");
    snapshot.version++;
    expect(!validSystemAboutSnapshot(snapshot), "invalid snapshot version rejected");
    snapshot.version = kSystemAboutSnapshotVersion;
    snapshot.productName[0] = '\0';
    expect(!validSystemAboutSnapshot(snapshot), "invalid empty snapshot field rejected");

    settings::SettingsPresentationCore presentation;
    expect(presentation.selectedCategory() == settings::CategoryId::System,
           "shared core initializes to System");
    expect(presentation.setFocusedControl(settings::FocusControl::SystemAbout),
           "shared core accepts the System/About focus target");
    expect(presentation.selectCategory(settings::CategoryId::About) &&
               presentation.selectedCategory() == settings::CategoryId::About,
           "shared core selects About category");
    expect(presentation.focusedControl() == settings::FocusControl::None,
           "shared core clears focus when its category changes");
    settings::CapabilitySnapshot unavailableCapability = {
        settings::CapabilityId::Network,
        settings::CapabilityAvailability::Unavailable,
        0
    };
    expect(presentation.applyCapabilitySnapshot(unavailableCapability) &&
               presentation.capability(settings::CapabilityId::Network)->availability ==
                   settings::CapabilityAvailability::Unavailable,
           "unimplemented provider capability is visibly unavailable");
    settings::CapabilitySnapshot newerRevision = {
        settings::CapabilityId::SystemInformation,
        settings::CapabilityAvailability::ReadOnly,
        2
    };
    settings::CapabilitySnapshot olderRevision = newerRevision;
    olderRevision.revision = 1;
    expect(presentation.applyCapabilitySnapshot(newerRevision),
           "shared core accepts the current provider revision");
    expect(!presentation.applyCapabilitySnapshot(olderRevision),
           "shared core rejects stale capability revisions");
    expect(rejectSettingsMutation(nullptr, nullptr) == false,
           "S11 native adapter rejects mutation requests");
    settings::OperationRequest attemptedMutation{};
    attemptedMutation.capability = settings::CapabilityId::Display;
    attemptedMutation.operationCode = static_cast<uint16_t>(settings::SettingsOperationCode::ApplyDisplayConfiguration);
    expect(rejectSettingsMutation(nullptr, &attemptedMutation) == false,
           "unauthorized Settings mutation is rejected without a write surface");

    expect(AppManager::launchAppWithParam("Notepad", kSettingsRuntimeAppId),
           "parameterized non-Settings app launch remains available");
    KernelApp* otherApp = AppManager::getRunningApp(1);
    expect(otherApp && std::string(otherApp->getRuntimeAppId()) == "gxos.builtin.notepad",
           "ordinary launch parameter cannot assign Settings identity");
    expect(g_lastParameter == std::string(kSettingsRuntimeAppId),
           "identity-like text remains ordinary application data");
    expect(readSystemAboutSnapshot(otherApp, &request, &snapshot, sizeof(snapshot),
                                   &snapshotSize, &snapshotRevision) ==
               SnapshotStatus::Unauthorized,
           "provider denies a caller without runtime Settings identity, including launch-argument spoofing");
    if (otherApp) otherApp->requestClose();
    AppManager::update();
    expect(AppManager::getRunningAppCount() == 1 && g_activeWindows == 1,
           "closing the unrelated app releases its window ownership");
    expect(AppManager::getRunningApp(0) == settingsApp &&
               std::string(settingsApp->getRuntimeAppId()) == kSettingsRuntimeAppId &&
               settingsApp->getWindow() && settingsApp->getWindow()->owner == settingsApp,
           "closing an unrelated app leaves the Settings identity and window ownership intact");

    KernelApp* settingsBeforeCycles = AppManager::getRunningApp(0);
    if (settingsBeforeCycles) settingsBeforeCycles->requestClose();
    AppManager::update();
    expect(AppManager::getRunningAppCount() == 0 && g_activeWindows == 0,
           "initial Settings instance is fully closed before lifecycle repetitions");

    const int createdBeforeCycles = g_appCreated;
    const int destroyedBeforeCycles = g_appDestroyed;
    const int registeredBeforeCycles = g_registeredWindows;
    const int unregisteredBeforeCycles = g_unregisteredWindows;
    for (int i = 0; i < 100; ++i) {
        expect(AppManager::launchApp("Settings"), "lifecycle cycle launch succeeds");
        expect(AppManager::launchApp("Settings"), "duplicate lifecycle launch is idempotent");
        expect(AppManager::getRunningAppCount() == 1,
               "duplicate lifecycle launch preserves one running instance");
        KernelApp* current = AppManager::getRunningApp(0);
        expect(current && std::string(current->getRuntimeAppId()) == kSettingsRuntimeAppId,
               "reopened instance receives a fresh runtime identity");
        if (current) current->requestClose();
        AppManager::update();
        expect(AppManager::getRunningAppCount() == 0 && g_activeWindows == 0,
               "lifecycle close releases app and compositor window");
    }
    expect(g_appCreated - createdBeforeCycles == 100 &&
               g_appDestroyed - destroyedBeforeCycles == 100,
           "100 lifecycle repetitions destroy every application object");
    expect(g_registeredWindows - registeredBeforeCycles == 100 &&
               g_unregisteredWindows - unregisteredBeforeCycles == 100,
           "100 lifecycle repetitions balance window registration and release");
    expect(g_runtimeFocusAcquired >= 100 && g_runtimeFocusLost >= 100,
           "window focus transitions are delivered through the compositor boundary");

    std::cout << "Settings native runtime tests: " << (g_checks - g_failures)
              << "/" << g_checks << " passed; lifecycle repetitions=100\n";
    return g_failures == 0 ? 0 : 1;
}
