# guideXOS Server .NET Support — C97 Same-Image Runtime Tail Selector and Image-Footprint Isolation

Status: PASS — Level 3 same-image shared-prefix convergence. B02 remains STILL_PREMATURE.

## Question and answer

C97 asks whether the C96 T216/T320 separation was caused by the managed tail length or by the NativeAOT image footprint. One NativeAOT proof artifact was built once, then launched from six fresh QEMU boots in the interleaved order 216, 320, 216, 320, 216, 320. The runtime tail bound was selected only from the launch-time C97TAIL.BIN scalar. The pre-managed, post-image, post-startup, pre-tail, and tail-prefix frame censuses converged across all six boots. The first selector-dependent checkpoint is tail-217: selector 216 reports allocationPresent=0x00000000; selector 320 reports allocationPresent=0x00000001.

This removes the C96 image-footprint confound and localizes the next controlled difference to the runtime-selected managed tail. It does not establish a B02 policy explanation; B02 is explicitly retained as STILL_PREMATURE.

## Reproduction identity

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Starting HEAD: `42b92a47282d58d826d02de95a014db4a9012d76`
- Branch: `v1.1_DOTNET_SUPPORT`
- Upstream: `origin/v1.1_DOTNET_SUPPORT`
- Runtime: NativeAOT 9.0.0, AMD64, Workstation GC, GC interface 5.3, EE interface 2
- Locked runtime source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- Runtime-pack manifest: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec95-runtime-pack\runtime-pack.manifest.json`
- Active PAL archive SHA-256: `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`
- QEMU: `QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`
- Exact command and all generated build commands: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/commands.txt`
- Evidence root: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368`
- Fresh boots: `6` total, `3` per selector
- Interleaving: 216, 320, 216, 320, 216, 320
- Workload parameters: C64Variant=W3, C66Strategy=P2, C66TailAllocations=320, C71Case=15mid8, TimeoutSeconds=90

## Single-image identity

- Managed PE SHA-256: `42547983D8F9E7971D2637F7AE34FCA05238B66BBDE8F5D3CB00E82E672A1590`
- Managed ELF SHA-256: `5741F8A05451F4C6CF0A9EA29A6F041FCCDB46045E86E325FE4AFFB3AD7C1350`
- Proof kernel SHA-256: `DB5CF5817D8D5433D65678D8BB8020059652324021AF68B70166BE612F15F0D1`
- Runtime-staged artifact FNV-1a identity: `0x07515B812C8B1E10`
- Runtime-staged artifact bytes: `0x0000000000184000`
- Proof-kernel copy: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/artifacts/proof-kernel.elf`
- One managed build: true
- Managed rebuild between selectors: false
- Kernel rebuild between selectors: false
- Selector-only ESP input: four-byte little-endian `C97TAIL.BIN` containing 216 or 320
- Per-boot selector/artifact authentication: one `C011EC97-SELECTOR` marker per boot; all six markers agree on the artifact identity and byte count

The raw ELF inspection is in `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/elf-inspection.txt`. The exact proof-kernel section audit is in `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/proof-kernel-sections.txt`.

### Image layout facts

- Managed ELF program headers: seven `PT_LOAD` segments.
- `PT_LOAD` MemSiz values: `0x1000`, `0x109E00`, `0x65A00`, `0x69A860` (RW), `0xB200`, `0x200`, `0x600`.
- Page-rounded `PT_LOAD` pages: `0x1`, `0x10B`, `0x66`, `0x69B`, `0xC`, `0x1`, `0x1`; total `0x81B` loaded image pages.
- Proof-kernel `.bss`: `0x62A8770` bytes; the C97 observer adds no static frame pool or per-run ledger.
- Runtime frame pool: `0x1000` total frames, page size `0x1000`.
- C97 post-image census: `0x81F` allocated = `0x81A` VmRegion + `0x5` PageTable, leaving `0x7E1` free.

## Six-boot frame census

All values are hexadecimal. Every row satisfies `free + allocated = 0x1000` and `allocated = regionOwned + pageTable`.

| Boot | Checkpoint | Selector | Allocation present | Free | Allocated | VmRegion | PageTable |
|---|---|---:|---:|---:|---:|---:|---:|
| tail-216-run-1 | pre-managed | 0x000000D8 | 0x00000000 | 0x0000000000001000 | 0x0000000000000000 | 0x0000000000000000 | 0x0000000000000000 |
| tail-216-run-1 | post-image | 0x000000D8 | 0x00000000 | 0x00000000000007E1 | 0x000000000000081F | 0x000000000000081A | 0x0000000000000005 |
| tail-216-run-1 | post-startup | 0x000000D8 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-216-run-1 | pre-tail | 0x000000D8 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-216-run-1 | tail-001 | 0x000000D8 | 0x00000001 | 0x0000000000000132 | 0x0000000000000ECE | 0x0000000000000EC1 | 0x000000000000000D |
| tail-216-run-1 | tail-079 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-1 | tail-080 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-1 | tail-143 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-1 | tail-203 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-1 | tail-216 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-1 | tail-217-pre | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-1 | tail-217 | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-1 | tail-complete | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | pre-managed | 0x00000140 | 0x00000000 | 0x0000000000001000 | 0x0000000000000000 | 0x0000000000000000 | 0x0000000000000000 |
| tail-320-run-1 | post-image | 0x00000140 | 0x00000000 | 0x00000000000007E1 | 0x000000000000081F | 0x000000000000081A | 0x0000000000000005 |
| tail-320-run-1 | post-startup | 0x00000140 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-320-run-1 | pre-tail | 0x00000140 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-320-run-1 | tail-001 | 0x00000140 | 0x00000001 | 0x0000000000000132 | 0x0000000000000ECE | 0x0000000000000EC1 | 0x000000000000000D |
| tail-320-run-1 | tail-079 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | tail-080 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | tail-143 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | tail-203 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | tail-216 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | tail-217-pre | 0x00000140 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | tail-217 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-1 | tail-complete | 0x00000140 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | pre-managed | 0x000000D8 | 0x00000000 | 0x0000000000001000 | 0x0000000000000000 | 0x0000000000000000 | 0x0000000000000000 |
| tail-216-run-2 | post-image | 0x000000D8 | 0x00000000 | 0x00000000000007E1 | 0x000000000000081F | 0x000000000000081A | 0x0000000000000005 |
| tail-216-run-2 | post-startup | 0x000000D8 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-216-run-2 | pre-tail | 0x000000D8 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-216-run-2 | tail-001 | 0x000000D8 | 0x00000001 | 0x0000000000000132 | 0x0000000000000ECE | 0x0000000000000EC1 | 0x000000000000000D |
| tail-216-run-2 | tail-079 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | tail-080 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | tail-143 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | tail-203 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | tail-216 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | tail-217-pre | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | tail-217 | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-2 | tail-complete | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | pre-managed | 0x00000140 | 0x00000000 | 0x0000000000001000 | 0x0000000000000000 | 0x0000000000000000 | 0x0000000000000000 |
| tail-320-run-2 | post-image | 0x00000140 | 0x00000000 | 0x00000000000007E1 | 0x000000000000081F | 0x000000000000081A | 0x0000000000000005 |
| tail-320-run-2 | post-startup | 0x00000140 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-320-run-2 | pre-tail | 0x00000140 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-320-run-2 | tail-001 | 0x00000140 | 0x00000001 | 0x0000000000000132 | 0x0000000000000ECE | 0x0000000000000EC1 | 0x000000000000000D |
| tail-320-run-2 | tail-079 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | tail-080 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | tail-143 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | tail-203 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | tail-216 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | tail-217-pre | 0x00000140 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | tail-217 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-2 | tail-complete | 0x00000140 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | pre-managed | 0x000000D8 | 0x00000000 | 0x0000000000001000 | 0x0000000000000000 | 0x0000000000000000 | 0x0000000000000000 |
| tail-216-run-3 | post-image | 0x000000D8 | 0x00000000 | 0x00000000000007E1 | 0x000000000000081F | 0x000000000000081A | 0x0000000000000005 |
| tail-216-run-3 | post-startup | 0x000000D8 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-216-run-3 | pre-tail | 0x000000D8 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-216-run-3 | tail-001 | 0x000000D8 | 0x00000001 | 0x0000000000000132 | 0x0000000000000ECE | 0x0000000000000EC1 | 0x000000000000000D |
| tail-216-run-3 | tail-079 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | tail-080 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | tail-143 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | tail-203 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | tail-216 | 0x000000D8 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | tail-217-pre | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | tail-217 | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-216-run-3 | tail-complete | 0x000000D8 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | pre-managed | 0x00000140 | 0x00000000 | 0x0000000000001000 | 0x0000000000000000 | 0x0000000000000000 | 0x0000000000000000 |
| tail-320-run-3 | post-image | 0x00000140 | 0x00000000 | 0x00000000000007E1 | 0x000000000000081F | 0x000000000000081A | 0x0000000000000005 |
| tail-320-run-3 | post-startup | 0x00000140 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-320-run-3 | pre-tail | 0x00000140 | 0x00000000 | 0x00000000000007CF | 0x0000000000000831 | 0x0000000000000827 | 0x000000000000000A |
| tail-320-run-3 | tail-001 | 0x00000140 | 0x00000001 | 0x0000000000000132 | 0x0000000000000ECE | 0x0000000000000EC1 | 0x000000000000000D |
| tail-320-run-3 | tail-079 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | tail-080 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | tail-143 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | tail-203 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | tail-216 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | tail-217-pre | 0x00000140 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | tail-217 | 0x00000140 | 0x00000001 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |
| tail-320-run-3 | tail-complete | 0x00000140 | 0x00000000 | 0x0000000000000001 | 0x0000000000000FFF | 0x0000000000000FF1 | 0x000000000000000E |

The decisive common physical state at `tail-080` is free `0x1`, allocated `0xFFF`, VmRegion `0xFF1`, PageTable `0xE` for both selectors. The C95 VM marker records the inherited failed 16-page growth shape at this frontier; C97 does not reinterpret that control as a new policy result.

## Tail-217 separation

Each boot records both `tail-217-pre` and `tail-217`. The pre-record is emitted immediately before the request for allocation 217; the post-record is emitted after that request, or as an explicit absence record for selector 216. The terminal record is emitted only after the inherited observer finish markers.

| Selector | tail-217-pre | tail-217 | terminal |
|---:|---:|---:|---:|
| 216 | absent by construction | `allocationPresent=0x00000000` | observed on all three boots |
| 320 | absent by construction | `allocationPresent=0x00000001` | observed on all three boots |

The managed C64 allocation census differs only after the common prefix: the selector-216 boots report the 216-request tail, while selector-320 boots report the 320-request tail. No per-allocation diagnostic logging was added; only the bounded checkpoints above are emitted.

## Inherited controls and observer integrity

- C93 fit-boundary: present on all six boots.
- C64 allocation and completion: present on all six boots; the inherited result is `outcome=C`.
- C65 completion: present on all six boots. Its `outcome=F`/invariant accounting is the same inherited physical-pressure control reproduced by C96 and is not used as a C97 selector result.
- C77 summary/completion: present on all six boots with zero invariant failures, zero sensitive diagnostic allocations, zero fail-fast, and zero page faults.
- C89 region-source records: present on all six boots.
- C67 completion: present on all six boots with the bounded region-supply result and zero observer invariant failures.
- C95 VM commit: present on all six boots.
- C97 frame accounting: zero accounting failures across all emitted rows.
- C26: C97 uses only the observer continuation bypass needed because runtime method layout is selector-independent but not identical to the compile-time T216/T320 layout; the authentic C19–C23 unwind path remains exercised. This does not change GC, VM, frame-pool, loader, or B02 policy behavior.

## C96 comparison

C96 used separate proof images. Historical C96 reported T320 RW `PT_LOAD` MemSiz `0x69A820` versus T216 `0x53E7D0`; the page-rounded image delta was `0x15E` VmRegion frames plus one PageTable frame by post-image. Both C96 images had the same proof-kernel `.bss` size `0x62A8770`. C97 uses one loaded ELF/PE/kernel identity for both selectors, so the early image delta cannot explain the tail-217 separation.

## Source audit

- scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1: EFE87B77AE02FA31EA31B7968769ACAB42AF7CC2896428D98386F4A664818D75
- samples/managed/HostLogProof/Program.cs: 30E62611ACA2537DCE28CC53F7809025FF2D4AE6F2D7C567FEA854B28CB8F6A7
- kernel/core/nativeaot_pal_qemu_test.cpp: E93E55922EA6FC6A01C18FC59158094810D719AA32CF48FC09F63703D484AD75
- tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp: 2076A1C672B8D41DC66E735A52C045527B69BDE3D65F484ECD989CE982FFBFCA
- kernel/core/main.cpp: 8CF921E9B25865641CE8C4C8AF5334FC27E2E22D0257AF4DCFD19EFB7D8E14F2

The experiment changes only the C97 selector transport, managed runtime tail selection, bounded C97 frame checkpoints, and the C97-specific continuation of layout-sensitive diagnostic gates. Ordinary kernel/ESP restoration is verified below. No push was performed.

## Final classification

- Outcome: Level 3 same-image shared-prefix convergence.
- Causal statement: C96 image footprint is excluded; the controlled difference begins at the runtime-selected tail request at allocation 217.
- B02: STILL_PREMATURE.
- Recommended next experiment: C98 only if a separate policy-level attribution is required after this isolation result.
- Suggested commit: `Isolate NativeAOT tail count from image footprint`

## Cleanup and repository state

- Ordinary kernel SHA-256 after cleanup: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`
- Ordinary ESP kernel SHA-256 after cleanup: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`
- Required ordinary SHA-256: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`
- QEMU process count after cleanup: zero
- Push: not performed
- Manifest: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/manifest.json`

## Source and evidence index

- Documentation: `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C97_SAME_IMAGE_TAIL_SELECTOR_ISOLATION.md`
- Build/runtime evidence: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368`
- Selector boots: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/runtime216-boots` and `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/runtime320-boots`
- Shared-prefix comparison: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/shared-prefix-comparison/comparison.txt`
- Historical comparison: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/historical-c96-comparison/comparison.txt`
- Final machine manifest: `out/dotnet/c011ec97-same-image-tail-selector-isolation/run-20260909-063546368/manifest.json`
