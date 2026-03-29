#!/bin/bash
#
# Linux Build Script for guideXOS Kernel (IA-64 / Itanium)
#
# Run from the scripts directory:  ./build-ia64.sh
# Or from the repository root:     ./scripts/build-ia64.sh
#
# Copyright (c) 2026 guideXOS Server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "============================================"
echo "  guideXOS Itanium (IA-64) Kernel Build"
echo "============================================"
echo ""

# -----------------------------------------------------------
# 1. Check for the cross-compiler
# -----------------------------------------------------------
CROSS=""
if command -v ia64-linux-gnu-g++ &>/dev/null; then
    CROSS="ia64-linux-gnu"
elif command -v ia64-elf-g++ &>/dev/null; then
    CROSS="ia64-elf"
else
    echo "[ERROR] No IA-64 cross-compiler found."
    echo ""
    echo "  Install with:  sudo apt install gcc-ia64-linux-gnu g++-ia64-linux-gnu"
    echo ""
    echo "  Or build an ia64-elf cross-toolchain from source."
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

echo "[BUILD] Cleaning previous IA-64 build..."
make ARCH=ia64 clean 2>/dev/null || true

echo "[BUILD] Compiling IA-64 kernel..."
make ARCH=ia64 -j"$(nproc)"

# -----------------------------------------------------------
# 3. Summary
# -----------------------------------------------------------
KERNEL="$REPO_ROOT/build/ia64/bin/kernel.elf"

echo ""
echo "============================================"
echo "  Build complete!"
echo "============================================"
echo ""

if [ -f "$KERNEL" ]; then
    SIZE=$(stat -c%s "$KERNEL" 2>/dev/null || stat -f%z "$KERNEL" 2>/dev/null)
    echo "  Kernel:  $KERNEL"
    echo "  Size:    $SIZE bytes"
    echo ""
    echo "  Run with ski:"
    echo "    ski $KERNEL"
    echo ""
    echo "  Or use the run script:"
    echo "    ./scripts/run-ia64_in_linux.sh"
else
    echo "  [WARN] Kernel ELF not found at expected path."
    echo "         Check build output above for errors."
fi
