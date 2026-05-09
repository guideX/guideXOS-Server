//
// CPU architecture detection implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/architecture_detector.h"

namespace kernel {

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__) || defined(_M_IX86)
static bool x86_cpu_supports_cpuid()
{
#if defined(_MSC_VER)
    __try {
        int regs[4];
        __cpuid(regs, 0);
        return true;
    } __except (1) {
        return false;
    }
#elif defined(__GNUC__) || defined(__clang__)
    uint32_t before;
    uint32_t after;
    __asm__ volatile(
        "pushfl\n\t"
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "popfl"
        : "=&r"(before), "=&r"(after)
        :
        : "cc");
    return ((before ^ after) & 0x200000U) != 0;
#else
    return false;
#endif
}

static void x86_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t& edx)
{
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    eax = static_cast<uint32_t>(regs[0]);
    ebx = static_cast<uint32_t>(regs[1]);
    ecx = static_cast<uint32_t>(regs[2]);
    edx = static_cast<uint32_t>(regs[3]);
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf));
#else
    (void)leaf;
    (void)subleaf;
    eax = 0;
    ebx = 0;
    ecx = 0;
    edx = 0;
#endif
}

static bool x86_cpu_supports_long_mode()
{
    if (!x86_cpu_supports_cpuid()) {
        return false;
    }

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    x86_cpuid(0x80000000U, 0, eax, ebx, ecx, edx);
    if (eax < 0x80000001U) {
        return false;
    }

    x86_cpuid(0x80000001U, 0, eax, ebx, ecx, edx);
    return (edx & (1U << 29)) != 0;
}
#endif

CpuArchitecture ArchitectureDetector::GetArchitecture()
{
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64)
    return CpuArchitecture::AMD64;
#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__) || defined(_M_IX86) || defined(__X86__)
    if (x86_cpu_supports_long_mode()) {
        return CpuArchitecture::AMD64;
    }

    return CpuArchitecture::X86;
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm64__) || defined(__ARM64_ARCH_8__) || defined(__ARM_ARCH_ISA_A64)
    return CpuArchitecture::ARM64;
#elif defined(__arm__) || defined(__aarch32__) || defined(_M_ARM) || defined(_M_ARMT) || defined(__thumb__) || defined(__ARM_ARCH)
    return CpuArchitecture::ARM;
#elif defined(__ia64__) || defined(_M_IA64)
    return CpuArchitecture::IA64;
#elif defined(__loongarch64) || (defined(__loongarch__) && defined(__loongarch_grlen) && (__loongarch_grlen == 64))
    return CpuArchitecture::LOONGARCH64;
#elif defined(__mips64) || defined(__mips64__) || (defined(__mips) && defined(_MIPS_SZLONG) && (_MIPS_SZLONG == 64)) || defined(_M_MRX000)
    return CpuArchitecture::MIPS64;
#elif defined(__powerpc64__) || defined(__ppc64__) || defined(_ARCH_PPC64) || defined(__PPC64__)
    return CpuArchitecture::PPC64;
#elif defined(__sparc__) && (defined(__arch64__) || defined(__sparcv9) || defined(__sparc_v9__) || defined(__sparc64__))
    return CpuArchitecture::SPARC64;
#elif defined(__sparc__) || defined(__sparc)
    return CpuArchitecture::SPARC;
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 64)
    return CpuArchitecture::RISCV64;
#elif defined(__s390x__)
    return CpuArchitecture::S390X;
#else
    return CpuArchitecture::Unknown;
#endif
}

const char* ArchitectureDetector::ToString(CpuArchitecture architecture)
{
    return gxos::CpuArchitectureToString(architecture);
}

} // namespace kernel
