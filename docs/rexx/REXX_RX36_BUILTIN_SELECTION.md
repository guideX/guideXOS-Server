# REXX RX36 Built-in Selection

## Result

RX36 ends as **Outcome C — blocked**. The output path has no authoritative logical line-width contract for REXX, so LINESIZE cannot be implemented without inventing a width. None of the other six missing functions can use the current accepted host interfaces with their documented semantics. No built-in, runtime source, or test source was changed.

The concrete dependency for a later phase is an output-capability contract that exposes the same logical columns used to wrap SAY, is owned by the output destination, and reports supported resize changes. It must distinguish an interactive Console from redirected, background, and record-oriented output before LINESIZE can be reconsidered.

## Gate 0 preflight

| Check | RX36 starting result |
|---|---|
| Repository and branch | D:\dev\guideXOSServer_REXX; REXX_SUPPORT |
| Starting HEAD | cd000901474d3b4a5f7db1b378ce73487472f0c5 |
| RX35 implementation | 1d87fd6eaf61a6bc9f1a35d18f4503ef6ba1d8c7 is the direct parent; ancestry check passed |
| Tracked paths and changes | 19,828 tracked paths; worktree and index clean |
| Starting untracked paths | 134,930; exact path set equals RX35 final set (zero additions, losses, or unexpected tracking) |
| Protected root artifacts | 31 of 31 paths, sizes, and SHA-256 values match the RX35 guard |
| Existing RX36 work | No RX36 docs, evidence, or build directory existed; no root .phase file exists |
| Qualification entry points | Hosted, sanitizer, and native PowerShell phase allowlists stop at RX35; RX36 entries do not exist |
| QAC first snapshot | CLI help, status, and list succeeded (exit 0); coordinator online; zero reservations, sessions, queued or blocked requests; QEMU census was empty |
| QAC later snapshot | A StellarNavigator acceptance reservation appeared after the first snapshot. The coordinator reported its owner verified, one managed QEMU child, and no foreign/unmanaged QEMU. The session is unrelated to RX36 and was left untouched. RX36 acquired no reservation and launched no guest. |
| QAC final snapshot | Read-only status and list both exited 0; coordinator online; zero reservations, sessions, queued or blocked requests; QEMU census empty. The earlier StellarNavigator session was no longer listed. RX36 did not signal, cancel, or release it. |

The RX36 starting path manifest is ordinal-sorted UTF-8 without BOM, LF terminated, and stored outside the repository at C:\Users\guideX\AppData\Local\Codex\rx36-preservation\20261024T173533Z\untracked-paths.utf8.txt. Its SHA-256 is 2924cb6124e3f53a7b56bee46782856d5afc634b99c19e3af918e86bf3bf17ec. Evidence copies the measurements and QAC preflight outputs under docs/rexx/evidence/rx36/.

The RX35 inventory CSV and scorecard both report 75 entries: 61 Implemented, 7 Not implemented, 5 Partial, 1 Not assessed, and 1 Not implemented by design. The request's estimated 8 Not implemented and 4 Partial counts do not match the verified RX35 artifact. The copied RX36 inventory retains the verified classifications. DATE, TIME, TRACE, CHARIN, and STREAM remain Partial.

RX34 provides a per-VM, bounded stream-status table and a byte-stream resolve/read/seek callback contract. Its status lookup does not read, seek, count remaining bytes, write, open, close, or inspect files. There is no production stream provider. CHARIN and STREAM remain Partial.

## Remaining-candidate matrix

The inventory and the dispatch table in rexx_builtin.c were cross-checked. None of the seven Not implemented names has a registered built-in descriptor. The RX34 CHARIN and STREAM descriptors are present but remain Partial.

| Candidate | IBM/classic profile and signature | Existing prerequisites | Host-service dependency | Resource concerns | Deterministic oracle | Positive native-guest feasibility | Scope risk | Disposition |
|---|---|---|---|---|---|---|---|---|
| CHAROUT | Classic/SAA stream output; [name][,string[,start]] | SAY output callbacks exist, but are not named writable streams; RX34 status is read-only | Named writable byte-stream provider, position/seek, open and write error behavior | Bounded REXX values, but partial writes and output side effects require a contract | A fake writer can check bytes and cursor results; Regina is unavailable | No production named-stream writer to exercise; a fixture alone would not establish production support | High; provider ownership and partial-write semantics | Defer until a writable stream provider exists |
| CHARS | Classic/SAA input query; [name] | RX34 resolver/read provider has no non-consuming remaining-byte query | Provider must expose remaining bytes/readability without consuming input | Scalar result; querying by reading would alter the cursor | Fixed streams could be an oracle after the provider contract adds a query | Existing guest fixture lacks a count operation; production stream provider is absent | Medium; a query contract and EOF semantics are missing | Defer until a non-consuming provider query exists |
| LINEIN | Classic/SAA line input; [name][,[line][,count]] | RX34 supports byte reads and optional byte seeks; no line framing or record index | Line/record provider or defined line framing, plus stream identity and cursor | Line result is capped by the 16 MiB REXX value bound; line-number scans can be unbounded | Fixed byte and record fixtures can test chosen framing; independent Regina comparison is unavailable | RX34 fixture streams could supply bytes, but a positive fixture would not prove production line input | High; byte/record semantics, line positioning, and transient input differ | Defer until production stream and line-framing contracts exist |
| LINEOUT | Classic/SAA line output; [name][,line[,line-number]] | No stream-writing or record-replacement API exists | Writable line/record service, cursor and write-error policy | Bounded input value; output and record replacement are externally visible | A fake record writer can validate a future contract | No production writer or record target is available for the guest | High; write side effects and persistent record semantics | Defer until writable record streams exist |
| LINES | Classic/SAA remaining-line query; [name[,option]] | RX34 STREAM observes status only; no line-count or line-availability API | Provider line count or a bounded non-consuming scan with line framing | Scanning can be unbounded; a count is small only after the query is defined | Fixed records can test a future line-count provider | No production line-count capability; fixture support would be test-only | Medium to high; stream modes and count semantics vary | Defer until a bounded line-query capability exists |
| LINESIZE | IBM TSO/E non-SAA; LINESIZE() | SAY writer passes bytes and newline only; host callback has no width query | Destination-owned logical line width and output-mode classification | Scalar result; metadata must be initialized, per destination, and resize-aware | A future injected provider is possible, but would not establish Console behavior | No production logical-width value to query or compare against | High; terminal, redirection, resize, and wrapping architecture is absent | Reject for RX36; see the host capability audit |
| USERID | IBM TSO/E non-SAA; USERID() | No accepted identity source or REXX environment identity contract | Per-environment identity provider and explicit unavailable/fallback policy | Small result, but identity lifetime and trust boundaries matter | A future identity fixture can test a defined contract | No production identity value is available for positive guest proof | High; identity/account architecture is out of scope | Defer until an approved identity capability exists |

IBM references: [TSO/E built-in function classification](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-built-in), [stream I/O functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=implementation-stream-io-functions), [CHARS](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-chars), [LINEIN](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-linein-line-input), and [LINESIZE](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-linesize).

## Qualification and retained state

No candidate passed the feasibility gate, so RX36 did not create a built-in test target or run the hosted, sanitizer, freestanding, or QAC guest suites. The QAC CLI health and reservation checks were read-only and passed. Regina remains NOT_RUN. No external compatibility percentage is claimed.

RX31 remains Outcome B. CHARIN remains Partial. RX1–RX35 source and qualification evidence are unchanged. The new RX36 inventory is a snapshot of the verified RX35 inventory; it makes no classification changes. The saved later QAC snapshot shows the unrelated managed session; the final snapshot shows it no longer listed. Neither snapshot is evidence of RX36 guest execution.

The evidence scorecard records the skipped qualification lanes and the reason. A skipped test is not reported as a pass.

## RX37 dependency

Design an output capability attached to each actual output destination. Its logical width must be the exact column boundary used by SAY wrapping, and it must report whether that width is known, how it changes with supported resizing, and whether the destination is interactive, redirected, background, or record-oriented. After the host owns and exposes that contract per VM, reconsider LINESIZE with a production Console comparison. Do not derive it from framebuffer pixels, font assumptions, the shell buffer limit, or IBM's environment-specific constants.
