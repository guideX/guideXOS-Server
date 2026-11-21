"""FONT15R4R6R2 acceptance runner.

The runner starts exactly one QAC-managed direct-QEMU command, discovers that
run's reservation by the newly created QAC owner PID, and delegates the sole
admission decision to the bounded FONT15R4R6R1 controller. Guest pointer input
is routed through fresh cursor measurement, bounded settling, and one guarded
click; this module has no unmanaged QEMU launch path.
"""

from __future__ import annotations

import json
import ntpath
import re
import subprocess
import sys
import time
import uuid
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Protocol


ROOT = Path(__file__).resolve().parents[2]
HELPER_DIR = ROOT / "out" / "font15r1-validation" / "guest"
if str(HELPER_DIR) not in sys.path:
    sys.path.insert(0, str(HELPER_DIR))

import font15r1_pointer_settling as pointer_settling  # noqa: E402
import font15r1_qac_admission as qac_admission  # noqa: E402


DEFAULT_DISCOVERY_CALL_SECONDS = 2.0
DEFAULT_POLL_INTERVAL_SECONDS = 0.5
DEFAULT_RECONCILIATION_SECONDS = 10.0
MAX_ADMISSION_SECONDS = qac_admission.MAX_ADMISSION_WINDOW_SECONDS
_RESERVATION_LINE = re.compile(r"^\s*([0-9a-fA-F]{32})\s*\|.*\(PID\s+(\d+)\)")


class QacRunHandle(Protocol):
    pid: int

    def poll(self) -> int | None: ...


class QacRequestDiscovery(Protocol):
    def find_owned_requests(self, owner_pid: int, *, timeout: float) -> list[str]: ...


class EventSink(Protocol):
    def __call__(self, event: dict[str, Any]) -> None: ...


class JsonlEventSink:
    """Append structured phase evidence without overwriting prior evidence."""

    def __init__(self, path: Path) -> None:
        self.path = path
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists():
            raise FileExistsError(f"refusing to overwrite acceptance evidence: {path}")

    def __call__(self, event: dict[str, Any]) -> None:
        with self.path.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(event, sort_keys=True, separators=(",", ":")) + "\n")


class QacCliAdapter(qac_admission.QacCliCoordinator):
    """QAC CLI adapter with bounded discovery of this exact run owner's ID."""

    def __init__(self, cli_path: Path, *, cwd: Path | None = None) -> None:
        super().__init__(cli_path)
        self.cwd = cwd

    def find_owned_requests(self, owner_pid: int, *, timeout: float) -> list[str]:
        if owner_pid <= 0:
            raise ValueError("owner_pid must be positive")
        if timeout <= 0:
            raise TimeoutError("no QAC reservation-discovery time remains")
        try:
            completed = subprocess.run(
                [str(self.cli_path), "list"],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                cwd=str(self.cwd) if self.cwd else None,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError(f"QAC list exceeded its {timeout:.3f}s call bound") from exc
        except OSError as exc:
            raise qac_admission.CoordinatorError(f"cannot execute QAC list: {exc}") from exc
        if completed.returncode != 0:
            detail = completed.stderr.strip() or completed.stdout.strip()
            raise qac_admission.CoordinatorError(
                f"QAC list failed ({completed.returncode}): {detail}"
            )
        return [
            match.group(1).lower()
            for line in completed.stdout.splitlines()
            if (match := _RESERVATION_LINE.match(line)) is not None
            and int(match.group(2)) == owner_pid
        ]


class QacRunLauncher:
    """Start QEMU only through QAC's direct-child ``run`` command."""

    def __init__(
        self,
        *,
        qac_cli: Path,
        project: str,
        mode: str,
        qemu_executable: Path,
        qemu_arguments: list[str],
        stdout_path: Path,
        stderr_path: Path,
        cwd: Path,
        popen: Callable[..., Any] = subprocess.Popen,
    ) -> None:
        if not project or not mode:
            raise ValueError("QAC project and mode are required")
        self.qac_cli = qac_cli
        self.project = project
        self.mode = mode
        self.qemu_executable = qemu_executable
        self.qemu_arguments = list(qemu_arguments)
        self.stdout_path = stdout_path
        self.stderr_path = stderr_path
        self.cwd = cwd
        self.popen = popen
        self.command = [
            str(qac_cli),
            "run",
            "--project",
            project,
            "--mode",
            mode,
            "--",
            str(qemu_executable),
            *self.qemu_arguments,
        ]

    def start(self) -> QacRunHandle:
        self.stdout_path.parent.mkdir(parents=True, exist_ok=True)
        self.stderr_path.parent.mkdir(parents=True, exist_ok=True)
        stdout = self.stdout_path.open("xb")
        try:
            stderr = self.stderr_path.open("xb")
        except BaseException:
            stdout.close()
            raise
        try:
            process = self.popen(
                self.command,
                cwd=str(self.cwd),
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                shell=False,
                creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
            )
        except BaseException:
            stdout.close()
            stderr.close()
            raise
        # The OS process owns duplicated file handles. Closing the launcher's
        # copies prevents leaks and never blocks on the long-lived QAC output.
        stdout.close()
        stderr.close()
        if int(process.pid) <= 0:
            raise RuntimeError("QAC did not provide a valid owner process identity")
        return process


@dataclass
class AdmissionGateResult:
    status: str
    owner_pid: int | None
    request_id: str | None
    deadline_monotonic: float
    finished_monotonic: float
    started_utc: str
    finished_utc: str
    guest_input_authorized: bool = False
    owner_process_must_remain_alive: bool = True
    cleanup_complete: bool = False
    detail: str | None = None
    decision: dict[str, Any] | None = None

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


class QacAcceptanceRunner:
    """The runner-level bounded admission path used by production acceptance.

    ``start_run`` must start QAC's direct-QEMU ``run`` process. The admission
    grant is decided only by ``wait_for_owned_admission`` after the exact
    reservation has been discovered for that process PID.
    """

    def __init__(
        self,
        *,
        start_run: Callable[[], QacRunHandle],
        discovery: QacRequestDiscovery,
        coordinator: qac_admission.Coordinator,
        admission_seconds: float = 180.0,
        discovery_call_seconds: float = DEFAULT_DISCOVERY_CALL_SECONDS,
        poll_interval: float = DEFAULT_POLL_INTERVAL_SECONDS,
        reconciliation_timeout: float = DEFAULT_RECONCILIATION_SECONDS,
        clock: Callable[[], float] = time.monotonic,
        sleeper: Callable[[float], None] = time.sleep,
        event_sink: EventSink | None = None,
        late_grant_cleanup: Callable[[QacRunHandle, qac_admission.AdmissionResult], bool] | None = None,
    ) -> None:
        if not 0 < admission_seconds <= MAX_ADMISSION_SECONDS:
            raise ValueError(f"admission_seconds must be in (0, {MAX_ADMISSION_SECONDS:g}]")
        if not 0 < discovery_call_seconds <= qac_admission.MAX_COORDINATOR_CALL_SECONDS:
            raise ValueError("discovery call bound is invalid")
        if not 0 < poll_interval <= qac_admission.MAX_POLL_INTERVAL_SECONDS:
            raise ValueError("poll interval is invalid")
        if not 0 < reconciliation_timeout <= qac_admission.MAX_RECONCILIATION_SECONDS:
            raise ValueError("reconciliation bound is invalid")
        self.start_run = start_run
        self.discovery = discovery
        self.coordinator = coordinator
        self.admission_seconds = float(admission_seconds)
        self.discovery_call_seconds = float(discovery_call_seconds)
        self.poll_interval = float(poll_interval)
        self.reconciliation_timeout = float(reconciliation_timeout)
        self.clock = clock
        self.sleeper = sleeper
        self.event_sink = event_sink or (lambda event: None)
        self.late_grant_cleanup = late_grant_cleanup
        self.owner: QacRunHandle | None = None
        self.result: AdmissionGateResult | None = None

    def _emit(self, kind: str, **fields: Any) -> None:
        self.event_sink(
            {
                "kind": kind,
                "monotonic": self.clock(),
                "utc": datetime.now(timezone.utc).isoformat(),
                **fields,
            }
        )

    def _finish(
        self,
        status: str,
        *,
        deadline: float,
        request_id: str | None = None,
        authorized: bool = False,
        keep_owner: bool = True,
        cleanup_complete: bool = False,
        detail: str | None = None,
        decision: qac_admission.AdmissionResult | None = None,
    ) -> AdmissionGateResult:
        result = AdmissionGateResult(
            status=status,
            owner_pid=self.owner.pid if self.owner is not None else None,
            request_id=request_id,
            deadline_monotonic=deadline,
            finished_monotonic=self.clock(),
            started_utc=self.started_utc,
            finished_utc=datetime.now(timezone.utc).isoformat(),
            guest_input_authorized=authorized,
            owner_process_must_remain_alive=keep_owner,
            cleanup_complete=cleanup_complete,
            detail=detail,
            decision=decision.as_dict() if decision is not None else None,
        )
        self.result = result
        self._emit("admission-result", **result.as_dict())
        return result

    def admit(self) -> AdmissionGateResult:
        if self.owner is not None:
            raise RuntimeError("this acceptance runner has already started its single QAC run")
        started = self.clock()
        deadline = started + self.admission_seconds
        self.started_utc = datetime.now(timezone.utc).isoformat()
        self._emit("qac-run-start", deadline_monotonic=deadline, started_utc=self.started_utc)
        try:
            self.owner = self.start_run()
        except Exception as exc:
            return self._finish(
                "QAC_RUN_START_FAILED",
                deadline=deadline,
                keep_owner=False,
                detail=str(exc),
            )
        if self.owner.pid <= 0:
            return self._finish(
                "RECONCILIATION_REQUIRED",
                deadline=deadline,
                detail="QAC run did not expose a valid owner PID",
            )
        self._emit("qac-run-owner", owner_pid=self.owner.pid)

        request_id: str | None = None
        discovery_error: str | None = None
        while self.clock() < deadline:
            remaining = deadline - self.clock()
            try:
                ids = self.discovery.find_owned_requests(
                    self.owner.pid,
                    timeout=min(self.discovery_call_seconds, remaining),
                )
                self._emit("qac-request-discovery", owner_pid=self.owner.pid, request_ids=ids)
                if len(ids) > 1:
                    return self._finish(
                        "RECONCILIATION_REQUIRED",
                        deadline=deadline,
                        detail="multiple QAC reservations matched this run owner; no reservation was selected or cancelled",
                    )
                if ids:
                    request_id = ids[0]
                    break
                discovery_error = None
            except Exception as exc:
                discovery_error = str(exc)
                self._emit("qac-request-discovery-error", owner_pid=self.owner.pid, error=discovery_error)
            if self.owner.poll() is not None:
                discovery_error = discovery_error or "QAC run exited before an owned reservation was discovered"
                break
            remaining = deadline - self.clock()
            if remaining > 0:
                self.sleeper(min(self.poll_interval, remaining))

        if request_id is None:
            # One bounded owner-specific lookup may discover a response that
            # returned after the deadline. It never resets the original limit.
            try:
                ids = self.discovery.find_owned_requests(
                    self.owner.pid,
                    timeout=min(self.reconciliation_timeout, qac_admission.MAX_COORDINATOR_CALL_SECONDS),
                )
                self._emit(
                    "qac-request-deadline-reconciliation",
                    owner_pid=self.owner.pid,
                    request_ids=ids,
                    original_deadline_monotonic=deadline,
                )
                if len(ids) == 1:
                    request_id = ids[0]
                elif len(ids) > 1:
                    return self._finish(
                        "RECONCILIATION_REQUIRED",
                        deadline=deadline,
                        detail="multiple QAC reservations matched this run owner during deadline reconciliation",
                    )
            except Exception as exc:
                discovery_error = str(exc)
                self._emit(
                    "qac-request-deadline-reconciliation-error",
                    owner_pid=self.owner.pid,
                    error=discovery_error,
                    original_deadline_monotonic=deadline,
                )
            if request_id is None:
                return self._finish(
                    "RECONCILIATION_REQUIRED",
                    deadline=deadline,
                    detail=(
                        "QAC reservation identity could not be reconciled for this owner; guest launch/input was not authorized"
                        + (f": {discovery_error}" if discovery_error else "")
                    ),
                )

        if not qac_admission.REQUEST_ID_RE.fullmatch(request_id):
            return self._finish(
                "RECONCILIATION_REQUIRED",
                deadline=deadline,
                request_id=request_id,
                detail="QAC returned a malformed reservation ID; no fallback admission path is available",
            )

        self._emit("qac-admission-wait", owner_pid=self.owner.pid, request_id=request_id)
        decision = qac_admission.wait_for_owned_admission(
            self.coordinator,
            request_id,
            expected_owner_pid=self.owner.pid,
            deadline=deadline,
            clock=self.clock,
            sleeper=self.sleeper,
            poll_interval=self.poll_interval,
            call_timeout=self.discovery_call_seconds,
            reconciliation_timeout=min(self.reconciliation_timeout, qac_admission.MAX_RECONCILIATION_SECONDS),
        )
        self._emit("qac-admission-decision", **decision.as_dict())

        if decision.status == "TIMELY_ADMISSION_RUNNING" and decision.guest_input_authorized:
            if self.owner.poll() is None:
                return self._finish(
                    "TIMELY_ADMISSION_RUNNING",
                    deadline=deadline,
                    request_id=request_id,
                    authorized=True,
                    keep_owner=True,
                    decision=decision,
                )
            # QAC's owner exited before the guest-input boundary. Treat the
            # lifecycle as ambiguous and run only the owned-child cleanup path.
            decision.status = "OWNER_EXITED_AFTER_ADMISSION"
            decision.guest_input_authorized = False
            decision.reconciliation_complete = False
            decision.owner_process_must_remain_alive = True

        if decision.status in {"LATE_GRANT_REQUIRES_CLEANUP", "OWNER_EXITED_AFTER_ADMISSION"}:
            cleaned = False
            if self.late_grant_cleanup is not None:
                try:
                    cleaned = bool(self.late_grant_cleanup(self.owner, decision))
                except Exception as exc:
                    decision.detail = f"late owned grant cleanup failed: {exc}"
            return self._finish(
                "LATE_GRANT_CLEANED_UP" if cleaned else "LATE_GRANT_CLEANUP_REQUIRED",
                deadline=deadline,
                request_id=request_id,
                authorized=False,
                keep_owner=not cleaned,
                cleanup_complete=cleaned,
                detail=decision.detail,
                decision=decision,
            )

        clean_terminal = decision.reconciliation_complete and decision.final_state in {
            "Released",
            "Cancelled",
            "Expired",
            "Failed",
        }
        return self._finish(
            decision.status,
            deadline=deadline,
            request_id=request_id,
            authorized=False,
            keep_owner=not clean_terminal,
            cleanup_complete=clean_terminal,
            detail=decision.detail,
            decision=decision,
        )


@dataclass
class SettledClickResult:
    status: str
    input_action: str = "click"
    initial_cursor: dict[str, int] | None = None
    initial_screenshot: dict[str, Any] | None = None
    settle: dict[str, Any] | None = None
    input_result: dict[str, Any] | None = None
    input_attempted: bool = False
    click_attempted: bool = False
    detail: str | None = None

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class SettledDragResult:
    status: str
    initial_cursor: dict[str, int] | None = None
    initial_screenshot: dict[str, Any] | None = None
    start_position: tuple[int, int] | None = None
    start_region: tuple[int, int, int, int] | None = None
    requested_delta: tuple[int, int] | None = None
    expected_position: tuple[int, int] | None = None
    observations: list[dict[str, Any]] = field(default_factory=list)
    input_result: dict[str, Any] | None = None
    input_attempted: bool = False
    detail: str | None = None

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


class SettledPointerInteractor:
    """Measure, settle once, and click only after live hit-region evidence."""

    def __init__(
        self,
        qmp: Any,
        *,
        cursor_template: pointer_settling.CursorTemplate,
        screenshot_directory: Path,
        event_sink: EventSink | None = None,
    ) -> None:
        self.qmp = qmp
        self.cursor_template = cursor_template
        self.screenshot_directory = screenshot_directory
        self.event_sink = event_sink or (lambda event: None)
        if not screenshot_directory.is_dir():
            raise ValueError(f"screenshot directory does not exist: {screenshot_directory}")

    def _emit(self, kind: str, **fields: Any) -> None:
        self.event_sink({"kind": kind, "monotonic": self.qmp.clock(), **fields})

    def interact(
        self,
        *,
        target_position: tuple[int, int],
        target_region: tuple[int, int, int, int],
        deadline: float,
        input_action: str = "click",
        wheel_delta: int = 0,
        max_screenshots: int = 8,
        min_interval: float = 0.25,
        tolerance: int = 1,
        delay: float = 0.2,
    ) -> SettledClickResult:
        if input_action not in {"click", "wheel"}:
            raise ValueError("input_action must be 'click' or 'wheel'")
        if input_action == "wheel" and wheel_delta == 0:
            raise ValueError("wheel_delta must be nonzero for wheel input")
        left, top, right, bottom = target_region
        target_x, target_y = target_position
        if left < 0 or top < 0 or right < left or bottom < top:
            raise ValueError("target_region must be a valid inclusive rectangle")
        if not (left <= target_x <= right and top <= target_y <= bottom):
            raise ValueError("target_position must lie within target_region")
        if self.qmp.clock() >= deadline:
            return SettledClickResult(
                "SETTLE_DEADLINE_EXPIRED",
                input_action=input_action,
                detail="deadline expired before initial measurement",
            )

        measurement_path = self.screenshot_directory / f"pointer-initial-{uuid.uuid4().hex}.ppm"
        try:
            self.qmp.verify_ownership(deadline=deadline)
            capture = self.qmp.screenshot(measurement_path, deadline=deadline)
            captured_at = self.qmp.clock()
            if (
                capture.get("fresh_capture") is not True
                or captured_at > deadline
                or float(capture.get("artifact_verified_monotonic", captured_at)) > deadline
            ):
                return SettledClickResult(
                    "INITIAL_OBSERVATION_STALE_OR_LATE",
                    input_action=input_action,
                    initial_screenshot=capture,
                    detail="initial cursor measurement was not a fresh in-deadline framebuffer",
                )
            image = pointer_settling.PpmImage.read(measurement_path)
            cursor = pointer_settling.locate_cursor(image, self.cursor_template)
        except pointer_settling.AmbiguousCursorError as exc:
            return SettledClickResult("AMBIGUOUS_INITIAL_CURSOR", input_action=input_action, detail=str(exc))
        except pointer_settling.CursorMeasurementError as exc:
            return SettledClickResult("INITIAL_CURSOR_UNAVAILABLE", input_action=input_action, detail=str(exc))
        except Exception as exc:
            return SettledClickResult("INITIAL_OBSERVATION_FAILED", input_action=input_action, detail=str(exc))

        if target_x >= image.width or target_y >= image.height:
            return SettledClickResult(
                "TARGET_OUTSIDE_FRAMEBUFFER",
                input_action=input_action,
                initial_cursor=cursor.as_dict(),
                initial_screenshot=capture,
                detail="target position is outside the observed framebuffer",
            )
        self._emit(
            "pointer-initial-measurement",
            screenshot=str(measurement_path),
            screenshot_width=image.width,
            screenshot_height=image.height,
            cursor=cursor.as_dict(),
            target_position=[target_x, target_y],
            target_region=list(target_region),
        )

        settled = pointer_settling.settle_pointer(
            self.qmp,
            initial_position=(cursor.x, cursor.y),
            dx=target_x - cursor.x,
            dy=target_y - cursor.y,
            cursor_template=self.cursor_template,
            screenshot_directory=self.screenshot_directory,
            deadline=deadline,
            max_screenshots=max_screenshots,
            min_interval=min_interval,
            tolerance=tolerance,
            delay=delay,
            target_region=target_region,
        )
        if not settled.click_authorized:
            result = SettledClickResult(
                "CLICK_BLOCKED",
                input_action=input_action,
                initial_cursor=cursor.as_dict(),
                initial_screenshot=capture,
                settle=settled.as_dict(),
                detail=settled.detail or f"pointer settling ended with {settled.status}",
            )
            self._emit("pointer-click-blocked", **result.as_dict())
            return result
        if self.qmp.clock() >= deadline:
            result = SettledClickResult(
                "CLICK_BLOCKED",
                input_action=input_action,
                initial_cursor=cursor.as_dict(),
                initial_screenshot=capture,
                settle=settled.as_dict(),
                detail="pointer settled, but the interaction deadline expired before click authorization",
            )
            self._emit("pointer-click-blocked", **result.as_dict())
            return result
        try:
            self.qmp.verify_ownership(deadline=deadline)
        except Exception as exc:
            result = SettledClickResult(
                "CLICK_BLOCKED",
                input_action=input_action,
                initial_cursor=cursor.as_dict(),
                initial_screenshot=capture,
                settle=settled.as_dict(),
                detail=f"QMP ownership could not be reconfirmed before click: {exc}",
            )
            self._emit("pointer-click-blocked", **result.as_dict())
            return result

        if input_action == "click":
            operation = self.qmp.drag("left", 0, 0, delay)
        else:
            operation = self.qmp.wheel(wheel_delta, delay)
        operation_data = asdict(operation) if hasattr(operation, "__dataclass_fields__") else dict(operation)
        success = bool(getattr(operation, "success", False))
        result = SettledClickResult(
            ("CLICK_SENT" if success else "CLICK_FAILED")
            if input_action == "click"
            else ("WHEEL_SENT" if success else "WHEEL_FAILED"),
            input_action=input_action,
            initial_cursor=cursor.as_dict(),
            initial_screenshot=capture,
            settle=settled.as_dict(),
            input_result=operation_data,
            input_attempted=True,
            click_attempted=input_action == "click",
            detail=None if success else "bounded input did not complete cleanly; do not repeat it",
        )
        self._emit(f"pointer-{input_action}-result", **result.as_dict())
        return result

    def drag(
        self,
        *,
        start_position: tuple[int, int],
        start_region: tuple[int, int, int, int],
        dx: int,
        dy: int,
        deadline: float,
        max_screenshots: int = 8,
        min_interval: float = 0.25,
        tolerance: int = 1,
        delay: float = 0.2,
    ) -> SettledDragResult:
        """Drag once from a freshly measured, already-settled pointer origin.

        The method never moves the pointer to the drag origin. A prior settled
        interaction must place it there. After the single drag, observations
        are read-only; incomplete movement is never resent.
        """
        if dx == 0 and dy == 0:
            raise ValueError("drag delta must be nonzero")
        if not 1 <= max_screenshots <= pointer_settling.MAX_SETTLE_SCREENSHOTS:
            raise ValueError("max_screenshots is outside the bounded observation range")
        if not 0 < min_interval <= pointer_settling.MAX_MINIMUM_INTERVAL_SECONDS:
            raise ValueError("min_interval is outside the bounded observation range")
        if tolerance < 0:
            raise ValueError("tolerance must be nonnegative")
        left, top, right, bottom = start_region
        start_x, start_y = start_position
        if left < 0 or top < 0 or right < left or bottom < top:
            raise ValueError("start_region must be a valid inclusive rectangle")
        if not (left <= start_x <= right and top <= start_y <= bottom):
            raise ValueError("start_position must lie within start_region")

        result = SettledDragResult(
            "DRAG_BLOCKED",
            start_position=start_position,
            start_region=start_region,
            requested_delta=(dx, dy),
        )
        if self.qmp.clock() >= deadline:
            result.detail = "deadline expired before initial drag measurement"
            return result

        measurement_path = self.screenshot_directory / f"drag-initial-{uuid.uuid4().hex}.ppm"
        try:
            self.qmp.verify_ownership(deadline=deadline)
            capture = self.qmp.screenshot(measurement_path, deadline=deadline)
            captured_at = self.qmp.clock()
            if (
                capture.get("fresh_capture") is not True
                or captured_at > deadline
                or float(capture.get("artifact_verified_monotonic", captured_at)) > deadline
            ):
                result.initial_screenshot = capture
                result.detail = "initial drag-origin observation was stale or late"
                return result
            image = pointer_settling.PpmImage.read(measurement_path)
            cursor = pointer_settling.locate_cursor(image, self.cursor_template)
        except pointer_settling.AmbiguousCursorError as exc:
            result.detail = f"ambiguous initial drag cursor: {exc}"
            return result
        except pointer_settling.CursorMeasurementError as exc:
            result.detail = f"initial drag cursor unavailable: {exc}"
            return result
        except Exception as exc:
            result.detail = f"initial drag observation failed: {exc}"
            return result

        result.initial_cursor = cursor.as_dict()
        result.initial_screenshot = capture
        if not (0 <= start_x < image.width and 0 <= start_y < image.height):
            result.detail = "drag origin lies outside the observed framebuffer"
            return result
        if not (
            left <= cursor.x <= right
            and top <= cursor.y <= bottom
            and abs(cursor.x - start_x) <= tolerance
            and abs(cursor.y - start_y) <= tolerance
        ):
            result.detail = "pointer is not at the settled drag origin; no drag was sent"
            return result

        expected = (cursor.x + dx, cursor.y + dy)
        result.expected_position = expected
        if not (0 <= expected[0] < image.width and 0 <= expected[1] < image.height):
            result.detail = "expected drag destination lies outside the observed framebuffer"
            return result
        try:
            self.qmp.verify_ownership(deadline=deadline)
        except Exception as exc:
            result.detail = f"QMP ownership could not be reconfirmed before drag: {exc}"
            return result

        result.input_attempted = True
        try:
            operation = self.qmp.drag("left", dx, dy, delay, deadline=deadline)
            result.input_result = (
                asdict(operation)
                if hasattr(operation, "__dataclass_fields__")
                else dict(operation)
            )
        except Exception as exc:
            result.status = "DRAG_FAILED"
            result.detail = f"drag outcome is uncertain; do not repeat it: {exc}"
            self._emit("pointer-drag-result", **result.as_dict())
            return result

        if not bool(getattr(operation, "success", False)):
            result.status = "DRAG_FAILED"
            result.detail = "bounded drag did not complete cleanly; do not repeat it"
            self._emit("pointer-drag-result", **result.as_dict())
            return result

        for index in range(max_screenshots):
            if self.qmp.clock() >= deadline:
                break
            observation_path = (
                self.screenshot_directory
                / f"drag-observation-{uuid.uuid4().hex}-{index + 1:02d}.ppm"
            )
            started = self.qmp.clock()
            try:
                self.qmp.verify_ownership(deadline=deadline)
                observation_capture = self.qmp.screenshot(observation_path, deadline=deadline)
                completed = self.qmp.clock()
                if (
                    observation_capture.get("fresh_capture") is not True
                    or completed > deadline
                    or float(
                        observation_capture.get("artifact_verified_monotonic", completed)
                    ) > deadline
                ):
                    result.status = "DRAG_OBSERVATION_FAILED"
                    result.detail = "post-drag screenshot was stale or late"
                    break
                observed_image = pointer_settling.PpmImage.read(observation_path)
                observed_cursor = pointer_settling.locate_cursor(
                    observed_image, self.cursor_template
                )
            except pointer_settling.AmbiguousCursorError as exc:
                result.status = "DRAG_OBSERVATION_FAILED"
                result.detail = f"post-drag cursor became ambiguous: {exc}"
                break
            except pointer_settling.CursorMeasurementError as exc:
                result.status = "DRAG_OBSERVATION_FAILED"
                result.detail = f"post-drag cursor could not be measured: {exc}"
                break
            except Exception as exc:
                result.status = "DRAG_OBSERVATION_FAILED"
                result.detail = f"post-drag observation failed: {exc}"
                break

            observation = {
                "index": index + 1,
                "started_monotonic": started,
                "completed_monotonic": completed,
                "screenshot_path": str(observation_path),
                "screenshot_fresh": True,
                "cursor": observed_cursor.as_dict(),
                "image_width": observed_image.width,
                "image_height": observed_image.height,
            }
            result.observations.append(observation)
            self._emit("pointer-drag-observation", **observation)
            if (
                abs(observed_cursor.x - expected[0]) <= tolerance
                and abs(observed_cursor.y - expected[1]) <= tolerance
            ):
                result.status = "DRAG_SETTLED"
                result.detail = None
                break

            overshot_x = (dx > 0 and observed_cursor.x > expected[0] + tolerance) or (
                dx < 0 and observed_cursor.x < expected[0] - tolerance
            )
            overshot_y = (dy > 0 and observed_cursor.y > expected[1] + tolerance) or (
                dy < 0 and observed_cursor.y < expected[1] - tolerance
            )
            if overshot_x or overshot_y:
                result.status = "DRAG_DESTINATION_UNCONFIRMED"
                result.detail = "post-drag observation overshot the expected pointer position"
                break
            if index + 1 >= max_screenshots or self.qmp.clock() >= deadline:
                result.status = "DRAG_DESTINATION_UNCONFIRMED"
                result.detail = "expected drag destination was not observed; no movement was resent"
                break
            self.qmp.sleeper(min(min_interval, max(0.0, deadline - self.qmp.clock())))

        if result.status == "DRAG_BLOCKED":
            result.status = "DRAG_DESTINATION_UNCONFIRMED"
            result.detail = result.detail or "expected drag destination was not observed"
        self._emit("pointer-drag-result", **result.as_dict())
        return result

    def click(self, **kwargs: Any) -> SettledClickResult:
        """Move once, confirm the live hit region, and send one left click."""
        return self.interact(input_action="click", **kwargs)

    def wheel(self, *, wheel_delta: int, **kwargs: Any) -> SettledClickResult:
        """Move once, confirm the live scroll region, and send one wheel event."""
        return self.interact(input_action="wheel", wheel_delta=wheel_delta, **kwargs)


class QacOwnedGrantCleanup:
    """Shut down only a late QAC child whose exact ownership was verified."""

    def __init__(
        self,
        *,
        coordinator: qac_admission.Coordinator,
        qmp_port: int,
        display_port: int,
        qmp_trace: Path,
        cleanup_seconds: float = 20.0,
        clock: Callable[[], float] = time.monotonic,
        sleeper: Callable[[float], None] = time.sleep,
        qmp_factory: Callable[[int, Path, int], Any] | None = None,
    ) -> None:
        if not 0 < cleanup_seconds <= 60:
            raise ValueError("cleanup_seconds must be in (0, 60]")
        self.coordinator = coordinator
        self.qmp_port = qmp_port
        self.display_port = display_port
        self.qmp_trace = qmp_trace
        self.cleanup_seconds = cleanup_seconds
        self.clock = clock
        self.sleeper = sleeper
        self.qmp_factory = qmp_factory

    def _endpoints_closed(self) -> bool:
        if self.qmp_factory is not None:
            return True
        from font15r1_qmp import WindowsEndpointVerifier  # noqa: PLC0415

        verifier = WindowsEndpointVerifier()
        return not verifier.listener_pids(self.qmp_port) and not verifier.listener_pids(self.display_port)

    def __call__(self, owner: QacRunHandle, decision: qac_admission.AdmissionResult) -> bool:
        if (
            decision.request_id == ""
            or decision.owner_pid != owner.pid
            or not decision.owner_verified
            or decision.degraded
        ):
            return False
        deadline = self.clock() + self.cleanup_seconds
        qmp = None
        quit_sent = False
        while self.clock() < deadline:
            try:
                snapshot = self.coordinator.inspect(
                    decision.request_id,
                    timeout=min(2.0, max(0.001, deadline - self.clock())),
                )
            except Exception:
                self.sleeper(min(0.25, max(0.0, deadline - self.clock())))
                continue
            if (
                snapshot.request_id != decision.request_id
                or snapshot.owner_pid != owner.pid
                or snapshot.degraded
            ):
                return False
            if snapshot.state == "Released":
                if owner.poll() is None:
                    self.sleeper(min(0.1, max(0.0, deadline - self.clock())))
                    continue
                return self._endpoints_closed()
            if snapshot.state in {"Cancelled", "Expired", "Failed"}:
                if owner.poll() is None:
                    self.sleeper(min(0.1, max(0.0, deadline - self.clock())))
                    continue
                return self._endpoints_closed()
            if snapshot.state == "Running":
                if (
                    not snapshot.owner_verified
                    or snapshot.child_pid is None
                    or (decision.child_pid is not None and snapshot.child_pid != decision.child_pid)
                ):
                    return False
                if not quit_sent:
                    try:
                        if self.qmp_factory is None:
                            from font15r1_qmp import QMP  # noqa: PLC0415

                            qmp = QMP(self.qmp_port, self.qmp_trace, snapshot.child_pid)
                        else:
                            qmp = self.qmp_factory(self.qmp_port, self.qmp_trace, snapshot.child_pid)
                        qmp._command("quit")
                        quit_sent = True
                    except Exception:
                        return False
                    finally:
                        if qmp is not None:
                            try:
                                qmp.close()
                            except Exception:
                                pass
                            qmp = None
            elif snapshot.state not in {"Granted", "Starting", "Recovery Required"}:
                return False
            self.sleeper(min(0.25, max(0.0, deadline - self.clock())))
        return False


class FontsGuestAcceptanceSession:
    """Production guest session facade over the qualified admission and input paths."""

    def __init__(
        self,
        admission_runner: QacAcceptanceRunner,
        *,
        qmp_port: int,
        display_port: int,
        qmp_trace: Path,
        screenshot_directory: Path,
        cursor_template: pointer_settling.CursorTemplate,
        qmp_factory: Callable[[int, Path, int], Any] | None = None,
        expected_qemu_executable: Path | None = None,
        event_sink: EventSink | None = None,
    ) -> None:
        self.admission_runner = admission_runner
        self.qmp_port = qmp_port
        self.display_port = display_port
        self.qmp_trace = qmp_trace
        self.screenshot_directory = screenshot_directory
        self.cursor_template = cursor_template
        self.qmp_factory = qmp_factory
        self.expected_qemu_executable = expected_qemu_executable
        self.event_sink = event_sink or (lambda event: None)
        self.qmp: Any | None = None
        self.pointer: SettledPointerInteractor | None = None
        self.admission: AdmissionGateResult | None = None

    def start(self) -> AdmissionGateResult:
        self.admission = self.admission_runner.admit()
        if not self.admission.guest_input_authorized:
            return self.admission
        decision = self.admission.decision or {}
        child_pid = decision.get("child_pid")
        child_executable = decision.get("child_executable")
        child_started_at_utc = decision.get("child_started_at_utc")
        expected_executable = (
            ntpath.normcase(ntpath.normpath(str(self.expected_qemu_executable)))
            if self.expected_qemu_executable is not None
            else None
        )
        actual_executable = (
            ntpath.normcase(ntpath.normpath(str(child_executable))) if child_executable else None
        )
        if (
            not decision.get("owner_verified")
            or decision.get("owner_pid") != self.admission.owner_pid
            or not isinstance(child_pid, int)
            or child_pid <= 0
            or not actual_executable
            or ntpath.basename(actual_executable) != "qemu-system-x86_64.exe"
            or not child_started_at_utc
            or (expected_executable is not None and actual_executable != expected_executable)
            or decision.get("degraded")
        ):
            self.admission.guest_input_authorized = False
            self.admission.status = "RECONCILIATION_REQUIRED"
            self.admission.detail = "verified QAC owner and direct-child identity were not intact at QMP construction"
            return self.admission
        try:
            if self.qmp_factory is None:
                from font15r1_qmp import QMP  # noqa: PLC0415

                self.qmp = QMP(self.qmp_port, self.qmp_trace, child_pid)
            else:
                self.qmp = self.qmp_factory(self.qmp_port, self.qmp_trace, child_pid)
        except Exception as exc:
            self.admission.guest_input_authorized = False
            self.admission.status = "QMP_OWNERSHIP_UNVERIFIED"
            self.admission.detail = f"QMP endpoint ownership check failed: {exc}"
            return self.admission
        self.pointer = SettledPointerInteractor(
            self.qmp,
            cursor_template=self.cursor_template,
            screenshot_directory=self.screenshot_directory,
            event_sink=self.event_sink,
        )
        identity = getattr(self.qmp, "_identity", None)
        self.event_sink(
            {
                "kind": "qmp-endpoint-ownership-verified",
                "utc": datetime.now(timezone.utc).isoformat(),
                "qmp_port": self.qmp_port,
                "qac_child_pid": child_pid,
                "qac_child_executable": child_executable,
                "qac_child_started_at_utc": child_started_at_utc,
                "identity": asdict(identity) if identity is not None and hasattr(identity, "__dataclass_fields__") else None,
            }
        )
        return self.admission

    def click_control(self, **kwargs: Any) -> SettledClickResult:
        if not (self.admission and self.admission.guest_input_authorized and self.pointer):
            return SettledClickResult(
                "GUEST_INPUT_NOT_AUTHORIZED",
                detail="QAC admission and QMP endpoint ownership must pass before guest input",
            )
        return self.pointer.click(**kwargs)

    def drag_control(self, **kwargs: Any) -> SettledDragResult:
        if not (self.admission and self.admission.guest_input_authorized and self.pointer):
            return SettledDragResult(
                "GUEST_INPUT_NOT_AUTHORIZED",
                detail="QAC admission and QMP endpoint ownership must pass before guest input",
            )
        return self.pointer.drag(**kwargs)

    def wheel_control(self, *, wheel_delta: int, **kwargs: Any) -> SettledClickResult:
        if not (self.admission and self.admission.guest_input_authorized and self.pointer):
            return SettledClickResult(
                "GUEST_INPUT_NOT_AUTHORIZED",
                input_action="wheel",
                detail="QAC admission and QMP endpoint ownership must pass before guest input",
            )
        return self.pointer.wheel(wheel_delta=wheel_delta, **kwargs)

    def shutdown(self, *, timeout_seconds: float = 30.0) -> dict[str, Any]:
        """Quit the verified child through QMP and verify QAC's own release."""
        if not 0 < timeout_seconds <= 120:
            raise ValueError("timeout_seconds must be in (0, 120]")
        if not self.admission or not self.admission.request_id or not self.admission.owner_pid:
            return {"status": "RECONCILIATION_REQUIRED", "detail": "no admitted QAC owner is available"}
        if self.qmp is None:
            return {"status": "RECONCILIATION_REQUIRED", "detail": "verified QMP session is unavailable"}
        verifier = getattr(self.qmp, "verifier", None)
        qmp_quit_error: str | None = None
        try:
            self.qmp.verify_ownership()
            self.qmp._command("quit")
        except Exception as exc:
            qmp_quit_error = str(exc)
        finally:
            try:
                self.qmp.close()
            except Exception:
                pass
            self.qmp = None

        owner = self.admission_runner.owner
        if owner is None:
            return {"status": "RECONCILIATION_REQUIRED", "detail": "QAC run owner handle is missing"}
        deadline = time.monotonic() + timeout_seconds
        while owner.poll() is None and time.monotonic() < deadline:
            time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))
        if owner.poll() is None:
            return {
                "status": "RECONCILIATION_REQUIRED",
                "request_id": self.admission.request_id,
                "owner_pid": self.admission.owner_pid,
                "detail": (
                    "QAC run owner did not exit within the shutdown bound; reservation remains owned"
                    + (f"; QMP quit error: {qmp_quit_error}" if qmp_quit_error else "")
                ),
            }
        try:
            snapshot = self.admission_runner.coordinator.inspect(
                self.admission.request_id,
                timeout=min(5.0, timeout_seconds),
            )
        except Exception as exc:
            return {
                "status": "RECONCILIATION_REQUIRED",
                "request_id": self.admission.request_id,
                "owner_pid": self.admission.owner_pid,
                "owner_exit_code": owner.poll(),
                "detail": f"QAC release inspection failed: {exc}",
            }
        released = (
            snapshot.request_id == self.admission.request_id
            and snapshot.owner_pid == self.admission.owner_pid
            and snapshot.state == "Released"
            and owner.poll() is not None
        )
        qmp_listeners: list[int] | None = None
        display_listeners: list[int] | None = None
        if released and verifier is not None and hasattr(verifier, "listener_pids"):
            try:
                qmp_listeners = verifier.listener_pids(self.qmp_port)
                display_listeners = verifier.listener_pids(self.display_port)
                if qmp_listeners or display_listeners:
                    released = False
            except Exception:
                released = False
        elif released:
            released = False
        return {
            "status": "QAC_RELEASE_VERIFIED" if released else "RECONCILIATION_REQUIRED",
            "request_id": self.admission.request_id,
            "owner_pid": self.admission.owner_pid,
            "owner_exit_code": owner.poll(),
            "reservation_state": snapshot.state,
            "child_pid": snapshot.child_pid,
            "qmp_client_socket_closed": True,
            "qmp_listener_pids_after_release": qmp_listeners,
            "display_listener_pids_after_release": display_listeners,
            "qmp_endpoint_closed": qmp_listeners == [],
            "display_endpoint_closed": display_listeners == [],
            "qmp_quit_error": qmp_quit_error,
        }


def start_qac_managed_run(
    *,
    qac_cli: Path,
    project: str,
    qemu_executable: Path,
    qemu_arguments: list[str],
    qmp_port: int,
    display_port: int,
    evidence_directory: Path,
    cwd: Path = ROOT,
    admission_seconds: float = 180.0,
    event_sink: EventSink | None = None,
    clock: Callable[[], float] = time.monotonic,
    sleeper: Callable[[float], None] = time.sleep,
    late_grant_cleanup: Callable[[QacRunHandle, qac_admission.AdmissionResult], bool] | None = None,
) -> QacAcceptanceRunner:
    """Production construction path used for an acceptance candidate run."""
    evidence_directory.mkdir(parents=True, exist_ok=True)
    late_cleanup_trace = evidence_directory / "qmp-transcript-late-cleanup.jsonl"
    if late_cleanup_trace.exists():
        raise FileExistsError(f"refusing to append to prior cleanup evidence: {late_cleanup_trace}")
    launcher = QacRunLauncher(
        qac_cli=qac_cli,
        project=project,
        mode="acceptance",
        qemu_executable=qemu_executable,
        qemu_arguments=qemu_arguments,
        stdout_path=evidence_directory / "qac-run.stdout.txt",
        stderr_path=evidence_directory / "qac-run.stderr.txt",
        cwd=cwd,
    )
    adapter = QacCliAdapter(qac_cli, cwd=cwd)
    cleanup = late_grant_cleanup or QacOwnedGrantCleanup(
        coordinator=adapter,
        qmp_port=qmp_port,
        display_port=display_port,
        qmp_trace=late_cleanup_trace,
        clock=clock,
        sleeper=sleeper,
    )
    runner = QacAcceptanceRunner(
        start_run=launcher.start,
        discovery=adapter,
        coordinator=adapter,
        admission_seconds=admission_seconds,
        clock=clock,
        sleeper=sleeper,
        event_sink=event_sink,
        late_grant_cleanup=cleanup,
    )
    return runner


def _session_from_plan(plan_path: Path) -> FontsGuestAcceptanceSession:
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    required = {
        "qacCli",
        "project",
        "qemuExecutable",
        "qemuArguments",
        "qmpPort",
        "displayPort",
        "qmpTrace",
        "cursorSource",
        "evidenceDirectory",
        "screenshotDirectory",
        "eventsPath",
    }
    missing = sorted(required - set(plan))
    if missing:
        raise ValueError(f"acceptance plan is missing required fields: {', '.join(missing)}")
    evidence_directory = Path(plan["evidenceDirectory"])
    event_sink = JsonlEventSink(Path(plan["eventsPath"]))
    qac_cli = Path(plan["qacCli"])
    qemu_executable = Path(plan["qemuExecutable"])
    qemu_arguments = [str(argument) for argument in plan["qemuArguments"]]
    qmp_port = int(plan["qmpPort"])
    qmp_trace = Path(plan["qmpTrace"])
    if qmp_trace.exists():
        raise FileExistsError(f"refusing to append to prior QMP evidence: {qmp_trace}")
    screenshot_directory = Path(plan["screenshotDirectory"])
    screenshot_directory.mkdir(parents=True, exist_ok=True)
    cwd = Path(plan.get("workingDirectory", ROOT))
    admission_runner = start_qac_managed_run(
        qac_cli=qac_cli,
        project=str(plan["project"]),
        qemu_executable=qemu_executable,
        qemu_arguments=qemu_arguments,
        qmp_port=qmp_port,
        display_port=int(plan["displayPort"]),
        evidence_directory=evidence_directory,
        cwd=cwd,
        admission_seconds=float(plan.get("admissionSeconds", 180.0)),
        event_sink=event_sink,
    )
    template = pointer_settling.CursorTemplate.from_cpp_source(Path(plan["cursorSource"]))
    return FontsGuestAcceptanceSession(
        admission_runner,
        qmp_port=qmp_port,
        display_port=int(plan["displayPort"]),
        qmp_trace=qmp_trace,
        screenshot_directory=screenshot_directory,
        cursor_template=template,
        expected_qemu_executable=qemu_executable,
        event_sink=event_sink,
    )


def _emit_json(value: dict[str, Any]) -> None:
    print(json.dumps(value, sort_keys=True, separators=(",", ":")), flush=True)


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", type=Path, help="candidate and isolated QEMU endpoint plan JSON")
    args = parser.parse_args(argv)
    session: FontsGuestAcceptanceSession | None = None
    try:
        session = _session_from_plan(args.plan)
        admission = session.start()
    except Exception as exc:
        _emit_json({"status": "RUNNER_START_FAILED", "detail": str(exc)})
        return 2
    _emit_json({"kind": "admission", **admission.as_dict()})
    if not admission.guest_input_authorized:
        return 2

    try:
        for line in sys.stdin:
            try:
                command = json.loads(line)
                if not isinstance(command, dict):
                    raise ValueError("command must be a JSON object")
                action = command.get("action")
                if action == "click":
                    result = session.click_control(
                        target_position=tuple(map(int, command["targetPosition"])),
                        target_region=tuple(map(int, command["targetRegion"])),
                        deadline=session.qmp.clock() + float(command.get("timeoutSeconds", 8.0)),
                    )
                    _emit_json({"kind": "interaction", **result.as_dict()})
                elif action == "wheel":
                    result = session.wheel_control(
                        wheel_delta=int(command["delta"]),
                        target_position=tuple(map(int, command["targetPosition"])),
                        target_region=tuple(map(int, command["targetRegion"])),
                        deadline=session.qmp.clock() + float(command.get("timeoutSeconds", 8.0)),
                    )
                    _emit_json({"kind": "interaction", **result.as_dict()})
                elif action == "drag":
                    delta = tuple(map(int, command["delta"]))
                    if len(delta) != 2:
                        raise ValueError("delta must contain exactly two integers")
                    timeout_seconds = float(command.get("timeoutSeconds", 12.0))
                    if not 0 < timeout_seconds <= 30:
                        raise ValueError("drag timeoutSeconds must be in (0, 30]")
                    result = session.drag_control(
                        start_position=tuple(map(int, command["startPosition"])),
                        start_region=tuple(map(int, command["startRegion"])),
                        dx=delta[0],
                        dy=delta[1],
                        deadline=session.qmp.clock() + timeout_seconds,
                    )
                    _emit_json({"kind": "drag", **result.as_dict()})
                elif action == "capture":
                    destination = session.screenshot_directory / f"acceptance-frame-{uuid.uuid4().hex}.ppm"
                    capture = session.qmp.screenshot(destination)
                    _emit_json({"kind": "capture", **capture})
                elif action == "key":
                    session.qmp.key(str(command["qcode"]))
                    _emit_json({"kind": "key", "status": "SENT", "qcode": str(command["qcode"])})
                elif action == "query":
                    response = session.qmp._command("query-status")
                    _emit_json({"kind": "query", "response": response})
                elif action == "shutdown":
                    result = session.shutdown(timeout_seconds=float(command.get("timeoutSeconds", 30.0)))
                    _emit_json({"kind": "shutdown", **result})
                    return 0 if result["status"] == "QAC_RELEASE_VERIFIED" else 3
                else:
                    raise ValueError("action must be click, wheel, drag, capture, key, query, or shutdown")
            except Exception as exc:
                _emit_json({"kind": "command-error", "detail": str(exc)})
    finally:
        # EOF is treated as an orderly request to stop only this verified child.
        if session.qmp is not None:
            result = session.shutdown()
            _emit_json({"kind": "shutdown", **result})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
