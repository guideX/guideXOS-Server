#!/usr/bin/env python3
"""Run bounded guideXOS-vs-Regina comparisons over the portable RX11 corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CASES = SCRIPT_DIR / "cases"
DEFAULT_OUTPUT = ROOT / "tmp" / "rexx-rx11-differential"
MAX_OUTPUT_BYTES = 1_048_576
CASE_TIMEOUT_SECONDS = 5.0
PROBE_TIMEOUT_SECONDS = 3.0
BUILD_TIMEOUT_SECONDS = 120.0


def kill_owned_process_tree(process: subprocess.Popen[bytes]) -> None:
    """Stop only the direct child and descendants created for this invocation."""
    if process.poll() is not None:
        return
    if os.name == "nt":
        taskkill = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "taskkill.exe"
        if taskkill.is_file():
            try:
                subprocess.run(
                    [str(taskkill), "/PID", str(process.pid), "/T", "/F"],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=3.0,
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired):
                pass
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
    if process.poll() is None:
        try:
            process.kill()
        except OSError:
            pass


def run_process(
    argv: list[str],
    *,
    cwd: Path,
    timeout: float,
    output_limit: int = MAX_OUTPUT_BYTES,
    env: dict[str, str] | None = None,
) -> dict[str, Any]:
    """Capture stdout/stderr separately, draining pipes while enforcing limits."""
    started = time.monotonic()
    creationflags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    try:
        process = subprocess.Popen(
            argv,
            cwd=str(cwd),
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            start_new_session=(os.name != "nt"),
            creationflags=creationflags,
        )
    except OSError as error:
        return {
            "argv": argv,
            "cwd": str(cwd),
            "exit_code": None,
            "timed_out": False,
            "output_limited": False,
            "duration_seconds": round(time.monotonic() - started, 6),
            "launch_error": str(error),
            "stdout": b"",
            "stderr": b"",
        }

    buffers = {"stdout": bytearray(), "stderr": bytearray()}
    output_limited = threading.Event()

    def drain(name: str, pipe: Any) -> None:
        try:
            while True:
                block = pipe.read(65536)
                if not block:
                    return
                remaining = output_limit - len(buffers[name])
                if remaining > 0:
                    buffers[name].extend(block[:remaining])
                if len(block) > max(remaining, 0):
                    output_limited.set()
        finally:
            pipe.close()

    readers = [
        threading.Thread(target=drain, args=("stdout", process.stdout), daemon=True),
        threading.Thread(target=drain, args=("stderr", process.stderr), daemon=True),
    ]
    for reader in readers:
        reader.start()

    deadline = started + timeout
    timed_out = False
    while process.poll() is None:
        if output_limited.is_set():
            break
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            timed_out = True
            break
        time.sleep(min(0.025, remaining))

    if timed_out or output_limited.is_set():
        kill_owned_process_tree(process)
    try:
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        kill_owned_process_tree(process)
        process.wait(timeout=2.0)
    for reader in readers:
        reader.join(timeout=2.0)

    return {
        "argv": argv,
        "cwd": str(cwd),
        "exit_code": process.returncode,
        "timed_out": timed_out,
        "output_limited": output_limited.is_set(),
        "duration_seconds": round(time.monotonic() - started, 6),
        "launch_error": None,
        "stdout": bytes(buffers["stdout"]),
        "stderr": bytes(buffers["stderr"]),
    }


def result_summary(result: dict[str, Any]) -> dict[str, Any]:
    return {
        key: value
        for key, value in result.items()
        if key not in {"stdout", "stderr"}
    } | {
        "stdout_bytes": len(result["stdout"]),
        "stderr_bytes": len(result["stderr"]),
        "stdout_sha256": hashlib.sha256(result["stdout"]).hexdigest(),
        "stderr_sha256": hashlib.sha256(result["stderr"]).hexdigest(),
        "stdout_text": result["stdout"].decode("utf-8", errors="replace"),
        "stderr_text": result["stderr"].decode("utf-8", errors="replace"),
    }


def normalize_newlines(data: bytes) -> bytes:
    """The only comparison normalization: CRLF and lone CR become LF."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def compile_cli(output_dir: Path, cc_arg: str | None) -> tuple[Path, list[str], str]:
    configured_cc = cc_arg or os.environ.get("CC") or "gcc"
    compiler_words = shlex.split(configured_cc, posix=(os.name != "nt"))
    if not compiler_words:
        raise RuntimeError("CC resolved to an empty compiler command")
    compiler = shutil.which(compiler_words[0])
    if compiler is None:
        raise RuntimeError(f"C compiler not found: {compiler_words[0]}")
    compiler_words[0] = compiler
    output_dir.mkdir(parents=True, exist_ok=True)
    cli_name = "guidexos_rexx_cli.exe" if os.name == "nt" else "guidexos_rexx_cli"
    cli_path = output_dir / cli_name
    sources = [
        ROOT / "tests" / "rexx_cli_test_driver.c",
        ROOT / "rexx_cli.c",
        ROOT / "rexx_os_io.c",
        ROOT / "rexx_output.c",
        ROOT / "rexx_lexer.c",
        ROOT / "rexx_parser.c",
        ROOT / "rexx_compiler.c",
        ROOT / "rexx_vm.c",
        ROOT / "rexx_builtin.c",
        ROOT / "rexx_numeric.c",
    ]
    command = compiler_words + [
        "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        *[str(source) for source in sources], "-lm", "-o", str(cli_path),
    ]
    # All paths in the command are absolute and -o is explicit. Keep the
    # compiler working directory in its caller-owned run directory as well.
    result = run_process(command, cwd=output_dir, timeout=BUILD_TIMEOUT_SECONDS)
    if result["launch_error"] or result["timed_out"] or result["output_limited"] or result["exit_code"] != 0:
        raise RuntimeError(
            "guideXOS CLI build failed: "
            + json.dumps(result_summary(result), ensure_ascii=False)
        )
    compiler_version = run_process(
        compiler_words + ["--version"], cwd=ROOT, timeout=PROBE_TIMEOUT_SECONDS,
        output_limit=8192,
    )
    version_text = (compiler_version["stdout"] + compiler_version["stderr"]).decode(
        "utf-8", errors="replace"
    ).splitlines()
    return cli_path, command, version_text[0] if version_text else "unknown compiler version"


def locate_regina(configured: str | None) -> str | None:
    candidate = configured or os.environ.get("GUIDEXOS_REGINA")
    if candidate:
        found = shutil.which(candidate)
        if found:
            return found
        candidate_path = Path(candidate).expanduser()
        return str(candidate_path.resolve()) if candidate_path.is_file() else None
    # Do not guess that an arbitrary `rexx` executable is Regina.
    return shutil.which("regina") or shutil.which("regina.exe")


def probe_regina(executable: str, work_dir: Path) -> dict[str, Any]:
    version = run_process(
        [executable, "-v"], cwd=work_dir, timeout=PROBE_TIMEOUT_SECONDS,
        output_limit=8192,
    )
    version_text = (version["stdout"] + version["stderr"]).decode(
        "utf-8", errors="replace"
    )
    identifies_regina = re.search(r"\bRegina\b", version_text, re.IGNORECASE) is not None
    version_ok = (
        version["launch_error"] is None
        and not version["timed_out"]
        and not version["output_limited"]
        and version["exit_code"] == 0
        and identifies_regina
    )
    probe_path = work_dir / "rx11_regina_probe.rex"
    probe_path.write_text('say "RX11_REGINA_PROBE_OK"\n', encoding="ascii", newline="\n")
    minimal = run_process(
        [executable, "-r", str(probe_path)], cwd=work_dir,
        timeout=PROBE_TIMEOUT_SECONDS, output_limit=8192,
    ) if version_ok else None
    minimal_ok = (
        minimal is not None
        and minimal["launch_error"] is None
        and not minimal["timed_out"]
        and not minimal["output_limited"]
        and minimal["exit_code"] == 0
        and normalize_newlines(minimal["stdout"]) == b"RX11_REGINA_PROBE_OK\n"
        and minimal["stderr"] == b""
    )
    return {
        "available": bool(version_ok and minimal_ok),
        "executable": executable,
        "version_text": version_text.strip(),
        "version_probe": result_summary(version),
        "minimal_program_probe": result_summary(minimal) if minimal else None,
        "minimal_program_verified": bool(minimal_ok),
        "restricted_mode": "-r",
    }


def classify(
    relative: str,
    guide: dict[str, Any],
    reference: dict[str, Any] | None,
) -> tuple[str, str]:
    if relative.replace("\\", "/").startswith("host_specific/"):
        return "HOST_SPECIFIC", "Stored separately; excluded from the portable differential set."
    if reference is None:
        return "NOT_RUN", "Regina was not verified as an available reference interpreter."
    for item in (guide, reference):
        if item["launch_error"]:
            return "INCONCLUSIVE", item["launch_error"]
        if item["timed_out"]:
            return "INCONCLUSIVE", "The per-interpreter time limit expired."
        if item["output_limited"]:
            return "INCONCLUSIVE", "The per-interpreter output limit was reached."

    guide_ok = guide["exit_code"] == 0
    reference_ok = reference["exit_code"] == 0
    if guide_ok and not reference_ok:
        return "REFERENCE_UNSUPPORTED", "guideXOS completed; Regina returned a nonzero status."
    if reference_ok and not guide_ok:
        return "GUIDEXOS_UNSUPPORTED", "Regina completed; guideXOS returned a nonzero status."

    equal = (
        guide["exit_code"] == reference["exit_code"]
        and normalize_newlines(guide["stdout"]) == normalize_newlines(reference["stdout"])
        and normalize_newlines(guide["stderr"]) == normalize_newlines(reference["stderr"])
    )
    if equal:
        return "MATCH", "Exit code, stdout, and stderr agree after line-ending normalization only."
    return "MISMATCH", "Observable exit code, stdout, or stderr differs; review raw logs before assigning cause."


def safe_case_id(relative: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", relative.replace("/", "__").replace("\\", "__"))


def write_raw(run_dir: Path, case_id: str, label: str, result: dict[str, Any] | None) -> None:
    if result is None:
        return
    raw_dir = run_dir / "raw" / safe_case_id(case_id)
    raw_dir.mkdir(parents=True, exist_ok=True)
    (raw_dir / f"{label}.stdout.bin").write_bytes(result["stdout"])
    (raw_dir / f"{label}.stderr.bin").write_bytes(result["stderr"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", help="Existing guideXOS hosted CLI; otherwise build it with GCC/CC.")
    parser.add_argument("--cc", help="Compiler command used when building the hosted CLI (default: CC or gcc).")
    parser.add_argument("--regina", help="Verified Regina executable path; may also use GUIDEXOS_REGINA.")
    parser.add_argument("--case-dir", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--case", help="Run one case by path relative to --case-dir, e.g. interpret/interpret_nested.rex.")
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--timeout", type=float, default=CASE_TIMEOUT_SECONDS)
    parser.add_argument("--output-limit", type=int, default=MAX_OUTPUT_BYTES)
    args = parser.parse_args()

    case_dir = args.case_dir.resolve()
    run_dir = args.results_dir.resolve()
    run_dir.mkdir(parents=True, exist_ok=True)
    build_dir = run_dir / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    compiler_version = "not used (prebuilt CLI supplied)"
    build_command: list[str] | None = None
    if args.cli:
        cli_candidate = shutil.which(args.cli) or args.cli
        cli_path = Path(cli_candidate).expanduser().resolve()
        if not cli_path.is_file():
            parser.error(f"guideXOS CLI not found: {args.cli}")
    else:
        try:
            cli_path, build_command, compiler_version = compile_cli(build_dir, args.cc)
        except RuntimeError as error:
            parser.error(str(error))

    regina_candidate = locate_regina(args.regina)
    if regina_candidate:
        regina_info = probe_regina(regina_candidate, run_dir)
        if not regina_info["available"]:
            regina_info["availability_reason"] = "Version identity or restricted minimal-program probe failed."
            regina_info = {**regina_info, "available": False}
    else:
        regina_info = {
            "available": False,
            "executable": None,
            "version_text": None,
            "availability_reason": "No regina executable found in PATH and no valid --regina/GUIDEXOS_REGINA path was supplied.",
        }

    case_paths = sorted(case_dir.rglob("*.rex"))
    expectations_path = case_dir / "expectations.json"
    expectations = json.loads(expectations_path.read_text(encoding="utf-8")) if expectations_path.is_file() else {}
    if args.case:
        wanted = Path(args.case).as_posix()
        case_paths = [path for path in case_paths if path.relative_to(case_dir).as_posix() == wanted]
        if not case_paths:
            parser.error(f"No case found at {wanted}")
    if not case_paths:
        parser.error(f"No .rex cases found under {case_dir}")

    try:
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True, text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        head = "unknown"

    env = os.environ.copy()
    env.pop("REGINA_OPTIONS", None)
    records: list[dict[str, Any]] = []
    guide_execution_counts: dict[str, int] = {}
    for path in case_paths:
        relative = path.relative_to(case_dir).as_posix()
        source = path.read_bytes()
        if len(source) > 65_536:
            raise RuntimeError(f"fixture exceeds RX10 interpreted-source boundary: {relative}")
        guide_argv = [str(cli_path), str(path)]
        guide = run_process(
            guide_argv, cwd=case_dir, timeout=args.timeout,
            output_limit=args.output_limit, env=env,
        )
        reference: dict[str, Any] | None = None
        if regina_info["available"] and not relative.startswith("host_specific/"):
            reference = run_process(
                [regina_candidate, "-r", str(path)], cwd=case_dir,
                timeout=args.timeout, output_limit=args.output_limit, env=env,
            )
        status, reason = classify(relative, guide, reference)
        expected_exit = int(expectations.get(relative, {}).get("exit_code", 0))
        if guide["launch_error"]:
            guide_execution = "LAUNCH_ERROR"
        elif guide["timed_out"]:
            guide_execution = "TIMEOUT"
        elif guide["output_limited"]:
            guide_execution = "OUTPUT_LIMIT"
        elif guide["exit_code"] == expected_exit:
            guide_execution = "EXPECTED_EXIT"
        else:
            guide_execution = "UNEXPECTED_EXIT"
        guide_execution_counts[guide_execution] = guide_execution_counts.get(guide_execution, 0) + 1
        write_raw(run_dir, relative, "guidexos", guide)
        write_raw(run_dir, relative, "regina", reference)
        reproduction = [
            sys.executable, str(Path(__file__).resolve()),
            "--case", relative,
            "--case-dir", str(case_dir),
            "--results-dir", str(run_dir),
        ]
        if regina_candidate:
            reproduction += ["--regina", regina_candidate]
        record = {
            "id": relative,
            "category": Path(relative).parts[0] if Path(relative).parts else "unknown",
            "classification": status,
            "classification_reason": reason,
            "guideXOS_execution": guide_execution,
            "expected_exit_code": expected_exit,
            "reproduction_command": reproduction,
            "guideXOS": result_summary(guide),
            "Regina": result_summary(reference) if reference is not None else None,
        }
        records.append(record)
        print(f"[{status}] {relative}")

    counts: dict[str, int] = {}
    for record in records:
        counts[record["classification"]] = counts.get(record["classification"], 0) + 1
    report = {
        "schema_version": 1,
        "generated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "repository_root": str(ROOT),
        "git_head": head,
        "guideXOS": {
            "executable": str(cli_path),
            "compiler_version": compiler_version,
            "build_command": build_command,
        },
        "Regina": regina_info,
        "normalization": "Only CRLF and lone CR line endings are normalized to LF. No trimming, case folding, numeric formatting, stderr, exit-code, or output-order normalization is applied.",
        "limits": {
            "per_interpreter_timeout_seconds": args.timeout,
            "stdout_limit_bytes": args.output_limit,
            "stderr_limit_bytes": args.output_limit,
            "restricted_reference_mode": "Regina is invoked with -r; scripts are corpus-controlled and no ADDRESS host commands, shell execution, filesystem mutation, or networking are used.",
        },
        "counts": counts,
        "guideXOS_execution_counts": guide_execution_counts,
        "total_cases": len(records),
        "cases": records,
    }
    (run_dir / "results.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"Summary: differential={json.dumps(counts, sort_keys=True)} local={json.dumps(guide_execution_counts, sort_keys=True)} ({len(records)} cases)")
    print(f"Results: {run_dir / 'results.json'}")
    return 0 if all(
        record["classification"] in {"MATCH", "NOT_RUN", "HOST_SPECIFIC"}
        and record["guideXOS_execution"] == "EXPECTED_EXIT"
        for record in records
    ) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"RX11 differential runner error: {error}", file=sys.stderr)
        raise SystemExit(2)
