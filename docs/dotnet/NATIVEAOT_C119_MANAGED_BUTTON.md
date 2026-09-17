# C119: Reusable Managed Button / Command Control

C119 adds a bounded `GuideXosButton` control to the managed HostLogProof
sample and converts Managed Notes' Open, Save, and Save As commands to use
managed buttons. The existing native picker buttons and the native Reload
button remain app-owned paths outside this conversion.

## Contract

- The application owns the command. `GuideXosButton` owns only its label,
  bounds, enabled state, focus state, input results, and text rendering.
- Bounds are explicit `x`, `y`, `width`, and `height` values. Width is bounded
  to 32..512, height to 18..128, and the right/bottom edges are exclusive.
  Coordinates stay within the 4095 coordinate ceiling.
- Labels use fixed managed storage: printable ASCII, non-empty, with a default
  limit of 32 code units and a hard supported maximum of 48.
- Pointer-down is activation. Pointer-up is intentionally not required because
  it is not forwarded by the current managed input route.
- Enter activates from `KeyDown` when the button is focused. Space is ignored
  by `KeyDown` and activates from `KeyChar` only.
- Rendering is text-only: `[ Label ]`, `>[ Label ]` while focused, and
  `x[ Label ]` while disabled. The control does not add a fill rectangle; the
  current managed frame primitive would reset prior rectangles.
- The control has no pressed/armed lifecycle because pointer-up is unavailable;
  each pointer-down is a complete command gesture. State and label operations
  use fixed per-instance storage and bounded loops; activation does not allocate
  a button collection.

The control is deliberately not an ABI or runtime feature. C119 leaves the
GuideXos Host ABI v1/table 104, managed runtime startup/resident lifecycle,
GC, and VFS contracts unchanged.

## Managed Notes wiring

Managed Notes creates three persistent controls at these surface-local bounds:

| Command | Bounds | Existing app action |
| --- | --- | --- |
| Open | `(20,220,90,28)` | picker open, action 20 |
| Save | `(120,220,90,28)` | direct current-document save, action 22 |
| Save As | `(220,220,100,28)` | picker save, action 21 |

The app routes activation to the existing action handlers, blurs the text area
when a button activates, and blurs all buttons when the text area or picker
regains focus. Disabled Save is tested both by pointer-down and by the
KeyDown/KeyChar path; it cannot open a picker or write a file.

## Space and focus proof

The C119 proof sends one physical Space representation as `KeyDown(' ')`
followed by `KeyChar(' ')` while Save is focused. The first event is consumed
and ignored by the button; the second is the sole activation. The serial proof
requires exactly one managed marker:

`[C119-MANAGED-OUTPUT] C119-SPACE button=Save activation=PASS count=1`

The proof separately focuses the C117 text area and sends `KeyChar(' ')`. It
checks that the document gains a trailing space, the status remains
`Opened from picker`, and no button activation occurs. This demonstrates both
the KeyDown/KeyChar exactly-once rule and focus isolation. Enter behavior is
covered by the button control's focused keyboard cases; the integrated boot
sequence uses one pointer Save and one Space Save to keep the resident proof
bounded.

## Non-goals and limitations

C119 does not add automatic Tab traversal, accelerator keys, default/cancel
dialog semantics, icons, arbitrary child content, command binding, toggle or
check state, radio groups, repeat-on-hold, true pointer-up pressed state,
accessibility metadata, animations, or themes. Picker filename/list input and
picker action widgets retain their existing ownership.

## Verification

Run the complete three-boot evidence harness from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dotnet/run-c119-managed-button.ps1
```

Each boot builds/stages an isolated ESP, launches the C119 native proof, and
requires PASS markers for button unit cases, disabled/re-enabled behavior,
managed Open/Save/Save As, VFS results, Space exactly-once, text-area Space
isolation, and C116-C118 regressions. The full C118 save-cancel,
overwrite-confirm, and reopen flow remains covered by the prior three-boot
`run-c118-managed-list-box.ps1` evidence; the C119 proof keeps its managed
button path bounded to the requested first Save As verification. Evidence is
written under `out/dotnet/c011ec119-managed-button`.
