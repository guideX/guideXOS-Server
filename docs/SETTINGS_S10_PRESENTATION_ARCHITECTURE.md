# Settings S10 presentation boundary

Date: 2026-10-11

## Architecture decision

S10 extracts the existing Settings category registry, route validation,
navigation transitions, bounded search index/query handling, and
control-to-category focus rules into one fixed-capacity C++14 presentation
core. The hosted `SettingsApplication` remains the only Settings application
and continues to own page rendering, page-specific provider snapshots,
hosted window lifecycle, focus/input dispatch, and actions. It delegates the
extracted rules to the core rather than carrying a second copy.

The core describes the existing twelve categories and searchable page
elements. It also owns presentation capability/status records and bounded
operation-request/completion transitions. It does not fetch provider data or
apply requested settings. Hosted and future native adapters supply snapshots
and act on requests through a transport-neutral value/callback contract.
Provider snapshots are copied into adapter-supplied bounded buffers with
explicit size and revision values; the core retains no runtime pointers.

## Ownership boundary

| Shared presentation core | Hosted or future runtime adapter |
|---|---|
| Stable category, target, and control identifiers | Process creation and termination |
| Route validation and category transitions | Hosted IPC and compositor/window allocation |
| Fixed-size query and search result matching | Page-specific provider calls and data snapshots |
| Focus eligibility and selected control identity | Persistence, hardware, network, and configuration writes |
| Declarative category and searchable element descriptions | Trusted identity and authorization decisions |
| Capability availability and operation presentation status | Rendering through the runtime's existing drawing API |

The small Settings-only descriptor/state contract reuses the existing page
and control vocabulary; it does not define a layout engine, widget library, or
replacement GUI framework. The hosted renderer remains authoritative for the
current appearance and detailed page behavior. A future native adapter can
consume the same route, element, focus, and capability descriptions through
`KernelApp`/`KernelCompositor` or the Native ELF event/window/drawing ABI.

The adapter contract carries no privileged Settings identity. A UI operation
request is only a request. A production runtime or service authority must
assign trusted identity and authorize every provider operation; provider
results, not local presentation state, establish success.

## Repository evidence and limits

`settings_center.cpp` is the hosted application implementation and depends on
`ProcessTable`, `ipc::Bus`, the hosted `Compositor`, `DesktopService`, hosted
provider owners, and standard-library containers. Its route/search/focus
model previously lived in `settings_center_model.h`, which includes hosted
standard-library facilities and cannot be included by the kernel target.

The bare-metal app path is `KernelApp` plus `AppManager`, with windows and
input routed through `KernelCompositor`; the Native ELF ABI separately offers
window, drawing, frame presentation, and event calls. Neither path currently
provides the complete hosted Settings provider contract or a trusted native
Settings identity. The shared core is therefore compiled as a reusable
freestanding component, but S10 does not register or launch it in the kernel.

The repository's `guideXOSServer.csproj` targets .NET 9 and invokes the
existing C++ kernel build. No Settings page/provider adapter or managed guest
runtime is present in this checkout. The separate .NET support checkout is
outside S10 and is not modified.

This boundary proves reuse of presentation state and transition rules only.
It does not claim native Settings rendering, provider integration,
authorization, launch, or QEMU acceptance.
