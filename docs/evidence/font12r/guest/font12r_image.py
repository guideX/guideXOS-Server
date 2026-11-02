import argparse
import hashlib
import json
from pathlib import Path

from pyfatfs.PyFatFS import PyFatFS


parser = argparse.ArgumentParser()
parser.add_argument("mode", choices=("inspect", "install-kernel", "remove"))
parser.add_argument("image", type=Path)
parser.add_argument("value", nargs="?", help="kernel file for install-kernel or FAT path for remove")
args = parser.parse_args()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def preference(raw):
    values = {}
    for line in raw.decode("ascii", "replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip()
    path = bytes.fromhex(values.get("pathHex", "")).decode("utf-8", "replace")
    family = bytes.fromhex(values.get("familyHex", "")).decode("utf-8", "replace")
    style = bytes.fromhex(values.get("styleHex", "")).decode("utf-8", "replace")
    return {
        "format": values.get("format"),
        "sequence": int(values.get("sequence", "0"), 16),
        "kind": values.get("kind"),
        "pixelSize": int(values.get("pixelSize", "0"), 16),
        "family": family,
        "style": style,
        "path": path,
        "fileBytes": int(values.get("fileBytes", "0"), 16),
        "fingerprint": values.get("fingerprint"),
        "identity": values.get("identity"),
        "checksum": values.get("checksum"),
        "recordBytes": len(raw),
        "sha256": sha256(raw),
    }


image = args.image.resolve()
result = {"image": str(image), "volume": "raw FAT32; PIIX IDE primary master"}

if args.mode == "install-kernel":
    if not args.value:
        parser.error("install-kernel requires the kernel ELF path")
    kernel_path = Path(args.value).resolve()
    kernel = kernel_path.read_bytes()
    with PyFatFS(str(image), preserve_case=True) as fs:
        if fs.exists("/kernel.elf"):
            fs.remove("/kernel.elf")
        with fs.openbin("/kernel.elf", "w") as stream:
            stream.write(kernel)
    with PyFatFS(str(image), preserve_case=True) as fs:
        with fs.openbin("/kernel.elf", "r") as stream:
            stored = stream.read()
    result.update({
        "mode": args.mode,
        "sourceKernel": str(kernel_path),
        "sourceBytes": len(kernel),
        "sourceSha256": sha256(kernel),
        "storedBytes": len(stored),
        "storedSha256": sha256(stored),
        "verified": kernel == stored,
    })
elif args.mode == "remove":
    if not args.value:
        parser.error("remove requires a FAT path")
    with PyFatFS(str(image), preserve_case=True) as fs:
        existed = fs.exists(args.value)
        if existed:
            fs.remove(args.value)
        result.update({"mode": args.mode, "path": args.value, "existed": existed, "remains": fs.exists(args.value)})
else:
    rows = []
    with PyFatFS(str(image), preserve_case=True) as fs:
        for path in (
            "/kernel.elf",
            "/system/fonts/Roboto-Regular.ttf",
            "/system/fonts/Roboto-Bold.ttf",
            "/system/fonts/Roboto-Italic.ttf",
            "/FONTSEL0.CFG",
            "/fontsel1.cfg",
        ):
            row = {"path": path, "exists": fs.exists(path)}
            if row["exists"]:
                with fs.openbin(path, "r") as stream:
                    data = stream.read()
                row.update({"bytes": len(data), "sha256": sha256(data)})
                if path.lower().endswith(".cfg"):
                    row["preference"] = preference(data)
            rows.append(row)
    result.update({"mode": args.mode, "entries": rows})

print(json.dumps(result, indent=2))
