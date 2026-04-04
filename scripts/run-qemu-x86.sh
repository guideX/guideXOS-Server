#!/bin/bash
#
# QEMU Launcher for guideXOS
# Supports both UEFI and legacy BIOS boot
#
# Usage:
#   ./run-qemu-x86.sh        # Legacy BIOS boot
#   ./run-qemu-x86.sh uefi   # UEFI boot
#
# Copyright (c) 2024 guideX
#

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Check if UEFI mode is requested
if [ "$1" = "uefi" ]; then
    echo -e "${CYAN}=====================================${NC}"
    echo -e "${CYAN}  guideXOS UEFI Boot${NC}"
    echo -e "${CYAN}=====================================${NC}"
    echo ""
    
    # Check if OVMF.fd exists
    if [ ! -f "OVMF.fd" ]; then
        echo -e "${RED}ERROR: OVMF.fd not found!${NC}"
        echo -e "${YELLOW}Please download OVMF.fd from:${NC}"
        echo -e "${YELLOW}https://github.com/tianocore/edk2/releases${NC}"
        echo ""
        echo -e "${YELLOW}Or on Ubuntu/Debian:${NC}"
        echo -e "  ${GREEN}sudo apt-get install ovmf${NC}"
        echo -e "  ${GREEN}cp /usr/share/ovmf/OVMF.fd .${NC}"
        echo ""
        exit 1
    fi
    
    # Check if ESP directory exists
    if [ ! -d "ESP" ]; then
        echo -e "${RED}ERROR: ESP directory not found!${NC}"
        echo -e "${YELLOW}Please run build script first:${NC}"
        echo -e "  ${GREEN}powershell ./build-uefi.ps1${NC}"
        echo ""
        exit 1
    fi
    
    echo -e "${GREEN}Launching QEMU with UEFI boot...${NC}"
    echo "ESP directory: ESP/"
    echo "Framebuffer display: QEMU window shows the desktop directly"
    echo "VNC secondary viewer: connect to localhost:5900"
    echo "Press Ctrl+A then X to exit QEMU"
    echo "----------------------------------------"
    echo ""
    
    # Launch QEMU in UEFI mode with native framebuffer display (primary)
    # and VNC as secondary viewer for remote access.
    # usb=off ensures mouse events are routed to PS/2 (IRQ12)
    # since the kernel does not yet have a USB HID driver.
    qemu-system-x86_64 \
        -machine q35,usb=off \
        -drive if=pflash,format=raw,readonly=on,file=OVMF.fd \
        -drive file=fat:rw:ESP,format=raw \
        -m 1024M \
        -vga std \
        -serial stdio \
        -display gtk \
        -vnc :0 \
        -no-reboot \
        -no-shutdown
else
    echo -e "${CYAN}=====================================${NC}"
    echo -e "${CYAN}  guideXOS Legacy BIOS Boot${NC}"
    echo -e "${CYAN}=====================================${NC}"
    echo ""
    
    KERNEL_PATH="kernel/build/x86/bin/kernel.elf"

    if [ ! -f "$KERNEL_PATH" ]; then
        echo -e "${RED}Error: Kernel not found at $KERNEL_PATH${NC}"
        echo -e "${YELLOW}Please build the kernel first:${NC}"
        echo -e "  ${GREEN}cd kernel && make ARCH=x86${NC}"
        exit 1
    fi

    echo -e "${GREEN}Launching guideXOS x86 kernel in QEMU...${NC}"
    echo "Kernel: $KERNEL_PATH"
    echo "Framebuffer display: QEMU window shows the desktop directly"
    echo "VNC secondary viewer: connect to localhost:5900"
    echo "Press Ctrl+A then X to exit QEMU"
    echo "----------------------------------------"
    echo ""

    # Launch with native framebuffer display (primary) and VNC (secondary viewer)
    # usb=off ensures mouse events are routed to PS/2 (IRQ12)
    # since the kernel does not yet have a USB HID driver.
    qemu-system-i386 \
        -machine pc,usb=off \
        -kernel "$KERNEL_PATH" \
        -m 128M \
        -vga std \
        -serial stdio \
        -display gtk \
        -vnc :0 \
        -no-reboot \
        -no-shutdown
fi

echo ""
echo -e "${CYAN}QEMU exited${NC}"

