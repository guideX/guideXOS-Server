#!/usr/bin/env python3
"""Provision a new, disposable FAT32 image for the guideXOS preview data disk.

This tool creates a new regular image file only. It refuses existing paths and
raw-device names; it never formats a discovered guest or host disk.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import sys


LABEL = "GXOSDATA"
MARKER_NAME = "GXOSDAT.ID"
MARKER_CONTENTS = b"guideXOS-user-data-v1\n"
MIN_SIZE_MIB = 64
MAX_SIZE_MIB = 2048


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="new .img file under E:\\CodexArtifacts")
    parser.add_argument("--size-mib", type=int, default=128,
                        help=f"image size ({MIN_SIZE_MIB}..{MAX_SIZE_MIB} MiB; default: 128)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    target = args.image.expanduser().absolute()
    if target.suffix.lower() != ".img":
        raise SystemExit("Refusing output without the .img suffix.")
    if "codexartifacts" not in {part.lower() for part in target.parts}:
        raise SystemExit("Output must be beneath a CodexArtifacts directory.")
    if str(target).startswith(("\\\\.\\", "\\\\?\\globalroot\\")):
        raise SystemExit("Raw-device and GlobalRoot paths are not accepted.")
    if not MIN_SIZE_MIB <= args.size_mib <= MAX_SIZE_MIB:
        raise SystemExit(f"--size-mib must be between {MIN_SIZE_MIB} and {MAX_SIZE_MIB}.")
    if not target.parent.is_dir():
        raise SystemExit(f"Output parent directory does not exist: {target.parent}")

    try:
        descriptor = os.open(target, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        raise SystemExit(f"Refusing to overwrite existing image: {target}")
    os.close(descriptor)

    try:
        from pyfatfs.PyFat import PyFat
        from pyfatfs.PyFatFS import PyFatFS

        filesystem = PyFat()
        filesystem.mkfs(
            str(target),
            fat_type=PyFat.FAT_TYPE_FAT32,
            size=args.size_mib * 1024 * 1024,
            sector_size=512,
            number_of_fats=2,
            label=LABEL,
            volume_id=int.from_bytes(os.urandom(4), "little"),
        )
        filesystem.close()

        fatfs = PyFatFS(str(target), preserve_case=False, read_only=False)
        fatfs.makedir("/APPDATA")
        with fatfs.openbin(f"/{MARKER_NAME}", "wb") as marker:
            marker.write(MARKER_CONTENTS)
        fatfs.close()

        image_size = target.stat().st_size
        if image_size != args.size_mib * 1024 * 1024:
            raise RuntimeError("The formatted image has an unexpected size.")
        image_digest = hashlib.sha256()
        with target.open("rb") as image:
            for chunk in iter(lambda: image.read(1024 * 1024), b""):
                image_digest.update(chunk)
        print(f"image={target}")
        print(f"size={image_size}")
        print(f"sha256={image_digest.hexdigest()}")
        print(f"filesystem=FAT32 label={LABEL} marker={MARKER_NAME} appData=APPDATA")
        return 0
    except Exception:
        # Only this invocation-created path may be removed on provisioning
        # failure; an existing image is never opened or modified.
        target.unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    sys.exit(main())
