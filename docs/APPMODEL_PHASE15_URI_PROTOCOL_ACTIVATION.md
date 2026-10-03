# App Model Phase 15 — URI / Protocol Handler Activation

Date: 2026-10-03

Outcome: **B — generic URI activation is in place, with NativeElf URI delivery deferred.**

## Existing URL behavior and the Phase 15 gap

Navigator already had one production URL pipeline: its address entry and internal navigation call `loadUrl`, which handles hosted HTTP, HTTPS, local `file://` pages, and Navigator-owned `about:` pages. Phase 12 had also routed local HTML files through owned document activation. Before this phase, however, an arbitrary caller could not submit a URI to DesktopService and have AppRegistry choose a capable application.

Navigator now classifies clicked links. HTTP, HTTPS, file, and about remain inside Navigator's existing navigation path. A valid link with another scheme is sent to the generic DesktopService URI entry point; AppRegistry selects a registered capable handler or returns a no-handler result. This does not register `file:` or `about:` as global protocol handlers.

## Bounded protocol model

Protocol declarations are part of `AppManifest` and are loaded from `protocols` together with `supportsProtocolActivation`. The manifest validator rejects undeclared activation support, malformed and duplicate schemes, case-equivalent duplicates, and declarations over the per-app limit. Scheme syntax is ASCII: the first byte is a letter; following bytes may be letters, digits, `+`, `-`, or `.`. Scheme identity is normalized to lowercase.

The shared limits are:

| Value | Bound |
|---|---:|
| URI activation payload | 2048 bytes |
| Protocol scheme | 32 bytes |
| Declarations per app | 8 |
| Registry protocol records | 256 |
| Retained handlers per scheme | 16 |

URI activation rejects empty values, over-bound values, NUL/control/DEL bytes, missing colons, and invalid or overlong schemes. The normalized scheme is used for lookup, while the URI itself is copied unchanged into `AppActivationContext::uri`. The 2048-byte value bound matches guideWeb's shared URL bound. This is bounded scheme validation, not a general URL parser or a URL security overhaul.

AppRegistry owns typed protocol records keyed by normalized scheme and canonical App ID, with registration owner and generation. Handler snapshots expose availability, activation support, default status, identity, and generation. Resolution rechecks the selected canonical ID, declaration, backend, owner, and generation before it returns an activation context. Reusing a temporary registration slot cannot inherit the old registration's protocol authority. Display names are presentation only; duplicate labels do not affect selection.

For HTTP and HTTPS the built-in/effective default is canonical `guidexos.navigator`. A valid durable configured default takes precedence; otherwise the built-in default is used when available, then the first available capable handler. When no handler is available, activation fails closed. The HTTP and HTTPS records are distinct, so their configured defaults are independent.

## Default persistence and Settings

`DefaultAppHandlerStore` now distinguishes extension keys from protocol keys. It continues reading v1 extension records unchanged. If no protocol override exists, serialization remains v1 and keeps the prior extension record format. When protocol overrides are present, v2 writes typed `extension:` and `protocol:` keys in one checksummed file. Loading validates normalized typed keys and preserves unrelated extension overrides. Writes retain the existing atomic replacement and read-back verification flow.

Settings Default Apps gets its protocol rows, defaults, and capable-handler choices through the same generic model/backend used for file extensions. It does not contain Navigator-specific selection logic. The interaction and model tests cover displaying protocol handlers and changing a protocol default. A synthetic alternate-handler case proves that policy and selection are generic; it is a model proof, not a second production application.

## Activation and Navigator integration

`AppActivationKind::Uri` is separate from application and document activation. `AppActivationContext` owns its URI string and carries the selected canonical ID and registration generation. `DesktopService::OpenUri` asks AppRegistry to resolve the URI; `OpenUriWithHandler` revalidates an explicit selected handler before dispatch. The built-in document dispatcher now delivers both document and URI contexts through the existing app-model launch path. The hosted Navigator consumes the owned URI via `LaunchWithActivation` and passes it to its existing `loadUrl` pipeline.

Navigator already has real HTTP and HTTPS support, so its built-in manifest declares both. A separate hosted HTTPS smoke against `https://example.com/` received HTTP 200 with TLS certificate validation for `example.com` using TLS 1.2. The App Model HTTPS smoke also delivered that exact URI to Navigator and completed a clean close. HTTP was exercised through the production App Model route against the local Navigator fixture. Both schemes remain independent declarations.

No NativeElf URI ABI was added. Developer Studio and other NativeElf apps can be represented in the registry model, but cannot be selected as available URI dispatchers until that backend has URI activation support. This is the bounded Outcome B limitation.

Navigator link clicks delegate valid schemes outside its internal `http`, `https`, `file`, and `about` set to `DesktopService::OpenUri`. Unknown protocols currently have no registered consumer and fail without launching an app. `file:///` remains Navigator-local and keeps Phase 12 path translation and root-containment behavior; it is not a global URI default.

## Runtime and model evidence

- URI/protocol model suite: **41/41** assertions; **100/100** persistence/reload cycles; **100/100** URI activation-resolution cycles; **100/100** unknown-protocol cycles.
- Protocol default persistence retained the extension defaults through all stress cycles. The existing extension-only format and prior v1 data remain readable.
- Unknown-protocol runtime smoke: **20** attempts; no process, compositor window, Recent Programs change, or registry mutation.
- HTTP App Model runtime smoke: **12/12** checks, including exact URI, canonical Navigator identity, existing `loadUrl` state, active process while its owned window is open, stopped process and closed window, Recent Programs, and clean close.
- HTTPS App Model runtime smoke: **12/12** checks against `https://example.com/`, including exact URI delivery, Navigator state/ownership, active/stopped process lifecycle, Recent Programs, and clean close.
- Settings Default Apps model: **39/39**; interaction: **9/9**; lifecycle: **100/100**. Settings Center: **89/89**.
- Existing default-handler suite: **54/54**; persistence/reload, ordinary open, and one-time open each completed 100/100 cycles.
- Navigator URL resolution smoke passed for absolute, protocol-relative, root/path/parent/dot segments, query-only, fragment-only, percent-encoded, and file-relative URLs.
- Phase 14: **27/27**, including **106/106** unsupported opens; no legacy BMP/GIF image fallback.
- Phase 13 JPEG: **20/20**; Phase 12 local Navigator: **11/11**; Phase 11 PNG: **21/21**; Phase 10 Developer Studio: **10/10**, including **4/4** NativeElf launch/close cycles.
- Phase 9 Settings Default Apps, Phase 8 extension persistence, Phase 7 Open With, Phase 6 document activation, and Phase 5B readiness regressions passed. Phase 6 included File Explorer **7/7**, Notepad **29/29**, and negative cases **3/3**. Phase 5B reported ready, zero unresolved, zero high risk.
- Startup regression passed: normal startup launched no apps, explicit canonical launches worked, and no unexpected windows persisted. Phase 4D Recent Programs regression passed and restored its temporary smoke state.

## Builds, QEMU, and visual evidence

The standard hosted build succeeded with **0 errors and 177 warning diagnostics**. The experimental hosted build succeeded with **0 errors and 176 warning diagnostics**. Review of touched Phase 15 source found **0 new Phase 15 warnings** in both builds.

The changed AppRegistry and default-handler sources are compiled by the standard and experimental hosted builds; they are not part of the bare-metal kernel build. The built-in dispatcher and DesktopService integration are hosted desktop code. QEMU was therefore not run because it would not exercise these changes. This is separate from the known, unrelated PacMan audio unresolved-symbol issue cited by the Phase 14 report.

Native Settings/Navigator windows were not visually inspected. Evidence is from Settings model/interaction tests, compositor ownership, Navigator state and logs, and runtime smoke assertions. No visual inspection is claimed.

## Next grounded consumers

At this checkout Navigator is the only production protocol handler; HTTP and HTTPS are the only registered external schemes. There is no existing mail client or other consumer that would justify registering `mailto:` or another speculative scheme. Keep `file:` in Navigator's local document path. A grounded next App Model activation target outside protocols is the existing File Explorer folder-open path, which is still separate from typed document activation. Add a new URI scheme only when an existing application can truthfully consume it.
