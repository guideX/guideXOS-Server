//
// CPU architecture detection
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "../../../../cpu_architecture.h"

namespace kernel {

using CpuArchitecture = gxos::CpuArchitecture;

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
