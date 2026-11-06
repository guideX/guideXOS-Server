#!/usr/bin/env python3
"""Negative tests for malformed IA-64 IVT geometry, labels, and boundaries."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "scripts" / "validate-ia64-elf.py"
sys.path.insert(0, str(VALIDATOR.parent))
import importlib.util

spec = importlib.util.spec_from_file_location("validate_ia64_elf", VALIDATOR)
validator = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(validator)


def run_validator(elf_path: Path, repo_root: Path, profile: str) -> str:
    result = subprocess.run(
        [sys.executable, str(VALIDATOR), "--elf", str(elf_path),
         "--repo-root", str(repo_root), "--profile", profile],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    if result.returncode == 0:
        raise AssertionError(f"malformed IVT unexpectedly passed:\n{result.stdout}\n{result.stderr}")
    return result.stdout + result.stderr


def tamper_section_size(source: Path, dest: Path) -> None:
    elf = validator.Elf64(source)
    section = elf.section(".ivt")
    assert section is not None
    data = bytearray(elf.data)
    struct.pack_into("<Q", data, elf.shoff + int(section["index"]) * elf.shentsize + 32,
                     int(section["size"]) - 1)
    dest.write_bytes(data)


def tamper_handler_boundary(source: Path, dest: Path) -> None:
    elf = validator.Elf64(source)
    target = "ivt_vhpt_translation_end"
    data = bytearray(elf.data)
    changed = False
    for section in elf.sections:
        if section["type"] not in (validator.SHT_SYMTAB, validator.SHT_DYNSYM):
            continue
        string_section = elf.sections[int(section["link"])]
        strings = elf._slice(int(string_section["offset"]), int(string_section["size"]), "strings")
        entsize = int(section["entsize"] or 24)
        for sym_off in range(int(section["offset"]), int(section["offset"]) + int(section["size"]), entsize):
            nameoff, _, _, _, value, _ = struct.unpack(
                "<IBBHQQ", elf._slice(sym_off, entsize, "symbol"))
            end = strings.find(b"\0", nameoff)
            name = strings[nameoff:end if end >= 0 else len(strings)].decode("utf-8", "replace")
            if name == target:
                struct.pack_into("<Q", data, sym_off + 8, value + 1)
                changed = True
                break
        if changed:
            break
    if not changed:
        raise AssertionError(f"missing {target} in test ELF")
    dest.write_bytes(data)


def tamper_source_offset(source: Path, temp_root: Path) -> Path:
    boot = source.read_text(encoding="utf-8")
    pattern = r"(?m)^(IVT_ENTRY_HALT\s+ivt_itlb_miss,\s*)0x0400"
    altered, count = re.subn(pattern, r"\g<1>0x0408", boot, count=1)
    if count != 1:
        raise AssertionError("could not alter the instruction-TLB vector offset")
    bad_repo = temp_root / "bad-source-repo"
    bad_boot = bad_repo / "kernel" / "arch" / "ia64" / "boot.s"
    bad_boot.parent.mkdir(parents=True)
    bad_boot.write_text(altered, encoding="utf-8")
    return bad_repo


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--profile", choices=("runtime", "compile-only", "early-exec"), required=True)
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"test ELF does not exist: {args.elf}")

    with tempfile.TemporaryDirectory(prefix="ia64-ivt-negative-") as temp_name:
        temp_root = Path(temp_name)
        bad_size = temp_root / "bad-ivt-size.elf"
        tamper_section_size(args.elf, bad_size)
        size_output = run_validator(bad_size, ROOT, args.profile)
        if "IVT_LAYOUT=FAIL" not in size_output:
            raise AssertionError(f"bad section size did not fail IVT_LAYOUT:\n{size_output}")

        bad_boundary = temp_root / "bad-handler-boundary.elf"
        tamper_handler_boundary(args.elf, bad_boundary)
        boundary_output = run_validator(bad_boundary, ROOT, args.profile)
        if "IVT_VECTOR_LAYOUT=FAIL" not in boundary_output:
            raise AssertionError(f"bad handler boundary did not fail IVT_VECTOR_LAYOUT:\n{boundary_output}")

        bad_repo = tamper_source_offset(ROOT / "kernel" / "arch" / "ia64" / "boot.s", temp_root)
        source_output = run_validator(args.elf, bad_repo, args.profile)
        if "IVT_VECTOR_LAYOUT=FAIL" not in source_output:
            raise AssertionError(f"bad source offset did not fail IVT_VECTOR_LAYOUT:\n{source_output}")

    print("IA64_IVT_NEGATIVE_TESTS=PASS count=3 bad-size,bad-handler-boundary,bad-source-offset")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
