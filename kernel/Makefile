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
    ifeq ($(ARCH),riscv64)
        ARCH := riscv64
    endif
endif

# Validate architecture
VALID_ARCHS := amd64 x86 arm arm64 ia64 sparc sparc64 riscv64 ppc64 loongarch64 mips64
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
include arch/$(ARCH)/Makefile.arch
CC ?= $(subst g++,gcc,$(CXX))

MBEDTLS_GUIDEXOS_ENABLED := 0
MBEDTLS_GUIDEXOS_COMPILE_PROBE_ENABLED := 0
MBEDTLS_GUIDEXOS_LINK_ENABLED := 0
MBEDTLS_GUIDEXOS_MANIFEST := ../third_party/mbedtls/guidexos/mbedtls_sources.mk
MBEDTLS_GUIDEXOS_VERSION_HEADER := ../third_party/mbedtls/include/mbedtls/version.h
ifneq ($(wildcard $(MBEDTLS_GUIDEXOS_MANIFEST)),)
include $(MBEDTLS_GUIDEXOS_MANIFEST)
ifeq ($(MBEDTLS_GUIDEXOS_COMPILE_READY),1)
MBEDTLS_GUIDEXOS_COMPILE_PROBE_ENABLED := 1
MBEDTLS_GUIDEXOS_ENABLED := 1
endif
ifeq ($(MBEDTLS_GUIDEXOS_LINK_READY),1)
MBEDTLS_GUIDEXOS_LINK_ENABLED := 1
endif
endif

MBEDTLS_GUIDEXOS_IMPORT_STATE_DEPS := \
	../third_party/mbedtls \
	../third_party/mbedtls/guidexos \
	../third_party/mbedtls/include \
	../third_party/mbedtls/include/mbedtls \
	../third_party/mbedtls/library \
	../third_party/mbedtls/tf-psa-crypto

# Common compiler flags
CFLAGS := -std=c++14 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
CFLAGS += -nostdlib -nostdinc++ -fno-builtin
CFLAGS += -DGXOS_BARE_METAL
CFLAGS += -I. -I.. -Icore/include -Iarch/$(ARCH)/include
CFLAGS += $(EXTRA_CFLAGS)
ifneq ($(MBEDTLS_GUIDEXOS_COMPILE_PROBE_ENABLED),0)
CFLAGS += $(MBEDTLS_GUIDEXOS_CFLAGS)
endif
ifneq ($(wildcard mbedtls-guidexos-defines.h),)
CFLAGS += -imacros mbedtls-guidexos-defines.h
endif
DEPFLAGS := -MMD -MP
# Append architecture-specific flags
CFLAGS += $(ARCH_CFLAGS)

# Linker flags
# LINKER_SCRIPT_FLAG is defined per-arch: compiler-driver arches (x86) use -Wl,-T,
# while plain ld arches (amd64) use -T directly.
# Compiler-driver-only flags like -ffreestanding/-nostdlib live in ARCH_LDFLAGS.
LDFLAGS := $(LINKER_SCRIPT_FLAG)arch/$(ARCH)/linker.ld $(ARCH_LDFLAGS)
# LIBS is set per-arch in Makefile.arch

# Source files
CORE_SOURCES := $(wildcard core/*.cpp)
ARCH_SOURCES := $(wildcard arch/$(ARCH)/*.cpp)
ASM_SOURCES := $(wildcard arch/$(ARCH)/*.asm) $(wildcard arch/$(ARCH)/*.s)
MBEDTLS_C_SOURCES := $(if $(filter 1,$(MBEDTLS_GUIDEXOS_LINK_ENABLED)),$(MBEDTLS_GUIDEXOS_C_SOURCES))
MBEDTLS_COMPILE_PROBE_C_SOURCES := $(if $(filter 1,$(MBEDTLS_GUIDEXOS_COMPILE_PROBE_ENABLED)),$(MBEDTLS_GUIDEXOS_COMPILE_PROBE_C_SOURCES))

# Object files
CORE_OBJS := $(patsubst core/%.cpp,$(OBJ_DIR)/core/%.o,$(CORE_SOURCES))
ARCH_OBJS := $(patsubst arch/$(ARCH)/%.cpp,$(OBJ_DIR)/arch/%.o,$(ARCH_SOURCES))
ASM_OBJS := $(patsubst arch/$(ARCH)/%.asm,$(OBJ_DIR)/arch/%.o,$(ASM_SOURCES))
ASM_OBJS += $(patsubst arch/$(ARCH)/%.s,$(OBJ_DIR)/arch/%.o,$(filter %.s,$(ASM_SOURCES)))
MBEDTLS_C_OBJS := $(patsubst ../third_party/%.c,$(OBJ_DIR)/third_party/%.o,$(MBEDTLS_C_SOURCES))
MBEDTLS_COMPILE_PROBE_C_OBJS := $(patsubst ../third_party/%.c,$(OBJ_DIR)/third_party/%.o,$(MBEDTLS_COMPILE_PROBE_C_SOURCES))

ALL_OBJS := $(CORE_OBJS) $(ARCH_OBJS) $(ASM_OBJS) $(MBEDTLS_C_OBJS)
DEPFILES := $(CORE_OBJS:.o=.d) $(ARCH_OBJS:.o=.d) $(MBEDTLS_COMPILE_PROBE_C_OBJS:.o=.d)

# Targets
.PHONY: all clean info mbedtls-guidexos-compile-probe

all: mbedtls-guidexos-compile-probe $(KERNEL)

ifneq ($(MBEDTLS_GUIDEXOS_COMPILE_PROBE_ENABLED),0)
mbedtls-guidexos-compile-probe: $(MBEDTLS_COMPILE_PROBE_C_OBJS)
	@echo "Mbed TLS/TF-PSA freestanding compile probe complete."
else
mbedtls-guidexos-compile-probe:
endif

# ----------------------------------------------------------------
# Link step - architecture-dependent
#
# amd64 (MinGW): link to PE, then convert PE to ELF64 with objcopy
# Everything else: linker outputs native ELF directly
# ----------------------------------------------------------------

ifeq ($(ARCH),amd64)

# Step 1: Link to PE format (amd64 / MinGW)
$(KERNEL_PE): $(ALL_OBJS)
	@$(call MKDIR,$(BIN_DIR))
	@echo "Linking $(ARCH) kernel (PE format)..."
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

# Step 2: Convert PE to ELF64 format
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

else
# All other architectures: direct ELF link

$(KERNEL): $(ALL_OBJS)
	@$(call MKDIR,$(BIN_DIR))
	@echo "Linking $(ARCH) kernel..."
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Built: $@"

endif

$(OBJ_DIR)/core/%.o: core/%.cpp
	@$(call MKDIR,$(OBJ_DIR)/core)
	@echo "Compiling $<..."
	$(CXX) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/core/gxos_tls_foundation.o: $(MBEDTLS_GUIDEXOS_IMPORT_STATE_DEPS)

$(OBJ_DIR)/core/kernel_apps.o: $(MBEDTLS_GUIDEXOS_IMPORT_STATE_DEPS)

$(OBJ_DIR)/core/%.d: core/%.cpp
	@$(call MKDIR,$(OBJ_DIR)/core)
	@echo "Bootstrapping dependencies for $<..."
	$(CXX) $(CFLAGS) $(DEPFLAGS) -c $< -o $(OBJ_DIR)/core/$*.o

$(OBJ_DIR)/arch/%.o: arch/$(ARCH)/%.cpp
	@$(call MKDIR,$(OBJ_DIR)/arch)
	@echo "Compiling $<..."
	$(CXX) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/arch/%.d: arch/$(ARCH)/%.cpp
	@$(call MKDIR,$(OBJ_DIR)/arch)
	@echo "Bootstrapping dependencies for $<..."
	$(CXX) $(CFLAGS) $(DEPFLAGS) -c $< -o $(OBJ_DIR)/arch/$*.o

$(OBJ_DIR)/arch/%.o: arch/$(ARCH)/%.asm
	@$(call MKDIR,$(OBJ_DIR)/arch)
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/arch/%.o: arch/$(ARCH)/%.s
	@$(call MKDIR,$(OBJ_DIR)/arch)
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/third_party/%.o: ../third_party/%.c
	@$(call MKDIR,$(dir $@))
	@echo "Compiling $<..."
	$(CXX) -x c $(filter-out -std=c++14 -fno-exceptions -fno-rtti -nostdinc++,$(CFLAGS)) $(DEPFLAGS) -c $< -o $@

clean:
	@$(call RM,build/$(ARCH))

info:
	@echo "Architecture: $(ARCH)"
	@echo "Compiler: $(CXX)"
	@echo "C compiler: $(CC)"
	@echo "Assembler: $(AS)"
	@echo "Linker: $(LD)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Kernel output: $(KERNEL)"

-include $(DEPFILES)
