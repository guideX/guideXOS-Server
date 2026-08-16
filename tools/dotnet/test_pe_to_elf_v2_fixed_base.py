"""Focused PE-to-ELF regression tests for NativeAOT image geometry."""

from __future__ import annotations

import struct
import unittest
from pathlib import Path
import sys


sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_to_elf_v2_fixed_base import to_elf  # noqa: E402


def synthetic_pe() -> bytes:
    """Build a minimal PE32+ with raw, zero-raw, and raw-tail sections."""
    image_base = 0x10000000
    pe_offset = 0x80
    optional_size = 0xF0
    section_table = pe_offset + 4 + 20 + optional_size
    image = bytearray(0x240)
    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, pe_offset)
    image[pe_offset:pe_offset + 4] = b"PE\0\0"
    coff = pe_offset + 4
    struct.pack_into("<H", image, coff + 2, 3)
    struct.pack_into("<H", image, coff + 16, optional_size)
    optional = coff + 20
    struct.pack_into("<H", image, optional, 0x20B)
    struct.pack_into("<I", image, optional + 16, 0x1000)
    struct.pack_into("<Q", image, optional + 24, image_base)
    struct.pack_into("<I", image, optional + 60, 0x200)

    sections = [
        (b".text", 0x30, 0x1000, 0x20, 0x200, 0x60000020),
        (b"hydrated", 0x180, 0x2000, 0, 0, 0xC0000040),
        (b".data", 0x40, 0x3000, 0x20, 0x220, 0xC0000040),
    ]
    for index, (name, vsize, vaddr, raw_size, raw_ptr, characteristics) in enumerate(sections):
        offset = section_table + index * 40
        image[offset:offset + len(name)] = name
        struct.pack_into("<I", image, offset + 8, vsize)
        struct.pack_into("<I", image, offset + 12, vaddr)
        struct.pack_into("<I", image, offset + 16, raw_size)
        struct.pack_into("<I", image, offset + 20, raw_ptr)
        struct.pack_into("<I", image, offset + 36, characteristics)
    image[0x200:0x220] = bytes(range(0x20))
    image[0x220:0x240] = bytes(range(0xA0, 0xC0))
    return bytes(image)


class PeToElfImageGeometryTests(unittest.TestCase):
    def test_zero_raw_section_is_a_zero_filled_load_segment(self) -> None:
        elf = to_elf(synthetic_pe())
        phnum = struct.unpack_from("<H", elf, 56)[0]
        self.assertEqual(phnum, 4)  # image-base reservation plus three PE sections

        segments = [
            struct.unpack_from("<IIQQQQQQ", elf, 64 + index * 56)
            for index in range(phnum)
        ]
        by_vaddr = {segment[3]: segment for segment in segments}
        image_base = by_vaddr[0x10000000]
        self.assertEqual(image_base[0], 1)  # PT_LOAD
        self.assertEqual(image_base[1], 4)  # read-only PE header page
        self.assertEqual(image_base[5], 0x200)  # actual PE header bytes
        self.assertEqual(image_base[6], 0x1000)  # loader-visible image-base page
        self.assertEqual(elf[image_base[2]:image_base[2] + 2], b"MZ")
        hydrated = by_vaddr[0x10002000]
        self.assertEqual(hydrated[0], 1)  # PT_LOAD
        self.assertEqual(hydrated[1], 6)  # readable and writable
        self.assertEqual(hydrated[5], 0)  # no fabricated file contents
        self.assertEqual(hydrated[6], 0x180)  # loader-provided zero fill
        self.assertEqual(hydrated[7], 0x1000)

        text = by_vaddr[0x10001000]
        data = by_vaddr[0x10003000]
        self.assertEqual((text[5], text[6]), (0x20, 0x30))
        self.assertEqual((data[5], data[6]), (0x20, 0x40))
        self.assertEqual(elf[data[2]:data[2] + 0x20], bytes(range(0xA0, 0xC0)))


if __name__ == "__main__":
    unittest.main()
