"""Focused host-only safety tests for the FONT15 QMP helper."""

from __future__ import annotations

import importlib.util
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
HELPER_PATH = ROOT / "out" / "font15r1-validation" / "guest" / "font15r1_qmp.py"
SPEC = importlib.util.spec_from_file_location("font15r1_qmp", HELPER_PATH)
assert SPEC is not None and SPEC.loader is not None
qmp_module = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = qmp_module
SPEC.loader.exec_module(qmp_module)


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class FakeVerifier:
    def __init__(self, *, reject_listener: bool = False, reject_after_checks: int | None = None) -> None:
        self.reject_listener = reject_listener
        self.reject_after_checks = reject_after_checks
        self.listener_calls = 0
        self.connection_checks: list[tuple[int, int, float]] = []

    def verify_listener(self, port: int, expected_pid: int, timeout: float) -> object:
        self.listener_calls += 1
        if self.reject_listener:
            raise qmp_module.EndpointIdentityError("stub listener is not the expected QEMU")
        return qmp_module.ProcessIdentity(expected_pid, "C:\\QEMU\\qemu-system-x86_64.exe", "123456")

    def verify_connection(
        self, port: int, expected_pid: int, identity: object, sock: Any, timeout: float
    ) -> None:
        self.connection_checks.append((port, expected_pid, timeout))
        if self.reject_after_checks is not None and len(self.connection_checks) > self.reject_after_checks:
            raise qmp_module.EndpointIdentityError("QMP process identity changed")
        if sock.getpeername() != ("127.0.0.1", port):
            raise qmp_module.EndpointIdentityError("unexpected peer")
        if identity.pid != expected_pid or identity.executable.rsplit("\\", 1)[-1] != "qemu-system-x86_64.exe":
            raise qmp_module.EndpointIdentityError("unexpected process identity")


class FakeSocket:
    def __init__(self, clock: FakeClock, handler: Any) -> None:
        self.clock = clock
        self.handler = handler
        self.timeout: float | None = None
        self.reads = [b'{"QMP":{"version":{},"capabilities":[]}}\r\n']
        self.requests: list[dict[str, Any]] = []
        self.closed = False
        self.peer = ("127.0.0.1", 63800)
        self.local = ("127.0.0.1", 50123)
        self.disconnect_when_empty = False

    def settimeout(self, value: float | None) -> None:
        self.timeout = value

    def getpeername(self) -> tuple[str, int]:
        return self.peer

    def getsockname(self) -> tuple[str, int]:
        return self.local

    def sendall(self, payload: bytes) -> None:
        if self.closed:
            raise OSError("socket is closed")
        request = json.loads(payload.decode("ascii"))
        self.requests.append(request)
        responses = self.handler(request, self)
        if responses is None:
            return
        if isinstance(responses, dict):
            responses = [responses]
        for response in responses:
            self.reads.append(json.dumps(response).encode("ascii") + b"\r\n")

    def recv(self, size: int) -> bytes:
        if self.closed:
            return b""
        if self.reads:
            return self.reads.pop(0)
        if self.disconnect_when_empty:
            return b""
        self.clock.now += self.timeout or 0.0
        raise socket.timeout("fake deadline")

    def close(self) -> None:
        self.closed = True


def qmp_response(request: dict[str, Any]) -> dict[str, Any]:
    return {"return": {}, "id": request["id"]}


class LoopbackQMPFixture:
    """A localhost QMP stub that exits when the helper closes its socket."""

    def __init__(self, *, withhold_capabilities: bool = False) -> None:
        self.withhold_capabilities = withhold_capabilities
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.requests: list[dict[str, Any]] = []
        self.finished = threading.Event()
        self.thread = threading.Thread(target=self._serve, name="font15r2r1-qmp-stub")
        self.thread.start()

    def _serve(self) -> None:
        connection: socket.socket | None = None
        try:
            connection, _ = self.listener.accept()
            connection.sendall(b'{"QMP":{"version":{},"capabilities":[]}}\r\n')
            with connection.makefile("rb") as stream:
                while True:
                    line = stream.readline()
                    if not line:
                        return
                    request = json.loads(line)
                    self.requests.append(request)
                    if self.withhold_capabilities and request.get("execute") == "qmp_capabilities":
                        continue
                    connection.sendall(json.dumps(qmp_response(request)).encode("ascii") + b"\r\n")
        except (OSError, json.JSONDecodeError):
            pass
        finally:
            if connection is not None:
                connection.close()
            self.listener.close()
            self.finished.set()

    def close(self) -> None:
        try:
            self.listener.close()
        except OSError:
            pass
        self.thread.join(timeout=2)


class QMPDragSafetyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="font15r2r1-qmp-test-")
        self.trace = Path(self.temp.name) / "qmp.jsonl"
        self.clock = FakeClock()
        self.verifier = FakeVerifier()
        self.sockets: list[FakeSocket] = []
        self.connect_calls = 0

    def tearDown(self) -> None:
        for sock in self.sockets:
            sock.close()
        self.temp.cleanup()

    def make_qmp(
        self,
        handler: Any | None = None,
        *,
        command_timeout: float = 1.0,
        cleanup_timeout: float = 0.5,
        sleeper: Any | None = None,
    ) -> qmp_module.QMP:
        def default_handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            return qmp_response(request)

        def connector(address: tuple[str, int], timeout: float) -> FakeSocket:
            self.connect_calls += 1
            sock = FakeSocket(self.clock, handler or default_handler)
            sock.peer = address
            self.sockets.append(sock)
            return sock

        return qmp_module.QMP(
            63800,
            self.trace,
            4242,
            verifier=self.verifier,
            connector=connector,
            clock=self.clock.monotonic,
            sleeper=sleeper or self.clock.sleep,
            command_timeout=command_timeout,
            cleanup_timeout=cleanup_timeout,
        )

    @staticmethod
    def input_events(sock: FakeSocket) -> list[dict[str, Any]]:
        return [
            event
            for request in sock.requests
            if request["execute"] == "input-send-event"
            for event in request["arguments"]["events"]
        ]

    def test_normal_drag_orders_down_separate_bounded_axes_and_up(self) -> None:
        qmp = self.make_qmp()
        try:
            outcome = qmp.drag("left", 36, -1, 0.2)
            self.assertTrue(outcome.success)
            self.assertEqual(outcome.button_state, "release-command-acknowledged")
            self.assertTrue(outcome.down_attempted)
            self.assertEqual(outcome.down_result, "acknowledged")
            self.assertTrue(outcome.down_acknowledged)
            self.assertTrue(outcome.release_acknowledged)
            self.assertEqual(outcome.guest_event_consumption, "unverified")
            events = self.input_events(self.sockets[0])
            self.assertEqual(
                events,
                [
                    {"type": "btn", "data": {"button": "left", "down": True}},
                    {"type": "rel", "data": {"axis": "x", "value": 35}},
                    {"type": "rel", "data": {"axis": "x", "value": 1}},
                    {"type": "rel", "data": {"axis": "y", "value": -1}},
                    {"type": "btn", "data": {"button": "left", "down": False}},
                ],
            )
            self.assertTrue(all(abs(e["data"].get("value", 0)) <= 35 for e in events))
            self.assertEqual(
                [r["execute"] for r in self.sockets[0].requests],
                ["qmp_capabilities"] + ["input-send-event"] * 5,
            )
            records = [json.loads(line) for line in self.trace.read_text(encoding="utf-8").splitlines()]
            release_id = next(
                request["id"]
                for request in reversed(self.sockets[0].requests)
                if request["execute"] == "input-send-event"
                and request["arguments"]["events"][0].get("data", {}).get("down") is False
            )
            self.assertTrue(
                any(
                    record.get("kind") == "qmp-response"
                    and record.get("request_id") == release_id
                    and "return" in record.get("response", {})
                    for record in records
                )
            )
            self.assertTrue(
                any(
                    record.get("kind") == "drag-state"
                    and record.get("phase") == "button-up"
                    and record.get("state") == "release-command-acknowledged"
                    for record in records
                )
            )
            self.assertTrue(all(check[1] == 4242 for check in self.verifier.connection_checks))
        finally:
            qmp.close()

    def test_movement_qmp_failure_runs_one_bounded_release(self) -> None:
        def handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            events = request.get("arguments", {}).get("events", [])
            if request.get("execute") == "input-send-event" and events and events[0]["type"] == "rel":
                return {"id": request["id"], "error": {"class": "GenericError", "desc": "move failed"}}
            return qmp_response(request)

        qmp = self.make_qmp(handler)
        try:
            outcome = qmp.drag("left", 1, 0, 0.2)
            self.assertFalse(outcome.success)
            self.assertEqual(outcome.primary_error["type"], "QMPCommandError")
            self.assertEqual(outcome.release_attempts, 1)
            self.assertTrue(outcome.release_acknowledged)
            self.assertEqual(self.input_events(self.sockets[0])[-1]["data"], {"button": "left", "down": False})
        finally:
            qmp.close()

    def test_movement_timeout_attempts_release_inside_cleanup_deadline(self) -> None:
        delayed_movement_id: int | None = None

        def handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            nonlocal delayed_movement_id
            events = request.get("arguments", {}).get("events", [])
            if request.get("execute") == "input-send-event" and events and events[0]["type"] == "rel":
                delayed_movement_id = request["id"]
                return None
            if request.get("execute") == "input-send-event" and events and events[0].get("data", {}).get("down") is False:
                return [
                    {"return": {}, "id": delayed_movement_id},
                    qmp_response(request),
                ]
            return qmp_response(request)

        qmp = self.make_qmp(handler, command_timeout=0.25, cleanup_timeout=0.4)
        try:
            started = self.clock.now
            outcome = qmp.drag("left", 1, 0, 0)
            self.assertFalse(outcome.success)
            self.assertEqual(outcome.primary_error["type"], "QMPTimeout")
            self.assertTrue(outcome.release_acknowledged)
            self.assertEqual(outcome.release_attempts, 1)
            self.assertTrue(self.verifier.connection_checks)
            self.assertLessEqual(self.verifier.connection_checks[-1][2], 0.4)
            self.assertLessEqual(self.clock.now - started, 0.15 + 0.25 + 0.4)
            self.assertEqual(self.connect_calls, 1)
        finally:
            qmp.close()

    def test_button_down_failure_is_tracked_and_release_is_attempted(self) -> None:
        def handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            events = request.get("arguments", {}).get("events", [])
            if request.get("execute") == "input-send-event" and events and events[0].get("data", {}).get("down") is True:
                return {"id": request["id"], "error": {"class": "GenericError", "desc": "down rejected"}}
            return qmp_response(request)

        qmp = self.make_qmp(handler)
        try:
            outcome = qmp.drag("left", 0, 0, 0)
            self.assertFalse(outcome.success)
            self.assertTrue(outcome.down_attempted)
            self.assertEqual(outcome.down_result, "failed")
            self.assertFalse(outcome.down_acknowledged)
            self.assertTrue(outcome.release_acknowledged)
            self.assertEqual(outcome.button_state, "release-command-acknowledged")
            self.assertEqual([event["type"] for event in self.input_events(self.sockets[0])], ["btn", "btn"])
        finally:
            qmp.close()

    def test_repeated_release_failure_reports_unknown_without_retry_loop(self) -> None:
        def handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            events = request.get("arguments", {}).get("events", [])
            if request.get("execute") == "input-send-event" and events and events[0].get("data", {}).get("down") is False:
                return {"id": request["id"], "error": {"class": "GenericError", "desc": "up rejected"}}
            return qmp_response(request)

        qmp = self.make_qmp(handler)
        try:
            outcome = qmp.drag("left", 0, 0, 0)
            self.assertFalse(outcome.success)
            self.assertEqual(outcome.release_attempts, 2)
            self.assertFalse(outcome.release_acknowledged)
            self.assertEqual(outcome.button_state, "unknown")
            self.assertEqual(outcome.primary_error["type"], "QMPCommandError")
            self.assertEqual(len(outcome.cleanup_errors), 1)
            self.assertEqual(
                sum(
                    event["type"] == "btn" and event["data"]["down"]
                    for event in self.input_events(self.sockets[0])
                ),
                1,
            )
            sent_before = len(self.sockets[0].requests)
            blocked = qmp.drag("left", 0, 0, 0)
            self.assertFalse(blocked.success)
            self.assertEqual(blocked.button_state, "unknown")
            self.assertEqual(len(self.sockets[0].requests), sent_before)
        finally:
            qmp.close()

    def test_connection_loss_stops_input_and_does_not_reconnect(self) -> None:
        def handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            events = request.get("arguments", {}).get("events", [])
            if request.get("execute") == "input-send-event" and events and events[0]["type"] == "rel":
                sock.disconnect_when_empty = True
                return None
            return qmp_response(request)

        qmp = self.make_qmp(handler)
        try:
            outcome = qmp.drag("left", 1, 0, 0)
            self.assertFalse(outcome.success)
            self.assertEqual(outcome.primary_error["type"], "QMPConnectionLost")
            self.assertEqual(outcome.button_state, "unknown")
            self.assertEqual(self.connect_calls, 1)
            sent_events = self.input_events(self.sockets[0])
            self.assertEqual(len(sent_events), 2)
            self.assertTrue(sent_events[0]["data"]["down"])
            self.assertEqual(sent_events[1]["type"], "rel")
        finally:
            qmp.close()

    def test_malformed_response_is_deterministic_and_does_not_reuse_stream(self) -> None:
        def handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            events = request.get("arguments", {}).get("events", [])
            if request.get("execute") == "input-send-event" and events and events[0]["type"] == "rel":
                return None
            return qmp_response(request)

        qmp = self.make_qmp(handler)
        self.sockets[0].reads.clear()
        # Replace the movement reply with malformed bytes after the request is written.
        original_sendall = self.sockets[0].sendall

        def malformed_sendall(payload: bytes) -> None:
            request = json.loads(payload.decode("ascii"))
            if request.get("execute") == "input-send-event" and request["arguments"]["events"][0]["type"] == "rel":
                self.sockets[0].requests.append(request)
                self.sockets[0].reads.append(b"{malformed}\r\n")
                return
            original_sendall(payload)

        self.sockets[0].sendall = malformed_sendall  # type: ignore[method-assign]
        try:
            outcome = qmp.drag("left", 1, 0, 0)
            self.assertFalse(outcome.success)
            self.assertEqual(outcome.primary_error["type"], "QMPProtocolError")
            self.assertEqual(outcome.button_state, "unknown")
            self.assertFalse(outcome.release_acknowledged)
        finally:
            qmp.close()

    def test_cleanup_failure_preserves_movement_and_release_errors(self) -> None:
        def handler(request: dict[str, Any], sock: FakeSocket) -> Any:
            events = request.get("arguments", {}).get("events", [])
            if request.get("execute") == "input-send-event" and events and (
                events[0]["type"] == "rel" or events[0].get("data", {}).get("down") is False
            ):
                return {"id": request["id"], "error": {"class": "GenericError", "desc": "rejected"}}
            return qmp_response(request)

        qmp = self.make_qmp(handler)
        try:
            outcome = qmp.drag("left", 1, 0, 0)
            self.assertFalse(outcome.success)
            self.assertEqual(outcome.primary_error["type"], "QMPCommandError")
            self.assertEqual(len(outcome.cleanup_errors), 1)
            self.assertIn("QMP input-send-event failed", outcome.cleanup_errors[0]["message"])
            self.assertEqual(outcome.button_state, "unknown")
        finally:
            qmp.close()

    def test_repeated_successful_drags_leave_no_helper_button_state(self) -> None:
        qmp = self.make_qmp()
        try:
            first = qmp.drag("left", 1, 1, 0)
            second = qmp.drag("left", -1, -1, 0)
            self.assertTrue(first.success)
            self.assertTrue(second.success)
            events = self.input_events(self.sockets[0])
            self.assertEqual(sum(e["type"] == "btn" and e["data"]["down"] for e in events), 2)
            self.assertEqual(sum(e["type"] == "btn" and not e["data"]["down"] for e in events), 2)
            self.assertEqual(qmp._uncertain_buttons, {})
        finally:
            qmp.close()

    def test_invalid_endpoint_identity_sends_no_input(self) -> None:
        self.verifier = FakeVerifier(reject_listener=True)

        def unexpected_connector(address: tuple[str, int], timeout: float) -> FakeSocket:
            self.fail("connector must not run for an invalid endpoint")

        with self.assertRaises(qmp_module.EndpointIdentityError):
            qmp_module.QMP(
                63800,
                self.trace,
                4242,
                verifier=self.verifier,
                connector=unexpected_connector,
                clock=self.clock.monotonic,
                sleeper=self.clock.sleep,
            )
        self.assertEqual(self.connect_calls, 0)

    @unittest.skipUnless(sys.platform == "win32", "Windows TCP ownership query")
    def test_windows_owner_query_rejects_a_non_qemu_loopback_listener(self) -> None:
        fixture = LoopbackQMPFixture()
        client = socket.create_connection(("127.0.0.1", fixture.port), timeout=1)
        try:
            client.recv(1024)
            verifier = qmp_module.WindowsEndpointVerifier()
            with self.assertRaisesRegex(
                qmp_module.EndpointIdentityError, "is not qemu-system-x86_64.exe"
            ):
                verifier.verify_listener(fixture.port, os.getpid(), timeout=5)
            with self.assertRaisesRegex(
                qmp_module.EndpointIdentityError, "is not qemu-system-x86_64.exe"
            ):
                verifier.verify_connection(
                    fixture.port,
                    os.getpid(),
                    qmp_module.ProcessIdentity(os.getpid(), sys.executable, "test-process"),
                    client,
                    timeout=5,
                )
        finally:
            client.close()
            fixture.close()
        self.assertFalse(fixture.thread.is_alive())

    def test_endpoint_identity_loss_blocks_release_and_all_later_input(self) -> None:
        self.verifier = FakeVerifier(reject_after_checks=3)
        qmp = self.make_qmp()
        try:
            outcome = qmp.drag("left", 1, 0, 0)
            self.assertFalse(outcome.success)
            self.assertEqual(outcome.primary_error["type"], "EndpointIdentityError")
            self.assertEqual(outcome.button_state, "unknown")
            self.assertFalse(outcome.release_acknowledged)
            events = self.input_events(self.sockets[0])
            self.assertEqual([event["type"] for event in events], ["btn", "rel"])
            self.assertTrue(events[0]["data"]["down"])
            self.assertEqual(self.connect_calls, 1)
        finally:
            qmp.close()

    def test_interrupted_drag_runs_release_cleanup(self) -> None:
        def interrupt_sleep(seconds: float) -> None:
            raise KeyboardInterrupt("fixture interruption")

        qmp = self.make_qmp(sleeper=interrupt_sleep)
        try:
            outcome = qmp.drag("left", 1, 0, 0)
            self.assertFalse(outcome.success)
            self.assertTrue(outcome.interrupted)
            self.assertTrue(outcome.release_acknowledged)
            self.assertEqual(outcome.button_state, "release-command-acknowledged")
        finally:
            qmp.close()

    def test_loopback_startup_communication_and_close(self) -> None:
        fixture = LoopbackQMPFixture()
        qmp = None
        verifier = FakeVerifier()
        try:
            qmp = qmp_module.QMP(
                fixture.port,
                self.trace,
                4242,
                verifier=verifier,
                command_timeout=1,
            )
            response = qmp._command("query-status")
            self.assertEqual(response.get("return"), {})
            self.assertEqual([request["execute"] for request in fixture.requests], ["qmp_capabilities", "query-status"])
        finally:
            if qmp is not None:
                qmp.close()
            fixture.close()
        self.assertTrue(fixture.finished.wait(1))
        self.assertFalse(fixture.thread.is_alive())

    def test_loopback_silent_capabilities_timeout_closes_transport(self) -> None:
        fixture = LoopbackQMPFixture(withhold_capabilities=True)
        try:
            with self.assertRaises(qmp_module.QMPTimeout):
                qmp_module.QMP(
                    fixture.port,
                    self.trace,
                    4242,
                    verifier=FakeVerifier(),
                    command_timeout=0.2,
                )
        finally:
            fixture.close()
        self.assertTrue(fixture.finished.wait(1))
        self.assertFalse(fixture.thread.is_alive())
        self.assertEqual([request["execute"] for request in fixture.requests], ["qmp_capabilities"])

    def test_closed_loopback_port_is_classified_and_leaves_no_socket(self) -> None:
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        with self.assertRaises((qmp_module.QMPConnectionLost, qmp_module.QMPTimeout)):
            qmp_module.QMP(
                port,
                self.trace,
                4242,
                verifier=FakeVerifier(),
                command_timeout=0.2,
            )

    def test_helper_cli_exits_with_failure_for_unowned_endpoint(self) -> None:
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        completed = subprocess.run(
            [
                sys.executable,
                str(HELPER_PATH),
                "drag",
                "--port",
                str(port),
                "--expected-pid",
                "4242",
                "--trace",
                str(self.trace),
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("does not match the expected QAC-owned PID", completed.stderr)

    def test_concurrent_query_waits_until_drag_cleanup_finishes(self) -> None:
        sleep_entered = threading.Event()
        allow_drag = threading.Event()
        query_started = threading.Event()
        query_done = threading.Event()

        def blocking_sleeper(seconds: float) -> None:
            sleep_entered.set()
            if not allow_drag.wait(2):
                raise TimeoutError("test did not release the drag sleeper")
            self.clock.now += seconds

        qmp = self.make_qmp(sleeper=blocking_sleeper)
        drag_results: list[Any] = []

        def run_drag() -> None:
            drag_results.append(qmp.drag("left", 1, 1, 0))

        def run_query() -> None:
            query_started.set()
            qmp._command("query-status")
            query_done.set()

        drag_thread = threading.Thread(target=run_drag, name="font15r2r1-drag-test")
        query_thread = threading.Thread(target=run_query, name="font15r2r1-query-test")
        drag_thread.start()
        try:
            self.assertTrue(sleep_entered.wait(1))
            query_thread.start()
            self.assertTrue(query_started.wait(1))
            self.assertFalse(query_done.wait(0.05))
        finally:
            allow_drag.set()
            drag_thread.join(timeout=2)
            if query_thread.ident is not None:
                query_thread.join(timeout=2)
            qmp.close()
        self.assertFalse(drag_thread.is_alive())
        self.assertFalse(query_thread.is_alive())
        self.assertEqual(len(drag_results), 1)
        self.assertTrue(drag_results[0].success)
        commands = [request["execute"] for request in self.sockets[0].requests]
        self.assertEqual(commands, ["qmp_capabilities"] + ["input-send-event"] * 4 + ["query-status"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
