"""Deterministic host-only pointer-settling and screenshot tests."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
HELPER_DIR = ROOT / "out" / "font15r1-validation" / "guest"
sys.path.insert(0, str(HELPER_DIR))

import font15r1_pointer_settling as settling  # noqa: E402
from font15r1_qmp import EndpointIdentityError, QMPTimeout  # noqa: E402


class FakeClock:
    def __init__(self) -> None:
        self.now = 100.0

    def monotonic(self) -> float:
        return self.now

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def ppm_with_cursors(
    positions: list[tuple[int, int]],
    template: settling.CursorTemplate,
    *,
    width: int = 640,
    height: int = 400,
) -> bytes:
    background = (31, 53, 79)
    image = bytearray(background * (width * height))
    for origin_x, origin_y in positions:
        for x, y, color in template.pixels:
            px, py = origin_x + x, origin_y + y
            if 0 <= px < width and 0 <= py < height:
                offset = (py * width + px) * 3
                image[offset : offset + 3] = bytes(color)
    return f"P6\n{width} {height}\n255\n".encode("ascii") + bytes(image)


class FakeQmp:
    def __init__(
        self,
        clock: FakeClock,
        frames: list[list[tuple[int, int]]],
        template: settling.CursorTemplate,
        *,
        frame_delay: float = 0.05,
        screenshot_error: Exception | None = None,
        stale_capture: bool = False,
        invalid_frame: bool = False,
        fail_verify_call: int | None = None,
    ) -> None:
        self.clock = clock
        self.sleeper = clock.sleep
        self.frames = frames
        self.template = template
        self.frame_delay = frame_delay
        self.screenshot_error = screenshot_error
        self.stale_capture = stale_capture
        self.invalid_frame = invalid_frame
        self.fail_verify_call = fail_verify_call
        self.verify_calls = 0
        self.relative_calls: list[tuple[int, int, float, float]] = []
        self.screenshot_calls: list[Path] = []
        self.records: list[dict[str, Any]] = []

    def _record(self, record: dict[str, Any]) -> None:
        self.records.append(record)

    def verify_ownership(self, *, deadline: float | None = None) -> None:
        self.verify_calls += 1
        if self.fail_verify_call == self.verify_calls:
            raise EndpointIdentityError("QMP endpoint ownership changed")
        if deadline is not None and self.clock.now >= deadline:
            raise QMPTimeout("ownership verification exceeded the observation deadline")

    def relative(
        self,
        dx: int,
        dy: int,
        delay: float,
        combined: bool = False,
        *,
        deadline: float | None = None,
    ) -> None:
        self.relative_calls.append((dx, dy, delay, self.clock.now))
        self.clock.now += 0.01
        if deadline is not None and self.clock.now > deadline:
            raise QMPTimeout("movement exceeded the observation deadline")

    def screenshot(self, path: Path, *, deadline: float | None = None) -> dict[str, Any]:
        self.screenshot_calls.append(path)
        request_started = self.clock.now
        if self.screenshot_error is not None:
            self.clock.now += max(0.0, (deadline or self.clock.now) - self.clock.now)
            raise self.screenshot_error
        frame_index = min(len(self.screenshot_calls) - 1, len(self.frames) - 1)
        if self.invalid_frame:
            payload = b"not a p6 frame"
        else:
            payload = ppm_with_cursors(self.frames[frame_index], self.template)
        path.write_bytes(payload)
        self.clock.now += self.frame_delay
        metadata_started = request_started - 1.0 if self.stale_capture else request_started
        return {
            "fresh_capture": True,
            "path": str(path.resolve()),
            "request_monotonic": metadata_started,
            "artifact_verified_monotonic": self.clock.now,
            "size_bytes": len(payload),
            "width": 640,
            "height": 400,
        }


class PointerSettlingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.template = settling.CursorTemplate.from_cpp_source(
            ROOT / "kernel" / "core" / "desktop.cpp"
        )

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="font15r1-pointer-settle-")
        self.directory = Path(self.temp.name)
        self.clock = FakeClock()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def observe(
        self,
        frames: list[list[tuple[int, int]]],
        *,
        dx: int = -137,
        dy: int = 0,
        initial: tuple[int, int] = (500, 180),
        deadline_after: float = 2.0,
        max_screenshots: int = 8,
        min_interval: float = 0.2,
        qmp_options: dict[str, Any] | None = None,
        target_region: tuple[int, int, int, int] | None = None,
    ) -> tuple[settling.PointerSettleResult, FakeQmp]:
        qmp = FakeQmp(self.clock, frames, self.template, **(qmp_options or {}))
        result = settling.settle_pointer(
            qmp,
            initial_position=initial,
            dx=dx,
            dy=dy,
            cursor_template=self.template,
            screenshot_directory=self.directory,
            deadline=self.clock.now + deadline_after,
            max_screenshots=max_screenshots,
            min_interval=min_interval,
            tolerance=1,
            delay=0.0,
            target_region=target_region,
        )
        return result, qmp

    def assert_one_movement_only(self, qmp: FakeQmp) -> None:
        self.assertEqual(len(qmp.relative_calls), 1)
        self.assertTrue(all(path.suffix == ".ppm" for path in qmp.screenshot_calls))

    def test_immediate_completion_needs_one_fresh_observation(self) -> None:
        result, qmp = self.observe(
            [[(363, 180)]], target_region=(360, 175, 372, 192)
        )
        self.assertEqual(result.status, settling.SettleStatus.EXPECTED_POSITION_OBSERVED)
        self.assertEqual(result.observed_position, (363, 180))
        self.assertTrue(result.click_authorized)
        self.assertEqual(len(result.observations), 1)
        self.assertTrue(result.observations[0].screenshot_fresh)
        self.assert_one_movement_only(qmp)

    def test_delayed_completion_uses_only_read_only_followup_captures(self) -> None:
        result, qmp = self.observe(
            [[(465, 180)], [(465, 180)], [(363, 180)]],
            target_region=(360, 175, 372, 192),
        )
        self.assertEqual(result.status, settling.SettleStatus.EXPECTED_POSITION_OBSERVED)
        self.assertEqual([item.cursor["x"] for item in result.observations], [465, 465, 363])
        self.assertEqual(len(qmp.screenshot_calls), 3)
        starts = [item.started_monotonic for item in result.observations]
        self.assertTrue(all(second - first >= 0.2 - 1e-9 for first, second in zip(starts, starts[1:])))
        self.assert_one_movement_only(qmp)

    def test_incomplete_movement_at_deadline_is_partial_and_not_retried(self) -> None:
        result, qmp = self.observe(
            [[(465, 180)]] * 8,
            deadline_after=1.0,
            min_interval=0.35,
        )
        self.assertEqual(result.status, settling.SettleStatus.PARTIAL_DISPLACEMENT_AT_DEADLINE)
        self.assertEqual(result.observed_position, (465, 180))
        self.assertLessEqual(self.clock.now, result.deadline_monotonic)
        self.assert_one_movement_only(qmp)

    def test_no_displacement_at_deadline_is_distinct(self) -> None:
        result, qmp = self.observe(
            [[(500, 180)]] * 8,
            deadline_after=0.65,
            min_interval=0.25,
        )
        self.assertEqual(result.status, settling.SettleStatus.NO_DISPLACEMENT_AT_DEADLINE)
        self.assertEqual(result.observed_position, (500, 180))
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_screenshot_count_limit_terminates_before_the_deadline(self) -> None:
        result, qmp = self.observe(
            [[(465, 180)]] * 8,
            deadline_after=10.0,
            max_screenshots=2,
            min_interval=0.1,
        )
        self.assertEqual(
            result.status,
            settling.SettleStatus.PARTIAL_DISPLACEMENT_SCREENSHOT_LIMIT,
        )
        self.assertEqual(len(result.observations), 2)
        self.assertLess(result.finished_monotonic, result.deadline_monotonic)
        self.assert_one_movement_only(qmp)

    def test_ambiguous_cursor_locations_fail_closed(self) -> None:
        result, qmp = self.observe(
            [[(465, 180), (363, 180)]],
            target_region=(360, 175, 372, 192),
        )
        self.assertEqual(result.status, settling.SettleStatus.AMBIGUOUS_CURSOR_LOCATION)
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_unavailable_cursor_measurement_is_explicit(self) -> None:
        qmp = FakeQmp(self.clock, [[]], self.template)
        result = settling.settle_pointer(
            qmp,
            initial_position=(500, 180),
            dx=-137,
            dy=0,
            cursor_template=self.template,
            screenshot_directory=self.directory,
            deadline=self.clock.now + 2,
            max_screenshots=4,
            min_interval=0.2,
        )
        self.assertEqual(result.status, settling.SettleStatus.CURSOR_MEASUREMENT_UNAVAILABLE)
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_stale_capture_metadata_is_rejected_not_treated_as_success(self) -> None:
        result, qmp = self.observe(
            [[(363, 180)]],
            qmp_options={"stale_capture": True},
            target_region=(360, 175, 372, 192),
        )
        self.assertEqual(result.status, settling.SettleStatus.SCREENSHOT_STALE_OR_REUSED)
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_endpoint_ownership_loss_stops_later_observations(self) -> None:
        result, qmp = self.observe(
            [[(465, 180)], [(363, 180)]],
            qmp_options={"fail_verify_call": 4},
        )
        self.assertEqual(result.status, settling.SettleStatus.VERIFIED_ENDPOINT_OWNERSHIP_LOST)
        self.assertEqual(len(qmp.screenshot_calls), 1)
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_capture_timeout_terminates_at_the_absolute_deadline(self) -> None:
        result, qmp = self.observe(
            [[(363, 180)]],
            deadline_after=0.4,
            qmp_options={"screenshot_error": QMPTimeout("capture timed out")},
        )
        self.assertEqual(result.status, settling.SettleStatus.OBSERVATION_DEADLINE_EXCEEDED)
        self.assertLessEqual(self.clock.now, result.deadline_monotonic)
        self.assert_one_movement_only(qmp)

    def test_two_axis_movement_checks_both_coordinates(self) -> None:
        result, qmp = self.observe(
            [[(377, 299)]],
            initial=(500, 340),
            dx=-123,
            dy=-41,
            target_region=(370, 292, 386, 306),
        )
        self.assertEqual(result.status, settling.SettleStatus.EXPECTED_POSITION_OBSERVED)
        self.assertEqual(result.expected_position, (377, 299))
        self.assertEqual(result.observed_position, (377, 299))
        self.assertTrue(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_boundary_clipping_is_a_successful_clamped_destination(self) -> None:
        qmp = FakeQmp(self.clock, [[(0, 20)]], self.template)
        result = settling.settle_pointer(
            qmp,
            initial_position=(5, 20),
            dx=-20,
            dy=0,
            cursor_template=self.template,
            screenshot_directory=self.directory,
            deadline=self.clock.now + 2,
            target_region=(0, 10, 5, 40),
        )
        self.assertEqual(result.status, settling.SettleStatus.EXPECTED_CLAMPED_POSITION_OBSERVED)
        self.assertEqual(result.expected_position, (0, 20))
        self.assertTrue(result.clipped)
        self.assertTrue(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_unexpected_overshoot_is_not_success(self) -> None:
        result, qmp = self.observe(
            [[(350, 180)]], target_region=(340, 175, 355, 192)
        )
        self.assertEqual(result.status, settling.SettleStatus.UNEXPECTED_OVERSHOOT)
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_pointer_outside_requested_movement_corridor_is_mismatch(self) -> None:
        result, qmp = self.observe([[(520, 190)]])
        self.assertEqual(result.status, settling.SettleStatus.UNEXPECTED_POSITION_MISMATCH)
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_invalid_ppm_is_classified_as_incomplete_or_invalid(self) -> None:
        result, qmp = self.observe(
            [[(363, 180)]], qmp_options={"invalid_frame": True}
        )
        self.assertEqual(result.status, settling.SettleStatus.SCREENSHOT_INCOMPLETE_OR_INVALID)
        self.assert_one_movement_only(qmp)

    def test_interrupted_capture_propagates_without_extra_input(self) -> None:
        qmp = FakeQmp(
            self.clock,
            [[(363, 180)]],
            self.template,
            screenshot_error=KeyboardInterrupt(),
        )
        with self.assertRaises(KeyboardInterrupt):
            settling.settle_pointer(
                qmp,
                initial_position=(500, 180),
                dx=-137,
                dy=0,
                cursor_template=self.template,
                screenshot_directory=self.directory,
                deadline=self.clock.now + 2,
            )
        self.assert_one_movement_only(qmp)

    def test_target_region_miss_does_not_authorize_a_click(self) -> None:
        result, qmp = self.observe(
            [[(363, 180)]], target_region=(400, 160, 420, 200)
        )
        self.assertEqual(result.status, settling.SettleStatus.TARGET_REGION_NOT_REACHED)
        self.assertFalse(result.click_authorized)
        self.assert_one_movement_only(qmp)

    def test_measurement_parser_rejects_non_p6_and_ambiguous_finds(self) -> None:
        path = self.directory / "invalid.ppm"
        path.write_bytes(b"P3\n1 1\n255\n0 0 0\n")
        with self.assertRaises(settling.PpmReadError):
            settling.PpmImage.read(path)
        image_path = self.directory / "ambiguous.ppm"
        image_path.write_bytes(ppm_with_cursors([(90, 100), (300, 200)], self.template))
        with self.assertRaises(settling.AmbiguousCursorError):
            settling.locate_cursor(settling.PpmImage.read(image_path), self.template)


if __name__ == "__main__":
    unittest.main()
