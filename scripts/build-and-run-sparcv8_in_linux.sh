#!/bin/bash
#
# Build and run guideXOS SPARC kernel in QEMU (Linux host)
#
# Usage:  ./kernel/build-and-run-sparc.sh [--graphics]
#
# Copyright (c) 2026 guideXOS Server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Parse arguments
GRAPHICS=0
if [ "$1" = "--graphics" ]; then
    GRAPHICS=1
fi

# -----------------------------------------------------------
# 1. Build
# -----------------------------------------------------------
"$SCRIPT_DIR/build-sparc.sh"

KERNEL="$REPO_ROOT/build/sparc/bin/kernel.elf"

# -----------------------------------------------------------
# 2. Check for QEMU
# -----------------------------------------------------------
if ! command -v qemu-system-sparc &>/dev/null; then
    echo "[ERROR] qemu-system-sparc not found."
    echo ""
    echo "  Install with:  sudo apt install qemu-system-sparc"
    echo ""
    exit 1
fi

# -----------------------------------------------------------
# 3. Run
# -----------------------------------------------------------
echo ""
echo "============================================"
echo "  Launching QEMU (Sun4m / SPARCstation 5)"
echo "============================================"
echo ""
echo "  Ctrl-A X  to quit (nographic mode)"
echo "  Ctrl-C    to interrupt"
echo ""

if [ "$GRAPHICS" -eq 1 ]; then
    # Graphical mode — shows TCX framebuffer window
    qemu-system-sparc \
        -machine SS-5 \
        -m 128 \
        -kernel "$KERNEL" \
        -vga tcx \
        -d guest_errors
else
    # Serial console mode — no window, output to terminal
    qemu-system-sparc \
        -machine SS-5 \
        -m 128 \
        -kernel "$KERNEL" \
        -nographic \
        -serial mon:stdio \
        -d guest_errors
fi