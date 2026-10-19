# NativeAOT C178 — Managed Calculator Resize Qualification

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `bd243e1179a0621d28f2d1bc2f747ae356fea08a`
**Outcome:** **A — Accepted**. Focused and proof-disabled production guest
qualification passed on the starting source HEAD.

## Architecture finding

Calculator is already a managed C# NativeAOT application. The C158 application
is registered as `com.guidexos.apps.managed.calculator`, selector 6, and already
owns its display, 18 buttons, integer arithmetic state, keyboard handling, and
pointer handling. A managed resize implementation therefore applies directly;
no port or application-model change is needed.

The compositor already sends resize actions through the NativeAOT managed
action dispatcher. C178 adds a narrowly matched selector-6 / `Managed
Calculator` route to the existing resize callback. It adds no callback, ABI
field, settings format change, global focus state, or alternate resize path.
The C178 geometry serial record is compiled only for proof builds.

The shared C174 bounds, candidate, transaction, result mapping, and rollback
facilities are reused. Calculator still computes its own layout and verifies
its own state after rendering. The native titlebar remains 24 pixels.

## Geometry and layout

The existing default remains **292×270 outer pixels**. C178 accepts an
inclusive interval of **284×262 through 512×420**. The minimum keeps the 240
pixel display and all four button columns visible; 18 controls remain
registered, with 64×28 buttons and a 200×28 Equals button. The app centers its
existing layout within the candidate client area and moves the display and
buttons together. The upper bound leaves room for comfortable spacing without
changing control ownership or order.

C174 derives client geometry by subtracting the native 24 pixel titlebar from
the outer height. Invalid dimensions, layout failure, render failure, or state
failure use the shared transaction result and restore the prior app layout.
No-op geometry returns without changing the calculator state.

## State and existing behavior

The resize snapshot compares the left operand, unsigned entry magnitude,
pending operator, calculator phase, entry length, sign, and error. It also
checks the display value, focused control, command count, registration count,
window handle, modal/capture state, and that every control and the display fit
the client area. A resize only recomputes positions and renders; it does not
dispatch a calculator command.

The existing calculator uses signed 64 bit integer arithmetic and has no
decimal-entry state or decimal control. C178 adds no decimal feature. Existing
clear, overflow, divide-by-zero, keyboard, pointer, and focus behavior remains
in the C158 implementation. Repeated Equals is an existing no-op that retains
the displayed result; C178 leaves that behavior unchanged. Arithmetic checks
include `7 × 8 = 56` and pending-operation resize cases.

## Host regression results

The full managed UI host regression preserves the accepted **30 suites / 823
cases** baseline. C178 adds 2 suites and 23 cases, for **32 suites / 846 cases,
all passing**. The new suites cover 18 Calculator resize cases and five
deterministic stress checks for geometry boundaries, rejected geometry,
rollback, hit testing, focus/state preservation, alternating sizes, and
lifecycle.

| C178 stress measure | Required | Result |
| --- | ---: | ---: |
| Layout updates | 1,000 | 1,000 |
| Alternating resize requests | 500 | 500 |
| Arithmetic and resize cycles | 250 | 250 |
| Application lifecycles | 100 | 100 |
| Resize hot-path allocation | 0 bytes | 0 bytes |
| Cumulative geometry drift | 0 | 0 |

The host manifest and suite log are under
`out/dotnet/c178-managed-calculator-resize/host/`.

## Focused and production guest evidence

The focused three-boot run `20261019-044144-73287dd3` passed geometry,
arithmetic, and concurrent-app scenarios. It checked exact outer/client
dimensions, both limits and rejected geometry, button hit testing, pending
arithmetic, Calculator/Settings/Explorer/Notes surface identity, and retained
application state. Its manifest is
`out/dotnet/c178-managed-calculator-resize/c178-focused-proof-manifest.json`
(SHA-256
`4A80AF8C6BD1A3EDE559BDB640E72B948B7C118FFB1D786E50D9660886965DC4`). The
focused host manifest SHA-256 is
`1A5A745B37FECA85115C58698BA55DF0DC24BC3A3930298D2A64C5F3C1104618`.

The final proof-disabled production run
`ordinary-run-20261019-054951-e3d1a887` passed all three ordinary boots:

| Boot | Scenario | Result |
| --- | --- | --- |
| 1 | Calculator geometry, limits, rejection, button hit test, reset, close | PASS |
| 2 | Pending `7×8` across resize, result `56`; `9+4` after resize, result `13` | PASS |
| 3 | Calculator resize with resident Explorer and Notes; stable identities; Settings capacity close and restoration | PASS |

The production manifest is
`out/dotnet/c178-managed-calculator-resize/production-acceptance/c178-production-acceptance-manifest.json`
(SHA-256
`723BD005C709079FE284E9A03C753EFB145CB8BD457FA3B50ED4C781BDC2A401`). Its
serial and QMP hashes were rechecked against all three boot artifacts. The
proof-disabled candidate hashes are: composite
`3D4291381BEC1E5B1D560D0F2CF768F0E9CC9FB188569AAC70C22DBEC0569F3F`, kernel
`C31D46274F775C22E9F21E8D4DB77D833432F7212DABB546A25167F432629114`, and
ramdisk
`9F0C1055759F3CFFC7871DCF5EC729D0AC504BA054EE40822903EB62F3CA756B`. The
candidate scanner reports proof control absent, and the ordinary serial logs
contain no C178 proof diagnostics. Existing Settings, Explorer, Notes,
Calculator, and Task Manager smoke paths passed.

The existing C172 and C173 production manifests also pass, with SHA-256
`09291ED24684E797082CA86A5FB8B9258173D44EF216811390A84AF0190B7FFE` and
`A6E4069F2F1370D9D326BC15AB7AA781027296EB846416B004FB5FA9E608B0B1`.
Evidence under `out/dotnet/c178-managed-calculator-resize/` retains each boot's
serial log, QMP input, screenshots, and manifests. Earlier failed runner
attempts and their candidate artifacts are preserved in separate run folders.

Focused proof diagnostics verify state fields that the production build
intentionally does not emit. In the ordinary concurrent boot, Notes and
Explorer identity generations and window handles were checked across the
Calculator resize; their proof-only state diagnostics remained absent. The
production screenshot helper measures titlebar width, while QMP input records
the requested height changes; exact height and client-area dimensions are
asserted by the focused proof. The isolated production media leaves the
pre-existing `ESP/ramdisk.img` modification untouched.

## ABI and compatibility

NativeAOT remains ABI v4 / 128 bytes, with callback offsets 104, 112, and 120.
The C174 transaction semantics, C175–C177 Notes behavior, Settings persistence,
file associations, and stored settings formats are unchanged. The small
freestanding C++ routing addition is built with the repository's AMD64 kernel
target and uses no STL, exceptions, RTTI, or new dynamic allocation.

The C178 screen-level concurrency exercise respects the three managed-surface
capacity: it closes Settings to create Calculator while keeping Explorer and
Notes resident, then restores a fresh Settings surface after Calculator
teardown. Persisted association slots are checked before Apply and Reset.

## Recommended C179 scope

Evaluate live resize for the existing managed Task Manager. It has a distinct
data-list layout and should test whether the C174 transaction remains reusable
for row-based content while preserving selection, scroll offset, task identity,
and focus. Do not begin C179 as part of C178.
