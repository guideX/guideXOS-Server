#!/bin/bash
#
# Linux Build Script for guideXOS Kernel (RISC-V 64 / RV64IMA)
#
# Run from the scripts directory:  ./build-riscv64.sh
# Or from the repository root:     ./scripts/build-riscv64.sh
#
# Copyright (c) 2026 guideXOS Server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "============================================"
echo "  guideXOS RISC-V 64 (RV64IMA) Kernel Build"
echo "============================================"
echo ""

# -----------------------------------------------------------
# 1. Check for the cross-compiler
# -----------------------------------------------------------
CROSS=""
if command -v riscv64-elf-g++ &>/dev/null; then
    CROSS="riscv64-elf"
elif command -v riscv64-unknown-elf-g++ &>/dev/null; then
    CROSS="riscv64-unknown-elf"
elif command -v riscv64-linux-gnu-g++ &>/dev/null; then
    CROSS="riscv64-linux-gnu"
else
    echo "[ERROR] No RISC-V 64 cross-compiler found."
    echo ""
    echo "  Install with:  sudo apt install gcc-riscv64-linux-gnu g++-riscv64-linux-gnu"
    echo ""
    echo "  Or install the bare-metal toolchain:"
    echo "    sudo apt install gcc-riscv64-unknown-elf"
    echo ""
    echo "  Or build a riscv64-elf cross-toolchain from source."
    echo ""
    exit 1
fi

echo "[INFO] Using toolchain prefix: ${CROSS}-"
echo "[INFO] Compiler: $(${CROSS}-g++ --version | head -1)"
echo ""

# -----------------------------------------------------------
# 2. Build
# -----------------------------------------------------------
cd "$REPO_ROOT"

echo "[BUILD] Cleaning previous RISC-V 64 build..."
make ARCH=riscv64 clean 2>/dev/null || true

echo "[BUILD] Compiling RISC-V 64 kernel..."
make ARCH=riscv64 -j"$(nproc)"

KERNEL="build/riscv64/bin/kernel.elf"

if [ ! -f "$KERNEL" ]; then
    echo "[ERROR] Build failed — $KERNEL not found."
    exit 1
fi

# -----------------------------------------------------------
# 3. Summary
# -----------------------------------------------------------
echo ""
echo "============================================"
echo "  Build complete!"
echo "============================================"
echo ""

SIZE=$(stat -c%s "$KERNEL" 2>/dev/null || stat -f%z "$KERNEL" 2>/dev/null)
echo "  Kernel:  $(realpath "$KERNEL")"
echo "  Size:    $SIZE bytes"
echo ""
file "$KERNEL"
${CROSS}-size "$KERNEL" 2>/dev/null || true
echo ""
echo "  Run with QEMU:"
echo "    qemu-system-riscv64 -machine virt -bios default -kernel $KERNEL -nographic"
echo ""
echo "  Or use the run script:"
echo "    ./scripts/build-and-run-riscv64_in_linux.sh"
echo ""

# -----------------------------------------------------------
# 4. Optional: copy to shared folder for Windows QEMU
# -----------------------------------------------------------
SHARE_DIR="${RISCV64_SHARE_DIR:-}"
if [ -n "$SHARE_DIR" ] && [ -d "$SHARE_DIR" ]; then
    cp "$KERNEL" "$SHARE_DIR/kernel-riscv64.elf"
    echo "[INFO] Copied kernel to shared folder: $SHARE_DIR"
fi

echo "Done."
