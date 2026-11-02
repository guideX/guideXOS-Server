#!/usr/bin/env python3
"""Bounded independent decoder for a captured Flattened Device Tree blob."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9
MAX_BLOB_SIZE = 16 * 1024 * 1024


class DecodeError(ValueError):
    pass


def be32(data: bytes, offset: int, end: int) -> int:
    if offset < 0 or offset + 4 > end:
        raise DecodeError(f"truncated 32-bit field at blob offset 0x{offset:x}")
    return struct.unpack_from(">I", data, offset)[0]


def bounded_range(offset: int, size: int, total: int, label: str) -> tuple[int, int]:
    if offset < 0 or size < 0 or offset > total or size > total - offset:
        raise DecodeError(f"{label} range is outside FDT totalsize")
    return offset, offset + size


def c_string(data: bytes, start: int, end: int, label: str) -> tuple[bytes, int]:
    if start < 0 or start >= end:
        raise DecodeError(f"{label} starts outside its bounded block")
    terminator = data.find(b"\0", start, end)
    if terminator < 0:
        raise DecodeError(f"{label} is not NUL-terminated inside its bounded block")
    return data[start:terminator], terminator + 1


def expected_encoding(name: str) -> str:
    if name == "compatible":
        return "nonempty NUL-terminated string list"
    if name in {"#address-cells", "#size-cells", "#interrupt-cells", "#redistributor-regions"}:
        return "one big-endian 32-bit cell (4 bytes)"
    if name == "redistributor-stride":
        return "binding-defined cell value; inspect raw cells and GIC binding"
    if name == "status":
        return "NUL-terminated string"
    if name in {"device_type", "model"}:
        return "NUL-terminated string"
    if name in {"reg", "interrupts"}:
        return "big-endian cells interpreted using the parent bus or interrupt binding"
    return "property-specific encoding from its applicable Devicetree binding"


def interpret(name: str, value: bytes, parent_address_cells: int, parent_size_cells: int) -> dict:
    result: dict = {"expected_encoding": expected_encoding(name)}
    if name in {"compatible", "status", "device_type", "model"}:
        if value.endswith(b"\0"):
            pieces = value[:-1].split(b"\0") if value else []
            result["strings"] = [piece.decode("utf-8", "backslashreplace") for piece in pieces]
            result["string_list_has_empty_element"] = any(not piece for piece in pieces)
        else:
            result["string_error"] = "missing final NUL"
    if len(value) % 4 == 0:
        result["u32_cells"] = list(struct.unpack(f">{len(value) // 4}I", value)) if value else []
    if name == "reg" and (parent_address_cells + parent_size_cells) > 0:
        stride = 4 * (parent_address_cells + parent_size_cells)
        result["parent_address_cells"] = parent_address_cells
        result["parent_size_cells"] = parent_size_cells
        result["reg_tuple_stride_bytes"] = stride
        result["reg_tuple_count"] = len(value) // stride if stride and len(value) % stride == 0 else None
    return result


def decode(path: Path, requested_offset: int | None) -> dict:
    data = path.read_bytes()
    if len(data) < 40:
        raise DecodeError("file is shorter than the 40-byte version 17 FDT header")
    if len(data) > MAX_BLOB_SIZE:
        raise DecodeError(f"file exceeds the bounded {MAX_BLOB_SIZE}-byte decoder limit")

    fields = struct.unpack_from(">10I", data, 0)
    (magic, total, struct_offset, strings_offset, reserve_offset, version,
     last_compatible, boot_cpuid, strings_size, struct_size) = fields
    if magic != FDT_MAGIC:
        raise DecodeError(f"bad FDT magic 0x{magic:08x}")
    if total < 40 or total > len(data) or total > MAX_BLOB_SIZE:
        raise DecodeError(f"invalid totalsize {total} for captured file length {len(data)}")
    if version < 17 or last_compatible > 17 or last_compatible > version:
        raise DecodeError(f"unsupported or inconsistent FDT version {version}/{last_compatible}")

    struct_start, struct_end = bounded_range(struct_offset, struct_size, total, "structure block")
    strings_start, strings_end = bounded_range(strings_offset, strings_size, total, "strings block")
    if struct_offset & 3:
        raise DecodeError("structure block offset is not four-byte aligned")
    if reserve_offset & 7 or reserve_offset < 40 or reserve_offset >= total:
        raise DecodeError("reservation map offset is invalid or misaligned")
    if max(struct_start, strings_start) < min(struct_end, strings_end):
        raise DecodeError("structure and strings blocks overlap")

    reserve_cursor = reserve_offset
    reservations = []
    while True:
        if reserve_cursor + 16 > total:
            raise DecodeError("reservation map has no bounded zero terminator")
        address, size = struct.unpack_from(">QQ", data, reserve_cursor)
        reserve_cursor += 16
        if address == 0 and size == 0:
            break
        reservations.append({"address": address, "size": size})
    if reserve_offset < struct_end and struct_start < reserve_cursor:
        raise DecodeError("reservation map overlaps structure block")
    if reserve_offset < strings_end and strings_start < reserve_cursor:
        raise DecodeError("reservation map overlaps strings block")

    strings = data[strings_start:strings_end]
    cursor = 0
    stack: list[dict] = []
    properties: list[dict] = []
    target: dict | None = None
    saw_end = False
    while cursor < struct_size:
        if cursor + 4 > struct_size:
            raise DecodeError(f"truncated structure token at relative offset 0x{cursor:x}")
        token_offset = cursor
        token = be32(data, struct_start + cursor, struct_end)
        cursor += 4
        if token == FDT_BEGIN_NODE:
            name_bytes, name_end = c_string(data, struct_start + cursor, struct_end, "node name")
            cursor = name_end - struct_start
            aligned = (cursor + 3) & ~3
            if aligned > struct_size:
                raise DecodeError(f"node name padding exceeds structure block at 0x{token_offset:x}")
            cursor = aligned
            name = name_bytes.decode("utf-8", "backslashreplace")
            if not stack:
                if name:
                    raise DecodeError("root node name is not empty")
                parent_address_cells, parent_size_cells = 2, 1
                full_path = "/"
            else:
                parent_address_cells = stack[-1]["address_cells"]
                parent_size_cells = stack[-1]["size_cells"]
                full_path = (stack[-1]["path"].rstrip("/") + "/" + name) or "/"
            stack.append({
                "name": name,
                "path": full_path,
                "address_cells": 2,
                "size_cells": 1,
                "parent_address_cells": parent_address_cells,
                "parent_size_cells": parent_size_cells,
            })
        elif token == FDT_END_NODE:
            if not stack:
                raise DecodeError(f"FDT_END_NODE without open node at 0x{token_offset:x}")
            stack.pop()
        elif token == FDT_PROP:
            if not stack or cursor + 8 > struct_size:
                raise DecodeError(f"truncated FDT_PROP descriptor at 0x{token_offset:x}")
            value_length = be32(data, struct_start + cursor, struct_end)
            name_field_offset = cursor + 4
            name_offset = be32(data, struct_start + name_field_offset, struct_end)
            value_offset = cursor + 8
            if value_length > struct_size - value_offset:
                raise DecodeError(f"FDT_PROP payload exceeds structure block at 0x{token_offset:x}")
            value_end = value_offset + value_length
            padding = (-value_length) & 3
            if padding > struct_size - value_end:
                raise DecodeError(f"FDT_PROP alignment padding exceeds structure block at 0x{token_offset:x}")
            next_cursor = value_end + padding
            if name_offset >= strings_size:
                raise DecodeError(f"FDT_PROP nameoff exceeds strings block at 0x{token_offset:x}")
            name_bytes, _ = c_string(strings, name_offset, strings_size, "property name")
            if not name_bytes:
                raise DecodeError(f"empty FDT_PROP name at 0x{token_offset:x}")
            name = name_bytes.decode("utf-8", "backslashreplace")
            value = data[struct_start + value_offset:struct_start + value_end]
            node = stack[-1]
            item = {
                "token_offset": token_offset,
                "value_length_field_offset": token_offset + 4,
                "name_offset_field_offset": token_offset + 8,
                "property_name_offset": name_offset,
                "value_offset": value_offset,
                "node_path": node["path"],
                "property_name": name,
                "value_length": value_length,
                "raw_hex": value.hex(),
                "raw_bytes": list(value),
                "inherited_address_cells": node["parent_address_cells"],
                "inherited_size_cells": node["parent_size_cells"],
                "interrupt_cells_in_node": node.get("interrupt_cells"),
                "interpreted": interpret(name, value, node["parent_address_cells"], node["parent_size_cells"]),
            }
            properties.append(item)
            if requested_offset is not None and token_offset == requested_offset:
                target = item
            if requested_offset is not None and token_offset + 4 == requested_offset:
                target = item
            if name == "#address-cells" and value_length == 4:
                node["address_cells"] = struct.unpack(">I", value)[0]
            elif name == "#size-cells" and value_length == 4:
                node["size_cells"] = struct.unpack(">I", value)[0]
            elif name == "#interrupt-cells" and value_length == 4:
                node["interrupt_cells"] = struct.unpack(">I", value)[0]
            cursor = next_cursor
        elif token == FDT_NOP:
            continue
        elif token == FDT_END:
            if stack:
                raise DecodeError(f"FDT_END with {len(stack)} open nodes at 0x{token_offset:x}")
            saw_end = True
            if any(data[struct_start + cursor:struct_end]):
                raise DecodeError("nonzero trailing bytes after FDT_END")
            break
        else:
            raise DecodeError(f"unknown structure token {token} at relative offset 0x{token_offset:x}")
    if not saw_end:
        raise DecodeError("structure block has no FDT_END token")
    if requested_offset is not None and target is None:
        raise DecodeError(f"no FDT_PROP token at structure-relative offset 0x{requested_offset:x}")

    return {
        "file": str(path.resolve()),
        "file_length": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "header": {
            "magic": f"0x{magic:08x}", "totalsize": total,
            "structure_offset": struct_offset, "structure_size": struct_size,
            "strings_offset": strings_offset, "strings_size": strings_size,
            "reservation_offset": reserve_offset, "version": version,
            "last_compatible_version": last_compatible, "boot_cpuid_phys": boot_cpuid,
        },
        "reservation_entries": reservations,
        "property_count": len(properties),
        "target_structure_offset": requested_offset,
        "target_property": target,
        "all_properties": properties if requested_offset is None else None,
        "decoder": "independent Python bounded FDT structural decoder",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path, help="captured raw DTB file")
    parser.add_argument("--structure-offset", type=lambda x: int(x, 0),
                        help="structure-block-relative FDT_PROP token or value-length-field offset")
    parser.add_argument("--output", type=Path, help="write JSON output to this path")
    args = parser.parse_args()
    try:
        result = decode(args.dtb, args.structure_offset)
        rendered = json.dumps(result, indent=2, sort_keys=True)
        if args.output:
            args.output.write_text(rendered + "\n", encoding="utf-8")
        print(rendered)
        return 0
    except (OSError, DecodeError, struct.error) as error:
        print(f"DTB decode failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
