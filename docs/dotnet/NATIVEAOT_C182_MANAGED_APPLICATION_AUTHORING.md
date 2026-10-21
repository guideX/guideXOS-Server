# C182 Managed Application Authoring Contract and Starter

**Branch:** v1.1_DOTNET_SUPPORT

**Starting revision:** 9394360818b7f84d1f45dfa48d0070d0995cf9f7
**Scope:** Host-only starter sample and authoring documentation. The sample is not registered as a production application.

## Result

C182 adds a compilable, runnable example in samples/managed/ManagedStarterC182/ManagedStarterC182.cs, a hosted callback harness in the same directory, and a runner project under samples/host/ManagedStarterC182/. The example uses the existing managed framework and C180 resize opt-in. It adds no framework behavior, production descriptor, selector, ABI field, or kernel code.

Outcome: **A — Accepted**. The focused hosted runner passed 15 checks, the existing host regression gate remained at 38 suites / 930 cases and passed, and the C160 NativeAOT production profile build completed with the sample source included in the project. The source is deliberately absent from the static application registry, so the NativeAOT image is not evidence that the sample can launch in a guest.

## Current authoring workflow

The managed UI framework currently lives as source inside the HostLogProof project. There is no separately published application SDK, package, or project template. The C182 host runner references that real project and its project file links in the example and hosted harness sources.

An application author currently needs to:

1. Add a class deriving from GuideXosApplication in the managed host assembly.
2. Implement Launch(GuideXosHost) and request a surface with TryCreateSurface. Create controls and fixed-capacity routing state after a surface is available.
3. Handle GuideXosInputEvent in HandleInput, map activated controls to application-owned action IDs, and implement those IDs in HandleAction.
4. Keep application state and layout in the application instance. Redraw after accepted state changes and restore state if rendering fails.
5. Override the internal C180 geometry and resize hooks only when the app supports resizing. Recompute and validate the app's layout inside the C174 transaction.
6. Implement OnTearingDown to release references and reset state. The native close path closes the surface before the managed lifetime pool calls teardown and frees the slot.
7. To launch it in the product, add a compile-time factory descriptor using a kernel-supported selector, then make the native selector metadata and application image available through the existing packaging path. This requires kernel-aware registration and package work. C182 makes no such changes.

The managed registry contains fixed descriptors and static factory lambdas; it does not search assemblies or use reflection. The five established production selectors are Notes (4), Settings (5), Calculator (6), Task Manager (7), and File Explorer (8). Do not pick a new selector without coordinating the native dispatch and packaging side. C181's lifetime pool remains bounded at five; capacity is not a count of spare selectors.

## Example

The complete class is in samples/managed/ManagedStarterC182/ManagedStarterC182.cs. It opens a 360×200 window titled “Managed Starter”, displays an activation count, and owns a button. Primary pointer input and Tab/Enter/Space keyboard input reach the same action handler. Its resize interval is 320×180 through 720×480.

The following excerpt is copied from the compilable class:

~~~csharp
private int _activationCount;

public override GuideXosResult Launch(GuideXosHost host)
{
    if (host == null) return GuideXosResult.InvalidArgument;
    if (_surface != null) return Render();

    GuideXosResult result = host.TryCreateSurface(
        "Managed Starter"u8, DefaultOuterWidth, DefaultOuterHeight,
        out GuideXosSurface surface);
    if (result != GuideXosResult.Success || surface == null) return result;

    _surface = surface;
    _outerWidth = DefaultOuterWidth;
    _outerHeight = DefaultOuterHeight;
    _activationCount = 0;
    _status = new GuideXosLabel(16, ContentTop, 328,
        "Activations: 0", maximumTextLength: 48);
    _incrementButton = new GuideXosButton(0, 0,
        IncrementButtonWidth, IncrementButtonHeight, "Add one");
    _controls = new GuideXosControlHost(1);

    if (_controls.TryRegisterButton(IncrementButtonId, _incrementButton) !=
            GuideXosControlHostResult.Registered ||
        !TryApplyLayout(DefaultOuterWidth, DefaultOuterHeight))
    {
        return FailLaunch(GuideXosResult.InvalidArgument);
    }

    result = Render();
    return result == GuideXosResult.Success
        ? result
        : FailLaunch(result);
}

public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
{
    if (actionId != IncrementActionId) return GuideXosResult.InvalidAction;
    if (_surface == null || _status == null || _incrementButton == null)
        return GuideXosResult.SurfaceCreationFailed;

    int nextCount = _activationCount == int.MaxValue
        ? 0 : _activationCount + 1;
    if (!_status.SetText(FormatStatus(nextCount)))
        return GuideXosResult.InvalidArgument;

    int priorCount = _activationCount;
    _activationCount = nextCount;
    GuideXosResult result = Render();
    if (result == GuideXosResult.Success) return result;

    _activationCount = priorCount;
    _ = _status.SetText(FormatStatus(priorCount));
    _ = Render();
    return result;
}
~~~

The full resize handler is also in that file. It reads the already validated transaction candidate, lays out controls using the candidate client size, renders, checks its own resulting state, and calls transaction.Complete. If layout, rendering, or state validation fails, it restores the prior geometry and renders the prior layout. The sample stores its dimensions, controls, and count on the app object rather than in shared static state.

## Lifecycle and failure rules

- A failed surface request returns before app state is initialized.
- A failure after surface creation calls TryClose and then clears all app-owned references and state.
- In normal close, the native close callback owns the window close. The managed lifetime pool calls OnTearingDown and releases its entry; the sample teardown does not close an already closed window.
- The C181 pool creates at most five residents. A failed factory, full pool, failed initial launch, and stale generation are handled by the existing lifetime/dispatch code. A freed slot can be reused with a new generation.
- A sample action ID is application-local. Unknown IDs return InvalidAction; they are not new kernel action values.
- Label widths must satisfy the control's eight-pixel character-width constraint. The sample rounds its responsive label width down to that boundary.
- The managed button host provides focus and keyboard activation. The application still performs pointer hit testing against the button bounds before routing a click.
- Render failures must not leave state advanced. C182 restores both the count and label text after a failed action render.

## Resizing contract

C180 resizing is opt-in and default-deny. GuideXosApplication's C180 hooks and the C174 bounds/candidate/transaction types are internal to the framework assembly. A separate assembly cannot override these hooks through the current source surface. The C182 source is linked into HostLogProof, which is why its override compiles.

The app supplies outer-window bounds and its current outer size. The shared C172/C180 dispatcher validates an encoded geometry action and constructs the C174 transaction. The application owns control layout, rendering, and state validation; C174 supplies bounded candidate data and the shared accept/reject result. C182 uses the shared 24-pixel native titlebar height when deriving the client height.

The sample rejects out-of-range geometry before drawing. For a supported request it adjusts control positions and label width, renders, and confirms the resulting app state. A rejected render rolls its layout and dimensions back. The hosted harness verifies the window handle and app state remain stable and measures 100 alternating resize transactions at zero allocated bytes. Teardown makes the app's resize opt-in hook return false.

The C180 contract report describes the earlier three-slot pool. C181 supersedes that historical capacity with five managed lifetime slots; the ABI and C180 default-deny behavior remain unchanged.

## API visibility and NativeAOT limits

| Surface | Current visibility | Authoring implication |
|---|---|---|
| GuideXosApplication, GuideXosHost, GuideXosSurface, GuideXosLabel, GuideXosButton, GuideXosControlHost, GuideXosInputEvent | Public types in the source-integrated host project | Used by the example. There is no external package/API compatibility promise yet. |
| C180 resize hooks and C174 resize transaction types | Internal | An opt-in app must currently compile in the same assembly or the framework boundary must be deliberately changed in a later phase. |
| Compile-time descriptor registry and five-entry lifetime pool | Internal framework integration | A product app needs a static descriptor and coordinated native selector and packaging support. There is no reflection discovery. |
| Native host-call ABI | ABI v4, 128-byte context; callback offsets 104, 112, 120 | Unchanged by C182. |

NativeAOT keeps registration explicit and statically reachable. Avoid reflection-based assembly discovery, dynamic code generation, and unbounded/global registration. Prefer static factories, fixed control capacity, instance-owned state, explicit failure paths, and allocation-free hot callbacks where the underlying APIs permit them. UTF-8 literals are used for the native title boundary in the sample.

## Build and validation

Run these commands from the repository root in PowerShell. They place build output under the ignored C182 output folder:

~~~powershell
$repo = (Get-Location).Path
$artifactRoot = Join-Path $repo "out\dotnet\c182-managed-application-authoring"
dotnet run --project samples/host/ManagedStarterC182/ManagedStarterC182.csproj --configuration Release "-p:ArtifactsPath=$artifactRoot"
~~~

Expected focused result:

~~~text
C182-MANAGED-STARTER cases=15 firstFailedCase=0 resizeTransactions=100 resizeAllocationBytes=0 result=PASS
~~~

Run the established hosted managed UI regression gate:

~~~powershell
dotnet run --project samples/host/ManagedUiRegression/ManagedUiRegression.csproj --configuration Release -p:SelfContained=false -p:HostLogProofHostTest=true "-p:ArtifactsPath=$(Join-Path $artifactRoot 'regression')"
~~~

Expected result remains:

~~~text
MANAGED-UI-HOST-GATE suites=38 passed=38 failed=0 cases=930 result=PASS
~~~

The C182 NativeAOT profile build uses the repository's runtime-pack path and the existing C160 production profile. It compiles the project with the C182 source included; it does not register the C182 class or package it for guest dispatch.

~~~powershell
$runtimePackRoot = Join-Path $repo "tools\dotnet\runtime-pack"
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/build-managed-hostlog-proof.ps1 -OutputRoot (Join-Path $artifactRoot "nativeaot-c160-runtimepack") -RuntimePackRoot $runtimePackRoot -RuntimePackOutputRoot (Join-Path $artifactRoot "runtime-pack") -UseGuideXosRuntimePack -ProductionApplication -AllocationMode Allocating -ManagedProjectMode C160Composite -C155ManagedNotesSession -C156ControlModifierShortcuts -C157ManagedNotesNewDocument -C158ManagedCalculator -C161ManagedTaskManager
~~~

This build requires the configured NativeAOT toolchain, Visual Studio C++ tools, and PE-to-ELF validation tools. The accepted C182 build produced nativeaot-c160-runtimepack/artifacts/HostLogProof.elf. The linker emitted missing-PDB warnings for runtime libraries. The PE-to-ELF utility also warned that it could not find a function prologue near the exported entry and used the symbol address; its validation completed and the build script returned success.

## C182 evidence

| Evidence | Result |
|---|---|
| out/dotnet/c182-managed-application-authoring/evidence/managed-starter-host.log | Focused hosted sample, 15 checks; includes pointer/keyboard/action behavior, initialization and allocation failures, invalid geometry, resize rollback, teardown, 64 construct/close cycles, five-slot exhaustion/reuse, and stale-generation rejection. |
| out/dotnet/c182-managed-application-authoring/evidence/managed-ui-regression.log | Existing 38 suites / 930 cases passed without changing the baseline. |
| out/dotnet/c182-managed-application-authoring/evidence/nativeaot-c160-build.log | Successful NativeAOT profile build and ELF envelope validation. |
| out/dotnet/c182-managed-application-authoring/evidence/manifest.json | Source and acceptance report SHA-256 values, protected-file hash, branch/revision, test results, QAC snapshot, and guest-run decision. |

The focused C182 harness is separate from the accepted 38-suite baseline. It uses hosted callbacks against the actual managed framework; it does not start QEMU. No guest run was required because C182 changes only a non-registered host sample and its authoring documentation. C181R1 remains the cited guest acceptance for the unchanged five production apps.

## Remaining developer-experience gaps

- The framework is not distributed as an application SDK or starter template.
- The internal C180 resize hooks prevent an independently built assembly from opting into resize without source integration.
- Creating a production app still requires a coordinated static descriptor, kernel-aware selector routing, native metadata, and ramdisk packaging.
- The hosted test proves callback behavior and lifecycle rules but does not prove guest rendering, actual compositor input, or guest availability of the C182 app.
- A separate test that roots the starter in a NativeAOT sample image would be needed to assert that its methods survive trimming as native code. C182 keeps the production registry unchanged.

## Suggested C183 direction

Define a supported external authoring boundary for managed apps: a small starter project, a deliberate public resize contract, and a static registration/package workflow that reports the required selector and image metadata. Add an isolated NativeAOT retention check for the starter without consuming a production selector or changing the five-app limit. Treat any guest launch proof as a separately qualified lane.
