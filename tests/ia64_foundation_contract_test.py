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
        self.assertIn("IA64_FP_PROFILE ?= runtime", arch_make)
        self.assertIn("-mfixed-range=f2-f127", arch_make)
        self.assertIn("-mfixed-range=f2-f5,f16-f31", arch_make)
        self.assertIn("__ia64_compile_only_profile=1", arch_make)
        self.assertIn("ASFLAGS := -mlp64 -mle -mconstant-gp -xexplicit", arch_make)
        self.assertIn("LINKER_SCRIPT_FLAG := -T", arch_make)
        kernel_make = read_text(ROOT / "kernel" / "Makefile")
        self.assertIn("$(LINKER_SCRIPT_FLAG)arch/$(ARCH)/linker.ld", kernel_make)

    def test_ia64_linker_separates_load_permissions_and_read_only_descriptors(self) -> None:
        linker = read_text(ROOT / "kernel" / "arch" / "ia64" / "linker.ld")
        self.assertIn("PHDRS", linker)
        self.assertIn("text   PT_LOAD FLAGS(5)", linker)
        self.assertIn("rodata PT_LOAD FLAGS(4)", linker)
        self.assertIn("data   PT_LOAD FLAGS(6)", linker)
        self.assertIn(".opd ALIGN(16) (READONLY)", linker)
        self.assertIn("} :text", linker)
        self.assertIn("} :rodata", linker)
        self.assertIn("} :data", linker)
        self.assertIn("architectural layout and", linker)
        self.assertIn("SIZEOF(.ivt) == 0x8000", linker)
        self.assertIn("ia64_ivt_end - ia64_ivt", linker)
        self.assertLess(linker.index(".sbss ALIGN"), linker.index(".bss ALIGN"))

    def test_kernel_makefile_filters_assembly_sources_by_extension(self) -> None:
        makefile = read_text(ROOT / "kernel" / "Makefile")
        self.assertIn("$(filter %.asm,$(ASM_SOURCES))", makefile)
        self.assertIn("$(filter %.s,$(ASM_SOURCES))", makefile)

    def test_ia64_linker_uses_target_libgcc_archive(self) -> None:
        arch_make = read_text(ROOT / "kernel" / "arch" / "ia64" / "Makefile.arch")
        script = read_text(BUILD_SCRIPT)
        self.assertIn("IA64_RUNTIME_LIBGCC ?= $(shell $(CXX) -print-libgcc-file-name)", arch_make)
        self.assertIn("LIBS := $(IA64_RUNTIME_LIBGCC)", arch_make)
        self.assertIn("IA64_RUNTIME_LIBGCC", script)
        self.assertIn("[LINKER_BLOCKED] Target libgcc archive is missing", script)

    def test_ia64_build_runs_independent_elf_and_object_validation(self) -> None:
        script = read_text(BUILD_SCRIPT)
        validator = ROOT / "scripts" / "validate-ia64-elf.py"
        self.assertTrue(validator.is_file())
        self.assertIn("validate-ia64-elf.py", script)
        self.assertIn("--object-dir", script)
        self.assertIn("--repo-root", script)
        self.assertIn("ia64-elf-inspection.txt", script)
        self.assertIn("kernel.elf.sha256", script)
        self.assertIn("sha256sum", script)

    def test_ia64_elf_validator_covers_abi_and_memory_acceptance(self) -> None:
        validator = read_text(ROOT / "scripts" / "validate-ia64-elf.py")
        for token in (
            "EM_IA_64 = 50",
            "ET_EXEC = 2",
            "IVT_BYTES = 32 * 1024",
            "IVT_LONG_SLOT_BYTES = 0x400",
            "IVT_SHORT_SLOT_BYTES = 0x100",
            "ENTRY_SYMBOL",
            "GLOBAL_POINTER",
            "ALLOC_SECTION_MAPPING",
            "FUNCTION_DESCRIPTORS",
            "IA64_UNWIND_SEGMENT",
            "IVT_VECTOR_LAYOUT",
            "IA64_BUILD_PROFILE",
            "REMAINING_RELOCATIONS",
            "UNRESOLVED_SYMBOLS",
            "KERNEL_STACK",
            "RSE_BACKING_STORE",
            "OBJECT_ARCHITECTURE",
            "W_X_SEPARATION",
            "SECTION_SEGMENT_PERMISSIONS",
        ):
            self.assertIn(token, validator)

    def test_boot_assembly_uses_gas_definitions_without_cpp_macros(self) -> None:
        boot = read_text(ROOT / "kernel" / "arch" / "ia64" / "boot.s")
        self.assertIn(".equ SSC_CONSOLE_INIT,", boot)
        self.assertIn(".macro IVT_ENTRY_HALT name, offset, slot_bytes", boot)
        self.assertIn(".macro IVT_ENTRY_NAT_CONSUMPTION name, offset, slot_bytes", boot)
        self.assertIn(".endm", boot)
        self.assertNotRegex(boot, r"(?m)^\s*#\s*define\b")
        calls = re.findall(
            r"(?m)^IVT_ENTRY_HALT\s+([A-Za-z_][A-Za-z0-9_]*),\s*"
            r"(0x[0-9a-fA-F]+),\s*(IVT_(?:LONG|SHORT)_SLOT_BYTES)\s*$", boot)
        self.assertEqual(len(calls), 68, "the source must emit exactly 68 architectural vector slots")
        for index, (_, offset, slot_size) in enumerate(calls):
            self.assertEqual(int(offset, 16), index * 0x400 if index < 20 else 0x5000 + (index - 20) * 0x100)
            self.assertEqual(slot_size, "IVT_LONG_SLOT_BYTES" if index < 20 else "IVT_SHORT_SLOT_BYTES")
        self.assertIn("ivt_virtual_external_interrupt", [call[0] for call in calls])
        self.assertIn("ia64_ivt_end:", boot)
        self.assertRegex(boot, r"cmp\.geu p6, p7 = r14, r15 ;;")

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

    @unittest.skipUnless(find_bash(), "Bash is unavailable; shell runtime checks were not run")
    def test_unsupported_fp_build_profile_is_rejected(self) -> None:
        bash = find_bash()
        env = os.environ.copy()
        env["IA64_FP_PROFILE"] = "unsafe"
        env["IA64_TOOLCHAIN_PREFIX"] = "ia64-contract-missing-toolchain"
        env.pop("IA64_BUILD_DIR", None)
        with tempfile.TemporaryDirectory() as caller_dir:
            result = subprocess.run(
                [bash, BUILD_SCRIPT.as_posix()], cwd=caller_dir, env=env,
                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("[PROFILE_BLOCKED] Unsupported IA64_FP_PROFILE", result.stderr)
        self.assertNotIn("[BUILD] Compiling", result.stdout)

    @unittest.skipUnless(find_bash(), "Bash is unavailable; shell runtime checks were not run")
    def test_compile_only_fp_profile_is_rejected_for_execution(self) -> None:
        bash = find_bash()
        env = os.environ.copy()
        env["IA64_FP_PROFILE"] = "compile-only"
        env.pop("IA64_BUILD_DIR", None)
        with tempfile.TemporaryDirectory() as caller_dir:
            result = subprocess.run(
                [bash, RUN_SCRIPT.as_posix()], cwd=caller_dir, env=env,
                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("[RUN_BLOCKED] The compile-only FP profile cannot be used", result.stderr)
        self.assertNotIn("[BUILD] Compiling", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
