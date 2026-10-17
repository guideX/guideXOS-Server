# RX9 Classic REXX Condition Traps

## Repository state

- Repository: `D:\dev\guideXOSServer_REXX`.
- Required branch: `REXX_SUPPORT`.
- RX9R1 starting HEAD: `1be2ededc2f620dd7295a1790bdae2ed0ae25bb4`.
- The starting worktree was clean; no RX9R1 completion artifact or root
  `.phase` file was present.
- Work remained on `REXX_SUPPORT`; no branch, fetch, push, merge, rebase,
  reset, clean, stash, or worktree operation was used.
- The local RX9R1 closure commit records the ending HEAD. The root `.phase`
  file remains absent.
- The kernel was built under the isolated
  `kernel/build/rexx-rx9r1-native-qualification-20261017` directory. QEMU used
  a separate copied ESP and copied read-only firmware; production media were
  not opened.
- Phase outcome: **A — RX9 accepted**. Hosted, freestanding AMD64, and native
  QEMU guest gates passed. Sanitizer runtimes and Regina were unavailable and
  are recorded as unavailable, not as successful runs.

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

NOVALUE now uses one read-time check for expression operands and the variable
reads performed by `PARSE VAR`, dynamic parsing-template patterns and
positions, subsidiary `PROCEDURE EXPOSE`, and subsidiary `DROP (name)`.
Dynamic compound tails are resolved before the condition is raised, so
`CONDITION('D')` contains the derived name. The classic compound-tail
exemption remains: a stem default supplies an uninitialized tail, and a tail
used only to construct a derived name is not itself read as a variable.

`PARSE` writes are staged in a transparent child environment. The staged
values are committed only after the entire parse completes, so a NOVALUE
transfer leaves every parse destination unchanged. The overlay reads through
the existing caller and `EXPOSE` environments and preserves recursive
aliases. Normal uninitialized reads still return their variable names while
NOVALUE is OFF. The subsidiary `DROP (name)` implementation reads the dynamic
name list through this same condition check; direct `DROP variable` behavior
is unchanged. IBM's [conditions reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=reference-conditions-condition-traps)
documents NOVALUE for variable references in parsing and procedure/drop
contexts and the compound-tail exception. IBM's [DROP reference](https://www.ibm.com/docs/SSGMCP_5.5.0/reference/rexx/drop.html)
documents the parenthesized dynamic name list.

SYNTAX trapping covers selected runtime statuses: numeric conversion and
division-by-zero errors, invalid NUMERIC settings, invalid function calls,
unknown functions/routines/labels, missing function results, and invalid
PROCEDURE use. The RC mapping includes 41 for bad arithmetic conversion, 42
for division by zero, 26 for NUMERIC settings, 16 for unknown labels, 43 for
unknown routines, 24 for invalid PROCEDURE, and 40 for other mapped
language-processing errors. CONDITION('D') returns the bounded runtime
description. Compile and parse failures happen before VM execution and cannot
be caught by the same source program; an undefined handler or failure inside a
handler also remains terminal.

Instruction-budget, numeric-work, stack, table, and allocation limits remain
fatal VM statuses and cannot be caught by SIGNAL ON SYNTAX. Hosted tests
confirm that instruction and numeric-work exhaustion stay outside the trap,
and that the VM can execute another program after the numeric-work failure.

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

Fresh RX9R1 run results:

- RX1 runtime: 11/11; CLI: 20/20.
- RX2 VFS/output: 45; freestanding numeric: 18; shell bridge: 7.
- RX3 decimal: 26; allocation failpoints: 570; fixtures: 13/13.
- RX4 arithmetic: 53; fixtures: 11/11.
- RX5 numeric context: 35/35; fixtures: 13/13.
- RX6 PARSE allocation failpoints: 439; fixtures: 25/25.
- RX7 routines: 13; allocation failpoints: parser 46, compiler 28, VM 111;
  fixtures: 33/33.
- RX8 C assertions: 279; allocation failpoints: 38/21/50; fixtures: 26/26.
- RX9R1 condition assertions: 452; allocation failpoints: parser 41,
  compiler 37, VM 73; fixtures: 24/24.
- Regina differential cases: 0; Regina is unavailable.
- ASan and UBSan build attempts fail because MinGW GCC cannot find `-lasan`
  and `-lubsan`. They were not run and are not reported as sanitizer passes.
- AMD64 kernel build and link: pass. The minimal freestanding-safe header
  change replaces `<cstddef>` with `<stddef.h>` in `app_model_limits.h`; the
  C++/freestanding flags remain unchanged.
- QEMU was found at `C:\Program Files\qemu\qemu-system-x86_64.exe`, version
  11.0.0. The new kernel booted with TCG, one vCPU, `-net none`, a copied
  read-only firmware image, and a disposable copied ESP. The booted and built
  kernel SHA-256 values match.
- Guest scripts verified NOVALUE, SYNTAX recovery, CALL ON ERROR/DELAY,
  CONDITION state, RC, SIGL, RESULT preservation, routine trap restoration,
  controlled untrapped errors, instruction-limit failure, recovery after the
  failure, and a continuing serial heartbeat. HALT/NOTREADY dispatch was
  verified through safe synthetic hosted tests; no real event source is
  claimed.

See [`REXX_RX9R1_NATIVE_QUALIFICATION.md`](REXX_RX9R1_NATIVE_QUALIFICATION.md)
and the detailed logs, guest transcripts, kernel identity, and screenshots in
[`evidence/rx9`](../../evidence/rx9).

## Classic compatibility matrix and remaining gaps

| Area | Result |
|---|---|
| Six-condition parser/compiler registration | Implemented and tested |
| CALL vs SIGNAL handler control flow | CALL ERROR/FAILURE and SIGNAL ERROR/FAILURE/NOVALUE/SYNTAX tested |
| CONDITION C/D/I/S | Implemented and tested, including nested restoration |
| Positive/negative command RC and precedence | Implemented and tested |
| NOVALUE read contexts | Expressions, PARSE source/templates, subsidiary PROCEDURE EXPOSE/DROP, recursive EXPOSE; compound-tail exemption preserved |
| Runtime SYNTAX | Selected runtime subset; compile-time and unlisted statuses are not catchable |
| ERROR/FAILURE DELAY | Repeated same-condition errors are ignored; nested distinct condition is tested |
| HALT | Registration only; no real cancellation event source |
| NOTREADY | Registration only; no eligible runtime stream-I/O source |
| Resource exhaustion | Remains terminal and outside script-level traps |
| Native guest execution | AMD64 kernel booted from an isolated, network-disabled QEMU guest |

RX10 may expand classic runtime error-code coverage or add real HALT/NOTREADY
event sources if the OS gains bounded, safe sources. Keep compile-time SYNTAX
and resource-boundary behavior distinct. Do not weaken the established trap
limits or begin unrelated language/runtime work as part of this phase.

## IBM references

- [IBM z/OS REXX Conditions reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=reference-conditions-condition-traps)
- [IBM REXX DROP reference](https://www.ibm.com/docs/SSGMCP_5.5.0/reference/rexx/drop.html)
- [z/VM 7.4 REXX/VM Reference, Chapter 6](https://www.vm.ibm.com/library/740pdfs/74631400.pdf)
