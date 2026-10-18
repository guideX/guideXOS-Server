# RX14 Numeric, Inspection, and Character-Range Built-ins

RX14 implements nine classic REXX built-ins on the existing decimal and VM foundations: `ABS`, `SIGN`, `MAX`, `MIN`, `TRUNC`, `FORMAT`, `DATATYPE`, `SYMBOL`, and `XRANGE`.

## Qualification record

- Branch: `REXX_SUPPORT`
- Starting HEAD: `0d67ef1130d11902deea41edecd19dd7dbc24baa`
- Qualification source HEAD: `0d67ef1130d11902deea41edecd19dd7dbc24baa`. The final local evidence commit becomes the repository's ending HEAD; its SHA is reported in the final report.
- The RX14 preflight found the required starting commit, no prior RX14 implementation, no tracked RX13 changes, and no root `.phase` file.
- The RX14 protected-artifact starting manifest records the current state of all 31 root `.obj`/`.pdb` files, including 28 untracked artifacts. It matches the preserved RX12R1 post-incident manifest. This is preservation of that recorded current state, not a recovery of earlier bytes.
- Hosted, sanitizer, native-build, and QEMU guard stages each verified all 31 paths and hashes. The final comparison is in `evidence/rx14/protected-artifacts-final.json` and `.csv`.
- Root `.phase` remained absent. Builds and temporary outputs used phase-owned `build/rexx/rx14` directories; durable reports and screenshots are in `evidence/rx14`.

## Implementation

All nine functions are dispatched through the VM-aware built-in path. Numeric source operands stay decimal strings and use `RexxDecimal`, whose value is a sign, a most-significant-first decimal coefficient, and a base-10 scale. The numeric engine bounds coefficients and input strings at 512 digits/bytes, numeric precision at 254 digits, and exponent magnitude at 1,000,000. The RX14 numeric built-ins do not convert source values through `float`, `double`, `long double`, `strtod`, or locale formatting.

Before a numeric built-in applies its operation, its numeric input is parsed and rounded under the active `NUMERIC DIGITS`. Numeric result strings use the active `NUMERIC FORM` (SCIENTIFIC by default, or ENGINEERING). `MAX` and `MIN` compare the prepared decimals exactly after DIGITS rounding; `NUMERIC FUZZ` does not make distinct extrema equal. Their comparison behavior is exercised with FUZZ enabled.

| Function | RX14 behavior and coverage |
|---|---|
| `ABS(number)` | Applies DIGITS rounding, removes a negative sign, and canonicalizes negative zero to zero. |
| `SIGN(number)` | Returns exactly `-1`, `0`, or `1` after DIGITS rounding, including values rounded to zero. |
| `MAX` / `MIN` | Require 2–20 arguments, round each decimal under DIGITS, compare numerically across scales, and return the selected decimal under current FORM. The 20-argument ceiling follows the documented implementation limit and bounds work. |
| `TRUNC(number[,n])` | Rounds the input under DIGITS first, then truncates toward zero. It emits fixed-point output, extends fractional zeros to `n`, defaults `n` to zero, and rejects negative or oversized `n`. |
| `FORMAT(number[,[before][,[after][,[expp][,expt]]]])` | Implements all five arguments and omitted middle arguments. It handles field padding/overflow, fraction rounding and zero extension, exponent triggering, exponent width and zero padding, `expp=0` suppression, SCIENTIFIC/ENGINEERING FORM, and blank exponent columns for zero exponent with explicit nonzero `expp`. |
| `DATATYPE(string[,type])` | Implements default `NUM`/`CHAR` and options A, B, L, M, N, S, U, W, and X. It uses the shared decimal parser for numeric classifications; W observes current DIGITS. X accepts empty text and spaces only between complete byte pairs. Option matching is case-insensitive and uses the first option character. |
| `SYMBOL(name)` | Returns `BAD`, `LIT`, or `VAR`. Numeric constants are literals. Names are validated before lookup; lookup is read-only, does not create variables or signal NOVALUE, resolves dynamic compound tails in the active variable generation, and observes stem defaults and PROCEDURE EXPOSE. |
| `XRANGE([start][,end])` | Defaults to bytes `00` through `FF`, requires each supplied endpoint to be exactly one byte, returns inclusive byte sequences, and wraps modulo 256 when start exceeds end. |

The common built-in result cap is the VM parse-source limit of 16,777,216 bytes. FORMAT and TRUNC check arithmetic and output sizes before allocation; numeric work is charged to the VM's bounded work budget. XRANGE is limited to 256 result bytes. MAX/MIN cap argument count at 20. Allocation failures leave argument values intact, release owned output, and permit reuse after failure.

## Semantics and limits

IBM's classic rules are followed for numeric preprocessing, TRUNC's round-then-truncate order, FORMAT's full parameter list and exponent conventions, DATATYPE's portable core options, SYMBOL's variable status, and XRANGE's byte sequence. The RX14 cases were checked against the supplied IBM documentation set:

- [IBM built-in functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-built-in)
- [IBM FORMAT](https://www.ibm.com/docs/en/zvm/7.2.0?topic=functions-format)
- [IBM DATATYPE](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-datatype)
- [IBM SYMBOL](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-symbol)
- [IBM TRUNC](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-trunc-truncate)
- [IBM XRANGE](https://www.ibm.com/docs/en/zvm/7.2.0?topic=functions-xrange-hexadecimal-range)
- [ANSI X3.274-1996 draft](https://www.rexxla.org/rexxlang/standards/j18pub.pdf)

DATATYPE's IBM-specific DBCS C/D options are not implemented: the current runtime is byte-oriented and has no DBCS character model. RX14 provides the portable ASCII/byte core listed above and does not claim DBCS support. The existing C embedding `RexxValue` double adapter remains for older interfaces; RX14 numeric BIFs reject a numeric-double value instead of converting it back to decimal. REXX source values, which enter as strings, use the decimal path.

The implementation uses ASCII symbol and alphabetic classification, consistent with the runtime's current byte-oriented lexical model. Multibyte character classification is outside RX14. Regina was not installed or otherwise available, so no independent Regina comparison is claimed.

## Tests and qualification evidence

Hosted RX14 direct tests passed **333 assertions** across **88 direct cases**, including **16 error cases**, allocation failure and recovery for each of the nine functions, maximum MAX/MIN arity, byte-preserving XRANGE wrap, VM reuse after errors, nested and ordinary INTERPRET, PROCEDURE EXPOSE, stem defaults, NUMERIC DIGITS/FUZZ/FORM, syntax recovery, and terminal resource-limit behavior. All **29 RX14 REXX fixtures** passed with byte-exact output and diagnostic expectations.

The inherited hosted regression run passed all suites: CLI 20/20; RX3 13/13; RX4 11/11; RX5 13/13; RX6 25/25; RX7 33/33; RX8 26/26; RX9 24/24; RX10 19/19; RX12 30/30; RX13 33/33. The C assertion suites also passed, including RX2 VFS/output 45, freestanding numeric 18, RX3 decimal 26 and allocator failure 570, RX4 arithmetic 53, RX5 context 35, RX6 allocator failure 439, RX7 routines 13 and parser/compiler/VM allocation 46/28/111, RX8 semantic/allocation/recovery 279 and allocation 38/21/50, RX9 conditions 467 and allocation 41/37/78, RX10 INTERPRET 257 and nested-VM/SYNTAX-VM/parser/compiler failpoints 33/14/8/11, RX12 BIFs 320, RX13 BIFs 418, and RX2 shell bridge 7.

ASan passed all 17 build/test targets. UBSan passed all 17 build/test targets. Each sanitizer's 170-case differential corpus recorded Regina `NOT_RUN` and guideXOS's expected local exit for every case; sanitizer findings were zero. Summaries and durable logs are under `evidence/rx14/sanitizer-address-a3b8762fbc6641adb195495d70320e35` and `evidence/rx14/sanitizer-undefined-5bd7cfb4fb0d4fb99716f6cb65b2b496`.

The freestanding AMD64 kernel built and linked nine REXX C translation units. The ELF SHA-256 is `a50c6ba5813537a5cd368c32ca2edb88f5ea3fd3819ac175fa93358469a8587d`; link and output-path audits are in `evidence/rx14/native-build`.

The isolated QEMU 11.0.0 guest passed nine commands/screenshots using the rebuilt kernel, with networking disabled and disposable ESP/firmware copies. It demonstrated the built-ins, SYMBOL scope, exact XRANGE bytes, INTERPRET, syntax-trap recovery, terminal resource rejection, post-error responsiveness, and shell exit status 23. QMP shutdown was acknowledged; the guest heartbeat advanced from 1 to 68. Evidence is in `evidence/rx14/native-guest/run-74321fb610a840c383ee48af6f8fc2bb`.

The local compatibility runner found Regina unavailable. Across the **170 portable corpus cases**, guideXOS results matched the checked-in local expectations; Regina results remain **NOT_RUN (170)** and no compatibility percentage is reported. The full corpus result files are retained in each sanitizer run's `differential/results.json`.

## Remaining compatibility gaps and next phase

- DBCS-specific DATATYPE C/D options require a future character-model decision.
- The runtime's byte-oriented ASCII classification does not provide locale or multibyte alphabetic semantics.
- The host embedding API has no decimal-valued RX14 adapter for callers that provide `REXX_VALUE_NUMBER`; source REXX execution is unaffected.
- Regina remains unavailable for independent differential qualification.

Recommended RX15 scope: implement the portable conversion built-ins `C2D`, `C2X`, `D2C`, `D2X`, `X2C`, `X2D`, `B2X`, and `X2B`, using byte-exact and numeric-boundary tests.
