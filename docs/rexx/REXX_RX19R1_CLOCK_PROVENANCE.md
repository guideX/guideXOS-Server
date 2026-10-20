# REXX RX19R1 — Clock Provenance and Civil-Time Decision

**Outcome: B — Partial.** The RX19 date engine, provider contract, clause snapshots, and PIT-backed elapsed timers remain qualified. The current kernel path provides no defensible local/UTC interpretation for its live CMOS reading, so RX19R1 keeps production wall-clock DATE/TIME operations explicitly unavailable.

## Decision

| Source or policy | Present on the REXX path? | Provenance and capabilities | RX19R1 disposition |
| --- | --- | --- | --- |
| x86/AMD64 CMOS RTC, ports `0x70`/`0x71` | Yes, read live by `kernel/core/time.cpp` | Stable calendar fields at one-second resolution. No source metadata identifies UTC or local time; no UTC offset or DST rule is supplied. The two-digit year is forced to `2000 + YY`. | Preserve the sample as `WALL_UNSPECIFIED`; do not format it as local time or convert it to UTC. |
| UEFI Runtime Services `GetTime()` | No | UEFI defines local calendar fields, a timezone value, daylight flags, and device resolution. `EFI_UNSPECIFIED_TIMEZONE` still denotes local civil fields but provides no UTC conversion. | Not used: this bootloader does not call `GetTime()` or pass a Runtime Services interface to the kernel, and the kernel has no runtime-services caller. |
| ACPI FADT century register | Not consumed by the RTC reader | The boot handoff includes the ACPI RSDP, but the RTC code does not read a century byte. Century metadata would improve year interpretation; it would not say whether the RTC is UTC or local. | No change in this phase; it cannot resolve the civil-time meaning. |
| Desktop `timeZoneId` preference | Exists for desktop display formatting | The current choices are Pacific, Mountain, Central, Eastern, and UTC. The default and invalid-value fallback are Pacific. The hosted clock applies this preference to a Unix/UTC epoch. It does not configure how the kernel RTC is stored and is not provided to the REXX kernel adapter. | Not an RTC policy and not evidence of the actual RTC setting. Do not reuse it for REXX wall time. |
| PIT monotonic counter | Yes, independently | Tick-derived monotonic nanoseconds, approximately 10 ms resolution at the normal heartbeat, available after the first interrupt. It has no civil-time interpretation. | Retain for `TIME('E')` and `TIME('R')`; never combine it with RTC fields to claim UTC/local time. |

## Boot-to-provider trace

The UEFI loader populates BootInfo v2 with the memory map, ACPI RSDP, framebuffer, ramdisk, NIC, and kernel base, then exits boot services. BootInfo has no calendar snapshot, timezone, RTC policy, or System Table/Runtime Services pointer. The kernel's current REXX adapter does not call a firmware time service. On x86/AMD64 it reads CMOS calendar registers directly; on targets without port I/O the RTC reader is unavailable.

The CMOS reader waits for the update-in-progress flag to clear and retries for a stable pair of samples. It supports BCD or binary and 12- or 24-hour registers. It converts the year register as `2000 + YY`, accepts only 2024–2100, and rejects out-of-range calendar/time fields. It does not independently validate BCD nibbles, consult the RTC valid-RAM/battery status bit, read a century register, or report a separate source-trust status. The read is live after boot, but stability and plausible fields do not establish that the clock is correct.

The shell adapter exposes successful raw CMOS fields at one-second resolution with `REXX_CLOCK_WALL_CIVIL` and `REXX_CLOCK_WALL_UNSPECIFIED`. That interpretation intentionally fails the REXX local-wall requirements. A failed or invalid RTC read does not create a replacement timestamp. The PIT capability is populated separately and remains usable for elapsed/reset timing.

There is a separate bare-metal helper in `gxos_tls_prerequisites.cpp` that converts the same RTC fields to Unix seconds and labels its backend `CMOS RTC (interpreted as UTC)`. No firmware, bootloader, or configuration metadata supports that interpretation. RX19's REXX adapter does not call this helper. Treat that label as an existing code assumption, not as provenance; it should be reconciled before guideXOS advertises a global UTC contract.

## Firmware specification implications

The [UEFI 2.10 Errata A Runtime Services specification, §8.3](https://uefi.org/specs/UEFI/2.10_A/08_Services_Runtime_Services.html) defines `GetTime()` fields as the platform's current local date/time. `TimeZone` is the number of minutes that local time is relative to UTC, with the equation `Localtime = UTC - TimeZone`; therefore RX19's `local = UTC + offset` convention would use `offset_seconds = -TimeZone * 60`. A valid timezone is from −1440 through +1440 minutes. `EFI_UNSPECIFIED_TIMEZONE` (`2047`) allows local civil formatting but does not allow UTC conversion. `Daylight` is state metadata, not a timezone database. The specification gives a normal PC-AT CMOS RTC one-second resolution.

The [UEFI system-table specification, §4.5](https://uefi.org/specs/UEFI/2.10_A/04_EFI_System_Table.html) identifies `GetTime()` as a Runtime Service. Runtime Services can remain available after `ExitBootServices()`, but an OS must preserve and map the firmware runtime ranges and retain a usable interface. The current guideXOS handoff does not give the kernel that interface, and the current kernel does not call it. Thus the standard's existence is not proof that the current kernel can use it.

The [ACPI 6.5 FADT definition](https://uefi.org/specs/ACPI/6.5/04_ACPI_Hardware_Specification.html) defines the `Century` field as the CMOS offset containing the century value. It can help resolve the RTC year but supplies no UTC/local or timezone interpretation.

## Configuration decision

The existing `desktop.clock.timeZoneId` setting is a display preference applied to epoch time. It does not distinguish an RTC configured as UTC from one configured as local civil time, and it has a silent Pacific default. It is therefore not a suitable configured RTC policy. There is no kernel setting for `RTC=UTC`, `RTC=local`, local UTC offset, or unknown interpretation. RX19R1 adds no persistent-settings subsystem and does not choose a default.

## REXX behavior retained

| Built-in behavior | Production status |
| --- | --- |
| Clock-free DATE conversions using a four-digit input year | Supported by the RX19 conversion engine. |
| DATE current-date outputs, including `DATE()` and current-only `C`, `J`, `M`, and `W` forms | `CLOCK_UNAVAILABLE`; not promoted to accepted live behavior. |
| DATE input conversion requiring the rolling two-digit-year window | `CLOCK_UNAVAILABLE` when the current year is needed. |
| TIME `E` and `R` elapsed/reset forms | Supported by the independent PIT monotonic provider after its first heartbeat. |
| TIME current-wall forms `C`, `H`, `L`, `M`, `N`, and `S` | `CLOCK_UNAVAILABLE`; no local/UTC interpretation is available. |
| Injected local-civil or fixed-offset provider readings | Exercised by hosted tests only; this does not qualify the production CMOS source. Fixed offsets do not provide DST or timezone-database behavior. |
| TIME input-time conversion arguments and z/VM DATE separator arguments | Outside the existing RX19 profile. |

The provider's existing validation, deterministic unavailable result, per-VM state, and clause-consistent snapshots remain unchanged. `TIME('L')` cannot be enabled until a qualified local reading exists; formatting six fractional digits does not imply six-digit source precision.

## Qualification and evidence

The updated hosted RX19 suite passed with 8,275 direct C assertions, 4,112 dispatch cases, and 2,048 deterministic calendar properties. It added both positive and negative year-boundary offsets, explicit rejection of valid-looking fields with unspecified interpretation, and four invalid provider-offset values. The inherited RX1–RX19 matrix passed; CLI 24/24, RX9 24/24, RX10 19/19, RX12 31/31, RX13 33/33, RX14 29/29, RX15 3/3, RX16 10/10, and RX17 10/10. The test runner did not find Regina, so differential comparison is `NOT_RUN`.

AddressSanitizer and UndefinedBehaviorSanitizer each built and ran all 21 targets, including the changed RX19 tests, with zero failures and zero findings. Each local expected-result corpus run completed 195/195 cases; the Regina side remained `NOT_RUN`. The updated run IDs are `a14e805923ba49e0b3fba41a0da99c32` (AddressSanitizer) and `a9661e7194234fdb9cc2831222d54099` (UndefinedBehaviorSanitizer).

The freestanding AMD64 qualification passed and linked nine REXX C objects. Its kernel ELF SHA-256 was `1fe3eaafc7895f46386eecb75fd9f0fc8b385bb2a1c25ef2a833f9d552281bfa`. The isolated QEMU run `21be302f91514f609c3976725cdeed29` exercised native deterministic DATE conversions, the explicit unavailable-clock path and recovery, shell responsiveness, and `TIME('E')` progression from `0.000000` to `2.599821`. The guest used `-rtc base=utc,clock=host` as test-harness configuration with networking disabled. This proves behavior along the guest's direct CMOS path under that test setup; it does not prove the RTC's physical civil-time meaning and does not qualify live wall-clock DATE/TIME acceptance. The owned QEMU process received acknowledged QMP shutdown and was absent afterward; the pre-existing foreign QEMU process was left untouched.

The QEMU runner's RTC setting is test-harness configuration; it can exercise the direct CMOS read and deterministic unsupported/error-recovery path, but it cannot prove a physical RTC's civil-time meaning. Firmware invalid-timezone cases are not applicable because there is no `GetTime()` parser or caller; four invalid injected provider offsets were tested instead. The historical RX19 corrected guest result and its separately retained superseded-failed fixture run remain unchanged.

The raw native-build stderr and guest serial captures are preserved byte-for-byte in `evidence/rx19r1/raw-log-captures.zip`; their SHA-256 values are listed in `raw-log-capture-index.json`. The protected root object/PDB hash baseline and untracked-entry snapshot are in `evidence/rx19r1/preflight.json`; final reconciliation is recorded in `evidence/rx19r1/artifact-integrity.json`. New run-specific logs and guest records are kept under `evidence/rx19r1/`.

## Recommendation for RX20

Add a kernel-owned clock contract with explicit provenance, validity, resolution, and separate local/UTC capabilities. On UEFI, a live `GetTime()` path can provide local fields even when its timezone is unspecified; only a validated `TimeZone` can support UTC conversion, with the UEFI sign convention translated explicitly. On non-UEFI paths, require an operator-configured RTC interpretation before enabling local or UTC operations. A boot-time sample alone is not a live clock. Reconcile the existing bare-metal UTC assertion, preserve the PIT timer unchanged, and keep wall-dependent REXX forms unavailable until the live path and guest proof pass.
