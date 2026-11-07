#!/usr/bin/env python3
"""Source, metadata, and runner gates for IA64-2; never executes IA-64 code."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "kernel/arch/ia64/boot.s"
MAIN = ROOT / "kernel/core/main.cpp"
ARCH = ROOT / "kernel/arch/ia64/arch.cpp"
CONTEXT = ROOT / "kernel/arch/ia64/include/arch/context_switch.h"
MAKE_ARCH = ROOT / "kernel/arch/ia64/Makefile.arch"
RUNNER = ROOT / "scripts/run-ia64_in_linux.sh"
BUILD = ROOT / "scripts/build-ia64.sh"
VALIDATOR_PATH = ROOT / "scripts/validate-ia64-elf.py"

spec = importlib.util.spec_from_file_location("validate_ia64_elf", VALIDATOR_PATH)
validator = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(validator)


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


class Ia64EarlyExecContractTests(unittest.TestCase):
    def test_rse_is_put_in_lazy_mode_before_loadrs_and_bspstore(self) -> None:
        boot = source(BOOT)
        sequence = ["mov ar.rsc = 0", "loadrs", "mov ar.bspstore = r14", "mov ar.rsc = 3"]
        positions = [boot.index(token, boot.index("rse_backing_store_base")) for token in sequence]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("BSPSTORE and loadrs are only legal in enforced-lazy mode", boot)

    def test_startup_enables_fp_register_banks_and_installs_abi_fpsr(self) -> None:
        boot = source(BOOT)
        self.assertLess(boot.index("rsm psr.dfl | psr.dfh"), boot.index("br.call.sptk.many rp = kernel_main"))
        self.assertIn("movl r14 = 0x0009804c0270033f", boot)
        self.assertIn("mov.m ar.fpsr = r14", boot)

    def test_early_entry_isolated_from_interrupts_devices_and_scheduling(self) -> None:
        text = source(MAIN)
        start = text.index("#if defined(GXOS_IA64_EARLY_EXEC_PROFILE)")
        end = text.index("#else", start)
        early = text[start:end]
        for forbidden in (
            "interrupts::init", "init_interrupts", "enable_interrupts", "desktop::tick",
            "process::schedule", "arch_thread_create", "arch_switch_to", "arch_timer_tick",
        ):
            self.assertNotIn(forbidden, early)
        self.assertIn("ski_console::stop()", early)
        self.assertIn("kernel::arch::halt()", early)

    def test_arch_init_only_enables_exception_collection_not_external_interrupts(self) -> None:
        text = source(ARCH)
        start = text.index("void init()")
        body = text[start:text.index("} // namespace ia64", start)]
        self.assertIn("ssm psr.ic", body)
        self.assertIsNone(re.search(r"\bssm\s+psr\.i\b", body))

    def test_ski_ssc_codes_and_call_registers_match_the_simulator_abi(self) -> None:
        header = source(ROOT / "kernel/arch/ia64/include/arch/ski_console.h")
        boot = source(BOOT)
        console = source(ROOT / "kernel/arch/ia64/ski_console.cpp")
        for name, value in (
            ("SSC_CONSOLE_INIT", 20),
            ("SSC_GETCHAR", 21),
            ("SSC_PUTCHAR", 31),
            ("SSC_EXIT", 66),
        ):
            self.assertRegex(header, rf"\b{name}\s*=\s*{value}\s*;")
        self.assertRegex(boot, r"(?m)^\.equ SSC_CONSOLE_INIT,\s+20$")
        self.assertRegex(boot, r"(?m)^\.equ SSC_PUTCHAR,\s+31$")
        ssc_asm = source(ROOT / "kernel/arch/ia64/ski_ssc.s")
        self.assertIn("guidexos_ski_ssc(func, arg0)", console)
        self.assertIn("guidexos_ski_ssc(SSC_GETCHAR, 0)", console)
        self.assertIn("alloc r34 = ar.pfs, 2, 1, 0, 0 ;;", ssc_asm)
        self.assertIn("mov r15 = r32 ;;", ssc_asm)
        self.assertIn("mov r32 = r33 ;;", ssc_asm)
        self.assertIn("break.i 0x80000 ;;", ssc_asm)
        self.assertIn("mov.i ar.pfs = r34 ;;", ssc_asm)
        self.assertIn("ssc(SSC_EXIT);", console)

    def test_fp_policy_has_three_distinct_profiles_and_link_identity(self) -> None:
        text = source(MAKE_ARCH)
        self.assertIn("IA64_FP_PROFILE ?= runtime", text)
        self.assertIn("compile-only early-exec", text)
        self.assertIn("-mfixed-range=f2-f127", text)
        self.assertIn("-mfixed-range=f2-f5,f16-f31", text)
        self.assertIn("-DGXOS_IA64_EARLY_EXEC_PROFILE=1", text)
        self.assertIn("__ia64_early_exec_profile=1", text)
        self.assertIn("choices=(\"runtime\", \"compile-only\", \"early-exec\")", source(VALIDATOR_PATH))

    def test_fp_context_layout_is_aligned_and_marked_as_storage_only(self) -> None:
        text = source(CONTEXT)
        self.assertIn("struct alignas(16) FpRegisterImage", text)
        self.assertIn("FpRegisterImage fr2_to_fr127[126]", text)
        self.assertIn("offsetof(FpContextState, fpsr) == 2016", text)
        self.assertIn("sizeof(FpContextState) == 2032", text)
        self.assertIn("not implemented, so this type defines storage only", text)
        self.assertIn("not be used to resume an exception or interrupt", text)

    def test_runner_requires_separate_guest_qualification_authorization(self) -> None:
        bash = os.environ.get("BASH") or "bash"
        if os.name == "nt":
            candidate = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Git/bin/bash.exe"
            if candidate.is_file():
                bash = str(candidate)
        with tempfile.TemporaryDirectory() as temp:
            env = os.environ.copy()
            env["IA64_FP_PROFILE"] = "early-exec"
            env.pop("IA64_GUEST_QUALIFICATION_AUTHORIZED", None)
            env["IA64_BUILD_DIR"] = str(Path(temp) / "must-not-build")
            result = subprocess.run([bash, str(RUNNER)], cwd=ROOT, env=env,
                                    capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("separately authorized guest-qualification phase", result.stderr)
        self.assertNotIn("[BUILD] Compiling", result.stdout)

    def test_manifest_rejects_stale_hash_and_mismatched_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            build_dir = Path(temp)
            elf = build_dir / "bin/kernel.elf"
            elf.parent.mkdir()
            elf.write_bytes(b"fixture ELF identity")
            manifest_path = build_dir / "ia64-artifact-manifest.json"
            manifest = {
                "schema": "guidexos-ia64-artifact-v1",
                "profile": "compile-only",
                "boot_contract": "none",
                "cpu_count_requirement": "none",
                "fp_register_policy": "abi-preserved-registers-fixed; scratch-registers-available",
                "elf": "bin/kernel.elf",
                "sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                "static_gates": "NOT_APPLICABLE",
                "runtime_qualified": False,
                "scheduler_qualified": False,
                "interrupt_fp_preservation": False,
                "guest_execution_authorized": False,
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(validator.check_artifact_manifest(elf, manifest_path, "compile-only"), [])

            stale = dict(manifest, sha256="0" * 64)
            manifest_path.write_text(json.dumps(stale), encoding="utf-8")
            self.assertTrue(validator.check_artifact_manifest(elf, manifest_path, "compile-only"))

            mismatched = dict(manifest, profile="early-exec")
            manifest_path.write_text(json.dumps(mismatched), encoding="utf-8")
            self.assertTrue(validator.check_artifact_manifest(elf, manifest_path, "compile-only"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
