# RX7 Classic REXX Internal Routines

**Outcome A — accepted.** RX7 implements bounded internal subroutine and
function calls, variable generations, routine arguments, state restoration,
and the qualified DO forms needed to exercise calls inside loops.

## Repository state

- Repository: `D:\dev\guideXOSServer_REXX`.
- Branch: `REXX_SUPPORT` throughout.
- Starting HEAD: `f14e55a7767ac1c6c1226b857d72b4927f890ac7`.
- Starting worktree: clean; branch was one local commit ahead of
  `origin/REXX_SUPPORT` from the accepted RX6 work.
- Root `.phase`: absent at start and end; it was not created or changed.
- Ending state: RX7 changes are committed locally on `REXX_SUPPORT`; the
  worktree is clean. The commit SHA is included in the RX7 completion report.
- No branch operations, network synchronization, production ESP writes, or
  changes to foreign QEMU processes were made.

## Existing architecture audit

RX6 already had AST and bytecode representations for labels, CALL, RETURN,
function expressions, argument expressions, PARSE ARG, numeric controls, and
DO loop setup/test/next/end instructions. The compiler emitted these routine
and loop instructions, but the VM did not yet dispatch internal labels or
create routine contexts. Counted and controlled DO instructions were also
unimplemented in the VM. RX7 reused those lexer/parser/compiler structures and
added VM execution and storage semantics; it did not add a second parser or
compiler.

Relevant implementation is in `rexx_lexer.c/.h`, `rexx_parser.c/.h`,
`rexx_compiler.c/.h`, `rexx_vm.c/.h`, and `rexx_builtin.c/.h`. The VM retains
one bytecode execution loop. Internal calls change its program counter and
push an explicit C data frame; recursive scripts do not recurse through the
C call stack and do not create a new VM per call.

## Call frames and routine resolution

The VM uses a bounded, dynamically grown call-frame array with a maximum depth
of 64. Each frame stores the continuation PC, caller evaluation-stack base,
caller DO depth, invocation kind, caller and callee variable environments,
caller and callee argument vectors, NUMERIC DIGITS/FUZZ/FORM, and the current
ADDRESS environment. Arguments and saved ADDRESS bytes are owned by the frame
and freed on return or unwind. Frame growth and its owned allocations use the
VM allocator, so deterministic allocation-failure tests cover these paths.

At a call, an unquoted routine name is looked up case-insensitively against
`OP_LABEL` instructions. The current lookup is a bounded linear scan of the
bytecode instruction array; it supports forward and backward labels. Duplicate
labels are accepted and resolve to the first label in source order. An
unquoted internal label is resolved before built-ins. A quoted name bypasses
internal lookup and proceeds to built-in resolution. Unknown names produce a
runtime diagnostic. External routine loading is out of scope.

The shared OP_CALL instruction distinguishes subroutine and function forms.
Arguments are evaluated left to right. A CALL stores a valued RETURN in
`RESULT`; RETURN without a value drops `RESULT`. CALL does not overwrite `RC`.
Internal function calls push a value back on the caller's expression stack and
must return a value; otherwise the VM reports a missing function result.
Top-level RETURN retains its terminate-the-script behavior.

## Variable generations, PROCEDURE, and EXPOSE

The root environment holds top-level scalar and stem variables. A routine
without PROCEDURE runs directly in its caller's active generation and shares
its variables. PROCEDURE, when it is the first instruction processed after
label entry, allocates a child generation linked to the caller. It does not
copy the caller's variable tables. Scalars and stem entries created in this
generation are freed on return.

PROCEDURE EXPOSE stores live aliases to the caller's generation. Reads,
writes, and DROPs resolve through that alias. A stem exposure such as
`items.` covers its compound variables; a named compound exposure such as
`items.1` aliases only that compound name. Nested exposures follow the
environment chain. Unexposed values remain local to the new generation.
The allocation-failure suite includes a nested PROCEDURE/EXPOSE path and
recovery checks.

The subsidiary-list spelling `PROCEDURE EXPOSE (list)` is parsed far enough to
produce an explicit `subsidiary PROCEDURE EXPOSE lists are unsupported`
diagnostic. It is not silently treated as a different exposure form.
PROCEDURE outside an internal routine and misplaced PROCEDURE instructions
are rejected.

## Arguments and ARG()

Each routine frame owns its argument vector, including presence flags for
omitted positions. Nested calls restore the caller's vector. PARSE ARG reads
the active routine vector; the RX6 top-level command-line argument string is
unchanged. The maximum is 256 arguments, with the inherited parse-source
length limit applied to copied argument bytes.

`ARG()` returns the active argument count. `ARG(n)` returns the selected
argument, and the first character of the optional second argument selects
existence (`E`) or omission (`O`). Indices must be positive integral values.
Top-level ARG continues to expose the CLI invocation string using the
supported RX6 model.

## NUMERIC and ADDRESS restoration

Internal calls begin with the caller's active NUMERIC DIGITS, FUZZ, and FORM.
The frame saves those values and restores them after RETURN. This works with
and without PROCEDURE and through recursive calls. The active ADDRESS
environment is copied into the frame and restored on return; the command RC
remains independent of the CALL result.

Instruction and numeric-work budgets are established once per VM execution
and are not reset at internal routine boundaries. Numeric operations in loop
control and every nested routine charge the same active work budget.

## DO integration and resource limits

The VM now executes bounded DO frames (maximum depth 64), including plain DO
groups, counted repetitions, controlled numeric loops with TO/BY/FOR, and
their compiler-emitted condition and jump structure. The DO stack depth is
saved on routine entry and restored on return, so a routine can run its own
loops or RETURN from inside a loop without losing the caller's active loop.
Controlled loops reload the control variable before comparison and increment,
so writes made by the loop body affect the next iteration. This supports
CALL inside loops, recursive functions, loop-local RETURN, and LEAVE/ITERATE
jumps to their compiler-patched locations.

Limits relevant to RX7 are:

- Internal routine depth: 64.
- Active DO depth: 64.
- Routine arguments: 256.
- Evaluation stack: existing maximum 1,048,576 entries.
- Variable tables: existing maximum 65,536 entries each.
- Instruction budget: existing 1,000,000 dispatches per execution.
- Numeric-work budget: existing 10,000,000 units per execution.

These bounds are compile-time VM constants. Depth exhaustion and allocation
failure produce deterministic VM errors, unwind active frames, and leave the
VM usable for a subsequent script.

## Compatibility matrix

| Feature | RX7 result | Qualification evidence |
|---|---|---|
| Case-insensitive, forward/backward internal labels | Supported | `backward_call`, routine fixtures |
| Duplicate labels | Supported; first source label wins | `duplicate_label` |
| Quoted routine names and built-in shadowing | Supported | `quoted_routine_name`, `quoted_builtin`, `builtin_shadowing` |
| CALL arguments, omitted positions, PARSE ARG | Supported | routine argument fixtures and C tests |
| RETURN value, RESULT lifecycle, RC isolation | Supported | `call_return_result`, C routine assertions |
| Internal functions and recursion | Supported | recursive factorial and missing-result fixtures |
| PROCEDURE scalar/stem isolation | Supported | `procedure_isolation` |
| EXPOSE scalar, stem, named compound, nested writes/DROPs | Supported | EXPOSE fixtures and C assertions |
| ARG count, value, E/O presence options | Supported | ARG fixtures, top-level CLI case |
| NUMERIC and current ADDRESS restoration | Supported | with- and without-PROCEDURE fixture/C cases |
| Counted/controlled DO and calls across loop frames | Supported for qualified forms | loop range and nested-call fixtures |
| `PROCEDURE EXPOSE (list)` | Explicitly unsupported | `procedure_expose_subsidiary` diagnostic |
| External routines, INTERPRET, SIGNAL, full TRACE | Out of scope | RX8 or later |

Duplicate-label and quoted-name behavior was checked against IBM's classic
REXX documentation. Regina was unavailable on this host, so RX7 differential
comparisons: **0 run**. No differential results are inferred.

Reference pages reviewed: [IBM instruction clauses and duplicate labels](https://www.ibm.com/docs/en/cics-ts/6.x?topic=concepts-clauses-instructions), [IBM CALL](https://www.ibm.com/docs/en/zos/2.5.0?topic=instructions-call), and [IBM ARG](https://www.ibm.com/docs/en/cics-ts/6.x?topic=functions-arg-argument).

## Hosted qualification

The final `tests/run_rexx_tests.ps1` run passed:

| Suite | Result |
|---|---:|
| RX1 runtime integration | 11/11 |
| Hosted CLI | 20/20 |
| RX2 VFS/output | 45/45 |
| RX2 numeric | 18/18 |
| RX2 shell bridge | 7/7 |
| RX3 decimal | 26/26 |
| RX3 allocator failures | 570/570 |
| RX3 fixtures | 13/13 |
| RX4 arithmetic | 53/53 |
| RX4 fixtures | 11/11 |
| RX5 numeric context | 35/35 |
| RX5 fixtures | 13/13 |
| RX6 allocation failures | 403/403 |
| RX6 compatibility/resource fixtures | 25/25 |
| RX7 internal-routine C assertions | 13 |
| RX7 parser allocation failures | 46/46 |
| RX7 compiler allocation failures | 28/28 |
| RX7 VM allocation failures | 103/103 |
| RX7 exact-output/resource fixtures | 33/33 |

The complete runner transcript is [`hosted-tests.log`](evidence/rx7/hosted-tests.log).
It records Regina as unavailable and all inherited suite results.

## Allocation and sanitizer evidence

Deterministic allocation sweeps cover parser AST creation, compiler bytecode
creation, routine/frame/argument/environment setup, DO-frame growth, live
EXPOSE resolution, return handling, and nested cleanup. The VM sweep runs a
script with a call inside nested DO loops and verifies both clean unwind and
successful recovery after each injected failure.

AddressSanitizer passed the 13 routine assertions and all allocation sweeps
(parser 46/46, compiler 28/28, VM 103/103). UndefinedBehaviorSanitizer passed
the 13 routine assertions. No sanitizer findings occurred. The UBSan Windows
link emitted LNK4217 warnings for sanitizer-runtime imports `_errno`,
`qsort`, `wcslen`, and `wcscat_s`; the executable completed successfully.

- [`asan-routines.log`](evidence/rx7/asan-routines.log)
- [`asan-allocations.log`](evidence/rx7/asan-allocations.log)
- [`ubsan-routines.log`](evidence/rx7/ubsan-routines.log)

## Native AMD64 and isolated guest

The AMD64 kernel linked as a freestanding ELF64 image. On this Windows host,
the standard make invocation did not expose MinGW's C++ headers under the
kernel's `-nostdinc++` setup. The successful build supplied the installed
MinGW C++ include directories explicitly:

```powershell
mingw32-make ARCH=amd64 `
  'EXTRA_CFLAGS=-IC:/mingw64/include/c++/15.2.0 -IC:/mingw64/include/c++/15.2.0/x86_64-w64-mingw32' `
  -C kernel
```

Final kernel SHA-256:
`2510840130A3A4C5E62DFE6191CD3D2F800394F0158A16974C8CA3A208141F1A`.
The exact build output is in [`amd64-build.log`](evidence/rx7/amd64-build.log)
and the hash is in [`kernel-sha256.txt`](evidence/rx7/kernel-sha256.txt).

An isolated QEMU guest used a copied, disposable ESP directory and firmware
copy, one CPU, 1 GB RAM, and `-net none`. The copied `ESP\kernel.elf` hash
matched the final build hash before boot; the serial log shows the UEFI loader
loading the image, and the running guest executed the RX7 scripts. Existing
QEMU processes were inventoried and left alone; only the process whose command
line, QMP port, and ESP matched this RX7 session was driven and stopped via
QMP. No production disk or ESP was modified.

Guest demonstrations and evidence:

- [`rx7-core-final.png`](evidence/rx7/rx7-core-final.png): CALL/RETURN and
  RESULT (`15`), function result (`25`), PROCEDURE isolation (`10`), EXPOSE
  write (`11`), and a CALL inside a controlled DO loop (`6`).
- [`rx7-context-final.png`](evidence/rx7/rx7-context-final.png): routine
  PARSE ARG, factorial (`120`), and restored NUMERIC values (`9`, `0`,
  `SCIENTIFIC`).
- [`rx7-depth-diagnostic.png`](evidence/rx7/rx7-depth-diagnostic.png): depth
  limit diagnostic.
- [`rx7-error-status.png`](evidence/rx7/rx7-error-status.png): guest shell
  status `1` after depth exhaustion.
- [`rx7-recovery-status.png`](evidence/rx7/rx7-recovery-status.png): recovery
  script succeeded and guest shell status returned to `0`.
- [`guest-serial.log`](evidence/rx7/guest-serial.log),
  [`kernel-identity.txt`](evidence/rx7/kernel-identity.txt),
  [`guest-media-manifest.txt`](evidence/rx7/guest-media-manifest.txt), and
  [`qemu-arguments.txt`](evidence/rx7/qemu-arguments.txt).

## Remaining compatibility limits and RX8 recommendation

RX7 does not support subsidiary `PROCEDURE EXPOSE (list)` syntax or external
routine loading. The existing unsupported diagnostic is covered by a fixture.
The suite also does not claim Regina differential compatibility because
Regina was unavailable. These are explicit limits; no other required RX7
acceptance item remains open.

Recommended RX8 scope: implement subsidiary exposure lists with IBM-compatible
name resolution and ordering, then qualify broader classic control flow and
condition handling (`SIGNAL`/conditions) without changing RX7 frame semantics.
