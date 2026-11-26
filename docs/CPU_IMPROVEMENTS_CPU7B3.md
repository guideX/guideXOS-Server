# CPU Improvements — CPU7B3 Exception Delivery Investigation

- Date: 2026-11-26
- Branch: CPU_IMPROVEMENTS
- Result: **Outcome A, scoped to QEMU TCG.** Real #GP(0) delivery and exact-site recovery are qualified in a disposable QEMU 11.0.0 TCG guest. Hardware-assisted delivery under WHPX remains unavailable, because QEMU 11.0.0 emulates every MSR the hypervisor does not handle as "return 0". No fault was injected on physical hardware.

The CPU7B2R1 guest did not see an exception because none was raised. Under WHPX, Hyper-V forwards MSRs it does not implement to QEMU as an MSR-access exit. QEMU 11.0.0 zeroes RAX/RDX, advances RIP past the instruction and injects nothing. TCG does the same for an unknown index. Switching accelerators alone does not help. A different index does help: the reserved x2APIC-range MSR 0x800 raises #GP(0) architecturally and under TCG. Under WHPX it still takes the same return-zero path.

If "qualified" must mean hardware-assisted virtualization, this result is Outcome B. The TCG evidence is genuine architectural delivery through the guest IDT, handler and IRETQ, independently logged by the host, but the CPU is emulated.

## 1. Starting repository state

- `E:\dev\guideXOSServer_CPU_IMPROVEMENTS`, branch `CPU_IMPROVEMENTS`, HEAD `4a5159dde59e1c8e01e6c1eb2db4eb62b739a1d2`, equal to `origin/CPU_IMPROVEMENTS`. It is the only worktree.
- `git status --porcelain --ignored` was empty and there was no stash. The earlier Codex attempt left no uncommitted, untracked or ignored files in this checkout.
- Existing evidence under `E:\CodexArtifacts\CPU7B2R1-20261126` and earlier directories was read in place and not modified. CPU7B3 output is under `E:\CodexArtifacts\CPU7B3-20261126`. Two failed build attempts (a Python preflight issue in my wrapper) and one failed runner attempt were renamed `*-attempt1-*` and kept.
- Nothing was reset, cleaned, rebased, merged or pushed.

## 2. Source and exception-contract findings

The CPU7B2R1 exact-site path in `kernel/arch/amd64/bsp_apic_base_fixup.asm` and `kernel/core/interrupts.cpp` is architecturally sound for a CPL0 → CPL0 #GP in 64-bit mode:

- **Frame.** With IST 0 and no privilege change, the CPU aligns RSP to 16 and pushes SS, RSP, RFLAGS, CS, RIP and the error code. After its three pushes the handler addresses that frame at `[rsp+24]`: error +0, RIP +8, CS +16, RFLAGS +24, RSP +32, SS +40. The match checks the armed RSP against the saved RSP, not the aligned entry RSP, which is correct.
- **Fault RIP.** #GP is a fault, so the saved RIP is the RDMSR itself. TCG's `gen_RDMSR` calls `gen_update_eip_cur` before the helper, and the helper raises with `GETPC()` unwinding, so it reports the same RIP.
- **Handler.** It touches only RAX, R10 and RFLAGS and restores them. It changes only the saved RIP, drops the one error word and returns with IRETQ. DF is rejected on entry, and the interrupt gate clears IF. A mismatch restores its scratch state and tail-jumps to `exception_stub_13` with the frame unchanged.
- **Cleanup.** Both paths clear the armed RSP and index before `lidt [rsp]` restores the saved IDTR, then `popfq` and `ret`. The temporary IDT gives NMI and #DF their own IST2/IST3 halt stubs.
- **Microsoft x64 ABI.** The reader uses only volatile registers (RAX, RCX, RDX, R8–R11) and keeps stack balance on every return.

Two weaknesses were found and fixed in the opt-in lane:

1. **Fail-closed publication was index-specific.** The reader suppressed a returned value only when the index was exactly `0xFFFFFFFF`. Any other qualification index would have published whatever an emulator returned. Publication is now allowed only for `0x1B`.
2. **No register or stack integrity evidence after IRETQ.** The continuation now records whether RSP equals the armed RSP and whether R11D and ECX still hold the armed index. A pass requires all three bits (`integrity=0x7`).

The earlier review's legacy findings still stand and are not changed here: the shared IST1 PIC/exception C++ stack, NMI and #DF on IST0 in the live IDT, the #MF/#AC/#CP error-word stub misclassification, the missing `cld` in the IRQ/exception/CPL3-exit stubs, and PIC handling that EOIs after dispatch without an IRQ7/IRQ15 spurious (ISR) check. See CPU7B2 §Exception-entry audit.

## 3. QEMU/WHPX behavior and evidence

### Source (QEMU v11.0.0, read from gitlab.com/qemu-project/qemu)

- `target/i386/whpx/whpx-all.c`: the partition setup enables `ExtendedVmExits.X64MsrExit`. The `WHvRunVpExitReasonX64MsrAccess` handler sets RIP to `Rip + InstructionLength` and writes zero RAX/RDX on reads, under the comment "For all unsupported MSR access we: ignore writes, return 0 on read". It calls `trace_whpx_unsupported_msr_access` and never sets a pending exception. The only special case is the Hyper-V APIC frequency MSR. v11.0.0 has no option to change this. Upstream master has since reworked this path; the summary I read says unknown MSRs now raise #GP unless an `ignore-unknown-msr` property is set, but I could not verify that verbatim.
- `target/i386/tcg/system/misc_helper.c` `helper_rdmsr`: the `default:` case returns `val = 0` (`/* XXX: exception? */`) after the MCE-bank range. The `MSR_APIC_START ... MSR_APIC_END` case calls `apic_msr_read`, which returns −1 when the APIC is not in x2APIC mode (`hw/intc/apic.c`). The helper then calls `raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC())`.
- The installed binary (`C:\Program Files\qemu\qemu-system-x86_64.exe`, `v11.0.0-12122-ga4bb4b10c9`, built 2026-04-21) contains the `whpx_unsupported_msr_access` trace event and no `ignore-unknown-msr` property, which is consistent with v11.0.0.

**CPU model.** Both paths are in accelerator code that does not consult `-cpu`, so changing the CPU model does not change the result for an unknown index. This is from source; I did not run a different `-cpu`.

**Unobserved delivery.** Neither run raised an exception for the guest to miss. The host-side trace shows QEMU completing the WHPX exit as a zero read, and the TCG `-d int` log contains no vector 0x0d for that RDMSR.

### Guest experiments (QAC development reservations, read-only ISO and OVMF, no network or writable disk, QMP quit)

| Run | ISO | Accelerator | Host evidence | Guest serial |
| --- | --- | --- | --- | --- |
| exp1 | CPU7B2R1 GP `1ff38a37…` | WHPX | Exactly one `Unsupported MSR access (0xffffffff), IsWrite=0` trace | `invalid-msr-gp=not-delivered` |
| exp2 | CPU7B2R1 GP | TCG | `-d int`: no `v=0d` anywhere | `invalid-msr-gp=not-delivered` |
| exp3 | CPU7B3 GP `f3b0a3d5…` | TCG | `-d int`: exactly one `v=0d e=0000 i=0 cpl=0 IP=0008:000000000024508e SP=0010:0000000022b88a10`; no vector 2 or 8 | 0xFFFFFFFF not-delivered; **0x800 delivered** |
| exp4 | CPU7B3 GP | WHPX | Traces for `0xffffffff` and `0x800`, both reads | both not-delivered, suppressed, clean |
| exp5 | CPU7B3 GP | TCG | Same delivery, identical RSP | delivered, then PIT heartbeats through tick 0x5DC and desktop initialization |

The QAC reservations were `24ee0bc6…`, `57e5a339…`, `0a019a86…`, `af77b695…` and `02979d04…`. Each released with QEMU exit 0. The ISO and OVMF hashes were unchanged after every run.

## 4. Recommended correction and test strategy

- Keep the opt-in qualification on a disposable **QEMU TCG** guest using reserved x2APIC MSR **0x800**, with host `-d int` as independent evidence. That combination is architecturally specified to raise #GP(0) and does so in the emulator's source and in practice. It reads no APIC register and maps no LAPIC MMIO.
- Do not treat the WHPX profile as able to qualify RDMSR faults with QEMU 11.0.0. A WHPX qualification would need a QEMU build with the upstream unknown-MSR #GP change (verify it first), or a different hardware-assisted backend. I did not evaluate either.
- Keep `0xFFFFFFFF` as a documented not-delivered control. Its WHPX and TCG behavior is now explained.
- Do not use physical hardware for this fault.

## 5. Implementation changes

Commit `25738942` "CPU7B3: add x2APIC-reserved exact-site #GP qualification read", local only:

- `bsp_apic_base_fixup.asm`: a `require_publishable_index` gate allows publication only for 0x1B. The continuation records integrity bits in a tenth evidence quadword. A hosted hook was added for the publication gate. The handler, matcher and single RDMSR site are unchanged.
- `interrupts.cpp`: the GP qualification lane now runs two reads: `[CPU7B2R1] invalid-msr-gp=` for 0xFFFFFFFF and `[CPU7B3] x2apic-reserved-msr-gp=` for 0x800. Each verifies cleanup and the sentinel and fails closed. A pass also requires `integrity=0x7`. A rejected result prints its evidence. The follow-up 0x1B read is unchanged. Everything stays behind `BSP_APIC_BASE_GP_QUALIFICATION_DIAGNOSTIC=1`, which still requires the probe flag.
- `tests/amd64_bsp_apic_base_probe_test.cpp`: a sixth group covers matcher index exactness for 0x800, 0x801, 0x8000001B, 0xFFFFFFFF and 0, armed-context idleness, and that only 0x1B publishes.
- `scripts/test-amd64-bsp-apic-base-probe.ps1`: the emitted-assembly check now requires the `cmp $0x1b,%r11d` gate, the absence of a 0xFFFFFFFF gate, and the continuation integrity record.

The stock build is unaffected. Production exception handling (`exception_stub_*`, the dispatcher, the PIC path) was not changed.

## 6. Hosted and guest verification

**Hosted.** All ten `scripts/test-amd64-*.ps1` runners exited 0 (boot page tables, BSP APIC, APIC-base probe with 6 groups, xAPIC admission, context switch, CPU capabilities, memory foundation, per-CPU with 7 groups, processor inventory with 10 groups, protected-task contract). Logs are in `hosted-regressions`.

**Builds.** These are independent full clones at `25738942`, built with the canonical `create-release-iso.ps1 -ReleaseProfile ServerV03PacManOnly -RequireCleanWorktree -BootstrapTools`. Each ISO is 90,245,120 bytes.

| Lane | Flags (probe / GP / inventory / CPL3) | Build ID | Kernel SHA-256 | ISO SHA-256 |
| --- | --- | --- | --- | --- |
| GP diagnostic | 1 / 1 / 0 / 0 | 2b764564-e70c-460e-8776-80031ef29fdb | c645f698…2b84d8 (2,853,224 bytes) | f3b0a3d55495e34f0e6e437b52337016507b11bdd58f5a5a212cfe508304d838 |
| CPL3 control | 1 / 0 / 1 / 1 | 63e6def8-d181-4d8d-aa1e-4a71ed3b1b74 | d3167983…9eddd2d6 (2,880,272 bytes) | 4fdfbb1a5018651a5b2d646a5a9876324780ecae7d01b45c53a729a59fabe601 |
| Stock (kernel only) | all 0 | — | c3d69e0f7f63b9fab00895f0d646c8da87959858f3a7915638002349ed1fc595 | — |

The stock kernel is byte-identical to the CPU7B2/CPU7B2R1 stock kernel, so the stock guest qualification carries over. In the GP kernel, RDMSR is at `0x24508E`, the authorized continuation at `0x24509E`, the IRETQ at `0x2451ED`, and the unrelated path jumps to `exception_stub_13` at `0x175FC7`. The linked window is in `assembly\gp-diagnostic-probe-window.txt`.

**Qualification (exp3 and exp5, TCG).** The serial line was:

`[CPU7B3] x2apic-reserved-msr-gp=delivered vector=13 error=0 rip=0x000000000024508E cs=0x08 rsp=0x22B88A10 rflags=0x10046 ss=0x10 continuation=0x000000000024509E reached=1 integrity=0x7 result=suppressed cleanup=clean`

| Requirement | Evidence |
| --- | --- |
| Genuine delivery | Host `-d int` logged the vector, independent of guest code |
| Vector 13, error 0 | Host `v=0d e=0000`; guest frame error word 0 |
| Exact faulting RIP | Host IP `0008:24508e` = guest RIP = linked RDMSR |
| Authorized continuation | Saved RIP rewritten to `0x24509E`; `reached=1` |
| Register and stack integrity | RSP restored to the armed `0x22B88A10`, which equals host SP; R11D and ECX preserved (`integrity=0x7`); the IDTR saved on that stack was restored intact |
| Restored IDTR, clean probe state | `cleanup_verified()` (SIDT equals the kernel IDTR, armed RSP and index zero) and sentinel unchanged; otherwise fail-stop |
| No interception of unrelated faults | Only one #GP occurred in the window, and no NMI or #DF. Matcher rejection of every other vector, RIP, RSP, selector, flag and error variation is covered by hosted tests. A real unrelated fault was not induced |
| Subsequent normal execution | Follow-up 0x1B read `0xFEE00900`, CPU7B1 `ownership-unverified`, one CPU online, PIC enabled, desktop initialization, PIT heartbeats to 0x5DC |

**Regression under WHPX (QAC acceptance-mode reservations).** I could not use the established S22R10 runner. It uses `ProcessStartInfo.ArgumentList` and `[IO.File]::Move(..., $true)`, which need PowerShell 7, and only Windows PowerShell 5.1 is installed now. A BOM-only copy confirmed that the 5.1 parse failure is caused by the unmarked UTF-8 em dash at line 658; the API dependency remains after that. Instead, I ran `qac run --mode acceptance` with the S22R18 ISO-only launch arguments, `-S` aside, and drove the same keyboard/pointer/F12/arrow/Escape sequence through QMP. The S22R10 classifier did not run.

| Lane | Reservation | Result |
| --- | --- | --- |
| GP diagnostic | 8d11976e0f764340b005bf5df13301a0 | Both qualification reads not-delivered, suppressed and clean. Follow-up `0xFEE00900`, CPU7B1 one online. Desktop, Start, pointer, PacMan launched and played (score 210 in the screenshot), Escape, `exit result=0 fault=none`, `lifecycle PASS`. 47 PIT heartbeats. QEMU exit 0 |
| CPL3 control | 4ee9f54b45264e15b23f238d08d12109 | `[CPL3] status=passed cycles=3 nonfaulting=1 preemption=0`, CS 0x23, SS 0x1B, IF=0, vector 0x80 exits. CPU3 roster 2 descriptors, 1 online. PacMan play, exit 0, lifecycle PASS. 47 heartbeats. QEMU exit 0 |

After the runs, QAC was online with zero QEMU processes, sessions, reservations and queue entries. No LAPIC MMIO, IOAPIC, IPI, AP startup or SMP path was enabled. The pre-existing QEMU warning "Ignoring request for interrupt vector 0" appears in all CPU7B2R1 lanes and in these runs.

## 7. Remaining blockers

1. Hardware-assisted (WHPX) #GP delivery for RDMSR is impossible with the installed QEMU 11.0.0. Qualifying it needs a newer QEMU with verified unknown-MSR #GP semantics.
2. The S22R10 acceptance runner needs PowerShell 7, which is not installed. Rerunning the formal runner and its classifier needs it installed. I did not install it.
3. The unrelated vector-13 tail path to `exception_stub_13` is verified only by hosted tests and disassembly. Exercising it would need a second, deliberately mismatched fault, which also runs through the legacy dispatcher's known defects.
4. The emergency NMI/#DF IST stubs were not exercised.
5. The legacy entry defects in §2 remain.

## 8. Next milestone recommendation

Proceed to **CPU7C: AMD64 interrupt-entry correctness.** Give NMI, #DF and #MC dedicated IST stacks in the live IDT. Separate the exception stack from the PIC IST1 dispatch stack, or add nesting protection. Correct the #MF/#AC/#CP error-code classification. Add `cld` to every entry stub. Add IRQ7/IRQ15 spurious detection through the PIC ISR. Qualify those in TCG with `-d int` using the same disposable-guest method, then under WHPX for regressions. Keep LAPIC mapping and SMP deferred behind the CPU7B1 prerequisites.
