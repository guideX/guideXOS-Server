#include "native_build_service.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace gxos::apps;

namespace {
static bool writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}

static std::string psSingleQuote(const std::string& value)
{
    std::string quoted = "'";
    for (char c : value) {
        quoted.push_back(c);
        if (c == '\'') quoted.push_back('\'');
    }
    quoted.push_back('\'');
    return quoted;
}
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::filesystem::path repoRoot = std::filesystem::absolute(argv[1]);
    const std::filesystem::path fixtureRoot = std::filesystem::absolute(argv[2]);
    const std::filesystem::path artifact = repoRoot / "Apps/HelloWorld/bin/amd64/helloworld.elf";
    assert(std::filesystem::is_regular_file(artifact));
    std::filesystem::create_directories(fixtureRoot / "app");
    std::filesystem::create_directories(fixtureRoot / "build/bin/amd64");
    assert(writeText(fixtureRoot / "guidexos.project",
        "{\"formatVersion\":1,\"projectId\":\"com.guidexos.comp3.hosted.fixture\","
        "\"projectKind\":\"native-gui-application\","
        "\"defaultTargetProfile\":\"guidexos.amd64.hosted.native\","
        "\"outputName\":\"comp3-hosted-fixture\"}"));
    assert(writeText(fixtureRoot / "app/app.json",
        "{\"id\":\"com.guidexos.comp3.hosted.fixture\",\"displayName\":\"COMP3 hosted fixture\","
        "\"kind\":\"native-gui-application\",\"architecture\":\"amd64\","
        "\"path\":\"bin/amd64/comp3-hosted-fixture.elf\",\"entryPoint\":\"gx_main\","
        "\"abi\":\"guidexos-c-abi-v1\"}"));
    const std::string script =
        "param([string]$SdkInclude, [string]$ToolchainRoot, [string]$Configuration)\r\n"
        "# GUIDEXOS_NATIVE_BUILD_RECIPE_V1\r\n"
        "Write-Output 'COMP3_FIXTURE_STARTED'\r\n"
        "Start-Sleep -Seconds 2\r\n"
        "$artifact = Join-Path $PSScriptRoot 'build/bin/amd64/comp3-hosted-fixture.elf'\r\n"
        "Copy-Item -LiteralPath " + psSingleQuote(artifact.string()) + " -Destination $artifact -Force\r\n"
        "Write-Output 'COMP3_FIXTURE_OUTPUT_READY'\r\n";
    assert(writeText(fixtureRoot / "build.ps1", script));

    NativeAppRuntimeContext context;
    context.runtimeId = 0xC03u;
    NativeBuildService::NativeBuildRequest request;
    request.projectRoot = fixtureRoot.string();
    request.projectId = "com.guidexos.comp3.hosted.fixture";
    request.projectKind = "native-gui-application";
    request.targetProfile = "guidexos.amd64.hosted.native";
    request.buildSystem = "guidexos-native-build-script-v1";
    request.buildScript = "build.ps1";
    request.expectedArtifact = "build/bin/amd64/comp3-hosted-fixture.elf";
    request.configuration = "Debug";

    gx_build_handle handle = 0;
    const auto start = std::chrono::steady_clock::now();
    assert(NativeBuildService::Start(context, request, &handle) == gxos::apps::GX_OK && handle != 0);
    const auto startElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(startElapsed < 1500);

    gx_build_handle competingHandle = 0;
    assert(NativeBuildService::Start(context, request, &competingHandle) == gxos::apps::GX_ERROR_BUSY && competingHandle == 0);

    bool observedActive = false;
    bool observedOutputWhileActive = false;
    bool completed = false;
    gx_build_snapshot snapshot = {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!completed && std::chrono::steady_clock::now() < deadline) {
        snapshot = {};
        assert(NativeBuildService::Poll(context, handle, &snapshot) == gxos::apps::GX_OK);
        completed = snapshot.state == GX_BUILD_SUCCEEDED || snapshot.state == GX_BUILD_FAILED ||
            snapshot.state == GX_BUILD_CANCELLED;
        if (!completed) {
            observedActive = observedActive || snapshot.state == GX_BUILD_RUNNING ||
                snapshot.state == GX_BUILD_PREPARING;
            for (uint32_t i = 0; i < snapshot.outputCount; ++i)
                if (std::string(snapshot.output[i].text).find("COMP3_FIXTURE_STARTED") != std::string::npos)
                    observedOutputWhileActive = true;
        }
        if (!completed) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(completed && observedActive && observedOutputWhileActive);
    assert(snapshot.state == GX_BUILD_SUCCEEDED && snapshot.artifactValid && snapshot.artifactEntryPoint &&
           std::string(snapshot.artifactArchitecture) == "amd64" &&
           std::string(snapshot.artifactSha256).size() == 64 && snapshot.elapsedMilliseconds >= 1500);
    bool sawCompletionOutput = false;
    for (uint32_t i = 0; i < snapshot.outputCount; ++i)
        if (std::string(snapshot.output[i].text).find("COMP3_FIXTURE_OUTPUT_READY") != std::string::npos)
            sawCompletionOutput = true;
    assert(sawCompletionOutput);
    assert(NativeBuildService::Release(context, handle) == gxos::apps::GX_OK);
    assert(NativeBuildService::Poll(context, handle, &snapshot) == gxos::apps::GX_ERROR_FAILED);
    std::puts("NativeBuildService hosted worker responsiveness, live output, admission and completion PASS");
    return 0;
}
