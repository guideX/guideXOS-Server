"""Monotonic admission polling and owned-request reconciliation for Fonts runs.

The QAC CLI's ``run`` process owns guest launch. This module monitors one
request ID independently, never waits on that long-lived process' output, and
never cancels a reservation other than the request ID supplied by the caller.
"""

from __future__ import annotations

import json
import re
import subprocess
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Callable, Protocol


REQUEST_ID_RE = re.compile(r"^[0-9a-fA-F]{32}$")
PENDING_STATES = {"Waiting", "Blocked"}
LIVE_STATES = {"Granted", "Starting", "Running", "Recovery Required"}
TERMINAL_STATES = {"Released", "Cancelled", "Expired", "Failed"}
KNOWN_STATES = PENDING_STATES | LIVE_STATES | TERMINAL_STATES
MAX_ADMISSION_WINDOW_SECONDS = 1800.0
MAX_POLL_INTERVAL_SECONDS = 5.0
MAX_COORDINATOR_CALL_SECONDS = 5.0
MAX_RECONCILIATION_SECONDS = 15.0


class CoordinatorError(RuntimeError):
    """A QAC command failed or returned an unusable response."""


class Coordinator(Protocol):
    def inspect(self, request_id: str, *, timeout: float) -> "ReservationSnapshot": ...
    def cancel(self, request_id: str, *, timeout: float) -> "ReservationSnapshot": ...


@dataclass(frozen=True)
class ReservationSnapshot:
    request_id: str
    state: str
    owner_pid: int | None = None
    owner_verified: bool = False
    child_pid: int | None = None
    degraded: bool = False
    version: int | None = None
    detail: str | None = None


@dataclass
class AdmissionResult:
    status: str
    request_id: str
    final_state: str | None = None
    owner_pid: int | None = None
    owner_verified: bool = False
    child_pid: int | None = None
    degraded: bool | None = None
    guest_input_authorized: bool = False
    owner_process_must_remain_alive: bool = True
    reconciliation_complete: bool = False
    deadline_monotonic: float = 0.0
    finished_monotonic: float = 0.0
    query_count: int = 0
    cancel_attempted: bool = False
    cancel_targets: list[str] = field(default_factory=list)
    late_returns: list[dict[str, Any]] = field(default_factory=list)
    transitions: list[dict[str, Any]] = field(default_factory=list)
    detail: str | None = None

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


class QacCliCoordinator:
    """Small bounded adapter for the QAC CLI's inspect/cancel JSON commands."""

    def __init__(self, cli_path: Path) -> None:
        self.cli_path = cli_path

    def _command(self, action: str, request_id: str, timeout: float) -> ReservationSnapshot:
        if not REQUEST_ID_RE.fullmatch(request_id):
            raise ValueError("request_id must be the exact 32-character QAC reservation ID")
        if timeout <= 0:
            raise TimeoutError("no coordinator-call time remains")
        try:
            completed = subprocess.run(
                [str(self.cli_path), action, request_id],
                capture_output=True,
                text=True,
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError(f"QAC {action} exceeded its {timeout:.3f}s call bound") from exc
        except OSError as exc:
            raise CoordinatorError(f"cannot execute QAC {action}: {exc}") from exc
        if completed.returncode != 0:
            detail = completed.stderr.strip() or completed.stdout.strip()
            raise CoordinatorError(f"QAC {action} failed ({completed.returncode}): {detail}")
        try:
            value = json.loads(completed.stdout)
            if not isinstance(value, dict):
                raise ValueError("response is not an object")
            returned_id = str(value.get("id", ""))
            state = str(value.get("state", ""))
            owner = str(value.get("owner", ""))
            pid_match = re.search(r"\(PID\s+(\d+)\)", owner)
            process_id = value.get("processId")
            return ReservationSnapshot(
                request_id=returned_id,
                state=state,
                owner_pid=int(pid_match.group(1)) if pid_match else None,
                owner_verified=value.get("ownerLiveness") == "Verified",
                child_pid=int(process_id) if process_id is not None else None,
                degraded=bool(value.get("degraded", False)),
                detail=value.get("reason"),
            )
        except (TypeError, ValueError, json.JSONDecodeError) as exc:
            raise CoordinatorError(f"QAC {action} returned invalid reservation JSON: {exc}") from exc

    def inspect(self, request_id: str, *, timeout: float) -> ReservationSnapshot:
        return self._command("inspect", request_id, timeout)

    def cancel(self, request_id: str, *, timeout: float) -> ReservationSnapshot:
        return self._command("cancel", request_id, timeout)


def _coerce_snapshot(snapshot: ReservationSnapshot | dict[str, Any]) -> ReservationSnapshot:
    if isinstance(snapshot, ReservationSnapshot):
        return snapshot
    return ReservationSnapshot(
        request_id=str(snapshot.get("request_id", snapshot.get("id", ""))),
        state=str(snapshot.get("state", "")),
        owner_pid=snapshot.get("owner_pid", snapshot.get("ownerPid")),
        owner_verified=bool(snapshot.get("owner_verified", snapshot.get("ownerVerified", False))),
        child_pid=snapshot.get("child_pid", snapshot.get("childPid", snapshot.get("processId"))),
        degraded=bool(snapshot.get("degraded", False)),
        version=snapshot.get("version"),
        detail=snapshot.get("detail", snapshot.get("reason")),
    )


def wait_for_owned_admission(
    coordinator: Coordinator,
    request_id: str,
    *,
    expected_owner_pid: int,
    deadline: float,
    clock: Callable[[], float] = time.monotonic,
    sleeper: Callable[[float], None] = time.sleep,
    poll_interval: float = 1.0,
    call_timeout: float = 2.0,
    reconciliation_timeout: float = 5.0,
) -> AdmissionResult:
    """Wait on one QAC request and reconcile only that request at the deadline.

    Timely authorization requires a fresh ``Running`` snapshot with the exact
    request ID, verified owner liveness, a registered child PID, and no degraded
    reservation. A late grant is recorded but never authorizes guest input.
    """
    if not REQUEST_ID_RE.fullmatch(request_id):
        raise ValueError("request_id must be the exact 32-character QAC reservation ID")
    if expected_owner_pid <= 0:
        raise ValueError("expected_owner_pid must identify this phase's QAC run client")
    if deadline <= clock():
        raise ValueError("deadline must be a future monotonic timestamp")
    if deadline - clock() > MAX_ADMISSION_WINDOW_SECONDS:
        raise ValueError(f"admission window must be no more than {MAX_ADMISSION_WINDOW_SECONDS:g} seconds")
    if (
        poll_interval <= 0
        or poll_interval > MAX_POLL_INTERVAL_SECONDS
        or call_timeout <= 0
        or call_timeout > MAX_COORDINATOR_CALL_SECONDS
        or reconciliation_timeout <= 0
        or reconciliation_timeout > MAX_RECONCILIATION_SECONDS
    ):
        raise ValueError("poll and QAC call/reconciliation bounds must be positive")

    result = AdmissionResult(
        status="QAC_FAILURE",
        request_id=request_id,
        deadline_monotonic=deadline,
        finished_monotonic=clock(),
    )
    last_snapshot: ReservationSnapshot | None = None
    last_version: int | None = None
    grant_seen = False
    late_grant_seen = False

    def apply(snapshot_value: ReservationSnapshot | dict[str, Any], *, at: float, late: bool) -> ReservationSnapshot | None:
        nonlocal last_snapshot, last_version, grant_seen, late_grant_seen
        snapshot = _coerce_snapshot(snapshot_value)
        result.query_count += 1
        result.transitions.append(
            {
                "observed_monotonic": at,
                "request_id": snapshot.request_id,
                "state": snapshot.state,
                "version": snapshot.version,
                "late_return": late,
            }
        )
        if late:
            result.late_returns.append(
                {"observed_monotonic": at, "state": snapshot.state, "version": snapshot.version}
            )
        if snapshot.request_id != request_id:
            result.detail = "coordinator response did not match this caller's reservation ID"
            return None
        if snapshot.owner_pid != expected_owner_pid:
            result.detail = "coordinator response owner PID did not match this caller's QAC run client"
            return None
        if snapshot.state not in KNOWN_STATES:
            result.detail = f"coordinator reported an ambiguous state {snapshot.state!r}"
            return None
        if last_version is not None and snapshot.version is not None and snapshot.version < last_version:
            result.transitions[-1]["stale"] = True
            return last_snapshot
        if last_version is not None and snapshot.version is not None and snapshot.version == last_version:
            if last_snapshot is not None and snapshot.state != last_snapshot.state:
                result.detail = "same-version coordinator responses contradict one another"
                return None
            result.transitions[-1]["duplicate"] = True
            return last_snapshot
        if last_snapshot is not None:
            if last_snapshot.state in LIVE_STATES and snapshot.state in PENDING_STATES:
                result.transitions[-1]["stale"] = True
                return last_snapshot
            if last_snapshot.state in TERMINAL_STATES and snapshot.state != last_snapshot.state:
                result.detail = "coordinator returned a contradictory state after a terminal result"
                return None
        if snapshot.state in LIVE_STATES:
            grant_seen = True
            late_grant_seen = late_grant_seen or late or clock() > deadline
        last_snapshot = snapshot
        if snapshot.version is not None:
            last_version = snapshot.version
        return snapshot

    def update_final(snapshot: ReservationSnapshot | None) -> None:
        if snapshot is not None:
            result.final_state = snapshot.state
            result.owner_pid = snapshot.owner_pid
            result.owner_verified = snapshot.owner_verified
            result.child_pid = snapshot.child_pid
            result.degraded = snapshot.degraded

    def finish(status: str, *, reconciled: bool, guest_input: bool = False, detail: str | None = None) -> AdmissionResult:
        update_final(last_snapshot)
        result.status = status
        result.guest_input_authorized = guest_input
        result.reconciliation_complete = reconciled
        result.owner_process_must_remain_alive = not reconciled or status in {
            "TIMELY_ADMISSION_RUNNING",
            "LATE_GRANT_REQUIRES_CLEANUP",
        }
        result.finished_monotonic = clock()
        if detail:
            result.detail = detail
        return result

    def timely_running(snapshot: ReservationSnapshot) -> bool:
        return (
            snapshot.state == "Running"
            and snapshot.request_id == request_id
            and snapshot.owner_verified
            and snapshot.owner_pid is not None
            and snapshot.child_pid is not None
            and not snapshot.degraded
            and clock() <= deadline
        )

    def query(timeout: float) -> ReservationSnapshot:
        return coordinator.inspect(request_id, timeout=timeout)

    def reconcile_at_deadline(first: ReservationSnapshot | None, first_error: Exception | None = None) -> AdmissionResult:
        reconcile_deadline = clock() + reconciliation_timeout
        current: ReservationSnapshot | None = first
        if current is None:
            if grant_seen and last_snapshot is not None:
                current = last_snapshot
            else:
                try:
                    before = clock()
                    current = apply(
                        query(min(call_timeout, max(0.001, reconcile_deadline - clock()))),
                        at=clock(),
                        late=clock() > deadline,
                    )
                    if current is None:
                        return finish("RECONCILIATION_REQUIRED", reconciled=False, detail=result.detail)
                    if clock() > reconcile_deadline:
                        result.late_returns.append({"phase": "deadline-reconcile-inspect", "started_monotonic": before, "returned_monotonic": clock()})
                        return finish("RECONCILIATION_REQUIRED", reconciled=False, detail="final QAC inspection returned after the reconciliation bound")
                except Exception as exc:
                    result.late_returns.append({"phase": "deadline-reconcile-inspect", "error": str(exc), "returned_monotonic": clock()})
                    if grant_seen and last_snapshot is not None:
                        current = last_snapshot
                    else:
                        return finish("RECONCILIATION_REQUIRED", reconciled=False, detail=str(exc))
        if current.state in LIVE_STATES or late_grant_seen:
            return finish(
                "LATE_GRANT_REQUIRES_CLEANUP",
                reconciled=False,
                guest_input=False,
                detail="this request acquired or may have acquired a grant after the caller deadline; no guest input is authorized",
            )
        if current.state in TERMINAL_STATES:
            if current.state == "Cancelled":
                return finish("CANCELLED_BEFORE_ADMISSION", reconciled=True, guest_input=False)
            return finish("TERMINAL_WITHOUT_ADMISSION", reconciled=True, guest_input=False)
        if current.state not in PENDING_STATES:
            return finish("RECONCILIATION_REQUIRED", reconciled=False, detail=result.detail or "reservation state is not safely cancellable")

        result.cancel_attempted = True
        result.cancel_targets.append(request_id)
        try:
            cancel_started = clock()
            cancelled = _coerce_snapshot(
                coordinator.cancel(request_id, timeout=min(call_timeout, max(0.001, reconcile_deadline - clock())))
            )
            cancel_returned = clock()
            if cancel_returned > reconcile_deadline:
                result.late_returns.append({"phase": "cancel", "started_monotonic": cancel_started, "returned_monotonic": cancel_returned, "state": cancelled.state})
            current = apply(cancelled, at=cancel_returned, late=cancel_returned > deadline) or current
        except Exception as exc:
            result.late_returns.append({"phase": "cancel", "error": str(exc), "returned_monotonic": clock()})

        # Cancellation and grant serialize in QAC. Always inspect this exact ID
        # after a cancel attempt; a rejected cancel may mean the request won.
        try:
            final_started = clock()
            final = apply(
                query(min(call_timeout, max(0.001, reconcile_deadline - clock()))),
                at=clock(),
                late=clock() > deadline,
            )
            if clock() > reconcile_deadline:
                result.late_returns.append({"phase": "post-cancel-inspect", "started_monotonic": final_started, "returned_monotonic": clock()})
                return finish("RECONCILIATION_REQUIRED", reconciled=False, detail="post-cancel inspection returned after the reconciliation bound")
            if final is None:
                return finish("RECONCILIATION_REQUIRED", reconciled=False, detail=result.detail)
            current = final
        except Exception as exc:
            return finish("RECONCILIATION_REQUIRED", reconciled=False, detail=f"post-cancel inspection failed: {exc}")

        if current.state == "Cancelled":
            return finish("CANCELLED_BEFORE_ADMISSION", reconciled=True, guest_input=False)
        if current.state in LIVE_STATES or late_grant_seen:
            return finish("LATE_GRANT_REQUIRES_CLEANUP", reconciled=False, guest_input=False,
                          detail="cancellation raced with a grant; the owned run must be reconciled before exit")
        return finish(
            "RECONCILIATION_REQUIRED",
            reconciled=False,
            guest_input=False,
            detail=first_error and str(first_error) or "QAC did not confirm cancellation or a terminal state",
        )

    while clock() < deadline:
        remaining = deadline - clock()
        if remaining <= 0:
            break
        started = clock()
        try:
            snapshot = query(min(call_timeout, remaining))
            returned = clock()
            snapshot = apply(snapshot, at=returned, late=returned > deadline)
            if returned > deadline:
                return reconcile_at_deadline(snapshot)
            if snapshot is None:
                return reconcile_at_deadline(None, CoordinatorError(result.detail or "ambiguous QAC response"))
            if timely_running(snapshot):
                return finish("TIMELY_ADMISSION_RUNNING", reconciled=True, guest_input=True)
            if snapshot.state in TERMINAL_STATES:
                return finish("TERMINAL_WITHOUT_ADMISSION", reconciled=True, guest_input=False)
            if snapshot.state in LIVE_STATES and clock() > deadline:
                return reconcile_at_deadline(snapshot)
        except Exception as exc:
            result.transitions.append(
                {"query_started_monotonic": started, "returned_monotonic": clock(), "error": str(exc)}
            )
            if clock() >= deadline:
                return reconcile_at_deadline(None, exc)
        remaining = deadline - clock()
        if remaining > 0:
            sleeper(min(poll_interval, remaining))

    return reconcile_at_deadline(None)
