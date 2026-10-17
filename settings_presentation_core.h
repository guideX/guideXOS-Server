#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gxos {
namespace apps {
namespace settings {

enum class CategoryId : unsigned char {
    System = 0,
    Display = 1,
    Network = 2,
    Personalization = 3,
    Devices = 4,
    Storage = 5,
    Apps = 6,
    Users = 7,
    DateTime = 8,
    Accessibility = 9,
    Developer = 10,
    About = 11,
    Count = 12
};

enum class TargetId : unsigned char {
    Page = 0,
    SystemDevice,
    Resolution,
    DisplayMode,
    IPv4,
    DNS,
    Gateway,
    StorageDisks,
    StorageVolumes,
    DevicesInput,
    DevicesNetwork,
    DevicesDisplay,
    DevicesStorage,
    DevicesAudio,
    DevicesUsb,
    DevicesOther,
    DeviceDetail,
    StorageDiskDetail,
    AboutVersion,
    PersonalizationBackground,
    PersonalizationFonts,
    AppsList,
    AppsDefault,
    AppDetail,
    DateTimeTime,
    DateTimeTimeZone,
    AccessibilityFocus,
    AccessibilityKeyboard,
    DeveloperApps,
    DeveloperServices,
    DeveloperDiagnostics,
    UsersSession,
    UsersSecurity,
};

enum : size_t {
    kCategoryCount = 12,
    kPageElementCount = 42,
    kSearchResultCapacity = 6,
    kSearchQueryMaxLength = 40,
    kSearchQueryBufferSize = kSearchQueryMaxLength + 1,
    kSettingsRouteMaxLength = 128,
    kOperationPayloadCapacity = 64
};

struct CategoryInfo {
    CategoryId id;
    const char* slug;
    const char* label;
    const char* description;
};

template <typename T, size_t Count>
struct FixedTable {
    T values[Count];

    constexpr size_t size() const { return Count; }
    constexpr const T& operator[](size_t index) const { return values[index]; }
    const T* begin() const { return values; }
    const T* end() const { return values + Count; }
};

extern const FixedTable<CategoryInfo, kCategoryCount> kCategories;

struct SettingsRoute {
    CategoryId category;
    TargetId target;
};

bool operator==(const SettingsRoute& left, const SettingsRoute& right);
bool validCategory(CategoryId id);
const CategoryInfo* categoryInfo(CategoryId id);
bool validRoute(const SettingsRoute& route);
bool parseSettingsRoute(const char* uri, SettingsRoute& route);

enum class PageElementKind : unsigned char {
    Page,
    Setting,
    Action
};

struct PageElementDescriptor {
    const char* label;
    const char* keywords;
    SettingsRoute route;
    PageElementKind kind;
};

using SearchEntry = PageElementDescriptor;
extern const FixedTable<PageElementDescriptor, kPageElementCount> kPageElements;

struct SearchResult {
    const char* label;
    SettingsRoute route;
};

struct SearchResultSet {
    enum : size_t { kCapacity = kSearchResultCapacity };
    SearchResult values[kCapacity];
    size_t count;
    bool accepted;
};

SearchResultSet searchSettings(const char* query);

enum class FocusControl : unsigned char {
    None,
    SystemDisplay,
    SystemNetwork,
    SystemStorage,
    SystemAbout,
    SystemDevices,
    NetworkAdapter,
    NetworkAdvanced,
    DisplayResolution,
    DisplayMode,
    DisplayApply,
    DisplayCancel,
    DisplayAdvanced,
    PersonalizationAdvanced,
    PersonalizationChooseBackground,
    DateTimeAdvanced,
    StorageDiskManager,
    DeviceEntry,
    DeviceDetailBack,
    DeviceDetailNetwork,
    DeviceDetailDisplay,
    DeviceDetailStorage,
    StorageDiskEntry,
    StorageVolumeEntry,
    StorageDetailBack,
    SystemControlPanel,
    DeveloperConsole,
    DeveloperStudio,
    DeveloperApps,
    DeveloperServices,
    DeveloperDiagnostics,
    DeveloperRefresh,
    DeveloperBack,
    AccessibilityKeyboard,
    AccessibilityEnhancedFocus,
    AppEntry,
    AppsInstalledTab,
    AppsDefaultTab,
    DefaultAppEntry,
    DefaultAppHandler,
    DefaultAppRestore,
    DefaultAppCancel,
    AppsDetailBack,
    AppsOpen,
    UsersSessionDetails,
    UsersSecurityDetails,
    UsersDeveloperServices,
    UsersBack
};

CategoryId focusControlCategory(FocusControl control);

enum class CapabilityId : unsigned char {
    SystemInformation,
    Display,
    Network,
    Personalization,
    Devices,
    Storage,
    Apps,
    Users,
    DateTime,
    Accessibility,
    Developer,
    Count
};

enum : size_t {
    kCapabilityCount = static_cast<size_t>(CapabilityId::Count)
};

enum class CapabilityAvailability : unsigned char {
    Unknown,
    Writable,
    ReadOnly,
    Unsupported,
    Unavailable
};

enum class PresentationStatus : unsigned char {
    None,
    Pending,
    Success,
    Failure
};

enum class OperationCompletion : unsigned char {
    Success,
    Failure
};

struct CapabilitySnapshot {
    CapabilityId id;
    CapabilityAvailability availability;
    uint64_t revision;
};

struct CapabilityPresentation {
    CapabilityAvailability availability;
    PresentationStatus status;
    uint64_t revision;
    uint32_t activeRequestId;
};

struct OperationRequest {
    uint32_t requestId;
    CapabilityId capability;
    uint16_t operationCode;
    uint64_t baseRevision;
    size_t payloadLength;
    uint8_t payload[kOperationPayloadCapacity];
};

struct OperationResult {
    uint32_t requestId;
    OperationCompletion completion;
};

class SettingsPresentationCore {
public:
    SettingsPresentationCore();

    void reset();
    CategoryId selectedCategory() const;
    SettingsRoute route() const;
    bool selectCategory(CategoryId category);
    bool nextCategory();
    bool previousCategory();
    bool navigate(const SettingsRoute& route);
    bool canFocus(FocusControl control) const;
    bool setFocusedControl(FocusControl control);
    FocusControl focusedControl() const;

    bool setSearchQuery(const char* query);
    bool appendSearchCharacter(char character);
    bool backspaceSearch();
    void clearSearch();
    const char* searchQuery() const;
    size_t searchQueryLength() const;
    bool hasSearchQuery() const;
    SearchResultSet searchResults() const;

    bool applyCapabilitySnapshot(const CapabilitySnapshot& snapshot);
    const CapabilityPresentation* capability(CapabilityId id) const;
    bool beginOperation(CapabilityId id, uint16_t operationCode,
                        OperationRequest& request,
                        const uint8_t* payload = nullptr,
                        size_t payloadLength = 0);
    bool completeOperation(const OperationResult& result);

private:
    SettingsRoute m_route;
    FocusControl m_focusedControl;
    char m_searchQuery[kSearchQueryBufferSize];
    size_t m_searchQueryLength;
    CapabilityPresentation m_capabilities[kCapabilityCount];
    uint32_t m_nextRequestId;
};

} // namespace settings
} // namespace apps
} // namespace gxos
