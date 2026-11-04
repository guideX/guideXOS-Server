# C188R12R4R5R4R5R2R2R4 PAL Capacity Fix Qualification

**Outcome: `PAL_CAPACITY_FIX_HOSTED_READY`**

## Result and boundary

The 16-slot startup PAL tracking defect is corrected in the diagnostic PAL using a fixed 256-slot table, matching the production NativeAOT PAL's existing 256-slot VM table. The correction passed the hosted boundary and lifecycle regressions. Fresh diagnostic NativeAOT and kernel artifacts, plus proof-disabled production NativeAOT and kernel artifacts, were built and staged. The linked diagnostic kernel contains the 12 KiB table.

This is hosted readiness only. No QEMU process was launched and no QAC reservation was made. No guest GC initialization, successful guest VM allocation, managed execution, or GC acceptance is claimed.

## Repository state and provenance

| Item | Value |
| --- | --- |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `8fa1f81d2f1a83ee1b79519ca90dc5f520d4901c` |
| Ending implementation HEAD | `6c4d5c1559049e5ae77c462d3f43f3108ae3726b` |
| Candidate build source HEAD | `8fa1f81d2f1a83ee1b79519ca90dc5f520d4901c` (working-tree PAL changes were included and source hashes were verified) |
| Pinned runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| `git diff --check` | PASS |
| Original `ESP/ramdisk.img` SHA-256 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The candidate builder manifest records the source HEAD and build script identities. The candidate's `nativeAllocationKernelPalSource` hash matches the current startup PAL source (`A95E9A749FB0D1330D89560B45374BF1192AF43D567E4870C996DED742E24D4D`). The isolated kernel dependency file records `nativeaot_gc_vm_slots.h`; its current SHA-256 is `C536DC8218AB6E18193DC8E3B442D2BFC7EF512D72C1EDF2156FB70581CF5626`. The helper and PAL source hashes are also recorded in the candidate integrity inventory.

Three tracked `.obj` files were already modified before this work. An early hosted compile wrote into the repository root and changed their COFF timestamp fields. Their exact starting bytes were reconstructed by matching the original recorded full-file SHA-256 values. All three hashes now match the starting values below, and none was staged or committed:

- `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` — `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD`
- `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` — `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3`
- `guidexos_nativeaot_native_alloc_record.obj` — `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183`

The hosted compiler runner now changes its working directory to its isolated output folder before invoking `cl.exe`. Later regression runs bracketed these hashes and the source ramdisk hash; both checks passed.

## Defect and correction

The preceding guest trace established that the PAL had 16 active VM tracking slots when an 8-byte `GCEvent::Impl` allocation required a 24-byte backing reservation. The PAL found no free slot and returned before calling the kernel VM reserve or commit path. The failure propagated through startup malloc and nothrow `operator new`, then stopped `init_semi_shared()` and `RhInitialize`. This was a tracking-table limit, not evidence of physical memory exhaustion or a kernel VM failure. The trace and its corrected analysis remain in the prior R2R2R3 acceptance evidence; this phase did not rewrite that evidence.

The correction factors the diagnostic PAL table behavior into `kernel/core/nativeaot_gc_vm_slots.h` and uses `kMaxGcVmSlots = 256`, matching the production PAL constant. The table is fixed-size and static; it does not allocate or grow metadata dynamically. On amd64, `VirtualMemoryRegion` is 40 bytes and each active flag plus region occupies 48 bytes, for `256 * 48 = 12,288` bytes (12 KiB). The existing 16-entry GC event handle table is separate and unchanged.

The diagnostic kernel's `g_gcVmSlots` BSS symbol is `0x3000` bytes in the newly built ELF. Its compile dependency records the new helper header. The tracking table itself does not change a public ABI or the `VirtualMemoryRegion` layout; a hosted compile-time assertion still checks that region is 40 bytes.

The corrected PAL is kernel code and is linked into the diagnostic kernel that boots the NativeAOT diagnostic ELF. The candidate's linked allocation map retains the existing `GCEvent::Impl` → `operator new(std::nothrow)` → startup malloc → PAL virtual-allocation route; this phase changes only the diagnostic PAL's VM tracking table.

### Slot and VM lifecycle

- Reserve scans only the 256 in-bounds slots. It calls the existing VM region registry and marks a slot active only after the backend returns `Ok`. A failed backend reserve leaves the slot inactive.
- Address lookup finds the active region containing the address using `value >= base` followed by subtraction and a `reservedSize` bound check. Commit and decommit keep using the stored region and the computed offset.
- Release requires the exact reservation base. It releases the stored `VirtualMemoryRegion` and clears the active flag only after successful backend release. A failed release retains the tracking entry; a non-base or unknown pointer cannot clear another entry.
- The release callback's size argument remains unused, as before. The backend releases the complete stored region, so the table change introduces no new pointer/size pairing. Reserve/commit rollback still attempts release; the caller returns null after commit failure.
- The PAL has no separate lock around this table. `GcVmSlotTable` documents that callers must serialize its operations. Hosted tests are single-threaded, and this phase does not establish safety for concurrent PAL reserve/release calls. That remains a concurrency boundary for later runtime qualification.
- The VM region registry is independently bounded at 128 records. It may reject a reservation before the PAL table reaches 256; the PAL then leaves its selected slot inactive and reports the backend failure. If all 256 PAL slots are occupied, reserve returns null and preserves every existing record. The correction therefore addresses the proven 16-slot limit without promising unlimited allocations.

## Hosted regressions

The final hosted qualification is under:

```text
out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/hosted-regression-r2r2r4-final3-20261103/
```

It passed with `qemuLaunched=false` and `qacReserved=false`.

| Regression | Result |
| --- | --- |
| Faithful 16-slot fixture | Filled 16 slots; reservation 17 failed; all 16 remained registered |
| Corrected shared table | Reservation 17 succeeded; metadata and interior-address lookup matched each synthetic region |
| Capacity boundary | Filled all 256 entries; the next reserve failed safely without overwriting records |
| Duplicate and invalid operations | Overlap reserve, unknown pointer, interior-pointer release, and failed backend release did not corrupt ownership |
| Release and reuse | Exact-base release freed only its matching slot; the next reserve reused that slot |
| Backend failures and rollback | Failed reserve registered nothing; simulated commit failure followed by rollback returned the slot for reuse |
| Repeated lifecycle | 1,024 synthetic reserve/release cycles passed |
| Slot-table test total | 2,425 assertions passed |
| Fixed allocation record | Hosted record test passed |
| Linked allocation path | Three hosted `operator new(std::nothrow)` cases passed: success, synthetic callback refusal returning null, and PAL-not-ready returning null |
| Analyzer branches | Seven fixtures passed: success, VM commit, VM reserve, PAL slot capacity, PAL not ready, missing record, and incomplete record |
| Guest runner contract | Parser and hosted contract checks passed for the R2R2R4 one-boot guard and candidate integrity gate |

The slot tests use a synthetic reserve/commit/release backend. The linked operator-new integration uses the actual linked startup imports and PAL contract, but its callbacks supply or refuse a hosted backing pointer; it does not exercise kernel VM allocation. These tests establish table and propagation behavior, not guest memory success.

Other retained hosted qualification results passed: semi-shared attribution (30 assertions and 11 branch fixtures), GC-init probes, ThreadStore readiness (`framePreserved=2`, `nested=1`, `overflowRejected=1`), init-return and init-attribution probes, GC ownership predicates, QAC admission predicates (32 cases), QAC ownership predicates (15), QAC stream capture (2), factory predicates (9), C188 lifecycle (25 checks, 1,000 identity, 500 resize, 250 invalid, 250 stale, 100 lifecycle; 0 allocations), factory integration (14 factory cases, 9 predicate cases, 9 link-boundary cases), and C181 QMP event capture. The previously accepted 40-suite / 969-case UI baseline was retained and not rerun; no UI or managed application source changed in this phase.

## Fresh builds and candidate identity

The fresh candidate is staged at:

```text
out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/candidate-2/
```

Its result is `NATIVE_ALLOC_PROBES_HOSTED_READY`. The staged ESP contains the new diagnostic kernel, bootloader, and the preserved ramdisk. The candidate includes the diagnostic NativeAOT ELF and map, the allocation analyzer and branch map, hosted outputs, production identities, and builder provenance.

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `210A7EDCC916A04CFF70C8E28429113DE147684129A3A08CBC9647BCF9A3D443` |
| Diagnostic kernel ELF | `8234961C77932A0BA16173FF702A71387F440AC0FC856F91A31FC926661F0AD9` |
| Staged ESP kernel | `8234961C77932A0BA16173FF702A71387F440AC0FC856F91A31FC926661F0AD9` |
| Source and staged ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Proof-disabled production NativeAOT ELF | `AEE3E9C54656F1E036D2D38E5F64D87C508F51BE60CFBB7F66299590536B89D7` |
| Proof-disabled production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Candidate manifest | `5CCC2ECB50ED9776B5C502AF46A08680E21C8EE70B7D723F44E1194C63607C01` |
| Candidate integrity inventory | `4FF88668F6C155BC8E105421D80FEAEB52E8393EE32ED32021720C60D3ACC5B7` |
| Capacity/build provenance sidecar | `6E8B361795BDCA2524A00929329DB933E5D2AB766DF1AF24ECCB3D8BC3F49554` |
| Linked capacity-symbol inspection | `5770BDC76252B6B4CCB5C44CFC5AA43A15899B1CC773522BD065261141C4DFC8` |

The full integrity inventory has 67 size-and-role rows. All 54 hashes declared by `candidate-manifest.json` were rehashed and matched; all inventory rows passed. Supplemental rows cover the candidate manifest, provenance, capacity-symbol report, source ramdisk, current hosted results, and correction sources. The production NativeAOT qualification reports `PASS` and `persistentCompositeLifecycle=true`; its proof and diagnostic profiles are absent. The production kernel qualification reports `PASS`, omits the development descriptor, and leaves standard kernel output untouched. The diagnostic kernel is ELF64 x86-64 with no dynamic section. The NativeAOT PE inspection reports no DLL imports.

The analyzer identity is recorded by the candidate as `2855A569225CDDDE55B5916AC3434131BBA2A80FBE7201458E5655ADFB120579`. The prepared QAC runner identity is `BF27168346E19E24FA2C4AE5F965D492717375AFF5C4B1041A15FFD4F8CA9737`. The candidate and sidecars record builder scripts, source hashes, manifests, test outputs, and the pinned runtime source commit.

Primary build-log warning line counts were: diagnostic kernel 220, diagnostic NativeAOT 65, production NativeAOT 67, production kernel 425 (777 across those four logs). Counting nested build-log copies raises the raw total to 901. The kernel warnings are mostly unused parameters, functions, and variables, with smaller groups of type-limit, narrowing, sign-compare, and array/string-operation warnings. NativeAOT warnings include missing PDBs (`LNK4099`: 55 diagnostic and 58 production), macro redefinitions, linker warnings, and existing compiler/analyzer warnings. All four builds completed and their qualification manifests passed.

## Later guest qualification

The separate `C188R12R4R5R4R5R2R2R4R1` guest phase was executed once using [the prepared guest acceptance plan](NATIVEAOT_C188R12R4R5R4R5R2R2R4R1_PAL_CAPACITY_CORRECTION_GUEST_ACCEPTANCE.md). Its 24-byte backing request passed reserve and commit after the table crossed the old 16-slot boundary; the 8-byte `GCEvent::Impl`, `init_semi_shared()`, `GCHeap::Initialize()`, and `RhInitialize` also succeeded. Module registration completed. The next observed boundary was a fail-fast during the `InitializeModules` transition, before managed entry. See the [guest acceptance results](NATIVEAOT_C188R12R4R5R4R5R2R2R4R1_PAL_CAPACITY_CORRECTION_GUEST_ACCEPTANCE_RESULTS.md) for independent milestone evidence, concurrency limits, QAC release, artifact preservation, and the overall incomplete startup-acceptance status. The hosted outcome alone did not imply guest success.
