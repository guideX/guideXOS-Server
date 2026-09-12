#!/usr/bin/env python3
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from tab_a8_recon import FdtParser, ReconError, parse_boot_image, parse_dtbo


def be32(value):
    return struct.pack(">I", value)


class FdtBuilder:
    def __init__(self):
        self.structure = bytearray()
        self.strings = bytearray()

    def string_offset(self, name):
        needle = name.encode() + b"\0"
        offset = self.strings.find(needle)
        if offset >= 0:
            return offset
        offset = len(self.strings)
        self.strings.extend(needle)
        return offset

    def begin(self, name):
        self.structure.extend(be32(1))
        self.structure.extend(name.encode() + b"\0")
        while len(self.structure) % 4:
            self.structure.append(0)

    def end(self):
        self.structure.extend(be32(2))

    def prop(self, name, value):
        self.structure.extend(be32(3))
        self.structure.extend(be32(len(value)))
        self.structure.extend(be32(self.string_offset(name)))
        self.structure.extend(value)
        while len(self.structure) % 4:
            self.structure.append(0)

    def cells(self, name, *values):
        self.prop(name, b"".join(be32(value) for value in values))

    def string(self, name, value):
        self.prop(name, value.encode() + b"\0")

    def compatibles(self, *values):
        self.prop("compatible", b"".join(value.encode() + b"\0" for value in values))

    def finish(self):
        self.structure.extend(be32(9))
        reserve_offset = 0x40
        struct_offset = 0x100
        strings_offset = (struct_offset + len(self.structure) + 0xff) & ~0xff
        total = strings_offset + len(self.strings)
        output = bytearray(total)
        output[0:4] = be32(0xd00dfeed)
        output[4:8] = be32(total)
        output[8:12] = be32(struct_offset)
        output[12:16] = be32(strings_offset)
        output[16:20] = be32(reserve_offset)
        output[20:24] = be32(17)
        output[24:28] = be32(16)
        output[32:36] = be32(len(self.strings))
        output[36:40] = be32(len(self.structure))
        output[reserve_offset:reserve_offset + 16] = b"\0" * 16
        output[struct_offset:struct_offset + len(self.structure)] = self.structure
        output[strings_offset:strings_offset + len(self.strings)] = self.strings
        return bytes(output)


def make_ums512_tree(overflow=False):
    fdt = FdtBuilder()
    fdt.begin("")
    fdt.compatibles("sprd,ums512", "samsung,gta8wifi")
    fdt.cells("#address-cells", 2)
    fdt.cells("#size-cells", 2)
    fdt.begin("memory@0")
    fdt.string("device_type", "memory")
    fdt.cells("reg", 0, 0, 0, 0x80000000)
    fdt.end()
    fdt.begin("reserved-memory")
    fdt.cells("#address-cells", 2)
    fdt.cells("#size-cells", 2)
    fdt.cells("ranges")
    fdt.begin("secure@10000000")
    fdt.cells("reg", 0, 0x10000000, 0, 0x100000)
    fdt.end()
    fdt.end()
    fdt.begin("soc")
    fdt.cells("#address-cells", 1)
    fdt.cells("#size-cells", 1)
    fdt.cells("ranges", 0x70000000, 0, 0x30000000, 0x10000000)
    fdt.begin("interrupt-controller@30000000")
    fdt.compatibles("arm,gic-v3")
    fdt.cells("#interrupt-cells", 3)
    fdt.cells("phandle", 1)
    fdt.cells("reg", 0, 0x30000000, 0, 0x10000, 0, 0x30010000, 0, 0x200000)
    fdt.end()
    fdt.begin("serial@70100000")
    fdt.compatibles("sprd,sc9833-uart")
    fdt.cells("interrupt-parent", 1)
    fdt.cells("interrupts", 0, 45, 4)
    fdt.cells("reg", 0x70100000, 0x1000)
    fdt.end()
    fdt.begin("sdhci@71400000")
    fdt.compatibles("sprd,ums512-sdhci")
    fdt.cells("interrupt-parent", 1)
    fdt.cells("interrupts", 0, 66, 4)
    fdt.cells("reg", 0x71400000, 0x1000)
    fdt.end()
    fdt.begin("usb@71000000")
    fdt.compatibles("snps,dwc3")
    fdt.cells("interrupt-parent", 1)
    fdt.cells("interrupts", 0, 70, 4)
    fdt.cells("reg", 0x71000000, 0x100000)
    fdt.end()
    fdt.begin("dsi@80000000")
    fdt.compatibles("sprd,ums512-dsi")
    fdt.cells("reg", 0x80000000, 0x10000)
    fdt.end()
    fdt.end()
    fdt.begin("timer")
    fdt.compatibles("arm,armv8-timer")
    fdt.cells("interrupt-parent", 1)
    fdt.cells("interrupts", 1, 14, 4, 1, 11, 4)
    fdt.end()
    fdt.begin("cpus")
    fdt.cells("#address-cells", 2)
    fdt.cells("#size-cells", 0)
    fdt.begin("cpu@0")
    fdt.compatibles("arm,cortex-a75")
    fdt.string("enable-method", "psci")
    fdt.cells("reg", 0, 0)
    fdt.end()
    fdt.end()
    fdt.begin("psci")
    fdt.string("method", "smc")
    fdt.end()
    fdt.end()
    result = fdt.finish()
    if overflow:
        # Replace the first memory size cell with a value that overflows the
        # 64-bit address+size check while retaining a structurally valid FDT.
        marker = be32(0) + be32(0) + be32(0) + be32(0x80000000)
        replacement = be32(0xffffffff) + be32(0xfffffff0) + be32(0) + be32(0x40)
        position = result.find(marker)
        assert position >= 0
        result = result[:position] + replacement + result[position + len(marker):]
    return result


class TabA8ReconTests(unittest.TestCase):
    def test_ums512_inventory_and_nested_translation(self):
        parser = FdtParser(make_ums512_tree(), "synthetic-ums512.dtb")
        platform = parser.platform_map()
        self.assertEqual(platform["root"]["compatible"][0], "sprd,ums512")
        self.assertEqual(platform["gic"]["generation"], "v3+")
        uart = next(item for item in platform["uart"] if "serial@" in item["path"])
        self.assertEqual(uart["reg"][0]["translated"], 0x30100000)
        self.assertEqual(uart["interrupts"][0]["raw"], "0x00000000 0x0000002d 0x00000004")
        self.assertEqual(platform["memory"][0]["reg"][0]["size"], 0x80000000)

    def test_32_bit_cells_and_empty_ranges_are_identity(self):
        fdt = FdtBuilder()
        fdt.begin("")
        fdt.compatibles("test,identity")
        fdt.cells("#address-cells", 1)
        fdt.cells("#size-cells", 1)
        fdt.begin("bus")
        fdt.cells("#address-cells", 1)
        fdt.cells("#size-cells", 1)
        fdt.cells("ranges")
        fdt.begin("uart@1000")
        fdt.compatibles("sprd,sc9833-uart")
        fdt.cells("reg", 0x1000, 0x100)
        fdt.end()
        fdt.end()
        fdt.end()
        parser = FdtParser(fdt.finish())
        uart = next(item for item in parser.platform_map()["uart"] if item["path"].endswith("uart@1000"))
        self.assertEqual(uart["reg"][0]["translated"], 0x1000)

    def test_malformed_truncated_and_overflow_rejected(self):
        good = make_ums512_tree()
        with self.assertRaises(ReconError):
            FdtParser(good[:-1])
        bad_magic = b"xxxx" + good[4:]
        with self.assertRaises(ReconError):
            FdtParser(bad_magic)
        with self.assertRaises(ReconError):
            FdtParser(make_ums512_tree(overflow=True)).platform_map()

    def test_dtbo_table_is_bounds_checked_and_identifies_entry(self):
        dtb = make_ums512_tree()
        entry_offset = 32
        dtb_offset = 64
        total = dtb_offset + len(dtb)
        image = bytearray(total)
        image[0:24] = struct.pack("<6I", total, 32, 1, entry_offset, 2048, 0)
        image[entry_offset:entry_offset + 32] = struct.pack("<8I", len(dtb), dtb_offset, 7, 1, 0, 0, 0, 0)
        image[dtb_offset:] = dtb
        result = parse_dtbo(bytes(image), "synthetic.dtbo")
        self.assertFalse(result["errors"])
        self.assertEqual(result["entries"][0]["model"], "unknown")
        self.assertIn("sprd,ums512", result["entries"][0]["compatible"])
        image[8:12] = struct.pack("<I", 2)
        self.assertTrue(parse_dtbo(bytes(image))["errors"])

    def test_android_boot_v2_layout_and_dtb(self):
        dtb = make_ums512_tree()
        page = 2048
        kernel = b"K" * 64
        dtb_offset = page + page
        image = bytearray(dtb_offset + len(dtb))
        image[0:8] = b"ANDROID!"
        fields = [len(kernel), 0, 0, 0, 0, 0, 0, page, 2, 0]
        for index, value in enumerate(fields):
            image[8 + index * 4:12 + index * 4] = struct.pack("<I", value)
        image[1644:1648] = struct.pack("<I", 1660)
        image[1652:1656] = struct.pack("<I", len(dtb))
        image[1656:1660] = struct.pack("<Q", 0x90000000)
        image[page:page + len(kernel)] = kernel
        image[dtb_offset:] = dtb
        result = parse_boot_image(bytes(image), "synthetic-boot.img")
        self.assertEqual(result["header_version"], 2)
        self.assertEqual(result["page_size"], 2048)
        self.assertEqual(len(result["dtbs"]), 1)
        self.assertEqual(result["dtbs"][0]["parser"].root.compatibles()[0], "sprd,ums512")


if __name__ == "__main__":
    unittest.main(verbosity=2)
