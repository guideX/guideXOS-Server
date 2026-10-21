from __future__ import annotations

import importlib.util
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "prepare-stellar-navigator-sn10-mbr-media.py"
SPEC = importlib.util.spec_from_file_location("sn10_mbr_media", SCRIPT)
assert SPEC and SPEC.loader
MEDIA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MEDIA)


class Sn10MbrMediaTests(unittest.TestCase):
    def test_mbr_places_mountable_root_before_uefi_esp(self) -> None:
        root_start = MEDIA.ALIGNMENT_SECTORS
        efi_start = root_start + MEDIA.PARTITION_SECTORS
        mbr = MEDIA.build_mbr(root_start, MEDIA.PARTITION_SECTORS, efi_start, MEDIA.PARTITION_SECTORS)
        self.assertEqual(mbr[510:512], b"\x55\xAA")
        partitions = MEDIA.parse_mbr(mbr)
        self.assertEqual([item["type"] for item in partitions], [0x0C, 0xEF])
        self.assertEqual(partitions[0]["startLba"], root_start)
        self.assertEqual(partitions[1]["startLba"], efi_start)
        self.assertEqual(partitions[0]["endLba"] + 1, partitions[1]["startLba"])

    def test_mbr_rejects_overlapping_or_unaligned_layout(self) -> None:
        with self.assertRaisesRegex(ValueError, "overlap"):
            MEDIA.build_mbr(2048, 4096, 4096, 4096)
        with self.assertRaisesRegex(ValueError, "aligned"):
            MEDIA.build_mbr(2049, 4096, 8192, 4096)

    def test_selected_root_type_is_supported_by_current_kernel_probe(self) -> None:
        source = (ROOT / "kernel" / "core" / "fs_fat.cpp").read_text(encoding="utf-8")
        supported = MEDIA.kernel_fat_partition_types(source)
        self.assertIn(0x0C, supported)
        self.assertEqual(MEDIA.ROOT_PARTITION_TYPE, 0x0C)
        self.assertEqual(MEDIA.EFI_PARTITION_TYPE, 0xEF)
        mbr = MEDIA.build_mbr(2048, MEDIA.PARTITION_SECTORS, 2048 + MEDIA.PARTITION_SECTORS, MEDIA.PARTITION_SECTORS)
        first_mountable = next(part["type"] for part in MEDIA.parse_mbr(mbr) if part["type"] in supported)
        self.assertEqual(first_mountable, 0x0C)


if __name__ == "__main__":
    unittest.main()
