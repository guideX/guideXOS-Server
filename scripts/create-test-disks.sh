#!/bin/bash
#
# create-test-disks.sh
# Creates FAT32 and ext4 test disk images for guideXOS filesystem testing
#
# Usage: sudo ./create-test-disks.sh
#
# Requirements:
#   - Linux with root privileges (for mount)
#   - dosfstools (mkfs.fat)
#   - e2fsprogs (mke2fs)
#
# Copyright (c) 2025 guideXOS Server
#

set -e

# Configuration
DISK_SIZE_MB=64
DISK_DIR="$(dirname "$0")/../disks"
MOUNT_POINT="/tmp/guidexos-testdisk"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
check_root() {
    if [ "$EUID" -ne 0 ]; then
        echo_error "Please run as root (sudo ./create-test-disks.sh)"
        exit 1
    fi
}

# Ensure sbin directories are in PATH (needed for mkfs.* tools when using sudo)
export PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin"

# Check for required tools
check_dependencies() {
    echo_info "Checking dependencies..."
    
    local missing=0
    
    # Check for mkfs.fat (could also be mkfs.vfat on some systems)
    if ! command -v mkfs.fat &> /dev/null && ! command -v mkfs.vfat &> /dev/null; then
        echo_error "mkfs.fat not found. Install: sudo apt install dosfstools"
        missing=1
    fi
    
    if ! command -v mke2fs &> /dev/null; then
        echo_error "mke2fs not found. Install: sudo apt install e2fsprogs"
        missing=1
    fi
    
    if ! command -v dd &> /dev/null; then
        echo_error "dd not found. This should be part of coreutils."
        missing=1
    fi
    
    if [ $missing -eq 1 ]; then
        exit 1
    fi
    
    echo_info "All dependencies found."
}

# Create directory structure
setup_directories() {
    echo_info "Setting up directories..."
    
    mkdir -p "$DISK_DIR"
    mkdir -p "$MOUNT_POINT"
}

# Create test files content
create_test_content() {
    local mount_path="$1"
    
    echo_info "Creating test files..."
    
    # Root level test file
    echo "Hello from guideXOS filesystem test!" > "$mount_path/test.txt"
    echo "This file tests basic root directory reading." >> "$mount_path/test.txt"
    
    # Create apps directory
    mkdir -p "$mount_path/apps"
    
    # Text file in subdirectory
    echo "This file is in the /apps subdirectory." > "$mount_path/apps/hello.txt"
    echo "Testing subdirectory traversal." >> "$mount_path/apps/hello.txt"
    
    # Small binary file (1KB) with known pattern
    dd if=/dev/urandom of="$mount_path/apps/test.bin" bs=1024 count=1 2>/dev/null
    
    # Large binary file (1MB) for multi-cluster reading
    dd if=/dev/urandom of="$mount_path/apps/large.bin" bs=1024 count=1024 2>/dev/null
    
    # Create a mock .gxapp file (just a text file for now)
    cat > "$mount_path/apps/sample.gxapp" << 'EOF'
GXAPP
VERSION: 1
CREATED: 2025-01-01T00:00:00Z
GENERATOR: create-test-disks.sh

This is a mock gxapp file for testing file reading.
In production, this would be a ZIP archive containing:
- metadata.json
- bin/x86/app.elf
- bin/amd64/app.elf
EOF

    # Create nested directory structure
    mkdir -p "$mount_path/deep/nested/directory"
    echo "Deep nested file" > "$mount_path/deep/nested/directory/deep.txt"
    
    # Create a file with special characters in content (but not name)
    echo "Special chars: äöü ñ © ® ™" > "$mount_path/special.txt"
    
    # Create an empty file
    touch "$mount_path/empty.txt"
    
    # Create a README
    cat > "$mount_path/README.txt" << 'EOF'
guideXOS Filesystem Test Disk
==============================

This disk image contains test files for validating the guideXOS
Virtual Filesystem (VFS) layer.

Directory Structure:
  /test.txt           - Simple text file (root)
  /empty.txt          - Empty file
  /special.txt        - File with special characters
  /README.txt         - This file
  /apps/              - Application directory
  /apps/hello.txt     - Text file in subdirectory
  /apps/test.bin      - 1KB binary file
  /apps/large.bin     - 1MB binary file
  /apps/sample.gxapp  - Mock gxapp package
  /deep/nested/...    - Deeply nested directory

Test Scenarios:
  1. Mount filesystem
  2. Read /test.txt
  3. List /apps directory
  4. Read /apps/hello.txt (subdirectory)
  5. Read /apps/large.bin (multi-cluster)
  6. Traverse deep directories

EOF

    echo_info "Test files created."
}

# Create FAT32 disk image
create_fat32_image() {
    local image_path="$DISK_DIR/test-fat32.img"
    
    echo_info "Creating FAT32 disk image ($DISK_SIZE_MB MB)..."
    
    # Remove existing image
    rm -f "$image_path"
    
    # Create empty image
    dd if=/dev/zero of="$image_path" bs=1M count=$DISK_SIZE_MB 2>/dev/null
    
    # Format as FAT32 (try mkfs.fat first, then mkfs.vfat)
    if command -v mkfs.fat &> /dev/null; then
        mkfs.fat -F 32 -n "GXOSTEST" "$image_path"
    else
        mkfs.vfat -F 32 -n "GXOSTEST" "$image_path"
    fi
    
    # Mount the image
    mount -o loop "$image_path" "$MOUNT_POINT"
    
    # Create test content
    create_test_content "$MOUNT_POINT"
    
    # Calculate SHA256 of test.bin for verification
    sha256sum "$MOUNT_POINT/apps/test.bin" > "$DISK_DIR/test-fat32-checksums.txt"
    sha256sum "$MOUNT_POINT/apps/large.bin" >> "$DISK_DIR/test-fat32-checksums.txt"
    
    # Sync and unmount
    sync
    umount "$MOUNT_POINT"
    
    echo_info "FAT32 image created: $image_path"
    ls -lh "$image_path"
}

# Create ext4 disk image
create_ext4_image() {
    local image_path="$DISK_DIR/test-ext4.img"
    
    echo_info "Creating ext4 disk image ($DISK_SIZE_MB MB)..."
    
    # Remove existing image
    rm -f "$image_path"
    
    # Create empty image
    dd if=/dev/zero of="$image_path" bs=1M count=$DISK_SIZE_MB 2>/dev/null
    
    # Format as ext4 (with minimal reserved blocks for testing)
    mke2fs -t ext4 -L "GXOSTEST" -m 0 "$image_path"
    
    # Mount the image
    mount -o loop "$image_path" "$MOUNT_POINT"
    
    # Create test content
    create_test_content "$MOUNT_POINT"
    
    # Calculate SHA256 of test.bin for verification
    sha256sum "$MOUNT_POINT/apps/test.bin" > "$DISK_DIR/test-ext4-checksums.txt"
    sha256sum "$MOUNT_POINT/apps/large.bin" >> "$DISK_DIR/test-ext4-checksums.txt"
    
    # Sync and unmount
    sync
    umount "$MOUNT_POINT"
    
    echo_info "ext4 image created: $image_path"
    ls -lh "$image_path"
}

# Cleanup function
cleanup() {
    # Ensure mount point is unmounted
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    
    # Remove mount point directory
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}

# Main execution
main() {
    echo "=============================================="
    echo "guideXOS Test Disk Image Creator"
    echo "=============================================="
    echo ""
    
    # Set trap for cleanup
    trap cleanup EXIT
    
    check_root
    check_dependencies
    setup_directories
    
    create_fat32_image
    echo ""
    create_ext4_image
    
    echo ""
    echo "=============================================="
    echo_info "All disk images created successfully!"
    echo ""
    echo "Images location: $DISK_DIR"
    echo ""
    echo "Files created:"
    ls -lh "$DISK_DIR"/*.img 2>/dev/null || true
    echo ""
    echo "To use with QEMU:"
    echo "  qemu-system-x86_64 -drive file=$DISK_DIR/test-fat32.img,format=raw,if=ide"
    echo ""
    echo "Or run: ./scripts/run-qemu-fs-test.sh"
    echo "=============================================="
}

main "$@"
