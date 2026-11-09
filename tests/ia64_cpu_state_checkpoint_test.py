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


def valid_post_rfi_records() -> dict[str, dict[str, int]]:
    return {
        "slot1": {"immediate": 0x6A17, "continuation": 0x109020,
                  "return_slot": 2, "order": 1, "valid": 0xBEEFC0DE4B505231,
                  "PSR": 0x2000},
        "slot2": {"immediate": 0x6A18, "continuation": 0x109030,
                  "return_slot": 0, "order": 2, "valid": 0xBEEFC0DE4B505232,
                  "PSR": 0x2000},
    }


def valid_post_rfi_output() -> str:
    return (
        "IA64_BREAK_POST_RFI_PSR slot=slot1 immediate=0x6a17 continuation=0x109020 "
        "return_slot=0x2 order=0x1 valid=0xbeefc0de4b505231 PSR=0x2000\n"
        "IA64_BREAK_POST_RFI_PSR slot=slot2 immediate=0x6a18 continuation=0x109030 "
        "return_slot=0x0 order=0x2 valid=0xbeefc0de4b505232 PSR=0x2000\n")


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

    def test_psr_architectural_bit_matrix_and_reversed_labels(self) -> None:
        expected = {
            0x0000: ("0", "0"),
            0x2000: ("0", "1"),
            0x4000: ("1", "0"),
            0x6000: ("1", "1"),
        }
        for psr, (interrupt_enable, collection_enable) in expected.items():
            with self.subTest(psr=f"0x{psr:04X}"):
                record = validator.parse_checkpoint_record(
                    line("C", stage=3, valid=0x1E7, psr=psr, iva=IVT))
                self.assertEqual(record["PSR.i"], interrupt_enable)
                self.assertEqual(record["PSR.ic"], collection_enable)

        reversed_labels = line("C", stage=3, valid=0x1E7, psr=0x2000, iva=IVT).replace(
            "PSR.i=0 PSR.ic=1", "PSR.i=1 PSR.ic=0")
        with self.assertRaisesRegex(ValueError, "PSR.i field disagrees"):
            validator.parse_checkpoint_record(reversed_labels)

    def test_lost_psr_ic_at_final_checkpoint_is_rejected(self) -> None:
        lost_ic = valid_guest_output().replace(
            "IA64_CHECKPOINT_D stage=0x4 valid=0x1E7 PSR=0x2000 PSR.i=0 PSR.ic=1",
            "IA64_CHECKPOINT_D stage=0x4 valid=0x1E7 PSR=0x0 PSR.i=0 PSR.ic=0")
        _, errors = validator.validate_checkpoint_output(
            lost_ic, ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("checkpoint D PSR.ic" in error for error in errors), errors)

    def test_contradictory_hex_incomplete_record_and_missing_checkpoint_are_rejected(self) -> None:
        contradictory = line("C", stage=3, valid=0x1E7, psr=0x2000, iva=IVT).replace(
            "PSR.i=0 PSR.ic=1", "PSR.i=0 PSR.ic=0")
        with self.assertRaisesRegex(ValueError, "PSR.ic field disagrees"):
            validator.parse_checkpoint_record(contradictory)

        incomplete = line("C", stage=3, valid=0x1E7, psr=0x2000, iva=IVT).replace(
            " IVA=0x100000", "")
        with self.assertRaisesRegex(ValueError, "missing or unexpected fields"):
            validator.parse_checkpoint_record(incomplete)

        _, contradictory_errors = validator.validate_checkpoint_output(
            valid_guest_output().replace(
                "PSR=0x2000 PSR.i=0 PSR.ic=1",
                "PSR=0x2000 PSR.i=0 PSR.ic=0", 1),
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("PSR.ic field disagrees" in error for error in contradictory_errors),
                        contradictory_errors)
        self.assertTrue(any("checkpoint C is missing" in error for error in contradictory_errors),
                        contradictory_errors)

        _, missing_errors = validator.validate_checkpoint_output(
            "\r\n".join(line for line in valid_guest_output().splitlines()
                        if not line.startswith("IA64_CHECKPOINT_C")),
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("checkpoint C is missing" in error for error in missing_errors),
                        missing_errors)

    def test_checkpoint_records_must_be_in_a_b_c_d_order(self) -> None:
        reversed_output = "\r\n".join(reversed(valid_guest_output().splitlines()))
        _, errors = validator.validate_checkpoint_output(
            reversed_output, ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("checkpoint order" in error for error in errors), errors)

    def test_runtime_evidence_binds_raw_records_exit_faults_and_candidate_hash(self) -> None:
        stdout = valid_guest_output() + "kernel exited with status 0\r\n"
        parsed, parse_errors = validator.validate_checkpoint_output(
            valid_guest_output(), ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertEqual(parse_errors, [])
        run_result = {
            "label": "guideXOS-kernel",
            "command": ["/qualified/bskinc", "-stats", "-icnt", "counts.txt",
                        "-forcesystem", "/tmp/selected.elf"],
            "artifact_sha256": "a" * 64,
            "classification": "GUEST_EXIT_ZERO",
            "exit_code": 0,
            "timed_out": False,
            "cleanup_complete": True,
            "cpu_state_checkpoints": parsed,
            "cpu_state_checkpoint_errors": [],
        }
        _, errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (0 faults), 0.01 sec",
            "per-instruction counts captured", run_result, "a" * 64,
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertEqual(errors, [])

        wrong_identity = dict(run_result, artifact_sha256="b" * 64)
        _, identity_errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (0 faults), 0.01 sec",
            "per-instruction counts captured", wrong_identity, "a" * 64,
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("artifact identity" in error for error in identity_errors),
                        identity_errors)

        _, fault_errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (1 faults), 0.01 sec",
            "per-instruction counts captured", run_result, "a" * 64,
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("unexpected fault" in error for error in fault_errors), fault_errors)

        mismatched_runner_records = dict(run_result, cpu_state_checkpoints={})
        _, synthesis_errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (0 faults), 0.01 sec",
            "per-instruction counts captured", mismatched_runner_records,
            "a" * 64, ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("exactly the four guest checkpoints" in error
                            for error in synthesis_errors), synthesis_errors)

    def test_runtime_evidence_accepts_only_two_attested_break_faults(self) -> None:
        stdout = valid_guest_output() + (
            "IA64_BREAK_RECORD slot1 valid=0xbeefc0de4b56414c\n"
            "IA64_BREAK_RECORD slot2 valid=0xbeefc0de4b56414c\n"
            + valid_post_rfi_output() +
            "IA64_BREAK_EXCEPTION_RESULT=PASS\n"
            "kernel exited with status 0\n")
        parsed, parse_errors = validator.validate_checkpoint_output(
            valid_guest_output(), ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertEqual(parse_errors, [])
        run_result = {
            "label": "guideXOS-kernel",
            "command": ["/qualified/bskinc", "-stats", "-icnt", "counts.txt",
                        "-forcesystem", "/tmp/selected.elf"],
            "artifact_sha256": "a" * 64,
            "classification": "BREAK_EXCEPTION_KERNEL_PASS",
            "exit_code": 0,
            "timed_out": False,
            "cleanup_complete": True,
            "ski_fault_count": 2,
            "break_exception_expected_faults": 2,
            "break_exception_evidence_errors": [],
            "break_exception_post_rfi_observations": valid_post_rfi_records(),
            "cpu_state_checkpoints": parsed,
            "cpu_state_checkpoint_errors": [],
        }
        _, errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (2 faults), 0.01 sec",
            "per-instruction counts captured", run_result, "a" * 64,
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertEqual(errors, [])

        wrong_post_rfi = dict(
            run_result,
            break_exception_post_rfi_observations={
                **valid_post_rfi_records(),
                "slot1": {**valid_post_rfi_records()["slot1"], "PSR": 0x6000},
            })
        _, post_rfi_errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (2 faults), 0.01 sec",
            "per-instruction counts captured", wrong_post_rfi, "a" * 64,
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("slot1 post-rfi" in error for error in post_rfi_errors), post_rfi_errors)

        wrong_expected_count = dict(run_result, break_exception_expected_faults=1)
        _, wrong_count_errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (2 faults), 0.01 sec",
            "per-instruction counts captured", wrong_expected_count, "a" * 64,
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("exactly two validated Break" in error
                            for error in wrong_count_errors), wrong_count_errors)

        _, unexpected_fault_errors = validator.validate_runtime_evidence(
            stdout, "kernel exited with status 0\n100 insts (3 faults), 0.01 sec",
            "per-instruction counts captured", run_result, "a" * 64,
            ivt=IVT, rse_base=RSE_BASE, rse_top=RSE_TOP)
        self.assertTrue(any("expected 2" in error for error in unexpected_fault_errors),
                        unexpected_fault_errors)


if __name__ == "__main__":
    unittest.main(verbosity=2)
