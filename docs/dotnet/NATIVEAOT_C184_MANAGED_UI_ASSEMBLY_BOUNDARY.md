# NativeAOT C184 — Managed UI Assembly Boundary

**Classification:** Outcome C — extraction deferred after Architecture Gate A
**Repository:** D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT
**Branch:** v1.1_DOTNET_SUPPORT
**Starting HEAD:** 9dff0c98de50406b7f0270416f363bcae52dba96
**Ending HEAD:** recorded with the C184 documentation commit

## Decision

C184 does not create GuideXos.ManagedUi. The dependency audit found no
bounded source-moving closure that can provide the requested application
contract, real host and surface operations, existing controls, public resize
opt-in, and teardown while keeping the NativeAOT host and product applications
behind a one-way assembly boundary.

The public controls are reusable components, but their rendering and input
paths depend on GuideXosSurface, GuideXosHost, control ownership containers,
and a shared control router. Those host and surface classes bind directly to
the native callback table and ABI structures. The application base also owns
the internal C180 override points, while its source file contains the static
production registry. Extracting only controls would leave an external
application unable to create and render a titled native window or enter the
existing resize transaction. Moving the base and those services would require
an application/host/surface contract and adapters, including a public resize
boundary that preserves C174 transaction semantics. That is an API and
composition redesign, not a narrow source ownership move.

The existing runtime, production registry, selectors, native sources, and
protected checkout artifacts were left unchanged. This is an architectural
deferral, not an environmental block. C185 can take up the staged separation
plan below after the active QAC reservation is released.

## Preflight and preserved state

| Check | Result |
|---|---|
| Repository | D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT |
| Branch and starting HEAD | v1.1_DOTNET_SUPPORT at 9dff0c98de50406b7f0270416f363bcae52dba96; both matched the request |
| Starting worktree | Only ESP/ramdisk.img modified and the three named obj/ / c182-host-smoke/ directories untracked |
| Protected ramdisk | SHA-256 AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8, matching the requested protected hash |
| Protected paths | All four protected paths remained present; no clean, stash, reset, or deletion was used |
| Prior reports | C180, C181R1, C182, and C183 reports reviewed |
| .phase and prompt guard | No repository .phase file or AGENTS.md exists. Earlier C179 documentation describes stale-prompt protection as checking source HEAD and evidence hashes; the requested starting HEAD matched before work |
| Host project compile set | HostLogProof.csproj is net9.0, win-x64, OutputType=Exe, and PublishAot=true by default. MSBuild resolved 144 C# compile items in the C160 host-test configuration: 142 files under HostLogProof plus the two explicit C182 Compile Include links |
| Project references | ManagedStarterC182 and ManagedUiRegression hosted runners reference HostLogProof.csproj. HostLogProof has no reusable framework project reference |
| SDK and runtime | .NET SDK 10.0.402; .NET 9.0.21 runtime is installed and the net9.0 external probe built |
| NativeAOT and ELF tooling | The NativeAOT host build script, cached Microsoft.DotNet.ILCompiler 9.0.0 package, Visual Studio 18 vcvars64.bat, Python, readelf, objdump, and tools/dotnet/pe_to_elf_v2_fixed_base.py are available |
| Storage | D: had about 15.2 GB free; E: had about 6.8 TB free |
| QAC and QEMU | QAC 2.0.0 was online. A read-only census showed one running verified guideXOSServer acceptance reservation and its managed QEMU child PID 25820; the reservation belongs to another active run and was left untouched. No C184 guest was started |

The two pre-existing hosted output directories and
samples/managed/HostLogProof/c182-host-smoke/ were preserved. Build output
created for the C183 probes was placed under
out/dotnet/c184-managed-ui-boundary/preflight/.

## Dependency graph

The current composition has one executable project as both application root
and owner of every managed framework implementation:

~~~mermaid
flowchart TB
    N[Native host selectors 4–8 and ABI v4] --> P[Program.cs / NativeAOT entry]
    P --> R[GuideXosApplicationRegistry]
    R --> H[GuideXosHost and native callback services]
    R --> L[Five-slot managed lifetime pool]
    R --> D[C172/C180 action and resize dispatch]
    D --> A[GuideXosApplication base]
    A --> S[GuideXosSurface]
    S --> H
    A --> C[Controls, input routing, layout and rendering]
    C --> S
    R --> Apps[Notes, Settings, Calculator, Task Manager, File Explorer]
    Apps --> A
    Apps --> C
    Apps --> H
    T[C182 hosted runner] --> P
    T --> Starter[C182 starter source]
    X[External application project] -. would need library .-> M[GuideXos.ManagedUi]
    M -. cannot depend on executable host .-> H
    P -. needs a direct static factory reference .-> X
~~~

| Area | Current owner and dependencies | Boundary assessment |
|---|---|---|
| Application lifecycle | GuideXosApplication in GuideXosApplication.cs exposes Launch, HandleAction, HandleInput, and OnTearingDown. Its callbacks take concrete GuideXosHost. The same file also defines the descriptor and GuideXosApplicationRegistry. | The lifecycle base can be separated only after host and resize contracts no longer name host-owned implementation types. The registry should stay in the NativeAOT composition root. |
| Host services and native entry | Program.cs, NativeAbi.cs, and GuideXosHost.cs handle the native entry, ABI v4 context, callback table, capabilities, window requests, file services, and snapshots. GuideXosHost stores native context/table pointers and checks fixed callback offsets. | Host-owned. Raw pointers and callback-table details cannot enter an app-facing assembly. An interface adapter is absent today. |
| Window surface and rendering | GuideXosSurface.cs holds a concrete GuideXosHost and calls its native table for text, primitives, and drawing operations. GuideXosHost.TryCreateSurface owns native window creation and surface construction. | Host-owned native surface construction and a framework-facing drawing surface need a one-way interface. Simply moving GuideXosSurface makes the library depend on the executable's native types. |
| Controls and layout | Public controls include GuideXosButton, GuideXosLabel, and the wider control set. Button and label store references to panel, scroll-view, group-box, and vertical-stack owners; both render through GuideXosSurface. The 65 KB GuideXosControlHost routes input across the supported control family. | Candidate reusable behavior exists, but the current dependency closure for the starter's rendered button, label, pointer input, and keyboard routing crosses concrete host, surface, container, and shared router types. No standalone slice presently satisfies the sample contract. |
| Input and action dispatch | The application base handles actions and input. C172 decodes resize actions; C180 dispatch checks the base's internal opt-in hooks before calling the app. Controls and the control host implement their own focus and event behavior. | An app-facing input/event contract and dispatch adapter must be designed together with the host and resize boundary. |
| C174/C180 resize | GuideXosManagedWindowResizeC174.cs contains internal bounds, candidate, failure, and transaction value types. GuideXosManagedWindowResizeC180.cs owns internal dispatch. GuideXosApplication has internal virtual opt-in and resize callbacks. Apps own layout, snapshots, render validation, and rollback. | Keep candidate validation and transaction coordination shared. A public contract needs safe value types or callbacks and an adapter that keeps app layout and snapshots app-owned. Exposing the current internal types directly is not required and would widen implementation surface. |
| Registry and factory | The production descriptor table is private and statically initialized in GuideXosApplicationRegistry; production factories are compile-time delegates for selectors 4–8. There is no external Register API or duplicate-identity check. | Host-owned static composition. A separate composition root can use a private explicit table without changing production selectors. |
| Lifetime and generation | GuideXosManagedApplicationLifetime is internal, has capacity five, and owns construction, teardown, reuse, and generation-safe dispatch. The production registry owns a static lifetime instance. | Host-owned. It must continue rejecting stale generations and remain the owner of instance teardown and slot reuse. |
| Product applications | Notes, Settings, Calculator, Task Manager, and File Explorer are in Applications/. Their layout, state snapshots, persistence, and app-specific rollback are application-owned. C182 starter source and its host tests are explicitly compiled into HostLogProof. | Keep product behavior and state in each application. Do not source-include a separate app merely to claim an assembly boundary. |
| Regression code | Most of the 119 GuideXos files are implementation and host tests compiled together. Existing tests share internal access by being in the executable project. | Moving implementations requires preserving test access or moving tests to a deliberate test assembly. This is another project-boundary change to account for in the extraction plan. |

### Gate A finding

The sample's minimum useful lifecycle needs all of the following to cross the
assembly boundary: application callbacks, host window creation, drawing
surface, label/button rendering, pointer and keyboard routing, C180 opt-in,
and teardown. These currently cross the dependency chain above. The new
library cannot reference HostLogProof; leaving the concrete host and
surface in that executable therefore leaves the external app without the
required types. Moving them as-is brings native ABI structures into the
library. Replacing those dependencies with interfaces and adapting the
existing application base and resize router is the required next design step.

**Gate A fails:** that interface inversion and production project composition
change is larger than a safe initial extraction. C184 stops before moving
source or changing runtime code.

## C183 probe reproduction

The public-surface probe was rebuilt as a separate net9.0 project. It
references the HostLogProof project and succeeds in deriving from the public
GuideXosApplication and constructing a GuideXosButton. It still depends
on HostLogProof's conditional build properties and does not create or render
a window, register a factory, or exercise input.

A second separate project attempted the C180 override. It failed as expected
with CS0122 because GuideXosManagedWindowResizeBoundsC174 is internal. This
confirms the C183 boundary finding on the current source.

The first compile emitted one CS0162 warning at the untouched
HostLogProof/Program.cs:2629; the external app probe itself had no warning.
This warning was observed during preflight and is not attributed to C184
source changes.

Build commands used from the repository root:

~~~powershell
dotnet build out/dotnet/c183-architecture-audit/external-reference-probe/ExternalReferenceProbe.csproj -c Release -p:ArtifactsPath=out/dotnet/c184-managed-ui-boundary/preflight
dotnet build out/dotnet/c184-managed-ui-boundary/preflight/external-resize-probe/ExternalResizeProbe.csproj -c Release -p:ArtifactsPath=out/dotnet/c184-managed-ui-boundary/preflight
~~~

The resize command is an expected compile failure. Its source and project are
retained alongside its log under the C184 preflight artifact directory.

## Existing authoring and registration contract

The current public C# types permit a narrow project to compile against
HostLogProof, but this is not a reusable framework assembly. The C182 starter
is source-linked into HostLogProof.csproj; its hosted runner also references
that project. The current probe does not satisfy independent application
authoring or registration.

GuideXosApplicationDescriptor can describe an instance and, for selected
build profiles, a factory delegate. The registry still owns a private,
compile-time descriptor array and fixed production selectors:

| Selector | Production application |
|---:|---|
| 4 | Notes |
| 5 | Settings |
| 6 | Calculator |
| 7 | Task Manager |
| 8 | File Explorer |

There is no public application-factory interface, registration method,
isolated static composition, or duplicate-registration diagnostic. A valid
future factory boundary should have a stable logical identity, an explicit
constructor/factory call, controlled initialization and error reporting, and
single-owner teardown. It should be called directly by an isolated
composition root so NativeAOT can see the constructor and handlers. The
production table and native selectors remain unchanged; the starter gets no
production selector.

The existing lifetime pool remains bounded to five and generation-safe. C180
remains default-deny, and each opted-in production app retains ownership of
its resize layout and rollback. C174 bounds are inclusive, the titlebar
calculation is shared, and the transaction uses value types. The public
contract design must preserve those decisions and zero resize hot-path
allocations without exposing native callback structures.

## Framework scope and API status

**Extracted scope: none.** No framework assembly or new public API is
created. The current public controls and callbacks remain members of the
HostLogProof assembly. The external app probe is an audit probe only.

A bounded future authoring API should expose only:

- An application lifecycle contract for initialization, input/action
  handling, redraw requests, and teardown.
- A host-services interface for titled window creation and the supported
  drawing/surface operations.
- The supported input event and basic controls needed by the app.
- A public, default-deny resize opt-in with explicit bounds and a validated
  geometry proposal. The host adapter continues to run the shared C174
  validation and transaction path.
- A factory contract with application identity, construction, initialization
  failure reporting, and one clear lifetime owner.

Host-owned items should remain private: NativeAbi structures, raw native
context pointers, callback offsets, native entry symbols, production selector
tables, proof-only services, lifetime slot/generation storage, and internal
C174 bookkeeping. Application-owned state, layout calculation, and resize
snapshots remain with each app.

## NativeAOT, integration, and packaging status

No C184 NativeAOT composition or retention proof was built because Gate A
failed. The prior C182 NativeAOT artifact compiled the starter source into
HostLogProof but did not register it. It is not evidence that a separately
compiled factory, constructor, input handler, resize callback, or teardown
handler survives trimming.

The existing production build uses
scripts/dotnet/build-managed-hostlog-proof.ps1, then converts the NativeAOT
PE with tools/dotnet/pe_to_elf_v2_fixed_base.py and inspects the ELF. A
future isolated integration must statically reference both the framework
assembly and separately compiled app assembly, call the factory from a fixed
composition table, exercise the handlers, and retain concrete link-map or
ELF evidence. Reflection, assembly discovery, runtime loading, trimming
suppression, and a standalone application ELF are outside this design.

The current packaging path stages one statically composed image as
/apps/GXOSAPP.ELF. It is not an installer for arbitrary managed assemblies.
No dynamic loader, managed plugin runtime, or independent executable
deployment model is supported or claimed.

## Regression, guest, and compatibility status

The accepted C183 host baseline remains **38 suites / 930 cases PASS**.
The C182 source-integrated starter baseline remains **15 hosted checks**;
its accepted evidence also records 100 resize transactions and zero allocated
bytes on that test path. C180 previously recorded 36 suites / 899 cases and
its resize stress measured zero allocations and zero geometry drift. These
are prior accepted results, not new C184 runtime test results.

C184 ran the two compile probes only. It did not rerun the 38/930 regression
or the C182 15-check host suite because no implementation or production
composition change was accepted past Gate A. No C184 resize stress, isolated
sample lifecycle, sample AOT retention test, or sample guest claim is made.

This deferral changed no production framework composition, NativeAOT
executable, kernel, or media, so no new guest qualification is applicable.
Prior C181R1 three-boot focused and three-boot proof-disabled production
acceptance remains historical evidence. The current QAC census had one
verified managed acceptance reservation, so no C184 guest work was attempted.
QAC was queried read-only and not modified.

ABI v4 remains 128 bytes; callback offsets 104, 112, and 120 are unchanged.
C174 transaction behavior, C180 default-deny opt-in, five-instance capacity,
generation-safe slot reuse, all five production selectors and applications,
Notes line-ending/save behavior, Settings persistence, and Task Manager
refresh behavior were not changed. No freestanding source or kernel code was
touched.

No extracted framework performance or binary-size comparison exists. The
only applicable historical performance observation is the C182 hosted
100-transaction resize result of zero allocated bytes. It does not establish
performance for a separate assembly.

## Artifacts and hashes

The C184 probe artifacts are under
out/dotnet/c184-managed-ui-boundary/preflight/:

| Artifact | SHA-256 |
|---|---|
| c183-external-reference-probe.log | 89E382A36C5BF1ED5FB57425CC76F2367F5D56E3D70021C9848BA6D17C2B7064 |
| c183-external-resize-probe.log | 0CB83BE9EE5EC6127A407CBE047989EF9383C829D3A2F14224A17F369880F1C5 |
| bin/ExternalReferenceProbe/release/ExternalReferenceProbe.dll | B4EDA31649E1617595E81F960E12FD7578FF864CF06F537C841FE01513D5F365 |
| bin/HostLogProof/release_win-x64/HostLogProof.dll | CEE98120C47EDB26C577F1003CA86517BA62ABFC360D77E003F8F7C04E72A648 |
| external-resize-probe/ExternalResizeApp.cs | A5D3C8C0DC847F368995FAF2DB3F018DFB70B9826FF3A80D783D761E26EC0DDD |
| external-resize-probe/ExternalResizeProbe.csproj | 2CE7D5A0D2A21932A0563E2E7176C68083B6004ADE9E73BE41F3F4FEC8F272C0 |

The original C183 probe source and project are unchanged; their hashes remain
A4D825D1BC7EBF865C0B2F8C8A85E35210A7F8A5644C7CCE135C21BA84FB01C1 and
565803F2B7CE4B9B916CA830F96BEAF943325C0F537A8751256B21BE46A4AA47. The
protected checkout ramdisk remains at
AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8.

No generated production image was created or committed.

## Recommended C185 direction

1. Define framework-facing application, host-service, surface, input, and
   resize contracts in a separate assembly. Keep native ABI types and
   callback-table operations in the host executable.
2. Add an adapter from the existing GuideXosHost and GuideXosSurface to
   those contracts. Make the current application base adapt to the new
   lifecycle contract so existing product apps can retain their semantics
   during the transition.
3. Move source ownership for one coherent, minimal supported UI slice into
   GuideXos.ManagedUi; do not duplicate button, label, or resize
   implementations. Keep the production registry and selectors 4–8 fixed.
4. Create a separate starter project and an isolated host composition with an
   explicit factory table. Exercise missing and duplicate registration,
   initialization and surface failures, teardown, stale generations, and
   five-slot exhaustion/reuse.
5. Prove NativeAOT retention through that direct static path and retain
   factory, constructor, input, resize, and teardown reachability evidence.
   Run the existing hosted regression and stress gates.
6. If production composition changes, reserve QAC after the active lease is
   released and run focused guest qualification for the framework and all
   five managed applications. If the production NativeAOT executable or
   kernel integration changes, also run proof-disabled production
   qualification.

C185 should continue this bounded assembly-boundary work. It should not add a
dynamic loader, plugin runtime, sixth production application, or standalone
managed deployment format.
