# Quick Build & Run Makefile
# Simplified interface for common tasks
#
# Copyright (c) 2024 guideX

.PHONY: help build run clean test qemu x86 amd64

# Default target
help:
	@echo "=========================================="
	@echo "guideXOS Kernel - Quick Commands"
	@echo "=========================================="
	@echo ""
	@echo "Common commands:"
	@echo "  make x86       Build x86 kernel"
	@echo "  make qemu      Build and run in QEMU"
	@echo "  make test      Test build system"
	@echo "  make clean     Clean build files"
	@echo ""
	@echo "Architecture-specific:"
	@echo "  make x86       Build 32-bit x86"
	@echo "  make amd64     Build 64-bit x86-64"
	@echo ""
	@echo "Advanced:"
	@echo "  make run       Run last built kernel"
	@echo "  make debug     Build with debug symbols"
	@echo ""

# Build x86 kernel
x86:
	@echo "Building x86 kernel..."
	cd kernel && $(MAKE) ARCH=x86

# Build AMD64 kernel
amd64:
	@echo "Building AMD64 kernel..."
	cd kernel && $(MAKE) ARCH=amd64

# Build and run in QEMU (x86)
qemu: x86
	@echo "Launching QEMU..."
	@bash scripts/run-qemu-x86.sh || cmd.exe /c scripts\\run-qemu-x86.bat

# Just run (without rebuilding)
run:
	@echo "Launching QEMU..."
	@bash scripts/run-qemu-x86.sh || cmd.exe /c scripts\\run-qemu-x86.bat

# Test build system
test:
	@echo "Testing build system..."
	@bash scripts/test-build.sh || cmd.exe /c scripts\\test-build.bat

# Clean all architectures
clean:
	@echo "Cleaning build files..."
	cd kernel && $(MAKE) ARCH=x86 clean
	cd kernel && $(MAKE) ARCH=amd64 clean
	@echo "Done!"

# Debug build (x86)
debug:
	@echo "Building x86 kernel with debug symbols..."
	cd kernel && $(MAKE) ARCH=x86 CFLAGS="-g -O0 -DDEBUG"

# Build alias
build: x86
