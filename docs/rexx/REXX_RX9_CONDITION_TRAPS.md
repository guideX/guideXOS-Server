# RX9 Classic REXX Condition Traps

## Repository state

- Repository: `D:\dev\guideXOSServer_REXX`.
- Required branch: `REXX_SUPPORT`.
- Starting HEAD: `2e77836a2220cb9938dcd642552f14354c241367`.
- The starting worktree was clean; no previous RX9 implementation or root
  `.phase` file was present.
- Work remained on `REXX_SUPPORT`; no branch, fetch, push, merge, rebase,
  reset, clean, stash, or worktree operation was used.
- The ending HEAD is the local RX9 commit recorded in the phase completion
  report. The ending worktree is clean after that commit.
- ESP and disk images were not used as build outputs. The kernel build output
  directory was isolated at `tmp/rx9-native-build`.
- Phase outcome: **B — Partial**. Hosted functionality is verified, but the
  native kernel build, sanitizer, and guest qualification gates are open.

## Design

The parser builds a distinct condition-registration AST clause for `CALL` and
`SIGNAL` ON/OFF. The compiler emits `OP_CONDITION`, storing the condition enum,
the action, and (for ON) a constant internal routine or label symbol. The VM
keeps six bounded entries per VM: ON/OFF/DELAY state, CALL/SIGNAL action, and
handler symbol. New VM executions initialize all traps OFF.

The six-element trap state and current condition information are copied into
the existing bounded iterative routine frames. RETURN and routine unwind
restore that snapshot along with the existing NUMERIC, ADDRESS, variable
generation, argument, and continuation state. Condition descriptions are
VM-owned copies bounded by the current source-length limit. CALL condition
handlers use the ordinary iterative routine path; SIGNAL handlers use the
existing nonreturning label transfer. A private control-transfer status lets
the VM continue dispatch without returning to the host or resetting either
execution budget.

IBM specifies that an ON/OFF reference replaces the previous state, action,
and trap name for that condition. Omitting NAME uses the condition name. CALL
and SIGNAL therefore share one registration slot per condition. This matches
the documented mutual replacement rule in the [z/VM 7.4 REXX/VM Reference,
Chapter 6](https://www.vm.ibm.com/library/740pdfs/74631400.pdf).

## Conditions and implemented registrations

| Condition | SIGNAL ON/OFF | CALL ON/OFF | Event dispatch in this runtime |
|---|---:|---:|---|
| ERROR | Yes | Yes | Positive host-command RC; negative RC only when FAILURE is OFF |
| FAILURE | Yes | Yes | Negative host-command RC when FAILURE is enabled |
| HALT | Yes | Yes | Registration only; no safe interrupt source exists |
| NOTREADY | Yes | Yes | Registration only; no eligible stream-I/O runtime source exists |
| NOVALUE | Yes | No | Simple and resolved compound variable expression reads |
| SYNTAX | Yes | No | Selected catchable VM language-processing errors |

`CALL ON/OFF NOVALUE` and `CALL ON/OFF SYNTAX` are rejected. SIGNAL ON/OFF
accepts all six named conditions; CALL ON/OFF accepts ERROR, FAILURE, HALT,
and NOTREADY. Unknown condition names are diagnosed instead of accepted
silently. Handler names must be constant symbols. SIGNAL targets resolve as
internal labels; CALL targets resolve as internal routines. A target that is
absent when a condition occurs returns the VM's controlled unknown-label or
unknown-routine status.

State transitions implemented by the runtime:

| Operation/event | State after operation |
|---|---|
| Initial program state | OFF |
| `CALL/SIGNAL ON condition` | ON, replacing action and handler |
| `CALL/SIGNAL OFF condition` | OFF |
| SIGNAL condition transfer begins | That condition becomes OFF |
| CALL condition handler begins | That condition becomes DELAY |
| CALL condition handler returns | Caller snapshot is restored |

## SIGNAL and CALL behavior

SIGNAL traps set SIGL to the physical source line of the triggering
instruction, disable that condition, preserve condition information, clear the
abandoned expression stack and current DO frames, and transfer through the
existing RX8 label machinery. The handler's `CONDITION('S')` therefore reports
OFF. The instruction that raised the condition is abandoned.

CALL traps set SIGL to the source line of the triggering instruction and
invoke the internal routine through the iterative VM call frame. The
interrupted instruction stream resumes after RETURN. The handler's returned
value is discarded and does not write RESULT. While the handler is active,
`CONDITION('S')` reports DELAY. Numeric settings, ADDRESS, trap registrations,
condition information, arguments, and the caller continuation are restored by
the ordinary frame return path.

The implementation ignores a repeated occurrence of the same ERROR or
FAILURE while that condition is DELAY. Other conditions with independent ON
registrations can nest; tests cover ERROR followed by FAILURE and verify the
outer condition information is restored afterward. IBM documents repeated
ERROR/FAILURE/NOTREADY in DELAY as ignored, and the return from CALL resumes
the interrupted flow. See Chapter 6, “Action Taken When a Condition Is
Trapped,” in the [IBM reference](https://www.vm.ibm.com/library/740pdfs/74631400.pdf).
An explicit OFF made within the handler changes CONDITION('S') to OFF for the
rest of that routine, then the caller's registration is restored on RETURN;
the C test raises the next ERROR and confirms that restored registration.

HALT has no event source here, so its DELAY behavior cannot be observed in a
real guest. The runtime does not synthesize pending events or claim support
for deferred HALT/NOTREADY dispatch.

## CONDITION()

`CONDITION()` defaults to option `I`. The first character of the option is
case-insensitive. Supported options are:

| Option | Result |
|---|---|
| C | Current trapped condition name |
| D | Condition description |
| I or omitted | `CALL` or `SIGNAL` |
| S | Current trap state (`ON`, `OFF`, or `DELAY`) |

With no active condition, each valid option returns the null string. More than
one argument, an empty option, or an unsupported first character produces the
runtime's invalid-function-call error, which can itself reach an active
SYNTAX trap. Condition information and its owned description are saved and
restored across nested routine calls.

## ERROR and FAILURE

The existing ADDRESS command dispatcher supplies the signed command status to
RC. The condition engine runs after the command clause completes, using the
original command bytes for CONDITION('D'). RC remains the actual command
status. RC zero raises no condition. Positive RC selects ERROR. Negative RC
selects FAILURE when its trap is enabled (including DELAY); otherwise ERROR is
eligible. This is covered with deterministic positive, negative, and zero
test callbacks. The CLI fixture also uses the existing safe default adapter:
without a configured host callback, the adapter returns its documented
  unsupported-command status (127), as confirmed by
  `rexx_os_dispatch_address()`; no shell or privileged command is invoked.

## NOVALUE and SYNTAX boundaries

NOVALUE checks variable expressions after resolving dynamic compound tails.
The check recognizes simple variables, exact compound entries, and stem
defaults. A compound tail used to derive a variable name does not independently
raise NOVALUE. The description uses the resolved variable name, such as
`B.Z`. Tests cover a simple missing variable, a stem default with an
uninitialized tail, and a dynamic compound name. The VM's PARSE, PROCEDURE,
and DROP paths are not all wired through this read-time condition check; these
forms remain a compatibility gap.

SYNTAX trapping covers selected runtime statuses: numeric conversion and
division-by-zero errors, invalid NUMERIC settings, invalid function calls,
unknown functions/routines/labels, missing function results, and invalid
PROCEDURE use. The current RC mapping includes 41 for bad arithmetic
conversion, 42 for division by zero, 26 for NUMERIC settings, 16 for unknown
labels, 43 for unknown routines, 24 for invalid PROCEDURE, and 40 for the
remaining mapped language-processing errors. CONDITION('D') returns the
runtime's bounded description. Compile and parse failures happen before VM
execution and cannot be caught by the same source program.

SYNTAX support is deliberately limited to those VM statuses. Instruction,
numeric-work, stack, table, and allocation limits remain fatal VM statuses and
cannot be caught by SIGNAL ON SYNTAX. This prevents a script from catching a
resource boundary and continuing with uncertain state.

## SIGL, RC, RESULT, and routine state

- Command conditions retain the command RC, including negative values.
- SYNTAX traps set RC to the mapped error number before dispatch.
- SIGNAL and CALL condition transfers update SIGL from the real instruction
  source line.
- CALL ON never assigns the handler's return string to RESULT.
- The routine frame restores trap state and condition information on RETURN;
  RX7's numeric and ADDRESS context restoration remains in the same path.
- Each VM execution clears trap registrations and current condition
  information, preventing stale trap metadata from crossing executions.

The IBM reference documents RC behavior for ERROR/FAILURE and SYNTAX and
defines SIGL as the line of the clause that caused the transfer. See the
[conditions chapter](https://www.vm.ibm.com/library/740pdfs/74631400.pdf).

## HALT and NOTREADY classification

The current REXX host has no asynchronous cancel/halt callback and no runtime
stream APIs that can produce a NOTREADY condition. Source-file loading occurs
before program execution; its failures remain CLI errors. Registration and
state replacement are supported, but no real guest dispatch mechanism is
claimed. No interrupt subsystem or filesystem API was added.

## Verification and evidence

The hosted runner passed after the RX8 `SIGNAL ON` label ambiguity was
preserved: a bare `SIGNAL ON` at a clause boundary continues to mean the RX8
label transfer, while a following condition token selects condition
registration. The RX8 label regression and condition registration fixtures
both pass.

Fresh run results:

- RX9 C condition assertions: 165.
- RX9 deterministic allocator failpoints: parser 17, compiler 15, VM 23.
- RX9 ordinary fixtures: 9/9.
- Hosted CLI cases: 20/20.
- RX1 through RX8 all pass; exact assertion and fixture counts are in
  [`evidence/rx9/hosted-test-summary.txt`](../../evidence/rx9/hosted-test-summary.txt).
- Regina differential cases: 0; Regina is not installed.
- Freestanding AMD64 compilation of the 9 REXX C objects succeeds using the
  kernel's C flags.
- Full AMD64 kernel link does not complete. The isolated build stops in
  existing C++ translation units because `-nostdinc++` is active while
  `app_model_limits.h` includes `<cstddef>`. The compiler reports
  `fatal error: cstddef: No such file or directory`. No unrelated kernel
  header was changed to work around this RX9-independent build problem.
- QEMU process census found no active QEMU process. QEMU is absent from PATH
  and the repository's configured common install paths, so no guest could be
  booted. ESP and disk media were not passed to a build or emulator command.
- ASan/UBSan could not run with the available toolchains: MinGW GCC lacks
  `-lasan`/`-lubsan`, and the installed Clang lacks its Windows ASan runtime
  archives. No sanitizer-clean claim is made.

Detailed build and runner logs are preserved in [`evidence/rx9`](../../evidence/rx9).

## Classic compatibility matrix and remaining gaps

| Area | Result |
|---|---|
| Six-condition parser/compiler registration | Implemented and tested |
| CALL vs SIGNAL handler control flow | CALL ERROR/FAILURE and SIGNAL ERROR/FAILURE/NOVALUE/SYNTAX tested |
| CONDITION C/D/I/S | Implemented and tested, including nested restoration |
| Positive/negative command RC and precedence | Implemented and tested |
| Simple and compound-expression NOVALUE | Implemented for expression reads; PARSE/PROCEDURE/DROP forms incomplete |
| Runtime SYNTAX | Selected runtime subset; compile-time and unlisted statuses are not catchable |
| ERROR/FAILURE DELAY | Repeated same-condition errors are ignored; nested distinct condition is tested |
| HALT | Registration only; no real cancellation event source |
| NOTREADY | Registration only; no eligible runtime stream-I/O source |
| Resource exhaustion | Remains terminal and outside script-level traps |
| Native guest execution | Not qualified because full kernel build and QEMU gates are unavailable |

RX10 should first complete a follow-up focused on true runtime HALT/NOTREADY
event sources (only if the OS adds bounded, safe sources), the remaining
NOVALUE contexts, and classic runtime error-code coverage. Do not advance to
RX10 until RX9's native build and guest-qualification gates are resolved.

## IBM references

- [z/VM 7.4 REXX/VM Reference, Chapter 6: Conditions and Condition Traps](https://www.vm.ibm.com/library/740pdfs/74631400.pdf)
- [IBM REXX/VM Reference PDF](https://www.ibm.com/support/pages/zvm/library/710pdfs/71631400.pdf)
