# C112 reusable NativeAOT managed application model

C112 extracts the user-facing managed application boundary from the first
single-purpose NativeAOT proof. `Managed Workspace`, `Managed Status`, and
`Managed Counter` are now compile-time registered logical applications. They
share one resident `/system/apps/GXOSAPP.ELF`, one bounded dispatcher, one
versioned host table, and the existing compositor/window lifecycle.

## Launch route

The production route is:

```text
Start menu / All Programs
  -> App Model record
  -> desktop::launch_app_with_context
  -> ManagedNativeAot launch target
  -> nativeaot::launchLogicalApplication
  -> resident NativeAOT composite
  -> GuideXosApplicationRegistry
  -> registered managed application
```

The records are:

| App ID | Selector | Display name | Image |
| --- | ---: | --- | --- |
| `com.guidexos.apps.managed.workspace` | 1 | Managed Workspace | `/system/apps/GXOSAPP.ELF` |
| `com.guidexos.apps.managed.status` | 2 | Managed Status | `/system/apps/GXOSAPP.ELF` |
| `com.guidexos.apps.managed.counter` | 3 | Managed Counter | `/system/apps/GXOSAPP.ELF` |

The registry is a static table with bounded linear lookup. It uses no
reflection, assembly scanning, plugin discovery, or per-launch registration.
Each application owns only its managed state and calls the shared
`GuideXosHost`/`GuideXosSurface` API.

## Stable managed API

`NativeGxAppContext` remains the 56-byte launch envelope. The launch context is
validated natively, limited to 48 bytes, and copied into a managed `byte[]`
before the application sees it. A managed application receives an immutable
`GuideXosLaunchContext`; no native pointer is exposed or retained.

`NativeHostCallTable` is version 1 and 72 bytes on amd64. The original C111
prefix remains stable; C112 appends:

```text
offset 56: uint64 capabilities
offset 64: addActionButton callback
```

The capability bits cover surface creation, text, primitives, actions, close,
launch context, and logging. Applications probe capabilities through the API
and can downgrade cleanly when an optional service is omitted. Host-version
mismatch returns the public unsupported result without entering application
code.

The action callback carries the registered selector and action ID through the
existing compositor input path. A button click therefore re-enters the
resident managed dispatcher, rather than depending on a per-application
native callback table.

## State and lifecycle proof

The C112 proof launches the three registered applications repeatedly, closes
their compositor surfaces, and activates managed-created buttons. It also
checks:

- Workspace and Counter action dispatch through actual compositor mouse input;
- managed persistent state across six logical launches;
- native Notepad regression between managed launches;
- host ABI mismatch and action-capability downgrade;
- oversized, null-with-length, zero-selector, duplicate-selector, unknown-ID,
  missing-image, and independent-image negative cases;
- one resident image/runtime mapping and the existing close/return path.

The reproducible runner is:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dotnet/run-c112-reusable-managed-app.ps1
```

It writes build inputs, hashes, descriptors, ABI notes, one serial log per
fresh boot, and `c112.manifest.json` under
`out/dotnet/c011ec112-managed-application-model/`. Use
`-SkipQemu` for build-only evidence, or `-FreshBootCount N` for a different
fresh-boot count.

The managed artifact can also be built directly through the existing build
script with `-ManagedProjectMode C112Composite`. The C112 mode uses the
production runtime-pack and persistent composite lifecycle switches; it does
not change the NativeAOT runtime or GC algorithm.

## Compatibility and scope

The old C111 entry remains available for its existing evidence runner. The new
C112 production mode routes through the reusable registry and API. The native
surface primitives and compositor close/input behavior are shared existing
services; C112 adds the versioned capability boundary and action dispatch
needed by multiple managed applications.
