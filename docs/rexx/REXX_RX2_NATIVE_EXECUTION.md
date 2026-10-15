# guideXOS Server REXX — RX2 Native Execution

## Result

**Outcome A — Native REXX accepted.** The AMD64 kernel linked the complete C interpreter, the hosted RX1 regressions passed, and two isolated UEFI/QEMU boots reached the guideXOS desktop. The second boot ran the requested `.rex` smoke test plus nonzero EXIT, syntax-error, instruction-budget, and post-error responsiveness checks in the Console.

## Starting state

- Repository: `D:\dev\guideXOSServer_REXX`
- Branch: `REXX_SUPPORT`
- Starting HEAD: `51130dd2687819b78cc7a38c307f5bf744f27c17`
- Worktree: clean at preflight; no branch switch, reset, stash, or push.
- Root `.phase`: absent at preflight and left absent. RX2 evidence is kept in this REXX-specific document.
- The pinned Mbed TLS dependency was not present at preflight. The documented bootstrap installed it under the ignored `third_party/mbedtls` directory so the repository's kernel build could run. The dependency was not added to the commit.

## Native integration

The AMD64 Makefile now compiles these interpreter modules as C11 objects and links them into `kernel.elf`: lexer, parser, compiler, VM, built-ins, numeric adapter, output adapter, VFS adapter, and CLI. `rexx_main.c` remains a hosted test driver and is not linked into the kernel. Other `ARCH` builds do not add the REXX C objects and retain the weak missing-engine path.

The kernel VFS already exposed C ABI open/size/read/close functions. The native adapter now uses those functions and the kernel heap. The kernel runtime supplies the missing C allocation calls and `memchr`; `realloc` copies only the requested allocation length, and allocation-size arithmetic is checked. No libc symbols were stubbed to make the link pass.

The shell bridge calls `guide_rexx_main_with_output` with an invocation-local host context. It resolves relative script paths against the invoking shell's current directory, retains quoted arguments and the `--` form, sends REXX output and diagnostics to that shell's output surface, and propagates the interpreter's process-style status to `$?`. The output interface has separate per-run byte counters, default bounds of 64 KiB for stdout and 4 KiB for stderr, and propagates writer failures to the VM/CLI.

Hosted builds retain the C runtime's `strtod`, `snprintf("%.15g")`, and stdout/stderr behavior. Freestanding builds use the small `rexx_numeric` implementation for decimal conversion, finite/integral checks, and 15-significant-digit formatting. RX2 does not change the interpreter's `double` numeric model.

### Initial diagnostics and final native link

The first bare-metal C compile exposed standard-library dependencies in `rexx_vm.c`, `rexx_builtin.c`, and `rexx_cli.c`: implicit `strtod`, `snprintf`, `fwrite`, `fprintf`, `fputs`, and `fflush` declarations, plus missing `stdout`, `stderr`, and `EOF`. The first manual link also reported missing allocation and memory symbols (`malloc`, `calloc`, `realloc`, `free`, `memcpy`, `memset`, `memcmp`, and `memchr`) and MinGW CRT/math references from those same hosted code paths. These were replaced with explicit host adapters and kernel-provided runtime functionality.

The first full kernel compile stopped in existing C++ sources at `app_model_limits.h:3` with `fatal error: cstddef: No such file or directory`; the kernel Makefile uses `-nostdinc++`. The successful build kept that flag and supplied this toolchain's C++ header directories through `EXTRA_CFLAGS`:

```powershell
mingw32-make -C kernel -j4 ARCH=amd64 `
  BUILD_DIR=build/rexx-rx2-amd64 `
  OBJ_DIR=build/rexx-rx2-amd64/obj `
  BIN_DIR=build/rexx-rx2-amd64/bin `
  KERNEL=build/rexx-rx2-amd64/bin/kernel.elf `
  KERNEL_PE=build/rexx-rx2-amd64/bin/kernel.pe `
  EXTRA_CFLAGS="-IC:/mingw64/include/c++/15.2.0 -IC:/mingw64/include/c++/15.2.0/x86_64-w64-mingw32"
```

The command completed with exit code 0, linked `kernel.pe`, converted it to ELF64, and passed the pinned Mbed TLS profile check. The resulting ELF is 2,737,640 bytes. `nm` on the linked PE confirmed definitions for `guide_rexx_main_with_output`, `shell_cmd_rexx`, `guidexos_rexx_vfs_open`, `malloc`, and `realloc`.

The root `guideXOSServer.vcxproj` now lists the C interpreter sources with `CompileAsC` and includes the shell bridge and public headers. Its XML parsed successfully. The full Visual Studio host application project was not built; the native kernel qualification used the AMD64 GNU/MinGW kernel path.

## Regression and adapter tests

Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_rexx_tests.ps1` successfully:

- RX1 runtime: 11/11 assertions.
- RX1 CLI: 16/16 cases.
- RX2 VFS/output/CLI integration: 45 assertions, including empty and partial reads, missing/open/size/read/close failures, zero-progress reads, the 16 MiB cap, embedded NUL, per-invocation output routing, bounded output, writer failure, and instruction-budget exhaustion.
- RX2 freestanding numeric adapter: 18 assertions.
- RX2 C++ shell bridge: 7 assertions covering quoted paths, `--`, per-shell output callbacks, and path-resolution errors.
- Regina differential cases were unavailable because `regina` is not installed.

The hosted suite's intent and original 11/11 and 16/16 checks were preserved.

## QEMU guest evidence

The UEFI bootloader was built into ignored scratch output with Visual Studio MSBuild using `Release|x64`, an isolated `OutDir`, and an isolated `IntDir`. Our QEMU runs used the isolated copy of the existing `ESP` under `tmp/rexx-rx2-native-guest-20261014/ESP`, with the new kernel ELF, bootloader, and test scripts. The project's existing ESP, disk images, and another project's active QEMU guest were left untouched. The acceptance boot used this isolated configuration (with paths shortened here):

```text
qemu-system-x86_64.exe
  -machine pc,accel=whpx,usb=off
  -drive if=pflash,format=raw,readonly=on,file=OVMF.fd
  -drive file=fat:rw:<temporary ESP>,format=raw,if=ide
  -m 1024M -smp 1 -vga std -display none
  -serial file:<acceptance-serial.log>
  -qmp tcp:127.0.0.1:4447,server=on,wait=off
  -net none -no-reboot -no-shutdown
```

A second guest boot was completed for the requested acceptance script. The first boot printed the RX2 smoke output in the guest shell; the second boot ran the full positive/negative sequence below.

On the second boot, the actual shell commands were:

```text
rexx hello.rex
echo $?
rexx nonzero.rex
echo $?
rexx invalid.rex
echo $?
rexx budget.rex
echo $?
echo guest-responsive
```

The guest-visible Console showed:

```text
Hello from guideXOS REXX!
Arithmetic and IF: OK
0
7
rexx: /invalid.rex:1:4: syntax error: expected an expression
1
rexx: /budget.rex: runtime error at instruction 1: instruction budget exhausted
1
guest-responsive
```

This verifies visible `SAY`, arithmetic and `IF`, successful `EXIT 0`, nonzero `EXIT 7`, a visible invalid-script diagnostic, bounded-loop termination, and execution of another shell command afterward. The serial log separately records VFS opens for `/hello.rex`, `/nonzero.rex`, `/invalid.rex`, and `/budget.rex`, plus entry into the kernel main loop. A first guest boot also ran `rexx rx2` and visibly printed `RX2 native guest says hello` and `42`.

![Guest Console acceptance screenshot](../../tmp/rexx-rx2-native-guest-20261014/rx2-required-guest-acceptance.png)

The screenshot and serial/build logs are retained under the ignored `tmp/rexx-rx2-native-guest-20261014` directory. The unrelated active QEMU instance identified during work used `D:\dev\guideXOSServer\out\validation\settings-s12-20261014\production\guest-final`; it was not sent commands, stopped, or modified.

## Changed files

- `kernel/Makefile`: AMD64 C object list, C11 flags, dependency files, and link inclusion.
- `kernel/core/cxx_runtime.cpp`: C heap API, allocation-size tracking, overflow handling, and `memchr`.
- `kernel/core/vfs.cpp` required no changes; its existing C ABI was linked directly.
- `kernel/core/shell.cpp`, `kernel/core/include/kernel/shell.h`, `kernel/shell/rexx_shell_cmd.cpp`, and `.h`: Console output, relative paths, per-run host, and exact status propagation.
- `rexx_numeric.c/.h` and `rexx_output.c/.h`: freestanding numeric and bounded output abstractions.
- `rexx_vm.c/.h`, `rexx_builtin.c`, `rexx_cli.c/.h`: VM output callback, numeric adapter use, and invocation-local CLI output.
- `guideXOSServer.vcxproj`: Visual Studio C-source and header entries.
- `tests/run_rexx_tests.ps1` and three RX2 test drivers: VFS/output/numeric/shell bridge acceptance.

## Known limits and RX3 recommendation

RX2 keeps the existing partial language implementation: only the currently registered built-ins and implemented opcodes work; unsupported operations still return a clear VM error. Native numeric behavior remains binary `double` with the existing 15-digit rendering policy. The hosted Windows/Linux code path remains available, but this work does not establish a full MSVC host-project build or Regina differential run.

For RX3, first define and test the compatibility surface for classic decimal behavior (`NUMERIC DIGITS`, `FUZZ`, `FORM`, decimal arithmetic and comparisons) with Regina/reference cases, then implement that model as a separate language-runtime milestone. Keep unrestricted `ADDRESS SHELL` out of scope until a separate bounded command-dispatch policy is specified.
