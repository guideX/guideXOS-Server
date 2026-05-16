#include "native_elf_image_loader.h"

#include "fs.h"
#include "logger.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace gxos {
namespace apps {
namespace {

constexpr uint64_t kMaxNativeElfSize = 128ULL * 1024ULL * 1024ULL;
constexpr uint32_t kPtLoad = 1;
constexpr uint32_t kPtInterp = 3;
constexpr uint16_t kEtExec = 2;
constexpr uint16_t kEtDyn = 3;
constexpr uint8_t kElfClass32 = 1;
constexpr uint8_t kElfClass64 = 2;
constexpr uint8_t kElfDataLittleEndian = 1;
constexpr uint8_t kElfDataBigEndian = 2;

void addDiagnostic(NativeElfImage& image, const std::string& diagnostic) {
    image.diagnostics.push_back(diagnostic);
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
    std::ostringstream oss;
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) oss << "; ";
        oss << diagnostics[i];
    }
    return oss.str();
}

uint16_t readU16(const std::vector<uint8_t>& bytes, size_t offset, bool littleEndian) {
    if (littleEndian) return static_cast<uint16_t>(bytes[offset]) | (static_cast<uint16_t>(bytes[offset + 1]) << 8);
    return (static_cast<uint16_t>(bytes[offset]) << 8) | static_cast<uint16_t>(bytes[offset + 1]);
}

uint32_t readU32(const std::vector<uint8_t>& bytes, size_t offset, bool littleEndian) {
    if (littleEndian) {
        return static_cast<uint32_t>(bytes[offset]) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    }

    return (static_cast<uint32_t>(bytes[offset]) << 24) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
        static_cast<uint32_t>(bytes[offset + 3]);
}

uint64_t readU64(const std::vector<uint8_t>& bytes, size_t offset, bool littleEndian) {
    uint64_t value = 0;
    if (littleEndian) {
        for (int i = 7; i >= 0; --i) value = (value << 8) | bytes[offset + i];
    } else {
        for (int i = 0; i < 8; ++i) value = (value << 8) | bytes[offset + i];
    }
    return value;
}

bool checkedAdd(uint64_t left, uint64_t right, uint64_t& result) {
    if (left > std::numeric_limits<uint64_t>::max() - right) return false;
    result = left + right;
    return true;
}

bool checkedAddSize(size_t left, uint64_t right, size_t& result) {
    if (right > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false;
    size_t converted = static_cast<size_t>(right);
    if (left > std::numeric_limits<size_t>::max() - converted) return false;
    result = left + converted;
    return true;
}

bool validateMagic(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 4 && bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F';
}

struct ProgramHeaderView {
    uint32_t type = 0;
    uint32_t flags = 0;
    uint64_t offset = 0;
    uint64_t virtualAddress = 0;
    uint64_t fileSize = 0;
    uint64_t memorySize = 0;
    uint64_t alignment = 0;
};

ProgramHeaderView readProgramHeader(const std::vector<uint8_t>& bytes, size_t offset, uint8_t elfClass, bool littleEndian) {
    ProgramHeaderView header;
    if (elfClass == kElfClass64) {
        header.type = readU32(bytes, offset, littleEndian);
        header.flags = readU32(bytes, offset + 4, littleEndian);
        header.offset = readU64(bytes, offset + 8, littleEndian);
        header.virtualAddress = readU64(bytes, offset + 16, littleEndian);
        header.fileSize = readU64(bytes, offset + 32, littleEndian);
        header.memorySize = readU64(bytes, offset + 40, littleEndian);
        header.alignment = readU64(bytes, offset + 48, littleEndian);
    } else {
        header.type = readU32(bytes, offset, littleEndian);
        header.offset = readU32(bytes, offset + 4, littleEndian);
        header.virtualAddress = readU32(bytes, offset + 8, littleEndian);
        header.fileSize = readU32(bytes, offset + 16, littleEndian);
        header.memorySize = readU32(bytes, offset + 20, littleEndian);
        header.flags = readU32(bytes, offset + 24, littleEndian);
        header.alignment = readU32(bytes, offset + 28, littleEndian);
    }
    return header;
}

} // namespace

NativeElfImage NativeElfImageLoader::LoadImage(const NativeElfLaunchResult& launchResult) {
    NativeElfImage image;
    image.appId = launchResult.appId;
    image.architecture = launchResult.architecture;
    image.sourcePath = launchResult.elfPath;

    if (!launchResult.success) {
        addDiagnostic(image, "Native ELF launch result was not successful");
        LogImage(image);
        return image;
    }

    std::vector<uint8_t> elfBytes;
    FSResult readResult = FS::readAll(image.sourcePath, elfBytes, kMaxNativeElfSize);
    if (!readResult.success) {
        addDiagnostic(image, "Failed to read Native ELF file: " + readResult.message);
        LogImage(image);
        return image;
    }

    if (elfBytes.size() < 52 || !validateMagic(elfBytes)) {
        addDiagnostic(image, "Invalid ELF image header");
        LogImage(image);
        return image;
    }

    uint8_t elfClass = elfBytes[4];
    uint8_t dataEncoding = elfBytes[5];
    if (elfClass != kElfClass32 && elfClass != kElfClass64) {
        addDiagnostic(image, "Unsupported ELF class");
        LogImage(image);
        return image;
    }
    if (dataEncoding != kElfDataLittleEndian && dataEncoding != kElfDataBigEndian) {
        addDiagnostic(image, "Unsupported ELF byte order");
        LogImage(image);
        return image;
    }

    bool littleEndian = dataEncoding == kElfDataLittleEndian;
    uint16_t elfType = readU16(elfBytes, 16, littleEndian);
    image.entryPointVirtualAddress = elfClass == kElfClass64 ? readU64(elfBytes, 24, littleEndian) : readU32(elfBytes, 24, littleEndian);
    uint64_t programHeaderOffset = elfClass == kElfClass64 ? readU64(elfBytes, 32, littleEndian) : readU32(elfBytes, 28, littleEndian);
    uint16_t programHeaderEntrySize = readU16(elfBytes, elfClass == kElfClass64 ? 54 : 42, littleEndian);
    uint16_t programHeaderCount = readU16(elfBytes, elfClass == kElfClass64 ? 56 : 44, littleEndian);

    if (elfType == kEtDyn) {
        image.isPositionIndependent = true;
        image.preferredBaseAddress = 0;
    } else if (elfType != kEtExec) {
        addDiagnostic(image, "ELF type is not executable or position-independent executable");
        LogImage(image);
        return image;
    } else {
        image.isExecutable = true;
    }

    if (programHeaderOffset == 0 || programHeaderEntrySize == 0 || programHeaderCount == 0) {
        addDiagnostic(image, "ELF image has no program headers");
        LogImage(image);
        return image;
    }

    size_t programHeaderTableStart;
    if (!checkedAddSize(0, programHeaderOffset, programHeaderTableStart) || programHeaderTableStart > elfBytes.size()) {
        addDiagnostic(image, "Invalid ELF program header offset");
        LogImage(image);
        return image;
    }

    size_t programHeaderTableSize = static_cast<size_t>(programHeaderEntrySize) * static_cast<size_t>(programHeaderCount);
    if (programHeaderTableSize > elfBytes.size() - programHeaderTableStart) {
        addDiagnostic(image, "ELF program header table extends beyond file");
        LogImage(image);
        return image;
    }

    uint64_t minVirtualAddress = std::numeric_limits<uint64_t>::max();
    uint64_t maxVirtualAddress = 0;

    for (uint16_t i = 0; i < programHeaderCount; ++i) {
        size_t headerOffset = programHeaderTableStart + static_cast<size_t>(i) * static_cast<size_t>(programHeaderEntrySize);
        ProgramHeaderView header = readProgramHeader(elfBytes, headerOffset, elfClass, littleEndian);

        if (header.type == kPtInterp) {
            image.hasInterpreter = true;
            addDiagnostic(image, "PT_INTERP present; dynamic loader is not supported yet");
            continue;
        }

        if (header.type != kPtLoad) continue;
        if (header.memorySize == 0) continue;
        if (header.fileSize > header.memorySize) {
            addDiagnostic(image, "PT_LOAD segment file size exceeds memory size");
            LogImage(image);
            return image;
        }

        size_t segmentStart;
        size_t segmentEnd;
        if (!checkedAddSize(0, header.offset, segmentStart) || !checkedAddSize(segmentStart, header.fileSize, segmentEnd) || segmentEnd > elfBytes.size()) {
            addDiagnostic(image, "PT_LOAD segment extends beyond ELF file");
            LogImage(image);
            return image;
        }

        uint64_t segmentVirtualEnd;
        if (!checkedAdd(header.virtualAddress, header.memorySize, segmentVirtualEnd)) {
            addDiagnostic(image, "PT_LOAD segment virtual range overflows");
            LogImage(image);
            return image;
        }

        NativeElfSegment segment;
        segment.virtualAddress = header.virtualAddress;
        segment.memorySize = header.memorySize;
        segment.fileSize = header.fileSize;
        segment.fileOffset = header.offset;
        segment.flags = header.flags;
        segment.alignment = header.alignment;
        if (header.memorySize > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            addDiagnostic(image, "PT_LOAD segment memory size is too large");
            LogImage(image);
            return image;
        }
        segment.data.resize(static_cast<size_t>(header.memorySize), 0);
        if (header.fileSize > 0) {
            std::copy(elfBytes.begin() + segmentStart, elfBytes.begin() + segmentEnd, segment.data.begin());
        }

        image.loadedSegments.push_back(segment);
        if (header.virtualAddress < minVirtualAddress) minVirtualAddress = header.virtualAddress;
        if (segmentVirtualEnd > maxVirtualAddress) maxVirtualAddress = segmentVirtualEnd;
    }

    if (image.loadedSegments.empty()) {
        addDiagnostic(image, "ELF image contains no loadable segments");
        LogImage(image);
        return image;
    }

    image.preferredBaseAddress = image.isPositionIndependent ? 0 : minVirtualAddress;
    image.imageSize = maxVirtualAddress - minVirtualAddress;
    image.success = true;
    addDiagnostic(image, "Native ELF image loaded");
    LogImage(image);
    return image;
}

void NativeElfImageLoader::LogImage(const NativeElfImage& image) {
    std::ostringstream oss;
    oss << "[NativeElfImageLoader] "
        << "App: " << image.appId
        << " Architecture: " << image.architecture
        << " EntryVA: 0x" << std::hex << image.entryPointVirtualAddress << std::dec
        << " Segments: " << image.loadedSegments.size()
        << " ImageSize: " << image.imageSize
        << " PIE: " << (image.isPositionIndependent ? "true" : "false")
        << " Interpreter: " << (image.hasInterpreter ? "true" : "false")
        << " Result: " << (image.success ? "success" : "failure")
        << " Diagnostics: " << joinDiagnostics(image.diagnostics);
    Logger::write(image.success ? LogLevel::Info : LogLevel::Warn, oss.str());
}

} // namespace apps
} // namespace gxos
