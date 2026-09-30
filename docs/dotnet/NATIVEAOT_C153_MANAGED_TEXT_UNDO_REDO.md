# NativeAOT C153: Managed Text Undo/Redo

## Outcome

**Outcome A — bounded reusable Undo/Redo validated.** Managed Notes can restore prior text states, return exactly to the last successful Open/Save checkpoint, and redo away from it while retaining the existing C150–C152 lifecycle. The implementation adds no editor framework, modal layer, focus system, ownership tree, or host ABI change.

## Mutation and lifecycle audit

`GuideXosTextArea` remains the authoritative document buffer. Its fixed `char[]` is the only live text buffer; `GuideXosTextHistory` is a reusable bounded snapshot store and has no VFS, path, dialog, or Settings dependency.

| Operation | C153 behavior |
| --- | --- |
| Printable character and LF insertion | Successful content mutation is recorded once after the buffer and caret are updated. Rejected input records nothing. |
| Backspace and Delete | Successful deletion, including deletion of a selection, is recorded once. No-op at a boundary records nothing. |
| Selection replacement | Existing anchor/caret selection is preserved in snapshots; replacing selected text is one content revision. C153 adds no selection model. |
| Caret movement, selection movement, focus, visibility, scrolling, wheel, menus, window lifecycle | No content revision. Caret and anchor movement update metadata on the current snapshot. |
| `SetText` / `SetUtf8` | Valid whole-document replacement clears prior states and establishes one new baseline. Invalid input is transactional and leaves text/history alone. C151/C152 successful Open hydration therefore cannot be undone into the old document. |
| Undo / Redo | Restore a stored state without recording another revision, recompute line count, clamp caret/anchor, reveal the caret, and emit one `ContentChanged` notification. Unavailable operations are no-ops. |
| Hide / close / relaunch | Visibility does not reset the current control's history. The C150 application teardown/relaunch creates a fresh Notes instance; history is in-memory and does not survive relaunch or reboot. |

There is no persistent undo, multi-document history, branching history, time/word coalescing, clipboard, rich text, crash recovery, autosave, New command, or document-session restoration.

## Bounded history and memory

The Notes TextArea supports 256 UTF-16 characters and 32 lines. History retains 16 complete states by default (the implementation supports capacities through 32). Editing, undo, and redo copy into preallocated arrays and do not allocate per-keystroke history objects.

Each snapshot stores 256 `char` slots (512 bytes) plus four 32-bit metadata fields (length, caret, anchor, reserved) and a 128-bit revision identity (32 metadata bytes total). The fixed history payload is therefore:

* text: `16 × 256 × 2 = 8,192` bytes;
* snapshot metadata: `16 × 32 = 512` bytes;
* **history arrays total: 8,704 bytes**.

This is array payload and excludes managed array headers, object fields/alignment, and TextArea's separate 256-character live buffer (512 bytes). Capacity is fixed; there are no unbounded lists or per-edit allocations in the history path.

The current state index moves within the ring. Editing after Undo first discards all forward states, then appends the new state. At capacity, the oldest retained state is evicted while the newest chain remains usable. `CanUndo` is false at the oldest state; `CanRedo` is false when no forward state exists.

Every baseline/content revision receives a 128-bit `(generation, sequence)` identity, never a ring index. Sequence rollover advances generation. If both unsigned 64-bit components are exhausted, identity creation fails closed: invalid identities cannot match a saved checkpoint, so Notes stays dirty instead of reporting a false clean state. Ring-slot reuse cannot recreate a prior identity.

## Caret, viewport, callbacks, and save point

Each snapshot captures text length, caret, selection anchor, and revision identity. Caret/anchor-only motion updates the current snapshot. Undo/Redo restores those positions, clamped to the restored text length. The viewport is not snapshotted: restoring a state recomputes line count and minimally reveals the restored caret through the existing viewport logic, which clamps the offset to its valid range.

`GuideXosTextArea.ContentChanged` fires once after a successful content edit, Undo, or Redo. Hydration instead raises `BaselineEstablished`; it does not masquerade as a user edit. Restoration does not recursively enter history.

`GuideXosNotesDocumentState` owns the current path and one saved revision identity, not a second text buffer/history. `Dirty` is derived from the live TextArea: it is true if the saved token is invalid, unreachable, or different from the current token. If ring eviction removes the saved state, every remaining state is dirty until a successful Save/Save As or Open establishes a new checkpoint. No reused slot can appear clean by accident.

* Successful Open transactionally replaces the text, clears old history, establishes one baseline, changes path, and is clean. Failed Open and canceled Open leave the prior document/history/checkpoint unchanged. A dirty Open prompt is based on derived `Dirty`; when Undo returns to the saved revision, Open proceeds to its chooser without that prompt. Canceling the chooser preserves the document.
* Successful Save keeps all history and marks the current revision saved; it creates no edit revision. Undo away from it is dirty and Redo back to it is clean.
* Successful Save As keeps history, updates the path, and marks the displayed text revision saved. Undo changes text/dirty state but never rolls the path back. Canceling Save As leaves path, text, history, and checkpoint alone; canceling overwrite returns to the chooser without changing them.
* Failed Save leaves the saved token and history untouched and retains the existing C152 transactional buffer/path behavior. `MarkSaveFailed` intentionally does not manufacture dirty state.
* Dirty Close and Notes-to-Settings checks consume the same derived dirty state. The proof shows Undo to the checkpoint avoids the prompt; Redo away from it restores the Close/Settings prompt. After a retry Save, Undo/Redo returns exactly to the new checkpoint and the clean Settings transition has no Notes prompt.

## Menu and compatibility

Notes adds **Undo** and **Redo** before the existing Open/Save commands, with enabled state refreshed when the existing Options popup opens. Existing popup action routing returns focus to the TextArea after a successful restore. Commands are menu-only: the managed input event exposes Shift but not Control, so Ctrl+Z/Ctrl+Y are not implemented. The existing popup/menu system is reused.

Host ABI remains version 1 with a 104-byte table; settings remain v2. The existing one-active-application and C150 return-target lifecycle, C145 modal scope, C151 Open dialog, and C152 Save dialog remain in place. The accepted document format remains printable ASCII plus LF, maximum 256 bytes and 32 lines. Existing C152 limits also remain: no power-loss atomicity guarantee, and FAT create names may have narrower 8.3 support than chooser input.

## Verification

Focused proof suites passed on every Notes launch:

* history core: **26/26**, including bounded ring behavior, revision identity/ABA checks, eviction, no-op boundaries, and 100 actual mutations;
* TextArea integration: **25/25**, including insert/LF/Backspace/Delete, caret/selection, callback count, undo/redo, viewport clamp, and 100-mutation stress;
* Notes save-point state: **17/17**, including Open baseline, Save/Save As checkpoint retention, failed/canceled operations, dirty derivation, eviction, and Undo/Redo.

Regression proof passed: C152 Save dialog 16/16 and document state 10/10; C151 10/10; C145 49/49; C137 46/46 including an actual QMP wheel scroll on the TextArea after multi-line Undo/Redo; C150 return-target 10/10 and lifecycle 8/8. Save stress (50 alternating writes) and Save As stress (5 paths) also remain part of the established C152 proof. The C153 QEMU runner exercises real Notes menu commands and keyboard edits, not just direct test calls.

Three fresh production QEMU boots passed:

1. **save-point** — edit, Undo to clean, clean Open cancellation, Redo to dirty, dirty Open cancellation, LF/Backspace, Save, Undo/Redo, six LF edits with six Undo/Redo operations, actual TextArea wheel input, and another Save/Undo/Redo roundtrip.
2. **save-as-history** — history around Save As checkpoints, chooser cancellation, redo preservation, edit-after-Undo branch truncation, overwrite cancellation/retry, dirty Open cancellation/discard, successful Open baseline reset, and dirty Close cancel/discard.
3. **dirty-lifecycle** — Undo/Redo Open/Settings/Close guards, injected VFS Save failure with history preserved, retry Save, Undo/Redo to the new clean checkpoint, then clean Settings transition.

NativeAOT C153 composite build completed with **0 errors** and existing toolchain/project warnings. It uses the Primary4MiB heap. The proof-kernel ELF was built fresh with the C153-compatible kernel configuration earlier in the runner sequence; the final repeat reused that proof kernel while rebuilding the NativeAOT composite and proof media. No kernel ABI change was made.

| Evidence | SHA-256 / result |
| --- | --- |
| Composite ELF | `25C42030F13D20F420A02E06E33307B2419D9299AA0128F8554025FAE6A9BA47` |
| Proof-kernel ELF | `AF2B22F13F13F0F78774118D2BF4E6D4390E85080E098A8DE4953BEB5D7A29E6` |
| C153 production boot 1 serial | `5124BABFAAB48D3D1C19EABA0900498CEE772DBFD347B53550DDA8B77517710D` |
| C153 production boot 2 serial | `18337826E2377D2A99AAE857A7004DA5B97B2DF223A656339AB5828EAC1E9F52` |
| C153 production boot 3 serial | `08C5500CBDFC2555BE90D672170667DCA0D2E1030AA7BE247D7F0A0101608E5C` |
| Protected ordinary kernel, before and after | `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203` |
| Protected ramdisk, before and after | `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE` |
| C153 proof ramdisk | `55E818601A58BF5286712C5851AA69671C5FB0EC459B569286243E8CB3357B82` |

Three ordinary restoration boots passed with the restored ordinary kernel/ramdisk:

| Boot | Serial SHA-256 |
| --- | --- |
| 1 | `F32DF8BD7B96243874286B9F31A83A9B0B021C56EE986962CAD624DF14233E2C` |
| 2 | `166B4203F4E73DC118696040C4764EE0D516D4188277670777A8F53896CCDE93` |
| 3 | `2FEBCE5D3CAD16D28370792A52399755D6D79FBCCB05F1AC54179A32618A9080` |

The complete proof manifest is `out/dotnet/c153-managed-text-undo-redo/c153-proof-manifest.json`. The separate ordinary restoration summary is `out/dotnet/c153-managed-text-undo-redo/ordinary-restoration-manifest.json`; both are generated evidence, not source files. The manifest records ABI v1/table 104, history capacity and memory, production and ordinary boot hashes, and byte-for-byte protected artifact restoration.

At the end of production boot 3, immediately before the clean Notes-to-Settings transition, current and saved revision identities were equal and `Dirty=false`. The identities are opaque runtime tokens and the serial proof logs their equality through workflow behavior rather than printing the token values. The Settings Center then became the active application under the existing C150 lifecycle; the Notes history was destroyed with that Notes instance. The transition was clean, with no C153 dirty prompt. This confirms history is bounded transient control/application state, not persistence.
