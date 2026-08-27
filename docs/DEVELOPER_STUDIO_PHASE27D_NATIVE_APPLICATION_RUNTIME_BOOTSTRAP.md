# Developer Studio Phase 27D — Native Application Runtime Bootstrap

Phase 27D extends the Phase 27C bare-metal compiler and NativeElf execution
proof into the first bounded application runtime contract. Source is still
read, compiled, linked into a small ELF64 image, written and reopened through
the guest VFS, validated, mapped, executed, and torn down by the running
AMD64 kernel.

> Phase 27D is a trusted NativeElf runtime bootstrap operating in kernel-owned
> context. It is not yet userspace isolation or a complete process model.

## Boundary

This phase deliberately does not add ring-3 execution, privilege separation,
scheduling, concurrency, arbitrary syscalls, fork/exec, arguments,
environment variables, file descriptors, dynamic linking, shared libraries,
PIE, relocations, general C/C++, GUI application support, debugger support, or
Developer Studio Build/Run integration. There is one reusable application
context and one small bare-metal host service.

## Dedicated application stack

The kernel handoff stack remains at the bootloader-provided low-memory
location. NativeElf applications instead use a deterministic 64 KiB stack in
the tail of the bootloader-reserved, identity-mapped NativeElf window:

```text
base: 0x00000000101F0000
top:  0x0000000010200000
size: 65536 bytes
```

The loader's maximum image mapping is 1 MiB, ending at `0x10100000`, so the
stack cannot overlap the generated image. The stack is cleared before every
invocation and after every invocation. It is writable and non-executable only
during preparation/execution, then cleared and made non-writable and
non-executable during teardown. A guard page is not used in this bounded
bootstrap; adding one requires a page-table contract change and remains future
work.

The stack top is aligned down to 16 bytes. The runtime records the kernel RSP
before invocation, the application entry RSP, and the kernel RSP after return.

## Stack switching trampoline

`kernel/arch/amd64/native_elf_trampoline.asm` provides the explicit AMD64
trampoline. It saves the Microsoft x64 nonvolatile GPRs (`RBX`, `RBP`, `RSI`,
`RDI`, `R12`–`R15`) and `XMM6`–`XMM15` in a fixed kernel-stack frame, retains
the four incoming arguments, switches to the application stack, reserves the
32-byte Microsoft home area, calls the generated entry with the context in
`RCX`, captures `EAX`, restores the exact kernel RSP and saved state, and
returns a success flag. The generated host call reserves `0x28` bytes: 32
bytes of home space plus 8 bytes of call-alignment padding.

## Runtime context and lifecycle

`NativeAppExecutionContext` is a single reusable structure containing image
base/size, entry point, read-only data range, stack range, the SDK context and
host table, result, host-log observation, RSP diagnostics, error text, and the
explicit state:

```text
Empty → Loaded → Prepared → Running → Returned → Cleaned
                                      ↘ Failed → Cleaned/Failed
```

Preparation validates the ELF and maps the bounded image. The runtime then
prepares and clears the stack, constructs the SDK context, invokes through the
trampoline, records the result, and deterministically clears the image and
stack. Context pointers and image/stack ranges are invalidated after cleanup.
The image and stack are reused; repeated execution does not allocate an
unbounded resource.

## `gx_app_context` and host table

The runtime uses the established SDK declarations in
`sdk/include/guidexos/app.h` and `abi.h`:

```cpp
struct gx_app_context {
    uint32_t size;
    uint32_t apiVersion;
    const gx_host_calls* host;
    void* userData;
};
```

The runtime initializes `size`, `apiVersion`, `host`, and `userData` with
kernel-owned values. The host table initializes its `size` and `version`, and
populates only `log` and `get_api_version`. All other host function pointers
are zero. Compile-time assertions lock the context host/user-data offsets and
the host-table log/version offsets used by the AMD64 backend.

## First host service

The implemented service is the SDK logging callback:

```cpp
gx_result (GX_CALL *log)(gx_app_context* ctx, const char* message);
```

The bare-metal callback validates that execution is active, that the context,
host table, and user data are the current runtime-owned objects, and that the
message pointer lies within the current ELF read-only data range. It requires a
NUL terminator within the range and caps the readable string at 255 bytes. A
valid message is emitted through the existing serial output path. Invalid
context, null, outside-range, or unterminated inputs return an error without
dereferencing the message.

## Compiler and IR extension

The intentionally constrained compiler now accepts one optional host-log
statement before the existing integer return:

```cpp
int gx_main(gx_app_context* ctx) {
    log(ctx, "Hello from guideXOS!");
    return 42;
}
```

The lexer adds bounded string literals and commas. Printable ASCII plus `\n`,
`\\`, and `\"` escapes are supported; malformed and over-255-byte literals
are rejected. The parser lowers the statement into explicit target-neutral
`FunctionIR` fields (`usesAppContext`, `hasHostLog`, and the decoded bounded
message), followed by `returnConstant`. The parser does not emit machine code.

The AMD64 backend emits the real operation sequence: load the source-derived
string address into `RDX`, load `ctx->host` from the SDK-derived offset, load
`host->log` from its SDK-derived offset, reserve Microsoft ABI call space, call
through the function pointer, restore the stack, and emit the integer return.
For the Phase 27D fixtures the host-call body is:

```text
48 BA <read-only-data-address>   mov rdx, imm64
48 8B 41 08                     mov rax, [rcx+8]
48 8B 40 08                     mov rax, [rax+8]
48 83 EC 28                     sub rsp, 0x28
FF D0                           call rax
48 83 C4 28                     add rsp, 0x28
B8 <return-constant> C3         return constant
```

## ELF layout and permissions

Legacy return-only programs retain the Phase 27C one-segment layout. A
host-log program has two page-aligned `PT_LOAD` segments:

```text
PT_LOAD RX: file offset 0x0000, image address 0x10000000,
           file/memory size 0x1000 + generated code bytes
PT_LOAD R:  file offset 0x2000, image address 0x10002000,
           file/memory size decoded string bytes including NUL
```

The first segment contains ELF metadata, deterministic padding, and code. The
second contains source-derived read-only data and is never executable. The
loader rejects malformed segments, addresses outside the reserved NativeElf
window, unsupported layouts, and files over the 12 KiB bootstrap limit. Image
pages are temporarily writable and non-executable while populated, then use
the segment permissions. No application image page is left RWX.

## Teardown and fault policy

After normal return the trampoline has restored the kernel stack. The loader
clears the image and stack, restores non-writable/non-executable permissions,
zeros the SDK structures, invalidates runtime ranges, records the integer
result and diagnostics, and enters `Cleaned`.

There is no fault recovery or exception-return path yet. The AMD64 page-fault,
general-protection, and invalid-opcode handlers detect an active NativeElf
runtime and print an application-fault diagnostic before preserving the
existing halt behavior. A generated application fault can therefore be
distinguished in serial output, but it is not safely recoverable in this
phase.

## Proof strategy

The guest smoke compiles and runs three source variants:

```text
d27a: Hello from guideXOS!                   → 42
d27b: Developer Studio native build works!  → 42
d27c: Hello from guideXOS!                   → 41
```

It compares source, ELF, and read-only-data hashes, observes the real host
log callback, checks dedicated-stack and app-context diagnostics, repeats the
compile/run lifecycle, checks invalid host-call pointer ranges, and verifies
that the generated ELF remains readable in the guest VFS after execution.
The QEMU harness requires every Phase 27B, Phase 27C, and Phase 27D marker.
Because the default boot harness VFS is memory-backed during a boot, exact
generated ELF bytes are also emitted over the serial proof channel and
reconstructed by the host harness for independent `readelf`/`objdump` audit.

## Validation evidence

The focused host checks passed:

```text
scripts/run-compiler-bootstrap-host-test.ps1
  compiler_bootstrap_host_test: PASS
scripts/run-native-elf-runtime-host-test.ps1
  native_elf_runtime_host_test: PASS
scripts/run-native-elf-host-test.ps1
  native_elf_validator_host_test: PASS
scripts/run-native-abi-layout-test.ps1
  Native ABI layout test PASS
```

The final QEMU command was:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 -BootCount 3 -TimeoutSeconds 90
```

All three fresh boots passed the Phase 27B, Phase 27C, and Phase 27D marker
sets. The external audit reconstructed guest-generated `r42.elf` and `d27a.elf`:
`r42.elf` has one RX load and `d27a.elf` has an RX load plus an R-only data
load at file offset `0x2000`; `objdump` shows the expected context/host/log
call sequence at `0x10001000`. Physical hardware was not tested.

## Security and architectural limitations

This is trusted kernel-owned execution. The application shares the kernel
address space and is not isolated from the kernel. Pointer validation is only
the bounded current-image read-only-data check; it is not general user-pointer
validation. The compiler grammar is tiny, AMD64-only, and has no linker,
relocations, dynamic linking, shared libraries, or GUI ABI. Only one host
service is implemented.

## Phase 27E boundary

The next bounded phase should connect Developer Studio's existing Build
controller to the new bare-metal compiler/runtime service, so Build can invoke
the guideXOS compiler path instead of the hosted PowerShell/LLVM path. That
integration is intentionally not implemented in Phase 27D.
