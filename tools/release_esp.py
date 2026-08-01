#!/usr/bin/env python3
"""Create and verify a FAT32 image from a guideXOS ESP directory.

This helper deliberately limits itself to regular files below the supplied
source directory.  It never opens a volume, mounts an image, or touches a
physical disk.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import shutil
import struct
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


PYFATFS_VERSION = "1.1.0"


def die(message: str) -> "NoReturn":
    raise RuntimeError(message)


def load_pyfatfs() -> tuple[Any, Any]:
    try:
        from pyfatfs.PyFat import PyFat
        from pyfatfs.PyFatFS import PyFatFS
    except ImportError as exc:
        die(
            "pyfatfs is unavailable. Run the release script with -BootstrapTools "
            "or install the pinned requirements into the repository-local release venv."
        )
    installed = importlib.metadata.version("pyfatfs")
    if installed != PYFATFS_VERSION:
        die(f"pyfatfs {PYFATFS_VERSION} is required; found {installed}.")
    return PyFat, PyFatFS


def normalize_relative(value: str) -> str:
    candidate = value.replace("\\", "/").strip("/")
    if not candidate or candidate == ".":
        die("empty relative file path")
    path = PurePosixPath(candidate)
    if path.is_absolute() or ".." in path.parts:
        die(f"path escapes the ESP: {value}")
    return path.as_posix()


def iter_source_files(source: Path) -> list[tuple[str, Path]]:
    source = source.resolve()
    if not source.is_dir():
        die(f"ESP source is not a directory: {source}")

    result: list[tuple[str, Path]] = []
    for item in sorted(source.rglob("*"), key=lambda p: p.as_posix().lower()):
        if item.is_symlink():
            die(f"symbolic links/reparse points are not allowed in the ESP: {item}")
        if item.is_dir():
            continue
        if not item.is_file():
            die(f"unsupported ESP entry: {item}")
        if item.stat().st_size <= 0:
            die(f"empty ESP file is not allowed: {item}")
        relative = normalize_relative(item.relative_to(source).as_posix())
        result.append((relative, item))
    return result


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def expected_files(path: Path) -> list[dict[str, Any]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        files = document["files"]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        die(f"invalid expected file manifest {path}: {exc}")
    if not isinstance(files, list):
        die(f"expected file manifest does not contain a files array: {path}")
    normalized: list[dict[str, Any]] = []
    seen: set[str] = set()
    for item in files:
        if not isinstance(item, dict):
            die(f"invalid file entry in expected manifest: {item!r}")
        relative = normalize_relative(str(item.get("path", "")))
        if relative in seen:
            die(f"duplicate expected file path: {relative}")
        seen.add(relative)
        size = int(item.get("size", -1))
        digest = str(item.get("sha256", "")).lower()
        if size <= 0 or len(digest) != 64 or any(
            character not in "0123456789abcdef" for character in digest
        ):
            die(f"invalid expected metadata for {relative}")
        normalized.append({"path": relative, "size": size, "sha256": digest})
    return normalized


def fat_path(relative: str) -> str:
    return "/" + normalize_relative(relative)


def directory_paths(files: Iterable[str]) -> list[str]:
    directories: set[str] = set()
    for relative in files:
        parts = PurePosixPath(relative).parts[:-1]
        for index in range(1, len(parts) + 1):
            directories.add("/" + "/".join(parts[:index]))
    return sorted(directories, key=lambda value: (value.count("/"), value.lower()))


def repair_fat32_root_cluster(image: Path) -> None:
    """Repair the root-cluster marker emitted by pyfatfs 1.1.0 mkfs().

    pyfatfs 1.1.0 writes a FAT32 BPB with root cluster 2 but leaves FAT[2]
    free.  A normal FAT reader therefore rejects the image when it is opened
    again.  Set only that required root entry in both FAT copies; all payload
    allocation and directory updates still go through pyfatfs.
    """

    with image.open("r+b") as stream:
        boot = stream.read(512)
        if len(boot) != 512:
            die("cannot read the FAT32 boot sector after mkfs")
        bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
        reserved_sectors = struct.unpack_from("<H", boot, 14)[0]
        fat_count = boot[16]
        fat_size = struct.unpack_from("<I", boot, 36)[0]
        root_cluster = struct.unpack_from("<I", boot, 44)[0]
        if bytes_per_sector != 512 or fat_count < 1 or fat_size <= 0 or root_cluster < 2:
            die("pyfatfs produced an invalid FAT32 BPB")
        root_offset = root_cluster * 4
        eoc = 0x0FFFFFFF
        for index in range(fat_count):
            fat_offset = (reserved_sectors + index * fat_size) * bytes_per_sector
            stream.seek(fat_offset + root_offset)
            value = struct.unpack("<I", stream.read(4))[0] & 0x0FFFFFFF
            if value == 0:
                stream.seek(fat_offset + root_offset)
                stream.write(struct.pack("<I", eoc))
            elif value < 0x0FFFFFF8:
                die(f"pyfatfs produced an invalid FAT32 root-cluster marker: {value:#x}")


def open_fat(fs_type: Any, image: Path, read_only: bool) -> Any:
    return fs_type(
        str(image),
        read_only=read_only,
        preserve_case=True,
        utc=True,
        encoding="cp1252",
        lazy_load=False,
    )


def verify_image(
    image: Path,
    expected: list[dict[str, Any]],
    report_path: Path | None = None,
) -> dict[str, Any]:
    _, PyFatFS = load_pyfatfs()
    if not image.is_file() or image.stat().st_size <= 0:
        die(f"FAT image is missing or empty: {image}")

    fs = None
    verified: list[dict[str, Any]] = []
    try:
        fs = open_fat(PyFatFS, image, read_only=True)
        for item in expected:
            path = fat_path(item["path"])
            if not fs.exists(path):
                die(f"FAT image is missing {item['path']}")
            actual_size = int(fs.getsize(path))
            if actual_size != item["size"]:
                die(
                    f"FAT image size mismatch for {item['path']}: "
                    f"expected {item['size']}, got {actual_size}"
                )
            digest = hashlib.sha256()
            with fs.openbin(path, "r") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            actual_hash = digest.hexdigest()
            if actual_hash != item["sha256"]:
                die(
                    f"FAT image hash mismatch for {item['path']}: "
                    f"expected {item['sha256']}, got {actual_hash}"
                )
            verified.append(
                {"path": item["path"], "size": actual_size, "sha256": actual_hash}
            )
    finally:
        if fs is not None:
            fs.close()

    report = {
        "format": "guideXOS-fat32-esp",
        "fatType": "FAT32",
        "imageBytes": image.stat().st_size,
        "files": verified,
    }
    if report_path is not None:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def create_image(
    source: Path,
    image: Path,
    size: int,
    expected_path: Path,
    report_path: Path,
    volume_id: int,
) -> dict[str, Any]:
    PyFat, PyFatFS = load_pyfatfs()
    if size <= 0 or size % 512 != 0:
        die(f"FAT image size must be a positive 512-byte multiple: {size}")
    files = iter_source_files(source)
    expected = expected_files(expected_path)
    source_map = {relative: path for relative, path in files}
    expected_map = {item["path"]: item for item in expected}
    if set(source_map) != set(expected_map):
        missing = sorted(set(source_map) - set(expected_map))
        extra = sorted(set(expected_map) - set(source_map))
        die(f"source/expected file list differs; missing={missing}, extra={extra}")
    for relative, path in files:
        item = expected_map[relative]
        if path.stat().st_size != item["size"] or sha256_file(path) != item["sha256"]:
            die(f"source changed after expected manifest was captured: {relative}")

    image.parent.mkdir(parents=True, exist_ok=True)
    if image.exists():
        image.unlink()
    image.touch()
    formatter = PyFat()
    try:
        formatter.mkfs(
            str(image),
            PyFat.FAT_TYPE_FAT32,
            size=size,
            sector_size=512,
            number_of_fats=2,
            label="GUIDEXOS",
            volume_id=volume_id,
        )
    finally:
        # mkfs opens the image itself.  Close that handle before the narrow
        # FAT32 root-cluster repair and the population handle are opened.
        formatter.close()
    repair_fat32_root_cluster(image)

    fs = None
    try:
        fs = open_fat(PyFatFS, image, read_only=False)
        for directory in directory_paths(source_map):
            fs.makedir(directory, recreate=True)
        for relative, source_path in files:
            with source_path.open("rb") as source_stream:
                with fs.openbin(fat_path(relative), "w") as target_stream:
                    shutil.copyfileobj(source_stream, target_stream, length=1024 * 1024)
    finally:
        if fs is not None:
            fs.close()

    return verify_image(image, expected, report_path)


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    subparsers = command.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create and verify a FAT32 ESP image")
    create.add_argument("--source", type=Path, required=True)
    create.add_argument("--image", type=Path, required=True)
    create.add_argument("--size", type=int, required=True)
    create.add_argument("--expected", type=Path, required=True)
    create.add_argument("--report", type=Path, required=True)
    create.add_argument("--volume-id", type=lambda value: int(value, 16), default=0x47584F53)

    verify = subparsers.add_parser("verify", help="verify a FAT32 ESP image")
    verify.add_argument("--image", type=Path, required=True)
    verify.add_argument("--expected", type=Path, required=True)
    verify.add_argument("--report", type=Path, required=True)
    return command


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "create":
            result = create_image(
                args.source.resolve(),
                args.image.resolve(),
                args.size,
                args.expected.resolve(),
                args.report.resolve(),
                args.volume_id,
            )
        else:
            result = verify_image(
                args.image.resolve(),
                expected_files(args.expected.resolve()),
                args.report.resolve(),
            )
        print(json.dumps(result, indent=2))
        return 0
    except Exception as exc:
        print(f"release_esp.py: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
