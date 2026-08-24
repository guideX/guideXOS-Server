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


def write_png(path: Path, rgba: tuple[int, int, int, int], width: int = WIDTH, height: int = HEIGHT) -> None:
    pixel = bytes(rgba)
    row = b"\x00" + pixel * width
    raw = row * height
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
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

    # Phase 8T: far resources deliberately appear first in source order but
    # are absolutely positioned below the initial content viewport.  Smaller
    # visible/near images follow them, giving the scheduler a real geometry
    # competition without changing the fixed 64 MiB policy.
    viewport_colors = (
        (24, 96, 156, 255),
        (42, 132, 88, 255),
        (162, 92, 34, 255),
        (118, 62, 148, 255),
    )
    for index, color in enumerate(viewport_colors, 1):
        write_png(output_dir / f"viewport-visible-{index:02d}.png", color, 1024, 1024)
        write_png(output_dir / f"viewport-near-{index:02d}.png", color, 1024, 1024)
        write_png(output_dir / f"viewport-far-{index:02d}.png", color, WIDTH, HEIGHT)
        write_png(output_dir / f"VPV{index:02d}.PNG", color, 1024, 1024)
        write_png(output_dir / f"VPN{index:02d}.PNG", color, 1024, 1024)
        write_png(output_dir / f"VPF{index:02d}.PNG", color, WIDTH, HEIGHT)
    viewport_css = "".join(
        f".visible-pressure-{index:02d}{{position:absolute;top:{120 + (index - 1) * 90}px;left:{24 + (index - 1) * 150}px;}}"
        for index in range(1, 5)
    ) + "".join(
        f".far-pressure-{index:02d}{{position:absolute;top:{2400 + (index - 1) * 140}px;left:24px;}}"
        for index in range(1, 5)
    ) + "".join(
        f".near-pressure-{index:02d}{{position:absolute;top:{700 + (index - 1) * 100}px;left:180px;}}"
        for index in range(1, 5)
    )
    far_tags = "".join(
        f'<img class="far-pressure-{index:02d}" src="VPF{index:02d}.PNG" width="128" height="128" '
        f'alt="far pressure image {index}">' for index in range(1, 5)
    )
    visible_tags = "".join(
        f'<img class="visible-pressure-{index:02d}" src="VPV{index:02d}.PNG" width="128" height="96" '
        f'alt="visible pressure image {index}">' for index in range(1, 5)
    )
    near_tags = "".join(
        f'<img class="near-pressure-{index:02d}" src="VPN{index:02d}.PNG" width="128" height="96" '
        f'alt="near pressure image {index}">' for index in range(1, 5)
    )
    viewport = (
        "<!doctype html><html><head><title>Navigator Viewport Pressure Fixture</title>"
        f"<style>{viewport_css}</style></head>"
        "<body><h1>Viewport-aware image admission</h1>"
        "<p>Four far 2048px images precede smaller visible and near images in source order.</p>"
        + far_tags + visible_tags + near_tags
        + "</body></html>"
    )
    (output_dir / "viewport-pressure.html").write_text(viewport, encoding="ascii", newline="\n")
    (output_dir / "VPRESS.HTM").write_text(viewport, encoding="ascii", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    write_pages(args.output_dir)
    print(f"generated Navigator persistent fixtures in {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
