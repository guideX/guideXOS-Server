# NativeAOT C180 Managed Window Resize Opt-In Contract

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`\
**Branch:** `v1.1_DOTNET_SUPPORT`\
**Starting HEAD:** `df5fcfe0a97ddcd619881a8cd867608da9925bfe`

## Audit conclusion

C174 already centralizes bounds validation, the 24-pixel titlebar calculation,
candidate geometry, transaction result mapping, and rollback decisions. The
five accepted applications correctly retain their own layout, state snapshots,
rendering, and rollback work. C180 found repeated integration around those
shared facilities: the NativeAOT resize callback maintained a selector/title
allowlist, and each app decoded the C172 action and created its own C174
transaction.

C180 adds an explicit, default-deny opt-in to the existing
`GuideXosApplication` base class and a shared C172 action dispatcher. The
dispatcher asks an opted-in application for its bounds and current outer size,
constructs one C174 transaction, and invokes the app's resize handler. The
base implementation declines opt-in and returns `InvalidAction`; an
application that does not override the geometry hook cannot be resized by the
managed route. Native code sends the existing geometry action through the
existing managed action entry point for any nonzero managed selector. No new
compositor callback or ABI surface was added.

An explicit interface prototype passed hosted tests but stalled in the
NativeAOT guest when the interface bounds getter was invoked. Proof-only stage
markers isolated the stall to that getter. The final contract uses virtual
hooks on the existing application base class, consistent with the framework's
existing NativeAOT dispatch model. The successful guest evidence below uses
that final design.

## Five-application duplication matrix

| Application | Shared work | App-owned layout and state | App-owned failure behavior |
| --- | --- | --- | --- |
| Settings | C172 action decoding; C174 bounds, client geometry, transaction, and result | Recomputes its pages, groups, controls, and scroll view; retains category, focus, pending association choices, working/applied settings, and scroll offset | Restores layout, scroll, focus control/member, and surface when layout, rendering, or focus/transient-state validation fails |
| File Explorer | C172 action decoding; C174 transaction and titlebar math | Controller lays out list, details, buttons, and scrollbar; retains path, selected entry/index, scroll, and focus | Restores the previous layout and scroll while preserving path and selection |
| Notes | C172 action decoding; C174 transaction and titlebar math | C175 recalculates editor, path, footer, and control layout; retains document/path, caret and selection, undo/redo, dirty state, focus, and non-wrapping viewport | Restores the previous layout and viewport after layout, rendering, or state rejection |
| Calculator | C172 action decoding; C174 transaction and titlebar math | C178 positions its existing display and buttons; calculator state remains owned by the calculator core | Restores prior control geometry when the transaction rejects |
| Task Manager | C172 action decoding; C174 transaction and titlebar math | C179 lays out list/details/status; retains the current system snapshot, view, selected application identity, refresh generation, and scroll | Restores prior layout/scroll on rejection; resize never triggers refresh |

Before C180, each resize path decoded the same tagged action and constructed
the same transaction, while native code had to know which selector/title pairs
were resize-capable. After C180 those steps have one implementation. Each app
provides its existing bounds and current outer size, then calls its existing
layout/render/state logic with the shared transaction.

## Contract and example

The default application implementation returns `false` from
`TryGetManagedResizeGeometryC180`, which prevents an unapproved application
from entering the geometry path. A resizable application opts in by overriding
the geometry hook and the callback:

```csharp
internal override bool TryGetManagedResizeGeometryC180(
    out GuideXosManagedWindowResizeBoundsC174 bounds,
    out int outerWidth, out int outerHeight)
{
    bounds = ResizeBounds;
    outerWidth = _windowWidth;
    outerHeight = _windowHeight;
    return true;
}

internal override GuideXosResult HandleManagedResizeC180(
    GuideXosHost host,
    ref GuideXosManagedWindowResizeTransactionC174 transaction) =>
    HandleLiveGeometry(host, ref transaction);
```

The example matches the Settings integration in
`ManagedSettingsCenter.cs`. The geometry hook reports app-owned bounds and
current outer dimensions; the app callback owns layout recalculation,
rendering, state checks, and restoration. The registry retains the existing
managed application factory and three-slot lifetime pool; opting into resize
does not change registration, instance ownership, or teardown.

The shared dispatcher keeps ordinary action routing intact. It rejects an
invalid candidate before calling app layout code and emits the common
C180 proof marker for that rejection. For valid candidates, each application's
existing callback completes or rejects the C174 transaction and performs its
own rollback. The compositor retains the prior native rectangle when the
managed result is nonzero.

## Geometry and compatibility invariants

Accepted bounds, default sizes, and the shared titlebar height remain:

| Application | Default outer size | Accepted outer bounds |
| --- | ---: | ---: |
| Settings | 560×340 | 512×222 to 720×420 |
| File Explorer | 800×390 | 600×360 to 960×526 |
| Notes | 600×360 | 560×344 to 960×436 |
| Calculator | 292×270 | 284×262 to 512×420 |
| Task Manager | 800×370 | 800×344 to 960×452 |

C174 continues to subtract 24 pixels from the outer height to produce the
client height. Bounds are inclusive. One-pixel-outside requests are rejected
before app layout changes. A completed transaction rejection preserves the
original geometry and runs the application's state restoration path.

The ABI remains v4 / 128 bytes, with callback offsets 104, 112, and 120. C172
action tags and selector values, compositor interfaces, association
persistence, and `GXSETT.BIN` v2 / 26-byte format are unchanged. No new kernel
API, window manager, layout engine, lifetime policy, global mutable state,
worker, polling loop, or event subscription was added. The existing maximum of
three simultaneously resident managed applications is unchanged.

## Host regression and performance evidence

The consolidated managed UI gate passed **36 suites / 899 cases**, compared
with the accepted C179 baseline of 34 suites / 871 cases. C180 adds two suites
and 28 cases: 21 focused opt-in/geometry/rollback cases and seven stress
assertions. The consolidated run reruns the existing C172–C179 and Settings
C167/C168 suites.

C180 stress covers 1,000 shared transactions: 500 alternating accepted
geometry requests, 250 rejected requests, and 250 cross-application geometry
and state combinations. It also runs 100 application lifecycle cycles. The
measured resize path reports zero allocated bytes and zero cumulative geometry
drift. The existing C172–C179 suites separately cover focus, control layout,
Notes editing, Calculator arithmetic, Task Manager snapshot refresh and
selection, and the three-resident-app lifecycle limit.

There is no accepted wall-clock resize benchmark in the C172–C179 baseline, so
C180 makes no latency improvement claim. The shared path adds a bounded
virtual opt-in query and value-type transaction construction; it allocates no
delegates or per-resize objects. Application layout and render costs remain
application-specific.

Host evidence is under `out/dotnet/c180-managed-resize-contract/host/`.
[`managed-ui-host-manifest.json`](../../out/dotnet/c180-managed-resize-contract/host/managed-ui-host-manifest.json)
passed **36 suites / 899 cases** with SHA-256
`A763CB4544F309F3AB9883AF5A9297B759E7494F62E695EFF84C4DE43DEB689E`.
Its C180 section records 1,000 shared transactions, 500 alternating accepted
requests, 250 rejected requests, 250 cross-application combinations, 100
lifecycle cycles, zero hot-path allocations, and zero geometry drift. The
separate C166R9 prerequisite host validation passed 604 native cases, 34
managed cases, and 1,000 persistence stress transactions.

## Guest acceptance

The focused lane passed three isolated QEMU boots. The integrated manifest
[`c179-focused-proof-manifest.json`](../../out/dotnet/c180-managed-resize-contract/c179-focused-proof-manifest.json)
has SHA-256
`E114FBAFB0BF7FE7E30CACB56192F45EAD4F43B34596B7127B7372CF62674054` and
records the geometry, refresh, and concurrent scenarios. C170, C172, C173,
C178, and C179 focused manifests also passed; the C170 manifest covers all
three guest boots and the C179 manifest includes the final integrated app
state. Proof media hashes were composite
`4293057796336540BA4740B5CAB0A6DFC89A8BAF9417DB87D60EBC5DEF9D00A4`, kernel
`C3DCDD3AC0E0B000283AE433A46AC0BF8C6C9CE68C88583F340F2C7427C09CD3`, and
neutral ramdisk
`86891CB6C32D6D7AE0CC1F70E874EAE98467094418D98996389715BCEF53DB9B`.

Focused run evidence is under
`out/dotnet/c180-managed-resize-contract/settings-ui-proof-runs/20261020-021504-8a714189/`.
The three serial and QMP input hashes are:

| Boot | Serial SHA-256 | QMP input SHA-256 |
| --- | --- | --- |
| Settings proof 1 | `4A23EA1DDA38E3ADBA0A493A0833F11E94E9943520987668514B430FD4FB47FD` | `67DA4E77F3FBAFCFFBA4D7EBD852C7682E359B061BEC232BFBE00976FEF0DCDE` |
| Settings proof 2 | `ADF2249CE9E0D885B6BC2861820200D71FE964A5BB8AC0F0774797D391A7567D` | `6DB25AA6DE4AD936AD4B88D32921D91D6172B734812DB0B3BDAA2E970D173B2D` |
| Settings proof 3 | `C5A2E5D649535785BF2EDAF20370CE869B88F7FE33E938EC368975A0B9F9A9C7` | `731CB4ADF680F81B79963AAE7AACA8BAFF6E6EABE41E5111FCCE5710F7C9B19E` |

The proof-disabled production lane passed three isolated boots. The integrated
production manifest
[`c179-production-acceptance-manifest.json`](../../out/dotnet/c180-managed-resize-contract/c179-production-acceptance-manifest.json)
has SHA-256
`E54D5F239700FE631569B142ED0881AE8D28AC3884018C90C4A2051E381ABD92`. Its
final marker list includes `C180-MANAGED-GEOMETRY`; the composite, kernel, and
ramdisk string scan passed with candidate strings absent, and the three guest
serial logs contained no proof markers.

| Production candidate | SHA-256 |
| --- | --- |
| NativeAOT composite | `3E10D1276F0F1D9A4D7BC34FAAABC03677751BD182C5D41EE9B67D1091455770` |
| Kernel | `411A2D38FC3D45087219032E50AE28276E23426CE9B7911BBF7E6493C182129E` |
| Ramdisk | `714F717053CB769041963FB9D5F3938A2D6F2B5CA806D95F1F170EE93EEAEE94` |

The three production serial and QMP input hashes are:

| Boot | Scenario | Serial SHA-256 | QMP input SHA-256 |
| --- | --- | --- | --- |
| 1 | Geometry | `A53E6CF0E2256940B27D18F4253125796975B18FB1148D0742F50EBDC52996F0` | `E65318C519C1627E0DF5381EA906783A75E3283A37FF6B911EB6801517E90E50` |
| 2 | Refresh | `5F4E3DB3A5ECF944225D1BFF4C2A82B23D2D2D2177BBA7021D2085043AA4A2A6` | `1027210633547ADCD4E90233E174E6B4D9EB620CF039C0962AAA9096FED1D82D` |
| 3 | Concurrent | `760E14FE2F7C8C563B4019F3C40A2F2C978B015A3FEF67F0B702B3BB8625B904` | `606C64BEAF6E73CE11C79D23DD70075ED5E45772065FE46A1C92EA39D6BA9289` |

The protected checkout `ESP/ramdisk.img` remained at its starting SHA-256:
`AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

### Historical C166 evidence note

The workspace no longer contains the raw C166 proof serials referenced by the
accepted C179 C166A2 manifest. The production runner now records that accepted
manifest as a historical baseline reference at
`out/dotnet/c180-managed-resize-contract/c166-accepted-baseline-reference.json`
(SHA-256
`A27924203CC11719F64FA0F37E293F71BFE052A3A960CE327919FDBC5CC59AC`). It
marks `rawProofSerialsAvailable=false`; the referenced accepted manifest is
`out/dotnet/c179-managed-task-manager-resize/production-acceptance/c166a2-manifest.json`
(SHA-256
`8D813F848168426C28A597DFBA5127007DF5C7E66CF7678C2BA4027B812E0F15`). A
supplemental attempt to regenerate those legacy C166 proof logs did not
complete because the boot did not materialize its association slots in the
host side FAT directory. That attempt is preserved under
`out/dotnet/c180-managed-resize-contract/proof1-guest-runs/`; it was not used
as current C180 proof evidence. Current C180 focused and production lanes each
completed their three boots and passed.

## Outcome and next phase

**Outcome:** A. The host regression gate, three focused proof boots, and three
proof-disabled production boots passed. The historical C166 prerequisite is
linked through its accepted C179 manifest, with the unavailable raw serials
explicitly recorded above.

C181 should consider a separate managed-app slot-capacity qualification only
if a concrete product requirement needs more than three simultaneous
applications. It should keep the current resize contract and app-owned state
boundaries intact.
