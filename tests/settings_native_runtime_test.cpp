#include "kernel/app_launch_target_resolver.h"
#include "kernel/kernel_app.h"
#include "kernel/kernel_compositor.h"
#include "kernel/framebuffer.h"
#include "kernel/desktop.h"
#include "kernel/desktop_gradient_persistence.h"
#include "display_configuration_command.h"
#include "display_configuration_service.h"
#include "kernel/native_elf_baremetal.h"
#include "kernel/settings_native_provider.h"
#include "kernel/settings_display_refresh.h"
#include "kernel/vfs.h"
#include "kernel/version.h"
#include "kernel/arch.h"
#include "app_launch_target.h"
#include "built_in_app_metadata.h"

#include <iostream>
#include <string>

namespace settings = gxos::apps::settings;
namespace settings_native = kernel::settings_native;

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
int g_runtimeAppearanceChanged = 0;
char g_lastParameter[64] = {};
bool g_testFramebufferAvailable = false;
uint32_t g_testFramebufferWidth = 0;
uint32_t g_testFramebufferHeight = 0;
uint32_t g_testFramebufferPitch = 0;
uint32_t g_testFramebufferBpp = 0;
kernel::framebuffer::PixelFormat g_testFramebufferFormat = kernel::framebuffer::PixelFormat::Unknown;
bool g_testActiveDisplayConfigurationAvailable = true;
bool g_testDisplayConfigurationServiceAvailable = true;
uint32_t g_testDisplayConfigurationGeneration = 8u;
uint32_t g_testLogicalDesktopWidth = 1379u;
uint32_t g_testLogicalDesktopHeight = 781u;
char g_testPrimaryModeId[32] = "mode-primary-test";
uint64_t g_testDisplayRequestId = 0u;
int g_testDisplayMutationSubmissions = 0;
int g_testDisplayQueryCount = 0;
int g_testAdapterDisplayReads = 0;
int g_testRefreshRepaints = 0;
bool g_testCorruptAdapterSnapshotVersion = false;
bool g_testTruncateAdapterSnapshot = false;
bool g_testAppearanceConfigurationAvailable = true;
kernel::desktop::DesktopAppearanceConfigurationV1 g_testAppearanceConfiguration{};
kernel::desktop::DesktopGradientApplyStatus g_testGradientApplyStatus =
    kernel::desktop::DesktopGradientApplyStatus::Committed;
int g_testGradientApplyCalls = 0;
bool g_testGradientApplyMutatesOnCommit = true;
char g_testGradientPersistedId[96] = "legacy_ameobagx";
bool g_testConfigMounted = false;
bool g_testConfigReadOnly = false;
char g_testConfigBytes[kernel::desktop::kGradientConfigTextCapacity] = {};
size_t g_testConfigLength = 0u;
int g_testConfigWriteCount = 0;
int g_testConfigFailWriteNumber = 0;
bool g_testConfigFailWritePartial = false;
int g_testConfigCorruptOnWriteNumber = 0;
bool g_testConfigFailNextRead = false;
bool g_testConfigCorruptNextRead = false;
kernel::vfs::MountPoint g_testConfigMount{};

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
    void onDesktopAppearanceChanged(uint64_t revision) override
    {
        if (revision != 0u) ++g_runtimeAppearanceChanged;
    }
};

class SettingsRefreshProbeApp : public ProbeApp {
public:
    SettingsRefreshProbeApp()
        : ProbeApp("Settings"), m_focused(false)
    {
        m_adapter.structureSize = sizeof(m_adapter);
        m_adapter.version = settings::kSettingsPresentationAdapterVersion;
        m_adapter.context = this;
        m_adapter.readProviderSnapshot = readProviderSnapshot;
    }

    bool init() override
    {
        if (!ProbeApp::init()) return false;
        m_presentation.reset();
        m_presentation.selectCategory(settings::CategoryId::System);
        m_refreshState.reset();
        m_changeSignal.reset();
        settings_native::refreshDisplaySnapshot(&m_adapter, m_refreshState, m_presentation);
        return true;
    }

    void shutdown() override
    {
        m_changeSignal.reset();
        m_refreshState.reset();
        ProbeApp::shutdown();
    }

    void update() override
    {
        uint32_t generation = 0u;
        if (!m_changeSignal.consume(&generation)) return;
        (void)generation;
        const settings_native::DisplaySnapshotRefreshResult result =
            settings_native::refreshDisplaySnapshot(&m_adapter, m_refreshState, m_presentation);
        if (m_focused && m_presentation.selectedCategory() == settings::CategoryId::Display &&
            (result == settings_native::DisplaySnapshotRefreshResult::Updated ||
             result == settings_native::DisplaySnapshotRefreshResult::Unavailable)) {
            ++g_testRefreshRepaints;
        }
    }

    void onDisplayConfigurationChanged(uint32_t generation) override
    {
        if (m_state == kernel::app::AppState::Running && m_window) {
            m_changeSignal.signal(generation);
        }
    }

    void onWindowFocus() override
    {
        m_focused = true;
        ProbeApp::onWindowFocus();
    }

    void onWindowBlur() override
    {
        m_focused = false;
        ProbeApp::onWindowBlur();
    }

    bool selectCategory(settings::CategoryId category)
    {
        if (!m_presentation.selectCategory(category)) return false;
        if (category == settings::CategoryId::Display) {
            settings_native::refreshDisplaySnapshot(&m_adapter, m_refreshState, m_presentation);
        }
        return true;
    }

    void setFocused(bool focused) { m_focused = focused; }
    settings_native::DisplaySnapshotRefreshState& refreshState() { return m_refreshState; }
    const settings_native::DisplaySnapshotRefreshState& refreshState() const { return m_refreshState; }
    settings::SettingsPresentationCore& presentation() { return m_presentation; }

private:
    static bool readProviderSnapshot(void* context, settings::CapabilityId capability,
                                     void* output, size_t capacity, size_t* outSize,
                                     uint64_t* outRevision)
    {
        SettingsRefreshProbeApp* self = static_cast<SettingsRefreshProbeApp*>(context);
        if (!self || capability != settings::CapabilityId::Display) return false;
        ++g_testAdapterDisplayReads;
        const settings_native::DisplaySnapshotRequestV1 request = {
            sizeof(settings_native::DisplaySnapshotRequestV1),
            settings_native::kDisplaySnapshotVersion,
            0u
        };
        const bool readOk = settings_native::readDisplaySnapshot(
            self, &request, output, capacity, outSize, outRevision) ==
                settings_native::SnapshotStatus::Ok;
        if (readOk && output && g_testCorruptAdapterSnapshotVersion) {
            static_cast<settings_native::DisplaySnapshotV1*>(output)->version++;
        }
        if (readOk && outSize && g_testTruncateAdapterSnapshot) --*outSize;
        return readOk;
    }

    settings::SettingsPresentationAdapterV1 m_adapter{};
    settings::SettingsPresentationCore m_presentation;
    settings_native::DisplaySnapshotRefreshState m_refreshState;
    settings_native::DisplayConfigurationChangeSignal m_changeSignal;
    bool m_focused;
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

kernel::app::KernelApp* createSettingsProbe() { return new SettingsRefreshProbeApp(); }
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
namespace framebuffer {

bool is_available() { return g_testFramebufferAvailable; }
uint32_t get_width() { return g_testFramebufferWidth; }
uint32_t get_height() { return g_testFramebufferHeight; }
uint32_t get_pitch() { return g_testFramebufferPitch; }
uint8_t get_bpp() { return static_cast<uint8_t>(g_testFramebufferBpp); }
PixelFormat get_pixel_format() { return g_testFramebufferFormat; }

} // namespace framebuffer
} // namespace kernel

namespace gxos {
namespace display {

uint64_t DisplayConfigurationService::nextRequestId()
{
    return ++g_testDisplayRequestId;
}

bool DisplayConfigurationService::submit(
    const DisplayConfigurationCommand& command,
    DisplayConfigurationResponse& response)
{
    response = DisplayConfigurationResponse{};
    response.version = kDisplayConfigurationContractVersion;
    response.structureSize = sizeof(response);
    response.requestId = command.requestId;
    response.commandType = command.commandType;
    response.completed = 1u;
    if (command.commandType != static_cast<uint32_t>(DisplayConfigurationCommandType::QueryActiveConfiguration)) {
        ++g_testDisplayMutationSubmissions;
        return false;
    }
    ++g_testDisplayQueryCount;
    if (!g_testDisplayConfigurationServiceAvailable) {
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::BackendBusy);
        return false;
    }
    response.activeConfigurationGeneration = g_testDisplayConfigurationGeneration;
    if (!g_testActiveDisplayConfigurationAvailable) {
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::BackendUnavailable);
        return false;
    }

    DisplayConfigurationSnapshot& active = response.activeConfiguration;
    active.version = kDisplayConfigurationContractVersion;
    active.structureSize = sizeof(active);
    active.mode = static_cast<uint32_t>(DisplayConfigurationMode::Extend);
    active.outputCount = 1u;
    active.virtualDesktopWidth = static_cast<int32_t>(g_testLogicalDesktopWidth);
    active.virtualDesktopHeight = static_cast<int32_t>(g_testLogicalDesktopHeight);
    DisplayConfigurationOutput& primary = active.outputs[0];
    copyText(primary.modeId, g_testPrimaryModeId, sizeof(primary.modeId));
    primary.enabled = 1u;
    primary.primary = 1u;
    response.success = 1u;
    response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::Success);
    return true;
}

} // namespace display
} // namespace gxos

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

bool get_appearance_configuration(DesktopAppearanceConfigurationV1* output)
{
    if (!output || !g_testAppearanceConfigurationAvailable) return false;
    *output = g_testAppearanceConfiguration;
    return true;
}

static const DesktopBuiltInGradientPresetV1 kTestGradients[] = {
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF142850u, 0xFF0F121Cu, 0xFF192337u, "gradient_midnight", "Midnight" },
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF063B5Cu, 0xFF061522u, 0xFF1496B8u, "gradient_ocean", "Ocean" },
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF0B2C35u, 0xFF251046u, 0xFF21C78Au, "gradient_aurora", "Aurora" },
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF26104Au, 0xFF0D0B18u, 0xFF8A52E8u, "gradient_violet", "Violet" },
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF5E1B45u, 0xFF17101Eu, 0xFFE06A55u, "gradient_sunset", "Sunset" },
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF123B2Bu, 0xFF071711u, 0xFF5E9C50u, "gradient_forest", "Forest" },
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF45170Fu, 0xFF120B09u, 0xFFD46A33u, "gradient_ember", "Ember" },
    { sizeof(DesktopBuiltInGradientPresetV1), kDesktopBuiltInGradientPresetVersion,
      0xFF333946u, 0xFF111318u, 0xFF7E8796u, "gradient_graphite", "Graphite" }
};

uint32_t get_builtin_gradient_preset_count()
{
    return static_cast<uint32_t>(sizeof(kTestGradients) / sizeof(kTestGradients[0]));
}

bool get_builtin_gradient_preset(uint32_t index, DesktopBuiltInGradientPresetV1* output)
{
    if (!output || index >= get_builtin_gradient_preset_count()) return false;
    *output = kTestGradients[index];
    return true;
}

DesktopGradientApplyStatus apply_builtin_gradient_preset(
    const char* presetId, uint64_t expectedRevision,
    DesktopGradientApplyResultV1* output)
{
    ++g_testGradientApplyCalls;
    if (!output) return DesktopGradientApplyStatus::ApplyFailed;
    DesktopGradientApplyResultV1 result{};
    result.structureSize = sizeof(result);
    result.version = kDesktopGradientApplyResultVersion;
    result.revision = g_testAppearanceConfiguration.revision;
    result.status = DesktopGradientApplyStatus::InvalidPreset;
    if (expectedRevision == 0u ||
        expectedRevision != g_testAppearanceConfiguration.revision) {
        result.status = DesktopGradientApplyStatus::StaleRevision;
    } else {
        const DesktopBuiltInGradientPresetV1* selected = nullptr;
        for (uint32_t index = 0u; index < get_builtin_gradient_preset_count(); ++index) {
            if (presetId && std::string(kTestGradients[index].id) == presetId) {
                selected = &kTestGradients[index];
                break;
            }
        }
        if (!selected) {
            result.status = DesktopGradientApplyStatus::InvalidPreset;
        } else {
            result.status = g_testGradientApplyStatus;
            if (result.status == DesktopGradientApplyStatus::Committed &&
                g_testGradientApplyMutatesOnCommit) {
                g_testAppearanceConfiguration.backgroundKind =
                    kDesktopAppearanceBackgroundGradient;
                copyText(g_testAppearanceConfiguration.backgroundId, selected->id,
                         sizeof(g_testAppearanceConfiguration.backgroundId));
                copyText(g_testAppearanceConfiguration.backgroundName,
                         selected->displayName,
                         sizeof(g_testAppearanceConfiguration.backgroundName));
                g_testAppearanceConfiguration.gradientTopColor = selected->topColor;
                g_testAppearanceConfiguration.gradientBottomColor = selected->bottomColor;
                g_testAppearanceConfiguration.gradientAccentColor = selected->accentColor;
                ++g_testAppearanceConfiguration.revision;
                copyText(g_testGradientPersistedId, selected->id,
                         sizeof(g_testGradientPersistedId));
            }
        }
    }

    result.revision = g_testAppearanceConfiguration.revision;
    result.flags = kDesktopGradientApplyActiveAvailable;
    copyText(result.appliedId, g_testAppearanceConfiguration.backgroundId,
             sizeof(result.appliedId));
    if (g_testGradientPersistedId[0]) {
        result.flags |= kDesktopGradientApplyPersistedAvailable;
        copyText(result.persistedId, g_testGradientPersistedId,
                 sizeof(result.persistedId));
    }
    if (result.status == DesktopGradientApplyStatus::SaveFailedRolledBack ||
        result.status == DesktopGradientApplyStatus::ReadbackFailedRolledBack) {
        result.flags |= kDesktopGradientApplyRollbackVerified;
    }
    if (result.status == DesktopGradientApplyStatus::RollbackFailed) {
        result.revision = g_testAppearanceConfiguration.revision + 1u;
    }
    *output = result;
    return result.status;
}
} // namespace desktop

namespace vfs {
const MountPoint* get_mount(const char*)
{
    if (!g_testConfigMounted) return nullptr;
    g_testConfigMount.active = true;
    g_testConfigMount.readOnly = g_testConfigReadOnly;
    return &g_testConfigMount;
}

Status stat(const char*, FileInfo* output)
{
    if (!g_testConfigMounted || !output || g_testConfigLength == 0u)
        return VFS_ERR_NOT_FOUND;
    output->type = FILE_TYPE_REGULAR;
    output->size = g_testConfigLength;
    return VFS_OK;
}

int32_t read_file(const char*, void* output, uint32_t capacity)
{
    if (!g_testConfigMounted || !output || g_testConfigLength == 0u)
        return VFS_ERR_NOT_FOUND;
    if (g_testConfigFailNextRead) {
        g_testConfigFailNextRead = false;
        return VFS_ERR_IO;
    }
    const size_t count = g_testConfigLength < capacity
        ? g_testConfigLength : capacity;
    char* destination = static_cast<char*>(output);
    for (size_t index = 0u; index < count; ++index) {
        destination[index] = g_testConfigBytes[index];
    }
    if (g_testConfigCorruptNextRead && count > 0u) {
        g_testConfigCorruptNextRead = false;
        destination[0] = destination[0] == 'v' ? 'x' : 'v';
    }
    return static_cast<int32_t>(count);
}

int32_t write_file(const char*, const void* bytes, uint32_t size)
{
    ++g_testConfigWriteCount;
    if (!g_testConfigMounted) return VFS_ERR_NOT_MOUNT;
    if (g_testConfigReadOnly) return VFS_ERR_READ_ONLY;
    if (!bytes || size >= sizeof(g_testConfigBytes)) return VFS_ERR_INVALID;
    if (g_testConfigFailWriteNumber != 0 &&
        g_testConfigWriteCount == g_testConfigFailWriteNumber) {
        if (g_testConfigFailWritePartial) {
            const uint32_t partial = size / 2u;
            const char* source = static_cast<const char*>(bytes);
            for (uint32_t index = 0u; index < partial; ++index) {
                g_testConfigBytes[index] = source[index];
            }
            g_testConfigLength = partial;
        }
        return VFS_ERR_IO;
    }
    const char* source = static_cast<const char*>(bytes);
    for (uint32_t index = 0u; index < size; ++index) {
        g_testConfigBytes[index] = source[index];
    }
    g_testConfigLength = size;
    if (g_testConfigCorruptOnWriteNumber != 0 &&
        g_testConfigWriteCount == g_testConfigCorruptOnWriteNumber) {
        g_testConfigCorruptNextRead = true;
    }
    return static_cast<int32_t>(size);
}
} // namespace vfs

namespace native_elf {
bool launch(const char*) { return false; }
const PackageInfo* lookup_package(const char*) { return nullptr; }
} // namespace native_elf
} // namespace kernel

extern "C" void desktop_request_redraw() {}

static void setTestDesktopConfig(const char* text)
{
    const size_t length = text ? std::char_traits<char>::length(text) : 0u;
    if (length >= sizeof(g_testConfigBytes)) return;
    for (size_t index = 0u; index < length; ++index) {
        g_testConfigBytes[index] = text[index];
    }
    g_testConfigLength = length;
    g_testConfigMounted = true;
    g_testConfigReadOnly = false;
    g_testConfigWriteCount = 0;
    g_testConfigFailWriteNumber = 0;
    g_testConfigFailWritePartial = false;
    g_testConfigCorruptOnWriteNumber = 0;
    g_testConfigFailNextRead = false;
    g_testConfigCorruptNextRead = false;
}

static std::string testDesktopConfigText()
{
    return std::string(g_testConfigBytes, g_testConfigLength);
}

static void testGradientConfigPersistence()
{
    using namespace kernel::desktop;
    const char* original =
        "version=1\r\n"
        "wallpaperId=gradient_sunset\r\n"
        "backgroundScaleMode=fit\r\n"
        "taskbarPosition=top\r\n"
        "futureAppearanceKey=keep-this-value\r\n";
    char persistedId[kGradientPresetIdCapacity] = {};
    setTestDesktopConfig(original);
    expect(readCanonicalWallpaperId(persistedId, sizeof(persistedId)) &&
               std::string(persistedId) == "gradient_sunset",
           "canonical desktop configuration exposes its current wallpaper id");

    GradientPersistenceResult persistence{};
    expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                    &persistence) == GradientPersistenceStatus::Ok &&
               std::string(persistence.persistedId) == "gradient_ocean" &&
               (persistence.flags & kGradientPersistenceReadbackAvailable) != 0u,
           "gradient persistence commits and reads back the requested id");
    std::string expected = original;
    expected.replace(expected.find("gradient_sunset"),
                     std::string("gradient_sunset").size(), "gradient_ocean");
    expect(testDesktopConfigText() == expected &&
               testDesktopConfigText().find("futureAppearanceKey=keep-this-value") != std::string::npos &&
               testDesktopConfigText().find("taskbarPosition=top") != std::string::npos,
           "canonical update preserves unrelated keys, values, and line endings");

    setTestDesktopConfig(original);
    expect(persistBuiltInGradientId("gradient_violet", "gradient_ocean",
                                    &persistence) == GradientPersistenceStatus::Conflict &&
               g_testConfigWriteCount == 0 &&
               std::string(persistence.persistedId) == "gradient_sunset",
           "canonical config conflict is rejected without a write");

    setTestDesktopConfig(original);
    g_testConfigMounted = false;
    expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                    &persistence) == GradientPersistenceStatus::Unavailable &&
               g_testConfigWriteCount == 0,
           "unavailable configuration storage rejects a gradient commit");
    setTestDesktopConfig(original);
    g_testConfigReadOnly = true;
    expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                    &persistence) == GradientPersistenceStatus::Unavailable &&
               g_testConfigWriteCount == 0,
           "read-only configuration storage rejects a gradient commit");

    setTestDesktopConfig(
        "wallpaperId=gradient_sunset\nwallpaperId=gradient_violet\n");
    expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                    &persistence) == GradientPersistenceStatus::InvalidConfiguration &&
               g_testConfigWriteCount == 0,
           "ambiguous duplicate wallpaper keys are rejected before writing");

    setTestDesktopConfig(original);
    g_testConfigFailWriteNumber = 1;
    g_testConfigFailWritePartial = true;
    expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                    &persistence) ==
               GradientPersistenceStatus::WriteFailedRolledBack &&
               (persistence.flags & kGradientPersistenceRollbackVerified) != 0u &&
               std::string(persistence.persistedId) == "gradient_sunset" &&
               testDesktopConfigText() == original,
           "partial write failure restores the exact previous canonical file");

    setTestDesktopConfig(original);
    g_testConfigCorruptOnWriteNumber = 1;
    expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                    &persistence) ==
               GradientPersistenceStatus::ReadbackFailedRolledBack &&
               (persistence.flags & kGradientPersistenceRollbackVerified) != 0u &&
               testDesktopConfigText() == original,
           "readback mismatch restores the exact previous canonical file");

    setTestDesktopConfig(original);
    g_testConfigFailWriteNumber = 1;
    g_testConfigFailWritePartial = true;
    g_testConfigCorruptOnWriteNumber = 2;
    // The first write fails after a partial update; the restoration write is
    // deliberately made unverifiable by corrupting its readback.
    expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                    &persistence) ==
               GradientPersistenceStatus::RollbackFailed &&
               (persistence.flags & kGradientPersistenceRollbackAttempted) != 0u &&
               (persistence.flags & kGradientPersistenceRollbackVerified) == 0u,
           "rollback failure is returned distinctly and is never reported as restored");

    const char* alternatingIds[2] = { "gradient_sunset", "gradient_ocean" };
    for (int cycle = 0; cycle < 100; ++cycle) {
        const char* from = alternatingIds[cycle % 2];
        const char* to = alternatingIds[(cycle + 1) % 2];
        std::string source = original;
        source.replace(source.find("gradient_sunset"),
                       std::string("gradient_sunset").size(), from);
        setTestDesktopConfig(source.c_str());
        expect(persistBuiltInGradientId(from, to, &persistence) ==
                   GradientPersistenceStatus::Ok &&
                   std::string(persistence.persistedId) == to,
               "repeated gradient commit cycle completes with verified state");
    }
    for (int cycle = 0; cycle < 100; ++cycle) {
        setTestDesktopConfig(original);
        g_testConfigFailWriteNumber = 1;
        g_testConfigFailWritePartial = true;
        expect(persistBuiltInGradientId("gradient_sunset", "gradient_ocean",
                                        &persistence) ==
                   GradientPersistenceStatus::WriteFailedRolledBack &&
                   (persistence.flags & kGradientPersistenceRollbackVerified) != 0u &&
                   testDesktopConfigText() == original,
               "repeated injected write failure preserves the previous config");
    }

    g_testConfigMounted = false;
    g_testConfigReadOnly = false;
}

int main()
{
    using namespace kernel;
    using namespace kernel::app;
    using namespace kernel::settings_native;

    AppManager::init();
    AppLogger::init();
    g_testFramebufferAvailable = true;
    g_testFramebufferWidth = 1377u;
    g_testFramebufferHeight = 779u;
    g_testFramebufferPitch = 5512u;
    g_testFramebufferBpp = 32u;
    g_testFramebufferFormat = framebuffer::PixelFormat::B8G8R8A8;
    g_testDisplayConfigurationGeneration = 8u;
    AppManager::notifyDisplayConfigurationChanged(8u);
    expect(AppManager::getRunningAppCount() == 0,
           "a display notification before Settings initializes has no retained recipient");
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
    SettingsRefreshProbeApp* settingsRefreshApp =
        static_cast<SettingsRefreshProbeApp*>(settingsApp);
    expect(settingsRefreshApp && settingsRefreshApp->refreshState().hasSnapshot() &&
               settingsRefreshApp->refreshState().snapshot().framebufferWidth == 1377u &&
               settingsRefreshApp->refreshState().revision() == 9u,
           "Settings initialization captures the current provider snapshot after a pre-init change");
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

    g_testAppearanceConfiguration = kernel::desktop::DesktopAppearanceConfigurationV1{};
    g_testAppearanceConfiguration.structureSize = sizeof(g_testAppearanceConfiguration);
    g_testAppearanceConfiguration.version = kernel::desktop::kDesktopAppearanceConfigurationVersion;
    g_testAppearanceConfiguration.revision = 4u;
    g_testAppearanceConfiguration.backgroundKind = kernel::desktop::kDesktopAppearanceBackgroundWallpaper;
    g_testAppearanceConfiguration.visibleIconFlags =
        kernel::desktop::kDesktopAppearanceIconTrash |
        kernel::desktop::kDesktopAppearanceIconFileManager;
    copyText(g_testAppearanceConfiguration.backgroundId, "legacy_ameobagx",
             sizeof(g_testAppearanceConfiguration.backgroundId));
    copyText(g_testAppearanceConfiguration.backgroundName, "Ameoba GX",
             sizeof(g_testAppearanceConfiguration.backgroundName));
    copyText(g_testAppearanceConfiguration.backgroundScaleMode, "fill",
             sizeof(g_testAppearanceConfiguration.backgroundScaleMode));

    DesktopAppearanceSnapshotV1 appearanceSnapshot{};
    expect(buildDesktopAppearanceSnapshot(
               "legacy_ameobagx", "Ameoba GX", "fill",
               kernel::desktop::kDesktopAppearanceBackgroundWallpaper,
               0u, 0u, 0u,
               g_testAppearanceConfiguration.visibleIconFlags, 4u,
               &appearanceSnapshot) == SnapshotStatus::Ok &&
               validDesktopAppearanceSnapshot(appearanceSnapshot),
           "valid wallpaper and desktop icon values produce a read-only appearance snapshot");
    expect(appearanceSnapshot.backgroundKind == DesktopBackgroundKind::Wallpaper &&
               std::string(appearanceSnapshot.backgroundId) == "legacy_ameobagx" &&
               std::string(appearanceSnapshot.backgroundName) == "Ameoba GX" &&
               std::string(appearanceSnapshot.backgroundScaleMode) == "fill" &&
               (appearanceSnapshot.flags & kDesktopAppearanceIconTrashVisible) != 0u &&
               (appearanceSnapshot.flags & kDesktopAppearanceIconFileManagerVisible) != 0u &&
               (appearanceSnapshot.flags & kDesktopAppearanceIconThisSystemVisible) == 0u,
           "appearance snapshot preserves wallpaper identity, scale, and each supported icon visibility");
    expect(buildDesktopAppearanceSnapshot(
               "gradient_aurora", "Aurora", "fit",
               kernel::desktop::kDesktopAppearanceBackgroundGradient,
               0xFF0B2C35u, 0xFF251046u, 0xFF21C78Au,
               kernel::desktop::kDesktopAppearanceIconThisSystem, 5u,
               &appearanceSnapshot) == SnapshotStatus::Ok &&
               validDesktopAppearanceSnapshot(appearanceSnapshot) &&
               appearanceSnapshot.backgroundKind == DesktopBackgroundKind::Gradient &&
               appearanceSnapshot.gradientTopColor == 0xFF0B2C35u &&
               appearanceSnapshot.gradientBottomColor == 0xFF251046u &&
               appearanceSnapshot.gradientAccentColor == 0xFF21C78Au &&
               (appearanceSnapshot.flags & kDesktopAppearanceGradientColorsAvailable) != 0u,
           "gradient snapshot includes the selected gradient's exact palette");
    expect(buildDesktopAppearanceSnapshot(
               "legacy_blue_flower", "Blue Flower", "fill",
               kernel::desktop::kDesktopAppearanceBackgroundWallpaper,
               1u, 0u, 0u, 0u, 1u, &appearanceSnapshot) == SnapshotStatus::InvalidArgument,
           "wallpaper snapshot rejects gradient-only color fields");
    expect(buildDesktopAppearanceSnapshot(
               "legacy_blue_flower", "Blue Flower", "fill", 99u,
               0u, 0u, 0u, 0u, 1u, &appearanceSnapshot) == SnapshotStatus::InvalidArgument,
           "unsupported appearance background kinds are rejected");
    expect(buildDesktopAppearanceSnapshot(
               "legacy_blue_flower", "Blue Flower", "unsupported",
               kernel::desktop::kDesktopAppearanceBackgroundWallpaper,
               0u, 0u, 0u, 0u, 1u, &appearanceSnapshot) == SnapshotStatus::Unavailable,
           "appearance snapshots reject unsupported wallpaper scale modes");

    DesktopAppearanceSnapshotRequestV1 appearanceRequest = {
        sizeof(DesktopAppearanceSnapshotRequestV1), kDesktopAppearanceSnapshotVersion, 0u
    };
    uint8_t unalignedAppearance[sizeof(DesktopAppearanceSnapshotV1) + 1u] = {};
    expect(readDesktopAppearanceSnapshot(settingsApp, &appearanceRequest,
               &unalignedAppearance[1], sizeof(DesktopAppearanceSnapshotV1),
               &snapshotSize, &snapshotRevision) == SnapshotStatus::Ok &&
               snapshotSize == sizeof(DesktopAppearanceSnapshotV1) && snapshotRevision == 4u,
           "trusted Settings caller reads the active desktop appearance into caller-owned storage");
    DesktopAppearanceSnapshotV1 readAppearance{};
    const uint8_t* appearanceSource = &unalignedAppearance[1];
    uint8_t* appearanceDestination = reinterpret_cast<uint8_t*>(&readAppearance);
    for (size_t index = 0; index < sizeof(readAppearance); ++index) {
        appearanceDestination[index] = appearanceSource[index];
    }
    expect(validDesktopAppearanceSnapshot(readAppearance) &&
               readAppearance.backgroundKind == DesktopBackgroundKind::Wallpaper &&
               std::string(readAppearance.backgroundId) == "legacy_ameobagx" &&
               (readAppearance.flags & kDesktopAppearanceIconTrashVisible) != 0u,
           "provider snapshot matches the live desktop configuration without exposing desktop pointers");
    appearanceRequest.knownRevision = snapshotRevision + 1u;
    expect(readDesktopAppearanceSnapshot(settingsApp, &appearanceRequest,
               &readAppearance, sizeof(readAppearance), &snapshotSize,
               &snapshotRevision) == SnapshotStatus::StaleRevision,
           "appearance provider rejects a stale requested revision");
    appearanceRequest.knownRevision = 0u;
    appearanceRequest.version = kDesktopAppearanceSnapshotVersion + 1u;
    expect(readDesktopAppearanceSnapshot(settingsApp, &appearanceRequest,
               &readAppearance, sizeof(readAppearance), &snapshotSize,
               &snapshotRevision) == SnapshotStatus::UnsupportedVersion,
           "appearance provider rejects an unsupported request version");
    appearanceRequest.version = kDesktopAppearanceSnapshotVersion;
    appearanceRequest.structureSize = sizeof(appearanceRequest) - 1u;
    expect(readDesktopAppearanceSnapshot(settingsApp, &appearanceRequest,
               &readAppearance, sizeof(readAppearance), &snapshotSize,
               &snapshotRevision) == SnapshotStatus::InvalidArgument,
           "appearance provider rejects an undersized request");
    appearanceRequest.structureSize = sizeof(appearanceRequest);
    expect(readDesktopAppearanceSnapshot(settingsApp, &appearanceRequest,
               &readAppearance, sizeof(readAppearance) - 1u, &snapshotSize,
               &snapshotRevision) == SnapshotStatus::BufferTooSmall,
           "appearance provider rejects an undersized output buffer");
    g_testAppearanceConfigurationAvailable = false;
    expect(readDesktopAppearanceSnapshot(settingsApp, &appearanceRequest,
               &readAppearance, sizeof(readAppearance), &snapshotSize,
               &snapshotRevision) == SnapshotStatus::Unavailable,
           "unavailable desktop configuration remains explicitly unavailable");
    g_testAppearanceConfigurationAvailable = true;

    BuiltInGradientPresetV1 gradientPresets[kBuiltInGradientPresetCapacity]{};
    size_t gradientPresetCount = 0u;
    expect(readBuiltInGradientPresets(settingsApp, gradientPresets,
                                      kBuiltInGradientPresetCapacity,
                                      &gradientPresetCount) == SnapshotStatus::Ok &&
               gradientPresetCount == 8u &&
               std::string(gradientPresets[1].id) == "gradient_ocean" &&
               gradientPresets[1].topColor == 0xFF063B5Cu,
           "Settings reads the fixed gradient list from the desktop-owned preset registry");
    expect(readBuiltInGradientPresets(settingsApp, gradientPresets, 2u,
                                      &gradientPresetCount) == SnapshotStatus::BufferTooSmall &&
               gradientPresetCount == 8u,
           "gradient list reports its bounded required capacity");

    g_testAppearanceConfiguration.backgroundKind =
        kernel::desktop::kDesktopAppearanceBackgroundGradient;
    g_testAppearanceConfiguration.revision = 4u;
    copyText(g_testAppearanceConfiguration.backgroundId, "gradient_sunset",
             sizeof(g_testAppearanceConfiguration.backgroundId));
    copyText(g_testAppearanceConfiguration.backgroundName, "Sunset",
             sizeof(g_testAppearanceConfiguration.backgroundName));
    g_testAppearanceConfiguration.gradientTopColor = 0xFF5E1B45u;
    g_testAppearanceConfiguration.gradientBottomColor = 0xFF17101Eu;
    g_testAppearanceConfiguration.gradientAccentColor = 0xFFE06A55u;
    copyText(g_testGradientPersistedId, "gradient_sunset",
             sizeof(g_testGradientPersistedId));
    g_testGradientApplyStatus = kernel::desktop::DesktopGradientApplyStatus::Committed;
    g_testGradientApplyMutatesOnCommit = true;
    g_testGradientApplyCalls = 0;
    ApplyBuiltInGradientPresetRequestV1 gradientRequest{};
    gradientRequest.structureSize = sizeof(gradientRequest);
    gradientRequest.version = kApplyBuiltInGradientPresetVersion;
    gradientRequest.expectedRevision = 4u;
    copyText(gradientRequest.presetId, "gradient_ocean",
             sizeof(gradientRequest.presetId));
    ApplyBuiltInGradientPresetResultV1 gradientResult{};
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::Committed &&
               gradientResult.status == GradientOperationStatus::Committed &&
               (gradientResult.flags & kGradientOperationActiveAvailable) != 0u &&
               (gradientResult.flags & kGradientOperationPersistedAvailable) != 0u &&
               std::string(gradientResult.appliedId) == "gradient_ocean" &&
               std::string(gradientResult.persistedId) == "gradient_ocean" &&
               gradientResult.revision == 5u,
           "authorized Settings request commits and reports verified active and persisted ids");
    expect(g_testGradientApplyCalls == 1 &&
               g_testAppearanceConfiguration.revision == 5u &&
               std::string(g_testAppearanceConfiguration.backgroundId) == "gradient_ocean",
           "authorized operation reaches the shared desktop owner and updates its source fixture");

    gradientRequest.expectedRevision = 5u;
    copyText(gradientRequest.presetId, "gradient_ocean",
             sizeof(gradientRequest.presetId));
    g_testGradientApplyStatus = kernel::desktop::DesktopGradientApplyStatus::NoChange;
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::NoChange &&
               gradientResult.revision == 5u &&
               std::string(gradientResult.appliedId) == "gradient_ocean",
           "selecting the current preset reports a no-op with unchanged revision");

    gradientRequest.expectedRevision = 4u;
    g_testGradientApplyStatus = kernel::desktop::DesktopGradientApplyStatus::Committed;
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::StaleRevision &&
               g_testGradientApplyCalls == 3,
           "stale appearance revision is rejected before the desktop mutation");

    gradientRequest.expectedRevision = 5u;
    copyText(gradientRequest.presetId, "gradient_unknown",
             sizeof(gradientRequest.presetId));
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::InvalidPreset &&
               g_testGradientApplyCalls == 4,
           "unknown built-in gradient id is rejected by the shared desktop registry");
    copyText(gradientRequest.presetId, "gradient!ocean",
             sizeof(gradientRequest.presetId));
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::InvalidPreset &&
               g_testGradientApplyCalls == 4,
           "malformed preset id is rejected before reaching the desktop owner");

    copyText(gradientRequest.presetId, "gradient_violet",
             sizeof(gradientRequest.presetId));
    gradientRequest.version = kApplyBuiltInGradientPresetVersion + 1u;
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::UnsupportedVersion &&
               g_testGradientApplyCalls == 4,
           "unsupported mutation request version is rejected without desktop access");
    gradientRequest.version = kApplyBuiltInGradientPresetVersion;
    gradientRequest.structureSize = sizeof(gradientRequest) - 1u;
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::InvalidArgument &&
               g_testGradientApplyCalls == 4,
           "invalid mutation request size is rejected without desktop access");
    gradientRequest.structureSize = sizeof(gradientRequest);

    g_testGradientApplyStatus =
        kernel::desktop::DesktopGradientApplyStatus::SaveFailedRolledBack;
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::SaveFailedRolledBack &&
               (gradientResult.flags & kGradientOperationRollbackVerified) != 0u &&
               std::string(gradientResult.appliedId) == "gradient_ocean" &&
               std::string(gradientResult.persistedId) == "gradient_ocean",
           "provider reports a verified save rollback and actual restored values");
    g_testGradientApplyStatus =
        kernel::desktop::DesktopGradientApplyStatus::RollbackFailed;
    copyText(g_testGradientPersistedId, "gradient_violet",
             sizeof(g_testGradientPersistedId));
    expect(applyBuiltInGradientPreset(settingsApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::RollbackFailed &&
               (gradientResult.flags & kGradientOperationRollbackVerified) == 0u &&
               std::string(gradientResult.appliedId) == "gradient_ocean" &&
               std::string(gradientResult.persistedId) == "gradient_violet",
           "rollback failure remains distinct and reports differing actual states");
    g_testGradientApplyStatus =
        kernel::desktop::DesktopGradientApplyStatus::Committed;
    copyText(g_testGradientPersistedId, "gradient_ocean",
             sizeof(g_testGradientPersistedId));

    DisplaySnapshotV1 displaySnapshot{};
    const uint32_t paddedPitch = 5512u;
    expect(buildDisplaySnapshot(true, 1377u, 779u, paddedPitch, 32u,
                                static_cast<uint32_t>(framebuffer::PixelFormat::B8G8R8A8),
                                1u, &displaySnapshot) == SnapshotStatus::Ok &&
               validDisplaySnapshot(displaySnapshot),
           "valid framebuffer geometry creates a valid Display snapshot");
    expect(displaySnapshot.status == DisplayInformationStatus::Available &&
               displaySnapshot.framebufferWidth == 1377u &&
               displaySnapshot.framebufferHeight == 779u &&
               displaySnapshot.bitsPerPixel == 32u &&
               displaySnapshot.bytesPerScanline == paddedPitch &&
               displaySnapshot.pixelFormat == static_cast<uint32_t>(framebuffer::PixelFormat::B8G8R8A8),
           "Display snapshot preserves active dimensions, depth, exact format, and padded pitch");
    expect((displaySnapshot.flags & (kDisplayFramebufferWidthAvailable |
               kDisplayFramebufferHeightAvailable | kDisplayPixelFormatAvailable |
               kDisplayBitsPerPixelAvailable | kDisplayPitchAvailable)) ==
               (kDisplayFramebufferWidthAvailable | kDisplayFramebufferHeightAvailable |
                kDisplayPixelFormatAvailable | kDisplayBitsPerPixelAvailable | kDisplayPitchAvailable),
           "Display snapshot flags match every returned field");

    expect(buildDisplaySnapshot(false, 0u, 0u, 0u, 0u, 0u, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               validDisplaySnapshot(displaySnapshot) &&
               displaySnapshot.status == DisplayInformationStatus::Unavailable &&
               displaySnapshot.flags == 0u,
           "missing framebuffer is represented as an explicit unavailable snapshot");
    expect(buildDisplaySnapshot(true, 0u, 779u, paddedPitch, 32u, 1u, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               displaySnapshot.status == DisplayInformationStatus::Unavailable &&
               validDisplaySnapshot(displaySnapshot),
           "zero framebuffer dimensions are rejected without exposing partial values");
    expect(buildDisplaySnapshot(true, 1377u, 779u, 0u, 24u, 0xFFFFFFFFu, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               validDisplaySnapshot(displaySnapshot) &&
               displaySnapshot.pixelFormat == static_cast<uint32_t>(framebuffer::PixelFormat::Unknown) &&
               (displaySnapshot.flags & kDisplayPixelFormatAvailable) != 0u &&
               (displaySnapshot.flags & kDisplayPitchAvailable) == 0u,
           "unknown pixel formats are reported safely and absent pitch stays unavailable");
    expect(buildDisplaySnapshot(true, 1377u, 779u, 5000u, 32u, 1u, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               validDisplaySnapshot(displaySnapshot) &&
               (displaySnapshot.flags & kDisplayPitchAvailable) == 0u,
           "pitch shorter than a scanline is withheld");
    expect(buildDisplaySnapshot(true, 1377u, 779u, 0u, 7u, 1u, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               validDisplaySnapshot(displaySnapshot) &&
               (displaySnapshot.flags & kDisplayBitsPerPixelAvailable) == 0u &&
               (displaySnapshot.flags & kDisplayPitchAvailable) == 0u,
           "unsupported pixel depth is withheld instead of misrepresented");
    expect(buildDisplaySnapshot(true, 1377u, 779u, 0u, 16u, 1u, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               (displaySnapshot.flags & kDisplayBitsPerPixelAvailable) != 0u,
           "supported 16-bit pixel depth is represented");
    expect(buildDisplaySnapshot(true, 1377u, 779u, 0u, 24u, 1u, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               displaySnapshot.bitsPerPixel == 24u,
           "supported 24-bit pixel depth is represented");
    expect(buildDisplaySnapshot(true, 1377u, 779u, 0u, 40u, 1u, 2u,
                                &displaySnapshot) == SnapshotStatus::Ok &&
               (displaySnapshot.flags & kDisplayBitsPerPixelAvailable) == 0u,
           "out-of-range pixel depth is not reported as a supported representation");
    expect(buildDisplaySnapshot(true, 1u, 1u, 1u, 32u, 1u, 0u,
                                &displaySnapshot) == SnapshotStatus::InvalidArgument,
           "zero Display snapshot revision is invalid");

    DisplaySnapshotRequestV1 displayRequest = {
        sizeof(DisplaySnapshotRequestV1), kDisplaySnapshotVersion, 0u
    };
    g_testFramebufferAvailable = true;
    g_testFramebufferWidth = 1377u;
    g_testFramebufferHeight = 779u;
    g_testFramebufferPitch = paddedPitch;
    g_testFramebufferBpp = 32u;
    g_testFramebufferFormat = framebuffer::PixelFormat::B8G8R8A8;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &displaySnapshot,
                               sizeof(displaySnapshot), &snapshotSize, &snapshotRevision) ==
               SnapshotStatus::Ok && validDisplaySnapshot(displaySnapshot) &&
               displaySnapshot.status == DisplayInformationStatus::Available,
           "trusted Settings caller reads active framebuffer metadata");
    expect(snapshotSize == sizeof(displaySnapshot) &&
               snapshotRevision == static_cast<uint64_t>(g_testDisplayConfigurationGeneration) + 1u &&
               displaySnapshot.revision == snapshotRevision,
           "Display provider reports bounded size and active-configuration revision");
    expect(displaySnapshot.logicalDesktopWidth == 1379u &&
               displaySnapshot.logicalDesktopHeight == 781u &&
               displaySnapshot.activeLayoutMode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend) &&
               std::string(displaySnapshot.primaryModeId) == "mode-primary-test",
           "active display mode and logical desktop values come from the trusted kernel service");
    expect((displaySnapshot.flags & (kDisplayLogicalDesktopWidthAvailable |
               kDisplayLogicalDesktopHeightAvailable | kDisplayActiveLayoutModeAvailable |
               kDisplayPrimaryModeIdAvailable)) ==
               (kDisplayLogicalDesktopWidthAvailable | kDisplayLogicalDesktopHeightAvailable |
                kDisplayActiveLayoutModeAvailable | kDisplayPrimaryModeIdAvailable),
           "active-mode flags match their validated values");
    DisplaySnapshotV1 repeatedDisplaySnapshot{};
    size_t repeatedDisplaySize = 0;
    uint64_t repeatedDisplayRevision = 0;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::Ok &&
               repeatedDisplaySize == snapshotSize &&
               repeatedDisplayRevision == snapshotRevision &&
               repeatedDisplaySnapshot.framebufferWidth == displaySnapshot.framebufferWidth &&
               repeatedDisplaySnapshot.bytesPerScanline == displaySnapshot.bytesPerScanline,
           "repeated Display snapshot refresh is stable while framebuffer state is unchanged");
    ++g_testDisplayConfigurationGeneration;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::Ok &&
               repeatedDisplayRevision == snapshotRevision + 1u,
           "active display configuration generation advances the snapshot revision");
    snapshotRevision = repeatedDisplayRevision;
    displayRequest.knownRevision = snapshotRevision + 1u;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::StaleRevision,
           "Display provider rejects stale requested revisions");
    displayRequest.knownRevision = 0u;
    displayRequest.version = kDisplaySnapshotVersion + 1u;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::UnsupportedVersion,
           "unsupported Display snapshot versions are rejected");
    displayRequest.version = kDisplaySnapshotVersion;
    displayRequest.structureSize = sizeof(displayRequest) - 1u;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::InvalidArgument,
           "undersized Display snapshot request is rejected");
    displayRequest.structureSize = sizeof(displayRequest);
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot) - 1u, &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::BufferTooSmall,
           "undersized Display output buffer is rejected");
    expect(readDisplaySnapshot(settingsApp, &displayRequest, nullptr,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::InvalidArgument,
           "null Display output buffer is rejected");
    expect(readDisplaySnapshot(settingsApp, nullptr, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::InvalidArgument,
           "null Display snapshot request is rejected");
    g_testActiveDisplayConfigurationAvailable = false;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::Ok &&
               validDisplaySnapshot(repeatedDisplaySnapshot) &&
               repeatedDisplaySnapshot.status == DisplayInformationStatus::Available &&
               (repeatedDisplaySnapshot.flags & kDisplayLogicalDesktopWidthAvailable) == 0u,
           "unavailable display-mode service leaves valid framebuffer fields intact");
    g_testFramebufferAvailable = false;
    expect(readDisplaySnapshot(settingsApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::Ok &&
               validDisplaySnapshot(repeatedDisplaySnapshot) &&
               repeatedDisplaySnapshot.status == DisplayInformationStatus::Unavailable,
           "provider returns unavailable when both framebuffer and active mode are unavailable");
    g_testFramebufferAvailable = true;
    g_testActiveDisplayConfigurationAvailable = true;
    g_testDisplayConfigurationGeneration = 8u;
    g_testDisplayConfigurationServiceAvailable = true;

    SettingsRefreshProbeApp* refreshApp = settingsRefreshApp;
    expect(refreshApp && refreshApp->refreshState().hasSnapshot() &&
               refreshApp->refreshState().revision() == 9u,
           "the native refresh state starts from the same authorized provider revision");
    expect(refreshApp && refreshApp->presentation().setFocusedControl(settings::FocusControl::SystemAbout),
           "refresh fixture starts with a stable System focus target");

    const int readsBeforeDuplicate = g_testAdapterDisplayReads;
    const int repaintsBeforeDuplicate = g_testRefreshRepaints;
    AppManager::notifyDisplayConfigurationChanged(8u);
    AppManager::notifyDisplayConfigurationChanged(8u);
    AppManager::update();
    expect(g_testAdapterDisplayReads == readsBeforeDuplicate + 1 &&
               g_testRefreshRepaints == repaintsBeforeDuplicate,
           "duplicate notifications coalesce to one read and unchanged data does not repaint");
    expect(refreshApp && refreshApp->presentation().selectedCategory() == settings::CategoryId::System &&
               refreshApp->presentation().focusedControl() == settings::FocusControl::SystemAbout,
           "a duplicate refresh preserves shared navigation and focus");

    g_testDisplayConfigurationGeneration = 9u;
    g_testFramebufferWidth = 1280u;
    g_testFramebufferHeight = 800u;
    g_testFramebufferPitch = 5120u;
    g_testLogicalDesktopWidth = 1280u;
    g_testLogicalDesktopHeight = 800u;
    copyText(g_testPrimaryModeId, "mode-primary-9", sizeof(g_testPrimaryModeId));
    const int hiddenPageRepaints = g_testRefreshRepaints;
    AppManager::notifyDisplayConfigurationChanged(9u);
    AppManager::update();
    expect(refreshApp && refreshApp->refreshState().hasSnapshot() &&
               refreshApp->refreshState().snapshot().framebufferWidth == 1280u &&
               refreshApp->refreshState().snapshot().bytesPerScanline == 5120u &&
               refreshApp->refreshState().snapshot().logicalDesktopWidth == 1280u &&
               refreshApp->refreshState().revision() == 10u,
           "one legitimate source change commits a consistent provider snapshot");
    expect(refreshApp && refreshApp->presentation().selectedCategory() == settings::CategoryId::System &&
               refreshApp->presentation().focusedControl() == settings::FocusControl::SystemAbout &&
               g_testRefreshRepaints == hiddenPageRepaints,
           "a refresh on System updates cached Display data without repainting the visible page");
    expect(refreshApp && refreshApp->selectCategory(settings::CategoryId::Display) &&
               refreshApp->refreshState().snapshot().framebufferWidth == 1280u,
           "returning to Display presents the current cached provider snapshot");

    g_testDisplayConfigurationGeneration = 10u;
    g_testFramebufferWidth = 1281u;
    g_testFramebufferPitch = 5124u;
    g_testLogicalDesktopWidth = 1281u;
    copyText(g_testPrimaryModeId, "mode-primary-10", sizeof(g_testPrimaryModeId));
    const int visibleRepaintsBeforeChange = g_testRefreshRepaints;
    AppManager::notifyDisplayConfigurationChanged(10u);
    AppManager::update();
    expect(g_testRefreshRepaints == visibleRepaintsBeforeChange + 1 &&
               refreshApp && refreshApp->refreshState().snapshot().framebufferWidth == 1281u,
           "a changed snapshot on the focused Display page requests one repaint");
    const int readsBeforeRapidDuplicates = g_testAdapterDisplayReads;
    AppManager::notifyDisplayConfigurationChanged(10u);
    AppManager::notifyDisplayConfigurationChanged(10u);
    AppManager::update();
    expect(g_testAdapterDisplayReads == readsBeforeRapidDuplicates + 1 &&
               g_testRefreshRepaints == visibleRepaintsBeforeChange + 1,
           "a harmless duplicate source event keeps current values and avoids a second repaint");

    expect(refreshApp && refreshApp->selectCategory(settings::CategoryId::About),
           "refresh fixture can move to About through the shared presentation core");
    g_testDisplayConfigurationGeneration = 11u;
    g_testFramebufferWidth = 1282u;
    g_testFramebufferPitch = 5128u;
    g_testLogicalDesktopWidth = 1282u;
    const int aboutRepaints = g_testRefreshRepaints;
    AppManager::notifyDisplayConfigurationChanged(11u);
    AppManager::update();
    expect(refreshApp && refreshApp->refreshState().snapshot().framebufferWidth == 1282u &&
               refreshApp->presentation().selectedCategory() == settings::CategoryId::About &&
               g_testRefreshRepaints == aboutRepaints,
           "notifications during About update cached data without repainting or navigating away");

    expect(refreshApp && refreshApp->selectCategory(settings::CategoryId::Display),
           "Display can be selected again after a hidden refresh");
    refreshApp->onWindowBlur();
    g_testDisplayConfigurationGeneration = 12u;
    g_testFramebufferWidth = 1283u;
    g_testFramebufferPitch = 5132u;
    g_testLogicalDesktopWidth = 1283u;
    const int unfocusedRepaints = g_testRefreshRepaints;
    AppManager::notifyDisplayConfigurationChanged(12u);
    AppManager::update();
    expect(refreshApp && refreshApp->refreshState().snapshot().framebufferWidth == 1283u &&
               g_testRefreshRepaints == unfocusedRepaints,
           "a refresh during focus loss is cached and does not redraw an unfocused window");
    refreshApp->onWindowFocus();
    expect(refreshApp && refreshApp->refreshState().snapshot().framebufferWidth == 1283u,
           "focus restoration reveals the latest accepted Display snapshot");

    const int rapidReadStart = g_testAdapterDisplayReads;
    const int rapidRepaintStart = g_testRefreshRepaints;
    g_testDisplayConfigurationGeneration = 13u;
    g_testFramebufferWidth = 1284u;
    g_testFramebufferPitch = 5136u;
    g_testLogicalDesktopWidth = 1284u;
    AppManager::notifyDisplayConfigurationChanged(13u);
    g_testDisplayConfigurationGeneration = 14u;
    g_testFramebufferWidth = 1285u;
    g_testFramebufferPitch = 5140u;
    g_testLogicalDesktopWidth = 1285u;
    copyText(g_testPrimaryModeId, "mode-primary-14", sizeof(g_testPrimaryModeId));
    AppManager::notifyDisplayConfigurationChanged(14u);
    AppManager::notifyDisplayConfigurationChanged(13u);
    AppManager::update();
    expect(g_testAdapterDisplayReads == rapidReadStart + 1 &&
               refreshApp && refreshApp->refreshState().revision() == 15u &&
               refreshApp->refreshState().snapshot().framebufferWidth == 1285u &&
               g_testRefreshRepaints == rapidRepaintStart + 1,
           "rapid state changes coalesce to one read of the newest generation");

    DisplaySnapshotV1 duplicateRevision = refreshApp->refreshState().snapshot();
    expect(refreshApp->refreshState().accept(duplicateRevision) ==
               DisplaySnapshotRefreshResult::NoChange,
           "duplicate snapshot revision with identical values is harmless");
    duplicateRevision.logicalDesktopWidth++;
    expect(refreshApp->refreshState().accept(duplicateRevision) ==
               DisplaySnapshotRefreshResult::Rejected &&
               refreshApp->refreshState().snapshot().framebufferWidth == 1285u,
           "inconsistent values at the same source revision are rejected");
    DisplaySnapshotV1 staleRevision = refreshApp->refreshState().snapshot();
    staleRevision.revision--;
    expect(refreshApp->refreshState().accept(staleRevision) ==
               DisplaySnapshotRefreshResult::Rejected &&
               refreshApp->refreshState().revision() == 15u,
           "an older snapshot revision cannot replace current Display state");

    const int providerRefreshStart = g_testAdapterDisplayReads;
    const int providerRepaintStart = g_testRefreshRepaints;
    for (uint32_t cycle = 0u; cycle < 250u; ++cycle) {
        ++g_testDisplayConfigurationGeneration;
        if (g_testDisplayConfigurationGeneration == 0u) ++g_testDisplayConfigurationGeneration;
        const uint32_t width = 1300u + (cycle % 450u);
        const uint32_t height = 700u + (cycle % 100u);
        g_testFramebufferWidth = width;
        g_testFramebufferHeight = height;
        g_testFramebufferPitch = width * 4u;
        g_testLogicalDesktopWidth = width + 2u;
        g_testLogicalDesktopHeight = height + 1u;
        const std::string mode = "mode-cycle-" + std::to_string(cycle);
        copyText(g_testPrimaryModeId, mode.c_str(), sizeof(g_testPrimaryModeId));
        AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
        AppManager::update();
    }
    expect(g_testAdapterDisplayReads == providerRefreshStart + 250 &&
               g_testRefreshRepaints == providerRepaintStart + 250 &&
               refreshApp && refreshApp->refreshState().revision() ==
                   static_cast<uint64_t>(g_testDisplayConfigurationGeneration) + 1u &&
               refreshApp->refreshState().snapshot().bytesPerScanline ==
                   refreshApp->refreshState().snapshot().framebufferWidth * 4u,
           "250 controlled provider refresh cycles preserve generation-consistent geometry");

    expect(refreshApp && refreshApp->selectCategory(settings::CategoryId::About),
           "1000-operation reliability fixture selects a hidden category");
    const int transitionReadStart = g_testAdapterDisplayReads;
    const int transitionRepaintStart = g_testRefreshRepaints;
    for (uint32_t operation = 0u; operation < 1000u; ++operation) {
        ++g_testDisplayConfigurationGeneration;
        if (g_testDisplayConfigurationGeneration == 0u) ++g_testDisplayConfigurationGeneration;
        const uint32_t width = 1400u + (operation % 300u);
        const uint32_t height = 720u + (operation % 80u);
        g_testFramebufferWidth = width;
        g_testFramebufferHeight = height;
        g_testFramebufferPitch = width * 4u;
        g_testLogicalDesktopWidth = width;
        g_testLogicalDesktopHeight = height;
        AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
        AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
        AppManager::update();
    }
    expect(g_testAdapterDisplayReads == transitionReadStart + 1000 &&
               g_testRefreshRepaints == transitionRepaintStart &&
               refreshApp && refreshApp->refreshState().snapshot().framebufferWidth ==
                   g_testFramebufferWidth &&
               refreshApp->presentation().selectedCategory() == settings::CategoryId::About,
           "1000 deterministic transitions coalesce duplicates and preserve hidden-page state");

    expect(refreshApp && refreshApp->selectCategory(settings::CategoryId::Display),
           "Display remains reachable after reliability refresh cycles");
    g_testDisplayConfigurationGeneration++;
    g_testFramebufferWidth++;
    g_testFramebufferPitch = g_testFramebufferWidth * 4u;
    g_testLogicalDesktopWidth = g_testFramebufferWidth;
    g_testCorruptAdapterSnapshotVersion = true;
    AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
    AppManager::update();
    g_testCorruptAdapterSnapshotVersion = false;
    expect(refreshApp && !refreshApp->refreshState().hasSnapshot() &&
               refreshApp->presentation().capability(settings::CapabilityId::Display)->availability ==
                   settings::CapabilityAvailability::Unavailable,
           "an invalid provider version clears displayed data and marks Display unavailable");

    g_testDisplayConfigurationGeneration++;
    g_testFramebufferWidth++;
    g_testFramebufferPitch = g_testFramebufferWidth * 4u;
    g_testLogicalDesktopWidth = g_testFramebufferWidth;
    g_testTruncateAdapterSnapshot = true;
    AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
    AppManager::update();
    g_testTruncateAdapterSnapshot = false;
    expect(refreshApp && !refreshApp->refreshState().hasSnapshot(),
           "an undersized adapter snapshot is rejected without exposing partial fields");

    g_testDisplayConfigurationServiceAvailable = false;
    AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
    AppManager::update();
    expect(refreshApp && !refreshApp->refreshState().hasSnapshot(),
           "a temporarily busy provider does not leave old values labeled as current");
    g_testDisplayConfigurationServiceAvailable = true;
    AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
    AppManager::update();
    expect(refreshApp && refreshApp->refreshState().hasSnapshot() &&
               refreshApp->refreshState().snapshot().framebufferWidth == g_testFramebufferWidth,
           "the Display provider recovers after an unavailable response");

    DisplaySnapshotV1 nearWrap{};
    DisplaySnapshotV1 afterWrap{};
    expect(buildDisplaySnapshot(true, 1280u, 800u, 5120u, 32u, 1u,
                                0x100000000ull, &nearWrap) == SnapshotStatus::Ok &&
               buildDisplaySnapshot(true, 1280u, 800u, 5120u, 32u, 1u,
                                2u, &afterWrap) == SnapshotStatus::Ok,
           "display wraparound fixtures build with source-generation revisions");
    DisplaySnapshotRefreshState wrapState;
    expect(wrapState.accept(nearWrap) == DisplaySnapshotRefreshResult::Updated &&
               wrapState.accept(afterWrap) == DisplaySnapshotRefreshResult::NoChange &&
               wrapState.revision() == 2u,
           "snapshot ordering accepts the documented nonzero generation wrap");
    settings::SettingsPresentationCore wrapPresentation;
    settings::CapabilitySnapshot nearWrapCapability = {
        settings::CapabilityId::Display, settings::CapabilityAvailability::ReadOnly,
        0x100000000ull
    };
    settings::CapabilitySnapshot afterWrapCapability = nearWrapCapability;
    afterWrapCapability.revision = 2u;
    expect(wrapPresentation.applyCapabilitySnapshot(nearWrapCapability) &&
               wrapPresentation.applyCapabilitySnapshot(afterWrapCapability),
           "the shared presentation core accepts Display generation wraparound");

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
    expect(presentation.selectCategory(settings::CategoryId::Display) &&
               presentation.selectedCategory() == settings::CategoryId::Display,
           "shared core navigates from System/About to Display");
    expect(presentation.selectCategory(settings::CategoryId::About) &&
               presentation.selectedCategory() == settings::CategoryId::About,
           "shared core navigates from Display to About");
    expect(presentation.selectCategory(settings::CategoryId::Display) &&
               presentation.selectedCategory() == settings::CategoryId::Display,
           "shared core navigates back from About to Display");
    settings::CapabilitySnapshot displayCapability = {
        settings::CapabilityId::Display,
        settings::CapabilityAvailability::ReadOnly,
        kDisplaySnapshotRevision
    };
    expect(presentation.applyCapabilitySnapshot(displayCapability) &&
               presentation.capability(settings::CapabilityId::Display)->availability ==
                   settings::CapabilityAvailability::ReadOnly,
           "shared core records Display as an authoritative read-only capability");
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
    expect(g_testDisplayMutationSubmissions == 0,
           "Display provider submits no configuration mutations");

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
    expect(readDisplaySnapshot(otherApp, &displayRequest, &repeatedDisplaySnapshot,
                               sizeof(repeatedDisplaySnapshot), &repeatedDisplaySize,
                               &repeatedDisplayRevision) == SnapshotStatus::Unauthorized,
           "Display provider denies a non-Settings caller despite identity-like launch data");
    appearanceRequest.knownRevision = 0u;
    expect(readDesktopAppearanceSnapshot(otherApp, &appearanceRequest,
               &readAppearance, sizeof(readAppearance), &snapshotSize,
               &snapshotRevision) == SnapshotStatus::Unauthorized,
           "appearance provider denies a non-Settings caller despite identity-like launch data");
    expect(readBuiltInGradientPresets(otherApp, gradientPresets,
                                      kBuiltInGradientPresetCapacity,
                                      &gradientPresetCount) == SnapshotStatus::Unauthorized,
           "preset list provider denies a non-Settings runtime identity");
    const int applyCallsBeforeSpoofedRequest = g_testGradientApplyCalls;
    expect(applyBuiltInGradientPreset(otherApp, &gradientRequest,
                                      &gradientResult) ==
               GradientOperationStatus::Unauthorized &&
               g_testGradientApplyCalls == applyCallsBeforeSpoofedRequest,
           "identity-like launch argument cannot authorize a spoofed gradient request");
    const int appearanceNotificationsBefore = g_runtimeAppearanceChanged;
    AppManager::notifyDesktopAppearanceChanged(9u);
    expect(g_runtimeAppearanceChanged == appearanceNotificationsBefore + 2,
           "appearance commits notify each running app through the bounded AppManager path");
    if (otherApp) otherApp->requestClose();
    AppManager::update();
    expect(AppManager::getRunningAppCount() == 1 && g_activeWindows == 1,
           "closing the unrelated app releases its window ownership");
    const int appearanceNotificationsAfterClose = g_runtimeAppearanceChanged;
    AppManager::notifyDesktopAppearanceChanged(10u);
    expect(g_runtimeAppearanceChanged == appearanceNotificationsAfterClose + 1,
           "appearance notification skips an app after its instance has closed");
    expect(AppManager::getRunningApp(0) == settingsApp &&
               std::string(settingsApp->getRuntimeAppId()) == kSettingsRuntimeAppId &&
               settingsApp->getWindow() && settingsApp->getWindow()->owner == settingsApp,
           "closing an unrelated app leaves the Settings identity and window ownership intact");

    KernelApp* settingsBeforeCycles = AppManager::getRunningApp(0);
    ++g_testDisplayConfigurationGeneration;
    if (g_testDisplayConfigurationGeneration == 0u) ++g_testDisplayConfigurationGeneration;
    AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
    if (settingsBeforeCycles) settingsBeforeCycles->requestClose();
    ++g_testDisplayConfigurationGeneration;
    if (g_testDisplayConfigurationGeneration == 0u) ++g_testDisplayConfigurationGeneration;
    AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration);
    const int readsBeforeLateNotification = g_testAdapterDisplayReads;
    AppManager::update();
    expect(AppManager::getRunningAppCount() == 0 && g_activeWindows == 0,
           "initial Settings instance is fully closed before lifecycle repetitions");
    AppManager::notifyDisplayConfigurationChanged(g_testDisplayConfigurationGeneration + 1u);
    AppManager::update();
    expect(g_testAdapterDisplayReads == readsBeforeLateNotification,
           "pending, during-close, and post-destruction notifications cannot call a stale Settings instance");

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
        DisplaySnapshotV1 lifecycleDisplay{};
        size_t lifecycleDisplaySize = 0;
        uint64_t lifecycleDisplayRevision = 0;
        expect(readDisplaySnapshot(current, &displayRequest, &lifecycleDisplay,
                                   sizeof(lifecycleDisplay), &lifecycleDisplaySize,
                                   &lifecycleDisplayRevision) == SnapshotStatus::Ok &&
                   validDisplaySnapshot(lifecycleDisplay) &&
                   lifecycleDisplay.status == DisplayInformationStatus::Available,
               "fresh lifecycle can read its Display snapshot");
        DisplaySnapshotV1 lifecycleDisplayRefresh{};
        expect(readDisplaySnapshot(current, &displayRequest, &lifecycleDisplayRefresh,
                                   sizeof(lifecycleDisplayRefresh), &lifecycleDisplaySize,
                                   &lifecycleDisplayRevision) == SnapshotStatus::Ok &&
                   lifecycleDisplayRefresh.revision == lifecycleDisplay.revision,
               "repeated Display reads do not retain stale app-owned provider state");
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

    testGradientConfigPersistence();

    std::cout << "Settings native runtime tests: " << (g_checks - g_failures)
              << "/" << g_checks << " passed; lifecycle repetitions=100"
              << "; provider refresh cycles=250; notification/state transitions=1000\n";
    return g_failures == 0 ? 0 : 1;
}
