#!/bin/bash
#
# QEMU x86 Launcher for guideXOS
#
# Copyright (c) 2024 guideX
#

KERNEL_PATH="build/x86/bin/kernel.elf"

if [ ! -f "$KERNEL_PATH" ]; then
    echo "Error: Kernel not found at $KERNEL_PATH"
    echo "Please build the kernel first: make ARCH=x86"
    exit 1
fi

echo "Launching guideXOS x86 kernel in QEMU..."
echo "Kernel: $KERNEL_PATH"
echo ""
echo "Press Ctrl+A then X to exit QEMU"
echo "----------------------------------------"

qemu-system-i386 \
    -kernel "$KERNEL_PATH" \
    -m 128M \
    -serial stdio \
    -display gtk \
    -no-reboot \
    -no-shutdown
