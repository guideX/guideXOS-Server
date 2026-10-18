# NativeAOT C176 Managed Notes Navigation and Line Endings

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`

**Branch:** `v1.1_DOTNET_SUPPORT`

**Starting HEAD:** `cdc292ea650bd916dbf923f81510e2788de97c8b`
**Outcome:** A — Accepted. Host regression, three focused guest boots, and three proof-disabled ordinary production boots passed. The final production media audit passed against run `ordinary-run-20261018-092325-d3bd117f`.

## 1. Starting audit

The repository, branch, and starting HEAD matched the request. The inherited
`ESP/ramdisk.img` worktree modification was preserved at SHA-256
`AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The
pre-existing untracked `samples/host/ManagedUiRegression/obj/` directory was
also preserved. The active QEMU processes were identified by command line and
media path; the unrelated QEMU owned by
`C:\gxR2\out\taskmanager-memorydetails-qualification-r2` was left untouched.
The C174 shared resize and C175 Notes resize reports and their manifests were
reviewed before implementation. Existing acceptance scripts use source-head
and per-file hash checks rather than `.phase` files; those protections were
retained and extended for C176.

Before C176, `GuideXosTextArea` stored a bounded document in a fixed `char[]`
and used UTF-16 buffer indices as caret and selection positions. The content
contract was printable ASCII plus LF. Line count and line boundaries were
derived from LF. A CR in CRLF was treated as an unsupported control, so CRLF
loads failed. CR-only text was unsupported. The control already had vertical
preferred-column navigation, Home/End, selection, undo/redo, vertical viewport
scrolling, and visible-caret tracking. It did not have Page Up/Page Down,
word wrapping, horizontal scrolling, or horizontal navigation beyond the
rendered columns. Long lines were clipped at the configured render width.

Notes retained its existing bounded document state, path, dirty flag, and
history. The C174 resize transaction was already used by C175. The editor's
initial outer size remains 600x360, with minimum 560x344 and maximum 960x436;
the titlebar remains 24 pixels high. The C176 work does not alter save
semantics or add a new file-writing path.

## 2. Supported text and navigation behavior

C176 now accepts LF and CRLF input, including mixed LF/CRLF documents. Line
counting uses LF, so CRLF contributes one logical line boundary while retaining
both physical characters in the backing buffer. Caret and selection indices
are physical buffer indices, normalized so a caret cannot sit between the CR
and LF of one CRLF pair. Left/Right cross the pair atomically; Backspace and
Delete remove it atomically. Rendering treats the pair as one delimiter and
does not draw a carriage-return glyph or create an extra line. `ToUtf8()`
serializes the stored characters without newline normalization.

LF, CRLF, no-final-newline, trailing-newline, empty-line, consecutive-empty-
line, and mixed-delimiter cases are covered. Mixed input is preserved as
authored. Enter continues to insert LF, so editing a CRLF document can create
a mixed-delimiter document; this phase does not normalize or select a
preferred save format. CR-only input remains rejected atomically.

The existing keyboard contract remains Up/Down with a preferred column,
Left/Right, Home/End, Backspace/Delete, Enter, and shift-selection. Caret and
anchor remain valid at document start/end and across short, long, and blank
lines. Vertical scrolling keeps the caret visible and remains clamped to the
document. Resizing recalculates clipping and viewport state without changing
text, path, selection validity, preferred-column state, dirty state, or
undo/redo history. A viewport offset may change when the visible height
changes. No Page Up/Page Down, wrapping, horizontal scroll, or support for
non-ASCII text was added. The bounded control remains limited to 1,024
characters, 128 lines, and 56 renderable columns in Notes.

## 3. Fixes and compatibility

The reproduced defect was that CRLF files were rejected by the LF-only input
validator and could not be navigated as multiline text. C176 makes the narrow
editor correction: accept CR only when paired with LF, preserve the pair in
the buffer, and treat it as one logical delimiter for rendering, caret
movement, editing, and viewport calculations. CR-only remains a negative
compatibility probe. Host tests prove exact mixed/LF/CRLF serialization and
atomic rejection of CR-only input.

The existing C174 transaction is reused for candidate geometry, bounds,
client-height calculation, result mapping, and rollback. C175 Notes bounds,
24-pixel titlebar, resize callback/action flow, and native compositor behavior
are unchanged. No kernel files, NativeAOT ABI structures, callbacks, callback
offsets, association persistence format, Settings operation, or Explorer
operation were changed. ABI v4 / 128 bytes and callback offsets 104, 112, and
120 remain intact.

Three validation-harness adjustments were needed to make the requested
existing workflows reliable. Explorer entry selection falls back to its real
list keyboard navigation when the pointer sweep reaches only visible rows.
The invalid-resize assertion records the actual rejected 599x359 or 600x359
QMP candidate and verifies a below-minimum dimension plus full rollback; it
no longer assumes that a one-pixel pointer delta survives QMP rounding.
Focused Notes window discovery now distinguishes the teal active titlebar from
inactive Notes windows and raises an existing visible Notes surface by clicking
its titlebar before measuring it. The production C173 audit now checks kernel
and ramdisk hashes in each ordinary boot's isolated ESP, and separately checks
that the checkout media still matches its protected baseline. These changes
affect validation only, not production application behavior.

## 4. Host regression results

The consolidated managed UI host suite passed **28 suites / 792 cases**,
up from the preserved **26 suites / 765 cases** baseline. C176 contributes 20
focused cases and seven stress assertions. The focused group covers empty,
LF/CRLF, trailing delimiters, blank lines, mixed formats, CR-only rejection,
caret normalization, preferred column, Home/End, selections, scrolling,
non-wrapping clipping, atomic editing, history, clipboard, resize state, and
exact serialization.

The C176 stress group passed with zero state drift and zero resize hot-path
allocations:

| Workload | Result |
| --- | ---: |
| Deterministic navigation operations | 1,000 |
| Alternating minimum/maximum resize requests | 500 |
| Navigation plus resize combinations | 250 |
| Rejected below-minimum geometry requests | 250 |
| Notes lifecycle cycles | 100 |
| Resize allocations | 0 bytes |
| State/geometry drift | 0 |

The C172, C173, C174, and C175 host groups also passed. Their existing stress
coverage includes 1,000 runtime geometry updates, 1,000 Explorer layout
updates, 1,000 C174 transitions, and C175 resize/layout/rollback workloads.
The separate C166R9 legacy host manifest passed with native 604 cases and
managed 34 cases, including 1,000 persistence transactions and dual-slot
checks.

Host manifests:

| Artifact | SHA-256 |
| --- | --- |
| `out/dotnet/c176-managed-notes-navigation/host/managed-ui-host-manifest.json` | `EC6B1B4A0381BA0E4AC4427F0F609524E2BCB05CE27CB51D08EE7B83B68501F7` |
| `out/dotnet/c176-managed-notes-navigation/c166r9-host-manifest.json` | see file; preserved as a separate legacy-gate artifact |

## 5. Focused guest qualification

Three isolated proof-enabled QEMU boots passed in run
`20261018-094613-54956651`. Each boot logged
`C176-NOTES-NAVIGATION focused=20 result=PASS`,
`firstFailedCase=0`, and fixture-collection PASS. Boot 1 used the LF fixture;
Boot 2 used the CRLF fixture and exercised editing/undo/redo back to the
original document revision; Boot 3 ran the concurrent Settings, Explorer,
and Notes workflow. The focused tests inspect internal editor state. The
three-window boot retained independent windows, Explorer selection, and
Settings association state while Notes stayed open and navigable.

The C167 proof manifest also records Calculator result `56`, Task Manager
launch/refresh/close, Settings state, Explorer selection, Notes activation by
`.txt` association, and shared-ESP state. Relevant focused evidence is under:

`out/dotnet/c176-managed-notes-navigation/c167/settings-ui-proof-runs/20261018-050156-1c9bfbe3/`

| Evidence | SHA-256 |
| --- | --- |
| Focused C167 proof manifest | `637C323CF9C175B1407A685A8C8B8F1A1DE2672F331BC282F7157E8EB06F8472` |
| Boot 1 serial log | `1C3B8106CDEB2505B8BB6A9DFC27C6191B81C3EC9ADB7D0681584A0B13966948` |
| Boot 2 serial log | `F8764F1709CF1B9D59005C341D9EB246A6CC5A1B6550378B828EB4C35D2AC232` |
| Boot 3 serial log | `41F74AAD0316E0D5F3F179C57EF180E80693FC79F21DB87F4BEA9E4282489ACB` |
| C172 focused manifest, three boots | `E9B74B343EEDC867217B58D25DFA7ED2FDE528EA59CBBC2DD2890052A08D53B4` |
| C173 focused manifest, three boots | `D2CB269E022531AED7FA66E2B6378C1BBCCAA5BF7C0FD4D04B9FE15C73D2FF91` |

Representative visible artifacts are `boot1-c176-notes-lf-scrolled.ppm`,
`boot2-c176-notes-crlf-after-navigation.ppm`, and
`boot3-cross-app-c176-notes-lf-after-navigation.ppm` in that run directory.
The manifest contains hashes for the full screenshot set, QMP inputs,
per-boot status, and isolated writable ESP. Proof-only diagnostics are not
claimed as production observations.

## 6. Proof-disabled ordinary production qualification

The proof-disabled candidate completed three ordinary guest boots in run
`ordinary-run-20261018-092325-d3bd117f`. Internal caret/history state is not
available in this configuration; production claims are limited to visible
interactions and serial acceptance markers. Boot 1 covers LF navigation and
resize, Boot 2 covers CRLF editing compatibility, and Boot 3 covers concurrent
Notes, Settings, and Explorer. The run also retains the existing `.txt` Notes
association and Calculator, Task Manager, Settings, and Explorer smoke
coverage. Per-boot screenshots, serial logs, QMP input logs, and candidate
identity are stored under:

`out/dotnet/c176-managed-notes-navigation/production-acceptance/ordinary-boots/ordinary-run-20261018-092325-d3bd117f/`

The runner completed all three ordinary boots; its first final C173 media
audit exposed a validator error because it compared the clean candidate to the
protected checkout ESP. After correcting the validator, the final manifest
was regenerated against the retained boot evidence. It confirms the three
isolated ESPs each contain kernel SHA-256
`19DD06153BEC2636E38AE6806E747158BC3D0A945B0391D1B4F11C57FF44ED7D` and
ramdisk SHA-256
`9B099E0D26E408B8DB9C6E5A8E5B007E684E6CB31AD5F627713A9BBB1E0F9CF8`.
The protected checkout kernel and ESP kernel remain at
`9F3091916D7CB6C109BB5D71B65E1D7F1047AB576C8C994105179ECCB6E6A8F1`; the
checkout ESP ramdisk remains at its starting
`AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
Ordinary user-visible results remain distinct from focused guest internal
state evidence and host-only tests.

| Artifact | SHA-256 |
| --- | --- |
| `out/dotnet/c176-managed-notes-navigation/production-acceptance/c166a2-manifest.json` | `20CD1F543ADE1BF6CEEDA66470F09E7CF55EB0F8BB8A740ABDFBAED58BB32EC8` |
| `out/dotnet/c176-managed-notes-navigation/c172-production-acceptance-manifest.json` | `550690B92B7E4C99EFD0CE284E4621B90C986DE19C82D471FB58A82E64B2BD71` |
| `out/dotnet/c176-managed-notes-navigation/c173-production-acceptance-manifest.json` | `85E349DBFA0402C9DE8EEF45011B5FF9CDE58FA89D609A0A126E82D13CC2DBCD` |
| Ordinary Boot 1 serial log | `1D76AA6A23E54FE3060A17D763D88CA03DF9B2C2A1DB343C0217F76C8C5C1ADA` |
| Ordinary Boot 2 serial log | `0042BF0D03CD31AE1883D1B76269613D349114BC241EB32F127FBCB6C1DEB1DB` |
| Ordinary Boot 3 serial log | `10C208DD7B75CEB394C02725DD12A0A4522A1CB54D9E5FFECE9649C7EC2A38E2` |

## 7. Artifacts, remaining limits, and next phase

The principal source and test changes are in `GuideXosTextArea.cs`,
`GuideXosTextAreaC176Tests.cs`, Notes resize-state integration, the managed UI
host runner, and the isolated acceptance scripts. C176 fixture files are
generated under the isolated wallpaper-pack/proof workflow; checkout
production media was not used as proof media. The pre-existing ramdisk
modification and untracked host `obj/` directory remain outside the C176
commit.

Remaining limits are explicit: CR-only files are rejected; accepted text is
ASCII and bounded to 1,024 characters / 128 lines; long lines are clipped at
56 renderable columns; there is no word wrap, horizontal scrolling, or Page
Up/Page Down; Enter inserts LF in a CRLF document; and no automatic newline
normalization or dominant-ending save policy exists. C176's CRLF focused
guest workflow edits and undoes/redoes without saving, so it does not claim a
file-save round trip for CRLF through the guest UI.

Recommended C177 direction: if product requirements call for it, decide and
test an explicit newline policy for inserted lines and saving. Keep
Page-Up/Page-Down and horizontal navigation as separate scoped feature work.
No C177 implementation is started in this phase.
