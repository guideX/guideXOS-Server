import argparse
import json
import socket
import time
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True, type=int)
parser.add_argument("--cursor", nargs=2, type=int, default=(640, 400))
parser.add_argument("--move-to", nargs=2, type=int)
parser.add_argument("--click", nargs=2, type=int)
parser.add_argument("--double-click", nargs=2, type=int)
parser.add_argument("--drag", nargs=4, type=int)
parser.add_argument("--key")
parser.add_argument("--combo", nargs="+")
parser.add_argument("--type-text")
parser.add_argument("--screen")
parser.add_argument("--trace")
parser.add_argument("--label", default="")
parser.add_argument("--query", action="store_true")
parser.add_argument("--powerdown", action="store_true")
args = parser.parse_args()

sock = socket.create_connection(("127.0.0.1", args.port), timeout=10)
stream = sock.makefile("rwb", buffering=0)
greeting = json.loads(stream.readline())
if "QMP" not in greeting:
    raise RuntimeError("unexpected QMP greeting")
sequence = 0
trace = []


def command(name, arguments=None):
    global sequence
    sequence += 1
    request = {"execute": name, "id": sequence}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\r\n").encode("utf-8"))
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError("QMP closed while waiting for " + name)
        result = json.loads(line)
        if result.get("id") != sequence:
            continue
        if "error" in result:
            raise RuntimeError(json.dumps(result["error"]))
        trace.append({"command": name, "arguments": arguments, "return": result.get("return")})
        return result.get("return")


command("qmp_capabilities")
if args.query:
    trace.append({"query_mice": command("query-mice")})
    trace.append({"query_status": command("query-status")})
if args.powerdown:
    trace.append({"acpi_powerdown_request": command("system_powerdown")})

cursor_x, cursor_y = args.cursor


def rel(dx, dy):
    events = []
    if dx:
        events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
    if dy:
        events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
    if events:
        command("input-send-event", {"events": events})
        time.sleep(0.06)


def move_to(target_x, target_y):
    global cursor_x, cursor_y
    dx = target_x - cursor_x
    dy = target_y - cursor_y
    while dx or dy:
        sx = max(-35, min(35, dx))
        sy = max(-35, min(35, dy))
        rel(sx, sy)
        cursor_x += sx
        cursor_y += sy
        dx -= sx
        dy -= sy
    trace.append({"ps2_relative_move_to": [target_x, target_y]})


def button(down):
    command("input-send-event", {"events": [
        {"type": "btn", "data": {"down": down, "button": "left"}}
    ]})
    time.sleep(0.12)


if args.move_to:
    move_to(*args.move_to)
if args.click or args.double_click:
    click_at = args.click if args.click else args.double_click
    move_to(*click_at)
    time.sleep(0.22)
    button(True)
    button(False)
    if args.double_click:
        time.sleep(0.14)
        button(True)
        button(False)
        trace.append({"ps2_left_double_click_at": list(args.double_click)})
    else:
        trace.append({"ps2_left_click_at": list(args.click)})
if args.drag:
    start_x, start_y, end_x, end_y = args.drag
    move_to(start_x, start_y)
    time.sleep(0.22)
    button(True)
    move_to(end_x, end_y)
    button(False)
    trace.append({"ps2_left_drag": [start_x, start_y, end_x, end_y]})
if args.key:
    command("send-key", {"keys": [{"type": "qcode", "data": args.key.lower()}], "hold-time": 120})
    trace.append({"qemu_key": args.key})
if args.combo:
    keys = [{"type": "qcode", "data": key.lower()} for key in args.combo]
    command("send-key", {"keys": keys, "hold-time": 180})
    trace.append({"qemu_key_combo": args.combo})
if args.type_text is not None:
    for character in args.type_text:
        if character in "\r\n":
            qcode = "ret"
        elif character == " ":
            qcode = "spc"
        elif character == ".":
            qcode = "dot"
        elif character.isalnum() and character.isascii():
            qcode = character.lower()
        else:
            raise RuntimeError("unsupported text character: " + repr(character))
        keys = [{"type": "qcode", "data": qcode}]
        if character.isupper():
            keys.insert(0, {"type": "qcode", "data": "shift"})
        command("send-key", {"keys": keys, "hold-time": 120})
        trace.append({"qemu_key": qcode, "typed": character})
        time.sleep(0.14)
if args.screen:
    time.sleep(0.3)
    command("screendump", {"filename": str(Path(args.screen).resolve()).replace("\\", "/"), "format": "ppm"})

if args.trace:
    record = {"label": args.label, "observedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "initialCursor": list(args.cursor), "actions": trace}
    with Path(args.trace).open("a", encoding="utf-8") as output:
        output.write(json.dumps(record) + "\n")

print(json.dumps({"label": args.label, "actions": trace}, indent=2))
stream.close()
sock.close()
