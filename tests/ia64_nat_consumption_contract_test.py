#!/usr/bin/env python3
"""Negative fixtures for the bounded IA-64 Register NaT Consumption contract."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ia64_nat_consumption_validator", ROOT / "scripts/validate-ia64-nat-consumption.py")
assert SPEC and SPEC.loader
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)

SYMBOLS = {
    "ivt": 0x100000,
    "vector": 0x105600,
    "entry": 0x108000,
    "record": 0x130000,
    "spec_site": 0x109000,
    "fault_site": 0x109040,
    "continuation": 0x109050,
    "target": 0x110000,
    "rse_base": 0x120000,
    "rse_top": 0x130000,
}


def valid_fields() -> dict[str, int]:
    bsp = 0x128018
    return {
        "valid": validator.VALID,
        "version": validator.RECORD_VERSION,
        "size": validator.RECORD_SIZE,
        "sequence": 1,
        "vector": validator.VECTOR,
        "subtype": validator.REGISTER_NAT_CONSUMPTION,
        "site": SYMBOLS["fault_site"],
        "IIP": SYMBOLS["fault_site"],
        "slot": 0,
        "IPSR": 0x2000,
        "IIPA": SYMBOLS["fault_site"] - 0x10,
        "IFS": 0,
        "IFS.valid": 0,
        "IFA": SYMBOLS["record"],
        "IFA.valid": 0,
        "ISR": validator.ISR_LD8_SLOT0_SKI,
        "ISR.ed": 0,
        "ISR.ei": 0,
        "ISR.so": 0,
        "ISR.ni": 0,
        "ISR.ir": 0,
        "ISR.rs": 0,
        "ISR.sp": 0,
        "ISR.na": 0,
        "ISR.r": 1,
        "ISR.w": 0,
        "ISR.x": 0,
        "IIB0": 0,
        "IIB1": 0,
        "IIB0.available": 0,
        "IIB1.available": 0,
        "handler.PSR": 0,
        "handler.RSC": 3,
        "handler.BSP": bsp,
        "PR": 0x401,
        "source.GR": 8,
        "destination.GR": 9,
        "spec.site": SYMBOLS["spec_site"],
        "pre.PSR": 0x2000,
        "spec.PSR": 0x80000002000,
        "post.spec.PSR": 0x2000,
        "pre.RSC": 3,
        "pre.BSP": bsp,
        "pre.UNAT": 0x1234,
        "source.NAT.before": 1,
        "source.NAT.handler": 1,
        "source.NAT.after": 1,
        "source.NAT.cleared": 0,
        "target": SYMBOLS["continuation"],
        "return.IPSR": 0x2000,
        "handler.status": validator.HANDLER_VALID,
        "completion": validator.COMPLETION,
        "count": 1,
        "return": validator.RETURNED,
        "post.PSR": 0x2000,
        "post.RSC": 3,
        "post.BSP": bsp,
        "post.UNAT": 0x1234,
        "pre.GR": 0xCAFE1234,
        "post.GR": 0xCAFE1234,
        "test": 0,
        "post.valid": validator.POST_VALID,
        "source.initial": SYMBOLS["target"],
        "post.IVA": SYMBOLS["ivt"],
    }


def record_line(fields: dict[str, int] | None = None) -> str:
    values = fields or valid_fields()
    return "IA64_NAT_CONSUMPTION_RECORD " + " ".join(
        f"{name}=0x{value:X}" for name, value in values.items())


def valid_output(*, probe: bool = False, fields: dict[str, int] | None = None) -> str:
    suffix = "PROBE_RESULT" if probe else "RESULT"
    return "\n".join((
        "IA64_NAT_CONSUMPTION_PRE_FAULT vector=0x5600 subtype=register-nat-consumption "
        "speculative=ld8.s PSR.ed=1 source=GR8 consumer=ld8 slot=0 expected_faults=1",
        record_line(fields),
        "IA64_NAT_CONSUMPTION_VALIDATION errors=0x0 test=0x0",
        f"IA64_NAT_CONSUMPTION_POST_RETURN target=0x{SYMBOLS['continuation']:x} "
        "count=0x1 PSR=0x2000",
        f"IA64_NAT_CONSUMPTION_{suffix}=PASS",
    )) + "\n"


class NatConsumptionContractTests(unittest.TestCase):
    def test_valid_kernel_and_probe_records_pass(self) -> None:
        for probe in (False, True):
            with self.subTest(probe=probe):
                fields, errors = validator.validate_output(valid_output(probe=probe), SYMBOLS,
                                                           probe=probe)
                self.assertEqual(errors, [])
                self.assertEqual(fields, valid_fields())

    def test_wrong_vector_subtype_site_slot_or_isr_is_rejected(self) -> None:
        for field, value in (("vector", 0x5000), ("subtype", 42),
                             ("site", SYMBOLS["fault_site"] + 0x10),
                             ("IIP", SYMBOLS["fault_site"] + 0x10), ("slot", 1),
                             ("ISR", 0x400000000)):
            with self.subTest(field=field):
                fields = valid_fields()
                fields[field] = value
                _, errors = validator.validate_output(valid_output(fields=fields), SYMBOLS)
                self.assertTrue(errors, errors)

    def test_unsupported_natpage_classification_is_rejected(self) -> None:
        fields = valid_fields()
        fields["ISR.na"] = 1
        _, errors = validator.validate_output(valid_output(fields=fields), SYMBOLS)
        self.assertTrue(any("ISR" in error for error in errors), errors)

    def test_invalid_frame_ifs_and_continuation_are_rejected(self) -> None:
        for field, value in (("valid", 0), ("IFS.valid", 1),
                             ("target", SYMBOLS["continuation"] + 0x10)):
            with self.subTest(field=field):
                fields = valid_fields()
                fields[field] = value
                _, errors = validator.validate_output(valid_output(fields=fields), SYMBOLS)
                self.assertTrue(errors, errors)

    def test_bad_post_rfi_state_and_nat_clear_are_rejected(self) -> None:
        for field, value in (("post.PSR", 0x6000), ("return.IPSR", 0x80000002000),
                             ("source.NAT.after", 0), ("source.NAT.cleared", 1)):
            with self.subTest(field=field):
                fields = valid_fields()
                fields[field] = value
                _, errors = validator.validate_output(valid_output(fields=fields), SYMBOLS)
                self.assertTrue(errors, errors)

    def test_missing_duplicate_records_and_fault_counts_are_rejected(self) -> None:
        output = valid_output()
        line = record_line()
        _, missing = validator.validate_output(output.replace(line + "\n", ""), SYMBOLS)
        self.assertTrue(any("expected one" in error for error in missing), missing)
        _, duplicate = validator.validate_output(output + line + "\n", SYMBOLS)
        self.assertTrue(any("expected one" in error for error in duplicate), duplicate)
        self.assertEqual(validator.validate_expected_fault_count(1, 1), [])
        self.assertTrue(validator.validate_expected_fault_count(0, 1))
        self.assertTrue(validator.validate_expected_fault_count(2, 1))

    def test_probe_manifest_rejects_mismatched_identity_and_authorization(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            elf = root / "nat-probe.elf"
            elf.write_bytes(b"bounded NaT probe fixture")
            digest = hashlib.sha256(elf.read_bytes()).hexdigest()
            manifest_path = root / "nat-probe.manifest.json"
            manifest = {
                "schema": "guidexos-ia64-nat-consumption-probe-v1",
                "profile": "nat-consumption-probe",
                "elf": elf.name,
                "sha256": digest,
                "guest_execution_authorized": "probe-only",
                "ivt_vector_offset": 0x5600,
                "fault_subtype": "register-nat-consumption",
                "speculative_load_forced_by_psr_ed": True,
                "speculative_instruction": "ld8.s r8=[r15]",
                "non_speculative_consumer": "ld8 r9=[r8]",
                "source_register": 8,
                "destination_register": 9,
                "expected_faults": 1,
                "expected_continuations": 1,
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            validator.validate_probe_manifest(elf, manifest_path, digest)
            with self.assertRaisesRegex(validator.GateError, "SHA-256 mismatch"):
                validator.validate_probe_manifest(elf, manifest_path, "0" * 64)
            manifest["guest_execution_authorized"] = True
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(validator.GateError):
                validator.validate_probe_manifest(elf, manifest_path, digest)
            manifest["guest_execution_authorized"] = "probe-only"
            manifest["elf"] = "other.elf"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(validator.GateError, "does not identify"):
                validator.validate_probe_manifest(elf, manifest_path, digest)

    def test_static_sources_keep_handler_profile_scoped_and_rse_bounded(self) -> None:
        asm = (ROOT / "kernel/arch/ia64/nat_consumption_entry.s").read_text(encoding="utf-8")
        handler = asm.split("ia64_nat_consumption_entry:", 1)[1].split(
            ".endp ia64_nat_consumption_entry", 1)[0]
        code = re.sub(r"/\*.*?\*/|//.*", "", handler, flags=re.S)
        self.assertNotRegex(code, r"\bmov\s+r\d+\s*=\s*cr\.iib[01]")
        self.assertIn("tnat.nz p6, p7 = r8 ;;", code)
        self.assertNotRegex(code, r"\b(?:alloc|br\.call|mov\s+ar\.(?:rnat|bspstore))\b")
        self.assertRegex((ROOT / "kernel/arch/ia64/boot.s").read_text(encoding="utf-8"),
                         r"\.ifdef GXOS_IA64_EARLY_EXEC_PROFILE\s+"
                         r"IVT_ENTRY_NAT_CONSUMPTION ivt_nat_consumption,\s*0x5600")


if __name__ == "__main__":
    unittest.main(verbosity=2)
