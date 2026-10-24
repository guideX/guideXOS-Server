# REXX RX38 — Production Capability Matrix

## Meaning of the labels

- **Production available:** reachable in ordinary guideXOS Server builds.
- **Fixture qualified:** demonstrated using a hosted or qualification-only injected provider. This does not make the provider available in production.
- **Partial:** callable, with material semantics or production services absent.
- **Unavailable:** no runtime implementation in the current inventory.
- **Not qualified:** implementation or API exists, but a specified acceptance proof remains open.

## Runtime boundary

| Capability | Production available | Fixture qualified | Current status and boundary |
|---|---|---|---|
| Core evaluation, control flow, numeric context, strings, routines, PARSE, SIGNAL, variables | Yes | Extensive hosted qualification; core also used by native guest runs | Implemented within the documented byte, numeric, and resource bounds. No demonstrated core safety defect. |
| `ADDRESS` | `KERNEL`/default command environment through the existing shell bridge | Hosted command callbacks and controlled command fixtures | Available; this is a host command-dispatch surface, not a general operating-system environment. |
| `QUEUE`, `PUSH`, `PULL`, `PARSE PULL`, `QUEUED()` | VM-owned external stack. Empty-stack PULL has no production input reader and fails closed. | RX31 input-reader callback behavior is hosted-qualified; no injected reader was installed in a native guest. | Stack behavior is implemented. RX31 native injected-provider acceptance remains open. |
| `VALUE` | Portable active variable pool | Hosted and native RX32 checks | Implemented. CMS-specific selectors are unsupported. |
| `RANDOM` | Yes, per-VM state | Hosted properties and native guest checks | Implemented and qualified for the tested seed/range behavior. |
| DATE/TIME | Elapsed `TIME('E')` and resettable `TIME('R')`; current wall forms fail closed | Deterministic clock providers and failure/recovery cases are hosted fixtures; native guests prove fail-closed production and PIT elapsed progress | Partial: no trusted local/UTC wall interpretation, timezone, or daylight-saving policy. |
| TRACE | Command-return actions `N`, `E`, `F`, `O` | Hosted and native command-return checks | Partial: no clause/expression/label event stream, scan mode, inhibition, or interactive debugger. |
| `CHARIN` / `STREAM` | No production stream provider; `CHARIN` fails closed and `STREAM()` reports `UNAVAILABLE` | Positive provider-backed `CHARIN` and stream-state fixture passed in a qualification-only RX34 native kernel | Partial. Fixture streams are bounded and named; names are provider-resolved identities, not filesystem paths. |
| `CHAROUT`, `CHARS`, `LINEIN`, `LINEOUT`, `LINES` | No implementation/provider | No accepted implementation evidence | Unavailable. No production file, device, or network stream access is added. |
| `LINESIZE` | No production logical-column policy; current shell storage chunks at 119 bytes | RX37 hosted capability contract/model only | Unavailable. Pixel width, byte capacity, and row storage are not character columns. |
| `USERID` | No user-identity provider | None | Unavailable; no identity is fabricated or disclosed. |
| Output capability callback | Shell `read_capability` is NULL. The CLI bridge returns unsupported if the VM query is invoked; ordinary SAY does not query it. | RX37 hosted API contract passed 4,203 assertions | Not qualified natively. No production width, destination policy, or resize generation is provided. |

## Every incomplete inventory entry

The following list reconciles the normalized classifications in the latest 75-entry inventory. RX38 preserves all classifications.

### CHARIN — Partial

- **Available:** 0–3 arguments; default/named provider identity; bounded byte reads and positions; validation of empty name, numeric positions, lengths, and resource limits; up to 16 VM-owned stream-state identities. RX34 fixture covers cursor continuation, binary bytes, short read, EOF, provider error, invalid positions, and recovery.
- **Missing dependency:** No production stream provider. The ordinary Server build cannot resolve a default or named stream.
- **Normal program effect:** Calls that need a stream fail explicitly; programs that do not call CHARIN are unaffected. No arbitrary paths are opened.
- **Server/security/reliability:** Production fails closed. Provider identity is resolved by the host; VM state and read sizes are bounded. There is no general filesystem, device, or network access.
- **v0.3:** Does not block the profile when REXX host input/stream reads are excluded.
- **Milestone:** P2, only after a separate host stream-provider contract and native acceptance plan exist.

### STREAM — Partial

- **Available:** Read-only `STREAM([name][,S|D])` status/detail for provider-resolved identities; the no-provider result is `UNAVAILABLE`.
- **Missing dependency:** Production provider. Open/close/query commands, output operations, discovery, error reset, and SIGNAL ON NOTREADY delivery are absent.
- **Normal program effect:** Programs can inspect fixture-backed read state but cannot use STREAM to open or manage a production resource.
- **Server/security/reliability:** No path interpretation or resource opening; state is per-VM and bounded.
- **v0.3:** Does not block the profile; no production stream service is promised.
- **Milestone:** P2, with CHARIN under a bounded host-stream integration.

### DATE — Partial

- **Available:** Date conversion/formatting options and supported explicit input forms; current calendar operations require a trusted wall reading and fail closed in production.
- **Missing dependency:** A validated clock source with declared local/UTC interpretation, offset/provenance, accuracy, and failure semantics. No timezone or DST model is present.
- **Normal program effect:** Explicit-date conversions work within the implementation; code requesting current date or current-date-derived forms gets an explicit unavailable error.
- **Server/security/reliability:** Avoids fabricated timestamps. Applications needing dates for expiry, audit, or certificate policy must not treat the raw CMOS reading as trusted time.
- **v0.3:** Does not block the profile if current DATE operations are excluded.
- **Milestone:** P2, after a versioned OS clock/policy contract exists.

### TIME — Partial

- **Available:** `TIME('E')` monotonic elapsed time and `TIME('R')` elapsed/reset state; selected conversions and options exist. Current wall forms fail closed.
- **Missing dependency:** Same trusted wall-clock and timezone policy needed by DATE; TIME input conversion parameters are absent.
- **Normal program effect:** Elapsed timing works; code expecting current wall time must trap the unavailable result.
- **Server/security/reliability:** No false wall time is emitted. Do not use elapsed time as civil time or audit time.
- **v0.3:** Does not block the profile if current wall forms are excluded.
- **Milestone:** P2, with a shared clock-contract phase rather than a separate REXX clock implementation.

### TRACE — Partial

- **Available:** Operational command-return trace actions `N`, `E`, `F`, and `O`; status is per VM and output uses the existing writer.
- **Missing dependency:** Interpreter clause/expression/label event hooks for richer tracing; scan execution controls; a pause/resume and input lifecycle for interactive debugging.
- **Normal program effect:** Supported command-return trace output is available. Unsupported options fail explicitly; there is no full trace or interactive debugger.
- **Server/security/reliability:** No execution pause or command-inhibition capability is implied by a stored trace setting.
- **v0.3:** Does not block the profile when trace promises are limited to the tested actions.
- **Milestone:** P3 for richer noninteractive tracing; interactive debugging is a separate future project.

### CHAROUT — Not implemented

- **Available:** No CHAROUT dispatch or production write stream.
- **Missing dependency:** A separately designed, bounded host write-stream service and identity/ownership contract.
- **Normal program effect:** Programs that call CHAROUT fail; programs not using stream output are unaffected.
- **Server/security/reliability:** No stream is written, and no filesystem path is opened. This avoids an unreviewed write surface.
- **v0.3:** Does not block the profile; exclude stream output.
- **Milestone:** P2 with a future stream-provider design and explicit write restrictions.

### CHARS — Not implemented

- **Available:** No stream-character-count query.
- **Missing dependency:** A provider exposing trustworthy per-stream availability/status.
- **Normal program effect:** Scripts using CHARS fail; other scripts are unaffected.
- **Server/security/reliability:** No resource is opened or probed; no false count is returned.
- **v0.3:** Does not block the profile.
- **Milestone:** P2 with the host stream contract.

### LINEIN — Not implemented

- **Available:** No stream line reader. The distinct VM input callback is not wired into a production shell input service.
- **Missing dependency:** A safe host line source with cancellation, ownership, and VM lifetime rules.
- **Normal program effect:** LINEIN calls fail; PULL from the VM data stack remains available.
- **Server/security/reliability:** No filesystem/stdin fallback or busy wait is used.
- **v0.3:** Does not block the profile; empty-stack PULL remains fail-closed.
- **Milestone:** P2 after the Console/input lifecycle is independently designed.

### LINEOUT — Not implemented

- **Available:** No stream line writer.
- **Missing dependency:** A bounded host write-stream provider and record/flush policy.
- **Normal program effect:** Scripts writing lines to streams fail; `SAY` remains a separate shell-output path.
- **Server/security/reliability:** No arbitrary file or device writes are available.
- **v0.3:** Does not block the profile.
- **Milestone:** P2 with stream output design.

### LINES — Not implemented

- **Available:** No line-count or line-availability query.
- **Missing dependency:** A provider with line-oriented stream semantics.
- **Normal program effect:** Calls fail; no effect on scripts not querying streams.
- **Server/security/reliability:** No stream is opened or inspected.
- **v0.3:** Does not block the profile.
- **Milestone:** P2 with future line-stream support.

### LINESIZE — Not implemented

- **Available:** No `LINESIZE()` built-in. The shell has a 119-byte storage/chunk size, but that is not a logical column count. Its writer currently stores the separate SAY LF as an empty row; the renderer does not share a logical wrap policy with the VM.
- **Missing dependency:** One production destination-owned character-column policy shared by actual SAY wrapping, with destination modes and resize generations.
- **Normal program effect:** Scripts using LINESIZE fail; scripts can still emit SAY output within the current shell behavior.
- **Server/security/reliability:** No invented 80/131-column value, pixel conversion, or record-size conversion is exposed. No memory safety issue is indicated by this limitation.
- **v0.3:** Does not block the bounded profile; do not promise width-aware formatting.
- **Milestone:** P3, only if the Server product profile requires logical columns and matching wrapping.

### USERID — Not implemented

- **Available:** No REXX user-identity built-in or production identity provider.
- **Missing dependency:** A supported guideXOS identity/security model and provider.
- **Normal program effect:** Calls fail; no identity string is fabricated.
- **Server/security/reliability:** Avoids leaking or misrepresenting a user identity. guideXOS shell documentation also reports user identity as unsupported.
- **v0.3:** Does not block the profile.
- **Milestone:** P3, after the OS identity model is defined.

### REGINA_EXTENSION_SET — Not assessed

- **Available:** No Regina extension namespace is enabled or selected.
- **Missing dependency:** A declared compatibility target and a usable Regina executable for comparison. Regina was unavailable during RX38.
- **Normal program effect:** No guideXOS extension behavior is claimed; portable supported programs are unaffected.
- **Server/security/reliability:** No unreviewed extension surface is enabled.
- **v0.3:** Does not block the guideXOS profile.
- **Milestone:** P3; assess only if a later compatibility target explicitly includes Regina extensions.

### “No functions proposed” — Not implemented by design

- **Available:** This is an inventory placeholder, not a callable function.
- **Missing dependency:** None; no candidate API has been designed.
- **Normal program/Server effect:** None.
- **Security/reliability:** Keeps future OS/API work out of the classic built-in namespace until designed.
- **v0.3:** Does not block the profile.
- **Milestone:** No milestone until a concrete requirement is proposed.

## Other known limits that are not inventory gaps

- The production output-width callback has no provider and no native guest query proof. SAY does not call that query, and RX35 native output markers demonstrate the ordinary shell writer path.
- RX37's hosted formatter is a test destination; it is not the kernel shell writer.
- Byte behavior follows the target byte representation; no ASCII/EBCDIC transcoding is claimed.
- Regina differential comparison remains `NOT_RUN`; local expected-result tests are not reported as an external compatibility percentage.
