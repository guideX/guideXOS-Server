#!/usr/bin/env python3
"""Negative fixtures for the bounded IA-64 Unaligned Reference contract."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


validator = load_module(
    "ia64_unaligned_reference_validator",
    ROOT / "scripts/validate-ia64-unaligned-reference.py")
early_validator = load_module(
    "ia64_early_exec_validator", ROOT / "scripts/validate-ia64-early-exec.py")

SYMBOLS = {
    "ivt": 0x100000,
    "vector": 0x105A00,
    "entry": 0x108000,
    "record": 0x120000,
    "buffer": 0x110000,
    "fault_site": 0x109000,
    "continuation": 0x109010,
    "test": 0x108800,
    "rse_base": 0x130000,
    "rse_top": 0x140000,
}


def valid_fields() -> dict[str, int]:
    bsp = 0x130018
    return {
        "valid": validator.RECORD_VALID,
        "version": 1,
        "size": validator.RECORD_SIZE,
        "sequence": 1,
        "vector": validator.VECTOR_OFFSET,
        "subtype": 0,
        "site": SYMBOLS["fault_site"],
        "IIP": SYMBOLS["fault_site"],
        "slot": 0,
        "IPSR": 0x2008,
        "IFS": 0,
        "IFS.valid": 0,
        "IFA": SYMBOLS["buffer"] + 1,
        "ISR": validator.ISR_READ,
        "ISR.ed": 0,
        "ISR.ei": 0,
        "ISR.ni": 0,
        "ISR.sp": 0,
        "ISR.r": 1,
        "ISR.w": 0,
        "handler.PSR": 0,
        "handler.RSC": 3,
        "handler.BSP": bsp,
        "PR": 0x401,
        "pre.PSR": 0x2000,
        "pre.RSC": 3,
        "pre.BSP": bsp,
        "pre.UNAT": 0x1234,
        "address": SYMBOLS["buffer"] + 1,
        "buffer": SYMBOLS["buffer"],
        "buffer.size": 16,
        "width": 8,
        "target": SYMBOLS["continuation"],
        "return.IPSR": 0x2000,
        "handler.status": validator.HANDLER_VALID,
        "completion": validator.COMPLETION,
        "count": 1,
        "return": validator.RETURN,
        "post.PSR": 0x2000,
        "post.RSC": 3,
        "post.BSP": bsp,
        "post.IVA": SYMBOLS["ivt"],
        "post.UNAT": 0x1234,
        "pre.GR": 0xCAFEBABE,
        "post.GR": 0xCAFEBABE,
        "cleanup": validator.CLEANUP,
        "post.valid": validator.POST_RFI_VALID,
        "reserved": 0,
    }


def output_for(fields: dict[str, int] | None = None) -> str:
    values = fields or valid_fields()
    record = "IA64_UNALIGNED_REFERENCE_RECORD " + " ".join(
        f"{key}=0x{value:X}" for key, value in values.items())
    return "\n".join((
        f"IA64_UNALIGNED_REFERENCE_PRE_FAULT vector=0x5a00 "
        f"subtype=unaligned-data-reference site=0x{SYMBOLS['fault_site']:x} "
        f"address=0x{SYMBOLS['buffer'] + 1:x} width=0x8 "
        f"target=0x{SYMBOLS['continuation']:x} expected_faults=1",
        record,
        "IA64_UNALIGNED_REFERENCE_VALIDATION errors=0x0 test=0x0",
        f"IA64_UNALIGNED_REFERENCE_POST_RETURN target=0x{SYMBOLS['continuation']:x} "
        f"count=0x1 PSR=0x2000 cleanup=0x{validator.CLEANUP:x}",
        "IA64_UNALIGNED_REFERENCE_RESULT=PASS",
    )) + "\n"


def replace_field(output: str, name: str, value: int) -> str:
    return re.sub(rf"\b{re.escape(name)}=0x[0-9A-Fa-f]+",
                  f"{name}=0x{value:X}", output, count=1)


class UnalignedReferenceContractTests(unittest.TestCase):
    def test_valid_fault_frame_and_single_continuation_are_accepted(self) -> None:
        fields, errors = validator.validate_output(output_for(), SYMBOLS)
        self.assertEqual(errors, [])
        self.assertEqual(fields, valid_fields())

    def test_wrong_vector_fault_site_or_slot_is_rejected(self) -> None:
        for field, value in (("vector", 0x5400), ("site", SYMBOLS["fault_site"] + 0x10),
                             ("IIP", SYMBOLS["fault_site"] + 0x10), ("slot", 1)):
            with self.subTest(field=field):
                fields = valid_fields()
                fields[field] = value
                _, errors = validator.validate_output(output_for(fields), SYMBOLS)
                self.assertTrue(errors, errors)

    def test_wrong_ifa_or_isr_access_classification_is_rejected(self) -> None:
        for field, value in (("IFA", SYMBOLS["buffer"] + 2), ("ISR", 0),
                             ("ISR.r", 0), ("ISR.w", 1), ("ISR.sp", 1)):
            with self.subTest(field=field):
                fields = valid_fields()
                fields[field] = value
                _, errors = validator.validate_output(output_for(fields), SYMBOLS)
                self.assertTrue(errors, errors)

    def test_unexpected_fault_count_is_rejected(self) -> None:
        self.assertEqual(validator.validate_expected_fault_count(1), [])
        self.assertTrue(validator.validate_expected_fault_count(0))
        self.assertTrue(validator.validate_expected_fault_count(2))

    def test_missing_or_duplicate_continuation_is_rejected(self) -> None:
        output = output_for()
        post = next(line for line in output.splitlines()
                    if line.startswith("IA64_UNALIGNED_REFERENCE_POST_RETURN"))
        _, missing_errors = validator.validate_output(
            output.replace(post + "\n", ""), SYMBOLS)
        self.assertTrue(any("post-return marker" in error for error in missing_errors))
        _, duplicate_errors = validator.validate_output(output + post + "\n", SYMBOLS)
        self.assertTrue(any("post-return marker" in error for error in duplicate_errors))
        fields = valid_fields()
        fields["count"] = 2
        _, count_errors = validator.validate_output(output_for(fields), SYMBOLS)
        self.assertTrue(any("count" in error for error in count_errors))

    def test_invalid_return_state_and_missing_ac_cleanup_are_rejected(self) -> None:
        fields = valid_fields()
        fields["return.IPSR"] = 0x2008
        fields["post.PSR"] = 0x6000
        _, errors = validator.validate_output(output_for(fields), SYMBOLS)
        self.assertTrue(any("pre-return IPSR" in error for error in errors), errors)
        self.assertTrue(any("post-rfi PSR" in error for error in errors), errors)

        source = (ROOT / "kernel/arch/ia64/unaligned_reference_entry.s").read_text(
            encoding="utf-8")
        self.assertEqual(validator.validate_ac_lifecycle_source(source), [])
        missing_cleanup = source.replace("rsm psr.ac ;;", "nop.i 0 ;;", 1)
        cleanup_errors = validator.validate_ac_lifecycle_source(missing_cleanup)
        self.assertTrue(any("cleared once" in error or "cleanup" in error
                            for error in cleanup_errors), cleanup_errors)

    def test_stale_or_mismatched_artifact_identity_and_authorization_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            artifact = root / "probe.elf"
            artifact.write_bytes(b"standalone probe fixture")
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            manifest_path = root / "probe.manifest.json"
            manifest = {
                "schema": validator.PROBE_SCHEMA,
                "profile": "unaligned-reference-probe",
                "elf": artifact.name,
                "sha256": digest,
                "guest_execution_authorized": "probe-only",
                "system_mode": "-forcesystem",
                "cpu_count": 1,
                "ivt_vector_offset": 0x5A00,
                "fault_subtype": "unaligned-data-reference",
                "faulting_instruction": "ld8 r15=[r14]",
                "access_width": 8,
                "buffer_address_offset": 1,
                "psr_ac_enabled_for_faulting_access": True,
                "expected_faults": 1,
                "expected_continuations": 1,
                "post_rfi_capture_required": True,
                "all_other_vectors": "fail-stop",
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(validator.validate_manifest_identity(
                artifact, manifest_path, digest)["sha256"], digest)

            with self.assertRaises(validator.GateError):
                validator.validate_manifest_identity(artifact, manifest_path, "0" * 64)
            stale = dict(manifest, sha256="0" * 64)
            manifest_path.write_text(json.dumps(stale), encoding="utf-8")
            with self.assertRaises(validator.GateError):
                validator.validate_manifest_identity(artifact, manifest_path, digest)
            wrong_path = dict(manifest, elf="other.elf")
            manifest_path.write_text(json.dumps(wrong_path), encoding="utf-8")
            with self.assertRaises(validator.GateError):
                validator.validate_manifest_identity(artifact, manifest_path, digest)
            unauthorized = dict(manifest, guest_execution_authorized=False)
            manifest_path.write_text(json.dumps(unauthorized), encoding="utf-8")
            with self.assertRaises(validator.GateError):
                validator.validate_manifest_identity(artifact, manifest_path, digest)

    def test_early_exec_rejects_external_interrupt_and_scheduler_activation(self) -> None:
        main = (ROOT / "kernel/core/main.cpp").read_text(encoding="utf-8")
        self.assertEqual(early_validator.validate_early_entry_source(main), [])
        for prohibited in ("ssm psr.i ;;", "interrupts::init();", "process::schedule();"):
            with self.subTest(prohibited=prohibited):
                errors = early_validator.validate_early_entry_source(
                    main.replace("run_unaligned_reference_qualification();", prohibited, 1))
                self.assertTrue(errors, errors)


if __name__ == "__main__":
    unittest.main(verbosity=2)
