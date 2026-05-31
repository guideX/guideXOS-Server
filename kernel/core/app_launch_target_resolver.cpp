#include "include/kernel/app_launch_target_resolver.h"

#include "built_in_app_metadata.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace appmodel {

static bool text_equals(const char* a, const char* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool text_ends_with(const char* text, const char* suffix)
{
    if (!text || !suffix) return false;
    int textLen = 0;
    int suffixLen = 0;
    while (text[textLen]) ++textLen;
    while (suffix[suffixLen]) ++suffixLen;
    if (suffixLen > textLen) return false;
    return text_equals(text + textLen - suffixLen, suffix);
}

static bool is_path_like(const char* label)
{
    if (!label || !label[0]) return false;
    if (label[0] == '/') return true;
    for (int i = 0; label[i]; ++i) {
        if (label[i] == '/' || label[i] == '\\') return true;
        if (i == 1 && label[i] == ':') return true;
    }
    return false;
}

static bool is_text_file_path(const char* path)
{
    return text_ends_with(path, ".txt") ||
           text_ends_with(path, ".log") ||
           text_ends_with(path, ".cfg") ||
           text_ends_with(path, ".ini");
}

static void fill_from_metadata(gxos::apps::LaunchTarget& target, const gxos::apps::BuiltInAppMetadata& metadata)
{
    target.type = gxos::apps::LaunchTargetType::BuiltInApp;
    target.appId = metadata.appId ? metadata.appId : "";
    target.displayName = metadata.displayName ? metadata.displayName : "";
    target.dispatchLaunchName = metadata.kernelAppName ? metadata.kernelAppName : (metadata.launchName ? metadata.launchName : "");
    target.hostedAvailable = gxos::apps::IsBuiltInAppAvailableInHosted(metadata);
    target.bareMetalAvailable = gxos::apps::IsBuiltInAppAvailableInBareMetal(metadata) &&
        target.dispatchLaunchName[0] &&
        app::AppManager::isAppAvailable(target.dispatchLaunchName);
}

static bool is_shell_label(const char* label)
{
    return text_equals(label, "Console") ||
           text_equals(label, "Terminal") ||
           text_equals(label, "Computer") ||
           text_equals(label, "This System") ||
           text_equals(label, "Documents") ||
           text_equals(label, "Pictures") ||
           text_equals(label, "Music") ||
           text_equals(label, "Network") ||
           text_equals(label, "Control Panel") ||
           text_equals(label, "Settings") ||
           text_equals(label, "System Settings");
}

static bool is_hosted_shell_label_for_comparison(const char* label)
{
    return text_equals(label, "ComputerFiles");
}

static void fill_shell_label(gxos::apps::LaunchTarget& target, const char* label)
{
    target.type = gxos::apps::LaunchTargetType::ShellAction;
    target.displayName = label;
    target.shellAction = label;
    target.hostedAvailable = false;
    target.bareMetalAvailable = true;
    target.diagnosticStatus = "resolved-shell";

    if (text_equals(label, "Console") || text_equals(label, "Terminal")) {
        target.dispatchLaunchName = "Console";
        target.diagnosticReason = "Bare-metal shell label opens the kernel terminal; it is not a kernel AppManager app";
    } else if (text_equals(label, "This System")) {
        target.dispatchLaunchName = "Files";
        target.pathParameter = "/";
        target.diagnosticReason = "Desktop system object opens File Manager at the root path";
    } else if (text_equals(label, "Control Panel") || text_equals(label, "Settings") || text_equals(label, "System Settings")) {
        // The bare-metal right-column Start Menu still dispatches "Settings"
        // literally. Keep this DisplayOptions candidate diagnostic-only until
        // that legacy path is intentionally migrated.
        target.dispatchLaunchName = "DisplayOptions";
        target.diagnosticReason = "Bare-metal settings/control labels are shell/system affordances, not app metadata identities";
    } else {
        target.diagnosticReason = "Bare-metal Start Menu right-column shell/system label; current runtime behavior remains unchanged";
    }
}

gxos::apps::LaunchTarget resolveLaunchTarget(const char* label)
{
    gxos::apps::LaunchTarget target;
    target.originalLabel = label ? label : "";

    if (!label || !label[0]) {
        target.type = gxos::apps::LaunchTargetType::Unknown;
        target.diagnosticStatus = "unresolved";
        target.diagnosticReason = "No launch label supplied";
        return target;
    }

    if (text_equals(label, "AppModel")) {
        if (const gxos::apps::BuiltInAppMetadata* metadata = gxos::apps::FindBuiltInAppMetadataByAppId("gxos.builtin.appmodeldemo")) {
            fill_from_metadata(target, *metadata);
        }
        target.type = gxos::apps::LaunchTargetType::LegacyAlias;
        target.legacyAlias = "AppModel";
        target.displayName = "App Model Demo";
        target.dispatchLaunchName = "AppModel";
        target.hostedAvailable = true;
        target.bareMetalAvailable = true;
        target.diagnosticStatus = "resolved-alias";
        target.diagnosticReason = "Bare-metal AppModel opens the local app-model explanation view; launch behavior is unchanged";
        return target;
    }

    if (text_equals(label, "Files")) {
        if (const gxos::apps::BuiltInAppMetadata* metadata = gxos::apps::FindBuiltInAppMetadataByKernelLegacyAlias("Files")) {
            fill_from_metadata(target, *metadata);
        }
        target.type = gxos::apps::LaunchTargetType::LegacyAlias;
        target.legacyAlias = "Files";
        target.displayName = "FileExplorer";
        target.dispatchLaunchName = "Files";
        target.bareMetalAvailable = app::AppManager::isAppAvailable("Files");
        target.diagnosticStatus = "resolved-alias";
        target.diagnosticReason = "Bare-metal legacy alias registered for FileExplorer";
        return target;
    }

    if (text_equals(label, "ImgViewer")) {
        if (const gxos::apps::BuiltInAppMetadata* metadata = gxos::apps::FindBuiltInAppMetadataByAppId("gxos.builtin.imageviewer")) {
            fill_from_metadata(target, *metadata);
        }
        target.type = gxos::apps::LaunchTargetType::LegacyAlias;
        target.legacyAlias = "ImgViewer";
        target.appId = "gxos.builtin.imageviewer";
        target.displayName = "ImageViewer";
        target.dispatchLaunchName = "ImgViewer";
        target.hostedAvailable = true;
        target.bareMetalAvailable = false;
        target.diagnosticStatus = "unsupported-target";
        target.diagnosticReason = "Bare-metal static Start Menu label for hosted ImageViewer; no current bare-metal AppManager registration";
        return target;
    }

    if (is_shell_label(label)) {
        fill_shell_label(target, label);
        return target;
    }

    if (is_path_like(label)) {
        target.type = gxos::apps::LaunchTargetType::FileOpen;
        target.pathParameter = label;
        target.hostedAvailable = false;
        target.bareMetalAvailable = true;

        vfs::FileInfo info{};
        if (vfs::stat(label, &info) == vfs::VFS_OK) {
            if (info.type == vfs::FILE_TYPE_DIRECTORY) {
                target.dispatchLaunchName = "Files";
                target.diagnosticStatus = "resolved-file-open";
                target.diagnosticReason = "Folder path resolves to existing File Manager parameter launch";
            } else if (is_text_file_path(label)) {
                target.dispatchLaunchName = "Notepad";
                target.diagnosticStatus = "resolved-file-open";
                target.diagnosticReason = "Text file path resolves to existing Notepad parameter launch";
            } else {
                target.diagnosticStatus = "unsupported-file-open";
                target.diagnosticReason = "Path exists, but current bare-metal file-open handlers only cover folders and obvious text files";
            }
        } else {
            target.diagnosticStatus = "unresolved-file-open";
            target.diagnosticReason = "Path-like label supplied, but VFS stat did not find a current target";
        }
        return target;
    }

    if (const gxos::apps::BuiltInAppMetadata* metadata = gxos::apps::FindBuiltInAppMetadataByIdentity(label)) {
        fill_from_metadata(target, *metadata);
        target.diagnosticStatus = target.bareMetalAvailable ? "resolved" : "unsupported-target";
        target.diagnosticReason = target.bareMetalAvailable
            ? "Matched shared built-in app metadata and current bare-metal AppManager registration"
            : "Matched shared metadata, but this target is not currently registered for bare-metal launch";
        return target;
    }

    target.type = gxos::apps::LaunchTargetType::Unknown;
    target.diagnosticStatus = "unresolved";
    target.diagnosticReason = "No bare-metal kernel app, metadata identity, legacy alias, shell action, or file-open target matched";
    return target;
}

static void write_pair(LaunchTargetDiagnosticWriter write, const char* key, const char* value)
{
    if (!write) return;
    write(key);
    write(value ? value : "");
    write("\n");
}

static void write_bool_pair(LaunchTargetDiagnosticWriter write, const char* key, bool value)
{
    write_pair(write, key, value ? "true" : "false");
}

static void write_uint(LaunchTargetDiagnosticWriter write, unsigned int value)
{
    char buf[16];
    int pos = 0;
    if (value == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[16];
        int t = 0;
        while (value && t < 15) {
            tmp[t++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (t > 0) buf[pos++] = tmp[--t];
    }
    buf[pos] = '\0';
    write(buf);
}

static void write_quoted(LaunchTargetDiagnosticWriter write, const char* value)
{
    write("\"");
    write(value ? value : "");
    write("\"");
}

struct LaunchStoragePreviewCounters {
    struct UnsupportedAliasDetail {
        const char* target;
        const char* label;
        const char* mapsTo;
        const char* reason;
        unsigned int count;
    };

    unsigned int total;
    unsigned int ready;
    unsigned int alias;
    unsigned int shellAction;
    unsigned int unresolved;
    unsigned int skippedLayoutOnly;
    unsigned int targetSpecificUnsupportedAliases;
    unsigned int highRisk;
    unsigned int printed;
    unsigned int truncated;
    UnsupportedAliasDetail unsupportedAliasDetails[8];
    unsigned int unsupportedAliasDetailCount;
};

static const char* preview_status_for_target(const gxos::apps::LaunchTarget& target)
{
    if (target.type == gxos::apps::LaunchTargetType::Unknown) return "unresolved";
    if (target.type == gxos::apps::LaunchTargetType::LegacyAlias) return "alias";
    if (target.type == gxos::apps::LaunchTargetType::ShellAction) return "shell-action";
    return "ready";
}

static const char* preview_risk_for_status(const char* baseRisk, const char* status)
{
    if (text_equals(status, "unresolved")) return "high";
    if (text_equals(status, "alias") || text_equals(status, "shell-action")) return "medium";
    return baseRisk && baseRisk[0] ? baseRisk : "medium";
}

static bool is_target_specific_unsupported_alias(const gxos::apps::LaunchTarget& target)
{
    return target.type == gxos::apps::LaunchTargetType::LegacyAlias &&
           text_equals(target.diagnosticStatus, "unsupported-target");
}

static const char* compact_unsupported_alias_reason(const gxos::apps::LaunchTarget& target)
{
    if (text_equals(target.diagnosticStatus, "unsupported-target")) {
        return "No bare-metal AppManager registration yet";
    }
    return target.diagnosticReason;
}

static const char* preview_existing_kind(const char* value, const gxos::apps::LaunchTarget& target, const char* hint)
{
    if (hint && hint[0]) return hint;
    if (target.type == gxos::apps::LaunchTargetType::LegacyAlias) return "legacy alias";
    if (target.type == gxos::apps::LaunchTargetType::ShellAction) return "shell action";
    if (target.type == gxos::apps::LaunchTargetType::FileOpen || is_path_like(value)) return "file path";
    if (target.appId && target.appId[0] && text_equals(value, target.appId)) return "app ID";
    if (target.displayName && target.displayName[0] && text_equals(value, target.displayName)) return "display name";
    if (target.dispatchLaunchName && target.dispatchLaunchName[0] && text_equals(value, target.dispatchLaunchName)) return "launch name";
    return target.type == gxos::apps::LaunchTargetType::Unknown ? "unknown" : "launch string";
}

static void preview_count_status(LaunchStoragePreviewCounters& counters, const char* status, const char* risk)
{
    ++counters.total;
    if (text_equals(status, "ready")) ++counters.ready;
    else if (text_equals(status, "alias")) ++counters.alias;
    else if (text_equals(status, "shell-action")) ++counters.shellAction;
    else if (text_equals(status, "skip-layout-only")) ++counters.skippedLayoutOnly;
    else ++counters.unresolved;
    if (text_equals(risk, "high")) ++counters.highRisk;
}

static void preview_count_target_specific_alias(LaunchStoragePreviewCounters& counters, const gxos::apps::LaunchTarget& target, const char* targetName)
{
    if (!is_target_specific_unsupported_alias(target)) return;
    ++counters.targetSpecificUnsupportedAliases;

    const char* label = target.legacyAlias && target.legacyAlias[0] ? target.legacyAlias : target.originalLabel;
    const char* mapsTo = target.displayName && target.displayName[0] ? target.displayName : target.appId;
    const char* reason = compact_unsupported_alias_reason(target);

    for (unsigned int i = 0; i < counters.unsupportedAliasDetailCount; ++i) {
        LaunchStoragePreviewCounters::UnsupportedAliasDetail& detail = counters.unsupportedAliasDetails[i];
        if (text_equals(detail.target, targetName) &&
            text_equals(detail.label, label) &&
            text_equals(detail.mapsTo, mapsTo) &&
            text_equals(detail.reason, reason)) {
            ++detail.count;
            return;
        }
    }

    if (counters.unsupportedAliasDetailCount >= sizeof(counters.unsupportedAliasDetails) / sizeof(counters.unsupportedAliasDetails[0])) {
        return;
    }

    LaunchStoragePreviewCounters::UnsupportedAliasDetail& detail = counters.unsupportedAliasDetails[counters.unsupportedAliasDetailCount++];
    detail.target = targetName;
    detail.label = label;
    detail.mapsTo = mapsTo;
    detail.reason = reason;
    detail.count = 1;
}

static void write_preview_record(LaunchTargetDiagnosticWriter write, LaunchStoragePreviewCounters& counters, const char* site, unsigned int index, const char* value, const char* existingKindHint, const char* baseRisk, unsigned int maxRows)
{
    gxos::apps::LaunchTarget target = resolveLaunchTarget(value);
    const char* adapterStatus = "";
    const char* adapterReason = "";
    const char* legacyDispatch = legacyDispatchStringForLaunchTarget(target, &adapterStatus, &adapterReason);
    const char* status = preview_status_for_target(target);
    const char* risk = preview_risk_for_status(baseRisk, status);
    preview_count_status(counters, status, risk);
    preview_count_target_specific_alias(counters, target, "bareMetal");

    if (counters.printed >= maxRows) {
        ++counters.truncated;
        return;
    }

    ++counters.printed;
    write("  record site=");
    write(site);
    write(" index=");
    write_uint(write, index);
    write(" existing=");
    write_quoted(write, value);
    write(" existingKind=");
    write_quoted(write, preview_existing_kind(value, target, existingKindHint));
    write(" resolvedType=");
    write(gxos::apps::ToString(target.type));
    write(" appId=");
    write_quoted(write, target.appId);
    write(" displayName=");
    write_quoted(write, target.displayName);
    write(" legacyDispatch=");
    write_quoted(write, legacyDispatch);
    write(" proposed={targetType=");
    write(gxos::apps::ToString(target.type));
    if (target.appId && target.appId[0]) { write(",appId="); write_quoted(write, target.appId); }
    if (target.displayName && target.displayName[0]) { write(",displayName="); write_quoted(write, target.displayName); }
    if (target.dispatchLaunchName && target.dispatchLaunchName[0]) { write(",launchName="); write_quoted(write, target.dispatchLaunchName); }
    if (target.legacyAlias && target.legacyAlias[0]) { write(",legacyAlias="); write_quoted(write, target.legacyAlias); }
    if (target.shellAction && target.shellAction[0]) { write(",shellAction="); write_quoted(write, target.shellAction); }
    if (target.pathParameter && target.pathParameter[0]) { write(",path="); write_quoted(write, target.pathParameter); }
    write(",hosted=");
    write(target.hostedAvailable ? "true" : "false");
    write(",bareMetal=");
    write(target.bareMetalAvailable ? "true" : "false");
    write("} risk=");
    write(risk);
    write(" status=");
    write(status);
    write(" reason=");
    write_quoted(write, target.diagnosticReason);
    write("\n");
}

static void write_preview_skip_record(LaunchTargetDiagnosticWriter write, LaunchStoragePreviewCounters& counters, const char* site, unsigned int index, const char* value, const char* note, unsigned int maxRows)
{
    preview_count_status(counters, "skip-layout-only", "low");
    if (counters.printed >= maxRows) {
        ++counters.truncated;
        return;
    }
    ++counters.printed;
    write("  record site=");
    write(site);
    write(" index=");
    write_uint(write, index);
    write(" existing=");
    write_quoted(write, value);
    write(" existingKind=\"layout key\" resolvedType=Unknown appId=\"\" displayName=\"\" legacyDispatch=\"\" proposed={skip=\"layout-only\"} risk=low status=skip-layout-only reason=");
    write_quoted(write, note);
    write("\n");
}

static void count_preview_value(LaunchStoragePreviewCounters& counters, const char* value, const char* baseRisk)
{
    gxos::apps::LaunchTarget target = resolveLaunchTarget(value);
    const char* status = preview_status_for_target(target);
    const char* risk = preview_risk_for_status(baseRisk, status);
    preview_count_status(counters, status, risk);
    preview_count_target_specific_alias(counters, target, "bareMetal");
}

static void count_preview_skip_only(LaunchStoragePreviewCounters& counters)
{
    preview_count_status(counters, "skip-layout-only", "low");
}

static void copy_preview_field(const char* src, int len, char* out, int outSize);

static void count_shortcut_file_preview_values(LaunchStoragePreviewCounters& counters)
{
    char shortcuts[2048];
    int32_t shortcutBytes = vfs::read_file("/desktop.shortcuts", shortcuts, sizeof(shortcuts) - 1);
    if (shortcutBytes <= 0) return;
    shortcuts[shortcutBytes] = '\0';

    unsigned int index = 0;
    int lineStart = 0;
    for (int i = 0; i <= shortcutBytes && index < 16; ++i) {
        if (shortcuts[i] != '\n' && shortcuts[i] != '\0') continue;
        int lineEnd = i;
        if (lineEnd > lineStart && shortcuts[lineEnd - 1] == '\r') --lineEnd;
        if (lineEnd > lineStart) {
            int tab1 = -1;
            int tab2 = -1;
            for (int j = lineStart; j < lineEnd; ++j) {
                if (shortcuts[j] == '\t') {
                    if (tab1 < 0) tab1 = j;
                    else { tab2 = j; break; }
                }
            }
            if (tab1 > lineStart && tab2 > tab1) {
                char target[160];
                copy_preview_field(shortcuts + tab1 + 1, tab2 - tab1 - 1, target, sizeof(target));
                count_preview_value(counters, target, "medium");
                ++index;
            }
        }
        lineStart = i + 1;
    }
}

static void count_icon_layout_preview_values(LaunchStoragePreviewCounters& counters)
{
    char iconLayout[2048];
    int32_t layoutBytes = vfs::read_file("/.desktop_icons", iconLayout, sizeof(iconLayout) - 1);
    if (layoutBytes <= 0) return;
    iconLayout[layoutBytes] = '\0';

    unsigned int index = 0;
    int lineStart = 0;
    for (int i = 0; i <= layoutBytes && index < 32; ++i) {
        if (iconLayout[i] != '\n' && iconLayout[i] != '\0') continue;
        int lineEnd = i;
        if (lineEnd > lineStart && iconLayout[lineEnd - 1] == '\r') --lineEnd;
        if (lineEnd > lineStart) {
            int tab = -1;
            for (int j = lineStart; j < lineEnd; ++j) {
                if (iconLayout[j] == '\t') { tab = j; break; }
            }
            if (tab > lineStart) {
                count_preview_skip_only(counters);
                ++index;
            }
        }
        lineStart = i + 1;
    }
}

static LaunchStoragePreviewCounters collect_bare_metal_preview_counts()
{
    LaunchStoragePreviewCounters counters{};

    const char* startMenuApps[] = {
        "Calculator", "Notepad", "Console", "Trash", "TaskManager", "DiskManager",
        "DisplayOptions", "guideXOS Navigator", "HDInstaller", "AppModel", "Paint",
        "Clock", "Files", "ImgViewer"
    };
    for (unsigned int i = 0; i < sizeof(startMenuApps) / sizeof(startMenuApps[0]); ++i) {
        count_preview_value(counters, startMenuApps[i], "medium");
    }

    const char* allPrograms[] = {
        "Calculator", "Clock", "Console", "ControlPanel", "DiskManager", "Files",
        "guideXOS Navigator", "HDInstaller", "ImgViewer", "AppModel", "Notepad",
        "Paint", "TaskManager", "Trash"
    };
    for (unsigned int i = 0; i < sizeof(allPrograms) / sizeof(allPrograms[0]); ++i) {
        count_preview_value(counters, allPrograms[i], "medium");
    }

    const char* rightColumn[] = {
        "Computer", "Documents", "Pictures", "Music", "Network", "Control Panel", "Settings"
    };
    for (unsigned int i = 0; i < sizeof(rightColumn) / sizeof(rightColumn[0]); ++i) {
        count_preview_value(counters, rightColumn[i], "medium");
    }

    count_shortcut_file_preview_values(counters);

    const char* systemIconFlags[] = { "Trash", "ThisSystem", "FileManager", "SystemSettings" };
    for (unsigned int i = 0; i < sizeof(systemIconFlags) / sizeof(systemIconFlags[0]); ++i) {
        count_preview_skip_only(counters);
    }

    count_icon_layout_preview_values(counters);
    return counters;
}

static void write_preview_counts_line(LaunchTargetDiagnosticWriter write, const char* label, const LaunchStoragePreviewCounters& counters, const char* extra)
{
    write(label);
    write(" total=");
    write_uint(write, counters.total);
    write(" ready=");
    write_uint(write, counters.ready);
    write(" alias=");
    write_uint(write, counters.alias);
    write(" shellAction=");
    write_uint(write, counters.shellAction);
    write(" unresolved=");
    write_uint(write, counters.unresolved);
    write(" skippedLayoutOnly=");
    write_uint(write, counters.skippedLayoutOnly);
    write(" targetSpecificUnsupportedAliases=");
    write_uint(write, counters.targetSpecificUnsupportedAliases);
    write(" highRisk=");
    write_uint(write, counters.highRisk);
    if (extra && extra[0]) {
        write(" ");
        write(extra);
    }
    write("\n");
}

static void write_preview_unsupported_alias_details(LaunchTargetDiagnosticWriter write, const LaunchStoragePreviewCounters& hostedCounts, const LaunchStoragePreviewCounters& bareMetalCounts)
{
    if (!write) return;
    if (hostedCounts.unsupportedAliasDetailCount == 0 && bareMetalCounts.unsupportedAliasDetailCount == 0) return;

    write("targetSpecificUnsupportedAliasDetails:\n");
    const LaunchStoragePreviewCounters* allCounts[] = { &hostedCounts, &bareMetalCounts };
    for (unsigned int c = 0; c < sizeof(allCounts) / sizeof(allCounts[0]); ++c) {
        const LaunchStoragePreviewCounters& counts = *allCounts[c];
        for (unsigned int i = 0; i < counts.unsupportedAliasDetailCount; ++i) {
            const LaunchStoragePreviewCounters::UnsupportedAliasDetail& detail = counts.unsupportedAliasDetails[i];
            write("  target=");
            write(detail.target);
            write(" label=");
            write_quoted(write, detail.label);
            write(" count=");
            write_uint(write, detail.count);
            write(" mapsTo=");
            write_quoted(write, detail.mapsTo);
            write(" reason=");
            write_quoted(write, detail.reason);
            write("\n");
        }
    }
}

void printLaunchTargetDiagnostic(const gxos::apps::LaunchTarget& target, LaunchTargetDiagnosticWriter write)
{
    if (!write) return;
    write("[LaunchTarget]\n");
    write_pair(write, "originalLabel: ", target.originalLabel);
    write_pair(write, "resolvedType: ", gxos::apps::ToString(target.type));
    write_pair(write, "appId: ", target.appId);
    write_pair(write, "displayName: ", target.displayName);
    write_pair(write, "dispatchLaunchName: ", target.dispatchLaunchName);
    write_pair(write, "legacyAlias: ", target.legacyAlias);
    write_pair(write, "shellAction: ", target.shellAction);
    write_pair(write, "pathParameter: ", target.pathParameter);
    write_bool_pair(write, "hostedAvailable: ", target.hostedAvailable);
    write_bool_pair(write, "bareMetalAvailable: ", target.bareMetalAvailable);
    write_pair(write, "status: ", target.diagnosticStatus);
    write_pair(write, "reason: ", target.diagnosticReason);
}

void printLaunchTargetDiagnostic(const char* label, LaunchTargetDiagnosticWriter write)
{
    printLaunchTargetDiagnostic(resolveLaunchTarget(label), write);
}

const char* legacyDispatchStringForLaunchTarget(const gxos::apps::LaunchTarget& target, const char** status, const char** reason)
{
    const char* localStatus = "unsupported";
    const char* localReason = "No legacy dispatch mapping for this launch target";
    const char* dispatch = "";

    switch (target.type) {
    case gxos::apps::LaunchTargetType::BuiltInApp:
    case gxos::apps::LaunchTargetType::LegacyAlias:
    case gxos::apps::LaunchTargetType::ShellAction:
    case gxos::apps::LaunchTargetType::ManifestApp:
    case gxos::apps::LaunchTargetType::NativeElfApp:
    case gxos::apps::LaunchTargetType::GXAppPackage:
    case gxos::apps::LaunchTargetType::Service:
    case gxos::apps::LaunchTargetType::HypervisorGuest:
    case gxos::apps::LaunchTargetType::Script:
        if (target.dispatchLaunchName && target.dispatchLaunchName[0]) {
            localStatus = "ok";
            localReason = "Adapter returns the resolver dispatchLaunchName used by current bare-metal dispatch surfaces";
            dispatch = target.dispatchLaunchName;
        }
        break;
    case gxos::apps::LaunchTargetType::FileOpen:
        if (target.dispatchLaunchName && target.dispatchLaunchName[0]) {
            localStatus = "ok";
            localReason = "File-open target carries the current handler app name; path remains a separate parameter";
            dispatch = target.dispatchLaunchName;
        } else {
            localStatus = "unsupported";
            localReason = "File-open target has no current bare-metal handler app";
        }
        break;
    case gxos::apps::LaunchTargetType::CrossArchEmulatedApp:
        localStatus = "unsupported";
        localReason = "Cross-arch/emulated launch dispatch is not implemented";
        break;
    case gxos::apps::LaunchTargetType::Unknown:
    default:
        localStatus = "unsupported";
        localReason = "Unknown launch target has no legacy dispatch string";
        break;
    }

    if (status) *status = localStatus;
    if (reason) *reason = localReason;
    return dispatch;
}

void printLaunchTargetAdapterDiagnostic(const char* label, LaunchTargetDiagnosticWriter write)
{
    if (!write) return;

    gxos::apps::LaunchTarget target = resolveLaunchTarget(label);
    const char* status = "";
    const char* reason = "";
    const char* legacyDispatch = legacyDispatchStringForLaunchTarget(target, &status, &reason);
    const bool matchesResolvedDispatch = legacyDispatch && legacyDispatch[0] && text_equals(legacyDispatch, target.dispatchLaunchName);
    const bool matchesOriginalLabel = legacyDispatch && legacyDispatch[0] && text_equals(legacyDispatch, label);

    write("[LaunchTargetAdapter]\n");
    write_pair(write, "originalLabel: ", target.originalLabel);
    write_pair(write, "resolvedType: ", gxos::apps::ToString(target.type));
    write_pair(write, "appId: ", target.appId);
    write_pair(write, "resolvedDispatchName: ", target.dispatchLaunchName);
    write_pair(write, "adapterLegacyDispatchString: ", legacyDispatch);
    write_bool_pair(write, "matchesResolvedDispatch: ", matchesResolvedDispatch);
    write_bool_pair(write, "matchesOriginalLabel: ", matchesOriginalLabel);
    write_pair(write, "status: ", status);
    write_pair(write, "reason: ", reason);
    write_pair(write, "nonFatal: ", "true");
}

struct LaunchTargetShadowSmokeCase {
    const char* name;
    const char* source;
    const char* label;
    const char* expectedCurrentDispatch;
};

static bool is_file_explorer_alias_pair(const char* a, const char* b)
{
    return (text_equals(a, "Files") && text_equals(b, "FileExplorer")) ||
           (text_equals(a, "FileExplorer") && text_equals(b, "Files"));
}

static bool is_expected_unsupported_shadow_target(const gxos::apps::LaunchTarget& target)
{
    return text_equals(target.diagnosticStatus, "unsupported-target") ||
           text_equals(target.diagnosticStatus, "unsupported-file-open");
}

static const char* launch_target_shadow_smoke_comparison(const gxos::apps::LaunchTarget& target, const char* expectedCurrentDispatch, const char* adapterLegacyDispatch)
{
    if (is_expected_unsupported_shadow_target(target)) return "expected-unsupported";
    if (adapterLegacyDispatch && adapterLegacyDispatch[0] &&
        expectedCurrentDispatch && expectedCurrentDispatch[0] &&
        text_equals(adapterLegacyDispatch, expectedCurrentDispatch)) {
        return "match";
    }
    if (target.appId && text_equals(target.appId, "gxos.builtin.fileexplorer") &&
        is_file_explorer_alias_pair(adapterLegacyDispatch, expectedCurrentDispatch)) {
        return "accepted-mismatch";
    }
    return "unexpected-mismatch";
}

void printLaunchTargetShadowSmokeDiagnostic(LaunchTargetDiagnosticWriter write)
{
    if (!write) return;

    static const LaunchTargetShadowSmokeCase kSmokeCases[] = {
        { "StartMenuNotepad", "StartMenu", "Notepad", "Notepad" },
        { "BuiltInAppIdNotepad", "ResolverAppId", "gxos.builtin.notepad", "Notepad" },
        { "StartMenuFilesAlias", "StartMenu", "Files", "Files" },
        { "FileExplorerCanonical", "DesktopFileManager", "FileExplorer", "Files" },
        { "NavigatorKernelApp", "StartMenu", "guideXOS Navigator", "guideXOS Navigator" },
        { "ImageViewerStaticAlias", "StartMenu", "ImgViewer", "ImgViewer" },
        { "RootFolderFileOpen", "FileOpen", "/", "Files" },
        { "UnknownProbe", "SmokeProbe", "FakeLaunchShadowApp", "" }
    };

    unsigned int observations = 0;
    unsigned int matches = 0;
    unsigned int acceptedMismatches = 0;
    unsigned int expectedUnsupported = 0;
    unsigned int unexpectedMismatches = 0;

    write("[LaunchTargetShadowSmoke]\n");
    write("command: desktop.smoke.launchshadow\n");
    write("mode: diagnostic-only\n");
    write("launchesApps: false\n");
    write("counterScope: command-local\n");
    write("counterReason: real bare-metal desktop launch paths are not instrumented in this smoke pass\n");
    write("cases:\n");

    for (unsigned int i = 0; i < sizeof(kSmokeCases) / sizeof(kSmokeCases[0]); ++i) {
        const LaunchTargetShadowSmokeCase& smokeCase = kSmokeCases[i];
        gxos::apps::LaunchTarget target = resolveLaunchTarget(smokeCase.label);
        const char* adapterStatus = "";
        const char* adapterReason = "";
        const char* adapterLegacyDispatch = legacyDispatchStringForLaunchTarget(target, &adapterStatus, &adapterReason);
        const char* comparison = launch_target_shadow_smoke_comparison(target, smokeCase.expectedCurrentDispatch, adapterLegacyDispatch);
        const bool candidateMatchesExpected = adapterLegacyDispatch && adapterLegacyDispatch[0] &&
            smokeCase.expectedCurrentDispatch && smokeCase.expectedCurrentDispatch[0] &&
            text_equals(adapterLegacyDispatch, smokeCase.expectedCurrentDispatch);

        ++observations;
        if (text_equals(comparison, "match")) ++matches;
        else if (text_equals(comparison, "accepted-mismatch")) ++acceptedMismatches;
        else if (text_equals(comparison, "expected-unsupported")) ++expectedUnsupported;
        else ++unexpectedMismatches;

        write("  case=");
        write(smokeCase.name);
        write(" source=");
        write(smokeCase.source);
        write(" inputLabel=");
        write_quoted(write, smokeCase.label);
        write(" expectedCurrentDispatch=");
        write_quoted(write, smokeCase.expectedCurrentDispatch);
        write(" resolvedType=");
        write(gxos::apps::ToString(target.type));
        write(" appId=");
        write_quoted(write, target.appId);
        write(" resolvedDispatch=");
        write_quoted(write, target.dispatchLaunchName);
        write(" adapterLegacyDispatch=");
        write_quoted(write, adapterLegacyDispatch);
        write(" candidateMatchesExpected=");
        write(candidateMatchesExpected ? "true" : "false");
        write(" comparison=");
        write(comparison);
        write(" adapterStatus=");
        write(adapterStatus);
        write(" adapterReason=");
        write_quoted(write, adapterReason);
        write(" status=");
        write(target.diagnosticStatus);
        write(" reason=");
        write_quoted(write, target.diagnosticReason);
        write(" nonFatal=true\n");
    }

    write("summary: observations=");
    write_uint(write, observations);
    write(" matches=");
    write_uint(write, matches);
    write(" acceptedMismatches=");
    write_uint(write, acceptedMismatches);
    write(" expectedUnsupported=");
    write_uint(write, expectedUnsupported);
    write(" unexpectedMismatches=");
    write_uint(write, unexpectedMismatches);
    write(" nonFatal=true\n");
    write("runtimeLaunchBehaviorChanged: false\n");
}

static const char* const kLaunchTargetComparisonLabels[] = {
    "Notepad",
    "gxos.builtin.notepad",
    "FileExplorer",
    "Files",
    "guideXOS Navigator",
    "ComputerFiles",
    "AppModel",
    "TotallyUnknownLaunchThing"
};

static gxos::apps::LaunchTarget resolveHostedLaunchTargetForComparison(const char* label)
{
    gxos::apps::LaunchTarget target;
    target.originalLabel = label ? label : "";

    if (!label || !label[0]) {
        target.type = gxos::apps::LaunchTargetType::Unknown;
        target.diagnosticStatus = "unresolved";
        target.diagnosticReason = "No launch label supplied";
        return target;
    }

    if (text_equals(label, "AppModel")) {
        if (const gxos::apps::BuiltInAppMetadata* metadata = gxos::apps::FindBuiltInAppMetadataByAppId("gxos.builtin.appmodeldemo")) {
            fill_from_metadata(target, *metadata);
        }
        target.type = gxos::apps::LaunchTargetType::LegacyAlias;
        target.legacyAlias = "AppModel";
        target.appId = "gxos.builtin.appmodeldemo";
        target.displayName = "App Model Demo";
        target.dispatchLaunchName = "App Model Demo";
        target.hostedAvailable = true;
        target.bareMetalAvailable = false;
        target.diagnosticStatus = "resolved-alias";
        target.diagnosticReason = "Hosted legacy UI alias for App Model Demo";
        return target;
    }

    if (is_hosted_shell_label_for_comparison(label)) {
        target.type = gxos::apps::LaunchTargetType::ShellAction;
        target.displayName = "Computer Files";
        target.dispatchLaunchName = "FileExplorer";
        target.shellAction = "ComputerFiles";
        target.hostedAvailable = true;
        target.bareMetalAvailable = false;
        target.diagnosticStatus = "resolved-shell";
        target.diagnosticReason = "Hosted shell/system label currently canonicalizes to FileExplorer";
        return target;
    }

    if (is_path_like(label)) {
        target.type = gxos::apps::LaunchTargetType::FileOpen;
        target.pathParameter = label;
        target.hostedAvailable = true;
        target.bareMetalAvailable = true;
        target.diagnosticStatus = "resolved-file-open";
        target.diagnosticReason = "Hosted path-like label mirror for comparison diagnostics";
        return target;
    }

    if (const gxos::apps::BuiltInAppMetadata* metadata = gxos::apps::FindBuiltInAppMetadataByIdentity(label)) {
        target.type = gxos::apps::LaunchTargetType::BuiltInApp;
        target.appId = metadata->appId ? metadata->appId : "";
        target.displayName = metadata->displayName ? metadata->displayName : "";
        target.dispatchLaunchName = metadata->launchName ? metadata->launchName : "";
        target.hostedAvailable = gxos::apps::IsBuiltInAppAvailableInHosted(*metadata);
        target.bareMetalAvailable = gxos::apps::IsBuiltInAppAvailableInBareMetal(*metadata);
        target.diagnosticStatus = "resolved";
        target.diagnosticReason = "Matched shared built-in metadata in hosted comparison mirror";
        return target;
    }

    target.type = gxos::apps::LaunchTargetType::Unknown;
    target.diagnosticStatus = "unresolved";
    target.diagnosticReason = "No mirrored hosted target matched";
    return target;
}

static bool same_target_core(const gxos::apps::LaunchTarget& a, const gxos::apps::LaunchTarget& b)
{
    return a.type == b.type &&
        text_equals(a.appId, b.appId) &&
        text_equals(a.dispatchLaunchName, b.dispatchLaunchName) &&
        text_equals(a.diagnosticStatus, b.diagnosticStatus);
}

static bool same_non_empty_app_id(const gxos::apps::LaunchTarget& a, const gxos::apps::LaunchTarget& b)
{
    return a.appId && a.appId[0] && text_equals(a.appId, b.appId);
}

static const char* comparison_status(const char* label, const gxos::apps::LaunchTarget& hosted, const gxos::apps::LaunchTarget& bareMetal)
{
    if (same_target_core(hosted, bareMetal)) return "exact";
    if (text_equals(label, "ComputerFiles")) return "intentional-difference";
    if (text_equals(label, "AppModel")) return "intentional-difference";
    if (same_non_empty_app_id(hosted, bareMetal) &&
        (hosted.type == gxos::apps::LaunchTargetType::LegacyAlias ||
         bareMetal.type == gxos::apps::LaunchTargetType::LegacyAlias)) {
        return "accepted-alias";
    }
    return "unexpected-drift";
}

static const char* comparison_note(const char* label, const char* status)
{
    if (text_equals(status, "exact")) return "hosted and bare-metal diagnostic targets match";
    if (text_equals(status, "accepted-alias")) return "same app identity with an accepted legacy alias difference";
    if (text_equals(label, "ComputerFiles")) return "hosted shell/system label; bare-metal uses separate right-column labels and system objects";
    if (text_equals(label, "AppModel")) return "legacy app-model demo alias has target-specific dispatch names";
    return "investigate launch target drift before feeding typed targets into dispatch";
}

static void write_int(LaunchTargetDiagnosticWriter write, int value)
{
    char buffer[12];
    int pos = 0;
    if (value == 0) {
        buffer[pos++] = '0';
    } else {
        char tmp[12];
        int tmpPos = 0;
        int v = value;
        while (v > 0 && tmpPos < 11) {
            tmp[tmpPos++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (tmpPos > 0) buffer[pos++] = tmp[--tmpPos];
    }
    buffer[pos] = '\0';
    write(buffer);
}

static void write_target_inline(LaunchTargetDiagnosticWriter write, const gxos::apps::LaunchTarget& target)
{
    write("{type=");
    write(gxos::apps::ToString(target.type));
    write(" status=");
    write(target.diagnosticStatus);
    write(" dispatch=");
    write(target.dispatchLaunchName);
    write(" appId=");
    write(target.appId);
    write("}");
}

void printLaunchTargetComparisonDiagnostic(LaunchTargetDiagnosticWriter write)
{
    if (!write) return;

    int exactCount = 0;
    int acceptedAliasCount = 0;
    int intentionalDifferenceCount = 0;
    int unexpectedDriftCount = 0;
    const int labelCount = (int)(sizeof(kLaunchTargetComparisonLabels) / sizeof(kLaunchTargetComparisonLabels[0]));

    write("[LaunchTargetComparison]\n");
    write("labels: ");
    write_int(write, labelCount);
    write("\nentries:\n");

    for (int i = 0; i < labelCount; ++i) {
        const char* label = kLaunchTargetComparisonLabels[i];
        gxos::apps::LaunchTarget hosted = resolveHostedLaunchTargetForComparison(label);
        gxos::apps::LaunchTarget bareMetal = resolveLaunchTarget(label);
        const char* result = comparison_status(label, hosted, bareMetal);

        if (text_equals(result, "exact")) ++exactCount;
        else if (text_equals(result, "accepted-alias")) ++acceptedAliasCount;
        else if (text_equals(result, "intentional-difference")) ++intentionalDifferenceCount;
        else ++unexpectedDriftCount;

        write("  label=");
        write(label);
        write(" result=");
        write(result);
        write(" hosted");
        write_target_inline(write, hosted);
        write(" bareMetal");
        write_target_inline(write, bareMetal);
        write(" note=");
        write(comparison_note(label, result));
        write("\n");
    }

    write("exactMatches: ");
    write_int(write, exactCount);
    write("\nacceptedAliasMatches: ");
    write_int(write, acceptedAliasCount);
    write("\nintentionalDifferences: ");
    write_int(write, intentionalDifferenceCount);
    write("\nunexpectedDrift: ");
    write_int(write, unexpectedDriftCount);
    write("\noverall: ");
    write(unexpectedDriftCount == 0 ? "OK" : "WARN");
    write("\nnonFatal: true\n");
}

void printLaunchStorageDiagnostic(LaunchTargetDiagnosticWriter write)
{
    if (!write) return;

    write("[LaunchStringStorage]\n");
    write("nonFatal: true\n");
    write("migrationState: not-started\n");
    write("hostedReference: desktop.launch.storage in hosted shell reports live desktop.json counts\n");
    write("bareMetalSites:\n");
    write("  site=desktop.cpp:s_startMenuApps[].name location=kernel/core/desktop.cpp fields=name,pinned,recent stores=kernel launch name or legacy alias count=14 typedDerivable=mostly risk=medium note=static Start Menu pinned/recent list includes AppModel and Files aliases\n");
    write("  site=desktop.cpp:s_allProgramsList[] location=kernel/core/desktop.cpp fields=string stores=kernel launch name or legacy alias count=14 typedDerivable=mostly risk=medium note=static All Programs list, not manifest-driven\n");
    write("  site=VFS:/desktop.shortcuts location=/desktop.shortcuts fields=shortcutType<TAB>target<TAB>label stores=App launch name or File/Folder path plus label count=up-to-16 typedDerivable=yes risk=medium note=bare-metal persisted shortcut format v2\n");
    write("  site=desktop.cpp:s_desktopIcons[] location=kernel/core/desktop.cpp fields=label,path,pinned,recent,kind,systemObject stores=system labels, app launch names, file paths count=static+dynamic typedDerivable=mostly risk=medium note=runtime desktop source for icon launch and recent flags\n");
    write("  site=VFS:/.desktop_icons location=/.desktop_icons fields=layoutKey<TAB>x<TAB>y stores=layout key, not launch source count=dynamic typedDerivable=not-applicable risk=low note=position-only storage can keep old string keys through migration\n");
    write("  site=VFS:/desktop.system.icons location=/desktop.system.icons fields=Trash,ThisSystem,FileManager,SystemSettings stores=system object visibility flags count=4 typedDerivable=not-applicable risk=low note=shell/system affordance visibility, not app identity\n");
    write("  site=desktop.cpp:s_taskbarEntries[] location=kernel/core/desktop.cpp fields=title,color,active stores=taskbar entry label, currently disabled/static count=0 typedDerivable=not-applicable risk=low note=no separate bare-metal taskbar pin storage found in this pass\n");
}

static void copy_preview_field(const char* src, int len, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    int n = len;
    if (n >= outSize) n = outSize - 1;
    for (int i = 0; i < n; ++i) out[i] = src[i];
    out[n] = '\0';
}

void printLaunchStoragePreviewDiagnostic(LaunchTargetDiagnosticWriter write)
{
    if (!write) return;

    const unsigned int maxRows = 96;
    LaunchStoragePreviewCounters counters{};

    write("[LaunchStringStoragePreview]\n");
    write("nonFatal: true\n");
    write("migrationState: preview-only\n");
    write("writesStorage: false\n");
    write("target: bare-metal\n");
    write("rowCap: ");
    write_uint(write, maxRows);
    write("\n");
    write("records:\n");

    const char* startMenuApps[] = {
        "Calculator", "Notepad", "Console", "Trash", "TaskManager", "DiskManager",
        "DisplayOptions", "guideXOS Navigator", "HDInstaller", "AppModel", "Paint",
        "Clock", "Files", "ImgViewer"
    };
    for (unsigned int i = 0; i < sizeof(startMenuApps) / sizeof(startMenuApps[0]); ++i) {
        write_preview_record(write, counters, "desktop.cpp:s_startMenuApps[].name", i, startMenuApps[i], "", "medium", maxRows);
    }

    const char* allPrograms[] = {
        "Calculator", "Clock", "Console", "ControlPanel", "DiskManager", "Files",
        "guideXOS Navigator", "HDInstaller", "ImgViewer", "AppModel", "Notepad",
        "Paint", "TaskManager", "Trash"
    };
    for (unsigned int i = 0; i < sizeof(allPrograms) / sizeof(allPrograms[0]); ++i) {
        write_preview_record(write, counters, "desktop.cpp:s_allProgramsList[]", i, allPrograms[i], "", "medium", maxRows);
    }

    const char* rightColumn[] = {
        "Computer", "Documents", "Pictures", "Music", "Network", "Control Panel", "Settings"
    };
    for (unsigned int i = 0; i < sizeof(rightColumn) / sizeof(rightColumn[0]); ++i) {
        write_preview_record(write, counters, "desktop.cpp:s_startMenuRight[].label", i, rightColumn[i], "shell action", "medium", maxRows);
    }

    char shortcuts[2048];
    int32_t shortcutBytes = vfs::read_file("/desktop.shortcuts", shortcuts, sizeof(shortcuts) - 1);
    if (shortcutBytes > 0) {
        shortcuts[shortcutBytes] = '\0';
        unsigned int index = 0;
        int lineStart = 0;
        for (int i = 0; i <= shortcutBytes && index < 16; ++i) {
            if (shortcuts[i] != '\n' && shortcuts[i] != '\0') continue;
            int lineEnd = i;
            if (lineEnd > lineStart && shortcuts[lineEnd - 1] == '\r') --lineEnd;
            if (lineEnd > lineStart) {
                int tab1 = -1;
                int tab2 = -1;
                for (int j = lineStart; j < lineEnd; ++j) {
                    if (shortcuts[j] == '\t') {
                        if (tab1 < 0) tab1 = j;
                        else { tab2 = j; break; }
                    }
                }
                if (tab1 > lineStart && tab2 > tab1) {
                    char type[16];
                    char target[160];
                    copy_preview_field(shortcuts + lineStart, tab1 - lineStart, type, sizeof(type));
                    copy_preview_field(shortcuts + tab1 + 1, tab2 - tab1 - 1, target, sizeof(target));
                    const char* kind = (text_equals(type, "App")) ? "launch name" : "file path";
                    write_preview_record(write, counters, "VFS:/desktop.shortcuts", index, target, kind, "medium", maxRows);
                    ++index;
                }
            }
            lineStart = i + 1;
        }
    }

    const char* systemIconFlags[] = { "Trash", "ThisSystem", "FileManager", "SystemSettings" };
    for (unsigned int i = 0; i < sizeof(systemIconFlags) / sizeof(systemIconFlags[0]); ++i) {
        write_preview_skip_record(write, counters, "VFS:/desktop.system.icons", i, systemIconFlags[i], "System icon visibility flag, not a launch target record", maxRows);
    }

    char iconLayout[2048];
    int32_t layoutBytes = vfs::read_file("/.desktop_icons", iconLayout, sizeof(iconLayout) - 1);
    if (layoutBytes > 0) {
        iconLayout[layoutBytes] = '\0';
        unsigned int index = 0;
        int lineStart = 0;
        for (int i = 0; i <= layoutBytes && index < 32; ++i) {
            if (iconLayout[i] != '\n' && iconLayout[i] != '\0') continue;
            int lineEnd = i;
            if (lineEnd > lineStart && iconLayout[lineEnd - 1] == '\r') --lineEnd;
            if (lineEnd > lineStart) {
                int tab = -1;
                for (int j = lineStart; j < lineEnd; ++j) {
                    if (iconLayout[j] == '\t') { tab = j; break; }
                }
                if (tab > lineStart) {
                    char key[160];
                    copy_preview_field(iconLayout + lineStart, tab - lineStart, key, sizeof(key));
                    write_preview_skip_record(write, counters, "VFS:/.desktop_icons", index, key, "Icon position record stores layout only and should not become a launch target", maxRows);
                    ++index;
                }
            }
            lineStart = i + 1;
        }
    }

    write("summary: totalRecords=");
    write_uint(write, counters.total);
    write(" ready=");
    write_uint(write, counters.ready);
    write(" alias=");
    write_uint(write, counters.alias);
    write(" shellAction=");
    write_uint(write, counters.shellAction);
    write(" unresolved=");
    write_uint(write, counters.unresolved);
    write(" skippedLayoutOnly=");
    write_uint(write, counters.skippedLayoutOnly);
    write(" targetSpecificUnsupportedAliases=");
    write_uint(write, counters.targetSpecificUnsupportedAliases);
    write(" highRisk=");
    write_uint(write, counters.highRisk);
    write(" printed=");
    write_uint(write, counters.printed);
    write(" truncated=");
    write_uint(write, counters.truncated);
    write("\n");
    write("notes: desktop.cpp:s_taskbarEntries[] count=0, so no taskbar pin records are previewed\n");
}

void printLaunchStoragePreviewComparisonDiagnostic(LaunchTargetDiagnosticWriter write)
{
    if (!write) return;

    LaunchStoragePreviewCounters hostedReference{};
    hostedReference.total = 74;
    hostedReference.ready = 69;
    hostedReference.alias = 0;
    hostedReference.shellAction = 5;
    hostedReference.unresolved = 0;
    hostedReference.skippedLayoutOnly = 0;
    hostedReference.highRisk = 0;

    LaunchStoragePreviewCounters bareMetalCounts = collect_bare_metal_preview_counts();
    const unsigned int unexpectedDrift = hostedReference.highRisk + bareMetalCounts.highRisk;

    write("[LaunchStringStoragePreviewComparison]\n");
    write("nonFatal: true\n");
    write("migrationState: preview-only\n");
    write("writesStorageHosted=false\n");
    write("writesStorageBareMetal=false\n");
    write_preview_counts_line(write, "hosted", hostedReference, "source=current-hosted-baseline use-hosted-shell-for-live-desktop-json");
    write_preview_counts_line(write, "bareMetal", bareMetalCounts, "source=bare-metal-actual");
    write_preview_unsupported_alias_details(write, hostedReference, bareMetalCounts);
    write("intentionalDifferences: 6\n");
    write("  difference=hosted-desktop-json note=hosted owns live desktop.json pinned/recent/desktopShortcuts/iconPositions storage\n");
    write("  difference=bare-metal-vfs note=bare-metal owns VFS /desktop.shortcuts, /.desktop_icons, and /desktop.system.icons storage\n");
    write("  difference=start-menu-source note=hosted all-programs are registry-derived while bare-metal Start Menu arrays are static today\n");
    write("  difference=shell-labels note=hosted ComputerFiles is a shell label while bare-metal uses Computer/Documents/Pictures/Music/Network/Settings labels\n");
    write("  difference=dynamic-runtime-sites note=desktop icon/taskbar runtime labels are target-specific and are not migrated in this diagnostic\n");
    write("  difference=bare-metal-imgviewer note=ImgViewer is a diagnostic-only legacy/static label for hosted ImageViewer and remains unsupported on bare-metal\n");
    write("unexpectedDrift: ");
    write_uint(write, unexpectedDrift);
    write("\n");
    if (bareMetalCounts.highRisk > 0) {
        write("  drift=bareMetalHighRisk count=");
        write_uint(write, bareMetalCounts.highRisk);
        write(" note=inspect bare-metal desktop.launch.storage.preview for unresolved/high-risk rows\n");
    }
    write("overall: ");
    write(unexpectedDrift == 0 ? "OK" : "WARN");
    write("\n");
    write("detailCommands: desktop.launch.storage.preview, desktop.launch.storage\n");
}

} // namespace appmodel
} // namespace kernel
