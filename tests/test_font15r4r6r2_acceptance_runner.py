"""Runner-level tests for FONT15R4R6R2 admission and click authorization."""

from __future__ import annotations

import sys
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
HELPER_DIR = ROOT / "out" / "font15r1-validation" / "guest"
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(HELPER_DIR))

import font15r1_pointer_settling as settling  # noqa: E402
import font15r1_qac_admission as qac  # noqa: E402
from tools.fonts import font15r4r6r2_acceptance as runner  # noqa: E402


REQUEST_ID = "0189d791aa3b4d40902143b020ce12a7"


class FakeClock:
    def __init__(self, now: float = 100.0) -> None:
        self.now = now
        self.sleeps: list[float] = []

    def monotonic(self) -> float:
        return self.now

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        self.now += seconds


def snapshot(
    state: str,
    *,
    request_id: str = REQUEST_ID,
    owner_pid: int | None = 111,
    owner_verified: bool = True,
    child_pid: int | None = 222,
    degraded: bool = False,
    version: int | None = None,
) -> qac.ReservationSnapshot:
    return qac.ReservationSnapshot(
        request_id=request_id,
        state=state,
        owner_pid=owner_pid,
        owner_verified=owner_verified,
        child_pid=child_pid,
        degraded=degraded,
        version=version,
        requested_at_utc="2026-11-21T18:00:00+00:00",
        granted_at_utc="2026-11-21T18:00:01+00:00" if state in {"Granted", "Starting", "Running"} else None,
        owner_started_at_utc="2026-11-21T17:59:59+00:00",
        child_started_at_utc="2026-11-21T18:00:01+00:00" if child_pid is not None else None,
        child_executable="C:\\Program Files\\QEMU\\qemu-system-x86_64.exe" if child_pid is not None else None,
    )


class FakeRun:
    pid = 111

    def __init__(self, exit_code: int | None = None) -> None:
        self.exit_code = exit_code

    def poll(self) -> int | None:
        return self.exit_code


class FakeDiscovery:
    def __init__(self, responses: list[Any] | None = None) -> None:
        self.responses = list(responses or [[REQUEST_ID]])
        self.calls: list[tuple[int, float]] = []

    def find_owned_requests(self, owner_pid: int, *, timeout: float) -> list[str]:
        self.calls.append((owner_pid, timeout))
        if not self.responses:
            return []
        result = self.responses.pop(0)
        if isinstance(result, BaseException):
            raise result
        return list(result)


class FakeCoordinator:
    def __init__(
        self,
        clock: FakeClock,
        inspections: list[Any],
        cancellations: list[Any] | None = None,
        *,
        inspect_delay: float = 0.0,
    ) -> None:
        self.clock = clock
        self.inspections = list(inspections)
        self.cancellations = list(cancellations or [])
        self.inspect_delay = inspect_delay
        self.inspect_calls: list[tuple[str, float, float]] = []
        self.cancel_calls: list[tuple[str, float, float]] = []

    def inspect(self, request_id: str, *, timeout: float) -> qac.ReservationSnapshot:
        self.inspect_calls.append((request_id, timeout, self.clock.now))
        self.clock.now += self.inspect_delay
        outcome = self.inspections.pop(0) if self.inspections else TimeoutError("no fake snapshot")
        if isinstance(outcome, BaseException):
            raise outcome
        return outcome

    def cancel(self, request_id: str, *, timeout: float) -> qac.ReservationSnapshot:
        self.cancel_calls.append((request_id, timeout, self.clock.now))
        outcome = self.cancellations.pop(0) if self.cancellations else TimeoutError("no fake cancel")
        if isinstance(outcome, BaseException):
            raise outcome
        return outcome


class AdmissionIntegrationTests(unittest.TestCase):
    def make_runner(
        self,
        coordinator: FakeCoordinator,
        *,
        clock: FakeClock | None = None,
        discovery: FakeDiscovery | None = None,
        cleanup: Any = None,
        admission_seconds: float = 0.25,
    ) -> tuple[runner.QacAcceptanceRunner, FakeRun, list[str], FakeClock]:
        clock = clock or coordinator.clock
        process = FakeRun()
        start_calls: list[str] = []

        def start_run() -> FakeRun:
            start_calls.append("qac-run")
            return process

        result = runner.QacAcceptanceRunner(
            start_run=start_run,
            discovery=discovery or FakeDiscovery(),
            coordinator=coordinator,
            admission_seconds=admission_seconds,
            discovery_call_seconds=0.1,
            poll_interval=0.1,
            reconciliation_timeout=0.4,
            clock=clock.monotonic,
            sleeper=clock.sleep,
            late_grant_cleanup=cleanup,
        )
        return result, process, start_calls, clock

    def test_immediate_grant_runs_through_the_production_admission_boundary(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(clock, [snapshot("Running", version=1)])
        gate, process, starts, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "TIMELY_ADMISSION_RUNNING")
        self.assertTrue(result.guest_input_authorized)
        self.assertEqual(result.request_id, REQUEST_ID)
        self.assertIs(gate.owner, process)
        self.assertEqual(starts, ["qac-run"])
        self.assertEqual(len(coordinator.inspect_calls), 1)

    def test_queued_then_granted_is_not_authorized_until_running(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Waiting", version=1), snapshot("Running", version=2)],
        )
        gate, _, _, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "TIMELY_ADMISSION_RUNNING")
        self.assertTrue(result.guest_input_authorized)
        self.assertEqual([call[0] for call in coordinator.inspect_calls], [REQUEST_ID, REQUEST_ID])
        self.assertFalse(coordinator.cancel_calls)

    def test_still_queued_at_deadline_is_cancelled_and_never_authorized(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Waiting", version=i) for i in (1, 2, 3, 4)] + [snapshot("Cancelled", version=6)],
            [snapshot("Cancelled", version=5)],
        )
        gate, _, _, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "CANCELLED_BEFORE_ADMISSION")
        self.assertFalse(result.guest_input_authorized)
        self.assertTrue(result.cleanup_complete)
        self.assertEqual([call[0] for call in coordinator.cancel_calls], [REQUEST_ID])

    def test_grant_winning_cancel_race_is_reconciled_and_cleaned(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Waiting", version=i) for i in (1, 2, 3, 4)] + [snapshot("Running", version=6)],
            [RuntimeError("grant won before cancel")],
        )
        cleaned: list[tuple[int, str, int | None]] = []

        def cleanup(owner: FakeRun, result: qac.AdmissionResult) -> bool:
            cleaned.append((owner.pid, result.request_id, result.child_pid))
            return True

        gate, _, _, _ = self.make_runner(coordinator, clock=clock, cleanup=cleanup)
        result = gate.admit()
        self.assertEqual(result.status, "LATE_GRANT_CLEANED_UP")
        self.assertFalse(result.guest_input_authorized)
        self.assertEqual(cleaned, [(111, REQUEST_ID, 222)])
        self.assertEqual([call[0] for call in coordinator.cancel_calls], [REQUEST_ID])

    def test_inspection_returned_late_is_classified_and_owned_child_is_cleaned(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(clock, [snapshot("Running", version=1)], inspect_delay=0.4)
        cleaned: list[int] = []
        gate, _, _, _ = self.make_runner(
            coordinator,
            clock=clock,
            cleanup=lambda owner, result: cleaned.append(result.child_pid or 0) or True,
        )
        result = gate.admit()
        self.assertEqual(result.status, "LATE_GRANT_CLEANED_UP")
        self.assertFalse(result.guest_input_authorized)
        self.assertTrue(result.cleanup_complete)
        self.assertEqual(cleaned, [222])
        self.assertGreater(result.finished_monotonic, result.deadline_monotonic)

    def test_unavailable_coordinator_fails_closed_without_cancelling_foreign_work(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(clock, [TimeoutError("QAC unavailable")] * 20)
        gate, _, _, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertFalse(result.guest_input_authorized)
        self.assertTrue(result.owner_process_must_remain_alive)
        self.assertFalse(coordinator.cancel_calls)

    def test_wrong_or_stale_reservation_response_cannot_authorize_or_cancel(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [
                snapshot("Running", request_id="f" * 32, version=2),
                snapshot("Running", request_id="f" * 32, version=2),
            ],
        )
        gate, _, _, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertFalse(result.guest_input_authorized)
        self.assertFalse(coordinator.cancel_calls)

    def test_version_stale_grant_response_cannot_replace_latest_queued_state(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [
                snapshot("Waiting", version=2),
                snapshot("Running", version=1),
                snapshot("Waiting", version=2),
                snapshot("Waiting", version=2),
                snapshot("Cancelled", version=4),
            ],
            [snapshot("Cancelled", version=3)],
        )
        gate, _, _, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "CANCELLED_BEFORE_ADMISSION")
        self.assertFalse(result.guest_input_authorized)
        self.assertTrue(any(row.get("stale") for row in result.decision["transitions"]))

    def test_prior_owner_mismatch_cannot_be_overwritten_by_later_running_response(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Running", owner_pid=999, version=1), snapshot("Running", version=2)],
        )
        cleaned: list[str] = []
        gate, _, _, _ = self.make_runner(
            coordinator,
            clock=clock,
            cleanup=lambda owner, result: cleaned.append(result.request_id) or True,
        )
        result = gate.admit()
        self.assertEqual(result.status, "LATE_GRANT_CLEANED_UP")
        self.assertFalse(result.guest_input_authorized)
        self.assertEqual(cleaned, [REQUEST_ID])

    def test_ambiguous_state_does_not_enable_a_legacy_launch_fallback(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(clock, [snapshot("Maybe", version=1), snapshot("Maybe", version=1)])
        starts: list[str] = []
        process = FakeRun()
        gate = runner.QacAcceptanceRunner(
            start_run=lambda: starts.append("qac-run") or process,
            discovery=FakeDiscovery(),
            coordinator=coordinator,
            admission_seconds=0.25,
            discovery_call_seconds=0.1,
            poll_interval=0.1,
            reconciliation_timeout=0.4,
            clock=clock.monotonic,
            sleeper=clock.sleep,
        )
        result = gate.admit()
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertFalse(result.guest_input_authorized)
        self.assertEqual(starts, ["qac-run"])
        self.assertFalse(coordinator.cancel_calls)
        self.assertFalse(hasattr(gate, "start_direct_qemu"))

    def test_foreign_owner_active_is_never_cancelled_or_authorized(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Running", owner_pid=999, version=1), snapshot("Running", owner_pid=999, version=1)],
        )
        gate, _, _, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertFalse(result.guest_input_authorized)
        self.assertFalse(coordinator.cancel_calls)

    def test_cancellation_failure_that_reconciles_to_grant_uses_owned_cleanup(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Waiting", version=i) for i in (1, 2, 3, 4)] + [snapshot("Running", version=6)],
            [TimeoutError("cancel response lost")],
        )
        cleaned: list[str] = []
        gate, _, _, _ = self.make_runner(
            coordinator,
            clock=clock,
            cleanup=lambda owner, result: cleaned.append(result.request_id) or True,
        )
        result = gate.admit()
        self.assertEqual(result.status, "LATE_GRANT_CLEANED_UP")
        self.assertEqual(cleaned, [REQUEST_ID])
        self.assertFalse(result.guest_input_authorized)

    def test_late_discovered_grant_uses_original_deadline_and_cleanup(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(clock, [snapshot("Running", version=1)], inspect_delay=0.02)
        discovery = FakeDiscovery([[], [REQUEST_ID]])
        cleaned: list[int] = []
        gate, _, _, _ = self.make_runner(
            coordinator,
            clock=clock,
            discovery=discovery,
            cleanup=lambda owner, result: cleaned.append(owner.pid) or True,
            admission_seconds=0.1,
        )
        result = gate.admit()
        self.assertEqual(result.status, "LATE_GRANT_CLEANED_UP")
        self.assertFalse(result.guest_input_authorized)
        self.assertEqual(cleaned, [111])
        self.assertGreater(result.finished_monotonic, result.deadline_monotonic)
        self.assertGreaterEqual(len(discovery.calls), 2)

    def test_duplicate_running_responses_do_not_start_or_admit_twice(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Running", version=1), snapshot("Running", version=1), snapshot("Running", version=2)],
        )
        gate, _, starts, _ = self.make_runner(coordinator, clock=clock)
        result = gate.admit()
        self.assertEqual(result.status, "TIMELY_ADMISSION_RUNNING")
        self.assertEqual(starts, ["qac-run"])
        self.assertEqual(result.decision["query_count"], 1)


class QacRunnerMechanismTests(unittest.TestCase):
    def test_qac_launcher_constructs_one_direct_managed_run_without_shell(self) -> None:
        with tempfile.TemporaryDirectory(prefix="font15r4r6r2-qac-launch-") as temp:
            root = Path(temp)
            calls: list[tuple[list[str], dict[str, Any]]] = []

            def fake_popen(command: list[str], **kwargs: Any) -> FakeRun:
                calls.append((command, kwargs))
                return FakeRun()

            launcher = runner.QacRunLauncher(
                qac_cli=Path("C:/tools/qac.exe"),
                project="guideXOSServer",
                mode="acceptance",
                qemu_executable=Path("C:/Program Files/qemu/qemu-system-x86_64.exe"),
                qemu_arguments=["-m", "2048", "-qmp", "tcp:127.0.0.1:63801,server=on,wait=off"],
                stdout_path=root / "qac.stdout",
                stderr_path=root / "qac.stderr",
                cwd=root,
                popen=fake_popen,
            )
            launched = launcher.start()
            self.assertEqual(launched.pid, 111)
            self.assertEqual(len(calls), 1)
            command, kwargs = calls[0]
            self.assertEqual(command[:6], [str(Path("C:/tools/qac.exe")), "run", "--project", "guideXOSServer", "--mode", "acceptance"])
            self.assertEqual(command[6:8], ["--", str(Path("C:/Program Files/qemu/qemu-system-x86_64.exe"))])
            self.assertIn("-qmp", command)
            self.assertIs(kwargs["stdin"], runner.subprocess.DEVNULL)
            self.assertIs(kwargs["shell"], False)
            self.assertFalse(hasattr(launcher, "start_direct_qemu"))
            self.assertTrue((root / "qac.stdout").exists())
            self.assertTrue((root / "qac.stderr").exists())

    def test_qac_list_discovery_is_bounded_and_matches_exact_owner_pid(self) -> None:
        original_run = runner.subprocess.run
        calls: list[tuple[list[str], dict[str, Any]]] = []

        def fake_run(command: list[str], **kwargs: Any) -> Any:
            calls.append((command, kwargs))

            class Completed:
                returncode = 0
                stderr = ""
                stdout = (
                    "Reservations: 2\n"
                    f"  {REQUEST_ID} | guideXOSServer | acceptance | Waiting | queue 1 | user (PID 111) | owner Verified\n"
                    f"  {'f' * 32} | OtherProject | acceptance | Running | queue 0 | user (PID 999) | owner Verified\n"
                )

            return Completed()

        runner.subprocess.run = fake_run
        try:
            adapter = runner.QacCliAdapter(Path("qac.exe"))
            found = adapter.find_owned_requests(111, timeout=0.4)
        finally:
            runner.subprocess.run = original_run
        self.assertEqual(found, [REQUEST_ID])
        self.assertEqual(calls[0][0], ["qac.exe", "list"])
        self.assertEqual(calls[0][1]["timeout"], 0.4)

    def test_authoritative_inspect_grant_timestamp_and_process_identity_are_preserved(self) -> None:
        original_run = runner.subprocess.run

        def fake_run(command: list[str], **kwargs: Any) -> Any:
            class Completed:
                returncode = 0
                stderr = ""
                stdout = (
                    '{"id":"' + REQUEST_ID + '","state":"Running",'
                    '"owner":"guideX (PID 111)","ownerLiveness":"Verified",'
                    '"processId":222,"processIdentity":"C:\\\\Program Files\\\\QEMU\\\\qemu-system-x86_64.exe",'
                    '"processStartTimeUtc":"2026-11-21T18:00:02Z",'
                    '"ownerProcessStartedAtUtc":"2026-11-21T17:59:59Z",'
                    '"requestedAtUtc":"2026-11-21T18:00:00Z",'
                    '"grantedAtUtc":"2026-11-21T18:00:01Z","degraded":false}'
                )

            return Completed()

        runner.subprocess.run = fake_run
        try:
            adapter = runner.QacCliAdapter(Path("qac.exe"))
            result = adapter.inspect(REQUEST_ID, timeout=0.3)
        finally:
            runner.subprocess.run = original_run
        self.assertEqual(result.granted_at_utc, "2026-11-21T18:00:01Z")
        self.assertEqual(result.owner_started_at_utc, "2026-11-21T17:59:59Z")
        self.assertEqual(result.child_started_at_utc, "2026-11-21T18:00:02Z")
        self.assertEqual(result.child_executable, "C:\\Program Files\\QEMU\\qemu-system-x86_64.exe")

    def test_late_grant_cleanup_quits_only_verified_child_and_waits_for_release(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(
            clock,
            [snapshot("Running"), snapshot("Released", child_pid=None)],
        )
        owner = FakeRun()
        qmp_calls: list[tuple[int, Path, int, str]] = []

        class FakeOwnedQmp:
            def _command(self, command: str) -> None:
                qmp_calls.append((63801, Path("trace.jsonl"), 222, command))
                owner.exit_code = 0

            def close(self) -> None:
                return None

        decision = qac.AdmissionResult(
            status="LATE_GRANT_REQUIRES_CLEANUP",
            request_id=REQUEST_ID,
            final_state="Running",
            owner_pid=111,
            owner_verified=True,
            child_pid=222,
            degraded=False,
        )
        cleanup = runner.QacOwnedGrantCleanup(
            coordinator=coordinator,
            qmp_port=63801,
            display_port=41597,
            qmp_trace=Path("trace.jsonl"),
            clock=clock.monotonic,
            sleeper=clock.sleep,
            qmp_factory=lambda port, trace, pid: FakeOwnedQmp(),
        )
        self.assertTrue(cleanup(owner, decision))
        self.assertEqual(qmp_calls, [(63801, Path("trace.jsonl"), 222, "quit")])
        self.assertEqual([call[0] for call in coordinator.inspect_calls], [REQUEST_ID, REQUEST_ID])

    def test_late_grant_cleanup_refuses_mismatched_owner(self) -> None:
        clock = FakeClock()
        coordinator = FakeCoordinator(clock, [snapshot("Running", owner_pid=999)])
        owner = FakeRun()
        qmp_calls: list[int] = []
        decision = qac.AdmissionResult(
            status="LATE_GRANT_REQUIRES_CLEANUP",
            request_id=REQUEST_ID,
            final_state="Running",
            owner_pid=111,
            owner_verified=True,
            child_pid=222,
            degraded=False,
        )
        cleanup = runner.QacOwnedGrantCleanup(
            coordinator=coordinator,
            qmp_port=63801,
            display_port=41597,
            qmp_trace=Path("trace.jsonl"),
            clock=clock.monotonic,
            sleeper=clock.sleep,
            qmp_factory=lambda port, trace, pid: qmp_calls.append(pid),
        )
        self.assertFalse(cleanup(owner, decision))
        self.assertFalse(qmp_calls)


def ppm_with_cursors(
    positions: list[tuple[int, int]],
    template: settling.CursorTemplate,
    *,
    width: int = 640,
    height: int = 400,
) -> bytes:
    image = bytearray((31, 53, 79) * (width * height))
    for origin_x, origin_y in positions:
        for x, y, color in template.pixels:
            px, py = origin_x + x, origin_y + y
            if 0 <= px < width and 0 <= py < height:
                offset = (py * width + px) * 3
                image[offset : offset + 3] = bytes(color)
    return f"P6\n{width} {height}\n255\n".encode("ascii") + bytes(image)


@dataclass
class FakeDrag:
    success: bool = True
    button_state: str = "release-command-acknowledged"
    primary_error: dict[str, Any] | None = None
    cleanup_errors: list[dict[str, Any]] | None = None


class FakeInteractiveQmp:
    def __init__(
        self,
        clock: FakeClock,
        frames: list[list[tuple[int, int]]],
        template: settling.CursorTemplate,
        *,
        fail_verify_call: int | None = None,
        screenshot_timeout_call: int | None = None,
        drag_result: FakeDrag | None = None,
    ) -> None:
        self._clock = clock
        self.clock = clock.monotonic
        self.sleeper = clock.sleep
        self.frames = frames
        self.template = template
        self.fail_verify_call = fail_verify_call
        self.screenshot_timeout_call = screenshot_timeout_call
        self.drag_result = drag_result or FakeDrag()
        self.verify_count = 0
        self.screenshot_count = 0
        self.relative_calls: list[tuple[int, int]] = []
        self.clicks: list[tuple[str, int, int]] = []
        self.wheels: list[tuple[int, int]] = []
        self.trace: list[dict[str, Any]] = []

    def verify_ownership(self, *, deadline: float | None = None) -> None:
        self.verify_count += 1
        if self.fail_verify_call == self.verify_count:
            raise RuntimeError("QMP endpoint identity changed")
        if deadline is not None and self._clock.now >= deadline:
            raise TimeoutError("ownership deadline expired")

    def screenshot(self, path: Path, *, deadline: float | None = None) -> dict[str, Any]:
        self.screenshot_count += 1
        if self.screenshot_timeout_call == self.screenshot_count:
            if deadline is not None:
                self._clock.now = deadline
            raise TimeoutError("screenshot timed out")
        positions = self.frames[min(self.screenshot_count - 1, len(self.frames) - 1)]
        payload = ppm_with_cursors(positions, self.template)
        path.write_bytes(payload)
        self._clock.now += 0.01
        return {
            "fresh_capture": True,
            "path": str(path.resolve()),
            "request_monotonic": self._clock.now - 0.01,
            "artifact_verified_monotonic": self._clock.now,
            "size_bytes": len(payload),
            "width": 640,
            "height": 400,
        }

    def relative(self, dx: int, dy: int, delay: float, combined: bool = False, *, deadline: float | None = None) -> None:
        self.relative_calls.append((dx, dy))
        self._clock.now += 0.01

    def _record(self, record: dict[str, Any]) -> None:
        self.trace.append(record)

    def drag(self, button: str, dx: int, dy: int, delay: float) -> FakeDrag:
        self.clicks.append((button, dx, dy))
        return self.drag_result

    def wheel(self, delta: int, delay: float) -> FakeDrag:
        self.wheels.append((delta, 0))
        return FakeDrag()


class SettledClickIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.template = settling.CursorTemplate.from_cpp_source(ROOT / "kernel" / "core" / "desktop.cpp")

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="font15r4r6r2-runner-")
        self.directory = Path(self.temp.name)
        self.clock = FakeClock()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def interactor(
        self,
        frames: list[list[tuple[int, int]]],
        **qmp_options: Any,
    ) -> tuple[runner.SettledPointerInteractor, FakeInteractiveQmp]:
        qmp = FakeInteractiveQmp(self.clock, frames, self.template, **qmp_options)
        return (
            runner.SettledPointerInteractor(
                qmp,
                cursor_template=self.template,
                screenshot_directory=self.directory,
            ),
            qmp,
        )

    def test_settled_pointer_authorizes_exactly_one_click(self) -> None:
        interactor, qmp = self.interactor([[(100, 100)], [(200, 150)]])
        result = interactor.click(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
            min_interval=0.05,
        )
        self.assertEqual(result.status, "CLICK_SENT")
        self.assertTrue(result.click_attempted)
        self.assertEqual(qmp.relative_calls, [(100, 50)])
        self.assertEqual(qmp.clicks, [("left", 0, 0)])

    def test_partial_movement_never_authorizes_a_click(self) -> None:
        interactor, qmp = self.interactor([[(100, 100)], [(150, 100)], [(150, 100)]])
        result = interactor.click(
            target_position=(200, 100),
            target_region=(190, 90, 210, 110),
            deadline=self.clock.now + 0.8,
            min_interval=0.2,
            max_screenshots=8,
        )
        self.assertEqual(result.status, "CLICK_BLOCKED")
        self.assertFalse(result.click_attempted)
        self.assertEqual(len(qmp.relative_calls), 1)
        self.assertFalse(qmp.clicks)

    def test_delayed_completion_allows_one_intended_click_after_observation(self) -> None:
        interactor, qmp = self.interactor(
            [[(100, 100)], [(150, 125)], [(180, 140)], [(200, 150)]]
        )
        result = interactor.click(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
            min_interval=0.1,
        )
        self.assertEqual(result.status, "CLICK_SENT")
        self.assertEqual(len(result.settle["observations"]), 3)
        self.assertEqual(len(qmp.relative_calls), 1)
        self.assertEqual(len(qmp.clicks), 1)

    def test_ambiguous_initial_cursor_measurement_blocks_movement_and_click(self) -> None:
        interactor, qmp = self.interactor([[(100, 100), (200, 150)]])
        result = interactor.click(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
        )
        self.assertEqual(result.status, "AMBIGUOUS_INITIAL_CURSOR")
        self.assertFalse(qmp.relative_calls)
        self.assertFalse(qmp.clicks)

    def test_lost_qmp_ownership_after_measurement_blocks_click(self) -> None:
        interactor, qmp = self.interactor([[(100, 100)], [(200, 150)]], fail_verify_call=2)
        result = interactor.click(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
        )
        self.assertEqual(result.status, "CLICK_BLOCKED")
        self.assertFalse(result.click_attempted)
        self.assertFalse(qmp.clicks)

    def test_observation_timeout_is_bounded_and_blocks_click(self) -> None:
        deadline = self.clock.now + 0.3
        interactor, qmp = self.interactor(
            [[(100, 100)], [(200, 150)]], screenshot_timeout_call=2
        )
        result = interactor.click(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=deadline,
            min_interval=0.05,
        )
        self.assertEqual(result.status, "CLICK_BLOCKED")
        self.assertLessEqual(self.clock.now, deadline)
        self.assertFalse(qmp.clicks)

    def test_safe_wheel_uses_same_settling_and_region_check(self) -> None:
        interactor, qmp = self.interactor([[(100, 100)], [(200, 150)]])
        result = interactor.wheel(
            wheel_delta=1,
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
            min_interval=0.05,
        )
        self.assertEqual(result.status, "WHEEL_SENT")
        self.assertFalse(result.click_attempted)
        self.assertEqual(qmp.wheels, [(1, 0)])
        self.assertFalse(qmp.clicks)

    def test_failed_click_is_not_repeated_after_release_state_is_known(self) -> None:
        interactor, qmp = self.interactor(
            [[(100, 100)], [(200, 150)]],
            drag_result=FakeDrag(success=False, button_state="release-command-acknowledged"),
        )
        result = interactor.click(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
            min_interval=0.05,
        )
        self.assertEqual(result.status, "CLICK_FAILED")
        self.assertEqual(len(qmp.clicks), 1)
        self.assertEqual(result.input_result["button_state"], "release-command-acknowledged")

    def test_guest_session_uses_confirmed_qac_child_and_same_settle_click_path(self) -> None:
        coordinator = FakeCoordinator(self.clock, [snapshot("Running", version=1)])
        gate, _, _, _ = AdmissionIntegrationTests().make_runner(coordinator, clock=self.clock)
        qmp_instances: list[tuple[int, Path, int, FakeInteractiveQmp]] = []

        def qmp_factory(port: int, trace: Path, pid: int) -> FakeInteractiveQmp:
            qmp = FakeInteractiveQmp(self.clock, [[(100, 100)], [(200, 150)]], self.template)
            qmp_instances.append((port, trace, pid, qmp))
            return qmp

        session = runner.FontsGuestAcceptanceSession(
            gate,
            qmp_port=63801,
            display_port=41597,
            qmp_trace=self.directory / "qmp.jsonl",
            screenshot_directory=self.directory,
            cursor_template=self.template,
            qmp_factory=qmp_factory,
        )
        admission = session.start()
        self.assertTrue(admission.guest_input_authorized)
        self.assertEqual([(port, pid) for port, _, pid, _ in qmp_instances], [(63801, 222)])
        result = session.click_control(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
            min_interval=0.05,
        )
        self.assertEqual(result.status, "CLICK_SENT")
        self.assertEqual(len(qmp_instances[0][3].clicks), 1)

    def test_guest_session_blocks_input_when_admission_is_ambiguous(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Running", owner_pid=999, version=1), snapshot("Running", owner_pid=999, version=1)],
        )
        gate, _, _, _ = AdmissionIntegrationTests().make_runner(coordinator, clock=self.clock)
        qmp_instances: list[Any] = []
        session = runner.FontsGuestAcceptanceSession(
            gate,
            qmp_port=63801,
            display_port=41597,
            qmp_trace=self.directory / "qmp.jsonl",
            screenshot_directory=self.directory,
            cursor_template=self.template,
            qmp_factory=lambda *args: qmp_instances.append(args),
        )
        admission = session.start()
        self.assertFalse(admission.guest_input_authorized)
        self.assertFalse(qmp_instances)
        result = session.click_control(
            target_position=(200, 150),
            target_region=(190, 140, 210, 160),
            deadline=self.clock.now + 2,
        )
        self.assertEqual(result.status, "GUEST_INPUT_NOT_AUTHORIZED")

    def test_session_shutdown_verifies_qac_release_and_both_endpoints_closed(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Running", version=1), snapshot("Released", child_pid=None, version=2)],
        )
        gate, process, _, _ = AdmissionIntegrationTests().make_runner(coordinator, clock=self.clock)
        quit_commands: list[str] = []

        class Verifier:
            def listener_pids(self, port: int) -> list[int]:
                return []

        class ShutdownQmp:
            verifier = Verifier()

            def verify_ownership(self) -> None:
                return None

            def _command(self, command: str) -> None:
                quit_commands.append(command)
                process.exit_code = 0

            def close(self) -> None:
                return None

        session = runner.FontsGuestAcceptanceSession(
            gate,
            qmp_port=63801,
            display_port=41597,
            qmp_trace=self.directory / "shutdown-qmp.jsonl",
            screenshot_directory=self.directory,
            cursor_template=self.template,
            qmp_factory=lambda *args: ShutdownQmp(),
        )
        self.assertTrue(session.start().guest_input_authorized)
        result = session.shutdown(timeout_seconds=1)
        self.assertEqual(result["status"], "QAC_RELEASE_VERIFIED")
        self.assertEqual(quit_commands, ["quit"])
        self.assertTrue(result["qmp_endpoint_closed"])
        self.assertTrue(result["display_endpoint_closed"])

    def test_shutdown_reports_a_foreign_reused_endpoint_without_touching_it(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Running", version=1), snapshot("Released", child_pid=None, version=2)],
        )
        gate, process, _, _ = AdmissionIntegrationTests().make_runner(coordinator, clock=self.clock)
        quit_commands: list[str] = []

        class Verifier:
            def listener_pids(self, port: int) -> list[int]:
                return [] if port == 63801 else [999]

        class ShutdownQmp:
            verifier = Verifier()

            def verify_ownership(self) -> None:
                return None

            def _command(self, command: str) -> None:
                quit_commands.append(command)
                process.exit_code = 0

            def close(self) -> None:
                return None

        session = runner.FontsGuestAcceptanceSession(
            gate,
            qmp_port=63801,
            display_port=41597,
            qmp_trace=self.directory / "shutdown-qmp-foreign.jsonl",
            screenshot_directory=self.directory,
            cursor_template=self.template,
            qmp_factory=lambda *args: ShutdownQmp(),
        )
        self.assertTrue(session.start().guest_input_authorized)
        result = session.shutdown(timeout_seconds=1)
        self.assertEqual(result["status"], "RECONCILIATION_REQUIRED")
        self.assertEqual(quit_commands, ["quit"])
        self.assertEqual(result["display_listener_pids_after_release"], [999])


if __name__ == "__main__":
    unittest.main()
