#!/bin/bash
#
# run-qemu-fs-test.sh
# Launches QEMU with test disk images attached for filesystem testing
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
KERNEL_DIR="$PROJECT_DIR/kernel/build"

# Default options
USE_FAT32=true
USE_EXT4=true
DEBUG=false
WAIT_GDB=false
MEMORY="256M"

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
            echo "  --memory N  Set memory size (default: 256M)"
            echo "  -h, --help  Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Find kernel
find_kernel() {
    # Try various locations
    local candidates=(
        "$KERNEL_DIR/amd64/bin/kernel.elf"
        "$KERNEL_DIR/x86/bin/kernel.elf"
        "$PROJECT_DIR/build/amd64/bin/kernel.elf"
        "$PROJECT_DIR/build/x86/bin/kernel.elf"
        "$PROJECT_DIR/kernel.elf"
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

# Build QEMU command
build_qemu_cmd() {
    local kernel="$1"
    local cmd="qemu-system-x86_64"
    
    # Basic options
    cmd="$cmd -m $MEMORY"
    cmd="$cmd -serial stdio"
    cmd="$cmd -no-reboot"
    cmd="$cmd -no-shutdown"
    
    # Kernel (if ELF, use -kernel; if ISO, use -cdrom)
    if [[ "$kernel" == *.elf ]]; then
        cmd="$cmd -kernel $kernel"
    elif [[ "$kernel" == *.iso ]]; then
        cmd="$cmd -cdrom $kernel"
    fi
    
    # Disk images as IDE drives
    local drive_index=0
    
    if $USE_FAT32 && [ -f "$DISK_DIR/test-fat32.img" ]; then
        cmd="$cmd -drive file=$DISK_DIR/test-fat32.img,format=raw,if=ide,index=$drive_index"
        drive_index=$((drive_index + 1))
    fi
    
    if $USE_EXT4 && [ -f "$DISK_DIR/test-ext4.img" ]; then
        cmd="$cmd -drive file=$DISK_DIR/test-ext4.img,format=raw,if=ide,index=$drive_index"
        drive_index=$((drive_index + 1))
    fi
    
    # Debug options
    if $DEBUG; then
        cmd="$cmd -d int,cpu_reset -D qemu-debug.log"
    fi
    
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
    
    # Check disk images
    if ! check_disks; then
        exit 1
    fi
    
    # Find kernel
    KERNEL=$(find_kernel)
    if [ -z "$KERNEL" ]; then
        echo "WARNING: Kernel not found. QEMU will start without a kernel."
        echo "Build the kernel first: make amd64"
        echo ""
        echo "Continuing anyway (for disk inspection)..."
        KERNEL=""
    else
        echo "Using kernel: $KERNEL"
    fi
    
    # Show disk info
    echo ""
    echo "Attached disks:"
    if $USE_FAT32; then
        echo "  IDE0: $DISK_DIR/test-fat32.img (FAT32)"
    fi
    if $USE_EXT4; then
        echo "  IDE1: $DISK_DIR/test-ext4.img (ext4)"
    fi
    echo ""
    
    # Build and run QEMU command
    QEMU_CMD=$(build_qemu_cmd "$KERNEL")
    
    echo "Running: $QEMU_CMD"
    echo ""
    echo "=============================================="
    echo "Press Ctrl+A, X to exit QEMU"
    echo "=============================================="
    echo ""
    
    # Run QEMU
    eval $QEMU_CMD
}

main "$@"
