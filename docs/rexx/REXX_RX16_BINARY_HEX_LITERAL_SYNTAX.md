# REXX RX16 Binary and Hexadecimal Literal Syntax

RX16 adds the classic quoted suffix spellings for hexadecimal and binary string
literals. The lexer recognizes them as literal tokens, the existing compiler
stores the exact source slice, and the VM decodes the packed bytes into its
length-bearing string value. No built-in function was added in this phase.

The syntax and examples below follow IBM's [REXX token reference](https://www.ibm.com/docs/en/cics-ts/5.5.0?topic=syntax-tokens)
and its [invalid hexadecimal or binary string diagnostic](https://www.ibm.com/docs/en/zos/2.5.0?topic=irx-irx0015i).
IBM describes these values as encodings. Their byte values are portable, while
the character each byte represents depends on the host encoding.

## Accepted forms

```rexx
'414243'X
'414243'x
"414243"X
'01000001'B
'01000001'b
"01000001"B
```

The suffix is exactly one `X`/`x` or `B`/`b`, adjacent to the closing quote.
It must end before a symbol character. Thus `'41'X` is a hex literal, while
`'41' X` is a normal string followed by the symbol `X`, and `'41'XY` is a
normal string followed by the symbol `XY`. Existing implicit concatenation
rules apply to those separate terms. Explicit `||` concatenation is unchanged.

Both quote styles use the existing string scanner, including doubled-quote
handling. A doubled quote remains an escaped quote in an ordinary string. It
cannot be a hex or binary digit, so a doubled quote inside an encoded literal
produces a lexical error. A normal quoted string immediately followed by `(`
retains the existing quoted function-name behavior. The `X` and `B` suffix
tokens are literals, so a following parenthesized term does not turn them into
function calls.

## Hexadecimal payload

- The payload accepts `0`–`9`, `A`–`F`, and `a`–`f`.
- Empty payloads such as `''X` produce the empty string.
- One or more ASCII spaces may separate byte groups. A space cannot start or
  end the payload, and each group boundary must fall between packed bytes.
- An odd number of digits is padded with one zero nibble at the left. For
  example, `'F'X` contains byte `0F`, not the character `F` and not a number.
- Spaces are removed, each pair of nibbles is packed into a byte, and every
  byte is retained, including `00` and values from `80` through `FF`.

Examples:

```rexx
C2X('414243'X)          /* 414243 */
C2X("41 42 43"X)       /* 414243 */
C2X('F'X)                /* 0F */
C2X('00FF80'X)           /* 00FF80 */
```

## Binary payload

- The payload accepts only `0` and `1`.
- Empty payloads such as `''B` produce the empty string.
- Spaces may separate nibbles or bytes, but cannot be leading or trailing.
  Group boundaries are checked after the leading padding needed to align the
  full bit string. This accepts the classic short first nibble and examples
  such as `'10000 10101010'B`.
- The final bit string is padded on the left with zeroes to a whole number of
  bytes, then packed most-significant bit first.
- The token is a string literal. It is not parsed as a numeric expression.

Examples:

```rexx
C2X('01000001'B)         /* 41 */
C2X('1'B)                /* 01 */
C2X('101 1101'B)         /* 5D */
C2X('11110000'b)         /* F0 */
C2X('10000 10101010'B)   /* 10AA */
```

IBM documents a 250-byte packed-length maximum for hexadecimal literals. RX16
uses a deterministic 250-byte packed-size limit for both suffix forms to bound
literal decoding in the freestanding runtime. A larger suffix literal reports
a lexer resource-limit error at its opening quote. This binary limit is a
guideXOS implementation bound; the cited IBM description does not state a
separate binary maximum.

## Front-end and value handling

The lexer scans the full quoted token first, then recognizes one eligible
suffix without consuming any character from the next token. It validates
digits, grouping, empty input, and packed size without allocating memory.
Malformed payloads become `TOKEN_ERROR` values with their original line and
byte column. Unterminated strings leave the line ending available to normal
lexer recovery.

The parser already treats hex and binary tokens as literal terms in
assignments, calls, comparisons, and expressions. The compiler continues to
store the original token span, including the suffix, so adjacency decisions
remain based on the source. The VM accepts both the existing guideXOS prefix
forms (`X'...'`, `B'...'`) and the new suffix forms and returns bytes through
`RexxValue.as.string.bytes` plus an explicit length. No C-string operation is
used to decide the value length.

The conversion functions added in RX15 remain unchanged. They provide useful
byte-visible checks such as `C2X`, `X2B`, and this round trip:

```rexx
X2B(C2X('0001FF80'X))
/* 00000000000000011111111110000000 */
```

## Qualification coverage

The RX16 direct test covers both quote styles, suffix case, empty and odd
payloads, grouping, NUL and high-bit bytes, leading-zero preservation,
explicit and implicit concatenation, comments, consecutive literals, function
call ambiguity, assignments, comparisons, conditional expressions, interpreted
source, source locations, malformed digits and groups, unterminated strings,
resource rejection, parser reuse after errors, and parser/compiler/VM allocation
failure recovery. The portable fixture corpus is under
`tests/rexx_compat/cases/literals/`; platform-specific ASCII/EBCDIC character
comparisons are excluded from it.

The hosted portable corpus is also compared with Regina when a verified Regina
executable is already available. A missing interpreter is recorded as
`NOT_RUN`; it does not count as a match. The RX16 scorecard records that status
and the independent fixture results.

## Deferred functionality

RX16 does not add quoted-name parsing to `PARSE` templates, prefix `X`/`B`
syntax variants beyond the forms already accepted by guideXOS, general source
encoding conversion, or new built-in functions. The packed result remains
machine-encoding data; the interpreter does not translate it between ASCII and
EBCDIC. A later phase can add bytewise logical BIFs without changing this
literal representation.
