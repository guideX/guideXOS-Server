# Settings S11 Native Runtime Slice

Status: **Outcome B — partial progress; S11 is not accepted.**

S11 adds a dedicated native Settings application to the production kernel app
manager and compositor. The native window reuses the S10 presentation core,
shows a bounded read-only System/About snapshot from kernel-owned identity,
version, and architecture values, and responds to guest keyboard and mouse
input. The remaining acceptance gap is the guest activation of the legacy
Control Panel: the Start menu entry and resolver route are preserved, but this
run did not complete its activation in QEMU.

## Architecture decision

The implementation keeps the five runtime responsibilities separate:

1. **Shared presentation state:** `settings_presentation_core.cpp` remains the
   S10 source of navigation, focus, descriptors, capability state, and input
   transitions. The AMD64 kernel links this same freestanding source.
2. **Production rendering adapter:** `SettingsNativeApp` in
   `kernel/core/kernel_apps.cpp` implements the S10 adapter contract. It maps
   kernel window input into core events and paints the resulting state through
   the existing framebuffer and `KernelCompositor`. It does not introduce a
   second UI state machine or GUI framework.
3. **Runtime-owned provider snapshot:** `settings_native_provider.cpp` copies
   the product name, kernel version, and architecture into fixed-capacity
   caller-owned storage. The versioned structure carries a size, version,
   availability flag, and revision. It retains no pointers and returns an
   explicit unavailable status when a source cannot be copied safely.
4. **Registration and lifecycle:** built-in metadata gives Settings the
   canonical identity `gxos.builtin.settings`, a native `KernelApp` name, and
   bare-metal availability. `AppManager` assigns the registered identity to a
   private `KernelApp` field before initialization. Duplicate launch follows
   the existing manager policy and focuses the running instance; close
   unregisters the window and destroys the app, so reopening starts fresh.
   Different factories cannot claim the same canonical runtime identity.
5. **Authorization authority:** the provider authorizes by reading the
   AppManager-assigned identity from the caller object. An ordinary launch
   parameter cannot set that field. The adapter exposes no write operation;
   every operation submission is rejected, and no Settings mutation syscall
   or provider write API was added.

The hosted Settings Center continues to use the same S10 core. The visible
Start-menu Settings action remains on its legacy Control Panel route until the
native launch and lifecycle have passed acceptance.

## Provider slice

The snapshot currently renders these values:

| Field | Production source |
|---|---|
| Product | `KERNEL_NAME` |
| Kernel version | `KERNEL_VERSION_STRING` |
| Architecture | `kernel::arch::get_arch_name()` |

The snapshot is version 1, revision 1, and uses fixed caller-provided storage.
Unsupported versions, invalid or undersized structures, stale revisions,
unauthorized callers, and unavailable values have explicit status results.
Only the Settings runtime identity can read the provider. No memory totals,
account information, network details, or mutation surface are included.

## Validation evidence

### Hosted and focused tests

- Native registration, provider safety, core input/focus, duplicate launch,
  parameter spoofing, and lifecycle harness: **549/549 passed**, including 100
  create/close/reopen repetitions with balanced window registration and app
  destruction.
- S10 shared presentation core: **2,645/2,645 passed**.
- Affected hosted Settings and service regression suites: **443/443 passed**.
  The prior S10 acceptance record remains **464/464 hosted regression checks**.
- `git diff --check`: clean.

### AMD64 kernel build and link

- Normal AMD64 production kernel build completed with zero compiler errors.
- Initial full build: 156 compile commands, 453 warning diagnostics.
- Final incremental builds after the input-mask, identity-collision, and focus
  hook fixes: 13 additional compile commands and 267 warning diagnostics.
- Across the recorded build passes: 169 compile commands and 720 warning
  diagnostic lines, with zero compiler errors.
- No warning was attributable to the new Settings source or its new code in
  `kernel_apps.cpp`; the diagnostics came from existing unused base-handler
  parameters and existing unrelated app code. No warning suppression was
  added.
- `settings_native_provider.o` and the shared presentation-core object have no
  undefined symbols. Comparing final `kernel.elf` with the S10 kernel found no
  new unresolved symbols (42 final versus 46 in that S10 baseline; four old
  unresolved entries were removed).
- The native provider and presentation path use `KernelApp` and
  `KernelCompositor`; they add no hosted `ProcessTable`, hosted IPC, or hosted
  `Compositor` dependency, and require no additional C++ runtime facility.

Final kernel image SHA-256:

`B5349D89DF7127D8CFE03F9697D28642B9EFC867FA4A1280A87B8F4BF7B46AF7`

### QEMU guest evidence

The normal AMD64 graphical image booted to the desktop from an isolated copy
of the ESP and OVMF files. The final kernel image was then launched through
`desktop.launch gxos.builtin.settings`; the guest resolver selected the
registered `Settings` KernelApp, and the window displayed the runtime
identity and real product/version/architecture snapshot.

The guest serial log records `focus=acquired`, a ready revision-1 snapshot,
keyboard selections of About and System through the shared core, shutdown,
and focus loss. An earlier final-input run also records mouse selection of
System and About through the same core, followed by close/reopen with a fresh
System selection. A final-symbol-fix guest boot reconfirmed the current kernel
launch, provider snapshot, keyboard transitions, close, and clean desktop
return.

The guest Start menu visibly contains both `Control Panel` and `Settings`.
Resolver and hosted regression tests confirm the legacy Control Panel identity
and shell-action target remain distinct. The QEMU run did not complete
activation of the legacy Control Panel entry, so its guest operation is not
claimed as validated and S11 remains Outcome B.

All guest runs used task-specific directories under
`out/validation/settings-s11-production-20261011/`, copied ESP/OVMF inputs,
and separate QMP ports. The QEMU process snapshots are saved beside each run.
Only the QEMU processes created for these runs were exited; unrelated existing
QEMU sessions were left untouched.

Key evidence files:

- Final kernel guest serial: `out/validation/settings-s11-production-20261011/guest-final-kernel/guest-com1.serial.log`
- Final kernel Settings launch screenshot: `out/validation/settings-s11-production-20261011/guest-final-kernel/settings-system.png`
- Final kernel keyboard About screenshot: `out/validation/settings-s11-production-20261011/guest-final-kernel/settings-about-keyboard.png`
- Provider fields and mouse-selected About screenshot: `out/validation/settings-s11-production-20261011/guest-acceptance-final/settings-about-mouse-selected.png`
- Earlier close/reopen screenshot: `out/validation/settings-s11-production-20261011/guest-acceptance-final/settings-reopen-system.png`
- Start-menu Control Panel entry screenshot: `out/validation/settings-s11-production-20261011/guest-acceptance-final/pointer-recover-y.png`

## Limitations and next phase

Only System/About is backed by a real provider. Other categories are visibly
deferred or unavailable; there are no mock providers. The snapshot revision is
constant at 1 for this immutable boot identity/version slice and must be
advanced if a later provider adds changing values. The legacy Start-menu
Settings routing has not been redirected.

**Recommended S12 scope:** finish and preserve QEMU evidence for activation,
operation, close, and reopen of the legacy Control Panel through its Start-menu
shell-action route. Once that regression passes, route Start → Settings to
`gxos.builtin.settings` and verify the legacy Control Panel remains independently
reachable. Keep S12 read-only; do not add a Settings write provider until the
native registration, identity, and guest lifecycle are accepted.
