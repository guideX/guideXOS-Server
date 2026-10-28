#!/usr/bin/env python3
"""Locate the kernel's exact black-and-white cursor in a QEMU P6 screenshot."""

from __future__ import annotations

import json
from pathlib import Path
import sys


# Keep in sync with kernel/core/desktop.cpp:s_cursorBitmap (12x19).
CURSOR = (
    (1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    (1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    (1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    (1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0),
    (1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0),
    (1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0),
    (1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0),
    (1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0),
    (1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0),
    (1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0),
    (1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0),
    (1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0),
    (1, 2, 2, 2, 1, 2, 1, 0, 0, 0, 0, 0),
    (1, 2, 2, 1, 0, 1, 2, 1, 0, 0, 0, 0),
    (1, 2, 1, 0, 0, 1, 2, 1, 0, 0, 0, 0),
    (1, 1, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0),
    (1, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0),
    (0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0),
    (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
)


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    cursor = 0

    def token() -> bytes:
        nonlocal cursor
        while cursor < len(data):
            if data[cursor] in b" \t\r\n":
                cursor += 1
            elif data[cursor] == ord("#"):
                while cursor < len(data) and data[cursor] not in b"\r\n":
                    cursor += 1
            else:
                break
        start = cursor
        while cursor < len(data) and data[cursor] not in b" \t\r\n#":
            cursor += 1
        if start == cursor:
            raise ValueError("malformed PPM header")
        return data[start:cursor]

    if token() != b"P6":
        raise ValueError("expected a binary P6 PPM screenshot")
    width, height, maximum = int(token()), int(token()), int(token())
    if maximum != 255:
        raise ValueError(f"unsupported PPM channel range: {maximum}")
    if cursor >= len(data) or data[cursor] not in b" \t\r\n":
        raise ValueError("missing PPM pixel separator")
    if data[cursor:cursor + 2] == b"\r\n":
        cursor += 2
    else:
        cursor += 1
    pixels = data[cursor:]
    if width < 12 or height < 19 or len(pixels) != width * height * 3:
        raise ValueError("invalid PPM dimensions or pixel payload")
    return width, height, pixels


def find_cursor(path: Path, target_x: int, target_y: int, radius: int) -> dict[str, object]:
    width, height, pixels = read_ppm(path)
    min_x, max_x = max(0, target_x - radius), min(width - 12, target_x + radius)
    min_y, max_y = max(0, target_y - radius), min(height - 19, target_y + radius)
    found: list[tuple[int, int]] = []

    # Two distinctive source pixels cheaply reject almost all locations.
    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            black = (y * width + x) * 3
            white = ((y + 2) * width + x + 1) * 3
            if pixels[black:black + 3] != b"\x00\x00\x00":
                continue
            if pixels[white:white + 3] != b"\xff\xff\xff":
                continue
            matches = True
            for row, bitmap_row in enumerate(CURSOR):
                for column, value in enumerate(bitmap_row):
                    if value == 0:
                        continue
                    index = ((y + row) * width + x + column) * 3
                    expected = b"\x00\x00\x00" if value == 1 else b"\xff\xff\xff"
                    if pixels[index:index + 3] != expected:
                        matches = False
                        break
                if not matches:
                    break
            if matches:
                found.append((x, y))

    unique = found[0] if len(found) == 1 else None
    return {
        "found": unique is not None,
        "matches": len(found),
        "x": unique[0] if unique else None,
        "y": unique[1] if unique else None,
        "targetX": target_x,
        "targetY": target_y,
        "searchRadius": radius,
        "framebufferWidth": width,
        "framebufferHeight": height,
    }


def main() -> int:
    if len(sys.argv) not in (4, 5):
        raise SystemExit("usage: find-c188r12r1-cursor.py SCREEN.ppm TARGET_X TARGET_Y [RADIUS]")
    result = find_cursor(Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]),
                         int(sys.argv[4]) if len(sys.argv) == 5 else 128)
    print(json.dumps(result, separators=(",", ":")))
    return 0 if result["found"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # noqa: BLE001
        print(f"C188R12R1 cursor detection failed: {error}", file=sys.stderr)
        raise SystemExit(2)
