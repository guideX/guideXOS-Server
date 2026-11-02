# AARCH64 ABI2 Versioned Developer Studio Adapter

**Outcome:** `VERSIONED_STUDIO_ADAPTER_HOSTED_VERIFIED`
**Branch:** `AARCH64_SUPPORT`
**Audited base HEAD:** `163a28ddbf3ea48a83afb422d23e2f8b2e466836`
**Requested expected HEAD:** `85f6179e90887460e23b1874d0eddcc1ed735782` (the checked-out branch had advanced past it)
**Pinned Developer Studio:** `058c83fe1ae270a103eadc7caca961328e8abe2a`
**Pinned v0.5 SDK contract:** `f63a7710a30b49f785aac134abf06a3a6c5b5aca`

## Result

The 400-byte AARCH64 ABI remains unchanged. ARM64 packages declaring
`guidexos-c-abi-v1-studio-v05-464` now receive a separately allocated, typed
464-byte v0.5 context and host-call table. The loader selects that path from
the exact package `abi` identity before entry dispatch. No table is enlarged,
reinterpreted, or passed through a function-pointer cast.

The adapter forwards the pinned Studio's startup GUI, event, VFS workspace,
Build, and ordinary Run services to existing AARCH64 runtime services. GUI
drawing is backed by the existing per-window renderer. Document activation,
debugging, cancellation, and the separate v0.5 NativeWindow create/set/run
surface remain unavailable and are reported below. The pinned Studio source
checks these as optional capabilities; it can start and use its ordinary
Studio interface without them. This result proves compilation, linking,
hosted adapter behavior, and package staging. It does **not** prove guest
runtime loading, interactive GUI behavior, or Run/Build behavior in a guest.

The earlier audit at
[`AARCH64_ABI2_DEVELOPER_STUDIO_ADAPTATION.md`](AARCH64_ABI2_DEVELOPER_STUDIO_ADAPTATION.md)
describes the pre-renderer state and is historical. This report is authoritative
for the versioned adapter state.

## Audited inputs

| Input | Revision / identity | Evidence |
|---|---|---|
| Server base | `163a28ddbf3ea48a83afb422d23e2f8b2e466836` | Existing branch already included the committed RENDER1 work; initial request's expected SHA was not HEAD. |
| Developer Studio | `058c83fe1ae270a103eadc7caca961328e8abe2a` | Clean pinned checkout; locked activation source blobs checked by the scripts. |
| v0.5 SDK | `f63a7710a30b49f785aac134abf06a3a6c5b5aca` | Header blobs in `scripts/phase12-sdk-alignment.json`; `abi.h` SHA-256 `7fa89a1d996defa69fdeb0f03ea83b02276ce303ef3288f55e34f253756a08d7`. |
| AARCH64 SDK baseline | `1d596d4eaeb194bddeaae53f032b14989deac5df` | Locked `abi.h` blob `97f49d2492e9c3c670b170f20358cb77f002c9d2`; SHA-256 `10bf1243bf56bdf14170b24d90bc3b949f7a2ba75b6ce4b563d70e976a30f5d2`. |

The source pins and include roots are asserted before compilation. The
Developer Studio checkout is copied to the Phase 12 output tree. The build
driver copy receives test-only missing-source-list corrections and a generated
ARM64 compile overlay: it imports two namespaced Build diagnostic declarations
and aliases the source's old `window_destroy` spelling to v0.5's
same-signature `native_window_destroy`. The pinned source checkout is not
modified. The overlay does not add runtime callbacks or remove Studio code.

## ABI facts and direct incompatibility

Both contracts use 64-bit pointers and 8-byte pointer alignment on AArch64.
`GX_CALL` is empty on ARM64; callbacks use AAPCS64. The 31-field common prefix,
from offset 0 through offset 232, occupy the same byte offsets. The first
divergence is at byte 240:

| Contract | Offset 240 | Offset of bare-metal Build start | Total table size |
|---|---|---:|---:|
| AARCH64 ABI v1 | `uint32_t guiVersion` | 288 | 400 |
| Studio v0.5 | `bare_metal_build_project_start` callback | 240 | 464 |

Thus a direct v0.5 call through a 400-byte AARCH64 table reads `guiVersion`
and padding as a function pointer. The reverse interpretation is equally
invalid. The ABI1 negative control compiles the pinned activation consumer
against the AARCH64 headers and proves rejection at the absent
`get_document_activation_path` member.

### Complete host-call table matrix

Signatures below use `C` = contract-specific `gx_app_context*`, `R` =
`gx_result` (`int32_t`), `H` = `gx_handle` (`uint64_t`), and `S` = UTF-8
NUL-terminated `const char*`. All function pointers use AAPCS64. Ownership codes
are: `T` scalar; `C` live contract context; `S` call-borrowed string; `I`
call-borrowed input bytes; `O` caller-owned output pointer/record; `H` integer
handle; `L` runtime-owned table. Adapter callbacks copy strings into bounded
kernel scratch before forwarding, never retain application pointers, and
validate output objects and byte buffers against the app image/stack ranges.

The first matrix column is the exact AARCH64 slot. The second is the exact
v0.5 slot at that same offset. `Forward` means the adapter installs a v0.5
typed wrapper and calls the named AARCH64 service. `Null` means the v0.5 slot
is explicitly left null. The profile field at byte 240 in AARCH64 is data,
not a callback.

| Offset | AARCH64 field and signature | v0.5 field and signature | Ownership | Studio use / adapter disposition |
|---:|---|---|---|---|
| 0 | `size: uint32_t` | `size: uint32_t` | T | Set to each complete contract size: 400 / 464. |
| 4 | `version: uint32_t` | `version: uint32_t` | T | Both API version 0; set explicitly. |
| 8 | `log: R(C,S)` | `log: R(C,S)` | C,S | Core startup/logging. **Forward** to A log. |
| 16 | `get_api_version: uint32_t(C)` | same | C | Core startup negotiation. **Forward** to A callback; missing backend returns `UINT32_MAX`. |
| 24 | `request_window: R(C,S,int,int,H*)` | same | C,S,O | Core Studio window. **Forward** using a local A handle, publish output only on success. |
| 32 | `draw_text: R(C,H,int,int,S)` | same | C,H,S | Required immediate-mode text. **Forward** through the runtime surface renderer. |
| 40 | `draw_rect: R(C,H,int,int,int,int,uint32_t)` | same | C,H | Required immediate-mode panels/cursor. **Forward** through the runtime surface renderer. |
| 48 | `wait_for_close: R(C,H,int)` | same | C,H | Optional alternative to event polling. **Null**; Studio uses its available `poll_event` loop. |
| 56 | `poll_event: R(C,gx_event*,int)` | same | C,O | Core input/event loop. **Forward** and copy each event field; v0.5 events are limited to values 0–6. |
| 64 | `exit: R(C,R)` | same | C | Core exit. **Forward** with result-code conversion. |
| 72 | `file_read_all: R(C,S,void*,uint32_t,uint32_t*)` | same | C,S,I,O | Legacy whole-file service. **Null**; Studio uses bounded bare-metal VFS workspace calls. |
| 80 | `file_exists: R(C,S,uint32_t*)` | same | C,S,O | Legacy existence check. **Null**; no Studio dependency on this slot. |
| 88 | `request_window_ex: R(C,S,int,int,uint32_t,H*)` | same | C,S,O | Core GUI flags path. **Forward** with bounded title and local handle. |
| 96 | `file_read: R(C,S,uint64_t,void*,uint32_t,uint32_t*)` | same | C,S,I,O | Hosted/random-offset extension. **Null**; bare VFS read is separate. |
| 104 | `present_frame: R(C,H,int,int,int,int,uint32_t,uint32_t,const void*,uint32_t)` | same | C,H,I | Hosted pixel-frame extension. **Null**; Studio uses immediate-mode text/rect callbacks. |
| 112 | `get_ticks_ms: uint64_t(C)` | same | C | Optional hosted clock. **Null**; no callback is fabricated. |
| 120 | `file_stat: R(C,S,gx_file_info*)` | same | C,S,O | Hosted absolute-path filesystem. **Null**; Studio uses bare VFS stat. |
| 128 | `file_read_workspace: R(C,S,void*,uint32_t,uint32_t*)` | same | C,S,I,O | Hosted workspace filesystem. **Null**; Studio uses bare VFS read. |
| 136 | `file_list: R(C,S,gx_file_entry*,uint32_t,uint32_t*,uint32_t*)` | same | C,S,O | Hosted filesystem. **Null**; Studio uses bare VFS list. |
| 144 | `file_write_all: R(C,S,const void*,uint32_t,uint32_t*)` | same | C,S,I,O | Hosted filesystem. **Null**; Studio uses bare VFS write. |
| 152 | `file_create_directory: R(C,S)` | same | C,S | Hosted filesystem. **Null**; Studio uses bare VFS mkdir. |
| 160 | `file_remove: R(C,S)` | same | C,S | Hosted filesystem. **Null**; Studio uses bare VFS remove. |
| 168 | `build_project_start: R(C,const gx_build_request*,gx_build_handle*)` | same | C,O | Hosted Build service. **Null**; Studio uses bare-metal Build. |
| 176 | `build_project_poll: R(C,gx_build_handle,gx_build_snapshot*)` | same | C,H,O | Hosted Build service. **Null**. |
| 184 | `build_project_release: R(C,gx_build_handle)` | same | C,H | Hosted Build service. **Null**. |
| 192 | `development_run_prepare: R(C,const gx_development_run_request*,gx_development_run_handle*,gx_development_run_snapshot*)` | same | C,O | Hosted Run service. **Null**; Studio uses bare-metal Run. |
| 200 | `development_run_start: R(C,gx_development_run_handle)` | same | C,H | Hosted Run service. **Null**. |
| 208 | `development_run_poll: R(C,gx_development_run_handle,gx_development_run_snapshot*)` | same | C,H,O | Hosted Run service. **Null**. |
| 216 | `development_run_request_close: R(C,gx_development_run_handle)` | same | C,H | Hosted Run service. **Null**. |
| 224 | `development_run_release: R(C,gx_development_run_handle)` | same | C,H | Hosted Run service. **Null**. |
| 232 | `development_debug: R(C,const gx_development_debug_request*,gx_development_debug_snapshot*)` | same name/signature spelling | C,O | Debugger. **Null**. The record layouts differ substantially; this slot is not forwarded or reinterpreted. |
| 240 | `guiVersion: uint32_t` (+ 4-byte alignment padding) | `bare_metal_build_project_start: R(C,const gx_build_request*,gx_build_handle*)` | T / C,O | First incompatible slot. A scalar is not a function pointer. v0.5 Build start **forwards** to A offset 288. |
| 248 | `window_destroy: R(C,H)` | `bare_metal_build_project_poll: R(C,gx_build_handle,gx_build_snapshot*)` | C,H / C,H,O | A window destroy is used only via the v0.5 NativeWindow destroy slot at 368. **Forward** with a typed wrapper. |
| 256 | `widget_create: R(C,H,uint32_t,int32_t,int32_t,int32_t,int32_t,S,H*)` | `bare_metal_build_project_release: R(C,gx_build_handle)` | C,S,H,O / C,H | A widget slot has no v0.5 equivalent; **not exposed**. V0.5 Build release **forwards** to A offset 304. |
| 264 | `widget_set_text: R(C,H,S)` | `bare_metal_file_stat: R(C,S,gx_file_info*)` | C,H,S / C,S,O | No widget equivalent; **not exposed**. Bare stat **forwards** to A offset 312. |
| 272 | `widget_set_value: R(C,H,int32_t)` | `bare_metal_file_read_workspace: R(C,S,void*,uint32_t,uint32_t*)` | C,H / C,S,I,O | No widget equivalent; **not exposed**. Bare read **forwards** to A offset 320. |
| 280 | `wait_event: R(C,gx_event*)` | `bare_metal_file_list: R(C,S,gx_file_entry*,uint32_t,uint32_t*,uint32_t*)` | C,O / C,S,O | A blocking event wait has no v0.5 equivalent; **not exposed**. Bare list **forwards** to A offset 328. |
| 288 | `bare_metal_build_project_start: R(C,const gx_build_request*,gx_build_handle*)` | `bare_metal_file_write_all: R(C,S,const void*,uint32_t,uint32_t*)` | C,O / C,S,I,O | **Forward** v0.5 Build start to this slot using a copied request. Bare write **forwards** to A offset 336. |
| 296 | `bare_metal_build_project_poll: R(C,gx_build_handle,gx_build_snapshot*)` | `bare_metal_file_create_directory: R(C,S)` | C,H,O / C,S | **Forward** v0.5 Build poll here using field-wise snapshot translation. Bare mkdir **forwards** to A offset 344. |
| 304 | `bare_metal_build_project_release: R(C,gx_build_handle)` | `bare_metal_file_remove: R(C,S)` | C,H / C,S | **Forward** v0.5 Build release here. Bare remove **forwards** to A offset 352. |
| 312 | `bare_metal_file_stat: R(C,S,gx_file_info*)` | `bare_metal_development_run_prepare: R(C,const gx_development_run_request*,gx_development_run_handle*,gx_development_run_snapshot*)` | C,S,O / C,O | Bare stat **forwards** with field-wise record conversion. Run prepare **forwards** to A offset 360. |
| 320 | `bare_metal_file_read_workspace: R(C,S,void*,uint32_t,uint32_t*)` | `bare_metal_development_run_start: R(C,gx_development_run_handle)` | C,S,I,O / C,H | Bare read **forwards** with a 4 MiB maximum transfer. Run start **forwards** to A offset 368. |
| 328 | `bare_metal_file_list: R(C,S,gx_file_entry*,uint32_t,uint32_t*,uint32_t*)` | `bare_metal_development_run_poll: R(C,gx_development_run_handle,gx_development_run_snapshot*)` | C,S,O / C,H,O | Bare list **forwards** with a 64-entry maximum. Run poll **forwards** to A offset 376. |
| 336 | `bare_metal_file_write_all: R(C,S,const void*,uint32_t,uint32_t*)` | `bare_metal_development_run_request_close: R(C,gx_development_run_handle)` | C,S,I,O / C,H | Bare write **forwards** with a 4 MiB maximum transfer. Run close **forwards** to A offset 384. |
| 344 | `bare_metal_file_create_directory: R(C,S)` | `bare_metal_development_run_release: R(C,gx_development_run_handle)` | C,S / C,H | Bare mkdir **forwards**. Run release **forwards** to A offset 392. |
| 352 | `bare_metal_file_remove: R(C,S)` | `native_window_create: H(C,int,int,S)` | C,S / C,S | Bare remove **forwards**. NativeWindow create is a different return/ownership contract; **left null**. |
| 360 | `bare_metal_development_run_prepare: R(C,const gx_development_run_request*,gx_development_run_handle*,gx_development_run_snapshot*)` | `native_window_set_text: R(C,H,S)` | C,O / C,H,S | **Forward** ordinary Run prepare to A offset 360. No direct A operation for NativeWindow title update; **left null**. |
| 368 | `bare_metal_development_run_start: R(C,gx_development_run_handle)` | `native_window_destroy: R(C,H)` | C,H | **Forward** Run start to A offset 368. NativeWindow destroy **forwards** to A `window_destroy` at offset 248; signatures match. |
| 376 | `bare_metal_development_run_poll: R(C,gx_development_run_handle,gx_development_run_snapshot*)` | `native_window_run: R(C,H)` | C,H,O / C,H | **Forward** Run poll to A offset 376. NativeWindow run has no safe equivalent; **left null**. |
| 384 | `bare_metal_development_run_request_close: R(C,gx_development_run_handle)` | `bare_metal_development_run_cancel: R(C,gx_development_run_handle)` | C,H | **Forward** ordinary Run close to A offset 384. Run cancellation has no A slot; **left null**. |
| 392 | `bare_metal_development_run_release: R(C,gx_development_run_handle)` | `bare_metal_development_debug: R(C,const gx_development_debug_request*,gx_development_debug_snapshot*)` | C,H / C,O | **Forward** Run release. Debug has no backend; **left null**. |
| 400 | — (A table ends) | `development_debug_call_stack: R(C,const gx_development_debug_request*,gx_development_debug_call_stack*)` | C,O | No A slot and v0.5 debug records are larger/different; **left null**. |
| 408 | — | `bare_metal_development_debug_call_stack: R(C,const gx_development_debug_request*,gx_development_debug_call_stack*)` | C,O | No equivalent; **left null**. |
| 416 | — | `development_debug_inspect_variables: R(C,const gx_development_debug_request*,gx_development_debug_variables*)` | C,O | No equivalent; **left null**. |
| 424 | — | `bare_metal_development_debug_inspect_variables: R(C,const gx_development_debug_request*,gx_development_debug_variables*)` | C,O | No equivalent; **left null**. |
| 432 | — | `development_debug_evaluate_expression: R(C,const gx_development_debug_request*,gx_development_debug_expression*)` | C,O | No equivalent; **left null**. |
| 440 | — | `bare_metal_development_debug_evaluate_expression: R(C,const gx_development_debug_request*,gx_development_debug_expression*)` | C,O | No equivalent; **left null**. |
| 448 | — | `get_document_activation_path: R(C,char*,uint32_t,uint32_t*)` | C,O | Optional App Model activation. **Left null**; Studio reports activation unavailable and continues normal startup. |
| 456 | — | `bare_metal_build_project_cancel: R(C,gx_build_handle)` | C,H | No A cancellation slot; **left null**. |
| 400 / 464 | end of A table | end of v0.5 table | L | Exact sizes are compile-time asserted; adapter owns the complete v0.5 table for the app call lifetime. |

### Related records and callback arguments

| Record | Contract difference | Adapter rule |
|---|---|---|
| `gx_app_context` | Same 24-byte ARM64 prefix: size, API version, host pointer, user data pointer. Its `host` pointer targets different table types. | Create a distinct v0.5 context. Compile-time assertions check size and pointer offsets; never pass the A context as the v0.5 context. |
| `gx_event` | Same 32-byte ARM64 field layout. v0.5 defines event values 0–6; A also defines text-input/widget values 7–8. v0.5 spells payloads `int`, A spells `int32_t`. | Copy fields explicitly. A-only event kinds become v0.5 `GX_EVENT_NONE`. |
| `gx_file_info`, `gx_file_entry` | Same field layout and sizes on ARM64 (16 bytes and 144 bytes). | Copy each field. File outputs are checked for alignment and app-memory containment. |
| `gx_build_request` | Same size and field order; eight string pointers follow the size/version prefix. | Still translate field-by-field. Require a complete request/version and copy each present string into a 256-byte temporary. |
| `gx_build_snapshot` | v0.5 ends after the four incremental counters. A appends sibling-artifact and package-result fields. Both share output line capacities and the incremental counters. | Require a complete v0.5 destination, initialize an A snapshot, then copy common fields and bounded output records. A-only package tails are not written into the shorter v0.5 object. |
| `gx_development_run_request` | v0.5 API v1 ends in source-debug selection fields. A API v2 uses capability/reserved words at that tail instead. Both share artifact size/architecture/ABI metadata. | Require v0.5 v1/full size; reject debug-controlled flags or any v0.5 debug source fields. Build an A API v2 request fieldwise. |
| `gx_development_run_snapshot` | Common prefix/output ends at the same offset. A API v2 appends `capabilities/reservedV2`; v0.5 v1 appends `closeRequested/cancellationRequested/generation`. | Copy common fields/output and translate error values. The three v0.5-only observations are set to zero because A has no matching fields; zero means unavailable in this adapter, not proof that no asynchronous request existed. |
| `gx_development_debug_*` | A's later debug request/snapshot types append commands, source-breakpoint policy, call-stack, variables, output, and expression records absent from the pinned v0.5 declaration. | No debugger callback is installed. This avoids forwarding differently sized request/output objects. Studio capability checks leave its debugger controls unavailable. |
| `gx_result` | Both use signed 32-bit results, but `-9` means A `QUOTA` and v0.5 `NOT_FOUND`; v0.5 also has `NOT_MOUNTED` and `IO` values not present in A. | Translate known errors. A quota maps to v0.5 `FAILED`; v0.5 not-found/not-mounted/I/O map to A `FAILED`. Unknown negative values also map to generic failure rather than aliasing a different error. |

Build error values 0–20 are shared by the pinned contracts. The v0.5 enum has
additional detailed compiler errors 21–34; the current AARCH64 Build service
does not emit those values. If that backend adds such detail codes, the
snapshot converter must add an explicit mapping before the backend is enabled
for those values.

## Adapter boundary and runtime selection

The adapter is in the ARM64 NativeElf runtime, next to the existing loader and
GUI service. It is compiled only for Phase 12. The runtime's A table and
context remain the service owners; the adapter constructs a separate v0.5
table/context and translates calls. The assembly entry bridge receives an
opaque verified ELF entry address and opaque contract-specific context in
AArch64 `x0`/`x1`; it does not cast the entry address to an A-ABI function type.

`Apps/DeveloperStudio/app.json` keeps AMD64 on `guidexos-c-abi-v1` and selects
the explicit ARM64 identity
`guidexos-c-abi-v1-studio-v05-464`. The package loader parses `architecture`,
`entryPoint`, and `abi`, accepts the custom identity only in Phase 12, and
rejects unknown/missing identities before entry dispatch. Selection does not
infer compatibility from table size. Non-Studio AARCH64 packages retain the
existing ABI-v1 path.

The v0.5 adapter state permits one active Studio adapter at a time. Creation
requires a matching live A context/host table, a valid renderer binding, and
overflow-checked nonempty image and stack ranges. After `gx_main` returns, the
runtime deactivates callbacks, drops the render binding, waits one idle
boundary, destroys the adapter, then completes normal mapping cleanup. The
hosted tests exercise deactivation, rejection of a second simultaneous
adapter, destruction, and recreation.

## Implemented service mappings

| v0.5 service | AARCH64/runtime backend | Bounds and behavior |
|---|---|---|
| `log`, `get_api_version`, `exit` | Existing A host callbacks | String copied; result collisions translated safely. |
| `request_window`, `request_window_ex` | Existing A GUI/window callbacks | Title copied into a 256-byte local; output handle written only after success. |
| `draw_rect`, `draw_text` | Existing `gui_render` surface and per-window renderer | Requires live binding and resolved valid surface; renderer clips rectangles/text; successful draw invalidates the owning window. Invalid handles/arguments fail; graphics are never accessed before binding validation. |
| `poll_event` | Existing A event callback | Output object must be aligned and in guest image/stack; fieldwise event conversion. |
| `native_window_destroy` | A `window_destroy` callback | Same AAPCS64 signature; typed wrapper calls the A service. |
| Bare-metal file stat/read/list/write/mkdir/remove | Existing Phase 11 VFS workspace callbacks | Paths copied and bounded; read/write capped at 4 MiB; list capped at 64 entries; caller buffers/count pointers range checked; returned counts are checked before copying. |
| Bare-metal Build start/poll/release | Existing kernel VFS compiler service | Request strings copied; output objects validated; snapshot fields copied individually and output count capped at 32. |
| Bare-metal Run prepare/start/poll/request-close/release | Existing Phase 12 App Model resolver/runtime | Only ordinary, non-debug Run supported. Request/snapshot versions translated; debug-controlled requests return `UNSUPPORTED`. |

Missing backend callbacks return v0.5 `GX_ERROR_UNSUPPORTED`. Invalid or
misaligned pointers, unsupported versions, out-of-range objects, overlong
strings, and excessive lengths return `GX_ERROR_INVALID_ARGUMENT`; backend
over-reporting of copied output sizes fails rather than copying past the
bounded destination. Service errors are not replaced with success.

## Required services and known gaps

The pinned Studio calls its immediate-mode `draw_text` and `draw_rect` wrappers
throughout startup and redraw, so both are required for a usable window. The
existing runtime already had window-owned renderer surfaces, clipped drawing,
and per-window invalidation; the adapter binds to those services and adds no
second window manager or graphics stack.

The Studio startup contract requires `get_api_version` and `log`, then a
window, drawing, and event polling path. Those are mapped. The source has a
`wait_for_close` fallback, but prefers `poll_event`; the adapter provides
`poll_event`. The NativeWindow create/set/run group is not the Studio's GUI
path and remains null. Build and ordinary Run are mapped to actual kernel
services. Their broader guest functionality is not claimed until runtime
qualification.

The exact remaining gaps are:

- App Model document activation (`get_document_activation_path`): no activation
  context is owned by this NativeElf runtime. Studio logs
  `appmodel_document_activation=UNAVAILABLE` and continues.
- Debugger, call-stack, variable inspection, expression evaluation, and
  debug-controlled Run: A and v0.5 debug record layouts differ and the required
  ARM64 service is absent. All related table slots stay null; Studio detects
  and withholds those controls.
- Build cancellation and Run cancellation: A's 400-byte contract has no
  matching cancel slots. The v0.5 slots stay null.
- NativeWindow create/set/run API: no equivalent A operation with the same
  contract. These v0.5 slots stay null; Studio uses the ordinary window/draw
  API.

These are feature gaps, not failed startup dependencies. The adapter is
sufficient for the pinned Studio's core non-debug interface at the ABI boundary;
it is not a complete implementation of every optional v0.5 API.

## Safety properties

- Explicit identity selection; unknown and overlong identities fail closed.
- Separate C++ types and separate context/table storage for each ABI.
- No table casts, no cross-contract function-pointer casts, and no writes into
  neighboring ABI slots.
- Context validation compares the opaque pointer before dereferencing it;
  callbacks then verify context size, version, table pointer, and adapter owner.
- User object pointers require natural alignment and complete containment in
  the app image or stack. Range arithmetic checks overflow.
- Input strings are copied one byte at a time only while inside guest memory,
  with a 256-byte maximum. Callback storage retains no guest string or buffer.
- File transfers are limited to 4 MiB and directory results to 64 entries;
  copied byte/entry counts are checked against requested capacity.
- Build/Run requests and records are converted field-by-field. Unsupported
  trailing fields are not read as A fields or written beyond v0.5 objects.
- GUI callbacks require a live context-bound renderer binding, resolved window
  surface, and valid surface storage before drawing; successful operations
  invalidate only that window.
- Adapter deactivation precedes teardown; it is destroyed only after the app
  returns and an idle boundary passes.
- ABI1 lock checks and the direct-v0.5-against-A negative control remain
  mandatory preflight gates.

## Hosted proof and regression results

| Check | Result |
|---|---|
| Independent AARCH64 400-byte SDK layout and target contract | PASS |
| Pinned v0.5 464-byte SDK layout and AArch64 target contract | PASS |
| Pinned Developer Studio document activation ABI test | PASS, 9/9 |
| Direct v0.5 activation consumer against AARCH64 SDK | Rejected at the missing member, as required |
| Version selection, unknown/null/overlong identities, table slots, and callback safety | PASS, adapter suite 104/104 |
| GUI forwarding, clipping, guard integrity, invalid surface/pointer, and no-binding behavior | PASS in hosted adapter suite |
| File, Build, Run translation; unsupported services; error alias handling; lifecycle | PASS in hosted adapter suite |
| Phase 10 hosted ARM64/AMD64 resolver and negative controls | PASS (`AARCH64_PHASE10_HOST_CONTROLS_PASS`) |
| Phase 11 freestanding compiler/package build | PASS; checked-in ARM64 Studio client and AMD64 control emitted |
| Phase 12 freestanding ARM64 kernel foundation | PASS; `kernel.elf` is EM_AARCH64 |
| GICv3 hosted suite | PASS, 62 assertions |
| Pinned Studio hosted source suite | PASS, including model/parser, activation, startup, Build/Run, navigation/refactor, debugger-model, and conditional-breakpoint checks |
| Authentic pinned Studio ARM64 compile/link against pinned v0.5 SDK | PASS; ELF64 little-endian EM_AARCH64, 1,327,476 bytes |
| Phase 12 package/ramdisk staging | PASS; Studio ARM64 payload and explicit ABI identity staged |

Key preserved outputs:

- `out/aarch64-studio-v05-adapter-phase12-final3-20261102/`
- `out/aarch64-studio-v05-adapter-phase12-final3-20261102-preflight/`
- `out/aarch64-studio-v05-adapter-phase12-final3-20261102-adapter-tests/`
- `out/aarch64-studio-v05-adapter-host-final-20261102/`
- `out/gicv3-host-studio-adapter-20261102/`
- `out/phase11-studio-adapter-20261102/`

The compiled Studio payload SHA-256 is
`8E4F74FEEBE4B3D3F245F31560584B565FDBA5743BE3A61F0B83B4725DBDE679`.
No QEMU, physical hardware, or QAC run was performed. The Phase 10 and Phase
11 checks above are compiler/hosted controls; they do not establish guest
execution.

## Next acceptance gate

Run a separate controlled guest qualification that verifies package discovery,
adapter selection, Studio window startup/redraw/input/close, VFS operations,
and ordinary Build/Run under the guest scheduler. Track document activation,
debugger services, and cancellation as separate capabilities before enabling
their v0.5 slots. Do not claim runtime-load or GUI qualification from the
compile/link and hosted proof recorded here.

## Files changed for this result

- `Apps/DeveloperStudio/app.json`
- `kernel/arch/arm64/native_elf_call.S`
- `kernel/core/native_elf_baremetal.cpp`
- `kernel/core/native_elf_baremetal_arm64.inc`
- `kernel/core/aarch64_studio_v05_abi_adapter.cpp`
- `kernel/core/include/kernel/aarch64_studio_v05_abi_adapter.h`
- `scripts/build-aarch64-phase10.ps1`
- `scripts/build-aarch64-phase12.ps1`
- `scripts/test-aarch64-studio-v05-adapter.ps1`
- `tests/aarch64_studio_v05_abi_adapter_tests.cpp`
- `docs/aarch64/AARCH64_ABI2_VERSIONED_STUDIO_ADAPTER.md`
