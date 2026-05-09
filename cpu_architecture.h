#pragma once

#include <string>

namespace gxos {

    enum class CpuArchitecture {
        Unknown = 0,
        X86,
        AMD64,
        ARM,
        ARM64,
        IA64,
        LOONGARCH64,
        MIPS64,
        PPC64,
        SPARC,
        SPARC64,
        RISCV64,
        S390X
    };

    inline const char* CpuArchitectureToString(CpuArchitecture architecture){
        switch (architecture) {
        case CpuArchitecture::X86: return "x86";
        case CpuArchitecture::AMD64: return "amd64";
        case CpuArchitecture::ARM: return "arm";
        case CpuArchitecture::ARM64: return "arm64";
        case CpuArchitecture::IA64: return "ia64";
        case CpuArchitecture::LOONGARCH64: return "loongarch64";
        case CpuArchitecture::MIPS64: return "mips64";
        case CpuArchitecture::PPC64: return "ppc64";
        case CpuArchitecture::SPARC: return "sparc";
        case CpuArchitecture::SPARC64: return "sparc64";
        case CpuArchitecture::RISCV64: return "riscv64";
        case CpuArchitecture::S390X: return "s390x";
        case CpuArchitecture::Unknown:
        default: return "unknown";
        }
    }

    inline CpuArchitecture CpuArchitectureFromString(const std::string& architecture){
        if (architecture == "x86") return CpuArchitecture::X86;
        if (architecture == "amd64") return CpuArchitecture::AMD64;
        if (architecture == "arm") return CpuArchitecture::ARM;
        if (architecture == "arm64") return CpuArchitecture::ARM64;
        if (architecture == "ia64") return CpuArchitecture::IA64;
        if (architecture == "loongarch64") return CpuArchitecture::LOONGARCH64;
        if (architecture == "mips64") return CpuArchitecture::MIPS64;
        if (architecture == "ppc64") return CpuArchitecture::PPC64;
        if (architecture == "sparc") return CpuArchitecture::SPARC;
        if (architecture == "sparc64") return CpuArchitecture::SPARC64;
        if (architecture == "riscv64") return CpuArchitecture::RISCV64;
        if (architecture == "s390x") return CpuArchitecture::S390X;
        return CpuArchitecture::Unknown;
    }

} // namespace gxos

using CpuArchitecture = gxos::CpuArchitecture;
