# CPU Improvements — CPU7C1 AMD64 Exception-Entry Correctness

- Date: 2026-11-27
- Branch: CPU_IMPROVEMENTS
- Result: **Outcome A — EXCEPTION_ENTRY_CORRECTNESS_QUALIFIED**, with the scope limits in §8. All five suspected defects were real and are fixed. The hosted test runs every production stub through its real return paths, and the emitted assembly is checked. Real exceptions were delivered through the corrected stubs in disposable guests: #BP under TCG and WHPX, and #GP(0) with a hardware error code under TCG. CPU7B3 requalified under TCG. Stock, CPU7B3-diagnostic and CPL3-control guests kept PIC/PIT, input, PacMan and the CPL3 cycles working under WHPX.

Read this as Outcome B if "qualified" must also cover the vectors that cannot be raised safely in a guest (#MF, #AC, #CP, #DF, NMI, #MC; see §8). Those vectors are covered by the architectural tables, the hosted execution of their exact stubs and the disassembly checks only.

## 1. Starting repository identity and provenance

- `E:\dev\guideXOSServer_CPU_IMPROVEMENTS`, branch `CPU_IMPROVEMENTS`, HEAD `1e44c71c9c7c96c3d74e2ce41f82734036144287`, upstream `origin/CPU_IMPROVEMENTS` (`git@github.com:guideX/guideXOS-Server.git`). Public HTTPS `ls-remote` confirmed the remote tip was the same commit. It is the only worktree.
- `git status --porcelain --ignored` was empty at the start.
- Prior evidence under `E:\CodexArtifacts\CPU7B2R1-20261126` and `E:\CodexArtifacts\CPU7B3-20261126` was read and not modified. After CPU7C1, the CPU7B3 GP ISO is still `f3b0a3d5…d838` and the CPU7B3 CPL3 ISO is still `4fdfbb1a…e601`. CPU7C1 output is under `E:\CodexArtifacts\CPU7C1-20261127`.
- I used the CPU7A findings preserved in CPU7B2 §Exception-entry audit and CPU7B3 §2. There is no standalone CPU7A report in this repository.
- Environment: QAC 2.0.0 was online with no sessions or reservations. QEMU was 11.0.0. **PowerShell 7.7.0 is now installed** (`pwsh`). The CPU7B3 report found only 5.1.
- Nothing was reset, cleaned, rebased or force-pushed. One lane build was stopped part-way (I wanted all lanes at one commit). I kept it as `lanes\stock-attempt1-stopped`.

## 2. Error-code classification matrix

The CPU pushes the error code, when there is one, after RIP, so it is the lowest word. In 64-bit mode the CPU always pushes SS:RSP and aligns RSP to 16 first, whether or not the privilege level changes. Sources: Intel SDM Vol. 3A §6.14 and Table 6-1; AMD APM Vol. 2 §8.2 and §8.9.

| Vec | Name | CPU error code | Before CPU7C1 | After | Delivery in this kernel |
| --- | --- | --- | --- | --- | --- |
| 0 | #DE | no | synthetic 0 | synthetic 0 | fault |
| 1 | #DB | no | synthetic | synthetic | fault/trap |
| 2 | NMI | no | synthetic | synthetic | interrupt (IST0, see CPU7C2) |
| 3 | #BP | no | synthetic | synthetic | trap |
| 4 | #OF | no | synthetic | synthetic | trap |
| 5 | #BR | no | synthetic | synthetic | fault |
| 6 | #UD | no | synthetic | synthetic | fault |
| 7 | #NM | no | synthetic | synthetic | fault |
| 8 | #DF | yes (always 0) | hardware | hardware | abort (IST0, see CPU7C2) |
| 9 | reserved (legacy overrun) | no | synthetic | synthetic | — |
| 10 | #TS | yes | hardware | hardware | fault |
| 11 | #NP | yes | hardware | hardware | fault |
| 12 | #SS | yes | hardware | hardware | fault |
| 13 | #GP | yes | hardware | hardware | fault |
| 14 | #PF | yes | hardware | hardware | fault |
| 15 | reserved | no | synthetic | synthetic | — |
| **16** | **#MF** | **no** | **hardware (wrong)** | synthetic | fault |
| **17** | **#AC** | **yes (always 0)** | **synthetic (wrong)** | hardware | fault |
| 18 | #MC | no | synthetic | synthetic | abort |
| 19 | #XM | no | synthetic | synthetic | fault |
| 20 | #VE (Intel) | no | synthetic | synthetic | fault |
| **21** | **#CP** | **yes** | **synthetic (wrong)** | hardware | fault |
| 22–27 | reserved | no | synthetic | synthetic | — |
| 28 | #HV (AMD) | no | synthetic | synthetic | — |
| 29 | #VC (AMD SEV-ES) | yes | hardware | hardware | fault |
| 30 | #SX (AMD) | yes | hardware | hardware | — |
| 31 | reserved | no | synthetic | synthetic | — |

The same set is encoded once as `kExceptionHardwareErrorCodeMask` (`native_elf_fault.h`) and once in the stub table (`exception_entry.asm`). The hosted test checks both against a third table written separately, and the script checks the instructions of each stub.

Other properties apply to every vector:

- **IST.** All 32 gates use IST 0 (gate `0x8E`, DPL 0), so the CPU does not switch stacks for a CPL0 interruption. The common entry then moves the C++ call onto the 16 KiB `gxos_exception_stack`. That stack is also PIC IST1. This sharing is unchanged and belongs to CPU7C2.
- **Privilege.** A fault at CPL3 (the CPL3 diagnostic task only) switches to TSS RSP0, and SS becomes null. The CPL3 exit gate is vector 0x80, not one of these stubs.
- **Software INT n.** INT n to vectors 0–31 cannot come from CPL3, because the gates are DPL 0 and the CPU raises #GP instead. At CPL0 it would push no error code. For an error-code vector that shifts the frame. The new frame check (§3) then rejects the frame and fail-stops.

## 3. Frame layout and stack cleanup

The normalized frame is `NativeExceptionFrame`. It now ends with the CPU-saved RSP and SS, and static_asserts pin its layout to 176 bytes:

```
+0    rax rcx rdx rbx rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15   (15 x 8)
+120  vector
+128  error code   (hardware, or a synthetic 0 pushed by the stub)
+136  rip  cs  rflags  rsp  ss                                     (CPU)
```

How each part of the entry handles the frame:

- **Stub.** A vector with no error code pushes `0`, then the vector. A vector with a hardware error code pushes only the vector. Both then jump to `gxos_amd64_exception_common`.
- **Common entry.** It pushes the 15 GPRs and runs `cld`. It saves the frame pointer in R12, which is nonvolatile. It sets RSP to the 16-aligned top of the exception stack minus 32 bytes of shadow space, so RSP is 16-aligned at the CALL. RCX carries the frame for the Microsoft x64 ABI.
- **Dispositions.** These are new and explicit:
  - `0` resume: restore RSP from R12, pop the 15 GPRs, `add rsp,16` (vector and error code, for every vector), then `iretq`. Before this change, the with-error stubs used `add rsp,8`, which left the hardware word where IRETQ expects RIP. That path was unreachable, because the dispatcher never returned 0, but it was wrong.
  - `1` Native ELF recovery: jump to the recorded kernel continuation, but only if both its RSP and its RIP are nonzero.
  - Any other value, or recovery with no continuation recorded, goes to a `cli; hlt` loop. Before this change, any nonzero value took the recovery jump.
- **Dispatcher.** `gxos_native_exception_dispatch` first calls `classify_exception_frame`. That check requires:
  - vector < 32;
  - a synthetic error code of zero;
  - a hardware error code with its upper 32 bits zero, and exactly zero for #DF and #AC;
  - CS 0x08 with SS 0x10 or null, or CS 0x23 with SS 0x1B;
  - RFLAGS bit 1 set, VM clear and bits 22–63 clear;
  - canonical RIP and RSP.

  A malformed frame prints `[EXCEPTION-FRAME] malformed reason=…` and halts. It never reaches recovery or user-fault accounting. The dispatcher now reports the CPU-saved RSP and SS. Before, it estimated RSP as `frame + sizeof(frame)` and printed the current SS. It also uses the saved RSP for the Native ELF `stackInRuntime` check and the stack dump.
- **Policy is unchanged.** A CPL3 fault is still recorded and halts. A CPL0 fault with an active Native ELF runtime whose RIP or stack is in the app still recovers to the desktop. Every other kernel fault still halts. No path returns 0 in the stock kernel. Only the opt-in self-test (§4) resumes, and only for its own armed exception.

The CPU7B3 fixup is unchanged. It still discards exactly one error word itself on its accepted path, and it tail-jumps to `exception_stub_13` with the hardware frame intact on rejection. `exception_stub_13` is still an error-code stub, so that contract holds. Its linked address moved to `0x2447E0`.

## 4. Source and assembly changes

Commits `12403097` ("correct AMD64 exception error-code and DF entry contract") and `c290d203` ("add opt-in cold-boot exception entry self-test"):

- `kernel/arch/amd64/exception_entry.asm` (new): the 32 stubs, the common entry and the dispositions. The kernel Makefile already assembles every `arch/amd64/*.asm` with `nasm -f win64`.
- `kernel/core/interrupts.cpp`: the inline exception-stub macros are removed and only declarations remain. Both PIC IRQ stub variants get `cld`. IDT setup, gate types, IST assignment, PIC handling and the interrupt-enable point are unchanged.
- `kernel/arch/amd64/protected_task.asm`: `cld` in the CPL3 exit stub after the 15 GPR saves.
- `kernel/core/include/kernel/native_elf_fault.h`: RSP and SS added to the frame, the layout asserts, the classification mask, the disposition constants and the frame check. All of this is internal and not part of the Native ELF ABI.
- `kernel/core/native_elf_baremetal.cpp`: the frame check, the saved RSP/SS, and the named recovery disposition.
- `kernel/arch/amd64/exception_entry_selftest.asm` and the `EXCEPTION_ENTRY_DIAGNOSTIC` Makefile flag (default 0, which leaves the file out of the build): this is the opt-in guest probe. It runs on the cold BSP after the live IDT is loaded and before STI. It loads known values into all 15 GPRs, sets DF and raises one exception:
  - `int3` (vector 3, synthetic error code);
  - RDMSR of the reserved x2APIC MSR 0x800 (vector 13, hardware error code under TCG).

  An armed intercept in the dispatcher accepts only the probe's exact vector, RIP, RSP and CS. For #GP it moves the saved RIP past the RDMSR. It then returns the resume disposition. The probe fail-stops unless all of these hold: all 15 GPRs restored, RSP restored, DF set in the frame, DF clear in the dispatcher, and DF restored by IRETQ.
- Tests: `tests/amd64_exception_entry_test.cpp`, `tests/amd64_exception_entry_probe.asm`, `scripts/test-amd64-exception-entry.ps1`.

No LAPIC MMIO, IOAPIC, IPI, AP startup, SMP, preemption, BootInfo, Native ELF ABI or cross-branch change.

## 5. Direction-flag audit

| Path | Enters C++ | Before | After | Flags on return |
| --- | --- | --- | --- | --- |
| PIC IRQ stubs (MS and SysV variants) | `irq_dispatch` | no `cld` | `cld` after the GPR saves | IRETQ restores the interrupted RFLAGS, including DF |
| CPU exception common entry | `gxos_native_exception_dispatch` | no `cld` | `cld` immediately after the GPR saves | resume: IRETQ restores DF. Recovery: continues in kernel C++ with DF=0, which the ABI requires |
| CPL3 exit stub (vector 0x80) | `gxos_amd64_user_exit_dispatch` | no `cld`. CPL3 controls DF and the gate does not clear it | `cld` after the GPR saves | resumes with the kernel continuation's own saved RFLAGS, so user DF never reaches the kernel |
| Native ELF recovery (`native_elf_call.asm .fault_return`) | returns into C++ | DF from the faulting app | reached only through the common entry, so DF=0 | — |
| CPU7B3 fixup handler | no C++ | rejects DF on its accepted path | unchanged. The rejected path enters `exception_stub_13`, which now clears DF | IRETQ |
| `boot.asm _start` | `kernel_main` | already `cld` | unchanged | — |
| `context_switch_asm.asm`, `native_elf_call.asm` entry | called from C++ | DF=0 by ABI | unchanged | — |

DF is cleared only after the interrupted state is saved, and the original RFLAGS stays in the CPU frame. IF policy is unchanged: interrupt gates still clear IF, and no `sti` was added. SMAP is not enabled, so RFLAGS.AC has no kernel effect today. A future SMAP change would need `clac` at the same three boundaries.

## 6. Hosted regression results

`scripts/test-amd64-exception-entry.ps1` (new) assembles the **production** `exception_entry.asm` and links it with a harness. For each vector, the harness builds the same frame the CPU would (aligns RSP to 16; pushes SS, RSP, RFLAGS, CS, RIP and the error word where there is one). It sets the live DF the way an interrupt gate leaves it and jumps into the real stub. A same-privilege IRETQ is legal at CPL3, so the stub's real resume path returns to the harness. Result: **2,804 checks, 0 failures.** Coverage:

- all 32 vectors, each with DF=0 and DF=1;
- the normalized frame: vector, hardware or synthetic error code, all 15 GPRs, CPU RSP and DF, and the frame address;
- in the dispatcher: DF clear, `RSP ≡ 8 (mod 16)` at entry, and 32 bytes of shadow space inside the exception stack;
- after IRETQ: the exact interrupted RSP (cleanup), all 15 GPRs while the dispatcher clobbers every volatile register, and the interrupted DF;
- the recovery path with DF clear at the continuation;
- the frame check: valid kernel and user frames, a null CPL0 SS, every rejection reason, and the shifted frames the old #MF, #AC and #CP stubs would have produced.

Mutation check: assembling a copy with the old #MF/#AC classification, without `cld`, or with `add rsp,8` makes the test fail every time (an access violation for the shifted stacks, and 39 DF failures without `cld`).

**Emitted-assembly checks.** From `objdump -dr`:

- each stub is exactly `push 0; push N; jmp common` or `push N; jmp common`, matching the classification;
- the common entry pushes GPRs in order, `cld` comes straight after, there is one CALL and its target is the dispatcher, then alignment and shadow space;
- the resume path is `mov rsp,r12`, 15 pops, `add rsp,0x10`, `iretq`, with exactly one IRETQ;
- the recovery path has its checks, and the fail-stop is `cli`/`hlt`;
- the CPL3 exit stub has `cld` after its 15 pushes and before its call.

With `-KernelObjectDirectory`, the script also checked the lane's compiled `interrupts.o`. All 16 IRQ stubs have `cld; mov ecx,N` before the call and end in IRETQ, and no inline exception stubs remain. The lane's `exception_entry.o` disassembles identically to the hosted object (483 lines, 0 differences).

**Existing regressions.** All ten earlier amd64 runners pass: boot page tables, BSP APIC, APIC-base probe (6 groups, including the CPU7B3 matcher), xAPIC admission, context switch, CPU capabilities, memory foundation, per-CPU, processor inventory, and protected-task contract. The Native ELF security policy test also passes (`failures=0 nativeEntryCalls=4`). Logs are in `hosted\`.

The hosted tests run at CPL3 with a synthetic frame. They are not real exception delivery.

## 7. Kernel and ISO hashes

All lanes are independent full clones at `c290d203`. Each was built with `create-release-iso.ps1 -ReleaseProfile ServerV03PacManOnly -RequireCleanWorktree -BootstrapTools`. Every ISO is 90,245,120 bytes.

| Lane | Flags (probe / GP / inventory / CPL3 / entry self-test) | Build ID | Kernel ELF bytes / SHA-256 | ISO SHA-256 |
| --- | --- | --- | --- | --- |
| Stock | 0/0/0/0/0 | 5f7b5ae4-b496-4b2c-b5d6-0800bfecc6b5 | 2,838,976 / 020ab6f02b8a7c75006fcf5cfdcd19ccc47b184c6f169adb5b66ed72dc767144 | fbabad6777753d19370a5fa718f84d1ec689c7637e69b6376f79c6ff5dacc878 |
| CPU7B3 GP diagnostic | 1/1/0/0/0 | a949b321-a92e-44cd-8f86-28f32ae4f120 | 2,851,960 / f73a0b7281938e1ce5d02d36e2d25273870c73593651f05ac384ab96b02b1068 | 18057d0e2564dd0e82b01bdfad304462159cb4829344ba00be7ba32f0c8d61e2 |
| CPL3 control | 1/0/1/1/0 | 88682881-8b8e-4c06-88df-2a7d6e6c6a0a | 2,879,016 / c1d6e96347f4eb2fe0127e8b33d22d098e2982c6df780a05d7a592517513598a | 754a345278381e6e03de033ab4def44cf48dd9dc7dd73ad41857dee6a775289d |
| Entry self-test | 0/0/0/0/1 | e3a2feb9-783c-4a32-abd2-de07710af1ac | 2,844,640 / 51ad20ea9b9f4579802bfb61252a29e0d604db57403713347d65ac16cc97360f | c2ba79cba9d95c8ab2958c5894ebc85c8df5d297045efe77aa8815e769f32298 |

The stock kernel differs from the CPU7B3 stock kernel (`c3d69e0f…`) as intended. The kernel in the ISO's ESP matches the build output for the self-test lane. OVMF code is `33090cc0…b2a` before and after every run.

## 8. TCG and WHPX guest results

Each run used one disposable QAC guest with read-only OVMF and ISO, no network and no writable disk. Teardown was QMP quit under a host-side deadline. The ISO hash was the same after every run. Logs are under `guest\` and `qac\`.

### TCG (`-d int` as independent host evidence)

| Run | Lane | Host `-d int` exceptions | Guest result |
| --- | --- | --- | --- |
| tcg1 | Entry self-test | exactly one `v=03 e=0000 i=1 cpl=0 IP=0008:24361b` and one `v=0d e=0000 i=0 cpl=0 IP=0008:24372c`, both at `SP=0010:22b84a20`. No NMI, #DF or other fault. 3,012 IRQ0 and 1 IRQ12 through the PIC stubs | `bp-entry=resumed … rip=0x24361C rsp=0x22B84A20 ss=0x10 frame-df=1 dispatcher-df=0 resumed-df=1 rsp-restored=1 gprs=0x7FFF`, `gp-entry=delivered vector=0xD error=0 rip=0x24372C … rsp-restored=1 gprs=0x7FFF`, `exception-entry-selftest=passed`, then desktop and 21 PIT heartbeats |
| tcg2 | CPU7B3 GP diagnostic | exactly one `v=0d e=0000 i=0 cpl=0 IP=0008:24450e SP=0010:22b88a10`. 2,171 IRQ0 | `[CPU7B3] x2apic-reserved-msr-gp=delivered vector=13 error=0 rip=0x24450E … continuation=0x24451E reached=1 integrity=0x7 result=suppressed cleanup=clean`, 0xFFFFFFFF not delivered, follow-up `0xFEE00900`, CPU7B1 one online, desktop, PIT heartbeats to tick 0x3E8 |

In tcg1, the two RIPs are `gxos_amd64_selftest_bp_resume` (0x24361C, the trap RIP after INT3 at 0x24361B) and `gxos_amd64_selftest_gp_rdmsr` (0x24372C) in that kernel's symbol table. This is genuine delivery through the new `exception_stub_3` and `exception_stub_13`, the common entry, the C++ dispatcher and the production IRETQ, with a synthetic and a hardware error code respectively. In tcg2, the RIP and continuation match `gxos_amd64_apic_base_rdmsr` and `gxos_amd64_apic_base_fault` in the GP kernel. The exact-site contract and its integrity bits are unchanged by CPU7C1.

### WHPX regressions

| Lane | Result |
| --- | --- |
| Stock (formal S22R10 runner under pwsh; reservation 41bdd9d6effd409893e43190f46b654c) | Desktop, Start menu, pointer, F12 PacMan, left/up input, Escape. `exit result=0x0 fault=none frames=0x185 inputEvents=0xD`, `lifecycle PASS`. The guest stayed up about 2.5 hours (see below) with 6,502 PIT heartbeats and no exception |
| CPU7B3 GP diagnostic (`qac run --mode acceptance` + QMP) | Both qualification reads not delivered, suppressed and clean (as expected under WHPX). Follow-up `0xFEE00900`, CPU7B1 one online, PacMan `exit result=0 fault=none`, `lifecycle PASS`, 109 heartbeats |
| CPL3 control (same procedure) | CPU3 roster 2 descriptors, 1 online. Three `[CPL3]` entries at CS 0x23, vector 0x80 traps with kernel stack verified and CR3 restored, `status=passed cycles=3`. PacMan `exit result=0 fault=none inputEvents=0xD`, `lifecycle PASS`, 87 heartbeats |
| Entry self-test (boot only) | **#BP delivered by hardware-assisted virtualization:** the `bp-entry=resumed` line is identical to TCG, with every check passing. `gp-entry=not-delivered rdmsr-returned=1` as expected for QEMU 11.0.0 WHPX. PIT heartbeats follow |

I did not repeat the unsupported WHPX #GP qualification. Its "not delivered" result in the GP lane is a known control.

**QAC classifier.** I ran the S22R10 classifier `Get-QacS22R10GuestResult` on all three WHPX serial logs. It returns `INCOMPLETE: No existing PacMan gameplay PASS/FAIL marker was observed.` for each, the same outcome as CPU7B2R1. These kernels emit no `[PACMAN-SMOKE] result=` marker. The gameplay and lifecycle results above are independent serial and QMP observations, not formal acceptance.

**Formal runner incident.** The stock run used the real S22R10 runner under PowerShell 7. That runner requires a `result pass|fail` before `shutdown`. `result pass` is rejected while the classifier is INCOMPLETE, so the reused command file's `shutdown` was rejected. The runner then exited on end of input ("QAC ownership and the workspace remain protected"), with QEMU still running. A session restart on my side meant I did not notice for about 2.5 hours.

I then identified the guest by its QEMU command line (my stock ISO and workspace) and its reservation (my runner's). I sent QMP `quit` to its own QMP endpoint (`SHUTDOWN reason=host-qmp-quit`). QEMU exited and the `qac run` owner exited, after which QAC reported zero QEMU processes, sessions and reservations. The runner's execution record stays at `RecoveryRequired / INCOMPLETE` as evidence. Running the formal runner unattended needs either a guest-initiated shutdown sequence (as in CPU7B2R1) or a `result fail <reason>`. The second would record FAIL, so I used the CPU7B3 `qac run` procedure for the other lanes.

### Scope limits

#MF, #AC, #CP, #DF, NMI and #MC were not raised in a guest:

- #AC needs CR0.AM and an unaligned access at CPL3.
- #CP needs CET.
- #MF needs unmasked x87 exceptions.
- #DF, NMI and #MC would run on the shared stack that CPU7C2 owns.

Their stubs are the same two macro forms that #BP and #GP exercised in the guest, and they are covered by the hosted execution and disassembly checks.

## 9. QAC cleanup and reservation status

| Run | Reservation | QEMU exit | Release |
| --- | --- | --- | --- |
| tcg1, tcg2, entry self-test WHPX, GP WHPX, CPL3 WHPX | per run (experiment.json) | QAC exit 0, 0 QEMU after | verified |
| Stock WHPX (formal runner) | 41bdd9d6effd409893e43190f46b654c | QMP quit after the incident above | verified (owner exited, reservation gone) |

Final QAC status: online, 0 QEMU processes (0 foreign), 0 sessions, 0 queued, 0 blocked. Other workstreams' build processes (guideXOSServer_NA) were running on the machine at the time. I did not touch them, and none of them had QAC guests.

## 10. Remaining CPU7C2 / CPU7C3 prerequisites

**CPU7C2.**

- The exception common entry still reloads RSP to the top of `gxos_exception_stack`, which is also PIC IST1. A nested exception, or an exception during IRQ dispatch, overwrites the live dispatch frame.
- NMI, #DF and #MC use IST0 in the live IDT and have no dedicated stacks.
- Before any vector may resume in production, the resume disposition (now correct and tested) needs nesting protection or per-vector stacks.
- The Native ELF recovery jump continues with IF still clear from the interrupt gate. That is the existing behavior and was not changed here.

**CPU7C3.**

- `irq_dispatch` EOIs after every handler, with no IRQ7/IRQ15 ISR check for spurious interrupts.
- EOI ownership and PIC acknowledgment are unchanged.

**Integration.**

- Branches that carry their own inline exception stubs, or another `NativeExceptionFrame`, must adopt `exception_entry.asm` and the 176-byte frame together.
- The SysV IRQ stub variant has `cld`, but the exception entry is Microsoft x64 only. It was before too, through the inline `mov rcx` stubs.

## 11. Repository result

- Commits on `CPU_IMPROVEMENTS`: `12403097`, `c290d203`, and this report.
- When I checked the remote before pushing the report, `origin/CPU_IMPROVEMENTS` already pointed at `c290d203`, so the two source commits had reached the remote while I was away. Only the report commit was pushed afterwards. I did not force-push, rebase or merge to main.
