# guideXOS Server AArch64 Phase 7 — Input and Interactive Desktop

## Status

This pass brings up the real QEMU input path and the common interactive
desktop path.  The validated high-level boundary remains the prior Outcome-B
proof: virtio keyboard/tablet events reach the common input queue, move the
common cursor, activate the real Start button, focus a common compositor
window, route text, and drag the window.  Phase-7B closes the investigation as
Outcome E rather than claiming Outcome A: QEMU 11's acknowledged
`input-send-event` producer still cannot deliver the requested full durability
workload through this QMP path.  Therefore this pass does not claim
`AARCH64_PHASE7_PASS` for full stress.

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

The QMP harness also has fail-closed controls for malformed JSON, impossible
batch sizes, unmatched/error responses, bounded read timeouts, QEMU exit, and
guest watermark timeout/retry exhaustion.  The final run exercised the normal
QMP framing and batch controls before the bounded guest watermark failure; no
orphan QEMU process remained.

## Phase-7B closure: deterministic transport audit

Phase 7B did not close the durability gate.  It classifies the remaining
failure as Outcome E: QEMU 11.0.0's `input-send-event` producer path cannot be
driven reliably to the requested 10,000/1,000/1,000 guest-observed workload
through this QMP interface, even after acknowledged and adaptively paced
delivery.  The original finalized run had observed approximately
`pointer=10157 buttons=147 keyboard=100`; that result was not accepted as a
durability pass because the button and keyboard thresholds were not reached.

The final bounded diagnostic run made the boundary explicit.  The host
intended 2,080 pointer reports, sent 260 QMP `input-send-event` commands, and
received all 260 command responses.  The guest reached total
`pointer=2087 buttons=7 keyboard=16`, including stress deltas
`stress-pointer=2072 stress-buttons=0 stress-keyboard=0`, then reached its
finite integration guard.  Its final telemetry was:

```text
queue-depth=0 queue-high-water=12 dropped=0 coalesced=1817
virtio-irq=269 virtio-polls=4000002 virtio-drains=2393 irq-status-acks=41
malformed=0 unknown-irq=0 exceptions=0
```

The host-side stress interval was 39.64 seconds at the point of failure,
which is 52.5 intended pointer reports/sec and 6.6 QMP acknowledgements/sec.
No QMP retry was needed in this particular run because the guest stopped at the
finite guard before the first bounded retry checkpoint; other runs exercised
the four-attempt, two-second watermark retry path and failed in the same
producer boundary.  Earlier paced variants reached approximately 2,300,
3,200, 7,300, and 10,000 pointer reports but stopped before all three class
thresholds.  Across those attempts the queue remained empty or nearly empty,
with zero drops and no unexplained IRQ storm.

The root cause is mixed at the host/QEMU device boundary, not a common queue
overflow or a guest IRQ loss bug.  QMP command acknowledgement means that QEMU
accepted and parsed the command; it does not mean that the virtio-input device
has placed every report in the used ring.  The guest continued to receive
virtio interrupts and drain used entries while QMP acknowledgements continued,
but the producer ceased making progress before the requested workload was
consumed.  The guest-side evidence is the opposite of overflow: queue depth
returned to zero, `dropped=0`, `malformed=0`, `unknown-irq=0`, and no exception
marker appeared.

The producer protocol was changed from the prior burst-style path to a
persistent QMP session with greeting validation, `qmp_capabilities`,
`query-commands`, `query-version`, request IDs, response matching, error
validation, and response draining.  Stress uses one eight-report command at a
time (`QmpBatchSize=8`, one command in flight), waits for its QMP response,
then applies a 120 ms inter-group delay for pointer reports and 50 ms for
button/keyboard reports.  Every 32 reports the host waits for a guest telemetry
watermark; a missing watermark has a bounded two-second wait and up to four
retries.  The workloads are padded only to that telemetry boundary:
10,016 pointer reports, 1,024 button reports, and 1,024 keyboard reports.
The acceptance thresholds remain unchanged at 10,000, 1,000, and 1,000.

Event-count semantics are explicit.  Pointer counts are hardware absolute
reports observed by the virtio driver, not common routed events.  The common
queue may safely coalesce adjacent motion events; the final coalesced count is
reported separately.  Button counts are individual hardware press/release
reports, and the stress sequence alternates down/up so its requested endpoint
is released.  Keyboard counts are individual hardware key reports; 1,000
reports therefore represent 500 press/release actions in the repeated `a`
pattern.  Text-widget storage capacity is not used as the keyboard durability
counter.

Guest telemetry is periodic rather than per-event and includes pointer,
button, keyboard, queue depth, queue high-water, drops, coalesces, virtio IRQs,
virtqueue polls/drains, and unexpected IRQs.  A stress-only render suppression
is enabled only after the already-proven cursor/focus/drag/keyboard/Start proof
has completed.  It prevents the test from turning every stress report into a
full wallpaper redraw while preserving the normal desktop path for the real
interaction proof and restoring normal drawing before any successful final
framebuffer verification.  It did not manufacture guest counters or markers.

The Phase-7 guest still emits `AARCH64_PHASE7_ERROR` when its finite bounded
integration loop expires; it never emits `AARCH64_PHASE7_PASS` for an
incomplete stress run.  Thus this is a fail-closed Outcome E, not a weakened
threshold or synthetic pass.

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

No three-boot full-durability result is claimed for this Outcome-E run:
the first fresh full-stress boot terminates at the bounded guest guard, and
the harness cleans up QEMU before another stress boot can be called complete.
The three-boot result that is complete is the historical high-level routing
probe above; it is intentionally not substituted for the requested durability
gate.

## Files and tooling

- `scripts/build-aarch64-phase7.ps1` — loader, kernel, proof app, host controls,
  and Phase-5 resource build;
- `scripts/run-aarch64-phase7.ps1` — QEMU virtio keyboard/tablet boot;
- `scripts/test-aarch64-phase7.ps1` — QMP interaction and acceptance harness;
- `tests/aarch64_phase7_host_tests.cpp` — bounded common negative controls;
- `tests/aarch64_phase7_dtb_probe.cpp` — DTB virtio descriptor probe; and
- `kernel/core/virtio_input.cpp` — ARM64 virtio input provider.

## Exact AARCH64-8 recommendation

Do not add NativeElf GUI windows or broaden device scope yet.  First upgrade
QEMU beyond 11.0.0, or validate another standard QEMU hardware-input
automation mechanism that still targets the existing virtio keyboard/tablet
devices.  Re-run this exact guest path with the unchanged 10,000/1,000/1,000
guest-observed thresholds, separate submitted/acknowledged/observed counts,
and three fresh complete durability boots.  Only after that gate passes should
AARCH64-8 expose one real ARM64 NativeElf application window through the
already-proven common compositor and App Model.
