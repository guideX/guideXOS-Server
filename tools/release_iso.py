#!/usr/bin/env python3
"""Read-only structural verification for a single-boot UEFI ISO.

The verifier intentionally uses only Python's standard library.  It checks
the ISO 9660 primary volume and El Torito catalog directly; it never mounts an
image or opens a disk device.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Any


SECTOR_SIZE = 2048


def die(message: str) -> "NoReturn":
    raise RuntimeError(message)


class IsoReader:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.stream = path.open("rb")
        self.size = path.stat().st_size
        if self.size <= 0 or self.size % SECTOR_SIZE != 0:
            die(f"ISO size is not a positive 2048-byte multiple: {self.size}")

    def close(self) -> None:
        self.stream.close()

    def read_at(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset + size > self.size:
            die(f"ISO read is outside the image: offset={offset}, size={size}")
        self.stream.seek(offset)
        value = self.stream.read(size)
        if len(value) != size:
            die(f"short ISO read at offset {offset}: expected {size}, got {len(value)}")
        return value

    def sector(self, lba: int) -> bytes:
        return self.read_at(lba * SECTOR_SIZE, SECTOR_SIZE)


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def normalize_name(raw: bytes) -> str:
    name = raw.decode("ascii", errors="replace").rstrip(" ")
    if name.endswith(";1"):
        name = name[:-2]
    if name.endswith("."):
        name = name[:-1]
    return name.upper()


def parse_directory(
    reader: IsoReader,
    extent_lba: int,
    data_length: int,
    prefix: str,
    files: dict[str, dict[str, int]],
    directories: set[str],
    visited: set[tuple[int, int]],
) -> None:
    key = (extent_lba, data_length)
    if key in visited:
        return
    visited.add(key)
    data = reader.read_at(extent_lba * SECTOR_SIZE, data_length)
    offset = 0
    while offset < len(data):
        record_length = data[offset]
        if record_length == 0:
            offset = ((offset // SECTOR_SIZE) + 1) * SECTOR_SIZE
            continue
        if offset + record_length > len(data) or record_length < 34:
            die(f"invalid ISO directory record at {prefix}, offset {offset}")
        record = data[offset : offset + record_length]
        name_length = record[32]
        if 33 + name_length > len(record):
            die(f"invalid ISO directory name at {prefix}, offset {offset}")
        raw_name = record[33 : 33 + name_length]
        if raw_name not in (b"\x00", b"\x01"):
            name = normalize_name(raw_name)
            path = f"{prefix}/{name}" if prefix else name
            extent = u32(record, 2)
            length = u32(record, 10)
            if record[25] & 0x02:
                directories.add(path)
                parse_directory(reader, extent, length, path, files, directories, visited)
            else:
                files[path] = {"lba": extent, "size": length}
        offset += record_length


def load_expected(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        values = document["files"]
        boot_image = document["bootImage"]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        die(f"invalid expected ISO manifest {path}: {exc}")
    if not isinstance(values, list):
        die(f"expected ISO manifest does not contain a files array: {path}")
    result: list[dict[str, Any]] = []
    seen: set[str] = set()
    for item in values:
        if not isinstance(item, dict):
            die(f"invalid expected ISO file entry: {item!r}")
        value = str(item.get("path", "")).replace("\\", "/").strip("/").upper()
        if not value or ".." in value.split("/"):
            die(f"invalid expected ISO path: {value}")
        if value in seen:
            die(f"duplicate expected ISO path: {value}")
        seen.add(value)
        size = int(item.get("size", -1))
        digest = str(item.get("sha256", "")).lower()
        if size <= 0 or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            die(f"invalid expected ISO metadata for {value}")
        result.append(
            {
                "path": value,
                "size": size,
                "sha256": digest,
            }
        )
    if not isinstance(boot_image, dict):
        die(f"expected ISO manifest does not contain a bootImage object: {path}")
    boot_path = str(boot_image.get("path", "")).replace("\\", "/").strip("/")
    boot_size = int(boot_image.get("size", -1))
    boot_hash = str(boot_image.get("sha256", "")).lower()
    if not boot_path or boot_size <= 0 or len(boot_hash) != 64 or any(
        c not in "0123456789abcdef" for c in boot_hash
    ):
        die(f"invalid expected El Torito boot-image metadata: {boot_image!r}")
    return {
        "files": result,
        "bootImage": {"path": boot_path, "size": boot_size, "sha256": boot_hash},
    }


def sha256_iso_file(reader: IsoReader, entry: dict[str, int]) -> str:
    digest = hashlib.sha256()
    remaining = entry["size"]
    offset = entry["lba"] * SECTOR_SIZE
    while remaining:
        amount = min(1024 * 1024, remaining)
        digest.update(reader.read_at(offset, amount))
        offset += amount
        remaining -= amount
    return digest.hexdigest()


def sha256_iso_region(reader: IsoReader, offset: int, size: int) -> str:
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        amount = min(1024 * 1024, remaining)
        digest.update(reader.read_at(offset, amount))
        offset += amount
        remaining -= amount
    return digest.hexdigest()


def verify(iso_path: Path, expected_path: Path, report_path: Path) -> dict[str, Any]:
    expected_document = load_expected(expected_path)
    expected = expected_document["files"]
    expected_boot_image = expected_document["bootImage"]
    reader = IsoReader(iso_path)
    try:
        pvd = reader.sector(16)
        if pvd[0] != 1 or pvd[1:6] != b"CD001" or pvd[6] != 1:
            die("ISO primary volume descriptor is missing or invalid")
        volume_space = u32(pvd, 80)
        if volume_space * SECTOR_SIZE > reader.size:
            die("ISO primary volume descriptor exceeds the actual image")

        boot_record = reader.sector(17)
        if boot_record[0] != 0 or boot_record[1:6] != b"CD001" or boot_record[6] != 1:
            die("ISO El Torito boot record is missing or invalid")
        boot_system = boot_record[7:39].rstrip(b" ").decode("ascii", errors="replace")
        if boot_system != "EL TORITO SPECIFICATION":
            die(f"unexpected El Torito boot system identifier: {boot_system!r}")
        catalog_lba = u32(boot_record, 71)
        catalog = reader.sector(catalog_lba)
        if catalog[0] != 1 or catalog[1] != 0xEF:
            die("El Torito validation entry is not a UEFI (0xEF) entry")
        if sum(struct.unpack_from("<16H", catalog, 0)) & 0xFFFF:
            die("El Torito validation-entry checksum is invalid")
        entry = catalog[32:64]
        if entry[0] != 0x88 or entry[1] != 0:
            die("El Torito boot entry is not a bootable no-emulation UEFI entry")
        if any(catalog[64:]):
            die("ISO contains an unexpected additional El Torito boot entry")
        boot_image_lba = u32(entry, 8)
        boot_sector_count = u16(entry, 6)
        if boot_image_lba * SECTOR_SIZE >= reader.size:
            die("El Torito boot image starts outside the ISO")

        root_record = pvd[156:]
        if root_record[0] < 34:
            die("ISO root directory record is invalid")
        root_extent = u32(root_record, 2)
        root_length = u32(root_record, 10)
        files: dict[str, dict[str, int]] = {}
        directories: set[str] = set()
        parse_directory(reader, root_extent, root_length, "", files, directories, set())

        required = {item["path"] for item in expected}
        missing = sorted(required - set(files))
        if missing:
            die(f"ISO is missing expected files: {missing}")

        verified_files: list[dict[str, Any]] = []
        for item in expected:
            actual = files[item["path"]]
            if actual["size"] != item["size"]:
                die(
                    f"ISO size mismatch for {item['path']}: "
                    f"expected {item['size']}, got {actual['size']}"
                )
            actual_hash = sha256_iso_file(reader, actual)
            if actual_hash != item["sha256"]:
                die(
                    f"ISO hash mismatch for {item['path']}: "
                    f"expected {item['sha256']}, got {actual_hash}"
                )
            verified_files.append(
                {
                    "path": item["path"],
                    "size": actual["size"],
                    "sha256": actual_hash,
                    "lba": actual["lba"],
                }
            )

        boot_image_size = int(expected_boot_image["size"])
        boot_image_offset = boot_image_lba * SECTOR_SIZE
        if boot_image_offset + boot_image_size > reader.size:
            die("El Torito EFI boot image extends beyond the ISO")
        boot_header = reader.read_at(boot_image_offset, 512)
        boot_fs_type = boot_header[82:90].decode("ascii", errors="replace").strip()
        if boot_fs_type != "FAT32" or boot_header[510:512] != b"\x55\xAA":
            die("El Torito boot image is not a valid FAT32 image")
        boot_hash = sha256_iso_region(reader, boot_image_offset, boot_image_size)
        if boot_hash != expected_boot_image["sha256"]:
            die(
                "El Torito boot-image hash differs from the generated FAT image: "
                f"expected {expected_boot_image['sha256']}, got {boot_hash}"
            )
        if boot_sector_count in (0, 1):
            # UEFI defines 0/1 as the large-image form: the ESP consumes the
            # no-emulation image through the end of the CD-ROM.
            if boot_image_offset + boot_image_size != reader.size:
                die(
                    "large UEFI El Torito boot image is not last in the ISO; "
                    "sector count 0/1 would include trailing data"
                )
        elif boot_sector_count * 512 != boot_image_size:
            die(
                "El Torito boot entry sector count does not match the FAT image "
                "size"
            )

        report = {
            "format": "guideXOS-iso-verification",
            "isoBytes": reader.size,
            "primaryVolumeDescriptorLba": 16,
            "bootRecordLba": 17,
            "bootCatalogLba": catalog_lba,
            "uefiPlatformId": "0xEF",
            "bootImageLba": boot_image_lba,
            "bootImageSectorCount512": boot_sector_count,
            "bootImagePath": expected_boot_image["path"],
            "bootImageBytes": boot_image_size,
            "bootImageSha256": boot_hash,
            "files": verified_files,
        }
    finally:
        reader.close()

    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    command = argparse.ArgumentParser(description=__doc__)
    command.add_argument("verify", choices=["verify"])
    command.add_argument("--iso", type=Path, required=True)
    command.add_argument("--expected", type=Path, required=True)
    command.add_argument("--report", type=Path, required=True)
    args = command.parse_args(argv)
    try:
        result = verify(args.iso.resolve(), args.expected.resolve(), args.report.resolve())
        print(json.dumps(result, indent=2))
        return 0
    except Exception as exc:
        print(f"release_iso.py: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
