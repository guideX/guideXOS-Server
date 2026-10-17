# guideXOS Server REXX RX12 — Classic String and Word Built-ins

## 1. Qualification identity and Git state

- **Qualification outcome: B (partial).** RX12 implementation and verification gates pass, but the mandatory pre-existing root-artifact preservation gate did not.
- Required branch: `REXX_SUPPORT`.
- Required starting HEAD: `7f6a145700fc84336b55fcae55ce4ea044e79b55` (verified before RX12 work).
- Ending HEAD and local commit SHA: reported with the final qualification result after committing.
- Root `.phase`: absent before and after the work.
- No branch switch, reset, clean, stash, fetch, pull, push, merge, rebase, cherry-pick, detach, or worktree operation was used.
- **Artifact-preservation incident:** the first RX12 sanitizer runs invoked MSVC `cl.exe` without `/Fo` and `/Fd`, so compiler intermediates landed in the repository root and overwrote several of the 28 pre-existing untracked `.obj`/`.pdb` artifacts. The files remain at their original paths, but their original bytes could not be recovered; no usable duplicate or shadow-copy backup was found. The sanitizer script now directs both object and PDB output into its isolated run directory. A 32-file hash manifest captured immediately before the final isolated rerun matched afterward; that proves the rerun made no further root-artifact changes, not that the originals were restored. This failure determines Outcome B. See [the preservation incident record](evidence/rx12/root-artifact-preservation-incident.md).
- The extra root-level `rexx_rx12_builtins_test.obj` created during the initial run was removed. The 28 pre-existing paths remain present and untracked; they must not be cleaned or treated as unchanged originals.
- Production ESP and disk images were not changed. The native guest used a separate copied ESP directory.

## 2. RX11 built-in baseline

The RX11 inventory contains 75 rows: 67 classic/SAA candidate functions, six IBM TSO/E non-SAA candidates, one Regina-extension set, and one future guideXOS-specific set. Of the 67 classic candidates, 11 were fully implemented and one (ARG) was partially implemented. RX12 preserved that historical inventory as `REXX_RX11_BUILTIN_INVENTORY.csv` and created the successor `REXX_RX12_BUILTIN_INVENTORY.csv`.

RX11 already dispatched LENGTH, SUBSTR, POS, WORD, WORDS, STRIP, and TRANSLATE, along with ARG, CONDITION, DIGITS, FUZZ, and FORM. RX12 strengthened the string edge semantics, added WORDPOS, SPACE, COPIES, COMPARE, and VERIFY, and completed ARG. The RX12 inventory therefore records 17 fully implemented dispatch entries, zero partial entries, and 50 unimplemented classic candidates. This inventory status is specific to the documented RX12 argument forms; it is not a claim of general ANSI compliance.

## 3. Functions added or corrected

RX12 implements or completes these 13 classic functions:

| Function | RX12 behavior covered |
| --- | --- |
| `LENGTH` | Counts the explicit byte length, including embedded zero bytes supported by `RexxValue`. |
| `SUBSTR` | One-based positive start; omitted length returns the remainder; explicit zero length is valid; a one-byte pad fills a requested overrun. |
| `POS` | Case-sensitive byte search with one-based start and result; no match and empty needle return zero. |
| `WORD` | Extracts a one-based blank-delimited word without changing its bytes; a missing word returns empty. |
| `WORDS` | Counts bounded words using the guideXOS REXX blank class. |
| `WORDPOS` | Finds a phrase as a sequence of complete, case-sensitive words, returning a one-based word position. |
| `STRIP` | B/L/T first-character option handling; defaults to both ends and the blank character; explicit strip character is one byte. |
| `SPACE` | Trims and normalizes words, supports an explicit separator count and one-byte pad. |
| `COPIES` | Accepts a nonnegative integer count and checks multiplication and output bounds before allocation. |
| `TRANSLATE` | Supports ASCII uppercase default and byte-oriented input/output tables, with distinct omitted and explicitly empty tables. |
| `COMPARE` | Returns zero for equality after padding, or the one-based first mismatch position. |
| `VERIFY` | Finds the first character included (`M`) or excluded (`N`) from a byte reference set, with optional positive start. |
| `ARG` | Counts argument positions and returns values or E/O supplied/omitted predicates, preserving holes and explicit empty values. |

The sample RX12 demonstration was independently checked against its expected output: length 23, three words, second word `REXX`, uppercase translation, prefix `guideXOS`, and `REXX` at position 10.

## 4. Argument dispatch and omission representation

Built-ins continue through the existing case-insensitive descriptor table and expression-call path. The dispatcher checks descriptor arity and required arguments before invoking a function, then clears any partial result if evaluation fails. It does not add a second interpreter or a separate BIF evaluation path.

An omitted comma position remains `REXX_VALUE_UNSET` through the existing AST/compiler/VM argument vector. It is not rewritten as an empty string. That lets a routine distinguish `call p 'x',,` from an explicitly supplied empty argument. Optional BIF parameters use the same distinction: for example, omitted STRIP option/character and omitted TRANSLATE tables are different from supplied empty values.

## 5. ARG completion

Internal CALLs and functions retain one value per argument position, including holes. `ARG()` returns the supplied position count; `ARG(n)` returns the value or empty for an absent position; `ARG(n,'E')` and `ARG(n,'O')` return `1` or `0`. Argument positions and indices are positive integral values. E/O use their first character case-insensitively; other options are rejected.

For a top-level script, the command tail is represented as zero invocation arguments when absent, and as one argument string when present, including an explicitly empty command-tail string. This preserves the distinction without changing PARSE ARG's existing command-tail handling. RX12 covers ARG at top level, internal subroutine/function frames, PROCEDURE and PROCEDURE EXPOSE, recursion, INTERPRET, no-argument calls, omitted middle positions, and explicit empty positions.

RX11's scorecard described an unsupported ARG `N` option as a gap. RX12 corrected that inventory note against IBM's classic ARG reference: E and O are the classic option tests implemented here; N is not a required classic option.

## 6. Word scanning and blank characters

WORD, WORDS, WORDPOS, and SPACE share a bounded scanner over explicit string lengths. It returns byte start/length pairs and allocates no storage per word. The guideXOS blank class is ASCII space plus horizontal tab, line feed, vertical tab, form feed, and carriage return. Unicode whitespace and locale-specific classification are deliberately not inferred.

WORDPOS normalizes separators between phrase words but compares each word byte-for-byte. It cannot match a substring inside a larger word. Its optional start is a positive one-based word index.

## 7. String, table, and padding semantics

All RX12 functions operate on the interpreter's existing byte-string representation and explicit lengths. Character-oriented behavior for multibyte encodings, DBCS, and Unicode has not been qualified.

SUBSTR's default pad is ASCII space; explicit pad must contain exactly one byte. STRIP's default character is ASCII space, and its supplied character must be exactly one byte. SPACE's separator count defaults to one and its pad defaults to ASCII space. COMPARE uses ASCII space when pad is omitted. TRANSLATE's one-argument form applies ASCII lowercase-to-uppercase mapping; table mappings are byte-oriented, and duplicate input-table characters use the first corresponding output entry. VERIFY options are N (default, first not in reference) and M (first in reference); option prefixes are case-insensitive.

Empty strings and embedded zero bytes use stored lengths rather than C-string termination where supported. POS returns zero for an empty needle, matching IBM's documented rule. COMPARE treats absent suffix bytes as pad bytes.

## 8. Bounds and ownership

Every result is bounded to 16,777,216 bytes (`REXX_BUILTIN_MAX_RESULT_LENGTH`, tied to the VM's parse-source limit). COPIES checks count conversion, multiplication, and the result limit before allocation; SPACE checks cumulative word and gap lengths; other expanding results validate their final length first. Out-of-range or unrepresentable numeric parameters are rejected without unsafe signed/unsigned conversion.

The VM-owned `rexx_value_allocate_string` helper allocates exact writable result storage plus its terminator. Inputs remain caller-owned and unchanged. On allocation or validation failure the dispatcher clears the result and releases partial ownership. The VM can execute again safely after an allocation failure.

## 9. Numeric validation and error handling

Numeric positions/counts use the existing decimal parser and checked nonnegative integer conversion. Starts and word numbers must be positive; length, separator count, and copies count may be zero where the function permits it. Negative, nonintegral, zero-for-positive, over-range, excessive-arity, omitted-required, invalid-option, and invalid-pad cases have direct coverage.

Eligible invalid function calls flow through the existing SYNTAX condition mechanism. The hosted fixtures verify recovery and preserve `SIGL`, `RC`, and `CONDITION()` information. Allocation failure and the 16 MiB resource guard remain terminal VM/resource statuses and cannot be caught as SYNTAX to bypass the resource limit. The native guest separately exercises both a trapped argument error and a controlled oversized COPIES request.

## 10. Allocation-failure results

RX12 direct allocator failpoints exercise output-producing COPIES, SPACE, TRANSLATE, and padded SUBSTR. Each failure leaves the input value intact, clears the result, and succeeds after failure injection is disabled. Twelve VM execution failpoints cover a BIF expression and rerun the same program after each out-of-memory result; all reached paths recover. The full RX1–RX11 allocator suites remain enabled.

## 11. ASan and UBSan evidence

The final current-tree run passed all 14 applicable hosted C executables under both MSVC x64 AddressSanitizer and Clang/LLVM `clang-cl` UndefinedBehaviorSanitizer. Each sanitizer also ran all 108 corpus cases locally; all had expected exit status and no sanitizer finding was reported. This retains the RX11 INTERPRET lifetime regression coverage. Regina was unavailable, so the sanitizer corpus's independent-comparison field remains `NOT_RUN` rather than a match.

Compact compile/run logs and per-process status records are in `evidence/rx12/sanitizers/`; sanitizer binaries and full run directories remain outside the repository under the user's C: Codex directory.

## 12. Hosted RX12 assertions and fixtures

The final RX1–RX12 hosted runner passed 320 RX12 C assertions with zero failures, 30/30 RX12 `.rex` fixtures, and 20/20 hosted CLI cases. The fixture set covers expected output, diagnostics, condition traps, omissions, and resource limits. The C assertions cover BIF cases/errors, byte strings, argument frames, integration, allocation failures, and VM recovery.

The 30 RX12 fixtures are included in the differential manifest. Local expected-output qualification is separate from an independent interpreter comparison.

## 13. Fresh inherited regression results

The final hosted run did not reduce or skip the inherited checks:

| Phase | Fresh result |
| --- | --- |
| RX1 | 11 runtime and 20 CLI cases passed. |
| RX2 | 45 hosted integration, 18 freestanding numeric, and 7 shell bridge assertions passed. |
| RX3 | 26 decimal assertions, 570 allocator failpoints, 13 fixtures passed. |
| RX4 | 53 arithmetic assertions, 11 fixtures passed. |
| RX5 | 35 numeric-context assertions, 13 fixtures passed. |
| RX6 | 439 allocation assertions, 25 fixtures passed. |
| RX7 | 13 routine assertions; 46/28/111 allocator checks and 33 fixtures passed. |
| RX8 | 279 assertions; parser/compiler/VM failpoints 38/21/50 and 26 fixtures passed. |
| RX9 | 467 condition assertions; parser/compiler/VM failpoints 41/37/78 and 24 fixtures passed. |
| RX10 | 257 assertions; nested VM/SYNTAX and parser/compiler failpoints 33/14/8/11; 19 fixtures passed. |
| RX11 | All 78 inherited portable corpus cases remain in the expanded 108-case run. |
| RX12 | 320 BIF assertions, 30 fixtures, and the same expanded corpus passed. |

See `evidence/rx12/hosted-final.log` for the complete run and `evidence/rx12/hosted-baseline.log` for the pre-edit inherited baseline.

## 14. Regina differential status

No approved Regina executable was found on PATH. No software was installed and global PATH was not changed. The expanded differential manifest contains 108 cases (78 inherited plus 30 RX12). Regina comparisons are `NOT_RUN` for all 108. The local hosted runner and both sanitizers independently checked the expected guideXOS output/exit behavior; those local checks are not reported as Regina matches.

## 15. Updated inventory and scorecard

`REXX_RX12_BUILTIN_INVENTORY.csv` adds argument forms, omission/default behavior, internal tests, Regina state, sanitizer coverage, native guest qualification, and remaining limits for each target function. `REXX_RX12_SCORECARD.json` retains RX11 dimensions and history, corrects the ARG option note, records 67 classic/SAA candidates with 17 fully implemented and 50 unimplemented, and keeps conformance percentage unset.

## 16. Freestanding AMD64 build

The full AMD64 kernel was force-built with the project's MinGW toolchain using `-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-builtin`, `-fno-exceptions`, and `-fno-rtti`. All nine REXX C translation units, C++ integration, and kernel linking are recorded in `evidence/rx12/native-build.log`. Isolated build outputs remain under `C:\Users\guideX\.codex\rx12-native-build` and are not committed. Artifact hashes, import/undefined-symbol checks, and link status are recorded in the RX12 scorecard/evidence README.

## 17. Isolated QEMU guest

The final build was booted from a copied disposable ESP directory using QEMU 11.0.0-12122-ga4bb4b10c9. The guest had `-net none`, a dedicated serial log and QMP port, and no access to production media. Sanitized pre- and post-launch ownership counts and the exact RX12 QEMU identity were recorded. Only the RX12-owned QEMU instance was closed through its QMP session; the other QEMU process was left untouched.

The guest ran plain-text scripts for the target BIF families, ARG omissions and routine inspection, nested INTERPRET, trapped SYNTAX recovery, oversized-output resource rejection, a subsequent recovery command, and shell exit status. String BIF, ARG, syntax, resource-limit, recovery, and `EXIT 23` shell statuses were observed as `0`, `0`, `0`, `1`, `0`, and `23`. The serial log, curated command results, screenshots, staged kernel hash, QMP shutdown receipt, and sanitized ownership censuses are in `evidence/rx12/native-guest/`.

## 18. Remaining classic compatibility gaps

- Regina comparison remains unavailable, so independently reproduced results are pending.
- String operations are byte-oriented; Unicode/DBCS character positions are not implemented or qualified.
- Only the listed RX12 built-ins have received this edge-case and native qualification; 50 other classic/SAA inventory candidates remain unimplemented.
- TSO/E non-SAA functions and Regina extensions remain outside the classic RX12 target.
- Existing VM, generated-source, and result-size limits still apply.

## 19. Recommended RX13 scope

Keep RX13 bounded: expand the standard pure BIF set with DATATYPE, LEFT/RIGHT, LASTPOS, SUBWORD, WORDLENGTH/WORDINDEX, and DELSTR/DELWORD, first verifying the exact classic function set and argument rules against IBM/ANSI references. Add independent Regina comparison when an approved executable becomes available. Do not bundle OS services, filesystems, external routines, TRACE, or application automation.

## 20. Primary references

- [IBM z/OS built-in functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-built-in)
- [IBM ARG](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-arg-argument)
- [IBM POS](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-pos-position)
- [IBM WORDPOS](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-wordpos-word-position)
- [IBM SPACE](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-space)
- [IBM STRIP](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-strip)
- [IBM VERIFY](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-verify)
- [IBM TRANSLATE](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-translate)
- [ANSI X3J18 public draft](https://www.rexxla.org/rexxlang/standards/j18pub.pdf)
