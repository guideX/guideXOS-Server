#!/usr/bin/env python3
"""Create and independently verify a guideXOS UEFI El Torito ISO.

The writer uses the repository-local :mod:`pycdlib` library directly.  The
verifier intentionally uses only the Python standard library and parses the
ISO9660 and El Torito structures from raw bytes, so a successful create is not
validated solely by the same library that produced it.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
import struct
import sys
import tempfile
from pathlib import Path
from typing import Any, NoReturn


ISO_SECTOR_SIZE = 2048
ELTORITO_VIRTUAL_SECTOR_SIZE = 512
ELTORITO_MAX_SECTORS = 0xFFFF
UEFI_PLATFORM_ID = 0xEF
OVERSIZED_EFI_SENTINEL = 0
PYCDLIB_VERSION = "1.16.0"
BOOT_IMAGE_ISO_PATH = "UEFI_BOOT.IMG"
README_ISO_PATH = "README.TXT"


def die(message: str) -> NoReturn:
    raise RuntimeError(message)


def load_pycdlib() -> Any:
    try:
        import pycdlib
    except ImportError as exc:
        die(
            "pycdlib is unavailable. Run the release script with -BootstrapTools "
            "or install the pinned requirements into the repository-local release venv."
        )
    installed = importlib.metadata.version("pycdlib")
    if installed != PYCDLIB_VERSION:
        die(f"pycdlib {PYCDLIB_VERSION} is required; found {installed}.")
    return pycdlib


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_iso_region(reader: "IsoReader", offset: int, size: int) -> str:
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        amount = min(1024 * 1024, remaining)
        digest.update(reader.read_at(offset, amount))
        offset += amount
        remaining -= amount
    return digest.hexdigest()


def is_zero_region(reader: "IsoReader", offset: int, size: int) -> bool:
    remaining = size
    while remaining:
        amount = min(1024 * 1024, remaining)
        if any(reader.read_at(offset, amount)):
            return False
        offset += amount
        remaining -= amount
    return True


class IsoReader:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.stream = path.open("rb")
        self.size = path.stat().st_size
        if self.size <= 0 or self.size % ISO_SECTOR_SIZE != 0:
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
        return self.read_at(lba * ISO_SECTOR_SIZE, ISO_SECTOR_SIZE)


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
    data = reader.read_at(extent_lba * ISO_SECTOR_SIZE, data_length)
    offset = 0
    while offset < len(data):
        record_length = data[offset]
        if record_length == 0:
            offset = ((offset // ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE
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
            if extent * ISO_SECTOR_SIZE + length > reader.size:
                die(f"ISO directory record extends beyond the image: {path}")
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
        result.append({"path": value, "size": size, "sha256": digest})

    if not isinstance(boot_image, dict):
        die(f"expected ISO manifest does not contain a bootImage object: {path}")
    boot_path = str(boot_image.get("path", "")).replace("\\", "/").strip("/").upper()
    boot_size = int(boot_image.get("size", -1))
    boot_hash = str(boot_image.get("sha256", "")).lower()
    if not boot_path or boot_size <= 0 or len(boot_hash) != 64 or any(
        c not in "0123456789abcdef" for c in boot_hash
    ):
        die(f"invalid expected El Torito boot-image metadata: {boot_image!r}")
    sector_count = boot_image.get("sectorCount")
    if sector_count is not None:
        sector_count = int(sector_count)
        if sector_count < 0 or sector_count > ELTORITO_MAX_SECTORS:
            die(f"invalid expected El Torito sector count: {sector_count}")
    return {
        "files": result,
        "bootImage": {
            "path": boot_path,
            "size": boot_size,
            "sha256": boot_hash,
            "sectorCount": sector_count,
        },
    }


def validate_fat_boot_sector(reader: IsoReader, offset: int) -> None:
    header = reader.read_at(offset, 512)
    bytes_per_sector = u16(header, 11)
    sectors_per_cluster = header[13]
    reserved_sectors = u16(header, 14)
    fat_count = header[16]
    fat_size = u32(header, 36)
    fs_type = header[82:90].decode("ascii", errors="replace").strip()
    if (
        bytes_per_sector != 512
        or sectors_per_cluster == 0
        or sectors_per_cluster & (sectors_per_cluster - 1)
        or reserved_sectors == 0
        or fat_count == 0
        or fat_size == 0
        or fs_type != "FAT32"
        or header[510:512] != b"\x55\xAA"
    ):
        die(
            "El Torito boot image does not begin with plausible FAT32 boot-sector data "
            f"(type={fs_type!r}, bytesPerSector={bytes_per_sector})"
        )


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
        if volume_space == 0 or volume_space * ISO_SECTOR_SIZE > reader.size:
            die("ISO primary volume descriptor exceeds the actual image")

        boot_record_lba = None
        boot_record = None
        for lba in range(17, min(volume_space, 64)):
            descriptor = reader.sector(lba)
            if descriptor[0] == 0 and descriptor[1:6] == b"CD001" and descriptor[6] == 1:
                boot_record_lba = lba
                boot_record = descriptor
                break
            if descriptor[0] == 255:
                break
        if boot_record is None or boot_record_lba is None:
            die("ISO El Torito boot record descriptor is missing")
        boot_system = boot_record[7:39].rstrip(b" \x00").decode("ascii", errors="replace")
        if boot_system != "EL TORITO SPECIFICATION":
            die(f"unexpected El Torito boot system identifier: {boot_system!r}")
        catalog_lba = u32(boot_record, 71)
        if catalog_lba == 0 or catalog_lba >= volume_space:
            die(f"El Torito boot catalog pointer is outside the ISO: {catalog_lba}")
        catalog = reader.sector(catalog_lba)
        if catalog[0] != 1 or catalog[1] != UEFI_PLATFORM_ID:
            die("El Torito validation entry is not a UEFI (0xEF) entry")
        if sum(struct.unpack_from("<16H", catalog, 0)) & 0xFFFF:
            die("El Torito validation-entry checksum is invalid")
        if catalog[30:32] != b"\x55\xAA":
            die("El Torito validation-entry key bytes are invalid")

        entry = catalog[32:64]
        if entry[0] != 0x88:
            die("El Torito boot entry is not marked bootable")
        if entry[1] != 0:
            die(f"El Torito boot entry uses emulation media type {entry[1]:#x}")
        if entry[5] != 0:
            die("El Torito boot entry unused byte is not zero")
        if any(catalog[64:]):
            die("ISO contains an unexpected additional El Torito boot entry")
        boot_image_lba = u32(entry, 8)
        boot_sector_count = u16(entry, 6)
        if boot_image_lba == 0 or boot_image_lba >= volume_space:
            die(f"El Torito boot image LBA is outside the ISO: {boot_image_lba}")

        root_record = pvd[156:]
        if root_record[0] < 34:
            die("ISO root directory record is invalid")
        root_extent = u32(root_record, 2)
        root_length = u32(root_record, 10)
        if root_extent == 0 or root_extent * ISO_SECTOR_SIZE + root_length > reader.size:
            die("ISO root directory extent is invalid")
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
            actual_hash = sha256_iso_region(reader, actual["lba"] * ISO_SECTOR_SIZE, actual["size"])
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

        boot_path = expected_boot_image["path"]
        boot_tree_entry = files.get(boot_path)
        if boot_tree_entry is None:
            die(f"ISO tree is missing the El Torito boot image file: {boot_path}")
        if boot_tree_entry["lba"] != boot_image_lba:
            die(
                "El Torito boot catalog does not reference the boot image file extent: "
                f"catalog={boot_image_lba}, file={boot_tree_entry['lba']}"
            )

        boot_image_size = int(expected_boot_image["size"])
        if boot_tree_entry["size"] != boot_image_size:
            die(
                f"El Torito boot-image file size differs from the source FAT image: "
                f"expected {boot_image_size}, got {boot_tree_entry['size']}"
            )
        boot_image_offset = boot_image_lba * ISO_SECTOR_SIZE
        if boot_image_offset + boot_image_size > reader.size:
            die("El Torito EFI boot image extends beyond the ISO")
        validate_fat_boot_sector(reader, boot_image_offset)
        boot_hash = sha256_iso_region(reader, boot_image_offset, boot_image_size)
        if boot_hash != expected_boot_image["sha256"]:
            die(
                "El Torito boot-image hash differs from the generated FAT image: "
                f"expected {expected_boot_image['sha256']}, got {boot_hash}"
            )

        expected_sector_count = expected_boot_image.get("sectorCount")
        natural_sector_count = (boot_image_size + ELTORITO_VIRTUAL_SECTOR_SIZE - 1) // ELTORITO_VIRTUAL_SECTOR_SIZE
        if natural_sector_count > ELTORITO_MAX_SECTORS:
            if expected_sector_count not in (0, 1):
                die(
                    "oversized EFI image requires El Torito sector-count sentinel 0 or 1; "
                    f"manifest requested {expected_sector_count!r}"
                )
            if boot_sector_count != expected_sector_count:
                die(
                    f"unexpected oversized EFI sector-count sentinel: "
                    f"expected {expected_sector_count}, got {boot_sector_count}"
                )
            padded_boot_end = boot_image_offset + (
                (boot_image_size + ISO_SECTOR_SIZE - 1) // ISO_SECTOR_SIZE
            ) * ISO_SECTOR_SIZE
            volume_end = volume_space * ISO_SECTOR_SIZE
            if padded_boot_end > volume_end:
                die("oversized EFI boot image extent exceeds the ISO volume")
            expected_tail_paths = {item["path"] for item in expected}
            boot_extent_end_lba = boot_image_lba + (
                (boot_image_size + ISO_SECTOR_SIZE - 1) // ISO_SECTOR_SIZE
            )
            max_data_end_lba = boot_extent_end_lba
            for path, entry_data in files.items():
                if path == boot_path:
                    continue
                entry_start_lba = entry_data["lba"]
                entry_end_lba = entry_start_lba + (
                    (entry_data["size"] + ISO_SECTOR_SIZE - 1) // ISO_SECTOR_SIZE
                )
                if entry_start_lba < boot_extent_end_lba and entry_end_lba > boot_image_lba:
                    die(f"ISO file overlaps the oversized EFI boot image: {path}")
                if entry_start_lba > boot_image_lba and path not in expected_tail_paths:
                    die(f"unexpected ISO data follows the oversized EFI boot image: {path}")
                max_data_end_lba = max(max_data_end_lba, entry_end_lba)
            if max_data_end_lba * ISO_SECTOR_SIZE > volume_end:
                die("ISO file data extends beyond the declared volume")
            if not is_zero_region(
                reader, max_data_end_lba * ISO_SECTOR_SIZE, volume_end - max_data_end_lba * ISO_SECTOR_SIZE
            ):
                die("ISO contains nonzero data after its final file extent")
        else:
            expected_natural = natural_sector_count
            if expected_sector_count is not None and expected_sector_count != expected_natural:
                die(
                    f"manifest sector count does not match the small EFI image: "
                    f"expected {expected_natural}, got {expected_sector_count}"
                )
            if boot_sector_count != expected_natural:
                die(
                    f"unexpected small EFI sector count: expected {expected_natural}, "
                    f"got {boot_sector_count}"
                )

        report = {
            "format": "guideXOS-iso-verification",
            "isoBytes": reader.size,
            "primaryVolumeDescriptorLba": 16,
            "bootRecordLba": boot_record_lba,
            "bootCatalogLba": catalog_lba,
            "uefiPlatformId": f"0x{UEFI_PLATFORM_ID:02X}",
            "bootable": True,
            "mediaType": "no-emulation",
            "bootImageLba": boot_image_lba,
            "bootImageDataEndLba": boot_image_lba + (
                (boot_image_size + ISO_SECTOR_SIZE - 1) // ISO_SECTOR_SIZE
            ),
            "bootImageSectorCount512": boot_sector_count,
            "bootImagePath": boot_path,
            "bootImageBytes": boot_image_size,
            "bootImageSha256": boot_hash,
            "files": verified_files,
        }
    finally:
        reader.close()

    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def reopen_with_pycdlib(iso_path: Path, expected: dict[str, Any]) -> dict[str, int]:
    pycdlib = load_pycdlib()
    iso = pycdlib.PyCdlib()
    try:
        iso.open(str(iso_path))
        boot_path = "/" + expected["bootImage"]["path"] + ";1"
        boot_record = iso.get_record(iso_path=boot_path)
        if boot_record is None:
            die(f"pycdlib could not reopen the boot image file: {boot_path}")
        catalog = iso.eltorito_boot_catalog
        if catalog is None:
            die("pycdlib reopened the ISO without an El Torito boot catalog")
        entry = catalog.initial_entry
        sector_count = int(entry.sector_count)
        boot_lba = int(entry.get_rba())
        expected_count = expected["bootImage"].get("sectorCount")
        if expected_count is not None and sector_count != expected_count:
            die(
                f"pycdlib reopened an unexpected El Torito sector count: "
                f"expected {expected_count}, got {sector_count}"
            )
        if boot_lba <= 0:
            die("pycdlib reopened an invalid El Torito boot-image LBA")
        return {"bootImageLba": boot_lba, "bootImageSectorCount512": sector_count}
    finally:
        iso.close()


def create_iso(
    boot_image: Path,
    readme: Path,
    output: Path,
    expected_path: Path,
    report_path: Path,
    volume_id: str,
    oversized_sentinel: int,
) -> dict[str, Any]:
    expected = load_expected(expected_path)
    if not boot_image.is_file() or boot_image.stat().st_size <= 0:
        die(f"EFI boot image is missing or empty: {boot_image}")
    if not readme.is_file() or readme.stat().st_size <= 0:
        die(f"release README is missing or empty: {readme}")
    if boot_image.stat().st_size != expected["bootImage"]["size"]:
        die("boot image changed after the expected ISO manifest was captured")
    if sha256_file(boot_image) != expected["bootImage"]["sha256"]:
        die("boot image hash changed after the expected ISO manifest was captured")
    expected_readme = next((item for item in expected["files"] if item["path"] == README_ISO_PATH), None)
    if expected_readme is None:
        die(f"expected ISO manifest must include {README_ISO_PATH}")
    if readme.stat().st_size != expected_readme["size"] or sha256_file(readme) != expected_readme["sha256"]:
        die("release README changed after the expected ISO manifest was captured")

    boot_size = boot_image.stat().st_size
    natural_sector_count = (boot_size + ELTORITO_VIRTUAL_SECTOR_SIZE - 1) // ELTORITO_VIRTUAL_SECTOR_SIZE
    if natural_sector_count > ELTORITO_MAX_SECTORS:
        sector_count = oversized_sentinel
    else:
        sector_count = natural_sector_count
    expected_sector_count = expected["bootImage"].get("sectorCount")
    if expected_sector_count is not None and expected_sector_count != sector_count:
        die(
            f"expected ISO manifest sector count {expected_sector_count} does not match "
            f"the selected value {sector_count}"
        )
    if natural_sector_count > ELTORITO_MAX_SECTORS and sector_count not in (0, 1):
        die("an oversized EFI image may use only sector-count sentinel 0 or 1")

    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{output.stem}-", suffix=".tmp.iso", dir=str(output.parent)
    )
    os.close(fd)
    temporary = Path(temporary_name)
    pycdlib = load_pycdlib()
    iso = pycdlib.PyCdlib()
    try:
        try:
            iso.new(interchange_level=3, joliet=3, vol_ident=volume_id[:32].upper())
            iso.add_file(
                str(readme),
                iso_path=f"/{README_ISO_PATH};1",
                joliet_path="/README.TXT",
            )
            # Add the complete source FAT image as the El Torito boot-linked extent.
            # pycdlib allocates boot-linked data before ordinary ISO inodes; the
            # independent verifier therefore permits only the expected README as
            # trailing ISO data and checks the FAT bytes and all padding separately.
            iso.add_file(
                str(boot_image),
                iso_path=f"/{BOOT_IMAGE_ISO_PATH};1",
                joliet_path="/EFI-BOOT.IMG",
            )
            iso.add_eltorito(
                f"/{BOOT_IMAGE_ISO_PATH};1",
                boot_load_size=sector_count,
                platform_id=UEFI_PLATFORM_ID,
                efi=True,
                media_name="noemul",
                bootable=True,
            )
            iso.write(str(temporary))
        finally:
            iso.close()
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise

    try:
        reopened = reopen_with_pycdlib(temporary, expected)
        report = verify(temporary, expected_path, report_path)
        if reopened["bootImageLba"] != report["bootImageLba"]:
            die("pycdlib and the independent raw parser disagree on the boot-image LBA")
        if reopened["bootImageSectorCount512"] != report["bootImageSectorCount512"]:
            die("pycdlib and the independent raw parser disagree on the sector count")
        os.replace(temporary, output)
        return report
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    subparsers = command.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create and verify a UEFI El Torito ISO")
    create.add_argument("--boot-image", type=Path, required=True)
    create.add_argument("--readme", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--expected", type=Path, required=True)
    create.add_argument("--report", type=Path, required=True)
    create.add_argument("--volume-id", default="GUIDEXOS")
    create.add_argument("--oversized-sentinel", type=int, choices=(0, 1), default=OVERSIZED_EFI_SENTINEL)

    verify_command = subparsers.add_parser("verify", help="verify a UEFI El Torito ISO")
    verify_command.add_argument("--iso", type=Path, required=True)
    verify_command.add_argument("--expected", type=Path, required=True)
    verify_command.add_argument("--report", type=Path, required=True)
    return command


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "create":
            result = create_iso(
                args.boot_image.resolve(),
                args.readme.resolve(),
                args.output.resolve(),
                args.expected.resolve(),
                args.report.resolve(),
                args.volume_id,
                args.oversized_sentinel,
            )
        else:
            result = verify(args.iso.resolve(), args.expected.resolve(), args.report.resolve())
        print(json.dumps(result, indent=2))
        return 0
    except Exception as exc:
        print(f"release_iso.py: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
