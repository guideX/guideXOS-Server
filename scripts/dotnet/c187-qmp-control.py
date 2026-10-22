#!/usr/bin/env python3
"""Send real QMP input and capture output for a running C187 guest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import struct
import sys
import time
import zlib


def read_response(reader, timeout=10.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        line = reader.readline()
        if not line:
            raise RuntimeError("QMP closed the connection")
        value = json.loads(line)
        if "event" in value:
            continue
        if "return" in value or "error" in value:
            if "error" in value:
                raise RuntimeError(f"QMP rejected request: {value['error']}")
            return value
    raise TimeoutError("Timed out waiting for a QMP response")


class Qmp:
    def __init__(self, port: int, log: Path) -> None:
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(10)
        self.reader = self.sock.makefile("r", encoding="ascii", newline="\n")
        self.writer = self.sock.makefile("w", encoding="ascii", newline="\n")
        self.log = log
        greeting = self.reader.readline().rstrip()
        self.call("qmp_capabilities")
        with self.log.open("a", encoding="utf-8") as stream:
            stream.write(f"greeting={greeting}\n")

    def call(self, command: str, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.writer.write(json.dumps(request, separators=(",", ":")) + "\n")
        self.writer.flush()
        result = read_response(self.reader)
        with self.log.open("a", encoding="utf-8") as stream:
            stream.write(f"request={json.dumps(request, separators=(',', ':'))}\nresponse={json.dumps(result, separators=(',', ':'))}\n")
        return result

    def event(self, event):
        self.call("input-send-event", {"events": [event]})

    def close(self):
        self.reader.close()
        self.writer.close()
        self.sock.close()


def get_context(active_path: Path):
    active = json.loads(active_path.read_text(encoding="utf-8-sig"))
    port = int(active["qmpPort"])
    width = int(active["screenWidth"])
    height = int(active["screenHeight"])
    if width < 2 or height < 2:
        raise RuntimeError("active.json does not contain valid guest framebuffer dimensions")
    state_path = active_path.with_name("qmp-control-state.json")
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding="utf-8-sig"))
        cursor = state.get("cursor")
    else:
        cursor = None
    return port, width, height, state_path, cursor


def send(qmp: Qmp, event):
    qmp.event(event)


def move(qmp: Qmp, cursor: list[int] | None, x: int, y: int, width: int, height: int):
    if not 0 <= x < width or not 0 <= y < height:
        raise ValueError(f"Pointer target ({x},{y}) is outside the {width}x{height} framebuffer")
    if cursor is None:
        # The standard QEMU VGA relative device starts at the framebuffer center.
        send(qmp, {"type": "rel", "data": {"axis": "x", "value": 1}})
        send(qmp, {"type": "rel", "data": {"axis": "y", "value": 1}})
        cursor = [min(width - 1, width // 2 + 1), min(height - 1, height // 2 + 1)]
    dx, dy = x - cursor[0], y - cursor[1]
    for axis, distance in (("x", dx), ("y", dy)):
        while distance:
            step = (1 if distance > 0 else -1) * min(abs(distance), 40)
            send(qmp, {"type": "rel", "data": {"axis": axis, "value": step}})
            distance -= step
    return [x, y]


def click(qmp: Qmp, cursor, x, y, width, height):
    cursor = move(qmp, cursor, x, y, width, height)
    send(qmp, {"type": "btn", "data": {"button": "left", "down": True}})
    time.sleep(0.12)
    send(qmp, {"type": "btn", "data": {"button": "left", "down": False}})
    time.sleep(0.35)
    return cursor


def key(qmp: Qmp, name: str):
    aliases = {"enter": "ret", "return": "ret", "escape": "esc", "spacebar": "spc", "space": "spc", "tab": "tab"}
    qcode = aliases.get(name.casefold(), name.casefold())
    event_key = {"type": "qcode", "data": qcode}
    send(qmp, {"type": "key", "data": {"down": True, "key": event_key}})
    time.sleep(0.05)
    send(qmp, {"type": "key", "data": {"down": False, "key": event_key}})
    time.sleep(0.3)


def ppm_to_png(ppm: Path, png: Path) -> None:
    raw = ppm.read_bytes()
    if not raw.startswith(b"P6"):
        raise RuntimeError(f"QEMU screenshot is not a binary PPM: {ppm}")
    index = 2

    def token():
        nonlocal index
        while index < len(raw):
            if raw[index] in b" \t\r\n":
                index += 1
            elif raw[index] == ord("#"):
                while index < len(raw) and raw[index] not in b"\r\n":
                    index += 1
            else:
                break
        start = index
        while index < len(raw) and raw[index] not in b" \t\r\n#":
            index += 1
        if start == index:
            raise RuntimeError("Malformed PPM header")
        return raw[start:index]

    width = int(token())
    height = int(token())
    maximum = int(token())
    if maximum != 255:
        raise RuntimeError(f"Unsupported PPM maximum channel value {maximum}")
    while index < len(raw) and raw[index] in b" \t\r\n":
        index += 1
    pixels = raw[index:]
    if width < 1 or height < 1 or len(pixels) != width * height * 3:
        raise RuntimeError(f"Malformed PPM pixel data: {width}x{height}, {len(pixels)} bytes")

    def chunk(kind: bytes, content: bytes) -> bytes:
        payload = kind + content
        return struct.pack(">I", len(content)) + payload + struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF)

    scanlines = b"".join(b"\x00" + pixels[row * width * 3 : (row + 1) * width * 3] for row in range(height))
    image = b"\x89PNG\r\n\x1a\n"
    image += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    image += chunk(b"IDAT", zlib.compress(scanlines, 6))
    image += chunk(b"IEND", b"")
    png.write_bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--active", required=True, type=Path, help="active.json written by run-c187-qac-guest.ps1")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("status")
    capture = sub.add_parser("capture")
    capture.add_argument("name")
    click_cmd = sub.add_parser("click")
    click_cmd.add_argument("x", type=int)
    click_cmd.add_argument("y", type=int)
    move_cmd = sub.add_parser("move")
    move_cmd.add_argument("x", type=int)
    move_cmd.add_argument("y", type=int)
    key_cmd = sub.add_parser("key")
    key_cmd.add_argument("key")
    drag = sub.add_parser("drag")
    for coordinate in ("x0", "y0", "x1", "y1"):
        drag.add_argument(coordinate, type=int)
    launch = sub.add_parser("launch")
    launch.add_argument("app", choices=("notes", "settings", "calculator", "taskmanager", "explorer"))
    sub.add_parser("quit")
    args = parser.parse_args()

    active_path = args.active.resolve(strict=True)
    port, width, height, state_path, cursor = get_context(active_path)
    log = active_path.with_name("qmp-control.log")
    qmp = Qmp(port, log)
    try:
        if args.action == "status":
            print(json.dumps(qmp.call("query-status"), sort_keys=True))
        elif args.action == "capture":
            ppm = active_path.parent / f"{args.name}.ppm"
            png = active_path.parent / f"{args.name}.png"
            qmp.call("screendump", {"filename": str(ppm)})
            ppm_to_png(ppm, png)
            print(png)
        elif args.action == "click":
            cursor = click(qmp, cursor, args.x, args.y, width, height)
        elif args.action == "move":
            cursor = move(qmp, cursor, args.x, args.y, width, height)
        elif args.action == "key":
            key(qmp, args.key)
        elif args.action == "drag":
            cursor = move(qmp, cursor, args.x0, args.y0, width, height)
            send(qmp, {"type": "btn", "data": {"button": "left", "down": True}})
            time.sleep(0.1)
            for axis, distance in (("x", args.x1 - args.x0), ("y", args.y1 - args.y0)):
                while distance:
                    step = (1 if distance > 0 else -1) * min(abs(distance), 40)
                    send(qmp, {"type": "rel", "data": {"axis": axis, "value": step}})
                    distance -= step
                    time.sleep(0.04)
            send(qmp, {"type": "btn", "data": {"button": "left", "down": False}})
            cursor = [args.x1, args.y1]
            time.sleep(0.4)
        elif args.action == "launch":
            menu_count = 22
            menu_height = 30 + menu_count * 22 + 36
            work_height = height - 40
            menu_y = max(0, work_height - menu_height)
            footer_y = menu_y + 30 + menu_count * 22
            cursor = click(qmp, cursor, 50, height - 20, width, height)
            cursor = click(qmp, cursor, 70, footer_y + 18, width, height)
            names = {"notes": (115, 12), "calculator": (115, 11), "taskmanager": (115, 14), "explorer": (115, 15), "settings": (310, 6)}
            x, row = names[args.app]
            content_y = menu_y + 31
            cursor = click(qmp, cursor, x, content_y + row * 22 + 11, width, height)
        elif args.action == "quit":
            qmp.call("quit")
        if args.action not in ("status", "capture", "quit"):
            state_path.write_text(json.dumps({"cursor": cursor, "framebuffer": {"width": width, "height": height}}, indent=2) + "\n", encoding="utf-8")
    finally:
        qmp.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # noqa: BLE001
        print(f"C187 QMP control failed: {error}", file=sys.stderr)
        raise SystemExit(1)
