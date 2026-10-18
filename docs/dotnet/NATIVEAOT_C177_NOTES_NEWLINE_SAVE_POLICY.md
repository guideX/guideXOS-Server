# NativeAOT C177 — Managed Notes Newline and Save Policy

## Outcome

**Outcome A — Accepted.** Enter now chooses a bounded local LF or CRLF delimiter
from the pre-edit document. Serialization retains every untouched byte, and
the existing Notes save path was qualified with real guest VFS writes,
read-back, and reopen checks. C177 does not add a new storage subsystem or
claim crash-safe or reboot-durable writes.

The implementation leaves the NativeAOT ABI, kernel, VFS architecture, and
association format unchanged. C174–C176 resizing, navigation, and line-ending
behavior remain covered by the complete host regression and focused guest
acceptance.

## Existing editor and file capabilities

Managed Notes already supports opening `.txt` files through its Open dialog
and Explorer association launch, editing, explicit Ctrl+S/Save, Save As,
creation, overwrite confirmation, read-back verification, and reopening.
`GuideXosFile` forwards bounded UTF-8 operations through the existing managed
host bridge. The native `fileWriteAll` callback validates capability, path,
and size before calling the VFS; the managed side retains no native pointer.

The Notes document state tracks the saved editor revision and current path.
Dirty state is derived from whether the current revision matches the saved
revision. A successful save marks the current revision saved only after exact
read-back verification. A failed save reports an error and leaves the
in-memory text, path, dirty state, and undo/redo history in place. Save As
checks an existing destination and asks before overwrite.

The Notes-specific write path is bounded to 256 serialized bytes. It snapshots
the existing file, writes through the VFS, checks file type and length, and
compares exact read-back bytes. If a write or verification fails, it attempts
to restore the prior bytes (or remove a partially created file) and reports
failure. This is an in-place VFS update: replacement is non-atomic and no flush
operation is available. Recovery is best-effort if the restore itself fails.
The guest RAM disk is volatile across guest reboot, so C177 makes no reboot
durability claim.

`GuideXosTextArea` supports up to 1,024 ASCII characters, 128 logical lines,
and 56 renderable columns. The current Managed Notes instance keeps its
existing 256-character, 32-line buffer; its responsive viewport grows from
48 to 56 renderable columns and from four to eight visible lines. C177 does
not raise the app's existing file or editor cap. For ASCII content, serialized
byte count equals character count; CRLF occupies two bytes and consumes two
positions in the bounded serialized buffer. Paths are limited to 256 bytes.
Non-ASCII and CR-only documents remain unsupported.

## Enter and serialization policy

The editor selects a delimiter using the document state before the edit,
starting at the selection start (or caret when there is no selection):

1. Use the nearest preceding LF or CRLF sequence.
2. If none precedes the insertion point, use the first following sequence.
3. If the document contains no sequence, use LF. Empty documents therefore
   also use LF.

This preserves local style in mixed LF/CRLF documents. The scanner recognizes
CRLF by its LF byte; normalized caret and selection positions cannot split the
pair. The complete delimiter is sent through the existing bounded edit path
as one history revision. A CRLF insertion uses two bytes, leaves the caret
after the pair, and cannot partially insert at a capacity boundary. One Undo
reverses one Enter; Redo restores the exact bytes. Selection replacement,
failed insertion, and dirty-state behavior use the existing history and
revision model.

Opening and serializing an unmodified supported document preserves its exact
bytes, including mixed delimiters, blank lines, whitespace, and whether the
file ends in a newline. Enter changes only the inserted sequence and the
intentional selection replacement; it does not normalize other delimiters or
add or remove a trailing newline elsewhere. CR-only files and unsupported
control or non-ASCII bytes are rejected without replacing the current text.

## Save behavior and evidence

The focused guest suite exercised the real Notes file callback and VFS using
`/system/apps/C152/c177.txt`. It verified an exact no-edit save of a mixed
fixture, inserted Enter before that fixture, exact destination bytes, reopen
through the existing read path, and an explicit invalid-path failure that
preserved the in-memory document, path, dirty state, and history. A separate
over-limit write returned `FileTooLarge` and did not create the destination.
The proof-disabled ordinary boots then exercised user-visible Ctrl+S and
reopen on LF and CRLF fixtures.

| Guest case | Input bytes | Saved and reopened bytes | Result |
| --- | ---: | ---: | --- |
| Focused Boot 1, LF | 204 | 205 | PASS; exact C152 read-back and C151 reopen |
| Focused Boot 2, CRLF | 170 | 172 | PASS; CRLF insertion stayed a two-byte sequence |
| Focused Boot 3, concurrent LF | 204 | 205 | PASS; Settings, Explorer, and Notes remained resident |
| Production Boot 1, LF | 204 | 205 | PASS; proof-disabled Ctrl+S and reopen |
| Production Boot 2, CRLF | 170 | 172 | PASS; proof-disabled Ctrl+S and reopen |
| Production Boot 3, concurrent LF | 204 | 205 | PASS; proof-disabled save/reopen during concurrent app use |

The focused test also covers no-edit LF, CRLF, mixed-format, trailing-newline,
no-trailing-newline, blank-line, and consecutive-delimiter round trips. It
checks mixed-format selection before and after local delimiters, beginning and
end insertion, active selection replacement, exact LF/CRLF capacity fits,
atomic rejection, Undo/Redo, dirty revisions, and resize followed by Enter.
The existing C152 save tests cover bounded write failure injection and
rollback behavior.

## Regression and guest acceptance

The complete managed UI host regression passed **30 suites / 823 cases, 0
failures**. C177 contributed 23 focused cases and passed its stress checks:

- 1,000 deterministic editing and navigation operations.
- 250 mixed-delimiter insertions and 250 removals.
- 250 capacity rejections.
- 500 alternating resizes and 250 rejected-geometry checks.
- 100 document lifecycles and 100 serialization round trips.
- Zero resize hot-path allocation bytes and zero cumulative geometry drift.

C172, C173, C175, and C176 host regression entries all passed. The focused
proof-enabled QEMU run completed **3/3 isolated boots**. Each boot reported 23
C177 focused cases and the guest VFS save/read-back proof as PASS. The separate
proof-disabled clean production candidate completed **3/3 ordinary boots**;
Settings, Explorer, Notes association activation, Calculator, Task Manager,
and the C170/C172/C173 production acceptance manifests passed. All three
ordinary boots used the same proof-disabled kernel and ramdisk hashes. The
manifest reports `proof1Excluded=true`, candidate strings for the forbidden
Proof1 markers absent, and C173 proof-only diagnostics absent from all
ordinary serial logs.

## Retained artifacts and hashes

Evidence is under the active checkout's `out/dotnet/c177-managed-notes-newline-save-policy/`.
Historical project artifacts also exist in the separately relocated
`C:\dev\guideXOSServer.NETSupport\out\dotnet` tree; the C177 evidence listed
here remains in the active checkout.

| Artifact | SHA-256 |
| --- | --- |
| `host/managed-ui-host-manifest.json` | `D6B457DC91D0CD6FA4C0E0B3D9D04BD5CB7A1407256A6A4B8A23C3E7814CD8C8` |
| `c173-focused-proof-manifest.json` | `65F30A1940E2668FB6FB779F5ED5AA02694451F4776AAD8D573BC2BB81F686A5` |
| `c173-production-acceptance-manifest.json` | `F0C4E64C7C2A08E2194458C173BDF20CD66B34D0E3827FD665AE97E592471A09` |
| `production-acceptance/c166a2-manifest.json` | `0E8FD191311AC3F904217EBF25DB09691416E3D3E78DD910F7EBC44C829AD3CA` |
| `c172-production-acceptance-manifest.json` | `808782E88D35712C0A043F1AA4812DA622962FC3815123E098352D8A97C67F22` |

The proof-disabled candidate is retained under
`clean-production-candidate/`:

| Candidate | SHA-256 |
| --- | --- |
| Composite `HostLogProof.elf` | `E8A339FD24CC46C50C2FD1BFB418B74AAC799EB73F5F64C7C809FC263345F1BE` |
| `kernel.elf` | `19DD06153BEC2636E38AE6806E747158BC3D0A945B0391D1B4F11C57FF44ED7D` |
| `ramdisk.img` | `DB3C7F192F8072883DBDF2C2F996E83EF279BF594560560BA86E63B5D87F204B` |

Focused boot screenshots are under
`settings-ui-proof-runs/20261018-180728-7838c1c5/`:

| Screenshot | SHA-256 |
| --- | --- |
| `boot1-c177-notes-lf-enter.ppm` | `6B0B6179C930F544C4666133CF64C60F3C516689248C7E894F11A848E4478F99` |
| `boot1-c177-notes-lf-reopened.ppm` | `313B19C6BA04772568C22A51A532CA5415F52AB426421797945965804551484B` |
| `boot2-c177-notes-crlf-enter.ppm` | `C5A79E1AC8A84A3CF45D5418817D34E56F5CB93ED6AB77D7E322F0C20766FEC8` |
| `boot2-c177-notes-crlf-reopened.ppm` | `28EECEDE5F40BB72724248061C197D446BABB03323C545FCD77B5E4BEBDCC29D` |
| `boot3-cross-app-c177-notes-lf-enter.ppm` | `6E8AA5CFDFAE35981C1396C03FDA202385D6EAAED949133BEC8CFCC1ED1FA237` |
| `boot3-cross-app-c177-notes-lf-reopened.ppm` | `9A43800E66C4436A1F5EC657263947FA9FCA07CA35E5EBB45455E3F43FDDAF47` |

Proof-disabled production logs are under
`production-acceptance/ordinary-boots/ordinary-run-20261018-184943-d146a978/`:

| Serial log | SHA-256 |
| --- | --- |
| `ordinary-boot-01/qemu/serial.log` | `E7DF5E770F406C112DDAE91862ABE0A1A504C5636BE32524710A9DADF7B27C5D` |
| `ordinary-boot-02/qemu/serial.log` | `FBC5A3A8C79F4D21EB95B6F484A9A201296175B32FF30D5F8223F0514E204136` |
| `ordinary-boot-03/qemu/serial.log` | `B73234D58F42313229479F01818C3BBDFB4306E8F552109DE88B11C2331C9921` |

The production C177 screenshots in those boot directories are
`ordinary-boot1-c177-notes-lf-{enter,reopened}.ppm`,
`ordinary-boot2-c177-notes-crlf-{enter,reopened}.ppm`, and
`ordinary-boot3-cross-app-c177-notes-lf-{enter,reopened}.ppm`. Their hashes
are recorded in `production-acceptance/c166a2-manifest.json`.

## Checkout preservation and limits

The starting branch and HEAD were `v1.1_DOTNET_SUPPORT` and
`ab2d48d1ae0ad13ef8f5ecbb4900e9087854924b`. The protected pre-existing
`ESP/ramdisk.img` remained at SHA-256
`AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`; the
untracked `samples/host/ManagedUiRegression/obj/` directory was retained.
QEMU media for C177 was isolated under the evidence tree. Foreign QEMU
processes were not changed.

Remaining limitations are the existing 256-byte Managed Notes document cap,
ASCII-only and LF/CRLF-only formats, the 56-column render limit without
wrapping or horizontal scrolling, and the in-place non-atomic VFS write with
no flush or reboot durability. C177 does not change NativeAOT ABI v4 (128
bytes), callback offsets, C174–C176 behavior, or `GXSETT.BIN` v2 (26 bytes).

Recommended C178 direction: evaluate a separately scoped storage contract
for atomic replacement, flush/durability reporting, and larger document limits
before expanding Notes' save guarantees. Do not infer those capabilities from
the verified in-session C152 write/read-back path.
