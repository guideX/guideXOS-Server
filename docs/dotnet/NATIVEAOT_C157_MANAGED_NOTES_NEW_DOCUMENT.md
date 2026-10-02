# NativeAOT C157 Managed Notes New Document

## Result

Outcome A: Managed Notes supports one New document workflow inside its existing
Notes instance. Ctrl+N and the menu item share the same action. Dirty content
uses C152's Save, Discard, or Cancel decision, and untitled Save uses the
existing Save As flow.

## Untitled audit before C157

With C152 enabled and no restored document, launch begins by trying the normal
Notes path `/system/apps/NOTES.TXT`. A not-found result clears the in-memory
path and calls `GuideXosNotesDocumentState.InitializeUntitled` on the existing
TextArea. The document is empty and clean, with a saved revision identity and
no Undo or Redo history. The status reads `Untitled note`; where the path label
is present it reads `Path: Untitled`. The window title remains `Managed Notes`.
The Save As chooser already uses `untitled.txt` under `/system/apps` when there
is no current path.

C152's state tests represented untitled documents with `InitializeUntitled`,
which resets the TextArea and records its current revision as the clean saved
revision. C155 can capture a clean pathless Notes state as a blank session with
caret, anchor, and viewport positions, then restore a fresh untitled baseline
without inventing a VFS path.

## New behavior

The single authoritative New baseline is:

| State | Value after New |
| --- | --- |
| Current path | None; empty in the document state |
| Text | Empty |
| Dirty | False |
| Saved revision | Current blank baseline revision |
| Undo / Redo | Both unavailable |
| Caret / selection | Caret 0; collapsed at 0 |
| Viewport | First visible line 0 |
| Status / path display | `Untitled note` / `Path: Untitled` |
| Clipboard | Unchanged |
| VFS and settings | No write; settings runtime unchanged |
| Notes lifetime | Same generation and surface |

`InitializeUntitled` is the one document replacement operation. New does not
use Reload, does not create an undoable TextArea edit, and does not change the
resident `GuideXosClipboard.Shared` object. The blank revision itself is the
clean checkpoint, so Paste followed by Undo returns to a clean blank document
even before a file exists.

Clean named or untitled New resets immediately without a prompt. Dirty named
or untitled New enters the existing C152 one-modal decision flow:

| Decision | Result |
| --- | --- |
| Save | Save the named path or open Save As for an untitled document. New occurs only after verified write success. |
| Discard | Create the blank untitled baseline without writing the old document. |
| Cancel | Preserve the current text, path, history, and dirty state. |

If Save or Save As fails, C152 clears the pending New operation, displays its
existing error dialog, and keeps the current document, path, history, and dirty
state. Canceling Save As has the same preservation behavior. The user can
retry New after the failure. Save As overwrite confirmation continues to use
C152's existing one-modal sequencing.

Saving after New uses the normal Save As chooser and `untitled.txt` default.
Successful Save As assigns a real VFS path and marks the current revision
saved while retaining history. Undo and Redo therefore move away from and back
to that save point correctly. Open after clean New opens the existing C151
chooser directly. Existing Reload behavior remains path based; New is a
separate action.

## Menu and shortcut

The current C154 Notes menu used 12 entries. C157 adds New as the first entry
and uses this 13-entry order:

```text
New, Open, Save, Save As..., separator, Undo, Redo, separator,
Cut, Copy, Paste, separator, Reload
```

`GuideXosPopupMenu.MaximumSupportedItemCount` rises from 12 to 13 while
`DefaultMaximumItemCount` stays 8. The Notes popup remains fixed-capacity; no
submenu or general popup default change was introduced. The Settings control
remains in its existing location.

The menu and Ctrl+N both dispatch action 25 to `RequestNewDocument`. C156's
native Control transport remains unchanged: Control is payload bit 22, Shift
is bit 23, ABI version is 1, and the host table is 104 bytes. Both left and
right Control use the same route. KeyChar events carrying Control are consumed
before TextArea dispatch, so Ctrl+N does not insert `n`. Popup and modal
ownership retain priority and block Ctrl+N from opening another workflow.

## State and integration coverage

`GuideXosManagedNotesNewDocumentC157Tests` covers initial untitled state,
named-to-New reset, repeated clean New, caret/selection/viewport reset,
save-point dirty state, Save As history retention, Undo to the untitled clean
checkpoint, Redo to dirty, named and untitled Save/Discard/Cancel state,
clipboard-empty and clipboard-populated New, Copy and Cut followed by New,
Paste/Undo/Redo, pathless C155 capture/consume/restore, Ctrl+N action mapping,
KeyChar suppression, and 25 bounded state stress cycles. It restores the
runtime clipboard snapshot after its focused tests.

Production QEMU flows exercise the real menu, shortcut transport, modal
decision, Save As, VFS failure injection, and C150/C155 lifecycle:

1. Clean named document -> Ctrl+N -> blank baseline -> type -> Ctrl+S / Save As
   -> Undo dirty -> Redo clean.
2. Dirty named Save, Cancel, Discard, and injected Save failure; dirty untitled
   Save As Cancel and failure, Cancel, and Discard.
3. Copy -> New -> Paste -> Undo; clean untitled Settings return; dirty
   untitled Settings Cancel, Save As named return, and Discard to blank return.
4. Twenty-five physical New cycles mixing clean New, edit/Undo/New, dirty
   Discard/New, and clipboard Paste/Undo.
5. Popup and modal ownership checks, including Ctrl+N while another owner is
   active, plus real left and right Control input.

## Regression and evidence

The production runner is
`scripts/dotnet/run-c157-managed-notes-new-document.ps1`. It publishes the
NativeAOT composite with C155, C156, and C157 enabled, creates disposable proof
media, runs three fresh QEMU production boots, restores the canonical kernel
and protected ramdisk byte-for-byte, then runs three ordinary restoration
boots. Hashes and serial evidence are written under
`out/dotnet/c157-managed-notes-new-document/`; the checked-in source tree does
not contain generated proof media.

| Area | Required result / classification |
| --- | --- |
| C157 New workflow | Focused and physical proof markers pass |
| C156 modifiers and shortcuts | Modifier decode 10/10; routing 15/15, including Ctrl+N |
| C153 history | History 26/26; TextArea 25/25; save points 17/17 |
| C154 clipboard | Clipboard tests pass; clipboard survives New |
| C152 Save | Save dialog 16/16; document state 10/10 |
| C151 Open | 10/10 |
| C155 session | Blank pathless, named Save, and Discard returns; Cancel blocks transition |
| C150 lifecycle | Return target 10/10; lifecycle 8/8 |
| C145 modal dialog | 49/49 |
| C135 popup menu | Focused menu and host tests pass; 13-entry Notes menu |
| C129 Tab transport | 31/31 focused; production Tab and Shift+Tab remain covered |
| C128 panel lifecycle | 46/46 remains unverified; no current pass is claimed |
| ABI and settings | ABI v1 / 104-byte table; settings v2 |

The proof manifest records the NativeAOT composite and kernel SHA-256 values,
all three C157 serial hashes, all three ordinary serial hashes, protected
artifact restoration, final Notes/session state, and stress count. Its expected
paths are `c157-proof-manifest.json` and
`c157-ordinary-restoration-manifest.json` in the evidence directory.

## Scope

C157 remains one Notes document at a time. It does not add multiple documents,
tabs, recent files, templates, autosave, crash recovery, persistent untitled
draft recovery, filename numbering, a document manager, print, rich text,
navigation stacks, or multi-document session serialization.
