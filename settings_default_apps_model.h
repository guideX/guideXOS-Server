#pragma once

#include "app_registry.h"
#include "settings_center_model.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace gxos {
namespace apps {
namespace settings {

constexpr size_t kMaxSettingsDefaultAppExtensions = 32;

inline int settingsDefaultAppsTabsY(int contentY) { return contentY + 68; }
inline int settingsDefaultAppsTabsHeight(bool compact) { return compact ? 26 : 30; }
inline int settingsDefaultAppsListTop(int contentY, bool compact)
{
    return settingsDefaultAppsTabsY(contentY) + settingsDefaultAppsTabsHeight(compact) + 44;
}
inline int settingsDefaultAppsPickerTop(int contentY, bool compact)
{
    return settingsDefaultAppsListTop(contentY, compact) + 8;
}
inline int settingsDefaultAppsPickerRowsTop(int contentY, bool compact)
{
    return settingsDefaultAppsPickerTop(contentY, compact) + 22;
}
inline int settingsDefaultAppsVisibleRows(int top, int bottom, int rowPitch)
{
    return std::max(1, rowPitch > 0 ? (bottom - top) / rowPitch : 1);
}
inline int settingsDefaultAppsMouseRowIndex(int y, int top, int bottom, int rowPitch,
                                           int scroll, size_t rowCount)
{
    if (rowPitch <= 0 || y < top || y >= bottom || scroll < 0) return -1;
    const int visible = settingsDefaultAppsVisibleRows(top, bottom, rowPitch);
    const int ordinal = (y - top) / rowPitch;
    if (ordinal < 0 || ordinal >= visible) return -1;
    const size_t index = static_cast<size_t>(scroll + ordinal);
    return index < rowCount ? static_cast<int>(index) : -1;
}
inline int settingsDefaultAppsScrollToInclude(int scroll, int rowIndex,
                                             size_t rowCount, int visibleRows)
{
    const int maxScroll = std::max(0, static_cast<int>(rowCount) - std::max(1, visibleRows));
    if (rowIndex < 0) return std::max(0, std::min(scroll, maxScroll));
    if (rowIndex < scroll) scroll = rowIndex;
    else if (rowIndex >= scroll + std::max(1, visibleRows))
        scroll = rowIndex - std::max(1, visibleRows) + 1;
    return std::max(0, std::min(scroll, maxScroll));
}
inline int nextDefaultAppsRowIndex(int current, size_t rowCount, int delta)
{
    if (rowCount == 0) return -1;
    return std::max(0, std::min(static_cast<int>(rowCount) - 1, current + delta));
}

enum class DefaultAppsPickerFocusKind { Handler, Restore, Cancel };
struct DefaultAppsPickerFocus {
    DefaultAppsPickerFocusKind kind{DefaultAppsPickerFocusKind::Cancel};
    size_t handlerIndex{0};
};

inline DefaultAppsPickerFocus nextDefaultAppsPickerFocus(DefaultAppsPickerFocus current,
                                                         size_t handlerCount,
                                                         bool hasRestore, int delta)
{
    const size_t sequenceCount = handlerCount + (hasRestore ? 1 : 0) + 1;
    size_t position = 0;
    if (current.kind == DefaultAppsPickerFocusKind::Handler)
        position = std::min(current.handlerIndex, handlerCount == 0 ? size_t(0) : handlerCount - 1);
    else if (current.kind == DefaultAppsPickerFocusKind::Restore && hasRestore)
        position = handlerCount;
    else
        position = sequenceCount - 1;
    const int next = std::max(0, std::min(static_cast<int>(sequenceCount) - 1,
        static_cast<int>(position) + delta));
    if (static_cast<size_t>(next) < handlerCount)
        return { DefaultAppsPickerFocusKind::Handler, static_cast<size_t>(next) };
    if (hasRestore && static_cast<size_t>(next) == handlerCount)
        return { DefaultAppsPickerFocusKind::Restore, 0 };
    return { DefaultAppsPickerFocusKind::Cancel, 0 };
}

class DefaultAppsBackend {
public:
    virtual ~DefaultAppsBackend() = default;
    virtual bool available() const = 0;
    virtual std::vector<std::string> knownDocumentExtensions() = 0;
    virtual DefaultHandlerInfo defaultHandlerInfo(const std::string& extension) = 0;
    virtual DocumentHandlerList capableHandlers(const std::string& extension) = 0;
    virtual std::string displayName(const std::string& canonicalAppId) = 0;
    virtual bool isDurableApp(const std::string& canonicalAppId) = 0;
    virtual DefaultHandlerMutationResult setDefaultHandler(
        const std::string& extension, const std::string& canonicalAppId) = 0;
    virtual DefaultHandlerMutationResult clearDefaultHandler(const std::string& extension) = 0;
};

struct DefaultAppsRow {
    std::string extension;
    DefaultHandlerInfo policy{};
    DocumentHandlerList handlers{};
    std::string builtInDisplayName;
    std::string configuredDisplayName;
    std::string effectiveDisplayName;
};

struct DefaultAppsSnapshot {
    std::array<DefaultAppsRow, kMaxSettingsDefaultAppExtensions> rows{};
    size_t count{0};
    size_t totalCount{0};
    bool truncated{false};
    bool available{false};
};

inline FocusControl initialDefaultAppsFocusControl(const DefaultAppsSnapshot& snapshot)
{
    return snapshot.available && snapshot.count > 0
        ? FocusControl::DefaultAppEntry
        : FocusControl::AppsDefaultTab;
}

inline const DefaultAppsRow* findDefaultAppsRow(const DefaultAppsSnapshot& snapshot,
                                                const std::string& extension)
{
    for (size_t i = 0; i < snapshot.count; ++i)
        if (snapshot.rows[i].extension == extension) return &snapshot.rows[i];
    return nullptr;
}

inline int findDefaultAppsRowIndex(const DefaultAppsSnapshot& snapshot,
                                   const std::string& extension)
{
    for (size_t i = 0; i < snapshot.count; ++i)
        if (snapshot.rows[i].extension == extension) return static_cast<int>(i);
    return -1;
}

inline bool openDefaultAppsPicker(const DefaultAppsSnapshot& snapshot, size_t rowIndex,
                                  std::string& selectedExtension, bool& pickerOpen,
                                  int& pickerScroll)
{
    if (!snapshot.available || rowIndex >= snapshot.count) return false;
    selectedExtension = snapshot.rows[rowIndex].extension;
    pickerOpen = true;
    pickerScroll = 0;
    return true;
}

inline int closeDefaultAppsPicker(const DefaultAppsSnapshot& snapshot,
                                 const std::string& selectedExtension,
                                 bool& pickerOpen, int& pickerScroll)
{
    pickerOpen = false;
    pickerScroll = 0;
    return findDefaultAppsRowIndex(snapshot, selectedExtension);
}

inline bool isEligibleDefaultAppHandler(const DocumentHandlerInfo& handler,
                                       DefaultAppsBackend& backend)
{
    return !handler.appId.empty() && handler.registrationCurrent &&
        handler.supportsDocumentActivation && handler.available &&
        handler.registrationOwner == 0 && handler.registrationGeneration == 0 &&
        backend.isDurableApp(handler.appId);
}

inline std::vector<size_t> eligibleDefaultAppHandlerIndices(const DefaultAppsRow& row,
                                                            DefaultAppsBackend& backend)
{
    std::vector<size_t> indices;
    indices.reserve(row.handlers.count);
    for (size_t i = 0; i < row.handlers.count; ++i)
        if (isEligibleDefaultAppHandler(row.handlers.handlers[i], backend)) indices.push_back(i);
    return indices;
}

inline int eligibleDefaultAppHandlerIndexByIdentity(const DefaultAppsRow& row,
                                                    DefaultAppsBackend& backend,
                                                    const std::string& appId,
                                                    uint64_t registrationOwner,
                                                    uint64_t registrationGeneration)
{
    const std::vector<size_t> eligible = eligibleDefaultAppHandlerIndices(row, backend);
    for (size_t i = 0; i < eligible.size(); ++i) {
        const DocumentHandlerInfo& handler = row.handlers.handlers[eligible[i]];
        if (handler.appId == appId && handler.registrationOwner == registrationOwner &&
            handler.registrationGeneration == registrationGeneration) return static_cast<int>(i);
    }
    return -1;
}

inline std::string formatDefaultAppHandlerChoice(const DefaultAppsRow& row, size_t handlerIndex,
                                                DefaultAppsBackend& backend, size_t maxCharacters)
{
    if (handlerIndex >= row.handlers.count) return std::string();
    const DocumentHandlerInfo& handler = row.handlers.handlers[handlerIndex];
    std::string label = handler.displayName;
    if (label.empty()) label = backend.displayName(handler.appId);
    if (label.empty()) label = "Application unavailable";

    const std::string normalizedLabel = lowerAscii(label);
    size_t sameLabelCount = 0;
    for (size_t i = 0; i < row.handlers.count; ++i) {
        const DocumentHandlerInfo& candidate = row.handlers.handlers[i];
        if (isEligibleDefaultAppHandler(candidate, backend) &&
            lowerAscii(candidate.displayName) == normalizedLabel) ++sameLabelCount;
    }
    if (sameLabelCount > 1) label += " · " + handler.appId;
    return boundedSettingValue(label, maxCharacters, "Application");
}

class DefaultAppsModel {
public:
    const DefaultAppsSnapshot& snapshot() const { return m_snapshot; }

    bool refresh(DefaultAppsBackend& backend)
    {
        DefaultAppsSnapshot refreshed;
        if (!backend.available()) {
            const bool changed = m_snapshot.available || m_snapshot.count != 0 || m_snapshot.totalCount != 0;
            m_snapshot = std::move(refreshed);
            return changed;
        }

        std::vector<std::string> extensions;
        for (const std::string& extension : backend.knownDocumentExtensions()) {
            std::string normalized;
            if (NormalizeDocumentExtension(extension, normalized)) extensions.push_back(std::move(normalized));
        }
        std::sort(extensions.begin(), extensions.end());
        extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());

        refreshed.available = true;
        refreshed.totalCount = extensions.size();
        refreshed.count = std::min(extensions.size(), kMaxSettingsDefaultAppExtensions);
        refreshed.truncated = extensions.size() > kMaxSettingsDefaultAppExtensions;
        for (size_t i = 0; i < refreshed.count; ++i) {
            DefaultAppsRow& row = refreshed.rows[i];
            row.extension = extensions[i];
            row.policy = backend.defaultHandlerInfo(row.extension);
            row.handlers = backend.capableHandlers(row.extension);
            row.builtInDisplayName = backend.displayName(row.policy.builtInDefaultAppId);
            row.configuredDisplayName = backend.displayName(row.policy.configuredOverrideAppId);
            row.effectiveDisplayName = backend.displayName(row.policy.effectiveDefaultAppId);
        }

        const bool changed = !sameSnapshot(m_snapshot, refreshed);
        m_snapshot = std::move(refreshed);
        return changed;
    }

    DefaultHandlerMutationResult chooseHandler(DefaultAppsBackend& backend,
                                                const std::string& extension,
                                                size_t eligibleChoiceIndex)
    {
        const DefaultAppsRow* row = findDefaultAppsRow(m_snapshot, extension);
        if (!m_snapshot.available || !row) return failure(extension, "File type is no longer available.");
        const std::vector<size_t> eligible = eligibleDefaultAppHandlerIndices(*row, backend);
        if (eligibleChoiceIndex >= eligible.size()) return failure(extension, "That application is no longer available.");

        const DocumentHandlerInfo selected = row->handlers.handlers[eligible[eligibleChoiceIndex]];
        const std::string selectedExtension = row->extension;
        DefaultHandlerMutationResult result;
        result.extension = selectedExtension;
        result.appId = selected.appId;
        if (row->policy.configuredOverrideAppId.empty() &&
            !row->policy.builtInDefaultAppId.empty() &&
            selected.appId == row->policy.builtInDefaultAppId) {
            result.status = DefaultHandlerMutationStatus::Success;
            result.reason = "The built-in default is already selected.";
            refresh(backend);
            return result;
        }

        result = backend.setDefaultHandler(selectedExtension, selected.appId);
        refresh(backend);
        if (result.succeeded()) {
            const DefaultAppsRow* verified = findDefaultAppsRow(m_snapshot, selectedExtension);
            if (!verified || verified->policy.configuredOverrideAppId != selected.appId ||
                verified->policy.effectiveDefaultAppId != selected.appId ||
                verified->policy.configuredStatus != ConfiguredDefaultHandlerStatus::Available ||
                !verified->policy.effectiveDefaultAvailable) {
                result.status = DefaultHandlerMutationStatus::VerificationFailure;
                result.reason = "The default app could not be verified after saving.";
            }
        }
        return result;
    }

    DefaultHandlerMutationResult restoreBuiltInDefault(DefaultAppsBackend& backend,
                                                       const std::string& extension)
    {
        const DefaultAppsRow* row = findDefaultAppsRow(m_snapshot, extension);
        if (!m_snapshot.available || !row) return failure(extension, "File type is no longer available.");
        const std::string selectedExtension = row->extension;
        const std::string effectiveAppId = row->policy.effectiveDefaultAppId;
        if (row->policy.configuredOverrideAppId.empty()) {
            DefaultHandlerMutationResult result;
            result.extension = selectedExtension;
            result.appId = effectiveAppId;
            result.status = DefaultHandlerMutationStatus::Success;
            result.reason = "No configured override is present.";
            refresh(backend);
            return result;
        }

        DefaultHandlerMutationResult result = backend.clearDefaultHandler(selectedExtension);
        refresh(backend);
        if (result.succeeded()) {
            const DefaultAppsRow* verified = findDefaultAppsRow(m_snapshot, selectedExtension);
            if (!verified || !verified->policy.configuredOverrideAppId.empty()) {
                result.status = DefaultHandlerMutationStatus::VerificationFailure;
                result.reason = "The configured default could not be cleared.";
            }
        }
        return result;
    }

private:
    static DefaultHandlerMutationResult failure(const std::string& extension, const std::string& reason)
    {
        DefaultHandlerMutationResult result;
        result.status = DefaultHandlerMutationStatus::VerificationFailure;
        result.extension = extension;
        result.reason = reason;
        return result;
    }

    static bool sameRow(const DefaultAppsRow& left, const DefaultAppsRow& right)
    {
        if (left.extension != right.extension ||
            left.policy.extension != right.policy.extension ||
            left.policy.builtInDefaultAppId != right.policy.builtInDefaultAppId ||
            left.policy.configuredOverrideAppId != right.policy.configuredOverrideAppId ||
            left.policy.effectiveDefaultAppId != right.policy.effectiveDefaultAppId ||
            left.policy.configuredStatus != right.policy.configuredStatus ||
            left.policy.effectiveDefaultAvailable != right.policy.effectiveDefaultAvailable ||
            left.handlers.count != right.handlers.count ||
            left.handlers.declaredHandlerCount != right.handlers.declaredHandlerCount ||
            left.handlers.availableHandlerCount != right.handlers.availableHandlerCount ||
            left.handlers.truncated != right.handlers.truncated) return false;
        for (size_t i = 0; i < left.handlers.count; ++i) {
            const DocumentHandlerInfo& a = left.handlers.handlers[i];
            const DocumentHandlerInfo& b = right.handlers.handlers[i];
            if (a.appId != b.appId || a.displayName != b.displayName ||
                a.registrationOwner != b.registrationOwner ||
                a.registrationGeneration != b.registrationGeneration ||
                a.supportsDocumentActivation != b.supportsDocumentActivation ||
                a.registrationCurrent != b.registrationCurrent ||
                a.backendAvailable != b.backendAvailable || a.available != b.available ||
                a.isDefault != b.isDefault) return false;
        }
        return left.builtInDisplayName == right.builtInDisplayName &&
            left.configuredDisplayName == right.configuredDisplayName &&
            left.effectiveDisplayName == right.effectiveDisplayName;
    }

    static bool sameSnapshot(const DefaultAppsSnapshot& left, const DefaultAppsSnapshot& right)
    {
        if (left.available != right.available || left.count != right.count ||
            left.totalCount != right.totalCount || left.truncated != right.truncated) return false;
        for (size_t i = 0; i < left.count; ++i)
            if (!sameRow(left.rows[i], right.rows[i])) return false;
        return true;
    }

    DefaultAppsSnapshot m_snapshot{};
};

} // namespace settings
} // namespace apps
} // namespace gxos
