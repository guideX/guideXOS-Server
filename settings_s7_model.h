#pragma once

#include "settings_s6_model.h"
#include "settings_center_model.h"
#include "system_service_protocol.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace gxos {
namespace apps {
namespace settings {

constexpr const char* kDeveloperStudioAppId = "com.guidexos.developerstudio";
constexpr const char* kConsoleAppId = "gxos.builtin.console";

struct AccessibilityPreferences {
    bool enhancedFocusIndicator{false};
};

inline bool parsePersistedBoolean(const std::string& value, bool fallback = false)
{
    std::string normalized = lowerAscii(trimAscii(value));
    if (normalized == "true" || normalized == "1" || normalized == "on") return true;
    if (normalized == "false" || normalized == "0" || normalized == "off") return false;
    return fallback;
}

enum class AccessibilityMutationResult : uint8_t {
    Applied,
    ReadFailed,
    PersistFailed,
    ReadBackFailed,
    ReadBackMismatch,
    ApplyFailed
};

// Store must provide read(bool&) and write(bool). Apply follows the owner write
// and precedes the final authoritative reread, so the displayed value reflects
// both production application and persisted state.
template <typename Store, typename Apply>
inline AccessibilityMutationResult updateEnhancedFocusPreference(
    Store& store, bool requested, Apply apply, bool& authoritativeValue)
{
    bool before = false;
    if (!store.read(before)) return AccessibilityMutationResult::ReadFailed;
    const auto restoreBefore = [&]() {
        bool restored = before;
        if (store.write(before) && store.read(restored)) authoritativeValue = restored;
        else authoritativeValue = before;
        apply(authoritativeValue);
    };
    if (!store.write(requested)) {
        authoritativeValue = before;
        return AccessibilityMutationResult::PersistFailed;
    }
    if (!apply(requested)) {
        restoreBefore();
        return AccessibilityMutationResult::ApplyFailed;
    }
    bool reread = false;
    if (!store.read(reread)) {
        restoreBefore();
        return AccessibilityMutationResult::ReadBackFailed;
    }
    authoritativeValue = reread;
    if (reread != requested) {
        restoreBefore();
        return AccessibilityMutationResult::ReadBackMismatch;
    }
    return AccessibilityMutationResult::Applied;
}

struct DeveloperToolStatus {
    bool registered{false};
    bool openSupported{false};
    std::string displayName;
};

struct DeveloperAppModelSnapshot {
    size_t registeredCount{0};
    size_t registryTotalCount{0};
    size_t nativeElfCount{0};
    bool truncated{false};
    DeveloperToolStatus developerStudio{};
    DeveloperToolStatus console{};
};

inline DeveloperToolStatus findDeveloperTool(const AppInventory& inventory,
                                              const char* appId)
{
    DeveloperToolStatus result;
    if (!appId) return result;
    for (size_t i = 0; i < inventory.count; ++i) {
        const AppInventoryEntry& entry = inventory.entries[i];
        if (entry.appId != appId) continue;
        result.registered = true;
        result.openSupported = entry.openSupported;
        result.displayName = entry.displayName;
        return result;
    }
    return result;
}

inline DeveloperAppModelSnapshot buildDeveloperAppModelSnapshot(
    const AppInventory& inventory)
{
    DeveloperAppModelSnapshot snapshot;
    snapshot.registeredCount = inventory.count;
    snapshot.registryTotalCount = inventory.totalCount;
    snapshot.truncated = inventory.truncated;
    for (size_t i = 0; i < inventory.count; ++i) {
        if (inventory.entries[i].kind == "NativeElf") ++snapshot.nativeElfCount;
    }
    snapshot.developerStudio = findDeveloperTool(inventory, kDeveloperStudioAppId);
    snapshot.console = findDeveloperTool(inventory, kConsoleAppId);
    return snapshot;
}

inline std::string diagnosticFieldValue(const std::string& diagnostic,
                                       const std::string& field,
                                       size_t maxCharacters = 64)
{
    if (field.empty()) return std::string();
    size_t offset = 0;
    while (offset < diagnostic.size()) {
        while (offset < diagnostic.size() &&
               std::isspace(static_cast<unsigned char>(diagnostic[offset]))) ++offset;
        const size_t tokenStart = offset;
        while (offset < diagnostic.size() &&
               !std::isspace(static_cast<unsigned char>(diagnostic[offset]))) ++offset;
        const size_t equals = diagnostic.find('=', tokenStart);
        if (equals == std::string::npos || equals >= offset) continue;
        if (diagnostic.compare(tokenStart, equals - tokenStart, field) != 0) continue;
        const size_t valueStart = equals + 1;
        return diagnostic.substr(valueStart, std::min(maxCharacters, offset - valueStart));
    }
    return std::string();
}

inline bool parseDiagnosticCount(const std::string& diagnostic,
                                 const std::string& field,
                                 uint64_t& output)
{
    output = 0;
    const std::string value = diagnosticFieldValue(diagnostic, field, 24);
    if (value.empty()) return false;
    uint64_t parsed = 0;
    for (unsigned char ch : value) {
        if (ch < '0' || ch > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
    }
    output = parsed;
    return true;
}

enum class ServiceAvailability : uint8_t { Unavailable, Available };

inline ServiceAvailability serviceAvailability(system_service::ClientResult result)
{
    return result == system_service::ClientResult::Ok
        ? ServiceAvailability::Available : ServiceAvailability::Unavailable;
}

inline bool anyServiceAvailable(ServiceAvailability network,
                                ServiceAvailability devices,
                                ServiceAvailability storage)
{
    return network == ServiceAvailability::Available ||
        devices == ServiceAvailability::Available ||
        storage == ServiceAvailability::Available;
}

inline bool developerToolActionEnabled(const DeveloperToolStatus& status)
{
    return status.registered && status.openSupported;
}

inline int maximumSettingsPageScroll(int rowCount, int rowPitch, int viewportHeight)
{
    if (rowCount <= 0 || rowPitch <= 0 || viewportHeight <= 0) return 0;
    return std::max(0, rowCount * rowPitch - viewportHeight);
}

inline int clampSettingsPageScroll(int requested, int maximum)
{
    return std::max(0, std::min(requested, std::max(0, maximum)));
}

inline size_t nextSettingsFocusIndex(size_t count, int current, bool reverse)
{
    if (count == 0) return 0;
    if (current < 0 || static_cast<size_t>(current) >= count)
        return reverse ? count - 1 : 0;
    const size_t index = static_cast<size_t>(current);
    return reverse ? (index + count - 1) % count : (index + 1) % count;
}

} // namespace settings
} // namespace apps
} // namespace gxos
