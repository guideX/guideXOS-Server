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


if __name__ == "__main__":
    unittest.main(verbosity=2)
