# NativeAOT C156: Control Modifier Transport and Notes Shortcuts

## Result

C156 production validation passed in three fresh QEMU boots. A real PS/2 Control chord now reaches Managed Notes as modifier-aware `KeyDown`, uses the existing Notes action path, and does not insert its printable letter. ABI v1 and the 104-byte host table are unchanged.

The standalone C129 focused suite emitted `31/31`, and production Tab/Shift+Tab passed in the C156 QEMU proof. A standalone C128 rerun was attempted but did not reach the managed panel lifecycle test marker; its `46/46` result is not claimed here.

## Keyboard path audit

The PS/2 decoder already tracked Control, but only in one combined `s_ctrlDown` flag. The decoder recognized the Set 2 Control make/break code `0x14`, yet did not keep left and right Control independently. The desktop dispatcher sent printable keys to `KeyChar`, and the NativeAOT input payload encoded Shift only. That native-to-managed input boundary was where Control was lost.

Shift remains independently tracked for left and right keys and is still carried on key events. Alt is tracked for the native Alt+F4 path but is not sent as a managed modifier. Caps Lock is a native toggle used for letter translation and is also not sent as a managed modifier. C156 changes only Control transport.

## Native and managed representation

The PS/2 layer now tracks left Control (`0x14`) and right Control (`E0 0x14`) independently. Ordinary input sees their logical OR. When a key make is queued, the native layer snapshots Control, each Control side, and Shift with that key. Later key-up timing therefore cannot change the modifier state attached to an earlier key-down. Native state clears on the matching physical release; there is no managed modifier latch to survive a surface or application transition.

Printable keys pressed while Control is down are routed through `KeyDown`. The desktop does not emit a `KeyChar` for the same press, and the NativeAOT `KeyChar` callback has a defensive Control guard. Managed Notes also consumes a split `KeyChar` carrying Control if another path supplies one.

The existing 24-bit input payload has spare key-only capacity:

| Bits | Meaning |
| --- | --- |
| 23 | Shift |
| 22 | Control |
| 0–21 | Key value |

The high input-kind flags are unchanged. Pointer coordinates retain their existing two 12-bit fields; Shift and Control decode only for key events, and Control is limited to `KeyDown` and `KeyChar`. Pointer move, pointer down/up, and wheel events do not acquire modifier state from these bits. This preserves ABI version 1 and the 104-byte host table.

The production proof exercised both release orders:

```text
Ctrl down, Z down, Z up, Ctrl up
Ctrl down, Z down, Ctrl up, Z up
```

Both ended with clean Control state. The proof also used left and right Control independently and combined Control with Shift for Save As. The native queue snapshots state per key-down; the current ABI does not add managed key-up events.

## Shortcut routing

Managed Notes intercepts Control-modified input before ordinary text editing and maps it to the same action IDs used by the menu:

| Chord | Notes action |
| --- | --- |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+C | Copy |
| Ctrl+X | Cut |
| Ctrl+V | Paste |
| Ctrl+S | Save, or Save As for an untitled document |
| Ctrl+O | Open |
| Ctrl+Shift+S | Save As |

Undo, Redo, Copy, Cut, and Paste check the existing TextArea or shared clipboard availability before dispatch. Save and Open enter the existing C152/C151 workflows. A disabled command is a no-op. The shortcut router does not synthesize menu clicks or create a second action implementation.

Modal dialogs, the file picker, transient input capture, and the Notes popup retain input priority. The shortcut handler returns control to those owners while they are active, so a shortcut cannot operate on the parent document behind a dialog. An Open request on a dirty document uses the existing Save/Discard/Cancel prompt.

Unrecognized Control chords have no Notes command and their printable character is consumed. Ctrl+Q was used to verify this. Ctrl+Tab remains on the C129 Tab path and produces no printable Tab character. Ctrl+Space produces no TextArea space insertion or synthetic `KeyChar`. The popup continues to own its keyboard navigation while open.

The platform's repeated key-downs are honored. Repeated Undo/Redo and clipboard commands repeat through the ordinary action path. Save and Open enter a modal workflow on the first event, after which modal ownership prevents repeated parent shortcuts from opening another dialog.

## Validation

Managed focused checks passed:

- Modifier decode: `10/10`, including no modifiers, Shift, Control, both modifiers, later unmodified input, Control on a split `KeyChar`, and pointer isolation.
- Shortcut routing: `13/13`, including all seven supported commands, unsupported Ctrl+Q, unsupported key kinds, Tab, Space, and platform repeat policy.
- C153 history: `26/26`; TextArea: `25/25`; save points: `17/17`.
- C154 clipboard tests passed. C156 additionally completed 25 Copy/Paste/Undo rounds, each returning to the saved revision.
- C152 Save dialog: `16/16`; document state: `10/10`; C151 Open: `10/10`.
- C145 modal tests: `49/49`; C135 popup and host startup regression checks passed.
- C150 return-target and lifecycle checks: `10/10` and `8/8`.
- C155 session core/capture/TextArea cases: `31/15/11/4`; nine VFS restore/fallback cases passed. The prior C155 proof covers 25 production clean-return cycles; C156 rechecked a fresh Notes return with the shared clipboard.
- C129 focused Shift/Tab suite: `31/31`. Production Tab and Shift+Tab markers passed during the C156 QEMU proof, including the modal picker case. A separate legacy C129 runner attempt timed out before its keyboard marker; the C156 physical-input proof is the production Shift/Tab evidence recorded below.
- C128 full lifecycle `46/46`: not verified in this run. The standalone attempt reached the catalog-valid marker but did not reach `C128-PANEL-LIFECYCLE-TESTS`; the proof was stopped and protected boot files were restored.

The C156 NativeAOT composite built with zero errors. The production proof used a 4 MiB managed heap. Three fresh production boots passed:

| Boot | Scenario | Serial SHA-256 |
| --- | --- | --- |
| 1 | Save points, Undo/Redo stress, clipboard stress, clean Settings return | `9990A1B9C0CE7478681ECF68D72C655B6A3B8FFA6F84A3B15ECE85D8FAA6C5AF` |
| 2 | Dirty Save, exact read-back, fresh Notes return, clipboard Paste/Undo | `CBCD7CF722BEE86BB2ED133A1AF24FCC552DF73A325BA04F2DFF244F5DC30BE2` |
| 3 | Popup and Save As priority, dirty Open prompt, Discard return | `60FB3A204F4F1615C5028932A404E08CFB127A40D607AE49FA4988D341563CB2` |

The first production boot ran 25 Undo/Redo pairs and 25 Copy/Paste/Undo rounds. With four physical transitions per chord, those bounded loops alone exercised more than 100 key transitions. Production traces show left and right Control make/break state, Control+Shift, native-to-managed modifier bits, command dispatch, and no printable leakage.

After the proof, the canonical kernel and protected ramdisk were restored byte-for-byte. Three ordinary boots passed, with the ordinary kernel, ESP kernel, and ramdisk hashes checked in the proof manifest. The settings format remains v2. The generated manifests are:

- `out/dotnet/c156-control-modifier-shortcuts/c156-proof-manifest.json`
- `out/dotnet/c156-control-modifier-shortcuts/c156-ordinary-restoration-manifest.json`

Key artifacts:

| Artifact | SHA-256 |
| --- | --- |
| NativeAOT composite ELF | `5CFBDBFA1364274713F2F0E282DA2F90D0F823B6860507AA900E9E74AF004B65` |
| C156 proof kernel | `A78E1E3D9AD31D01C9BE29C5C28E3CA559B81809FF3B1897D34DCACDB29C60BC` |
| Restored canonical and ESP kernel | `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203` |
| Restored protected ramdisk | `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE` |

The final returned Notes view is fresh from `/system/apps/C155/alpha.txt`, clean, and has fresh Undo/Redo history. The one-shot return target is consumed. Clipboard ownership remains `GuideXosClipboard.Shared` for the current boot. Control and Shift are clear after the proof; modal, popup capture, and drag state do not persist across the return.

## Scope limits

C156 adds no general shortcut registry, configurable bindings, second focus system, modal stack, capture stack, or new ownership tree. It does not transport Alt or Caps Lock as managed modifiers. Clipboard and Notes session state remain RAM-only for the current boot, and undo history is intentionally fresh after a C155 return.
