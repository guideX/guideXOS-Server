# CPU Improvements — CPU7B2R1 Exact-Site #GP(0) Qualification

- Date: 2026-11-26
- Branch: CPU_IMPROVEMENTS
- Result: **Outcome B — isolated guest qualification completed, exact hardware fault delivery remains open.**

The default-off qualification lane was built and exercised in disposable QAC guests. In the selected QEMU 11.0.0 WHPX profile, the read of reserved MSR index 0xFFFFFFFF returned without delivering vector 13 to the exact-site handler. The diagnostic suppressed the output value, verified cleanup, and then completed a valid IA32_APIC_BASE read. This is safe partial qualification; it is not evidence of #GP(0) delivery or recovery.

## 1. Starting state and source commits

- The recovered E: repository was on CPU_IMPROVEMENTS at 36c645023bf934e17a90e16303ef0fe65f3ec97d. The index and worktree were clean.
- The configured origin/CPU_IMPROVEMENTS tip was dcd0307d8be850d8b0ae0325f4383d433bd48849. The local branch was one commit ahead.
- Recovery commit 96342c70 was an ancestor of the starting HEAD.
- The recovered implementation was not reconstructed again. Source changes for this qualification were committed as 7a3c65f42ff2f4641b5f6dbcf54a3a96d4a5c87b, “CPU7B2R1: add exact-site GP qualification lane.”
- This report is a separate follow-up commit. Both commits remain local; nothing was pushed, merged, rebased, reset, or cleaned.

The source commit changes only the AMD64 probe assembly, probe caller, kernel build flag/stamp, hosted probe fixtures, and the emitted-assembly checker. It does not change BootInfo or the Native ELF ABI. The default build does not compile the probe or qualification lane.

## 2. Fault-recovery contract and scope

The qualification option is BSP_APIC_BASE_GP_QUALIFICATION_DIAGNOSTIC. It defaults to zero and the Makefile rejects it unless BSP_APIC_BASE_PROBE_DIAGNOSTIC is one. The stock build has all diagnostic options disabled. The invalid MSR read exists only in the explicitly enabled GP qualification diagnostic and was run only in disposable QAC guests.

The reader is entered from cold interrupt initialization after the kernel GDT/IDT setup and before the PIC/PIT path is enabled. The pure probe owner requires an online bootstrap processor at logical index zero, exactly one online roster entry, and the required long-mode, APIC, and MSR capabilities. It atomically acquires the probe owner and rejects reentrancy. The assembly reader is one-shot and additionally checks the current kernel CS/SS, loaded task register, the expected TSS with RSP0 still zero, a safe interrupted flags state, and the expected live IDTR. It disables maskable interrupts while the detached IDT is active and declines CET shadow-stack or FRED contexts whose fault frame is not covered by this contract.

The temporary IDT changes only vector 13 and installs separate no-return emergency handlers on IST2 for NMI and IST3 for double fault. It does not change the live IDT. The expected #GP path bypasses the ordinary C++ exception dispatcher. An unexpected vector, MSR index, error code, CS, flags, RSP, SS, or saved RIP restores the handler scratch state and jumps to the existing exception_stub_13 path with the hardware error word intact. The fixup does not catch arbitrary kernel faults.

The exact linked diagnostic kernel has RDMSR at address 0x244B83. A matching fault frame must report vector 13, error code zero, kernel CS 0x08, kernel SS 0x10, the armed RSP, permitted flags, and saved RIP 0x244B83. The handler records that frame, changes only the saved RIP to the authorized continuation at 0x244B93, restores RAX, R10, and RFLAGS, removes the single hardware error-code word, and executes IRETQ. The continuation marks itself reached and joins common cleanup at 0x244BA3, which disarms the RSP/MSR context and restores the previous IDTR before returning.

The final linked object also confirms that the reserved-index path never stores the RDMSR value through the caller output pointer. The only RDMSR in the fixup object is at object offset 0x103; the linked address is 0x244B83. The expected handler path has no CALL instruction. The unrelated path resolves directly to exception_stub_13 at 0x175FC7.

### Existing ordinary exception findings

The probe does not repair the wider CPU7A/CPU7B2 exception-entry issues recorded in CPU_IMPROVEMENTS_CPU7B2.md:

- Legacy PIC and exception entry share a 16 KiB C++ stack; nested exception/IRQ dispatch can overwrite a live dispatch stack.
- NMI and double fault use IST0 in the legacy IDT. CLI does not block NMI.
- Legacy error-code stubs misclassify #MF, #AC, and #CP error words.
- Legacy IRQ, exception, and CPL3 exit stubs omit CLD.

The exact-site reader rejects DF, TF, IF, NT, or VM in the flags it accepts, preserves the interrupted flags, and uses isolated emergency IST entries while its temporary IDT is active. These bounded changes do not qualify the generic legacy exception path. Native ELF and CPL3 execution remain outside the read window; the normal CPL3 control path still starts with IF clear and uses its existing TSS RSP0 and DPL3 0x80 exit gate.

The long-mode exception-frame review follows the Intel SDM and AMD APM descriptions of the 64-bit exception return frame: [Intel SDM](https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf) · [AMD APM Vol. 2](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf).

## 3. Hosted regressions and assembly evidence

All ten CPU1–CPU7B2 hosted regression runners exited zero:

1. test-amd64-boot-page-tables
2. test-amd64-bsp-apic
3. test-amd64-bsp-apic-base-probe
4. test-amd64-bsp-xapic-admission
5. test-amd64-context-switch
6. test-amd64-cpu-capabilities
7. test-amd64-memory-foundation
8. test-amd64-per-cpu
9. test-amd64-processor-inventory
10. test-amd64-protected-task-contract

The dedicated BSP APIC-base probe fixture reports five passing groups. It exercises expected versus unexpected frames, vector/error/RIP matching, continuation matching, reentrancy/stale-context rejection, context cleanup, state preservation, and ordinary exception routing. The checker also disassembles the emitted fixup object and confirms the handler save/restore and IRETQ sequence, one RDMSR site, zero CALLs in the fixup object, and the ordinary exception route.

These fixtures and the disassembly verify software matching and code shape only. They do not count as hardware exception delivery.

Build and hosted evidence is retained under E:\\CodexArtifacts\\CPU7B2R1-20261126\\hosted-regressions. The hosted fixup-object dump is bsp_apic_base_fixup_test.disassembly.txt. The final GP diagnostic object dump and linked instruction window are under E:\\CodexArtifacts\\CPU7B2R1-20261126\\assembly.

## 4. Isolated builds and identities

Each profile was built from source commit 7a3c65f42ff2f4641b5f6dbcf54a3a96d4a5c87b in a separate full clone under E:\\CodexArtifacts\\CPU7B2R1-20261126\\lanes. The canonical ServerV03PacManOnly release builder reported a clean worktree at packaging start and created structurally verified candidate ISOs under each clone’s out directory. No build used the destructive -Clean option.

| Lane | Diagnostic settings | Build ID | Kernel ELF size / SHA-256 | Candidate ISO SHA-256 |
| --- | --- | --- | --- | --- |
| Stock | APIC/probe/GP, CPU inventory, CPL3 all off | 86635911-bf7a-4527-9583-7e4188512a76 | 2,840,216 bytes / c3d69e0f7f63b9fab00895f0d646c8da87959858f3a7915638002349ed1fc595 | 173bc1e17f501535f72cd0d42df8d3068ecb050c4ca736ffb8f3f7a42392ee13 |
| GP diagnostic | probe=1, GP qualification=1; unrelated diagnostics off | a6a6a1cc-92d2-46e1-93cf-325f48d1de29 | 2,852,888 bytes / f514f9df94a27a3e2c9cc1f64846855c0325f237a262bf4b0db112e8d54ca4b7 | 1ff38a371bd488c19ee8ce6e9c2eb8e68b084a200b90e441864008d5d6b4e54e |
| CPL3 control | probe=1, GP qualification=0, CPU inventory=1, CPL3=1 | 90ba161a-149c-4182-80e0-f7955b79c5ce | 2,880,112 bytes / d28290a84aed4dce015ad55ff1d9fae4877deb250e405edfa7c2439718cf024d | fe35705c5901208d7aa9b97bcc32cb615f6f76473644f8d5ebddb8b63f067dd6 |

Each ISO is 90,245,120 bytes. The stock, GP diagnostic, and CPL3 control kernel hashes are distinct, as expected for the isolated compile-time profiles. The build manifests identify source commit 7a3c65f42ff2f4641b5f6dbcf54a3a96d4a5c87b and the three successful build IDs above. The GP lane’s kernel config stamp records probe=1 and GP=1; the control lane records probe=1 and GP=0. The emitted diagnostic string appears only in the GP kernel, and the CPL3 run later confirms its three-cycle code was present.

## 5. QAC ISO-only guest runs

The runner used the explicit recovered CLI path E:\\dev\\QAC\\src\\GuideXOS.QAC.Cli\\bin\\Debug\\net10.0-windows\\GuideXOS.QAC.Cli.exe and the S22R18 public-preview ISO-only acceptance workflow. Each run attached only a read-only candidate ISO and read-only OVMF firmware, with no guest network or writable guest disk. The QAC runner used a 300-second admission timeout and a 900-second host-side guest runtime deadline. Guests were run one at a time.

| Lane | QAC run ID | Reservation | Guest result | QEMU / QAC exit | Release verified |
| --- | --- | --- | --- | --- | --- |
| Stock | cpu7b2r1-stock-20261126-01-a1af6c13dc0a406db94c3296a303a5c6 | 7427f1f0aee24e588c7afd21da3f30ed | INCOMPLETE | 0 / 0 | Yes |
| GP diagnostic | cpu7b2r1-gp-diagnostic-20261126-01-4d8acf7e467f44ef9812012385df1551 | 05a747aa691942e594ce1e18314a0c2b | INCOMPLETE | 0 / 0 | Yes |
| CPL3 control | cpu7b2r1-cpl3-control-20261126-01-8b07f20a1b414b97abb74930d427a631 | 9ccabb928d2c4e758b48a85b3c1e26b0 | INCOMPLETE | 0 / 0 | Yes |

The QAC classifier’s exact result was preserved. Its reason for all three runs was “No existing PacMan gameplay PASS/FAIL marker was observed.” We did not enter a manual result or relabel the classifier. Separate serial, QMP input, and screenshots establish the observed desktop and game behavior:

- All three guests reached the desktop, opened Nexgen PacMan with F12, accepted left/up input, and returned to the desktop after Escape.
- The post-input screenshots show game score changes from zero to 170 in stock, 190 in the GP diagnostic, and 180 in the CPL3 control.
- Each serial log records PacMan Escape, runtime exit result zero with no fault, and “lifecycle PASS window/resource cleanup complete.”
- The stock, diagnostic, and control logs all show continuing PIT_IRQ0 heartbeats.
- The GP diagnostic and CPL3-control logs each report online=0x00000001 and offline=0x00000001. The CPL3 control also reports two MADT descriptors and one executing boot processor. The stock build leaves CPU inventory diagnostics disabled and emits no roster line; its source path starts no secondary processors.

### Exact GP diagnostic guest observation

The diagnostic guest’s serial log contains:

- [CPU7B2R1] invalid-msr-gp=not-delivered returned-value=suppressed cleanup=clean
- [CPU7B2R1] followup-apic-base=observed cleanup=clean
- [CPU7B2] apic-base-msr=observed raw=0x00000000FEE00900
- [CPU7B1] admission=ownership-unverified online=0x00000001 offline=0x00000001 mapping=not-installed controller-access=not-attempted

The invalid-read output pointer retained its sentinel and was not published. The guest verified the original IDTR and zeroed probe context after the returned instruction, then performed the valid 0x1B read. No exact vector-13/error-zero evidence record or continuation-reached marker was produced. Thus this run proves suppression and cleanup for the not-delivered case, plus a successful follow-up read, but not the requested fault handler path.

The QEMU command used the QAC runner’s pc,accel=whpx profile, with -smp 2. The selected QEMU executable reported version 11.0.0 (v11.0.0-12122-ga4bb4b10c9). The observed not-delivered result is scoped to this guest/profile; it is not a claim that all QEMU accelerators or hosts behave the same.

After each run, the candidate ISO hash matched its pre-run manifest hash. OVMF remained 33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a before and after the runs. Final QAC status was online, with zero QEMU processes, zero foreign QEMU processes, zero sessions, zero reservations, zero queued requests, and zero blockers. Each workspace remains under E:\\CodexArtifacts\\CPU7B2R1-20261126\\qac with its execution record, artifact manifest, serial log, QEMU debug log, QMP transcript, action evidence, and screenshots.

## 6. Historical single-byte stock-kernel difference

The recovered comparison recorded one difference at file offset 0x77C7B, virtual address 0x176C79 in kernel::interrupts::init(). CPU7B1 has cmp rsi,rax (48 39 c6); CPU7B2 has cmp rax,rsi (48 39 f0). The following JNE consumes the equality flag. Both operand orders set ZF to one exactly when the operands are equal, so the branch outcome is identical. The change came from the IDT descriptor destination refactor; no source change was made to reproduce a historical hash.

## 7. Outcome and CPU7B3 gate

**Outcome B.** The offline audit, hosted regressions, three isolated builds, and three QAC guest runs completed. The chosen guest did not deliver the deliberately invalid RDMSR as #GP(0), so Outcome A is not met. No safety prerequisite prevented the experiment, so Outcome C does not apply. The exact-site hardware fault-delivery gate remains open.

CPU7B3 must use a disposable guest profile where an invalid RDMSR is known to deliver vector 13 with error code zero, then retain serial proof of the exact saved RIP 0x244B83, authorized continuation 0x244B93, reached marker, clean probe context, and restored IDTR. Keep an independent host deadline and the same fail-closed output suppression. Do not use physical hardware for deliberate fault injection. Keep unrelated exceptions on the ordinary vector-13 path and qualify that path separately.

CPU7B3 must also continue to defer LAPIC MMIO mapping, controller ownership or configuration, IPIs, AP startup, and SMP. The existing CPU7B1 prerequisites for any later mapping remain separate gates: platform memory ownership, complete alias and permission evidence, effective cacheability, table-budget accounting, and a transactional mapping/rollback/teardown plan.
