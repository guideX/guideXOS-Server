"""Reliable, paced QMP input for headless guideXOS desktop qualification.

QMP may acknowledge an ``input-send-event`` without the guest processing a
large batch.  Keep PS/2 relative axes and key/button transitions separate,
and leave QEMU's display-device routing argument unset unless a later
qualification proves that an explicit route is required.
"""

from __future__ import annotations

import time
from collections.abc import Callable, Sequence
from typing import Any, Protocol


MAX_PS2_DELTA = 100


class QmpEventSender(Protocol):
    def send_events(self, events: list[dict[str, Any]]) -> Any:
        """Send one or more QMP input events without a display override."""


def _key_event(qcode: str, down: bool) -> dict[str, Any]:
    return {
        "type": "key",
        "data": {
            "down": down,
            "key": {"type": "qcode", "data": qcode},
        },
    }


class QmpGuiInput:
    """Send guest input as individually acknowledged, paced QMP events."""

    def __init__(
        self,
        qmp: QmpEventSender,
        *,
        mouse_delay_seconds: float = 0.2,
        key_delay_seconds: float = 0.08,
        button_delay_seconds: float = 0.15,
        sleeper: Callable[[float], None] = time.sleep,
    ) -> None:
        if min(mouse_delay_seconds, key_delay_seconds, button_delay_seconds) < 0:
            raise ValueError("input delays must be nonnegative")
        self._qmp = qmp
        self._mouse_delay = mouse_delay_seconds
        self._key_delay = key_delay_seconds
        self._button_delay = button_delay_seconds
        self._sleep = sleeper

    def _send(self, event: dict[str, Any], delay: float) -> Any:
        # Do not pass a display-device override: the qualified candidate has
        # the implicit PS/2 devices, and QMP query-mice identifies mouse #2.
        response = self._qmp.send_events([event])
        self._sleep(delay)
        return response

    def move_relative(self, dx: int, dy: int) -> list[Any]:
        """Move by relative QMP deltas, splitting each axis into safe steps."""
        responses: list[Any] = []
        for axis, remaining in (("x", dx), ("y", dy)):
            while remaining:
                step = max(-MAX_PS2_DELTA, min(MAX_PS2_DELTA, remaining))
                event = {"type": "rel", "data": {"axis": axis, "value": step}}
                responses.append(self._send(event, self._mouse_delay))
                remaining -= step
        return responses

    def press(self, qcode: str, modifiers: Sequence[str] = ()) -> list[Any]:
        """Press and release one QEMU qcode, pacing each transition."""
        held: list[str] = []
        responses: list[Any] = []
        try:
            for modifier in modifiers:
                responses.append(self._send(_key_event(modifier, True), self._key_delay))
                held.append(modifier)
            responses.append(self._send(_key_event(qcode, True), self._key_delay))
            held.append(qcode)
        finally:
            for code in reversed(held):
                responses.append(self._send(_key_event(code, False), self._key_delay))
        return responses

    def type_text(self, text: str) -> list[Any]:
        """Type ASCII letters, digits, and spaces with a release per key."""
        if any(
            char != " "
            and not ("a" <= char <= "z")
            and not ("A" <= char <= "Z")
            and not ("0" <= char <= "9")
            for char in text
        ):
            unsupported = next(
                char
                for char in text
                if char != " "
                and not ("a" <= char <= "z")
                and not ("A" <= char <= "Z")
                and not ("0" <= char <= "9")
            )
            raise ValueError(f"unsupported QMP text character: {unsupported!r}")
        responses: list[Any] = []
        for char in text:
            if char == " ":
                qcode, modifiers = "spc", ()
            elif "a" <= char <= "z":
                qcode, modifiers = char, ()
            elif "A" <= char <= "Z":
                qcode, modifiers = char.lower(), ("shift",)
            elif "0" <= char <= "9":
                qcode, modifiers = char, ()
            else:
                raise ValueError(f"unsupported QMP text character: {char!r}")
            responses.extend(self.press(qcode, modifiers))
        return responses

    def click(self, button: str = "left") -> list[Any]:
        """Click with independently delivered and paced button transitions."""
        down = {"type": "btn", "data": {"button": button, "down": True}}
        up = {"type": "btn", "data": {"button": button, "down": False}}
        return [
            self._send(down, self._button_delay),
            self._send(up, self._button_delay),
        ]
