#!/bin/bash
#
# Test build script for guideXOS kernel
#
# Copyright (c) 2024 guideX
#

set -e  # Exit on error

echo "=========================================="
echo "guideXOS Kernel Build Test"
echo "=========================================="
echo ""

# Check for required tools
echo "Checking required tools..."
command -v i686-elf-gcc >/dev/null 2>&1 || { echo "Error: i686-elf-gcc not found"; exit 1; }
command -v nasm >/dev/null 2>&1 || { echo "Error: nasm not found"; exit 1; }
echo "? Cross-compiler found"
echo "? Assembler found"
echo ""

# Navigate to kernel directory
cd "$(dirname "$0")/../kernel" || exit 1

# Clean build
echo "Cleaning previous build..."
make ARCH=x86 clean 2>&1 | grep -v "No such file"
echo ""

# Build kernel
echo "Building x86 kernel..."
if make ARCH=x86; then
    echo ""
    echo "=========================================="
    echo "? BUILD SUCCESSFUL"
    echo "=========================================="
    echo ""
    echo "Kernel location: build/x86/bin/kernel.elf"
    
    # Show file info
    if command -v file >/dev/null 2>&1; then
        echo ""
        echo "File information:"
        file build/x86/bin/kernel.elf
    fi
    
    # Show size
    echo ""
    echo "Kernel size:"
    ls -lh build/x86/bin/kernel.elf | awk '{print $5, $9}'
    
    # Check for multiboot header
    if command -v objdump >/dev/null 2>&1; then
        echo ""
        echo "Multiboot header check:"
        if objdump -x build/x86/bin/kernel.elf | grep -q "multiboot"; then
            echo "? Multiboot header present"
        else
            echo "? Warning: Multiboot header not detected"
        fi
    fi
    
    echo ""
    echo "Next steps:"
    echo "  1. Run in QEMU: ./scripts/run-qemu-x86.sh"
    echo "  2. Or manually: qemu-system-i386 -kernel build/x86/bin/kernel.elf"
    echo ""
    
    exit 0
else
    echo ""
    echo "=========================================="
    echo "? BUILD FAILED"
    echo "=========================================="
    echo ""
    echo "Check the error messages above."
    echo "Common issues:"
    echo "  - Missing cross-compiler (i686-elf-gcc)"
    echo "  - Missing NASM assembler"
    echo "  - Syntax errors in source files"
    echo ""
    exit 1
fi
