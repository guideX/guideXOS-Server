# IA64-4F Register NaT Consumption Qualification

**Date:** 2026-11-11
**Branch:** `IA64_SUPPORT`
**Starting HEAD:** `80b011a5bff55e708f5af924b36d255b12d1a6f6`
**Outcome A — `IA64_NAT_CONSUMPTION_RETURN_ACCEPTED`**

One independent Ski probe and one authorized guideXOS early-exec kernel run qualified a single Register NaT Consumption fault at vector `0x5600`. The kernel recorded the expected source register, faulting instruction and slot, resumed once at its authorized continuation, passed the immediate post-RFI state checks, preserved the four accepted IA64-4E exception events, and exited intentionally. The run recorded exactly five deliberate faults and no unexpected fault.

This accepts one bounded Register NaT Consumption return in pinned, one-CPU Ski system mode. It does not qualify general speculative-execution recovery, arbitrary NaT faults, NaTPage handling, complete NaT/RSE context preservation, or another boot/runtime profile.

## Architectural contract

Each general register has data bits and an architectural NaT bit. A set GR.NaT marks a deferred exception token; it is not a distinguished integer data value. A floating-point deferred exception uses the separate NaTVal encoding in an FR. During control speculation, NaT propagates through speculative instructions as dataflow: an instruction that reads a NaTed GR and writes a GR result propagates the token as specified by that instruction's operation. Ordinary register-result integer arithmetic and data-manipulation operations do this, including register-form `add`/`addp4`, logical operations such as `and`, integer multiply such as `mpy4`, and register copies such as `mov r1=r3` (an alias for `adds r1=0,r3`). The exact rule is per instruction and operand form; an immediate-only source does not contribute a NaT bit. Compare instructions that write predicates have separate behavior: with a NaT source, their predicate targets are generally cleared or left unchanged. `tnat` tests a GR.NaT without consuming it. A non-speculative instruction that reads a NaTed GR (except `chk.s`) raises Register NaT Consumption; `chk.s` instead tests the token and branches through its speculation-recovery mechanism. The selected test therefore uses a non-speculative load and does not use `chk.s`.

The control-speculative `ld.s` form can defer eligible exceptions and set the destination NaT. This test makes the deferred token deterministic: it sets `PSR.ed` in the saved IPSR for the restart into `ld8.s r8=[r15]`. The pinned Ski and the target kernel both showed no immediate fault, then `tnat` reported GR8 NaTed. The test uses a known valid pointer cell in ordinary RAM; it does not rely on a bad host mapping or assume that every possible load exception is deferred.

Register NaT Consumption is interruption 43. Its vector is `0x5600`, shared by other NaT Consumption subtypes. In particular, Data NaT Page Consumption is a different interruption (56) caused by memory access to a NaTPage; it is not a register token fault. The NaT vector therefore accepts only the one exact register-consumption site and its qualified ISR pattern. All other NaT vector subtypes remain fail-stop.

For this fault, the record treats `IIP` as the faulting bundle and `IPSR.ri` / `ISR.ei` as the slot. The kernel checks the exact site and slot 0. It also captures `IIPA`, `IFS`, `IFA`, and the simulator’s IIB availability state for audit. `IFS.v` is clear for this event, so there is no valid interrupted stacked frame to unwind. `IFA` is copied for diagnosis but is explicitly marked invalid and is not used to classify the fault or choose the return target. The observed `IIPA` is retained as diagnostic data and is not used as the fault-site identity.

The raw Ski ISR was `0x400000010`. Its architectural read bit was set and the standard `ed`, `ei`, `so`, `ni`, `ir`, `rs`, `sp`, `na`, write, and execute fields were clear, as expected for this slot-0 read. The low `0x10` value comes from the pinned Ski implementation’s `NAT_REG_CODE`; it is an implementation-specific discriminator, not an Intel-defined ISR field. The handler verifies both the standard ISR bits and this pinned simulator value. Ski 1.5.1 does not model `CR.IIB0/IIB1`; the record stores zeros with both availability flags clear. It does not pretend those values were read from hardware.

GR.NaT, UNAT, and RNAT have different roles. A live static GR carries its NaT bit directly. `UNAT` supports NaT collection when general registers are spilled and filled with `st8.spill` / `ld8.fill`; the RSE uses RNAT collection for stacked registers in the backing store. This test keeps the NaTed source in caller-saved static GR8 and does not spill it or consume a stacked GR. The assembly handler makes no C++ call, does not allocate a frame, and does not read or alter RNAT or BSPSTORE. It records the observable `RSC`, `BSP`, and `UNAT` values and verifies the pre/post values needed by this test.

The architectural references are Intel’s [Itanium Software Developer’s Manual, Volume 1](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-1-manual.pdf), §§3.4.3 and 4.4.4, [Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), interruption/vector and ISR tables, and [Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf), instruction definitions including `add`, `addp4`, `and`, and `mpy4`. The pinned Ski source is commit [`dfc2902ea1423d9b32543a5daf8026213f2b37a1`](https://github.com/trofi/ski/tree/dfc2902ea1423d9b32543a5daf8026213f2b37a1); `src/interruption.c` defines `NAT_REG_CODE=0x10` and routes `regNatConsumptionFault` to the NaT vector.

## Exact stimulus and return

The standalone probe and kernel use a dedicated assembly path with explicit register ownership:

```asm
ld8.s r8 = [r15]       // PSR.ed=1 forces a deferred token in GR8
tnat.nz p6, p7 = r8    // confirms the source without consuming it
ld8 r9 = [r8]          // sole non-speculative consumer; result is unused
```

The saved-PSR restart points at the speculative load. After it returns, `PSR.ed` is clear again. The probe confirms GR8.NaT before the ordinary `ld8`. The vector handler validates the faulting `IIP`, slot 0, subtype, standard ISR fields, RSE state, source NaT, and authorized target before changing `CR.IIP` to the continuation and clearing `PSR.ed` in the return IPSR. It restores the saved predicates and uses `rfi`. The continuation’s first instruction reads PSR, before it tests or clears GR8. The skipped load’s destination is not used.

The handler does not repair the source register. GR8 remained NaTed at the handler and after RFI. Only after those observations were recorded, the isolated test cleared GR8 for its own cleanup and checked that the NaT bit was clear. This is evidence for one safe skip-to-continuation path, not a general NaT recovery policy.

## Independent Ski probe

The isolated one-CPU system-mode probe qualified before the kernel run:

| Evidence | Result |
|---|---|
| Probe ELF SHA-256 | `a7e20b47446af18514cf8f71dc5de5b1859fe33e0796ffd8209f6569fed8fcec` |
| Probe manifest SHA-256 | `da5099541143bb632b6be52d7358c491a04b9ed9a6423497d8c4116e737a1c93` |
| Qualification | `NAT_CONSUMPTION_PROBE_PASS`; one fault, one continuation |
| Vector / subtype / instruction | `0x5600` / 43 / slot-0 `ld8 r9=[r8]` |
| GR8.NaT | `1` before the consumer, `1` in the handler, `1` after RFI |
| Return state | `PSR=0x2000`; RSC and BSP unchanged; UNAT unchanged |
| Exit and cleanup | intentional SSC function 66; process status 0; no timeout; cleanup complete |
| Ski identity | version 1.5.1, SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |

The probe disassembly is preserved at `/home/guidex/src/ia64-4f-nat-consumption-probe-20261111-r13/nat-consumption-probe.disassembly.txt`; its full run evidence is in `/home/guidex/src/ia64-4f-nat-consumption-probe-20261111-r13/probe-1/`.

## Candidate and static qualification

The isolated early-exec kernel candidate is `/home/guidex/src/ia64-4f-nat-consumption-kernel-20261111-r3/bin/kernel.elf`, SHA-256 `8b53224a9cc70e51b3db040df5aeda15d329513db2b55f11326f453fd115739f`. Its post-probe manifest SHA-256 is `15ce009ad584bf302344fda55b47a31c3d20516880b2f87f95681c72d7950cab`. The manifest remains `guest_execution_authorized: false`; execution authorization was an explicit runner argument. The candidate-bound direct-loader record is [ia64-4f-ski-loader-record.json](ia64-4f-ski-loader-record.json).

Independent ELF validation and all early-exec, Break, General Exception, CPU-state, Unaligned Reference, and NaT static gates passed. The linked IVT is 32 KiB, aligned at `0x100000`, with the new route at `0x5600`. The candidate contains 68 validated vector slots, no unresolved symbols or relocations, separate W/X load permissions, and a 64 KiB stack disjoint from its 64 KiB RSE backing store. Actual target disassembly confirms the PSR.ed restart setup, the forced `ld8.s`, the exact slot-0 `ld8` consumer, vector route, assembly-only handler, and immediate post-RFI PSR capture. The build profile marks static gates passed but runtime, scheduler, and interrupt-FP preservation unqualified.

The candidate build’s Makefile verifier could not invoke Windows PowerShell with the WSL `/mnt/d/...` path. The Mbed TLS profile verifier was run directly with the documented PowerShell runtime before the candidate build and passed (`MBEDTLS_VERSION=4.1.0`, PSA Crypto `1.1.0`, three profile patches). The isolated build used `POWERSHELL=true` for that already-verified Makefile prerequisite. The initial failed output directories `r1` and `r2` were preserved; the accepted candidate is the separate `r3` directory.

## Single kernel run

The pinned Ski executable and candidate-bound loader record passed offline preflight. The kernel runner was invoked once with `-forcesystem`, one CPU, a 60-second wall timeout, a 2 GiB address-space limit, and 120-second CPU limits. No QEMU or QAC run occurred.

| Evidence | Observed result |
|---|---|
| Kernel execution count | 1 |
| Runner classification | `NAT_CONSUMPTION_KERNEL_PASS` |
| Candidate SHA-256 | `8b53224a9cc70e51b3db040df5aeda15d329513db2b55f11326f453fd115739f` |
| Manifest SHA-256 | `15ce009ad584bf302344fda55b47a31c3d20516880b2f87f95681c72d7950cab` |
| Loader-record SHA-256 | `5093a5c5dfc4104aabc670ee0b1dee7c575e4ffd8e640943492a9c0030137e98` |
| Ski SHA-256 / mode / CPUs | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` / `-forcesystem` / 1 |
| Fault and instruction counts | 5 expected faults; 56,821,269 instructions; 0 unexpected faults |
| NaT event | vector `0x5600`, subtype `0x2b` (43), IIP/site `0x3da490`, slot 0, ISR `0x400000010` |
| Pre-fault stimulus | speculative site `0x3da3e0`; `PSR.ed=1`; GR8.NaT before fault = 1 |
| Handler and RFI | GR8.NaT at handler = 1 and after RFI = 1; target `0x3da4a0`; continuation count 1 |
| Post-RFI state | first-instruction PSR `0x2000` (`i=0`, `ic=1`, `ac=0`); RSC `3`; BSP and UNAT unchanged; GR checksum unchanged |
| Frame / address notes | `IFS.valid=0`; `IFA.valid=0`; IIB0/IIB1 unavailable in Ski 1.5.1 |
| Guest exit / cleanup | intentional SSC function 66; process status 0; no timeout; cleanup complete |

Fault attribution was exact: two Break faults (slot 1 and slot 2), one Illegal Operation at General Exception vector `0x5400`, one Unaligned Reference at `0x5a00`, and the single Register NaT Consumption event above. Every expected record and continuation passed. The immediate post-RFI continuation is candidate-bound; no claim is made about the invalid `IFA` value or Ski’s unavailable IIB registers.

The full run capture, including stdout, stderr, instruction counts, run result, and saved invocation, is preserved in `/home/guidex/src/ia64-4f-nat-consumption-kernel-run-20261111-r1/`. The candidate disassembly and gate logs are in `/home/guidex/src/ia64-4f-nat-consumption-kernel-20261111-r3/`.

## Regression results

| Suite | Tests | Result |
|---|---:|---|
| IA-64 foundation contracts | 17 | PASS |
| Early-exec contracts | 9 | PASS |
| Ski loader contracts | 8 | PASS |
| CPU-state checkpoint contracts | 13 | PASS |
| Break exception contracts | 9 | PASS |
| General Exception contracts | 7 | PASS |
| Unaligned Reference contracts | 8 | PASS |
| NaT Consumption contracts | 8 | PASS |
| **Total contract tests** | **79** | **PASS** |

The ELF validator’s three negative IVT fixtures passed. The Ski diagnostic suite and FP codegen harness passed. The Mbed TLS profile verification passed as noted above. Targeted `bash -n` checks passed for the IA-64 build wrapper, Linux runner, FP codegen harness, NaT probe build, and Ski diagnostic runner.

## Remaining limits

- Only the exact Register NaT Consumption site and subtype are handled. Instruction/Data/IR Data NaTPage consumption and every other NaT-vector subtype remain fail-stop.
- No arbitrary speculative fault recovery, saved task context, stacked-register NaT reconstruction, complete RSE state capture, or general ABI unwinding is implemented.
- The handler’s IFA value is diagnostic only and marked invalid; IIB0/IIB1 are unavailable in the pinned Ski model. RNAT and BSPSTORE were not read in the handler.
- This qualification covers one early-exec, single-CPU Ski run. It does not qualify real Itanium firmware, a hypervisor, SMP, interrupts, scheduling, paging, user mode, or desktop/device initialization.
