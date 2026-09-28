#include "settings_system_information.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

namespace {

template <typename Function>
Function kernelFunction(const char* name)
{
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    if (!kernel) return nullptr;
    const FARPROC address = GetProcAddress(kernel, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address), "Windows function pointer size mismatch");
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

std::string trimValue(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    return value.substr(begin);
}

std::string hostedArchitecture(WORD architecture)
{
    switch (architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return "amd64";
    case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
    case PROCESSOR_ARCHITECTURE_ARM: return "arm";
#ifdef PROCESSOR_ARCHITECTURE_ARM64
    case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
#endif
    case PROCESSOR_ARCHITECTURE_IA64: return "ia64";
    default: return "unknown";
    }
}

std::string hostedProcessorName()
{
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};

    char value[512]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const LONG result = RegQueryValueExA(key, "ProcessorNameString", nullptr, &type,
        reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    value[sizeof(value) - 1] = '\0';
    return trimValue(value);
}

void queryInstalledMemory(uint64_t& bytes)
{
    using QueryInstalledMemory = BOOL(WINAPI*)(PULONGLONG);
    const QueryInstalledMemory query = kernelFunction<QueryInstalledMemory>("GetPhysicallyInstalledSystemMemory");
    if (!query) return;
    ULONGLONG kilobytes = 0;
    if (!query(&kilobytes) || kilobytes > std::numeric_limits<uint64_t>::max() / 1024ull) return;
    bytes = static_cast<uint64_t>(kilobytes) * 1024ull;
}

std::string queryFirmware()
{
    // Resolve dynamically so older hosted Windows environments still build
    // and report unavailable when the API is not present.
    using QueryFirmwareType = BOOL(WINAPI*)(int*);
    const QueryFirmwareType query = kernelFunction<QueryFirmwareType>("GetFirmwareType");
    if (!query) return {};
    int type = 0; // FIRMWARE_TYPE: 1 = BIOS, 2 = UEFI.
    if (!query(&type)) return {};
    if (type == 2) return "uefi";
    if (type == 1) return "bios";
    return {};
}

} // namespace
#endif

namespace gxos {
namespace apps {
namespace settings {

HostedSystemInformation readHostedSystemInformation()
{
    HostedSystemInformation result;
#if defined(_WIN32)
    result.runtime = "Hosted on Windows";

    char computerName[256]{};
    DWORD computerNameBytes = sizeof(computerName);
    if (GetComputerNameA(computerName, &computerNameBytes)) result.computerName = computerName;
    result.processorName = hostedProcessorName();
    result.firmware = queryFirmware();
    queryInstalledMemory(result.installedMemoryBytes);

    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
    result.architecture = hostedArchitecture(systemInfo.wProcessorArchitecture);
    result.logicalProcessorCount = systemInfo.dwNumberOfProcessors;
#else
    result.runtime = "Hosted runtime details unavailable";
#endif
    return result;
}

} // namespace settings
} // namespace apps
} // namespace gxos
