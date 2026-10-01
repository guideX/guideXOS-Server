# NativeAOT C154: Bounded Managed Clipboard

## Outcome

**Outcome A — bounded managed text clipboard validated.** Managed Notes now copies, cuts, and pastes through one fixed-size managed-runtime clipboard and the existing `GuideXosTextArea` edit/history path. Copy leaves the document revision alone; Cut and Paste create ordinary C153 revisions. The final proof preserves host ABI v1/table size 104 and settings format v2.

## Audit and ownership

The repository already contains clipboard-like features, but none is a suitable service for managed Notes:

* `kernel/core/kernel_apps.cpp` has a static clipboard owned by the legacy native `NotepadApp`.
* `navigator.cpp` has a dynamically sized Navigator clipboard and can also call the Windows host clipboard.
* The older `notepad.cpp` contains clipboard TODOs.

Those paths have different owners and lifetime/transport boundaries from the managed Notes TextArea. C154 adds no native clipboard ABI and does not bridge Notes to the Windows or Navigator clipboard. Its reusable service is `GuideXosClipboard.Shared`, a resident managed-runtime singleton backed by one 256-byte array.

The managed runtime stays resident while C150 destroys and recreates application instances. The clipboard therefore survives a fresh Notes instance after Notes → Settings Center → Notes during the same boot. Production boot 1 proves Copy, a clean Settings Center transition, fresh Notes relaunch, and Paste. Clipboard storage is initialized empty with the runtime and is not written to a file or retained across a runtime/OS restart. The C154 launch regressions clear their synthetic test data before the production workflow.

The clipboard stores one text item. It accepts printable ASCII bytes and LF; CR, other control bytes, and non-ASCII bytes are rejected. The slot is initially `HasText=false`, `Length=0`. Empty Paste is an ignored no-op. Invalid or oversized `TrySetText` calls leave the previous clipboard bytes intact. Copy with no selection also leaves a prior item intact.

Clipboard data is not logged. Production operation lines record the command and byte length only.

## Editing behavior

`GuideXosTextArea` remains the only live Notes document buffer. The clipboard does not expose its internal array, and Notes does not synthesize key events to replay a paste.

| Command | C154 behavior |
| --- | --- |
| Copy | Requires a non-empty selection; copies exact bytes without changing document text, caret, selection, history, current revision, or dirty state. A missing selection is a no-op and does not clear the clipboard. |
| Cut | Requires a non-empty selection that fits. It validates and copies the selection first, then deletes it through the existing TextArea selection deletion path. The selection collapses at the deletion point. A successful Cut emits one content-changed callback and one C153 revision. |
| Paste | Empty clipboard is a no-op. Otherwise, it inserts at the caret or replaces the selected range in one `PasteText` mutation. A successful Paste collapses selection at the end of inserted text, emits one callback, and creates one C153 revision. Paste does not change the clipboard. |
| Undo / Redo | Restore the TextArea snapshot according to C153, including its saved caret/anchor state. Clipboard contents are unaffected. |

Selection bounds are normalized by `GuideXosTextArea.SelectionStart` and `SelectionEnd`, so forward and reverse selections copy the same logical bytes. LF is copied exactly and recreates line breaks when pasted.

Paste validates the full replacement before editing. If the resulting document would exceed 256 bytes or 32 lines, the entire paste is rejected. Text, caret, anchor/selection, history, revision, and dirty state remain unchanged; the clipboard remains populated. No partial insertion occurs. Exact byte and line fits are accepted.

The clipboard allocates one 256-byte array at singleton initialization. Selection copy uses a 256-byte stack buffer. Clipboard operations have no per-operation heap allocation for the payload and do not create an unbounded string or transfer object. Existing TextArea/UI rendering behavior is unchanged.

## History, save points, and file workflows

Copy does not enter history and cannot make a clean document dirty. Cut and Paste call ordinary TextArea mutation functions, so the C153 ring, revision identities, branch truncation, and saved-revision semantics remain authoritative:

* Copy leaves the current revision and saved checkpoint unchanged.
* Cut or Paste away from the saved revision makes Notes dirty.
* Undo to the saved revision makes Notes clean; Redo away from it makes Notes dirty again.
* Save and Save As mark the current revision saved without clearing history or clipboard contents.
* Open resets the document history but does not clear the clipboard. Production boot 3 copies from `/system/apps/C152/alpha.txt`, opens `/system/apps/C151/alpha.txt`, pastes, and saves a copy as `/system/apps/C151/c154copy`.
* The managed clipboard is a separate static service, so it is not owned by Open/Save choosers or modal dialogs. C151/C152 dialog and failure regressions passed. Production Open and Save As flows leave the copied item available.

The boot 3 Save As read-back verified the written document. Undo after Save As retained the new path and made the document dirty; Redo returned to the saved revision and clean state. The final revision identity values are opaque and were not printed. `Dirty=false` after Redo establishes that the current revision equals the saved revision.

## Notes menu

The existing Options popup is reused:

```text
Undo
Redo
----------------
Cut
Copy
Paste
----------------
Open
Save
Save As...
----------------
Reload
```

The previous Notes menu contained 8 entries under the popup's default limit of 8. C154 gives this menu a bounded capacity of 12 for its 12 entries; the PopupMenu default remains 8. No submenu, new focus manager, modal stack, capture stack, or ownership tree was added.

Each menu open refreshes command availability. Cut and Copy require a selection. Paste requires a non-empty clipboard and a destination that can currently fit the entire payload within both limits. Undo/Redo continue to reflect the C153 history cursor. Disabled commands are not dispatched. Managed input exposes Shift but not Control, so commands are menu-only; Ctrl+C/X/V were not added.

## Focused regression coverage

The C154 focused suite reports passing core, Cut, Paste, history/save-point, menu, and stress groups on each Notes launch:

* **Clipboard core:** initial empty state, supported encoding, exact 256-byte capacity, oversized transactional rejection, replacement of old contents, bounded copy-out, and clear.
* **Copy:** no-selection preservation, forward/reverse ordering, one-character, whole-document, multiline, and maximum-size selections; Copy preserves selection, revision, and history.
* **Cut:** no-selection no-op, deletion boundaries and direction, whole/multiline selection, oversized rejection safety, exactly one callback/revision, and Undo/Redo retaining clipboard contents.
* **Paste:** empty clipboard, empty document, beginning/middle/end, selection replacement, multiline, exact capacity, byte overflow, line overflow, repeated insertion, Undo/Redo, and unchanged state on rejected paste.
* **History and save points:** Copy is revision-free; Cut/Paste are single revisions; Undo/Redo retain clipboard data and correctly return to or leave saved revisions; Open-like hydration and Save/Save As checkpoint behavior preserve the separate slot.
* **Menu state:** selection and clipboard availability, paste-fit checks, retained Undo/Redo behavior, disabled items, selection/history neutrality, and repeated open/close.
* **Stress:** 100 deterministic mixed clipboard/edit/history operations check text, bounds, history, clipboard, and dirty-state invariants.

The repository clipboard audit, clipboard slot, C154 action integration, TextArea operations, and menu changes are confined to the managed support paths. The production traces emit operation and length, not copied content.

## Regression and NativeAOT evidence

The final C154 runner rebuilt the production NativeAOT composite with **0 errors** and existing toolchain/linker warnings. It used the Primary4MiB heap. The proof-kernel was reused from the earlier fresh C154-compatible kernel build; its hash is recorded below. Every one of the three production Notes boots reran these focused regressions:

| Phase | Current C154 rerun |
| --- | --- |
| C135 PopupMenu and host API | focused menu and host suites passed |
| C145 modal/dialog routing | 49/49 passed |
| C150 return target and managed lifetime | 10/10 and 8/8 passed; production Boot 1 also exercised clipboard across fresh Notes relaunch |
| C137 input/scrolling | 46/46 passed; production Boot 1 sent a real QMP wheel event after C154 Paste and Undo/Redo and verified TextArea scrolling |
| C151 Open File | 10/10 passed |
| C152 Save workflow | Save dialog 16/16; document state 10/10; save stress 50 operations and Save As stress 5 paths passed |
| C153 Undo/Redo | history core 26/26; TextArea 25/25; save-point 17/17; 100-mutation history and TextArea stress passed |
| C154 clipboard | core, Cut, Paste, history/save-point, menu, and 100-operation stress groups passed |

Three fresh production boots passed:

1. **Copy / Paste / Undo / Redo and C150 return:** copied one byte, Undid the temporary source edit to a clean checkpoint, opened Settings Center, closed it to a fresh Notes instance, pasted the retained byte, then Undid/Redid. Six LF edits were added and the real wheel event moved the TextArea viewport; another Undo/Redo passed.
2. **Cut / save point:** Cut one selected byte; Undo/Redo restored/deleted it; Save established a checkpoint; Undo made the document dirty and Redo returned it clean.
3. **Cross-document:** copied a 15-byte selection from C152's fixture, opened the C151 fixture through the existing dialog, pasted it, and Save As wrote and verified `/system/apps/C151/c154copy`; Undo retained that path and Redo returned to the saved revision.

The final Boot 3 state before QEMU shutdown was Notes on `/system/apps/C151/c154copy`, with a 15-byte clipboard item, Redo restored to the saved revision (`Dirty=false`), no open dialog/modal, no active popup capture, and no drag owner. The final revision tokens are runtime-opaque; equality is evidenced by the clean saved state. Notes/TextArea had focus after the final menu action. Runtime shutdown ends this clipboard lifetime.

| Evidence | SHA-256 / result |
| --- | --- |
| Composite ELF | `C46DEED19762DDB2F7F452C9AE243A5007044D6917B37ECF59B9168466262C88` |
| Proof-kernel ELF | `AF2B22F13F13F0F78774118D2BF4E6D4390E85080E098A8DE4953BEB5D7A29E6` |
| Production Boot 1 serial | `8C6A22962A75CE89456D81CCE0FDB37DD9EB7EDC21FDB92DEF89C3BC75435D4D` |
| Production Boot 2 serial | `16A1B9C4025D0895959B9800E357DD581D2764D18AA46CFC13DBAE67434A6617` |
| Production Boot 3 serial | `821E8E45D320FFD1D75940DC6D5BAD2D3E84B23425504112A018B44AF68860B6` |
| Generated C154 ramdisk | `41BECE35BFF7A642B01B4419195756FB475F0A431895EE6454E4B66351F6A26F` |
| Ordinary kernel and ESP kernel, before/after | `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203` |
| Protected ramdisk, before/after | `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE` |

The proof manifest is `out/dotnet/c154-managed-clipboard-cut-copy-paste/c154-proof-manifest.json`. The ordinary restoration manifest is `out/dotnet/c154-managed-clipboard-cut-copy-paste/ordinary-restoration-manifest.json`. Both record **3/3 PASS** and artifact restoration; they are generated evidence and are not source files.

| Ordinary restoration boot | Serial SHA-256 |
| --- | --- |
| 1 | `166B4203F4E73DC118696040C4764EE0D516D4188277670777A8F53896CCDE93` |
| 2 | `E8603D715B106AE555B8C51D1E0C0BB4B2BB57BB6A3F1ECEDE3B4B18AC4F15D9` |
| 3 | `D84FE57FD01E978B5B5C0328F94D6ED7267902DC4553282FF04BCBEC358F59B4` |

## Deferred

C154 does not add clipboard history, images or rich formats, arbitrary binary data, persistent clipboard storage, clipboard sync, drag-and-drop, OS IPC/native app sharing, Ctrl modifier transport, rectangular/multiple selection, or document-session persistence. Clipboard data is transient text owned by the resident managed runtime for one boot.
