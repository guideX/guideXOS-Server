#define KERNEL_NO_GLOBAL_ARCHITECTURE_DETECTOR_ALIASES
#include "kernel/core/include/kernel/architecture_detector.h"
#include "gxapp_container.h"
#include "gxapp_loader.h"
#include "logger.h"
#include <cstring>
#include <limits>
#include <sstream>

namespace gxos {
namespace {

const char* const UNSUPPORTED_ARCH_ERROR = "This application does not support your CPU architecture.";
const uint8_t ELF_CLASS_32 = 1;
const uint8_t ELF_CLASS_64 = 2;
const uint8_t ELF_DATA_LITTLE = 1;
const uint8_t ELF_DATA_BIG = 2;
const uint16_t ELF_TYPE_EXECUTABLE = 2;
const uint16_t ELF_TYPE_DYNAMIC = 3;
const uint32_t ELF_PT_LOAD = 1;

static std::string archName(CpuArchitecture architecture){
    return CpuArchitectureToString(architecture);
}

static bool expectedElfEndian(CpuArchitecture architecture, bool& littleEndian){
    switch (architecture) {
    case CpuArchitecture::SPARC:
    case CpuArchitecture::SPARC64:
    case CpuArchitecture::S390X:
        littleEndian = false;
        return true;
    case CpuArchitecture::X86:
    case CpuArchitecture::AMD64:
    case CpuArchitecture::ARM:
    case CpuArchitecture::ARM64:
    case CpuArchitecture::IA64:
    case CpuArchitecture::LOONGARCH64:
    case CpuArchitecture::MIPS64:
    case CpuArchitecture::PPC64:
    case CpuArchitecture::RISCV64:
        littleEndian = true;
        return true;
    case CpuArchitecture::Unknown:
    default:
        return false;
    }
}

static void logInfo(const std::string& message){
    Logger::write(LogLevel::Info, std::string("GXAppLoader: ") + message);
}

static void logError(const std::string& message){
    Logger::write(LogLevel::Error, std::string("GXAppLoader: ") + message);
}

static bool fail(std::string& error, const std::string& message){
    error = message;
    logError(message);
    return false;
}

static bool hostIsLittleEndian(){
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return false;
#else
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1;
#endif
}

static uint8_t expectedElfClass(CpuArchitecture architecture){
    switch (architecture) {
    case CpuArchitecture::X86:
    case CpuArchitecture::ARM:
    case CpuArchitecture::SPARC:
        return ELF_CLASS_32;
    case CpuArchitecture::AMD64:
    case CpuArchitecture::ARM64:
    case CpuArchitecture::IA64:
    case CpuArchitecture::LOONGARCH64:
    case CpuArchitecture::MIPS64:
    case CpuArchitecture::PPC64:
    case CpuArchitecture::SPARC64:
    case CpuArchitecture::RISCV64:
    case CpuArchitecture::S390X:
        return ELF_CLASS_64;
    case CpuArchitecture::Unknown:
    default:
        return 0;
    }
}

static uint16_t expectedElfMachine(CpuArchitecture architecture){
    switch (architecture) {
    case CpuArchitecture::X86: return 3;
    case CpuArchitecture::AMD64: return 62;
    case CpuArchitecture::ARM: return 40;
    case CpuArchitecture::ARM64: return 183;
    case CpuArchitecture::IA64: return 50;
    case CpuArchitecture::LOONGARCH64: return 258;
    case CpuArchitecture::MIPS64: return 8;
    case CpuArchitecture::PPC64: return 21;
    case CpuArchitecture::SPARC: return 2;
    case CpuArchitecture::SPARC64: return 43;
    case CpuArchitecture::RISCV64: return 243;
    case CpuArchitecture::S390X: return 22;
    case CpuArchitecture::Unknown:
    default: return 0;
    }
}

static bool readU16At(const std::vector<uint8_t>& data, size_t offset, bool littleEndian, uint16_t& value){
    if (offset + 2 > data.size()) return false;
    if (littleEndian) value = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
    else value = static_cast<uint16_t>(data[offset + 1]) | (static_cast<uint16_t>(data[offset]) << 8);
    return true;
}

static bool readU32At(const std::vector<uint8_t>& data, size_t offset, bool littleEndian, uint32_t& value){
    if (offset + 4 > data.size()) return false;
    if (littleEndian) {
        value = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) | (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
    } else {
        value = static_cast<uint32_t>(data[offset + 3]) | (static_cast<uint32_t>(data[offset + 2]) << 8) | (static_cast<uint32_t>(data[offset + 1]) << 16) | (static_cast<uint32_t>(data[offset]) << 24);
    }
    return true;
}

static bool readU64At(const std::vector<uint8_t>& data, size_t offset, bool littleEndian, uint64_t& value){
    if (offset + 8 > data.size()) return false;
    value = 0;
    if (littleEndian) {
        for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    } else {
        for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(data[offset + 7 - i]) << (i * 8);
    }
    return true;
}

static bool checkedRange(size_t offset, uint64_t size, size_t limit){
    if (offset > limit) return false;
    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max() - offset)) return false;
    return offset + static_cast<size_t>(size) <= limit;
}

static std::string numberString(uint64_t value){
    std::ostringstream out;
    out << value;
    return out.str();
}

static bool validateElfBinary(const std::vector<uint8_t>& binary, CpuArchitecture architecture, std::string& error){
    if (binary.size() < 16) {
        return fail(error, "Selected binary is too small to contain an ELF header.");
    }

    if (binary[0] != 0x7f || binary[1] != 'E' || binary[2] != 'L' || binary[3] != 'F') {
        return fail(error, "Selected binary is not an ELF executable; refusing to execute unknown binary format.");
    }

    const uint8_t elfClass = binary[4];
    const uint8_t elfData = binary[5];
    const uint8_t expectedClass = expectedElfClass(architecture);
    if (elfClass != ELF_CLASS_32 && elfClass != ELF_CLASS_64) {
        return fail(error, "ELF validation failed: unsupported ELF class " + numberString(elfClass) + ".");
    }
    if (expectedClass == 0 || elfClass != expectedClass) {
        return fail(error, "ELF validation failed: ELF class does not match CPU architecture " + archName(architecture) + ".");
    }
    if (elfData != ELF_DATA_LITTLE && elfData != ELF_DATA_BIG) {
        return fail(error, "ELF validation failed: unsupported ELF endian encoding " + numberString(elfData) + ".");
    }

    const bool littleEndian = elfData == ELF_DATA_LITTLE;
    bool expectedLittleEndian = false;
    if (!expectedElfEndian(architecture, expectedLittleEndian) || littleEndian != expectedLittleEndian) {
        return fail(error, "ELF validation failed: ELF endian does not match CPU architecture " + archName(architecture) + ".");
    }
    if (littleEndian != hostIsLittleEndian()) {
        return fail(error, "ELF validation failed: ELF endian does not match the current CPU byte order.");
    }

    const size_t headerSize = elfClass == ELF_CLASS_32 ? 52 : 64;
    if (binary.size() < headerSize) {
        return fail(error, "ELF validation failed: truncated ELF header.");
    }

    uint16_t type = 0;
    uint16_t machine = 0;
    uint32_t version = 0;
    if (!readU16At(binary, 16, littleEndian, type) || !readU16At(binary, 18, littleEndian, machine) || !readU32At(binary, 20, littleEndian, version)) {
        return fail(error, "ELF validation failed: unable to read ELF type/machine/version fields.");
    }
    if (type != ELF_TYPE_EXECUTABLE && type != ELF_TYPE_DYNAMIC) {
        return fail(error, "ELF validation failed: ELF file type " + numberString(type) + " is not executable.");
    }
    const uint16_t expectedMachine = expectedElfMachine(architecture);
    if (expectedMachine == 0 || machine != expectedMachine) {
        return fail(error, "ELF validation failed: machine type " + numberString(machine) + " does not match CPU architecture " + archName(architecture) + ".");
    }
    if (version != 1) {
        return fail(error, "ELF validation failed: unsupported ELF version " + numberString(version) + ".");
    }

    uint64_t entry = 0;
    uint64_t phoff = 0;
    uint16_t phentsize = 0;
    uint16_t phnum = 0;
    if (elfClass == ELF_CLASS_32) {
        uint32_t entry32 = 0;
        uint32_t phoff32 = 0;
        if (!readU32At(binary, 24, littleEndian, entry32) || !readU32At(binary, 28, littleEndian, phoff32) || !readU16At(binary, 42, littleEndian, phentsize) || !readU16At(binary, 44, littleEndian, phnum)) {
            return fail(error, "ELF validation failed: unable to read 32-bit ELF program header fields.");
        }
        entry = entry32;
        phoff = phoff32;
    } else {
        if (!readU64At(binary, 24, littleEndian, entry) || !readU64At(binary, 32, littleEndian, phoff) || !readU16At(binary, 54, littleEndian, phentsize) || !readU16At(binary, 56, littleEndian, phnum)) {
            return fail(error, "ELF validation failed: unable to read 64-bit ELF program header fields.");
        }
    }

    if (phnum == 0) {
        return fail(error, "ELF validation failed: executable has no program headers, so the entry point cannot be validated.");
    }
    if (phentsize < (elfClass == ELF_CLASS_32 ? 32 : 56)) {
        return fail(error, "ELF validation failed: invalid ELF program header entry size.");
    }
    if (phoff > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return fail(error, "ELF validation failed: program header offset is outside addressable memory.");
    }
    const size_t programHeaderOffset = static_cast<size_t>(phoff);
    if (phentsize != 0 && phnum > (std::numeric_limits<size_t>::max() - programHeaderOffset) / phentsize) {
        return fail(error, "ELF validation failed: program header table size overflows.");
    }
    if (programHeaderOffset + static_cast<size_t>(phentsize) * phnum > binary.size()) {
        return fail(error, "ELF validation failed: program header table extends past the binary.");
    }

    bool foundLoadableSegment = false;
    bool entryInLoadableSegment = false;
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t offset = programHeaderOffset + static_cast<size_t>(i) * phentsize;
        uint32_t type = 0;
        uint64_t segmentOffset = 0;
        uint64_t vaddr = 0;
        uint64_t filesz = 0;
        uint64_t memsz = 0;
        if (elfClass == ELF_CLASS_32) {
            uint32_t segmentOffset32 = 0;
            uint32_t vaddr32 = 0;
            uint32_t filesz32 = 0;
            uint32_t memsz32 = 0;
            if (!readU32At(binary, offset, littleEndian, type) || !readU32At(binary, offset + 4, littleEndian, segmentOffset32) || !readU32At(binary, offset + 8, littleEndian, vaddr32) || !readU32At(binary, offset + 16, littleEndian, filesz32) || !readU32At(binary, offset + 20, littleEndian, memsz32)) {
                return fail(error, "ELF validation failed: unable to read 32-bit ELF program header.");
            }
            segmentOffset = segmentOffset32;
            vaddr = vaddr32;
            filesz = filesz32;
            memsz = memsz32;
        } else {
            if (!readU32At(binary, offset, littleEndian, type) || !readU64At(binary, offset + 8, littleEndian, segmentOffset) || !readU64At(binary, offset + 16, littleEndian, vaddr) || !readU64At(binary, offset + 32, littleEndian, filesz) || !readU64At(binary, offset + 40, littleEndian, memsz)) {
                return fail(error, "ELF validation failed: unable to read 64-bit ELF program header.");
            }
        }

        if (type != ELF_PT_LOAD) continue;
        foundLoadableSegment = true;
        if (filesz > memsz) {
            return fail(error, "ELF validation failed: loadable segment file size exceeds memory size.");
        }
        if (segmentOffset > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) || !checkedRange(static_cast<size_t>(segmentOffset), filesz, binary.size())) {
            return fail(error, "ELF validation failed: loadable segment extends past the binary.");
        }
        if (memsz != 0 && entry >= vaddr && entry - vaddr < memsz) {
            entryInLoadableSegment = true;
        }
    }

    if (!foundLoadableSegment) {
        return fail(error, "ELF validation failed: executable has no loadable segments.");
    }
    if (!entryInLoadableSegment) {
        return fail(error, "ELF validation failed: entry point is not inside a loadable segment.");
    }

    logInfo("ELF validation passed for architecture " + archName(architecture) + ".");
    return true;
}

} // namespace

int GXAppLoader::currentArchitecture(){
    return static_cast<int>(kernel::ArchitectureDetector::GetArchitecture());
}

bool GXAppLoader::LoadBinary(const std::string& packagePath, std::vector<uint8_t>& binary, std::string& entryPoint, std::string& error){
    gxos::CpuArchitecture architecture = static_cast<gxos::CpuArchitecture>(currentArchitecture());
    if (architecture == CpuArchitecture::Unknown) {
        return fail(error, UNSUPPORTED_ARCH_ERROR);
    }
    logInfo("loading package " + packagePath + " for CPU architecture " + archName(architecture) + ".");

    GXApp app = GXAppContainer::Open(packagePath);
    if (!app.IsValid()) {
        return fail(error, std::string("Failed to open gxapp package: ") + app.GetLastError());
    }

    if (!app.HasBinary(architecture)) {
        return fail(error, std::string(UNSUPPORTED_ARCH_ERROR) + " Required architecture: " + archName(architecture) + ".");
    }

    binary = app.GetBinary(architecture);
    entryPoint = app.GetEntryPoint(architecture);
    if (binary.empty()) {
        return fail(error, "The application binary is empty.");
    }

    return validateElfBinary(binary, architecture, error);
}

bool GXAppLoader::Execute(const std::string& packagePath, std::string& error){
    std::vector<uint8_t> binary;
    std::string entryPoint;
    if (!LoadBinary(packagePath, binary, entryPoint, error)) {
        return false;
    }

    (void)entryPoint;
    return fail(error, "GXApp ELF execution is not available in this runtime; validated binary was not executed.");
}

} // namespace gxos
