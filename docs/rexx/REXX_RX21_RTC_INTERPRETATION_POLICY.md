# REXX RX21 — RTC Interpretation Policy

**Outcome B — Partial.** RX21 completes the Settings/configuration audit and defines the safe policy contract for a later integration. This checkout has durable file-writing primitives and a persisted Display Options preference store, but it has no suitable, versioned system-settings service that both the Settings owner and kernel clock provider can use. RX21 therefore adds no production RTC policy or wall-clock capability.

The production CMOS path remains the accepted RX20 implementation: a successful read supplies raw, validated calendar fields, with `ClockProvenance::Unknown`, no local or UTC capability, one-second resolution, and unknown accuracy. Missing, invalid, or unavailable RTC data remains fail-closed. The PIT monotonic source is unchanged.

## Policy model for a supported configuration source

The policy values are explicit operator declarations:

| Policy | Meaning | Capability when valid |
| --- | --- | --- |
| `Unknown` | No reliable UTC/local interpretation is configured. This is the default and the fallback after missing, invalid, unsupported, or unreadable configuration. | Raw calendar only; no UTC or local wall clock. |
| `RTC_IS_UTC` | The operator declares the RTC calendar fields represent UTC. This is configured intent, not proof that the physical RTC is correct. | UTC. With a valid fixed offset, local civil time can also be derived. |
| `RTC_IS_LOCAL` | The operator declares the RTC fields represent local civil time. | Local civil time. With a valid fixed offset, UTC can also be derived. |

The policy must come from an identified, supported configuration service. The implementation must not infer it from QEMU options, host settings, geography, or the apparent value of the RTC. A write that fails validation or persistence must leave the previously effective policy unchanged; a load failure must leave `Unknown` effective.

## Fixed offset semantics

The proposed internal convention is a signed integer number of minutes east of UTC:

```text
local = UTC + utc_offset_minutes
```

The bounded range is −1440 through +1440 minutes. A separate `offset_known` state distinguishes missing from explicitly configured zero. A configured value outside the range is invalid; it must reject the complete candidate policy instead of partially applying it. If a future UEFI adapter is added, translate at that boundary using `utc_offset_minutes = -EFI_TIME.TimeZone`.

Under `RTC_IS_UTC`, a valid offset converts UTC fields to local fields. Under `RTC_IS_LOCAL`, it converts local fields to UTC. Without an offset, UTC policy grants UTC only and local policy grants local only. A fixed offset carries no daylight-saving transition rules and must not be presented as an IANA time zone. No implicit daylight adjustment is allowed.

The RX20 clock contract already validates offsets in the proposed range, distinguishes known zero from unknown, and uses the proposed sign convention. Its current provenance values can represent configured UTC, configured local, and configured UTC with an offset. They cannot represent configured local with an offset; that case needs a contract extension before it can be reported as `LocalAndUtc`.

## RX20 contract boundary

`ClockReading` already keeps source status, raw calendar availability, local/UTC calendars, offset availability, provenance basis, accuracy, resolution, source limitations, and PIT monotonic data separate. `ClockProvenance::ConfiguredRtcUtc`, `ConfiguredRtcLocal`, and `ConfiguredRtcUtcWithOffset` map to `InterpretationBasis::OperatorConfiguration` and the unverified-setting limitation. The production CMOS source still emits only `Unknown` provenance.

The accepted contract has no policy source identifier, persistence state, configuration generation, or settings load/update/invalidation interface. Its pure classifier can qualify injected observations, but it is not a production settings provider. In particular, no persisted policy is loaded before capability queries, no Settings change invalidates clock capability state, and no physical RTC correctness is verified.

## Settings and REXX boundary

The current Settings Date & Time page is a hosted clock/display-preference view. It reads `std::chrono::system_clock` and the existing display time-zone preference; it does not read the kernel CMOS clock or configure its interpretation. The page's advanced action opens Display Options. The Settings S6 report records model and hosted-runtime qualification but says the window was not visually inspected.

No RTC setting is exposed in Settings, no kernel `GetTime()` or `SetTime()` path is added, and CMOS contents are not modified. The test harness may continue using `-rtc base=utc,clock=host` to exercise the raw read; that flag is not evidence of trustworthy production RTC semantics.

REXX `DATE` and `TIME` remain **Partial**. Clock-free DATE conversions, `TIME('E')`, and `TIME('R')` retain their accepted behavior. Current wall-clock forms continue to fail closed with production CMOS. Policy configuration alone would not complete these built-ins: the separate gate still requires a supported source contract, native guest execution, and qualification of all supported option semantics.

## RX22 direction

Do not schedule RTC-only repair phases until a suitable system-settings contract is available. RX22 should return to independent classic REXX built-ins. Keep RTC policy as a gated future integration with its storage, validation, kernel load lifecycle, cache invalidation, and Settings owner specified before code is enabled.
