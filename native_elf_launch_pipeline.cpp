#include "native_elf_launch_pipeline.h"

#include "elf_validator.h"
#include "fs.h"
#include "logger.h"

#include <filesystem>
#include <sstream>

namespace gxos {
namespace apps {
namespace {

constexpr uint64_t kMaxNativeElfSize = 128ULL * 1024ULL * 1024ULL;

void addError(NativeElfLaunchResult& result, const std::string& error) {
    result.validationErrors.push_back(error);
    if (result.message.empty()) result.message = error;
}

std::string joinErrors(const std::vector<std::string>& errors) {
    std::ostringstream oss;
    for (size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) oss << "; ";
        oss << errors[i];
    }
    return oss.str();
}

std::filesystem::path resolveRelativeEntryPath(const RegisteredApp& app, const LaunchDecision& decision, NativeElfLaunchResult& result) {
    std::filesystem::path entryPath(decision.entryPath);
    if (entryPath.empty()) {
        addError(result, "Native ELF entry path is empty");
        return std::filesystem::path();
    }

    if (entryPath.is_absolute()) {
        addError(result, "Native ELF entry path must be relative to the app directory");
        return std::filesystem::path();
    }

    if (app.appDirectory.empty()) {
        addError(result, "Native ELF app directory is empty");
        return std::filesystem::path();
    }

    return app.appDirectory / entryPath;
}

} // namespace

NativeElfLaunchResult NativeElfLaunchPipeline::PrepareLaunch(const RegisteredApp& app, const LaunchDecision& decision) {
    NativeElfLaunchResult result;
    result.appId = app.manifest.id;
    result.architecture = decision.architecture;
    result.elfPath = decision.entryPath;
    result.runtime = decision.runtime;

    const AppEntry* entry = nullptr;
    for (const AppEntry& candidate : app.manifest.entries) {
        if (candidate.path == decision.entryPath) {
            entry = &candidate;
            break;
        }
    }
    if (!entry) entry = app.FindCompatibleEntry(decision.architecture);
    if (entry) {
        result.abi = entry->abi;
        if (result.runtime.empty()) result.runtime = entry->runtime;
    }

    if (decision.strategy != AppLaunchStrategy::NativeElf) {
        addError(result, "Launch decision strategy is not NativeElf");
        LogResult(result);
        return result;
    }

    std::filesystem::path resolvedPath = resolveRelativeEntryPath(app, decision, result);
    if (!result.validationErrors.empty()) {
        LogResult(result);
        return result;
    }

    result.elfPath = resolvedPath.string();
    if (!FS::exists(result.elfPath)) {
        addError(result, "Native ELF file does not exist: " + result.elfPath);
        LogResult(result);
        return result;
    }

    std::vector<uint8_t> elfBytes;
    FSResult readResult = FS::readAll(result.elfPath, elfBytes, kMaxNativeElfSize);
    if (!readResult.success) {
        addError(result, "Failed to read Native ELF file: " + readResult.message);
        LogResult(result);
        return result;
    }

    ElfValidationResult validation = ElfValidator::Validate(elfBytes, decision.architecture);
    result.entryPoint = validation.entryPoint;
    if (!validation.architecture.empty()) result.architecture = validation.architecture;

    if (!validation.valid) {
        result.validationErrors = validation.errors;
        result.message = joinErrors(result.validationErrors);
        LogResult(result);
        return result;
    }

    result.success = true;
    result.message = "Native ELF validated but execution is not implemented yet.";
    LogResult(result);
    return result;
}

void NativeElfLaunchPipeline::LogResult(const NativeElfLaunchResult& result) {
    std::ostringstream oss;
    oss << "[NativeElfLaunch] "
        << "App: " << result.appId
        << " Architecture: " << result.architecture
        << " Path: " << result.elfPath
        << " Validation: " << (result.validationErrors.empty() ? "valid" : joinErrors(result.validationErrors))
        << " Result: " << (result.success ? "success" : "failure")
        << " Message: " << result.message;
    Logger::write(result.success ? LogLevel::Info : LogLevel::Warn, oss.str());
}

} // namespace apps
} // namespace gxos
