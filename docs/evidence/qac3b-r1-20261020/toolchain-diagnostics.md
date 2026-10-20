# QAC3B-R1 x86 toolchain diagnostics

Captured on LEON2_RDT on 2026-10-20 before the repaired build.

## Compiler and sysroot

- Build compiler: `C:\i686-elf-tools\bin\i686-elf-g++.exe` (found on `PATH`; none of the three hard-coded optional toolchain directories in `kernel/build-x86.bat` existed).
- Companion C compiler: `C:\i686-elf-tools\bin\i686-elf-gcc.exe`.
- Version: GCC/G++ 15.2.0.
- Target triplet: `i686-elf`.
- Configure options include `--host=i686-w64-mingw32.static --target=i686-elf --enable-languages=c,c++ --without-headers`.
- `-print-sysroot` returns an empty string.
- Preprocessor search paths are the GCC 15.2.0 internal `include` and `include-fixed` directories. The target `i686-elf/sys-include` and `i686-elf/include` directories do not exist. The internal directories also do not provide `string.h`.

The compiler is an intentional freestanding cross-compiler, not a hosted toolchain with a broken installation. Its lack of a target C runtime/sysroot is consistent with `--without-headers`.

## Project include configuration

`kernel/build-x86.bat` already passes `-Icore/include` alongside the kernel and architecture include directories. The project owns other freestanding headers there, including `stddef.h`-dependent headers, but had no project `string.h`. Three existing sources included `<string.h>`: `core/framebuffer.cpp`, `core/system_font.cpp`, and `core/kernel_apps.cpp`. Those includes predate QAC3B integration (file history dates from May–July 2026).

The referenced runtime symbols are already implemented in `core/cxx_runtime.cpp` within `extern "C"`: `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `strcmp`, `strncmp`, `strchr`, `strrchr`, and `strstr`. The included sources currently call `memcpy`, `strcmp`, and `strstr`.

## Finding and correction

Root cause: a missing project-owned freestanding declaration header. The build include path is correct; there is no target libc/sysroot to supply hosted headers, and adding one is unnecessary. The build must remain freestanding and link only the existing kernel runtime implementations.

The correction adds `kernel/core/include/string.h` with C-linkage declarations matching those existing implementations and the target `size_t` ABI. It adds no implementation, libc, Windows/MSVC dependency, compiler suppression, or hosted link input.

The post-change preprocessor check used the launcher's freestanding flags and include paths. It resolved `<string.h>` to `core/include/string.h` and `<stddef.h>` to GCC's internal target compiler header; it exited 0.

## Build path and additional freestanding prerequisites

The first integrated retry exposed a second defect in the old hand-written `kernel/build-x86.bat`: its parenthesized Mbed TLS CFLAGS block expanded `%CFLAGS%` before earlier `set` statements in that block took effect, dropping the dependency include paths. After correcting that temporarily, the script compiled only `core/*.cpp` and linked with `i686-elf-ld` directly. That path omitted architecture-specific x86 objects, the shared settings presentation source, the pinned Mbed TLS/TF-PSA-Crypto sources, and the target `libgcc` runtime. The existing canonical `kernel/makefile` already contains the complete source manifest and links with the `i686-elf-g++` driver and `-lgcc`.

`kernel/build-x86.bat` now retains local compiler/NASM discovery and invokes `mingw32-make.exe ARCH=x86` (or `make.exe` when available) from the kernel directory. It does not clean or remove existing outputs. This routes the documented build-and-run launcher through the repository's complete build configuration rather than maintaining a second incomplete object list.

The canonical build then exposed two more missing freestanding/profile prerequisites:

- The pinned Mbed TLS profile includes sources that require `<assert.h>`, while the project had no such header and the target has no hosted C library. `kernel/core/include/assert.h` now implements the standard `NDEBUG` behavior and traps on a failed assertion with `__builtin_trap()`; it adds no hosted I/O or `abort()` dependency.
- `guidexos/crypto_config.h` configured the project's existing no-op `snprintf` and `vsnprintf` callbacks as `MBEDTLS_PLATFORM_STD_*` functions but omitted the matching `MBEDTLS_PLATFORM_*_ALT` hooks required by the pinned TF-PSA-Crypto configuration checks. The two alternate-platform macros were added, and `guidexos/mbedtls_profile.json` was updated to the normalized SHA-256 of the changed tracked overlay. The profile verifier passed; no dependency source or diagnostic was suppressed.

## Build result

The full canonical x86 build completed with exit code 0 using GCC 15.2.0, `ARCH=x86`, the freestanding flags, pinned Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 profile, and target `-lgcc`. The full build log is preserved at `E:\QAC3B-R1\kernel-make-build4.log`; it contains 0 errors and 14 warnings (13 compiler warnings plus the linker's RWX LOAD-segment warning). No hosted C/C++ libraries are linked. `i686-elf-nm -u` reports no unresolved symbols.

The first real guest run reached `kernel_main` and reported `No valid boot method detected`. The first inspection found the x86 assembly argument order was reversed for `kernel_main(void* boot_environment, uint32_t boot_magic)`; cdecl requires pushing the second parameter first. That order was corrected, but a second live run still failed the boot-method check. Further inspection found `_start` loaded the segment selector with `mov ax, 0x10` before passing EAX, overwriting the low 16 bits of the Multiboot magic. The final entry code copies EAX to ECX before segment setup, then pushes ECX (`boot_magic`) followed by EBX (`boot_environment`). The final disassembly shows `mov %eax,%ecx`, `mov $0x10,%ax`, `push %ecx`, `push %ebx`, and `call kernel_main`.

The final integrated build and live run used `kernel/build/x86/bin/kernel.elf`: 2,478,204 bytes, SHA-256 `3566D010B087DD9309593ACAF59D6CF2CD48B15BCBDBC0C39F207AFB0774FC00`, ELF32 `EXEC`, machine Intel 80386, entry point `0x10000c`, and no undefined symbols. The latest canonical link emitted the existing RWX LOAD-segment warning; no hosted libraries were added. The artifact remains freestanding; `-print-sysroot` is still empty by design. The full canonical build log is preserved at `E:\QAC3B-R1\kernel-make-build4.log`; the final integrated launcher output and guest evidence are recorded in `live-qac-evidence.md`.
