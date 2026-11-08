#!/usr/bin/env python3
"""Regression tests for IA-64 checkpoint layout, access rules, and guest records."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR_PATH = ROOT / "scripts" / "validate-ia64-cpu-state.py"
SPEC = importlib.util.spec_from_file_location("ia64_cpu_state_checkpoint_validator", VALIDATOR_PATH)
assert SPEC and SPEC.loader
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


IVT = 0x100000
RSE_BASE = 0x4C6FE00
RSE_TOP = RSE_BASE + 65536


def line(name: str, *, stage: int, valid: int, psr: int = 0, rsc: int = 3,
         bsp: int = RSE_BASE + 0x18, bspstore: int | None = None,
         fpsr: int = validator.EXPECTED_FPSR, iva: int | None = None,
         pfs: int | None = 0, unat: int | None = 0) -> str:
    def value(item: int | None) -> str:
        return f"0x{item:X}" if item is not None else "UNVERIFIED"

    return " ".join((
        f"IA64_CHECKPOINT_{name}", f"stage=0x{stage:X}", f"valid=0x{valid:X}",
        f"PSR=0x{psr:X}", f"PSR.i={(psr >> 14) & 1}", f"PSR.ic={(psr >> 13) & 1}",
        f"RSC=0x{rsc:X}", f"RSC.mode={rsc & 3}", f"BSP=0x{bsp:X}",
        f"BSPSTORE={value(bspstore)}", "RNAT=UNVERIFIED", f"FPSR={value(fpsr)}",
        f"IVA={value(iva)}", f"PFS={value(pfs)}", f"UNAT={value(unat)}",
        f"IVT_EXPECTED=0x{IVT:X}", f"RSE_RANGE=[0x{RSE_BASE:X},0x{RSE_TOP:X})",
    ))


def valid_guest_output() -> str:
    return "\r\n".join((
        line("A", stage=1, valid=0x27, rsc=3, bsp=0x200000,
             bspstore=None, iva=None, pfs=None, unat=None),
        line("B", stage=2, valid=0x2F, rsc=0, bsp=RSE_BASE,
             bspstore=RSE_BASE, iva=None, pfs=None, unat=None),
        line("C", stage=3, valid=0x1E7, rsc=3, bsp=RSE_BASE + 0x18,
             bspstore=None, iva=IVT, psr=1 << 13),
        line("D", stage=4, valid=0x1E7, rsc=3, bsp=RSE_BASE + 0x38,
             bspstore=None, iva=IVT, psr=1 << 13),
    )) + "\r\n"


class CpuStateCheckpointTests(unittest.TestCase):
    def test_current_source_obeys_mode_and_frame_contracts(self) -> None:
        self.assertEqual(validator.validate_checkpoint_sources(ROOT), [])

    def test_prohibited_mode_restricted_register_read_is_rejected(self) -> None:
        sources = validator.read_sources(ROOT)
        sources["asm"] = sources["asm"].replace(
            "mov r3 = ar.bsp ;;", "mov r3 = ar.bspstore ;;", 1)
        errors = validator.validate_checkpoint_sources(ROOT, sources)
        self.assertTrue(any("BSPSTORE/RNAT" in error for error in errors), errors)

    def test_invalid_lazy_to_eager_transition_is_rejected(self) -> None:
        sources = validator.read_sources(ROOT)
        sources["boot"] = sources["boot"].replace("mov ar.rsc = 0 ;;", "mov ar.rsc = 3 ;;", 1)
        errors = validator.validate_checkpoint_sources(ROOT, sources)
        self.assertTrue(any("RSE transition" in error for error in errors), errors)

    def test_collection_enable_does_not_unmask_external_interrupts(self) -> None:
        sources = validator.read_sources(ROOT)
        sources["arch"] = sources["arch"].replace("ssm psr.ic;;", "rsm psr.ic;;", 1)
        errors = validator.validate_checkpoint_sources(ROOT, sources)
        self.assertTrue(any("architecture init" in error for error in errors), errors)

    def test_guest_records_validate_candidate_state_and_symbol_identity(self) -> None:
        records, errors = validator.validate_checkpoint_output(
            valid_guest_output(), ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertEqual(errors, [])
        self.assertEqual(set(records), {"A", "B", "C", "D"})
        self.assertEqual(records["B"]["bspstore_value"], RSE_BASE)

        _, bad_ivt = validator.validate_checkpoint_output(
            valid_guest_output(), ivt=IVT + 0x8000, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("IVT expectation" in error for error in bad_ivt), bad_ivt)
        _, bad_range = validator.validate_checkpoint_output(
            valid_guest_output(), ivt=IVT, rse_base=RSE_BASE + 8, rse_top=RSE_TOP)
        self.assertTrue(any("RSE range" in error for error in bad_range), bad_range)

    def test_malformed_and_architecturally_invalid_records_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing or unexpected fields"):
            validator.parse_checkpoint_record(line("A", stage=1, valid=0x27).replace(" PSR.i=0", ""))

        illegal_rnat = line("C", stage=3, valid=0x1E7, rsc=3, iva=IVT).replace(
            "RNAT=UNVERIFIED", "RNAT=0x1234")
        with self.assertRaisesRegex(ValueError, "RNAT value disagrees"):
            validator.parse_checkpoint_record(illegal_rnat)

        wrong_mode = line("B", stage=2, valid=0x2F, rsc=3, bspstore=RSE_BASE,
                          iva=None, pfs=None, unat=None)
        _, errors = validator.validate_checkpoint_output(
            "\r\n".join((
                line("A", stage=1, valid=0x27, rsc=3, bsp=0x200000,
                     bspstore=None, iva=None, pfs=None, unat=None),
                wrong_mode,
                line("C", stage=3, valid=0x1E7, rsc=3, iva=IVT, psr=1 << 13),
                line("D", stage=4, valid=0x1E7, rsc=3, iva=IVT, psr=1 << 13),
            )), ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("enforced-lazy" in error for error in errors), errors)

        wrong_ic = valid_guest_output().replace("PSR=0x2000 PSR.i=0 PSR.ic=1",
                                                "PSR=0x0 PSR.i=0 PSR.ic=0", 1)
        _, ic_errors = validator.validate_checkpoint_output(
            wrong_ic, ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("PSR.ic" in error for error in ic_errors), ic_errors)


if __name__ == "__main__":
    unittest.main(verbosity=2)
