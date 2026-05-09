#pragma once

#include <string>
#include <vector>

namespace gxos {

struct PackageInstallResult {
    bool success = false;
    bool supportedArchitecture = false;
    std::string applicationName;
    std::string installedPath;
    std::string message;
};

class PackageManager {
public:
    static const char* ApplicationDirectory();

    static PackageInstallResult InstallGXApp(const std::string& packagePath);
    static bool ValidateGXAppArchitecture(const std::string& packagePath, std::string& warningOrError);
    static bool LaunchGXApp(const std::string& applicationName, std::string& error);
    static std::vector<std::string> ListInstalledGXApps();

private:
    static std::string installedPackagePath(const std::string& applicationName);
};

} // namespace gxos

using PackageManager = gxos::PackageManager;
using PackageInstallResult = gxos::PackageInstallResult;
