//
// CPU architecture detection
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

namespace kernel {

enum class CpuArchitecture {
    Unknown = 0,
    X86,
    Amd64,
    Arm,
    Arm64,
    Ia64,
    LoongArch64,
    Mips64,
    Ppc64,
    Sparc,
    Sparc64
};

class ArchitectureDetector {
public:
    static CpuArchitecture GetArchitecture();
    static const char* ToString(CpuArchitecture architecture);
};

} // namespace kernel

#ifndef KERNEL_NO_GLOBAL_ARCHITECTURE_DETECTOR_ALIASES
using CpuArchitecture = kernel::CpuArchitecture;
using ArchitectureDetector = kernel::ArchitectureDetector;
#endif
