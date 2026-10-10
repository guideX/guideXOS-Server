#!/usr/bin/env python3
"""Prepare and independently verify disposable DM34 raw disk images."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

SECTOR_SIZE = 512
TOTAL_SECTORS = 540_000
TARGET_START = 2_048
TARGET_SECTORS = 524_288
NEIGHBOR_START = 530_000
NEIGHBOR_SECTORS = 8_192
CHUNK_SIZE = 1024 * 1024


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def fill_range(stream, offset: int, length: int, byte_value: int) -> None:
    block = bytes([byte_value]) * CHUNK_SIZE
    stream.seek(offset)
    remaining = length
    while remaining:
        count = min(remaining, len(block))
        stream.write(block[:count])
        remaining -= count


def set_mbr_partition(mbr: bytearray, slot: int, part_type: int,
                      start_lba: int, sectors: int) -> None:
    offset = 446 + slot * 16
    mbr[offset] = 0
    mbr[offset + 1:offset + 4] = b"\xFE\xFF\xFF"
    mbr[offset + 4] = part_type
    mbr[offset + 5:offset + 8] = b"\xFE\xFF\xFF"
    mbr[offset + 8:offset + 12] = start_lba.to_bytes(4, "little")
    mbr[offset + 12:offset + 16] = sectors.to_bytes(4, "little")


def create_fixture(path: Path) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    image_bytes = TOTAL_SECTORS * SECTOR_SIZE
    with path.open("wb") as stream:
        stream.truncate(image_bytes)
        fill_range(stream, 0, image_bytes, 0x3C)
        fill_range(stream, TARGET_START * SECTOR_SIZE,
                   TARGET_SECTORS * SECTOR_SIZE, 0xA5)
        fill_range(stream, NEIGHBOR_START * SECTOR_SIZE,
                   NEIGHBOR_SECTORS * SECTOR_SIZE, 0x5A)
        fill_range(stream, SECTOR_SIZE, SECTOR_SIZE, 0xE1)
        fill_range(stream, (TARGET_START + TARGET_SECTORS) * SECTOR_SIZE,
                   SECTOR_SIZE * 32, 0xD2)
        fill_range(stream, (TOTAL_SECTORS - 32) * SECTOR_SIZE,
                   SECTOR_SIZE * 32, 0xB7)
        mbr = bytearray(SECTOR_SIZE)
        mbr[440:444] = b"\x34\x5E\x4A\xD3"
        set_mbr_partition(mbr, 0, 0x0C, TARGET_START, TARGET_SECTORS)
        set_mbr_partition(mbr, 1, 0x83, NEIGHBOR_START, NEIGHBOR_SECTORS)
        mbr[510:512] = b"\x55\xAA"
        stream.seek(0)
        stream.write(mbr)

    return {
        "image": path.name,
        "bytes": image_bytes,
        "sha256": sha256_file(path),
        "sectorSize": SECTOR_SIZE,
        "totalSectors": TOTAL_SECTORS,
        "partitionScheme": "MBR",
        "diskSignatureHex": "D34A5E34",
        "target": {
            "partitionNumber": 1,
            "type": "0x0C",
            "startLba": TARGET_START,
            "endLbaInclusive": TARGET_START + TARGET_SECTORS - 1,
            "sectors": TARGET_SECTORS,
            "bytes": TARGET_SECTORS * SECTOR_SIZE,
            "initialPatternHex": "A5",
            "sampleInitialBytesHex": "A5" * 32,
        },
        "neighbor": {
            "partitionNumber": 2,
            "type": "0x83",
            "startLba": NEIGHBOR_START,
            "endLbaInclusive": NEIGHBOR_START + NEIGHBOR_SECTORS - 1,
            "sectors": NEIGHBOR_SECTORS,
            "patternHex": "5A",
        },
        "preservedMarkers": [
            {"range": "LBA 0", "expectedHex": "MBR + disk signature + partition entries + 55AA"},
            {"range": "LBA 1", "patternHex": "E1"},
            {"range": "target end + 1 through target end + 32", "patternHex": "D2"},
            {"range": "neighbor partition", "patternHex": "5A"},
            {"range": "last 32 sectors", "patternHex": "B7"},
            {"range": "all other out-of-target bytes", "patternHex": "3C"},
        ],
    }


def prepare(directory: Path) -> int:
    directory.mkdir(parents=True, exist_ok=True)
    entries = []
    for name in ("stop-test.raw", "complete-test.raw"):
        image = directory / name
        pre = directory / f"{name}.pre.raw"
        entry = create_fixture(image)
        shutil.copyfile(image, pre)
        entry["preImage"] = pre.name
        entry["preImageSha256"] = sha256_file(pre)
        entries.append(entry)
    manifest = {
        "schema": "DM34-partition-wipe-fixture-v1",
        "images": entries,
        "notes": [
            "Disposable raw files; do not attach host physical drives.",
            "Only MBR Partition 1 is an eligible test target; retain boot/system media separately.",
            "The target starts with identifiable nonzero 0xA5 bytes.",
        ],
    }
    manifest_path = directory / "fixture-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"prepared {len(entries)} images; manifest={manifest_path}")
    for entry in entries:
        print(f"{entry['image']}: bytes={entry['bytes']} sha256={entry['sha256']} "
              f"target=LBA {TARGET_START}-{TARGET_START + TARGET_SECTORS - 1}")
    return 0


def compare_outside(pre: Path, post: Path, target_start: int,
                    target_end: int) -> tuple[bool, int, str | None]:
    total_bytes = pre.stat().st_size
    if post.stat().st_size != total_bytes:
        return False, 0, "image size changed"
    intervals = ((0, target_start), (target_end, total_bytes))
    checked = 0
    with pre.open("rb") as before, post.open("rb") as after:
        for begin, end in intervals:
            cursor = begin
            while cursor < end:
                count = min(CHUNK_SIZE, end - cursor)
                before.seek(cursor)
                after.seek(cursor)
                if before.read(count) != after.read(count):
                    return False, checked, f"outside-target content changed at byte {cursor}"
                checked += count
                cursor += count
    return True, checked, None


def verify(manifest_path: Path, image_path: Path, mode: str) -> int:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    image_entry = next((item for item in manifest["images"]
                        if item["image"] == image_path.name), None)
    if image_entry is None:
        raise ValueError(f"{image_path.name} is not listed in {manifest_path}")
    directory = manifest_path.parent
    pre = directory / image_entry["preImage"]
    sector_size = image_entry["sectorSize"]
    start = image_entry["target"]["startLba"] * sector_size
    end = (image_entry["target"]["endLbaInclusive"] + 1) * sector_size
    if sha256_file(pre) != image_entry["preImageSha256"]:
        raise ValueError("pre-image hash does not match the fixture manifest")
    if image_path.stat().st_size != image_entry["bytes"]:
        raise ValueError("post-image size does not match the fixture manifest")

    outside_ok, outside_bytes, outside_error = compare_outside(pre, image_path,
                                                               start, end)
    if not outside_ok:
        raise ValueError(outside_error)

    target_bytes = end - start
    zero_bytes = 0
    changed_bytes = 0
    with pre.open("rb") as before, image_path.open("rb") as after:
        cursor = start
        while cursor < end:
            count = min(CHUNK_SIZE, end - cursor)
            original = before.read(count) if before.tell() == cursor else None
            if original is None:
                before.seek(cursor)
                original = before.read(count)
            current = after.read(count) if after.tell() == cursor else None
            if current is None:
                after.seek(cursor)
                current = after.read(count)
            zero_bytes += current.count(0)
            changed_bytes += sum(a != b for a, b in zip(original, current))
            cursor += count

    if mode == "complete" and zero_bytes != target_bytes:
        raise ValueError(f"target is not fully zero: {zero_bytes}/{target_bytes} bytes")
    if mode == "stopped" and changed_bytes == 0:
        raise ValueError("stopped test shows no byte changes inside the target")

    result = {
        "mode": mode,
        "image": str(image_path),
        "preSha256": image_entry["preImageSha256"],
        "postSha256": sha256_file(image_path),
        "targetRangeLbaInclusive": [
            image_entry["target"]["startLba"],
            image_entry["target"]["endLbaInclusive"],
        ],
        "targetBytes": target_bytes,
        "zeroBytesInTarget": zero_bytes,
        "changedBytesInTarget": changed_bytes,
        "outsideTargetBytesCompared": outside_bytes,
        "outsideTargetPreserved": True,
        "partitionTablePreserved": True,
        "neighborAndGuardsPreserved": True,
        "result": "PASS",
    }
    print(json.dumps(result, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--directory", type=Path, required=True)
    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--manifest", type=Path, required=True)
    verify_parser.add_argument("--image", type=Path, required=True)
    verify_parser.add_argument("--mode", choices=("complete", "stopped"), required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            return prepare(args.directory)
        return verify(args.manifest, args.image, args.mode)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"DM34 image verification error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
