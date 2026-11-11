#!/usr/bin/env python3
"""Contract tests for Ski direct loading, explicit authorization, and cleanup."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts" / "run-ia64-ski.py"
LOADER_RECORD_PATH = ROOT / "docs" / "ia64-3r1-ski-loader-record.json"
SMOKE_SOURCE_PATH = ROOT / "tests" / "ia64_ski_smoke" / "smoke.S"
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("ia64_ski_runner", RUNNER_PATH)
assert SPEC and SPEC.loader
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


def make_elf(path: Path, *, vaddr: int = 0x100000, paddr: int | None = None,
             memsz: int = 4, filesz: int = 4, entry: int | None = None,
             machine: int = 50, elf_type: int = 2) -> None:
    """Write the smallest ELF accepted by the runner's generic IA-64 parser."""
    if paddr is None:
        paddr = vaddr
    phoff = 64
    shoff = phoff + 56
    data_offset = 0x1000
    image = bytearray(data_offset + filesz)
    image[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<HHIQQQIHHHHHH", image, 16,
                     elf_type, machine, 1, vaddr if entry is None else entry, phoff, shoff, 0,
                     64, 56, 1, 64, 1, 0)
    struct.pack_into("<IIQQQQQQ", image, phoff,
                     1, 5, data_offset, vaddr, paddr, filesz, memsz, 0x1000)
    image[data_offset:] = bytes([0x11]) * filesz
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)


def make_overlapping_elf(path: Path) -> None:
    phoff = 64
    phnum = 2
    shoff = phoff + phnum * 56
    data_offset = 0x1000
    image = bytearray(data_offset + 8)
    image[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<HHIQQQIHHHHHH", image, 16,
                     2, 50, 1, 0x100000, phoff, shoff, 0,
                     64, 56, phnum, 64, 1, 0)
    struct.pack_into("<IIQQQQQQ", image, phoff,
                     1, 5, data_offset, 0x100000, 0x100000, 4, 8, 1)
    struct.pack_into("<IIQQQQQQ", image, phoff + 56,
                     1, 4, data_offset + 4, 0x100004, 0x100004, 4, 8, 1)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)


def make_manifest(path: Path, artifact: Path, profile: str = "early-exec") -> dict:
    value = {
        "schema": "guidexos-ia64-artifact-v1",
        "profile": profile,
        "boot_contract": "ski-physical-entry-single-cpu-no-preempt" if profile == "early-exec" else "none",
        "cpu_count_requirement": "exactly-one-CPU-external-SKI-precondition-runtime-unverified" if profile == "early-exec" else "none",
        "fp_register_policy": "abi-preserved-registers-fixed; scratch-registers-available",
        "elf": "bin/kernel.elf",
        "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        "static_gates": "PASS" if profile == "early-exec" else "NOT_APPLICABLE",
        "cpu_state_checkpoint_schema": "guidexos-ia64-cpu-state-checkpoints-v1" if profile == "early-exec" else "none",
        "cpu_state_static_gates": "PASS" if profile == "early-exec" else "NOT_APPLICABLE",
        "nat_consumption_static_gates": "PASS" if profile == "early-exec" else "NOT_APPLICABLE",
        "nat_consumption_expected_faults": 1 if profile == "early-exec" else 0,
        "expected_exception_faults": 5 if profile == "early-exec" else 0,
        "runtime_qualified": False,
        "scheduler_qualified": False,
        "interrupt_fp_preservation": False,
        "guest_execution_authorized": False,
    }
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")
    return value


def make_fake_ski(prefix: Path, sentinel: Path) -> str:
    bindir = prefix / "bin"
    bindir.mkdir(parents=True)
    script = bindir / "ski"
    script.write_text(
        "#!/bin/sh\n"
        "printf 'launched\\n' > \"$IA64_SKI_LAUNCH_SENTINEL\"\n"
        "exit 0\n",
        encoding="utf-8",
    )
    script.chmod(0o755)
    (bindir / "bski").symlink_to("ski")
    (bindir / "bskinc").symlink_to("ski")
    return hashlib.sha256(script.read_bytes()).hexdigest()


class SkiLoaderContractTests(unittest.TestCase):
    def test_qualification_record_pins_ski_build_and_kernel_handoff(self) -> None:
        record = json.loads(LOADER_RECORD_PATH.read_text(encoding="utf-8"))
        self.assertEqual(record["ski_source"]["commit"], "dfc2902ea1423d9b32543a5daf8026213f2b37a1")
        self.assertEqual(record["ski_source"]["describe"], "v1.5.1-32-gdfc2902")
        self.assertEqual(record["ski_version"], "1.5.1")
        self.assertEqual(record["ski_sha256"], "ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45")
        self.assertEqual(record["single_cpu_contract"], "SOURCE_ENFORCED_ONE_CPU")
        self.assertEqual(record["system_mode_smoke"], "PASS")
        self.assertFalse(record["kernel_execution_authorized"])
        self.assertEqual(record["kernel_entry"], "0x108000")
        self.assertEqual(record["handoff"]["global_pointer"], "_start loads __gp=0x672000 before C code")
        smoke = record["smoke_execution"]
        self.assertEqual(smoke["console_marker"], "SKI_SYSTEM_MODE_SMOKE_PASS")
        self.assertEqual(smoke["ssc_exit"], 66)
        self.assertEqual(smoke["simulator_exit_status"], 0)
        self.assertEqual(smoke["guest_faults"], 0)

    def test_smoke_source_exercises_system_privilege_console_and_exit(self) -> None:
        text = SMOKE_SOURCE_PATH.read_text(encoding="utf-8")
        self.assertIn("rsm psr.i", text)
        self.assertIn("mov r8 = psr", text)
        self.assertIn("SSC_CONSOLE_INIT, 20", text)
        self.assertIn("SSC_PUTCHAR, 31", text)
        self.assertIn("SSC_EXIT, 66", text)
        self.assertIn("SKI_SYSTEM_MODE_SMOKE_PASS", text)
        runner_source = RUNNER_PATH.read_text(encoding="utf-8")
        self.assertIn('"-forcesystem"', runner_source)
        self.assertIn("--authorize-kernel-guest-execution", runner_source)
        self.assertIn('kernel.add_argument("--artifact", type=Path, required=True)', runner_source)
        self.assertNotIn("build-ia64.sh", runner_source)

        wrapper = (ROOT / "scripts" / "run-ia64_in_linux.sh").read_text(encoding="utf-8")
        self.assertIn("run-ia64-ski.py", wrapper)
        self.assertNotIn('ski "$KERNEL"', wrapper)
        self.assertNotIn('bski "$KERNEL"', wrapper)

    def test_stock_linux_bootloader_overlaps_the_kernel_ivt_load_segment(self) -> None:
        stock_loader = (0x100000, 0x10A990)
        kernel_text = (0x100000, 0x3CDE30)
        self.assertTrue(runner.ranges_overlap(stock_loader, kernel_text))

        record = json.loads(LOADER_RECORD_PATH.read_text(encoding="utf-8"))
        loader_range = record["stock_linux_bootloader"]["load_segment"]
        kernel_range = record["kernel_load_segments"][0]
        self.assertTrue(runner.ranges_overlap(
            (int(loader_range["start"], 16), int(loader_range["end_exclusive"], 16)),
            (int(kernel_range["start"], 16), int(kernel_range["end_exclusive"], 16)),
        ))

    def test_verified_snapshot_preserves_the_selected_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            artifact = Path(temp) / "candidate.elf"
            artifact.write_bytes(b"immutable fixture")
            expected = hashlib.sha256(artifact.read_bytes()).hexdigest()
            with runner.verified_snapshot(artifact, expected) as snapshot:
                self.assertNotEqual(snapshot.resolve(), artifact.resolve())
                self.assertEqual(snapshot.read_bytes(), artifact.read_bytes())
                self.assertFalse(snapshot.stat().st_mode & 0o222)
            self.assertEqual(hashlib.sha256(artifact.read_bytes()).hexdigest(), expected)

    def test_direct_elf_validation_rejects_physical_translation_and_bounds(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            artifact = Path(temp) / "valid.elf"
            make_elf(artifact)
            self.assertEqual(len(runner.validate_direct_system_elf(artifact)), 1)

            make_elf(artifact, vaddr=0x100000, paddr=0x200000)
            with self.assertRaisesRegex(runner.GateError, "p_vaddr/p_paddr differ"):
                runner.validate_direct_system_elf(artifact)

            make_elf(artifact, vaddr=(1 << 63) - 0x1000, memsz=0x2000)
            with self.assertRaisesRegex(runner.GateError, "-palen 63"):
                runner.validate_direct_system_elf(artifact)

    def test_direct_elf_validation_rejects_bad_headers_ranges_and_entry(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            artifact = Path(temp) / "candidate.elf"

            make_elf(artifact, memsz=4, filesz=8)
            with self.assertRaisesRegex(runner.GateError, "filesz/memsz"):
                runner.validate_direct_system_elf(artifact)

            make_elf(artifact)
            image = bytearray(artifact.read_bytes())
            header = struct.unpack_from("<IIQQQQQQ", image, 64)
            struct.pack_into("<IIQQQQQQ", image, 64, *header[:2], 0x2000, *header[3:])
            artifact.write_bytes(image)
            with self.assertRaisesRegex(runner.GateError, "file bytes exceed"):
                runner.validate_direct_system_elf(artifact)

            make_elf(artifact, entry=0x200000)
            with self.assertRaisesRegex(runner.GateError, "entry is not inside"):
                runner.validate_direct_system_elf(artifact)

            make_elf(artifact, machine=62)
            with self.assertRaisesRegex(runner.GateError, "EM_IA_64 ET_EXEC"):
                runner.validate_direct_system_elf(artifact)

            make_overlapping_elf(artifact)
            with self.assertRaisesRegex(runner.GateError, "PT_LOAD ranges overlap"):
                runner.validate_direct_system_elf(artifact)

    @unittest.skipUnless(sys.platform.startswith("linux"), "the Ski runner is Linux-only")
    def test_rejections_happen_before_the_simulator_can_launch_a_guest(self) -> None:
        cases = (
            "invalid hash", "profile mismatch", "unsafe memory map",
            "missing loader evidence", "wrong simulator mode", "absent authorization",
            "compile-only profile", "missing CPU-state checkpoint gate",
            "missing NaT Consumption static gate",
            "incorrect simulator identity",
        )
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp:
                base = Path(temp)
                build_dir = base / "build"
                artifact = build_dir / "bin" / "kernel.elf"
                unsafe = case == "unsafe memory map"
                make_elf(artifact, vaddr=(1 << 63) - 0x1000 if unsafe else 0x100000,
                         memsz=0x2000 if unsafe else 4)
                profile = "compile-only" if case == "compile-only profile" else "early-exec"
                manifest_path = build_dir / "ia64-artifact-manifest.json"
                manifest = make_manifest(manifest_path, artifact, profile)
                if case == "missing CPU-state checkpoint gate":
                    manifest.pop("cpu_state_static_gates")
                    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                if case == "missing NaT Consumption static gate":
                    manifest.pop("nat_consumption_static_gates")
                    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                expected_hash = manifest["sha256"]

                prefix = base / "ski"
                sentinel = base / "simulator-launched"
                sim_hash = make_fake_ski(prefix, sentinel)
                record = {
                    "schema": "guidexos-ia64-ski-loader-v1",
                    "outcome": "SKI_LOADER_READY",
                    "loader_kind": "ski-direct-system-elf",
                    "kernel_sha256": expected_hash,
                    "kernel_profile": profile,
                    "ski_sha256": sim_hash,
                    "ski_source": {
                        "commit": "dfc2902ea1423d9b32543a5daf8026213f2b37a1",
                        "source_tree_clean": True,
                    },
                    "ski_version": "1.5.1",
                    "system_mode_smoke": "PASS",
                    "single_cpu_contract": "SOURCE_ENFORCED_ONE_CPU",
                    "direct_loader": {
                        "uses_system_mode_option": "-forcesystem",
                        "candidate_paddr_equals_vaddr": True,
                        "guest_resident_loader_ranges": [],
                    },
                    "kernel_execution_authorized": False,
                }
                if case == "incorrect simulator identity":
                    record["ski_sha256"] = "0" * 64
                record_path = base / "loader-record.json"
                record_path.write_text(json.dumps(record), encoding="utf-8")
                if case == "missing loader evidence":
                    record_path = base / "missing-loader-record.json"

                requested_profile = "runtime" if case == "profile mismatch" else profile
                requested_hash = "0" * 64 if case == "invalid hash" else expected_hash
                command = [
                    sys.executable, str(RUNNER_PATH), "kernel",
                    "--artifact", str(artifact), "--manifest", str(manifest_path),
                    "--profile", requested_profile, "--expected-sha256", requested_hash,
                    "--loader-record", str(record_path), "--ski-prefix", str(prefix),
                    "--capture-dir", str(base / "capture"),
                ]
                if case != "absent authorization":
                    command.append("--authorize-kernel-guest-execution")
                if case == "wrong simulator mode":
                    command.extend(("--mode", "user"))
                environment = os.environ.copy()
                environment["IA64_SKI_LAUNCH_SENTINEL"] = str(sentinel)
                result = subprocess.run(command, cwd=ROOT, env=environment,
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertFalse(sentinel.exists(), f"simulator launched for {case}")

    @unittest.skipUnless(sys.platform.startswith("linux"), "process groups are Linux-only")
    def test_timeout_captures_both_streams_and_kills_the_process_group(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            sleeper = base / "fake-simulator"
            child_pid_file = base / "child.pid"
            sleeper.write_text(
                "#!/bin/sh\n"
                "echo guest-stdout\n"
                "echo guest-stderr >&2\n"
                f"sleep 30 & echo $! > {child_pid_file}\n"
                "wait\n",
                encoding="utf-8",
            )
            sleeper.chmod(0o755)
            capture = base / "capture"
            result = runner.run_simulator([str(sleeper)], 0.5, capture, "timeout-test")
            self.assertEqual(result, 1)
            self.assertIn("guest-stdout", (capture / "stdout.log").read_text(encoding="utf-8"))
            self.assertIn("guest-stderr", (capture / "stderr.log").read_text(encoding="utf-8"))
            metadata = json.loads((capture / "run-result.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["classification"], "TIMEOUT")
            pid = int(child_pid_file.read_text(encoding="utf-8"))
            time.sleep(0.2)
            try:
                state = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8").split()[2]
            except FileNotFoundError:
                state = "gone"
            self.assertIn(state, ("Z", "gone"), f"simulator child remains running (state={state})")


if __name__ == "__main__":
    unittest.main(verbosity=2)
