"""Paced, individually acknowledged QMP input for the FONT15 guest helpers.

Input actions require the PID of the QAC-owned QEMU child. The helper verifies
that the loopback listener and the established QMP connection belong to that
same QEMU process before sending input.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import socket
import struct
import sys
import threading
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


QMP_HOST = "127.0.0.1"
MAX_RELATIVE_STEP = 35
MAX_RELATIVE_EVENTS = 128
MAX_DELAY_SECONDS = 5.0
MAX_DRAG_SECONDS = 60.0
DEFAULT_CLEANUP_SECONDS = 3.0
DEFAULT_COMMAND_SECONDS = 5.0
MAX_QMP_LINE_BYTES = 1024 * 1024


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


class QMPError(RuntimeError):
    """Base class for classified QMP helper failures."""


class EndpointIdentityError(QMPError):
    """The QMP endpoint could not be tied to the expected QEMU process."""


class QMPConnectionLost(QMPError):
    """The connected QMP transport closed or failed during I/O."""


class QMPProtocolError(QMPError):
    """The connected endpoint returned malformed or uncorrelatable data."""


class QMPTimeout(QMPError):
    """A bounded QMP command or operation deadline expired."""


class QMPCommandError(QMPError):
    def __init__(self, execute: str, response: dict[str, Any]) -> None:
        self.execute = execute
        self.response = response
        super().__init__(f"QMP {execute} failed: {response.get('error')}")


@dataclass(frozen=True)
class ProcessIdentity:
    pid: int
    executable: str
    creation_date: str


class WindowsEndpointVerifier:
    """Verify loopback QMP TCP ownership using Windows' TCP/process tables."""

    AF_INET = 2
    TCP_TABLE_OWNER_PID_LISTENER = 3
    TCP_TABLE_OWNER_PID_ALL = 5
    MIB_TCP_STATE_LISTEN = 2
    MIB_TCP_STATE_ESTABLISHED = 5
    ERROR_INSUFFICIENT_BUFFER = 122
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

    class _TcpRowOwnerPid(ctypes.Structure):
        _fields_ = [
            ("state", ctypes.c_ulong),
            ("local_address", ctypes.c_ulong),
            ("local_port", ctypes.c_ulong),
            ("remote_address", ctypes.c_ulong),
            ("remote_port", ctypes.c_ulong),
            ("owning_pid", ctypes.c_ulong),
        ]

    class _FileTime(ctypes.Structure):
        _fields_ = [("low", ctypes.c_ulong), ("high", ctypes.c_ulong)]

    @classmethod
    def _tcp_rows(cls, table_class: int) -> list[Any]:
        if sys.platform != "win32":
            raise EndpointIdentityError("QMP process ownership verification requires Windows")
        try:
            iphlpapi = ctypes.WinDLL("iphlpapi.dll", use_last_error=True)
            get_table = iphlpapi.GetExtendedTcpTable
            get_table.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_ulong),
                ctypes.c_int,
                ctypes.c_ulong,
                ctypes.c_int,
                ctypes.c_ulong,
            ]
            get_table.restype = ctypes.c_ulong
            size = ctypes.c_ulong(0)
            result = get_table(
                None,
                ctypes.byref(size),
                0,
                cls.AF_INET,
                table_class,
                0,
            )
            if result != cls.ERROR_INSUFFICIENT_BUFFER:
                raise EndpointIdentityError(f"GetExtendedTcpTable size query failed with {result}")
            buffer = ctypes.create_string_buffer(size.value)
            result = get_table(
                buffer,
                ctypes.byref(size),
                0,
                cls.AF_INET,
                table_class,
                0,
            )
            if result != 0:
                raise EndpointIdentityError(f"GetExtendedTcpTable failed with {result}")
            if size.value < ctypes.sizeof(ctypes.c_ulong):
                raise EndpointIdentityError("GetExtendedTcpTable returned a truncated table")
            count = ctypes.c_ulong.from_buffer_copy(buffer.raw[: ctypes.sizeof(ctypes.c_ulong)]).value
            row_size = ctypes.sizeof(cls._TcpRowOwnerPid)
            required_size = ctypes.sizeof(ctypes.c_ulong) + count * row_size
            if required_size > size.value:
                raise EndpointIdentityError("GetExtendedTcpTable returned an invalid row count")
            rows: list[Any] = []
            offset = ctypes.sizeof(ctypes.c_ulong)
            for index in range(count):
                start = offset + index * row_size
                end = start + row_size
                rows.append(cls._TcpRowOwnerPid.from_buffer_copy(buffer.raw[start:end]))
            return rows
        except AttributeError as exc:
            raise EndpointIdentityError("Windows TCP ownership APIs are unavailable") from exc
        except OSError as exc:
            raise EndpointIdentityError(f"cannot inspect QMP TCP ownership: {exc}") from exc

    @classmethod
    def _process_identity(cls, pid: int) -> ProcessIdentity:
        if sys.platform != "win32":
            raise EndpointIdentityError("QMP process ownership verification requires Windows")
        kernel32 = ctypes.WinDLL("kernel32.dll", use_last_error=True)
        open_process = kernel32.OpenProcess
        open_process.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        open_process.restype = ctypes.c_void_p
        query_image = kernel32.QueryFullProcessImageNameW
        query_image.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
        query_image.restype = ctypes.c_int
        get_times = kernel32.GetProcessTimes
        get_times.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(cls._FileTime),
            ctypes.POINTER(cls._FileTime),
            ctypes.POINTER(cls._FileTime),
            ctypes.POINTER(cls._FileTime),
        ]
        get_times.restype = ctypes.c_int
        close_handle = kernel32.CloseHandle
        close_handle.argtypes = [ctypes.c_void_p]
        close_handle.restype = ctypes.c_int
        handle = open_process(cls.PROCESS_QUERY_LIMITED_INFORMATION, 0, pid)
        if not handle:
            raise EndpointIdentityError(f"cannot open expected QEMU PID {pid} for identity verification")
        try:
            path_buffer = ctypes.create_unicode_buffer(32768)
            path_length = ctypes.c_ulong(len(path_buffer))
            if not query_image(handle, 0, path_buffer, ctypes.byref(path_length)):
                raise EndpointIdentityError(f"cannot read executable path for QMP PID {pid}")
            created = cls._FileTime()
            exited = cls._FileTime()
            kernel = cls._FileTime()
            user = cls._FileTime()
            if not get_times(
                handle,
                ctypes.byref(created),
                ctypes.byref(exited),
                ctypes.byref(kernel),
                ctypes.byref(user),
            ):
                raise EndpointIdentityError(f"cannot read creation time for QMP PID {pid}")
            created_ticks = (int(created.high) << 32) | int(created.low)
            return ProcessIdentity(pid, path_buffer.value, str(created_ticks))
        finally:
            close_handle(handle)

    @staticmethod
    def _validate_qemu(identity: ProcessIdentity, expected_pid: int) -> None:
        if identity.pid != expected_pid:
            raise EndpointIdentityError(
                f"QMP endpoint belongs to PID {identity.pid}, expected QAC-owned PID {expected_pid}"
            )
        executable_name = identity.executable.replace("/", "\\").rsplit("\\", 1)[-1].casefold()
        if executable_name != "qemu-system-x86_64.exe":
            raise EndpointIdentityError(
                f"QMP listener PID {expected_pid} is not qemu-system-x86_64.exe"
            )
        if not identity.creation_date:
            raise EndpointIdentityError("QEMU process creation time is unavailable")

    @staticmethod
    def _row_address(value: int) -> str:
        return socket.inet_ntoa(struct.pack("<I", value))

    @staticmethod
    def _row_port(value: int) -> int:
        return socket.ntohs(value & 0xFFFF)

    def verify_listener(self, port: int, expected_pid: int, timeout: float) -> ProcessIdentity:
        if timeout <= 0:
            raise QMPTimeout("listener ownership check exceeded its deadline")
        rows = self._tcp_rows(self.TCP_TABLE_OWNER_PID_LISTENER)
        listener_pids = sorted(
            {
                int(row.owning_pid)
                for row in rows
                if row.state == self.MIB_TCP_STATE_LISTEN
                and self._row_address(row.local_address) == QMP_HOST
                and self._row_port(row.local_port) == port
            }
        )
        if listener_pids != [expected_pid]:
            raise EndpointIdentityError(
                f"loopback port {port} listener PID set {listener_pids!r} does not match "
                f"the expected QAC-owned PID {expected_pid}"
            )
        identity = self._process_identity(expected_pid)
        self._validate_qemu(identity, expected_pid)
        return identity

    def verify_connection(
        self,
        port: int,
        expected_pid: int,
        identity: ProcessIdentity,
        sock: Any,
        timeout: float,
    ) -> None:
        if timeout <= 0:
            raise QMPTimeout("connection ownership check exceeded its deadline")
        try:
            peer = sock.getpeername()
            local = sock.getsockname()
        except OSError as exc:
            raise QMPConnectionLost(f"cannot inspect connected QMP socket: {exc}") from exc
        if peer != (QMP_HOST, port) or local[0] != QMP_HOST:
            raise EndpointIdentityError(
                f"QMP socket endpoints {local!r}->{peer!r} do not match loopback port {port}"
            )
        client_port = int(local[1])
        rows = self._tcp_rows(self.TCP_TABLE_OWNER_PID_ALL)
        connection_pids = sorted(
            {
                int(row.owning_pid)
                for row in rows
                if row.state == self.MIB_TCP_STATE_ESTABLISHED
                and self._row_address(row.local_address) == QMP_HOST
                and self._row_port(row.local_port) == port
                and self._row_address(row.remote_address) == QMP_HOST
                and self._row_port(row.remote_port) == client_port
            }
        )
        if connection_pids != [expected_pid]:
            raise EndpointIdentityError(
                f"established QMP connection owner set {connection_pids!r} does not match "
                f"the expected QAC-owned PID {expected_pid}"
            )
        current = self._process_identity(expected_pid)
        self._validate_qemu(current, expected_pid)
        if current != identity:
            raise EndpointIdentityError(
                f"QEMU process identity changed while QMP port {port} was connected"
            )


@dataclass
class DragOutcome:
    success: bool
    button: str
    button_state: str
    down_attempted: bool = False
    down_result: str = "not-attempted"
    down_acknowledged: bool = False
    movement_completed: bool = False
    release_attempts: int = 0
    release_acknowledged: bool = False
    primary_error: dict[str, str] | None = None
    cleanup_errors: list[dict[str, str]] = field(default_factory=list)
    interrupted: bool = False
    guest_event_consumption: str = "unverified"

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


class QMP:
    def __init__(
        self,
        port: int,
        trace: Path,
        expected_pid: int,
        *,
        verifier: Any | None = None,
        connector: Callable[..., Any] = socket.create_connection,
        clock: Callable[[], float] = time.monotonic,
        sleeper: Callable[[float], None] = time.sleep,
        command_timeout: float = DEFAULT_COMMAND_SECONDS,
        cleanup_timeout: float = DEFAULT_CLEANUP_SECONDS,
    ) -> None:
        if not 1 <= int(port) <= 65535:
            raise ValueError("--port must be between 1 and 65535")
        if int(expected_pid) <= 0:
            raise ValueError("--expected-pid must be a positive QAC-owned QEMU PID")
        if command_timeout <= 0 or cleanup_timeout <= 0:
            raise ValueError("QMP command and cleanup deadlines must be positive")
        self.port = int(port)
        self.expected_pid = int(expected_pid)
        self.trace = trace
        self.verifier = verifier or WindowsEndpointVerifier()
        self.clock = clock
        self.sleeper = sleeper
        self.command_timeout = float(command_timeout)
        self.cleanup_timeout = float(cleanup_timeout)
        self.sequence = 0
        self._lock = threading.RLock()
        self._rx_buffer = bytearray()
        self._pending: set[int] = set()
        self._protocol_usable = True
        self._closed = False
        self._uncertain_buttons: dict[str, str] = {}
        self._active_drag_button: str | None = None
        self._identity = self.verifier.verify_listener(
            self.port, self.expected_pid, self.command_timeout
        )
        self.sock: Any | None = None
        try:
            try:
                self.sock = connector((QMP_HOST, self.port), timeout=self.command_timeout)
            except (socket.timeout, TimeoutError) as exc:
                raise QMPTimeout(f"timed out connecting to loopback QMP port {self.port}") from exc
            except OSError as exc:
                raise QMPConnectionLost(
                    f"cannot connect to loopback QMP port {self.port}: {exc}"
                ) from exc
            self._verify_connection(self.command_timeout)
            greeting = self._read_message(self.clock() + self.command_timeout)
            if not isinstance(greeting.get("QMP"), dict):
                raise QMPProtocolError(f"unexpected QMP greeting: {greeting!r}")
            self._record(
                {
                    "kind": "endpoint-verified",
                    "host": QMP_HOST,
                    "port": self.port,
                    "expected_pid": self.expected_pid,
                    "executable": self._identity.executable,
                    "creation_date": self._identity.creation_date,
                }
            )
            self._command("qmp_capabilities")
        except BaseException:
            self.close()
            raise

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        sock, self.sock = self.sock, None
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass

    def _record(self, record: dict[str, Any]) -> None:
        self.trace.parent.mkdir(parents=True, exist_ok=True)
        with self.trace.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"utc": utc_now(), **record}, separators=(",", ":")) + "\n")

    def _remaining(self, deadline: float) -> float:
        remaining = deadline - self.clock()
        if remaining <= 0:
            raise QMPTimeout("QMP operation deadline expired")
        return remaining

    def _verify_connection(self, timeout: float) -> None:
        if self.sock is None or self._closed:
            raise QMPConnectionLost("QMP connection is closed")
        self.verifier.verify_connection(
            self.port, self.expected_pid, self._identity, self.sock, timeout
        )

    def _read_message(self, deadline: float) -> dict[str, Any]:
        if self.sock is None or self._closed:
            raise QMPConnectionLost("QMP connection is closed")
        while True:
            newline = self._rx_buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self._rx_buffer[:newline]).strip()
                del self._rx_buffer[: newline + 1]
                if not line:
                    continue
                try:
                    value = json.loads(line)
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    self._protocol_usable = False
                    raise QMPProtocolError(f"malformed QMP JSON response: {exc}") from exc
                if not isinstance(value, dict):
                    self._protocol_usable = False
                    raise QMPProtocolError("QMP response must be a JSON object")
                return value
            if len(self._rx_buffer) > MAX_QMP_LINE_BYTES:
                self._protocol_usable = False
                raise QMPProtocolError("QMP response exceeded the maximum line size")
            remaining = self._remaining(deadline)
            try:
                self.sock.settimeout(remaining)
                chunk = self.sock.recv(65536)
            except (socket.timeout, TimeoutError) as exc:
                raise QMPTimeout("timed out waiting for a QMP response") from exc
            except OSError as exc:
                self._protocol_usable = False
                raise QMPConnectionLost(f"QMP read failed: {exc}") from exc
            if not chunk:
                self._protocol_usable = False
                raise QMPConnectionLost("QMP connection closed unexpectedly")
            self._rx_buffer.extend(chunk)

    def _validate_delay(self, delay: float) -> None:
        if not math.isfinite(delay) or delay < 0 or delay > MAX_DELAY_SECONDS:
            raise ValueError(f"delay must be between 0 and {MAX_DELAY_SECONDS:g} seconds")

    def _sleep(self, delay: float, deadline: float | None = None) -> None:
        self._validate_delay(delay)
        if delay == 0:
            return
        if deadline is None:
            self.sleeper(delay)
            return
        remaining = self._remaining(deadline)
        self.sleeper(min(delay, remaining))
        if delay > remaining:
            raise QMPTimeout("QMP operation deadline expired during pacing delay")

    def _command_deadline(self, deadline: float | None) -> float:
        command_deadline = self.clock() + self.command_timeout
        return min(command_deadline, deadline) if deadline is not None else command_deadline

    def _command(
        self,
        execute: str,
        arguments: dict[str, Any] | None = None,
        *,
        deadline: float | None = None,
    ) -> dict[str, Any]:
        with self._lock:
            if self.sock is None or self._closed:
                raise QMPConnectionLost("QMP connection is closed")
            if not self._protocol_usable:
                raise QMPProtocolError("QMP connection is not safe to reuse")
            if execute == "input-send-event" or execute == "quit":
                self._verify_connection(self._remaining(self._command_deadline(deadline)))
            command_deadline = self._command_deadline(deadline)
            self.sequence += 1
            request: dict[str, Any] = {"execute": execute, "id": self.sequence}
            if arguments:
                request["arguments"] = arguments
            wire = json.dumps(request, separators=(",", ":")).encode("ascii") + b"\r\n"
            self._record({"kind": "qmp-request", "request": request})
            try:
                self.sock.settimeout(self._remaining(command_deadline))
                self.sock.sendall(wire)
            except (socket.timeout, TimeoutError) as exc:
                self._protocol_usable = False
                raise QMPTimeout(f"timed out writing QMP {execute}") from exc
            except OSError as exc:
                self._protocol_usable = False
                raise QMPConnectionLost(f"QMP write failed for {execute}: {exc}") from exc
            self._pending.add(self.sequence)
            request_id = self.sequence
            while True:
                response = self._read_message(command_deadline)
                if "event" in response and "id" not in response:
                    self._record({"kind": "qmp-event", "response": response})
                    continue
                response_id = response.get("id")
                if type(response_id) is not int or response_id not in self._pending:
                    self._protocol_usable = False
                    raise QMPProtocolError(
                        f"QMP response has an unknown or invalid command id: {response!r}"
                    )
                self._pending.remove(response_id)
                self._record(
                    {
                        "kind": "qmp-response" if response_id == request_id else "qmp-late-response",
                        "request_id": response_id,
                        "response": response,
                    }
                )
                if response_id != request_id:
                    continue
                if "error" in response:
                    raise QMPCommandError(execute, response)
                if "return" not in response:
                    self._protocol_usable = False
                    raise QMPProtocolError(
                        f"QMP response for {execute} has neither return nor error"
                    )
                return response

    def input_event(self, event: dict[str, Any], *, deadline: float | None = None) -> dict[str, Any]:
        with self._lock:
            if event.get("type") == "btn":
                event_button = event.get("data", {}).get("button")
                if self._active_drag_button != event_button:
                    raise QMPError("mouse button events must be issued through the bounded drag action")
            if self._uncertain_buttons:
                event_data = event.get("data", {})
                releasing_uncertain = (
                    event.get("type") == "btn"
                    and event_data.get("down") is False
                    and event_data.get("button") in self._uncertain_buttons
                )
                if not releasing_uncertain:
                    buttons = ", ".join(sorted(self._uncertain_buttons))
                    raise QMPError(f"input blocked while button state is uncertain: {buttons}")
            return self._command("input-send-event", {"events": [event]}, deadline=deadline)

    def input_events_together(
        self, events: list[dict[str, Any]], *, deadline: float | None = None
    ) -> dict[str, Any]:
        with self._lock:
            if any(event.get("type") == "btn" for event in events):
                raise QMPError("mouse button events must be issued through the bounded drag action")
            if self._uncertain_buttons:
                buttons = ", ".join(sorted(self._uncertain_buttons))
                raise QMPError(f"input blocked while button state is uncertain: {buttons}")
            return self._command(
                "input-send-event", {"events": events}, deadline=deadline
            )

    def relative(
        self,
        dx: int,
        dy: int,
        delay: float,
        combined: bool = False,
        *,
        deadline: float | None = None,
    ) -> None:
        self._validate_delay(delay)
        if (abs(dx) + MAX_RELATIVE_STEP - 1) // MAX_RELATIVE_STEP + (
            abs(dy) + MAX_RELATIVE_STEP - 1
        ) // MAX_RELATIVE_STEP > MAX_RELATIVE_EVENTS:
            raise ValueError(
                f"relative movement exceeds {MAX_RELATIVE_EVENTS} bounded QMP events"
            )
        if deadline is None:
            deadline = self.clock() + MAX_DRAG_SECONDS
        with self._lock:
            remaining = {"x": dx, "y": dy}
            if combined:
                while remaining["x"] or remaining["y"]:
                    events: list[dict[str, Any]] = []
                    for axis in ("x", "y"):
                        value = remaining[axis]
                        if value:
                            step = max(-MAX_RELATIVE_STEP, min(MAX_RELATIVE_STEP, value))
                            events.append({"type": "rel", "data": {"axis": axis, "value": step}})
                            remaining[axis] -= step
                    self.input_events_together(events, deadline=deadline)
                    self._sleep(delay, deadline)
                return
            for axis in ("x", "y"):
                while remaining[axis]:
                    value = remaining[axis]
                    step = max(-MAX_RELATIVE_STEP, min(MAX_RELATIVE_STEP, value))
                    self.input_event(
                        {"type": "rel", "data": {"axis": axis, "value": step}},
                        deadline=deadline,
                    )
                    remaining[axis] -= step
                    self._sleep(delay, deadline)

    @staticmethod
    def _error_dict(error: BaseException) -> dict[str, str]:
        return {"type": type(error).__name__, "message": str(error)}

    def _record_drag_state(self, button: str, phase: str, state: str, **extra: Any) -> None:
        self._record(
            {"kind": "drag-state", "button": button, "phase": phase, "state": state, **extra}
        )

    def drag(self, button: str, dx: int, dy: int, delay: float) -> DragOutcome:
        self._validate_delay(delay)
        if not button:
            raise ValueError("button must not be empty")
        outcome = DragOutcome(False, button, "not-attempted")
        with self._lock:
            if self._uncertain_buttons:
                outcome.button_state = "unknown"
                outcome.primary_error = self._error_dict(
                    QMPError("input blocked because a prior button state is unresolved")
                )
                return outcome
            self._active_drag_button = button
            operation_deadline = self.clock() + MAX_DRAG_SECONDS
            phase = "button-down"
            try:
                outcome.down_attempted = True
                outcome.down_result = "attempted"
                outcome.button_state = "down-attempted-unknown"
                self._record_drag_state(button, "button-down", outcome.button_state, attempted=True)
                self.input_event(
                    {"type": "btn", "data": {"button": button, "down": True}},
                    deadline=operation_deadline,
                )
                outcome.down_acknowledged = True
                outcome.down_result = "acknowledged"
                outcome.button_state = "down-command-acknowledged"
                self._record_drag_state(
                    button,
                    "button-down",
                    outcome.button_state,
                    acknowledged=True,
                    guest_event_consumption="unverified",
                )
                phase = "down-pacing"
                self._sleep(0.15, operation_deadline)
                phase = "movement"
                self.relative(dx, dy, delay, deadline=operation_deadline)
                outcome.movement_completed = True
                self._record_drag_state(button, "movement", "completed", dx=dx, dy=dy)

                phase = "button-up"
                outcome.release_attempts += 1
                self._record_drag_state(
                    button, "button-up", "release-attempted", attempt=outcome.release_attempts
                )
                self.input_event(
                    {"type": "btn", "data": {"button": button, "down": False}},
                    deadline=operation_deadline,
                )
                outcome.release_acknowledged = True
                outcome.button_state = "release-command-acknowledged"
                self._record_drag_state(
                    button,
                    "button-up",
                    outcome.button_state,
                    acknowledged=True,
                    guest_event_consumption="unverified",
                )
                phase = "up-pacing"
                self._sleep(0.15, operation_deadline)
            except BaseException as exc:
                outcome.primary_error = self._error_dict(exc)
                outcome.interrupted = isinstance(exc, KeyboardInterrupt)
                if phase == "button-down" and not outcome.down_acknowledged:
                    outcome.down_result = (
                        "failed"
                        if isinstance(exc, (QMPCommandError, EndpointIdentityError, ValueError, OSError))
                        else "attempted-unconfirmed"
                    )

            if outcome.down_attempted and not outcome.release_acknowledged:
                cleanup_deadline = self.clock() + self.cleanup_timeout
                outcome.release_attempts += 1
                try:
                    try:
                        self._record_drag_state(
                            button,
                            "cleanup-button-up",
                            "release-attempted",
                            attempt=outcome.release_attempts,
                            deadline_seconds=self.cleanup_timeout,
                        )
                    except BaseException as exc:
                        outcome.cleanup_errors.append(self._error_dict(exc))
                    self.input_event(
                        {"type": "btn", "data": {"button": button, "down": False}},
                        deadline=cleanup_deadline,
                    )
                    outcome.release_acknowledged = True
                    outcome.button_state = "release-command-acknowledged"
                    self._record_drag_state(
                        button,
                        "cleanup-button-up",
                        outcome.button_state,
                        acknowledged=True,
                        guest_event_consumption="unverified",
                    )
                except BaseException as exc:
                    outcome.cleanup_errors.append(self._error_dict(exc))

            if not outcome.release_acknowledged:
                outcome.button_state = "unknown"
                reason = "button-up was not acknowledged"
                if outcome.cleanup_errors:
                    reason += f": {outcome.cleanup_errors[-1]['message']}"
                self._uncertain_buttons[button] = reason
            outcome.success = (
                outcome.primary_error is None
                and not outcome.cleanup_errors
                and outcome.down_acknowledged
                and outcome.movement_completed
                and outcome.release_acknowledged
            )
            self._active_drag_button = None
            return outcome

    def key(
        self, qcode: str, delay: float = 0.08, modifiers: tuple[str, ...] = ()
    ) -> None:
        self._validate_delay(delay)
        held: list[str] = []
        for modifier in modifiers:
            event_key = {"type": "qcode", "data": modifier}
            self.input_event({"type": "key", "data": {"down": True, "key": event_key}})
            held.append(modifier)
            self._sleep(delay)
        event_key = {"type": "qcode", "data": qcode}
        self.input_event({"type": "key", "data": {"down": True, "key": event_key}})
        held.append(qcode)
        self._sleep(delay)
        for code in reversed(held):
            release_key = {"type": "qcode", "data": code}
            self.input_event({"type": "key", "data": {"down": False, "key": release_key}})
            self._sleep(delay)

    def type_text(self, value: str, delay: float = 0.08) -> None:
        for char in value:
            if char == " ":
                self.key("spc", delay)
            elif char == ".":
                self.key("dot", delay)
            elif char.isalpha() and char.isascii():
                if char.isupper():
                    self.key(char.lower(), delay, ("shift",))
                else:
                    self.key(char, delay)
            elif char.isdigit():
                self.key(char, delay)
            else:
                raise ValueError(f"unsupported text character {char!r}")

    def hmp(self, command: str) -> dict[str, Any]:
        return self._command("human-monitor-command", {"command-line": command})

    def screenshot(self, path: Path) -> None:
        full_path = str(path.resolve())
        response = self.hmp(f"screendump {full_path}")
        self._record({"kind": "screenshot", "path": full_path, "response": response})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "action",
        choices=("query", "move", "combined-move", "drag", "click", "key", "type", "wheel", "screenshot", "quit"),
    )
    parser.add_argument("--port", type=int, default=63800)
    parser.add_argument("--expected-pid", type=int, required=True, help="PID reported for this QAC-owned QEMU child")
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--dx", type=int, default=0)
    parser.add_argument("--dy", type=int, default=0)
    parser.add_argument("--qcode", default="")
    parser.add_argument("--text", default="")
    parser.add_argument("--button", default="left")
    parser.add_argument("--wheel", type=int, default=0)
    parser.add_argument("--delay-ms", type=int, default=200)
    parser.add_argument("--screenshot-path", type=Path)
    args = parser.parse_args()
    delay = args.delay_ms / 1000
    if not 0 <= args.delay_ms <= MAX_DELAY_SECONDS * 1000:
        raise ValueError(f"--delay-ms must be between 0 and {int(MAX_DELAY_SECONDS * 1000)}")
    qmp = QMP(args.port, args.trace, args.expected_pid)
    try:
        if args.action == "query":
            qmp._command("query-status")
            qmp.hmp("info mice")
        elif args.action in ("move", "combined-move"):
            qmp.relative(args.dx, args.dy, delay, args.action == "combined-move")
        elif args.action == "drag":
            outcome = qmp.drag(args.button, args.dx, args.dy, delay)
            print(json.dumps({"action": args.action, **outcome.as_dict()}, separators=(",", ":")))
            return 0 if outcome.success else (130 if outcome.interrupted else 2)
        elif args.action == "click":
            outcome = qmp.drag(args.button, 0, 0, delay)
            print(json.dumps({"action": args.action, **outcome.as_dict()}, separators=(",", ":")))
            return 0 if outcome.success else (130 if outcome.interrupted else 2)
        elif args.action == "key":
            if not args.qcode:
                raise ValueError("--qcode is required for key")
            qmp.key(args.qcode)
        elif args.action == "type":
            qmp.type_text(args.text)
        elif args.action == "wheel":
            if args.wheel == 0:
                raise ValueError("--wheel must be nonzero")
            qmp.input_event({"type": "rel", "data": {"axis": "z", "value": args.wheel}})
            qmp._sleep(delay)
        elif args.action == "screenshot":
            if args.screenshot_path is None:
                raise ValueError("--screenshot-path is required for screenshot")
            qmp.screenshot(args.screenshot_path)
        elif args.action == "quit":
            qmp._command("quit")
        print(
            json.dumps(
                {
                    "action": args.action,
                    "port": args.port,
                    "expected_pid": args.expected_pid,
                    "dx": args.dx,
                    "dy": args.dy,
                    "qcode": args.qcode,
                },
                separators=(",", ":"),
            )
        )
        return 0
    finally:
        qmp.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("FONT15R1 QMP input interrupted", file=sys.stderr)
        raise SystemExit(130)
    except Exception as exc:
        print(f"FONT15R1 QMP input error: {exc}", file=sys.stderr)
        raise SystemExit(2)
