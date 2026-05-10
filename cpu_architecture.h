#pragma once

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__
#include <string>
#endif

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

    inline bool CpuArchitectureStringEquals(const char* left, const char* right){
        if (!left || !right) return false;

        while (*left && *right) {
            if (*left != *right) return false;
            ++left;
            ++right;
        }

        return *left == *right;
    }

    inline CpuArchitecture CpuArchitectureFromString(const char* architecture){
        if (CpuArchitectureStringEquals(architecture, "x86")) return CpuArchitecture::X86;
        if (CpuArchitectureStringEquals(architecture, "amd64")) return CpuArchitecture::AMD64;
        if (CpuArchitectureStringEquals(architecture, "arm")) return CpuArchitecture::ARM;
        if (CpuArchitectureStringEquals(architecture, "arm64")) return CpuArchitecture::ARM64;
        if (CpuArchitectureStringEquals(architecture, "ia64")) return CpuArchitecture::IA64;
        if (CpuArchitectureStringEquals(architecture, "loongarch64")) return CpuArchitecture::LOONGARCH64;
        if (CpuArchitectureStringEquals(architecture, "mips64")) return CpuArchitecture::MIPS64;
        if (CpuArchitectureStringEquals(architecture, "ppc64")) return CpuArchitecture::PPC64;
        if (CpuArchitectureStringEquals(architecture, "sparc")) return CpuArchitecture::SPARC;
        if (CpuArchitectureStringEquals(architecture, "sparc64")) return CpuArchitecture::SPARC64;
        if (CpuArchitectureStringEquals(architecture, "riscv64")) return CpuArchitecture::RISCV64;
        if (CpuArchitectureStringEquals(architecture, "s390x")) return CpuArchitecture::S390X;
        return CpuArchitecture::Unknown;
    }

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__
    inline CpuArchitecture CpuArchitectureFromString(const std::string& architecture){
        return CpuArchitectureFromString(architecture.c_str());
    }
#endif

} // namespace gxos

using CpuArchitecture = gxos::CpuArchitecture;
