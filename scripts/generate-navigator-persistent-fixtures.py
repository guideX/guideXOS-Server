#!/usr/bin/env python3
"""Create deterministic, compact image-pressure pages for Navigator smoke."""

from __future__ import annotations

import argparse
import binascii
import struct
import zlib
from pathlib import Path


WIDTH = 2048
HEIGHT = 2048


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)


def write_png(path: Path, rgba: tuple[int, int, int, int]) -> None:
    pixel = bytes(rgba)
    row = b"\x00" + pixel * WIDTH
    raw = row * HEIGHT
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(raw, 9))
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)


def write_pages(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    colors = (
        (24, 78, 119, 255),
        (35, 120, 84, 255),
        (145, 84, 34, 255),
        (116, 54, 132, 255),
        (170, 52, 56, 255),
    )
    for index, color in enumerate(colors, 1):
        write_png(output_dir / f"persistent-heavy-{index:02d}.png", color)

    image_tags = []
    for index in range(40):
        image_number = index % 5 + 1
        image_tags.append(
            f'<img src="HEAVY{image_number:02d}.PNG" '
            f'width="128" height="64" alt="pressure image {image_number}">' 
        )
    heavy = (
        "<!doctype html><html><head><title>Navigator Persistent Heavy Fixture</title></head>"
        "<body><h1>Persistent image pressure</h1>"
        "<p>Five unique 2048x2048 RGBA PNGs, repeated deterministically forty times.</p>"
        + "".join(image_tags)
        + "</body></html>"
    )
    tiny = (
        "<!doctype html><html><head><title>Navigator Persistent Tiny Fixture</title></head>"
        "<body><h1>Persistent tiny control</h1><p>No image resources remain in this document.</p>"
        "</body></html>"
    )
    failure = (
        "<!doctype html><html><head><title>Navigator Persistent Failure Fixture</title></head>"
        "<body><h1>Injected image failure</h1><p>The first image allocation is deliberately failed.</p>"
        '<img src="HEAVY01.PNG" width="128" height="64" alt="injected failure">'
        "</body></html>"
    )
    # Keep the descriptive long names for the host-side fixture copy, and add
    # strict 8.3 aliases for the generated FAT ramdisk.  The bare-metal FAT
    # alias exposes these short names consistently even when long-name entries
    # are truncated by the fixed directory walker.
    for index, color in enumerate(colors, 1):
        write_png(output_dir / f"HEAVY{index:02d}.PNG", color)
    (output_dir / "persistent-heavy.html").write_text(heavy, encoding="ascii", newline="\n")
    (output_dir / "persistent-tiny.html").write_text(tiny, encoding="ascii", newline="\n")
    (output_dir / "persistent-failure.html").write_text(failure, encoding="ascii", newline="\n")
    (output_dir / "HEAVY.HTM").write_text(heavy, encoding="ascii", newline="\n")
    (output_dir / "TINY.HTM").write_text(tiny, encoding="ascii", newline="\n")
    (output_dir / "FAIL.HTM").write_text(failure, encoding="ascii", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    write_pages(args.output_dir)
    print(f"generated Navigator persistent fixtures in {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
