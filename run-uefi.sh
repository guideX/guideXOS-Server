#!/bin/bash
#
# Run guideXOS in QEMU with UEFI
#
# Copyright (c) 2024 guideX
#

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

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
    echo -e "  ${GREEN}./build-uefi.ps1${NC}"
    echo ""
    exit 1
fi

# Check if qemu-system-x86_64 is available
if ! command -v qemu-system-x86_64 &> /dev/null; then
    echo -e "${RED}ERROR: qemu-system-x86_64 not found${NC}"
    echo -e "${YELLOW}Please install QEMU:${NC}"
    echo -e "  ${GREEN}sudo apt-get install qemu-system-x86${NC}"
    echo ""
    exit 1
fi

echo -e "${GREEN}Launching QEMU with UEFI boot...${NC}"
echo ""

# Launch QEMU
qemu-system-x86_64 \
    -bios OVMF.fd \
    -drive file=fat:rw:ESP,format=raw \
    -m 1024M \
    -serial stdio \
    -no-reboot \
    -d int,cpu_reset

echo ""
echo -e "${CYAN}QEMU exited${NC}"
