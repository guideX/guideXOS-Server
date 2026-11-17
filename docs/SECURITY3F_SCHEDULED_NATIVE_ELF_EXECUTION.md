# SECURITY3F: Scheduled Native ELF Execution

**Project:** guideXOS Server  
**Branch:** `SECURITY_SETTINGS`  
**Starting baseline:** `0900235076516f8ab0f813d04789b24f2b3a81ea`  
**Implementation commit:** `f082f5563d555a5efa096557fdd8fc507f82ef1d`

## Status

**Outcome: `SECURITY3F_SCHEDULED_NATIVE_ELF_VERIFIED`.** An authentic Native ELF pilot booted through the production launcher on its scheduler-owned stack, yielded and resumed, progressed alongside the desktop service task, returned successfully, and released its resources after stack reaping. The desktop continued after the pilot and after the legacy compatibility launch. This outcome does not imply preemption or isolation.

## Starting architecture and synchronous assumptions

SECURITY3E provides one eight-slot cooperative scheduler, 64 KiB stacks, AMD64 Microsoft x64 GPR switching, round-robin dispatch, yield/resume, stack guards, root-stack reaping, and scheduled system/network service work. Before SECURITY3F, the Native ELF launcher validated and retained the package/image, created a SECURITY3D lifecycle record, then called the app synchronously on the desktop caller's stack. The call did not return until the app returned. Runtime storage, image bytes, window/audio references, and cleanup were consequently scoped around that synchronous call.

This phase reuses that scheduler. It does not migrate PacMan or other existing apps. A missing `executionModel` remains synchronous; `cooperative-scheduler-v1` opts in; an unknown nonempty model is rejected. Slot allocation failure and scheduler-context creation failure are returned explicitly. There is no silent synchronous fallback after a scheduled request.

## CPU context contract

AMD64 context switching now saves/restores the enabled architectural xstate with `XSAVE64`/`XRSTOR64`, including the enabled x87, SSE, MXCSR, and AVX state, alongside the existing nonvolatile GPRs, RSP, and RFLAGS. Initialization checks CPUID/XCR0 support and the fixed 16 KiB per-context xstate capacity; unsupported configurations fail scheduler initialization. The kernel still compiles ordinary C++ with MMX/SSE disabled. AVX state is enabled only when supported by the CPU and OS configuration.

The switch path preserves Microsoft x64 call requirements: caller-side 16-byte alignment, the ABI-mandated post-call stack alignment at function entry, the 32-byte caller-provided shadow space, and the nonvolatile XMM6–XMM15 registers as part of xstate. Interrupts remain disabled across the low-level switch bookkeeping; the scheduler restores the caller's intended flags on return. This does not establish a general interrupt virtualization or CPU-local-state isolation boundary.

The kernel self-test exercises independent GPR state, XMM6–XMM15, MXCSR, x87 control/stack state, optional AVX YMM6–YMM15 state, repeated switching, post-resume stack alignment and shadow space, return/reap behavior, capacity, reserved service capacity, and stale generations. The guest-proof kernel passed these checks in QAC with XCR0 `0x7` and 832 enabled xstate bytes.

## Native ELF scheduling and ABI

Only manifests that explicitly select `cooperative-scheduler-v1` use the new route. The production launcher still resolves the canonical App Model ID and applies the existing execution policy and executable verification before scheduling. It captures the current trusted identity/session provenance at the launch boundary, creates one SECURITY3D managed record, then creates a scheduler task bound to that record. The bounded scheduler handle and the managed-execution handle remain separate and generation checked.

The scheduler enters the authentic `gx_main` through a dedicated scheduler-owned stack. The launcher verifies stack bounds before entry. The verified ELF/image and runtime references remain live while the app is suspended or running. Normal return records the real exit value; faults without a verified recovery path remain kernel-fatal. The app stack is never freed while active: completion returns to the scheduler/root stack, which reaps the task before the launcher releases the image, window/audio state, and runtime references. Failed task creation discards the pre-entry lifecycle record and releases staged resources.

The existing host ABI table is extended by appending a nullable `yield` callback. All previous offsets remain unchanged; the ABI layout test pins the prior layout, the new callback offset, and the enlarged table size. Synchronous launches expose a null callback. The SDK helper checks table size and callback presence before invoking it. Yield is accepted only from the active scheduler-managed Native ELF context; unsupported callers receive an error without changing scheduler state.

Supported apps can yield directly and can yield when event polling has no event to return. A successful lifecycle transition is `Running -> Yielded -> Running`; completion can follow either `Running` or `Yielded`. Yield points occur after host callbacks return, so the scheduler does not re-enter desktop drawing/input services from a second context. Apps must not hold locks or transient host-service state across a yield. The kernel cannot verify arbitrary app lock ownership.

## Desktop coexistence, lifecycle, and security

The desktop remains the owner of UI, compositor, and input state. The main loop continues desktop work and then gives the scheduler a bounded dispatch opportunity. A scheduler service-dispatch counter and app yield/poll state provide evidence that a service ran while the pilot was suspended. The pilot checks a volatile local value after resume and exits on close/Escape or after a bounded number of frames.

The scheduler reserves slot 0 for a trusted kernel/system task; Native ELF apps can use slots 1–7. The maximum scheduled app count is therefore seven, while the overall fixed context count remains eight. Exhaustion returns a capacity result without evicting a live context. Completed tasks are reaped and slot generations advance; stale handles do not become valid after reuse.

The SECURITY3D managed-execution record remains the lifecycle source of truth. It retains canonical app ID, launch-time owner attribution, session generation, exit/fault details, and resource-release status. Historical attribution is not a capability: authorization-sensitive services still consult the current authentication/session runtime. Yielded work remains active. Termination requests remain authorization checked and return `TerminationUnsupported` because arbitrary kernel-privileged code cannot be safely stopped.

Native ELF still runs with kernel privilege and shared address space. It can corrupt kernel memory, alter interrupt state, or hang forever by failing to yield. This phase adds cooperative scheduling, not preemption, forced termination, memory protection, or fault isolation.

## Compatibility and policy

Only the SECURITY3F pilot opts into the scheduler. Existing manifests without an execution model retain synchronous behavior; `Hello World` is exercised by the guest-proof macro as a legacy synchronous launch. PacMan is not migrated, and the PacMan-only public-preview trust policy is unchanged. No trust checks were weakened. Native ELF release-policy tests pass.

The SDK pilot is a freestanding AMD64 ELF with `-mno-red-zone`, Microsoft x64 ABI, no runtime library, and a normal `gx_main` entry. It yields, validates a local value after resume, services events, and returns an exit status.

## Build and test evidence

Verified in this checkout:

| Check | Result |
|---|---|
| Native ABI layout | PASS |
| Managed execution lifecycle | 48/48 PASS |
| Identity foundation | 43/43 PASS |
| Authentication sessions | 60/60 PASS |
| Native ELF security policy | PASS |
| App Model identity | 25/25 PASS |
| Native present validation | PASS |
| Native filesystem contract | PASS |
| Native Settings runtime | 1447/1447 PASS |
| Native Settings layout | 1037/1037 PASS |
| Native debugger runtime | PASS on isolated final rerun; earlier concurrent attempts were flaky |
| AMD64 context-switch NASM assembly | PASS |
| Normal AMD64 freestanding kernel | PASS |
| Public-preview AMD64 freestanding kernel | PASS |
| Guest-proof AMD64 kernel (scheduler self-test + launcher pilot) | PASS |
| Hosted `build.bat` | PASS |
| Scheduler pilot ELF | PASS; ELF64 x86-64 ET_EXEC, `gx_main`, no undefined symbols |
| QAC guest qualification with `-cpu max` | PASS; scheduler self-test, scheduled Native ELF, legacy synchronous ELF, desktop, and clean QAC release |

The hosted build emits existing warnings in unrelated desktop/UI code. The canonical UEFI script rebuilt `BOOTX64.EFI` successfully in the isolated worktree, then exited while creating an empty ramdisk because it calls `WriteAllBytes($Ramdisk, $null)`. The scratch ESP was completed by staging a zero-byte ramdisk, the hashed guest-proof kernel, Apps, and assets. The bootloader logs that the zero-sized ramdisk cannot be allocated; this did not prevent the kernel, desktop, or Apps tree from booting. No root `ESP` or pre-existing QEMU guest was changed.

The UEFI script prints static inherited labels `GXOS-LARGE-FILE-PASTE-TRACE-V1` and `GXOS-LFPASTE-20260726-02`. Those strings predate SECURITY3F and are not candidate identity. The scratch artifact identity is recorded separately in `out/validation/security3f/guest/artifact-identity.txt`.

### Current artifact identities

| Artifact | SHA-256 |
|---|---|
| Normal AMD64 kernel | `4171adcef91f4760bfa74faad48fec4594ef2f6468aa08f90fdf22805a9d0946` |
| Public-preview AMD64 kernel | `2d8b06c3c6718f15c2b1bb5b1e81be6ba015268c52fa1d8f9918e50df52b8669` |
| Scheduler self-test + guest-proof kernel | `864b00b0742b5ae97f2ba3d6afa1be6d413381347af0735102ab1896084b11fe` |
| Isolated UEFI bootloader | `ccea8f2498fe82f82546eb585a433a659d3014bd9a6278a8411074feecf17f07` |
| Scheduler pilot ELF | `7546d7156da162c742520a4f2f696d87750cec7706e3311988578c5921f6dbe9` |
| QEMU x86-64 code firmware used with pflash | `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a` |
| QEMU executable | `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02` |
| Workspace `OVMF.fd` (rejected by QEMU `-bios`) | `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792` |

## Capability matrix

| Capability | Status |
|---|---|
| Independent kernel execution contexts | Guest-verified with full supported xstate self-test |
| Scheduled Native ELF execution | Guest-verified through production launcher on dedicated scheduler stack |
| Cooperative application yield/resume | Guest-verified; 254 yield transitions and stack-local state checks |
| Desktop/service progress while app is suspended | Guest-verified; four trusted service dispatches recorded during pilot suspension |
| Lifecycle completion and cleanup | Guest-verified; exit 0, resources released, scheduler stack reaped |
| Timer preemption | Not implemented |
| Forced termination | Not implemented; reports `TerminationUnsupported` |
| User-mode isolation | Not implemented |
| Address-space isolation | Not implemented |
| Protection from noncooperative application hangs | Not provided |

## QAC guest qualification record

Candidate source commit: `f082f5563d555a5efa096557fdd8fc507f82ef1d`. QAC granted development reservation `46025cc6e3f648ad83d52c7d03144dae`; QEMU PID 27024 ran from `2026-11-17T06:16:57Z` to `2026-11-17T06:19:10Z`, exited with code 0 through QMP, and QAC released the reservation. Post-run QAC status showed zero QEMU processes, zero foreign processes, zero managed sessions, and zero queued or blocked reservations.

The candidate used the isolated ESP at `out/validation/security3f/source/ESP`, QEMU's x86-64 code firmware on pflash, `-cpu max`, and the guest-proof kernel flags in `out/validation/security3f/guest/artifact-identity.txt`. A first boot with QEMU's default virtual CPU correctly failed scheduler initialization because XSAVE was unavailable (`xcr0=0`); no scheduled-app capability was claimed from that run. The final boot reported `xcr0=0x7` and 832 enabled xstate bytes.

Serial evidence in `out/validation/security3f/guest/serial6.log` includes:

- `[NATIVE-SCHEDULER-SELFTEST] result=PASS` with GPR, XMM, x87, MXCSR, AVX, stack/shadow-space, generation, capacity, reaping, and repeated-cycle proofs.
- `[NATIVE-SCHEDULER] desktop service context entered`.
- Pilot launch with scheduler slot 1/generation 5, a 64 KiB stack range `0x655470..0x665470`, managed ID 1/generation 1, and unassigned launch provenance.
- Scheduled entry with `entryStackVerified=yes`, state `Completed`, exit code 0, 254 yields, 255 dispatches, and stack released.
- Four `service-progress-during-app-suspension` observations with trusted dispatch counts 2–5, plus repeated yield/resume records.
- Managed lifecycle `Exited`, exit status 0, resources released, scheduler stack reaped, and `scheduled lifecycle PASS yield/resume/return/cleanup`.
- The later synchronous Hello World launch and cleanup passed; QMP Escape returned control to the desktop, which continued its main loop and heartbeat.

QMP screenshots are in `out/validation/security3f/guest/hello-world-screen.png` and `out/validation/security3f/guest/desktop-after-completion.png`. The first shows the legacy app running on the desktop; the second shows the desktop after Escape and after both app lifecycles completed. The optional demo of another Native ELF app running concurrently with the pilot was not attempted; the runtime admits one active Native ELF instance at a time.

## Recommended SECURITY3G scope

Prioritize timer-driven preemption and a reviewed fault-recovery boundary only after a privilege/isolation design exists. Add per-address-space memory protection before treating arbitrary Native ELF code as safe. Define lock-aware yield rules and cancellation semantics, and qualify more apps individually; do not infer PacMan compatibility from this pilot.
