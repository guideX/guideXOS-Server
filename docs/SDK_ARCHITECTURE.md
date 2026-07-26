================================================================================
guideXOS Developer SDK - Architecture & Implementation Guide
================================================================================

Version: 1.0
Last Updated: 2025
Phase: 8b

This document provides detailed technical guidance for implementing the
guideXOS Developer SDK, including cross-compilation toolchain setup,
musl libc integration, and developer workflow.

================================================================================
TABLE OF CONTENTS
================================================================================

1. SDK Overview
2. Directory Structure
3. Cross-Compilation Toolchain
4. musl libc Integration
5. System Call Interface
6. C Runtime (crt0)
7. libguidexos Implementation
8. Build System Integration
9. Developer Workflow
10. Testing & Validation

================================================================================
1. SDK OVERVIEW
================================================================================

The guideXOS SDK enables developers to build applications that run on guideXOS.
It provides:

- Cross-compilers for all target architectures
- musl libc (POSIX-compatible C library)
- libguidexos (native guideXOS API)
- gxbuild tool (build and package applications)
- Headers and documentation

Design Principles:
- Static linking by default (self-contained binaries)
- POSIX compatibility for easy porting
- Native API for full guideXOS features
- Single command builds for multiple architectures

Experimental hosted Native ELF execution status:
- Execution is experimental and disabled in normal `build.bat` builds.
- `build-native-experimental.bat` enables execution for trusted local hosted-runtime validation.
- Supported: amd64 host, amd64 app, static ET_EXEC, no PT_INTERP, no dynamic linking, no relocations, preferred base maps successfully, guideXOS C ABI v1 (`guidexos-c-abi-v1`) only.
- Unsupported: ET_DYN/PIE, shared libraries, libc-heavy apps, cross-architecture execution, dynamic linker, and arbitrary host filesystem access.
- Available host calls: log, get_api_version, request_window, draw_text, draw_rect, wait_for_close, poll_event, file_exists, file_read_all, request_window_ex, file_read, present_frame, get_ticks_ms, and the hosted-development workspace calls file_stat, file_read_workspace, file_list, file_write_all, file_create_directory, and file_remove. The last two are exact-path, non-recursive directory/file operations for Developer Studio project generation and rollback; they are not a bare-metal filesystem contract.

================================================================================
2. DIRECTORY STRUCTURE
================================================================================

Installation Location: /opt/guidexos-sdk/

/opt/guidexos-sdk/
?
??? bin/                              # Developer tools
?   ??? gxbuild                       # Build and package tool
?   ??? gxpackage                     # Package creation (standalone)
?   ??? gxrun                         # Run applications (QEMU wrapper)
?   ??? gxinfo                        # Display SDK information
?
??? include/                          # Header files
?   ??? guidexos/                     # Native guideXOS headers
?   ?   ??? types.h                   # Basic types
?   ?   ??? syscall.h                 # System call interface
?   ?   ??? syscall_numbers.h         # Syscall number definitions
?   ?   ??? process.h                 # Process management
?   ?   ??? fs.h                      # Filesystem API
?   ?   ??? net.h                     # Networking API
?   ?   ??? ipc.h                     # Inter-process communication
?   ?   ??? memory.h                  # Memory management
?   ?   ??? time.h                    # Time functions
?   ?   ??? error.h                   # Error codes
?   ?
?   ??? c/                            # POSIX/C headers (from musl)
?       ??? stdio.h
?       ??? stdlib.h
?       ??? string.h
?       ??? unistd.h
?       ??? fcntl.h
?       ??? errno.h
?       ??? sys/
?       ?   ??? types.h
?       ?   ??? stat.h
?       ?   ??? socket.h
?       ?   ??? ...
?       ??? ...
?
??? lib/                              # Architecture-specific libraries
?   ??? x86/
?   ?   ??? libc.a                    # musl libc (static)
?   ?   ??? libguidexos.a             # guideXOS native API
?   ?   ??? crt0.o                    # C runtime startup
?   ?   ??? crti.o                    # Init section
?   ?   ??? crtn.o                    # Fini section
?   ?
?   ??? amd64/
?   ?   ??? libc.a
?   ?   ??? libguidexos.a
?   ?   ??? crt0.o
?   ?   ??? crti.o
?   ?   ??? crtn.o
?   ?
?   ??? riscv64/
?       ??? libc.a
?       ??? libguidexos.a
?       ??? crt0.o
?       ??? crti.o
?       ??? crtn.o
?
??? toolchain/                        # Cross-compiler wrappers
?   ??? x86-guidexos-cc              # C compiler wrapper
?   ??? x86-guidexos-c++             # C++ compiler wrapper
?   ??? x86-guidexos-ld              # Linker wrapper
?   ??? x86-guidexos-ar              # Archiver wrapper
?   ??? amd64-guidexos-cc
?   ??? amd64-guidexos-c++
?   ??? amd64-guidexos-ld
?   ??? amd64-guidexos-ar
?   ??? riscv64-guidexos-cc
?   ??? riscv64-guidexos-c++
?   ??? riscv64-guidexos-ld
?   ??? riscv64-guidexos-ar
?
??? share/
?   ??? templates/                    # Project templates
?   ?   ??? hello/                    # Hello World template
?   ?   ??? cli-app/                  # CLI application template
?   ?   ??? gui-app/                  # GUI application template
?   ?
?   ??? examples/                     # Example applications
?   ?   ??? hello/
?   ?   ??? echo-server/
?   ?   ??? file-copy/
?   ?   ??? calculator/
?   ?
?   ??? docs/                         # Documentation
?       ??? getting-started.md
?       ??? api-reference.md
?       ??? building-apps.md
?       ??? universal-binaries.md
?
??? etc/
    ??? sdk.conf                      # SDK configuration
    ??? targets.json                  # Target architecture definitions

================================================================================
3. CROSS-COMPILATION TOOLCHAIN
================================================================================

3.1 Toolchain Selection
-----------------------

Primary: LLVM/Clang
Rationale:
- Single toolchain supports all target architectures
- Modern, actively maintained
- Good cross-compilation support
- Excellent diagnostics

Fallback: GCC (where Clang has issues)
- Some architectures may need GCC
- IA-64, SPARC may have better GCC support

3.2 Required Components
-----------------------

For each target architecture:

| Component      | Tool              | Purpose                    |
|----------------|-------------------|----------------------------|
| C Compiler     | clang             | Compile C code             |
| C++ Compiler   | clang++           | Compile C++ code           |
| Assembler      | llvm-as / as      | Assemble .s files          |
| Linker         | ld.lld            | Link object files          |
| Archiver       | llvm-ar           | Create static libraries    |
| Objcopy        | llvm-objcopy      | Manipulate object files    |
| Strip          | llvm-strip        | Remove debug symbols       |

3.3 Compiler Wrapper Scripts
----------------------------

Each wrapper script sets the correct flags for the target architecture.

Example: toolchain/amd64-guidexos-cc

#!/bin/bash
# amd64-guidexos-cc - C compiler for guideXOS amd64 target

SDK_ROOT="/opt/guidexos-sdk"
ARCH="amd64"

exec clang \
    --target=x86_64-unknown-elf \
    -ffreestanding \
    -nostdlib \
    -nostdinc \
    -fno-builtin \
    -fno-stack-protector \
    -mno-red-zone \
    -isystem "${SDK_ROOT}/include/c" \
    -isystem "${SDK_ROOT}/include/guidexos" \
    -L "${SDK_ROOT}/lib/${ARCH}" \
    "$@"

Example: toolchain/x86-guidexos-cc

#!/bin/bash
# x86-guidexos-cc - C compiler for guideXOS x86 target

SDK_ROOT="/opt/guidexos-sdk"
ARCH="x86"

exec clang \
    --target=i686-unknown-elf \
    -ffreestanding \
    -nostdlib \
    -nostdinc \
    -fno-builtin \
    -fno-stack-protector \
    -m32 \
    -isystem "${SDK_ROOT}/include/c" \
    -isystem "${SDK_ROOT}/include/guidexos" \
    -L "${SDK_ROOT}/lib/${ARCH}" \
    "$@"

Example: toolchain/riscv64-guidexos-cc

#!/bin/bash
# riscv64-guidexos-cc - C compiler for guideXOS riscv64 target

SDK_ROOT="/opt/guidexos-sdk"
ARCH="riscv64"

exec clang \
    --target=riscv64-unknown-elf \
    -ffreestanding \
    -nostdlib \
    -nostdinc \
    -fno-builtin \
    -march=rv64gc \
    -mabi=lp64d \
    -isystem "${SDK_ROOT}/include/c" \
    -isystem "${SDK_ROOT}/include/guidexos" \
    -L "${SDK_ROOT}/lib/${ARCH}" \
    "$@"

3.4 Linker Configuration
------------------------

Default linker script: lib/<arch>/guidexos.ld

Sections:
- .text    : Executable code (RX)
- .rodata  : Read-only data (R)
- .data    : Initialized data (RW)
- .bss     : Uninitialized data (RW)

Example linker script (amd64):

/* guidexos.ld - guideXOS application linker script */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;  /* User-space base address */

    .text : {
        *(.text.entry)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}

================================================================================
4. MUSL LIBC INTEGRATION
================================================================================

4.1 Why musl?
-------------

- Lightweight and clean implementation
- Full POSIX compliance
- Static linking friendly
- Permissive MIT license
- Supports all target architectures
- Well-documented syscall interface

4.2 Building musl for guideXOS
------------------------------

musl must be modified to use guideXOS syscalls instead of Linux syscalls.

Step 1: Clone musl source

    git clone https://git.musl-libc.org/cgit/musl
    cd musl

Step 2: Modify syscall interface

    File: arch/<arch>/syscall_arch.h

    Replace Linux syscall invocation with guideXOS syscall.

Step 3: Configure for target

    # For amd64
    ./configure \
        --target=x86_64-guidexos \
        --prefix=/opt/guidexos-sdk \
        --libdir=/opt/guidexos-sdk/lib/amd64 \
        --includedir=/opt/guidexos-sdk/include/c \
        --syslibdir=/opt/guidexos-sdk/lib/amd64 \
        --disable-shared \
        CFLAGS="-O2 -fPIC"

Step 4: Build and install

    make
    make install

4.3 Syscall Backend Modification
--------------------------------

File: arch/x86_64/syscall_arch.h (for amd64)

Original (Linux):

    static __inline long __syscall0(long n)
    {
        unsigned long ret;
        __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
        return ret;
    }

Modified (guideXOS):

    // guideXOS uses 'syscall' instruction with different register convention
    // RAX = syscall number
    // RDI, RSI, RDX, R10, R8, R9 = arguments
    // RAX = return value

    static __inline long __syscall0(long n)
    {
        unsigned long ret;
        __asm__ __volatile__ (
            "syscall"
            : "=a"(ret)
            : "a"(n)
            : "rcx", "r11", "memory"
        );
        return ret;
    }

    static __inline long __syscall1(long n, long a1)
    {
        unsigned long ret;
        __asm__ __volatile__ (
            "syscall"
            : "=a"(ret)
            : "a"(n), "D"(a1)
            : "rcx", "r11", "memory"
        );
        return ret;
    }

    // ... similar for __syscall2 through __syscall6

4.4 Syscall Number Mapping
--------------------------

File: arch/x86_64/bits/syscall.h.in

Map POSIX syscalls to guideXOS syscall numbers:

    #define SYS_read        0
    #define SYS_write       1
    #define SYS_open        2
    #define SYS_close       3
    #define SYS_stat        4
    #define SYS_fstat       5
    #define SYS_lseek       6
    #define SYS_mmap        7
    #define SYS_munmap      8
    #define SYS_brk         9
    #define SYS_ioctl       10
    // ... continue for all syscalls

4.5 Missing Functionality
-------------------------

Some Linux-specific features won't exist in guideXOS initially:

| Feature          | Status      | Notes                          |
|------------------|-------------|--------------------------------|
| fork()           | Stub        | Return -ENOSYS, use spawn()    |
| clone()          | Stub        | Return -ENOSYS                 |
| ptrace()         | Stub        | Return -ENOSYS                 |
| signalfd()       | Stub        | Return -ENOSYS                 |
| epoll_*          | Implement   | Important for servers          |
| eventfd()        | Implement   | Useful for IPC                 |

================================================================================
5. SYSTEM CALL INTERFACE
================================================================================

5.1 Syscall ABI
---------------

AMD64:
  Instruction: syscall
  Syscall number: RAX
  Arguments: RDI, RSI, RDX, R10, R8, R9
  Return: RAX (negative = -errno)
  Clobbered: RCX, R11

x86:
  Instruction: int 0x80
  Syscall number: EAX
  Arguments: EBX, ECX, EDX, ESI, EDI, EBP
  Return: EAX (negative = -errno)

RISC-V 64:
  Instruction: ecall
  Syscall number: a7
  Arguments: a0, a1, a2, a3, a4, a5
  Return: a0 (negative = -errno)

5.2 Syscall Numbers
-------------------

File: include/guidexos/syscall_numbers.h

// Process management
#define SYS_exit        0
#define SYS_spawn       1
#define SYS_wait        2
#define SYS_getpid      3
#define SYS_getppid     4
#define SYS_kill        5

// File operations
#define SYS_open        10
#define SYS_close       11
#define SYS_read        12
#define SYS_write       13
#define SYS_lseek       14
#define SYS_stat        15
#define SYS_fstat       16
#define SYS_unlink      17
#define SYS_mkdir       18
#define SYS_rmdir       19
#define SYS_readdir     20
#define SYS_chdir       21
#define SYS_getcwd      22
#define SYS_dup         23
#define SYS_dup2        24
#define SYS_pipe        25
#define SYS_fcntl       26
#define SYS_ioctl       27

// Memory management
#define SYS_mmap        30
#define SYS_munmap      31
#define SYS_mprotect    32
#define SYS_brk         33

// Networking
#define SYS_socket      40
#define SYS_bind        41
#define SYS_listen      42
#define SYS_accept      43
#define SYS_connect     44
#define SYS_send        45
#define SYS_recv        46
#define SYS_sendto      47
#define SYS_recvfrom    48
#define SYS_shutdown    49
#define SYS_setsockopt  50
#define SYS_getsockopt  51

// Time
#define SYS_time        60
#define SYS_gettimeofday 61
#define SYS_nanosleep   62
#define SYS_clock_gettime 63

// IPC
#define SYS_shmget      70
#define SYS_shmat       71
#define SYS_shmdt       72
#define SYS_shmctl      73

// System info
#define SYS_uname       80
#define SYS_getuid      81
#define SYS_getgid      82

5.3 Syscall Wrappers
--------------------

File: include/guidexos/syscall.h

#pragma once

#include "types.h"
#include "syscall_numbers.h"

#ifdef __cplusplus
extern "C" {
#endif

// Low-level syscall interface
long syscall0(long n);
long syscall1(long n, long a1);
long syscall2(long n, long a1, long a2);
long syscall3(long n, long a1, long a2, long a3);
long syscall4(long n, long a1, long a2, long a3, long a4);
long syscall5(long n, long a1, long a2, long a3, long a4, long a5);
long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6);

// High-level wrappers
static inline void gx_exit(int code) {
    syscall1(SYS_exit, code);
    __builtin_unreachable();
}

static inline pid_t gx_spawn(const char* path, const char** argv) {
    return (pid_t)syscall2(SYS_spawn, (long)path, (long)argv);
}

static inline int gx_open(const char* path, int flags, int mode) {
    return (int)syscall3(SYS_open, (long)path, flags, mode);
}

static inline int gx_close(int fd) {
    return (int)syscall1(SYS_close, fd);
}

static inline ssize_t gx_read(int fd, void* buf, size_t count) {
    return (ssize_t)syscall3(SYS_read, fd, (long)buf, count);
}

static inline ssize_t gx_write(int fd, const void* buf, size_t count) {
    return (ssize_t)syscall3(SYS_write, fd, (long)buf, count);
}

// ... additional wrappers for all syscalls

#ifdef __cplusplus
}
#endif

================================================================================
6. C RUNTIME (CRT0)
================================================================================

The C runtime startup code initializes the application and calls main().

6.1 crt0.o (Entry Point)
------------------------

File: lib/amd64/crt0.S

    .section .text.entry
    .global _start
    .type _start, @function

_start:
    # Clear frame pointer for stack traces
    xor %rbp, %rbp

    # argc is in %rdi (passed by kernel)
    # argv is in %rsi
    # envp is in %rdx

    # Align stack to 16 bytes (ABI requirement)
    and $-16, %rsp

    # Call __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)
    # For static linking, init/fini/rtld_fini are handled differently
    
    # Simple path: just call main directly
    # argc already in %rdi
    mov %rsi, %rdi          # argc -> first arg
    mov %rdx, %rsi          # argv -> second arg
    mov %rcx, %rdx          # envp -> third arg

    call main

    # Exit with return value from main
    mov %eax, %edi
    call gx_exit

    # Should never reach here
    ud2

    .size _start, . - _start

6.2 crti.o / crtn.o
-------------------

These provide .init and .fini section bookends for constructors/destructors.

File: lib/amd64/crti.S

    .section .init
    .global _init
    .type _init, @function
_init:
    push %rbp
    mov %rsp, %rbp
    # Constructor code will be inserted here by linker

    .section .fini
    .global _fini
    .type _fini, @function
_fini:
    push %rbp
    mov %rsp, %rbp
    # Destructor code will be inserted here by linker

File: lib/amd64/crtn.S

    .section .init
    pop %rbp
    ret

    .section .fini
    pop %rbp
    ret

================================================================================
7. LIBGUIDEXOS IMPLEMENTATION
================================================================================

libguidexos.a provides the native guideXOS API, independent of POSIX.

7.1 File Structure
------------------

sdk/libguidexos/
??? include/
?   ??? guidexos/
?       ??? process.h
?       ??? fs.h
?       ??? net.h
?       ??? ...
??? src/
?   ??? process.c
?   ??? fs.c
?   ??? net.c
?   ??? syscall_amd64.S
?   ??? syscall_x86.S
?   ??? syscall_riscv64.S
??? Makefile

7.2 Example: process.c
----------------------

#include <guidexos/process.h>
#include <guidexos/syscall.h>

pid_t gx_getpid(void) {
    return (pid_t)syscall0(SYS_getpid);
}

pid_t gx_spawn(const char* path, const char** argv) {
    return (pid_t)syscall2(SYS_spawn, (long)path, (long)argv);
}

int gx_wait(pid_t pid, int* status) {
    return (int)syscall2(SYS_wait, pid, (long)status);
}

void gx_exit(int code) {
    syscall1(SYS_exit, code);
    __builtin_unreachable();
}

7.3 Example: syscall_amd64.S
----------------------------

    .text
    
    .global syscall0
    .type syscall0, @function
syscall0:
    mov %rdi, %rax
    syscall
    ret

    .global syscall1
    .type syscall1, @function
syscall1:
    mov %rdi, %rax
    mov %rsi, %rdi
    syscall
    ret

    .global syscall2
    .type syscall2, @function
syscall2:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    syscall
    ret

    .global syscall3
    .type syscall3, @function
syscall3:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    syscall
    ret

    # ... syscall4, syscall5, syscall6

================================================================================
8. BUILD SYSTEM INTEGRATION
================================================================================

8.1 CMake Support
-----------------

SDK provides CMake toolchain files for easy integration.

File: share/cmake/guidexos-amd64.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(GUIDEXOS_SDK "/opt/guidexos-sdk")

set(CMAKE_C_COMPILER "${GUIDEXOS_SDK}/toolchain/amd64-guidexos-cc")
set(CMAKE_CXX_COMPILER "${GUIDEXOS_SDK}/toolchain/amd64-guidexos-c++")
set(CMAKE_AR "${GUIDEXOS_SDK}/toolchain/amd64-guidexos-ar")
set(CMAKE_LINKER "${GUIDEXOS_SDK}/toolchain/amd64-guidexos-ld")

set(CMAKE_FIND_ROOT_PATH "${GUIDEXOS_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_C_FLAGS_INIT "-ffreestanding -nostdlib")
set(CMAKE_CXX_FLAGS_INIT "-ffreestanding -nostdlib -fno-exceptions -fno-rtti")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -T${GUIDEXOS_SDK}/lib/amd64/guidexos.ld")

Usage:

    cmake -DCMAKE_TOOLCHAIN_FILE=/opt/guidexos-sdk/share/cmake/guidexos-amd64.cmake ..

8.2 pkg-config Support
----------------------

File: lib/pkgconfig/guidexos.pc

prefix=/opt/guidexos-sdk
libdir=${prefix}/lib
includedir=${prefix}/include

Name: guideXOS
Description: guideXOS development libraries
Version: 1.0.0
Cflags: -I${includedir}/guidexos -I${includedir}/c
Libs: -L${libdir}/${arch} -lguidexos -lc

================================================================================
9. DEVELOPER WORKFLOW
================================================================================

9.1 Hello World Example
-----------------------

File: hello.c

#include <stdio.h>

int main(int argc, char** argv) {
    printf("Hello, guideXOS!\n");
    return 0;
}

Build (single architecture):

    gxbuild build hello.c --target amd64 -o hello

Build (multiple architectures):

    gxbuild build hello.c --targets x86,amd64,riscv64 -o hello

Package:

    gxbuild package hello --output hello.gxapp

All-in-one:

    gxbuild build hello.c --targets x86,amd64,riscv64 --package -o hello.gxapp

9.2 Project-Based Build
-----------------------

File: project.gxproj

{
    "name": "hello",
    "version": "1.0.0",
    "type": "executable",
    "sources": ["src/*.c"],
    "include_dirs": ["include"],
    "targets": ["x86", "amd64", "riscv64"],
    "cflags": "-O2 -Wall"
}

Build:

    gxbuild build project.gxproj

9.3 Manual Build (Without gxbuild)
----------------------------------

# Compile
amd64-guidexos-cc -c hello.c -o hello.o

# Link
amd64-guidexos-ld \
    /opt/guidexos-sdk/lib/amd64/crt0.o \
    hello.o \
    -L/opt/guidexos-sdk/lib/amd64 \
    -lc -lguidexos \
    -T/opt/guidexos-sdk/lib/amd64/guidexos.ld \
    -o hello

================================================================================
10. TESTING & VALIDATION
================================================================================

10.1 SDK Self-Tests
-------------------

Location: share/tests/

Tests:
- syscall_test.c      - Verify all syscalls work
- stdio_test.c        - Test printf, fopen, etc.
- memory_test.c       - Test malloc, free
- network_test.c      - Test socket operations
- multiarch_test.c    - Verify same code runs on all archs

Run:

    gxbuild test         # Runs all tests on native
    gxbuild test --arch x86 --qemu    # Run x86 tests in QEMU

10.2 QEMU Testing
-----------------

gxbuild run command automatically uses QEMU for non-native architectures.

QEMU Commands by Architecture:

| Arch     | QEMU Command                                      |
|----------|---------------------------------------------------|
| x86      | qemu-system-i386 -kernel kernel.elf -initrd app   |
| amd64    | qemu-system-x86_64 -kernel kernel.elf -initrd app |
| riscv64  | qemu-system-riscv64 -machine virt -kernel ...     |

10.3 Validation Checklist
-------------------------

Before SDK release:

[_] All syscalls implemented and working
[_] musl compiles for all target architectures
[_] Hello World runs on all targets
[_] printf/sprintf working
[_] File I/O working
[_] Socket operations working
[_] Memory allocation working
[_] C++ basic support (new/delete, but no exceptions)
[_] gxbuild builds for all targets
[_] gxbuild package creates valid .gxapp
[_] Documentation complete

================================================================================
APPENDIX A: TROUBLESHOOTING
================================================================================

Problem: "undefined reference to __stack_chk_fail"
Solution: Add -fno-stack-protector to CFLAGS

Problem: "cannot find -lgcc"
Solution: Use LLVM's compiler-rt instead, or disable built-ins

Problem: Linker errors about missing symbols
Solution: Ensure crt0.o is first, and -lc -lguidexos at end of link

Problem: Application crashes immediately
Solution: Check that crt0 properly sets up stack and calls main

================================================================================
APPENDIX B: ARCHITECTURE-SPECIFIC NOTES
================================================================================

x86 (32-bit):
- Uses int 0x80 for syscalls
- 4-byte pointers
- Stack must be 4-byte aligned

amd64 (64-bit):
- Uses syscall instruction
- Red zone must be disabled (-mno-red-zone)
- Stack must be 16-byte aligned before call

riscv64:
- Uses ecall instruction
- Assumes RV64GC (IMAFD + C extensions)
- Stack must be 16-byte aligned

================================================================================
END OF DOCUMENT
================================================================================
