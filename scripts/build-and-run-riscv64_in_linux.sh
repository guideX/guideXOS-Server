#!/bin/bash
#
# Build and run guideXOS RISC-V 64 kernel in QEMU (Linux host)
#
# Usage:  ./build-and-run-riscv64_in_linux.sh [--nographic]
#
# The kernel boots on the QEMU "virt" machine with OpenSBI as
# the firmware (SBI implementation).  By default it runs in
# graphical framebuffer mode with VNC on :0 for remote viewing;
# pass --nographic for serial-only console mode.
#
# Prerequisites:
#   - riscv64-elf, riscv64-unknown-elf, or riscv64-linux-gnu cross-compiler
#   - qemu-system-riscv64
#     Install:  sudo apt install qemu-system-misc
#
# Copyright (c) 2026 guideXOS Server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Parse arguments
NOGRAPHIC=0
if [ "$1" = "--nographic" ]; then
    NOGRAPHIC=1
fi

# -----------------------------------------------------------
# 1. Build
# -----------------------------------------------------------
"$SCRIPT_DIR/build-riscv64.sh"

KERNEL="$REPO_ROOT/build/riscv64/bin/kernel.elf"

# -----------------------------------------------------------
# 2. Check for QEMU
# -----------------------------------------------------------
if ! command -v qemu-system-riscv64 &>/dev/null; then
    echo "[ERROR] qemu-system-riscv64 not found."
    echo ""
    echo "  Install with:  sudo apt install qemu-system-misc"
    echo ""
    exit 1
fi

# -----------------------------------------------------------
# 3. Run
# -----------------------------------------------------------
echo ""
echo "============================================"
echo "  Launching QEMU (RISC-V 64 virt machine)"
echo "============================================"
echo ""
echo "  Ctrl-A X  to quit (nographic mode)"
echo "  Ctrl-C    to interrupt"
echo ""

if [ "$NOGRAPHIC" -eq 1 ]; then
    # Serial console mode — no window, output to terminal
    qemu-system-riscv64 \
        -machine virt \
        -bios default \
        -m 256 \
        -kernel "$KERNEL" \
        -nographic \
        -serial mon:stdio \
        -d guest_errors
else
    # Graphical mode — native framebuffer window (primary) with VNC (secondary viewer)
    qemu-system-riscv64 \
        -machine virt \
        -bios default \
        -m 256 \
        -kernel "$KERNEL" \
        -vga std \
        -vnc :0 \
        -d guest_errors
fi
