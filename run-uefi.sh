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

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo -e "${CYAN}=====================================${NC}"
echo -e "${CYAN}  guideXOS UEFI Boot${NC}"
echo -e "${CYAN}=====================================${NC}"
echo ""

# Detect OVMF firmware location
# Prefer local OVMF.fd, then system-installed split images (OVMF_CODE + OVMF_VARS)
OVMF_CODE=""
OVMF_VARS=""
SPLIT_PFLASH=0

if [ -f "${SCRIPT_DIR}/OVMF.fd" ]; then
    OVMF_CODE="${SCRIPT_DIR}/OVMF.fd"
    echo -e "${GREEN}Using local OVMF.fd${NC}"
elif [ -f "/usr/share/OVMF/OVMF_CODE.fd" ]; then
    # Debian/Ubuntu ovmf package - split images
    OVMF_CODE="/usr/share/OVMF/OVMF_CODE.fd"
    SPLIT_PFLASH=1
    echo -e "${GREEN}Using system OVMF (split images)${NC}"
    # Create local writable vars copy if needed
    if [ ! -f "${SCRIPT_DIR}/OVMF_VARS.fd" ]; then
        if [ -f "/usr/share/OVMF/OVMF_VARS.fd" ]; then
            echo -e "${YELLOW}Creating local UEFI variable store...${NC}"
            cp "/usr/share/OVMF/OVMF_VARS.fd" "${SCRIPT_DIR}/OVMF_VARS.fd"
        else
            echo -e "${YELLOW}WARNING: OVMF_VARS.fd not found, creating empty vars store${NC}"
            dd if=/dev/zero of="${SCRIPT_DIR}/OVMF_VARS.fd" bs=1K count=128 2>/dev/null
        fi
    fi
    OVMF_VARS="${SCRIPT_DIR}/OVMF_VARS.fd"
elif [ -f "/usr/share/edk2/ovmf/OVMF_CODE.fd" ]; then
    # Fedora/RHEL edk2-ovmf package
    OVMF_CODE="/usr/share/edk2/ovmf/OVMF_CODE.fd"
    SPLIT_PFLASH=1
    echo -e "${GREEN}Using system OVMF (Fedora/RHEL)${NC}"
    if [ ! -f "${SCRIPT_DIR}/OVMF_VARS.fd" ]; then
        if [ -f "/usr/share/edk2/ovmf/OVMF_VARS.fd" ]; then
            cp "/usr/share/edk2/ovmf/OVMF_VARS.fd" "${SCRIPT_DIR}/OVMF_VARS.fd"
        else
            dd if=/dev/zero of="${SCRIPT_DIR}/OVMF_VARS.fd" bs=1K count=128 2>/dev/null
        fi
    fi
    OVMF_VARS="${SCRIPT_DIR}/OVMF_VARS.fd"
elif [ -f "/usr/share/ovmf/OVMF.fd" ]; then
    # Some distros have combined image here
    OVMF_CODE="/usr/share/ovmf/OVMF.fd"
    echo -e "${GREEN}Using system OVMF.fd${NC}"
else
    echo -e "${RED}ERROR: OVMF firmware not found!${NC}"
    echo -e "${YELLOW}Please download OVMF.fd from:${NC}"
    echo -e "${YELLOW}https://github.com/tianocore/edk2/releases${NC}"
    echo ""
    echo -e "${YELLOW}Or on Ubuntu/Debian:${NC}"
    echo -e "  ${GREEN}sudo apt-get install ovmf${NC}"
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

# Launch QEMU with UEFI firmware
# -machine q35,usb=off uses the Q35 chipset which includes HPET, LPC
# (for i8042 PS/2), and other devices OVMF expects. usb=off disables
# the implicit USB tablet so mouse events are routed to the PS/2
# controller (IRQ12) instead.
#
# Using pflash instead of -bios properly maps the firmware flash region
# and eliminates "Invalid read at addr 0xFFC00000" errors from OVMF
# trying to access unmapped flash addresses.
#
# Split pflash images (OVMF_CODE + OVMF_VARS) need two pflash drives:
# unit 0 for code (read-only) and unit 1 for variables (read-write).

cd "${SCRIPT_DIR}"

if [ "$SPLIT_PFLASH" = "1" ]; then
    echo -e "${CYAN}Using split pflash: CODE + VARS${NC}"
    qemu-system-x86_64 \
        -machine q35,usb=off \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="${OVMF_CODE}" \
        -drive if=pflash,format=raw,unit=1,file="${OVMF_VARS}" \
        -drive file=fat:rw:ESP,format=raw \
        -netdev user,id=net0 \
        -device e1000,netdev=net0 \
        -m 1024M \
        -vga std \
        -display gtk \
        -vnc :0 \
        -serial stdio \
        -no-reboot
else
    echo -e "${CYAN}Using combined pflash: OVMF.fd${NC}"
    qemu-system-x86_64 \
        -machine q35,usb=off \
        -drive if=pflash,format=raw,readonly=on,file="${OVMF_CODE}" \
        -drive file=fat:rw:ESP,format=raw \
        -netdev user,id=net0 \
        -device e1000,netdev=net0 \
        -m 1024M \
        -vga std \
        -display gtk \
        -vnc :0 \
        -serial stdio \
        -no-reboot
fi

echo ""
echo -e "${CYAN}QEMU exited${NC}"
