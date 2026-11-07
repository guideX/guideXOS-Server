#!/usr/bin/env python3
"""Static safety gates for the bounded Ski-only IA-64 early-exec profile."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys


FORBIDDEN_RUNTIME_CALLS = (
    "interrupts::init", "init_interrupts", "enable_interrupts", "desktop::tick",
    "process::schedule", "arch_thread_create", "arch_switch_to", "switch_context",
    "arch_timer_tick", "arch_set_timer",
)


def run_disassembly(objdump: str, elf: Path, symbol: str) -> str:
    result = subprocess.run(
        [objdump, "-d", "-C", f"--disassemble={symbol}", str(elf)],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        raise RuntimeError(f"objdump failed for {symbol}: {result.stderr.strip()}")
    return result.stdout


def validate(repo_root: Path, elf: Path, objdump: str) -> list[str]:
    failures: list[str] = []

    def check(ok: bool, label: str, detail: str) -> None:
        print(f"{label}={'PASS' if ok else 'FAIL'} {detail}")
        if not ok:
            failures.append(label)

    boot = (repo_root / "kernel/arch/ia64/boot.s").read_text(encoding="utf-8")
    rsc_setup = re.search(
        r"movl r14 = rse_backing_store_base\s*;;\s*"
        r"mov ar\.rsc = 0\s*;;\s*loadrs\s*;;\s*"
        r"mov ar\.bspstore = r14\s*;;\s*mov ar\.rsc = 3",
        boot,
    )
    check(rsc_setup is not None, "RSE_STARTUP_ORDER",
          "enforced-lazy mode precedes loadrs/BSPSTORE; eager mode follows")
    check(bool(re.search(r"rsm psr\.dfl \| psr\.dfh\s*;;", boot)) and
          "movl r14 = 0x0009804c0270033f" in boot and "mov.m ar.fpsr = r14" in boot,
          "FP_STARTUP_STATE", "FP register sets enabled and ABI FPSR default installed before C")
    check("rsm psr.i | psr.ic ;;" in boot and "ssm psr.i" not in boot,
          "BOOT_INTERRUPT_MASK", "entry masks interrupts and contains no interrupt enable")

    console_header = (repo_root / "kernel/arch/ia64/include/arch/ski_console.h").read_text(encoding="utf-8")
    expected_ssc = {
        "SSC_STOP": 0,
        "SSC_CONSOLE_INIT": 20,
        "SSC_GETCHAR": 21,
        "SSC_PUTCHAR": 31,
        "SSC_EXIT": 66,
    }
    ssc_values_ok = all(
        re.search(rf"\b{name}\s*=\s*{value}\s*;", console_header)
        for name, value in expected_ssc.items()
    )
    ssc_values_ok = ssc_values_ok and all(
        re.search(rf"(?m)^\.equ {name},\s+{value}$", boot)
        for name, value in expected_ssc.items()
    )
    check(ssc_values_ok, "SKI_SSC_CODES",
          "Ski SSC values match the pinned interface (STOP=0, INIT=20, GETCHAR=21, PUTCHAR=31, EXIT=66)")

    console_cpp = (repo_root / "kernel/arch/ia64/ski_console.cpp").read_text(encoding="utf-8")
    ssc_asm = (repo_root / "kernel/arch/ia64/ski_ssc.s").read_text(encoding="utf-8")
    check("guidexos_ski_ssc(func, arg0)" in console_cpp and
          "guidexos_ski_ssc(SSC_GETCHAR, 0)" in console_cpp and
          "C ABI inputs: in0/r32 = SSC function, in1/r33 = argument 0" in ssc_asm and
          "alloc r34 = ar.pfs, 2, 1, 0, 0 ;;" in ssc_asm and
          "mov r15 = r32 ;;" in ssc_asm and "mov r32 = r33 ;;" in ssc_asm and
          "break.i 0x80000 ;;" in ssc_asm and "mov.i ar.pfs = r34 ;;" in ssc_asm and
          "br.ret.sptk.many b0 ;;" in ssc_asm and
          "ssc(SSC_EXIT);" in console_cpp,
          "SKI_SSC_CALL_ABI", "standalone ABI bridge maps r32/r33 to r15/r32 and returns Ski r8")

    main = (repo_root / "kernel/core/main.cpp").read_text(encoding="utf-8")
    early_start = main.find("#if defined(GXOS_IA64_EARLY_EXEC_PROFILE)")
    early_end = main.find("#else", early_start)
    early_body = main[early_start:early_end] if early_start >= 0 and early_end > early_start else ""
    check(bool(early_body), "EARLY_ENTRY_ISOLATION", "dedicated compile-time entry branch exists")
    bad_source_calls = [token for token in FORBIDDEN_RUNTIME_CALLS if token in early_body]
    check(not bad_source_calls and "ski_console::stop()" in early_body and
          "kernel::arch::halt()" in early_body,
          "EARLY_ENTRY_CALLS", "stop-only path; forbidden calls=" +
          (",".join(bad_source_calls) if bad_source_calls else "none"))

    arch = (repo_root / "kernel/arch/ia64/arch.cpp").read_text(encoding="utf-8")
    init_start = arch.find("void init()")
    init_end = arch.find("} // namespace ia64", init_start)
    arch_init = arch[init_start:init_end] if init_start >= 0 and init_end > init_start else ""
    bad_arch_calls = [token for token in FORBIDDEN_RUNTIME_CALLS
                      if token != "enable_interrupts" and token in arch_init]
    check(bool(arch_init) and not bad_arch_calls and
          "ssm psr.ic" in arch_init and
          not re.search(r"\bssm\s+psr\.i\b", arch_init),
          "ARCH_INIT_INTERRUPT_POLICY", "IVT collection only; external interrupts stay masked")

    try:
        main_disassembly = run_disassembly(objdump, elf, "kernel_main")
        boot_disassembly = run_disassembly(objdump, elf, "_start")
        bad_binary_calls = [token for token in FORBIDDEN_RUNTIME_CALLS if token in main_disassembly]
        check("<kernel_main>:" in main_disassembly and "ski_console::stop" in main_disassembly and
              not bad_binary_calls,
              "EARLY_ENTRY_DISASSEMBLY", "compiled kernel_main has stop path; forbidden call targets=" +
              (",".join(bad_binary_calls) if bad_binary_calls else "none"))
        check("<_start>:" in boot_disassembly and "loadrs" in boot_disassembly and
              "bspstore" in boot_disassembly and "fpsr" in boot_disassembly and
              "rsm 0xc0000" in boot_disassembly and
              not re.search(r"\bssm\b[^\n]*psr\.i\b", boot_disassembly),
              "STARTUP_DISASSEMBLY", "target startup includes RSE/FP setup (DFL|DFH mask 0xc0000) and no interrupt enable")
        ssc_disassembly = run_disassembly(
            objdump, elf, "kernel::arch::ia64::ski_console::ssc(unsigned long, unsigned long)")
        bridge_disassembly = run_disassembly(objdump, elf, "guidexos_ski_ssc")
        putchar_disassembly = run_disassembly(
            objdump, elf, "kernel::arch::ia64::ski_console::putchar(char)")
        getchar_disassembly = run_disassembly(
            objdump, elf, "kernel::arch::ia64::ski_console::getchar()")
        stop_disassembly = run_disassembly(
            objdump, elf, "kernel::arch::ia64::ski_console::stop()")
        check("guidexos_ski_ssc" in ssc_disassembly and
              "alloc r34=ar.pfs,3,3,0" in bridge_disassembly and
              "mov r15=r32" in bridge_disassembly and "mov r32=r33" in bridge_disassembly and
              "break.i 0x80000" in bridge_disassembly and "mov.i ar.pfs=r34" in bridge_disassembly and
              "br.ret.sptk.many b0" in bridge_disassembly,
              "SSC_WRAPPER_DISASSEMBLY", "target ABI bridge allocates its input frame, maps registers, restores ar.pfs, and returns through r8")
        check("guidexos_ski_ssc" in putchar_disassembly and
              "guidexos_ski_ssc" in getchar_disassembly and
              "guidexos_ski_ssc" in stop_disassembly and
              "mov r15=20" in boot_disassembly,
              "SSC_CALL_DISASSEMBLY", "console-init/putchar/getchar/exit paths call the standalone SSC bridge")
    except (OSError, RuntimeError) as exc:
        check(False, "TARGET_DISASSEMBLY", str(exc))

    if failures:
        print(f"IA64_EARLY_EXEC_STATIC_GATES=FAIL count={len(failures)}")
        return failures
    print("IA64_EARLY_EXEC_STATIC_GATES=PASS")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", required=True)
    args = parser.parse_args()
    try:
        failures = validate(args.repo_root, args.elf, args.objdump)
    except OSError as exc:
        print(f"IA64_EARLY_EXEC_STATIC_GATES=FAIL error={exc}", file=sys.stderr)
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
