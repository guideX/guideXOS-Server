# NativeAOT C175 Managed Notes Live Window Resizing

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`

**Branch:** `v1.1_DOTNET_SUPPORT`

**Starting HEAD:** `03d05e5c260fdde6d7ed6b837b59b6bf6f2689dc`
**Outcome:** Accepted. Host, focused guest, and proof-disabled ordinary production gates passed.

## Architecture and ownership

The existing compositor bottom-right drag still proposes the new outer
rectangle. The existing NativeAOT `NativeAotManagedSurface::onWindowResize`
recognizes selector 4 and the current Managed Notes title, encodes the target
with the existing C172 geometry action, and calls the application's existing
managed action dispatcher. It does not add a callback or ABI structure. The
native host keeps ownership of window identity, clipping, the 24-pixel
titlebar, and compositor rollback when the managed callback rejects a size.

Managed Notes decodes the same action through the existing C172 geometry
decoder and creates a C174 `GuideXosManagedWindowResizeTransactionC174`. C174
provides inclusive outer-size validation, the outer/client geometry candidate,
the shared 24-pixel client-height calculation, failure-to-result mapping, and
rollback decision. Notes owns its layout calculation and snapshots the editor
and control state it needs to verify acceptance. It applies the candidate to
the existing editor/control objects, renders the same surface, and completes
the C174 transaction. A layout, render, or state rejection restores the prior
Notes layout and scroll position; the native compositor restores the prior
outer rectangle.

This extends the existing C174 path used by Settings and File Explorer. The
host ABI remains v4 / 128 bytes, callbacks at offsets 104, 112, and 120 remain
unchanged, and Notes retains its existing instance and window identity.

## Notes layout and supported geometry

The existing Notes window starts at **600×360 outer pixels**. C175 derives
application bounds from its existing fixed-width controls, path display,
footer, and editor viewport:

| Outer size | Client size | Editor viewport | Path display |
| --- | --- | --- | --- |
| Minimum 560×344 | 560×320 | 27 columns × 4 lines | 54 px high |
| Initial 600×360 | 600×336 | 32 columns × 4 lines | 72 px high |
| Maximum 960×436 | 960×412 | 56 columns × 8 lines | 72 px high |

The editor retains the existing 8-pixel character width, 18-pixel line height,
and non-wrapping behavior. Its renderer clips to the current viewport. Height
changes update the existing text-area viewport and lower control positions;
horizontal and vertical text scrollbars use the resulting bounds. The editor
starts at y=84, one 18-pixel row below the optional path label at y=66. The
minimum keeps the editor at 27 columns and four lines, leaves at least 54
pixels for the path display, and fits the bottom Settings control at the client
area's lower edge. The maximum is the Notes layout's supported 56 renderable
columns and eight editor lines; larger outer sizes are outside the
application's existing useful editor geometry.

## Editor state preservation

Before applying an accepted candidate, Notes records the document's UTF-8
bytes, current path, caret and selection anchor, preferred column, text focus,
active control, viewport offset, undo/redo indices and availability, text
revision, dirty state, window handle, and launch count. Resize does not call
editing or document hydration APIs. The transaction validates the same values
after rendering. The text area adjusts its first visible line only when the
new viewport makes the old offset invalid; it retains the caret and selection.
Rejected geometry is rejected before Notes changes layout. A later layout,
render, or state failure restores the prior Notes layout and scroll offset.

The implementation leaves document edits, save behavior, undo/redo history,
dirty-state semantics, and application lifecycle to their existing Notes
owners. Existing line-ending and file behavior is not changed. Notes has no
separate multi-document tab model or text wrapping to preserve; neither was
added for this phase.

## Host regression evidence

The consolidated managed UI regression gate passed **26 suites / 765 cases**,
up from the C174 baseline of 24 suites / 736 cases. C172 Settings and C173
Explorer suites were rerun unchanged. C175 adds 22 focused cases and 7 stress
checks. These are hosted tests of the managed layout and editor components;
they are not guest observations. The manifest is
`out/dotnet/c175-managed-notes-resize/host-recheck7/managed-ui-host-manifest.json`
(SHA-256 `ACD91C606CE9867B69F2389B0C2EF313B29D76E6083DAFAA15F91C0A37686C60`).

C175 stress measured 1,000 deterministic layout updates, 500 alternating
minimum/maximum resize operations, 250 rejected geometry requests with 250
rollback checks, and 100 create/resize/close lifecycles. It reported zero
allocation bytes on the measured resize loops and zero cumulative geometry
drift. The full host manifest and its exact log hashes are recorded under
`out/dotnet/c175-managed-notes-resize/host-recheck7/`.

## Focused guest qualification

The proof-enabled focused gate passed three isolated guest boots. Its manifest
is `out/dotnet/c175-managed-notes-resize/settings-ui-proof-runs/20261017-234303-8521e9da/c167-settings-ui-proof-manifest.json`
(SHA-256 `0339FE570ED3FF183A2C0896B79214952AEF52AF775AFBA187CE8003D976E2D5`).

* Boot 1 observed the initial 600×360 Notes surface, accepted live sizes from
  560×344 through 960×436, and rejected requests one pixel outside both axes
  at both limits. Rejected requests left the Notes layout unchanged and the
  compositor reported `REJECT`; accepted requests retained the same Notes
  instance/window. The serial trace records client dimensions, editor columns
  and rows, focus, and layout snapshots.
* Boot 2 opened `/system/apps/C164/hello.txt` through the existing `.txt`
  association and rendered the exact text `hello from C164`. It resized while
  open, edited, exercised undo/redo, and returned to the original 15 document
  bytes. The final record had caret 15, anchor 15, view 0, history index 0,
  undo unavailable, redo available, and `dirty=0`.
* Boot 3 kept Settings (`000003E9`), Explorer (`000003EC`), and Notes
  (`000003EB`) resident with independent window identities. Notes resized
  while the other windows remained open; Settings and Explorer also resized
  with Notes resident. Explorer retained the selected file, Notes retained
  its document/editor state, and Settings retained pending association
  semantics. Focus moved among the windows without replacing their surfaces.

Per-boot serial SHA-256 values are Boot 1
`124795B5EA5AF4AC1899A8D12083E5D34D4609C4024BDF47D732125647F484B4`, Boot 2
`C0DB410842B9E6A0844144BAFB4E8EE645684FEF9DF91D84A0DE9CDB0F48C532`, and
Boot 3 `D7D64ECA998D923189FA510E2A6D65734C40AECFC62BE32B1EA948738E5B936D`.
QMP traces, serial logs, screenshots, and per-boot media are retained under
`out/dotnet/c175-managed-notes-resize/settings-ui-proof-runs/20261017-234303-8521e9da/`.

## Proof-disabled production acceptance

The ordinary NativeAOT production composite, kernel, and ramdisk were built
without `GUIDEXOS_PROOF_CONTROL`. The build's byte-string scan and the retained
rescan found none of the proof-control, scenario, C172, or C175 geometry
markers in any of the three artifacts. The scan report is
`out/dotnet/c175-managed-notes-resize/production-acceptance/c175-proof-disabled-binary-scan.json`
(SHA-256 `AF0EDB59ACC3161112A0BB5BCB6CD4F2CDEB15E29DC2F6E5387773098D9C5F08`).

Three ordinary proof-disabled QEMU boots passed. They booted isolated media
copies; the final C170 evidence step verifies every boot's kernel and ramdisk
SHA-256 against the clean candidate. A compatibility check originally compared
those isolated images with the protected checkout's `ESP` files and rejected
the intentional difference. The C170 evidence writer now distinguishes
isolated production media from checkout-installed media and verifies the
actual boot copies. It retains separate records for the protected checkout
files; no repository ramdisk was replaced.

* **Boot 1, geometry:** Managed Notes retained generation/window
  `00000004:000003EB` and editor focus. It accepted 960×436, 560×344, and the
  restored 600×360 size. Requests at 961×436, 960×437, 559×344, and 560×343
  were rejected and rolled back to the preceding accepted size. Screenshots
  include the maximum (`ordinary-boot1-c175-notes-maximum.ppm`, SHA-256
  `7DE335123B1875BDF1CBBCB1A00AFBFA6B0701508DBC8A0426D63D904867842D`),
  minimum (`ordinary-boot1-c175-notes-minimum.ppm`,
  `891AB469A4FB151F77DC69BBFE8313742B0B3EF9DEA0FE292837570644D5380C`), and
  rejected maximum width (`ordinary-boot1-c175-notes-rejected-maximum.ppm`,
  `06C16DB4DA1690F150450F4F80C56BFE685D93BB57B1C4D7C4B7A41DA73FBCAD`).
* **Boot 2, editor:** Managed Notes opened `/system/apps/C164/hello.txt` through
  the existing file association and verified the exact 15-byte text `hello
  from C164`. The run selected the first character, resized at maximum,
  inserted five lines, reduced to minimum while exercising the viewport,
  exercised undo and redo, and returned to the saved revision without issuing
  Save. The proof-disabled production build does not emit internal C175 state
  records, so its final in-memory state snapshot is unavailable; the focused
  proof boot above verifies the exact path, caret, selection, history, redo,
  dirty state, and focus invariants. The proof-disabled screenshots are
  `ordinary-boot2-c175-notes-editor-maximum.ppm`
  (`AA3ABA27D0151F0D0A11C5C5E43695C04A9A2E2CCBC41E82195FECAA0FFBD9C4`),
  `ordinary-boot2-c175-notes-editor-minimum.ppm`
  (`AD69B9839EC8C49FA85D7ED864319032D806DDBD90262424D000CD2B6D61243E`), and
  `ordinary-boot2-c175-notes-editor-restored.ppm`
  (`3469383F3951D864C13C66BC04491A094A975B22169744A67615721F002DBB20`).
* **Boot 3, concurrent apps:** Settings (`00000007:000003EE`), File Explorer
  (`0000000A:000003F1`), and Notes (`00000009:000003F0`) had distinct,
  simultaneously resident window identities. Notes resized with both other
  windows open; Settings and Explorer resized while Notes remained open.
  Explorer retained `/system/apps/C164` and `hello.txt`. The Settings
  `Disabled` selection remained pending and was not written before Apply; the
  ordinary association test ended with the expected system-default Notes
  handler. The boot also passed managed Calculator (`7*8=56`) and Task Manager
  launch/refresh/close checks.

The ordinary source manifest is
`out/dotnet/c175-managed-notes-resize/production-acceptance/c166a2-manifest.json`
(SHA-256 `5407517093CDF2C3EA25CDBD9D4DCCB78ECF39C019F1F2357823727A27B75ECA`).
The C170 ordinary-media manifest is
`out/dotnet/c175-managed-notes-resize/production-acceptance/c170-ordinary-production-manifest.json`
(SHA-256 `65EF2246F857B02C8E82DD526B7B9E65CD8A127C0722E972678B5C5D5629D9F3`).
Both record three passing boots. Production serial SHA-256 values are Boot 1
`08142421C36C40BD38FE427FCE96DCCE9A8479CA606DDD3638F47B1A0F91AD4E`, Boot 2
`2DB0CBCF406010FF167BFD3A21B25FEFC4FE12CF1B7C9E2EC6BA601CCD5E119F`, and
Boot 3 `FD09168EB18B6911A79F90D0AFB577960FA7655377B9094BE6CE99C69437EA2C`.
Their QMP input logs and screenshots are kept beside each serial log under
`out/dotnet/c175-managed-notes-resize/production-acceptance/ordinary-boots/ordinary-run-20261018-000639-0bb962c5/`.

## Artifacts and hashes

| Artifact | SHA-256 |
| --- | --- |
| Proof-disabled NativeAOT composite `clean-production-candidate/build/composite-c166-production/artifacts/HostLogProof.elf` | `3FDE23932E3757EA755E1890708CF645003D7654F21314D76C22643F4A205887` |
| Proof-disabled kernel `clean-production-candidate/kernel.elf` | `19DD06153BEC2636E38AE6806E747158BC3D0A945B0391D1B4F11C57FF44ED7D` |
| Proof-disabled ramdisk `clean-production-candidate/ramdisk.img` | `8BD2154BDA847B22B45EAAD8076C0C55498A02278871F08757F189D2F3768F44` |
| Host regression manifest `host-recheck7/managed-ui-host-manifest.json` | `ACD91C606CE9867B69F2389B0C2EF313B29D76E6083DAFAA15F91C0A37686C60` |
| Focused guest manifest `settings-ui-proof-runs/20261017-234303-8521e9da/c167-settings-ui-proof-manifest.json` | `0339FE570ED3FF183A2C0896B79214952AEF52AF775AFBA187CE8003D976E2D5` |
| Proof-disabled binary scan `production-acceptance/c175-proof-disabled-binary-scan.json` | `AF0EDB59ACC3161112A0BB5BCB6CD4F2CDEB15E29DC2F6E5387773098D9C5F08` |
| Ordinary production source manifest `production-acceptance/c166a2-manifest.json` | `5407517093CDF2C3EA25CDBD9D4DCCB78ECF39C019F1F2357823727A27B75ECA` |
| Ordinary production media manifest `production-acceptance/c170-ordinary-production-manifest.json` | `65EF2246F857B02C8E82DD526B7B9E65CD8A127C0722E972678B5C5D5629D9F3` |
| Protected checkout `ESP/ramdisk.img`, before and after acceptance | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

Every per-boot proof-disabled kernel and ramdisk copy matches the candidate
hashes above. The protected repository ramdisk remains the pre-existing
modified file and retains its starting hash; it is excluded from the C175
commit.

## Limitations and C176 handoff

Notes retains its existing non-wrapping text surface and bounded editor model;
automatic line wrapping and multi-document tabs remain unsupported. The
proof-disabled production build deliberately omits internal editor-state
diagnostics. It exercises selection, keyboard insertion, viewport navigation,
undo/redo, and return to the saved revision; exact internal caret, anchor,
history, and dirty-state values are verified in the focused proof build and
host suite. The recommended C176 handoff is to exercise more line-ending
variants and broader editor-navigation cases in an ordinary guest while
continuing to use the C174 transaction. Do not start C176 as part of this
phase.
