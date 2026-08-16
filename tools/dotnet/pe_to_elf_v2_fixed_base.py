#!/usr/bin/env python3
"""guideXOS Server proof copy of guideXOSUEFI Tools/pe_to_elf_v2.py.

Origin: D:/dev/guideXOSUEFI/Tools/pe_to_elf_v2.py
Origin source revision: e2bbb0ef6d3eb78eb316235ec4b5748ee95718b0
Origin blob before the proof patch: ac3f8ecef8451e471d486c4d96a54e2b039071f7

This copy preserves the canonical converter and adds the guideXOS NativeAOT
image envelope required by the locked Windows runtime: the first PT_LOAD
contains the PE headers at the fixed image base. The production
CoffNativeCodeManager::RhRegisterOSModule path consumes those headers when it
finds the exception directory. Keep this copy synchronized with the canonical
converter; do not evolve it into a second general-purpose converter.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

ELF_MAGIC = b"\x7fELF"


def find_symbol_in_map(map_path: Path, symbol_name: str) -> int | None:
    """Parse a linker map file to find a symbol's address."""
    if not map_path.exists():
        return None
    
    content = map_path.read_text(errors='replace')
    
    for line in content.split('\n'):
        parts = line.split()
        if len(parts) >= 3:
            for i, part in enumerate(parts):
                if part == symbol_name:
                    for j in range(i + 1, min(i + 3, len(parts))):
                        candidate = parts[j]
                        if len(candidate) == 16 and all(c in '0123456789abcdefABCDEF' for c in candidate):
                            return int(candidate, 16)
    return None


def u16(b: bytes, off: int) -> int:
    return struct.unpack_from("<H", b, off)[0]


def u32(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]


def u64(b: bytes, off: int) -> int:
    return struct.unpack_from("<Q", b, off)[0]


@dataclass
class PeSection:
    name: str
    vaddr: int
    vsize: int
    raw_ptr: int
    raw_size: int
    characteristics: int


def parse_pe_sections(pe: bytes) -> tuple[int, int, int, int, list[PeSection]]:
    if pe[:2] != b"MZ":
        raise SystemExit("Input is not PE (missing MZ)")

    if len(pe) < 0x40:
        raise SystemExit("Input PE is truncated before e_lfanew")
    pe_off = u32(pe, 0x3C)
    if pe_off > len(pe) - 4:
        raise SystemExit("Input PE e_lfanew is outside the file")
    if pe[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise SystemExit("Input is not PE (missing PE\\0\\0)")

    coff_off = pe_off + 4
    number_of_sections = u16(pe, coff_off + 2)
    size_of_optional_header = u16(pe, coff_off + 16)

    opt_off = coff_off + 20
    if opt_off > len(pe) - size_of_optional_header:
        raise SystemExit("Input PE optional header is truncated")
    magic = u16(pe, opt_off)
    if magic != 0x20B:
        raise SystemExit(f"Unsupported PE optional header magic: 0x{magic:04X}")

    address_of_entry_point = u32(pe, opt_off + 16)
    image_base = u64(pe, opt_off + 24)
    size_of_headers = u32(pe, opt_off + 60)
    if size_of_headers == 0 or size_of_headers > 0x1000 or size_of_headers > len(pe):
        raise SystemExit(f"Unsupported PE header span: 0x{size_of_headers:X}")

    sec_table_off = opt_off + size_of_optional_header
    section_table_size = number_of_sections * 40
    if section_table_size > len(pe) - sec_table_off:
        raise SystemExit("Input PE section table is truncated")

    sections: list[PeSection] = []
    for i in range(number_of_sections):
        off = sec_table_off + i * 40
        name = pe[off:off + 8].split(b"\x00", 1)[0].decode("ascii", errors="replace")
        vsize = u32(pe, off + 8)
        vaddr = u32(pe, off + 12)
        raw_size = u32(pe, off + 16)
        raw_ptr = u32(pe, off + 20)
        characteristics = u32(pe, off + 36)
        if vaddr > 0xFFFFFFFF - vsize:
            raise SystemExit(f"PE section {name!r} virtual range overflows 32-bit RVA")
        if raw_ptr > len(pe) or raw_size > len(pe) - raw_ptr:
            raise SystemExit(f"PE section {name!r} raw range is outside the file")
        sections.append(PeSection(name, vaddr, vsize, raw_ptr, raw_size, characteristics))

    if image_base > 0xFFFFFFFFFFFFFFFF - address_of_entry_point:
        raise SystemExit("PE entry point address overflows 64-bit VA")
    entry = image_base + address_of_entry_point
    return image_base, address_of_entry_point, entry, size_of_headers, sections


def find_function_prologue(pe: bytes, symbol_va: int, image_base: int, sections: list[PeSection]) -> int | None:
    """Search near a symbol address to find the actual function prologue.
    
    NativeAOT RuntimeExport symbols often point to code WITHIN the function,
    not at the start. We search both forwards AND backwards to find the nearest
    prologue that could belong to this function.
    """
    symbol_rva = symbol_va - image_base
    target_section = None
    for s in sections:
        if s.vaddr <= symbol_rva < s.vaddr + s.vsize:
            target_section = s
            break
    
    if target_section is None:
        print(f"WARNING: Symbol VA 0x{symbol_va:X} not found in any section")
        return None
    
    offset_in_section = symbol_rva - target_section.vaddr
    symbol_file_offset = target_section.raw_ptr + offset_in_section

    # The map symbol is already the function start for compact NativeAOT
    # methods whose register-save set differs from the historical full
    # prologue below (for example a method that calls an internal P/Invoke
    # helper). Preserve the exact managed symbol instead of selecting the
    # next unrelated function that happens to match the old prologue.
    if pe[symbol_file_offset:symbol_file_offset + 1] == b"\x55":
        print(f"Found function prologue at symbol VA 0x{symbol_va:X} (0 bytes after symbol)")
        return symbol_va

    # Common x64 function prologue: push rbp; push r15; push r14; push r13; push r12
    full_prologue = bytes([0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54])
    
    # Search FORWARD first (symbol might point to middle of function)
    # Limit forward search to 0x800 bytes (2KB)
    search_end = min(target_section.raw_ptr + target_section.raw_size, symbol_file_offset + 0x800)
    
    for file_off in range(symbol_file_offset, search_end):
        if pe[file_off:file_off + 9] == full_prologue:
            match_offset_in_section = file_off - target_section.raw_ptr
            match_va = image_base + target_section.vaddr + match_offset_in_section
            distance = match_va - symbol_va
            print(f"Found function prologue at VA 0x{match_va:X} ({distance} bytes after symbol)")
            return match_va
    
    # If not found forward, search backward (limited to 0x400 bytes / 1KB)
    search_start = max(target_section.raw_ptr, symbol_file_offset - 0x400)
    
    best_match = None
    best_match_pos = -1
    
    for file_off in range(symbol_file_offset - 1, search_start - 1, -1):
        if pe[file_off:file_off + 9] == full_prologue:
            if file_off > best_match_pos:
                best_match_pos = file_off
                match_offset_in_section = file_off - target_section.raw_ptr
                best_match = image_base + target_section.vaddr + match_offset_in_section
    
    if best_match is not None:
        distance = symbol_va - best_match
        print(f"Found function prologue at VA 0x{best_match:X} ({distance} bytes before symbol)")
        return best_match
    
    print(f"WARNING: No function prologue found near symbol at 0x{symbol_va:X}")
    return None


def align_up(x: int, a: int) -> int:
    return (x + (a - 1)) & ~(a - 1)


def to_elf(pe: bytes, custom_entry: int | None = None) -> bytes:
    image_base, ep_rva, entry, size_of_headers, sections = parse_pe_sections(pe)

    if custom_entry is not None:
        entry = custom_entry
        print(f"Using custom entry point: 0x{entry:X}")
    else:
        print(f"Using PE entry point: 0x{entry:X}")

    # PE sections with no raw bytes are still loadable image memory.  NativeAOT
    # uses one such section ("hydrated") for runtime-rehydrated metadata.  The
    # QEMU image loader maps PT_LOAD memory and zero-fills it; omitting this
    # section leaves the metadata address absent and faults before RhpNewArray.
    # Keep the PE section order and emit a fileless PT_LOAD for zero-raw
    # sections rather than fabricating metadata contents here.
    load_secs = [s for s in sections if s.raw_size > 0 or s.vsize > 0]
    if not load_secs:
        raise SystemExit("No loadable sections with raw data")

    e_ehsize = 64
    e_phentsize = 56
    if image_base % 0x1000 != 0:
        raise SystemExit(f"PE image base is not page aligned: 0x{image_base:X}")

    e_phnum = len(load_secs) + 1
    if e_phnum > 0xFFFF:
        raise SystemExit("Too many PE sections for an ELF program-header table")

    phoff = e_ehsize
    elf_headers_size = e_ehsize + e_phentsize * e_phnum
    cur_off = align_up(elf_headers_size, 0x1000)

    # The fixed-base loader maps the first PT_LOAD at the PE image base. Keep
    # the actual PE DOS/NT/section headers there: the locked Windows
    # CoffNativeCodeManager reads IMAGE_NT_HEADERS and its exception data
    # directory from this address during RhRegisterOSModule.
    header_file_off = cur_off
    header_file_end = header_file_off + size_of_headers
    if header_file_end < header_file_off:
        raise SystemExit("PE header file offset overflows")
    cur_off = align_up(header_file_end, 0x1000)
    phdrs = [(1, 4, header_file_off, image_base, image_base, size_of_headers, 0x1000, 0x1000)]
    seg_blobs: list[tuple[int, bytes]] = [(header_file_off, pe[:size_of_headers])]

    for s in load_secs:
        data = pe[s.raw_ptr:s.raw_ptr + s.raw_size]
        pf_x = 1 if (s.characteristics & 0x20000000) else 0
        pf_w = 1 if (s.characteristics & 0x80000000) else 0
        pf_r = 1 if (s.characteristics & 0x40000000) else 1
        p_flags = (pf_x * 1) | (pf_w * 2) | (pf_r * 4)

        file_off = cur_off
        data_end = file_off + len(data)
        if data_end < file_off:
            raise SystemExit(f"PE section {s.name!r} file range overflows")
        cur_off = align_up(data_end, 0x1000)

        if image_base > 0xFFFFFFFFFFFFFFFF - s.vaddr:
            raise SystemExit(f"PE section {s.name!r} virtual address overflows 64-bit VA")
        vaddr = image_base + s.vaddr
        p_type = 1
        p_offset = file_off
        p_vaddr = vaddr
        p_paddr = vaddr
        p_filesz = len(data)
        p_memsz = max(p_filesz, s.vsize)
        p_align = 0x1000

        phdrs.append((p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align))
        seg_blobs.append((file_off, data))

    e_ident = bytearray(16)
    e_ident[0:4] = ELF_MAGIC
    e_ident[4] = 2  # 64-bit
    e_ident[5] = 1  # Little endian
    e_ident[6] = 1  # ELF version

    e_type = 2  # ET_EXEC
    e_machine = 0x3E  # x86-64
    e_version = 1
    e_entry = entry
    e_phoff = phoff
    e_shoff = 0
    e_flags = 0
    e_shentsize = 0
    e_shnum = 0
    e_shstrndx = 0

    elf_hdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        bytes(e_ident), e_type, e_machine, e_version,
        e_entry, e_phoff, e_shoff, e_flags, e_ehsize,
        e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx,
    )

    phdr_bytes = b"".join(struct.pack("<IIQQQQQQ", *p) for p in phdrs)

    out = bytearray(cur_off)
    out[0:len(elf_hdr)] = elf_hdr
    out[phoff:phoff + len(phdr_bytes)] = phdr_bytes

    for off, data in seg_blobs:
        out[off:off + len(data)] = data

    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description="Convert PE to ELF64 with prologue search")
    ap.add_argument("input", type=Path, help="Input PE executable")
    ap.add_argument("output", type=Path, help="Output ELF file")
    ap.add_argument("--entry", type=lambda x: int(x, 0), default=None,
                    help="Override entry point address")
    ap.add_argument("--map", type=Path, default=None,
                    help="Linker map file to search for symbol")
    ap.add_argument("--symbol", type=str, default="KMain",
                    help="Symbol name to use as entry point")
    args = ap.parse_args()

    pe = args.input.read_bytes()
    pe_image_base, _, pe_entry, _, sections = parse_pe_sections(pe)
    print(f"PE Image Base: 0x{pe_image_base:X}")
    print(f"PE Default Entry: 0x{pe_entry:X}")

    custom_entry = args.entry

    if args.map and custom_entry is None:
        symbol_addr = find_symbol_in_map(args.map, args.symbol)
        if symbol_addr:
            print(f"Found {args.symbol} in map file at 0x{symbol_addr:X}")
            
            # Search for the actual function prologue
            prologue_addr = find_function_prologue(pe, symbol_addr, pe_image_base, sections)
            if prologue_addr is not None:
                custom_entry = prologue_addr
            else:
                print(f"WARNING: Using symbol address as-is (may be wrong)")
                custom_entry = symbol_addr
        else:
            print(f"WARNING: Symbol '{args.symbol}' not found, using PE entry")

    elf = to_elf(pe, custom_entry)
    args.output.write_bytes(elf)

    if args.output.read_bytes()[:4] != ELF_MAGIC:
        raise SystemExit("Conversion failed: output is not ELF")

    print(f"Successfully created {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
