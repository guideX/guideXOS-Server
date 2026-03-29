#!/bin/bash
#
# Build and run guideXOS SPARC v9 kernel in QEMU (Linux host)
#
# Usage:  ./build-and-run-sparcv9_in_linux.sh [--graphics]
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
"$SCRIPT_DIR/build-sparcv9.sh"

KERNEL="$REPO_ROOT/build/sparc64/bin/kernel.elf"

# -----------------------------------------------------------
# 2. Check for QEMU
# -----------------------------------------------------------
if ! command -v qemu-system-sparc64 &>/dev/null; then
    echo "[ERROR] qemu-system-sparc64 not found."
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
echo "  Launching QEMU (Sun4u / UltraSPARC)"
echo "============================================"
echo ""
echo "  Ctrl-A X  to quit (nographic mode)"
echo "  Ctrl-C    to interrupt"
echo ""

if [ "$GRAPHICS" -eq 1 ]; then
    # Graphical mode — shows VGA framebuffer window
    qemu-system-sparc64 \
        -machine sun4u \
        -m 256 \
        -kernel "$KERNEL" \
        -vga std \
        -d guest_errors
else
    # Serial console mode — no window, output to terminal
    qemu-system-sparc64 \
        -machine sun4u \
        -m 256 \
        -kernel "$KERNEL" \
        -nographic \
        -serial mon:stdio \
        -d guest_errors
fi
