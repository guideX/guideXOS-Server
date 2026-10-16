# guideXOS Server — Phase RX4: Classic REXX Arithmetic

## Phase record

- Required branch: `REXX_SUPPORT`.
- Starting HEAD: `f11a8286b25001939ef86fc14b3bed6af53452fa`.
- Starting worktree: clean; no RX4 implementation was present.
- Ending state: `HEAD` is the local RX4 closeout commit on `REXX_SUPPORT`; worktree clean; root `.phase` absent and untouched. The exact HEAD is recorded in the phase report.
- No branch changes, pushes, production ESP changes, or production disk-image changes were made.

## Architecture audit

RX3 stores source values as strings and parses numeric operands into `RexxDecimal`: a signed, most-significant-first coefficient, decimal scale, and fixed 512-digit storage. The VM defaults to `NUMERIC DIGITS 9`, allows at most 254 digits, and has a separate 10,000,000-unit numeric-work budget. RX3/RX3R1 documented the existing decimal representation and allocation-failure model in [RX3](REXX_RX3_DECIMAL_NUMERIC_FOUNDATION.md) and [RX3R1](REXX_RX3R1_QUALIFICATION_CLOSEOUT.md).

The lexer and parser already recognized `/`, `%`, `//`, and `**`; the parser produced the corresponding AST operators and the compiler emitted `OP_DIVIDE`, `OP_INTEGER_DIVIDE`, `OP_REMAINDER`, and `OP_POWER`. The missing work was VM dispatch and decimal arithmetic. The existing parser gives prefix operators higher precedence than power, power higher precedence than multiplicative operators, and evaluates equal-precedence binary operators left to right. This matches [IBM REXX/VM operator precedence](https://www.ibm.com/support/pages/zvm/library/710pdfs/71631400.pdf#page=30). RX4 preserves that behavior: `-2 ** 3` is `-8`, `-2 ** 2` is `4`, and `2 ** 3 ** 2` is `64`.

The implementation keeps all source arithmetic in the decimal path. The legacy `double` adapters remain available only to RX2 C embedders and are not called by these operators.

## Operator behavior and design

### `/` — decimal division

Division truncates both operands to `DIGITS + 1` significant digits, then uses base-10 long division to generate a leading quotient plus a guard digit. It rounds traditional half-up to the active precision, normalizes the decimal scale, and removes insignificant trailing zeros. No binary floating-point calculation is used.

Division handles exact and repeating results, signed operands, division by zero, scale overflow, and work exhaustion. A zero result formats as `0`. Examples at the default precision are `10 / 2 -> 5`, `7 / 2 -> 3.5`, `2 / 3 -> 0.666666667`; at `DIGITS 5`, `1 / 3 -> 0.33333`. `2.40 / 2` formats as `1.2`.

### `%` and `//` — integer division and remainder

The quotient is formed by decimal long division of absolute coefficients; its sign is the sign of ordinary division. Fractional quotient digits are discarded without deriving the result from rounded `/`. A quotient that needs more integer digits than `NUMERIC DIGITS` is an error. This also makes `//` fail whenever the corresponding `%` operation would fail.

The remainder is calculated as the dividend minus quotient times divisor, using decimal arithmetic. A nonzero remainder keeps the dividend's sign, regardless of the divisor's sign. Fractional input scale is retained where needed by the residue: `3.6 // 1.3 -> 1.0`, `10 // 0.3 -> 0.1`, and `-7 // 2 -> -1`. Other checks include `7 % 2 -> 3`, `-7 % 2 -> -3`, `7 % -2 -> -3`, and `7.5 % 2 -> 3`.

These rules follow IBM REXX/VM's additional-operator definition: `%` uses absolute operands and the sign of normal division; its quotient must fit within `DIGITS`; `//` is the residue of that integer division and a nonzero residue has the dividend's sign. IBM also notes that `%` need not match truncating a rounded `/` result. See [IBM REXX/VM Reference, arithmetic operators, pp. 155–157](https://www.ibm.com/support/pages/zvm/library/710pdfs/71631400.pdf#page=175).

### `**` — integer power

The exponent must be an exact whole number. The absolute exponent is bounded at 1,000,000. Power uses left-to-right binary reduction, with intermediate precision `DIGITS + L + 1`, where `L` is the number of digits in the exponent's integer part. A negative exponent computes the reciprocal with decimal division. The final result is rounded to `DIGITS` and insignificant trailing zeros are removed.

`0 ** 0` returns `1`; a zero base with a negative exponent is an error. Negative bases preserve the expected odd/even sign. Checks include `2 ** -2 -> 0.25`, `-2 ** 3 -> -8`, `2 ** 3 ** 2 -> 64`, and at `DIGITS 5`, `1.7 ** 8 -> 69.758`. The binary-reduction algorithm, intermediate precision, and examples follow [IBM REXX/VM Reference, pp. 155–157](https://www.ibm.com/support/pages/zvm/library/710pdfs/71631400.pdf#page=175). The [ANSI X3.274-1996 approved draft](https://rexx.oorexx.org/rexxlang/standards/j18pub.pdf) is the language-standard reference used for numeric operand rules.

## Formatting and source-string preservation

Variables keep their original string bytes until an operation needs a number. In the compatibility fixture, `a = "001.00"` followed by `say a` still prints `001.00`; `say a / 2` prints the arithmetic result `0.5`. Division and power remove insignificant zeros after rounding; the existing addition, subtraction, and multiplication formatting remains unchanged.

The existing decimal formatter supplies leading zeroes before a fractional point, renders zero as `0`, and applies the current default scientific-output policy for extreme magnitudes. `NUMERIC FORM` configuration is outside RX4.

## Resource and error bounds

- Coefficients remain fixed arrays of at most 512 digits; input numeric text remains limited to 512 bytes.
- `NUMERIC DIGITS` remains bounded at 254. Internal power precision uses the existing coefficient headroom and does not raise the VM setting limit.
- Decimal exponent/scale magnitude is bounded at 1,000,000; power exponent magnitude is independently bounded at 1,000,000.
- The VM's numeric-work budget remains 10,000,000. RX4 charges generated quotient digits and repeated long-division compare/subtract steps, power iterations and multiplications, reciprocal division, remainder multiplication/scale alignment/subtraction, operand scans, and formatted result length.
- Arithmetic temporaries use bounded stack storage. The numeric core performs no heap allocation. VM string result allocations continue through the existing ownership and cleanup paths.
- Division by zero, invalid numeric text, fractional exponents, zero to a negative exponent, unrepresentable `%`/`//` quotients, out-of-range scales, and exhausted work return VM errors. Resource exhaustion reports `VM resource limit exceeded`; invalid/range arithmetic uses the current generic numeric diagnostic.

## Compatibility matrix

| Operator | Required behavior | Qualified examples |
| --- | --- | --- |
| `/` | Decimal long division, `DIGITS + 1` guard, half-up rounding, strip insignificant zeros | `2 / 3 = 0.666666667`; `1 / 3` at 5 digits is `0.33333`; `2.40 / 2 = 1.2` |
| `%` | Integer quotient toward zero, sign of ordinary division, quotient fits `DIGITS` | `-7 % 2 = -3`; `7 % -2 = -3`; `999999999.6 % 1 = 999999999` even though rounded `/` would reach `1000000000`; `10000000000 % 3` errors at 9 digits |
| `//` | Residue after `%`; nonzero sign follows dividend; fractional operands allowed | `-7 // 2 = -1`; `10 // 0.3 = 0.1`; `3.6 // 1.3 = 1.0` |
| `**` | Whole exponent, binary reduction, decimal reciprocal for negative exponents | `0 ** 0 = 1`; `2 ** -2 = 0.25`; `1.7 ** 8 = 69.758` at 5 digits |

## Tests and qualification results

`tests/rexx_rx4_arithmetic_test.c` adds 53 C-level assertions covering exact/repeating division, precision, rounding carry and sign, `%`, `//`, fractional operands, power, negative/zero cases, range and quotient limits, maximum precision, input-size boundaries, numeric-work exhaustion, VM stack cleanup, and reuse after failure. Four RX4 output fixtures and seven diagnostic/resource fixtures bring the RX4 fixture suite to 11 cases. RX3's previous `division_pending` diagnostic was replaced by the correct `0.33333` output expectation; the remaining unsupported-feature checks were preserved.

The full `tests/run_rexx_tests.ps1` rerun passed:

- RX1 runtime: 11 checks.
- RX1 CLI: 16/16.
- RX2 VFS/output: 45 assertions.
- RX2 freestanding numeric: 18 assertions.
- RX2 shell bridge: 7 assertions.
- RX3 decimal: 26 assertions.
- RX3 compatibility: 13/13 fixtures.
- RX3R1 allocation-failure suite: 529 assertions (up from the prior 361, including new `/`, `%`, `//`, and `**` failpoint matrices).
- RX4 arithmetic: 53 assertions; RX4 fixtures: 11/11.

Regina REXX was not installed or available on `PATH`; the runner recorded zero Regina differential cases for RX3 and RX4. Fixture expectations use the IBM-derived behavior described above.

AddressSanitizer could not be run in this environment. The available MinGW GCC 15.2 compiler failed the link probe with `ld.exe: cannot find -lasan`; neither Clang nor MSVC `cl` was available. The allocation-failure and VM recovery tests did pass, but they are not represented as sanitizer coverage.

## Native AMD64 build and QEMU guest

The real AMD64 kernel build completed successfully using the RX2 toolchain's required C++ include paths and a dedicated ignored output directory. The RX4 kernel at `kernel/build/rexx-rx4-qualification-20261016/bin/kernel.elf` was 2,751,800 bytes and was copied to the isolated scratch ESP for boot. The successful log is `tmp/rexx-rx4-qualification-20261016/native-kernel-build-with-cxx-includes.log`.

A genuine plain-text `.rex` acceptance script was executed through the native guideXOS Console in a QEMU guest booted from a copied RX3R1 test ESP, with the RX4 kernel overlaid. No production ESP or disk images were used. The guest confirmed:

1. `/`, `%`, `//`, and `**` results, including signed remainder and precision changes.
2. The script's `exit 0` propagated as shell status `0`.
3. Division by zero returned a runtime diagnostic and shell status `1` without a kernel crash.
4. Numeric-work exhaustion returned `VM resource limit exceeded` and shell status `1`.
5. A subsequent `echo rx4-responsive` command succeeded; serial heartbeat continued and the guest remained responsive.

Captured local evidence is under the ignored scratch directory `tmp/rexx-rx4-qualification-20261016/`: `guest-console-transcript.txt`, `serial.log`, `qemu-owner.txt`, `guest-media-manifest.txt`, `acceptance-status2.png` (successful arithmetic and status 0), `recovery-command.png` (error statuses and subsequent command), and both kernel build logs. QEMU was shut down through the verified owned QMP endpoint after the run; its PID exited and its listener closed.

## Remaining language limits and RX5 recommendation

RX4 completes the four arithmetic operators on the existing numeric core. `NUMERIC FUZZ`, configurable `NUMERIC FORM`, and associated built-ins remain out of scope, as do the broader procedure/routine and host-environment features already identified in earlier phase reports. Large/small arithmetic remains bounded by the current coefficient, scale, input, and work limits.

Recommended RX5 work: implement and test `NUMERIC FUZZ` and `NUMERIC FORM` against ANSI and IBM references, then add built-ins that depend on those settings. Keep Regina differential tests in the normal qualification path when an interpreter becomes available.


