# C109 — Production composite NativeAOT application lifecycle

Status: **Outcome A — ordinary production launch integrated**

Date: 2026-09-13
Branch: `v1.1_DOTNET_SUPPORT`
Evidence: `out/dotnet/c011ec109-production-composite-lifecycle/`

## C108 handoff and C109 boundary

C108 proved Model P: one resident composite NativeAOT image, one
`RuntimeInstance`, one initialized PAL/GC/code-manager/module domain, and one
persistent attached managed thread. It also repaired the guideXOS bridge defect
that cleared the bounded managed heap and rewound the allocation cursor on
every `RhpReversePInvoke` entry. The repaired invariant is **initialize once,
preserve thereafter**.

C109 carries that lifecycle through the ordinary desktop launch interface. It
does not add a second runtime, image unload, GC restart, module registration, or
managed assembly loader. `HostLogProof` remains the bounded validation sample;
the production-facing selector and image ownership are implemented in the
normal launcher path.

## Production-versus-diagnostic audit

The production-required pieces are:

- `desktop::launch_app(const char*)` as the ordinary launch request boundary;
- the stable logical application-id mapping in
  `nativeaot::launchLogicalApplication`;
- one resident image at `/system/apps/GXOSAPP.ELF`;
- the `NativeGxAppContext.userData` selector ABI (`1 = A`, `2 = B`, `0 =
  invalid`);
- the `ProductionComposite` managed build mode; and
- `PersistentCompositeLifecycle` runtime-pack behavior, which keeps managed
  heap/runtime storage launch-resident after the first initialization.

The following remain test-only or historical and are not required to select or
execute the production lifecycle:

| Configuration or evidence | Classification |
| --- | --- |
| `C107Composite` and `C108ThreadStaticLifecycle` managed modes | Historical phase-only proof modes |
| `GXOS_C107_PRODUCTION_LAUNCH` and `GXOS_C108_PRODUCTION_LAUNCH` | Historical phase-only launcher paths |
| C108 TLS identity census and managed callback shim | Historical/test-only diagnostics |
| `[NATIVEAOT-PERSISTENCE]`, `[NATIVEAOT-HEAP]`, and startup serial records | Neutral observational evidence; not control flow |
| `run-c109-production-composite-lifecycle.ps1` and QEMU sequencing | Test-only validation |

The production boot contains no `[C107-LAUNCH]` or `[C108-...]` launcher
markers. The validation sample retains its historical `C107-APP-*` semantic
markers so the same managed assertions can be compared with C107/C108; those
strings do not select the runtime lifecycle.

## Production launch contract and identity

The smallest ordinary contract is:

```text
desktop::launch_app(applicationId)
```

The production sample uses these stable logical ids:

| Logical id | Selector | Meaning |
| --- | ---: | --- |
| `com.guidexos.nativeaot.hostlogproof.app-a` | `1` | App A |
| `com.guidexos.nativeaot.hostlogproof.app-b` | `2` | App B |
| `com.guidexos.nativeaot.hostlogproof.invalid` | `0` | Managed invalid-id path |

An id under the production namespace is resolved to the single composite
image. A known A/B id supplies its selector through the existing bounded
`NativeGxAppContext.userData` field. The invalid id, and an unknown suffix in
the namespace, supply selector zero; the managed dispatcher returns `-4`,
which the native launcher reports as `invalid-app-id`. No package discovery,
dynamic assembly loading, or app-specific entrypoint address is introduced.

## Composite image ownership

`nativeaot::launchLogicalApplication` owns the mapping decision. The first
logical launch maps and records `/system/apps/GXOSAPP.ELF`, starts the runtime
foundation, attaches the local-storage/ThreadStore bridge, and retains the
image for the boot lifetime. Later logical launches must name the same image;
the resident loader reuses its recorded mapping and runtime state. A different
image is still rejected as `Busy` or `BaseCollision` before another runtime or
module can be registered.

The deterministic NativeAOT artifact is the `HostLogProof.elf` build output,
staged in the ordinary wallpaper-pack application directory as
`/apps/GXOSAPP.ELF`, which is mounted by the kernel at
`/system/apps/GXOSAPP.ELF`.

## Initial production launch path

The exact source-backed call chain is:

```text
ordinary desktop/icon/start-menu request
  -> desktop::launch_app(applicationId)
  -> nativeaot::isProductionLogicalApplicationId(applicationId)
  -> nativeaot::launchLogicalApplication(applicationId, report)
  -> selector 1/2/0 from the stable logical id
  -> nativeaot::launchLogical("/system/apps/GXOSAPP.ELF", selector, report)
  -> launchInternal(path, selector, report)
  -> read/probe and map one ELF image
  -> initialize local storage and the guideXOS NativeAOT ThreadStore adapter
  -> GuideXosNativeAotApplicationEntry(startup context)
  -> install PAL hooks, PAL/GC startup tables, and call RhInitialize(0)
  -> install the guideXOS TLS bridge
  -> ManagedMain(startup context)
  -> managed dispatcher selects App A, App B, or invalid-id
  -> return managed status to launchInternal
  -> desktop::launch_app returns to guideXOS
```

The accepted serial logs show one executable mapping and startup stages 1
through 7 exactly once. PAL initialization, GC initialization, code-manager
registration, and module initialization are each one-time events.

## Resident production relaunch path

The subsequent call chain is:

```text
ordinary desktop/icon/start-menu request
  -> desktop::launch_app(applicationId)
  -> nativeaot::launchLogicalApplication(applicationId, report)
  -> same selector mapping and same image path
  -> launchLogical/launchInternal resident branch
  -> verify the resident image path and reuse its ELF mapping
  -> reuse local storage and the attached ThreadStore adapter record
  -> GuideXosNativeAotApplicationEntry(startup context)
  -> Ready-state wrapper reinstalls the current TLS bridge
  -> skip RhInitialize, PAL/GC startup, module init, and code-manager registration
  -> ManagedMain(startup context)
  -> managed dispatcher selects the logical app
  -> return managed status to launchInternal
  -> desktop::launch_app returns to guideXOS
```

The resident branch does not map an executable again and does not detach the
current NativeAOT thread. `RhpReversePInvoke` still reaches the bridge's
`initializeRuntimeState`, but the production runtime pack performs the bounded
heap/runtime-storage initialization only on the first entry and preserves the
heap and allocation cursor on all later entries.

## Managed-thread, TLS, and heap contract

All five valid managed entries in each accepted boot ran on guideXOS thread
identity `1`. The production-visible guideXOS NativeAOT ThreadStore adapter
record was stable at `0x2C2500`, with native thread id `1`, generation `1`, and
`attached=1`. The guideXOS TLS identities were stable across A/B/A/B/A:

| Identity | Value in the accepted boot |
| --- | --- |
| guideXOS scheduler thread/task | `1` |
| ThreadStore adapter record | `0x2C2500` |
| Native thread id / attached | `1 / 1` |
| GS area | `0x382B1F0` |
| TLS vector | `0x382B0D8` |
| TLS block | `0x382B0E0` |
| TLS bridge installs | `1` initial + `5` resident |

These addresses are evidence from one boot and are not hardcoded. The C108
locked-runtime census likewise observed one `tls_CurrentThread`/`Thread*`
identity for the continuing attached managed thread; C109 preserves that
contract through the ordinary launcher.

The production heap evidence is one `initialize` record followed by six
`preserve` records. The first managed reverse-P/Invoke establishes the bounded
heap and allocation cursor; the initial launch then performs the same resident
entry transition that later logical launches use. No subsequent entry clears
the heap or rewinds the cursor.

## Managed results

The ordinary production sequence is:

```text
A1 -> B1 -> invalid rejected -> A2 -> B2 -> A3
```

Every accepted boot produced:

| Logical app | Ordinary static | `[ThreadStatic]` | Allocation |
| --- | --- | --- | --- |
| App A | `0→1`, `1→2`, `2→3` | `0→1`, `1→2`, `2→3` | `byte[8]`, sum `36`, PASS on A1/A2/A3 |
| App B | `0→1`, `1→2` | `0→1`, `1→2` | `byte[5]`, weighted result `150`, PASS on B1/B2 |

The invalid launch returned managed `-4` and native `invalid-app-id`. A2,
B2, and A3 still passed afterward. All valid dispatches returned control to
the kernel and the serial log reached the guideXOS main-loop marker.

## Safety regressions retained

Missing-image behavior is inherited from the C103/C108 negative coverage:
opening a missing image returns deterministic `not-found`, does not create or
mutate resident runtime state, and a later available same-image launch can
still proceed. C109 does not add a partial-resident state or a retry that could
reinitialize the runtime.

The independent-image boundary remains unchanged. A resident image rejects a
different image as `Busy`/`BaseCollision`; the supported model is one composite
image with logical applications, not multiple independent NativeAOT images
sharing one runtime. The C109 production runner retains this assertion in its
manifest; the independent-image negative boot itself remains the earlier C104/
C105 coverage.

## QEMU evidence and counts

Runner: `scripts/dotnet/run-c109-production-composite-lifecycle.ps1`
Evidence root: `out/dotnet/c011ec109-production-composite-lifecycle/`

The run used three fresh QEMU boots with this production configuration:

```text
ProductionComposite managed mode
PersistentCompositeLifecycle runtime-pack behavior
GXOS_NATIVEAOT_PRODUCTION_APPLICATION
GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH
single composite HostLogProof.elf staged as /apps/GXOSAPP.ELF
```

No C107/C108 launcher mode was selected. All three boots passed the strict
sequence, state, identity, heap, mapping, initialization, invalid-id, and
main-loop checks. Each boot reported:

```text
mapping=1  resident-reentries=5  TLS-installs=6
runtime=1  PAL=1  GC=1  code-manager=1  module=1
heap-initialize=1  heap-preserve=6
A1 PASS -> B1 PASS -> invalid rejected -> A2 PASS -> B2 PASS -> A3 PASS
```

The manifest also records the locked NativeAOT identity: version `9.0.0`,
AMD64, Workstation GC, `net9.0/win-x64`, source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The composite ELF SHA-256 is
`65236446A06086120C2C81341EF1376B31A7469F4B8575F96F60DCD3ADA10D6A`.

## Remaining diagnostics and change boundary

The C107/C108 modes and their historical evidence remain available for
regression comparison. They are not needed to reach the production launcher,
choose a logical app, preserve state, or return to guideXOS. The neutral
production serial records are observational only; removing their logging
must not change the lifecycle.

No NativeAOT runtime-source files were changed. No GC algorithm, GC policy,
unload, shutdown, second code-manager registration, second module
initialization, scheduler, PMM, VM, EH, unwind, or assembly-loading behavior
was introduced.

## Outcome and next phase

**Outcome A.** Ordinary guideXOS production application launch now dispatches
multiple logical managed applications through one resident composite NativeAOT
image while preserving the attached managed thread, TLS, heap, allocation
cursor, ordinary statics, `[ThreadStatic]` state, and shared runtime/GC domain.

The recommended next phase is **C110 — replace the bounded HostLogProof
logical-id sample mapping with the real guideXOS managed App Model application
records, preserving this same one-composite-image/one-resident-runtime
contract**. C110 was not started by this change.
