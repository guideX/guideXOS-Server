#define KERNEL_NO_GLOBAL_ARCHITECTURE_DETECTOR_ALIASES
#include "kernel/core/include/kernel/architecture_detector.h"
#include "gxapp_container.h"
#include "gxapp_loader.h"
#include <cstdlib>
#include <cstring>

namespace gxos {
namespace {

const char* const UNSUPPORTED_ARCH_ERROR = "This application does not support your CPU architecture.";

static bool parseUnsignedAddress(const std::string& text, uintptr_t& value){
    if (text.empty()) return false;
    const char* s = text.c_str();
    int base = 10;
    if (text.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    }
    char* end = nullptr;
    unsigned long long parsed = std::strtoull(s, &end, base);
    if (!end || *end != '\0') return false;
    value = static_cast<uintptr_t>(parsed);
    return true;
}

static bool resolveEntryAddress(const std::vector<uint8_t>& binary, const std::string& entryPoint, uintptr_t& address){
    if (entryPoint == "main" || entryPoint == "_start") {
        if (binary.empty()) return false;
        address = reinterpret_cast<uintptr_t>(binary.data());
        return true;
    }
    return parseUnsignedAddress(entryPoint, address);
}

static void copyToExecutableMemory(const std::vector<uint8_t>& binary, std::vector<uint8_t>& memory){
    memory = binary;
}

} // namespace

int GXAppLoader::currentArchitecture(){
    return static_cast<int>(kernel::ArchitectureDetector::GetArchitecture());
}

bool GXAppLoader::LoadBinary(const std::string& packagePath, std::vector<uint8_t>& binary, std::string& entryPoint, std::string& error){
    gxos::CpuArchitecture architecture = static_cast<gxos::CpuArchitecture>(currentArchitecture());
    if (architecture == CpuArchitecture::Unknown) {
        error = UNSUPPORTED_ARCH_ERROR;
        return false;
    }

    GXApp app = GXAppContainer::Open(packagePath);
    if (!app.IsValid()) {
        error = app.GetLastError();
        return false;
    }

    if (!app.HasBinary(architecture)) {
        error = UNSUPPORTED_ARCH_ERROR;
        return false;
    }

    binary = app.GetBinary(architecture);
    entryPoint = app.GetEntryPoint(architecture);
    if (binary.empty()) {
        error = "The application binary is empty.";
        return false;
    }

    return true;
}

bool GXAppLoader::Execute(const std::string& packagePath, std::string& error){
    std::vector<uint8_t> binary;
    std::string entryPoint;
    if (!LoadBinary(packagePath, binary, entryPoint, error)) {
        return false;
    }

    std::vector<uint8_t> loadedImage;
    copyToExecutableMemory(binary, loadedImage);

    uintptr_t entryAddress = 0;
    if (!resolveEntryAddress(loadedImage, entryPoint, entryAddress)) {
        error = "Unable to resolve application entry point.";
        return false;
    }

    using AppEntry = int (*)();
    AppEntry entry = reinterpret_cast<AppEntry>(entryAddress);
    entry();
    return true;
}

} // namespace gxos
