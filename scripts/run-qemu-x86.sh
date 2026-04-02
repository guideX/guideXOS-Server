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
    echo "USB tablet mode: No mouse grab required"
    echo "Press Ctrl+A then X to exit QEMU"
    echo "----------------------------------------"
    echo ""
    
    # Launch QEMU in UEFI mode with USB tablet for absolute mouse positioning
    qemu-system-x86_64 \
        -bios OVMF.fd \
        -device qemu-xhci,id=xhci \
        -device usb-tablet,bus=xhci.0 \
        -drive file=fat:rw:ESP,format=raw \
        -m 1024M \
        -serial stdio \
        -display gtk \
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
    echo "USB tablet mode: No mouse grab required"
    echo "Press Ctrl+A then X to exit QEMU"
    echo "----------------------------------------"
    echo ""

    # Launch with USB tablet for absolute mouse positioning
    qemu-system-i386 \
        -device usb-ehci,id=ehci \
        -device usb-tablet,bus=ehci.0 \
        -kernel "$KERNEL_PATH" \
        -m 128M \
        -vga std \
        -serial stdio \
        -display gtk \
        -no-reboot \
        -no-shutdown
fi

echo ""
echo -e "${CYAN}QEMU exited${NC}"

