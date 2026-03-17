# guideXOS Kernel Build System
#
# Copyright (c) 2024 guideX
#

# Detect OS for cross-platform compatibility
ifeq ($(OS),Windows_NT)
    MKDIR = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
    RM = if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
else
    MKDIR = mkdir -p $1
    RM = rm -rf $1
endif

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

# Kernel binary outputs
KERNEL_PE := $(BIN_DIR)/kernel.pe
KERNEL := $(BIN_DIR)/kernel.elf

# Architecture-specific settings (sets CXX, AS, LD, and ARCH_CFLAGS)
include kernel/arch/$(ARCH)/Makefile.arch

# Common compiler flags
CFLAGS := -std=c++14 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
CFLAGS += -nostdlib -nostdinc++ -fno-builtin
CFLAGS += -Ikernel/core/include -Ikernel/arch/$(ARCH)/include
# Append architecture-specific flags
CFLAGS += $(ARCH_CFLAGS)

# Linker flags - link to PE format first, then convert to ELF64
LDFLAGS := -T kernel/arch/$(ARCH)/linker.ld -nostdlib $(ARCH_LDFLAGS)

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

# Step 1: Link to PE format
$(KERNEL_PE): $(ALL_OBJS)
	@$(call MKDIR,$(BIN_DIR))
	@echo "Linking $(ARCH) kernel (PE format)..."
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

# Step 2: Convert PE to ELF64 format
# PE RVAs need to be adjusted by image base (0x100000) when converting to ELF
$(KERNEL): $(KERNEL_PE)
	@echo "Converting to ELF64 format..."
	objcopy -I pei-x86-64 -O elf64-x86-64 \
		--change-section-address .boot+0x100000 \
		--change-section-address .text+0 \
		--change-section-address .rdata+0 \
		--change-section-address .data+0 \
		--change-section-address .bss+0 \
		--change-section-address .reloc+0 \
		--set-start 0x100000 $< $@
	@echo "Built: $@"

$(OBJ_DIR)/core/%.o: kernel/core/%.cpp
	@$(call MKDIR,$(OBJ_DIR)/core)
	@echo "Compiling $<..."
	$(CXX) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/arch/%.o: kernel/arch/$(ARCH)/%.cpp
	@$(call MKDIR,$(OBJ_DIR)/arch)
	@echo "Compiling $<..."
	$(CXX) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/arch/%.o: kernel/arch/$(ARCH)/%.asm
	@$(call MKDIR,$(OBJ_DIR)/arch)
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/arch/%.o: kernel/arch/$(ARCH)/%.s
	@$(call MKDIR,$(OBJ_DIR)/arch)
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

clean:
	@$(call RM,build/$(ARCH))

info:
	@echo "Architecture: $(ARCH)"
	@echo "Compiler: $(CXX)"
	@echo "Assembler: $(AS)"
	@echo "Linker: $(LD)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Kernel output: $(KERNEL)"
