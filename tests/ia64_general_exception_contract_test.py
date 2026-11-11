"""Negative fixtures for the bounded IA-64 General Exception contract."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "ia64_general_exception_contract", ROOT / "scripts" / "validate-ia64-general-exception.py")
assert SPEC and SPEC.loader
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "ia64_general_exception_runner", ROOT / "scripts" / "run-ia64-ski.py")
assert RUNNER_SPEC and RUNNER_SPEC.loader
runner = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(runner)

EXPECTED = {
    "ivt": 0x100000,
    "fault_site": 0x109000,
    "continuation": 0x109010,
    "rse_base": 0x110000,
    "rse_top": 0x120000,
}


def record_line(**overrides: int) -> str:
    ipsr = (1 << 13)
    fields = {
        "valid": validator.RECORD_VALID,
        "version": 1,
        "size": 256,
        "vector": 0x5400,
        "subtype": 0,
        "IIP": EXPECTED["fault_site"],
        "slot": 0,
        "IPSR": ipsr,
        "IFS": 0,
        "IFS.valid": 0,
        "ISR": 0,
        "handler.PSR": 0,
        "handler.RSC": 3,
        "handler.BSP": 0x118000,
        "PR": 0x401,
        "site": EXPECTED["fault_site"],
        "target": EXPECTED["continuation"],
        "handler.status": validator.HANDLER_VALID,
        "pre.BSP": 0x118000,
        "pre.RSC": 3,
        "pre.UNAT": 0,
        "pre.GR": 0x47454E4552414C44,
        "completion": validator.COMPLETION,
        "count": 1,
        "return": validator.RETURN,
        "post.PSR": 0x2000,
        "post.RSC": 3,
        "post.BSP": 0x118000,
        "post.IVA": EXPECTED["ivt"],
        "post.UNAT": 0,
        "post.GR": 0x47454E4552414C44,
        "post.valid": validator.POST_RFI_VALID,
    }
    fields.update(overrides)
    return "IA64_GENERAL_EXCEPTION_RECORD " + " ".join(
        f"{name}=0x{value:x}" for name, value in fields.items())


def valid_output(*, probe: bool = False) -> str:
    lines = [
        "IA64_GENERAL_EXCEPTION_PRE_FAULT vector=0x5400 subtype=illegal-operation "
        "site=0x109000 target=0x109010 expected_faults=1",
        record_line(),
        "IA64_GENERAL_EXCEPTION_VALIDATION errors=0x0 test=0x0",
        "IA64_GENERAL_EXCEPTION_POST_RETURN target=0x109010 count=0x1 PSR=0x2000",
        "IA64_GENERAL_EXCEPTION_RESULT=PASS",
    ]
    if probe:
        lines.append("IA64_GENERAL_EXCEPTION_PROBE_RESULT=PASS")
    return "\n".join(lines) + "\n"


def general_contract(*, probe: bool = True) -> dict[str, object]:
    return {
        "expected_faults": 1,
        "symbols": EXPECTED,
        "probe": probe,
        "success_classification": "GENERAL_EXCEPTION_PROBE_PASS",
        "require_intentional_exit": True,
        "ssc_exit_function": 66,
    }


class GeneralExceptionContractTests(unittest.TestCase):
    def test_valid_record_and_markers_pass(self) -> None:
        fields, errors = validator.validate_output(valid_output(probe=True), EXPECTED, probe=True)
        self.assertEqual(errors, [])
        self.assertIsNotNone(fields)

    def test_wrong_subtype_vector_or_slot_is_rejected(self) -> None:
        for overrides, expected_text in (
                ({"subtype": 1}, "record subtype"),
                ({"vector": 0x5500}, "record vector"),
                ({"slot": 1}, "record slot")):
            with self.subTest(overrides=overrides):
                fields, errors = validator.validate_output(
                    valid_output().replace(record_line(), record_line(**overrides)), EXPECTED)
                self.assertIsNotNone(fields)
                self.assertTrue(any(expected_text in error for error in errors), errors)

    def test_missing_or_duplicate_record_is_rejected(self) -> None:
        one = valid_output().replace(record_line() + "\n", "")
        _, errors = validator.validate_output(one, EXPECTED)
        self.assertTrue(any("found 0" in error for error in errors), errors)

        duplicate = valid_output() + record_line() + "\n"
        _, errors = validator.validate_output(duplicate, EXPECTED)
        self.assertTrue(any("found 2" in error for error in errors), errors)

    def test_invalid_frame_and_ifs_state_are_rejected(self) -> None:
        bad_frame = valid_output().replace(
            record_line(), record_line(valid=0, **{"IFS.valid": 1}))
        _, errors = validator.validate_output(bad_frame, EXPECTED)
        self.assertTrue(any("record valid" in error for error in errors), errors)
        self.assertTrue(any("record IFS.valid" in error for error in errors), errors)

    def test_wrong_post_rfi_psr_and_unauthorized_continuation_are_rejected(self) -> None:
        wrong_psr = valid_output().replace(record_line(), record_line(**{"post.PSR": 0}))
        _, errors = validator.validate_output(wrong_psr, EXPECTED)
        self.assertTrue(any("record post.PSR" in error for error in errors), errors)

        wrong_target = valid_output().replace(
            record_line(), record_line(target=0x109020)).replace(
                "POST_RETURN target=0x109010", "POST_RETURN target=0x109020")
        _, errors = validator.validate_output(wrong_target, EXPECTED)
        self.assertTrue(any("record target" in error for error in errors), errors)
        self.assertTrue(any("post-return marker" in error for error in errors), errors)

    def test_manifest_identity_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            artifact = root / "probe.elf"
            artifact.write_bytes(b"fixture ELF bytes")
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            manifest = {
                "schema": validator.PROBE_SCHEMA,
                "profile": "general-exception-probe",
                "elf": artifact.name,
                "sha256": digest,
                "guest_execution_authorized": "probe-only",
                "system_mode": "-forcesystem",
                "cpu_count": 1,
                "ivt_vector_offset": 0x5400,
                "fault_subtype": "illegal-operation",
                "faulting_instruction": "mov r14=ar.bspstore",
                "expected_faults": 1,
                "expected_continuations": 1,
                "post_rfi_capture_required": True,
                "all_other_vectors": "fail-stop",
            }
            manifest_path = root / "probe.manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(validator.GateError, "artifact identity mismatch"):
                validator.validate_manifest_identity(artifact, manifest_path, "0" * 64)
            with self.assertRaisesRegex(validator.GateError, "manifest identity"):
                manifest["elf"] = "other.elf"
                manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                validator.validate_manifest_identity(artifact, manifest_path, digest)

    @unittest.skipUnless(sys.platform.startswith("linux"), "process groups are Linux-only")
    def test_unexpected_fault_count_fails_even_when_output_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fake = root / "fake-ski"
            fake.write_text(
                "#!/usr/bin/env python3\n"
                "import os, sys\n"
                "from pathlib import Path\n"
                "args=sys.argv[1:]\n"
                "counts=Path(args[args.index('-icnt')+1])\n"
                "faults=int(os.environ.get('IA64_GENERAL_FAULT_COUNT','1'))\n"
                "counts.write_text(f'ski_$faults$ 0 {faults:x}\\nski_$insts$ 0 10\\n')\n"
                "sys.stdout.write(Path(os.environ['IA64_GENERAL_FIXTURE']).read_text())\n"
                "sys.stdout.write('kernel exited with status 0\\n')\n",
                encoding="utf-8")
            fake.chmod(0o755)
            fixture = root / "guest-output.txt"
            fixture.write_text(valid_output(probe=True), encoding="utf-8")
            old_env = os.environ.copy()
            capture = root / "capture"
            try:
                os.environ["IA64_GENERAL_FAULT_COUNT"] = "2"
                os.environ["IA64_GENERAL_FIXTURE"] = str(fixture)
                status = runner.run_simulator(
                    [str(fake)], 5, capture, "general-exception-count-negative",
                    general_exception_contract=general_contract())
            finally:
                os.environ.clear()
                os.environ.update(old_env)
            self.assertEqual(status, 1)
            run_result = json.loads((capture / "run-result.json").read_text(encoding="utf-8"))
            self.assertEqual(run_result["classification"], "GENERAL_EXCEPTION_QUALIFICATION_FAIL")
            self.assertTrue(any("fault count" in error
                                for error in run_result["general_exception_evidence_errors"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
