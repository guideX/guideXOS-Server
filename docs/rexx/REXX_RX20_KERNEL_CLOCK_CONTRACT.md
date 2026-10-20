# REXX RX20 — Kernel Clock Contract

## Scope and current behavior

RX20 adds an internal clock contract to the existing `kernel::time` service. It does not add a public kernel ABI, a persistent RTC setting, or a firmware runtime-services caller. The production x86/AMD64 source remains the CMOS RTC at ports `0x70`/`0x71`; a successful sample is a raw calendar observation only.

For a successful CMOS sample the contract reports:

- source `CmosRtc`;
- valid raw calendar fields using the existing `2000 + YY` year rule;
- one-second resolution;
- unknown accuracy;
- unknown interpretation, no local or UTC capability, and no known offset.

The two-digit year is accepted for 2024–2099, matching the existing effective reader range. The reader does not consume the ACPI century register. A plausible date and stable hardware sample do not establish the physical clock's correctness or timezone.

## Result fields and state separation

`ClockReading` in `kernel/core/include/kernel/time.h` separates:

- source read status (`Ok`, `Unavailable`, `Invalid`, or `Unsupported`);
- source identity and calendar validity;
- raw, local, and UTC calendar values and their separate availability flags;
- conventional signed UTC offset minutes, with an independent known flag;
- interpretation provenance and its basis;
- timestamp resolution from clock accuracy;
- source limitations; and
- PIT monotonic duration and resolution.

The raw calendar survives an unknown interpretation. Invalid or unavailable reads clear calendar fields and all wall-time capabilities. A subsequent source failure cannot reuse a prior successful timestamp. The source callback interface `read_clock_from_source` is also used by deterministic host fixtures; capabilities are derived from source and provenance rather than passed as caller-selected flags.

For offset metadata the kernel uses the conventional sign: `local = UTC + utc_offset_minutes`. A known zero offset is distinct from an unknown offset.

## CMOS adapter

`kernel/core/time.cpp` keeps the existing update-in-progress wait and stable double sample. The indexed-register sampling sequence masks and restores maskable interrupts using architecture helpers that preserve the prior IF state. CMOS index/data access leaves NMI enabled; no current NMI handler reads CMOS. Each bounded sample checks the update-in-progress state before and between snapshots. The decoder in `kernel/core/clock_contract.cpp`:

- accepts BCD and binary mode;
- accepts 12-hour and 24-hour mode, including AM/PM conversion;
- rejects invalid BCD nibbles and invalid 12-hour values;
- checks the RTC valid-time bit in status register D;
- validates Gregorian month lengths, leap days, and time ranges; and
- applies the existing `2000 + YY` convention without claiming century metadata.

The PC/AT RTC reports seconds, so the effective resolution is one second. Its accuracy and synchronization state are unknown. Reading the valid-time bit is a hardware validity check, not proof that the battery or wall clock is correct.

## Provenance and policy

The contract distinguishes unknown interpretation, operator-configured RTC UTC/local intent, UTC with an explicit local offset, a source-defined interpretation, and firmware-provided local time. An operator configuration can authorize an interpretation, but the result is marked `OperatorConfiguration` with `CLOCK_LIMIT_CONFIGURED_SETTING_UNVERIFIED`; it cannot prove that the physical RTC was set accordingly.

There is no suitable kernel RTC setting in this checkout. The desktop `timeZoneId` is a display preference for hosted epoch time and is not an RTC policy. RX20 therefore selects no production policy: unknown remains the default. Configuration policy is modeled and tested so a later settings service can pass an explicit choice without silently changing this phase's behavior.

## UEFI-shaped metadata adapter

`classify_uefi_time` is a pure compatibility adapter for fixtures and future work. It is not a production UEFI runtime adapter. It validates the Gregorian fields, the timezone range `-1440..+1440`, the `0x07FF` unspecified sentinel, and the two permitted daylight flag bits.

UEFI `TimeZone` is converted to the kernel's conventional offset with `utc_offset_minutes = -TimeZone`, following `Localtime = UTC - TimeZone`. Unspecified timezone still permits firmware-local calendar output but blocks UTC conversion. A numeric zero is a known zero offset. Invalid timezone or daylight metadata blocks UTC derivation while preserving valid firmware-local fields.

The adapter consumes the local calendar as supplied and does not add a daylight hour. Daylight flags are retained as state metadata; they do not provide transition rules. No daylight-saving database behavior is claimed. The current boot handoff does not pass a System Table or Runtime Services interface, and the kernel has no safe UEFI runtime-services path. RX20 does not call `GetTime()` or `SetTime()`.

## Monotonic time and REXX

The independent PIT source remains the only production monotonic provider. Its elapsed nanoseconds are computed from the programmed divisor and 1,193,182 Hz input frequency, become available after the first heartbeat, saturate at the tick counter, and fail closed on arithmetic overflow. Resolution describes the programmed tick interval; it does not imply matching accuracy. Wall-clock changes, CMOS read failures, and RTC interpretation do not alter PIT elapsed time.

The REXX shell adapter now consumes `ClockReading`. Raw CMOS fields may be passed to the existing private provider as `WALL_UNSPECIFIED`; this keeps current wall forms unavailable. It maps local wall fields only when the kernel reading actually has the local capability. The existing REXX provider continues to provide clause snapshots, per-VM timer state, error recovery, and the same `TIME('E')`/`TIME('R')` behavior.

DATE and TIME remain Partial. Clock-free DATE conversions remain available, and current DATE/TIME wall forms remain fail-closed with production CMOS. No UTC-dependent REXX behavior is enabled by the existence of this contract.

## Future work

RX21 can add a deliberately selected RTC policy through a suitable persisted setting or a verified source contract. Any policy must preserve the configured-versus-physically-correct distinction and should separately decide whether local, UTC, and offset capabilities are available. A real UEFI adapter requires a verified boot handoff, runtime memory mapping and lifetime, calling convention, synchronization, and guest qualification before use. DATE/TIME completion remains a separate gate requiring a live source and native guest proof.

The shared bare-metal TLS prerequisite helper now consumes ClockReading and requires utc_available before it returns Unix seconds or UTC text. With the production CMOS source still unknown, those values remain unavailable and its backend reports that interpretation is unknown. This removes the previous helper's unsupported CMOS RTC interpreted-as-UTC claim. The hosted Windows UTC provider is unchanged.

## RX20 qualification evidence

The RX20 scorecard is `REXX_RX20_SCORECARD.json`; run details are indexed in `evidence/rx20/qualification-summary.json`.

- The injectable contract suite passed 123 checks, including CMOS decoding, source/provenance capability rows, UEFI timezone conversion and invalid metadata, calendar boundaries, source failure/recovery, concurrent reads, and PIT overflow handling.
- The inherited hosted REXX suite passed, including the RX19 DATE/TIME and allocation-failure coverage. Regina differential comparison was `NOT_RUN` because the executable was unavailable.
- AddressSanitizer and UndefinedBehaviorSanitizer each passed all 22 hosted targets. Each local expected-result corpus completed 195/195; Regina remained `NOT_RUN`.
- The freestanding AMD64 build linked nine REXX C objects. The accepted kernel ELF SHA-256 is `a3950c82a95c7b80383c345984e6e61257d3bb1f6b31806c9f4ae910568b0c80`.
- Isolated QEMU run `run-0506436b9bbf43f683684baded0e460f` read CMOS successfully with raw capability only. Its screenshots show current DATE/TIME unavailable, unchanged DATE conversions, `TIME('E')` advancing from `0.000000` to `0.099993`, `TIME('R')` advancing from `0.099993` to `0.199992`, and recovery after the current-date error. Networking was disabled, disposable media was used, and the owned VM exited after acknowledged QMP shutdown.
- Artifact guards matched all 31 protected root object/PDB hashes and sizes at each qualification gate. The RX20 preflight recorded all 63 RX19R1 baseline untracked entries present and no root `.phase` file.
