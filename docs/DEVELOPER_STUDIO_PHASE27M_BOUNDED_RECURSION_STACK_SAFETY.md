# Developer Studio Phase 27M — Bounded Recursion and Stack Safety

Phase 27M makes direct and mutual recursion legal in the bootstrap compiler.
Recursion is supported only within the bounded runtime call-stack policy; it
is not arbitrary or unbounded recursion.

## NativeElf stack contract

The NativeElf window is `IMAGE_BASE=0x10000000` with
`REGION_SIZE=0x00200000`. The dedicated application stack is the half-open
range `[0x101f0000, 0x10200000)`: low address `0x101f0000`, high address
`0x10200000`, exactly 65,536 bytes (64 KiB). The loader passes the high
address to the trampoline. The trampoline aligns it, reserves 32-byte
Microsoft x64 shadow space, and performs the target call, so target-entry RSP
is `0x101fffd8` (`stackTop - 0x28`). The target therefore enters with the
required ABI alignment, and the reported application RSP is checked against
the same half-open stack range.

The loader records the kernel RSP before and after invocation and restores the
reusable application image and stack during teardown. The stack has no
hardware guard page dependency; the software depth guard is the primary
recursion defense.

## Activation cost and derived depth

The conservative maximum generated activation includes:

```text
return address                         8 bytes
saved RBP                              8 bytes
maximum aligned generated frame      448 bytes
maximum expression push/pop transient 128 bytes
maximum outgoing call reserve          40 bytes
                                      -----
                                      632 bytes
```

The 448-byte frame is derived from the legal compiler maxima:
`40 + 4*4 parameter bytes + 32*4 local bytes + 64*4 temporary-slot bytes`,
rounded to a 16-byte boundary. The 128-byte transient bound is
`COMPILER_MAX_EXPRESSION_NESTING * 8`; the emitter tracks actual transient
push depth and rejects code that exceeds it. The 40-byte outgoing reserve is
32-byte shadow space plus the worst required alignment padding. Host calls
use this same bounded reserve but do not increment the source-defined call
depth. Nested source calls do increment it.

The runtime reserve is 8,192 bytes. The configured maximum total generated
activation depth is derived, not guessed:

```text
floor((65536 - 8192 - 40) / 632) = 90
```

`gx_main` is activation depth 1. Every source-defined call increments the
depth before its `E8 rel32` call and decrements it in the callee epilogue.
Thus `recurse(88)` reaches total depth 90 and succeeds; the next activation
is rejected. `recurse(89)` and the excessive-depth fixture fail at the
software guard before the deeper call executes. Static assertions prove that
90 activations fit the stack model and 91 do not.

## Guard and private runtime state

Generated applications reserve two Microsoft x64 nonvolatile registers:

* `R14D` is the current generated activation depth. The entry emitter sets it
  to 1, each source-defined call emits `cmp r14d, 90; jae failure; inc r14d`,
  and each function epilogue emits `dec r14d`.
* `R15D` is zero on success and becomes the sticky failure depth (90) when a
  guard trips. This lets all active frames unwind normally while retaining the
  runtime failure signal for the trampoline.

The guard is emitted around every source-defined call, including nonrecursive
helper calls. It is not limited to edges classified as recursive. No guard
page fault, deliberate trap, RSP corruption, kernel jump, or application exit
integer is used as the primary failure mechanism.

The direct-recursion fixture contains a real backward `E8 rel32` self-call;
the mutual fixture contains both forward and backward `E8 rel32` edges. The
compiler emits one function body per source function and does not unroll
recursion.

## Trampoline and runtime status

The invocation trampoline initializes the private registers before entering
the application, captures the return value, status, and sticky failure depth
after return, and restores the exact kernel RSP. It saves/restores all
Microsoft x64 nonvolatile GPRs (`RBX`, `RBP`, `RSI`, `RDI`, `R12`–`R15`) and
`XMM6`–`XMM15`. The existing trampoline result prefix is unchanged; the
append-only result fields are:

```text
NativeElfTrampolineResult.runtimeStatus     offset 16, uint32-compatible enum
NativeElfTrampolineResult.runtimeCallDepth offset 20, uint32
sizeof(NativeElfTrampolineResult) = 24
```

The distinct status is `NativeRuntimeStatus::CallDepthExceeded`. The loader
copies it into `NativeElfRunReport`, emits:

```text
ELF Loader: Application terminated: recursive call depth limit exceeded.
```

and tears down the application before returning failure. Developer Studio
maps that status to `RunErrorCode::CallDepthExceeded`; it does not present the
failure as an ordinary signed application exit. The runtime status and depth
are cleared with the reusable application context before the next run.

## Call-graph policy

Phase 27L rejected call-graph cycles by design. Phase 27M retains the bounded
call graph and classifies direct and mutual cycles as recursive SCCs. The
compiler now permits those SCCs because runtime protection exists. Unknown
functions, argument-count checks, duplicate symbols, parameter limits, the
`gx_main` call restriction, and all other non-recursive validation remain.
The old `phase27l_recursion_rejected` expectation was retired; current proof
coverage uses `phase27m_recursion_policy_migrated`.

## Proof fixtures and recovery

The Phase 27M proof covers direct recursion (`sum_down(6) * 2 = 42`), local
and parameter isolation, mutual recursion, `if`, `while`, nested helper
calls, recursive call expressions, the safe boundary, one-beyond-boundary
failure, and an excessive request of one million recursive calls. The failure
path marks `CallDepthExceeded`, skips the unsafe call, unwinds through normal
generated epilogues, restores the trampoline/kernel stack, and completes
loader teardown. The same boot then rebuilds and runs the valid recursive
program successfully, and repeats it without losing call-budget state.

Developer Studio’s end-to-end fixture exercises edit, Run, Save All, build,
NativeElf launch, runtime diagnostic, source edit, safe failure, recovery,
and repeated runs. The generated recursive artifact is retained long enough
for VFS and disassembly evidence checks.

The focused QEMU harness mode is
`scripts/smoke-compiler-bootstrap.ps1 -BootCount 3 -TimeoutSeconds 600 -Phase27MOnly`.
It runs the baseline compiler and NativeElf runtime checks plus the complete
Phase 27M suite on three fresh guest images, without making optional earlier
Developer Studio IDE suites a gate for this recursion-specific proof.

## Resource limits

The relevant fixed limits are:

```text
source bytes                 65536
tokens                        2048
functions                       16
integer parameters/function     4
call expressions/function      32
call argument nodes            128
call-graph edges               128
expression nodes             1024
expression nesting              16
locals/function                 32
temporary slots/function        64
statements/function            256
blocks/function                 32
loop nesting                     8
generated code bytes         24576
bootstrap ELF bytes          32768
loader ELF bytes            262144
NativeElf application stack  65536
runtime safety reserve         8192
maximum activation cost         632
maximum total runtime depth      90
```

The language remains one source file, integer-only, direct-call-only, and
AMD64-only. It has no prototypes, arrays, general pointers, function
pointers, indirect source calls, multi-file linking, or full C/C++ semantics.
The runtime is trusted kernel-owned code with a reusable 64 KiB application
stack and no general process isolation. Debugger attachment is outside this
phase.

The separate known Developer Studio close/freeze issue was intentionally not
changed; it was not a recursion-safety blocker.
