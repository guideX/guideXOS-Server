# Pre-startup baseline identity

This directory is a new, additive evidence root for the Workstation GC
initialization experiment. It preserves the completed PAL and generic proof
artifacts without replacing the earlier `pal-win64-qemu-bridge` evidence.

## Locked NativeAOT identity

- NativeAOT source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- Runtime pack: `9.0.0`
- Architecture: AMD64
- Runtime identifier: `win-x64`
- GC interface: `5.3`
- EE interface: `2`
- Collector: Workstation, one heap
- Server GC: disabled
- Background/concurrent GC: disabled
- CPU count: one
- NUMA and large pages: disabled

## Active PAL baseline

- Active four-object archive:
  `Runtime.WorkstationGC.guidexos-nativeaot-pal.lib`
- SHA-256: `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`
- Stock PAL/runtime objects removed: `PalRedhawkCommon.cpp.obj`,
  `PalRedhawkMinWin.cpp.obj`, `thread.cpp.obj`, `time.c.obj`
- Exact mandatory symbol parity: 6/6, 38/38, 74/74, 3/3
- Missing, unexpected, and duplicate definitions: 0
- Replacement Windows imports: 0

## Adapted Workstation GC baseline

- Adapted archive SHA-256:
  `BA847F225439A8D693CD975CCAACDD01264BDA88BE4F2BBF18D5A0E4DB0F1F52`
- Stock `gcenv.windows.cpp.obj` removed and replaced with the guideXOS
  `gcenv` object plus the already-proven platform adapters.
- Replacement `gcenv` object SHA-256:
  `1F255DFCF8CBEB0A93289E0F160C04DA5410EB41F9B06113186BF9D3438F0CCB`

## Scope guard

The baseline was captured before this experiment invokes `RhInitialize`.
The baseline proofs remain no-collection proofs: `RhInitialize` false,
collections zero, and GC-backed allocations zero. This experiment must not
overwrite those claims.
