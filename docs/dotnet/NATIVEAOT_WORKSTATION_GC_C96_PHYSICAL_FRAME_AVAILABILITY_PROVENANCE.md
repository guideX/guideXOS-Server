# NativeAOT Workstation GC C96 Physical-Frame Availability and Ownership Provenance

Status: complete for T320/T216, three fresh boots per target. This C96 record extends the accepted C93/C94/C95 chain and does not replace it.

## Scope and decision

The question was whether the C95 T320 failure was caused by an unavailable physical-frame pool, a leak, a page-table shortage, or a different consumer of the same pool. The exact normalized comparator was retained:

- T320: `15mid8`, tail `320`, exact constrained 16-page commit.
- T216: `15mid8`, tail `216`, the same normalized 16-page commit with sufficient free frames.
- Three fresh boots for each target.
- NativeAOT 9.0.0 AMD64 Workstation GC, using the authenticated C95 runtime-pack manifest.

Decision: this is Outcome B, `IMAGE_FOOTPRINT`. Both boots provision the same `0x1000`-frame pool with no permanently unavailable frames. T320 consumes `0x15E` more `VmRegion` frames and one more `PageTable` frame by `post-image`, before managed execution. That footprint delta persists through `pre-tail`; at the decisive T320 commit only one frame is free, so the first tentative `VmRegion` page is acquired and the next page cannot be acquired. Rollback returns the tentative page, leaving one free frame. T216 has `0x7A0` free frames before the same 16-page commit and succeeds.

This is not a leak: the allocator owner ledger reconciles at every checkpoint and the failed T320 commit's rollback returns the one newly acquired frame. It is not a page-table exhaustion: the failed allocation is a `VmRegion` allocation and page-table ownership remains `0xE` during rollback. There is no separate allocator bucket for `Kernel`, `Stack`, `Runtime`, or `Other`; the actual C96 enum has only `VmRegion` and `PageTable`.

## Source-backed allocator facts

The source of truth is `kernel/core/address_space.cpp` and `kernel/core/include/kernel/address_space.h`:

- Page size is `0x1000` bytes and the runtime pool is capped at `0x1000` pages.
- `initialize()` resets every frame state and mapping bit and accepts the boot-provisioned nonzero aligned pool.
- `allocateFrameInternal()` scans the pool from index zero, allocates the first `Free` frame, records the owner, clears its mapping bit, and increments the total and owner counters.
- A return value of physical address zero is the allocation-failure sentinel. The pool base itself is required to be nonzero; frame index zero is otherwise an ordinary frame.
- `releaseFrame()` requires an in-pool, aligned address with the expected owner and no active mapping, then returns the frame to `Free` and decrements the matching owner count.
- `accounting()` reports `totalKnownFrames`, `freeFrames`, `allocatedFrames`, `regionOwnedFrames`, `pageTableFrames`, `mappingCount`, and release counters. There is no permanently unavailable-frame counter in this allocator.
- `FrameOwner` contains only `VmRegion` and `PageTable`; the requested broader consumer labels are not distinct allocator owners.

The boot loader provisions `runtimeFramePoolPages=4096`, or `0x1000` frames / `0x1000000` bytes, for both targets. The managed virtual-memory commit path allocates `VmRegion` frames and rolls back newly allocated pages when a physical allocation returns zero. Page mapping can separately allocate `PageTable` frames.

## Checkpoint chronology

All values below are hexadecimal frame counts. `free + allocated = total`, and `allocated = region + pageTable` at each row.

| Checkpoint | T320 free / allocated / region / PT | T216 free / allocated / region / PT | T320 minus T216 allocated delta |
|---|---:|---:|---:|
| `pre-managed` | `0x1000 / 0 / 0 / 0` | `0x1000 / 0 / 0 / 0` | `0`
| `post-image` | `0x7E1 / 0x81F / 0x81A / 0x5` | `0x940 / 0x6C0 / 0x6BC / 0x4` | `0x15F`
| `post-startup` | `0x7CF / 0x831 / 0x827 / 0xA` | `0x92E / 0x6D2 / 0x6C9 / 0x9` | `0x15F`
| `pre-tail` | `0x7CF / 0x831 / 0x827 / 0xA` | `0x92E / 0x6D2 / 0x6C9 / 0x9` | `0x15F`
| decisive `COMMIT-BEFORE` | `1 / 0xFFF / 0xFF1 / 0xE` | `0x7A0 / 0x860 / 0x854 / 0xC` | `0x79F`
| decisive `COMMIT-AFTER` | `1 / 0xFFF / 0xFF1 / 0xE` | `0x790 / 0x870 / 0x864 / 0xC` | `0x79F`

The earliest nonzero divergence is `post-image`, before managed entry. The T320 NativeAOT ELF has an RW `PT_LOAD` `MemSiz` of `0x69A820`; T216 has `0x53E7D0`. The page-rounded loaded-image difference is `0x15E` pages, matching the `VmRegion` delta. The additional one-frame page-table delta accounts for the total `0x15F` frame delta.

There is no separately serialized tail-79 checkpoint. The safe retained boundary is `pre-tail`, followed by the exact `COMMIT-BEFORE` immediately before the decisive tail-80-equivalent commit. This avoids inventing an unsupported intermediate observation.

## Decisive commit and rollback

T320's C95 grow marker identifies address `0x1018C1000`, requested/committed size `0x10000`, with the prior grow commit returning success. C96 observes:

- Before commit: free `1`, allocated `0xFFF`, region `0xFF1`, page-table `0xE`.
- The request needs `0x10` pages, so after the first tentative page the pool is exhausted.
- `COMMIT-AFTER` returns `0x7` (`OutOfMemory`) with the same stable counts after rollback.
- Correlated rollback stage 1: `firstPage=0x18C1`, `pageCount=0x10`, `newlyAllocatedPages=1`, `free=0`, `allocated=0x1000`, `region=0xFF2`, `pageTable=0xE`.
- Correlated rollback stage 2: same page/count/newly fields, `free=1`, `allocated=0xFFF`, `region=0xFF1`, `pageTable=0xE`.

T216's C95 grow marker identifies `0x101301000` with the same `0x10000` request and a successful result. C96 observes `free=0x7A0` before and `free=0x790` after, with no correlated rollback. The available margin after the commit is `0x790` frames (`1936` frames).

## C96 BSS and artifact facts

The C96 observer adds no static frame array or static ledger. The proof-kernel `.bss` is `0x62A8770` bytes for both T320 and T216; C96 BSS delta is `0` bytes. The physical frame state array is the existing bounded allocator array, not new per-run proof storage.

The proof NativeAOT ELF artifacts are different because the target tails produce different loaded image footprints: T320 is `1589248` bytes with SHA-256 `3B07B6912D44184280EDA3D1B6A69F31CE4DBB40334E9A844854860A786A5A18`; T216 is `1581056` bytes with SHA-256 `876C16BAFF07ABB96FDFB1913FD2D376329C463E0B1759361C1E0F959C419406`. The proof-kernel SHA-256 values are recorded in the run manifests and differ per target. The ordinary kernel/ESP SHA-256 is unchanged at `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.

## Reproducibility and retained controls

The exact harness mode is `physical-frame-availability-provenance`. Its parser retains the C93 fit, C94 capacity, C95 commit/primitive, C67 completion, C77/C89/C51 runtime-pack, and authenticity controls. T320 and T216 each passed all three fresh boots. The C96 rollback parser correlates rollback events to the C95 grow-gate address so unrelated VM rollback events do not count as decisive evidence.

The authenticated runtime-pack manifest is `out/dotnet/c011ec95-runtime-pack/runtime-pack.manifest.json`, proving NativeAOT 9.0.0 AMD64 Workstation GC, locked runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, and patched function-pointer state `PATCHED_CORRECTLY`. The two C96 run roots and manifests are:

- `out/dotnet/c011ec96-physical-frame-availability-provenance/15mid8/tail-320/run-20260908-182831326`
- `out/dotnet/c011ec96-physical-frame-availability-provenance/15mid8/tail-216/run-20260908-183142277`

C52 Tier-All was not run because it is unrelated to this physical-pool provenance question. B02 remains premature: the evidence already establishes an image-footprint boundary before managed execution, so a same-image C97 experiment is the next scientifically justified step if further discrimination is required.

