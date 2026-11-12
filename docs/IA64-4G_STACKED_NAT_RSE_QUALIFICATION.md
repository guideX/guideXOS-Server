# IA64-4G Stacked-Register NaT and RSE Preservation Qualification

**Date:** 2026-11-11
**Branch:** `IA64_SUPPORT`
**Starting HEAD:** `2456ef7ab89672ab18c0682e8617879808b22c8d`
**Outcome A — `IA64_STACKED_NAT_RSE_PRESERVATION_ACCEPTED`**

One independent Ski probe passed before one authorized guideXOS kernel run. The probe and kernel both demonstrated a real RSE spill/fill, preserved the NaT bit in stacked GR33, retained an ordinary GR32 sentinel, and returned with the parent BSP/BSPSTORE and RSC restored. The kernel also preserved all five previously accepted early-exec faults and exited intentionally. The new qualification itself generated zero faults.

This accepts only the bounded three-register parent frame and one 96-register child frame exercised below in pinned, one-CPU Ski system mode. It does not qualify arbitrary task-context switching, general RNAT/UNAT preservation, interrupt or scheduler RSE handling, NaTPage recovery, firmware, or a general Itanium ABI unwinder.

## Scope and preservation

- The work began from a clean tracked worktree on `IA64_SUPPORT`; the branch was four commits ahead of its upstream. The accepted 4F ELF SHA-256 `8b53224a9cc70e51b3db040df5aeda15d329513db2b55f11326f453fd115739f` and its manifest, loader record, and run evidence were preserved.
- Existing untracked Python `__pycache__` files and directories were left in place. No reset, clean, pull, merge, rebase, or push was performed.
- Isolated probe and candidate outputs are under `/home/guidex/src/ia64-4g-*`. Failed experiments and their logs remain in their original directories. The final kernel candidate is `ia64-4g-stacked-nat-rse-kernel-20261111-r3/bin/kernel.elf`.
- No QEMU or QAC execution occurred.

## Architectural contract

The test uses stacked GR32 and GR33, rather than static GR8 alone, to make the RSE behavior observable. GR32 carries the `GXOSNAT` sentinel. GR33 receives a copy of GR8 while GR8 has NaT set; `tnat` checks the token before and after the child call without consuming it. A child `alloc` creates SOF 96 and forces the parent frame to memory. The child reads back the sentinel after the stores complete and checks the NaT bit in the architecturally valid RNAT source.

The routine records RSC and sets enforced-lazy mode before reading BSPSTORE or RNAT. It serializes the RSC change with `srlz.d`. `flushrs` drains older dirty frames but leaves the current test frame live; it is used only with mode 0 established and begins its own instruction group. The later child allocation, not the ordinary `br.call`, exceeds the 96 physical stacked-register capacity (three parent locals plus 96 child locals) and makes the RSE spill the parent frame. The RSE writes the parent frame; software does not write active RSE backing-store or RNAT memory. `mf` precedes memory observations. The partial AR.RNAT is accepted only with its valid-bit mask derived from BSPSTORE. If the selected RNAT collection were not fully stored, its memory word would not be read.

The return path checks that the child's `br.ret` filled the parent frame: GR32 still has its sentinel, GR33 still has NaT, and BSP/BSPSTORE match the recorded parent values. The routine restores the original RSC and the wrapper clears GR8 before C++ starts.

### Register frames and control state

- GR0–GR31 are static general registers. GR32–GR127 are stacked registers, renamed through the current frame and allocated by `alloc`. The test establishes the original NaT in static GR8, copies it to stacked GR33 with `mov` (which propagates NaT), and never consumes either NaTed value as an integer.
- CFM describes the active frame. Its SOF/SOL/SOR fields describe the stacked frame, locals, and rotating-register region. The parent `alloc` creates a three-local frame: GR32 sentinel, GR33 NaT, and GR34 saved B0. The child `alloc` creates 96 locals. `alloc` returns the previous frame marker in a general register; the test records and restores the saved PFS/CFM state on each return path.
- `br.call` writes the return address to B0. The parent preserves its B0 in GR34 before calling the child; the child restores its saved PFS and returns through B0. This is explicit ownership for this sequence, not a general ABI wrapper.
- IFS is interruption function state used by `rfi` to restore a frame marker. `cover` can copy CFM to IFS when interruption collection is disabled and creates a zero-sized frame while preserving the previous locals and outputs. The test uses neither `cover` nor `rfi`; no interrupt frame or IFS state is part of this qualification.
- BSP is the logical backing-store position for the active register stack. BSPSTORE is the memory-store cursor. Before pressure, both are `0x4c81e00`; after the three-register parent spill, both advance to `0x4c81e18`; the child return refills the parent and restores both to `0x4c81e00`.

### Backing-store and RNAT validity

Backing-store entries are 8 bytes. Each 512-byte collection contains 63 stacked-register values followed by one 8-byte RNAT collection slot at offset `0x1f8`. Thus the collection slot is the 64th word, not a register value. A register's NaT bit index is its word index within that 512-byte collection. If the next register word would land in slot 63, address calculation skips the collection word and uses slot 0 in the next collection. The run stayed at indices 0–3 and did not exercise that boundary.

RSC.mode 0 is enforced-lazy: the RSE performs mandatory spill/fill work only. Modes 1, 2, and 3 permit eager stores, eager loads, or both, so software cannot assume when those extra transfers occur. Reads of AR.BSPSTORE and AR.RNAT, and execution of `loadrs`, require mode 0; access while mode is nonzero is illegal. The code saves the original RSC, clears only its mode bits, executes `srlz.d`, performs the observations, and restores the original value with another `srlz.d`.

AR.RNAT holds up to 63 not-yet-stored NaT bits. Its bit index is BSPSTORE bits 8:3. Bits from that index down through bit 0 are defined; higher bits through bit 62 are undefined, and bit 63 reads as zero. A write to BSPSTORE, `loadrs`, or an RSE spill of the RNAT register makes AR.RNAT contents undefined. The test therefore derives a validity mask from the child BSPSTORE and checks only the selected valid bit. Here BSPSTORE is `0x4c81e18`, giving mask `0xf`; the selected GR33 bit is index 1, and the defined portion of AR.RNAT is `0x2` under that mask. The other RNAT bits remain undefined. The RNAT memory collection address is `0x4c81ff8`, but it is not required: no complete collection was stored. No memory RNAT value was read.

`flushrs` must be unpredicated and first in its instruction group; it waits until active physical frames up to, but excluding, the current frame are spilled. It is used here to drain older dirty frames and verify BSPSTORE reached the current frame before the controlled child allocation. `loadrs` also requires mode 0 and must be unpredicated and first in its instruction group; its RSC.loadrs field controls how much is filled, with zero requesting all stacked registers outside the current frame before use. It is not used: the child's normal `br.ret` performs the tested fill. `cover` must be unpredicated and last in its group; it creates the interruption frame boundary described above. None of these instructions permits software to fabricate or overwrite active RSE-owned backing-store data.

References: Intel [Itanium Architecture Software Developer’s Manual, Volume 1](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-1-manual.pdf), [Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), and [Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf), covering stacked registers, NaT propagation, `tnat`, `alloc`, RSE spill/fill, RNAT, RSC mode restrictions, and `flushrs`. The Ski implementation was pinned to commit [`dfc2902ea1423d9b32543a5daf8026213f2b37a1`](https://github.com/trofi/ski/tree/dfc2902ea1423d9b32543a5daf8026213f2b37a1), version 1.5.1.

## Boot placement

The first integration attempt called the test from an active C++ NaT-consumption caller. `flushrs` spilled that live caller frame; returning without `loadrs` left the C++ continuation stalled. Those diagnostics were not accepted as passes and are preserved. The final integration runs the RSE test in an assembly wrapper after BSS clearing and before `kernel_main` allocates any C++ register frame. The wrapper establishes GR8.NaT with the previously qualified PSR.ed/RFI sequence, checks it around the test, and clears GR8 before returning to `_start`.

The independent r10 probe now performs the same Ski console initialization before BSS clearing, then runs the wrapper before entering C++. It passed this boot sequence independently. No `loadrs` or active backing-store write was added to the qualification routine.

## Independent Ski probe

| Evidence | Result |
|---|---|
| Probe ELF | `/home/guidex/src/ia64-4g-stacked-nat-probe-20261111-r10/stacked-nat-rse-probe.elf` |
| Probe ELF SHA-256 | `4d22d474ceb084ad0658f02d048c1f6f64180700aba96d9ad254c6eec73cf915` |
| Probe manifest SHA-256 | `b4487893ff8a201717f1254b51a1bdaea4a672781dc4c8ac1ee68841c4e9a80b` |
| Run result SHA-256 | `3b2ce7103126342a50b38452771a8fdf97132a45f4c46ae4d0002bf42e71bfa3` |
| Qualification | `STACKED_NAT_RSE_PROBE_PASS`; zero Ski faults; intentional SSC exit; cleanup complete |
| Runtime RSE evidence | Three-register spill, valid partial RNAT, GR33.NaT preserved across child return |
| Ski | 1.5.1; SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |

The full capture is `/home/guidex/src/ia64-4g-stacked-nat-probe-20261111-r10-run/`. Its stdout SHA-256 is `8360ad1cacdd3174951c8cbfad43634477a6d6bf2abc0accbaeb729e4f2e78e4`; stderr SHA-256 is `0ae96a8e859a61558f6cad1958d981912dd0bcc349449e002a16df2aad6b6ccb`; instruction-count SHA-256 is `638dbdf1d9c113a9ea2d6ef49ce6adeeb76303a9fe62e06d73db52546a5bd02f`.

The probe target disassembly is `/home/guidex/src/ia64-4g-stacked-nat-probe-20261111-r10/stacked-nat-rse-probe.disassembly.txt`. It independently shows `alloc r10=ar.pfs,3,3,0` at `0x10a440`, `mov r32=0x4758534f534e4154` at `0x10a476`, `mov r33=r8` at `0x10a480`, `tnat.z` on GR33 at `0x10a4a6`, `flushrs` at `0x10a670`, and the child `br.call` at `0x10a74c`. The child uses `alloc r10=ar.pfs,96,96,0` at `0x10aaf0`, reads BSPSTORE and RNAT only after its mode-zero guard, and returns through `br.ret` at `0x10af7c`. The guest validator also runs three negative record checks: cleared post-call NaT, incorrect spilled sentinel, and mismatched magic/record identity. All three were rejected (`negative_failures=0`).

## Kernel candidate and static qualification

The final candidate is ELF64 little-endian `EM_IA_64`, `ET_EXEC`, with entry `_start=0x108000` and GP `0x684000`. Its SHA-256 is `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. The candidate manifest SHA-256 is `dada3a361ad245fb1a65b35f68cd5a19e0e8eef41d7ae8f26fd7fe41fb6df41d`; its guest authorization remains false. The candidate-bound Ski loader record is [ia64-4g-ski-loader-record.json](ia64-4g-ski-loader-record.json), SHA-256 `a224c0ad755cc35ea4e655f96b39a3d7f7ae810c8c3f21718a96b84d634f1a43`.

All ELF, early-exec, Break, General Exception, CPU-state, Unaligned Reference, Register NaT Consumption, and stacked NaT/RSE static gates passed. The IVT is 32 KiB and aligned at `0x100000`; the RSE backing store is 64 KiB at `[0x4c81e00,0x4c91e00)`, separate from the 64 KiB memory stack. The new record is 320 bytes. The target disassembly confirms the boot wrapper call occurs after BSS clearing and before `kernel_main`, and that the wrapper checks GR8.NaT, calls the RSE routine, clears GR8, restores its saved boot registers, and returns.

The first candidate build exposed an object-name collision between `stacked_nat_rse.cpp` and its assembly file. The assembly source was renamed `stacked_nat_rse_test.s` so Make gives it a distinct object. Candidate r1’s failed link is preserved; r2 and final r3 built and passed static qualification. The final build used the pinned GCC 13.5.0 frontend, Binutils 2.41 assembler/linker/objdump, and target libgcc. The build wrapper now accepts explicit tool paths, which supports this split installation.

The independently inspected kernel target disassembly is `/home/guidex/src/ia64-4g-stacked-nat-rse-kernel-20261111-r3/ia64-disassembly.txt`. The boot wrapper is at `0x3dc1c0`; its `rfi` establishes the GR8.NaT stimulus at `0x3dc32c`, speculative load is at `0x3dc330`, pre-call `tnat` is at `0x3dc346`, and it calls the RSE test at `0x3dc37c`. The test function is at `0x3dc4b0`, child at `0x3dcda0`, and the record at `0x4c94c30`. Key instructions are `alloc r10=ar.pfs,3,3,0` (`0x3dc6f0`), sentinel materialization (`0x3dc726`), `mov r33=r8` (`0x3dc730`), `tnat.z` of GR33 (`0x3dc756`), `flushrs` (`0x3dc920`), the child call (`0x3dc9fc`), child `alloc r10=ar.pfs,96,96,0` (`0x3dcda0`), BSPSTORE read (`0x3dce30`), `mf` followed by RNAT read (`0x3dce60`/`0x3dce66`), and child return (`0x3dd22c`). The parent's post-call `tnat.z` of GR33 is at `0x3dca4c`. The child return is the physical fill point; the subsequent values and pointers are checked before the outer frame is restored.

## One authorized kernel run

After the independent probe and final no-guest preflight passed, the runner executed the r3 candidate once in pinned Ski `-forcesystem` mode with one CPU, a 60-second wall timeout, a 2 GiB address-space limit, and a 120-second CPU limit. The run is captured at `/home/guidex/src/ia64-4g-stacked-nat-rse-kernel-run-20261111-r1/`.

| Evidence | Observed result |
|---|---|
| Kernel executions | 1 |
| Runner classification | `NAT_CONSUMPTION_KERNEL_PASS` |
| Kernel / manifest / loader SHA-256 | `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c` / `dada3a361ad245fb1a65b35f68cd5a19e0e8eef41d7ae8f26fd7fe41fb6df41d` / `a224c0ad755cc35ea4e655f96b39a3d7f7ae810c8c3f21718a96b84d634f1a43` |
| Exit / timeout / cleanup | status 0; no timeout; cleanup complete |
| Instructions / elapsed | 47,420,644 / 0.816 seconds |
| Fault attribution | exactly 5 expected faults: two Break, one General Exception, one Unaligned Reference, one Register NaT Consumption |
| Stacked NaT/RSE marker | `IA64_STACKED_NAT_RSE_KERNEL_RESULT=PASS`; zero errors; zero negative-fixture failures |
| Parent and child BSPSTORE | `0x4c81e00` → `0x4c81e18` (24-byte, three-register spill) |
| NaT evidence | GR33 NaT before/after `1/1`; GR32 sentinel before/spill/after preserved |
| RSC / RNAT | original/restored `3/3`; observation/child mode `0/0`; partial AR.RNAT valid mask `0xf`, defined portion `0x2`, selected bit 1 set; higher bits undefined |
| Return | BSP/BSPSTORE restored to `0x4c81e00`; RNAT collection memory correctly marked not required |

Run-result SHA-256 is `2f9f6e6be609b2ea64a41ac26e61311afffc506f6efbc524af7eb73a7e06750e`; stdout SHA-256 is `55579bdd1f1d75da76d8f651a1a5ba4f8e158a5a27529c4c48c5b5598eec7f9f`; stderr SHA-256 is `b06329abb64aa5844e8fe545ede44c287381f7701be0fed37870e98564301985`; instruction-count SHA-256 is `15795a7054375f3317f718c0801470d17228acf61fe283962650cf9ab36a7bb2`. The existing five-fault result and all four earlier qualification markers passed in the same run.

## Regression and dependency checks

| Check | Result |
|---|---|
| IA-64 contract regressions | 84 tests passed across foundation, early-exec, loader, CPU-state, Break, General Exception, Unaligned Reference, NaT Consumption, and stacked NaT/RSE suites |
| ELF malformed-input fixtures | 3 passed: bad IVT size, bad handler boundary, bad source offset |
| IA-64 FP code-generation harness | PASS; report at `/home/guidex/src/ia64-4g-fp-codegen-20261111-r1/report.md` |
| Independent register diagnostics | PASS: known register checks, FPSR readback, and SSC GETCHAR API in both standalone ELFs; fixed ABI wrapper passes byte-exact output, legacy formatter failure reproduced. RNAT is explicitly unverified by this separate diagnostic. |
| Mbed TLS profile verifier | Direct `pwsh` invocation PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| Shell syntax | PASS for the IA-64 build/runner, FP harness, diagnostic runner, and relevant probe build scripts |

The register diagnostics ran after the one authorized kernel execution as two separate small standalone ELFs; they were not guideXOS kernel executions. Their manifest is `/home/guidex/src/ia64-4g-register-diagnostics-20261111-r1/diagnostic-manifest.json`. The fixed wrapper ELF SHA-256 is `d0fac0795a98ddcad208b95553c377456bf5843291e872d6aca5a6c23e9838eb`; the legacy comparison ELF SHA-256 is `2d74701ff89c1f6c2e4a08554bcc82fdc23124df49f6b5dbf3a1dbc3f99153ae`. The diagnostic confirms IVA/BSPSTORE/RSC/PSR and FPSR readback, while RNAT remains explicitly unverified in that separate diagnostic. The Mbed TLS check was run directly with `pwsh`; `POWERSHELL=true` was not used. Final static and preflight logs are under the candidate’s `preflight/` directory. The regression log is `/home/guidex/src/ia64-4g-contract-regressions-20261111-r2.log`.

## Limits

- Only the known three-register parent frame and one 96-register child spill/fill are qualified. Generalized RSE state save/restore, task switches, interrupts, and nested faults remain unqualified.
- The record observes a partial AR.RNAT whose valid mask is derived from the child BSPSTORE. It intentionally does not claim that an incomplete memory RNAT collection is readable.
- The PSR.ed/RFI NaT stimulus and GR8 cleanup run only in the pre-C++ boot wrapper for this early-exec profile. This is not a reusable interrupt or C++ caller API.
- No firmware, SMP, scheduler, paging, user-mode, device-init, QEMU, or QAC behavior is qualified.
