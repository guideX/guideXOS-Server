"""Fake-coordinator tests for bounded QAC admission reconciliation."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
HELPER_DIR = ROOT / "out" / "font15r1-validation" / "guest"
sys.path.insert(0, str(HELPER_DIR))

import font15r1_qac_admission as admission  # noqa: E402


REQUEST_ID = "0189d791aa3b4d40902143b020ce12a7"


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0
        self.sleeps: list[float] = []

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        self.now += seconds


def snapshot(
    state: str,
    *,
    version: int | None = None,
    request_id: str = REQUEST_ID,
    owner_verified: bool = True,
    owner_pid: int | None = 111,
    child_pid: int | None = 222,
    degraded: bool = False,
) -> admission.ReservationSnapshot:
    return admission.ReservationSnapshot(
        request_id=request_id,
        state=state,
        owner_pid=owner_pid,
        owner_verified=owner_verified,
        child_pid=child_pid,
        degraded=degraded,
        version=version,
    )


class FakeCoordinator:
    def __init__(
        self,
        clock: FakeClock,
        inspections: list[Any],
        cancellations: list[Any] | None = None,
        *,
        inspect_delay: float = 0.0,
        foreign_reservation_id: str | None = None,
    ) -> None:
        self.clock = clock
        self.inspections = list(inspections)
        self.cancellations = list(cancellations or [])
        self.inspect_delay = inspect_delay
        self.foreign_reservation_id = foreign_reservation_id
        self.inspect_calls: list[tuple[str, float, float]] = []
        self.cancel_calls: list[tuple[str, float, float]] = []

    def inspect(self, request_id: str, *, timeout: float) -> admission.ReservationSnapshot:
        self.inspect_calls.append((request_id, timeout, self.clock.now))
        self.clock.now += self.inspect_delay
        if self.inspections:
            outcome = self.inspections.pop(0)
        else:
            outcome = TimeoutError("no fake inspect result remains")
        if isinstance(outcome, BaseException):
            raise outcome
        return outcome

    def cancel(self, request_id: str, *, timeout: float) -> admission.ReservationSnapshot:
        self.cancel_calls.append((request_id, timeout, self.clock.now))
        if self.cancellations:
            outcome = self.cancellations.pop(0)
        else:
            outcome = TimeoutError("no fake cancel result remains")
        if isinstance(outcome, BaseException):
            raise outcome
        return outcome


class QacAdmissionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.clock = FakeClock()

    def wait(
        self,
        coordinator: FakeCoordinator,
        *,
        deadline: float = 1.0,
        poll_interval: float = 0.1,
        reconcile: float = 0.5,
    ) -> admission.AdmissionResult:
        return admission.wait_for_owned_admission(
            coordinator,
            REQUEST_ID,
            expected_owner_pid=111,
            deadline=deadline,
            clock=self.clock.monotonic,
            sleeper=self.clock.sleep,
            poll_interval=poll_interval,
            call_timeout=0.3,
            reconciliation_timeout=reconcile,
        )

    def test_immediate_grant_requires_verified_running_child(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [
                snapshot("Granted", version=1, child_pid=None),
                snapshot("Running", version=2),
            ],
        )
        result = self.wait(coordinator)
        self.assertEqual(result.status, "TIMELY_ADMISSION_RUNNING")
        self.assertTrue(result.guest_input_authorized)
        self.assertEqual(result.child_pid, 222)
        self.assertEqual(result.query_count, 2)

    def test_busy_then_granted_before_deadline_is_accepted(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Waiting", version=1), snapshot("Running", version=2)],
        )
        result = self.wait(coordinator)
        self.assertEqual(result.status, "TIMELY_ADMISSION_RUNNING")
        self.assertTrue(result.guest_input_authorized)
        self.assertFalse(result.cancel_attempted)

    def test_pending_request_at_deadline_is_cancelled_and_confirmed(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Waiting", version=i) for i in (1, 2, 3, 4)] + [snapshot("Cancelled", version=6)],
            [snapshot("Cancelled", version=5)],
        )
        result = self.wait(coordinator, deadline=0.25)
        self.assertEqual(result.status, "CANCELLED_BEFORE_ADMISSION")
        self.assertEqual(result.final_state, "Cancelled")
        self.assertTrue(result.reconciliation_complete)
        self.assertEqual(result.cancel_targets, [REQUEST_ID])
        self.assertFalse(result.guest_input_authorized)

    def test_grant_found_at_deadline_never_authorizes_input(self) -> None:
        coordinator = FakeCoordinator(self.clock, [snapshot("Waiting", version=1), snapshot("Running", version=2)])
        result = self.wait(coordinator, deadline=0.1)
        self.assertEqual(result.status, "LATE_GRANT_REQUIRES_CLEANUP")
        self.assertFalse(result.guest_input_authorized)
        self.assertTrue(result.owner_process_must_remain_alive)
        self.assertEqual(coordinator.cancel_calls, [])

    def test_previously_observed_grant_is_not_lost_when_final_inspection_fails(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Granted", version=1, child_pid=None)]
            + [TimeoutError("coordinator temporarily unavailable")] * 8,
        )
        result = self.wait(coordinator, deadline=0.25)
        self.assertEqual(result.status, "LATE_GRANT_REQUIRES_CLEANUP")
        self.assertEqual(result.final_state, "Granted")
        self.assertFalse(result.guest_input_authorized)
        self.assertTrue(result.owner_process_must_remain_alive)
        self.assertEqual(coordinator.cancel_calls, [])

    def test_blocking_inspect_returned_after_deadline_is_recorded_as_late(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Running", version=1)],
            inspect_delay=0.4,
        )
        result = self.wait(coordinator, deadline=0.25)
        self.assertEqual(result.status, "LATE_GRANT_REQUIRES_CLEANUP")
        self.assertFalse(result.guest_input_authorized)
        self.assertTrue(result.late_returns)
        self.assertEqual(result.late_returns[0]["state"], "Running")

    def test_cancel_rejected_by_grant_race_is_reconciled(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Waiting", version=i) for i in (1, 2, 3, 4)] + [snapshot("Running", version=6)],
            [RuntimeError("reservation already transitioned")],
        )
        result = self.wait(coordinator, deadline=0.25)
        self.assertEqual(result.status, "LATE_GRANT_REQUIRES_CLEANUP")
        self.assertEqual(coordinator.cancel_calls[0][0], REQUEST_ID)
        self.assertFalse(result.guest_input_authorized)
        self.assertFalse(result.reconciliation_complete)

    def test_foreign_reservation_is_never_cancelled(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Waiting", version=i) for i in (1, 2, 3, 4)] + [snapshot("Cancelled", version=6)],
            [snapshot("Cancelled", version=5)],
            foreign_reservation_id="b3efbee849c74f2db5b05c56dc324188",
        )
        result = self.wait(coordinator, deadline=0.25)
        self.assertEqual(result.status, "CANCELLED_BEFORE_ADMISSION")
        self.assertEqual([call[0] for call in coordinator.cancel_calls], [REQUEST_ID])
        self.assertNotEqual(coordinator.cancel_calls[0][0], coordinator.foreign_reservation_id)

    def test_coordinator_unavailable_leaves_owned_request_for_reconciliation(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [TimeoutError("coordinator unavailable")] * 8,
        )
        result = self.wait(coordinator, deadline=0.2)
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertFalse(result.reconciliation_complete)
        self.assertTrue(result.owner_process_must_remain_alive)
        self.assertFalse(result.guest_input_authorized)
        self.assertEqual(coordinator.cancel_calls, [])

    def test_poll_delay_is_capped_by_deadline_and_records_late_observation(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Waiting", version=1), snapshot("Running", version=2)],
            inspect_delay=0.04,
        )
        result = self.wait(coordinator, deadline=0.19, poll_interval=0.1)
        self.assertEqual(result.status, "TIMELY_ADMISSION_RUNNING")
        self.assertTrue(all(delay <= 0.1 for delay in self.clock.sleeps))
        self.assertLessEqual(result.finished_monotonic, result.deadline_monotonic)

    def test_duplicate_and_stale_response_do_not_discard_a_grant(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [
                snapshot("Waiting", version=4),
                snapshot("Waiting", version=4),
                snapshot("Granted", version=6, child_pid=None),
                snapshot("Waiting", version=5),
                snapshot("Running", version=7),
            ],
        )
        result = self.wait(coordinator, deadline=0.7, poll_interval=0.1)
        self.assertEqual(result.status, "TIMELY_ADMISSION_RUNNING")
        self.assertTrue(any(item.get("duplicate") for item in result.transitions))
        self.assertTrue(any(item.get("stale") for item in result.transitions))
        self.assertTrue(result.guest_input_authorized)

    def test_ambiguous_coordinator_state_fails_closed(self) -> None:
        coordinator = FakeCoordinator(self.clock, [snapshot("Maybe", version=1)])
        result = self.wait(coordinator, deadline=0.2)
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertFalse(result.guest_input_authorized)
        self.assertFalse(result.reconciliation_complete)

    def test_cancel_response_for_another_request_is_not_accepted(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Waiting", version=i) for i in (1, 2, 3, 4)]
            + [snapshot("Waiting", version=6, request_id="f" * 32)],
            [snapshot("Cancelled", version=5, request_id="e" * 32)],
        )
        result = self.wait(coordinator, deadline=0.25)
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertEqual(result.cancel_targets, [REQUEST_ID])
        self.assertFalse(result.guest_input_authorized)

    def test_foreign_request_owner_pid_is_never_cancelled_or_authorized(self) -> None:
        coordinator = FakeCoordinator(
            self.clock,
            [snapshot("Waiting", version=1, owner_pid=999)],
        )
        result = self.wait(coordinator, deadline=0.2)
        self.assertEqual(result.status, "RECONCILIATION_REQUIRED")
        self.assertFalse(result.guest_input_authorized)
        self.assertFalse(result.reconciliation_complete)
        self.assertEqual(coordinator.cancel_calls, [])

    def test_cli_adapter_uses_single_reservation_id_and_timeout(self) -> None:
        original_run = admission.subprocess.run
        calls: list[tuple[list[str], dict[str, Any]]] = []

        def fake_run(args: list[str], **kwargs: Any) -> Any:
            calls.append((args, kwargs))
            class Completed:
                returncode = 0
                stdout = '{"id":"' + REQUEST_ID + '","state":"Cancelled","owner":"guideX (PID 11)","ownerLiveness":"Verified","processId":null,"degraded":false}'
                stderr = ""
            return Completed()

        admission.subprocess.run = fake_run
        try:
            backend = admission.QacCliCoordinator(Path("qac.exe"))
            result = backend.cancel(REQUEST_ID, timeout=0.2)
        finally:
            admission.subprocess.run = original_run
        self.assertEqual(result.request_id, REQUEST_ID)
        self.assertEqual(result.state, "Cancelled")
        self.assertEqual(calls[0][0], ["qac.exe", "cancel", REQUEST_ID])
        self.assertEqual(calls[0][1]["timeout"], 0.2)


if __name__ == "__main__":
    unittest.main()
