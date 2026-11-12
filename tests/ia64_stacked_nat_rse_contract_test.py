#!/usr/bin/env python3
"""Runtime-output and boot-order fixtures for stacked NaT/RSE qualification."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ia64_stacked_nat_rse_validator", ROOT / "scripts/validate-ia64-stacked-nat-rse.py")
assert SPEC and SPEC.loader
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


def valid_output() -> str:
    record = {
        "magic": 0x53544E4154525345,
        "version": 1,
        "size": 320,
        "sequence": 1,
        "result": 0,
        "stage": 4,
        "static.NAT.before": 1,
        "stacked.NAT.before": 1,
        "stacked.NAT.after": 1,
        "static.NAT.after": 1,
        "ordinary.before": 0x4758534F534E4154,
        "ordinary.after": 0x4758534F534E4154,
        "frame.parent": 3,
        "frame.child": 96,
        "RSC.original": 3,
        "RSC.lazy": 0,
        "RSC.child": 0,
        "RSC.restored": 3,
        "BSP.parent": 0x11E000,
        "BSPSTORE.before": 0x11E000,
        "BSP.child": 0x11E018,
        "BSPSTORE.child": 0x11E018,
        "spill.sentinel": 0x4758534F534E4154,
        "BSP.return": 0x11E000,
        "BSPSTORE.return": 0x11E000,
        "NaT.slot.address": 0x11E008,
        "NaT.collection.address": 0x11E1F8,
        "NaT.slot.index": 1,
        "RNAT.source": 1,
        "RNAT.observed": 1,
    }
    record_line = "IA64_STACKED_NAT_RSE_RECORD " + " ".join(
        f"{name}=0x{value:X}" for name, value in record.items())
    return "\n".join((
        record_line,
        "IA64_STACKED_NAT_RSE_RNAT valid_mask=0xF value=0x2 "
        "collection_memory=not-required",
        "IA64_STACKED_NAT_RSE_VALIDATION errors=0x0 negative_failures=0x0",
        "IA64_STACKED_NAT_RSE_PROBE_RESULT=PASS",
    )) + "\n"


class StackedNatRseContractTests(unittest.TestCase):
    def test_accepts_valid_partial_rnat_and_round_trip(self) -> None:
        runtime = validator.validate_output(valid_output())
        self.assertEqual(runtime["record"]["stacked.NAT.after"], 1)
        self.assertEqual(runtime["record"]["BSPSTORE.child"], 0x11E018)
        self.assertEqual(runtime["rnat"], {"valid_mask": 0xF, "value": 2})

    def test_rejects_missing_duplicate_or_failed_markers(self) -> None:
        output = valid_output()
        for candidate in (
                output.replace("IA64_STACKED_NAT_RSE_RNAT", "MISSING_RNAT", 1),
                output + output.splitlines()[0] + "\n",
                output.replace("PROBE_RESULT=PASS", "PROBE_RESULT=FAIL")):
            with self.subTest(candidate=candidate[-90:]):
                with self.assertRaises(validator.QualificationError):
                    validator.validate_output(candidate)

    def test_rejects_invalid_partial_rnat_mask_and_selected_bit(self) -> None:
        output = valid_output()
        for bad_rnat in (
                "valid_mask=0x7 value=0x2 collection_memory=not-required",
                "valid_mask=0xF value=0x0 collection_memory=not-required"):
            candidate = output.replace(
                "valid_mask=0xF value=0x2 collection_memory=not-required", bad_rnat)
            with self.subTest(bad_rnat=bad_rnat):
                with self.assertRaises(validator.QualificationError):
                    validator.validate_output(candidate)

    def test_boot_runs_stack_test_after_bss_and_before_cpp(self) -> None:
        boot = (ROOT / "kernel/arch/ia64/boot.s").read_text(encoding="utf-8")
        bss_clear = boot.index("(p7) br.cond.dptk clear_bss")
        stacked_call = boot.index("br.call.sptk.many b0 = ia64_stacked_nat_rse_boot_test")
        kernel_call = boot.index("br.call.sptk.many rp = kernel_main")
        self.assertLess(bss_clear, stacked_call)
        self.assertLess(stacked_call, kernel_call)
        guard = boot.rfind("#ifdef GXOS_IA64_EARLY_EXEC_PROFILE", bss_clear, stacked_call)
        end = boot.find("#endif", stacked_call, kernel_call)
        self.assertGreater(guard, bss_clear)
        self.assertGreater(end, stacked_call)

    def test_existing_nat_consumer_remains_standalone(self) -> None:
        source = (ROOT / "kernel/arch/ia64/nat_consumption_entry.s").read_text(
            encoding="utf-8")
        self.assertNotIn("ia64_stacked_nat_rse_test", source)
        build = (ROOT / "tests/ia64_nat_consumption/build.sh").read_text(encoding="utf-8")
        self.assertNotIn("stacked_nat_rse", build)


if __name__ == "__main__":
    unittest.main(verbosity=2)
