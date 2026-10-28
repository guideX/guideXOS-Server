# C188R11R4R2R1 Managed Notes Text Entry Acceptance

**Outcome: `NOTES_TEXT_ENTRY_ACCEPTED`**

The single authorized QAC guest run proved that one ASCII `a` reached the Managed Notes document editor through the C120 document route, changed the document buffer at the focused caret, advanced the caret, and appeared in the settled after-input render. Five managed descriptors remained registered and Notes selector 4 returned successfully.

## Revision and preserved worktree state

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD and end of hosted/guest qualification HEAD: `698db369d00f268a045dc8521416ccc85b6e2c1f`.
- Closeout documentation and the focused predicate, runner, fixture, and inventory corrections are committed after this tested HEAD; the closeout commit is the repository HEAD that contains this record.
- The user's pre-existing modified `ESP/ramdisk.img` was preserved. SHA-256 before and after qualification: `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- Pre-existing generated/untracked `bin`, `obj`, and smoke paths were left in place and excluded from the commit. No reset, clean, stash, branch switch, or source-ramdisk write was performed.
- The candidate image was staged separately under the ignored `out/dotnet/c188r11-managed-state-provenance` tree. Its staging manifest reports 185 unrelated files preserved and `changedOtherFiles=0`.

## Focus predicate and preserved R2 evidence

The route-aware predicate now reports Managed Notes window activation, C120 document route, actual editable-widget focus, unrelated-control routing, unsupported routing, and unknown focus as separate facts. Window activation alone cannot authorize input. Editable document focus requires the document pointer route between the before/after-pointer snapshots, the source-defined editor target `(24,99)`, matching native pointer evidence, and the resulting focused editor state (`edit=2`, `render=1`). Later unrelated or unsupported routes prevent stale document focus from qualifying.

The preserved R2 serial fixture `scripts/dotnet/fixtures/c188r11r2/c120-document-route.serial.txt` recognizes the active Notes window and `C120-POINTER target=document result=PASS`, but correctly leaves widget focus unknown because that excerpt has no correlated pointer/editor snapshot. Positive and negative tests cover the exact route, window-only evidence, control focus, disabled routes, missing route evidence, and later control focus invalidating earlier document focus.

## Hosted validation

All focused hosted validation completed before the guest run:

- C188 aggregate: **PASS** — R9 14 cases, prior R10 14, R11 15, runner predicates 30, output bounds 6, runtime lifecycle 7, QAC ownership predicates 15, QAC stream capture 2, keyboard contracts 27, keyboard runner predicates 24. The aggregate restore and runner exit codes were both 0.
- Managed UI aggregate: **PASS** — 38 of 38 suites and 932 of 932 cases. C175 Notes resize correctly reports 24 cases after the two R2 pointer-insertion cases; the aggregate now reads that count from its authoritative inventory.
- Dedicated route-aware predicate suite: **30 cases PASS**; keyboard contracts: **27 cases PASS**; keyboard runner predicates: **24 cases PASS**; managed UI inventory test: **PASS**.
- C188, QAC ownership/stream, output-bound, lifecycle, and keyboard focused checks all passed. PowerShell parse checks and `git diff --check` passed.

## NativeAOT and boot artifact identity

The guest reused the validated persistent-lifecycle NativeAOT candidate; no lifecycle, registry, allocator, or heap change was made for this acceptance. The diagnostic and production runtime-pack manifests both report `persistentCompositeLifecycle=true`, `Primary64KiB` / 65,536-byte managed heap, `bounded-static-image-backed-no-collection`, and passing archive membership. The five-descriptor registry and Notes selector 4 contract remain unchanged.

The one guest used candidate `candidate/notes-text-entry-003`. Its staging manifest reports:

| Boot artifact | SHA-256 |
| --- | --- |
| Staged diagnostic `HostLogProof.elf` (`/APPS/GXOSAPP.ELF`) | `751F6A1934CD8FF5F690ABE1692BC3130CB2B05BBC1213888BF6C2EEB8ED34BE` |
| Candidate ramdisk | `45A64BC1719AD2EA1C6B634D87F157C475EE187ED155FBE42BCCD8862019A507` |
| `kernel.elf` | `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93` |
| `EFI/BOOT/BOOTX64.EFI` | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

The diagnostic runtime-pack source hash is `C47A6DF47EC42EFFE820E97F18D1832C4CD43F80DF2060DF4B6D1EF084544224`; its runtime archive membership passed. Production compatibility was not claimed for this diagnostic boot.

## QAC admission, one guest, and release

The preflight captured an online coordinator, no reservations or sessions, and zero local QEMU processes. QAC granted exclusive acceptance reservation `34b50f33c9654785b33b214caa6c8737` for run identity `1da2f335b1e44190a4cf717aef3d7ff1`. The recorded owner PID was 1952 and the uniquely identified QEMU child PID was 32204.

Immediately before the only key transaction, the runner re-read QAC status, list, and reservation inspection and recaptured owner/runner/child identity. The proof matched the same run and reservation: one owned session, one owned QEMU, zero foreign processes, no degraded state. Exactly one `a` make/break QMP transaction was issued. QAC recorded guest launch at `2026-10-28T00:48:33.0750166Z`; the owned QEMU exited with code 0 at `2026-10-28T00:49:02.3105477Z`; QAC verified release of the same reservation. The final QAC census reported zero QEMU processes, reservations, sessions, queued requests, or blocked requests. No foreign QEMU was terminated and no second boot was launched.

## Guest launch and editor proof

The serial and keyboard-dispatch manifest show the normal production Notes selector 4 path, successful native/managed launch return (`0`), five registered descriptors at managed return and input, Notes resident mask `1`, successful registry lookup (`0`), and stable persistent-heap storage with no GC. The Notes callback was entered and returned `0`.

The actual input sequence was:

1. Managed Notes active-window evidence was recorded.
2. The before-pointer state showed the document unfocused (`focused=0`, caret 0, length 95).
3. `C120-POINTER target=document result=PASS` was emitted between the before/after pointer snapshots. Native pointer-down at local `(24,99)` passed.
4. The after-pointer snapshot showed the document focused, `edit=2`, `render=1`, length 95, and caret 8. The runner classified focus as `editable-document`.
5. Immediately after the pre-key QAC proof, one QMP `a` down/up was sent. Native input event `00000001` mapped qcode `a` / Set-2 scancode `0x1C` to ASCII 97. Managed Notes callback completed successfully.
6. The document changed from length 95 to 96; caret advanced from 8 to 9. The bounded text changed from `Line 00\nLine 01` to `Line 00\naLine 01`, placing exactly one `a` at the established caret. The after-key editor snapshot reported `edit=1`, `render=1`.
7. After render evidence and a 250 ms settle interval, the after-input screenshot showed `aLine 01` in the document area and the modified state. The before-input image showed `Line 01` at that position. The cursor was moved to the title bar before capture so it did not cover the inserted glyph.

Evidence files remain under `out/dotnet/c188r11-managed-state-provenance/runs/notes-text-entry-004/guest-001/` (including raw serial, QMP actions/log, ownership inspections, manifests, and screenshots). Raw serial SHA-256: `8956FE192EB2921BFFBF45662CF8792D126E9AF32EBD5A6D9F514901DDAF06F6`. Before-input screenshot SHA-256: `3E3F7012916FD95D55DB2CB6A015ED3FA28D72D830C6EC64E8AC238341C46A6D`. After-input screenshot SHA-256: `7E6F9159E710D90867D501569F5EDB351F42863AAA8589B3E840A81FB68301D4`.

## Final outcome and next phase

**Outcome A — `NOTES_TEXT_ENTRY_ACCEPTED`.** Actual C120 document focus, the Notes callback, exact single-character buffer insertion, caret movement, settled visible glyph, and five-resident managed state all passed in the one authorized guest. The source ramdisk remained byte-for-byte unchanged.

The smallest justified next phase is the **five-resident managed application qualification phase**. No further C188 diagnostic boot or memory-lifecycle investigation is indicated by this result.
