"""Regression tests for the bounded IA-64 Break exception contract."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts" / "run-ia64-ski.py"
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("ia64_break_ski_runner", RUNNER_PATH)
assert SPEC and SPEC.loader
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


def record_line(name: str, immediate: int, iip: int, slot: int) -> str:
    ipsr = (1 << 13) | (1 << 44) | (slot << 41)
    isr = slot << 41
    return (
        f"IA64_BREAK_RECORD {name} valid=0xbeefc0de4b56414c vector=0x2c00 "
        f"immediate=0x{immediate:x} IIP=0x{iip:x} slot=0x{slot:x} "
        f"IPSR=0x{ipsr:x} IFS.valid=0x0 ISR=0x{isr:x} handler.PSR=0x0 "
        f"RSC=0x3 BSP=0x118000 completion=0xbeefc0de4b00000{slot:x} "
        f"return=0xbeefc0de4b10000{slot:x} count=0x1 PR=0x401"
    )


def valid_output(prefix: str = "IA64_BREAK_RECORD") -> str:
    return "\n".join((
        record_line("slot1", 0x6A17, 0x109000, 1).replace("IA64_BREAK_RECORD", prefix),
        record_line("slot2", 0x6A18, 0x109010, 2).replace("IA64_BREAK_RECORD", prefix),
        "IA64_BREAK_EXCEPTION_VALIDATION errors=0x0 test=0x0",
        "IA64_BREAK_EXCEPTION_RESULT=PASS",
    )) + "\n"


def contract() -> dict[str, object]:
    return runner.make_break_contract(
        {"break_slot1": 0x109000, "break_slot2": 0x109010,
         "rse_base": 0x110000, "rse_top": 0x120000},
        "BREAK_EXCEPTION_PROBE_PASS", "IA64_BREAK_RECORD",
        "IA64_BREAK_EXCEPTION_RESULT=PASS", "IA64_BREAK_EXCEPTION_RESULT=FAIL",
        "IA64_BREAK_EXCEPTION_VALIDATION",
    )


class BreakExceptionContractTests(unittest.TestCase):
    def test_both_slot_continuations_and_records_validate(self) -> None:
        self.assertEqual(runner.validate_break_output(valid_output(), contract()), [])

    def test_incorrect_slot_or_duplicate_record_is_rejected(self) -> None:
        output = valid_output().replace("immediate=0x6a17", "immediate=0x80000")
        errors = runner.validate_break_output(output, contract())
        self.assertTrue(any("slot1 record immediate" in error for error in errors), errors)

        wrong_slot = valid_output().replace("slot=0x1 IPSR=", "slot=0x2 IPSR=", 1)
        errors = runner.validate_break_output(wrong_slot, contract())
        self.assertTrue(any("slot1 record slot" in error for error in errors), errors)

        output = valid_output() + record_line("slot1", 0x6A17, 0x109000, 1) + "\n"
        errors = runner.validate_break_output(output, contract())
        self.assertTrue(any("duplicate IA64_BREAK_RECORD slot1" in error for error in errors), errors)

    def test_rfi_restoration_when_direct_psr_observations_are_present(self) -> None:
        incoming_ipsr = (1 << 13) | (1 << 44) | (1 << 41)
        self.assertEqual(runner.validate_exception_return_state(incoming_ipsr, 1, 0x2000), [])
        lost_ic = runner.validate_exception_return_state(incoming_ipsr, 1, 0x0000)
        self.assertTrue(any("post-rfi PSR did not restore i=0 and ic=1" in error
                            for error in lost_ic), lost_ic)

        correct = valid_output() + (
            "IA64_BREAK_POST_RFI_PSR slot=slot1 PSR=0x2000\n"
            "IA64_BREAK_POST_RFI_PSR slot=slot2 PSR=0x2000\n")
        self.assertEqual(runner.validate_break_output(correct, contract()), [])

        incorrect = valid_output() + (
            "IA64_BREAK_POST_RFI_PSR slot=slot1 PSR=0x0\n"
            "IA64_BREAK_POST_RFI_PSR slot=slot2 PSR=0x2000\n")
        errors = runner.validate_break_output(incorrect, contract())
        self.assertTrue(any("slot1 post-rfi PSR did not restore i=0 and ic=1" in error
                            for error in errors), errors)

        duplicate = correct + "IA64_BREAK_POST_RFI_PSR slot=slot1 PSR=0x2000\n"
        errors = runner.validate_break_output(duplicate, contract())
        self.assertTrue(any("exactly two direct post-rfi" in error for error in errors), errors)

    def test_unexpected_exception_and_duplicate_pass_marker_are_rejected(self) -> None:
        checked = contract()
        checked["stderr_text"] = "unexpected general exception at 0x1000\n"
        errors = runner.validate_break_output(valid_output() * 2, checked)
        self.assertTrue(any("exactly one IA64_BREAK_EXCEPTION_RESULT=PASS" in error
                            for error in errors), errors)
        self.assertTrue(any("unexpected Ski fault/exception diagnostic" in error
                            for error in errors), errors)

    @unittest.skipUnless(sys.platform.startswith("linux"), "resource limits are Linux-only")
    def test_kernel_preflight_requires_documented_limits(self) -> None:
        import resource

        limits = {
            resource.RLIMIT_AS: (2 * 1024 * 1024 * 1024, 2 * 1024 * 1024 * 1024),
            resource.RLIMIT_CPU: (120, 120),
        }
        with patch.object(resource, "getrlimit", side_effect=lambda key: limits[key]):
            recorded = runner.verify_kernel_resource_limits(60)
        self.assertEqual(recorded["address_space_hard_bytes"], 2 * 1024 * 1024 * 1024)
        self.assertEqual(recorded["cpu_hard_seconds"], 120)
        self.assertEqual(recorded["wall_clock_seconds"], 60)

        unbounded = dict(limits)
        unbounded[resource.RLIMIT_AS] = (resource.RLIM_INFINITY, resource.RLIM_INFINITY)
        with patch.object(resource, "getrlimit", side_effect=lambda key: unbounded[key]):
            with self.assertRaisesRegex(runner.GateError, "2 GiB address-space"):
                runner.verify_kernel_resource_limits(60)
        with self.assertRaisesRegex(runner.GateError, "no greater than 60"):
            runner.verify_kernel_resource_limits(61)

    @unittest.skipUnless(sys.platform.startswith("linux"), "process groups are Linux-only")
    def test_runner_classifies_expected_faults_and_records_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            fake = base / "fake-ski"
            fake.write_text(
                "#!/usr/bin/env python3\n"
                "import os, sys\n"
                "from pathlib import Path\n"
                "args=sys.argv[1:]\n"
                "counts=Path(args[args.index('-icnt')+1])\n"
                "faults=int(os.environ.get('IA64_BREAK_FAULT_COUNT','2'))\n"
                "counts.write_text(f'ski_$faults$ 0 {faults:x}\\nski_$insts$ 0 1adef\\n')\n"
                "sys.stdout.write(Path(os.environ['IA64_BREAK_FIXTURE']).read_text())\n",
                encoding="utf-8",
            )
            fake.chmod(0o755)
            fixture = base / "guest-output.txt"
            fixture.write_text(valid_output(), encoding="utf-8")
            capture = base / "capture"
            environment = os.environ.copy()
            environment["IA64_BREAK_FIXTURE"] = str(fixture)
            environment["IA64_BREAK_FAULT_COUNT"] = "2"
            old_environment = os.environ.copy()
            try:
                os.environ.update(environment)
                status = runner.run_simulator(
                    [str(fake)], 5, capture, "break-contract-test",
                    artifact_hash="a" * 64, break_exception_contract=contract())
                os.environ["IA64_BREAK_FAULT_COUNT"] = "1"
                failed_capture = base / "failed-capture"
                failed_status = runner.run_simulator(
                    [str(fake)], 5, failed_capture, "break-contract-wrong-fault-count",
                    artifact_hash="a" * 64, break_exception_contract=contract())
            finally:
                os.environ.clear()
                os.environ.update(old_environment)
            result = json.loads((capture / "run-result.json").read_text(encoding="utf-8"))
            self.assertEqual(status, 0)
            self.assertEqual(result["classification"], "BREAK_EXCEPTION_PROBE_PASS")
            self.assertEqual(result["ski_fault_count"], 2)
            self.assertEqual(result["ski_instruction_count"], 0x1ADEF)
            self.assertIsNone(result["break_exception_post_rfi_psr_observations"])
            self.assertTrue(result["cleanup_complete"])
            self.assertFalse(result["timed_out"])
            failed_result = json.loads(
                (failed_capture / "run-result.json").read_text(encoding="utf-8"))
            self.assertEqual(failed_status, 1)
            self.assertEqual(failed_result["classification"],
                             "BREAK_EXCEPTION_QUALIFICATION_FAIL")
            self.assertEqual(failed_result["ski_fault_count"], 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
