#!/usr/bin/env python3
"""Host-side failure-containment tests for the bounded IA-64 switch probe."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts" / "run-ia64-ski.py"
spec = importlib.util.spec_from_file_location("ia64_ski_runner", RUNNER_PATH)
assert spec is not None and spec.loader is not None
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


EVENTS = (
    (1, 0xA, "A_TO_B"),
    (2, 0xB, "B_FIRST_ENTRY"),
    (3, 0xB, "B_TO_A_FIRST"),
    (4, 0xA, "A_RESUMED_FIRST"),
    (5, 0xA, "A_TO_B_SECOND"),
    (6, 0xB, "B_RESUMED"),
    (7, 0xB, "B_TO_A_SECOND"),
    (8, 0xA, "A_RESUMED_FINAL"),
)


def valid_output() -> str:
    lines = [
        f"IA64_COOP_EVENT sequence=0x{seq:x} context=0x{context:x} event={event}"
        for seq, context, event in EVENTS
    ]
    lines.extend((
        "IA64_COOP_CONTEXT_STATE id=A stack_base=0x100000 stack_limit=0x110000 "
        "sp=0x10f000 rp=0x200100 code_base=0x200000 code_limit=0x210000 "
        "rbs_base=0x140000 rbs_limit=0x150000 switches=0x2 first_entries=0x0 "
        "resumes=0x2 bsp=0x140000 bspstore=0x140000 rnat_defined=0x0 "
        "rnat_valid_mask=0x1 observed_psr=0x0",
        "IA64_COOP_CONTEXT_STATE id=B stack_base=0x120000 stack_limit=0x130000 "
        "sp=0x12f000 rp=0x200200 code_base=0x200000 code_limit=0x210000 "
        "rbs_base=0x160000 rbs_limit=0x170000 switches=0x2 first_entries=0x1 "
        "resumes=0x1 bsp=0x160000 bspstore=0x160000 rnat_defined=0x0 "
        "rnat_valid_mask=0x1 observed_psr=0x0",
        "IA64_COOP_CONTEXT_VALIDATION errors=0x0 nat_a_before=0x1 nat_a_after=0x1 "
        "nat_b_before=0x0 nat_b_after=0x0 pfs_a_before=0x11 pfs_a_after=0x11 "
        "pfs_b_before=0x22 pfs_b_after=0x22 psr_a=0x0 psr_b=0x0 "
        "fp_a_low=0x415f46505f303031 fp_a_high=0x3fff800000000000 "
        "fp_b_first_low=0x425f46505f303032 fp_b_first_high=0x3fff800000000000 "
        "fp_b_resume_low=0x425f46505f303032 fp_b_resume_high=0x3fff800000000000",
        "IA64_COOP_CONTEXT_PROBE_RESULT=PASS",
    ))
    return "\n".join(lines)


def replace_once(output: str, old: str, new: str) -> str:
    if output.count(old) != 1:
        raise AssertionError(f"fixture token is not unique: {old}")
    return output.replace(old, new, 1)


class CooperativeContextOutputTests(unittest.TestCase):
    def assert_rejected(self, output: str) -> None:
        with self.assertRaises(runner.GateError):
            runner.validate_cooperative_context_probe_output(output)

    def test_valid_two_context_record(self) -> None:
        result = runner.validate_cooperative_context_probe_output(valid_output())
        self.assertEqual(len(result["events"]), 8)
        self.assertEqual({line.split(" id=")[1].split()[0] for line in
                          result["context_state_lines"]}, {"A", "B"})

    def test_wrong_identity_and_invalid_return_order_are_rejected(self) -> None:
        wrong_identity = replace_once(valid_output(), "sequence=0x1 context=0xa",
                                       "sequence=0x1 context=0xb")
        self.assert_rejected(wrong_identity)

        lines = valid_output().splitlines()
        lines[6], lines[7] = lines[7], lines[6]
        self.assert_rejected("\n".join(lines))

    def test_duplicate_or_restarted_transition_is_rejected(self) -> None:
        output = valid_output()
        duplicate = output.replace(
            "IA64_COOP_EVENT sequence=0x2 context=0xb event=B_FIRST_ENTRY\n",
            "IA64_COOP_EVENT sequence=0x2 context=0xb event=B_FIRST_ENTRY\n"
            "IA64_COOP_EVENT sequence=0x2 context=0xb event=B_FIRST_ENTRY\n",
            1,
        )
        self.assert_rejected(duplicate)
        restarted = replace_once(output, "first_entries=0x1 resumes=0x1",
                                 "first_entries=0x2 resumes=0x0")
        self.assert_rejected(restarted)

    def test_malformed_or_duplicate_record_fields_are_rejected(self) -> None:
        malformed = valid_output().replace(
            "IA64_COOP_EVENT sequence=0x1 context=0xa event=A_TO_B",
            "IA64_COOP_EVENT sequence=broken context=0xa event=A_TO_B",
            1,
        )
        self.assert_rejected(malformed)
        duplicate_field = replace_once(
            valid_output(), "switches=0x2 first_entries=0x0",
            "switches=0x2 switches=0x2 first_entries=0x0",
        )
        self.assert_rejected(duplicate_field)

    def test_overlapping_backing_stores_are_rejected(self) -> None:
        output = replace_once(valid_output(), "rbs_base=0x160000 rbs_limit=0x170000",
                              "rbs_base=0x140000 rbs_limit=0x150000")
        self.assert_rejected(output)

    def test_invalid_stack_alignment_or_bounds_are_rejected(self) -> None:
        misaligned = replace_once(valid_output(), "sp=0x12f000", "sp=0x12f008")
        self.assert_rejected(misaligned)
        out_of_bounds = replace_once(valid_output(), "sp=0x12f000", "sp=0x130010")
        self.assert_rejected(out_of_bounds)

    def test_corrupted_return_continuation_is_rejected(self) -> None:
        output = replace_once(valid_output(), "rp=0x200100", "rp=0x210000")
        self.assert_rejected(output)

    def test_missing_or_changed_pfs_is_rejected(self) -> None:
        missing = replace_once(valid_output(), "pfs_b_before=0x22 ", "")
        self.assert_rejected(missing)
        changed = replace_once(valid_output(), "pfs_a_after=0x11", "pfs_a_after=0x12")
        self.assert_rejected(changed)

    def test_lost_nat_or_incorrect_defined_rnat_bits_are_rejected(self) -> None:
        lost_nat = replace_once(valid_output(), "nat_a_after=0x1", "nat_a_after=0x0")
        self.assert_rejected(lost_nat)
        bad_rnat = replace_once(
            valid_output(),
            "resumes=0x2 bsp=0x140000 bspstore=0x140000 rnat_defined=0x0",
            "resumes=0x2 bsp=0x140000 bspstore=0x140000 rnat_defined=0x2",
        )
        self.assert_rejected(bad_rnat)

    def test_psr_interrupt_enable_is_rejected(self) -> None:
        context_psr = replace_once(
            valid_output(),
            "resumes=0x2 bsp=0x140000 bspstore=0x140000 rnat_defined=0x0 "
            "rnat_valid_mask=0x1 observed_psr=0x0",
            "resumes=0x2 bsp=0x140000 bspstore=0x140000 rnat_defined=0x0 "
            "rnat_valid_mask=0x1 observed_psr=0x4000",
        )
        self.assert_rejected(context_psr)
        resumed_psr = replace_once(valid_output(), "psr_b=0x0", "psr_b=0x4000")
        self.assert_rejected(resumed_psr)

    def test_unsupported_switch_profile_is_rejected(self) -> None:
        manifest = {
            "guest_execution_authorized": "probe-only",
            "execution_count_authorized": 1,
            "system_mode": "-forcesystem",
            "cpu_count": 1,
            "ivt_bytes": 0x8000,
            "expected_faults": 0,
            "expected_events": list(runner.COOP_CONTEXT_EVENT_NAMES),
            "intentional_exit_ssc": 66,
            "scheduler_enabled": False,
            "external_interrupts_enabled": False,
        }
        runner.validate_cooperative_context_probe_manifest(manifest)
        for key, value in (("guest_execution_authorized", "kernel"),
                           ("execution_count_authorized", 2),
                           ("scheduler_enabled", True),
                           ("external_interrupts_enabled", True)):
            altered = copy.deepcopy(manifest)
            altered[key] = value
            with self.subTest(key=key):
                with self.assertRaises(runner.GateError):
                    runner.validate_cooperative_context_probe_manifest(altered)

        source = (ROOT / "tests" / "ia64_cooperative_context_switch" /
                  "context_switch.s").read_text(encoding="utf-8")
        runner.validate_cooperative_context_switch_source(source)
        for required in ("CTX_PROFILE_MAGIC", "CTX_A_ID", "CTX_B_ID", "C_RSC",
                         "C_PFS", "C_FIRST_ENTRY", "ia64_coop_switch_fail"):
            with self.subTest(required=required):
                self.assertIn(required, source)

        bad_first_entry = source.replace(
            "cmp.eq p6, p7 = r20, r0 ;;\n"
            "(p7) br.cond.sptk ia64_coop_switch_fail ;; /* bootstrap PFS must be zero */",
            "cmp.eq p6, p7 = r20, r0 ;;\n"
            "(p6) br.cond.sptk ia64_coop_switch_fail ;; /* bootstrap PFS must be zero */",
            1,
        )
        with self.assertRaises(runner.GateError):
            runner.validate_cooperative_context_switch_source(bad_first_entry)

        bad_rsc = source.replace("mov r21 = 3 ;;", "mov r21 = 1 ;;", 1)
        with self.assertRaises(runner.GateError):
            runner.validate_cooperative_context_switch_source(bad_rsc)


if __name__ == "__main__":
    unittest.main()
