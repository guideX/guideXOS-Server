# guideXOS Server REXX — RX5 Numeric Context

**Outcome A — accepted.** RX5 adds classic `NUMERIC FUZZ`, `NUMERIC FORM`, and their built-ins while retaining the bounded decimal core. Hosted regression and RX5 fixtures pass, AddressSanitizer reports no errors, the freestanding AMD64 kernel builds, and an isolated QEMU guest executed the required scripts.

## Qualification state

| Item | Result |
|---|---|
| Repository | `D:\dev\guideXOSServer_REXX` |
| Branch | `REXX_SUPPORT` |
| Required starting HEAD | `25627ce9021d60bb4280eadb56412cc294a7c997` |
| Starting worktree | Clean; no untracked files |
| Qualified code/test ending HEAD | `59a44bb212d15dcb44946b7a12a2a3819ddf56b7` |
| Root `.phase` | Absent at preflight and left untouched |
| Production ESP/disk images | Not modified or attached to the RX5 guest |
| Branch topology / network | No switching, reset, fetch, pull, or push |

Compatibility target: ANSI REXX X3.274-1996 and traditional IBM REXX semantics.

The documentation closeout is committed as a documentation-only child of the code/test commit above. Its final repository HEAD is reported in the task response.

## Files changed

- Runtime: `rexx_parser.c/.h`, `rexx_compiler.c/.h`, `rexx_vm.c/.h`, and `rexx_numeric.c/.h`.
- Test drivers and runner: `tests/rexx_rx5_numeric_context_test.c`, `tests/rexx_rx3_alloc_failure_test.c`, and `tests/run_rexx_tests.ps1`.
- RX5 fixture pairs (`.rex` plus `.out` or `.diag`): `context_defaults_and_builtins`, `dynamic_form`, `form_formats`, `fuzz_comparisons`, `invalid_digits_below_fuzz`, `invalid_digits_reset_under_fuzz`, `invalid_form_argument_count`, `invalid_form_missing_value_keyword`, `invalid_form_value`, `invalid_fuzz_argument_count`, `invalid_fuzz_digits_boundary`, `invalid_fuzz_fraction`, and `invalid_fuzz_negative` under `tests/rexx_rx5/`.
- RX3 pending-setting fixtures: `tests/rexx_rx3/form_pending.rex` and `fuzz_pending.rex` now have `.out` expectations in place of the old unsupported-setting `.diag` files.
- Qualification report: `docs/rexx/REXX_RX5_NUMERIC_CONTEXT.md`.

## Audit and corrections

The RX3/RX3R1/RX4 reports and numeric/parser/compiler/VM code were reviewed. The implementation keeps `RexxDecimal` as a signed coefficient and scale with bounded storage; source values remain strings until a numeric operation needs conversion.

The audit found these RX5 gaps:

- The parser represented `NUMERIC DIGITS` but rejected `FUZZ` and `FORM`.
- The VM already had DIGITS and reserved FUZZ/FORM context fields, but did not implement FUZZ/FORM or their built-ins.
- Ordinary comparison subtracted exact decimal values at `DIGITS - FUZZ`; exact subtraction retained tiny differences, so FUZZ could not work by itself.
- The decimal formatter only selected scientific notation and had no active FORM parameter.
- Resetting DIGITS to its default did not validate against a nonzero FUZZ.

RX5 adds parser AST clauses and bytecode opcodes, validation and VM dispatch, and a FORM-aware entry point in the existing decimal formatter. The four-argument `rexx_decimal_format` C interface remains available and continues to select SCIENTIFIC form.

## NUMERIC FUZZ

`FUZZ` defaults to zero. `NUMERIC FUZZ` resets it to zero; an expression is evaluated and must be a nonnegative whole number less than the active DIGITS. Invalid assignments leave the previous setting unchanged. Changing or resetting DIGITS likewise fails atomically if the resulting DIGITS would not be greater than the current FUZZ.

Ordinary comparisons whose operands are both numeric use decimal arithmetic at temporary precision `DIGITS - FUZZ`: both operands are rounded to that precision and then subtracted. This implements the classic subtraction-based comparison rule without epsilon constants or binary floating point. FUZZ does not affect strict string comparisons, arithmetic precision, or stored source strings.

The IBM-documented five-digit example produces `0`, `1`, `1`, `0` for the two comparisons with FUZZ 0 and then 1. Native guest capture: [FUZZ comparison output](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-fuzz-clean.png).

## NUMERIC FORM and result formatting

SCIENTIFIC is the default; bare `NUMERIC FORM` restores it. The parser accepts direct SCIENTIFIC or ENGINEERING, `FORM VALUE expression`, and omitted VALUE only when the expression begins with syntax permitted by the grammar (for example a parenthesized expression). Symbol- and string-leading dynamic expressions require VALUE. FORM values are validated case-insensitively as SCIENTIFIC or ENGINEERING, and an invalid selection leaves the previous form unchanged.

FORM is passed through the existing result formatter for arithmetic and unary plus/minus paths, including division, integer division, remainder, and exponentiation. The formatter retains the pre-RX5 exponential-notation threshold; ENGINEERING only changes the exponent/mantissa layout after that threshold is reached. SCIENTIFIC has one digit before the decimal point. ENGINEERING floors the exponent to a multiple of three and emits one to three integer digits. The rounded decimal value determines the final exponent, including transitions at engineering boundaries. Exponent zero uses ordinary decimal notation. Changing FORM does not rewrite string values already stored in variables.

Examples at DIGITS 5:

| Input result | SCIENTIFIC | ENGINEERING |
|---|---:|---:|
| `12345678 + 0` | `1.2346E+7` | `12.346E+6` |
| `-0.000000123456 + 0` | `-1.2346E-7` | `-123.46E-9` |

Guest captures: [SCIENTIFIC](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-scientific.png), [ENGINEERING](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-engineering.png), and dynamic [FORM VALUE and reset](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-form-value.png).

## Built-ins and lifecycle

`DIGITS()`, `FUZZ()`, and `FORM()` accept zero arguments and return the active VM context. Other argument counts use the existing built-in argument error path. FUZZ’s value is returned as a VM-owned numeric string; FORM returns a VM-owned SCIENTIFIC or ENGINEERING string.

`rexx_vm_init` initializes each VM to DIGITS 9, FUZZ 0, and SCIENTIFIC. Separate VM instances remain isolated. The CLI creates and destroys a VM per invocation, so successive CLI executions do not inherit settings. If an embedder deliberately executes multiple bytecode chunks on the same VM, its variables and numeric context remain with that VM. Classic routine-call save/restore is not implemented; RX6 should add routine frames and save/restore DIGITS, FUZZ, and FORM together.

## Tests and resource limits

The RX5 suite has a 35-assertion C test and 13 ordinary REXX fixtures. It covers defaults and built-ins, FUZZ validation and reset, DIGITS/FUZZ boundaries, the IBM comparison example, negative/magnitude/rounding cases, strict comparisons, arithmetic precision, both forms, exponent signs, engineering rounding, dynamic FORM, invalid settings, VM isolation, allocation failures, and instruction/numeric-work limits. Two RX3 pending-setting fixtures now assert supported behavior instead of expecting the old unsupported diagnostic; no unrelated fixture was removed.

The full hosted command `powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_rexx_tests.ps1` exited 0:

| Suite | Result |
|---|---:|
| RX1 runtime | 11 passes |
| Hosted CLI | 16/16 |
| RX2 VFS/output | 45 assertions |
| RX2 freestanding numeric | 18 assertions |
| RX2 shell bridge | 7 assertions |
| RX3 decimal | 26 assertions |
| RX3 compatibility | 13/13 fixtures |
| RX3 allocation failure | 570 assertions |
| RX4 arithmetic | 53 assertions |
| RX4 compatibility | 11/11 fixtures |
| RX5 numeric context | 35/35 assertions |
| RX5 compatibility | 13/13 fixtures |
| Regina differential | 0 cases; Regina unavailable |

Microsoft Visual Studio 18 x64 AddressSanitizer ran the RX5 numeric-context suite (35/35) and the extended allocator-failure suite (570 assertions) with no sanitizer report. The logs are retained under the ignored `tmp` qualification directory.

The bounded engine remains unchanged in kind: maximum DIGITS is 254; decimal coefficient storage is 512 digits; numeric input is bounded to 512 bytes; exponent/scale bounds are ±1,000,000; VM numeric-work and instruction budgets remain enforced. RX5 adds no floating-point path or unbounded allocation.

## Native AMD64 guest qualification

A full freestanding AMD64 build completed with exit code 0 in `kernel/build/rexx-rx5-qualification-evidence-amd64`. The MbedTLS profile reports PASS. The resulting `kernel.elf` is 2,756,096 bytes with SHA-256 `0BBEB96DA51AC8432091196002532587C6D84103499F6D78E7BD5A9C4DCF589A`; that hash matches the kernel copied into the guest ESP. The build log is `tmp/rexx-rx5-native-20261016-151648/native-kernel-build.log`.

The guest booted from a copied scratch ESP using QEMU PID 9408, loopback QMP port 54947, and `-net none`. Process command line, QMP ownership, arguments, serial output, and shutdown verification are retained in the same ignored scratch directory. No production ESP or disk image was attached. The guest was shut down through its verified QMP connection after capture.

The real Console opened all seven RX5 VFS scripts and displayed:

- Defaults and built-ins: `9`, `0`, `SCIENTIFIC`.
- IBM FUZZ sample: `0`, `1`, `1`, `0`.
- Strict string equality while FUZZ was nonzero: ordinary numeric equality returned `1`; strict string equality returned `0`.
- SCIENTIFIC: `1.2346E+7`, `-1.2346E-7`.
- ENGINEERING: `12.346E+6`, `-123.46E-9`.
- Dynamic FORM VALUE and reset: `ENGINEERING`, `12.346E+6`, `SCIENTIFIC`.
- Invalid setting: `invalid NUMERIC FUZZ setting`; `echo $?` returned `1`.
- Recovery: a following `echo guestresponsive` printed `guestresponsive`; serial heartbeat continued.

Console screenshots: [defaults](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-default.png), [FUZZ](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-fuzz-clean.png), [strict comparison](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-strict.png), [SCIENTIFIC](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-scientific.png), [ENGINEERING](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-engineering.png), [FORM VALUE](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-form-value.png), [diagnostic](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-invalid.png), [exit status](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-status.png), and [recovery](../../tmp/rexx-rx5-native-20261016-151648/guest-rx5-recovered.png). The serial log and full hosted/ASAN/build logs are also retained there.

## Remaining classic REXX gaps and RX6 direction

This phase does not add CALL/PROCEDURE/EXPOSE routine frames or routine-level numeric-context save/restore. The bounded parser and VM also retain the broader unsupported language features recorded in the RX3/RX4 reports; Regina differential verification was not available. RX6 should focus on routine frames and context save/restore before expanding unrelated REXX syntax.

Compatibility references: [IBM NUMERIC instruction](https://www.ibm.com/docs/en/zos/2.5.0?topic=instructions-numeric), [IBM numeric comparisons](https://www.ibm.com/docs/en/zos/2.5.0?topic=definition-numeric-comparisons), and [IBM arithmetic operators](https://www.ibm.com/docs/en/zos/2.5.0?topic=definition-arithmetic-operators).
