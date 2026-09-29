#!/usr/bin/env python3
"""Independently verify the DM13 disposable QEMU USB GPT/FAT32 proof image."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import uuid
import zlib
from pathlib import Path


SECTOR_SIZE = 512
GPT_BASIC_DATA = uuid.UUID("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7").bytes_le
PARTITION_NAME = "DM13 QEMU USB Proof"
DIRECTORY_NAME = b"DM13       "
FILE_NAME = b"PROOF   BIN"
PAYLOAD = b"guideXOS DM13 USB lifecycle proof 001\r\n"
EOC = 0x0FFFFFF8
CLUSTER_MASK = 0x0FFFFFFF


class VerificationError(RuntimeError):
    pass


def require(condition: bool, detail: str) -> None:
    if not condition:
        raise VerificationError(detail)


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="read-only raw disk image")
    parser.add_argument("--check-blank", action="store_true",
                        help="require that the raw image is entirely zeroed")
    args = parser.parse_args()

    image_path = args.image.resolve(strict=True)
    if args.check_blank:
        with image_path.open("rb") as blank_image:
            for chunk in iter(lambda: blank_image.read(1024 * 1024), b""):
                require(not any(chunk), "input image is not blank")
        print("blank_source=PASS all_bytes_zero=yes")
        return 0

    image_digest = hashlib.sha256()
    with image_path.open("rb") as hash_stream:
        for chunk in iter(lambda: hash_stream.read(1024 * 1024), b""):
            image_digest.update(chunk)
    digest = image_digest.hexdigest().upper()

    with image_path.open("rb") as image:
        image.seek(0, 2)
        image_bytes = image.tell()
        require(image_bytes % SECTOR_SIZE == 0, "image is not 512-byte aligned")
        total_sectors = image_bytes // SECTOR_SIZE

        def read_sector(lba: int) -> bytes:
            require(0 <= lba < total_sectors, f"LBA {lba} is outside the image")
            image.seek(lba * SECTOR_SIZE)
            sector = image.read(SECTOR_SIZE)
            require(len(sector) == SECTOR_SIZE, f"short read at LBA {lba}")
            return sector

        mbr = read_sector(0)
        require(mbr[510:512] == b"\x55\xAA", "protective MBR signature is missing")
        protective = mbr[446:462]
        require(protective[4] == 0xEE, "MBR does not contain a protective EE entry")
        require(u32(protective, 8) == 1, "protective MBR does not start at LBA 1")
        require(u32(protective, 12) == min(total_sectors - 1, 0xFFFFFFFF),
                "protective MBR length does not cover the image")

        primary = read_sector(1)
        backup = read_sector(total_sectors - 1)
        require(primary[:8] == b"EFI PART", "primary GPT signature is invalid")
        require(backup[:8] == b"EFI PART", "backup GPT signature is invalid")

        def validate_header(header: bytes, expected_current: int,
                            expected_backup: int) -> tuple[int, int, int, int, bytes, bytes]:
            header_size = u32(header, 12)
            stored_crc = u32(header, 16)
            require(92 <= header_size <= SECTOR_SIZE, "GPT header size is invalid")
            copy = bytearray(header[:header_size])
            copy[16:20] = b"\0\0\0\0"
            require(zlib.crc32(copy) & 0xFFFFFFFF == stored_crc,
                    "GPT header CRC does not match")
            current = u64(header, 24)
            back = u64(header, 32)
            first_usable = u64(header, 40)
            last_usable = u64(header, 48)
            disk_guid = header[56:72]
            entries_lba = u64(header, 72)
            entry_count = u32(header, 80)
            entry_size = u32(header, 84)
            entries_crc = u32(header, 88)
            require(current == expected_current and back == expected_backup,
                    "GPT current/backup LBA values are inconsistent")
            require(1 <= first_usable <= last_usable < total_sectors,
                    "GPT usable LBA range is invalid")
            require(entry_count == 128 and entry_size == 128,
                    "GPT entry geometry differs from the proof format")
            array_bytes = entry_count * entry_size
            image.seek(entries_lba * SECTOR_SIZE)
            entries = image.read(array_bytes)
            require(len(entries) == array_bytes, "GPT entry array is truncated")
            require(zlib.crc32(entries) & 0xFFFFFFFF == entries_crc,
                    "GPT entry-array CRC does not match")
            return first_usable, last_usable, entries_lba, entry_size, disk_guid, entries

        p_first, p_last, p_entries_lba, entry_size, disk_guid, p_entries = \
            validate_header(primary, 1, total_sectors - 1)
        b_first, b_last, b_entries_lba, _, backup_guid, b_entries = \
            validate_header(backup, total_sectors - 1, 1)
        require((p_first, p_last, disk_guid) == (b_first, b_last, backup_guid),
                "primary and backup GPT identities disagree")
        require(p_entries_lba == 2, "primary GPT entry array is not at LBA 2")
        expected_array_sectors = (128 * entry_size + SECTOR_SIZE - 1) // SECTOR_SIZE
        require(b_entries_lba == total_sectors - 1 - expected_array_sectors,
                "backup GPT entry array is not adjacent to the backup header")
        require(p_entries == b_entries, "primary and backup GPT arrays differ")

        partitions = []
        for index in range(128):
            entry = p_entries[index * entry_size:(index + 1) * entry_size]
            if entry[:16] != GPT_BASIC_DATA:
                continue
            name_bytes = entry[56:128]
            name = name_bytes.decode("utf-16le", errors="strict").split("\0", 1)[0]
            partitions.append((index + 1, u64(entry, 32), u64(entry, 40), name, entry))
        require(len(partitions) == 1, "proof disk does not contain exactly one GPT partition")
        partition = next((entry for entry in partitions if entry[3] == PARTITION_NAME), None)
        require(partition is not None, "named DM13 GPT partition was not found")
        partition_number, start_lba, end_lba, _, _ = partition
        require(p_first <= start_lba <= end_lba <= p_last,
                "DM13 partition escapes the GPT usable range")
        primary_array_end = p_entries_lba + expected_array_sectors
        require(start_lba > primary_array_end,
                "DM13 partition does not leave a leading canary gap")
        image.seek(primary_array_end * SECTOR_SIZE)
        gap_bytes = (start_lba - primary_array_end) * SECTOR_SIZE
        remaining = gap_bytes
        while remaining:
            chunk = image.read(min(1024 * 1024, remaining))
            require(chunk and not any(chunk), "sectors before the partition were modified")
            remaining -= len(chunk)
        trailing_start = end_lba + 1
        trailing_count = b_entries_lba - trailing_start
        image.seek(trailing_start * SECTOR_SIZE)
        remaining = trailing_count * SECTOR_SIZE
        while remaining:
            chunk = image.read(min(1024 * 1024, remaining))
            require(chunk and not any(chunk), "sectors after the partition were modified")
            remaining -= len(chunk)

        boot = read_sector(start_lba)
        require(boot[510:512] == b"\x55\xAA", "FAT32 boot-sector signature is invalid")
        bytes_per_sector = u16(boot, 11)
        sectors_per_cluster = boot[13]
        reserved = u16(boot, 14)
        fat_count = boot[16]
        total_fat16 = u16(boot, 22)
        total_fat32 = u32(boot, 36)
        total_fs16 = u16(boot, 19)
        total_fs32 = u32(boot, 32)
        root_cluster = u32(boot, 44)
        fsinfo_sector = u16(boot, 48)
        backup_boot_sector = u16(boot, 50)
        volume_label = boot[71:82]
        require(bytes_per_sector == SECTOR_SIZE, "FAT32 bytes-per-sector is not 512")
        require(sectors_per_cluster and sectors_per_cluster & (sectors_per_cluster - 1) == 0,
                "FAT32 sectors-per-cluster is invalid")
        require(reserved > 0 and fat_count == 2, "FAT32 reserved/FAT geometry is invalid")
        require(total_fat16 == 0 and total_fs16 == 0 and total_fs32 > 0,
                "volume does not use FAT32 total-sector fields")
        require(total_fs32 <= end_lba - start_lba + 1,
                "FAT32 volume exceeds its GPT partition")
        require(u32(boot, 28) == start_lba,
                "FAT32 hidden-sector value does not match the GPT partition start")
        require(volume_label == b"DM13PROOF  ", "FAT32 volume label is unexpected")
        require(boot[82:90] == b"FAT32   ", "FAT32 system identifier is missing")
        require(backup_boot_sector > 0 and backup_boot_sector < reserved,
                "FAT32 backup boot-sector location is invalid")

        backup_boot = read_sector(start_lba + backup_boot_sector)
        require(backup_boot == boot, "FAT32 backup boot sector differs from the primary")
        fsinfo = read_sector(start_lba + fsinfo_sector)
        backup_fsinfo = read_sector(start_lba + backup_boot_sector + fsinfo_sector)
        require(fsinfo == backup_fsinfo, "primary and backup FSInfo sectors differ")
        require(u32(fsinfo, 0) == 0x41615252 and
                u32(fsinfo, 484) == 0x61417272 and
                u32(fsinfo, 508) == 0xAA550000,
                "FSInfo signatures are invalid")

        fat_size = total_fat32
        first_fat_lba = start_lba + reserved
        second_fat_lba = first_fat_lba + fat_size
        fat_bytes = fat_size * SECTOR_SIZE
        image.seek(first_fat_lba * SECTOR_SIZE)
        first_fat = image.read(fat_bytes)
        image.seek(second_fat_lba * SECTOR_SIZE)
        second_fat = image.read(fat_bytes)
        require(len(first_fat) == fat_bytes and len(second_fat) == fat_bytes,
                "FAT table is truncated")
        require(first_fat == second_fat, "the two FAT copies differ")
        first_data_lba = start_lba + reserved + fat_count * fat_size
        cluster_count = (total_fs32 - (first_data_lba - start_lba)) // sectors_per_cluster
        require(cluster_count >= 65525, "FAT32 cluster count is below the standard minimum")
        require(root_cluster >= 2, "FAT32 root cluster is invalid")

        def fat_entry(cluster: int) -> int:
            offset = cluster * 4
            require(offset + 4 <= len(first_fat), f"cluster {cluster} is outside the FAT")
            return u32(first_fat, offset) & CLUSTER_MASK

        require(fat_entry(0) >= 0x0FFFFFF8 and fat_entry(1) >= EOC,
                "FAT reserved entries are invalid")

        def cluster_bytes(cluster: int) -> bytes:
            lba = first_data_lba + (cluster - 2) * sectors_per_cluster
            require(lba + sectors_per_cluster <= start_lba + total_fs32,
                    f"cluster {cluster} escapes the FAT32 volume")
            image.seek(lba * SECTOR_SIZE)
            return image.read(sectors_per_cluster * SECTOR_SIZE)

        def chain(start_cluster: int) -> list[int]:
            found: list[int] = []
            current = start_cluster
            while True:
                require(2 <= current < cluster_count + 2,
                        "cluster chain contains an out-of-range cluster")
                require(current not in found, "cluster chain contains a loop")
                found.append(current)
                next_cluster = fat_entry(current)
                if next_cluster >= EOC:
                    return found
                require(next_cluster >= 2, "cluster chain ends at an invalid entry")
                current = next_cluster

        def find_short_entry(start_cluster: int, name: bytes) -> bytes | None:
            for cluster in chain(start_cluster):
                data = cluster_bytes(cluster)
                for offset in range(0, len(data), 32):
                    entry = data[offset:offset + 32]
                    if entry[0] == 0:
                        return None
                    if entry[0] == 0xE5 or entry[11] == 0x0F:
                        continue
                    if entry[:11] == name:
                        return entry
            return None

        chain(root_cluster)
        directory = find_short_entry(root_cluster, DIRECTORY_NAME)
        require(directory is not None and directory[11] & 0x10,
                "DM13 directory entry is missing")
        directory_cluster = (u16(directory, 20) << 16) | u16(directory, 26)
        require(directory_cluster >= 2 and fat_entry(directory_cluster) >= EOC,
                "DM13 directory cluster is unallocated")

        file_entry = find_short_entry(directory_cluster, FILE_NAME)
        require(file_entry is not None and not file_entry[11] & 0x10,
                "proof file entry is missing or is a directory")
        file_cluster = (u16(file_entry, 20) << 16) | u16(file_entry, 26)
        file_size = u32(file_entry, 28)
        require(file_size == len(PAYLOAD), "proof file size is unexpected")
        file_chain = chain(file_cluster)
        cluster_size = sectors_per_cluster * SECTOR_SIZE
        require(len(file_chain) == (file_size + cluster_size - 1) // cluster_size,
                "proof file cluster chain length disagrees with its size")
        contents = b"".join(cluster_bytes(cluster) for cluster in file_chain)[:file_size]
        require(contents == PAYLOAD, "proof file contents differ from the deterministic payload")

        # The raw image began entirely zeroed. Only FAT metadata and clusters
        # reachable from the root, proof directory, and proof file may contain
        # nonzero bytes inside the formatted partition.
        allowed_data_sectors: set[int] = set()
        for cluster in chain(root_cluster) + chain(directory_cluster) + file_chain:
            cluster_lba = first_data_lba + (cluster - 2) * sectors_per_cluster
            allowed_data_sectors.update(
                range(cluster_lba, cluster_lba + sectors_per_cluster))
        data_sector_count = total_fs32 - (first_data_lba - start_lba)
        cursor_lba = first_data_lba
        remaining_sectors = data_sector_count
        while remaining_sectors:
            count = min(2048, remaining_sectors)
            image.seek(cursor_lba * SECTOR_SIZE)
            data = image.read(count * SECTOR_SIZE)
            require(len(data) == count * SECTOR_SIZE,
                    "FAT32 data region is truncated during isolation check")
            for offset in range(count):
                lba = cursor_lba + offset
                if lba in allowed_data_sectors:
                    continue
                sector = data[offset * SECTOR_SIZE:(offset + 1) * SECTOR_SIZE]
                require(not any(sector),
                        f"unexpected nonzero sector inside FAT32 data area at LBA {lba}")
            cursor_lba += count
            remaining_sectors -= count

        print(f"image={image_path}")
        print(f"image_sha256={digest}")
        print(f"image_bytes={image_bytes}")
        print(f"protective_mbr=PASS signature=PASS start_lba=1")
        print(f"gpt_primary_header_crc=PASS gpt_backup_header_crc=PASS")
        print(f"gpt_entry_array_crc=PASS copies_agree=PASS disk_guid={uuid.UUID(bytes_le=disk_guid)}")
        print(f"partition=PASS number={partition_number} start_lba={start_lba} end_lba={end_lba} name={PARTITION_NAME}")
        print(f"leading_canary_gap=PASS first_lba={primary_array_end} last_lba={start_lba - 1} bytes={gap_bytes}")
        print(f"fat32_bpb=PASS bps={bytes_per_sector} spc={sectors_per_cluster} clusters={cluster_count} label=DM13PROOF")
        print(f"fsinfo=PASS backup_boot=PASS fat_mirror=PASS")
        print(f"root_directory=PASS dm13_directory=PASS proof_file=PASS payload_bytes={file_size}")
        print("metadata_isolation=PASS gaps_zero=PASS unallocated_data_zero=PASS")
        print("result=PASS read_only_inspection=yes")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, struct.error, UnicodeError, VerificationError) as error:
        print(f"result=FAIL detail={error}", file=sys.stderr)
        raise SystemExit(1)
