from __future__ import annotations

import sys
import unittest
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from dm34_qmp_gui_input import QmpGuiInput  # noqa: E402


class FakeQmp:
    def __init__(self) -> None:
        self.calls: list[list[dict[str, Any]]] = []

    def send_events(self, events: list[dict[str, Any]]) -> dict[str, bool]:
        self.calls.append(events)
        return {"accepted": True}


class QmpGuiInputTests(unittest.TestCase):
    def make_input(self) -> tuple[FakeQmp, QmpGuiInput, list[float]]:
        qmp = FakeQmp()
        sleeps: list[float] = []
        sender = QmpGuiInput(qmp, sleeper=sleeps.append)
        return qmp, sender, sleeps

    def test_relative_axes_are_separate_and_large_deltas_are_split(self) -> None:
        qmp, sender, sleeps = self.make_input()

        responses = sender.move_relative(-250, 200)

        self.assertEqual(len(responses), 5)
        self.assertEqual(
            qmp.calls,
            [
                [{"type": "rel", "data": {"axis": "x", "value": -100}}],
                [{"type": "rel", "data": {"axis": "x", "value": -100}}],
                [{"type": "rel", "data": {"axis": "x", "value": -50}}],
                [{"type": "rel", "data": {"axis": "y", "value": 100}}],
                [{"type": "rel", "data": {"axis": "y", "value": 100}}],
            ],
        )
        self.assertEqual(sleeps, [0.2] * 5)

    def test_key_and_modifier_transitions_are_separate_and_released(self) -> None:
        qmp, sender, sleeps = self.make_input()

        sender.press("w", modifiers=("shift",))

        self.assertEqual(
            qmp.calls,
            [
                [{"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": "shift"}}}],
                [{"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": "w"}}}],
                [{"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": "w"}}}],
                [{"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": "shift"}}}],
            ],
        )
        self.assertEqual(sleeps, [0.08] * 4)

    def test_confirmation_phrase_uses_individual_press_release_requests(self) -> None:
        qmp, sender, sleeps = self.make_input()

        sender.type_text("ERASE DM34SAFE01")

        self.assertEqual(len(qmp.calls), 54)
        self.assertTrue(all(len(call) == 1 for call in qmp.calls))
        self.assertEqual(len(sleeps), 54)
        self.assertEqual(sleeps, [0.08] * 54)

    def test_click_delivers_button_down_and_up_separately(self) -> None:
        qmp, sender, sleeps = self.make_input()

        sender.click()

        self.assertEqual(
            qmp.calls,
            [
                [{"type": "btn", "data": {"button": "left", "down": True}}],
                [{"type": "btn", "data": {"button": "left", "down": False}}],
            ],
        )
        self.assertEqual(sleeps, [0.15, 0.15])

    def test_unsupported_text_is_rejected_before_dispatch(self) -> None:
        qmp, sender, sleeps = self.make_input()

        with self.assertRaises(ValueError):
            sender.type_text("ERASE!")

        self.assertEqual(qmp.calls, [])
        self.assertEqual(sleeps, [])


if __name__ == "__main__":
    unittest.main()
