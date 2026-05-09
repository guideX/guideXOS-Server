#!/usr/bin/env python3
"""
gxbuild - guideXOS universal application build tool

Builds an application for multiple CPU architectures and packages the results
into a .gxapp universal application container.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional

SUPPORTED_ARCHITECTURES = [
    "x86",
    "amd64",
    "arm",
    "arm64",
    "ia64",
    "loongarch64",
    "mips64",
    "ppc64",
    "sparc",
    "sparc64",
]

ARCHITECTURE_IDS = {
    "unknown": 0,
    "x86": 1,
    "amd64": 2,
    "arm": 3,
    "arm64": 4,
    "ia64": 5,
    "loongarch64": 6,
    "mips64": 7,
    "ppc64": 8,
    "sparc": 9,
    "sparc64": 10,
}

GXAPP_MAGIC = b"GXAPP\r\n\x1a"
GXAPP_FORMAT_VERSION = 1
GXAPP_FLAG_REQUIRED = 1
ENTRY_METADATA = 1
ENTRY_BINARY = 2


@dataclass
class BuildTarget:
    arch: str
    entry_point: str
    binary_path: Path


@dataclass
class BuildConfig:
    name: str
    version: str
    required_guidexos: str
    output: Path
    source: Path
    build_root: Path
    targets: List[str]
    build_command: Optional[str]
    binary_template: str
    entry_point: str
    skip_build: bool
    keep_build_dir: bool


def parse_targets(value: str) -> List[str]:
    if value.strip().lower() == "all":
        return list(SUPPORTED_ARCHITECTURES)

    targets = [item.strip().lower() for item in value.split(",") if item.strip()]
    invalid = [arch for arch in targets if arch not in SUPPORTED_ARCHITECTURES]
    if invalid:
        raise ValueError(f"unsupported target(s): {', '.join(invalid)}")
    return targets


def load_json_config(path: Path) -> Dict[str, object]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def first_value(args: argparse.Namespace, config: Dict[str, object], name: str, default=None):
    value = getattr(args, name)
    if value is not None:
        return value
    return config.get(name.replace("_", "-"), config.get(name, default))


def create_config(args: argparse.Namespace) -> BuildConfig:
    config: Dict[str, object] = {}
    if args.config:
        config = load_json_config(Path(args.config))

    name = first_value(args, config, "name")
    version = first_value(args, config, "version")
    required_guidexos = first_value(args, config, "required_guidexos")

    if not name:
        raise ValueError("application name is required")
    if not version:
        raise ValueError("application version is required")
    if not required_guidexos:
        raise ValueError("required guideXOS version is required")

    output = Path(first_value(args, config, "output", f"{name}.gxapp"))
    source = Path(first_value(args, config, "source", "."))
    build_root = Path(first_value(args, config, "build_root", "build/gxbuild"))
    targets_value = first_value(args, config, "targets", "all")
    if isinstance(targets_value, list):
        targets = parse_targets(",".join(str(item) for item in targets_value))
    else:
        targets = parse_targets(str(targets_value))

    return BuildConfig(
        name=str(name),
        version=str(version),
        required_guidexos=str(required_guidexos),
        output=output,
        source=source,
        build_root=build_root,
        targets=targets,
        build_command=first_value(args, config, "build_command"),
        binary_template=str(first_value(args, config, "binary_template", "{build_dir}/app.bin")),
        entry_point=str(first_value(args, config, "entry_point", "_start")),
        skip_build=bool(first_value(args, config, "skip_build", False)),
        keep_build_dir=bool(first_value(args, config, "keep_build_dir", False)),
    )


def format_template(template: str, arch: str, source: Path, build_dir: Path, output: Path) -> str:
    return template.format(
        arch=arch,
        source=str(source),
        build_dir=str(build_dir),
        output=str(output),
    )


def split_command(command: str) -> List[str]:
    if os.name == "nt":
        return shlex.split(command, posix=False)
    return shlex.split(command)


def run_build(config: BuildConfig, arch: str, build_dir: Path, binary_path: Path) -> None:
    if config.skip_build:
        return
    if not config.build_command:
        raise ValueError("--build-command is required unless --skip-build is used")

    build_dir.mkdir(parents=True, exist_ok=True)
    command = format_template(config.build_command, arch, config.source, build_dir, binary_path)
    print(f"[gxbuild] building {arch}: {command}", flush=True)
    subprocess.run(split_command(command), check=True)


def collect_binary(config: BuildConfig, arch: str) -> BuildTarget:
    build_dir = config.build_root / arch
    default_output = build_dir / "app.bin"
    binary_path = Path(format_template(config.binary_template, arch, config.source, build_dir, default_output))
    run_build(config, arch, build_dir, binary_path)

    if not binary_path.is_file():
        raise FileNotFoundError(f"missing binary for {arch}: {binary_path}")

    return BuildTarget(arch=arch, entry_point=config.entry_point, binary_path=binary_path)


def create_metadata(config: BuildConfig, targets: Iterable[BuildTarget]) -> Dict[str, object]:
    binaries = []
    for target in targets:
        binaries.append(
            {
                "architecture": target.arch,
                "path": f"bin/{target.arch}/app.bin",
                "entryPoint": target.entry_point,
            }
        )

    return {
        "format": "gxapp",
        "formatVersion": GXAPP_FORMAT_VERSION,
        "applicationName": config.name,
        "version": config.version,
        "requiredGuideXOSVersion": config.required_guidexos,
        "binaries": binaries,
    }


def add_entry(out: bytearray, kind: int, arch: str, path: str, data: bytes) -> None:
    path_bytes = path.encode("utf-8")
    if len(path_bytes) > 0xFFFF:
        raise ValueError(f"entry path is too long: {path}")

    out += struct.pack("<IIHQ", kind, ARCHITECTURE_IDS[arch], len(path_bytes), len(data))
    out += path_bytes
    out += data


def write_gxapp(config: BuildConfig, targets: List[BuildTarget], metadata: Dict[str, object]) -> None:
    entries: List[tuple[int, str, str, bytes]] = []
    metadata_bytes = json.dumps(metadata, indent=2).encode("utf-8") + b"\n"
    entries.append((ENTRY_METADATA, "unknown", "metadata.json", metadata_bytes))

    for target in targets:
        with target.binary_path.open("rb") as f:
            binary = f.read()
        entries.append((ENTRY_BINARY, target.arch, f"bin/{target.arch}/app.bin", binary))

    package = bytearray()
    package += GXAPP_MAGIC
    package += struct.pack("<III", GXAPP_FORMAT_VERSION, GXAPP_FLAG_REQUIRED, len(entries))
    for kind, arch, path, data in entries:
        add_entry(package, kind, arch, path, data)

    config.output.parent.mkdir(parents=True, exist_ok=True)
    with config.output.open("wb") as f:
        f.write(package)

    metadata_path = config.build_root / "metadata.json"
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    with metadata_path.open("wb") as f:
        f.write(metadata_bytes)


def clean_build_root(config: BuildConfig) -> None:
    if config.keep_build_dir or config.skip_build:
        return
    if config.build_root.exists():
        shutil.rmtree(config.build_root)


def build_package(config: BuildConfig) -> None:
    print(f"[gxbuild] application: {config.name} {config.version}")
    print(f"[gxbuild] targets: {', '.join(config.targets)}")

    targets = [collect_binary(config, arch) for arch in config.targets]
    metadata = create_metadata(config, targets)
    write_gxapp(config, targets, metadata)

    print(f"[gxbuild] wrote {config.output}")
    print(f"[gxbuild] metadata: {config.build_root / 'metadata.json'}")


def create_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gxbuild",
        description="Build multi-architecture guideXOS applications and package them as .gxapp files.",
    )
    parser.add_argument("--config", help="JSON configuration file for CI/reproducible builds")
    parser.add_argument("--name", help="application name")
    parser.add_argument("--version", help="application version")
    parser.add_argument("--required-guidexos", dest="required_guidexos", help="minimum required guideXOS version")
    parser.add_argument("--source", help="application source directory")
    parser.add_argument("--output", "-o", help="output .gxapp path")
    parser.add_argument("--build-root", dest="build_root", help="intermediate build directory")
    parser.add_argument("--targets", default=None, help="comma-separated target list or 'all'")
    parser.add_argument("--build-command", dest="build_command", help="command template used for each target")
    parser.add_argument("--binary-template", dest="binary_template", help="binary path template after each target build")
    parser.add_argument("--entry-point", dest="entry_point", help="entry point recorded for each architecture")
    parser.add_argument("--skip-build", action="store_true", default=None, help="package prebuilt binaries only")
    parser.add_argument("--keep-build-dir", action="store_true", default=None, help="keep intermediate build files")
    parser.add_argument("--list-targets", action="store_true", help="print supported targets and exit")
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = create_arg_parser()
    args = parser.parse_args(argv)

    if args.list_targets:
        print("\n".join(SUPPORTED_ARCHITECTURES))
        return 0

    try:
        config = create_config(args)
        build_package(config)
        return 0
    except subprocess.CalledProcessError as ex:
        print(f"gxbuild: build command failed with exit code {ex.returncode}", file=sys.stderr)
        return ex.returncode or 1
    except Exception as ex:
        print(f"gxbuild: error: {ex}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
