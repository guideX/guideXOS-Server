# NativeAOT C148 — Settings v2 and Scroll Amount

C148 evolves the existing bounded Settings Center file by one byte and makes
wheel movement magnitude an applied runtime setting. The existing
NaturalScroll setting continues to control direction. The default amount is
three logical rows/lines per normalized notch, matching C137's prior behavior.

## C147 baseline and v1 audit

C147 loads /system/apps/GXSETT.BIN into shared runtime state before the first
managed application dispatch. The VFS resolves that path to /GXSETT.BIN on
the existing writable boot-time FAT volume. Runtime state is owned by the
existing managed control host; applications do not parse the settings file.

The v1 file is exactly 25 bytes:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII GXSC |
| 4 | 2 | Unsigned version 1, little-endian |
| 6 | 2 | Payload size 9, little-endian |
| 8 | 4 | Flags, required to be zero, little-endian |
| 12 | 1 | Density enum: 0 or 1 |
| 13 | 1 | Show status Boolean: 0 or 1 |
| 14 | 1 | Show advanced Boolean: 0 or 1 |
| 15 | 1 | Input enabled Boolean: 0 or 1 |
| 16 | 1 | Natural Scroll Boolean: 0 or 1 |
| 17 | 1 | Scroll speed enum: 0, 1, or 2 |
| 18 | 1 | Show keyboard tips Boolean: 0 or 1 |
| 19 | 1 | Status detail enum: 0 or 1 |
| 20 | 1 | Report format enum: 0 or 1 |
| 21 | 4 | CRC-32/IEEE of bytes 0–20, stored little-endian |

The 9-byte payload is fully semantic; v1 has no reserved payload bytes. The
header flags are not spare setting storage. C148 therefore introduces version
2 instead of reinterpreting a v1 byte. The parser retains the 64-byte hard file
size cap and rejects unknown versions, wrong lengths, nonzero flags, invalid
field values, and checksum mismatches.

The authentic C147 custom v1 proof record is
SHA-256 952CA2183EE7DA2923EF76DBC121193C499750BDA7627D862D7F8B8BF5734FF5
(25 bytes). It has Natural Scroll enabled and the following legacy values:
density 1, show status 0, advanced 1, input enabled 1, speed 1, keyboard tips
1, detail 0, and report format 0.

## v2 layout and migration policy

Version 2 keeps the same 12-byte header and all nine v1 payload bytes in their
original order. It appends one byte for ScrollLinesPerNotch, followed by the
CRC:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0–11 | 12 | Existing GXSC header; version is 2 and payload size is 10 |
| 12–20 | 9 | The unchanged v1 semantic fields |
| 21 | 1 | ScrollLinesPerNotch, valid from 1 through 8 |
| 22–25 | 4 | CRC-32/IEEE of bytes 0–21, stored little-endian |

The v2 file is exactly 26 bytes. A v1 load reads and validates every legacy
field, copies NaturalScroll, and supplies the canonical amount of 3 in memory.
Loading and opening Settings Center do not write the file. Only a successful
explicit Apply serializes v2, overwrites the existing bounded file, checks its
stat and full read-back, validates the parsed record against the candidate,
and then commits the whole runtime snapshot. A failed write leaves the prior
runtime/applied/persisted snapshots in place and keeps the working snapshot
dirty.

The host-backed FAT path has a specific 25-to-26-byte growth case: the first
write makes the larger directory size visible, and a second write persists all
26 record bytes after that size change. The kernel verifies the full read-back
before reporting success. If verification fails, it attempts to restore the
backed-up 25-byte v1 record and verifies that restoration before returning
failure. This protects the old record from returned I/O failures; the
filesystem does not provide an atomic transaction across sudden power loss.

This is a two-version parser, not a general migration framework. Both versions
are explicit fixed-width layouts. There is no TLV, registry, JSON/XML layer,
settings daemon, or per-control file access.

## UI and runtime behavior

The Input section has a real eight-choice ComboBox: 1 line through 8 lines.
Its mapping is explicit in both directions: selection 0 means 1 line,
selection 1 means 2, …, and selection 7 means 8. ComboBox indices are not
serialized. The default maps to 3 lines.

The immutable shared runtime snapshot contains NaturalScroll and
ScrollLinesPerNotch. Applying settings publishes the full validated snapshot
after verified persistence. Before Apply, an edited working amount does not
affect the active runtime or persisted state. Reset changes the working
snapshot only; Apply is required to persist defaults.

The existing host wheel route composes settings at the logical policy layer:

1. The native PS/2 transport and C137 packet normalization produce the
   existing signed logical notch delta.
2. NaturalScroll transforms its sign.
3. ScrollLinesPerNotch multiplies each normalized notch's requested movement.
4. Each consumer's existing viewport clamps the result.

No raw PS/2 packet parser, IntelliMouse negotiation, ABI event kind, or bounded
accumulation code is changed. Deltas are bounded to eight notches before
multiplication, and consumer movement arithmetic is bounded. Direction and
magnitude are separate settings.

## Capacity and architecture

| Measure | Before | After | Existing capacity |
| --- | ---: | ---: | ---: |
| Settings Center host registrations | 9 | 10 | 10 |
| Input GroupBox/stack members | 5 | 6 | 8 |
| ScrollView direct members | 21 | 22 | 24 |
| Leaf controls | 17 | 18 | — |

No capacity increase is needed. The existing application-owned controls,
non-owning container references, single modal scope, single transient capture
owner, focus path, and ScrollView remain in place. Host ABI remains v1 with
table size 104.

## Verification

The C148 focused suite keeps the C146 v1 format coverage and adds dedicated
v1-to-v2 migration, v2 bounds, runtime transaction, direction/magnitude
orthogonality, consumer clamping, and startup compatibility cases: 23 format,
17 runtime, and 15 consumer checks. C146 format,
store, and Settings Center suites run alongside the C147 startup, runtime, and
consumer regression suites. C144 and C145 claims are limited to the integrated
Settings Center and dialog paths exercised by this phase; their standalone
suites are historical unless the C148 manifest says otherwise. C137's
historical 46-case physical-wheel result is referenced separately from any
fresh C148 run.

After the final source guard fix, a fresh focused C137 regression run also
passed all 46 managed/native wheel checks and three fresh production transport
boots. Per-boot serial hashes and classifications are recorded in
`out/dotnet/c137-mouse-wheel-scrolling/c137.manifest.json`.

The C148 NativeAOT regression composite uses a 256 KiB managed heap so its
C146, C147, and C148 focused suites can run in the same resident proof process.
This is a proof-build setting; ordinary runtime-pack builds retain the existing
64 KiB default.

The production runner executes three distinct sequences:

1. Start with the authentic 25-byte C147 v1 record, boot Notes-only without
   opening Settings Center or applying changes, prove the default three-row
   movement, and verify the v1 hash is unchanged after clean shutdown. Then
   boot Settings Center, verify it still leaves v1 unchanged on open, select
   amount 5, Apply v2, and reboot Notes-only to prove the real ListBox moves
   five rows before Settings Center can launch.
2. Start with a custom v2 record using Natural Scroll and amount 7, prove the
   real Notes movement before Settings Center, and verify that both controls
   hydrate cleanly without rewriting the file.
3. Start from custom v2 settings, prove the custom Notes movement, Reset and
   verify the custom runtime remains active until Apply, persist defaults, and
   reboot into Notes-only mode to prove default direction and three-row
   movement.

A malformed v2 record with an invalid amount and a valid recomputed checksum
also boots Notes-only, falls back to defaults, and retains the malformed file
without a startup write. The Settings Center failure-injection tests check that
a failed Apply retains the previous runtime/persisted amount, leaves the new
working amount dirty, and allows a later retry.

The runner records source and resulting file versions, sizes, SHA-256 hashes,
legacy field values, runtime values, serial hashes, and authoritative
firstVisibleIndex before/after movement. It saves both proof kernels under the
evidence directory, restores the canonical ordinary kernels and protected
ramdisk, checks their original hashes, and executes three fresh ordinary QEMU
boots. See the generated out/dotnet/c148-settings-v2-scroll-amount/c148.manifest.json
and ordinary.manifest.json for run-specific results.

## Evidence status

This document describes the C148 implementation and proof protocol. Run
results, hashes, outcome classification, regression counts, and ordinary boot
serial hashes are recorded in the generated C148 manifest after the full
NativeAOT/QEMU runner completes; historical phase evidence is not silently
reclassified as a fresh C148 pass.
