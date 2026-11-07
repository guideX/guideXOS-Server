#!/usr/bin/env python3
"""Run only hash-bound IA-64 Ski smoke or explicitly authorized kernel ELFs."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from typing import Any

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SKI_PREFIX = Path("/home/guidex/opt/ski-dfc2902")
DEFAULT_LOADER_RECORD = ROOT / "docs" / "ia64-3r1-ski-loader-record.json"
QUALIFIED_SKI_COMMIT = "dfc2902ea1423d9b32543a5daf8026213f2b37a1"
QUALIFIED_SKI_VERSION = "1.5.1"
MAX_PA_BITS = 63  # Ski's documented and built default is -palen 63.
PT_LOAD = 1
PF_X = 1
PF_W = 2
EM_IA_64 = 50
ET_EXEC = 2
SMOKE_MARKER = "SKI_SYSTEM_MODE_SMOKE_PASS"
FAULT_MARKERS = (
    "exception", "fault @", " fault\n", "unallocated page", "illegal address",
    "privileged operation fault", "not an elf", "kernel not for this processor",
)


class GateError(ValueError):
    pass


def load_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_elf_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load independent ELF validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ELF_VALIDATOR = load_validator()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def ranges_overlap(left: tuple[int, int], right: tuple[int, int]) -> bool:
    return left[0] < right[1] and right[0] < left[1]


@contextmanager
def verified_snapshot(source: Path, expected_sha256: str):
    """Run from a hash-checked private copy to close the validation/load race."""
    with tempfile.TemporaryDirectory(prefix="guidexos-ia64-ski-elf-") as temp:
        snapshot = Path(temp) / "selected.elf"
        shutil.copyfile(source, snapshot)
        actual = sha256(snapshot)
        if actual != expected_sha256:
            raise GateError(f"artifact changed while taking execution snapshot: actual={actual}")
        snapshot.chmod(0o444)
        yield snapshot


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"{label} is missing or invalid: {exc}") from exc
    if not isinstance(value, dict):
        raise GateError(f"{label} must be a JSON object")
    return value


def check_manifest(artifact: Path, manifest_path: Path, profile: str,
                   expected_sha256: str, schema: str = "guidexos-ia64-artifact-v1") -> dict[str, Any]:
    manifest = read_json(manifest_path, "artifact manifest")
    actual = sha256(artifact)
    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha256) or actual != expected_sha256:
        raise GateError(f"artifact SHA-256 mismatch: actual={actual}")
    if manifest.get("schema") != schema:
        raise GateError(f"artifact manifest schema must be {schema}")
    if manifest.get("profile") != profile:
        raise GateError(f"declared profile {profile!r} does not match the manifest")
    if manifest.get("sha256") != actual:
        raise GateError("manifest SHA-256 does not match the selected ELF")
    elf_name = manifest.get("elf")
    if not isinstance(elf_name, str) or not elf_name:
        raise GateError("manifest does not declare an ELF path")
    if artifact.resolve() != (manifest_path.parent / elf_name).resolve():
        raise GateError("the manifest path does not identify the selected ELF")
    return manifest


def validate_direct_system_elf(artifact: Path) -> list[dict[str, int]]:
    """Check exactly the ranges Ski's system ELF loader will copy by p_vaddr."""
    try:
        elf = ELF_VALIDATOR.Elf64(artifact)
    except (OSError, ELF_VALIDATOR.InvalidElf) as exc:
        raise GateError(f"invalid IA-64 ELF: {exc}") from exc
    if elf.machine != EM_IA_64 or elf.type != ET_EXEC:
        raise GateError("direct Ski loading requires ELF64 EM_IA_64 ET_EXEC")
    if elf.data[5] != 1:
        raise GateError("direct Ski loading requires little-endian ELF")

    loads: list[dict[str, int]] = []
    pa_limit = 1 << MAX_PA_BITS
    for index, header in enumerate(elf.programs):
        if header["type"] != PT_LOAD:
            continue
        p_offset = header["offset"]
        vaddr = header["vaddr"]
        paddr = header["paddr"]
        filesz = header["filesz"]
        memsz = header["memsz"]
        align = header["align"]
        flags = header["flags"]
        if memsz == 0 or filesz > memsz:
            raise GateError(f"PT_LOAD[{index}] has invalid filesz/memsz")
        if p_offset + filesz > len(elf.data):
            raise GateError(f"PT_LOAD[{index}] file bytes exceed the ELF image")
        if vaddr != paddr:
            raise GateError(f"PT_LOAD[{index}] p_vaddr/p_paddr differ; Ski loads by p_vaddr")
        if vaddr >= pa_limit or vaddr + memsz > pa_limit:
            raise GateError(f"PT_LOAD[{index}] is outside Ski's -palen 63 address range")
        if align > 1 and (align & (align - 1) or vaddr % align != p_offset % align):
            raise GateError(f"PT_LOAD[{index}] has invalid ELF alignment")
        if flags & PF_W and flags & PF_X:
            raise GateError(f"PT_LOAD[{index}] is writable and executable")
        loads.append({"start": vaddr, "end": vaddr + memsz,
                      "filesz": filesz, "memsz": memsz,
                      "flags": flags, "index": index})

    if not loads:
        raise GateError("ELF has no PT_LOAD segments")
    loads.sort(key=lambda segment: (segment["start"], segment["end"]))
    for previous, current in zip(loads, loads[1:]):
        if ranges_overlap((previous["start"], previous["end"]),
                          (current["start"], current["end"])):
            raise GateError(f"PT_LOAD ranges overlap: [{previous['start']:#x},{previous['end']:#x}) "
                            f"and [{current['start']:#x},{current['end']:#x})")
    if not any(segment["flags"] & PF_X and
               segment["start"] <= elf.entry < segment["end"] for segment in loads):
        raise GateError("ELF entry is not inside an executable PT_LOAD segment")
    for segment in loads:
        print(f"PT_LOAD[{segment['index']}]=[0x{segment['start']:x},0x{segment['end']:x}) "
              f"filesz=0x{segment['filesz']:x} memsz=0x{segment['memsz']:x} "
              f"flags=0x{segment['flags']:x}")
    return loads


def verify_ski(prefix: Path, expected_hash: str) -> Path:
    bindir = prefix / "bin"
    paths = {name: bindir / name for name in ("ski", "bski", "bskinc")}
    for name, path in paths.items():
        if not path.exists() or not path.is_file():
            raise GateError(f"verified Ski executable is missing: {path}")
    resolved = {name: path.resolve() for name, path in paths.items()}
    if len(set(resolved.values())) != 1:
        raise GateError("ski, bski, and bskinc do not resolve to the same installed simulator")
    simulator = resolved["ski"]
    actual = sha256(simulator)
    if actual != expected_hash:
        raise GateError(f"installed Ski SHA-256 mismatch: actual={actual}")
    if not os.access(paths["bskinc"], os.X_OK):
        raise GateError("installed bskinc is not executable")
    return paths["bskinc"]


def verify_loader_record(path: Path, artifact_hash: str, profile: str,
                         simulator_hash: str) -> dict[str, Any]:
    record = read_json(path, "loader compatibility record")
    if record.get("schema") != "guidexos-ia64-ski-loader-v1":
        raise GateError("loader compatibility record has an unsupported schema")
    if record.get("outcome") != "SKI_LOADER_READY":
        raise GateError("loader compatibility record is not SKI_LOADER_READY")
    if record.get("loader_kind") != "ski-direct-system-elf":
        raise GateError("loader record does not qualify Ski's direct system-mode ELF path")
    if record.get("kernel_sha256") != artifact_hash or record.get("kernel_profile") != profile:
        raise GateError("loader evidence does not bind this kernel hash and profile")
    if record.get("ski_sha256") != simulator_hash:
        raise GateError("loader evidence does not bind this Ski executable hash")
    ski_source = record.get("ski_source")
    if not isinstance(ski_source, dict) or \
            ski_source.get("commit") != QUALIFIED_SKI_COMMIT or \
            ski_source.get("source_tree_clean") is not True or \
            record.get("ski_version") != QUALIFIED_SKI_VERSION:
        raise GateError("loader evidence does not bind the pinned clean Ski source/version")
    if record.get("system_mode_smoke") != "PASS" or record.get("single_cpu_contract") != "SOURCE_ENFORCED_ONE_CPU":
        raise GateError("loader evidence lacks a passing system-mode smoke or one-CPU contract")
    direct_loader = record.get("direct_loader")
    if not isinstance(direct_loader, dict) or \
            direct_loader.get("uses_system_mode_option") != "-forcesystem" or \
            direct_loader.get("candidate_paddr_equals_vaddr") is not True or \
            direct_loader.get("guest_resident_loader_ranges") != []:
        raise GateError("loader record lacks the qualified direct system-ELF contract")
    if record.get("kernel_execution_authorized") is not False:
        raise GateError("loader record must remain separate from guest-execution authorization")
    return record


def _kill_process_group(process: subprocess.Popen[bytes], grace_seconds: float = 1.0) -> None:
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=grace_seconds)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    # Kill helper children too, including xterm if an unexpected build opens one.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    time.sleep(0.05)
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def run_simulator(command: list[str], timeout: float, capture_dir: Path,
                  label: str, success_marker: str | None = None,
                  artifact_hash: str | None = None) -> int:
    if timeout <= 0 or timeout > 300:
        raise GateError("timeout must be greater than zero and no more than 300 seconds")
    if capture_dir.exists():
        if not capture_dir.is_dir() or any(capture_dir.iterdir()):
            raise GateError(f"capture directory must be absent or empty: {capture_dir}")
    else:
        capture_dir.mkdir(parents=True)

    stdout_path = capture_dir / "stdout.log"
    stderr_path = capture_dir / "stderr.log"
    counts_path = capture_dir / "instruction-counts.txt"
    command = command.copy()
    command[1:1] = ["-stats", "-icnt", str(counts_path)]
    started = time.monotonic()
    timed_out = False
    with stdout_path.open("xb") as stdout_file, stderr_path.open("xb") as stderr_file:
        process = subprocess.Popen(command, stdout=stdout_file, stderr=stderr_file,
                                   start_new_session=True, close_fds=True)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
        finally:
            _kill_process_group(process)
    elapsed = time.monotonic() - started
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    combined = (stdout_text + "\n" + stderr_text).lower()
    return_code = process.returncode

    if timed_out:
        classification = "TIMEOUT"
    elif any(marker in combined for marker in FAULT_MARKERS):
        classification = "GUEST_FAULT"
    elif return_code != 0:
        classification = "SIMULATOR_ERROR"
    elif success_marker and success_marker not in stdout_text:
        classification = "SMOKE_MARKER_MISSING"
    elif success_marker:
        classification = "SMOKE_PASS"
    elif "kernel exited with status 0" in combined or "processor 0 exited with status 0" in combined:
        classification = "GUEST_EXIT_ZERO"
    else:
        classification = "SIMULATOR_EXIT_UNCLASSIFIED"

    record = {
        "label": label,
        "command": command,
        "artifact_sha256": artifact_hash,
        "classification": classification,
        "exit_code": return_code,
        "timed_out": timed_out,
        "elapsed_seconds": round(elapsed, 3),
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "instruction_counts": str(counts_path) if counts_path.is_file() else None,
    }
    (capture_dir / "run-result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"[{classification}] exit={return_code} elapsed={elapsed:.3f}s capture={capture_dir}")
    return 0 if classification in ("SMOKE_PASS", "GUEST_EXIT_ZERO") else 1


def require_linux() -> None:
    if not sys.platform.startswith("linux"):
        raise GateError("the verified Ski runner must run inside the Ubuntu WSL2 environment")


def setup_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)

    smoke = subparsers.add_parser("smoke", help="run the separately authorized Ski smoke ELF")
    smoke.add_argument("--artifact", type=Path, required=True)
    smoke.add_argument("--manifest", type=Path, required=True)
    smoke.add_argument("--expected-sha256", required=True)
    smoke.add_argument("--simulator-sha256", required=True)
    smoke.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    smoke.add_argument("--capture-dir", type=Path)
    smoke.add_argument("--timeout", type=float, default=30)
    smoke.add_argument("--mode", choices=("system",), default="system")

    kernel = subparsers.add_parser("kernel", help="run a kernel only after separate authorization")
    kernel.add_argument("--artifact", type=Path, required=True)
    kernel.add_argument("--manifest", type=Path, required=True)
    kernel.add_argument("--profile", choices=("runtime", "compile-only", "early-exec"), required=True)
    kernel.add_argument("--expected-sha256", required=True)
    kernel.add_argument("--loader-record", type=Path, default=DEFAULT_LOADER_RECORD)
    kernel.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    kernel.add_argument("--capture-dir", type=Path)
    kernel.add_argument("--timeout", type=float, default=60)
    kernel.add_argument("--mode", choices=("system",), default="system")
    kernel.add_argument("--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")
    kernel.add_argument("--authorize-kernel-guest-execution", action="store_true",
                        help="explicit authorization for the selected kernel hash")
    return parser


def run_smoke(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected smoke ELF does not exist: {args.artifact}")
    manifest = check_manifest(args.artifact, args.manifest, "system-mode-smoke",
                              args.expected_sha256, "guidexos-ia64-ski-smoke-v1")
    if manifest.get("guest_execution_authorized") != "smoke-only" or \
            manifest.get("privileged_instruction_test") != "rsm psr.i; mov r8=psr" or \
            manifest.get("console_marker") != SMOKE_MARKER or manifest.get("exit_ssc") != 66:
        raise GateError("smoke manifest does not declare the bounded privilege/console/exit contract")
    validate_direct_system_elf(args.artifact)
    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(prefix="guidexos-ia64-ski-smoke-"))
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator), "-forcesystem", str(snapshot)]
        return run_simulator(command, args.timeout, capture_dir, "system-mode-smoke",
                             SMOKE_MARKER, args.expected_sha256)


def run_kernel(args: argparse.Namespace) -> int:
    require_linux()
    if args.profile == "compile-only":
        raise GateError("compile-only artifacts are permanently non-executable through this runner")
    if not args.artifact.is_file():
        raise GateError(f"selected kernel ELF does not exist: {args.artifact}")
    manifest = check_manifest(args.artifact, args.manifest, args.profile, args.expected_sha256)
    if not args.authorize_kernel_guest_execution:
        raise GateError("kernel guest execution requires --authorize-kernel-guest-execution")

    simulator_hash = None
    try:
        simulator_hash = sha256((args.ski_prefix / "bin" / "ski").resolve(strict=True))
    except OSError as exc:
        raise GateError(f"installed Ski binary is unavailable: {exc}") from exc
    verify_loader_record(args.loader_record, manifest["sha256"], args.profile, simulator_hash)
    simulator = verify_ski(args.ski_prefix, simulator_hash)
    validate_direct_system_elf(args.artifact)

    if args.profile == "early-exec":
        if manifest.get("static_gates") != "PASS" or manifest.get("runtime_qualified") is not False or \
                manifest.get("scheduler_qualified") is not False or \
                manifest.get("interrupt_fp_preservation") is not False or \
                manifest.get("guest_execution_authorized") is not False:
            raise GateError("early-exec manifest or static gates are incomplete")
        validator_cmd = [sys.executable, str(ROOT / "scripts" / "validate-ia64-elf.py"),
                         "--elf", str(args.artifact), "--repo-root", str(ROOT),
                         "--profile", args.profile, "--manifest", str(args.manifest)]
        validation_env = os.environ.copy()
        validation_env["PYTHONDONTWRITEBYTECODE"] = "1"
        result = subprocess.run(validator_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("independent IA-64 ELF/profile validation failed")
        gate_cmd = [sys.executable, str(ROOT / "scripts" / "validate-ia64-early-exec.py"),
                    "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump]
        result = subprocess.run(gate_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("early-exec static source/disassembly gates failed")
    elif manifest.get("runtime_qualified") is not True or \
            manifest.get("scheduler_qualified") is not True or \
            manifest.get("interrupt_fp_preservation") is not True or \
            manifest.get("guest_execution_authorized") is not True:
        raise GateError("runtime profile lacks complete runtime and guest qualification records")

    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(prefix="guidexos-ia64-kernel-"))
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator), "-forcesystem", str(snapshot)]
        return run_simulator(command, args.timeout, capture_dir,
                             "guideXOS-kernel", artifact_hash=args.expected_sha256)


def main(argv: list[str] | None = None) -> int:
    parser = setup_parser()
    args = parser.parse_args(argv)
    try:
        if args.mode != "system":
            raise GateError("only the verified Ski system mode is supported")
        if args.operation == "smoke":
            return run_smoke(args)
        return run_kernel(args)
    except GateError as exc:
        print(f"[RUN_BLOCKED] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
