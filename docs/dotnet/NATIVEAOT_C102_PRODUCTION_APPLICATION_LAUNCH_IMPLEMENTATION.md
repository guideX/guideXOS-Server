# C102 — Production NativeAOT application launch implementation

## Milestone transition

- **C100:** production NativeAOT launch architecture validated; implementation was intentionally deferred while the artifact converter was unauthenticated.
- **C101:** the strict PE→ELF conversion guard was repaired by updating the stale expected converter hash. The authenticated converter is `5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621`.
- **C102:** the first actual ordinary production NativeAOT C# application launch completes through guideXOS boot, discovery, ELF mapping, PAL/VM/GC startup, managed `Main`, return, and kernel continuation.

The accepted C102 chain is:

```text
ordinary UEFI boot
  -> kernel/core/main.cpp ordinary startup
  -> /system/wall/C102.ELF discovery
  -> kernel::nativeaot::launch
  -> authenticated ELF64 AMD64 ET_EXEC mapping
  -> production PAL/VM/GC startup seam
  -> managed Main
  -> managed allocation and sum validation
  -> managed return 0
  -> launcher return status
  -> ordinary kernel main loop
```

## Reused C100 design

C102 reuses the C100 conclusions. The ordinary application model remains in `kernel/core/main.cpp` and the existing VFS/application infrastructure. C102 adds the narrow executable seam that C100 identified as missing; it does not promote the hosted generic ELF executor and it does not use `nativeaot_pal_qemu_test` as a launcher.

The production interface is:

```cpp
kernel::nativeaot::LaunchStatus
kernel::nativeaot::launch(const char* path,
                           kernel::nativeaot::LaunchReport* report);
```

The ordinary caller supplies a VFS path and receives a structured result containing loader status, entry point, mapped span, segment count, managed return value, managed marker observations, and before/after frame counts. Missing or malformed artifacts are rejected without entering the runtime.

The initial application identity is `/system/wall/C102.ELF`. The staging build places the authenticated ELF in the existing wallpaper/FAT32 pack; ordinary boot mounts that pack at `/system`, so discovery is through the real VFS path rather than an embedded proof payload.

## Loader and mapping

`kernel/core/nativeaot_application.cpp` is the sole production NativeAOT executable loader. It validates:

- ELF64, little-endian, AMD64, `ET_EXEC`.
- bounded program-header and file ranges.
- seven aligned `PT_LOAD` segments.
- `p_filesz <= p_memsz`, including writable BSS zero-fill.
- non-W+X permissions and executable entry-point containment.
- the authenticated high image base `0x100000000` and image span.

The loader reserves and commits through the ordinary `AddressSpace`, `VmRegion`, `PageTable`, and physical-frame allocator path. There is no NativeAOT-private physical pool. The high image base avoids the low identity-mapped UEFI/kernel range while remaining inside the existing production virtual-memory range.

The C99 allocator remains firmware-map-derived. The C102 boot evidence reports `totalTracked=0x306C3` (198,339) frames, which is above `0x1000`; production ELF pages consume normal `VmRegion` and `PageTable` ownership. The launch report requires `free + allocated = totalTracked` and an owner residual of zero.

## Production runtime startup seam

The application-owned `GuideXosNativeAotApplicationEntry` bootstrap is linked into the managed artifact. It performs the minimum shared runtime startup contract:

1. install PAL hooks and the startup host callback table;
2. initialize the NativeAOT module/code-manager state;
3. initialize the GC and startup platform hooks;
4. initialize runtime/module state and finalization support;
5. transfer control to the authentic NativeAOT entry contract;
6. invoke managed `Main` and return its status.

The shared PAL/GC startup code is proof-neutral. The production path does not require `nativeaot_pal_qemu_test`, `qemu_test` orchestration, diagnostic startup probes, or the diagnostic GCHelpers substitution. Dormant proof code remains available for its existing tests, but it is not on the ordinary C102 launch call path.

The authenticated managed map contains `ManagedMain`, `RhpNewArray`, `PalInit`, `PalStartFinalizerThread`, `RhInitialize`, `InitializeModules`, `FindMethodInfo`, and `RhRegisterOSModule`. These are artifact/source evidence in addition to the boot markers.

## Managed application

The candidate is `samples/managed/HostLogProof`. In the C102 production branch its `Main`:

1. emits `C102-MANAGED-ENTRY` through the ordinary PAL/host output callback;
2. allocates a managed `byte[8]` array;
3. fills it with `1..8` and validates first element, last element, and deterministic sum `36`;
4. emits `C102-MANAGED-PASS` only when the allocation and calculation are valid;
5. returns `0`.

The accepted serial evidence is:

```text
[C102-MANAGED-OUTPUT] C102-MANAGED-ENTRY
[C102-MANAGED-OUTPUT] C102-MANAGED-PASS
[C102-MANAGED-STATUS] entry=00000001 pass=00000001
[C102-LAUNCH-RETURN] status=00000000
[C102-LAUNCH] result=success managedReturn=00000000
[KERNEL] Entering main loop (waiting for input)...
```

## Lifecycle classification

C102 intentionally implements one launch per ordinary boot. The current application image, page tables, runtime state, and mapped artifact remain live after `Main` returns because the existing application lifecycle has no production process teardown/relaunch contract. This is classified as **CURRENTLY_PERSISTENT_BY_DESIGN**, not a leak: the frame and owner residuals reconcile, and the VM/PAL regression suite reports no release leaks.

Repeat launch was not forced. The next smallest milestone is C103 production application teardown/relaunch, including explicit ownership of the persistent image and runtime context. Command-line arguments, packaging/install, and managed filesystem access should follow that lifecycle work.

## Negative behavior

The bounded negative boot requests `/system/wall/MISSING.ELF`. The VFS reports not-found, the launcher emits `result=not-found`, and the ordinary kernel reaches its main loop. No ELF mapping, runtime startup, managed marker, fail-fast, page fault, or frame-accounting corruption occurs.

## Evidence

The complete C102 evidence root is:

`out/dotnet/c011ec102-production-nativeaot-application-launch/`

It is divided into C100 architecture import, C101 trusted artifact identity, application build, production staging, loader/mapping, runtime startup, managed execution, lifecycle, negative case, validation, and final classification. The final production ELF and PE hashes, runtime-pack manifest, converter identity, QEMU serial captures, C99 matrix, and ordinary artifact restoration checks are retained there.

## Scope boundaries

C102 does not compile C# inside guideXOS and does not expand BCL compatibility. It preserves the C18/runtime fundamentals, Workstation GC identity, authentic code manager and `FindMethodInfo`, root scanning, mark closure, NativeAOT 9.0.0 AMD64 identity, C99 PMM scaling, and the ordinary kernel/ESP artifacts. B02 remains `STILL_PREMATURE`.
