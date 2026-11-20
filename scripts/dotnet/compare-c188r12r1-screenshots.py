#!/usr/bin/env python3
"""Compare two binary P6 screenshots using only the Python standard library."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    cursor = 0

    def token() -> bytes:
        nonlocal cursor
        while cursor < len(data):
            if data[cursor] == ord("#"):
                while cursor < len(data) and data[cursor] not in b"\r\n":
                    cursor += 1
            elif data[cursor] in b" \t\r\n":
                cursor += 1
            else:
                break
        start = cursor
        while cursor < len(data) and data[cursor] not in b" \t\r\n#":
            cursor += 1
        if start == cursor:
            raise ValueError(f"invalid PPM header: {path}")
        return data[start:cursor]

    if token() != b"P6":
        raise ValueError(f"not a binary P6 PPM: {path}")
    width = int(token())
    height = int(token())
    maximum = int(token())
    if maximum != 255:
        raise ValueError(f"unsupported PPM channel range {maximum}: {path}")
    if cursor >= len(data) or data[cursor] not in b" \t\r\n":
        raise ValueError(f"missing PPM data separator: {path}")
    if data[cursor:cursor + 2] == b"\r\n":
        cursor += 2
    else:
        cursor += 1
    pixels = data[cursor:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"pixel payload length {len(pixels)} != {expected}: {path}")
    return width, height, pixels


def verify_settings_target(
    reference_path: Path,
    live_path: Path,
    target_x: int,
    target_y: int,
    button_width: int = 90,
    button_height: int = 28,
) -> int:
    reference_w, reference_h, reference = read_ppm(reference_path)
    live_w, live_h, live = read_ppm(live_path)
    if (reference_w, reference_h) != (live_w, live_h):
        raise ValueError("reference and live screenshots have different dimensions")
    left = target_x - button_width // 2
    top = target_y - button_height // 2
    right = left + button_width
    bottom = top + button_height
    if left < 0 or top < 0 or right > live_w or bottom > live_h:
        raise ValueError("Settings target rectangle is outside the screenshot")

    matching_pixels = 0
    for y in range(top, bottom):
        start = (y * live_w + left) * 3
        end = start + button_width * 3
        for offset in range(start, end, 3):
            if reference[offset:offset + 3] == live[offset:offset + 3]:
                matching_pixels += 1

    total_pixels = button_width * button_height
    accepted = matching_pixels == total_pixels
    result = {
        "result": "PASS_TARGET_VISIBLE_UNOBSTRUCTED" if accepted else "FAIL_TARGET_MISMATCH",
        "targetCenter": {"x": target_x, "y": target_y},
        "targetBounds": {"left": left, "top": top, "right": right, "bottom": bottom},
        "framebuffer": {"width": live_w, "height": live_h},
        "matchingPixels": matching_pixels,
        "totalPixels": total_pixels,
        "referenceSha256": hashlib.sha256(reference_path.read_bytes()).hexdigest().upper(),
        "liveSha256": hashlib.sha256(live_path.read_bytes()).hexdigest().upper(),
    }
    print(json.dumps(result, separators=(",", ":")))
    return 0 if accepted else 1


def main() -> int:
    if len(sys.argv) == 6 and sys.argv[1] == "--verify-settings-target":
        return verify_settings_target(
            Path(sys.argv[2]),
            Path(sys.argv[3]),
            int(sys.argv[4]),
            int(sys.argv[5]),
        )
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: compare-c188r12r1-screenshots.py BEFORE.ppm AFTER.ppm\n"
            "       compare-c188r12r1-screenshots.py --verify-settings-target REFERENCE.ppm LIVE.ppm CENTER_X CENTER_Y"
        )
    before_path, after_path = map(Path, sys.argv[1:])
    before_w, before_h, before = read_ppm(before_path)
    after_w, after_h, after = read_ppm(after_path)
    if (before_w, before_h) != (after_w, after_h):
        raise ValueError("screenshots have different dimensions")
    changed_pixels = 0
    changed_channels = 0
    absolute_delta = 0
    for index in range(0, len(before), 3):
        pixel_changed = False
        for channel in range(3):
            difference = abs(before[index + channel] - after[index + channel])
            if difference:
                pixel_changed = True
                changed_channels += 1
                absolute_delta += difference
        if pixel_changed:
            changed_pixels += 1
    pixel_count = before_w * before_h
    result = {
        "width": before_w,
        "height": before_h,
        "pixelCount": pixel_count,
        "changedPixels": changed_pixels,
        "changedPixelPercent": round(100.0 * changed_pixels / pixel_count, 4),
        "changedChannelBytes": changed_channels,
        "meanAbsoluteChannelDelta": round(absolute_delta / (pixel_count * 3), 6),
        "beforeSha256": hashlib.sha256(before_path.read_bytes()).hexdigest().upper(),
        "afterSha256": hashlib.sha256(after_path.read_bytes()).hexdigest().upper(),
        "different": before != after,
    }
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
