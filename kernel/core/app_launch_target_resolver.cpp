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
    unsigned int total;
    unsigned int ready;
    unsigned int alias;
    unsigned int shellAction;
    unsigned int unresolved;
    unsigned int skippedLayoutOnly;
    unsigned int highRisk;
    unsigned int printed;
    unsigned int truncated;
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

static void write_preview_record(LaunchTargetDiagnosticWriter write, LaunchStoragePreviewCounters& counters, const char* site, unsigned int index, const char* value, const char* existingKindHint, const char* baseRisk, unsigned int maxRows)
{
    gxos::apps::LaunchTarget target = resolveLaunchTarget(value);
    const char* adapterStatus = "";
    const char* adapterReason = "";
    const char* legacyDispatch = legacyDispatchStringForLaunchTarget(target, &adapterStatus, &adapterReason);
    const char* status = preview_status_for_target(target);
    const char* risk = preview_risk_for_status(baseRisk, status);
    preview_count_status(counters, status, risk);

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
    write(" highRisk=");
    write_uint(write, counters.highRisk);
    write(" printed=");
    write_uint(write, counters.printed);
    write(" truncated=");
    write_uint(write, counters.truncated);
    write("\n");
    write("notes: desktop.cpp:s_taskbarEntries[] count=0, so no taskbar pin records are previewed\n");
}

} // namespace appmodel
} // namespace kernel
