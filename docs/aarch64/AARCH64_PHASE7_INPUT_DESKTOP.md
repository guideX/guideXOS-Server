# guideXOS Server AArch64 Phase 7 — Input and Interactive Desktop

## Status

This pass brings up the real QEMU input path and the common interactive
desktop path.  The validated boundary is Outcome B: virtio keyboard/tablet
events reach the common input queue, move the common cursor, activate the
real Start button, focus a common compositor window, route text, and drag the
window.  The high-rate durability workload remains incomplete because the
QEMU 11 `input-send-event` producer coalesces or back-pressures long bursts
before the guest can observe the requested event counts.  Therefore this
pass does not claim `AARCH64_PHASE7_PASS` or Outcome A.

The historical static Phase-6 desktop path remains separate and unchanged
when no input is supplied.  The Phase-6 regression was rerun after the
Phase-7 changes: three fresh boots produced `0xcd1073981e16c449` each time.

## Existing architecture audit

The common path was already present in `kernel/core/input_manager.*`,
`display_input_mapper.*`, `desktop.cpp`, `kernel_compositor.cpp`, and the
kernel application/widget model.  It provides unified pointer coordinates,
button masks, key values, modifier state, cursor painting, desktop hit
testing, compositor focus, title-bar dragging, Start-button dispatch, and
focused-window key routing.

PS/2 and x86 port I/O remain AMD64-specific.  USB HID is an optional existing
provider, but expanding a USB stack was deliberately out of scope.  The new
ARM64 code ends at the existing common input-manager boundary; no ARM64 UI
event system was added.

## Selected QEMU transport

The selected devices are:

- `virtio-keyboard-device`, using Linux/evdev-style virtio key events; and
- `virtio-tablet-device`, using absolute X/Y and button events.

They are the smallest clean Phase-7 substrate on the installed QEMU 11.0.0
`virt` machine: they avoid a new USB controller/HID stack, expose a real
virtio-MMIO transport, and have a deterministic QMP `input-send-event`
facility for automation.  QEMU is started with
`virtio-mmio.force-legacy=false`, so the modern virtio-MMIO v2 path is used.

The platform parser consumes the QEMU DTB `virtio,mmio` nodes rather than
depending on undocumented fixed device addresses.  The explicit keyboard and
tablet nodes are found in the DTB-described virtio-MMIO table; in the tested
configuration their bases are `0xA003E00` and `0xA003C00`, with global GICv2
IRQs 79 and 78 respectively.  Those values are diagnostic evidence, not
hard-coded discovery constants.

## IRQ and provider boundary

The path is:

```text
virtio input device
  -> virtio-MMIO used ring
  -> ARM64/GICv2 interrupt registration and bounded acknowledge
  -> scheduler-context virtio poll
  -> common input::submit_platform_pointer_* / submit_platform_key
  -> bounded common input queue
  -> desktop::cooperative_yield
  -> common desktop/compositor/cursor
```

The IRQ callback only acknowledges the device, masks the level-triggered SPI,
and records pending work.  The driver consumes used-ring entries from normal
scheduler context, then unmasks the SPI, avoiding allocation in IRQ context and
preventing a pending device line from starving the consumer.  The common IRQ
registry was extended to cover
the QEMU GICv2 SPI range used by the virtio devices; the architectural timer
and preemption path remain active.

## Common queue and mappings

`kernel/core/input_queue.*` provides a fixed 256-event SPSC queue.  It uses a
drop-newest overflow policy, preserves button/key events, and coalesces only
consecutive pointer-motion events with the same button mask, wheel value, and
hardware origin.  It records received, queued, dropped, coalesced, and
high-water counters.  The ARM64 provider submits `OriginVirtioHardware`, so
the guest proof can distinguish real QEMU events from synthetic unit tests.

The keyboard provider maps virtio Linux key codes to the common guideXOS
values.  It covers letters, digits, Escape, Enter, Backspace, Shift, Ctrl,
Alt, Meta, Caps Lock, Space, arrows, Delete, and basic modifier state.  The
tablet maps absolute X/Y through `DisplayInputMapper`, clips to the GOP
desktop bounds with signed-safe arithmetic, and maps left/right/middle/touch
button transitions to the common mask.  Invalid event types, unsupported key
codes, malformed used entries, and invalid coordinate ranges are rejected
without becoming unchecked queue indexes or desktop coordinates.

## Desktop proof surface

`phase7_input_proof.cpp` creates one ordinary common `KernelWindow` titled
`Input Routing Proof`, with a title bar, label, and common textbox.  It starts
unfocused.  The normal compositor handles the title-bar press, focus callback,
bounded drag, redraw, and release.  The normal desktop handles the Start
button; no Phase-7-specific Start handler or cursor painter was added.

The deterministic QMP harness sends the following real-device sequence:

1. absolute tablet movement to the proof window title bar;
2. primary press, pointer movement, and primary release;
3. textbox activation and `guidexos` keyboard events; and
4. a real taskbar Start-button click.

Observed guest markers include:

```text
[guideXOS] cursor routing: PASS
[guideXOS] focus routing: PASS
[guideXOS] window drag: PASS initial=140,120 final=280,191 delta=140,71
[guideXOS] keyboard routing: PASS
[guideXOS] text input: PASS value=guidexos
[guideXOS] Start button input: PASS
```

These markers are emitted by the guest after common routing, not by the host
harness.  The title-bar drag is a normal compositor drag, not a direct test
assignment to window coordinates.

## Deterministic host mechanism

`scripts/test-aarch64-phase7.ps1` starts QEMU with serial capture and a TCP
QMP endpoint.  It uses QMP `input-send-event` for the tablet and keyboard.
QEMU 11 requires a tagged `InputKey` object for key events (`qcode` plus
`data`), which the harness supplies.  The initial interaction is therefore
fully reproducible and does not require a human mouse or keyboard.

The host controls in `tests/aarch64_phase7_host_tests.cpp` cover queue
coalescing, drop-newest overflow, invalid event rejection, signed relative
clipping, and absolute bounds.  They pass with
`AARCH64_PHASE7_HOST_CONTROLS_PASS`.

## Durability result and limitation

The guest queue remains bounded and safe: observed progress lines retain
`queue=0 dropped=0`, and no exception, fatal marker, or unexpected IRQ was
observed.  However, the current QEMU/QMP burst producer does not deliver the
full requested stress volume to the guest.  The finalized paced run observed
`pointer=10157 buttons=147 keyboard=100` before the producer stalled, with
`queue=0 dropped=0`; earlier pacing attempts observed smaller subsets.  The
guest consequently does not emit `input durability: PASS`,
`input/scheduler integration: PASS`, or `AARCH64_PHASE7_PASS` for this pass.

The queue high-water mark and coalesce total are intentionally not presented
as completed-stress statistics: the burst stopped before the guest emitted
its final totals.  The observed progress lines showed no queue overflow,
exception, fatal marker, or unexpected IRQ, and the normal architectural
timer/preemption workload remained live while the input producer was active.

This is an honest acceptance boundary, not a synthetic success marker.  The
next input pass should either use a QMP producer that preserves individual
events at a controlled rate or add a validated QEMU-side event pacing method,
then require the original 10,000/1,000/1,000 observed guest counts and three
fresh successful boots.

## Regression and AMD64 status

The Phase-7 build includes the Phase-5 ARM64 payload and the Phase-6 common
desktop sources.  The static Phase-6 framebuffer path is not changed by the
interactive provider when no events are supplied.  The common changes retain
the existing AMD64 PS/2/USB provider boundary; AMD64 device-specific code was
not redirected through virtio.

The host queue/mapper controls compile with the normal hosted C++ compiler.
The completed regression evidence for this pass is:

- Phase 1: three fresh boots plus the EM_X86_64 wrong-machine negative
  control — PASS;
- Phase 2: three fresh boots plus host malformed-input controls — PASS;
- Phase 3: three fresh boots, 10,000 preemptions per boot, and scheduler
  contract controls — PASS;
- Phase 4: three fresh boots with allocator/VFS/write/read and scheduler
  durability — PASS;
- Phase 5: three fresh boots with the ARM64 proof app, 100 launches per boot,
  zero allocator delta, and wrong-machine rejection — PASS;
- Phase 6: three fresh boots with the original static framebuffer hash —
  PASS; and
- common window UI regression and Phase-7 host controls — PASS.

AMD64 device-specific PS/2/USB paths were not redirected through virtio.  The
full AMD64 build remains subject to the pre-existing Mbed TLS profile blocker;
changed common input units were compiled by the host controls and the common
window regression.  No unrelated Mbed TLS/networking repair was attempted.

## Three fresh Phase-7 boot results

`scripts/test-aarch64-phase7.ps1 -InteractiveOnly` completed three
independent UEFI/QEMU boots.  Each guest log contained device discovery,
keyboard and pointing-device initialization, cursor routing, mouse button
routing, focus routing, the common-window drag
`initial=140,120 final=280,191 delta=140,71`, keyboard routing, textbox value
`guidexos`, and real Start-button activation.  The interactive-only harness
deliberately reports “durability not claimed”; none of these boots emitted
`AARCH64_PHASE7_PASS`.

The full stress harness remains bounded and fail-closed.  It requests at least
10,000 pointer reports, 1,000 button events, and 1,000 keyboard events through
QMP, requires the guest to observe the counts, and only then permits the
Phase-7 pass marker.  On QEMU 11.0.0 the long QMP producer stalls before the
button and keyboard thresholds, so the test exits without a false success.

## Files and tooling

- `scripts/build-aarch64-phase7.ps1` — loader, kernel, proof app, host controls,
  and Phase-5 resource build;
- `scripts/run-aarch64-phase7.ps1` — QEMU virtio keyboard/tablet boot;
- `scripts/test-aarch64-phase7.ps1` — QMP interaction and acceptance harness;
- `tests/aarch64_phase7_host_tests.cpp` — bounded common negative controls;
- `tests/aarch64_phase7_dtb_probe.cpp` — DTB virtio descriptor probe; and
- `kernel/core/virtio_input.cpp` — ARM64 virtio input provider.

## Exact AARCH64-8 recommendation

Do not expand to general USB or physical hardware yet.  AARCH64-8 should
first finish the QEMU input-event pacing/durability proof, then expose one
real ARM64 NativeElf application window through the already-proven common
compositor and App Model.  Require three fresh boots, the original Phase-5
regression, the static Phase-6 hash, and the full Phase-7 observed stress
counts before adding richer application-window behavior.
