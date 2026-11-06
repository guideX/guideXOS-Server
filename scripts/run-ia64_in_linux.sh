#!/usr/bin/env bash
# Build and optionally run guideXOS on the HP ski IA-64 simulator.
# Usage: scripts/run-ia64_in_linux.sh [--build-only]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if (( $# > 1 )) || { (( $# == 1 )) && [[ "$1" != '--build-only' ]]; }; then
    echo 'Usage: run-ia64_in_linux.sh [--build-only]' >&2
    exit 2
fi
BUILD_ONLY=0
if [[ "${1:-}" == '--build-only' ]]; then
    BUILD_ONLY=1
fi

FP_PROFILE="${IA64_FP_PROFILE:-runtime}"
case "$FP_PROFILE" in
    runtime|compile-only|early-exec) ;;
    *)
        echo "[RUN_BLOCKED] Unsupported IA64_FP_PROFILE '$FP_PROFILE'." >&2
        exit 2
        ;;
esac
if [[ "$FP_PROFILE" == compile-only && "$BUILD_ONLY" -eq 0 ]]; then
    echo '[RUN_BLOCKED] The compile-only FP profile cannot be used for simulator or guest execution.' >&2
    echo '[RUN_BLOCKED] Use build-ia64.sh directly to produce compile-only ELF evidence.' >&2
    exit 2
fi
if [[ "$FP_PROFILE" == early-exec && "$BUILD_ONLY" -eq 0 &&
      "${IA64_GUEST_QUALIFICATION_AUTHORIZED:-no}" != yes ]]; then
    echo '[RUN_BLOCKED] IA64-2 early-exec requires a separately authorized guest-qualification phase.' >&2
    echo '[RUN_BLOCKED] Static gates and a distinct artifact identity do not authorize execution.' >&2
    exit 2
fi

# Pick one isolated output directory and pass it through to the build script.
if [[ -z "${IA64_BUILD_DIR:-}" ]]; then
    IA64_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/guidexos-ia64-1.XXXXXX")"
elif [[ "$IA64_BUILD_DIR" != /* ]]; then
    IA64_BUILD_DIR="$REPO_ROOT/$IA64_BUILD_DIR"
fi
export IA64_BUILD_DIR

"$SCRIPT_DIR/build-ia64.sh"
KERNEL="$IA64_BUILD_DIR/bin/kernel.elf"

if (( BUILD_ONLY )); then
    echo '[INFO] Build-only mode; skipping simulator launch.'
    exit 0
fi

PROFILE_FILE="$IA64_BUILD_DIR/kernel-build-profile.txt"
MANIFEST_FILE="$IA64_BUILD_DIR/ia64-artifact-manifest.json"
if [[ ! -f "$PROFILE_FILE" || ! -f "$MANIFEST_FILE" ]]; then
    echo '[RUN_BLOCKED] The IA-64 build is missing its profile or artifact manifest.' >&2
    exit 2
fi
if ! python3 - "$KERNEL" "$MANIFEST_FILE" "$FP_PROFILE" \
    "${IA64_GUEST_QUALIFICATION_AUTHORIZED:-no}" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

elf = Path(sys.argv[1])
manifest_path = Path(sys.argv[2])
profile = sys.argv[3]
authorized = sys.argv[4] == "yes"
try:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
except (OSError, json.JSONDecodeError) as exc:
    print(f"[RUN_BLOCKED] Invalid artifact manifest: {exc}", file=sys.stderr)
    raise SystemExit(1)

actual = hashlib.sha256(elf.read_bytes()).hexdigest()
if manifest.get("schema") != "guidexos-ia64-artifact-v1" or \
        manifest.get("profile") != profile or manifest.get("elf") != "bin/kernel.elf" or \
        manifest.get("sha256") != actual:
    print("[RUN_BLOCKED] Artifact identity, profile, or SHA-256 does not match the manifest.", file=sys.stderr)
    raise SystemExit(1)
if profile == "compile-only":
    print("[RUN_BLOCKED] The compile-only IA-64 artifact cannot be executed.", file=sys.stderr)
    raise SystemExit(1)
if profile == "early-exec":
    if manifest.get("boot_contract") != "ski-physical-entry-single-cpu-no-preempt" or \
            manifest.get("cpu_count_requirement") != "exactly-one-CPU-external-SKI-precondition-runtime-unverified" or \
            manifest.get("static_gates") != "PASS" or \
            manifest.get("runtime_qualified") is not False or \
            manifest.get("scheduler_qualified") is not False or \
            manifest.get("interrupt_fp_preservation") is not False or \
            manifest.get("guest_execution_authorized") is not False or not authorized:
        print("[RUN_BLOCKED] Early-exec gates or separate guest-qualification authorization are incomplete.", file=sys.stderr)
        raise SystemExit(1)
else:
    if manifest.get("runtime_qualified") is not True or \
            manifest.get("scheduler_qualified") is not True or \
            manifest.get("guest_execution_authorized") is not True:
        print("[RUN_BLOCKED] This IA-64 build has no runtime qualification record.", file=sys.stderr)
        raise SystemExit(1)
PY
then
    echo "[RUN_BLOCKED] Profile record: $PROFILE_FILE" >&2
    exit 2
fi

if command -v ski >/dev/null 2>&1; then
    SKI_CMD=ski
elif command -v bski >/dev/null 2>&1; then
    SKI_CMD=bski
else
    echo '[SIMULATOR_BLOCKED] HP ski/bski simulator not found.' >&2
    echo 'The built ELF is IA-64; this execution profile is the HP ski simulator.' >&2
    exit 2
fi

printf '\n[INFO] Simulator profile: HP ski (%s)\n' "$SKI_CMD"
printf '[INFO] Kernel: %s\n' "$KERNEL"
echo '[INFO] SKI execution is separate from UEFI firmware and guideXOS Hypervisor boot.'
if [[ "$SKI_CMD" == bski ]]; then
    bski "$KERNEL"
else
    ski "$KERNEL"
fi
