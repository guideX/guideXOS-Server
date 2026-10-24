# REXX RX35 Built-in Selection

## Preflight and starting inventory

- Repository: `D:\dev\guideXOSServer_REXX`
- Required branch: `REXX_SUPPORT` (verified)
- Starting HEAD: `4ff79526bd719c19dbb650559fdbfcb904c2afe8` (verified RX34 commit)
- Tracked worktree: clean; index: clean.
- Existing RX35 work: none found in tracked or Git-reported untracked paths; no RX35 phase guard or root `.phase` file exists.
- Starting untracked paths: 131,064, captured in ordinal-sorted UTF-8/LF manifest `C:\Users\guideX\AppData\Local\Codex\rx35-preservation\20261024T152000Z\untracked-paths.utf8.txt`, SHA-256 `2C28ECD47D7B5E62E2B89BB4CF8B150646E7CAACE02F9632C32F85B2D91060BD`.
- Protected root artifacts: 31 `.obj`/`.pdb` files. Their starting paths, byte sizes, and SHA-256 hashes are captured in `C:\Users\guideX\AppData\Local\Codex\rx35-preservation\20261024T152000Z\preflight.json`.
- RX34 commit is the current HEAD. RX34's historical qualification is not treated as RX35 evidence.

The RX34 scorecard records 75 inventory entries: 60 Implemented, 8 Not implemented, 5 Partial, 1 Not assessed, and 1 Not implemented by design. The counts sum to 75. The inventory's normalized statuses and `rexx_builtin.c` dispatch were cross-checked. CHARIN and STREAM remain Partial; DATE, TIME, and TRACE remain Partial. No classifications are changed by this selection checkpoint.

## Candidate matrix

The inventory marks all eight candidates below Not implemented. “Specification” is scoped to the cited IBM profile; a separate platform with a different contract is not silently treated as equivalent.

| Name | Specification and dialect | Current classification | Related functionality | Required runtime changes | Host dependencies | Complexity / resources | Testability and portability | Priority / decision |
|---|---|---|---|---|---|---|---|---|
| `CHAROUT` | IBM z/OS 2.5 byte-oriented stream output; `[name][,string[,start]]` | Not implemented | RX33 CHARIN byte provider; RX34 per-VM STREAM status | Add output callback contract, argument/position handling, partial-write and error-state behavior, implicit-open policy | Writable stream provider, potentially filesystem/device policy | High; output bounded by the 16 MiB value but provider-side effects and partial writes need a contract | Deterministic with a fixture, but production requires host output integration; stream-specific and not a generic portable filesystem API | P3 — Host-dependent; defer |
| `CHARS` | IBM z/OS 2.5 stream input query; `[name]` | Not implemented | RX33 CHARIN read callback; RX34 STREAM observation | Add a non-consuming remaining-byte/readability query with provider state and EOF semantics | Provider must report remaining input without consuming it | Medium; bounded scalar result, but query cannot be derived correctly from the current read/seek-only provider | Fixture-testable; host stream status and dialect behavior vary | P3 — Host-dependent; defer |
| `LINEIN` | IBM z/OS 2.5 line input; `[name][,[line][,count]]` | Not implemented | RX33 byte-oriented CHARIN; RX30 data stack is a separate source | Add record-aware reads, line positioning/count semantics, and line-state/error behavior | Line/record provider or defined line framing over a stream | High; line length capped at 16 MiB, with count and aggregation limits needed | Can be tested with fixtures, but byte reads alone do not establish record boundaries; line endings and record streams are host-specific | P3 — Host-dependent; defer |
| `LINEOUT` | IBM z/OS 2.5 line output; `[name][,line[,line-number]]` | Not implemented | RX34 STREAM status; no writable provider exists | Add line writes, record positioning/replacement, implicit-open and write-error semantics | Writable line/record provider | High; bounded input line, but output side effects and target record semantics need host contract | Fixture-testable; production requires host output policy | P3 — Host-dependent; defer |
| `LINES` | IBM z/OS 2.5 remaining-line query; `[name[,option]]` | Not implemented | RX34 STREAM read-only observation | Add non-consuming line-count/readability query and option semantics | Provider must expose line/record counts or a safe scan facility | Medium to high; scans may be unbounded unless the provider has a bounded query | Fixture-testable only after record semantics are defined; behavior differs across stream implementations | P3 — Host-dependent; defer |
| `EXTERNALS` | IBM TSO/E 2.5 non-SAA built-in, `EXTERNALS()`; TSO/E has no terminal input buffer and returns zero. IBM z/VM instead counts logical typed-ahead lines. | Not implemented | RX30 QUEUED/data stack is a distinct REXX external data stack, not the terminal input event queue | Register the zero-argument built-in and return numeric zero; reject supplied arguments through existing dispatch validation | None for the selected TSO/E profile. z/VM profile requires a terminal input buffer and is excluded. | Low; constant-size decimal result, no additional allocation beyond existing numeric-value handling | Direct, VM, INTERPRET, routine, and guest tests can assert exact zero. Non-SAA; platform behavior is explicitly dialect-dependent. | P1 — Independent for the selected TSO/E profile; select |
| `LINESIZE` | IBM TSO/E/VM non-SAA built-in; no-argument form | Not implemented | SAY and console/output routing | Add an environment query and define foreground, batch, and non-TSO output-record behavior | Terminal width, batch default, or output DD logical record length | Medium; scalar result, but depends on execution environment configuration | Testable only with a modeled host environment; semantics differ by TSO/E mode and output DD | P3 — Host-dependent; defer |
| `USERID` | IBM TSO/E/VM non-SAA built-in; `USERID()` | Not implemented | No accepted identity provider | Add identity resolution and TSO/E/non-TSO fallback behavior | TSO/E identity or configured user/step/job identity and possibly replaceable routine | Medium; returned value is small and capped by the host contract | Not portable; environment-dependent and explicitly outside RX35 identity/account scope | P3 — Host-dependent; defer |

### Not-assessed entry

`REGINA_EXTENSION_SET` is version/configuration-dependent and denotes a collection of Regina-specific functions, not one function with a stable cross-version contract. It is not a bounded candidate for this phase. Its Not assessed classification is retained.

## Selected function

Select **`EXTERNALS()`**, implementing the IBM TSO/E 2.5 result contract only. IBM classifies it as a non-SAA TSO/E function; in that implementation it returns zero because there is no terminal input buffer. The VM contract is different: it returns the number of logical typed-ahead lines. RX35 will not claim or implement that VM extension.

This is the only candidate that can be completed without adding a kernel/host service, stream subsystem, account lookup, or terminal input buffer. It is genuinely absent from the current dispatcher. It has deterministic output, exact numeric semantics, no mutable state, and a direct oracle independent of production code. `QUEUED()` and RX30 stack state will not be reused because they represent a different queue.

## References

- IBM z/OS 2.5, [Built-in functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-built-in): identifies EXTERNALS as a non-SAA TSO/E function and documents built-in argument/numeric conventions.
- IBM z/OS 2.5, [EXTERNALS](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-externals): TSO/E behavior is zero because that implementation has no terminal input buffer.
- IBM z/VM 7.2, [EXTERNALS](https://www.ibm.com/docs/en/zvm/7.2.0?topic=f-externals): VM behavior counts logical typed-ahead terminal lines; this profile is excluded.
- IBM z/OS 2.5, [The Stream I/O Functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=implementation-stream-io-functions): CHARIN/LINEIN, CHAROUT/LINEOUT, and CHARS/LINES belong to stream input/output and depend on stream facilities.
- RX34, `docs/rexx/REXX_RX34_SCORECARD.json`, `docs/rexx/REXX_RX34_STREAM_CONDITION_STATE.md`, and `docs/rexx/REXX_RX34_CHARIN_COMPATIBILITY.md`.

## Expected acceptance criteria

- Preserve the single selected TSO/E interpretation and state the VM exclusion in user-facing RX35 documentation.
- Reuse `rexx_builtin_set_size`; add no new generic framework, host API, allocation, or VM-global state.
- Verify the no-argument result is exact decimal `0`; reject extra or explicitly supplied arguments using established invalid-built-in-call behavior.
- Add direct tests for dispatch, argument count, repeated invocation, numeric-setting independence, internal routine, INTERPRET, trap/recovery, and independent VMs.
- Add a deterministic property model that returns zero for bounded invocation counts and is independent of the production dispatch helper.
- Run the user's full RX1–RX34 hosted, sanitizer, freestanding, artifact-preservation, and exclusive QAC gates before claiming acceptance.

## Explicit exclusions

- No VM terminal input-buffer or interactive Console service; no claim for z/VM EXTERNALS semantics.
- No CHAROUT, CHARS, LINEIN, LINEOUT, or LINES implementation, and no filesystem or device stream service.
- No DATE/TIME, TRACE, ADDRESS, queue, external identity, or account changes.
- No Regina extension-set reclassification, and no changes to RX31 or RX34 acceptance status.
