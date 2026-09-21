# Phase C132 — Reusable Managed RadioButton and Option Groups

## Outcome

C132 implements reusable mutually-exclusive managed choices through ordinary
independent controls. `GuideXosRadioButton` owns its visual/input state and an
optional `GuideXosRadioGroup` coordinates selection. A group is not a Panel,
does not own children, and is not a second control registry.

The public control surface is bounded and NativeAOT-friendly:

- `Text` and the existing `Label` alias;
- `Checked` and the compatibility `Selected` alias;
- `Enabled` and `Visible`;
- `Group` and bounded `Changed(Action<bool>)` callbacks.

## Group representation and algorithm

Group identity is explicit: applications create a `GuideXosRadioGroup` and
register each member with it. The coordinator is a fixed array with a maximum
of four members. Registration order is deterministic and is used only for
optional arrow navigation. Overflow, duplicate registration, and registration
of a button already attached to another group are rejected.

When a member is selected, the group clears its selected index, deselects the
previous member and any inconsistent checked members, then sets the new index
and selects the new member. The completed operation has zero or one selected
member. Selecting the selected member is idempotent. An ungrouped button may
be checked independently; a group with no selection is valid.

`Checked = true` is a programmatic group selection and replaces the current
member. `Checked = false` clears the current member and leaves the group with
no selection; it does not select a fallback. Disabled or hidden lifecycle
changes also preserve the current checked value and do not automatically
select another member.

If multiple members are constructed checked before registration, the last
registered checked member wins. Registration applies the same normal selection
transaction, so the result is deterministic and callbacks are not duplicated.

## Callbacks and bounded re-selection

Each real `Checked` transition invokes `Changed` once. Replacing A with B is
ordered as:

1. the group publishes no selected index;
2. A becomes unchecked and receives its callback;
3. B becomes the selected member and receives its callback.

Thus A's callback can observe that neither member is checked, while B's
callback observes B as the selected member. A callback may request another
member; the group queues that request and processes at most eight selection
transactions. The queue is deliberately bounded and is not a general event
dispatcher.

## Pointer, keyboard, and focus behavior

The button's entire bounded rectangle, including its text, is the hit area.
Pointer selection requires enabled and effective-visible state. Clicking an
already-selected member leaves it selected. Space is committed only by the
existing `KeyChar` path; Space `KeyDown` is ignored, so one physical press
cannot select twice.

RadioButtons are ordinary host entries. They participate in Tab and
Shift+Tab traversal, skip disabled or hidden members, and remain independent
focusable controls. C132 also implements bounded group navigation: Left/Up
select the previous eligible member and Right/Down select the next eligible
member, with registration-order wrapping. Focus and selection move together.
For an already-selected member, Space remains idempotent and the host does
not retain a delayed selection transaction; unchecked members still use the
pending-gesture path so lifecycle cancellation protects their later Space
character.

All host lifecycle transitions cancel pending Space gestures: visibility,
enabled state, focus loss, Panel membership, unregister, and modal entry/exit.
The stale `KeyChar` is consumed without selecting a recovered fallback.

## Panel and lifecycle integration

The existing Panel remains single-level and non-owning. A RadioButton may be a
Panel member, but its group identity is independent of Panel membership.
Panel visibility and child membership affect effective visibility and gesture
cancellation only. `GuideXosControlHost.TryUnregister` removes the button
from its group, compacts the bounded host table, and repairs focus. Host reset
unregisters all radio members, so close/relaunch cannot retain stale group
membership.

## Managed Notes integration

Managed Notes already had a legitimate mutually-exclusive Path Display choice:
Full path versus File name. C132 upgrades those existing two independently
registered controls to the reusable `Checked`/`Changed` RadioButton contract,
uses the group callback to update the Path label, and exercises the pair with
pointer, Space, arrow, Tab, Shift+Tab, lifecycle, modal, and relaunch paths.

No duplicate registrations were added. The C131 baseline had eight Managed
Notes registrations, including the two path radios, and C132 remains at eight:

`8 -> 8`

The unchanged count is intentional: C132 reuses the real existing setting
rather than adding fake application complexity.

## Tests and NativeAOT proof

The focused C132 suite contains 38 RadioButton/group cases and 12 host cases.
It covers initial state, replacement and idempotence, callbacks and ordering,
programmatic changes, conflict normalization, independent groups, pointer and
Space input, Tab/Shift+Tab, Panel membership, hidden/disabled/unregister
lifecycle, close/relaunch, all stale-Space transitions, modal isolation, and
bounded callback-driven re-selection.

The NativeAOT proof uses production app launch, host registration, pointer
dispatch, native key dispatch, managed callbacks, group exclusion, arrow
navigation, lifecycle cancellation, modal isolation, and close/relaunch. It
also runs the focused suites inside the real managed image. Evidence is stored
under `out\dotnet\c132-managed-radiobutton` with the C132 manifest, composite
image hash, proof-kernel hash, and three serial hashes.
The composite C132 modal marker is the focused host-level modal-isolation
proof with a registered RadioButton background; the full file-picker modal
path remains covered by the standalone C120/C128 production proofs so the
cumulative C132 image stays within its bounded native proof budget.

The ordinary kernel is rebuilt/restored after proof-image validation. Its
canonical locations are `kernel\build\amd64\bin\kernel.elf` and
`ESP\kernel.elf`; the ordinary validator records three fresh boot hashes and
the ordinary manifest.

## Preserved architecture and limitations

ABI v1 and table size 104 are unchanged. No native UI ABI, capability,
runtime, GC, VFS, or input transport expansion was needed. Tab remains
KeyDown-only with no synthetic Tab `KeyChar`; independent left/right Shift
tracking and the C128 lifecycle semantics are preserved.

Deferred features are automatic layout, nested groups, arbitrary group
containers, data binding, theming, animation, and tri-state semantics.
