#!/bin/bash
#
# Linux Build Script for guideXOS Kernel (SPARC v9 / UltraSPARC)
#
# Run from the scripts directory:  ./build-sparcv9.sh
# Or from the repository root:     ./scripts/build-sparcv9.sh
#
# Copyright (c) 2026 guideXOS Server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "============================================"
echo "  guideXOS SPARC v9 (64-bit) Kernel Build"
echo "============================================"
echo ""

# -----------------------------------------------------------
# 1. Check for the cross-compiler
# -----------------------------------------------------------
CROSS=""
if command -v sparc64-linux-gnu-g++ &>/dev/null; then
    CROSS="sparc64-linux-gnu"
elif command -v sparc64-elf-g++ &>/dev/null; then
    CROSS="sparc64-elf"
else
    echo "[ERROR] No SPARC64 cross-compiler found."
    echo ""
    echo "  Install with:  sudo apt install gcc-sparc64-linux-gnu g++-sparc64-linux-gnu"
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

echo "[BUILD] Cleaning previous SPARC64 build..."
make ARCH=sparc64 clean 2>/dev/null || true

echo "[BUILD] Compiling SPARC v9 kernel..."
make ARCH=sparc64 -j"$(nproc)"

KERNEL="build/sparc64/bin/kernel.elf"

if [ ! -f "$KERNEL" ]; then
    echo "[ERROR] Build failed — $KERNEL not found."
    exit 1
fi

echo ""
echo "[OK] Build succeeded."
echo ""
file "$KERNEL"
${CROSS}-size "$KERNEL" 2>/dev/null || true
echo ""
echo "Kernel ELF: $(realpath "$KERNEL")"
echo ""

# -----------------------------------------------------------
# 3. Optional: copy to shared folder for Windows QEMU
# -----------------------------------------------------------
SHARE_DIR="${SPARC64_SHARE_DIR:-}"
if [ -n "$SHARE_DIR" ] && [ -d "$SHARE_DIR" ]; then
    cp "$KERNEL" "$SHARE_DIR/kernel-sparc64.elf"
    echo "[INFO] Copied kernel to shared folder: $SHARE_DIR"
fi

echo "Done."
