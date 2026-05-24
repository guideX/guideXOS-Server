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

} // namespace appmodel
} // namespace kernel
