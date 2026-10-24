# RX38 Native Evidence Audit — RX22 onward

These are historical phase results, not RX38 runs. The source references below remain the source of record; RX38 records the kernel digest and only the process fields those reports actually captured. “Not recorded” is not treated as exit 0.

## Native QAC runs

| Phase | Kernel SHA-256 | Guest configuration and mode | Observed scripts/markers | QAC parent / child / runner | QMP / reservation | Unverified checks |
|---|---|---|---|---|---|---|
| RX22 | `252ec777…50445807` | `guideXOSServer`, exclusive acceptance, network disabled, disposable media, production kernel | REVERSE text/binary, SOURCELINE, ERRORTEXT 16/60, invalid-call recovery, DATE/TIME fail-closed, TIME(E), heartbeat | 0 / 0 / not separately recorded | acknowledged / released | None listed in the scorecard. |
| RX24 | `7f1a82b7…f215b8dc2` | `guideXOSServer`, acceptance, network disabled, disposable media, production kernel | TRACE E at safe command dispatch RC 127; invalid TRACE recovery; DATE/TIME fail-closed and elapsed TIME/PIT progress | not separately recorded / 0 / nonzero at immediate post-QMP status check | acknowledged / release logged; later QAC list showed zero sessions/reservations | No repeat launch while another checkout held QAC. |
| RX25 | `fcfd3ee4…01e338d88` | `guideXOSServer`, acceptance, QEMU 11.0.0, network disabled, disposable media, production kernel | RANDOM unseeded/seeded/reseed/range/error recovery; current DATE/TIME fail-closed; TIME(E) 0.000000→0.099993; PIT heartbeat 1→63 | not captured / 0 / not separately recorded | acknowledged / release marker and final empty QAC list | QAC CLI process exit field was blank. |
| RX26 | `ce32736f…42ec6678` | `guideXOSServer`, acceptance, QEMU 11.0.0, network disabled, disposable media, production kernel | INDEX/POS vectors and binary; invalid argument recovery; ADDRESS/TRACE; RANDOM; DATE/TIME; TIME(E)/TIME(R), heartbeat 1→158 | 0 / 0 / not separately recorded | acknowledged / released | No guest subcase marked incomplete in the scorecard. |
| RX27 | `da340faf…b058c4c` | `guideXOSServer`, exclusive acceptance, QEMU 11.0.0, network disabled, disposable media, production kernel | JUSTIFY vectors/custom and binary pads/truncation; `RX27_JUSTIFY_DONE`; post-error responsiveness; inherited INDEX/TRACE/RANDOM/clock checks | 0 / 0 / 0 | acknowledged / released | Earlier mistyped attempt is separately retained; accepted retry passed. |
| RX28 | `a7bf227e…decef` | `guideXOSServer`, exclusive acceptance, network disabled, disposable media, production kernel | FIND demo/vectors/error recovery; DATE/TIME guard; RX20 clock script | 0 / 0 / 1 | manual QMP acknowledged / released | Automated runner stopped before guest work on an `ExecutablePath` race. Every required script passed in a manual suite under the independently verified QAC-owned child. |
| RX30 | `f3647c4b…4ad5fac9` | `guideXOSServer`, exclusive acceptance, network disabled, disposable media, production kernel | Queue demo, empty PULL, recovery, DATE/TIME guard, RX20 clock script | 0 / 0 / 0 | acknowledged / released | None listed. |
| RX32 | `a3588d59…d4026dd` | `guideXOSServer`, acceptance, network disabled, disposable media, production kernel | Seven commands: VALUE, invalid-name recovery, queue, provider fail-closed/recovery, DATE/TIME, raw clock/PIT | 0 / 0 / 0 | acknowledged / released | Scorecard explicitly leaves RX31 native acceptance open. |
| RX33 | `d459c9ab…f44f4e1d` | `guideXOSServer`, acceptance, network disabled, disposable media, production no-provider kernel | Invalid CHARIN start/recovery; no-provider fail-closed; post-error response; VALUE; queue; RX31 fail-closed; DATE/TIME; TIME(E) progress | 0 captured / 0 / 0 | acknowledged / released | TIME(R) and final clock marker not observed. Scorecard explicitly leaves RX31 native acceptance open. |
| RX34 production | `67edd7fb…6d4034d2` | `guideXOSServer`, acceptance, network disabled, copied disposable ESP/firmware, production kernel, fixture off | `STREAM()=UNAVAILABLE`; CHARIN failed closed; unavailable state persisted; recovery script ran | 0 (derived from documented QAC CLI contract) / 0 / not separately recorded | acknowledged / released | None listed for the no-provider contract. |
| RX34 fixture | `bea97f4a…f1b4fe33` | `guideXOSServer`, acceptance, network disabled, copied disposable media, fixture kernel with injected RX34 stream provider | Default/named reads, cursor continuation, binary bytes, zero-length reposition, EOF, short read, provider error, unknown identity, non-seekable position, invalid-position preservation, oversized limit and recovery | 0 (derived from documented QAC CLI contract) / 0 / not separately recorded | acknowledged / released | Fixture result is not production provider evidence. |
| RX35 | `4f7ad559…eaddbae53` | `guideXOSServer`, acceptance, QEMU 11.0.0, `-net none`, copied disposable ESP/firmware, production media not used, `rx34_stream_fixture=false` | Eight commands: EXTERNALS, RX32 queue, RX31 empty PULL, RX31 recovery, VALUE, RX34 no-provider, RX34 recovery, DATE/TIME/PIT | 0 captured / 0 / not separately recorded | acknowledged / released; final QAC sessions 0 and QEMU count 0 | TIME(R) screenshot/final marker not claimed. |

For RX22–RX35, the full marker transcripts and QAC/guest metadata remain under the linked phase evidence paths referenced in their scorecards. The summaries above preserve known process gaps rather than filling them by inference.

## Phases without a new guest proof

| Phase | Build hash if applicable | QAC/guest state | Relevance |
|---|---|---|---|
| RX23 | — | No standalone REXX QAC run is claimed in the reviewed phase scorecards. | No RX23 guest result is used by RX38. |
| RX29 | `a7bf227e…decef` freestanding build | NOT_RUN; no reservation requested. | The decision gate did not require a guest run. |
| RX31 | `13089ff7…693e2d` freestanding build | NOT_RUN; QAC CLI was unavailable on PATH/`GUIDEXOS_QAC_CLI`; a foreign QEMU PID 4868 was preserved. | RX31 injected-provider acceptance is still open. |
| RX36 | No guest hash | NOT_RUN at feasibility stop. A later QAC snapshot saw a separate StellarNavigator guest/reservation, which RX36 left untouched. | No REXX guest proof. |
| RX37 | `11aa057a…92bfe6a8` freestanding kernel ELF | NOT_RUN; managed acceptance lane and queued requests occupied QAC. No guest SAY or capability markers. | Hosted capability API passed; native query/wrap proof did not run. |
| RX37R1 | No guest kernel | Outcome C: foreign acceptance reservation; request waited 20 seconds at queue position 1, received no grant, then was cancelled. No guest, runner, or QMP session. | Native output-capability query remains unobserved. |

## RX31 gate: what later guests establish

The RX31 scorecard's gate was not silently promoted by similar scripts. RX32 and RX33 both directly exercised production input-provider fail-closed behavior, and both scorecards explicitly retained RX31 native acceptance as OPEN. RX35 then ran the named production scripts `/rx31-empty-pull.rex` and `/rx31-recovery.rex`: the first showed deterministic default-input-unavailable with no fabricated line; the second showed `RX31_POST_ERROR_RESPONSIVE`, `RX31_RECOVERY_COUNT=0`, and exit 0. This is direct evidence for the production **no-reader** contract and queue cleanup.

None of those guests installed the RX31 callback and returned a provider-owned line. The native injected-reader contract—including native provider data, ownership/lifetime cases, and provider status variants—therefore remains open. The missing proof does not affect the profile that leaves the provider uninstalled.

## RX37R1 and current QAC status

At RX37R1, QAC was online but a verified acceptance guest from `guideXOSServer_DiskManagerImprovements` held reservation `8b84465adf184cc6a1f416744168127c` (owner CLI PID 31704, QEMU child PID 24052). RX37R1 did not send QMP commands or signal the owner. Its queue request was cancelled without a grant. No RX37R1 parent/child/runner guest exit, output marker, screenshot, QMP acknowledgment, or reservation release is claimed.

RX38 checked QAC status/list read-only. The coordinator was online with zero managed reservations, sessions, queued requests, or blocked requests, but reported two unmanaged/foreign QEMU processes (PIDs 20392 and 21932) and warned that acceptance reservations were blocked. RX38 launched no guest and left both untouched. The final QAC snapshots and a process identity hash are in this directory.
