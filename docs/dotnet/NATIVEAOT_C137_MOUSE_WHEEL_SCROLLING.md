# NativeAOT C137 — Mouse-Wheel Transport and Managed Vertical Scrolling

Phase C137 adds vertical wheel input to the existing guideXOS pointer path:

```text
QEMU input-send-event(btn wheel-up/wheel-down)
  -> emulated PS/2 IntelliMouse packet
  -> PS/2 IRQ12 decoder
  -> input manager / compositor
  -> existing NativeAOT launch-flags input dispatch
  -> GuideXosControlHost
  -> TextArea or ListBox viewport
```

## Native architecture

The existing driver already performed the standard IntelliMouse negotiation:
sample rates `200, 100, 80`, followed by `GetDeviceId`; device ID `3` selects
four-byte wheel packets. A standard device remains supported: if the device
does not report ID `3`, the driver stays in three-byte mode and the wheel byte
is not expected.

The packet decoder retains the PS/2 first-byte bit-3 synchronization check,
rejects overflow flags, validates X/Y sign bits, and resets the assembly phase
when a malformed packet is rejected. The fourth byte is normalized at the
driver boundary as a signed four-bit vertical delta (`1..7` positive,
`8..15` negative after two's-complement normalization). A bounded accumulator
preserves short interrupt bursts without changing the public `int8_t` input
manager contract. Horizontal wheel, tilt, extra buttons, and other HID
features remain deferred.

The C137 convention is:

* positive delta = scroll up, toward earlier lines/items;
* negative delta = scroll down, toward later lines/items;
* zero delta is ignored;
* one normalized notch moves three logical lines/items;
* managed controls clamp input to eight notches per event and clamp the
  resulting viewport to its valid range.

## ABI and managed representation

The Host ABI remains version `1` with table size `104`. The existing launch
input field has a four-bit event-kind lane and a 24-bit payload containing the
12-bit X and 12-bit Y coordinates. The six existing pointer/key kinds remain
unchanged. C137 uses reserved kind values `0x07..0x0F` as nine wheel values,
encoding normalized deltas `-4..+4`; the 24-bit coordinate payload is retained
unchanged. Delta zero is not emitted by the native path.

Managed `GuideXosInputEvent` adds the independent `Wheel` kind and signed
`WheelDelta`. Wheel events have no primary or secondary button identity and do
not enter click, key, or stale-gesture paths. The current pointer coordinates
are carried through unchanged; no pointer movement is synthesized.

## Host routing and controls

`GuideXosControlHost.HandleWheel` first delegates to a modal host. An active
one-owner transient capture then swallows the event, preventing wheel-through
to an underlying TextArea or ListBox. Otherwise the application hit-tests the
current pointer position and offers the event to the eligible control under
that point. C137 does not add bubbling, a capture stack, or another focus
system.

`GuideXosTextArea` tracks the first visible logical line, visible-line count,
and total logical-line count. Wheel changes only the viewport. The caret and
logical selection remain unchanged; keyboard editing/navigation continues to
use the existing caret-visibility rules. Hidden and disabled controls cannot
scroll, and replacing content reclamps the viewport.

`GuideXosListBox` tracks `FirstVisibleIndex` independently of
`SelectedIndex`. Wheel changes only the viewport. Keyboard navigation still
reconciles the selected item into view, and pointer selection maps the visible
row back to its logical item. Empty and short lists remain at offset zero.

Panel membership, modal isolation, visibility, enabled state, close/relaunch,
and existing C134/C135/C136 lifecycle rules are retained. Wheel does not
activate buttons, toggle checkboxes, select radio buttons, open context menus,
or complete a pointer gesture. The synchronous route uses the existing bounded
dispatch state and does not create a per-event wheel object or temporary
transport array.

## QEMU proof

The focused runner uses QMP `input-send-event` with `btn` events whose button
is `wheel-up` or `wheel-down`. This is injected into the same emulated PS/2
device used by the real mouse driver; no managed wheel handler is called
directly. It also sends primary/secondary button sequences, popup Escape,
pointer moves, a newly visible ListBox row click, and a bounded 100-event
alternating wheel burst. The sequence then sends a physical F12 key through
QMP. The production managed surface closes, the native desktop relaunches
Managed Notes with the `c137-relaunch` context, and the new instance reports
eight registrations, zero TextArea/ListBox viewport offsets, zero selection,
and no capture.

The C137 focused suite contains 46 cases:

* transport representation/state preservation: 12;
* TextArea viewport, caret, host, capture, modal, lifecycle, and burst cases:
  16;
* ListBox viewport, selection, keyboard/pointer reconciliation, host, modal,
  lifecycle, and burst cases: 18.

The authoritative serial logs, input contract, hashes, manifest, and three
fresh-boot records are emitted below:

`out/dotnet/c137-mouse-wheel-scrolling-final3/`

The same runner retains the C127–C136 regression evidence and the genuine C136
secondary context-menu sequence. Ordinary-kernel restoration is performed
after proof activity; `ESP/ramdisk.img` is treated as protected input and its
hash is checked before and after the run.

## Deferred

Horizontal/tilt wheel, touchpad gestures, kinetic or smooth scrolling,
scrollbars, drag auto-scroll, wheel selection in ComboBox or PopupMenu,
nested routed-event bubbling, and global scroll preferences are outside C137.
