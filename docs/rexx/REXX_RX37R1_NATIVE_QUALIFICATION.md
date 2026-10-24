# REXX RX37R1 Native Qualification

## Outcome C — native qualification blocked

RX37R1 did not obtain QAC admission and did not launch a guest. The exclusive coordinator had one verified acceptance guest running from the separate `guideXOSServer_DiskManagerImprovements` checkout. A FIFO request waited 20 seconds at queue position 1, received no grant, and was cancelled by the requesting client. The existing guest and its QMP endpoint were left untouched. No native output marker, screenshot, QMP shutdown acknowledgement, RX37R1 QEMU child exit, or guest reservation release is claimed.

No production source or kernel configuration was changed. No RX37R1 capability fixture was added. The current kernel embedding has a host callback slot, but the RX37 guest has no diagnostic path that calls the VM capability query or exercises a fixed-column destination. Adding that path would require new test-only kernel/CLI wiring. This qualification therefore records the hosted contract and source audit without presenting them as native guest proof.

## Starting state and guards

- Branch: `REXX_SUPPORT`.
- Starting HEAD: `4906677df9818f6c241d4c233bde1a65e68f5741`, the expected RX37 commit. RX37 is its parent (`034de0e75b9c0db1125de360f0764ff58e8bde6d`).
- Tracked worktree and index were clean. There were 19,874 tracked paths.
- No root `.phase` marker, RX37R1 evidence, or RX37R1 build directory existed. No RX37R1 completion record was found in the repository.
- Starting untracked paths: 141,969. The ordinal-sorted, UTF-8/LF manifest is outside the repository at `C:\Users\guideX\AppData\Local\Codex\rx37r1-preservation\20261024T190000Z\untracked-paths.utf8.txt`; SHA-256 `87df2bb82b29e1ab25a2f363742e268baf34869a0826b9282f1a61f0596f49ec`. This matches the recorded RX37 ending manifest and count.
- All 31 root `.obj`/`.pdb` files were hashed at start and rechecked. Size and SHA-256 matched for all 31; see [the comparison](evidence/rx37r1/protected-artifact-comparison.json).

## Capability contract audit

The API is per `RexxVM`. `rexx_vm_set_output_capability_reader()` binds an optional reader to the current output writer context. `rexx_vm_get_output_capability()` makes a synchronous, allocation-free query, validates the returned struct and enum/unit/policy combinations, and copies valid metadata by value. Metadata contains no provider pointers. Known widths must be nonzero and at most 1,048,576; identity and destination generation must be nonzero. The VM does not convert pixels, bytes, or record lengths into character columns.

There is a source-level status nuance in the RX37 wording. The kernel shell sets `RexxOutputHost.read_capability` to `NULL`, so it has no width provider. However, `rexx_cli.c` unconditionally installs its bridge callback in the VM. If that VM query is called, the bridge returns `READ_UNSUPPORTED`, and the VM reports `QUERY_UNSUPPORTED` with the zero-width `DESTINATION_NOT_INSTALLED` result. `QUERY_NO_PROVIDER` is returned only when the VM itself has no reader or writer. Ordinary SAY does not query the capability API, so its execution does not depend on a provider.

Writer replacement clears the VM reader and advances the VM binding generation. The query snapshots the writer, user-data pointer, reader, and binding generation; a binding change during a callback returns `QUERY_STALE`. Teardown is rejected while execution or the capability callback is active, then clears the provider and writer references. The host remains responsible for keeping callback context alive while installed. Destination generation is provider-owned: the VM requires it to be nonzero but does not enforce monotonic increments.

## SAY and hosted-test boundary

`OP_SAY` writes the complete value and then sends its terminating LF in a separate callback. The kernel writer uses a 120-byte temporary array and processes up to 119 payload bytes at a time. `output_string()` flushes any remainder at the end of each call, so those callback/chunk boundaries can form stored rows. This is actual byte-storage behavior, not a shared logical character-column policy. The renderer calculates a pixel-derived `charsPerLine` value but does not use it to wrap stored rows; rows can be clipped. The shell has no resize generation provider.

The RX37 hosted fixture supplies an independent destination and formatter. It carries column state across its own 119-byte processing chunks and compares wrapping against a separate expected width. The production kernel writer does not do that and does not consume capability metadata. The hosted model proves the VM callback contract and test destination behavior, not production SAY wrapping or native `LINESIZE()` behavior.

## Native and regression evidence

No native RX37R1 guest scripts were executed. This includes RX37 SAY output, missing-width diagnostics, capability validation/generation/replacement, RX30 queue, RX32 VALUE, RX35 EXTERNALS, RX34 production stream-provider isolation, DATE/TIME fail-closed checks, and PIT progression. There are no RX37R1 screenshots or guest output markers.

The existing RX37 hosted and sanitizer results remain historical evidence, not new RX37R1 runs: 4,203 capability assertions, hosted regression exit 0, CLI 28/28, ASan 36/36, UBSan 36/36, and Regina `NOT_RUN`. The tracked RX37 sources used for this audit match the current HEAD blobs. The historical runner logs do not embed the final commit SHA, so exact build-to-commit provenance is not independently attested here. The existing freestanding AMD64 ELF was rehashed as `11aa057a68734c8ec39defac19b9a1310ab7f1d7c946f6f64ef8d22f92bfe6a8`; it was not rebuilt for RX37R1.

QAC CLI help/status/list/inspect checks exited 0. The QAC CLI hash is `f7089d508c1d8307e11de1b944b6e1087c038c4901333e8228ec117291ca8df6`. The active reservation was `8b84465adf184cc6a1f416744168127c`, owner-verified CLI PID 31704, QEMU child PID 24052. Its arguments identify the separate `D:\dev\guideXOSServer_DiskManagerImprovements` checkout and QMP endpoint `127.0.0.1:59140`. RX37R1 sent no QMP command and signalled no foreign process. Its own queued request was recorded as `Cancelled` by QAC; no reservation was granted. The PTY wrapper returned exit 1 on interrupt; the QAC CLI's own exit code was not independently captured. No RX37R1 runner or QEMU child existed, so their exit codes are unavailable.

## Inventory and acceptance boundary

The current 75-entry RX37 inventory still has 61 Implemented, 7 Not implemented, 5 Partial, 1 Not assessed, and 1 Not implemented by design. `LINESIZE` remains Not implemented. DATE, TIME, TRACE, and CHARIN remain Partial. RX31 remains separately recorded as Outcome B. No built-in was added or reclassified.

Outcome C means the native qualification is blocked; it does not accept the RX37 destination-width contract. The active QAC lane, production missing-provider guest output, and native fixture behavior remain open. Once a QAC slot is available, finish a bounded native run with production SAY and explicit missing-provider diagnostics, then add a clearly isolated fixture only if its provider owns both metadata and the actual SAY break behavior. Keep `LINESIZE()` Not implemented until a production character-column policy matches observed SAY wrapping and updates its generation after supported resize changes.

## Local record

The detailed snapshots and preservation records are in [the RX37R1 evidence directory](evidence/rx37r1/README.md). The documentation-only record was committed locally on `REXX_SUPPORT`; no push or branch change was made.
