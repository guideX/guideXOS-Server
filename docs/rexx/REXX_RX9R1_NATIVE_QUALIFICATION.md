# RX9R1 Condition-Trap and Native Qualification

## Decision and repository state

**Outcome A — RX9 accepted.** RX9R1 closes the supported NOVALUE gaps, keeps
the SYNTAX protection boundaries, builds and links the complete AMD64 kernel,
and boots that exact kernel in an isolated QEMU guest with condition and
recovery evidence.

- Repository: `D:\dev\guideXOSServer_REXX`
- Branch: `REXX_SUPPORT`
- Required starting HEAD: `1be2ededc2f620dd7295a1790bdae2ed0ae25bb4`
- Starting worktree: clean; no RX9R1 artifact or root `.phase` file existed.
- Ending state: focused local RX9R1 commit on `REXX_SUPPORT`; `.phase` remains
  absent. No push, branch switch, merge, rebase, reset, clean, stash, or
  worktree operation was used.
- Production ESP and disk images were not opened. All build and QEMU media
  were isolated copies.

The pre-change hosted baseline passed before implementation. The existing
RX9 condition engine and its CALL/SIGNAL continuation rules were retained.

## NOVALUE implementation

NOVALUE checks now share `load_variable_value()`, which resolves subsidiary
compound names before deciding whether to raise the condition. The covered
read sites are:

- Expression operands, including simple variables and derived compound names.
- A variable named by `PARSE VAR`.
- Dynamic string patterns and positions in parsing templates.
- The control list and resolved entries of `PROCEDURE EXPOSE (name)`.
- The control list and dropped names of the supported `DROP (name)` form.
- Reads through nested and recursive `PROCEDURE EXPOSE` generations.

The compound-tail exemption is preserved. A stem default supplies a missing
tail, and the tail used only to form a compound name is not read as a separate
variable. `CONDITION('D')` receives the resolved name. With NOVALUE OFF, a
missing simple variable retains its existing value-as-name behavior.

PARSE writes are staged in a transparent child environment. Values are
committed only after the entire parse finishes successfully; a NOVALUE transfer
discards the overlay and leaves previous destinations intact. The overlay
looks through the current environment and its aliases, so caller variables and
recursive EXPOSE links remain live. Dynamic `DROP (name)` is a focused addition
to the already supported DROP statement; direct `DROP variable` behavior is
unchanged.

The new fixtures cover PARSE VAR, dynamic template strings and positions,
subsidiary PROCEDURE, live and recursive EXPOSE generations, dynamic DROP,
compound-tail behavior, and the NOVALUE OFF default. Hosted and guest outputs
are in [`evidence/rx9/rx9r1-hosted-final.log`](../../evidence/rx9/rx9r1-hosted-final.log)
and the `guest-novalue-*.png` captures.

## SYNTAX boundary

The supported runtime SYNTAX set continues to map selected language errors:
bad arithmetic conversion (RC 41), division by zero (42), invalid NUMERIC
settings (26), unknown labels (16), unknown routines (43), invalid PROCEDURE
(24), and other mapped language errors (40). The description, RC, SIGL, and
condition state are set before handler dispatch.

Qualification covers runtime conversion and division errors, an invalid
NUMERIC setting, a missing handler, a nested handler failure, compile-time
syntax failure, a successful trapped recovery, instruction-budget exhaustion,
and numeric-work exhaustion. Compile/parse failures happen before execution;
undefined handlers and failures inside handlers do not recursively enter the
same trap. Instruction, numeric-work, stack, table, and allocation boundaries
remain terminal and cannot be caught by SIGNAL ON SYNTAX.

The guest shows division-by-zero recovery as:

```text
SYNTAX
42
2
Arithmetic overflow/underflow
recovered
2
```

The guest also shows conversion recovery as `SYNTAX`, RC `41`, SIGL `2`,
`Bad arithmetic conversion`, and `recovered`. The native instruction-limit
fixture terminates with `VM resource limit exceeded`; the shell then runs a
separate recovery script successfully.

## Condition-trap behavior and synthetic conditions

Guest and hosted checks retain these contracts:

- `SIGNAL` turns the trapped condition OFF, sets SIGL, and transfers without
  resuming the interrupted clause.
- `CALL` enters the handler in DELAY, resumes after RETURN, and does not
  overwrite RESULT with the handler's return string.
- A repeated same-condition error during DELAY is ignored; nested condition
  information and routine state restore on return.
- `CONDITION('C'/'D'/'I'/'S')`, RC, and SIGL are checked in guest and hosted
  fixtures.
- A safe default ADDRESS adapter returns RC 127 when no host callback exists;
  this exercises guest CALL ON ERROR without running a shell command. Hosted
  tests use bounded deterministic callbacks for positive, negative, and zero
  return codes.
- A controlled unknown SIGNAL label remains an untrapped runtime error.

HALT and NOTREADY have no real event sources in this host. Four synthetic
hosted dispatch cases exercise CALL and SIGNAL for both conditions, including
ON/DELAY/OFF transitions, handler entry, condition information, SIGL, and
allocation cleanup. No OS-level HALT or NOTREADY event integration is claimed.

## Freestanding build repair and result

The initial native failure was reproduced in `core/desktop.cpp`. The include
chain was `desktop.cpp` → `kernel_apps.h` → `guide_web_http_shared.h` →
`app_model_limits.h`, where `<cstddef>` failed under the existing
`-nostdinc++` flags. The same header dependency also affected other kernel
translation units. `app_model_limits.h` uses global `size_t` and fixed
constants; a freestanding probe confirmed that `<stddef.h>` supplies the
required declaration without changing the API or values.

The only kernel build-source change is `<cstddef>` → `<stddef.h>` in
`app_model_limits.h`. The build retains `-ffreestanding`, `-nostdlib`,
`-nostdinc++`, `-fno-exceptions`, and `-fno-rtti`; no standard-library path,
module, or broad compiler flag was added.

Command:

```text
mingw32-make -C kernel ARCH=amd64 BUILD_DIR=build/rexx-rx9r1-native-qualification-20261017 all
```

Result: exit 0; all core, shell, AMD64, Mbed TLS, and nine native REXX C
objects compiled and linked. The pinned Mbed TLS profile verified as 4.1.0
with TF-PSA Crypto 1.1.0. The kernel is
[`kernel.elf`](../../kernel/build/rexx-rx9r1-native-qualification-20261017/bin/kernel.elf)
with SHA-256
`9DABFA9AFDCB3AC7EE55DD4E1677A8E9FA839ED93072E5DDA8CF10E9CF0E7B1E`.
`objdump` identifies ELF64 x86-64 with entry `0x100000`.

The PE import table has no DLL imports. The REXX entry points
`guide_rexx_main_with_output`, `rexx_vm_execute`, and `rexx_vm_init` are
unmangled C ABI definitions. The binary has no hosted standard-library,
exception, or RTTI runtime references; the kernel's own allocator shims are
defined in the image. The PE-to-ELF symbol table retains `mainCRTStartup` and
`__dso_handle` marker entries, but the relocation scan finds no references to
them, and the PE import table is empty. The firmware loader entered at
`0x100000`; the guest boot log records the matching entry address.

Full output is preserved in
[`rx9r1-amd64-build.log`](../../evidence/rx9/rx9r1-amd64-build.log), the
freestanding symbol/ABI probe in
[`rx9r1-native-symbols.txt`](../../evidence/rx9/rx9r1-native-symbols.txt),
and the original failing compile in
[`amd64-kernel-build.log`](../../evidence/rx9/amd64-kernel-build.log).

## QEMU discovery and guest evidence

QEMU was absent from PATH but found at
`C:\Program Files\qemu\qemu-system-x86_64.exe`, version 11.0.0. Existing
QEMU processes for other projects were left untouched. The qualification guest
used:

- One TCG thread and one vCPU, 1024 MiB guest memory.
- `-net none`; no host forwarding or network interface.
- A copied read-only firmware code image and a separate writable copied ESP
  directory containing the exact build output and plain-text `.rex` files.
- Dedicated loopback QMP and serial log. The final QEMU process was stopped
  through its verified QMP endpoint after guest checks completed.

The built kernel and staged/booted kernel hashes match. The guest ran the RX8
regression script plus RX9R1 NOVALUE and SYNTAX fixtures, CALL ON ERROR,
condition-state restoration, controlled unknown-label failure, resource
exhaustion, and post-error recovery. `call_error_delay.rex` printed:

```text
resumed
ERROR
positive
CALL
DELAY
3
127
OLD
0
```

This confirms guest continuation after RETURN, CONDITION state and description,
SIGL 3, RC 127 from the safe unavailable-command adapter, RESULT preservation,
and cleared condition information after returning. The serial log continues
to report increasing PIT heartbeat ticks after these commands. Evidence:

- [PARSE VAR NOVALUE](../../evidence/rx9/guest-novalue-parse-var.png)
- [PARSE template and subsidiary reads](../../evidence/rx9/guest-novalue-parse-template-string.png)
- [Dynamic DROP and recursive EXPOSE](../../evidence/rx9/guest-novalue-drop-subsidiary.png)
- [SYNTAX recovery](../../evidence/rx9/guest-syntax-recovery.png)
- [CALL ON ERROR, RC, SIGL, and RESULT](../../evidence/rx9/guest-call-error.png)
- [Untrapped error and resource limit](../../evidence/rx9/guest-resource-limit.png)
- [Console recovery after resource failure](../../evidence/rx9/guest-recovery.png)
- [Serial boot and heartbeat](../../evidence/rx9/rx9r1-qemu-serial.log)
- [Guest command output summary](../../evidence/rx9/rx9r1-qemu-results.txt)
- [QEMU arguments and shutdown identity](../../evidence/rx9/rx9r1-qemu-arguments.txt)

## Hosted regression, allocation, and tools

The final hosted runner exited 0. Exact fresh counts:

| Suite | Result |
|---|---:|
| RX1 runtime | 11/11 |
| CLI | 20/20 |
| RX2 VFS/output, numeric, shell bridge | 45, 18, 7 |
| RX3 decimal, allocation, fixtures | 26, 570, 13/13 |
| RX4 arithmetic, fixtures | 53, 11/11 |
| RX5 numeric context, fixtures | 35/35, 13/13 |
| RX6 allocation, fixtures | 439, 25/25 |
| RX7 routines, parser/compiler/VM allocation, fixtures | 13; 46/28/111; 33/33 |
| RX8 C assertions, parser/compiler/VM allocation, fixtures | 279; 38/21/50; 26/26 |
| RX9R1 condition assertions, parser/compiler/VM allocation, fixtures | 452; 41/37/73; 24/24 |

The allocation-failure matrix includes the new subsidiary EXPOSE, PARSE
overlay, subsidiary DROP, trap dispatch, routine restoration, and recovery
paths. Every injected VM allocation failure releases its allocations, and
the VM executes a recovery program afterward.

MinGW GCC sanitizer builds were attempted against the final RX9 condition
suite. ASan cannot link because `-lasan` is missing; UBSan cannot link because
`-lubsan` is missing. Neither sanitizer executable ran. Regina is unavailable,
so Regina differential cases were 0; all repository-hosted fixture suites
passed. Logs are in [`evidence/rx9`](../../evidence/rx9), including
[`rx9r1-hosted-final.log`](../../evidence/rx9/rx9r1-hosted-final.log) and
[`rx9r1-sanitizer-attempts.log`](../../evidence/rx9/rx9r1-sanitizer-attempts.log).

## Remaining limits and RX10 direction

RX9's supported subset still does not provide CALL ON NOVALUE or CALL ON
SYNTAX. SYNTAX remains limited to the documented runtime status mapping;
compile-time errors and resource-protection statuses remain outside the
handler. HALT and NOTREADY are registration/state support only until bounded,
safe event sources exist. Regina differential validation and sanitizer runs
remain unavailable in this environment.

RX10 can consider broader classic runtime error-code coverage or safe OS event
sources if those become available. Keep resource boundaries and compile-time
error handling as currently classified; RX9 work is complete and no RX10
changes were started here.
