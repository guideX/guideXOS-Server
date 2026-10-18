# RX13 Classic REXX String Editing and Word Utilities

- **Branch:** `REXX_SUPPORT`
- **Starting HEAD:** `d3dec615a8ed2df59342769970f19dcdc1d9a803`
- **Starting state:** RX12R1 accepted; no root `.phase` file; no pre-existing RX13 implementation or evidence.
- **Outcome:** RX13 accepted after hosted, sanitizer, freestanding AMD64, isolated QEMU, and protected-artifact gates passed. The local commit SHA is recorded in the completion report.

RX13 adds the ten standard built-ins `LEFT`, `RIGHT`, `INSERT`, `OVERLAY`, `DELSTR`, `DELWORD`, `SUBWORD`, `WORDINDEX`, `WORDLENGTH`, and `LASTPOS`. It reuses the RX12 word scanner and checked decimal helpers in [rexx_builtin.c](../../rexx_builtin.c), and leaves the parser, VM, string representation, and dispatch framework intact.

## Compatibility behavior

The implementation follows the documented IBM defaults and byte-range rules. IBM's reference describes `INSERT` as inserting after a position, `OVERLAY` as replacing at a position, and `LASTPOS` as a backward search whose optional start sets the point where the scan begins. The [IBM string-manipulating function index](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-string-manipulating), [INSERT](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-insert), [OVERLAY](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-overlay), [DELWORD](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-delword-delete-word), [SUBWORD](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-subword), and [LASTPOS](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-lastpos-last-position) pages were used for the behavioral cases. The [ANSI X3.274-1996 final draft](https://www.rexxla.org/rexxlang/standards/j18pub.pdf) supplies the word and blank configuration model and a bounded `LASTPOS` reference algorithm.

| Function | Signature and behavior | RX13 direct cases |
| --- | --- | ---: |
| `LEFT` | `LEFT(string,n[,pad])`; nonnegative length, default one blank; truncates on the right or pads on the right. | 10 |
| `RIGHT` | `RIGHT(string,n[,pad])`; nonnegative length, default one blank; truncates on the left or pads on the left. | 10 |
| `INSERT` | `INSERT(new,target[,[position][,[length][,pad]]])`; default position `0`, so insertion precedes the first target byte. Default length is `LENGTH(new)`. | 15 |
| `OVERLAY` | `OVERLAY(new,target[,[position][,[length][,pad]]])`; default position `1`; replaces target bytes and pads gaps or an extended result. | 14 |
| `DELSTR` | `DELSTR(string,start[,length])`; positive one-based start; omitted length deletes the remainder, overlong deletion is clipped. | 13 |
| `DELWORD` | `DELWORD(string,start[,length])`; removes selected words and blanks after the final removed word, preserving blanks before the first. | 13 |
| `SUBWORD` | `SUBWORD(string,start[,length])`; selected words without leading/trailing blanks and with the original bytes between selected words. | 12 |
| `WORDINDEX` | `WORDINDEX(string,n)`; one-based byte position of word `n`, or `0` when absent. | 8 |
| `WORDLENGTH` | `WORDLENGTH(string,n)`; byte length of word `n`, or `0` when absent. | 8 |
| `LASTPOS` | `LASTPOS(needle,haystack[,start])`; case-sensitive backward scan, one-based result, and `0` for no match or an empty needle. | 13 |

`INSERT` and `OVERLAY` are separate implementations. For example, `INSERT('XX','abcd',2)` returns `abXXcd`; `OVERLAY('XX','abcd',2)` returns `aXXd`. `INSERT('123','abc')` returns `123abc`, and `OVERLAY('qq','abcd')` returns `qqcd`. When either function extends past the target, it uses the requested pad byte for the gap and for any requested length beyond `new`.

`DELWORD('Now is the  time',2,2)` returns `Now time`; `DELWORD('Now is   the time',3,1)` returns `Now is   time`. The implementation finds byte boundaries with the shared bounded RX12 word scanner rather than splitting and rejoining the string. `SUBWORD('Now is the  time',3)` returns `the  time`: leading and trailing separators are removed, while the two original spaces between `the` and `time` remain. Exact C assertions and `.out` fixture files cover spaces, tabs, and other configured ASCII blank bytes. The scanner's blank class is ASCII space plus HT through CR, consistent with the ANSI configurable `other_blank_characters` set; it is byte-oriented and does not decode multibyte text.

For `LASTPOS`, `start` is a positive whole number and caps where the backward scan begins. The implementation clamps a start greater than the haystack length and computes the highest candidate start as `start - LENGTH(needle) + 1`, matching the ANSI reference pseudocode without unsigned underflow. Cases cover repeated and overlapping needles, case sensitivity, empty needle, absent matches, omitted start, and explicit bounds.

## Validation, ownership, and resource bounds

Argument values are converted with the existing decimal parser and exact integer conversion; C floating-point rounding and locale-dependent parsing are not used. Decimal forms with an integral value such as `2.0` are accepted. Fractions, negative values, invalid strings, and values above `uint32` are rejected with controlled runtime conditions. Zero is accepted for nonnegative lengths and INSERT positions; functions requiring positive indexes reject zero. Explicit padding must be exactly one byte; omitted padding is one blank. Optional omission holes are preserved.

Result size is checked before allocation, and each built-in result is capped at 16 MiB (16,777,216 bytes). The RX13 C suite proves the exact cap succeeds, the next byte is rejected, a maximum valid `uint32` DELSTR length clips safely, and an over-range numeric argument is rejected. INSERT and OVERLAY calculate checked output lengths. Returned strings own their storage, inputs remain unchanged, and output allocation failure is injected for every one of the ten new BIF paths. Each path fails cleanly, succeeds after the failpoint is removed, and releases all owned buffers.

Built-in name resolution remains case-insensitive and uses the established routine rules. Hosted coverage includes a colliding internal `LEFT` function, quoted invocation of the BIF, top-level expressions, IF/DO, CALL and PROCEDURE EXPOSE, INTERPRET and nested INTERPRET, and catchable SYNTAX recovery. No parser, VM, or function-dispatch redesign was needed.

## Hosted coverage

The final expanded run passed **418 RX13 assertions**, including **116 semantic cases**, **32 controlled-error cases**, and the deterministic allocation-failure checks. All **33 RX13 `.rex` fixtures** passed exact `.out` comparisons. The exact 16 MiB boundary was included in this expanded run.

The inherited runner passed in the same run:

| Suite | Observed result |
| --- | ---: |
| CLI | 20/20 |
| RX2 VFS/output integration C assertions | 45 |
| RX2 freestanding numeric assertions | 18 |
| RX2 shell bridge assertions | 7 |
| RX3 decimal assertions / allocation failures | 26 / 570 |
| RX3 / RX4 / RX5 / RX6 / RX7 / RX8 / RX9 / RX10 / RX12 fixtures | 13/13 / 11/11 / 13/13 / 25/25 / 33/33 / 26/26 / 24/24 / 19/19 / 30/30 |
| RX4 arithmetic / RX5 numeric context assertions | 53 / 35 |
| RX6 PARSE allocation-failure assertions | 439 |
| RX7 routine assertions; parser/compiler/VM failpoints | 13; 46/28/111 |
| RX8 semantic/allocation/recovery assertions; parser/compiler/VM failpoints | 279; 38/21/50 |
| RX9 condition assertions; parser/compiler/VM failpoints | 467; 41/37/78 |
| RX10 INTERPRET assertions; nested VM, syntax VM, parser, compiler failpoints | 257; 33/14/8/11 |
| RX12 built-in assertions | 320 |
| RX13 string editing assertions and fixtures | 418; 33/33 |

The full output is preserved in [hosted-final-boundary-extended.log](evidence/rx13/hosted-final-boundary-extended.log). The pre-change inherited baseline is [hosted-inherited-baseline-c4b9b8b1615a4e5eb8ab5f4bd47c7d93.log](evidence/rx13/hosted-inherited-baseline-c4b9b8b1615a4e5eb8ab5f4bd47c7d93.log).

## Sanitizers and compatibility corpus

The expanded 418-assertion RX13 executable passed under both AddressSanitizer and UndefinedBehaviorSanitizer. Each run built and passed all 15 inherited and RX13 hosted executables, then executed the 141-case portable corpus. All 141 guideXOS runs had their expected exit status. Regina was not available, so its status is explicitly `NOT_RUN` for all 141 cases and no conformance percentage is claimed. Both sanitizer corpora include the 33 RX13 fixtures. Durable compiler/runtime logs, `results.json`, per-case classifications, raw guideXOS output, and artifact preservation records are in the corresponding `evidence/rx13/sanitizer-address-*` and `evidence/rx13/sanitizer-undefined-*` directories.

## Native AMD64 build and guest

The full freestanding AMD64 kernel built and linked successfully. All nine REXX C translation units were compiled with the kernel build's freestanding flags (`-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-builtin`, `-fno-exceptions`, `-fno-rtti`). Every generated build output was audited under `build/rexx/rx13/native-amd64-ac2a66e250084abd97de33022bf6b2a1`.

- Kernel ELF SHA-256: `56c5137b4450cadcb8ca906dadecf4e6d5888773c3fc7e54c47bb0aa6a792b95`
- Kernel PE SHA-256: `d5d97e9f44a859df79c5fa513e8c915c9d30e3e557e6c10d6aa375fa2c60775c`
- Build and link records: [output audit](evidence/rx13/native-build/output-audit-ac2a66e250084abd97de33022bf6b2a1.json), [link evidence](evidence/rx13/native-build/native-link-evidence-ac2a66e250084abd97de33022bf6b2a1.json)

QEMU used copied disposable media, disabled networking, a unique QMP port, and the kernel with the matching SHA-256 above. The native Console executed seven commands and captured one screenshot per command. The first script demonstrates LEFT/RIGHT padding, INSERT defaults and explicit positioning, OVERLAY replacement and extension, and DELSTR. The second demonstrates DELWORD, SUBWORD, WORDINDEX, WORDLENGTH, LASTPOS, and INTERPRET. Further commands demonstrated catchable syntax recovery, rejection of an oversized result, a responsive command after the error, and `echo $?` returning `23`. Serial heartbeat records increased from 1 to 62; QMP acknowledged shutdown for the verified owned QEMU PID. The oversized-output path does not emit the VFS `Opened` serial line, so its runtime rejection is evidenced by the Console screenshot instead.

The run transcript and screenshots are in [native guest evidence](evidence/rx13/native-guest/run-78b4c4f23377400493d5e48b095f24ed/). The final status screenshot is [rx13-07-exit-status-check.png](evidence/rx13/native-guest/run-78b4c4f23377400493d5e48b095f24ed/rx13-07-exit-status-check.png).

## Protected build artifacts and remaining work

The RX12R1 protection manifest held 31 root `.obj`/`.pdb` artifacts, including 28 untracked files. Every RX13 build and test stage ran its fail-closed guard. The final comparison hashes each current root artifact against both the RX13 start manifest and the RX12R1 post-incident baseline: 31/31 match both; no path is missing or unexpected, and `.phase` remains absent. The RX12R1 baseline is explicitly the current post-incident state, not recovered pre-RX12 bytes. The historical incident records remain unchanged. See [final artifact comparison](evidence/rx13/protected-artifacts-final.json) and [guard report](evidence/rx13/rx13-final-precommit.txt).

The successor [RX13 built-in inventory](REXX_RX13_BUILTIN_INVENTORY.csv) retains RX12 history and marks the ten new BIFs complete. Of 67 listed classic/SAA candidates, 27 are now implemented and 40 remain. Six IBM TSO/E non-SAA candidates remain separately categorized. The [RX13 scorecard](REXX_RX13_SCORECARD.json) records the full suite, sanitizer, native, QEMU, corpus, and artifact evidence. Regina remains an independent-comparison gap, and strings use bounded byte indexing rather than Unicode character decoding.

Recommended RX14 scope: audit and implement the portable conversion BIF family (`C2D`, `C2X`, `D2C`, `D2X`, `X2C`, `X2D`, `B2X`, `X2B`) with byte-exact outputs and numeric-boundary coverage. RX14 is not started by this phase.
