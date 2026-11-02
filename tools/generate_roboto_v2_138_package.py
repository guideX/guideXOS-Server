#!/usr/bin/env python3
"""Rebuild the approved Roboto v2.138 subsets from a locally pinned archive."""

from __future__ import annotations

import argparse
import hashlib
from io import BytesIO
import json
import re
import sys
import zipfile
from pathlib import Path

from fontTools import __version__ as FONTTOOLS_VERSION
from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTFont


EXPECTED_FONTTOOLS = "4.60.1"
ALLOWED_ARCHIVE_MEMBERS = {
    "Roboto-Regular.ttf",
    "Roboto-Bold.ttf",
    "Roboto-Italic.ttf",
    "LICENSE",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_ranges(values: list[str]) -> set[int]:
    result: set[int] = set()
    for value in values:
        match = re.fullmatch(r"U\+([0-9A-Fa-f]{4,6})(?:-U\+([0-9A-Fa-f]{4,6}))?", value)
        if not match:
            raise ValueError(f"invalid Unicode range in manifest: {value!r}")
        start = int(match.group(1), 16)
        end = int(match.group(2), 16) if match.group(2) else start
        if end < start or end > 0x10FFFF:
            raise ValueError(f"invalid Unicode range bounds: {value!r}")
        result.update(range(start, end + 1))
    return result


def safe_archive_index(archive: zipfile.ZipFile) -> dict[str, zipfile.ZipInfo]:
    index: dict[str, zipfile.ZipInfo] = {}
    casefolded: set[str] = set()
    for info in archive.infolist():
        name = info.filename
        normalized = name.replace("\\", "/")
        parts = normalized.rstrip("/").split("/")
        if (
            not name
            or normalized.startswith("/")
            or "\\" in name
            or any(part in ("", ".", "..") for part in parts)
        ):
            raise ValueError(f"unsafe upstream archive member: {name!r}")
        key = normalized.casefold()
        if key in casefolded:
            raise ValueError(f"duplicate case-insensitive upstream member: {name!r}")
        casefolded.add(key)
        if name in ALLOWED_ARCHIVE_MEMBERS:
            if "/" in name or info.is_dir():
                raise ValueError(f"selected upstream member is not a root file: {name!r}")
            index[name] = info
    missing = ALLOWED_ARCHIVE_MEMBERS.difference(index)
    if missing:
        raise ValueError(f"pinned archive is missing expected members: {sorted(missing)}")
    return index


def name_value(font: TTFont, name_id: int) -> str:
    values = sorted(
        {
            record.toUnicode()
            for record in font["name"].names
            if record.nameID == name_id
        }
    )
    return values[0] if values else ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    if FONTTOOLS_VERSION != EXPECTED_FONTTOOLS:
        raise ValueError(
            f"fontTools {EXPECTED_FONTTOOLS} is required; found {FONTTOOLS_VERSION}"
        )
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    archive_bytes = args.archive.read_bytes()
    upstream = manifest["upstream"]
    if len(archive_bytes) != int(upstream["archiveBytes"]):
        raise ValueError("upstream archive size differs from the pinned manifest")
    if sha256(archive_bytes) != upstream["archiveSha256"]:
        raise ValueError("upstream archive SHA-256 differs from the pinned manifest")

    output_dir = args.output_dir.resolve()
    if output_dir.exists():
        if output_dir.is_symlink() or any(output_dir.iterdir()):
            raise ValueError(f"output directory must be empty and not a symlink: {output_dir}")
    else:
        output_dir.mkdir(parents=True)

    unicode_set = parse_ranges(manifest["subset"]["ranges"])
    if len(unicode_set) != int(manifest["subset"]["unicodeMappings"]):
        raise ValueError("Unicode range list does not match the approved mapping count")

    with zipfile.ZipFile(args.archive, "r") as archive:
        if archive.testzip() is not None:
            raise ValueError("upstream ZIP CRC validation failed")
        entries = safe_archive_index(archive)
        license_data = archive.read(entries["LICENSE"])
        if (
            len(license_data) != int(upstream["licenseBytes"])
            or sha256(license_data) != upstream["licenseSha256"]
        ):
            raise ValueError("embedded upstream license differs from the pinned manifest")

        outputs = []
        for spec in manifest["fonts"]:
            output_name = spec["fileName"]
            if Path(output_name).name != output_name or "/" in output_name or "\\" in output_name:
                raise ValueError(f"unsafe approved output filename: {output_name!r}")
            source_name = spec["sourceMember"]
            if source_name not in ALLOWED_ARCHIVE_MEMBERS:
                raise ValueError(f"unapproved source member in manifest: {source_name!r}")
            source_data = archive.read(entries[source_name])
            if (
                len(source_data) != int(spec["sourceBytes"])
                or sha256(source_data) != spec["sourceSha256"]
            ):
                raise ValueError(f"upstream source font hash mismatch: {source_name}")

            font = TTFont(BytesIO(source_data), recalcBBoxes=False, recalcTimestamp=False)
            source_created = font["head"].created
            source_modified = font["head"].modified
            if "glyf" not in font or "CFF " in font or "CFF2" in font or "fvar" in font:
                raise ValueError(f"source font uses an unsupported outline or variant: {source_name}")
            for name_id, expected in (
                (0, spec["copyright"]),
                (1, spec["family"]),
                (2, spec["style"]),
                (5, spec["sourceVersion"]),
                (9, spec["designer"]),
                (13, spec["embeddedLicense"]),
                (14, spec["embeddedLicenseUrl"]),
            ):
                if name_value(font, name_id) != expected:
                    raise ValueError(f"source font metadata mismatch for {source_name}, name ID {name_id}")

            options = Options()
            options.name_IDs = ["*"]
            options.name_languages = ["*"]
            options.retain_gids = False
            subsetter = Subsetter(options=options)
            subsetter.populate(unicodes=unicode_set)
            subsetter.subset(font)
            font["name"].setName(
                "Unicode subset by guideX for guideXOS; see accompanying NOTICE.txt.",
                10,
                3,
                1,
                0x409,
            )
            font["head"].created = source_created
            font["head"].modified = source_modified
            font.recalcTimestamp = False
            destination = output_dir / output_name
            font.save(str(destination), reorderTables=True)
            output_data = destination.read_bytes()
            output_font = TTFont(
                str(destination), recalcBBoxes=False, recalcTimestamp=False, lazy=False
            )
            output_cmap = set().union(
                *(
                    set(table.cmap)
                    for table in output_font["cmap"].tables
                    if table.isUnicode()
                )
            )
            if (
                output_cmap != unicode_set
                or "glyf" not in output_font
                or "CFF " in output_font
                or "CFF2" in output_font
                or "fvar" in output_font
                or name_value(output_font, 10)
                != "Unicode subset by guideX for guideXOS; see accompanying NOTICE.txt."
            ):
                raise ValueError(f"generated font failed subset/format checks: {output_name}")
            if (
                len(output_data) != int(spec["bytes"])
                or sha256(output_data) != spec["sha256"]
            ):
                raise ValueError(
                    f"generated output differs from approved asset: {output_name}; "
                    f"got {len(output_data)} bytes sha256={sha256(output_data)}"
                )
            outputs.append(
                {
                    "fileName": output_name,
                    "bytes": len(output_data),
                    "sha256": sha256(output_data),
                }
            )

    print(json.dumps({"fontTools": FONTTOOLS_VERSION, "fonts": outputs}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"font package regeneration failed: {error}", file=sys.stderr)
        raise SystemExit(1)
