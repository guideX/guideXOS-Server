#!/usr/bin/env python3
"""Negative fixtures for independent cooperative stack-marker evidence."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
DIAGNOSTIC_PATH = ROOT / "scripts" / "diagnose-ia64-coop-switch.py"
spec = importlib.util.spec_from_file_location("ia64_coop_diagnostic", DIAGNOSTIC_PATH)
assert spec is not None and spec.loader is not None
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)

ARTIFACT_SHA256 = "8209d2f779363fdde9cdb23f5ec68ae88884a87d01d75aeb5955128eb1a1c4c1"
MARKER_ADDRESS = 0x124010
EXPECTED_WORDS = (0x415F535441434B30, 0x415F535441434B31)
CHECKPOINT_IPS = {
    "initial_before_first_suspension": 0x10C7F0,
    "after_first_resumption": 0x10CC10,
    "after_second_resumption": 0x10D520,
}
STACK_RANGE = (0x115010, 0x125010)


def valid_observations() -> list[dict[str, object]]:
    return [
        {
            "checkpoint": name,
            "artifact_sha256": ARTIFACT_SHA256,
            "task_id": "A",
            "ip": CHECKPOINT_IPS[name],
            "instructions": instructions,
            "event_count": event_count,
            "sp": 0x124010,
            "address": MARKER_ADDRESS,
            "words": list(EXPECTED_WORDS),
        }
        for (name, _, event_count), instructions in zip(
            diagnostic.STACK_MARKER_CHECKPOINTS, (100, 200, 300), strict=True)
    ]


class StackMarkerEvidenceTests(unittest.TestCase):
    def validate(self, observations: list[dict[str, object]]) -> list[dict[str, object]]:
        return diagnostic.validate_stack_marker_observations(
            observations,
            expected_artifact_sha256=ARTIFACT_SHA256,
            actual_artifact_sha256=ARTIFACT_SHA256,
            marker_address=MARKER_ADDRESS,
            expected_words=EXPECTED_WORDS,
            checkpoint_ips=CHECKPOINT_IPS,
            stack_range=STACK_RANGE,
        )

    def test_all_three_independent_observations_pass(self) -> None:
        result = self.validate(valid_observations())
        self.assertEqual([item["checkpoint"] for item in result], list(CHECKPOINT_IPS))

    def test_changed_marker_value_is_rejected(self) -> None:
        observations = valid_observations()
        observations[0]["words"] = [0, EXPECTED_WORDS[1]]
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "actual marker words changed"):
            self.validate(observations)

    def test_marker_at_incorrect_stack_position_is_rejected(self) -> None:
        observations = valid_observations()
        observations[0]["address"] = MARKER_ADDRESS + 8
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "wrong stack address"):
            self.validate(observations)

    def test_missing_marker_observation_is_rejected(self) -> None:
        observations = valid_observations()
        del observations[1]
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "missing actual marker observation"):
            self.validate(observations)

    def test_marker_corrupted_after_suspension_is_rejected(self) -> None:
        observations = valid_observations()
        observations[1]["words"] = [EXPECTED_WORDS[0], 0]
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "first resumption"):
            self.validate(observations)

    def test_second_resumption_corruption_is_rejected_after_first_passes(self) -> None:
        observations = valid_observations()
        observations[2]["words"] = [0, EXPECTED_WORDS[1]]
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "final resumption"):
            self.validate(observations)

    def test_observation_at_incorrect_execution_position_is_rejected(self) -> None:
        observations = valid_observations()
        observations[1]["ip"] = CHECKPOINT_IPS["after_first_resumption"] + 16
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "wrong execution position"):
            self.validate(observations)

    def test_raw_memory_dump_words_are_used_and_corruption_is_rejected(self) -> None:
        damaged_words = (EXPECTED_WORDS[0], 0)
        dump = f"{MARKER_ADDRESS:016x}: " + " ".join(
            f"{word:016x}" for word in damaged_words) + "\n"
        observations = valid_observations()
        observations[1]["words"] = diagnostic._parse_addressed_marker_dump(
            dump, MARKER_ADDRESS)
        self.assertEqual(observations[1]["words"], list(damaged_words))
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "actual marker words changed"):
            self.validate(observations)

    def test_raw_memory_dump_at_another_address_is_rejected(self) -> None:
        dump = f"{MARKER_ADDRESS + 8:016x}: " + " ".join(
            f"{word:016x}" for word in EXPECTED_WORDS) + "\n"
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "expected 0x124010"):
            diagnostic._parse_addressed_marker_dump(dump, MARKER_ADDRESS)

    def test_incomplete_raw_memory_dump_is_rejected(self) -> None:
        dump = f"{MARKER_ADDRESS:016x}: {EXPECTED_WORDS[0]:016x}\n"
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "has 1 qwords"):
            diagnostic._parse_addressed_marker_dump(dump, MARKER_ADDRESS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
