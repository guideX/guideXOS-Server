#!/usr/bin/env python3
"""Read-only Samsung Galaxy Tab A8 reconnaissance helpers.

This module intentionally has no flashing, block-device, bootloader-unlock, or
image-writing operations.  It reads ADB properties/procfs and supplied firmware
artifacts, then writes human-readable reports under the caller-selected out/dir.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
from dataclasses import dataclass, field
from typing import Any, Iterable, Optional


EXPECTED_MODEL = "SM-X200"
EXPECTED_DEVICE = "gta8wifi"
EXPECTED_PLATFORM = "UMS512"
EXPECTED_BOARD = "ums512_25c10"
EXPECTED_FAMILY = "sharkl5pro"
PROPERTY_NAMES = [
    "ro.product.model", "ro.product.device", "ro.product.name",
    "ro.product.board", "ro.board.platform", "ro.hardware", "ro.soc.model",
    "ro.soc.manufacturer", "ro.boot.hardware", "ro.boot.bootloader",
    "ro.boot.slot_suffix", "ro.build.version.release", "ro.build.version.sdk",
    "ro.build.fingerprint", "ro.build.version.security_patch",
    "ro.boot.verifiedbootstate", "ro.boot.vbmeta.device_state",
    "ro.boot.flash.locked", "ro.boot.secureboot", "ro.boot.veritymode",
    "ro.oem_unlock_supported", "ro.boot.dynamic_partitions",
    "ro.virtual_ab.enabled", "ro.boot.vbmeta.avb_version",
    "ro.boot.vbmeta.digest", "ro.boot.boot_devices",
]
IMPORTANT_TERMS = (
    "memory", "reserved-memory", "chosen", "cpus", "interrupt-controller",
    "timer", "serial", "uart", "sdhci", "mmc", "ufs", "usb", "dwc3",
    "display", "dsi", "panel", "backlight", "gpio", "pinctrl", "i2c",
    "spi", "regulator", "framebuffer", "touch", "goodix", "ilitek",
    "synaptics", "fts", "gpio-keys", "power-key", "volume", "psci",
)
MAX_FDT_BYTES = 16 * 1024 * 1024
MAX_ARCHIVE_MEMBER_BYTES = 512 * 1024 * 1024


class ReconError(ValueError):
    pass


def u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ReconError(f"u32 outside artifact at 0x{offset:x}")
    return struct.unpack_from("<I", data, offset)[0]


def be32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ReconError(f"be32 outside DTB at 0x{offset:x}")
    return struct.unpack_from(">I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 8 > len(data):
        raise ReconError(f"u64 outside artifact at 0x{offset:x}")
    return struct.unpack_from("<Q", data, offset)[0]


def be_cells(value: bytes) -> list[int]:
    if len(value) % 4:
        raise ReconError("property is not a whole number of 32-bit cells")
    return [struct.unpack_from(">I", value, i)[0] for i in range(0, len(value), 4)]


def cells_to_int(cells: Iterable[int]) -> int:
    result = 0
    for cell in cells:
        if result > ((1 << 64) - 1) >> 32:
            raise ReconError("device-tree cell value exceeds 64 bits")
        result = (result << 32) | cell
    return result


def align(value: int, boundary: int) -> int:
    if boundary <= 0 or boundary & (boundary - 1):
        raise ReconError(f"invalid alignment {boundary}")
    return (value + boundary - 1) & ~(boundary - 1)


def checked_end(start: int, size: int, limit: Optional[int] = None) -> int:
    if start < 0 or size < 0 or start > (1 << 64) - 1 - size:
        raise ReconError(f"address range overflow: 0x{start:x}+0x{size:x}")
    end = start + size
    if limit is not None and end > limit:
        raise ReconError(f"range 0x{start:x}+0x{size:x} exceeds 0x{limit:x}")
    return end


def hex_value(value: Optional[int]) -> str:
    return "unknown" if value is None else f"0x{value:x}"


def hex_cells(value: bytes) -> str:
    try:
        return " ".join(f"0x{cell:08x}" for cell in be_cells(value))
    except ReconError:
        return value.hex()


def text_value(value: bytes) -> str:
    raw = value.rstrip(b"\0")
    if not raw:
        return ""
    if all(32 <= byte < 127 or byte in (9, 10, 13) for byte in raw):
        return raw.decode("ascii", errors="replace")
    return ""


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass
class FdtNode:
    name: str
    path: str
    parent: Optional["FdtNode"]
    props: dict[str, bytes] = field(default_factory=dict)
    children: list["FdtNode"] = field(default_factory=list)

    def prop_u32(self, name: str, default: Optional[int] = None) -> Optional[int]:
        value = self.props.get(name)
        if value is None:
            return default
        cells = be_cells(value)
        if len(cells) != 1:
            raise ReconError(f"{self.path}:{name} is not one cell")
        return cells[0]

    def compatibles(self) -> list[str]:
        value = self.props.get("compatible", b"")
        if not value:
            return []
        if value[-1:] != b"\0":
            raise ReconError(f"{self.path}:compatible is not NUL terminated")
        return [part.decode("ascii", errors="replace") for part in value.split(b"\0") if part]


class FdtParser:
    """Bounds-checked FDT parser with exact raw cell preservation."""

    def __init__(self, data: bytes, source: str = ""):
        if len(data) < 40 or data[:4] != b"\xd0\x0d\xfe\xed":
            raise ReconError("not a flattened device tree")
        self.data = data
        self.source = source
        total = be32(data, 4)
        struct_offset = be32(data, 8)
        strings_offset = be32(data, 12)
        reserve_offset = be32(data, 16)
        version = be32(data, 20)
        last_compatible = be32(data, 24)
        strings_size = be32(data, 32)
        struct_size = be32(data, 36)
        if total < 40 or total > len(data) or total > MAX_FDT_BYTES:
            raise ReconError("invalid FDT total size")
        if version < 16 or version > 19 or last_compatible < 16 or last_compatible > version:
            raise ReconError("unsupported FDT version")
        if reserve_offset & 7:
            raise ReconError("unaligned FDT reserve-map offset")
        checked_end(reserve_offset, 16, total)
        if struct_offset & 3 or strings_offset & 3:
            raise ReconError("unaligned FDT block offset")
        checked_end(struct_offset, struct_size, total)
        checked_end(strings_offset, strings_size, total)
        self.header = {
            "total_size": total, "struct_offset": struct_offset,
            "struct_size": struct_size, "strings_offset": strings_offset,
            "strings_size": strings_size, "reserve_offset": reserve_offset,
            "version": version, "last_compatible": last_compatible,
        }
        self.reserved_map: list[dict[str, int]] = []
        cursor = reserve_offset
        while True:
            checked_end(cursor, 16, total)
            address = struct.unpack_from(">Q", data, cursor)[0]
            size = struct.unpack_from(">Q", data, cursor + 8)[0]
            cursor += 16
            if address == 0 and size == 0:
                break
            checked_end(address, size)
            self.reserved_map.append({"address": address, "size": size})
            if len(self.reserved_map) > 4096:
                raise ReconError("FDT reserve map is unreasonably large")
        self.root = self._parse_structure(struct_offset, struct_size, strings_offset, strings_size)
        self.nodes = self._flatten(self.root)
        self.phandles: dict[int, FdtNode] = {}
        for node in self.nodes:
            for name in ("phandle", "linux,phandle"):
                if name in node.props:
                    cells = be_cells(node.props[name])
                    if len(cells) == 1:
                        self.phandles[cells[0]] = node

    def _parse_structure(self, start: int, size: int, strings: int, strings_size: int) -> FdtNode:
        end = start + size
        offset = start
        stack: list[FdtNode] = []
        root: Optional[FdtNode] = None
        while offset < end:
            if offset + 4 > end:
                raise ReconError("truncated FDT token")
            token = be32(self.data, offset)
            offset += 4
            if token == 1:  # FDT_BEGIN_NODE
                name_start = offset
                nul = self.data.find(b"\0", name_start, end)
                if nul < 0:
                    raise ReconError("unterminated FDT node name")
                name = self.data[name_start:nul].decode("ascii", errors="replace")
                offset = align(nul + 1 - start, 4) + start
                if offset > end:
                    raise ReconError("FDT node name padding exceeds structure block")
                parent = stack[-1] if stack else None
                if parent is None:
                    if root is not None:
                        raise ReconError("multiple FDT roots")
                    path = "/"
                else:
                    path = "/" + name if parent.path == "/" else parent.path + "/" + name
                node = FdtNode(name=name, path=path, parent=parent)
                if parent is not None:
                    parent.children.append(node)
                else:
                    root = node
                stack.append(node)
            elif token == 2:  # FDT_END_NODE
                if not stack:
                    raise ReconError("FDT_END_NODE without node")
                stack.pop()
            elif token == 3:  # FDT_PROP
                if not stack or offset + 8 > end:
                    raise ReconError("truncated FDT property header")
                length = be32(self.data, offset)
                name_offset = be32(self.data, offset + 4)
                offset += 8
                checked_end(offset - start, length, size)
                value = self.data[offset:offset + length]
                if name_offset >= strings_size:
                    raise ReconError("FDT property name offset outside strings block")
                name_start = strings + name_offset
                name_end = self.data.find(b"\0", name_start, strings + strings_size)
                if name_end < 0:
                    raise ReconError("unterminated FDT property name")
                name = self.data[name_start:name_end].decode("ascii", errors="replace")
                stack[-1].props[name] = value
                offset = align(offset - start + length, 4) + start
                if offset > end:
                    raise ReconError("FDT property padding exceeds structure block")
            elif token == 4:  # FDT_NOP
                continue
            elif token == 9:  # FDT_END
                if stack or root is None:
                    raise ReconError("FDT_END before all nodes closed")
                return root
            else:
                raise ReconError(f"unknown FDT token {token}")
        raise ReconError("FDT structure block has no FDT_END")

    @staticmethod
    def _flatten(node: FdtNode) -> list[FdtNode]:
        result = [node]
        for child in node.children:
            result.extend(FdtParser._flatten(child))
        return result

    @staticmethod
    def _cells_for(node: FdtNode, name: str, default: int, allow_zero: bool = False) -> int:
        value = node.prop_u32(name, default)
        minimum = 0 if allow_zero else 1
        if value is None or value < minimum or value > 2:
            raise ReconError(f"{node.path}:{name} has unsupported cell count {value}")
        return value

    def reg_entries(self, node: FdtNode) -> list[dict[str, Any]]:
        value = node.props.get("reg")
        if value is None:
            return []
        parent = node.parent
        if parent is None:
            raise ReconError(f"{node.path}:root cannot have reg")
        address_cells = self._cells_for(parent, "#address-cells", 2)
        size_cells = self._cells_for(parent, "#size-cells", 1, allow_zero=True)
        tuple_bytes = (address_cells + size_cells) * 4
        if not value or len(value) % tuple_bytes:
            raise ReconError(f"{node.path}:reg has invalid tuple length")
        result = []
        for offset in range(0, len(value), tuple_bytes):
            raw = value[offset:offset + tuple_bytes]
            address = cells_to_int(be_cells(raw[:address_cells * 4]))
            region_size = cells_to_int(be_cells(raw[address_cells * 4:])) if size_cells else 0
            end = checked_end(address, region_size)
            translated = self.translate(node, address, region_size)
            result.append({
                "raw": hex_cells(raw), "address": address, "size": region_size,
                "end": end, "translated": translated,
            })
        return result

    def ranges_entries(self, bus: FdtNode) -> list[dict[str, Any]]:
        value = bus.props.get("ranges")
        if value is None or len(value) == 0:
            return []
        parent = bus.parent
        if parent is None:
            raise ReconError(f"{bus.path}:root ranges is invalid for this report")
        child_cells = self._cells_for(bus, "#address-cells", 2)
        parent_cells = self._cells_for(parent, "#address-cells", 2)
        size_cells = self._cells_for(bus, "#size-cells", 1, allow_zero=True)
        tuple_cells = child_cells + parent_cells + size_cells
        tuple_bytes = tuple_cells * 4
        if len(value) % tuple_bytes:
            raise ReconError(f"{bus.path}:ranges has a truncated tuple")
        result = []
        for offset in range(0, len(value), tuple_bytes):
            raw = value[offset:offset + tuple_bytes]
            cells = be_cells(raw)
            child = cells_to_int(cells[:child_cells])
            parent_address = cells_to_int(cells[child_cells:child_cells + parent_cells])
            size = cells_to_int(cells[child_cells + parent_cells:])
            checked_end(child, size)
            checked_end(parent_address, size)
            result.append({"raw": hex_cells(raw), "child": child,
                           "parent": parent_address, "size": size})
        return result

    def translate(self, node: FdtNode, address: int, size: int) -> Optional[int]:
        current = address
        current_end = checked_end(current, size)
        bus = node.parent
        translated = False
        while bus is not None and bus.parent is not None:
            ranges = bus.props.get("ranges")
            if ranges is None:
                return None
            entries = self.ranges_entries(bus)
            if entries:
                match = None
                for entry in entries:
                    child_end = checked_end(entry["child"], entry["size"])
                    if current >= entry["child"] and current_end <= child_end:
                        match = entry
                        break
                if match is None:
                    return None
                current = checked_end(match["parent"], current - match["child"])
                current_end = checked_end(current, size)
                translated = True
            # Empty ranges explicitly means an identity mapping.
            bus = bus.parent
        return current if translated else address

    def interrupt_tuples(self, node: FdtNode) -> list[dict[str, Any]]:
        value = node.props.get("interrupts")
        if value is None:
            return []
        parent_ref = None
        inherited = node
        while inherited is not None and parent_ref is None:
            parent_ref = inherited.props.get("interrupt-parent")
            inherited = inherited.parent
        parent_node = None
        if parent_ref is not None:
            cells = be_cells(parent_ref)
            if len(cells) == 1:
                parent_node = self.phandles.get(cells[0])
        cells_per = parent_node.prop_u32("#interrupt-cells", None) if parent_node else None
        if cells_per is None:
            cells_per = 3
        if cells_per < 1 or cells_per > 4 or len(value) % (cells_per * 4):
            raise ReconError(f"{node.path}:interrupts has invalid tuple length")
        result = []
        for offset in range(0, len(value), cells_per * 4):
            raw = value[offset:offset + cells_per * 4]
            tuple_cells = be_cells(raw)
            decoded: dict[str, Any] = {}
            if cells_per >= 2 and tuple_cells[0] in (0, 1, 2):
                kind = {0: "SPI", 1: "PPI", 2: "extended"}[tuple_cells[0]]
                decoded = {"type": kind, "number": tuple_cells[1],
                           "global_irq": (32 + tuple_cells[1]) if tuple_cells[0] == 0
                           else (16 + tuple_cells[1]) if tuple_cells[0] == 1 else None}
            result.append({"raw": hex_cells(raw), "cells": tuple_cells, "decoded": decoded})
        return result

    def _record(self, node: FdtNode) -> dict[str, Any]:
        lowered = (node.path + " " + " ".join(node.compatibles())).lower()
        record: dict[str, Any] = {
            "path": node.path, "name": node.name,
            "compatible": node.compatibles(), "properties": {},
        }
        for name, value in node.props.items():
            if name in {"compatible", "model", "device_type", "status", "stdout-path",
                        "bootargs", "enable-method", "clock-frequency", "assigned-clock-rates",
                        "interrupt-parent", "phandle", "linux,phandle", "#address-cells",
                        "#size-cells", "#interrupt-cells", "ranges", "reg", "interrupts",
                        "interrupts-extended", "memory-region", "label"} or any(term in lowered for term in IMPORTANT_TERMS):
                item: dict[str, Any] = {"hex": value.hex(), "cells": hex_cells(value)}
                string = text_value(value)
                if string:
                    item["text"] = string
                record["properties"][name] = item
        if "reg" in node.props:
            record["reg"] = self.reg_entries(node)
        if "ranges" in node.props:
            record["ranges"] = self.ranges_entries(node)
        if "interrupts" in node.props:
            record["interrupts"] = self.interrupt_tuples(node)
        record["is_memory"] = (node.name == "memory" or node.name.startswith("memory@") or
                                node.props.get("device_type") == b"memory\0")
        record["is_reserved_memory"] = node.path.startswith("/reserved-memory/")
        record["is_uart"] = ("uart" in lowered or "serial" in lowered or
                              any(c in {"sprd,sc9833-uart", "sprd,sharkl5pro-uart",
                                       "arm,pl011", "brcm,bcm2835-pl011"} for c in node.compatibles()))
        record["is_gic"] = any("gic" in c.lower() for c in node.compatibles()) or "interrupt-controller" in lowered
        record["is_timer"] = "timer" in lowered or any("arm,armv8-timer" in c or "arm,armv7-timer" in c for c in node.compatibles())
        record["is_storage"] = any(term in lowered for term in ("sdhci", "mmc", "ufs", "emmc"))
        record["is_usb"] = any(term in lowered for term in ("usb", "dwc3", "xhci"))
        record["is_display"] = any(term in lowered for term in ("display", "dsi", "panel", "framebuffer", "backlight"))
        record["is_touch"] = any(term in lowered for term in ("touch", "goodix", "ilitek", "synaptics", "fts", "gt9"))
        record["is_buttons"] = any(term in lowered for term in ("gpio-keys", "power-key", "volume", "button"))
        record["is_important"] = node.path == "/" or any(term in lowered for term in IMPORTANT_TERMS)
        return record

    def inventory(self) -> list[dict[str, Any]]:
        result = []
        for node in self.nodes:
            record = self._record(node)
            if record["is_important"]:
                result.append(record)
        return result

    def platform_map(self) -> dict[str, Any]:
        records = self.inventory()
        root = next(record for record in records if record["path"] == "/")
        def first(flag: str) -> Optional[dict[str, Any]]:
            return next((r for r in records if r.get(flag)), None)
        memories = [r for r in records if r.get("is_memory")]
        reserved = [r for r in records if r.get("is_reserved_memory")]
        gics = [r for r in records if r.get("is_gic")]
        timers = [r for r in records if r.get("is_timer")]
        uarts = [r for r in records if r.get("is_uart")]
        storage = [r for r in records if r.get("is_storage")]
        usb = [r for r in records if r.get("is_usb")]
        display = [r for r in records if r.get("is_display")]
        touch = [r for r in records if r.get("is_touch")]
        buttons = [r for r in records if r.get("is_buttons")]
        gic_version = "unknown"
        gic_regs: list[dict[str, Any]] = []
        if gics:
            comp = " ".join(gics[0]["compatible"]).lower()
            gic_version = "v3+" if "gic-v3" in comp else "v2" if ("gic-400" in comp or "gic-v2" in comp or "cortex-a15-gic" in comp) else "unknown"
            gic_regs = gics[0].get("reg", [])
        timer_frequency = None
        for node in self.nodes:
            lowered_node = (node.path + " " + " ".join(node.compatibles())).lower()
            if "timer" not in lowered_node and node.path != "/cpus":
                continue
            for property_name in ("clock-frequency", "cntfrq"):
                if property_name not in node.props:
                    continue
                try:
                    values = be_cells(node.props[property_name])
                    if len(values) == 1 and values[0] > 0:
                        timer_frequency = values[0]
                        break
                except ReconError:
                    pass
            if timer_frequency is not None:
                break
        def simple(nodes: list[dict[str, Any]]) -> list[dict[str, Any]]:
            return [{"path": n["path"], "compatible": n["compatible"],
                     "reg": n.get("reg", []), "interrupts": n.get("interrupts", [])} for n in nodes]
        return {
            "root": {"model": root["properties"].get("model", {}).get("text", "unknown"),
                     "compatible": root["compatible"]},
            "root_cells": {"address": root["properties"].get("#address-cells", {}).get("cells", "unknown"),
                           "size": root["properties"].get("#size-cells", {}).get("cells", "unknown")},
            "memory": simple(memories), "reserved_memory": simple(reserved),
            "gic": {"generation": gic_version, "nodes": simple(gics), "regs": gic_regs},
            "timer": simple(timers), "timer_frequency": timer_frequency,
            "uart": simple(uarts), "storage": simple(storage),
            "usb": simple(usb), "display": simple(display), "touch": simple(touch),
            "buttons": simple(buttons),
            "chosen": next((r for r in records if r["path"] == "/chosen"), None),
            "psci": next((r for r in records if r["path"] == "/psci" or "psci" in r["path"].lower()), None),
            "all_nodes": records,
        }

    def to_json(self) -> dict[str, Any]:
        return {"source": self.source, "header": self.header,
                "sha256": sha256_bytes(self.data[:self.header["total_size"]]),
                "reserved_map": self.reserved_map, "platform_map": self.platform_map()}


def scan_dtbs(data: bytes, source: str = "") -> list[dict[str, Any]]:
    found: list[dict[str, Any]] = []
    offset = 0
    seen: set[int] = set()
    while True:
        offset = data.find(b"\xd0\x0d\xfe\xed", offset)
        if offset < 0:
            break
        if offset % 4 == 0 and offset not in seen:
            seen.add(offset)
            try:
                total = be32(data, offset + 4)
                checked_end(offset, total, len(data))
                parser = FdtParser(data[offset:offset + total], f"{source}+0x{offset:x}")
                found.append({"offset": offset, "size": total,
                              "sha256": sha256_bytes(data[offset:offset + total]),
                              "parser": parser})
            except (ReconError, struct.error):
                pass
        offset += 4
    return found


def parse_dtbo(data: bytes, source: str = "") -> dict[str, Any]:
    result: dict[str, Any] = {"source": source, "sha256": sha256_bytes(data), "entries": [], "errors": []}
    if len(data) < 24:
        result["errors"].append("DTBO header is truncated")
        return result
    table_size, entry_size, entry_count, entries_offset, page_size, version = struct.unpack_from("<6I", data, 0)
    result["header"] = {"dt_size": table_size, "dt_entry_size": entry_size,
                         "dt_entry_count": entry_count, "dt_entries_offset": entries_offset,
                         "page_size": page_size, "version": version}
    if table_size > len(data) or entry_size < 32 or entry_count > 65536:
        result["errors"].append("DTBO table header has unsafe bounds")
        return result
    try:
        checked_end(entries_offset, entry_size * entry_count, len(data))
    except ReconError as error:
        result["errors"].append(str(error))
        return result
    for index in range(entry_count):
        entry_offset = entries_offset + index * entry_size
        values = struct.unpack_from("<8I", data, entry_offset)
        dt_size, dt_offset, dt_id, dt_rev, custom0, custom1, custom2, custom3 = values
        entry: dict[str, Any] = {"index": index, "dt_size": dt_size, "dt_offset": dt_offset,
                                 "id": dt_id, "rev": dt_rev, "custom": [custom0, custom1, custom2, custom3]}
        try:
            checked_end(dt_offset, dt_size, len(data))
            dtb = FdtParser(data[dt_offset:dt_offset + dt_size], f"{source}[entry {index}]")
            entry["sha256"] = sha256_bytes(data[dt_offset:dt_offset + dt_size])
            entry["model"] = text_value(dtb.root.props.get("model", b"")) or "unknown"
            entry["compatible"] = dtb.root.compatibles()
            entry["parser"] = dtb
        except ReconError as error:
            entry["error"] = str(error)
            result["errors"].append(f"entry {index}: {entry['error']}")
        result["entries"].append(entry)
    return result


def parse_vbmeta(data: bytes, source: str = "") -> Optional[dict[str, Any]]:
    if not data.startswith(b"AVB0"):
        return None
    result: dict[str, Any] = {"source": source, "sha256": sha256_bytes(data), "size": len(data)}
    if len(data) < 256:
        result["error"] = "AVB header is truncated"
        return result
    result["required_libavb_major"] = u32(data, 4)
    result["required_libavb_minor"] = u32(data, 8)
    result["authentication_block_size"] = u64(data, 12)
    result["auxiliary_block_size"] = u64(data, 20)
    result["algorithm_type"] = u32(data, 28)
    result["hash_offset"] = u64(data, 32)
    result["hash_size"] = u64(data, 40)
    result["signature_offset"] = u64(data, 48)
    result["signature_size"] = u64(data, 56)
    result["public_key_offset"] = u64(data, 64)
    result["public_key_size"] = u64(data, 72)
    result["descriptors_offset"] = u64(data, 96)
    result["descriptors_size"] = u64(data, 104)
    result["rollback_index"] = u64(data, 112)
    result["flags"] = u32(data, 120)
    result["rollback_index_location"] = u32(data, 124)
    result["release_string"] = data[128:176].split(b"\0", 1)[0].decode("ascii", errors="replace")
    return result


def parse_linux_image(data: bytes) -> Optional[dict[str, Any]]:
    if len(data) < 64 or data[56:60] != b"ARM\x64":
        return None
    text_offset, image_size, flags = struct.unpack_from("<QQQ", data, 8)
    page_code = (flags >> 1) & 3
    return {"magic": "ARM\\x64", "text_offset": text_offset, "image_size": image_size,
            "flags": flags, "endianness": "BE" if flags & 1 else "LE",
            "page_size": {0: "unspecified", 1: "4K", 2: "16K", 3: "64K"}.get(page_code, "unknown"),
            "placement": "anywhere" if flags & 8 else "near DRAM base"}


def parse_boot_image(data: bytes, source: str = "") -> Optional[dict[str, Any]]:
    if data.startswith(b"ANDROID!"):
        result: dict[str, Any] = {"source": source, "format": "Android boot image",
                                  "sha256": sha256_bytes(data), "size": len(data), "errors": [], "warnings": []}
        if len(data) < 44:
            result["errors"].append("Android boot header is truncated")
            return result
        version_field = u32(data, 40)
        result["header_version"] = version_field
        if version_field <= 2:
            result["page_size"] = u32(data, 36)
            result["kernel_size"] = u32(data, 8)
            result["kernel_addr"] = u32(data, 12)
            result["ramdisk_size"] = u32(data, 16)
            result["ramdisk_addr"] = u32(data, 20)
            result["second_size"] = u32(data, 24)
            result["second_addr"] = u32(data, 28)
            result["tags_addr"] = u32(data, 32)
            result["os_version"] = u32(data, 44)
            result["name"] = data[48:64].split(b"\0", 1)[0].decode("ascii", errors="replace")
            result["cmdline"] = (data[64:576] + data[608:1632]).split(b"\0", 1)[0].decode("ascii", errors="replace")
            header_size = 1632
            if version_field >= 1:
                if len(data) < 1648:
                    result["errors"].append("v1/v2 header is truncated")
                    return result
                result["recovery_dtbo_size"] = u32(data, 1632)
                result["recovery_dtbo_offset"] = u64(data, 1636)
                header_size = u32(data, 1644)
            if version_field >= 2:
                if len(data) < 1660:
                    result["errors"].append("v2 header is truncated")
                    return result
                result["dtb_size"] = u32(data, 1652)
                result["dtb_addr"] = u64(data, 1656)
            page = result["page_size"]
            if page < 512 or page > 65536 or page & (page - 1):
                result["errors"].append(f"unsafe page size {page}")
                return result
            if header_size < 1632 or header_size > 1024 * 1024:
                result["warnings"].append(f"unusual header_size {header_size}")
            result["header_size"] = header_size
            cursor = page
            sections: dict[str, tuple[int, int]] = {}
            for name, size in (("kernel", result["kernel_size"]), ("ramdisk", result["ramdisk_size"]),
                               ("second", result["second_size"]), ("recovery_dtbo", result.get("recovery_dtbo_size", 0)),
                               ("dtb", result.get("dtb_size", 0))):
                if size:
                    checked_end(cursor, size, len(data))
                    sections[name] = (cursor, size)
                cursor = align(checked_end(cursor, size), page)
            result["sections"] = {name: {"offset": offset, "size": size} for name, (offset, size) in sections.items()}
            if result.get("recovery_dtbo_offset") and result["recovery_dtbo_offset"] != sections.get("recovery_dtbo", (None, 0))[0]:
                result["warnings"].append("recovery_dtbo_offset differs from sequential layout")
            if "kernel" in sections:
                kernel_offset, kernel_size = sections["kernel"]
                result["linux_image"] = parse_linux_image(data[kernel_offset:kernel_offset + kernel_size])
            if "dtb" in sections:
                dtb_offset, dtb_size = sections["dtb"]
                result["dtbs"] = scan_dtbs(data[dtb_offset:dtb_offset + dtb_size], f"{source}+0x{dtb_offset:x}")
            else:
                result["dtbs"] = []
        else:
            # v3/v4 have no page-size field in boot_img_hdr.  AOSP uses a
            # 4096-byte section alignment; vendor_boot is the DTB carrier.
            result["os_version"] = u32(data, 16)
            result["header_size"] = u32(data, 20)
            result["kernel_size"] = u32(data, 8)
            result["ramdisk_size"] = u32(data, 12)
            result["cmdline"] = data[44:1580].split(b"\0", 1)[0].decode("ascii", errors="replace")
            result["signature_size"] = u32(data, 1580) if len(data) >= 1584 else None
            result["page_size"] = 4096
            cursor = result["page_size"]
            sections = {}
            for name, size in (("kernel", result["kernel_size"]), ("ramdisk", result["ramdisk_size"])):
                checked_end(cursor, size, len(data))
                sections[name] = (cursor, size)
                cursor = align(checked_end(cursor, size), result["page_size"])
            result["sections"] = {name: {"offset": offset, "size": size} for name, (offset, size) in sections.items()}
            if "kernel" in sections:
                kernel_offset, kernel_size = sections["kernel"]
                result["linux_image"] = parse_linux_image(data[kernel_offset:kernel_offset + kernel_size])
            result["dtbs"] = []
            result["warnings"].append("v3/v4 DTB must be obtained from vendor_boot, not boot.img")
        return result
    if data.startswith(b"VNDRBOOT"):
        result = {"source": source, "format": "Android vendor boot image",
                  "sha256": sha256_bytes(data), "size": len(data), "errors": [], "warnings": []}
        if len(data) >= 32:
            result.update({"header_version": u32(data, 8), "page_size": u32(data, 12),
                           "vendor_ramdisk_size": u32(data, 16), "dtb_size": u32(data, 20),
                           "dtb_addr": u64(data, 24)})
            page = result["page_size"]
            if page and page <= 65536 and page & (page - 1):
                result["errors"].append("vendor_boot page size is not a power of two")
            elif page:
                dtb_offset = align(page + result["vendor_ramdisk_size"], page)
                if result["dtb_size"]:
                    try:
                        checked_end(dtb_offset, result["dtb_size"], len(data))
                        result["dtbs"] = scan_dtbs(data[dtb_offset:dtb_offset + result["dtb_size"]], f"{source}+0x{dtb_offset:x}")
                    except ReconError as error:
                        result["errors"].append(str(error))
        return result
    return None


def parse_props(raw: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in raw.splitlines():
        match = re.match(r"^\[([^]]+)\]: \[([^]]*)\]$", line.strip())
        if match:
            result[match.group(1)] = match.group(2)
    return result


def parse_devices(raw: str) -> list[str]:
    devices = []
    for line in raw.splitlines():
        if line.startswith("List of devices") or not line.strip():
            continue
        pieces = line.split()
        if pieces and pieces[0] != "*":
            if len(pieces) > 1 and pieces[1] == "device":
                devices.append(pieces[0])
    return devices


def adb_run(adb: str, serial: str, args: list[str], binary: bool = False) -> tuple[int, bytes, bytes]:
    command = [adb, "-s", serial] + args
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    return completed.returncode, completed.stdout, completed.stderr


def capture_device(output: Path, requested_serial: str = "") -> dict[str, Any]:
    adb = shutil.which("adb")
    result: dict[str, Any] = {"available": False, "serial": requested_serial, "properties": {}, "commands": {}}
    if not adb:
        result["error"] = "adb is not installed or not on PATH"
        return result
    devices_proc = subprocess.run([adb, "devices", "-l"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    devices_text = devices_proc.stdout.decode("utf-8", errors="replace")
    (output / "adb-devices.txt").write_text(devices_text, encoding="utf-8")
    devices = parse_devices(devices_text)
    serial = requested_serial or (devices[0] if len(devices) == 1 else "")
    if not serial:
        result["error"] = "no unique online ADB device; pass -Serial when more than one is attached"
        result["online_devices"] = devices
        return result
    result["serial"] = serial
    def capture(name: str, args: list[str], filename: str, binary: bool = False) -> bytes:
        code, stdout, stderr = adb_run(adb, serial, args, binary=binary)
        result["commands"][name] = {"exit_code": code, "stderr": stderr.decode("utf-8", errors="replace")}
        path = output / filename
        if binary:
            path.write_bytes(stdout)
        else:
            path.write_text(stdout.decode("utf-8", errors="replace"), encoding="utf-8")
        return stdout
    getprop_raw = capture("getprop", ["shell", "getprop"], "getprop.txt")
    props = parse_props(getprop_raw.decode("utf-8", errors="replace"))
    result["properties"] = {name: props.get(name, "<missing>") for name in PROPERTY_NAMES}
    result["all_properties"] = props
    capture("uname", ["shell", "uname", "-a"], "uname-a.txt")
    capture("uname_machine", ["shell", "uname", "-m"], "uname-m.txt")
    capture("cpuinfo", ["shell", "cat", "/proc/cpuinfo"], "proc-cpuinfo.txt")
    capture("meminfo", ["shell", "cat", "/proc/meminfo"], "proc-meminfo.txt")
    capture("iomem", ["shell", "cat", "/proc/iomem"], "proc-iomem.txt")
    capture("interrupts", ["shell", "cat", "/proc/interrupts"], "proc-interrupts.txt")
    capture("partitions", ["shell", "cat", "/proc/partitions"], "proc-partitions.txt")
    capture("cmdline", ["shell", "cat", "/proc/cmdline"], "proc-cmdline.txt")
    capture("block_by_name", ["shell", "ls", "-l", "/dev/block/by-name"], "block-by-name.txt")
    capture("bootdevice_by_name", ["shell", "ls", "-l", "/dev/block/bootdevice/by-name"], "block-bootdevice-by-name.txt")
    for dt_path in ("/sys/firmware/fdt", "/proc/bootconfig"):
        name = "device-tree.dtb" if dt_path.endswith("fdt") else "proc-bootconfig.txt"
        output_bytes = capture(dt_path, ["exec-out", "cat", dt_path], name, binary=dt_path.endswith("fdt"))
        if dt_path.endswith("fdt") and output_bytes[:4] == b"\xd0\x0d\xfe\xed":
            result["dtb_path"] = str(output / name)
            break
    result["available"] = True
    return result


def artifact_name_is_relevant(name: str) -> bool:
    lower = name.lower().replace("\\", "/")
    return any(token in Path(lower).name for token in ("boot", "recovery", "dtbo", "vbmeta", "vendor_boot", "super", "userdata", "metadata", "system", "vendor")) or lower.endswith((".img", ".bin", ".tar", ".tar.md5", ".lz4"))


def parse_artifact(name: str, data: bytes) -> dict[str, Any]:
    boot = parse_boot_image(data, name)
    if boot is not None:
        return boot
    vbmeta = parse_vbmeta(data, name)
    if vbmeta is not None:
        return {"format": "AVB vbmeta", **vbmeta}
    if Path(name).name.lower().startswith("dtbo") or len(data) >= 24:
        dtbo = parse_dtbo(data, name)
        if "header" in dtbo and not dtbo["errors"]:
            return {"format": "Android DTBO", **dtbo}
    return {"source": name, "format": "unclassified artifact", "sha256": sha256_bytes(data), "size": len(data)}


def artifact_from_file(path: Path) -> tuple[dict[str, Any], Optional[bytes]]:
    info: dict[str, Any] = {"source": str(path), "filename": path.name,
                            "size": path.stat().st_size, "sha256": sha256_file(path)}
    if path.stat().st_size > MAX_ARCHIVE_MEMBER_BYTES:
        info["format"] = "large artifact not parsed"
        return info, None
    data = path.read_bytes()
    parsed = parse_artifact(str(path), data)
    info.update(parsed)
    return info, data


def collect_firmware(directory: Optional[str]) -> dict[str, Any]:
    result: dict[str, Any] = {"directory": directory or "", "files": [], "artifacts": [], "archive_members": [], "errors": []}
    if not directory:
        return result
    root = Path(directory).expanduser().resolve()
    if not root.is_dir():
        result["errors"].append(f"firmware directory not found: {root}")
        return result
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        try:
            result["files"].append({"path": str(path), "name": path.name,
                                    "size": path.stat().st_size, "sha256": sha256_file(path)})
            if artifact_name_is_relevant(str(path)):
                info, _ = artifact_from_file(path)
                if info.get("format") != "unclassified artifact" or path.suffix.lower() in {".img", ".bin"}:
                    result["artifacts"].append(info)
        except (OSError, ReconError, struct.error) as error:
            result["errors"].append(f"{path}: {error}")
        if path.suffix.lower() in {".tar", ".md5"} or path.name.lower().endswith(".tar.md5"):
            try:
                with tarfile.open(path, "r:*") as archive:
                    for member in archive.getmembers():
                        if member.isdir() or not artifact_name_is_relevant(member.name):
                            continue
                        member_info = {"archive": str(path), "name": member.name, "size": member.size}
                        if member.size <= MAX_ARCHIVE_MEMBER_BYTES:
                            stream = archive.extractfile(member)
                            if stream is not None:
                                payload = stream.read()
                                member_info["sha256"] = sha256_bytes(payload)
                                if any(token in Path(member.name).name.lower() for token in ("boot", "recovery", "dtbo", "vbmeta", "vendor_boot")):
                                    member_info["artifact"] = parse_artifact(f"{path}!{member.name}", payload)
                        else:
                            member_info["sha256"] = "not read (larger than safety cap)"
                        result["archive_members"].append(member_info)
            except (tarfile.TarError, OSError) as error:
                result["errors"].append(f"{path}: archive listing failed: {error}")
    return result


def parse_cpuinfo(raw: str) -> dict[str, Any]:
    processors = re.findall(r"(?m)^processor\s*:\s*(\d+)", raw)
    implementers = sorted(set(re.findall(r"(?m)^CPU implementer\s*:\s*(.+)$", raw)))
    parts = sorted(set(re.findall(r"(?m)^CPU part\s*:\s*(.+)$", raw)))
    architecture = sorted(set(re.findall(r"(?m)^CPU architecture\s*:\s*(.+)$", raw)))
    features = sorted(set(re.findall(r"(?m)^Features\s*:\s*(.+)$", raw)))
    return {"count": len(processors), "implementers": implementers, "parts": parts,
            "architecture": architecture, "features": features,
            "aarch64_hint": "AArch64" if "aarch64" in raw.lower() or "ARMv8" in raw else "unknown"}


def parse_meminfo(raw: str) -> dict[str, str]:
    values = {}
    for line in raw.splitlines():
        match = re.match(r"^(MemTotal|MemFree|CmaTotal|CmaFree|KernelCode|KernelData|DirectMap[^:]*):\s*(.*)$", line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def table(headers: list[str], rows: list[list[Any]]) -> str:
    def clean(value: Any) -> str:
        return str(value).replace("|", "\\|").replace("\n", "<br>")
    output = ["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |"]
    output += ["| " + " | ".join(clean(v) for v in row) + " |" for row in rows]
    return "\n".join(output)


def report_status(device: dict[str, Any], firmware: dict[str, Any], dt_sources: list[dict[str, Any]]) -> tuple[str, str]:
    props = device.get("properties", {})
    model = props.get("ro.product.model", "<missing>")
    codename = props.get("ro.product.device", "<missing>")
    identity = model == EXPECTED_MODEL and codename == EXPECTED_DEVICE
    parsed_dtb = any(source.get("parse_ok") for source in dt_sources)
    parsed_boot = any(a.get("format") == "Android boot image" and not a.get("errors") for a in firmware.get("artifacts", [])) or any(m.get("artifact", {}).get("format") == "Android boot image" for m in firmware.get("archive_members", []))
    if not device.get("available"):
        return "E", "exact attached tablet identity is not available"
    if not identity:
        return "E", "attached identity is missing or does not match SM-X200/gta8wifi"
    if parsed_dtb and parsed_boot:
        return "A", "identity, a DTB, and an Android boot image were parsed; custom handoff remains design-only"
    if parsed_dtb:
        return "B", "identity and DT mapping succeeded; matching boot artifact/handoff evidence is incomplete"
    return "C", "identity was captured but usable stock DTB/boot artifacts remain unavailable"


def write_reports(output: Path, device: dict[str, Any], firmware: dict[str, Any]) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=True)
    dt_sources: list[dict[str, Any]] = []
    device_dtb = output / "device-tree.dtb"
    if device_dtb.is_file() and device_dtb.read_bytes()[:4] == b"\xd0\x0d\xfe\xed":
        try:
            parser = FdtParser(device_dtb.read_bytes(), str(device_dtb))
            dt_sources.append({"source": str(device_dtb), "classification": "stock-proven (read-only Android DT)", "parse_ok": True, "parser": parser})
        except ReconError as error:
            dt_sources.append({"source": str(device_dtb), "classification": "stock candidate", "parse_ok": False, "error": str(error)})
    for artifact in firmware.get("artifacts", []):
        for dtb in artifact.get("dtbs", []):
            dt_sources.append({"source": dtb["parser"].source, "classification": "supplied firmware; not correlated to installed build", "parse_ok": True, "parser": dtb["parser"], "hash": dtb["sha256"]})
    for member in firmware.get("archive_members", []):
        artifact = member.get("artifact", {})
        for dtb in artifact.get("dtbs", []):
            dt_sources.append({"source": dtb["parser"].source, "classification": "supplied firmware archive; not correlated to installed build", "parse_ok": True, "parser": dtb["parser"], "hash": dtb["sha256"]})
    for source in dt_sources:
        parser = source.get("parser")
        if parser is not None:
            try:
                parser.platform_map()
            except ReconError as error:
                source["parse_ok"] = False
                source["error"] = str(error)
    outcome, reason = report_status(device, firmware, dt_sources)
    props = device.get("properties", {})
    raw_cpu = (output / "proc-cpuinfo.txt").read_text(encoding="utf-8", errors="replace") if (output / "proc-cpuinfo.txt").is_file() else ""
    raw_mem = (output / "proc-meminfo.txt").read_text(encoding="utf-8", errors="replace") if (output / "proc-meminfo.txt").is_file() else ""
    cpu = parse_cpuinfo(raw_cpu)
    mem = parse_meminfo(raw_mem)
    model = props.get("ro.product.model", "<missing>")
    codename = props.get("ro.product.device", "<missing>")
    identity_matches = model == EXPECTED_MODEL and codename == EXPECTED_DEVICE
    device_report = f"""# Galaxy Tab A8 T0 Device Report

Status: **Outcome {outcome}** — {reason}

This report is generated by a read-only inspection pass. Missing values are evidence. No root, partition read, flashing, unlock, AVB modification, or boot-chain bypass operation is performed.

## Identity

{table(["Property", "Captured value", "Interpretation"], [[name, props.get(name, "<missing>"), "expected match" if name == "ro.product.model" and props.get(name) == EXPECTED_MODEL else "expected match" if name == "ro.product.device" and props.get(name) == EXPECTED_DEVICE else "read-only capture"] for name in PROPERTY_NAMES])}

Identity gate: **{"PASS — SM-X200 / gta8wifi" if identity_matches else "NOT PROVEN — do not apply SM-X200 constants"}**.

## CPU and Android kernel

{table(["Field", "Evidence"], [["CPU count", cpu["count"]], ["Implementer", ", ".join(cpu["implementers"]) or "<missing>"], ["Part", ", ".join(cpu["parts"]) or "<missing>"], ["Architecture", ", ".join(cpu["architecture"]) or "<missing>"], ["Features", "; ".join(cpu["features"]) or "<missing>"], ["AArch64 assessment", cpu["aarch64_hint"]], ["uname -a", (output / "uname-a.txt").read_text(encoding="utf-8", errors="replace").strip() if (output / "uname-a.txt").is_file() else "<missing>"], ["uname -m", (output / "uname-m.txt").read_text(encoding="utf-8", errors="replace").strip() if (output / "uname-m.txt").is_file() else "<missing>"]])}

## Memory

{table(["/proc/meminfo field", "Value"], [[key, value] for key, value in mem.items()] or [["all fields", "<missing or denied>"]])}

`/proc/iomem` is captured as [proc-iomem.txt](proc-iomem.txt) when permitted. Treat RAM totals as Android-visible, not automatically usable by guideXOS; reserved, secure-world, modem, display, DMA/CMA, and firmware regions require DT/boot evidence.

## AVB, slots, and OEM state

{table(["Field", "Value", "T0 action"], [["bootloader lock", props.get("ro.boot.flash.locked", "<missing>"), "observe only"], ["verified boot", props.get("ro.boot.verifiedbootstate", "<missing>"), "observe only"], ["vbmeta device state", props.get("ro.boot.vbmeta.device_state", "<missing>"), "observe only"], ["AVB secureboot", props.get("ro.boot.secureboot", "<missing>"), "observe only"], ["slot suffix", props.get("ro.boot.slot_suffix", "<missing>"), "A/B evidence only"], ["dynamic partitions", props.get("ro.boot.dynamic_partitions", "<missing>"), "observe only"], ["virtual A/B", props.get("ro.virtual_ab.enabled", "<missing>"), "observe only"], ["OEM unlock support property", props.get("ro.oem_unlock_supported", "<missing>"), "do not unlock"]])}

No OEM unlock action is attempted. Any later unlock must be treated as a security-state change that may erase data and affect Knox/warranty state.

## Read-only command evidence

Captured files include `getprop.txt`, `proc-cpuinfo.txt`, `proc-meminfo.txt`, `proc-iomem.txt`, `proc-interrupts.txt`, `proc-partitions.txt`, `proc-cmdline.txt`, and by-name directory listings when ADB permissions allow them. ADB status: **{"available" if device.get("available") else "unavailable"}**; serial: `{device.get("serial", "<none>")}`.
"""
    (output / "DEVICE_REPORT.md").write_text(device_report, encoding="utf-8")

    artifact_rows: list[list[Any]] = []
    for info in firmware.get("files", []):
        artifact_rows.append([info["name"], info["size"], info["sha256"]])
    boot_sections: list[str] = []
    for info in firmware.get("artifacts", []):
        if info.get("format") in {"Android boot image", "Android vendor boot image", "Android DTBO", "AVB vbmeta"}:
            fields = []
            for key, value in info.items():
                if key in {"dtbs", "errors", "warnings", "sections", "linux_image", "parser"}:
                    continue
                if key == "entries" and isinstance(value, list):
                    value = [{entry_key: entry_value for entry_key, entry_value in entry.items() if entry_key != "parser"} for entry in value]
                fields.append([key, value])
            boot_sections.append(f"### `{info.get('source')}`\n\n" + table(["Field", "Value"], fields))
            if info.get("sections"):
                boot_sections.append(table(["Section", "Offset", "Size"], [[key, value["offset"], value["size"]] for key, value in info["sections"].items()]))
            if info.get("linux_image"):
                boot_sections.append("Linux ARM64 Image header: " + json.dumps(info["linux_image"], sort_keys=True))
            if info.get("errors"):
                boot_sections.append("Errors: " + "; ".join(info["errors"]))
            if info.get("warnings"):
                boot_sections.append("Warnings: " + "; ".join(info["warnings"]))
    boot_report = f"""# Galaxy Tab A8 T0 Boot-Image Report

Only supplied files and in-memory ADB output are inspected. Source firmware is never modified and no artifact is written back to a device.

## Firmware file integrity

{table(["Filename", "Bytes", "SHA-256"], artifact_rows or [["<none supplied>", "—", "—"]])}

## Parsed artifacts

{"\n\n".join(boot_sections) if boot_sections else "No Android boot, vendor_boot, DTBO, or AVB artifact was available for parsing."}

## Interpretation boundary

Android boot header v2 is the expected community lead for SM-X200, including a DTB field; this is not an installed-firmware fact until a matching artifact is hash-correlated with the device build. Header v3/v4 requires vendor_boot DTB handling. The likely kernel load address and entry state remain unknown until the exact artifact and stock handoff evidence are correlated.
"""
    (output / "BOOT_IMAGE_REPORT.md").write_text(boot_report, encoding="utf-8")

    dt_rows = []
    for source in dt_sources:
        parser = source.get("parser")
        usable = parser is not None and source.get("parse_ok")
        dt_rows.append([source["source"], source["classification"], parser.header["total_size"] if usable else "—", parser.to_json()["sha256"] if usable else source.get("error", "—"), len(parser.nodes) if usable else "—"])
    dt_sections = []
    for source in dt_sources:
        parser = source.get("parser")
        if not parser or not source.get("parse_ok"):
            continue
        platform = parser.platform_map()
        rows = []
        for record in platform["all_nodes"]:
            regs = "; ".join(f"{hex_value(entry.get('address'))}+{hex_value(entry.get('size'))} -> {hex_value(entry.get('translated'))}" for entry in record.get("reg", []))
            irqs = "; ".join(entry["raw"] for entry in record.get("interrupts", []))
            rows.append([record["path"], ", ".join(record["compatible"]), regs or "—", irqs or "—"])
        dt_sections.append(f"### `{source['source']}` ({source['classification']})\n\n" + table(["Node", "Compatible", "reg (raw → translated)", "interrupts (raw tuples)"], rows or [["<none>", "—", "—", "—"]]))
    dt_report = f"""# Galaxy Tab A8 T0 Device-Tree Report

DT source classification is explicit. Raw cells are retained so a translated address is never mistaken for an encoded address. Empty `ranges` is reported as identity mapping; absent or malformed `ranges` is unknown/error.

## DTB inventory

{table(["Source", "Classification", "Bytes", "DTB SHA-256", "Node count"], dt_rows or [["<none>", "unknown", "—", "—", "—"]])}

{"\n\n".join(dt_sections) if dt_sections else "No parseable DTB was available."}

## Required evidence searches

The parser inventories `compatible`, `model`, `memory`, `reserved-memory`, `chosen`, `cpus`, interrupt controllers, architectural timer, serial/UART, SDHCI/MMC/UFS, USB, display/DSI/panel/backlight, GPIO/pinctrl, I2C/SPI, regulators, framebuffer, touch, buttons, and PSCI nodes. It records `reg` and interrupt cells verbatim and separately computes safe nested `ranges` translations.

## Classification rule

Running Android DT under a model-verified device is **stock-proven**. Supplied firmware DTB/DTBO is only **stock candidate** until model/build correlation. Samsung/downstream DTS is **source-proven** for what the source says, not proof of the installed board. Community configuration and other UMS512 boards are **community-derived** or **SoC-family inference**.
"""
    (output / "DT_REPORT.md").write_text(dt_report, encoding="utf-8")

    known = ["boot", "recovery", "dtbo", "vbmeta", "super", "userdata", "vendor", "system", "metadata", "misc"]
    by_name = ""
    for filename in ("block-by-name.txt", "block-bootdevice-by-name.txt"):
        path = output / filename
        if path.is_file():
            by_name += f"\n### {filename}\n\n```text\n{path.read_text(encoding='utf-8', errors='replace').strip()}\n```\n"
    partition_report = f"""# Galaxy Tab A8 T0 Partition Report

Known names attempted: `{', '.join(known)}`. The report uses firmware filenames/archive members and read-only directory listings only; it never opens a live block device and never invokes `dd`.

## Supplied firmware files and archive members

{table(["Source", "Entry", "Bytes", "SHA-256"], [[item.get("archive", item.get("path", "")), item.get("name", ""), item.get("size", ""), item.get("sha256", "not computed")] for item in firmware.get("files", []) + firmware.get("archive_members", [])] or [["<none>", "—", "—", "—"]])}

## Live by-name evidence
{by_name or "\nNo live by-name listing was captured.\n"}

## A/B and dynamic-partition assessment

Use `DEVICE_REPORT.md` values for `ro.boot.slot_suffix`, `ro.boot.dynamic_partitions`, and `ro.virtual_ab.enabled`. A nonempty slot suffix is evidence of slot selection, not permission to write. `super`/dynamic partition membership from community BoardConfig is recorded as community-derived until matching firmware metadata is supplied.
"""
    (output / "PARTITION_REPORT.md").write_text(partition_report, encoding="utf-8")

    pmap = next((source["parser"].platform_map() for source in dt_sources if source.get("parser")), None)
    platform_report = f"""# Galaxy Tab A8 T0 Normalized Platform Map

Outcome: **{outcome}**. This map is design input only; no Samsung assumptions have been added to Pi/QEMU paths.

{table(["Subsystem", "Identified / evidence", "Address / IRQ evidence", "guideXOS support", "T1 required"], [
    ["CPU", "AArch64 only after uname/cpuinfo capture", "topology from DT/cpuinfo", "common ARM64 reusable", "single boot CPU"],
    ["Memory", json.dumps(pmap.get("memory", []), sort_keys=True) if pmap else "unknown", "DT reg and reserved-memory", "BootInfo map only", "reserve safe regions"],
    ["Boot chain", "Android/Samsung path; no UEFI assumption", "exact stages unknown", "adapter design only", "stock handoff capture"],
    ["DTB", "parseable" if pmap else "unknown", "hash/count in DT_REPORT.md", "BootInfo DTB", "matching exact DTB"],
    ["GIC", pmap.get("gic", {}).get("generation", "unknown") if pmap else "unknown", json.dumps(pmap.get("gic", {}).get("regs", []), sort_keys=True) if pmap else "unknown", "none on Tab A8 yet", "GIC routing"],
    ["Timer", "architectural timer if DT proves it" if pmap else "unknown", "raw PPI tuples in DT report", "architecture-neutral parser", "frequency/IRQ"],
    ["UART", "candidate UART nodes" if pmap else "unknown", "raw reg/IRQ in DT report", "not implemented", "diagnostic access"],
    ["Framebuffer", "simple-framebuffer only if exposed" if pmap else "unknown", "reserved range if exposed", "none", "handoff test"],
    ["Display/DSI", "DT inventory only" if pmap else "unknown", "controller/panel nodes", "none", "panel bring-up or preserve buffer"],
    ["Backlight", "DT inventory only" if pmap else "unknown", "controller/regulator nodes", "none", "power sequencing"],
    ["Internal storage", "DT MMC/SDHCI/UFS inventory" if pmap else "unknown", "reg/IRQ/clock clues", "none", "read-only driver plan"],
    ["MicroSD", "not assumed" if not pmap else "DT inventory only", "bootloader access unknown", "none", "Download Mode evidence"],
    ["USB", "DT controller/PHY inventory" if pmap else "unknown", "reg/IRQ/role switch", "none", "gadget or host diagnostic"],
    ["Touch", "controller/bus/IRQ inventory" if pmap else "unknown", "I2C/SPI/GPIO tuples", "none", "input driver"],
    ["Buttons", "GPIO/PMIC key inventory" if pmap else "unknown", "key nodes/interrupts", "none", "early input"],
    ["Wi-Fi", "board-specific; no implementation", "unknown", "none", "later"],
    ["Audio", "board-specific; no implementation", "unknown", "none", "later"],
    ["Battery/PMIC", "board-specific; no implementation", "unknown", "none", "later"],
    ["GPU", "Mali identity deferred to DT/source", "unknown", "irrelevant initially", "software rendering first"],
])}

## First-boot adapter design (not built or flashed)

`Samsung/Unisoc bootloader → Android-compatible guideXOS boot image → small ARM64 Tab A8 adapter → normalized BootInfo (memory map, DTB, framebuffer, ramdisk, platform identity) → existing common ARM64 kernel`.

The adapter boundary must normalize x0/DTB, MMU/cache/EL state, command-line/ramdisk, and any preserved framebuffer. It must not make the common kernel consume Samsung bootloader quirks. Initial acceptance target is one boot CPU plus a serial/USB/framebuffer marker; SMP and GPU are later work.
"""
    (output / "PLATFORM_MAP.md").write_text(platform_report, encoding="utf-8")
    summary = {"outcome": outcome, "reason": reason, "dt_sources": len(dt_sources),
               "dtb_hashes": [source.get("hash") or source["parser"].to_json()["sha256"] for source in dt_sources if source.get("parser")],
               "artifact_count": len(firmware.get("artifacts", [])), "output": str(output)}
    (output / "SUMMARY.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    return summary


def run(args: argparse.Namespace) -> int:
    output = Path(args.output_directory).resolve()
    output.mkdir(parents=True, exist_ok=True)
    device = capture_device(output, args.serial) if args.capture_device else {"available": False, "error": "device capture not requested", "properties": {}}
    firmware = collect_firmware(args.firmware_directory)
    summary = write_reports(output, device, firmware)
    print(f"T0 reports: {output}")
    print(f"Outcome {summary['outcome']}: {summary['reason']}")
    if summary["outcome"] == "A":
        print("AARCH64_TAB_A8_T0_RECON_PASS")
    else:
        print("AARCH64_TAB_A8_T0_RECON_NOT_READY")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read-only Galaxy Tab A8 T0 inspection")
    parser.add_argument("--output-directory", required=True)
    parser.add_argument("--firmware-directory", default="")
    parser.add_argument("--serial", default="")
    parser.add_argument("--capture-device", action="store_true")
    return run(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())
