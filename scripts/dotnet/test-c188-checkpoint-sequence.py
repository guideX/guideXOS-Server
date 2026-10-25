#!/usr/bin/env python3
"""Focused cases for the C188 checkpoint sequence validator."""

from __future__ import annotations

import importlib.util
import re
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("c188-checkpoint-sequence.py")
SPEC = importlib.util.spec_from_file_location("c188_checkpoint_sequence", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECKPOINTS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECKPOINTS
SPEC.loader.exec_module(CHECKPOINTS)


class CheckpointSequenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.steps = [
            CHECKPOINTS.literal("start", "C188R3-100 NOTES-LAUNCH-ENTRY"),
            CHECKPOINTS.literal("optional-context", "C147-NOTES-CONTEXT startup=loaded list=enabled result=PASS", optional=True),
            CHECKPOINTS.literal("reset", "C188R3-103 NOTES-STARTUP-STATE-RESET"),
            CHECKPOINTS.literal("controls", "C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN"),
        ]

    def test_correctly_ordered_and_optional_branch_omitted(self) -> None:
        serial = "\n".join(
            [
                "C188R3-100 NOTES-LAUNCH-ENTRY",
                "C188R3-103 NOTES-STARTUP-STATE-RESET",
                "C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN",
            ]
        ) + "\n"
        result = CHECKPOINTS.validate_text(serial, self.steps, capture_state="complete")
        self.assertEqual("PASS", result["result"])
        self.assertEqual("C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN", result["lastObservedCheckpoint"])
        self.assertEqual("complete-capture", result["serialTruncationStatus"])
        self.assertEqual(["optional-context"], result["missingOptionalCheckpoints"])

    def test_missing_checkpoint_is_reported(self) -> None:
        serial = "C188R3-100 NOTES-LAUNCH-ENTRY\nC188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN\n"
        result = CHECKPOINTS.validate_text(serial, self.steps, capture_state="complete")
        self.assertEqual("FAIL", result["result"])
        self.assertEqual("reset", result["firstExpectedMissingCheckpoint"])
        self.assertIn("reset", result["missingRequiredCheckpoints"])
        self.assertEqual("unobserved-in-complete-capture", result["checkpointAbsenceInterpretation"])

    def test_duplicate_checkpoint_is_reported(self) -> None:
        serial = "\n".join(
            [
                "C188R3-100 NOTES-LAUNCH-ENTRY",
                "C188R3-100 NOTES-LAUNCH-ENTRY",
                "C188R3-103 NOTES-STARTUP-STATE-RESET",
                "C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN",
            ]
        ) + "\n"
        result = CHECKPOINTS.validate_text(serial, self.steps, capture_state="complete")
        self.assertEqual("FAIL", result["result"])
        self.assertTrue(any(item.get("marker") == "C188R3-100 NOTES-LAUNCH-ENTRY" for item in result["duplicateCheckpoints"]))

    def test_out_of_order_checkpoint_is_reported(self) -> None:
        serial = "\n".join(
            [
                "C188R3-100 NOTES-LAUNCH-ENTRY",
                "C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN",
                "C188R3-103 NOTES-STARTUP-STATE-RESET",
            ]
        ) + "\n"
        result = CHECKPOINTS.validate_text(serial, self.steps, capture_state="complete")
        self.assertEqual("FAIL", result["result"])
        self.assertTrue(any(item["checkpoint"] == "controls" for item in result["outOfOrderCheckpoints"]))

    def test_incomplete_capture_does_not_claim_checkpoint_absence(self) -> None:
        result = CHECKPOINTS.validate_text("C188R3-100 NOTES-LAUNCH-ENTRY\n", self.steps, capture_state="incomplete")
        self.assertEqual("not-distinguishable-from-capture-truncation", result["checkpointAbsenceInterpretation"])
        self.assertEqual("capture-incomplete", result["serialTruncationStatus"])

    def test_skipped_branch_does_not_require_its_operation_pair(self) -> None:
        steps = [
            CHECKPOINTS.branch_step("feature branch", 201, "FEATURE"),
            *CHECKPOINTS.branch_pair(202, "FEATURE-OPERATION", "FEATURE"),
            CHECKPOINTS.literal("next", "C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN"),
        ]
        result = CHECKPOINTS.validate_text(
            "C188R4-201 BRANCH FEATURE=SKIPPED\n"
            "C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN\n",
            steps,
            capture_state="complete",
        )
        self.assertEqual("PASS", result["result"])
        self.assertEqual([], result["missingRequiredCheckpoints"])
        self.assertEqual([], result["missingOptionalCheckpoints"])

    def test_taken_branch_requires_both_operation_markers(self) -> None:
        steps = [
            CHECKPOINTS.branch_step("feature branch", 201, "FEATURE"),
            *CHECKPOINTS.branch_pair(202, "FEATURE-OPERATION", "FEATURE"),
        ]
        result = CHECKPOINTS.validate_text(
            "C188R4-201 BRANCH FEATURE=TAKEN\nC188R4-202 BEFORE FEATURE-OPERATION\n",
            steps,
            capture_state="complete",
        )
        self.assertEqual("FAIL", result["result"])
        self.assertEqual("after FEATURE-OPERATION", result["firstExpectedMissingCheckpoint"])

    def test_historical_r3_serial_boundary(self) -> None:
        serial_path = MODULE_PATH.parents[2] / "out/dotnet/c188r3-notes-startup-diagnostics/runs/diag-001/serial.log"
        if not serial_path.is_file():
            self.skipTest("historical C188R3 serial evidence is not present")
        result = CHECKPOINTS.validate_text(serial_path.read_text(encoding="utf-8", errors="replace"), CHECKPOINTS.r3_steps(False), capture_state="complete")
        self.assertEqual("C188R3-103 NOTES-STARTUP-STATE-RESET", result["lastObservedCheckpoint"])
        self.assertEqual("control initialization", result["firstExpectedMissingCheckpoint"])

    def test_complete_r4_skip_path_matches_authoritative_order(self) -> None:
        steps = CHECKPOINTS.r3_steps(True)
        lines: list[str] = []
        for step in steps:
            if step.branch_key is not None:
                continue
            if "BRANCH" in step.patterns[0]:
                lines.append(re.sub(r"\\(.)", r"\1", step.patterns[-1]))
                continue
            pattern = step.patterns[0]
            if step.name == "C150 application instance":
                marker = "C150-APP-INSTANCE id=4 generation=1"
            elif step.name == "native launch return":
                marker = "[C102-LAUNCH-RETURN] status=0x00000000"
            else:
                marker = re.sub(r"\\(.)", r"\1", pattern)
            lines.append(marker)

        result = CHECKPOINTS.validate_text("\n".join(lines) + "\n", steps, capture_state="complete")
        self.assertEqual("PASS", result["result"])
        self.assertEqual([], result["missingRequiredCheckpoints"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C127-LAYOUT"])

    def test_c188r5_uses_the_r4_instrumented_skip_path(self) -> None:
        self.assertEqual(
            CHECKPOINTS.describe("C188R4")["expectedOrderedCheckpoints"],
            CHECKPOINTS.describe("C188R5")["expectedOrderedCheckpoints"],
        )
        steps = CHECKPOINTS.r3_steps(True)
        lines: list[str] = []
        for step in steps:
            if step.branch_key is not None:
                continue
            if "BRANCH" in step.patterns[0]:
                lines.append(re.sub(r"\\(.)", r"\1", step.patterns[-1]))
                continue
            pattern = step.patterns[0]
            if step.name == "C150 application instance":
                marker = "C150-APP-INSTANCE id=4 generation=1"
            elif step.name == "native launch return":
                marker = "[C102-LAUNCH-RETURN] status=0x00000000"
            else:
                marker = re.sub(r"\\(.)", r"\1", pattern)
            lines.append(marker)

        result = CHECKPOINTS.validate_text(
            "\n".join(lines) + "\n",
            CHECKPOINTS.r3_steps(True),
            capture_state="complete",
        )
        self.assertEqual("PASS", result["result"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C133-PROOF-TEST"])
        self.assertEqual([], result["missingRequiredCheckpoints"])

    def test_c188r6_adds_ordered_postrender_diagnostic_branches(self) -> None:
        names = [step["name"] for step in CHECKPOINTS.describe("C188R6")["expectedOrderedCheckpoints"]]
        self.assertLess(names.index("post-render settings return"), names.index("after checkpoint 163"))
        self.assertLess(names.index("C133 initial summary branch"), names.index("post-render context summaries return"))
        self.assertLess(names.index("post-render context summaries return"), names.index("before C117 post-render status logs"))
        self.assertLess(names.index("before ManagedNotes.Launch return"), names.index("after application launch return"))
        self.assertLess(names.index("after application launch return"), names.index("before application result log"))
        self.assertLess(names.index("before application result log"), names.index("application result"))
        self.assertLess(names.index("application result"), names.index("after application result log"))

    def test_c188r6_taken_c137_branch_requires_bounded_operations(self) -> None:
        steps = [
            CHECKPOINTS.r6_branch_step("C137 test branch", 206, "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(207, "C137-RUNTIME-CAPTURE", "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(209, "C137-DEFAULTS-COMMIT", "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(211, "C137-SUITE", "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(213, "C137-RUNTIME-RESTORE", "C137-TEST"),
        ]
        serial = "\n".join([
            "C188R6-206 BRANCH C137-TEST=TAKEN",
            "C188R6-207 BEFORE C137-RUNTIME-CAPTURE",
            "C188R6-208 AFTER C137-RUNTIME-CAPTURE",
            "C188R6-209 BEFORE C137-DEFAULTS-COMMIT",
            "C188R6-210 AFTER C137-DEFAULTS-COMMIT",
            "C188R6-211 BEFORE C137-SUITE",
        ]) + "\n"
        result = CHECKPOINTS.validate_text(serial, steps, capture_state="complete")
        self.assertEqual("FAIL", result["result"])
        self.assertEqual("after C137-SUITE", result["firstExpectedMissingCheckpoint"])

    def test_c188r6_skipped_c137_branch_does_not_require_suite_markers(self) -> None:
        steps = [
            CHECKPOINTS.r6_branch_step("C137 test branch", 206, "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(207, "C137-RUNTIME-CAPTURE", "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(209, "C137-DEFAULTS-COMMIT", "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(211, "C137-SUITE", "C137-TEST"),
            *CHECKPOINTS.r6_branch_pair(213, "C137-RUNTIME-RESTORE", "C137-TEST"),
        ]
        result = CHECKPOINTS.validate_text("C188R6-206 BRANCH C137-TEST=SKIPPED\n", steps, capture_state="complete")
        self.assertEqual("PASS", result["result"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C137-TEST"])
        self.assertEqual([], result["missingRequiredCheckpoints"])

    def test_c188r7_ordinary_notes_skip_path_is_branch_aware(self) -> None:
        steps = CHECKPOINTS.r7_steps()
        serial = "\n".join([
            "C150-APP-INSTANCE id=4 generation=1",
            "C188R7-100 NOTES-LAUNCH-ENTRY",
            "C147-NOTES-CONTEXT startup=loaded list=enabled result=PASS",
            "C148-NOTES-CONTEXT settings=v2-migration-proof source=shared-runtime result=PASS",
            "C188R7-180 BRANCH C133-HOST-TEST=SKIPPED",
            "C188R7-181 BRANCH C134-HOST-TEST=SKIPPED",
            "C188R7-182 BRANCH C133-C134-COMPANION-TEST=SKIPPED",
            "C188R7-110 NOTES-CONTROL-HOST-RESET",
            "C188R7-101 INITIAL-RENDER-RETURN",
            "C188R7-102 POST-RENDER-SETTINGS-RETURN",
            "C188R7-183 BRANCH C133-COMBO-SUITE=SKIPPED",
            "C188R7-200 BRANCH C137-TEST=SKIPPED",
            "C188R7-203 BRANCH C137-TEST-SUMMARY=SKIPPED",
            "C188R7-204 AFTER C137-INVOCATION-DECISION",
            "C188R7-205 AFTER POSTRENDER-PROOF-CHECKS",
            "C188R7-206 NOTES-LAUNCH-RESULT=SUCCESS",
            "C188R7-207 BEFORE MANAGEDNOTES-LAUNCH-RETURN",
            "C188R7-208 AFTER APPLICATION-LAUNCH-RETURN",
            "[C102-LAUNCH-RETURN] status=0x00000000",
            "[KERNEL] Input manager initialized",
            "[KERNEL] Entering main loop (waiting for input)...",
        ]) + "\n"
        result = CHECKPOINTS.validate_text(serial, steps, capture_state="complete")
        self.assertEqual("PASS", result["result"])
        self.assertEqual("[KERNEL] Entering main loop (waiting for input)...", result["lastExpectedCheckpointCompleted"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C133-HOST-TEST"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C134-HOST-TEST"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C133-C134-COMPANION-TEST"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C133-COMBO-SUITE"])
        self.assertEqual("SKIPPED", result["branchOutcomes"]["C137-TEST"])
        self.assertEqual([], result["missingRequiredCheckpoints"])

    def test_c188r7_skipped_c137_rejects_suite_operation_markers(self) -> None:
        result = CHECKPOINTS.validate_text(
            "C188R7-200 BRANCH C137-TEST=SKIPPED\n"
            "C188R7-201 BEFORE C137-SUITE\n"
            "C188R7-202 AFTER C137-SUITE\n",
            [
                CHECKPOINTS.r7_branch_step("C137 test invocation", 200, "C137-TEST"),
                *CHECKPOINTS.r7_branch_pair(201, "C137-SUITE", "C137-TEST"),
            ],
            capture_state="complete",
        )
        self.assertEqual("FAIL", result["result"])
        self.assertEqual(
            ["C188R7-201 BEFORE C137-SUITE", "C188R7-202 AFTER C137-SUITE"],
            result["unexpectedBranchCheckpoints"],
        )

    def test_c188r7_checkpoint_order_keeps_launch_and_readiness_after_c137_decision(self) -> None:
        names = [step["name"] for step in CHECKPOINTS.describe("C188R7")["expectedOrderedCheckpoints"]]
        self.assertLess(names.index("C137 test invocation"), names.index("after C137 invocation decision"))
        self.assertLess(names.index("after C137 invocation decision"), names.index("after post-render proof checks"))
        self.assertLess(names.index("after post-render proof checks"), names.index("ManagedNotes launch return"))
        self.assertLess(names.index("ManagedNotes launch return"), names.index("application launch return"))
        self.assertLess(names.index("application launch return"), names.index("input manager initialized"))
        self.assertLess(names.index("input manager initialized"), names.index("main input loop"))

    def test_native_return_accepts_prefixed_and_unprefixed_zero_status(self) -> None:
        step = next(step for step in CHECKPOINTS.r7_steps() if step.name == "native launch return")
        for marker in (
            "[C102-LAUNCH-RETURN] status=0x00000000",
            "[C102-LAUNCH-RETURN] status=00000000",
            "[C102-LAUNCH-RETURN] status=0X00000000",
        ):
            with self.subTest(marker=marker):
                result = CHECKPOINTS.validate_text(marker + "\n", [step], capture_state="complete")
                self.assertEqual("PASS", result["result"])
                self.assertEqual([], result["missingRequiredCheckpoints"])

    def test_native_return_rejects_nonzero_missing_and_malformed_status(self) -> None:
        step = next(step for step in CHECKPOINTS.r7_steps() if step.name == "native launch return")
        cases = (
            "[C102-LAUNCH-RETURN] status=00000001\n",
            "",
            "[C102-LAUNCH-RETURN] status=0x0000000G\n",
            "[C102-LAUNCH-RETURN] status=0000000\n",
            "[C102-LAUNCH-RETURN] status=000000000\n",
        )
        for serial in cases:
            with self.subTest(serial=serial):
                result = CHECKPOINTS.validate_text(serial, [step], capture_state="complete")
                self.assertEqual("FAIL", result["result"])
                self.assertEqual(["native launch return"], result["missingRequiredCheckpoints"])

    def test_native_return_duplicate_and_out_of_order_integrity_remain_enforced(self) -> None:
        step = next(step for step in CHECKPOINTS.r7_steps() if step.name == "native launch return")
        marker = "[C102-LAUNCH-RETURN] status=00000000"
        duplicate = CHECKPOINTS.validate_text(marker + "\n" + marker + "\n", [step], capture_state="complete")
        self.assertEqual("FAIL", duplicate["result"])
        self.assertTrue(any(item.get("marker") == marker for item in duplicate["duplicateCheckpoints"]))

        ordered = [CHECKPOINTS.literal("before", "C188R8-001 BEFORE"), step]
        out_of_order = CHECKPOINTS.validate_text(
            marker + "\nC188R8-001 BEFORE\n", ordered, capture_state="complete"
        )
        self.assertEqual("FAIL", out_of_order["result"])
        self.assertEqual("native launch return", out_of_order["firstExpectedMissingCheckpoint"])
        self.assertTrue(any(item["checkpoint"] == "native launch return" for item in out_of_order["outOfOrderCheckpoints"]))

    def test_c188r8_uses_r7_startup_checkpoints_with_status_format_fix(self) -> None:
        self.assertEqual(
            CHECKPOINTS.describe("C188R7")["expectedOrderedCheckpoints"],
            CHECKPOINTS.describe("C188R8")["expectedOrderedCheckpoints"],
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
