#!/usr/bin/env python3
"""Read-only verification of DM27's before, after, and cold-restart images."""

from __future__ import annotations

import argparse
import hashlib
import struct
import zlib
from pathlib import Path

DEFAULT_SECTOR_SIZE = 512
GPT_BASIC_DATA = bytes.fromhex("A2A0D0EBE5B9334487C068B6B72699C7")
PARTITION_NAME = "DM9 QEMU Proof"
OLD_LABEL = b"DM9PROOF   "
NEW_LABEL = b"DM27FRESH  "
OLD_PAYLOAD = b"guideXOS DM9 QEMU proof 001\r\n"
NEW_PAYLOAD = OLD_PAYLOAD
MULTI_BYTES = 96 * 1024
EOC = 0x0FFFFFF8
MASK = 0x0FFFFFFF


class VerificationError(RuntimeError):
    pass


def require(ok: bool, message: str) -> None:
    if not ok:
        raise VerificationError(message)


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


class Image:
    def __init__(self, path: Path, sector_size: int):
        self.path = path.resolve(strict=True)
        self.stream = self.path.open("rb")
        self.sector_size = sector_size
        self.size = self.path.stat().st_size
        require(self.size % sector_size == 0,
                f"{self.path.name}: image is not sector aligned")
        self.sectors = self.size // sector_size

    def close(self) -> None:
        self.stream.close()

    def read(self, offset: int, size: int) -> bytes:
        self.stream.seek(offset)
        data = self.stream.read(size)
        require(len(data) == size, f"{self.path.name}: short read at byte {offset}")
        return data

    def sector(self, lba: int) -> bytes:
        require(0 <= lba < self.sectors, f"{self.path.name}: LBA {lba} outside image")
        return self.read(lba * self.sector_size, self.sector_size)

    def sha256(self) -> str:
        digest = hashlib.sha256()
        self.stream.seek(0)
        for chunk in iter(lambda: self.stream.read(1024 * 1024), b""):
            digest.update(chunk)
        return digest.hexdigest().upper()


def gpt_copy(image: Image, lba: int, primary: bool) -> dict:
    header = image.sector(lba)
    require(header[:8] == b"EFI PART", f"{image.path.name}: missing GPT header at LBA {lba}")
    header_size = u32(header, 12)
    require(92 <= header_size <= image.sector_size, "invalid GPT header size")
    header_crc = u32(header, 16)
    check = bytearray(header[:header_size])
    check[16:20] = b"\0" * 4
    require((zlib.crc32(check) & 0xFFFFFFFF) == header_crc, "GPT header CRC mismatch")
    current, backup, first, last = struct.unpack_from("<QQQQ", header, 24)
    expected_current = 1 if primary else image.sectors - 1
    expected_backup = image.sectors - 1 if primary else 1
    require((current, backup) == (expected_current, expected_backup), "GPT header pointers are invalid")
    array_lba = u64(header, 72)
    count, entry_size, array_crc = struct.unpack_from("<III", header, 80)
    require(count == 128 and entry_size == 128, "unexpected GPT entry geometry")
    array = image.read(array_lba * image.sector_size, count * entry_size)
    require((zlib.crc32(array) & 0xFFFFFFFF) == array_crc, "GPT array CRC mismatch")
    return {"header": header, "array_lba": array_lba, "array": array,
            "first": first, "last": last, "guid": header[56:72],
            "count": count, "entry_size": entry_size}


def partition_identity(image: Image) -> tuple[dict, tuple]:
    mbr = image.sector(0)
    require(mbr[510:512] == b"\x55\xaa", "protective MBR signature is invalid")
    protective = mbr[446:462]
    require(protective[4] == 0xEE and u32(protective, 8) == 1,
            "protective MBR entry is invalid")
    primary = gpt_copy(image, 1, True)
    backup = gpt_copy(image, image.sectors - 1, False)
    require(primary["array"] == backup["array"], "primary and backup GPT arrays differ")
    require(primary["guid"] == backup["guid"] and
            (primary["first"], primary["last"]) == (backup["first"], backup["last"]),
            "primary and backup GPT identity differs")
    found = []
    for slot in range(primary["count"]):
        entry = primary["array"][slot * 128:(slot + 1) * 128]
        if entry[:16] != GPT_BASIC_DATA:
            continue
        name = entry[56:128].decode("utf-16le", errors="strict").split("\0", 1)[0]
        if name == PARTITION_NAME:
            found.append((slot + 1, entry, u64(entry, 32), u64(entry, 40)))
    require(len(found) == 1, "expected exactly one named DM9 GPT partition")
    number, entry, start, end = found[0]
    require(primary["first"] <= start <= end <= primary["last"],
            "DM9 partition is outside GPT usable LBAs")
    identity = (primary["guid"], primary["first"], primary["last"],
                primary["array"], entry, number, start, end)
    return {"mbr": mbr, "primary": primary, "backup": backup,
            "number": number, "entry": entry, "start": start, "end": end}, identity


def fat_volume(image: Image, start: int, end: int, expected_label: bytes) -> dict:
    boot = image.sector(start)
    require(boot[510:512] == b"\x55\xaa", "FAT32 boot-sector signature missing")
    bps, spc, reserved, fats = u16(boot, 11), boot[13], u16(boot, 14), boot[16]
    fat_sectors = u32(boot, 36)
    total = u32(boot, 32)
    root = u32(boot, 44)
    fsinfo_sector, backup_boot = u16(boot, 48), u16(boot, 50)
    require(bps == image.sector_size and spc > 0 and (spc & (spc - 1)) == 0,
            "FAT32 sector/cluster geometry is invalid")
    require(reserved >= 8 and fats == 2 and fat_sectors > 0 and total > 0,
            "FAT32 reserved/FAT geometry is invalid")
    require(total <= end - start + 1 and u32(boot, 28) == start,
            "FAT32 volume does not match its GPT partition")
    require(boot[71:82] == expected_label and boot[82:90] == b"FAT32   ",
            "FAT32 volume label or system identifier is wrong")
    require(0 < fsinfo_sector < reserved and 0 < backup_boot < reserved,
            "FAT32 FSInfo or backup boot location is invalid")
    backup = image.sector(start + backup_boot)
    require(backup == boot, "FAT32 primary and backup boot sectors differ")
    fsinfo = image.sector(start + fsinfo_sector)
    backup_fsinfo_sector = u16(boot, 50) + fsinfo_sector
    backup_fsinfo = image.sector(start + backup_fsinfo_sector)
    for info in (fsinfo, backup_fsinfo):
        require(u32(info, 0) == 0x41615252 and u32(info, 484) == 0x61417272 and
                u32(info, 508) == 0xAA550000, "FAT32 FSInfo signatures are invalid")
    fat1_lba = start + reserved
    fat2_lba = fat1_lba + fat_sectors
    fat_size_bytes = fat_sectors * bps
    fat1 = image.read(fat1_lba * bps, fat_size_bytes)
    fat2 = image.read(fat2_lba * bps, fat_size_bytes)
    require(fat1 == fat2, "FAT32 mirrored FAT copies differ")
    first_data = start + reserved + fats * fat_sectors
    clusters = (total - (first_data - start)) // spc
    require(clusters >= 65525, "volume does not meet FAT32 minimum cluster count")

    def fat_entry(cluster: int) -> int:
        offset = cluster * 4
        require(offset + 4 <= len(fat1), "cluster index exceeds the FAT")
        return u32(fat1, offset) & MASK

    require(fat_entry(0) >= 0x0FFFFFF8 and fat_entry(1) >= EOC,
            "FAT32 reserved entries are invalid")

    def chain(first: int) -> list[int]:
        result = []
        current = first
        while True:
            require(2 <= current < clusters + 2 and current not in result,
                    "invalid or cyclic FAT32 cluster chain")
            result.append(current)
            nxt = fat_entry(current)
            if nxt >= EOC:
                return result
            require(nxt >= 2, "FAT32 cluster chain terminates at a free entry")
            current = nxt

    def cluster_data(cluster: int) -> bytes:
        lba = first_data + (cluster - 2) * spc
        require(lba + spc <= start + total, "FAT32 cluster extends past volume")
        return image.read(lba * bps, spc * bps)

    def short_entries(first_cluster: int) -> list[bytes]:
        entries = []
        for cluster in chain(first_cluster):
            data = cluster_data(cluster)
            for offset in range(0, len(data), 32):
                entry = data[offset:offset + 32]
                if entry[0] == 0:
                    return entries
                if entry[0] == 0xE5 or entry[11] == 0x0F:
                    continue
                entries.append(entry)
        return entries

    return {"boot": boot, "root": root, "reserved": reserved, "fat_sectors": fat_sectors,
            "fat1_lba": fat1_lba, "fat2_lba": fat2_lba, "fat_bytes": fat_size_bytes,
            "first_data": first_data, "spc": spc, "total": total, "clusters": clusters,
            "fat": fat1, "fat_entry": fat_entry, "chain": chain,
            "cluster_data": cluster_data, "short_entries": short_entries}


def find_short(entries: list[bytes], name: bytes) -> bytes:
    matches = [entry for entry in entries if entry[:11] == name]
    require(len(matches) == 1, f"expected one FAT32 entry named {name!r}")
    return matches[0]


def entry_cluster(entry: bytes) -> int:
    return (u16(entry, 20) << 16) | u16(entry, 26)


def multi_payload(size: int) -> bytes:
    return bytes(((index * 37 + (index >> 8) * 13 + 0x5A) & 0xFF)
                 for index in range(size))


def file_contents(volume: dict, entry: bytes) -> tuple[bytes, list[int]]:
    size = u32(entry, 28)
    clusters = volume["chain"](entry_cluster(entry))
    content = b"".join(volume["cluster_data"](cluster) for cluster in clusters)[:size]
    return content, clusters


def unchanged_except(before: Image, after: Image, allowed: list[tuple[int, int]]) -> int:
    ranges = sorted(allowed)
    require(all(0 <= start < end <= before.sectors for start, end in ranges),
            "allowed sector range is invalid")
    def is_allowed(lba: int) -> bool:
        return any(start <= lba < end for start, end in ranges)
    changed = 0
    sector_size = before.sector_size
    for first in range(0, before.sectors, 2048):
        count = min(2048, before.sectors - first)
        old = before.read(first * sector_size, count * sector_size)
        new = after.read(first * sector_size, count * sector_size)
        if old == new:
            continue
        for offset in range(0, len(old), sector_size):
            if old[offset:offset + sector_size] == new[offset:offset + sector_size]:
                continue
            lba = first + offset // sector_size
            changed += 1
            require(is_allowed(lba), f"unexpected changed sector at LBA {lba}")
    return changed


def verify(before_path: Path, after_path: Path, final_path: Path,
           sector_size: int = DEFAULT_SECTOR_SIZE) -> list[str]:
    before = Image(before_path, sector_size)
    after = Image(after_path, sector_size)
    final = Image(final_path, sector_size)
    try:
        require(before.size == after.size == final.size, "checkpoint image sizes differ")
        old_gpt, old_identity = partition_identity(before)
        new_gpt, new_identity = partition_identity(after)
        final_gpt, final_identity = partition_identity(final)
        require(old_identity == new_identity == final_identity,
                "GPT disk or partition identity changed during reformat")
        for label, image, gpt in (("post", after, new_gpt), ("final", final, final_gpt)):
            require(old_gpt["mbr"] == gpt["mbr"], f"protective MBR changed in {label} checkpoint")
            require(old_gpt["primary"]["header"] == gpt["primary"]["header"] and
                    old_gpt["backup"]["header"] == gpt["backup"]["header"] and
                    old_gpt["primary"]["array"] == gpt["primary"]["array"] and
                    old_gpt["backup"]["array"] == gpt["backup"]["array"],
                    f"GPT bytes changed in {label} checkpoint")
        start, end = old_gpt["start"], old_gpt["end"]
        old = fat_volume(before, start, end, OLD_LABEL)
        old_volume_id = u32(old["boot"], 67)
        require(old_volume_id != 0, "pre-reformat FAT32 volume ID is zero")
        root_entries = old["short_entries"](old["root"])
        old_dir = find_short(root_entries, b"DM9        ")
        require(old_dir[11] & 0x10, "old DM9 fixture directory flag is missing")
        directory_entries = old["short_entries"](entry_cluster(old_dir))
        old_file = find_short(directory_entries, b"PROOF   BIN")
        old_content, old_file_clusters = file_contents(old, old_file)
        old_expected = OLD_PAYLOAD if sector_size == 512 else multi_payload(MULTI_BYTES)
        require(old_content == old_expected, "pre-reformat fixture payload is incorrect")
        multi_file = find_short(directory_entries, b"MULTI   BIN")
        multi_content, multi_clusters = file_contents(old, multi_file)
        require(multi_content == multi_payload(MULTI_BYTES),
                "pre-reformat deterministic multi-cluster payload is incorrect")
        require(len(multi_clusters) > 1, "old multi-cluster file occupies only one cluster")
        old_data_lba = old["first_data"] + (old_file_clusters[0] - 2) * old["spc"]
        old_multi_data_lba = old["first_data"] + (multi_clusters[0] - 2) * old["spc"]
        old_canary = before.read(old_data_lba * sector_size, sector_size)
        old_multi_canary = before.read(old_multi_data_lba * sector_size, sector_size)

        fresh = fat_volume(after, start, end, NEW_LABEL)
        new_volume_id = u32(fresh["boot"], 67)
        require(new_volume_id != 0 and new_volume_id != old_volume_id,
                "fresh FAT32 volume ID was not replaced")
        require(fresh["boot"][28:32] == old["boot"][28:32] and
                fresh["boot"][14:16] == old["boot"][14:16],
                "fresh BPB changed the partition offset or reserved geometry")
        primary_fsinfo = after.sector(start + u16(fresh["boot"], 48))
        backup_fsinfo = after.sector(start + u16(fresh["boot"], 50) +
                                     u16(fresh["boot"], 48))
        require(primary_fsinfo == backup_fsinfo,
                "primary and backup FSInfo sectors differ")
        metadata_reserved = {0, u16(fresh["boot"], 48),
                             u16(fresh["boot"], 50),
                             u16(fresh["boot"], 50) + u16(fresh["boot"], 48)}
        for relative in range(fresh["reserved"]):
            if relative not in metadata_reserved:
                require(not any(after.sector(start + relative)),
                        f"new reserved sector {relative} was not initialized to zero")
        fresh_entries = fresh["short_entries"](fresh["root"])
        label_entries = [entry for entry in fresh_entries if entry[11] & 0x08]
        file_entries = [entry for entry in fresh_entries if not entry[11] & 0x08]
        require(len(label_entries) == 1 and label_entries[0][:11] == NEW_LABEL,
                "fresh root volume-label entry is missing or incorrect")
        fresh_file = find_short(file_entries, b"FRESH   BIN")
        require(len(file_entries) == 1 and not (fresh_file[11] & 0x10),
                "fresh root contains unexpected old paths or a non-file entry")
        new_content, new_file_clusters = file_contents(fresh, fresh_file)
        new_expected = NEW_PAYLOAD if sector_size == 512 else multi_payload(MULTI_BYTES)
        require(new_content == new_expected, "fresh file payload is incorrect")
        cluster_bytes = sector_size * fresh["spc"]
        expected_file_clusters = (len(new_expected) + cluster_bytes - 1) // cluster_bytes
        expected_chain = list(range(3, 3 + expected_file_clusters))
        require(fresh["root"] == 2 and entry_cluster(fresh_file) == 3 and
                new_file_clusters == expected_chain,
                "fresh root and file allocation are unexpected")
        for cluster in range(2, fresh["clusters"] + 2):
            value = fresh["fat_entry"](cluster)
            if cluster == 2:
                require(value >= EOC, "fresh root cluster is not end-of-chain")
            elif cluster in expected_chain[:-1]:
                require(value == cluster + 1,
                        f"fresh file chain breaks at cluster {cluster}")
            elif cluster == expected_chain[-1]:
                require(value >= EOC,
                        f"fresh file cluster {cluster} is not end-of-chain")
            else:
                require(value == 0, f"unexpected allocated cluster {cluster} remains in fresh FAT")

        allowed = [
            (start, start + fresh["reserved"]),
            (fresh["fat1_lba"], fresh["fat1_lba"] + fresh["fat_sectors"]),
            (fresh["fat2_lba"], fresh["fat2_lba"] + fresh["fat_sectors"]),
            (fresh["first_data"], fresh["first_data"] + fresh["spc"]),
        ]
        for cluster in expected_chain:
            cluster_start = fresh["first_data"] + (cluster - 2) * fresh["spc"]
            allowed.append((cluster_start, cluster_start + fresh["spc"]))
        changed = unchanged_except(before, after, allowed)
        require(after.read(old_data_lba * sector_size, sector_size) == old_canary,
                "old named-file data sector changed during quick reformat")
        require(after.read(old_multi_data_lba * sector_size, sector_size) == old_multi_canary,
                "old multi-cluster data sector changed during quick reformat")
        require(final.read(old_data_lba * sector_size, sector_size) == old_canary,
                "old named-file data sector changed after cold restart")
        require(final.read(old_multi_data_lba * sector_size, sector_size) == old_multi_canary,
                "old multi-cluster data sector changed after cold restart")
        require(after.sha256() == final.sha256(),
                "cold-restart read-only mount changed the disk image")
        final_fresh = fat_volume(final, start, end, NEW_LABEL)
        require(u32(final_fresh["boot"], 67) == new_volume_id,
                "cold restart changed the new FAT32 volume ID")
        final_entries = final_fresh["short_entries"](final_fresh["root"])
        final_file = find_short(final_entries, b"FRESH   BIN")
        final_content, _ = file_contents(final_fresh, final_file)
        require(final_content == new_expected, "fresh payload failed after cold restart")
        return [
            f"DM27 verifier: PASS sectorSize={sector_size} ({before.path.name} -> {after.path.name} -> {final.path.name})",
            f"GPT identity preserved; partition number={old_gpt['number']} startLba={start} endLba={end}",
            f"FAT32 label {OLD_LABEL.decode().strip()} -> {NEW_LABEL.decode().strip()} and volumeId={old_volume_id:08X}->{new_volume_id:08X}; mirrored FATs, BPB, reserved region, FSInfo, and backup metadata verified",
            f"Old file and multi-cluster data canaries unchanged at LBAs {old_data_lba},{old_multi_data_lba}; SHA256={hashlib.sha256(old_canary).hexdigest().upper()},{hashlib.sha256(old_multi_canary).hexdigest().upper()}",
            f"Changed sectors confined to reformat metadata/root/new-file regions ({changed} sectors changed)",
            f"Fresh root contains only its volume label and exact FRESH.BIN payload ({len(new_content)} bytes); no old named paths",
            "Cold-restart image is byte-identical to the post-reformat checkpoint",
            f"SHA256 before={before.sha256()} after={after.sha256()} final={final.sha256()}",
        ]
    finally:
        before.close()
        after.close()
        final.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path, help="read-only pre-reformat raw image")
    parser.add_argument("after", type=Path, help="read-only post-reformat raw image")
    parser.add_argument("final", type=Path, help="read-only post-cold-restart raw image")
    parser.add_argument("--sector-size", type=int, choices=(512, 4096),
                        default=DEFAULT_SECTOR_SIZE)
    args = parser.parse_args()
    try:
        for line in verify(args.before, args.after, args.final, args.sector_size):
            print(line)
    except (OSError, VerificationError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
