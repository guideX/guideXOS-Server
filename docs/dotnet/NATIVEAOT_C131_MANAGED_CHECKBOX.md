# NativeAOT C131 — Reusable Managed CheckBox

## Outcome

C131 is Outcome A: the reusable managed CheckBox works through the existing
control host, pointer, keyboard, programmatic, Panel, lifecycle, Managed Notes,
NativeAOT, and QEMU paths without expanding the control architecture.

Starting HEAD was `bc04643fc57045169c34acaac1dfce013631a3c0`, on
`v1.1_DOTNET_SUPPORT`, with a clean worktree and `0/0` ahead/behind relative to
`origin/v1.1_DOTNET_SUPPORT`.

## Architecture and API

`GuideXosCheckBox` is a sealed, bounded managed control in the existing
`GuideXos` namespace. It uses the same fixed-capacity `GuideXosControlHost`,
explicit control IDs, bounds, visibility, enabled state, focus state,
pointer-down routing, split Space `KeyDown`/`KeyChar` transport, rendering, and
Panel membership hooks used by the other managed controls.

The public state surface is:

- `Text`/`Label`: bounded printable label text.
- `Checked`: readable and writable two-state value.
- `Enabled`: readable and writable activation/focus eligibility.
- `Visible`: readable and writable local visibility.
- `IsFocused`, `EffectiveVisible`, `ParentPanel`, and bounded rejection count.
- `Changed`: `Action<bool>` invoked once after a real checked-value transition.

`SetChecked`, `Checked = value`, `Toggle`, pointer activation, and Space
activation share the same state transition path. Assigning the current value is
a no-op. A callback that assigns another value updates the value but does not
recursively re-enter the callback; the dispatch guard makes this bounded and
deterministic. User pointer and Space toggles therefore produce exactly one
callback per real transition.

Pointer-down on the box or label bounds focuses and toggles once. Disabled,
hidden, removed, or ineligible controls are rejected by the existing host and
modal rules. Space commits only on `KeyChar`; `KeyDown` alone does not toggle.
The host owns pending-gesture identity and cancels stale Space characters when
visibility, enabled state, focus, Panel membership, or modal scope changes.
Tab and Shift+Tab remain host traversal operations.

Rendering is text-backed and deterministic: `[ ]` unchecked, `[x]` checked,
`>[ ]`/`>[x]` focused, and `x[ ]`/`x[x]` disabled, followed by the bounded
label. No image asset or parallel compositor was added.

The control remains compatible with the existing single-level, non-owning
`GuideXosPanel`. Panel membership changes local coordinates and effective
visibility; it does not transfer ownership or create a child tree.

## Managed Notes integration

Managed Notes adds one real application setting: `Show status`. It is registered
as control ID 7 and its `Changed` callback controls only the existing status-line
presentation. This avoids adding a text-layout engine for word wrapping.

The Notes registration count changes deterministically from seven in C128 to
eight in C131:

`Open, Save, Save As, Show Path, Full Path, File Name, Show Status, Document`.

Close/relaunch clears and rebuilds the host registration array; C131 verifies
that relaunch returns to eight registrations with no focus theft or duplicate
entries.

## Regression coverage

The focused C131 suites are:

- `GuideXosCheckBoxC131Tests`: 34/34 cases.
- `GuideXosCheckBoxC131HostTests`: 23/23 cases.

They cover initial states, programmatic assignment and same-value assignment,
bounded callback reentrancy, pointer and label activation, Space transport,
disabled/hidden/non-member behavior, rendering states, Panel membership,
forward and reverse traversal, all interrupted-Space transitions, stale
characters, modal isolation, and close/relaunch accounting.

Preserved prior results:

- C127 Panel focused suite: 60/60; Panel remains single-level and non-owning.
- C128 lifecycle suite: 46/46.
- C129 production Shift/Tab transport suite: 31/31.
- C130: obsolete direct-managed C127 reverse helper remains explicitly
  `SKIP`; production C129 Shift/Tab is the replacement.
- Existing managed control-host, Button, Panel, TextInput, TextArea, ListBox,
  and NativeAOT regression markers remain PASS in the retained evidence.

## NativeAOT and QEMU evidence

The C131 NativeAOT proof exercises registration, initial state, pointer
activation, focus, Space activation, forward Tab, production reverse Shift+Tab,
disabled rejection, lifecycle cancellation, Notes behavior, modal routing, and
close/relaunch through the production paths.

Evidence manifest:

`out/dotnet/c131-managed-checkbox/c131.manifest.json`

The three fresh QEMU boots all passed. The composite managed image hash and
proof-kernel hash were:

- composite/combined NativeAOT image (`HostLogProof.elf`):
  `E6B585D8328920216369575D82FAEE7DE889047184E56EA5E7C08E717158A7AD`
- proof kernel (`kernel/build/amd64/bin/kernel.elf` during C131 boots):
  `9673FD65D6B0A458246447794E12D3C62C05D71943882725FD169679F0E4AFEC`

Serial hashes:

- boot 1: `6AEA07F248A18E082A6347B0EBCEB555A35E40290DDF9414D88863BF052AD1E4`
- boot 2: `485863BD3BCC6672653243B0EEB529F16B50E00CE6BCC73B917A0B79BD93B2A2`
- boot 3: `CE747560F8202FF81F03ACB2F611516CCDF08038B416CCD5126F233F81CB83D6`

Each boot reported `[C131-RESULT] outcome=PASS`, registration 8, focused
checkbox/host PASS, and all C131 interaction markers PASS.

## Ordinary-kernel restoration

After proof testing, the canonical ordinary kernel was rebuilt without C131
proof defines and copied to `ESP/kernel.elf`. Both locations matched:

`1EA9C48EC631EAECA2EA71BBAF7CDA711600697E584EBCA3557A672D4AD0BADE`

The ordinary validator passed three fresh boots. Manifest:

`out/dotnet/c131-ordinary-boot/run-20260920-214730409-f2216e71/ordinary-boot.manifest.json`

Ordinary serial hashes:

- boot 1: `24B9C4E0F9133BA0097B2221ED20C75A5FE1503D40FA1270D3ACE3517251651C`
- boot 2: `99AA4BA18823F602E4B224BDBE07F3915B78760A9FF0A484DA6CBB86BDC8B922`
- boot 3: `287CCE83205B8D034E7ACBDC2C94392707D544FA24964D50BEF82B7344FBC9BE`

All ordinary boots reached the kernel main loop and Navigator PASS. No proof
kernel remains installed as the ordinary kernel.

## Preserved architecture and limitations

C131 preserves ABI v1, ABI table size 104, the NativeAOT runtime/GC/VFS/App
Model, production input transport, independent left/right Shift tracking,
key-down-only Tab semantics, and the existing C128 lifecycle model. No ABI
change or synthetic Tab `KeyChar` was added.

C131 intentionally does not provide tri-state/indeterminate mode, nested
control ownership, automatic layout, theming, animation, data binding, custom
checkmark images, or arbitrary child content.
