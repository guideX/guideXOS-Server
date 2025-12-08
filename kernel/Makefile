# guideXOS Kernel Build System
#
# Copyright (c) 2024 guideX
#

# Detect architecture if not specified
ifndef ARCH
    ARCH := $(shell uname -m)
    ifeq ($(ARCH),x86_64)
        ARCH := amd64
    endif
    ifeq ($(ARCH),i686)
        ARCH := x86
    endif
    ifeq ($(ARCH),armv7l)
        ARCH := arm
    endif
endif

# Validate architecture
VALID_ARCHS := amd64 x86 arm ia64 sparc
ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
    $(error Invalid ARCH: $(ARCH). Valid options: $(VALID_ARCHS))
endif

# Output directories
BUILD_DIR := build/$(ARCH)
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

# Kernel binary
KERNEL := $(BIN_DIR)/kernel.elf

# Architecture-specific settings
include kernel/arch/$(ARCH)/Makefile.arch

# Common compiler flags
CFLAGS := -std=c++14 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
CFLAGS += -nostdlib -nostdinc++ -fno-builtin
CFLAGS += -Ikernel/core/include -Ikernel/arch/$(ARCH)/include

# Linker flags
LDFLAGS := -T kernel/arch/$(ARCH)/linker.ld -nostdlib

# Source files
CORE_SOURCES := $(wildcard kernel/core/*.cpp)
ARCH_SOURCES := $(wildcard kernel/arch/$(ARCH)/*.cpp)
ASM_SOURCES := $(wildcard kernel/arch/$(ARCH)/*.asm) $(wildcard kernel/arch/$(ARCH)/*.s)

# Object files
CORE_OBJS := $(patsubst kernel/core/%.cpp,$(OBJ_DIR)/core/%.o,$(CORE_SOURCES))
ARCH_OBJS := $(patsubst kernel/arch/$(ARCH)/%.cpp,$(OBJ_DIR)/arch/%.o,$(ARCH_SOURCES))
ASM_OBJS := $(patsubst kernel/arch/$(ARCH)/%.asm,$(OBJ_DIR)/arch/%.o,$(ASM_SOURCES))
ASM_OBJS += $(patsubst kernel/arch/$(ARCH)/%.s,$(OBJ_DIR)/arch/%.o,$(filter %.s,$(ASM_SOURCES)))

ALL_OBJS := $(CORE_OBJS) $(ARCH_OBJS) $(ASM_OBJS)

# Targets
.PHONY: all clean info

all: $(KERNEL)

$(KERNEL): $(ALL_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $(ARCH) kernel..."
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Built: $@"

$(OBJ_DIR)/core/%.o: kernel/core/%.cpp
	@mkdir -p $(OBJ_DIR)/core
	@echo "Compiling $<..."
	$(CXX) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/arch/%.o: kernel/arch/$(ARCH)/%.cpp
	@mkdir -p $(OBJ_DIR)/arch
	@echo "Compiling $<..."
	$(CXX) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/arch/%.o: kernel/arch/$(ARCH)/%.asm
	@mkdir -p $(OBJ_DIR)/arch
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/arch/%.o: kernel/arch/$(ARCH)/%.s
	@mkdir -p $(OBJ_DIR)/arch
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -rf build/$(ARCH)

info:
	@echo "Architecture: $(ARCH)"
	@echo "Compiler: $(CXX)"
	@echo "Assembler: $(AS)"
	@echo "Linker: $(LD)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Kernel output: $(KERNEL)"
