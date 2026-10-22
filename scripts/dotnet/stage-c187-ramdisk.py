#!/usr/bin/env python3
"""Replace GXOSAPP.ELF in one isolated guideXOS FAT32 ramdisk copy.

The guideXOS FAT image uses a custom media descriptor that general-purpose FAT
libraries reject. This narrowly scoped writer edits the existing file's
allocation and directory size without changing the FAT geometry or boot sector.
It extends the file chain only through verified free clusters when needed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


TARGET_PATH = "/APPS/GXOSAPP.ELF"
EXPECTED_OEM_ID = b"GUIDEXOS"
CUSTOM_MEDIA_DESCRIPTOR = 0x00
FAT_MASK = 0x0FFFFFFF
FAT_EOC = 0x0FFFFFF8
FAT_BAD = 0x0FFFFFF7


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest().upper()


def assert_within(path: Path, root: Path, label: str) -> None:
    try:
        path.resolve(strict=False).relative_to(root.resolve(strict=True))
    except (OSError, ValueError) as error:
        raise RuntimeError(f"{label} must be below the isolated C187 candidate root") from error


class GuideXosFat32:
    def __init__(self, image: Path) -> None:
        self.image = image
        self.data = bytearray(image.read_bytes())
        if len(self.data) < 512:
            raise RuntimeError("Ramdisk is shorter than one boot sector")
        boot = self.data[:512]
        if boot[510:512] != b"\x55\xaa" or boot[3:11] != EXPECTED_OEM_ID:
            raise RuntimeError("Ramdisk does not have the expected guideXOS FAT boot sector")
        if boot[21] != CUSTOM_MEDIA_DESCRIPTOR:
            raise RuntimeError(f"Expected custom media descriptor 0x00, found 0x{boot[21]:02X}")
        self.bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
        self.sectors_per_cluster = boot[13]
        self.reserved_sectors = struct.unpack_from("<H", boot, 14)[0]
        self.fat_count = boot[16]
        self.fat_sectors = struct.unpack_from("<I", boot, 36)[0]
        self.total_sectors = struct.unpack_from("<I", boot, 32)[0]
        self.root_cluster = struct.unpack_from("<I", boot, 44)[0]
        if self.bytes_per_sector != 512 or not self.sectors_per_cluster:
            raise RuntimeError("Unsupported guideXOS FAT geometry")
        self.cluster_bytes = self.bytes_per_sector * self.sectors_per_cluster
        self.fat_offset = self.reserved_sectors * self.bytes_per_sector
        self.data_sector = self.reserved_sectors + self.fat_count * self.fat_sectors

    def cluster_offset(self, cluster: int) -> int:
        if cluster < 2:
            raise RuntimeError(f"Invalid FAT32 cluster {cluster}")
        offset = (self.data_sector + (cluster - 2) * self.sectors_per_cluster) * self.bytes_per_sector
        if offset + self.cluster_bytes > len(self.data):
            raise RuntimeError(f"FAT32 cluster {cluster} points outside the image")
        return offset

    def fat_next(self, cluster: int) -> int:
        offset = self.fat_offset + cluster * 4
        if offset + 4 > self.fat_offset + self.fat_sectors * self.bytes_per_sector:
            raise RuntimeError(f"FAT entry for cluster {cluster} is outside the FAT")
        return struct.unpack_from("<I", self.data, offset)[0] & FAT_MASK

    def set_fat_next(self, cluster: int, value: int) -> None:
        for copy in range(self.fat_count):
            offset = (self.reserved_sectors + copy * self.fat_sectors) * self.bytes_per_sector + cluster * 4
            struct.pack_into("<I", self.data, offset, value)

    def allocate_free_clusters(self, count: int, existing: set[int]) -> list[int]:
        max_cluster = 2 + (self.total_sectors - self.data_sector) // self.sectors_per_cluster
        selected: list[int] = []
        for cluster in range(2, max_cluster):
            if cluster in existing or self.fat_next(cluster) != 0:
                continue
            # FAT mirrors must agree before a free cluster is claimed.
            for copy in range(1, self.fat_count):
                mirror = (self.reserved_sectors + copy * self.fat_sectors) * self.bytes_per_sector + cluster * 4
                if struct.unpack_from("<I", self.data, mirror)[0] & FAT_MASK:
                    raise RuntimeError(f"FAT mirrors disagree for cluster {cluster}")
            selected.append(cluster)
            if len(selected) == count:
                return selected
        raise RuntimeError(f"Only found {len(selected)} of {count} required free clusters")

    def chain(self, first_cluster: int) -> list[int]:
        if first_cluster < 2:
            raise RuntimeError(f"Invalid first cluster {first_cluster}")
        result: list[int] = []
        seen: set[int] = set()
        cluster = first_cluster
        while True:
            if cluster in seen:
                raise RuntimeError(f"FAT cluster loop at {cluster}")
            seen.add(cluster)
            self.cluster_offset(cluster)
            result.append(cluster)
            next_cluster = self.fat_next(cluster)
            if next_cluster >= FAT_EOC:
                return result
            if next_cluster in (0, 1, FAT_BAD):
                raise RuntimeError(f"Invalid FAT chain value 0x{next_cluster:X} after cluster {cluster}")
            cluster = next_cluster

    @staticmethod
    def short_name(entry: bytes) -> str:
        base = entry[:8].decode("ascii", "replace").rstrip()
        extension = entry[8:11].decode("ascii", "replace").rstrip()
        return f"{base}.{extension}" if extension else base

    def directory_entries(self, first_cluster: int) -> list[tuple[str, int, int, int, int]]:
        result: list[tuple[str, int, int, int, int]] = []
        for cluster in self.chain(first_cluster):
            offset = self.cluster_offset(cluster)
            for position in range(offset, offset + self.cluster_bytes, 32):
                entry = bytes(self.data[position : position + 32])
                if entry[0] == 0:
                    return result
                if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
                    continue
                name = self.short_name(entry)
                first = (struct.unpack_from("<H", entry, 20)[0] << 16) | struct.unpack_from("<H", entry, 26)[0]
                size = struct.unpack_from("<I", entry, 28)[0]
                result.append((name, entry[11], first, size, position))
        return result

    def find_path(self, path: str) -> tuple[str, int, int, int, int]:
        cluster = self.root_cluster
        found = None
        pieces = [piece for piece in path.strip("/").split("/") if piece]
        for index, piece in enumerate(pieces):
            found = next(
                (item for item in self.directory_entries(cluster) if item[0].casefold() == piece.casefold()),
                None,
            )
            if found is None:
                raise RuntimeError(f"FAT path does not exist: {path}")
            if index < len(pieces) - 1:
                if not found[1] & 0x10:
                    raise RuntimeError(f"FAT path component is not a directory: {piece}")
                cluster = found[2]
        assert found is not None
        return found

    def read_file(self, first_cluster: int, size: int) -> bytes:
        if size == 0:
            return b""
        output = bytearray()
        for cluster in self.chain(first_cluster):
            offset = self.cluster_offset(cluster)
            output.extend(self.data[offset : offset + self.cluster_bytes])
            if len(output) >= size:
                return bytes(output[:size])
        raise RuntimeError(f"FAT chain is too short for a {size}-byte file")

    def snapshot_files(self, directory_cluster: int | None = None, prefix: str = "") -> dict[str, dict[str, Any]]:
        if directory_cluster is None:
            directory_cluster = self.root_cluster
        result: dict[str, dict[str, Any]] = {}
        for name, attributes, first, size, _ in self.directory_entries(directory_cluster):
            if name in (".", ".."):
                continue
            path = f"{prefix}/{name}" if prefix else f"/{name}"
            if attributes & 0x10:
                result.update(self.snapshot_files(first, path))
            else:
                content = self.read_file(first, size)
                result[path.upper()] = {"path": path, "bytes": size, "sha256": sha256_bytes(content)}
        return result

    def write_image(self) -> None:
        self.image.write_bytes(self.data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-root", required=True, type=Path)
    parser.add_argument("--ramdisk", required=True, type=Path)
    parser.add_argument("--managed-app", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    root = args.candidate_root.resolve(strict=True)
    ramdisk = args.ramdisk.resolve(strict=True)
    app = args.managed_app.resolve(strict=True)
    manifest = args.manifest.resolve(strict=False)
    assert_within(ramdisk, root, "ramdisk")
    assert_within(manifest, root, "manifest")

    app_bytes = app.read_bytes()
    if not app_bytes.startswith(b"\x7fELF"):
        raise RuntimeError("Managed application candidate is not an ELF image")
    fs = GuideXosFat32(ramdisk)
    before_files = fs.snapshot_files()
    name, attributes, first_cluster, old_size, directory_offset = fs.find_path(TARGET_PATH)
    if attributes & 0x10:
        raise RuntimeError(f"Target is a directory: {TARGET_PATH}")
    allocated_chain = fs.chain(first_cluster)
    capacity = len(allocated_chain) * fs.cluster_bytes
    required_clusters = (len(app_bytes) + fs.cluster_bytes - 1) // fs.cluster_bytes
    extra_count = max(0, required_clusters - len(allocated_chain))
    if extra_count:
        extra = fs.allocate_free_clusters(extra_count, set(allocated_chain))
        fs.set_fat_next(allocated_chain[-1], extra[0])
        for index, cluster in enumerate(extra):
            fs.set_fat_next(cluster, extra[index + 1] if index + 1 < len(extra) else FAT_EOC)
        allocated_chain.extend(extra)
        capacity = len(allocated_chain) * fs.cluster_bytes

    cursor = 0
    for cluster in allocated_chain:
        offset = fs.cluster_offset(cluster)
        count = min(fs.cluster_bytes, max(0, len(app_bytes) - cursor))
        if count:
            fs.data[offset : offset + count] = app_bytes[cursor : cursor + count]
            cursor += count
        if count < fs.cluster_bytes:
            fs.data[offset + count : offset + fs.cluster_bytes] = bytes(fs.cluster_bytes - count)
    if cursor != len(app_bytes):
        raise RuntimeError("Candidate ELF data did not fit the existing FAT allocation")

    struct.pack_into("<I", fs.data, directory_offset + 28, len(app_bytes))
    fs.write_image()

    verified = GuideXosFat32(ramdisk)
    final_entry = verified.find_path(TARGET_PATH)
    staged = verified.read_file(final_entry[2], final_entry[3])
    if staged != app_bytes:
        raise RuntimeError("The staged ELF did not round-trip from the FAT image")
    if verified.data[21] != CUSTOM_MEDIA_DESCRIPTOR:
        raise RuntimeError("The guideXOS custom media descriptor changed")
    after_files = verified.snapshot_files()
    before_target = before_files[TARGET_PATH]
    after_target = after_files[TARGET_PATH]
    changed_others = sorted(
        path for path, record in before_files.items()
        if path != TARGET_PATH and after_files.get(path) != record
    )
    added = sorted(set(after_files) - set(before_files))
    missing = sorted(set(before_files) - set(after_files) - {TARGET_PATH})
    if changed_others or added or missing:
        raise RuntimeError(f"Unrelated FAT files changed: changed={changed_others}, added={added}, missing={missing}")

    report = {
        "result": "PASS",
        "ramdisk": str(ramdisk),
        "managedApp": str(app),
        "targetPath": TARGET_PATH,
        "previousAppBytes": old_size,
        "previousAppSha256": before_target["sha256"],
        "stagedAppBytes": len(staged),
        "stagedAppSha256": sha256_bytes(staged),
        "ramdiskSha256": sha256_bytes(ramdisk.read_bytes()),
        "mediaDescriptorRestored": "0x00",
        "allocatedClustersRetained": len(allocated_chain),
        "allocatedClustersAdded": extra_count,
        "unrelatedFilesPreserved": len(before_files) - 1,
        "changedOtherFiles": 0,
    }
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # noqa: BLE001
        print(f"C187 ramdisk staging failed: {error}", file=sys.stderr)
        raise SystemExit(1)
