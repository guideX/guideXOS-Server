# RX8 Classic SIGNAL and Subsidiary EXPOSE

## Repository state

- Repository: `D:\dev\guideXOSServer_REXX`.
- Required branch: `REXX_SUPPORT`; it remained selected for the full phase.
- Starting HEAD: `23a9df7218d79e521ad14af75de6306a53d8fd25`.
- Starting branch tracking state: two local commits ahead of the existing
  `origin/REXX_SUPPORT` reference; no fetch or push was performed.
- Starting worktree was clean and had no untracked files. The root `.phase`
  file was absent and remains absent.
- Ending branch: `REXX_SUPPORT`. The ending HEAD is the local RX8 commit
  reported in the phase completion report; its identifier is generated only
  after this document is committed.
- No branch operations, network synchronization, production image writes, or
  interaction with a foreign QEMU process were performed.

## RX7 architecture audit

RX8 extends the existing lexer, AST, bytecode compiler, and iterative VM. It
does not add a second interpreter or recurse through C calls for REXX routines.
The RX7 audit is in [`REXX_RX7_INTERNAL_ROUTINES.md`](REXX_RX7_INTERNAL_ROUTINES.md).

- **AST and opcodes:** labels are `REXX_AST_CLAUSE_LABEL`; PROCEDURE carries an
  ordered name list, with a subsidiary flag on each parenthesized entry;
  SIGNAL distinguishes a static label from an expression. The compiler emits
  `OP_LABEL`, `OP_PROCEDURE`, `OP_EXPOSE`, `OP_EXPOSE_SUBSIDIARY`, and
  `OP_SIGNAL`. A dynamic SIGNAL expression is compiled once before
  `OP_SIGNAL`.
- **Labels:** the bytecode label index is currently a bounded linear scan of
  `OP_LABEL` instructions. Lookup starts at the beginning of the program, so
  the first duplicate wins. CALL and static SIGNAL symbols compare without
  case sensitivity. Dynamic SIGNAL values compare exactly against the
  compiler's uppercase label symbols.
- **Source locations:** each instruction carries the lexer token's one-based
  source line. SIGNAL and internal CALL/function instructions use their
  originating clause line. This is a scalar field per instruction, not a
  source map or bytecode-offset approximation.
- **Call-frame ownership:** the VM uses a bounded iterative frame array (64
  levels). Frames own copied arguments and saved ADDRESS strings and save the
  return PC, evaluation-stack base, DO depth, caller/callee environments,
  caller arguments, and NUMERIC DIGITS/FUZZ/FORM. Return and error unwind
  restore those contexts.
- **Variable generations:** PROCEDURE creates a child variable generation
  linked to its caller. EXPOSE entries are names in that generation which
  resolve through the parent environment; values are not copied. Returning or
  unwinding frees a PROCEDURE generation and its exposure links.
- **DO and condition state:** DO frames are explicit bounded VM frames, up to
  64. IF and SELECT use compiled expression and branch instructions, with no
  separate runtime frame. LEAVE and ITERATE use patched bytecode targets.
  INTERPRET is not implemented.
- **Evaluation stack and continuation:** the value stack is VM-owned and
  bounded. CALL records a continuation PC and the caller's stack/DO bases;
  SIGNAL records no continuation and leaves existing routine activations in
  place. It clears pending expression values and DO frames only above the
  current activation's saved bases before dispatching at the target.
- **Numeric, ADDRESS, and budgets:** each routine frame saves numeric
  settings and the current ADDRESS environment. The instruction budget is
  local to one `rexx_vm_execute` run; numeric-work remaining is VM execution
  state. Neither is replenished by SIGNAL or routine unwinding.

This ownership model supports a whole-program label search without changing
the active CALL chain. IBM's documented multiway SIGNAL example transfers
from a PROCEDURE to a label outside its apparent source region, then uses the
same arguments and RETURN continuation at the destination.

## Subsidiary PROCEDURE EXPOSE

The parser accepts ordered direct names and parenthesized variable names in
one EXPOSE list, including several subsidiary lists. It compiles every list
entry into its own exposure opcode. At runtime, each subsidiary opcode:

1. Resolves and exposes its controlling symbol first.
2. Reads that symbol through the now-live alias.
3. Scans the resulting blank-separated names once, left to right.
4. Validates each name, resolves any compound tail against the variables
   exposed earlier in the list, and appends a live alias to the child
   generation.

The subsidiary text is not parsed as REXX source. Parentheses within that text
are rejected; an empty string adds no subsidiary names. Missing direct or
subsidiary variables follow ordinary REXX default-value behavior, so a newly
exposed variable can be assigned by the routine and observed by its caller.
Repeated names are permitted and do not create duplicate alias nodes. Names
are matched without case sensitivity. Stems expose all matching compound
variables; a named compound exposes only that name.

Each PROCEDURE generation permits at most 4,096 exposure aliases and
1,048,576 bytes of exposure names. A subsidiary value is capped at 65,536
bytes and a name at 65,535 bytes. Scans use checked `size_t` bounds. Invalid
names produce `invalid subsidiary PROCEDURE EXPOSE list`; resource-limit and
allocation failures use the VM's existing deterministic errors. Setup aliases
live only in the callee generation. If a later item or allocation fails, the
normal execution failure path unwinds that generation and releases every
alias without changing a caller value.

Exposure ordering is observable. In `PROCEDURE EXPOSE i a.i`, the alias for
`i` is installed before `a.i` is resolved, so the compound tail observes the
caller's `i`. Reversing the entries resolves `a.i` while the local `i` is
uninitialized and therefore binds the corresponding default compound name.
This behavior is covered separately in the ordered and reversed fixture.

## SIGNAL syntax and label resolution

RX8 accepts `SIGNAL label`, `SIGNAL VALUE expression`, and the classic omitted
VALUE spelling where the expression starts with an operator, number, or left
parenthesis. A bare symbol is a constant label operand; it is not evaluated as
a variable. Quoted string operands are evaluated string values. This keeps
the classic distinction between constant symbol labels and computed
destinations.

Static symbol operands use case-insensitive lookup. Literal and computed
string destinations use exact matching, so they must already be uppercase to
match the compiler's uppercase label symbols. RX8 does not silently normalize
computed values. An empty, malformed, or missing destination returns a
controlled invalid/unknown SIGNAL label diagnostic. Lookup is over the
current compiled program only and chooses the first matching label from the
beginning of the instruction array.

SIGNAL VALUE consumes exactly one evaluated value. The value is converted to
an owned string once, checked against the bounded symbol rules, used for the
program-wide lookup, and freed on success or failure. No CALL frame or return
address is created.

## Nonlocal transfer and routine interaction

RX8 searches labels from the beginning of the current compiled program, even
when a destination is outside the active routine's apparent source region.
The transfer itself does not implicitly return, create a return address, or
unwind existing CALL/function activations. The target executes in the current activation,
with its arguments, PROCEDURE generation, EXPOSE aliases, numeric settings,
and ADDRESS environment still active. An explicit RETURN at the target uses
the original CALL/function continuation; EXIT and runtime failure retain
their existing unwind behavior.

SIGNAL terminates pending control structures in the current routine. RX8
keeps the current call frame's saved stack and DO bases, clears evaluation
values above that stack base, and discards DO frames above that activation's
saved DO depth. This also removes loops inside nested routine calls while
preserving a caller's suspended expression and DO loop. IF/SELECT branches
have no runtime frames; their pending expression values are cleared with the
same stack operation. INTERPRET is not implemented. Recursive and function
activations remain valid until their own RETURN/EXIT/error path completes.

The behavior is covered by a cross-region label transfer from a PROCEDURE
inside a callee DO loop. Its target observes the callee's local and argument,
returns through the original CALL continuation, and lets the caller's DO loop
continue. Recursive SIGNAL and SIGNAL from a function in a pending arithmetic
expression also verify that the right activation and outer stack values
survive. The VM instruction and numeric-work budgets are unchanged by SIGNAL.

The VM instruction and numeric-work budgets are not reset by the transfer.
Backward SIGNAL loops exhaust the same instruction budget as other loops.
Allocation or expression errors before the jump use ordinary VM error unwind
and leave the VM usable for a subsequent script.

## SIGL and line metadata

SIGL is stored as an ordinary variable in the current environment. A
successful static or dynamic SIGNAL stores the source line of the SIGNAL
clause there. Internal CALL and function invocation store their clause line
in the caller's current environment before entering the routine. PROCEDURE
and EXPOSE govern SIGL through the normal variable-environment rules; an
ordinary assignment or RETURN does not update it. Since SIGNAL retains the
active routine, the target observes SIGL through that same generation or
live EXPOSE alias.

The lexer-provided line field counts physical source lines, including blank
lines and comments. Multiple clauses on one physical line share that line
number. The fixtures cover labels, comments, blanks, multiple clauses, CALL,
function invocation, and SIGNAL. No bytecode PC is used to invent source
lines.

## Condition traps and RX9

`SIGNAL ON condition` and `SIGNAL OFF condition` remain unsupported. The
parser recognizes ON/OFF followed by a classic condition name and returns an
explicit unsupported-condition-trap diagnostic; it never compiles those
forms as label jumps. Bare `ON`, `OFF`, and `VALUE` operands remain valid
constant label names. RX9 will need condition enable/disable state, trap-label
resolution, condition-specific RC and SIGL updates, per-routine trap-state
save/restore rules, and safe handling of traps raised during routine unwind.
CALL ON/OFF and full CONDITION() semantics are also outside RX8.

## Compatibility and test coverage

| Feature | RX8 result | Evidence |
|---|---|---|
| Direct and subsidiary EXPOSE mixed in one list | Supported | `procedure_expose_multiple.rex` |
| Multiple subsidiary lists, empty/missing/repeated names | Supported | `procedure_expose_empty`, `missing`, `repeated` |
| Scalar, compound, stem, DROP, and nested-generation aliases | Supported | RX7 and RX8 EXPOSE fixtures |
| Left-to-right compound resolution | Supported | `procedure_expose_order.rex` |
| Static forward/backward label transfers | Supported | SIGNAL fixtures |
| SIGNAL VALUE and omitted VALUE expressions | Supported | `signal_dynamic`, `signal_omitted_value` |
| Literal/static versus dynamic casing | Supported | `signal_literal_case`, `signal_dynamic_lowercase` |
| ON/OFF/VALUE label operands versus trap/VALUE subkeywords | Supported | `signal_subkeyword_labels`, `signal_condition_trap` |
| SIGNAL from CALL, function, and recursion | Supported; active routine calls remain until explicit RETURN/EXIT/error | `signal_cross_label_call`, `signal_call_function`, `signal_recursive` |
| Counted and controlled DO termination; clean following loop | Supported | `signal_loop_termination.rex`, `signal_do_while.rex` |
| SIGL line for SIGNAL, CALL, and function; RETURN does not replace it | Supported | `signal_labels_sigl`, `signal_call_sigl`, `signal_call_function` |
| SIGNAL ON/OFF traps | Explicitly unsupported | `signal_condition_trap.rex` |
| Regina differential run | 0 cases; Regina unavailable | Hosted runner log |

RX8 hosted results (fresh `tests/run_rexx_tests.ps1` run):

| Suite | Result |
|---|---:|
| RX1 runtime | 11/11 |
| Hosted CLI | 20/20 |
| RX2 VFS/output | 45/45 |
| RX2 numeric | 18/18 |
| RX2 shell bridge | 7/7 |
| RX3 decimal | 26/26 |
| RX3 allocator | 570/570 |
| RX3 fixtures | 13/13 |
| RX4 arithmetic | 53/53 |
| RX4 fixtures | 11/11 |
| RX5 numeric context | 35/35 |
| RX5 fixtures | 13/13 |
| RX6 allocator | 403/403 |
| RX6 fixtures/resources | 25/25 |
| RX7 routine assertions | 13 |
| RX7 parser/compiler/VM allocation sweeps | 46/46, 28/28, 107/107 |
| RX7 fixtures | 33/33 |
| RX8 C semantic/recovery assertions | 279/279 |
| RX8 parser/compiler/VM allocation sweeps | 38/38, 21/21, 50/50 |
| RX8 fixtures | 26/26 |

The full transcript is [`hosted-tests.log`](../../evidence/rx8/hosted-tests.log).
The RX8 fixture sources and expected output are in
[`tests/rexx_rx8`](../../tests/rexx_rx8).

## Allocation and sanitizer results

The RX8 C test sweeps deterministic parser, compiler, and VM allocation
failpoints. The VM sweep includes a PROCEDURE EXPOSE alias followed by a
SIGNAL VALUE transfer, asserts zero active routine/DO/evaluation state after
each injected allocation error, and runs a successful recovery execution.
Additional C checks cover invalid and oversized subsidiary lists, cleanup of
transient state, a backward SIGNAL instruction-budget exhaustion, and another
successful script run. The parser sweep caught a missing AST free case for
SIGNAL expressions; the AST deallocator now releases that expression.

ASan and UBSan execution could not be qualified with the installed Windows
toolchains. The installed Clang GNU-target driver lacks its ASan and UBSan
linker runtime archives; the MinGW toolchain also lacks the sanitizer runtime
libraries. This is recorded as unavailable, not as a sanitizer pass. Logs:
[`asan.log`](../../evidence/rx8/asan.log) and
[`ubsan.log`](../../evidence/rx8/ubsan.log).

## Native AMD64 and QEMU evidence

The full guideXOS AMD64 kernel is built freestanding with exceptions and RTTI
disabled. The RX8 VM/parser/compiler C modules compile and link into the
kernel. The final rebuild uses
`kernel/build/rexx-rx8-final-qualification-20261017`; its ELF SHA-256 is
`D6F5B5DCE0BEE422B832A44AEFFBCA0198F1419E3046A5F57CE734193BE3CAFD`. The
build transcript and image identity are recorded in the evidence directory.

The guest qualification uses copied writable ESP media, read-only copied
firmware code, TCG with one CPU, and `-net none`. No production ESP or disk is
used. QEMU process ownership is checked against the dedicated command line,
QMP port, serial file, and copied ESP; any unrelated QEMU process is left
untouched. The final serial transcript records heartbeat ticks while the
Console remains responsive.

Guest demonstrations and evidence:

- [`rx8-corrected-acceptance.png`](../../evidence/rx8/rx8-corrected-acceptance.png): scalar,
  compound and stem subsidiary exposure, dynamic SIGNAL, SIGL, and DO-loop
  termination.
- [`rx8-corrected-cross-signal.png`](../../evidence/rx8/rx8-corrected-cross-signal.png):
  cross-region transfer preserves the active PROCEDURE arguments/local and
  returns to the suspended caller loop.
- [`rx8-corrected-unknown-label.png`](../../evidence/rx8/rx8-corrected-unknown-label.png) and
  [`rx8-corrected-error-status.png`](../../evidence/rx8/rx8-corrected-error-status.png): unknown
  destination and nonzero guest command status.
- [`rx8-corrected-recovery-status.png`](../../evidence/rx8/rx8-corrected-recovery-status.png): a
  successful command after the failure.
- [`rx8-corrected-resource-failure.png`](../../evidence/rx8/rx8-corrected-resource-failure.png),
  [`rx8-corrected-resource-status.png`](../../evidence/rx8/rx8-corrected-resource-status.png), and
  [`rx8-corrected-resource-recovery.png`](../../evidence/rx8/rx8-corrected-resource-recovery.png),
  [`rx8-corrected-resource-recovery-status.png`](../../evidence/rx8/rx8-corrected-resource-recovery-status.png):
  oversized subsidiary-list diagnostic, status propagation, and recovery.
- [`guest-transcript-corrected.txt`](../../evidence/rx8/guest-transcript-corrected.txt),
  [`serial-rx8-corrected.log`](../../evidence/rx8/serial-rx8-corrected.log),
  [`kernel-identity-corrected.txt`](../../evidence/rx8/kernel-identity-corrected.txt),
  [`amd64-build-rx8-final.log`](../../evidence/rx8/amd64-build-rx8-final.log),
  [`qemu-arguments-corrected.txt`](../../evidence/rx8/qemu-arguments-corrected.txt),
  and [`qemu-shutdown-verification.txt`](../../evidence/rx8/qemu-shutdown-verification.txt)
  are the fresh corrected-kernel evidence. The guest remains interactive and
  heartbeat ticks continue during and after REXX execution; shutdown was sent
  only to the verified RX8 QEMU PID/QMP listener.

Earlier exploratory `rx8-final-*` captures and logs are preserved unchanged;
the corrected screenshots, transcript, serial log, and kernel identity above
are the final acceptance evidence after routine-frame behavior was corrected.

## Remaining classic REXX gaps and RX9

RX8 does not implement condition traps, CALL ON/OFF, CONDITION(), INTERPRET,
external routines, or the full TRACE debugger. These are explicit phase
limits. Regina was unavailable, so compatibility is based on ANSI REXX
X3.274-1996 and IBM documentation rather than a differential run. A useful
RX9 phase should add one condition at a time with trap-state ownership tied to
routine frames, well-defined RC/SIGL updates, and allocation/failure recovery
tests before exposing the full ON/OFF surface.

References reviewed:

- ANSI X3.274-1996, *Programming Language REXX*.
- [IBM PROCEDURE instruction](https://www.ibm.com/docs/en/cics-ts/6.x?topic=instructions-procedure).
- [IBM SIGNAL instruction](https://www.ibm.com/docs/en/zvm/7.2?topic=instructions-signal).
- [IBM special variables](https://www.ibm.com/docs/en/zvm/7.2.0?topic=variables-special).
- [IBM SIGNAL instructions](https://www.ibm.com/docs/en/cics-ts/6.x?topic=instructions-signal).
