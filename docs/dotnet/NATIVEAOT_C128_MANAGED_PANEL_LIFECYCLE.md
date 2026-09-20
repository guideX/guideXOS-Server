# NativeAOT C128 — Managed Panel visibility and interaction lifecycle

## Purpose

C128 validates the lifecycle boundary around the existing C127
`GuideXosPanel`. It does not add another control or change the Panel's
fixed-capacity, non-owning, single-level architecture. The proof follows
existing controls through parent visibility, child-local visibility and
enabled state, membership removal and reuse, focus recovery, modal entry and
exit, and Managed Notes close/relaunch.

## Transition contract

The application owns Panel membership and Panel visibility. A Panel stores
non-owning child references; it does not register, unregister, dispose, reset,
or otherwise take lifetime ownership of a child. `TryRemoveChild` and
`Clear` detach a live control, restore its standalone effective visibility,
and leave the control registered with `GuideXosControlHost` when the caller
registered it there. Reattachment applies the new documented local position
and the current parent visibility; it does not implicitly focus the child.

The application also owns child-local visibility and enabled state. The
effective eligibility of a registered child is the conjunction of host
registration/focusability, enabled state, child-local visibility, and Panel
visibility. `GuideXosControlHost` is the only focus and input authority:

* pointer activation focuses and routes through the host;
* `KeyDown` Space begins the existing split activation gesture and
  `KeyChar` Space commits it; ordinary uninterrupted gestures preserve their
  existing timing and exactly-once behavior;
* Tab and Shift+Tab use registration order and skip ineligible controls;
* hiding or removing the active child clears its control focus and the host
  selects the next eligible registration, or a valid no-focus state;
* showing a Panel or child never steals focus;
* modal entry isolates input, saves the background active identity, and modal
  exit restores that identity when it remains eligible, otherwise it uses
  the existing forward fallback.

An interrupted Space gesture is cancelled when its pending target loses
visibility, enabled state, membership, focus, or modal ownership. The
following stale `KeyChar` is consumed once by the same host and cannot be
rerouted to the fallback control selected during recovery. A later ordinary
gesture starts cleanly. This is deliberately a bounded pending-target state
in the existing host; no input queue, generation abstraction, or new focus
authority was introduced.

## Defect repaired

Before C128, a button, check box, or radio button ignored `KeyDown` Space and
committed on the later `KeyChar` Space. `GuideXosControlHost` normalized focus
before routing each event. If the active control was hidden or removed between
those two events, normalization could select another eligible control and
route the old `KeyChar` to it. A later show or reattachment could therefore
make an interrupted activation observable on the wrong target.

C128 records the pending Space target in `GuideXosControlHost`, cancels it on
the relevant lifecycle transitions, recovers focus through the existing
registration order, and consumes the stale character exactly once. The Panel
remains non-owning and the host registration array remains independent from
Panel membership.

## Managed Notes proof path

The production Notes fixture keeps the C127 layout and seven host
registrations in the same order:
`Open`, `Save`, `Save As`, `Show path`, `Full path`, `File name`, `Document`.
The Path Display Panel remains at `(12,264)` with size `456 x 90`; its two
radio children remain local `(8,14)` and `(148,14)`. The bounded C128 path
launches a focused Panel lifecycle fixture, a separate Workspace launch, and
Managed Notes. It then exercises parent hide/show, a pending Space gesture,
modal picker isolation with pending background input, close/relaunch, and
the initial registration/focus contract. Notes is closed and relaunched once
per proof boot, which is sufficient to detect retained Panel, focus, or
pending-activation state without an allocation-heavy loop.

## Validation evidence

The focused managed fixture is
`GuideXosPanelLifecycleTests`, executed in the
`c128-panel-lifecycle-tests` launch context. It covers Panel hide/show,
remove/clear/reattach, child-local hide/enable transitions, no-eligible and
recovered focus, Tab/Shift+Tab traversal, modal isolation, standalone bounds,
and ordinary uninterrupted activation. The C128 runner is
`scripts/dotnet/run-c128-managed-panel-lifecycle.ps1`; its evidence root is
`out/dotnet/c011ec128-managed-panel-lifecycle/` and its default proof count is
three fresh QEMU boots.

Host-side managed tests and serial execution inside guideXOS are reported
separately; this phase does not claim bare-metal validation.

Final validation on 2026-09-20:

* `dotnet build samples/managed/HostLogProof/HostLogProof.csproj -c Release
  -p:ProofPhase=C128Composite --no-restore` passed with 0 errors and the
  existing two warning classes (unreachable proof branch and explicit
  `Microsoft.DotNet.ILCompiler` package reference).
* The focused managed fixture passed 46 cases on each proof boot. The C127
  managed Panel regression fixture also passed its 60 cases, and the C126
  result plus the C116-C127 regression marker remained PASS.
* The C128 NativeAOT/QEMU proof passed three fresh boots. Evidence is in
  `out/dotnet/c011ec128-managed-panel-lifecycle/c128.manifest.json`; the
  composite ELF hash is
  `EE0B01E7138E9ECBEDA9E039E1BD7F526DCCCD7A56BF5CE5FC1DC20D777237A7`, the
  proof kernel hash is
  `D39B6020ACEB8475B4777886532E112C8B15B8A27D5E34DFB3EFC6739FEFAF0D`, and
  the proof ramdisk hash is
  `69F3285BB4C95A456B9C58F706F5FD45B8DBBEE4465F1B7626CF4E94C5BE1F3F`.
  Serial hashes for boots 1-3 are respectively
  `BC67F42C1066EFDD9549E7E9576B2D7C7E50F47B31D477717CBCD36BCDD60143`,
  `4ECBF49E3D3452258CF49D022F62AF390EB52D037CB39DBBC2B69497996F808F`, and
  `7E6BE592164CAC7114C27E490976FE387780C9E30A37780D47CCB0836CB8569F`.
* The ordinary artifact was restored from `ESP/kernel.elf`; the final
  ordinary validator passed three fresh boots at
  `out/dotnet/c011ec128-managed-panel-lifecycle/ordinary-boot-final/run-20260920-161000808-422008ec/ordinary-boot.manifest.json`.
  `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` both ended at
  `A3325E6C5E62C4ECD9C463EE38DB0763DDA2A67F54D2A1AE1F86673B37457CDF`.
  The ordinary ramdisk and bootloader remained
  `7E93E34732526DBFFEE097912F776467B0DD03C989CBDA06F4AA24B0945A969B` and
  `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`.

The C128 image retains and executes the C127 managed 60-case fixture and the
C126 proof. The older C127 kernel-level direct Shift/Tab transport sequence
was not duplicated in the combined C128 image after that direct call stalled
before the C128 block; the C127 standalone evidence remains separate, while
C128 covers Shift/Tab lifecycle behavior in the 46-case managed fixture. This
is an execution-scope note, not a weakened assertion: every C128 marker and
all required managed assertions remained enabled.

Host ABI v1/table 104, capabilities, input transport, NativeAOT runtime, GC,
and VFS remain unchanged. Nested containers, clipping, scrolling, layout
engines, and managed in-OS compilation/debugging remain deferred roadmap
items. The Panel remains single-level, fixed-capacity, and non-owning.
