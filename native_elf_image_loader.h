#pragma once

#include "native_elf_launch_pipeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct NativeElfSegment {
    uint64_t virtualAddress = 0;
    uint64_t memorySize = 0;
    uint64_t fileSize = 0;
    uint64_t fileOffset = 0;
    uint32_t flags = 0;
    uint64_t alignment = 0;
    std::vector<uint8_t> data;
};

struct NativeElfImage {
    bool success = false;
    std::string appId;
    std::string architecture;
    std::string sourcePath;
    uint64_t entryPointVirtualAddress = 0;
    uint64_t preferredBaseAddress = 0;
    uint64_t imageSize = 0;
    std::vector<NativeElfSegment> loadedSegments;
    std::vector<std::string> requiredPermissions;
    bool isPositionIndependent = false;
    bool isExecutable = false;
    bool hasInterpreter = false;
    std::vector<std::string> diagnostics;
};

class NativeElfImageLoader {
public:
    static NativeElfImage LoadImage(const NativeElfLaunchResult& launchResult);

private:
    static void LogImage(const NativeElfImage& image);
};

} // namespace apps
} // namespace gxos
