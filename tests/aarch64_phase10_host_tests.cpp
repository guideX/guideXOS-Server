#include "app_launch_resolver.h"
#include "app_manifest_loader.h"
#include "app_payload_resolver.h"
#include "app_registry.h"
#include "elf_validator.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

void put64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

std::vector<uint8_t> elf(uint16_t machine)
{
    std::vector<uint8_t> bytes(256, 0);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    put16(bytes, 16, 2); put16(bytes, 18, machine); put32(bytes, 20, 1);
    put64(bytes, 24, 0x1000); put64(bytes, 32, 64);
    put16(bytes, 52, 64); put16(bytes, 54, 56); put16(bytes, 56, 1);
    put32(bytes, 64, 1); put32(bytes, 68, 5); put64(bytes, 72, 0);
    put64(bytes, 80, 0x1000); put64(bytes, 88, 0x1000); put64(bytes, 96, 0x100);
    put64(bytes, 104, 0x100); put64(bytes, 112, 0x1000);
    return bytes;
}

bool write_file(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

bool write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path);
    file << text;
    return file.good();
}

std::string manifest(const std::string& entries, const std::string& supported = "\"amd64\", \"arm64\"")
{
    return std::string("{\n") +
        "  \"schemaVersion\": 1,\n" +
        "  \"id\": \"com.guidexos.phase10.hostproof\",\n" +
        "  \"displayName\": \"Phase 10 Host Proof\",\n" +
        "  \"version\": \"0.1.0\",\n" +
        "  \"kind\": \"NativeElf\",\n" +
        "  \"supportedArchitectures\": [" + supported + "],\n" +
        "  \"entries\": [" + entries + "],\n" +
        "  \"permissions\": []\n" +
        "}\n";
}

std::string entry(const char* architecture, const char* path)
{
    return std::string("{\"architecture\":\"") + architecture +
        "\",\"path\":\"" + path +
        "\",\"entryPoint\":\"gx_main\",\"abi\":\"guidexos-c-abi-v1\",\"runtime\":\"native-elf\"}";
}

bool check(bool value, const char* message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    using namespace gxos::apps;
    const fs::path root = fs::temp_directory_path() / "guidexos-phase10-host-controls";
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    fs::create_directories(root / "bin/amd64");
    fs::create_directories(root / "bin/arm64");

    bool pass = true;
    pass &= check(write_file(root / "bin/amd64/proof.elf", elf(62)), "write AMD64 fixture");
    pass &= check(write_file(root / "bin/arm64/proof.elf", elf(183)), "write ARM64 fixture");

    const std::string packageJson = manifest(
        entry("amd64", "bin/amd64/proof.elf") + "," + entry("arm64", "bin/arm64/proof.elf"));
    const AppManifestLoadResult loaded = AppManifestLoader::LoadFromString(packageJson);
    pass &= check(loaded.valid, "load one shared multiarch manifest");
    if (!loaded.valid) {
        for (const std::string& error : loaded.errors) std::cerr << error << '\n';
        return 1;
    }

    const AppPayloadResolution arm = AppPayloadResolver::Resolve(
        loaded.manifest, root, NativeArchitecture::ARM64);
    const AppPayloadResolution amd = AppPayloadResolver::Resolve(
        loaded.manifest, root, NativeArchitecture::AMD64);
    pass &= check(arm.success && arm.relativePath == "bin/arm64/proof.elf" && arm.architectureName == "arm64",
                  "ARM64 resolver selects the ARM64 payload");
    pass &= check(amd.success && amd.relativePath == "bin/amd64/proof.elf" && amd.architectureName == "amd64",
                  "AMD64 resolver selects the AMD64 payload");
    pass &= check(arm.payloadPath != amd.payloadPath, "architecture payloads are distinct files");

    auto read = [](const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    };
    const ElfValidationResult armElf = ElfValidator::Validate(read(arm.payloadPath), "arm64");
    const ElfValidationResult amdElf = ElfValidator::Validate(read(amd.payloadPath), "amd64");
    pass &= check(armElf.valid && armElf.machineType == "EM_AARCH64", "ARM64 ELF machine validates");
    pass &= check(amdElf.valid && amdElf.machineType == "EM_X86_64", "AMD64 ELF machine validates");

    for (int i = 0; i < 100; ++i) {
        pass &= check(AppPayloadResolver::Resolve(loaded.manifest, root, "arm64").success,
                      "ARM64 resolver durability");
        if (!pass) break;
    }
    std::error_code removeError;
    fs::remove(root / "bin/arm64/proof.elf", removeError);
    const AppPayloadResolution missing = AppPayloadResolver::Resolve(loaded.manifest, root, "arm64");
    pass &= check(!missing.success && missing.status == AppPayloadResolutionStatus::PayloadNotFound,
                  "missing ARM64 payload is rejected without AMD64 fallback");
    pass &= check(AppPayloadResolver::Resolve(loaded.manifest, root, "riscv64").status ==
                      AppPayloadResolutionStatus::UnsupportedArchitecture,
                  "unknown architecture is rejected deterministically");

    write_file(root / "bin/arm64/proof.elf", elf(62));
    const AppPayloadResolution wrongDirectory = AppPayloadResolver::Resolve(loaded.manifest, root, "arm64");
    const ElfValidationResult wrongElf = ElfValidator::Validate(read(wrongDirectory.payloadPath), "arm64");
    pass &= check(wrongDirectory.success && !wrongElf.valid && wrongElf.machineType == "EM_X86_64",
                  "wrong ELF machine is rejected after path selection");

    const AppManifestLoadResult traversal = AppManifestLoader::LoadFromString(
        manifest(entry("arm64", "bin/arm64/../proof.elf"), "\"arm64\""));
    pass &= check(!traversal.valid, "traversal payload path is rejected");
    const AppManifestLoadResult malformed = AppManifestLoader::LoadFromString("{ \"id\": ");
    pass &= check(!malformed.valid && !malformed.errors.empty(), "malformed manifest is rejected");
    const AppManifestLoadResult oversized = AppManifestLoader::LoadFromString(
        manifest(entry("arm64-identifier-too-long", "bin/arm64/proof.elf"), "\"arm64-identifier-too-long\""));
    pass &= check(!oversized.valid, "oversized architecture field is rejected");
    const AppManifestLoadResult duplicate = AppManifestLoader::LoadFromString(
        manifest(entry("arm64", "bin/arm64/proof.elf") + "," + entry("aarch64", "bin/arm64/proof.elf"), "\"arm64\""));
    pass &= check(!duplicate.valid, "duplicate canonical architecture declaration is rejected");

    const fs::path legacy = root / "legacy";
    fs::create_directories(legacy / "bin/amd64");
    write_file(legacy / "bin/amd64/legacy.elf", elf(62));
    const AppManifestLoadResult legacyManifest = AppManifestLoader::LoadFromString(
        manifest(entry("amd64", "bin/amd64/legacy.elf"), "\"amd64\""));
    const AppPayloadResolution legacyResolution = AppPayloadResolver::Resolve(
        legacyManifest.manifest, legacy, "amd64");
    pass &= check(legacyManifest.valid && legacyResolution.success &&
                      legacyResolution.relativePath == "bin/amd64/legacy.elf",
                  "legacy explicit AMD64 package remains resolvable");

    const fs::path canonical = root / "canonical";
    fs::create_directories(canonical / "bin/amd64");
    fs::create_directories(canonical / "bin/arm64");
    write_file(canonical / "bin/amd64/canonical.elf", elf(62));
    write_file(canonical / "bin/arm64/canonical.elf", elf(183));
    const std::string canonicalJson =
        "{\"schemaVersion\":1,\"id\":\"com.guidexos.phase10.canonical\","
        "\"displayName\":\"Canonical\",\"version\":\"1\",\"kind\":\"NativeElf\","
        "\"executable\":\"canonical.elf\",\"supportedArchitectures\":[\"amd64\",\"arm64\"],"
        "\"entries\":[],\"permissions\":[]}";
    const AppManifestLoadResult canonicalManifest = AppManifestLoader::LoadFromString(canonicalJson);
    const AppPayloadResolution canonicalArm = AppPayloadResolver::Resolve(
        canonicalManifest.manifest, canonical, "arm64");
    pass &= check(canonicalManifest.valid && canonicalArm.success &&
                      canonicalArm.relativePath == "bin/arm64/canonical.elf",
                  "canonical executable convention resolves beneath package root");

    pass &= check(write_text(root / "app.json", packageJson), "write discovery manifest");
    pass &= check(write_text(canonical / "app.json", canonicalJson), "write canonical discovery manifest");
    AppRegistry registry;
    const AppScanResult scan = registry.Scan({ { AppSourceKind::Package, root } });
    pass &= check(scan.registeredAppCount == 2, "two fixture packages register independently");
    const AppScanResult oneScan = registry.Scan({ { AppSourceKind::Package, canonical } });
    pass &= check(oneScan.registeredAppCount == 1 && oneScan.registeredApps.size() == 1,
                  "one multiarch package produces one discovery entry");

    if (!pass) return 1;
    std::cout << "[phase10-host] ARM64 resolver: PASS path=bin/arm64/proof.elf machine=EM_AARCH64\n";
    std::cout << "[phase10-host] AMD64 resolver: PASS path=bin/amd64/proof.elf machine=EM_X86_64\n";
    std::cout << "[phase10-host] negative controls: PASS missing,unknown,wrong-machine,traversal,malformed,oversized,duplicate,legacy\n";
    std::cout << "[phase10-host] single discovery entry: PASS\n";
    std::cout << "AARCH64_PHASE10_HOST_CONTROLS_PASS\n";
    fs::remove_all(root, cleanupError);
    return 0;
}
