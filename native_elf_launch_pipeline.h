#pragma once

#include "app_launch_resolver.h"

#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct NativeElfLaunchResult {
    bool success = false;
    std::string appId;
    std::string architecture;
    std::string elfPath;
    std::string entryPoint;
    std::string abi;
    std::string runtime;
    std::string message;
    std::vector<std::string> validationErrors;
};

class NativeElfLaunchPipeline {
public:
    static NativeElfLaunchResult PrepareLaunch(const RegisteredApp& app, const LaunchDecision& decision);

private:
    static void LogResult(const NativeElfLaunchResult& result);
};

} // namespace apps
} // namespace gxos
