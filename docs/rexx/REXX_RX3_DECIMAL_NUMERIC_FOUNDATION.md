# guideXOS Server REXX — RX3 Decimal Numeric Foundation

**Outcome: B — partial; keep RX3 open.** The bounded decimal core, string-preserving source values, supported arithmetic/comparisons, and `NUMERIC DIGITS` are implemented and verified in hosted and freestanding builds. RX3 is not marked accepted because decimal division remains unsupported, Regina differential testing was unavailable, allocator-failure injection was unavailable, and no guest run was safe while a QEMU process was already active.

## Repository state

| Item | RX3 state |
|---|---|
| Repository | `D:\dev\guideXOSServer_REXX` |
| Branch | `REXX_SUPPORT` (unchanged) |
| Starting HEAD | `7a33c7b1929bf2da29a60cf9a67d9187ab727278` |
| Starting worktree | Clean; no modified or untracked files |
| Ending state | RX3 changes committed locally on `REXX_SUPPORT`; working tree clean after commit |
| Root `.phase` | Not changed |
| ESP and disk images | Preserved; no existing ESP or disk image was rewritten |
| Existing QEMU guests | Left untouched |

The preflight inventory found existing build outputs, `ESP\ramdisk.img`, `disks\test-fat32.img`, and `disks\test-ext4.img`. The RX3 kernel output was placed in the isolated `kernel\build\rexx-rx3-amd64` directory. Regina was not available on `PATH`. A `qemu-system-x86_64` process was active during qualification, so no guest was started or controlled.

## Existing implementation audit

RX1 and RX2 reports describe `RexxValue` as a tagged union containing `double`, string, boolean, and unset variants. Numeric constants were decoded through `rexx_numeric_parse`; arithmetic converted operands to `double`; results were formatted with `%.15g`; comparisons used binary numeric values or byte ordering. The hosted path used `strtod` and `snprintf`; the freestanding path approximated the same binary model without the hosted C library. RX2's native numeric tests covered conversion and formatting helpers, not classic decimal REXX arithmetic.

RX3 changed the REXX execution path as follows:

| Site | RX3 behavior |
|---|---|
| Numeric source literals | Compiler constants retain their original token spelling; the VM stores the token as an owned string. |
| Quoted strings and variables/stems | Continue to own and copy their character bytes. Assigning `"001.00"` preserves those bytes. |
| Addition, subtraction, multiplication, unary `+`/`-` | Parse source text into `RexxDecimal`, operate in base 10, and store a formatted result string. |
| Numeric comparisons | Use decimal values and subtraction at the active comparison precision. |
| Strict comparisons | Compare original rendered strings byte by byte, without numeric conversion or right padding. |
| Ordinary nonnumeric comparisons | Compare strings with right-side blank padding. |
| Built-in numeric arguments/results | Numeric argument recognition is decimal; `LENGTH`, `WORDS`, `POS`, and `DIGITS()` return string values. |
| `RC` and CLI `EXIT` | `RC` is stored as decimal text; CLI `EXIT` conversion uses decimal parsing and exact `int32_t` range/integrality checks. |
| C embedding API | The legacy `REXX_VALUE_NUMBER`/`rexx_value_set_number(double)` arm remains for callers that explicitly inject a C double. It is not used for REXX source literals or supported REXX arithmetic. |

The old `rexx_numeric_parse`/`rexx_numeric_format` double APIs remain for that embedding boundary and RX2 adapter tests. They are not the authoritative numeric representation for script execution.

## Decimal representation and bounds

`RexxDecimal` is a fixed-size, freestanding value:

- A sign of -1, 0, or +1.
- Up to 512 coefficient digits stored most-significant first.
- A signed base-10 scale, with value `sign × coefficient × 10^-scale`.
- Trailing coefficient zeroes and scale are retained so arithmetic can preserve significant trailing zeroes.

The parser accepts decimal digits, an optional sign, an optional decimal point, optional exponent notation, and surrounding ASCII blanks. It rejects malformed and nonnumeric text without first converting through binary floating point. Numeric input is capped at 512 bytes, coefficient storage at 512 digits, and the absolute decimal scale/exponent at 1,000,000. A VM may set `NUMERIC DIGITS` from 1 through 254. Requests outside those bounds fail with a specific numeric-setting or resource diagnostic.

The coefficient and temporary arithmetic arrays are fixed-size stack storage; decimal parsing and the core add/subtract/multiply algorithms do not allocate. Formatting the resulting REXX string and storing it still use the VM's existing allocator. No allocator fault-injection facility exists in the current test harness, so allocation-failure paths were not injected.

To keep large values bounded, each VM execution has a 10,000,000-unit decimal-work budget in addition to the existing bytecode instruction budget. Multiplication charges coefficient-length work; addition/subtraction and numeric comparison charge input and alignment work. Exhaustion returns the VM resource-limit error. The 254-digit limit also bounds the maximum multiplication to a small fixed coefficient operation.

## Classic numeric rules and RX3 coverage

ANSI X3.274-1996 establishes the REXX numeric model; IBM's reference describes the detailed arithmetic rules used here. Before an arithmetic operation, terms are limited to `DIGITS + 1` significant digits. The operation is carried out in decimal, then rounded to `DIGITS` using the guard digit: 5–9 rounds up, 0–4 rounds down. Addition, subtraction, and multiplication preserve significant trailing zeroes; an arithmetic zero is rendered as `0`. Numeric comparisons are performed by subtracting the terms and comparing the difference with zero. `FUZZ` lowers the comparison precision. Strict operators compare the strings exactly and do not perform numeric conversion.

RX3 implements the following bounded subset:

| Semantic area | RX3 behavior |
|---|---|
| String-oriented values | Implemented for source literals, quoted strings, assigned variables/stems, arithmetic results, built-in numeric outputs, `RC`, and retained `EXIT` values. |
| Numeric string recognition | Exact decimal parse; optional sign, point, exponent, and surrounding blanks; malformed strings fail when arithmetic requires a number. The tested parser also accepts blanks after a sign. |
| Addition/subtraction/multiplication | Decimal coefficient/scale arithmetic, with `DIGITS + 1` input truncation and final precision rounding. |
| Unary positive/negative | Implemented through decimal parsing and result rounding; negative zero becomes arithmetic zero. |
| Numeric comparisons | All compiled equality/inequality and relational comparison opcodes use decimal comparison when both terms are numeric. `FUZZ` is fixed at zero in RX3. |
| Strict comparisons | All compiled strict equality/inequality and relational comparison opcodes compare strings exactly, with no numeric coercion or blank padding. |
| Significant precision | Default `DIGITS=9`; `NUMERIC DIGITS expression` changes it for later operations in that VM. Omission resets it to 9. `DIGITS()` exposes the active setting. Maximum implemented precision is 254. |
| Rounding | Traditional guard-digit rounding, not banker’s rounding. Verified at a carry boundary and at a negative tie. |
| Numeric formatting | Fixed notation is used unless integer places exceed `DIGITS` or fractional scale exceeds `2 × DIGITS`; then scientific notation with uppercase `E` and a signed exponent is used. Arithmetic results preserve coefficient zeroes except that zero is `0`. |
| Exponent notation | Exact decimal parse and bounded scientific output; scale/exponent magnitude is limited to 1,000,000. |
| `NUMERIC FUZZ` | Explicit parser diagnostic; it is not accepted as an executed statement. `FUZZ()` remains unsupported. |
| `NUMERIC FORM` | Explicit parser diagnostic; only the default SCIENTIFIC context is in effect. `FORM()` remains unsupported. |
| Division, integer division, remainder, power | The parser/compiler may emit their opcodes, but the VM reports `opcode is not implemented by this VM`. No binary floating-point fallback is used. |

IBM describes the standard defaults as DIGITS 9, FUZZ 0, and FORM SCIENTIFIC. RX3 initializes all three fields per VM, but only DIGITS is mutable. Routine/procedure inheritance and restoration are not yet observable because routine frames are not implemented.

## Automated compatibility suite

Ordinary `.rex` fixtures and expected `.out`/`.diag` files are in [`tests/rexx_rx3`](../../tests/rexx_rx3). The suite covers:

- `0.1 + 0.2`, `1.25 + 2.75`, subtraction, multiplication, and unary operators.
- Precision changes, the default reset, `DIGITS()`, and invalid/fractional DIGITS requests.
- String preservation (`001.00` remains unchanged until arithmetic), leading/trailing blanks, and exponent input.
- Numeric and strict comparisons, right-padded nonnumeric comparisons, equality/inequality, and relational operators.
- Zero/negative zero, large integers, small fractions, exponential output, and rounding carry/tie boundaries.
- Invalid numeric strings, explicit pending division/FUZZ/FORM diagnostics, decimal input limits, and the per-execution numeric-work limit.

The C decimal assertions also cover whole-number conversion, negative-zero normalization, malformed exponents, exact equivalent decimal forms, and the fixed input resource limit. Memory-allocation failure injection is not available; decimal core operations themselves use fixed buffers, while VM string storage still follows existing allocation-failure handling.

## Verification results

Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_rexx_tests.ps1` successfully after the final code changes:

| Suite | Result |
|---|---:|
| RX1 runtime | 11/11 assertions |
| RX1 CLI | 16/16 cases |
| RX2 VFS/output integration | 45 assertions |
| RX2 freestanding numeric adapter | 18 assertions |
| RX2 shell bridge | 7 assertions |
| RX3 decimal foundation | 26 assertions |
| RX3 `.rex` compatibility fixtures | 13/13 cases |
| Regina differential | 0 run; Regina unavailable |

The RX3 output fixtures verify actual CLI output. Division, FUZZ, FORM, invalid numeric input, invalid DIGITS, and numeric-work exhaustion are expected diagnostics rather than false success cases.

## Native AMD64 build and guest qualification

The full kernel Makefile completed successfully with the new REXX C files compiled freestanding and linked into:

- `kernel\build\rexx-rx3-amd64\bin\kernel.pe`
- `kernel\build\rexx-rx3-amd64\bin\kernel.elf`

The build used `mingw32-make -C kernel -j4 ARCH=amd64` with isolated `BUILD_DIR`, `OBJ_DIR`, `BIN_DIR`, and `KERNEL` paths under `kernel\build\rexx-rx3-amd64`, plus the C++ include directories recorded in the RX2 report. This verifies native compilation and linking, not guest runtime.

No QEMU smoke test was run: a `qemu-system-x86_64` process was already active at the safety check. No QEMU process, ESP, or disk image was touched. Native runtime output therefore remains unqualified in RX3.

## Remaining incompatibilities

- `/`, `%`, `//`, and `**` remain explicit unsupported-opcode errors.
- `NUMERIC FUZZ` and `NUMERIC FORM`, plus `FUZZ()` and `FORM()`, are not implemented.
- Routine/procedure frames and numeric-setting inheritance/restoration are not implemented.
- The 254-digit precision ceiling, 512-byte numeric input limit, ±1,000,000 exponent/scale bound, and decimal-work budget are implementation limits rather than unlimited classic REXX numeric capacity.
- Regina comparison was unavailable, and allocator-failure paths were not fault-injected.
- Guest execution could not be safely qualified while another QEMU process was active.
- Other pre-existing language gaps remain, including internal routine calls, many built-ins, and several logical/control-flow opcodes.

## Recommended RX4 scope

Implement decimal division, integer division, remainder, and bounded integer exponentiation using the same decimal/context layer; add `NUMERIC FUZZ` and `NUMERIC FORM` with their built-ins; and add routine-frame save/restore for the numeric context. Add allocator-failure injection, broaden differential runs against Regina, and perform an isolated AMD64 guest smoke when no unrelated QEMU guest is active.

## References

- [ANSI X3.274-1996 REXX standard information and approved draft](https://rexx.oorexx.org/rexxlang/standards/index.srsp)
- IBM, [Arithmetic operators and general precision/rounding rules](https://www.ibm.com/docs/en/cics-ts/6.x?topic=operations-arithmetic-operators)
- IBM, [Basic arithmetic operation rules](https://www.ibm.com/docs/en/cics-ts/6.x?topic=operations-arithmetic-operation-rules-basic-operators)
- IBM, [NUMERIC DIGITS precision](https://www.ibm.com/docs/en/cics-ts/6.x?topic=operations-precision)
- IBM, [Numeric comparisons and FUZZ](https://www.ibm.com/docs/en/zos/3.1.0?topic=definition-numeric-comparisons)
- IBM, [Strict and ordinary comparison operators](https://www.ibm.com/docs/en/cics-ts/5.6.0?topic=operators-comparison)
- IBM, [Numeric notation and output rules](https://www.ibm.com/docs/en/cics-ts/6.x?topic=reference-numbers-arithmetic-operations)
