#define KERNEL_NO_GLOBAL_ARCHITECTURE_DETECTOR_ALIASES
#include "kernel/core/include/kernel/architecture_detector.h"
#include "package_manager.h"
#include "gxapp_container.h"
#include "fs.h"
#include "logger.h"
#include "universal_app_loader.h"

#include <algorithm>
#include <ctime>

namespace gxos {
namespace {

const char* const kApplicationDirectory = "/system/apps";
const char* const kStagingDirectory = "/system/apps/.staging";
const char* const kUnsupportedArchitectureWarning = "This application does not support your CPU architecture.";

static bool hasGxappExtension(const std::string& path){
    const std::string ext = ".gxapp";
    if (path.size() < ext.size()) return false;
    return path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}

static std::string fileNameFromPath(const std::string& path){
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

static std::string sanitizeApplicationName(const std::string& name){
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out.push_back(c);
        } else if (c == ' ' || c == '.') {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "application";
    return out;
}

static CpuArchitecture currentGXAppArchitecture(){
    return kernel::ArchitectureDetector::GetArchitecture();
}

static bool ensureApplicationDirectory(){
    return FS::createDirectories(kApplicationDirectory) && FS::createDirectories(kStagingDirectory);
}

static std::string stagingPackagePath(const std::string& applicationName){
    return std::string(kStagingDirectory) + "/" + sanitizeApplicationName(applicationName) + "." + std::to_string(static_cast<long long>(std::time(nullptr))) + ".gxapp.tmp";
}

static std::string replacementPackagePath(const std::string& applicationName){
    return std::string(kApplicationDirectory) + "/" + sanitizeApplicationName(applicationName) + ".replacing." + std::to_string(static_cast<long long>(std::time(nullptr))) + ".gxapp";
}

} // namespace

const char* PackageManager::ApplicationDirectory(){
    return kApplicationDirectory;
}

std::string PackageManager::installedPackagePath(const std::string& applicationName){
    return std::string(kApplicationDirectory) + "/" + sanitizeApplicationName(applicationName) + ".gxapp";
}

bool PackageManager::ValidateGXAppArchitecture(const std::string& packagePath, std::string& warningOrError){
    Logger::write(LogLevel::Info, std::string("PackageManager: validating gxapp architecture: ") + packagePath);

    GXApp app = GXAppContainer::Open(packagePath);
    if (!app.IsValid()) {
        warningOrError = app.GetLastError();
        Logger::write(LogLevel::Error, std::string("PackageManager: gxapp validation failed: ") + warningOrError);
        return false;
    }

    CpuArchitecture arch = currentGXAppArchitecture();
    if (arch == CpuArchitecture::Unknown || !app.HasBinary(arch)) {
        warningOrError = kUnsupportedArchitectureWarning;
        Logger::write(LogLevel::Warn, std::string("PackageManager: ") + warningOrError);
        return false;
    }

    warningOrError.clear();
    Logger::write(LogLevel::Info, std::string("PackageManager: gxapp supports architecture ") + GXAppContainer::ArchitectureToString(arch));
    return true;
}

PackageInstallResult PackageManager::InstallGXApp(const std::string& packagePath){
    PackageInstallResult result;
    Logger::write(LogLevel::Info, std::string("PackageManager: installing gxapp: ") + packagePath);

    if (!hasGxappExtension(packagePath)) {
        result.message = "Package is not a .gxapp file.";
        Logger::write(LogLevel::Error, std::string("PackageManager: ") + result.message);
        return result;
    }

    GXApp app = GXAppContainer::Open(packagePath);
    if (!app.IsValid()) {
        result.message = app.GetLastError();
        Logger::write(LogLevel::Error, std::string("PackageManager: failed to open gxapp: ") + result.message);
        return result;
    }

    result.applicationName = app.GetMetadata().applicationName;
    if (result.applicationName.empty()) {
        result.applicationName = fileNameFromPath(packagePath);
    }

    CpuArchitecture arch = currentGXAppArchitecture();
    result.supportedArchitecture = arch != CpuArchitecture::Unknown && app.HasBinary(arch);
    if (!result.supportedArchitecture) {
        result.message = kUnsupportedArchitectureWarning;
        Logger::write(LogLevel::Warn, std::string("PackageManager: install warning for ") + result.applicationName + ": " + result.message);
        return result;
    }

    if (!ensureApplicationDirectory()) {
        result.message = "Failed to create application directory.";
        Logger::write(LogLevel::Error, std::string("PackageManager: ") + result.message);
        return result;
    }

    std::vector<uint8_t> packageBytes;
    FSResult readResult = FS::readAll(packagePath, packageBytes, 128ULL * 1024ULL * 1024ULL);
    if (!readResult.success) {
        result.message = std::string("Failed to read gxapp package bytes: ") + readResult.message;
        Logger::write(LogLevel::Error, std::string("PackageManager: ") + result.message);
        return result;
    }

    result.installedPath = installedPackagePath(result.applicationName);
    std::string stagedPath = stagingPackagePath(result.applicationName);
    if (!FS::writeAll(stagedPath, packageBytes)) {
        result.message = "Failed to write staged gxapp package.";
        Logger::write(LogLevel::Error, std::string("PackageManager: ") + result.message);
        return result;
    }

    GXApp stagedApp = GXAppContainer::Open(stagedPath);
    if (!stagedApp.IsValid()) {
        result.message = std::string("Staged gxapp validation failed: ") + stagedApp.GetLastError();
        FS::removeFile(stagedPath);
        Logger::write(LogLevel::Error, std::string("PackageManager: ") + result.message);
        return result;
    }

    if (!FS::exists(result.installedPath)) {
        if (!FS::renameFile(stagedPath, result.installedPath, false)) {
            result.message = "Failed to atomically install staged gxapp package.";
            FS::removeFile(stagedPath);
            Logger::write(LogLevel::Error, std::string("PackageManager: ") + result.message);
            return result;
        }
    } else {
        std::string replacementPath = replacementPackagePath(result.applicationName);
        Logger::write(LogLevel::Warn, std::string("PackageManager: existing package found at ") + result.installedPath + "; installing replacement to " + replacementPath);
        if (!FS::renameFile(stagedPath, replacementPath, false)) {
            result.message = "Existing gxapp package was not overwritten; failed to store safe replacement package.";
            FS::removeFile(stagedPath);
            Logger::write(LogLevel::Error, std::string("PackageManager: ") + result.message);
            return result;
        }
        result.installedPath = replacementPath;
    }

    result.success = true;
    result.message = "Installed gxapp package.";
    Logger::write(LogLevel::Info, std::string("PackageManager: installed ") + result.applicationName + " to " + result.installedPath);
    return result;
}

bool PackageManager::LaunchGXApp(const std::string& applicationName, std::string& error){
    std::string path = installedPackagePath(applicationName);
    Logger::write(LogLevel::Info, std::string("PackageManager: launching gxapp: ") + path);

    if (!UniversalAppLoader::Execute(path, error)) {
        Logger::write(LogLevel::Error, std::string("PackageManager: gxapp launch failed: ") + error);
        return false;
    }

    Logger::write(LogLevel::Info, std::string("PackageManager: launched gxapp: ") + applicationName);
    return true;
}

std::vector<std::string> PackageManager::ListInstalledGXApps(){
    std::vector<std::string> apps;
    for (const FileInfo& file : FS::list(kApplicationDirectory)) {
        if (!file.isDir && hasGxappExtension(file.name)) {
            apps.push_back(file.name.substr(0, file.name.size() - 6));
        }
    }
    std::sort(apps.begin(), apps.end());
    return apps;
}

} // namespace gxos
