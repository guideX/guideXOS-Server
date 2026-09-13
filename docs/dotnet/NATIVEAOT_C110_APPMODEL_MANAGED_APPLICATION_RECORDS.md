# C110 — App Model managed NativeAOT application records

## Result

C110 reached Outcome A. The shared guideXOS App Model can now describe two
registered managed NativeAOT logical applications, and the ordinary
desktop::launch_app(const char*) path routes those records through the
production resident composite launcher.

The accepted path is:

BuiltInAppMetadata record
-> appmodel::resolveLaunchTarget
-> desktop::launch_app
-> managed App Model route
-> nativeaot::launchLogicalApplication
-> launchLogical
-> resident /system/apps/GXOSAPP.ELF
-> GuideXosNativeAotApplicationEntry
-> managed selector dispatcher
-> managed return status.

The accepted validation used three fresh QEMU boots. All three passed.

## C109 handoff and preflight

- Repository: D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT
- Branch: v1.1_DOTNET_SUPPORT
- Starting HEAD: 8055fb434996728d9e27fa8aca069a9b3d4a50c6
- Starting subject: Integrate production NativeAOT composite application lifecycle
- Upstream: origin/v1.1_DOTNET_SUPPORT
- Starting ahead/behind: 0/0
- Starting worktree: clean
- Starting diff stat and summary: empty

C109’s authoritative production image remained the accepted managed artifact:

/system/apps/GXOSAPP.ELF

SHA-256:

65236446A06086120C2C81341EF1376B31A7469F4B8575F96F60DCD3ADA10D6A

C110 does not create one ELF per record and does not register a runtime per
record.

## Existing App Model audit

The existing architecture already had the smallest useful boundary:

- BuiltInAppMetadata is the shared static application catalog.
- Hosted synthetic registration consumes the shared catalog.
- Bare-metal AppManager registration consumes the records that advertise
  bare-metal availability.
- appmodel::resolveLaunchTarget resolves catalog identity into a typed
  LaunchTarget.
- kernel::desktop::launch_app is the ordinary bare-metal launch API.
- The Start menu and All Programs list are existing registration surfaces.
- Native records use kernelAppName and the existing AppManager factory path.

C110 extends this catalog and typed target rather than creating a parallel
managed-only registry. The two C110 records are bare-metal production records
because the production NativeAOT composite lifecycle is the freestanding
QEMU path. Hosted target resolution understands the new target kind and
metadata, but hosted execution of this freestanding NativeAOT image is outside
C110.

## Record and launch-kind design

The reused descriptor type is:

gxos::apps::BuiltInAppMetadata

The minimal added metadata is:

- BuiltInAppLaunchKind::Native
- BuiltInAppLaunchKind::ManagedNativeAot
- managedSelector
- managedCompositeImagePath

LaunchTarget gains:

- LaunchTargetType::ManagedNativeAotApp
- managedSelector
- managedImagePath

The App Model stores identity and bounded metadata only. It stores no managed
function address, runtime pointer, TLS address, thread-store pointer, GC
address, or code-manager address.

Managed records must use the shared image identity:

/system/apps/GXOSAPP.ELF

Selector 0 is invalid. Catalog validation is static and bounded. It rejects
invalid managed records, duplicate managed application IDs, duplicate
selectors, the wrong composite image, managed records carrying a native kernel
app name, and native records carrying managed selector/image metadata.

## Production records

### Managed Workspace

- guideXOS application ID: com.guidexos.apps.managed.workspace
- display name: Managed Workspace
- launch kind: ManagedNativeAot
- selector: 1
- composite image: /system/apps/GXOSAPP.ELF
- shell surface: Start menu and All Programs
- managed dispatcher identity: existing C109 App A

### Managed Status

- guideXOS application ID: com.guidexos.apps.managed.status
- display name: Managed Status
- launch kind: ManagedNativeAot
- selector: 2
- composite image: /system/apps/GXOSAPP.ELF
- shell surface: Start menu and All Programs
- managed dispatcher identity: existing C109 App B

The historical C109 App A/App B logical IDs remain compatibility aliases in
the shared metadata table, but they are not the user-visible record names.
The production launcher does not contain a selector conditional for those
historical IDs or for the C110 display names. It resolves the record and
reads the bounded selector from metadata.

## Routing behavior

For a managed record, appmodel::resolveLaunchTarget produces
ManagedNativeAotApp, clears the native dispatch name, and carries the record’s
selector and shared image identity. desktop::launch_app then dispatches the
typed managed record to nativeaot::launchLogicalApplication.

For a native record, the existing resolution and AppManager path is unchanged.
The C110 route is not a global NativeAOT route and does not alter native
application semantics.

The Start menu calls the same managed-record route before its existing native
fallback. No new UI or alternate launch API was introduced.

## Negative behavior

C110’s test-only negative descriptor is not registered in the normal catalog.
It copies a valid record and changes its selector to 0; bounded validation
rejects it without starting NativeAOT state.

The unknown application ID com.guidexos.apps.missing is rejected by the
ordinary launcher without a crash or fallback to a managed selector.

The missing image probe /system/apps/C110-MISSING.ELF returns not-found before
resident state is created.

After the valid managed sequence has established the resident GXOSAPP.ELF, a
staged independent image at /system/wall/C104A.ELF returns busy and cannot
create a second runtime. The C104/C105 resident-image boundary remains intact.

## Accepted managed sequence

Each fresh boot ran this normal App Model sequence:

Managed Workspace
-> Managed Status
-> Managed Workspace
-> Managed Status
-> Managed Workspace

The managed implementation retains the C109 evidence labels:

- App A ordinary static: 0 -> 1 -> 2 -> 3
- App B ordinary static: 0 -> 1 -> 2
- App A [ThreadStatic]: 0 -> 1 -> 2 -> 3
- App B [ThreadStatic]: 0 -> 1 -> 2

All five managed allocations passed.

## Native regression

The C110 boot sequence launches the existing native Notepad record before the
managed records:

[C110-NATIVE-REGRESSION] app=Notepad result=PASS

The launch is resolved as the existing native BuiltInApp/AppManager typed
dispatch and does not enter NativeAOT.

## Lifecycle evidence

Each accepted boot reported:

- runtime initialization: 1
- PAL initialization: 1
- GC initialization: 1
- code-manager registration: 1
- module initialization: 1
- executable mapping: 1
- heap initialization: 1
- heap preserve records: 5
- resident image reentries: 4
- TLS bridge installations: 5
- persistent identity records: 5
- one continuing guideXOS managed thread
- stable native thread store, native thread, GS area, TLS vector, and TLS
  block
- return to [KERNEL] Entering main loop (waiting for input)...

No GC algorithm, EH/unwind, runtime unload, RuntimeInstance, code-manager
registry, or process-isolation change was made.

## Build and validation

Kernel build:

mingw32-make -C kernel -B ARCH=amd64 EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C110_APPMODEL_LAUNCH

Managed composite build:

powershell -ExecutionPolicy Bypass -File scripts/dotnet/build-managed-hostlog-proof.ps1 -UseGuideXosRuntimePack -ProductionApplication -PersistentCompositeLifecycle -AllocationMode Allocating -ManagedProjectMode ProductionComposite

The accepted run intentionally staged C109’s authoritative composite image:

powershell -ExecutionPolicy Bypass -File scripts/dotnet/run-c110-appmodel-managed-records.ps1 -CompositeElfPath out/dotnet/c011ec109-production-composite-lifecycle/build/composite/artifacts/HostLogProof.elf -SkipManagedBuild -SkipKernelBuild -FreshBootCount 3

The harness stages /system/apps/GXOSAPP.ELF and /system/wall/C104A.ELF, boots
QEMU three separate times, parses serial evidence, and fails on any missing
lifecycle, routing, negative-case, static, thread-static, allocation, or
shell-return marker.

An exploratory Visual Studio guideXOSServer.vcxproj build was also attempted
but is not an acceptance gate: the existing project compiles freestanding
kernel sources with the hosted MSVC include environment and failed on baseline
include/configuration errors such as stdio.h conflicts and missing
freestanding include paths. The MinGW freestanding kernel build and QEMU
acceptance path are the production C110 gates.

## Evidence

Ignored evidence root:

out/dotnet/c011ec110-appmodel-managed-records/

Important files:

- c110.manifest.json
- inputs.json
- registered-app-descriptors.txt
- selector-mapping.txt
- commands.txt
- boot-01/serial.log
- boot-02/serial.log
- boot-03/serial.log
- boot-01/qemu.stdout.log and qemu.stderr.log
- boot-02/qemu.stdout.log and qemu.stderr.log
- boot-03/qemu.stdout.log and qemu.stderr.log
- staging/ramdisk-c110.img

Accepted serial SHA-256 values:

- boot 1: CC9186D4C58C7F2A5FDA91EA7645A41C2527832728919AB646BD079B7F000B02
- boot 2: B0C68E63C86238CB0EF426C88159A6BAEAC6A7486C93F71FB0BFE0037D8B0E0F
- boot 3: 34BB23E2F4AF1DDD286A18D80E2045CA6EE8816BB67ECBF6945AEB842A3375D4

The manifest also records the kernel, bootloader, ramdisk, runtime-pack, map,
source, and composite-image hashes.

## Outcome and next phase

Outcome: Outcome A — real App Model managed records proven.

C110 establishes the App Model boundary and the production routing contract.
The exact next phase is C111: add a small genuinely user-facing managed
application surface and user-visible behavior/arguments, and expose the same
bounded records in the hosted application catalog where appropriate. C111 must
continue to use the existing record-to-selector mapping and shared resident
image; it must not add per-app NativeAOT images or alter the resident lifecycle.
