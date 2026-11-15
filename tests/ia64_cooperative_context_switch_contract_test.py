#!/usr/bin/env python3
"""Host-side failure-containment tests for the bounded IA-64 switch probe."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts" / "run-ia64-ski.py"
spec = importlib.util.spec_from_file_location("ia64_ski_runner", RUNNER_PATH)
assert spec is not None and spec.loader is not None
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

DIAGNOSTIC_PATH = ROOT / "scripts" / "diagnose-ia64-coop-switch.py"
diagnostic_spec = importlib.util.spec_from_file_location(
    "ia64_coop_diagnostic", DIAGNOSTIC_PATH)
assert diagnostic_spec is not None and diagnostic_spec.loader is not None
diagnostic = importlib.util.module_from_spec(diagnostic_spec)
diagnostic_spec.loader.exec_module(diagnostic)


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

    def test_partial_observations_are_retained_without_qualification(self) -> None:
        stdout = "\n".join((
            "IA64_COOP_CONTEXT_PROBE_START mode=forced-system cpu_count=1",
            "IA64_COOP_EVENT sequence=0x1 context=0xa event=A_TO_B",
            "IA64_COOP_CONTEXT_STATE id=A stack_base=0x100000 bsp=0x140000 "
            "bspstore=0x140000 observed_psr=0x0",
        ))
        observations = runner.capture_cooperative_context_probe_observations(
            stdout, "Ski fault @ 0x10dc86\n")
        self.assertEqual(observations["transition_records"], [
            {"sequence": 1, "context_id": 0xA, "event": "A_TO_B"}])
        self.assertEqual(len(observations["context_state_lines"]), 1)
        self.assertEqual(observations["last_observed_guest_address"], {
            "address": 0x10DC86, "source": "fault diagnostic"})


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


class CooperativeContextRunnerConfigurationTests(unittest.TestCase):
    def test_command_uses_headless_batch_console_and_explicit_system_mode(self) -> None:
        self.assertEqual(runner.cooperative_context_probe_command(
            Path("/opt/ski/bin/bskinc"), Path("/tmp/selected.elf")), [
                "/opt/ski/bin/bskinc", "-noconsole", "-forcesystem",
                "/tmp/selected.elf",
            ])

    def test_cooperative_batch_run_supplies_a_pty_stdin(self) -> None:
        source = (ROOT / "scripts" / "run-ia64-ski.py").read_text(encoding="utf-8")
        cooperative_runner = source.split("def run_cooperative_context_probe", 1)[1].split(
            "def verify_break_probe_evidence", 1)[0]
        self.assertIn("stdin_pty=True", cooperative_runner)


class CooperativeContextSwitchCallerTests(unittest.TestCase):
    def test_a_and_b_callers_pass_contexts_through_output_registers(self) -> None:
        source = (ROOT / "tests" / "ia64_cooperative_context_switch" /
                  "probe_sequence.s").read_text(encoding="utf-8")
        call_args = re.compile(
            r"movl r14 = ia64_coop_context_([ab]) ;;\s*"
            r"mov out0 = r14 ;;\s*"
            r"movl r14 = ia64_coop_context_([ab]) ;;\s*"
            r"mov out1 = r14 ;;\s*"
            r"(?:RECORD_EVENT [^\n]*\s*)?"
            r"br\.call\.sptk\.many b0 = ia64_coop_context_switch ;;"
        )
        callers = (
            ("ia64_coop_a_test:", "ia64_coop_b_test:", [("a", "b")] * 2),
            ("ia64_coop_b_test:", "ia64_coop_test_fail:", [("b", "a")] * 2),
        )
        for start, end, expected in callers:
            with self.subTest(caller=start):
                body = source.split(start, 1)[1].split(end, 1)[0]
                self.assertIn("alloc r16 = ar.pfs, 6, 4, 2, 0 ;;", body)
                self.assertEqual(call_args.findall(body), expected)


class CooperativeContextRseModeTests(unittest.TestCase):
    def test_context_b_uses_mode_independent_bsp_for_rbs_bounds(self) -> None:
        source = (ROOT / "tests" / "ia64_cooperative_context_switch" /
                  "probe_sequence.s").read_text(encoding="utf-8")
        body = source.split(".global ia64_coop_b_test", 1)[1].split(
            ".endp ia64_coop_b_test", 1)[0]
        self.assertIn("Context B's RSC.mode=3", body)
        self.assertIn("mov r14 = ar.bsp ;;", body)
        self.assertNotRegex(body, r"\bar\.bspstore\b")


class CooperativeSetupReturnLinkTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = (ROOT / "tests" / "ia64_cooperative_context_switch" /
                       "probe_sequence.s").read_text(encoding="utf-8")

    def test_setup_saves_and_restores_stacked_return_link_and_pfs(self) -> None:
        runner.validate_cooperative_probe_setup_source(self.source)

    def test_missing_return_link_save_is_rejected(self) -> None:
        damaged = self.source.replace("mov r33 = b0 ;;", "mov r33 = r16 ;;", 1)
        with self.assertRaises(runner.GateError):
            runner.validate_cooperative_probe_setup_source(damaged)

    def test_incorrect_restore_order_is_rejected(self) -> None:
        damaged = self.source.replace(
            "mov ar.pfs = r32 ;;\n    mov b0 = r33 ;;",
            "mov b0 = r33 ;;\n    mov ar.pfs = r32 ;;", 1)
        with self.assertRaises(runner.GateError):
            runner.validate_cooperative_probe_setup_source(damaged)

    def test_invalid_branch_continuation_is_rejected(self) -> None:
        damaged = self.source.replace(
            "br.ret.sptk.many b0 ;;\n.endp ia64_coop_probe_setup",
            "br.ret.sptk.many b1 ;;\n.endp ia64_coop_probe_setup", 1)
        with self.assertRaises(runner.GateError):
            runner.validate_cooperative_probe_setup_source(damaged)


class CooperativeDiagnosticFrontierTests(unittest.TestCase):
    def test_objdump_slots_map_to_ski_breakpoint_ips(self) -> None:
        self.assertEqual(diagnostic.ski_breakpoint_ip(0x10DC86), 0x10DC84)
        self.assertEqual(diagnostic.ski_breakpoint_ip(0x10E19C), 0x10E198)
        self.assertEqual(diagnostic.ski_breakpoint_ip(0x10AE30), 0x10AE30)
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.ski_breakpoint_ip(0x10DC84)

    def test_setup_return_requires_saved_target_and_reached_continuation(self) -> None:
        breakpoints = (
            (0x10A2D0, "ia64_coop_probe_setup entry"),
            (0x10A300, "return from ia64_coop_set_nat8"),
            (0x10AB48, "setup final br.ret"),
            (0x10AE30, "setup caller continuation"),
        )
        samples = [
            {"ip": 0x10A2D0, "iip": 0x10A2D0, "instructions": 8},
            {"ip": 0x10A300, "iip": 0x10A300, "instructions": 20},
            {"ip": 0x10AB48, "iip": 0x10AB40, "instructions": 30},
            {"ip": 0x10AE30, "iip": 0x10AE30, "instructions": 31},
        ]
        correct = [
            {"instructions": 8, "registers": {"b0": 0x10AE30, "pfs": 0x123456}},
            {"instructions": 20, "registers": {
                "b0": 0x10A300, "r32": 0x123456, "r33": 0x10AE30}},
            {"instructions": 30, "registers": {"b0": 0x10AE30, "pfs": 0x123456}},
        ]
        evidence = diagnostic.analyze_runtime_frontiers(
            samples, correct, breakpoints, 0x10AE30, 0x10A300)
        self.assertTrue(evidence["setup_return"]["verified"])
        wrong = list(correct)
        wrong[1] = {"instructions": 20, "registers": {
            "b0": 0x10A300, "r32": 0x123456, "r33": 0x10A300}}
        evidence = diagnostic.analyze_runtime_frontiers(
            samples, wrong, breakpoints, 0x10AE30, 0x10A300)
        self.assertFalse(evidence["setup_return"]["verified"])


if __name__ == "__main__":
    unittest.main()
