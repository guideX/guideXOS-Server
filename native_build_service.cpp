#include "native_build_service.h"

#include "logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace gxos {
namespace apps {
namespace NativeBuildService {
namespace {

constexpr uint32_t kMaxPathBytes = 240;
constexpr uint32_t kMaxMetadataBytes = 16u * 1024u;
constexpr uint32_t kMaxOutputBytes = 64u * 1024u;
constexpr uint32_t kMaxLineBytes = GX_BUILD_MAX_OUTPUT_LINE_BYTES;
constexpr uint32_t kMaxOutputLines = GX_BUILD_MAX_OUTPUT_LINES;
constexpr uint64_t kMaxBuildMilliseconds = 300000;
constexpr uint64_t kMaxArtifactBytes = 64ull * 1024ull * 1024ull;
constexpr char kBuildSystem[] = "guidexos-native-build-script-v1";
constexpr char kProjectKind[] = "native-gui-application";
constexpr char kTargetProfile[] = "guidexos.amd64.hosted.native";
constexpr char kBuildScript[] = "build.ps1";
constexpr char kConfiguration[] = "Debug";

struct CapturedLine {
    uint32_t stream = 0;
    std::string text;
};

struct BuildJob {
    gx_build_handle handle = 0;
    uint64_t runtimeId = 0;
    NativeBuildRequest request;
    std::string artifactAbsolute;
    std::string sdkInclude;
    std::string toolchainRoot;
    std::mutex mutex;
    std::thread worker;
    std::vector<CapturedLine> lines;
    uint64_t stdoutBytes = 0;
    uint64_t stderrBytes = 0;
    bool outputTruncated = false;
    bool completed = false;
    bool terminateRequested = false;
    uint64_t startTicks = 0;
    uint64_t endTicks = 0;
    gx_build_snapshot snapshot = {};
#if defined(_WIN32)
    HANDLE process = nullptr;
    HANDLE processJob = nullptr;
#endif
};

std::mutex g_jobsMutex;
std::map<gx_build_handle, std::shared_ptr<BuildJob>> g_jobs;
std::atomic<uint64_t> g_nextHandle{1};

uint64_t nowMilliseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void copyBounded(char* destination, size_t capacity, const std::string& value) {
    if (!destination || capacity == 0) return;
    const size_t count = std::min(capacity - 1, value.size());
    std::copy(value.begin(), value.begin() + static_cast<std::ptrdiff_t>(count), destination);
    destination[count] = '\0';
}

const char* errorName(uint32_t error) {
    switch (error) {
    case GX_BUILD_ERROR_NONE: return "none";
    case GX_BUILD_ERROR_INVALID_REQUEST: return "invalid_request";
    case GX_BUILD_ERROR_BUSY: return "busy";
    case GX_BUILD_ERROR_SDK_NOT_FOUND: return "sdk_not_found";
    case GX_BUILD_ERROR_TOOLCHAIN_NOT_FOUND: return "toolchain_not_found";
    case GX_BUILD_ERROR_POWERSHELL_NOT_FOUND: return "powershell_not_found";
    case GX_BUILD_ERROR_BUILD_SCRIPT_MISSING: return "build_script_missing";
    case GX_BUILD_ERROR_INVALID_PROJECT_ROOT: return "invalid_project_root";
    case GX_BUILD_ERROR_PROCESS_START_FAILED: return "process_start_failed";
    case GX_BUILD_ERROR_PROCESS_FAILED: return "process_failed";
    case GX_BUILD_ERROR_BUILD_TIMEOUT: return "build_timeout";
    case GX_BUILD_ERROR_ARTIFACT_MISSING: return "artifact_missing";
    case GX_BUILD_ERROR_ARTIFACT_INVALID: return "artifact_invalid";
    case GX_BUILD_ERROR_ARTIFACT_WRONG_ARCHITECTURE: return "artifact_wrong_architecture";
    case GX_BUILD_ERROR_ENTRY_POINT_MISSING: return "entry_point_missing";
    case GX_BUILD_ERROR_MANIFEST_ARTIFACT_MISMATCH: return "manifest_artifact_mismatch";
    case GX_BUILD_ERROR_OUTPUT_TRUNCATED: return "output_truncated";
    case GX_BUILD_ERROR_INTERNAL: return "internal";
    default: return "unknown";
    }
}

void setState(const std::shared_ptr<BuildJob>& job, uint32_t state) {
    std::lock_guard<std::mutex> lock(job->mutex);
    job->snapshot.state = state;
}

void setFailure(const std::shared_ptr<BuildJob>& job, uint32_t error, int32_t exitCode = -1) {
    std::lock_guard<std::mutex> lock(job->mutex);
    job->snapshot.state = GX_BUILD_FAILED;
    job->snapshot.errorCode = error;
    job->snapshot.processExitCode = exitCode;
    copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(error));
    job->completed = true;
    job->endTicks = nowMilliseconds();
    job->snapshot.elapsedMilliseconds = job->endTicks >= job->startTicks ? job->endTicks - job->startTicks : 0;
}

bool isSafeRelativePath(const std::string& value, size_t maxBytes) {
    if (value.empty() || value.size() >= maxBytes || value[0] == '/' || value[0] == '\\') return false;
    if (value.size() >= 2 && value[1] == ':') return false;
    size_t start = 0;
    for (size_t i = 0; i <= value.size(); ++i) {
        if (i < value.size() && value[i] != '/' && value[i] != '\\') continue;
        if (i == start) return false;
        const std::string segment = value.substr(start, i - start);
        if (segment == "." || segment == "..") return false;
        start = i + 1;
    }
    return true;
}

bool isSafeOutputName(const std::string& value) {
    if (value.empty() || value.size() >= 96 || value[0] == '.') return false;
    for (char valueChar : value) {
        if (!((valueChar >= 'a' && valueChar <= 'z') || (valueChar >= '0' && valueChar <= '9') ||
              valueChar == '-' || valueChar == '_' || valueChar == '.')) return false;
    }
    return true;
}

bool pathHasSymlinkComponent(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        const std::filesystem::file_status status = std::filesystem::symlink_status(current, ec);
        if (std::filesystem::is_symlink(status)) return true;
        if (ec == std::errc::no_such_file_or_directory) { ec.clear(); continue; }
        if (ec) return true;
    }
    return false;
}

bool isContainedPath(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const std::string rootText = root.lexically_normal().generic_string();
    const std::string candidateText = candidate.lexically_normal().generic_string();
    if (candidateText.size() < rootText.size() || candidateText.compare(0, rootText.size(), rootText) != 0) return false;
    return candidateText.size() == rootText.size() || candidateText[rootText.size()] == '/';
}

bool readBoundedFile(const std::filesystem::path& path, uint32_t maxBytes, std::string& output) {
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) return false;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    output.assign(static_cast<size_t>(size), '\0');
    if (size > 0) input.read(&output[0], static_cast<std::streamsize>(size));
    return static_cast<bool>(input) || size == 0;
}

bool extractJsonString(const std::string& json, const std::string& key, std::string& value) {
    const std::string needle = "\"" + key + "\"";
    size_t position = json.find(needle);
    if (position == std::string::npos || json.find(needle, position + needle.size()) != std::string::npos) return false;
    position += needle.size();
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position++] != ':') return false;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position++] != '"') return false;
    value.clear();
    while (position < json.size()) {
        const char valueChar = json[position++];
        if (valueChar == '"') return true;
        if (valueChar == '\\' || static_cast<unsigned char>(valueChar) < 0x20) return false;
        value.push_back(valueChar);
        if (value.size() >= 240) return false;
    }
    return false;
}

bool extractJsonNumber(const std::string& json, const std::string& key, uint32_t& value) {
    const std::string needle = "\"" + key + "\"";
    size_t position = json.find(needle);
    if (position == std::string::npos || json.find(needle, position + needle.size()) != std::string::npos) return false;
    position += needle.size();
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position++] != ':') return false;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position] < '0' || json[position] > '9') return false;
    uint64_t number = 0;
    while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
        number = number * 10u + static_cast<uint32_t>(json[position++] - '0');
        if (number > std::numeric_limits<uint32_t>::max()) return false;
    }
    value = static_cast<uint32_t>(number);
    return true;
}

bool validateProjectAndManifest(const NativeBuildRequest& request, const std::filesystem::path& root, std::string& artifactAbsolute, uint32_t& error) {
    std::string metadata;
    if (!readBoundedFile(root / "guidexos.project", kMaxMetadataBytes, metadata)) { error = GX_BUILD_ERROR_INVALID_PROJECT_ROOT; return false; }
    uint32_t formatVersion = 0;
    std::string projectId, projectKind, targetProfile, outputName;
    if (!extractJsonNumber(metadata, "formatVersion", formatVersion) || formatVersion != 1 ||
        !extractJsonString(metadata, "projectId", projectId) || !extractJsonString(metadata, "projectKind", projectKind) ||
        !extractJsonString(metadata, "defaultTargetProfile", targetProfile) || !extractJsonString(metadata, "outputName", outputName)) {
        error = GX_BUILD_ERROR_INVALID_REQUEST;
        return false;
    }
    if (projectId != request.projectId || projectKind != kProjectKind || targetProfile != kTargetProfile || !isSafeOutputName(outputName)) {
        error = GX_BUILD_ERROR_INVALID_REQUEST;
        return false;
    }
    if (request.projectKind != kProjectKind || request.targetProfile != kTargetProfile || request.buildSystem != kBuildSystem ||
        request.buildScript != kBuildScript || request.configuration != kConfiguration || !isSafeRelativePath(request.expectedArtifact, 160)) {
        error = GX_BUILD_ERROR_INVALID_REQUEST;
        return false;
    }
    const std::string expectedRelative = std::string("build/bin/amd64/") + outputName + ".elf";
    if (request.expectedArtifact != expectedRelative) { error = GX_BUILD_ERROR_INVALID_REQUEST; return false; }
    const std::filesystem::path script = root / std::filesystem::path(kBuildScript);
    const std::filesystem::path artifact = root / std::filesystem::path(request.expectedArtifact);
    if (!isContainedPath(root, script) || !isContainedPath(root, artifact) || pathHasSymlinkComponent(script) || pathHasSymlinkComponent(artifact)) { error = GX_BUILD_ERROR_INVALID_PROJECT_ROOT; return false; }
    std::error_code ec;
    const std::filesystem::file_status rootStatus = std::filesystem::symlink_status(root, ec);
    if (ec || !std::filesystem::is_directory(rootStatus) || pathHasSymlinkComponent(root)) { error = GX_BUILD_ERROR_INVALID_PROJECT_ROOT; return false; }
    const std::filesystem::file_status scriptStatus = std::filesystem::symlink_status(script, ec);
    if (ec || !std::filesystem::is_regular_file(scriptStatus) || std::filesystem::is_symlink(scriptStatus)) { error = GX_BUILD_ERROR_BUILD_SCRIPT_MISSING; return false; }
    std::string scriptContent;
    if (!readBoundedFile(script, kMaxMetadataBytes, scriptContent) ||
        scriptContent.find("# GUIDEXOS_NATIVE_BUILD_RECIPE_V1") == std::string::npos) {
        error = GX_BUILD_ERROR_BUILD_SCRIPT_MISSING;
        return false;
    }
    std::string manifest;
    if (!readBoundedFile(root / "app/app.json", kMaxMetadataBytes, manifest)) { error = GX_BUILD_ERROR_INVALID_REQUEST; return false; }
    std::string manifestPath, manifestEntry, manifestAbi, manifestArchitecture, manifestId;
    if (!extractJsonString(manifest, "path", manifestPath) || !extractJsonString(manifest, "entryPoint", manifestEntry) ||
        !extractJsonString(manifest, "abi", manifestAbi) || !extractJsonString(manifest, "architecture", manifestArchitecture) ||
        !extractJsonString(manifest, "id", manifestId) || manifestId != request.projectId || manifestArchitecture != "amd64" ||
        manifestEntry != "gx_main" || manifestAbi != "guidexos-c-abi-v1" || manifestPath != "bin/amd64/" + outputName + ".elf") {
        error = GX_BUILD_ERROR_MANIFEST_ARTIFACT_MISMATCH;
        return false;
    }
    artifactAbsolute = artifact.lexically_normal().string();
    return true;
}

bool resolveDirectoryCandidate(const std::filesystem::path& candidate, const char* child, std::string& output) {
    std::error_code ec;
    const std::filesystem::path directory = child ? candidate / child : candidate;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(directory, ec)) || ec || pathHasSymlinkComponent(directory)) return false;
    output = directory.lexically_normal().string();
    return true;
}

bool resolveSdkInclude(std::string& output) {
    const char* configured = std::getenv("GUIDEXOS_SDK_ROOT");
    if (configured && *configured) {
        if (resolveDirectoryCandidate(std::filesystem::path(configured), "include", output)) return true;
        return false;
    }
    std::error_code ec;
    const std::filesystem::path serverSdk = std::filesystem::current_path(ec) / "sdk";
    return !ec && resolveDirectoryCandidate(serverSdk, "include", output);
}

bool resolveToolchainRoot(std::string& output) {
    std::vector<std::filesystem::path> candidates;
    const char* configured = std::getenv("GUIDEXOS_TOOLCHAIN_ROOT");
    if (configured && *configured) candidates.emplace_back(configured);
#if defined(_WIN32)
    candidates.emplace_back("C:/Program Files/LLVM/bin");
    candidates.emplace_back("C:/mingw64/bin");
#endif
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(candidate / "clang++.exe", ec)) || ec) continue;
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(candidate / "ld.lld.exe", ec)) || ec) continue;
        if (pathHasSymlinkComponent(candidate)) continue;
        output = candidate.lexically_normal().string();
        return true;
    }
    return false;
}

bool isDiagnostic(const std::string& line, const char* token) {
    const std::string needle = std::string(": ") + token + ":";
    return line.find(needle) != std::string::npos || line.rfind(std::string(token) + ":", 0) == 0;
}

void appendLine(const std::shared_ptr<BuildJob>& job, uint32_t stream, const std::string& raw) {
    std::string line;
    line.reserve(std::min<size_t>(raw.size(), kMaxLineBytes - 1));
    for (unsigned char value : raw) line.push_back(value < 0x20 && value != '\t' ? '?' : static_cast<char>(value));
    if (line.size() >= kMaxLineBytes) { line.resize(kMaxLineBytes - 1); std::lock_guard<std::mutex> lock(job->mutex); job->outputTruncated = true; }
    std::lock_guard<std::mutex> lock(job->mutex);
    if (stream == 2) job->stderrBytes += raw.size(); else job->stdoutBytes += raw.size();
    if ((stream == 2 ? job->stderrBytes : job->stdoutBytes) > kMaxOutputBytes) { job->outputTruncated = true; return; }
    if (job->lines.size() >= kMaxOutputLines) { job->outputTruncated = true; return; }
    job->lines.push_back({stream, line});
    if (isDiagnostic(line, "warning")) ++job->snapshot.warningCount;
    if (isDiagnostic(line, "error") || line.find("fatal error:") != std::string::npos) ++job->snapshot.errorCount;
}

void appendBytes(const std::shared_ptr<BuildJob>& job, uint32_t stream, std::string& pending, const char* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        char value = bytes[i];
        if (value == '\r') continue;
        if (value == '\n') { appendLine(job, stream, pending); pending.clear(); }
        else pending.push_back(value);
    }
}

void finishOutput(const std::shared_ptr<BuildJob>& job, uint32_t stream, std::string& pending) {
    if (!pending.empty()) appendLine(job, stream, pending);
}

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring output(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &output[0], length) != length) return std::wstring();
    return output;
}

std::wstring quoteWindowsArgument(const std::wstring& value) {
    std::wstring output = L"\"";
    size_t backslashes = 0;
    for (wchar_t valueChar : value) {
        if (valueChar == L'\\') { ++backslashes; continue; }
        if (valueChar == L'"') output.append(backslashes * 2 + 1, L'\\');
        else output.append(backslashes, L'\\');
        backslashes = 0;
        output.push_back(valueChar);
    }
    output.append(backslashes * 2, L'\\');
    output.push_back(L'"');
    return output;
}

std::wstring powerShellPath() {
    wchar_t windowsDirectory[MAX_PATH] = {};
    const UINT length = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::wstring();
    std::wstring path(windowsDirectory, windowsDirectory + length);
    path += L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return std::wstring();
    return path;
}

std::wstring environmentBlock(const std::string& toolchainRoot) {
    std::wstring block;
    const char* names[] = {"SystemRoot", "WINDIR", "TEMP", "TMP", "PATHEXT"};
    for (const char* name : names) {
        const char* value = std::getenv(name);
        if (!value || !*value) continue;
        std::wstring wideName = utf8ToWide(name);
        std::wstring wideValue = utf8ToWide(value);
        if (wideName.empty() || wideValue.empty()) continue;
        block += wideName + L"=" + wideValue;
        block.push_back(L'\0');
    }
    block += L"PATH=" + utf8ToWide(toolchainRoot);
    block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

bool runProcess(const std::shared_ptr<BuildJob>& job) {
    const std::wstring executable = powerShellPath();
    if (executable.empty()) { setFailure(job, GX_BUILD_ERROR_POWERSHELL_NOT_FOUND); return false; }
    const std::wstring root = utf8ToWide(job->request.projectRoot);
    const std::wstring script = utf8ToWide((std::filesystem::path(job->request.projectRoot) / job->request.buildScript).string());
    const std::wstring sdk = utf8ToWide(job->sdkInclude);
    const std::wstring tools = utf8ToWide(job->toolchainRoot);
    if (root.empty() || script.empty() || sdk.empty() || tools.empty()) { setFailure(job, GX_BUILD_ERROR_INVALID_REQUEST); return false; }

    SECURITY_ATTRIBUTES attributes = {};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE stdoutReadHandle = nullptr;
    HANDLE stdoutWriteHandle = nullptr;
    HANDLE stderrReadHandle = nullptr;
    HANDLE stderrWriteHandle = nullptr;
    if (!CreatePipe(&stdoutReadHandle, &stdoutWriteHandle, &attributes, 0) ||
        !CreatePipe(&stderrReadHandle, &stderrWriteHandle, &attributes, 0)) {
        if (stdoutReadHandle) CloseHandle(stdoutReadHandle);
        if (stdoutWriteHandle) CloseHandle(stdoutWriteHandle);
        if (stderrReadHandle) CloseHandle(stderrReadHandle);
        if (stderrWriteHandle) CloseHandle(stderrWriteHandle);
        setFailure(job, GX_BUILD_ERROR_PROCESS_START_FAILED);
        return false;
    }
    SetHandleInformation(stdoutReadHandle, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrReadHandle, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nullptr;
    startup.hStdOutput = stdoutWriteHandle;
    startup.hStdError = stderrWriteHandle;
    std::wstring command = quoteWindowsArgument(executable) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        quoteWindowsArgument(script) + L" -SdkInclude " + quoteWindowsArgument(sdk) + L" -ToolchainRoot " + quoteWindowsArgument(tools) +
        L" -Configuration " + quoteWindowsArgument(utf8ToWide(job->request.configuration));
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    std::wstring environment = environmentBlock(job->toolchainRoot);
    PROCESS_INFORMATION process = {};
    const BOOL created = CreateProcessW(executable.c_str(), &mutableCommand[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT, &environment[0], root.c_str(), &startup, &process);
    CloseHandle(stdoutWriteHandle);
    CloseHandle(stderrWriteHandle);
    if (!created) { CloseHandle(stdoutReadHandle); CloseHandle(stderrReadHandle); setFailure(job, GX_BUILD_ERROR_PROCESS_START_FAILED); return false; }
    HANDLE processJob = CreateJobObjectW(nullptr, nullptr);
    if (processJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(processJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(processJob, process.hProcess);
    }
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->process = process.hProcess;
        job->processJob = processJob;
        job->snapshot.state = GX_BUILD_RUNNING;
    }
    CloseHandle(process.hThread);
    std::thread stdoutReader([job, stdoutReadHandle]() {
        char buffer[4096] = {};
        std::string pending;
        DWORD bytesRead = 0;
        while (ReadFile(stdoutReadHandle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) appendBytes(job, 1, pending, buffer, bytesRead);
        finishOutput(job, 1, pending);
        CloseHandle(stdoutReadHandle);
    });
    std::thread stderrReader([job, stderrReadHandle]() {
        char buffer[4096] = {};
        std::string pending;
        DWORD bytesRead = 0;
        while (ReadFile(stderrReadHandle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) appendBytes(job, 2, pending, buffer, bytesRead);
        finishOutput(job, 2, pending);
        CloseHandle(stderrReadHandle);
    });
    bool timeout = false;
    bool waitFailed = false;
    const uint64_t deadline = nowMilliseconds() + kMaxBuildMilliseconds;
    while (true) {
        const DWORD waitResult = WaitForSingleObject(process.hProcess, 50);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult == WAIT_FAILED) { waitFailed = true; break; }
        if (nowMilliseconds() >= deadline) {
            timeout = true;
            std::lock_guard<std::mutex> lock(job->mutex);
            job->terminateRequested = true;
            if (job->processJob) TerminateJobObject(job->processJob, 124);
            else TerminateProcess(job->process, 124);
            break;
        }
    }
    if (stdoutReader.joinable()) stdoutReader.join();
    if (stderrReader.joinable()) stderrReader.join();
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (processJob) CloseHandle(processJob);
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->process = nullptr;
        job->processJob = nullptr;
        job->snapshot.processExitCode = static_cast<int32_t>(exitCode);
    }
    if (timeout) { setFailure(job, GX_BUILD_ERROR_BUILD_TIMEOUT, static_cast<int32_t>(exitCode)); return false; }
    if (waitFailed) { setFailure(job, GX_BUILD_ERROR_PROCESS_FAILED, static_cast<int32_t>(exitCode)); return false; }
    if (exitCode != 0) { setFailure(job, GX_BUILD_ERROR_PROCESS_FAILED, static_cast<int32_t>(exitCode)); return false; }
    return true;
}
#else
bool runProcess(const std::shared_ptr<BuildJob>& job) {
    setFailure(job, GX_BUILD_ERROR_TOOLCHAIN_NOT_FOUND);
    return false;
}
#endif

struct Sha256 {
    uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint64_t length = 0;
    unsigned char buffer[64] = {};
    uint32_t used = 0;

    static uint32_t rotate(uint32_t value, uint32_t count) { return (value >> count) | (value << (32u - count)); }
    static uint32_t readWord(const unsigned char* bytes) { return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3]; }
    static void writeWord(unsigned char* bytes, uint32_t value) { bytes[0] = static_cast<unsigned char>(value >> 24); bytes[1] = static_cast<unsigned char>(value >> 16); bytes[2] = static_cast<unsigned char>(value >> 8); bytes[3] = static_cast<unsigned char>(value); }
    void transform(const unsigned char* bytes) {
        static const uint32_t k[64] = {0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        uint32_t words[64] = {};
        for (uint32_t i = 0; i < 16; ++i) words[i] = readWord(bytes + i * 4);
        for (uint32_t i = 16; i < 64; ++i) { uint32_t s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^ (words[i - 15] >> 3); uint32_t s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^ (words[i - 2] >> 10); words[i] = words[i - 16] + s0 + words[i - 7] + s1; }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (uint32_t i = 0; i < 64; ++i) { uint32_t s1=rotate(e,6)^rotate(e,11)^rotate(e,25); uint32_t choose=(e&f)^((~e)&g); uint32_t temp1=h+s1+choose+k[i]+words[i]; uint32_t s0=rotate(a,2)^rotate(a,13)^rotate(a,22); uint32_t majority=(a&b)^(a&c)^(b&c); uint32_t temp2=s0+majority; h=g;g=f;f=e;e=d+temp1;d=c;c=b;b=a;a=temp1+temp2; }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    void update(const unsigned char* bytes, size_t count) { length += count; while (count > 0) { const uint32_t take = std::min<uint32_t>(static_cast<uint32_t>(count), 64u - used); std::copy(bytes, bytes + take, buffer + used); used += take; bytes += take; count -= take; if (used == 64) { transform(buffer); used = 0; } } }
    std::string finish() { const uint64_t bitLength = length * 8u; buffer[used++] = 0x80; while (used != 56) { if (used == 64) { transform(buffer); used = 0; } buffer[used++] = 0; } for (int i = 7; i >= 0; --i) buffer[used++] = static_cast<unsigned char>(bitLength >> (i * 8)); transform(buffer); std::ostringstream output; output << std::hex; for (uint32_t value : state) output << std::setw(8) << std::setfill('0') << value; return output.str(); }
};

bool validateArtifact(const std::shared_ptr<BuildJob>& job) {
    std::filesystem::path artifact(job->artifactAbsolute);
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(artifact, ec);
    if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) { setFailure(job, GX_BUILD_ERROR_ARTIFACT_MISSING); return false; }
    const uintmax_t fileSize = std::filesystem::file_size(artifact, ec);
    if (ec || fileSize == 0 || fileSize > kMaxArtifactBytes) { setFailure(job, GX_BUILD_ERROR_ARTIFACT_INVALID); return false; }
    std::ifstream input(artifact, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<size_t>(fileSize));
    input.read(reinterpret_cast<char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    if (!input) { setFailure(job, GX_BUILD_ERROR_ARTIFACT_INVALID); return false; }
    if (bytes.size() < 64 || bytes[0] != 0x7F || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F' || bytes[4] != 2 || bytes[5] != 1) { setFailure(job, GX_BUILD_ERROR_ARTIFACT_INVALID); return false; }
    const uint16_t type = static_cast<uint16_t>(bytes[16] | (bytes[17] << 8));
    const uint16_t machine = static_cast<uint16_t>(bytes[18] | (bytes[19] << 8));
    if (machine != 62) { setFailure(job, GX_BUILD_ERROR_ARTIFACT_WRONG_ARCHITECTURE); return false; }
    if (type != 2) { setFailure(job, GX_BUILD_ERROR_ARTIFACT_INVALID); return false; }
    const uint64_t shoff = *reinterpret_cast<const uint64_t*>(&bytes[40]);
    const uint16_t shentsize = static_cast<uint16_t>(bytes[58] | (bytes[59] << 8));
    const uint16_t shnum = static_cast<uint16_t>(bytes[60] | (bytes[61] << 8));
    const uint16_t shstrndx = static_cast<uint16_t>(bytes[62] | (bytes[63] << 8));
    bool entryPoint = false;
    if (shentsize >= 64 && shnum > 0 && shoff <= bytes.size() && shnum <= (bytes.size() - shoff) / shentsize && shstrndx < shnum) {
        const unsigned char* shstr = &bytes[shoff + static_cast<uint64_t>(shstrndx) * shentsize];
        (void)shstr;
        for (uint16_t i = 0; i < shnum; ++i) {
            const unsigned char* section = &bytes[shoff + static_cast<uint64_t>(i) * shentsize];
            const uint32_t sectionType = *reinterpret_cast<const uint32_t*>(section + 4);
            if (sectionType != 2 && sectionType != 11) continue;
            const uint64_t offset = *reinterpret_cast<const uint64_t*>(section + 24);
            const uint64_t size = *reinterpret_cast<const uint64_t*>(section + 32);
            const uint64_t entrySize = *reinterpret_cast<const uint64_t*>(section + 56);
            const uint32_t link = *reinterpret_cast<const uint32_t*>(section + 40);
            if (entrySize < 24 || link >= shnum || offset > bytes.size() || size > bytes.size() - offset || size % entrySize != 0) continue;
            const unsigned char* stringSection = &bytes[shoff + static_cast<uint64_t>(link) * shentsize];
            const uint64_t stringOffset = *reinterpret_cast<const uint64_t*>(stringSection + 24);
            const uint64_t stringSize = *reinterpret_cast<const uint64_t*>(stringSection + 32);
            if (stringOffset > bytes.size() || stringSize > bytes.size() - stringOffset) continue;
            for (uint64_t symbolOffset = 0; symbolOffset < size; symbolOffset += entrySize) {
                const unsigned char* symbol = &bytes[offset + symbolOffset];
                const uint32_t nameOffset = *reinterpret_cast<const uint32_t*>(symbol);
                if (nameOffset >= stringSize) continue;
                const char* name = reinterpret_cast<const char*>(&bytes[stringOffset + nameOffset]);
                const size_t remaining = static_cast<size_t>(stringSize - nameOffset);
                size_t nameLength = 0;
                while (nameLength < remaining && name[nameLength] != '\0') ++nameLength;
                if (std::string(name, nameLength) == "gx_main") { entryPoint = true; break; }
            }
            if (entryPoint) break;
        }
    }
    Sha256 sha;
    sha.update(&bytes[0], bytes.size());
    std::lock_guard<std::mutex> lock(job->mutex);
    job->snapshot.artifactSize = fileSize;
    job->snapshot.artifactValid = 1;
    job->snapshot.artifactEntryPoint = entryPoint ? 1u : 0u;
    copyBounded(job->snapshot.artifactPath, sizeof(job->snapshot.artifactPath), job->request.expectedArtifact);
    copyBounded(job->snapshot.artifactSha256, sizeof(job->snapshot.artifactSha256), sha.finish());
    copyBounded(job->snapshot.artifactArchitecture, sizeof(job->snapshot.artifactArchitecture), "amd64");
    if (!entryPoint) { job->snapshot.state = GX_BUILD_FAILED; job->snapshot.errorCode = GX_BUILD_ERROR_ENTRY_POINT_MISSING; copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(GX_BUILD_ERROR_ENTRY_POINT_MISSING)); job->completed = true; job->endTicks = nowMilliseconds(); return false; }
    job->snapshot.state = GX_BUILD_SUCCEEDED;
    job->snapshot.errorCode = GX_BUILD_ERROR_NONE;
    job->completed = true;
    job->endTicks = nowMilliseconds();
    job->snapshot.elapsedMilliseconds = job->endTicks >= job->startTicks ? job->endTicks - job->startTicks : 0;
    return true;
}

void copySnapshot(const std::shared_ptr<BuildJob>& job, gx_build_snapshot* output) {
    std::lock_guard<std::mutex> lock(job->mutex);
    *output = job->snapshot;
    output->size = sizeof(gx_build_snapshot);
    output->version = GX_BUILD_API_VERSION;
    output->handle = job->handle;
    output->outputCount = static_cast<uint32_t>(job->lines.size());
    output->outputTruncated = job->outputTruncated ? 1u : 0u;
    for (uint32_t i = 0; i < output->outputCount; ++i) {
        output->output[i].stream = job->lines[i].stream;
        copyBounded(output->output[i].text, sizeof(output->output[i].text), job->lines[i].text);
    }
    if (job->endTicks != 0) output->elapsedMilliseconds = job->endTicks >= job->startTicks ? job->endTicks - job->startTicks : 0;
    else output->elapsedMilliseconds = nowMilliseconds() >= job->startTicks ? nowMilliseconds() - job->startTicks : 0;
}

void workerMain(const std::shared_ptr<BuildJob>& job) {
    if (!runProcess(job)) return;
    setState(job, GX_BUILD_VALIDATING_ARTIFACT);
    validateArtifact(job);
    std::lock_guard<std::mutex> lock(job->mutex);
    if (job->outputTruncated && job->snapshot.state == GX_BUILD_SUCCEEDED) {
        job->snapshot.errorCode = GX_BUILD_ERROR_OUTPUT_TRUNCATED;
        copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(GX_BUILD_ERROR_OUTPUT_TRUNCATED));
    }
}

std::shared_ptr<BuildJob> findJob(gx_build_handle handle) {
    std::lock_guard<std::mutex> lock(g_jobsMutex);
    auto it = g_jobs.find(handle);
    return it == g_jobs.end() ? std::shared_ptr<BuildJob>() : it->second;
}

} // namespace

gx_result Start(NativeAppRuntimeContext& context, const NativeBuildRequest& request, gx_build_handle* outHandle) {
    if (!outHandle) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    if (request.projectRoot.empty() || request.projectId.empty()) return GX_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_jobsMutex);
    for (const auto& pair : g_jobs) {
        std::lock_guard<std::mutex> jobLock(pair.second->mutex);
        if (!pair.second->completed) return GX_ERROR_BUSY;
    }
    auto job = std::make_shared<BuildJob>();
    job->handle = g_nextHandle.fetch_add(1);
    job->runtimeId = context.runtimeId;
    job->request = request;
    job->startTicks = nowMilliseconds();
    job->snapshot.size = sizeof(gx_build_snapshot);
    job->snapshot.version = GX_BUILD_API_VERSION;
    job->snapshot.handle = job->handle;
    job->snapshot.state = GX_BUILD_PREPARING;
    std::filesystem::path root(request.projectRoot);
    uint32_t validationError = GX_BUILD_ERROR_NONE;
    if (!root.is_absolute() || pathHasSymlinkComponent(root) || !validateProjectAndManifest(request, root, job->artifactAbsolute, validationError)) {
        job->snapshot.state = GX_BUILD_FAILED;
        job->snapshot.errorCode = validationError == GX_BUILD_ERROR_NONE ? GX_BUILD_ERROR_INVALID_PROJECT_ROOT : validationError;
        copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(job->snapshot.errorCode));
        job->completed = true;
    } else if (!resolveSdkInclude(job->sdkInclude)) {
        job->snapshot.state = GX_BUILD_FAILED; job->snapshot.errorCode = GX_BUILD_ERROR_SDK_NOT_FOUND; copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(job->snapshot.errorCode)); job->completed = true;
    } else if (!resolveToolchainRoot(job->toolchainRoot)) {
        job->snapshot.state = GX_BUILD_FAILED; job->snapshot.errorCode = GX_BUILD_ERROR_TOOLCHAIN_NOT_FOUND; copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(job->snapshot.errorCode)); job->completed = true;
    } else {
        std::error_code ec;
        const std::filesystem::path artifact(job->artifactAbsolute);
        const std::filesystem::file_status status = std::filesystem::symlink_status(artifact, ec);
        if (!ec && std::filesystem::is_directory(status)) { job->snapshot.state = GX_BUILD_FAILED; job->snapshot.errorCode = GX_BUILD_ERROR_INVALID_REQUEST; copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(job->snapshot.errorCode)); job->completed = true; }
        else if (!ec && std::filesystem::is_symlink(status)) { job->snapshot.state = GX_BUILD_FAILED; job->snapshot.errorCode = GX_BUILD_ERROR_INVALID_PROJECT_ROOT; copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(job->snapshot.errorCode)); job->completed = true; }
        else if (!ec && std::filesystem::exists(status) && !std::filesystem::remove(artifact, ec)) { job->snapshot.state = GX_BUILD_FAILED; job->snapshot.errorCode = GX_BUILD_ERROR_INVALID_REQUEST; copyBounded(job->snapshot.errorMessage, sizeof(job->snapshot.errorMessage), errorName(job->snapshot.errorCode)); job->completed = true; }
    }
    g_jobs[job->handle] = job;
    *outHandle = job->handle;
    if (!job->completed) job->worker = std::thread(workerMain, job);
    return GX_OK;
}

gx_result Poll(NativeAppRuntimeContext&, gx_build_handle handle, gx_build_snapshot* outSnapshot) {
    if (!outSnapshot) return GX_ERROR_INVALID_ARGUMENT;
    auto job = findJob(handle);
    if (!job) return GX_ERROR_FAILED;
    copySnapshot(job, outSnapshot);
    return GX_OK;
}

gx_result Release(NativeAppRuntimeContext& context, gx_build_handle handle) {
    auto job = findJob(handle);
    if (!job || job->runtimeId != context.runtimeId) return GX_ERROR_PERMISSION_DENIED;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (!job->completed) return GX_ERROR_FAILED;
    }
    if (job->worker.joinable()) job->worker.join();
    std::lock_guard<std::mutex> lock(g_jobsMutex);
    g_jobs.erase(handle);
    return GX_OK;
}

void CancelForRuntime(uint64_t runtimeId) {
    std::vector<std::shared_ptr<BuildJob>> matches;
    {
        std::lock_guard<std::mutex> lock(g_jobsMutex);
        for (const auto& pair : g_jobs) if (pair.second->runtimeId == runtimeId) matches.push_back(pair.second);
    }
    for (const auto& job : matches) {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->completed) continue;
        job->terminateRequested = true;
#if defined(_WIN32)
        if (job->processJob) TerminateJobObject(job->processJob, 125);
        else if (job->process) TerminateProcess(job->process, 125);
#endif
    }
}

} // namespace NativeBuildService
} // namespace apps
} // namespace gxos
