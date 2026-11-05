#!/usr/bin/env python3
"""Hosted checks for IA-64 build and source contracts; this does not execute IA-64 code."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD_SCRIPT = ROOT / "scripts" / "build-ia64.sh"
RUN_SCRIPT = ROOT / "scripts" / "run-ia64_in_linux.sh"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def find_bash() -> str | None:
    if os.name == "nt":
        git_bash = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Git" / "bin" / "bash.exe"
        if git_bash.is_file():
            return str(git_bash)
    return shutil.which("bash")


class Ia64FoundationContractTests(unittest.TestCase):
    def test_build_wrapper_resolves_repo_and_calls_kernel_makefile(self) -> None:
        script = read_text(BUILD_SCRIPT)
        self.assertIn('BASH_SOURCE[0]', script)
        self.assertIn('REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"', script)
        self.assertRegex(script, r'make -C "\$REPO_ROOT/kernel" ARCH=ia64')
        self.assertIn('BUILD_DIR="$BUILD_DIR"', script)
        self.assertNotIn("make ARCH=ia64 clean", script)
        self.assertNotIn("make ARCH=ia64 -j", script)

    def test_wrapper_rejects_missing_or_incompatible_target_tools(self) -> None:
        script = read_text(BUILD_SCRIPT)
        self.assertIn("TOOLCHAIN_BLOCKED", script)
        self.assertIn('"$CXX" -dumpmachine', script)
        self.assertRegex(script, r"ia64\|ia64-\*\)")
        self.assertIn('"$LD" -V', script)
        self.assertIn('grep -qi ia64', script)
        self.assertIn("${CROSS}-readelf", script)

    def test_ia64_architecture_flags_and_linker_script_are_target_specific(self) -> None:
        arch_make = read_text(ROOT / "kernel" / "arch" / "ia64" / "Makefile.arch")
        self.assertIn("ARCH_CFLAGS := -mlp64 -mlittle-endian -mconstant-gp -mfixed-range=f2-f127", arch_make)
        self.assertIn("ASFLAGS := -mlp64 -mle -mconstant-gp", arch_make)
        self.assertIn("LINKER_SCRIPT_FLAG := -T", arch_make)
        kernel_make = read_text(ROOT / "kernel" / "Makefile")
        self.assertIn("$(LINKER_SCRIPT_FLAG)arch/$(ARCH)/linker.ld", kernel_make)

    def test_boot_assembly_uses_gas_definitions_without_cpp_macros(self) -> None:
        boot = read_text(ROOT / "kernel" / "arch" / "ia64" / "boot.s")
        self.assertIn(".equ SSC_CONSOLE_INIT,", boot)
        self.assertIn(".macro IVT_ENTRY_HALT name", boot)
        self.assertIn(".endm", boot)
        self.assertNotRegex(boot, r"(?m)^\s*#\s*define\b")
        calls = re.findall(r"(?m)^IVT_ENTRY_HALT\s+[A-Za-z_][A-Za-z0-9_]*\s*$", boot)
        self.assertEqual(len(calls), 68, "the source currently emits 68 vector macro calls")

    def test_ia64_register_and_break_operands_are_compile_time_values(self) -> None:
        header = read_text(ROOT / "kernel" / "arch" / "ia64" / "include" / "arch" / "ia64.h")
        self.assertIn("template <uint64_t Immediate>", header)
        self.assertIn("template <uint32_t ArNum>", header)
        self.assertIn("template <uint32_t CrNum>", header)
        self.assertNotIn("break_instruction(uint64_t imm)", header)
        self.assertNotIn("read_cr(uint32_t cr_num)", header)
        self.assertNotIn("write_cr(uint32_t cr_num", header)
        for header_name in ("ia64.h", "ski_console.h", "usb_hci.h"):
            arch_header = read_text(ROOT / "kernel" / "arch" / "ia64" / "include" / "arch" / header_name)
            self.assertNotIn("<cstdint>", arch_header)
            self.assertIn("<kernel/types.h>", arch_header)

    def test_linux_make_uses_pwsh_for_profile_verification(self) -> None:
        kernel_make = read_text(ROOT / "kernel" / "Makefile")
        self.assertIn("POWERSHELL ?= powershell", kernel_make)
        self.assertIn("POWERSHELL ?= pwsh", kernel_make)
        self.assertIn("@$(POWERSHELL) -NoProfile", kernel_make)

    @unittest.skipUnless(find_bash(), "Bash is unavailable; shell runtime checks were not run")
    def test_both_shell_wrappers_parse(self) -> None:
        bash = find_bash()
        for script in (BUILD_SCRIPT, RUN_SCRIPT):
            result = subprocess.run(
                [bash, "-n", script.as_posix()],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(find_bash(), "Bash is unavailable; shell runtime checks were not run")
    def test_absent_compiler_is_blocked_from_an_arbitrary_working_directory(self) -> None:
        bash = find_bash()
        env = os.environ.copy()
        env["IA64_TOOLCHAIN_PREFIX"] = "ia64-contract-missing-toolchain"
        env.pop("IA64_BUILD_DIR", None)
        with tempfile.TemporaryDirectory() as caller_dir:
            result = subprocess.run(
                [bash, BUILD_SCRIPT.as_posix()],
                cwd=caller_dir,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("[INFO] Repository:", result.stdout)
        self.assertIn(ROOT.name, result.stdout)
        self.assertIn("[TOOLCHAIN_BLOCKED] Required target tool is missing", result.stderr)
        self.assertNotIn("[BUILD] Compiling", result.stdout)

    @unittest.skipUnless(find_bash(), "Bash is unavailable; shell runtime checks were not run")
    def test_file_as_output_path_is_rejected_before_toolchain_use(self) -> None:
        bash = find_bash()
        env = os.environ.copy()
        env["IA64_TOOLCHAIN_PREFIX"] = "ia64-contract-missing-toolchain"
        env["IA64_BUILD_DIR"] = "kernel/Makefile"
        with tempfile.TemporaryDirectory() as caller_dir:
            result = subprocess.run(
                [bash, BUILD_SCRIPT.as_posix()],
                cwd=caller_dir,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("IA64_BUILD_DIR exists and is not a directory", result.stderr)
        self.assertNotIn("Required target tool is missing", result.stderr)

    @unittest.skipUnless(find_bash(), "Bash is unavailable; shell runtime checks were not run")
    def test_nonempty_output_path_is_preserved_and_rejected(self) -> None:
        bash = find_bash()
        env = os.environ.copy()
        env["IA64_TOOLCHAIN_PREFIX"] = "ia64-contract-missing-toolchain"
        env["IA64_BUILD_DIR"] = "kernel"
        with tempfile.TemporaryDirectory() as caller_dir:
            result = subprocess.run(
                [bash, BUILD_SCRIPT.as_posix()],
                cwd=caller_dir,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("IA64_BUILD_DIR must be empty to protect existing artifacts", result.stderr)
        self.assertNotIn("Required target tool is missing", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
