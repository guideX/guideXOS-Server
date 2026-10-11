#!/usr/bin/env python3
"""Create and inspect the C167 raw FAT proof disk used by the QEMU runner."""

import argparse
import hashlib
import json
import math
import pathlib
import struct
import zlib


SECTOR_SIZE = 512
PARTITION_START = 2048
PARTITION_SECTORS = 262144
SECTORS_PER_CLUSTER = 32
RESERVED_SECTORS = 1
FAT_COPIES = 2
ROOT_ENTRY_COUNT = 512
ROOT_DIR_SECTORS = ROOT_ENTRY_COUNT * 32 // SECTOR_SIZE
FAT_SECTORS = 32
ROOT_DIR_LBA = PARTITION_START + RESERVED_SECTORS + FAT_COPIES * FAT_SECTORS
DATA_LBA = ROOT_DIR_LBA + ROOT_DIR_SECTORS
CLUSTER_BYTES = SECTOR_SIZE * SECTORS_PER_CLUSTER


def put_at(stream, offset, data):
    stream.seek(offset)
    stream.write(data)


def short_entry(name, attributes, first_cluster, size=0):
    base, dot, extension = name.partition(".")
    if len(base) > 8 or len(extension) > 3:
        raise ValueError(f"not an 8.3 name: {name}")
    encoded = base.upper().ljust(8).encode("ascii")
    encoded += extension.upper().ljust(3).encode("ascii")
    entry = bytearray(32)
    entry[:11] = encoded
    entry[11] = attributes
    struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return entry


def set_fat_entry(fat, cluster, value):
    struct.pack_into("<H", fat, cluster * 2, value)


def create_image(image_path, bootloader_path, kernel_path, ramdisk_path,
                 settings_path):
    image_path = pathlib.Path(image_path)
    sources = {
        "BOOTX64.EFI": pathlib.Path(bootloader_path).read_bytes(),
        "KERNEL.ELF": pathlib.Path(kernel_path).read_bytes(),
        "RAMDISK.IMG": pathlib.Path(ramdisk_path).read_bytes(),
        "GXSETT.BIN": pathlib.Path(settings_path).read_bytes(),
    }
    if len(sources["GXSETT.BIN"]) != 26:
        raise ValueError("GXSETT.BIN must retain the C146 v2 26-byte record")

    cluster_count = (PARTITION_SECTORS - RESERVED_SECTORS -
                     FAT_COPIES * FAT_SECTORS - ROOT_DIR_SECTORS) // SECTORS_PER_CLUSTER
    if cluster_count + 2 > FAT_SECTORS * SECTOR_SIZE // 2:
        raise ValueError("FAT16 table is too small for the selected volume")

    mbr = bytearray(SECTOR_SIZE)
    partition = 446
    mbr[partition] = 0
    mbr[partition + 1:partition + 4] = b"\xfe\xff\xff"
    mbr[partition + 4] = 0xEF  # EFI System Partition, FAT16 volume
    mbr[partition + 5:partition + 8] = b"\xfe\xff\xff"
    struct.pack_into("<I", mbr, partition + 8, PARTITION_START)
    struct.pack_into("<I", mbr, partition + 12, PARTITION_SECTORS)
    mbr[510:512] = b"\x55\xaa"

    boot_sector = bytearray(SECTOR_SIZE)
    boot_sector[0:3] = b"\xeb\x3c\x90"
    boot_sector[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", boot_sector, 11, SECTOR_SIZE)
    boot_sector[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot_sector, 14, RESERVED_SECTORS)
    boot_sector[16] = FAT_COPIES
    struct.pack_into("<H", boot_sector, 17, ROOT_ENTRY_COUNT)
    struct.pack_into("<H", boot_sector, 19, 0)
    boot_sector[21] = 0xF8
    struct.pack_into("<H", boot_sector, 22, FAT_SECTORS)
    struct.pack_into("<H", boot_sector, 24, 63)
    struct.pack_into("<H", boot_sector, 26, 255)
    struct.pack_into("<I", boot_sector, 28, PARTITION_START)
    struct.pack_into("<I", boot_sector, 32, PARTITION_SECTORS)
    boot_sector[36] = 0x80
    boot_sector[38] = 0x29
    struct.pack_into("<I", boot_sector, 39, 0xC1672026)
    boot_sector[43:54] = b"GUIDEXOS   "
    boot_sector[54:62] = b"FAT16   "
    boot_sector[510:512] = b"\x55\xaa"

    fat = bytearray(FAT_SECTORS * SECTOR_SIZE)
    set_fat_entry(fat, 0, 0xFFF8)
    set_fat_entry(fat, 1, 0xFFFF)
    next_cluster = 2

    def allocate(content):
        nonlocal next_cluster
        content = bytes(content)
        count = max(1, math.ceil(len(content) / CLUSTER_BYTES))
        clusters = list(range(next_cluster, next_cluster + count))
        next_cluster += count
        if next_cluster > cluster_count + 2:
            raise ValueError("proof disk ran out of FAT16 clusters")
        for index, cluster in enumerate(clusters):
            following = clusters[index + 1] if index + 1 < len(clusters) else 0xFFFF
            set_fat_entry(fat, cluster, following)
        return clusters, content

    efi_cluster = allocate(b"")[0][0]
    boot_dir_cluster = allocate(b"")[0][0]
    bootloader_clusters, bootloader_bytes = allocate(sources.pop("BOOTX64.EFI"))
    file_clusters = {}
    for name, data in sources.items():
        clusters, _ = allocate(data)
        file_clusters[name] = (clusters, len(data))

    dot = short_entry(".", 0x10, boot_dir_cluster)
    dotdot = short_entry("..", 0x10, efi_cluster)
    boot_dir = bytearray(CLUSTER_BYTES)
    boot_dir[0:32] = dot
    boot_dir[32:64] = dotdot
    boot_dir[64:96] = short_entry(
        "BOOTX64.EFI", 0x20, bootloader_clusters[0], len(bootloader_bytes))

    efi_dir = bytearray(CLUSTER_BYTES)
    efi_dir[0:32] = short_entry(".", 0x10, efi_cluster)
    efi_dir[32:64] = short_entry("..", 0x10, 0)
    efi_dir[64:96] = short_entry("BOOT", 0x10, boot_dir_cluster)

    root_dir = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)
    root_entries = [short_entry("EFI", 0x10, efi_cluster)]
    for name, (clusters, size) in file_clusters.items():
        root_entries.append(short_entry(name, 0x20, clusters[0], size))
    for index, entry in enumerate(root_entries):
        root_dir[index * 32:(index + 1) * 32] = entry

    image_size = (PARTITION_START + PARTITION_SECTORS) * SECTOR_SIZE
    image_path.parent.mkdir(parents=True, exist_ok=True)
    with image_path.open("wb") as image:
        image.truncate(image_size)
        put_at(image, 0, mbr)
        put_at(image, PARTITION_START * SECTOR_SIZE, boot_sector)
        fat_offset = (PARTITION_START + RESERVED_SECTORS) * SECTOR_SIZE
        put_at(image, fat_offset, fat)
        put_at(image, fat_offset + len(fat), fat)
        put_at(image, ROOT_DIR_LBA * SECTOR_SIZE, root_dir)

        def write_chain(clusters, content):
            position = 0
            for cluster in clusters:
                chunk = content[position:position + CLUSTER_BYTES]
                if len(chunk) < CLUSTER_BYTES:
                    chunk += bytes(CLUSTER_BYTES - len(chunk))
                sector = DATA_LBA + (cluster - 2) * SECTORS_PER_CLUSTER
                put_at(image, sector * SECTOR_SIZE, chunk)
                position += min(CLUSTER_BYTES, len(content) - position)

        write_chain([efi_cluster], efi_dir)
        write_chain([boot_dir_cluster], boot_dir)
        write_chain(bootloader_clusters, bootloader_bytes)
        source_paths = {
            "KERNEL.ELF": pathlib.Path(kernel_path),
            "RAMDISK.IMG": pathlib.Path(ramdisk_path),
            "GXSETT.BIN": pathlib.Path(settings_path),
        }
        for name, (clusters, size) in file_clusters.items():
            position = 0
            with source_paths[name].open("rb") as source:
                for cluster in clusters:
                    chunk = source.read(CLUSTER_BYTES)
                    if len(chunk) < CLUSTER_BYTES:
                        chunk += bytes(CLUSTER_BYTES - len(chunk))
                    sector = DATA_LBA + (cluster - 2) * SECTORS_PER_CLUSTER
                    put_at(image, sector * SECTOR_SIZE, chunk)
                    position += len(chunk)
            if position < size:
                raise ValueError(f"incomplete image data for {name}")

    print(json.dumps({
        "image": str(image_path.resolve()),
        "sizeBytes": image_size,
        "partitionStartLba": PARTITION_START,
        "partitionSectors": PARTITION_SECTORS,
        "filesystem": "FAT16",
        "initialAssociationSlots": "none",
    }))


def inspect_slots(image_path):
    image_path = pathlib.Path(image_path)
    with image_path.open("rb") as stream:
        mbr = stream.read(SECTOR_SIZE)
        if mbr[510:512] != b"\x55\xaa":
            raise ValueError("proof disk has no MBR signature")
        partition_start = struct.unpack_from("<I", mbr, 454)[0]
        boot = (stream.seek(partition_start * SECTOR_SIZE), stream.read(SECTOR_SIZE))[1]
        if boot[510:512] != b"\x55\xaa" or boot[54:62] != b"FAT16   ":
            raise ValueError("proof disk does not contain the expected FAT16 partition")
        bps = struct.unpack_from("<H", boot, 11)[0]
        spc = boot[13]
        reserved = struct.unpack_from("<H", boot, 14)[0]
        fat_count = boot[16]
        root_entries = struct.unpack_from("<H", boot, 17)[0]
        fat_sectors = struct.unpack_from("<H", boot, 22)[0]
        root_sectors = (root_entries * 32 + bps - 1) // bps
        root_lba = partition_start + reserved + fat_count * fat_sectors
        data_lba = root_lba + root_sectors
        stream.seek((partition_start + reserved) * bps)
        fat = stream.read(fat_sectors * bps)
        stream.seek(root_lba * bps)
        root = stream.read(root_sectors * bps)

        found = {}
        for offset in range(0, len(root), 32):
            entry = root[offset:offset + 32]
            if not entry or entry[0] == 0:
                break
            if entry[0] == 0xE5 or entry[11] == 0x0F:
                continue
            base = entry[0:8].decode("ascii").rstrip()
            extension = entry[8:11].decode("ascii").rstrip()
            name = f"{base}.{extension}" if extension else base
            if name not in ("GXAS0.BIN", "GXAS1.BIN"):
                continue
            first_cluster = struct.unpack_from("<H", entry, 26)[0]
            size = struct.unpack_from("<I", entry, 28)[0]
            data = bytearray()
            cluster = first_cluster
            while cluster >= 2 and len(data) < size:
                sector = data_lba + (cluster - 2) * spc
                stream.seek(sector * bps)
                data.extend(stream.read(spc * bps))
                following = struct.unpack_from("<H", fat, cluster * 2)[0]
                if following >= 0xFFF8:
                    break
                if following < 2 or following == cluster:
                    raise ValueError(f"invalid cluster chain in {name}")
                cluster = following
            data = bytes(data[:size])
            if len(data) != size or size != 136:
                raise ValueError(f"{name} has invalid C166 v2 length {len(data)}")
            if data[:4] != b"GSA2" or struct.unpack_from("<H", data, 4)[0] != 2:
                raise ValueError(f"{name} has an invalid C166 v2 header")
            count = struct.unpack_from("<H", data, 6)[0]
            generation = struct.unpack_from("<Q", data, 8)[0]
            expected_crc = struct.unpack_from("<I", data, 132)[0]
            if generation == 0 or zlib.crc32(data[:132]) & 0xFFFFFFFF != expected_crc:
                raise ValueError(f"{name} has invalid generation or CRC")
            if count == 0:
                state = "NoOverride"
                application_id = ""
            elif count == 1:
                persisted_state = struct.unpack_from("<I", data, 32)[0]
                state = {1: "ApplicationOverride", 2: "Disabled"}.get(
                    persisted_state, "Invalid")
                application_id = data[36:132].split(b"\0", 1)[0].decode("ascii")
            else:
                raise ValueError(f"{name} has an invalid C166 v2 record count")
            slot = "A" if name == "GXAS0.BIN" else "B"
            found[slot] = {
                "slot": slot,
                "path": f"{image_path.resolve()}::{name}",
                "present": True,
                "sizeBytes": size,
                "sha256": hashlib.sha256(data).hexdigest().upper(),
                "generation": generation,
                "recordCount": count,
                "override": state,
                "applicationId": application_id,
            }

    return [found.get(slot, {
        "slot": slot,
        "path": f"{image_path.resolve()}::{name}",
        "present": False,
        "sizeBytes": 0,
        "sha256": None,
        "generation": None,
        "recordCount": None,
        "override": None,
        "applicationId": None,
    }) for slot, name in (("A", "GXAS0.BIN"), ("B", "GXAS1.BIN"))]


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create")
    create.add_argument("--image", required=True)
    create.add_argument("--bootloader", required=True)
    create.add_argument("--kernel", required=True)
    create.add_argument("--ramdisk", required=True)
    create.add_argument("--settings", required=True)
    slots = sub.add_parser("slots")
    slots.add_argument("--image", required=True)
    args = parser.parse_args()
    if args.command == "create":
        create_image(args.image, args.bootloader, args.kernel, args.ramdisk,
                     args.settings)
    else:
        print(json.dumps(inspect_slots(args.image), separators=(",", ":")))


if __name__ == "__main__":
    main()
