# AARCH64 Phase 8 — NativeElf GUI application

Status: implemented on `AARCH64_SUPPORT`.

Phase 8 is the first App Model GUI proof. The graphical client is an
independently compiled `EM_AARCH64` ET_EXEC NativeElf stored in the normal
`/Apps` tree. The kernel does not create a proof window or inject proof
events on its behalf.

## Existing architecture used

The audit of the Phase 5–7 path found one common GUI model:

`KernelApp`/`KernelWindow` (`kernel/core/include/kernel/kernel_app.h`) own the
window and widgets; `KernelCompositor` registers, draws, hit-tests, focuses,
drags, and closes those windows; and `desktop::cooperative_yield()` drains the
common input queue before dispatching to the compositor. The existing widget
path supplies labels, buttons, and textboxes. Phase 8 adds a NativeElf owner
adapter over those classes rather than adding an ARM64-specific GUI stack.

The NativeElf entry and C ABI are provided by
`kernel/core/native_elf_baremetal.cpp`, `sdk/include/guidexos/abi.h`, and
`sdk/include/guidexos/app.h`. The Phase 5 console application remains a
separate package and does not require GUI services.

## ABI additions and layout

The existing `GX_API_VERSION` remains `0` and all historical host-call slots
remain at their original offsets. The append-only GUI extension is:

| Field | Offset on AArch64/AMD64 |
| --- | ---: |
| `guiVersion` | 240 |
| `window_destroy` | 248 |
| `widget_create` | 256 |
| `widget_set_text` | 264 |
| `widget_set_value` | 272 |
| `sizeof(gx_host_calls)` / `GX_GUI_HOST_CALLS_SIZE` | 280 |

`GX_GUI_ABI_VERSION` is `1`. A NativeElf client must validate the context
size, API version, host table size, host API version, and GUI version before
calling the extension. The event structure remains 32 bytes and its payload
fields are explicitly `int32_t`; handles are `uint64_t`, and widget types and
event types use fixed-width C fields. The interface exposes no STL, kernel C++
object, or C++ string representation.

The architecture-neutral calls are `window_destroy`, `widget_create`,
`widget_set_text`, and `widget_set_value`, alongside the existing
`request_window_ex` and `poll_event`. The same conceptual ABI is compiled for
AMD64; ARM64 changes only the NativeElf machine type, AAPCS64 entry path, and
executable mapping/cache handling.

## Handles, ownership, and lifetime

Phase 8 application handles are opaque bounded tokens. A token contains a
format magic value, a runtime generation, an object kind, and a small object
ID. The runtime validates the exact active `Arm64Runtime`, generation, window
owner, object kind, table membership, and live compositor object on every
call. A widget handle is resolved through the owning runtime's widget table;
an application cannot use another runtime's handle. Destroyed and duplicate
handles are rejected.

Window creation allocates a normal `KernelWindow`, assigns the NativeElf
runtime as its `KernelApp` owner, and registers it with the common compositor.
Controls are created through `addLabel`, `addButton`, and `addTextBox`, so
their drawing and hit testing use the production widget path. Window close
calls the common `KernelApp::requestClose()` path, which invokes the owner,
unregisters the compositor object, deletes the window, and invalidates focus.
The NativeElf runtime then deletes its owner and releases the image and stack.
No application-owned compositor object survives NativeElf return.

Application strings are pointer-plus-NUL-terminated inputs with a bounded
`GX_ABI_MAX_STRING_BYTES` scan (256 bytes). Kernel-owned title and widget
strings are copied before the call returns; temporary application stack
storage is not retained. Dimension, type, null, overflow, and client-area
bounds are checked. The current model still executes NativeElf at EL1, so
these checks are ABI corruption defenses, not a claim of process isolation.

## Event and input flow

The real route is:

`virtio keyboard/tablet IRQ → IRQ registry handler → scheduler/UI context →`
`virtio poll → common input queue → desktop hit testing → KernelApp owner →`
`bounded NativeElf event queue → gx_main poll_event()`

The virtio IRQ handler only acknowledges/masks the level-triggered source and
never calls application code. During Phase 8, the NativeElf event bridge runs
the existing `desktop::cooperative_yield()` from its scheduler/UI context,
while the desktop-render worker deliberately does not consume the same
stateful input pump concurrently. This keeps one consumer for the virtio
ring and preserves timer preemption and common worker progress. The bridge
converts focus, blur, pointer, key, text-input, widget, and close activity to
`gx_event` values; raw virtio reports are not exposed to the application.

## Proof application and package

Source: `sdk/samples/phase8_arm64_gui/main.cpp`.

Package identity: `com.guidexos.phase8.arm64guiproof`.

Staged layout:

```text
/Apps/Phase8Arm64GuiProof/app.json
/Apps/Phase8Arm64GuiProof/bin/arm64/phase8-arm64-gui-proof.elf
```

The manifest selects the `arm64` entry, `gx_main`, `guidexos-c-abi-v1`, and
the `native-elf` runtime. The application creates a 460×300 common window,
then creates five application-owned controls: the ARM64 proof label, prompt,
textbox, button, and click-count label. It polls bounded events, accumulates
the real textbox characters, increments the click count in application code,
updates the label through `widget_set_text`, handles close, and returns 42.

The payload is independently compiled with Clang as
`--target=aarch64-none-elf -march=armv8-a`, linked by `ld.lld` as an ET_EXEC
at `0x50000000`, and verified as `EM_AARCH64`. The AAPCS64 trampoline places
the `gx_app_context*` in the normal first argument register and changes only
the application stack; the application/kernel boundary remains C-compatible.

## Framebuffer and interaction proof

The Phase 8 runner uses conservative, individually acknowledged QMP
`input-send-event` commands against QEMU's virtio tablet and keyboard. It
does not run the Phase 7 high-rate workload. The proof sequence is:

1. click the textbox with the real tablet and type `arm64` with real keyboard
   key events;
2. click the real button and verify application click count 1;
3. drag the title bar by a meaningful delta;
4. click the common close decoration;
5. wait for the app's close marker, return 42, cleanup, and a distinct second
   launch.

The kernel records bounded window-region verification hashes after the initial
render, text update, button-count update, and drag. The application-originated
markers are:

```text
[phase8-app] gx_main entered
[phase8-app] ABI: GUI services OK
[phase8-app] text input: PASS value=arm64
[phase8-app] button event: PASS count=1
[phase8-app] close requested
[phase8-app] returning 42
```

## Cleanup and durability

Each launch checks the observed return value, stack canaries, compositor
window count, control ownership, executable mapping restoration, stack/image
release, and allocator page delta. The bounded durability gate performs 25
distinct App Model launches in one boot. The first two launches receive the
full real interaction sequence; later launches use the normal app-owned
close-event path after a short bounded auto-close interval. This is still
actual NativeElf load, window creation, compositor registration, app entry,
close, return, and cleanup on every cycle, not reopening one existing window.

The acceptance driver performs three independent fresh boots, each with a
fresh UEFI variable file. It requires 25 `gx_main` entries and 25 cleanup
markers per boot, no unexpected IRQs or exceptions, and
`AARCH64_PHASE8_PASS` only after the app and scheduler checks complete.

## Negative controls and regressions

The host controls cover invalid/cross-owner/stale handles, invalid control
kind, duplicate destroy, null and overlong strings, bad geometry, application
events after exit, and GUI ABI version mismatch. The staged wrong-machine
package verifies that the ARM64 NativeElf selector rejects an AMD64 payload.

The Phase 5 console NativeElf proof and Phase 6 static desktop regression are
run separately. The Phase 6 historical static artifact remains responsible
for the expected `0xcd1073981e16c449` hash. Phase 7's bounded high-level
hardware-input route remains the regression route. The unresolved Phase 7B
QEMU 11 high-rate QMP acknowledgement/delivery limitation remains Outcome E
documentation and is intentionally not part of Phase 8 acceptance; Phase 8
does not emit `AARCH64_PHASE7_PASS`.

The Phase 8 build also runs the host ABI layout test and compiles the same
GUI application source with AMD64 freestanding flags. This validates the
architecture-neutral contract even where the existing full AMD64 NativeElf
runtime remains subject to its separate Mbed TLS build issue.

## AARCH64-9 follow-up

The recommended runtime-service work is implemented in
`AARCH64_PHASE9_APP_RUNTIME.md`: per-application event queues, explicit
wait/wake, generation-checked handles, quotas, idempotent cleanup, concurrent
App A/App B runtimes, and a three-boot QMP harness. EL0 isolation, SMP, GPU
acceleration, networking, QMP transport redesign, and new device classes remain
out of scope.
