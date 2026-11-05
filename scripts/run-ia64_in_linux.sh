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
