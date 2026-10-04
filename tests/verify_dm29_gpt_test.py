import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFIER = os.path.join(REPO_ROOT, "scripts", "verify-dm29-gpt.py")


def put32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def put64(data, offset, value):
    struct.pack_into("<Q", data, offset, value)


def build_image(path, sector_size, total_sectors, corrupt_backup=False):
    image = bytearray(sector_size * total_sectors)
    array = bytearray(128 * 128)
    array[0:16] = bytes(range(1, 17))
    array[16:32] = bytes(range(33, 49))
    put64(array, 32, 16 if sector_size == 4096 else 40)
    put64(array, 40, 17 if sector_size == 4096 else 48)
    name = "Verifier proof".encode("utf-16le")
    array[56:56 + len(name)] = name
    array_sectors = (len(array) + sector_size - 1) // sector_size
    backup_header_lba = total_sectors - 1
    backup_array_lba = backup_header_lba - array_sectors
    first_usable = array_sectors + 2
    last_usable = backup_array_lba - 1

    image[sector_size * 2:sector_size * 2 + len(array)] = array
    image[backup_array_lba * sector_size:
          backup_array_lba * sector_size + len(array)] = array

    def header(current, peer, entries_lba):
        result = bytearray(sector_size)
        result[:8] = b"EFI PART"
        put32(result, 8, 0x00010000)
        put32(result, 12, 92)
        put64(result, 24, current)
        put64(result, 32, peer)
        put64(result, 40, first_usable)
        put64(result, 48, last_usable)
        result[56:72] = bytes(range(81, 97))
        put64(result, 72, entries_lba)
        put32(result, 80, 128)
        put32(result, 84, 128)
        put32(result, 88, zlib.crc32(array) & 0xFFFFFFFF)
        put32(result, 16, zlib.crc32(result[:92]) & 0xFFFFFFFF)
        return result

    primary = header(1, backup_header_lba, 2)
    backup = header(backup_header_lba, 1, backup_array_lba)
    if corrupt_backup:
        backup_offset = backup_array_lba * sector_size
        image[backup_offset + 16] ^= 0x40
        # Keep a valid header around an independently CRC-invalid array.
        put32(backup, 16, 0)
        put32(backup, 16, zlib.crc32(backup[:92]) & 0xFFFFFFFF)
    image[sector_size:2 * sector_size] = primary
    image[backup_header_lba * sector_size:(backup_header_lba + 1) * sector_size] = backup
    image[510:512] = b"\x55\xaa"
    image[450] = 0xEE
    image[454:458] = struct.pack("<I", 1)
    image[458:462] = struct.pack("<I", min(total_sectors - 1, 0xFFFFFFFF))
    with open(path, "wb") as output:
        output.write(image)


class Dm29VerifierTests(unittest.TestCase):
    def test_reports_crc_state_and_metadata_only_repair_diff(self):
        with tempfile.TemporaryDirectory(prefix="dm29-verifier-") as temp:
            for sector_size, total_sectors in ((512, 4096), (4096, 256)):
                before = os.path.join(temp, f"before-{sector_size}.img")
                after = os.path.join(temp, f"after-{sector_size}.img")
                build_image(before, sector_size, total_sectors, corrupt_backup=True)
                build_image(after, sector_size, total_sectors)
                result = subprocess.run(
                    [sys.executable, VERIFIER, after, "--sector-size",
                     str(sector_size), "--before", before],
                    check=True, capture_output=True, text=True)
                report = json.loads(result.stdout)
                self.assertEqual("Healthy", report["redundancy"])
                self.assertTrue(report["normalized_copies_equal"])
                self.assertEqual("Valid", report["primary"]["state"])
                self.assertEqual("Valid", report["backup"]["state"])
                self.assertEqual("ArrayCrcInvalid", report["before"]["backup_state"])
                self.assertTrue(report["protective_mbr_unchanged"])
                self.assertTrue(report["changed_ranges"]["metadata_only"])
                self.assertTrue(report["changed_ranges"]["ranges"])
                self.assertTrue(all(
                    "outside-gpt-metadata" not in item["classification"]
                    for item in report["changed_ranges"]["ranges"]))


if __name__ == "__main__":
    unittest.main()
