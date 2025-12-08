#!/bin/bash
#
# Launch guideXOS kernel in QEMU with VNC support
# This allows remote viewing from another computer
#
# Copyright (c) 2024 guideX

echo "Starting guideXOS Kernel with VNC support..."
echo ""
echo "VNC Server will be available on:"
echo "  - localhost:5900 (from this computer)"
echo "  - $(hostname):5900 (from network)"
echo "  - $(hostname -I | awk '{print $1}'):5900 (from other computers)"
echo ""
echo "Connect with any VNC client:"
echo "  vncviewer localhost:5900"
echo "  or use TightVNC, RealVNC, etc."
echo ""

# Path to kernel
KERNEL="build/x86/bin/kernel.elf"

# Check if kernel exists
if [ ! -f "$KERNEL" ]; then
    echo "Error: Kernel not found at $KERNEL"
    echo "Please build the kernel first:"
    echo "  cd kernel"
    echo "  make ARCH=x86"
    exit 1
fi

# Launch QEMU with VNC
qemu-system-i386 \
    -kernel "$KERNEL" \
    -m 128M \
    -vnc :0 \
    -k en-us \
    -serial stdio

# Check if QEMU exited with error
if [ $? -ne 0 ]; then
    echo ""
    echo "Error: QEMU failed to start"
    echo "Please check QEMU installation"
    exit 1
fi
