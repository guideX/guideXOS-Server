# REXX RX17: Bitwise String Built-ins

RX17 implements the classic `BITAND`, `BITOR`, and `BITXOR` built-ins in the
REXX runtime. The functions operate on string bytes and return a binary-safe
string. They do not convert their operands to integers or use REXX logical
Boolean operators.

## Signatures and behavior

| Function | Signature | Per-byte operation |
| --- | --- | --- |
| `BITAND` | `BITAND(string1[, string2[, pad]])` | Inclusive byte AND |
| `BITOR` | `BITOR(string1[, string2[, pad]])` | Inclusive byte OR |
| `BITXOR` | `BITXOR(string1[, string2[, pad]])` | Exclusive byte XOR |

`string1` is required, but may be empty. `string2` defaults to the empty
string. If `pad` is omitted, the result length is the longer operand length:
the overlapping bytes are combined and the remaining tail of the longer
operand is copied unchanged. If `pad` is supplied, it must be exactly one
byte; the shorter operand is extended on the right with that byte, then the
operation is applied through the longer length. Pad bytes from `00` through
`FF` are accepted.

An omitted middle argument is preserved by the call frame, so forms such as
`BITAND('13'X,,'00'X)` apply an explicit pad while leaving `string2` omitted.
An explicit empty `string2` has the same empty-byte value as an omitted one.
An omitted pad is distinct from a supplied pad; an empty or multibyte supplied
pad raises the runtime's built-in argument error. A missing `string1` also
raises that error, while an explicitly empty first string is valid.

Arguments use the runtime's existing string conversion rules. Numeric-looking
values retain their normal REXX string representation; the implementation
does not round raw byte strings or consult `NUMERIC DIGITS` for bitwise work.

## Implementation and ownership

The three dispatch entries share one internal bytewise implementation in
`rexx_builtin.c`. It reads length-bearing string views, validates arity and
pad length before result allocation, checks the result against the existing
16 MiB runtime string limit, allocates only the result length, and copies
unmatched bytes explicitly when padding was omitted. No arbitrary input is
passed to a C-string operation. The result owns its allocation and does not
alias either input; input buffers remain unchanged on success and failure.

This runtime limit applies to general string operands. RX16's 250-byte packed
literal bound remains a parser/literal bound and is not imposed on runtime
strings.

## Reference vectors

RX17 uses the behavior described by IBM's classic REXX references: process
the common bytes, preserve the longer tail when no pad is supplied, and use
the one-byte pad to extend the shorter input when it is supplied. Byte-exact
hosted assertions cover the documented examples and additional binary cases.
Examples include:

```rexx
C2X(BITAND('73'X,'27'X))             /* 23 */
C2X(BITOR('15'X,'2456'X,'F0'X))      /* 35F6 */
C2X(BITXOR('1111'X,'444444'X))      /* 555544 */
C2X(BITXOR('1111'X,'444444'X,'40'X)) /* 555504 */
C2X(BITXOR('13'X,,'FF'X))            /* EC */
```

The guest and direct suites also cover NUL and high-bit bytes, `00` and `FF`
padding, empty values, unequal lengths in both operand orders, shared backing
storage, unaligned buffers, argument errors, maximum-size runtime strings,
and recovery after allocation failure.

## Qualification evidence

| Gate | Result |
| --- | --- |
| Hosted direct suite | 15,843 assertions, 2,339 dispatch cases, zero failures |
| Deterministic properties | 2,304 checks, seed `0x52583137`; independent bytewise oracle, unequal lengths, padding, equal-length identities, commutativity, and De Morgan checks |
| RX17 compatibility fixtures | 10/10; nine expected argument errors and one byte-exact result fixture |
| Full compatibility fixtures | 279/279, including RX17; hosted CLI cases 20/20; three resource-limit checks passed |
| Allocation failure | All three functions return out-of-memory without publishing a result, preserve inputs, and recover successfully with no leaked buffers |
| AddressSanitizer | 20 run targets passed; RX17 target had zero failures and no sanitizer diagnostics |
| UndefinedBehaviorSanitizer | 20 run targets passed; RX17 target had zero failures and no sanitizer diagnostics |
| Portable expected-result corpus | 193 local cases passed; Regina differential comparison is `NOT_RUN` because Regina is unavailable |
| Freestanding AMD64 | Kernel built and linked with 9 REXX C objects and freestanding flags |
| QEMU guest | Isolated RX17 scripts passed for all three functions, explicit padding, binary output, handled error, and post-error responsiveness |
| Protected artifacts | All 31 protected root `.obj`/`.pdb` hashes and sizes match the captured starting baseline |

The sanitizer logs predate a test-counter-only correction and report 1,792
property cases; the property checks themselves were already present and ran.
The later hosted qualification reports the corrected total of 2,304. Both
sanitizer lanes built and ran the RX17 target successfully with 15,843
assertions and zero failures.

Detailed machine-readable results and QEMU process censuses are under
[`evidence/rx17/`](evidence/rx17/), with the summary in
[`REXX_RX17_SCORECARD.json`](REXX_RX17_SCORECARD.json).

## Compatibility limits and next phase

The runtime treats these inputs as bytes in its existing target character
representation. It does not transcode ASCII to EBCDIC, so character examples
that depend on a particular encoding must compare byte values explicitly.
Regina was not installed or found in the environment; no Regina compatibility
percentage is claimed. Runtime results remain bounded by the existing 16 MiB
string limit.

Recommended RX18 scope: implement the related classic string built-ins
`ABBREV`, `CENTER`, and its documented alias `CENTRE`, with exact argument
errors and byte-preserving tests. This is a bounded, portable string-function
group that builds on RX12/RX13 coverage without adding host dependencies.

## References

- [IBM z/OS: BITAND — bit by bit](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-bitand-bit-by-bit)
- [IBM z/VM: BITOR — bit by bit](https://www.ibm.com/docs/en/zvm/7.2?topic=functions-bitor-bit-by-bit)
- [IBM z/OS: BITXOR — bit by bit exclusive](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-bitxor-bit-by-bit-exclusive)
