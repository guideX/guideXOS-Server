# REXX RX15 Portable Conversion Built-ins

RX15 adds the eight classic conversion built-ins to the direct REXX built-in dispatch in `rexx_builtin.c`: `C2D`, `C2X`, `D2C`, `D2X`, `X2C`, `X2D`, `B2X`, and `X2B`. Their behavior follows the IBM classic REXX descriptions for conversion and binary/hex string grouping. Numeric parsing and decimal results use the existing `RexxDecimal` machinery.

## Supported behavior

| Function | Behavior |
| --- | --- |
| `C2D(string[,n])` | Converts bytes in most-significant-first order. With no `n`, returns an unsigned decimal value. With `n`, left zero-pads or left-truncates to `n` bytes, then interprets the top bit as a signed two's-complement value. `n=0` and an empty string return decimal `0`. |
| `C2X(string)` | Emits two uppercase hexadecimal digits for each input byte. Empty input returns the empty string. |
| `D2C(wholenumber[,n])` | Emits raw bytes in most-significant-first order. Omitted `n` uses the shortest unsigned representation; explicit width is in bytes, zero-pads positive numbers, sign-extends negative numbers, and keeps the low bytes when truncated. Negative input requires an explicit width. Width zero returns empty. |
| `D2X(wholenumber[,n])` | Emits uppercase hexadecimal. Omitted `n` uses the shortest unsigned representation; explicit width is in hexadecimal digits, zero-pads positive values, sign-extends negative values, and keeps the low digits when truncated. Negative input requires explicit width. Width zero returns empty. |
| `X2C(hexstring)` | Converts hexadecimal digits to bytes. Hex digits are case-insensitive; output preserves every byte, including NUL and nonprintable bytes. Odd digit counts are left-padded with one zero nibble. Empty input returns empty. |
| `X2D(hexstring[,n])` | With no `n`, returns an unsigned decimal value. With `n`, left zero-pads or left-truncates to `n` hexadecimal digits and interprets the highest retained nibble's high bit as the sign bit. `n=0` and empty input return decimal `0`. |
| `B2X(binary_string)` | Converts binary digits to uppercase hexadecimal, adding up to three zero bits on the left to complete a nibble. Empty input returns empty. |
| `X2B(hexstring)` | Emits four binary digits per hexadecimal digit. Hex input is case-insensitive; an odd number of hex digits is valid. Empty input returns empty. |

Hexadecimal inputs permit embedded ASCII blanks only at byte boundaries, with no leading or trailing blank. Binary input permits embedded ASCII blanks only at four-bit boundaries, with no leading or trailing blank; the first group can be shorter than four bits. Invalid characters and malformed group boundaries fail the call. These constraints follow the IBM REXX input rules rather than treating arbitrary whitespace as ignorable.

`D2C` and `D2X` accept whole-number forms handled by the existing decimal parser, including a leading sign, leading zeroes, a zero fractional part, and exponent notation when the resulting value is whole. Fractional values are rejected. Conversion results obey the current `NUMERIC DIGITS` setting; insufficient precision returns the runtime's numeric error. `NUMERIC FUZZ` is not used for ordering or integer conversion. The current per-result bound is `REXX_BUILTIN_MAX_RESULT_LENGTH` (16 MiB); larger requested widths fail with the runtime resource-limit status before allocation.

## Examples and byte order

On guideXOS's ASCII byte representation, the required examples produce:

```text
C2X('ABC')             -> 414243
C2X(X'00FF')           -> 00FF
C2D(X'81')             -> 129
C2D(X'81',1)           -> -127
C2D(X'81',2)           -> 129
D2X(129)               -> 81
D2X(129,4)             -> 0081
D2X(-127,2)            -> 81
D2X(-127,4)            -> FF81
D2X(12,0)              -> empty string
C2X(D2C(129,2))        -> 0081
C2X(D2C(-1,4))         -> FFFFFFFF
X2C('414243')           -> ABC
C2X(X2C('F'))          -> 0F
X2D('FF')              -> 255
X2D('FF',2)            -> -1
X2D('FF',4)            -> 255
B2X('11000011')        -> C3
B2X('10111')           -> 17
X2B('C3')              -> 11000011
X2B('7')               -> 0111
```

Widths have different units by function: `C2D` and `D2C` count bytes; `X2D` and `D2X` count hexadecimal digits. All multi-byte/multi-digit results are most-significant-first. A width of zero yields numeric zero for `C2D`/`X2D` and an empty string for `D2C`/`D2X`.

Character conversion is encoding-dependent by definition. guideXOS currently uses ASCII, so `C2X('ABC')` is `414243`. IBM's EBCDIC examples produce different character bytes (for example, `C2X('ABC')` is `C1C2C3` under the corresponding EBCDIC code page). The implementation preserves bytes; it does not transcode between ASCII and EBCDIC.

## Numeric and resource handling

The implementation converts between decimal coefficient digits and base-16/base-256 digits without narrowing the represented integer to `double`, `float`, `int64_t`, or `uint64_t`. C2D/X2D build exact decimal coefficients using bounded decimal-digit accumulation and return through the existing decimal formatter. D2C/D2X repeatedly divide the decimal digit sequence by the target radix. This supports values wider than the native machine word within the configured decimal precision.

All output lengths are checked against the existing built-in result ceiling before allocation. Allocation failure propagates as an error, clears the output value, and leaves arguments unchanged. The RX15 direct tests inject repeated allocation failures and then successfully reuse each of the eight functions. Sanitizer runs covered the same test executable under AddressSanitizer and UndefinedBehaviorSanitizer.

## Parser compatibility gap

The current lexer accepts prefixed hexadecimal/binary literals such as `X'00FF'` and `B'1010'`. It does not yet accept the classic suffix spelling such as `'00FF'X` or `'1010'B`. RX15 tests therefore use the already-supported prefix spelling where a binary byte literal is needed. This limitation is in literal parsing; the conversion built-ins themselves accept byte strings and ordinary textual hex/binary input. The core conversion behavior remains byte-testable independently of literal spelling.

## Qualification and known limits

- Dedicated RX15 direct suite: 211 assertions, 60 cases, zero failures; includes positive, zero, negative, boundary, width, empty-input, grouping, malformed-input, numeric precision, wider-than-native, allocation-failure, and recovery checks.
- Hosted inherited RX1–RX14 runtime, CLI, built-in, and compatibility fixtures passed in `tests/run_rexx_tests.ps1 -Phase RX15`.
- AddressSanitizer and UndefinedBehaviorSanitizer inherited suites passed, including `rx15_conversion`; no sanitizer diagnostics were reported.
- Freestanding AMD64 kernel compilation and link passed with `-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-exceptions`, and `-fno-rtti`. Isolated QEMU guest accepted all eight functions, an invalid conversion, an oversized width, and a successful command after both failures.
- Regina differential comparison is `NOT_RUN`: Regina was not available. The independent expected-result corpus and byte-exact direct tests remain the compatibility evidence.
- The maximum conversion width/output is currently 16 MiB. Larger `NUMERIC DIGITS` settings remain bounded by the existing decimal engine's configured maximum and runtime work limits.

## References

- IBM [Conversion Functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-conversion) lists the eight classic conversion functions.
- IBM function descriptions: [C2D](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-c2d-character-decimal), [C2X](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-c2x-character-hexadecimal), [D2C](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-d2c-decimal-character), [D2X](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-d2x-decimal-hexadecimal), [X2C](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-x2c-hexadecimal-character), [X2D](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-x2d-hexadecimal-decimal), [B2X](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-b2x-binary-hexadecimal), and [X2B](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-x2b-hexadecimal-binary).
- IBM [hexadecimal/binary string syntax and grouping](https://www.ibm.com/docs/en/zos/2.5.0?topic=irx-irx0015i) and [character-set dependence](https://www.ibm.com/docs/en/zos/2.5.0?topic=syntax-characters).
