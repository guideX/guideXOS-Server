# REXX RX20 — Clock Capability Matrix

`Raw` means a structurally valid source calendar observation. It does not imply a timezone interpretation or clock accuracy.

| Source and provenance | Raw | Local | UTC | Offset known | Result and notes |
| --- | --- | --- | --- | --- | --- |
| Valid CMOS, interpretation unknown | Yes | No | No | No | `Ok`; 1-second resolution, accuracy unknown. Production default. |
| Invalid CMOS observation | No | No | No | No | `Invalid`; result fields are cleared. |
| Source-defined local civil time | Yes | Yes | No | No | Local calendar is the source calendar. |
| Source-defined UTC | Yes | No | Yes | No | UTC calendar is the source calendar. |
| Source-defined UTC with known offset | Yes | Yes | Yes | Yes | Local calendar is derived using the conventional offset sign. Zero is valid and known. |
| UEFI local time with unspecified timezone (`0x07FF`) | Yes | Yes | No | No | Firmware local fields remain usable; UTC is not extrapolated. |
| Configured RTC UTC/local intent | Yes | Policy dependent | Policy dependent | Policy dependent | Basis is `OperatorConfiguration`; physical RTC correctness remains unverified. |
| Unsupported source | No | No | No | No | `Unsupported`; no stale calendar is retained. |

The shared bare-metal TLS clock APIs require the UTC capability before they return an epoch or UTC text. Production CMOS remains unknown, so the APIs report UTC unavailable.

## UEFI conversion checks

The UEFI relationship is `Localtime = UTC - TimeZone`; the kernel offset is therefore `-TimeZone`:

| UEFI TimeZone | Kernel UTC offset | Example |
| ---: | ---: | --- |
| `0` | `0` | Local and UTC both available with known zero offset. |
| `-480` | `+480` | Local `2024-01-01 08:00` converts to UTC `2024-01-01 00:00`. |
| `+330` | `-330` | Local `2024-03-01 03:30` converts to UTC `2024-03-01 09:00`. |
| `0x07FF` | Unknown | Local available; UTC unavailable. |
| `< -1440` or `> +1440` | Invalid | Local stays available; UTC is unavailable. |

Only daylight flag bits `0x01` and `0x02` are accepted. The adapter never adds an extra hour to supplied local fields. It retains daylight state but does not implement timezone transition rules.

## Deterministic qualification

`tests/kernel_clock_contract_test.cpp` exercises the capability rows, CMOS BCD/binary and 12/24-hour decoding, UIP and valid-time failure, invalid BCD/calendar fields, leap day, positive/negative/zero offsets, UEFI sign conversion and sentinel, invalid metadata, date/month/year boundary crossings, source/configuration transitions, source failure/recovery with stale-data clearing, concurrent injected reads, and PIT conversion/overflow. The direct host run is recorded under `docs/rexx/evidence/rx20/direct-clock-tests/`.

The inherited hosted REXX suite separately covers clause-consistent snapshots, interpreter isolation, allocation failure, DATE/TIME fail-closed behavior, and monotonic elapsed/reset options. The actual CMOS source and PIT integration require the native guest lane; injected UEFI-shaped fixtures alone do not prove firmware support.

## Accepted production result

The production CMOS adapter classifies a valid sample as raw-only: `Ok`, `raw_available=true`, `local_available=false`, `utc_available=false`, and `utc_offset_known=false`. Resolution is one second and accuracy is unknown. The current REXX DATE/TIME wall-clock forms therefore remain unavailable, while clock-free DATE conversions and PIT `TIME('E')`/`TIME('R')` remain available.

The accepted native guest proof is `evidence/rx20/native-guest/run-0506436b9bbf43f683684baded0e460f/`. Its serial log records `source=CMOS_RTC read=ok raw=1 local=0 utc=0 offset=unknown resolution_ns=3B9ACA00 accuracy=unknown monotonic=PIT`; the two console captures show the script results and post-error recovery. The complete qualification status and limitations are in `REXX_RX20_SCORECARD.json`.
