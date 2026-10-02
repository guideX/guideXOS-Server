# NativeAOT C155: Managed Notes Session Restoration

## Outcome

**Outcome A — bounded Notes position restoration across Settings Center.** When Notes returns from Settings Center, a fresh Notes instance reloads the current named document from VFS and restores its caret, selection anchor, and viewport. The feature adds one RAM-only semantic slot, preserves the C150 return-target lifecycle, and leaves the C154 clipboard independent.

The session does not retain document text, undo history, a Notes object, or persistent settings. It restores the last saved file state. Dirty text is preserved only when the user resolves the Settings transition by saving it; choosing Discard restores the VFS baseline.

## Bounded slot and ownership

`GuideXosNotesReturnSessionC155.Shared` owns one fixed slot in the resident managed runtime. Its payload is:

| Field | Bytes | Meaning |
| --- | ---: | --- |
| Tag | 4 | Validity marker for this slot format |
| Generation | 4 | Nonzero sequence advanced on each successful arm |
| Caret index | 4 | Logical insertion point |
| Selection anchor | 4 | Logical opposite endpoint of the selection |
| First visible line | 4 | Requested TextArea viewport |
| Flags | 1 | Whether a named path is present |
| Path buffer | 97 | Up to 96 UTF-8 bytes plus a NUL terminator |
| **Total** | **118** | Fixed storage; no document text |

The path must be canonical and pass the same bounded path validation used by C151. The slot has capacity one. A second arm is rejected while it is pending. A mismatched consumer leaves the managed slot untouched; a matching Notes return copies its bounded metadata and clears the slot before it attempts VFS I/O. Clearing preserves the generation counter. The C150 native return target is paired with the canonical Notes identity; a failed or mismatched target path clears the session instead of carrying it into another application.

The slot is RAM-only. Direct Notes launches clear stale pending state. It is consumed for one returned Notes launch and is not encoded in Settings v2, written to disk, or retained across reboot.

## Settings transition rules

Session capture occurs only after the Settings transition has a resolved outcome:

| Resolved outcome | Capture rule |
| --- | --- |
| Clean | Capture only if the saved revision is present, reachable, and the document is clean. |
| Save | Capture only after Save or Save As succeeds, read-back verification succeeds, and the document is clean. |
| Discard | Capture the current path and positions only when the document was dirty; the returned instance reloads the saved VFS version. |
| Cancel or unresolved prompt | Do not arm a session. The Settings transition is blocked or deferred. |
| Failed Save or canceled Save As | Do not arm a session. Notes keeps its existing document/path state and the Settings transition does not proceed. |

The C152 Settings prompt remains the authority for dirty resolution. Session metadata is armed only when that workflow accepts the transition, and the C150 return-target arm is paired to it. The native target and managed slot are both cleared if Settings cannot launch or Notes cannot relaunch.

## Fresh Notes restoration

The returned Notes launch begins with an untitled clean baseline. For a named session, it reads the path through the existing C151 loader: stat the file, require a regular file, enforce the 256-byte editor limit, read into bounded stack storage, validate the text, then hydrate through C152. Successful hydration establishes a new saved revision. The new TextArea has no prior Undo or Redo history and is clean.

The caret and selection anchor clamp to the reloaded text length. The saved viewport is first bounded to the TextArea's valid range; the existing TextArea rule then moves the viewport as needed to reveal the restored caret. For example, the clean-return trace restores caret 84 and anchor 87 exactly; the viewport changes from line 7 to line 5 so the caret is visible. This position adjustment does not change document text or revision state.

If the slot is invalid or the VFS load fails, the slot is already consumed and Notes falls back to a usable blank untitled document with a fresh clean baseline. The startup proof covers a missing file, a 257-byte file, an invalid path, and a directory supplied where a regular document is required. The directory case exercises the non-regular/unreadable-file failure mapping. No fallback preserves stale text or history.

The C154 clipboard remains owned by `GuideXosClipboard.Shared`; session capture, consumption, clearing, and VFS reload do not touch it. Production Boot 1 copies text, returns through Settings to a fresh Notes instance, pastes the retained item, and Undoes back to the restored save point.

## Focused coverage

The C155 in-process checks cover 31 slot, capture-decision, and TextArea cases: 16 bounded-slot/one-shot cases, 11 resolved-state capture cases, and 4 position-restore cases. They cover the single pending slot, target mismatch, generation, matching consume, path validation, clear behavior, clean/save/discard eligibility, cancel and unresolved rejection, clamping, and caret visibility.

Nine VFS restoration cases cover five successful reloads (multi-line named file, empty file, maximum 256-byte file, caret/anchor clamp, and another named file) plus four blank fallbacks (missing, oversized, invalid path, and a directory/non-regular file). Each successful reload must establish a fresh clean save point with no Undo/Redo. The fallback cases must leave an empty untitled document with no history.

## Regression and production evidence

The NativeAOT managed composite was rebuilt with the C155 feature flag and 0 build errors. The proof reused the fresh C155-compatible kernel artifact from the preceding native build. Existing NativeAOT, MSVC linker, and PDB warnings remain. Host ABI v1/table size 104 and Settings format v2 are unchanged.

The initial Notes launch passed the established regression markers: C135 popup/host, C145 dialogs 49/49, C150 return-target 10/10 and lifecycle 8/8, C137 46/46, C151 10/10, C152 Save dialog 16/16 and document state 10/10, C153 history 26/26, TextArea 25/25 and save points 17/17, and C154 clipboard. C152 Save stress covered 50 alternating writes, Save As stress covered five paths, and C152 max/empty-file checks passed.

Three fresh production QEMU boots passed:

1. **Clean named return:** Copy, clean Settings transition, fresh Notes/VFS reload, Paste, and Undo. Then 25 additional clean Notes → Settings Center → Notes cycles passed. The generation advanced from 1 through 26; every return paired and consumed one session and restored the named path, caret, and selection anchor.
2. **Dirty Save return:** Edit the named document, choose Save in the dirty Settings flow, verify exact read-back, return to a fresh Notes instance, Paste the clipboard item, and Undo to the new saved baseline.
3. **Dirty Discard return:** Edit the named document, choose Discard, return to a fresh Notes instance, and reload the saved VFS baseline with clean history.

The runner then performed three ordinary restoration boots. Before/after SHA-256 values match for the protected canonical kernel, ESP kernel, and ramdisk. The manifests and serial logs are generated proof evidence and are not source files.

| Evidence | SHA-256 / result |
| --- | --- |
| NativeAOT composite ELF | `1571AC4F52560033673953FBFA444D0A24B00B25B5C2BB31CC3F753DD3EE828A` |
| Reused proof-kernel ELF | `1E6A3C6026719210A7FD1BC16327CDEA77E048783D1879D10A5EB393D8C1D0B2` |
| Production Boot 1 serial | `5C0D6813CF4DF01700DB7B9D577E0B63A086A4315010513361571761C9C3581F` |
| Production Boot 2 serial | `DAD2818DC09F4127DD2AEF2AE7357EEE892AC4EFE1B9B41EEBC2C1002788939A` |
| Production Boot 3 serial | `E4504921A3F61159553ADAE92C0072C896894A13D5F3B138CB4D06E89DC851A1` |
| Generated C155 ramdisk | `D13F7DBCEBD1FDF95BEFABF41EE03C46C21EDDD0E928E307B168EA3E55267D42` |
| Ordinary kernel and ESP kernel, before and after | `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203` |
| Protected ramdisk, before and after | `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE` |

| Ordinary restoration boot | Serial SHA-256 |
| --- | --- |
| 1 | `A6F21B26C60C8B13FE998E32C8B7779797073789CA33DE5085E6E17895C5A07B` |
| 2 | `09E4A4DEDC6860138CAAE244EFE08D77FC88074B72E8F768B1DB481EE1CA939D` |
| 3 | `74A46F57B238196B23990EA7BF45E71C64D0AD0147C62BDCDA7CDB766E9340ED` |

The proof manifest is `out/dotnet/c155-managed-notes-session-restoration/c155-proof-manifest.json`. The ordinary restoration summary is `out/dotnet/c155-managed-notes-session-restoration/c155-ordinary-restoration-manifest.json`.

## Deferred

C155 does not restore Undo/Redo history, unsaved buffers, multiple documents, a navigation stack, autosave state, crash recovery, or a session across reboot. It does not add persistent session serialization. It restores one saved Notes path and logical view position for one Settings return within the resident managed runtime.
