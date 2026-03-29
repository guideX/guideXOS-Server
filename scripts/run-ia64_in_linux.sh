#!/bin/bash
#
# Build and run guideXOS IA-64 kernel on the HP ski simulator (Linux host)
#
# Usage:  ./run-ia64_in_linux.sh [--build-only]
#
# Prerequisites:
#   - ia64-linux-gnu or ia64-elf cross-compiler (for building)
#   - ski IA-64 simulator (for running)
#     Install:  Build from https://github.com/trofi/ski
#     Or:       Check if your distro packages it
#
# Copyright (c) 2026 guideXOS Server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_ONLY=0
if [ "$1" = "--build-only" ]; then
    BUILD_ONLY=1
fi

# -----------------------------------------------------------
# 1. Build
# -----------------------------------------------------------
"$SCRIPT_DIR/build-ia64.sh"

KERNEL="$REPO_ROOT/build/ia64/bin/kernel.elf"

if [ "$BUILD_ONLY" -eq 1 ]; then
    echo ""
    echo "Build-only mode; skipping emulator launch."
    exit 0
fi

# -----------------------------------------------------------
# 2. Check for ski
# -----------------------------------------------------------
if ! command -v ski &>/dev/null; then
    # Also check for bski (batch-mode ski)
    if ! command -v bski &>/dev/null; then
        echo "[ERROR] HP ski IA-64 simulator not found."
        echo ""
        echo "  Build from source:  https://github.com/trofi/ski"
        echo ""
        echo "  Or run in batch mode with bski if available."
        echo ""
        exit 1
    fi
    SKI_CMD="bski"
else
    SKI_CMD="ski"
fi

# -----------------------------------------------------------
# 3. Run
# -----------------------------------------------------------
echo ""
echo "============================================"
echo "  Launching ski IA-64 Simulator"
echo "============================================"
echo ""
echo "  Simulator: $SKI_CMD"
echo "  Kernel:    $KERNEL"
echo ""
echo "  Inside ski, type 'quit' to exit."
echo "  Or press Ctrl-C to interrupt."
echo ""

# ski loads the ELF and starts execution at _start
# Use batch mode (bski) if available for non-interactive use
if [ "$SKI_CMD" = "bski" ]; then
    bski "$KERNEL"
else
    ski "$KERNEL"
fi
