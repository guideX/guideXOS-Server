#!/bin/bash
#
# run-qemu-fs-test.sh
# Launches QEMU with test disk images attached for filesystem testing
#
# This script boots guideXOS via UEFI (same as run-uefi.sh) but also
# attaches the test FAT32 and ext4 disk images for filesystem testing.
#
# Usage: ./run-qemu-fs-test.sh [options]
#
# Options:
#   --fat32     Only attach FAT32 disk
#   --ext4      Only attach ext4 disk
#   --debug     Enable QEMU debug output
#   --gdb       Wait for GDB connection on port 1234
#
# Copyright (c) 2025 guideXOS Server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DISK_DIR="$PROJECT_DIR/disks"
ESP_DIR="$PROJECT_DIR/ESP"

# Default options
USE_FAT32=true
USE_EXT4=true
DEBUG=false
WAIT_GDB=false
MEMORY="1024M"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --fat32)
            USE_FAT32=true
            USE_EXT4=false
            shift
            ;;
        --ext4)
            USE_FAT32=false
            USE_EXT4=true
            shift
            ;;
        --debug)
            DEBUG=true
            shift
            ;;
        --gdb)
            WAIT_GDB=true
            shift
            ;;
        --memory)
            MEMORY="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --fat32     Only attach FAT32 disk"
            echo "  --ext4      Only attach ext4 disk"
            echo "  --debug     Enable QEMU debug output"
            echo "  --gdb       Wait for GDB connection on port 1234"
            echo "  --memory N  Set memory size (default: 1024M)"
            echo "  -h, --help  Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Find OVMF firmware
find_ovmf() {
    # Check local OVMF.fd first
    if [ -f "$PROJECT_DIR/OVMF.fd" ]; then
        echo "$PROJECT_DIR/OVMF.fd"
        return 0
    fi
    
    # Check common system locations
    local candidates=(
        "/usr/share/OVMF/OVMF_CODE.fd"
        "/usr/share/edk2/ovmf/OVMF_CODE.fd"
        "/usr/share/qemu/OVMF.fd"
        "/usr/share/ovmf/OVMF.fd"
    )
    
    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    
    return 1
}

# Check for disk images
check_disks() {
    local missing=0
    
    if $USE_FAT32 && [ ! -f "$DISK_DIR/test-fat32.img" ]; then
        echo "ERROR: FAT32 test disk not found: $DISK_DIR/test-fat32.img"
        echo "Run: sudo ./scripts/create-test-disks.sh"
        missing=1
    fi
    
    if $USE_EXT4 && [ ! -f "$DISK_DIR/test-ext4.img" ]; then
        echo "ERROR: ext4 test disk not found: $DISK_DIR/test-ext4.img"
        echo "Run: sudo ./scripts/create-test-disks.sh"
        missing=1
    fi
    
    return $missing
}

# Check ESP directory
check_esp() {
    if [ ! -d "$ESP_DIR" ]; then
        echo "ERROR: ESP directory not found: $ESP_DIR"
        echo "Run: ./build.ps1 or make to build the kernel and bootloader"
        return 1
    fi
    
    if [ ! -f "$ESP_DIR/kernel.elf" ]; then
        echo "WARNING: kernel.elf not found in ESP"
        echo "The bootloader will run but may not have a kernel to boot."
    fi
    
    return 0
}
    
    # GDB support
    if $WAIT_GDB; then
        cmd="$cmd -s -S"
        echo "Waiting for GDB connection on localhost:1234..."
    fi
    
    echo "$cmd"
}

# Main
main() {
    echo "=============================================="
    echo "guideXOS Filesystem Test - QEMU Launcher"
    echo "=============================================="
    echo ""
    
    # Check for QEMU
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        echo "ERROR: qemu-system-x86_64 not found"
        echo "Install: sudo apt install qemu-system-x86"
        exit 1
    fi
    
    # Find OVMF firmware
    OVMF=$(find_ovmf)
    if [ -z "$OVMF" ]; then
        echo "ERROR: UEFI firmware (OVMF) not found"
        echo "Install: sudo apt install ovmf"
        echo "Or download OVMF.fd to: $PROJECT_DIR"
        exit 1
    fi
    echo "Using UEFI: $OVMF"
    
    # Check ESP directory
    if ! check_esp; then
        exit 1
    fi
    echo "Using ESP: $ESP_DIR"
    
    # Check disk images
    if ! check_disks; then
        exit 1
    fi
    
    # Show disk info
    echo ""
    echo "Attached test disks:"
    if $USE_FAT32; then
        echo "  SATA Port 0: $DISK_DIR/test-fat32.img (FAT32)"
    fi
    if $USE_EXT4; then
        echo "  SATA Port 1: $DISK_DIR/test-ext4.img (ext4)"
    fi
    echo ""
    
    # Build QEMU command
    QEMU_ARGS=(
        "-machine" "pc"
        "-drive" "if=pflash,format=raw,readonly=on,file=$OVMF"
        "-drive" "file=fat:rw:$ESP_DIR,format=raw,if=ide,index=0"
    )
    
    # Add test disk images as IDE drives (compatible with legacy ATA driver)
    if $USE_FAT32 && [ -f "$DISK_DIR/test-fat32.img" ]; then
        QEMU_ARGS+=("-drive" "file=$DISK_DIR/test-fat32.img,format=raw,if=ide,index=1,media=disk")
    fi
    
    if $USE_EXT4 && [ -f "$DISK_DIR/test-ext4.img" ]; then
        QEMU_ARGS+=("-drive" "file=$DISK_DIR/test-ext4.img,format=raw,if=ide,index=2,media=disk")
    fi
    
    # Memory and display
    QEMU_ARGS+=("-m" "$MEMORY")
    QEMU_ARGS+=("-vga" "std")
    QEMU_ARGS+=("-serial" "stdio")
    QEMU_ARGS+=("-no-reboot")
    
    # Debug options
    if $DEBUG; then
        QEMU_ARGS+=("-d" "int,cpu_reset" "-D" "qemu-debug.log")
        echo "Debug output will be written to qemu-debug.log"
    fi
    
    # GDB support
    if $WAIT_GDB; then
        QEMU_ARGS+=("-s" "-S")
        echo "Waiting for GDB connection on localhost:1234..."
    fi
    
    echo "=============================================="
    echo "Filesystem Testing Quick Reference:"
    echo "  The test disks are attached as IDE drives."
    echo "  In the shell, use these commands:"
    echo "    vfstest        - Run filesystem diagnostics"
    echo "    vfsmount / 1   - Mount FAT32 disk (device 1) at /"
    echo "    vfsls          - List files in mounted filesystem"
    echo "    vfscat /test.txt - Read a file"
    echo ""
    echo "  Mouse: Ctrl+Alt+G to grab/release"
    echo "  Exit:  Close window or Ctrl+C in terminal"
    echo "=============================================="
    echo ""
    
    # Change to project directory
    cd "$PROJECT_DIR"
    
    # Run QEMU
    qemu-system-x86_64 "${QEMU_ARGS[@]}"
}

main "$@"
