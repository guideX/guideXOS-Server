# REXX RX19 — DATE/TIME and kernel clock integration

**Outcome: B — Partial.** The conversion engine, provider contract, per-VM elapsed timer, and clause snapshots are implemented and qualified. The production kernel still cannot identify its RTC value as local civil time or UTC with a configured offset. RX19 therefore does not claim usable current DATE/TIME output or complete DATE/TIME acceptance.

## Clock audit

The AMD64 kernel reads calendar fields from CMOS/RTC in `kernel/core/time.cpp`. It waits for the update-in-progress flag, reads fields repeatedly until stable, converts binary or BCD fields, and validates the result. The current reader maps the two-digit RTC year to `2000 + YY` and accepts only 2024–2100. It does not expose a validity/trust status, century metadata, UTC/local interpretation, timezone, UTC offset, or daylight-saving rules. BCD digit nibbles are not independently validated. These gaps make the RTC unsuitable as a trusted local wall clock for portable REXX output.

The PIT in `kernel/core/pit.cpp` is already configured for the kernel heartbeat at approximately 100 Hz. RX19 exposes its tick count as a monotonic nanosecond value using the actual PIT divisor and the 1,193,182 Hz input clock. Its reported resolution is the programmed tick duration (about 10 ms at the normal setting). It becomes available after the first PIT interrupt, does not use wall time, saturates rather than wrapping, and preserves its count across PIT reinitialization. Architectures without the port-I/O PIT implementation report the capability as unavailable.

There is no kernel timezone configuration or UTC-offset service. RX19 does not infer one from QEMU, the host, or the RTC. The runtime and guest adapter do not call hosted time APIs.

## Provider and execution behavior

`RexxVMClockReading` carries capability bits, wall interpretation, optional fixed UTC offset, wall and monotonic resolution, civil fields, fractional nanoseconds, and monotonic nanoseconds. A provider is installed per VM and is borrowed for the synchronous execution. Missing, failed, malformed, or insufficient clock data returns `REXX_VM_STATUS_CLOCK_UNAVAILABLE`; the VM reports a catchable SYNTAX condition with code 40 and a deterministic diagnostic. The default CLI has no provider and does not fabricate time.

The kernel shell adapter offers the RTC fields only as `WALL_UNSPECIFIED`; it does not enable local-wall operations. The same adapter offers PIT monotonic readings when available. Thus `TIME('E')` and `TIME('R')` can use the PIT independently of wall-clock trust. Hosted direct tests inject deterministic providers; the RX2 hosted shell bridge returns unavailable rather than importing the host clock.

The compiler gives emitted instructions a clause identity. A VM caches the provider reading and elapsed result for that clause, refreshes on a subsequent clause, and invalidates the cache when control loops back within the same chunk. The snapshot is per VM. Internal routine calls save and restore the caller's clause snapshot and elapsed baseline; elapsed changes made by the callee do not change the caller's timer state. INTERPRET uses its own dynamic chunk and restores the caller's snapshot when it returns. Failed reads are cached only for the failing clause and do not leave a stale valid reading.

## DATE profile

The implementation follows the classic z/OS DATE conversion profile and supports output options `B`, `C`, `D`, `E`, `J`, `M`, `N`, `O`, `S`, `U`, and `W`; input formats `B`, `E`, `J`, `N`, `O`, `S`, and `U`. `C`, `J`, `M`, and `W` are current-date-only outputs. Pure conversions work without a clock. Calendar arithmetic is bounded integer proleptic Gregorian arithmetic over years 0001–9999; the Base origin is 0001-01-01 and Base zero denotes that day. Invalid dates and separators are rejected without normalization. Normal input requires an unpadded day and exact mixed-case English month abbreviation. Two-digit years use the provider's current year window, current year minus 50 through current year plus 49.

The z/VM DATE extension that accepts input and output separator-character arguments is outside this profile; RX19 accepts the portable three-argument interface only. Current-date options remain unavailable in production until a trusted local-wall contract is added.

## TIME profile

`TIME([option])` supports `C`, `E`, `H`, `L`, `M`, `N`, `R`, and `S`. Current-wall options `C`, `H`, `L`, `M`, `N`, and `S` require a local-civil or fixed-offset wall reading. The fixed-offset form is converted to local civil time using the explicitly supplied offset and handles date boundaries; it does not model daylight-saving transitions. The Long and Elapsed/Reset forms expose six microsecond digits, truncated to the precision the provider actually supplies. Elapsed values use monotonic timestamps, not wall-clock fields. The first `E` or `R` call starts the per-VM timer and returns zero. A Reset call returns the clause interval and resets the baseline once for that clause. Values beyond the nine-digit seconds range return a resource-limit status.

TIME input-time conversion arguments are not implemented or claimed.

## Qualification

Direct coverage injects wall and monotonic values, crosses midnight and fixed-offset boundaries, jumps wall time in both directions, checks reset and nested-routine isolation, exercises failed-read recovery and multiple VMs, and verifies 2,048 deterministic Base-day properties against an independent calendar oracle. The hosted CLI also runs pure date conversions and traps a missing current-time capability before recovering with a clock-free conversion. The freestanding AMD64 build and isolated QEMU guest passed. The visually reviewed guest run 758845fd006940f2a2263c08f42be306 exercised deterministic conversions, unavailable-clock recovery, TIME E, and post-error responsiveness; it makes no current wall-clock claim. The first guest run (2b642a5f72b84b54a59f88e1e61ef6d5) is retained as superseded-failed because manual screenshot review found an invalid DATE W conversion with an explicit input date; the corrected rerun removed that case. See each run directory for screenshots, logs, process reconciliation, and review record.

See `REXX_RX19_CLOCK_PROVIDER_CONTRACT.md`, `REXX_RX19_BUILTIN_INVENTORY.csv`, `REXX_RX19_SCORECARD.json`, and `docs/rexx/evidence/rx19/` for API detail and qualification evidence.

## References and next phase

- [IBM z/OS 3.2 DATE](https://www.ibm.com/docs/en/zos/3.2.0?topic=functions-date)
- [IBM z/OS 3.1 TIME](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-time)
- [IBM z/VM 7.4 REXX/VM Reference](https://www.vm.ibm.com/library/740pdfs/74631400.pdf) — documents separator-character extensions that are intentionally not merged into the z/OS profile.

RX20 should establish an explicit trusted RTC/UTC/local-time policy, validated year and timezone metadata, and a documented conversion/offset-update model before enabling current wall-clock DATE/TIME output. Keep the monotonic timer independent from that work.
