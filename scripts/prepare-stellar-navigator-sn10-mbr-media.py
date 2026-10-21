#!/usr/bin/env python3
"""Build a validated MBR/FAT32 SN10 disk with separate root and UEFI partitions.

The root partition is MBR type 0x0C and appears first so the current kernel FAT
probe selects it. The UEFI boot partition is MBR type 0xEF and remains
standards-compatible. Both FAT volumes are generated from the accepted staged
ESP with the repository's pinned release_esp.py formatter.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import re
import shutil
import struct
import sys
from pathlib import Path, PurePosixPath
from typing import Any


SECTOR_SIZE = 512
ALIGNMENT_SECTORS = 2048
PARTITION_SECTORS = (128 * 1024 * 1024) // SECTOR_SIZE
ROOT_PARTITION_TYPE = 0x0C
EFI_PARTITION_TYPE = 0xEF
EXPECTED_REQUIRED_HASHES = {
    "kernel.elf": "3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a",
    "EFI/BOOT/BOOTX64.EFI": "07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783",
    "Apps/StellarNavigator/bin/amd64/stellarnavigator.elf": "6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb",
    "Apps/StellarNavigator/app.json": "08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c",
    "Apps/StellarNavigator/Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin": "d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a",
}
BOOT_FILES = {
    "EFI/BOOT/BOOTX64.EFI",
    "kernel.elf",
    "ramdisk.img",
    "startup.nsh",
    "build-identity.txt",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_file_manifest(entries: Any) -> dict[str, dict[str, Any]]:
    if not isinstance(entries, list):
        raise ValueError("expected file manifest must contain a files array")
    result: dict[str, dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("expected file manifest contains a malformed entry")
        relative = PurePosixPath(str(entry.get("path", "")).replace("\\", "/"))
        if relative.is_absolute() or not relative.parts or ".." in relative.parts:
            raise ValueError(f"unsafe relative payload path: {entry.get('path')!r}")
        name = relative.as_posix()
        if name in result:
            raise ValueError(f"duplicate file manifest path: {name}")
        size = int(entry.get("size", -1))
        digest = str(entry.get("sha256", "")).lower()
        if size <= 0 or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"invalid manifest size or SHA-256 for {name}")
        result[name] = {"path": name, "size": size, "sha256": digest}
    return result


def actual_source_manifest(source: Path) -> dict[str, dict[str, Any]]:
    if not source.is_dir():
        raise ValueError(f"staged ESP directory is missing: {source}")
    result: dict[str, dict[str, Any]] = {}
    for path in sorted(source.rglob("*"), key=lambda item: item.as_posix().casefold()):
        if path.is_symlink():
            raise ValueError(f"staged ESP contains a symlink: {path}")
        if path.is_dir():
            continue
        if not path.is_file() or path.stat().st_size <= 0:
            raise ValueError(f"staged ESP contains an unsupported or empty entry: {path}")
        relative = path.relative_to(source).as_posix()
        result[relative] = {
            "path": relative,
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
        }
    return result


def mbr_partition_entry(partition_type: int, start_lba: int, sectors: int) -> bytes:
    if not 0 < partition_type <= 0xFF or start_lba <= 0 or sectors <= 0:
        raise ValueError("invalid MBR partition fields")
    end_lba = start_lba + sectors - 1

    def chs(lba: int) -> bytes:
        cylinder = lba // (255 * 63)
        head = (lba // 63) % 255
        sector = (lba % 63) + 1
        if cylinder > 1023:
            return bytes((254, 255, 255))
        return bytes((head, (sector & 0x3F) | ((cylinder >> 2) & 0xC0), cylinder & 0xFF))

    return bytes((0,)) + chs(start_lba) + bytes((partition_type,)) + chs(end_lba) + struct.pack(
        "<II", start_lba, sectors
    )


def build_mbr(root_start: int, root_sectors: int, efi_start: int, efi_sectors: int) -> bytes:
    if root_start % ALIGNMENT_SECTORS or efi_start % ALIGNMENT_SECTORS:
        raise ValueError("partition starts must be aligned to 1 MiB")
    if root_start + root_sectors > efi_start:
        raise ValueError("root and EFI partitions overlap")
    mbr = bytearray(SECTOR_SIZE)
    # Stable, nonzero MBR disk signature; it is not used as the payload identity.
    struct.pack_into("<I", mbr, 440, 0x38524E53)
    first = 446
    mbr[first : first + 16] = mbr_partition_entry(ROOT_PARTITION_TYPE, root_start, root_sectors)
    mbr[first + 16 : first + 32] = mbr_partition_entry(EFI_PARTITION_TYPE, efi_start, efi_sectors)
    mbr[510:512] = b"\x55\xAA"
    return bytes(mbr)


def parse_mbr(mbr: bytes) -> list[dict[str, int]]:
    if len(mbr) != SECTOR_SIZE or mbr[510:512] != b"\x55\xAA":
        raise ValueError("invalid MBR sector or signature")
    partitions = []
    for index in range(4):
        offset = 446 + index * 16
        entry = mbr[offset : offset + 16]
        partition_type = entry[4]
        start_lba, sectors = struct.unpack_from("<II", entry, 8)
        if partition_type == 0:
            if any(entry):
                raise ValueError(f"unused MBR entry {index + 1} contains nonzero data")
            continue
        if entry[0] not in (0, 0x80) or start_lba == 0 or sectors == 0:
            raise ValueError(f"malformed MBR partition entry {index + 1}")
        partitions.append({
            "index": index + 1,
            "type": partition_type,
            "startLba": start_lba,
            "sectors": sectors,
            "endLba": start_lba + sectors - 1,
        })
    for previous, current in zip(partitions, partitions[1:]):
        if previous["endLba"] >= current["startLba"]:
            raise ValueError("MBR partitions overlap or are out of order")
    return partitions


def read_fat32_bpb(stream: Any, start_lba: int, sectors: int) -> dict[str, int]:
    stream.seek(start_lba * SECTOR_SIZE)
    boot = stream.read(SECTOR_SIZE)
    if len(boot) != SECTOR_SIZE or boot[510:512] != b"\x55\xAA":
        raise ValueError(f"invalid FAT32 boot sector at LBA {start_lba}")
    bps = struct.unpack_from("<H", boot, 11)[0]
    spc = boot[13]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    fat_count = boot[16]
    total16 = struct.unpack_from("<H", boot, 19)[0]
    total32 = struct.unpack_from("<I", boot, 32)[0]
    fat_size16 = struct.unpack_from("<H", boot, 22)[0]
    fat_size32 = struct.unpack_from("<I", boot, 36)[0]
    root_cluster = struct.unpack_from("<I", boot, 44)[0]
    fsinfo_sector = struct.unpack_from("<H", boot, 48)[0]
    if bps != SECTOR_SIZE or spc == 0 or spc & (spc - 1):
        raise ValueError(f"invalid FAT32 sector or cluster geometry at LBA {start_lba}")
    if reserved == 0 or fat_count != 2 or fat_size16 != 0 or fat_size32 == 0:
        raise ValueError(f"invalid FAT32 reserved/FAT geometry at LBA {start_lba}")
    if total16 != 0 or total32 != sectors or root_cluster < 2:
        raise ValueError(f"FAT32 BPB extent does not match its MBR partition at LBA {start_lba}")
    if boot[82:90] != b"FAT32   ":
        raise ValueError(f"FAT32 filesystem signature missing at LBA {start_lba}")
    first_data = reserved + fat_count * fat_size32
    if first_data >= total32:
        raise ValueError("FAT32 data region is empty")
    clusters = (total32 - first_data) // spc
    if clusters < 65525 or (clusters + 2) * 4 > fat_size32 * SECTOR_SIZE:
        raise ValueError("FAT32 cluster count or FAT capacity is invalid")
    if fsinfo_sector >= reserved:
        raise ValueError("FAT32 FSInfo sector is outside its reserved region")

    for fat_index in range(fat_count):
        fat_offset = (start_lba + reserved + fat_index * fat_size32) * SECTOR_SIZE
        stream.seek(fat_offset)
        first_entries = stream.read(8)
        if len(first_entries) != 8:
            raise ValueError("truncated FAT32 allocation table")
        fat0, fat1 = struct.unpack("<II", first_entries)
        if fat0 & 0x0FFFFFF8 != 0x0FFFFFF8 or fat1 & 0x0FFFFFF8 != 0x0FFFFFF8:
            raise ValueError("FAT32 reserved table entries are invalid")
        stream.seek(fat_offset + root_cluster * 4)
        root_entry_bytes = stream.read(4)
        if len(root_entry_bytes) != 4:
            raise ValueError("truncated FAT32 root directory allocation entry")
        root_entry = struct.unpack("<I", root_entry_bytes)[0]
        if root_entry & 0x0FFFFFF8 != 0x0FFFFFF8:
            raise ValueError("FAT32 root directory cluster is not allocated")
    return {
        "bytesPerSector": bps,
        "sectorsPerCluster": spc,
        "reservedSectors": reserved,
        "fatCopies": fat_count,
        "fatSizeSectorsPerCopy": fat_size32,
        "totalSectors": total32,
        "rootCluster": root_cluster,
        "fsInfoSector": fsinfo_sector,
        "dataClusters": clusters,
    }


def copy_partition_source(source: Path, destination: Path, files: list[dict[str, Any]]) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    for entry in files:
        relative = PurePosixPath(entry["path"])
        target = destination.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source.joinpath(*relative.parts), target)
        if target.stat().st_size != entry["size"] or sha256_file(target) != entry["sha256"]:
            raise ValueError(f"partition staging copy differs from accepted source: {entry['path']}")


def fat_filesystem_module(repo: Path) -> Any:
    if importlib.metadata.version("pyfatfs") != "1.1.0":
        raise RuntimeError("the pinned pyfatfs 1.1.0 environment is required")
    if str(repo) not in sys.path:
        sys.path.insert(0, str(repo))
    from tools import release_esp

    return release_esp


def kernel_fat_partition_types(source: str) -> set[int]:
    probe = re.search(r"static bool is_fat_partition_type\(uint8_t partType\)\s*\{([^}]+)\}", source)
    if not probe:
        raise RuntimeError("could not identify the kernel FAT MBR partition probe")
    return {int(value, 16) for value in re.findall(r"partType\s*==\s*0x([0-9A-Fa-f]{2})", probe.group(1))}


def validate_fat_paths(image: Path, files: list[dict[str, Any]], kind: str, repo: Path) -> dict[str, Any]:
    helper = fat_filesystem_module(repo)
    _, PyFatFS = helper.load_pyfatfs()
    fs = helper.open_fat(PyFatFS, image, read_only=True)
    try:
        for entry in files:
            path = "/" + entry["path"]
            if not fs.exists(path) or int(fs.getsize(path)) != entry["size"]:
                raise ValueError(f"FAT traversal missing or size-mismatching {kind} path: {path}")
        directories: list[str] = []
        if kind == "root":
            apps = fs.listdir("/")
            if not any(name.casefold() == "apps" for name in apps):
                raise ValueError("root FAT partition does not expose /Apps")
            app_names = fs.listdir("/Apps")
            if not any(name.casefold() == "stellarnavigator" for name in app_names):
                raise ValueError("root FAT partition does not expose /Apps/StellarNavigator")
            if not fs.exists("/Apps/StellarNavigator/app.json"):
                raise ValueError("root FAT partition is missing the Stellar Navigator manifest")
            if not fs.exists("/Apps/StellarNavigator/bin/amd64/stellarnavigator.elf"):
                raise ValueError("root FAT partition is missing the native AMD64 ELF")
            if not fs.exists("/Apps/StellarNavigator/Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin"):
                raise ValueError("root FAT partition is missing DE440s data")
            directories = ["/Apps", "/Apps/StellarNavigator"]
        else:
            for path in ("/EFI/BOOT/BOOTX64.EFI", "/kernel.elf", "/ramdisk.img"):
                if not fs.exists(path):
                    raise ValueError(f"EFI FAT partition is missing required boot file: {path}")
            directories = ["/EFI", "/EFI/BOOT"]
        return {"directoryTraversalPassed": True, "checkedDirectories": directories, "checkedFiles": len(files)}
    finally:
        fs.close()


def write_expected_manifest(path: Path, files: list[dict[str, Any]]) -> None:
    path.write_text(json.dumps({"files": files}, indent=2) + "\n", encoding="utf-8")


def sha256_stream(stream: Any, offset: int, length: int) -> str:
    stream.seek(offset)
    digest = hashlib.sha256()
    remaining = length
    while remaining:
        chunk = stream.read(min(1024 * 1024, remaining))
        if not chunk:
            raise ValueError("truncated disk while hashing partition payload")
        digest.update(chunk)
        remaining -= len(chunk)
    return digest.hexdigest()


def prepare(args: argparse.Namespace) -> dict[str, Any]:
    source = args.source_esp.resolve()
    candidate_manifest_path = args.candidate_manifest.resolve()
    full_tree_path = args.full_tree_manifest.resolve()
    original_disk_path = args.original_gpt_image.resolve()
    output = args.output_dir.resolve()
    repo = Path(__file__).resolve().parents[1]
    helper = fat_filesystem_module(repo)
    helper_path = repo / "tools" / "release_esp.py"
    if not helper_path.is_file():
        raise RuntimeError("repository release_esp.py formatter is missing")
    if output.exists():
        raise FileExistsError(f"refusing to overwrite existing output directory: {output}")
    if not source.is_dir() or not candidate_manifest_path.is_file() or not full_tree_path.is_file():
        raise FileNotFoundError("accepted source ESP or its manifests are missing")

    candidate = json.loads(candidate_manifest_path.read_text(encoding="utf-8-sig"))
    if Path(candidate["stagedEsp"]).resolve() != source:
        raise ValueError("staged-candidate manifest names a different source ESP")
    source_manifest = normalize_file_manifest(json.loads(full_tree_path.read_text(encoding="utf-8-sig"))["files"])
    actual = actual_source_manifest(source)
    if actual != source_manifest:
        missing = sorted(set(source_manifest) - set(actual))
        extra = sorted(set(actual) - set(source_manifest))
        changed = sorted(name for name in set(actual) & set(source_manifest) if actual[name] != source_manifest[name])
        raise ValueError(f"source ESP differs from accepted full-tree manifest; missing={missing}, extra={extra}, changed={changed}")
    for name, digest in EXPECTED_REQUIRED_HASHES.items():
        if name not in actual or actual[name]["sha256"] != digest:
            raise ValueError(f"accepted payload SHA-256 mismatch: {name}")
    if not BOOT_FILES.issubset(actual):
        raise ValueError(f"accepted media lacks boot files: {sorted(BOOT_FILES - set(actual))}")
    root_files = [entry for name, entry in sorted(actual.items()) if name.startswith("Apps/") or name == "desktop.cfg"]
    efi_files = [entry for name, entry in sorted(actual.items()) if name in BOOT_FILES]
    if set(entry["path"] for entry in root_files) & set(entry["path"] for entry in efi_files):
        raise ValueError("partition payloads overlap")
    if {entry["path"] for entry in root_files + efi_files} != set(actual):
        raise ValueError("accepted staged payload is not fully assigned to the root or EFI partition")

    original_hash_before = sha256_file(original_disk_path)
    if original_hash_before != args.expected_original_gpt_sha256.lower():
        raise ValueError("original GPT source hash differs from the SN10R6 accepted identity")

    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(Path(__file__).resolve(), output / Path(__file__).name)
    source_manifest_out = output / "accepted-full-tree-files.json"
    source_manifest_out.write_text(json.dumps({"files": list(actual.values())}, indent=2) + "\n", encoding="utf-8")
    root_source = output / "root-source"
    efi_source = output / "efi-source"
    copy_partition_source(source, root_source, root_files)
    copy_partition_source(source, efi_source, efi_files)
    root_expected = output / "root-files.json"
    efi_expected = output / "efi-files.json"
    write_expected_manifest(root_expected, root_files)
    write_expected_manifest(efi_expected, efi_files)
    root_fat = output / "root-fat32.img"
    efi_fat = output / "efi-fat32.img"
    root_fat_report = output / "root-fat32-verification.json"
    efi_fat_report = output / "efi-fat32-verification.json"
    helper.create_image(root_source, root_fat, PARTITION_SECTORS * SECTOR_SIZE, root_expected, root_fat_report, 0x47585238)
    helper.create_image(efi_source, efi_fat, PARTITION_SECTORS * SECTOR_SIZE, efi_expected, efi_fat_report, 0x47584538)
    root_traversal = validate_fat_paths(root_fat, root_files, "root", repo)
    efi_traversal = validate_fat_paths(efi_fat, efi_files, "efi", repo)

    root_start = ALIGNMENT_SECTORS
    efi_start = root_start + PARTITION_SECTORS
    total_sectors = efi_start + PARTITION_SECTORS + ALIGNMENT_SECTORS
    image = output / "stellar-navigator-sn10r8-mbr.raw"
    mbr = build_mbr(root_start, PARTITION_SECTORS, efi_start, PARTITION_SECTORS)
    with image.open("xb") as disk:
        disk.truncate(total_sectors * SECTOR_SIZE)
        disk.seek(0)
        disk.write(mbr)
        disk.seek(root_start * SECTOR_SIZE)
        with root_fat.open("rb") as volume:
            shutil.copyfileobj(volume, disk, length=1024 * 1024)
        disk.seek(efi_start * SECTOR_SIZE)
        with efi_fat.open("rb") as volume:
            shutil.copyfileobj(volume, disk, length=1024 * 1024)

    with image.open("rb") as disk:
        parsed = parse_mbr(disk.read(SECTOR_SIZE))
        expected_partitions = [
            {"index": 1, "type": ROOT_PARTITION_TYPE, "startLba": root_start, "sectors": PARTITION_SECTORS, "endLba": root_start + PARTITION_SECTORS - 1},
            {"index": 2, "type": EFI_PARTITION_TYPE, "startLba": efi_start, "sectors": PARTITION_SECTORS, "endLba": efi_start + PARTITION_SECTORS - 1},
        ]
        if parsed != expected_partitions:
            raise ValueError(f"written MBR table differs from required root/EFI layout: {parsed}")
        root_bpb = read_fat32_bpb(disk, root_start, PARTITION_SECTORS)
        efi_bpb = read_fat32_bpb(disk, efi_start, PARTITION_SECTORS)
        if sha256_stream(disk, root_start * SECTOR_SIZE, PARTITION_SECTORS * SECTOR_SIZE) != sha256_file(root_fat):
            raise ValueError("root FAT32 partition does not match its validated volume image")
        if sha256_stream(disk, efi_start * SECTOR_SIZE, PARTITION_SECTORS * SECTOR_SIZE) != sha256_file(efi_fat):
            raise ValueError("EFI FAT32 partition does not match its validated volume image")
    if image.stat().st_size != total_sectors * SECTOR_SIZE:
        raise ValueError("raw MBR image size does not match its sector geometry")

    original_hash_after = sha256_file(original_disk_path)
    if original_hash_after != original_hash_before:
        raise ValueError("original GPT disk changed during candidate preparation")
    image_hash = sha256_file(image)
    if image_hash == original_hash_before:
        raise ValueError("new MBR candidate unexpectedly has the original GPT SHA-256")
    source_after = actual_source_manifest(source)
    if source_after != actual:
        raise ValueError("accepted staged ESP changed during candidate preparation")
    image.chmod(image.stat().st_mode & ~0o222)

    kernel_source = (repo / "kernel" / "core" / "fs_fat.cpp").read_text(encoding="utf-8")
    supported_types = sorted(kernel_fat_partition_types(kernel_source))
    if ROOT_PARTITION_TYPE not in supported_types:
        raise RuntimeError("kernel FAT probe does not accept the selected root MBR type 0x0C")
    first_supported = next((entry["type"] for entry in parsed if entry["type"] in supported_types), None)
    if first_supported != ROOT_PARTITION_TYPE:
        raise RuntimeError("kernel FAT probe would not select the /Apps root partition first")

    report = {
        "schemaVersion": 2,
        "passed": True,
        "format": "raw",
        "imagePath": str(image),
        "imageBytes": image.stat().st_size,
        "imageSha256": image_hash,
        "sectorSizeBytes": SECTOR_SIZE,
        "diskSectors": total_sectors,
        "partitionTable": "MBR",
        "mbrSignature": "55AA",
        "mbrDiskSignature": "0x38524E53",
        "sourceOriginalGpt": {
            "path": str(original_disk_path),
            "sha256Before": original_hash_before,
            "sha256After": original_hash_after,
            "unchanged": original_hash_before == original_hash_after,
        },
        "sourceEsp": str(source),
        "sourceEspUnchanged": source_after == actual,
        "candidateImageReadOnly": True,
        "acceptedCandidateManifest": str(candidate_manifest_path),
        "acceptedCandidateManifestSha256": sha256_file(candidate_manifest_path),
        "acceptedFullTreeManifest": str(full_tree_path),
        "acceptedFullTreeManifestSha256": sha256_file(full_tree_path),
        "kernelFatPartitionProbe": {
            "source": str(repo / "kernel" / "core" / "fs_fat.cpp"),
            "supportedMbrPartitionTypes": [f"0x{value:02X}" for value in supported_types],
            "rootType0x0cSupported": True,
            "efiType0xefSupportedByCurrentProbe": EFI_PARTITION_TYPE in supported_types,
            "firstMountablePartitionType": f"0x{first_supported:02X}",
        },
        "rootPartition": {
            **expected_partitions[0],
            "typeHex": "0x0C",
            "fatType": "FAT32",
            "fatBpb": root_bpb,
            "volumeImagePath": str(root_fat),
            "volumeImageSha256": sha256_file(root_fat),
            "verificationReport": str(root_fat_report),
            "directoryTraversal": root_traversal,
            "containsAppsDirectory": True,
            "containsStellarNavigatorManifest": True,
            "containsNativeAmd64Elf": True,
            "containsDE440sData": True,
            "files": root_files,
        },
        "efiPartition": {
            **expected_partitions[1],
            "typeHex": "0xEF",
            "fatType": "FAT32",
            "fatBpb": efi_bpb,
            "volumeImagePath": str(efi_fat),
            "volumeImageSha256": sha256_file(efi_fat),
            "verificationReport": str(efi_fat_report),
            "directoryTraversal": efi_traversal,
            "firmwareBootPath": "EFI/BOOT/BOOTX64.EFI",
            "files": efi_files,
        },
        # Legacy top-level fields keep the runner's manifest comparison concise;
        # they identify the partition the kernel is expected to mount at /.
        "partitionStartLba": root_start,
        "partitionSectors": PARTITION_SECTORS,
        "partitionEndLba": root_start + PARTITION_SECTORS - 1,
        "fatType": "FAT32",
        "files": list(actual.values()),
        "requiredPaths": list(EXPECTED_REQUIRED_HASHES),
        "payloadHashesMatchAcceptedManifest": True,
        "originalGptUnchanged": original_hash_before == original_hash_after,
        "noKernelSourceChangeRequired": True,
    }
    report_path = output / "stellar-navigator-sn10r8-verification.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    command.add_argument("--source-esp", type=Path, required=True)
    command.add_argument("--candidate-manifest", type=Path, required=True)
    command.add_argument("--full-tree-manifest", type=Path, required=True)
    command.add_argument("--original-gpt-image", type=Path, required=True)
    command.add_argument("--expected-original-gpt-sha256", required=True)
    command.add_argument("--output-dir", type=Path, required=True)
    return command


def main() -> int:
    try:
        report = prepare(parser().parse_args())
        summary = {key: report[key] for key in (
            "passed", "imagePath", "imageBytes", "imageSha256", "sourceOriginalGpt",
            "partitionTable", "rootPartition", "efiPartition", "kernelFatPartitionProbe",
        )}
        print(json.dumps(summary, indent=2))
        return 0
    except Exception as exc:
        print(f"SN10 MBR media preparation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
