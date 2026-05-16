#include "elf_validator.h"

#include <cstdio>
#include <limits>

namespace gxos {
namespace apps {
namespace {

constexpr size_t kElfIdentSize = 16;
constexpr uint8_t kElfClass32 = 1;
constexpr uint8_t kElfClass64 = 2;
constexpr uint8_t kElfDataLittleEndian = 1;
constexpr uint8_t kElfDataBigEndian = 2;
constexpr uint16_t kElfVersionCurrent = 1;
constexpr uint16_t kElfTypeExecutable = 2;
constexpr uint16_t kElfTypeShared = 3;
constexpr uint32_t kElfProgramHeaderTypeInterp = 3;

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

std::string formatHex(uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
}

std::string machineTypeName(uint16_t machine) {
    switch (machine) {
    case 0x03: return "EM_386";
    case 0x08: return "EM_MIPS";
    case 0x14: return "EM_PPC";
    case 0x28: return "EM_ARM";
    case 0x2a: return "EM_SUPERH";
    case 0x2b: return "EM_SPARCV9";
    case 0x32: return "EM_IA_64";
    case 0x3e: return "EM_X86_64";
    case 0xB7: return "EM_AARCH64";
    case 0xF3: return "EM_RISCV";
    case 0x102: return "EM_LOONGARCH";
    default: return "EM_UNKNOWN(" + std::to_string(machine) + ")";
    }
}

std::string elfTypeName(uint16_t type) {
    switch (type) {
    case kElfTypeExecutable: return "ET_EXEC";
    case kElfTypeShared: return "ET_DYN";
    default: return "ET_UNKNOWN(" + std::to_string(type) + ")";
    }
}

std::string architectureForMachine(uint16_t machine, uint8_t elfClass) {
    switch (machine) {
    case 0x03: return "x86";
    case 0x08: return "mips64";
    case 0x14: return "ppc64";
    case 0x28: return "arm";
    case 0x2a: return "sparc";
    case 0x2b: return "sparc64";
    case 0x32: return "ia64";
    case 0x3e: return "amd64";
    case 0xB7: return "arm64";
    case 0xF3: return "riscv64";
    case 0x102: return "loongarch64";
    default: return std::string();
    }
}

bool architectureMatches(const std::string& actual, const std::string& expected) {
    return expected.empty() || expected == "any" || expected == "*" || actual == expected;
}

bool checkedAdd(size_t left, uint64_t right, size_t& result) {
    if (right > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false;
    size_t converted = static_cast<size_t>(right);
    if (left > std::numeric_limits<size_t>::max() - converted) return false;
    result = left + converted;
    return true;
}

uint32_t readProgramHeaderType(const std::vector<uint8_t>& bytes, size_t offset, bool littleEndian) {
    return readU32(bytes, offset, littleEndian);
}

} // namespace

ElfValidationResult ElfValidator::Validate(const std::vector<uint8_t>& bytes, const std::string& expectedArchitecture) {
    ElfValidationResult result;

    if (bytes.size() < 52) {
        result.errors.push_back("ELF file is too small to contain a valid header");
        return result;
    }

    if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
        result.errors.push_back("File does not contain ELF magic");
        return result;
    }

    uint8_t elfClass = bytes[4];
    if (elfClass != kElfClass32 && elfClass != kElfClass64) {
        result.errors.push_back("Unsupported ELF class");
        return result;
    }

    uint8_t dataEncoding = bytes[5];
    if (dataEncoding != kElfDataLittleEndian && dataEncoding != kElfDataBigEndian) {
        result.errors.push_back("Unsupported ELF byte order");
        return result;
    }

    bool littleEndian = dataEncoding == kElfDataLittleEndian;
    uint16_t type = readU16(bytes, 16, littleEndian);
    uint16_t machine = readU16(bytes, 18, littleEndian);
    uint32_t version = readU32(bytes, 20, littleEndian);
    uint64_t entry = elfClass == kElfClass64 ? readU64(bytes, 24, littleEndian) : readU32(bytes, 24, littleEndian);
    uint64_t programHeaderOffset = elfClass == kElfClass64 ? readU64(bytes, 32, littleEndian) : readU32(bytes, 28, littleEndian);
    uint16_t headerSize = readU16(bytes, elfClass == kElfClass64 ? 52 : 40, littleEndian);
    uint16_t programHeaderEntrySize = readU16(bytes, elfClass == kElfClass64 ? 54 : 42, littleEndian);
    uint16_t programHeaderCount = readU16(bytes, elfClass == kElfClass64 ? 56 : 44, littleEndian);

    result.elfClass = elfClass == kElfClass64 ? "ELF64" : "ELF32";
    result.endian = littleEndian ? "little" : "big";
    result.machineType = machineTypeName(machine);
    result.elfType = elfTypeName(type);
    result.architecture = architectureForMachine(machine, elfClass);
    result.entryPoint = formatHex(entry);

    if (version != kElfVersionCurrent) result.errors.push_back("Unsupported ELF version");
    if (type == kElfTypeShared) result.errors.push_back("ET_DYN/PIE is unsupported; Native ELF experimental execution currently supports static ET_EXEC only");
    else if (type != kElfTypeExecutable) result.errors.push_back("Unsupported ELF type " + elfTypeName(type) + "; Native ELF experimental execution currently supports static ET_EXEC only");
    if (result.architecture.empty()) result.errors.push_back("Unsupported ELF machine architecture");
    if (!architectureMatches(result.architecture, expectedArchitecture)) result.errors.push_back("Wrong architecture: ELF architecture " + result.architecture + " does not match manifest architecture " + expectedArchitecture);
    if (headerSize < (elfClass == kElfClass64 ? 64 : 52)) result.errors.push_back("Invalid ELF header size");
    if (programHeaderEntrySize < (elfClass == kElfClass64 ? 56 : 32)) result.errors.push_back("Invalid ELF program header entry size");
    if (programHeaderOffset == 0 || programHeaderCount == 0) result.errors.push_back("ELF has no program headers");

    if (programHeaderOffset != 0 && programHeaderCount != 0 && programHeaderEntrySize >= (elfClass == kElfClass64 ? 56 : 32)) {
        size_t tableStart;
        size_t tableSize = static_cast<size_t>(programHeaderEntrySize) * static_cast<size_t>(programHeaderCount);
        if (!checkedAdd(0, programHeaderOffset, tableStart) || tableStart > bytes.size() || tableSize > bytes.size() - tableStart) {
            result.errors.push_back("ELF program header table extends beyond file");
        } else {
            for (uint16_t i = 0; i < programHeaderCount; ++i) {
                size_t programHeaderStart = tableStart + static_cast<size_t>(i) * static_cast<size_t>(programHeaderEntrySize);
                if (readProgramHeaderType(bytes, programHeaderStart, littleEndian) == kElfProgramHeaderTypeInterp) {
                    result.errors.push_back("PT_INTERP present; dynamic linker/dynamic linking is not supported");
                    break;
                }
            }
        }
    }

    result.valid = result.errors.empty();
    return result;
}

} // namespace apps
} // namespace gxos
