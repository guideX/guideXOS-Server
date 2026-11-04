# C188R12R4R5R4R5R2R2R4R1 PAL Capacity Correction Guest Acceptance

**Status: prepared, not executed**

## Candidate to use

Use the fresh, hosted-qualified candidate:

```text
out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/candidate-2/
```

Identity files:

- `candidate-manifest.json` — SHA-256 `5CCC2ECB50ED9776B5C502AF46A08680E21C8EE70B7D723F44E1194C63607C01`
- `candidate-integrity-inventory.json` — SHA-256 `4FF88668F6C155BC8E105421D80FEAEB52E8393EE32ED32021720C60D3ACC5B7`
- Diagnostic NativeAOT ELF — SHA-256 `210A7EDCC916A04CFF70C8E28429113DE147684129A3A08CBC9647BCF9A3D443`
- Diagnostic kernel ELF — SHA-256 `8234961C77932A0BA16173FF702A71387F440AC0FC856F91A31FC926661F0AD9`
- Candidate ramdisk — SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`

The inventory rechecks every hash declared by the candidate manifest, includes size and role for each row, and records the 256-slot source, 12 KiB linked symbol, and hosted regression result. `ESP/ramdisk.img` remains unchanged at the same SHA-256.

## Admission and one-boot boundary

Before launching the guest, the runner must capture a fresh, coherent QAC status/list census and complete the exclusive admission flow. Confirm the coordinator and QEMU process census agree and there are no active or queued reservations that block admission. Do not reuse an earlier census or guard.

Use the R2R2R4 attempt. Its guard is created with `CreateNew` at the unique `qac-guest-r2r2r4-001` location. The guest boot limit is one, and automatic retry is disabled. If preflight or admission fails, record a blocked outcome and stop without launching QEMU.

Command after admission is authorized:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/run-c188r12r4r5-gc-ownership-qac-guest.ps1 `
  -CandidateRoot out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/candidate-2 `
  -CandidateIntegrityInventory out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/candidate-2/candidate-integrity-inventory.json `
  -NativeAllocationGuestAttempt R2R2R4
```

Do not run this command during the hosted qualification phase.

## Guest observations to collect

For the one guest boot, preserve raw serial bytes before analysis and record their SHA-256 before and after parsing. Preserve the native-allocation, semi-shared, and GC initialization diagnostic records separately. Record the candidate identity and guard identity with the QAC reservation and owned QEMU process identity.

The trace should determine:

1. Whether the 8-byte `GCEvent::Impl` nothrow allocation again requests a 24-byte backing reservation.
2. PAL VM slot capacity, active slots, and free-slot index at that request.
3. Whether startup PAL reserve reaches the kernel VM reserve path and its actual result.
4. Whether commit is reached and its actual result, if reservation succeeds.
5. Whether the PAL returns a pointer and whether `GCEvent::Impl` construction begins.
6. Whether the request instead encounters the independently bounded 128-entry VM region registry or another backend failure.
7. Whether `init_semi_shared()` completes or records a subsequent failure and cleanup result.
8. The actual GC initialization milestones and `RhInitialize` return value.
9. Whether the managed-entry gate remains closed or opens under the recorded runtime prerequisites.

Run managed GC proof operations only if initialization and the established thread-attachment, module-registration, and runtime safety prerequisites actually pass. Do not infer these prerequisites from hosted tests or force the gate open.

## Shutdown and result

Shut down only the QEMU child owned by this run through its QMP channel. Verify process exit and QAC reservation release; record preflight, admission, guest, and release results. A missing or incomplete trace is inconclusive. A backend reserve/commit failure is reported at its observed site. A successful allocation advances the observed initialization milestone but does not by itself establish successful GC initialization or managed collection.

The maximum is one QEMU boot. Do not retry automatically. Preserve the raw serial and every prior candidate or guest artifact unchanged.
