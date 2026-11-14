# Server v0.3 — S22R25 Protected Execution Architecture

**Outcome C — `PROTECTED_EXECUTION_ARCHITECTURE_BLOCKED`.** The voluntary AMD64 function-call context path is corrected and has hosted ABI coverage. The repository still lacks the page-table ownership, user/kernel entry, asynchronous context, and service boundaries needed to schedule a protected Native ELF task safely. No scheduler, CPL3 execution, forced termination, or QAC guest test is claimed.

## Repository and candidate provenance

- Repository: `D:\dev\guideXOSServer`; branch `main`.
- Starting HEAD: `6dc43f3ede0adfb36384f6534406c94d4a409560`, matching the requested S22R24 handoff. At preflight it was five commits ahead of `origin/main`, with no tracked edits. The only pre-existing untracked path was `assets/Backgrounds/troncity.png`; its SHA-256 at preflight was `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda` and it was preserved.
- The accepted S22R23 PacMan-only candidate remains sourced from `67dc2e111ea4d6e7208a8d5fac7dbd1a389d8491`, profile `ServerV03PublicPreviewPacManOnly`, build ID `4c9988cf-5d0d-414d-afe8-81f7351c2706`.
- Its ISO remains 90,245,120 bytes with SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`. The embedded accepted kernel was 2,812,208 bytes with SHA-256 `9d2f17c33f157a566b6ee513196b5b74831bf8532699be78538b5979886fc166`.
- The accepted persistent user-data image remains `E:\CodexArtifacts\S22R22\persistent-data\guidexos-user-data.img`, SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`. Persistence tests used a separate writable copy.
- No ISO was packaged, the accepted ISO/data image were not modified, and `dist`, signing, publishing, promotion, and release-readiness markers were not touched.

## Existing native execution model

The AMD64 Native ELF route remains the S22R24 model:

1. `desktop.cpp` dispatches the launch synchronously to `native_elf::launch()`.
2. The loader validates the existing PacMan-only release policy, loads a bounded static ELF image into kernel heap memory, and applies the supported static rebasing rules.
3. `run_package()` creates a stack-local `Runtime`, verified image, 128 KiB Native ELF stack, one `NativeWindowOwner`, and audio owner ID.
4. `gxos_native_call_on_stack()` changes only `RSP`, then calls the ELF entry at CPL0 in the kernel address space. It does not change CR3 or establish page permissions.
5. PacMan's callbacks may pump input, compositor work, and audio. The desktop launch dispatcher and its caller remain suspended until the application returns.
6. Normal return performs the existing window/audio cleanup and frees runtime-owned buffers. Escape/Close remains cooperative.

A distinct stack is not a privilege boundary. A scheduled thread is not automatically an isolated process. A CPL3 transition without a compatible service ABI would not run the current PacMan host table safely. The S22R18 package-integrity policy authenticates which bytes enter this path; it does not constrain their ring-0 behavior.

## AMD64 primitives and blockers

### GDT, TSS, and privilege transitions

`kernel/core/interrupts.cpp` installs only the null, ring-0 code/data, and 64-bit TSS descriptors (selectors `0x08`, `0x10`, and `0x18`). No ring-3 code/data descriptors exist. `TSS64.rsp0` is zero-initialized and never assigned. `IST1` points at the shared 16 KiB exception stack; the current PIC gates also select that IST stack. This is diagnostic/interrupt infrastructure, not a per-task user-to-kernel stack transition contract.

Before CPL3, the kernel must establish ring-3 selectors, a valid per-CPU TSS `RSP0`, a deliberate IST policy for double fault/NMI/machine check, and a kernel stack whose lifetime follows its task. Return must use a correctly constructed `iretq` frame. User exceptions must be classified against the owning address space and routed to task exit/reaping without using the current ring-0 Native ELF recovery predicate.

### Paging and memory ownership

AMD64 exposes `read_cr3`, `write_cr3`, and `invlpg`, but there is no general physical-page allocator or per-task address-space owner. `kernel/core/mmio.cpp` has a bounded 64-page table pool for the current address space's MMIO window; it walks/extends that mapping and does not provide isolated address spaces, executable loading, user-stack creation, or page-table teardown. The fixed 512 MiB kernel heap is a virtual kernel allocation arena, not a physical-page manager.

The current code therefore cannot establish separate user pages with supervisor-only kernel mappings, enforce ELF W^X permissions, copy user buffers safely, or reclaim an address space. Mapping writable kernel pages into a diagnostic user context would not be an acceptable substitute.

### Interrupts, syscalls, and preemption

- `pit::irq_handler()` increments ticks and emits a heartbeat. The registered IRQ0 path does not make a scheduling decision.
- The ordinary IRQ stubs preserve the Microsoft x64 volatile GPR set for their dispatch call and return through the interrupted frame; they do not save a complete asynchronous task context.
- The AMD64 context header's previous `FullContext` save/restore exports did not implement a valid full frame or `iretq`; those unsupported declarations/implementations are removed. The replacement context is explicitly call-boundary only.
- `kernel/arch/amd64/syscall.cpp` contains syscall policy stubs, but its `syscall_entry` currently returns `-ENOSYS`; `sys_exit` halts and `sys_yield` does not schedule. It has no user-pointer validation or service capability checks.
- Existing kernel code is not generally safe for arbitrary interrupt preemption. A first timer policy should mark `need_resched` and switch only at validated user-return boundaries; kernel critical sections and service callbacks require explicit deferral/ownership rules.

These findings explain why the voluntary switcher is not connected to PIT IRQ0, why no uncontrolled loop was launched, and why no guest was run for this milestone.

## Context-switch ABI contract after S22R25

The production AMD64 build uses MinGW's Microsoft x64 ABI. Its two-argument `switch_context` interface now explicitly uses that ABI: `RCX` points to the old-context slot and `RDX` points to the new context. The saved call-boundary structure is 80 bytes and contains all eight Microsoft x64 nonvolatile general-purpose registers (`RBX`, `RBP`, `RDI`, `RSI`, `R12`–`R15`), resumed `RSP`, and continuation `RIP`.

`init_context(stack_base, stack_size, entry, arg)` bounds the initial frame to a caller-owned stack. The bootstrap provides the Microsoft x64 32-byte shadow space, starts with the required 16-byte call-site alignment, passes the task argument in `RCX`, and uses a per-context entry/argument pair in saved `R12`/`R13`; there is no shared pending-entry global. Returning from a context reaches a halt stub because a scheduler-owned reaper does not exist. The stack remains caller-owned and must not be destroyed while active.

The freestanding C++ build disables MMX/SSE (`-mno-mmx -mno-sse -mno-sse2`), and a compile-time guard rejects a bare-metal context object built with those features enabled. Thus this voluntary kernel-context contract does not save XMM/FPU state. Microsoft x64 normally makes XMM6–XMM15 nonvolatile; future tasks that may use SIMD require CPU FPU/SSE initialization and per-task FXSAVE/XSAVE state before they can use this switch path. Interrupt frames, RFLAGS, segment state, and CPL transitions are outside this structure.

## Future protected-service contract

The current SDK `gx_app_context` contains a pointer to `gx_host_calls` and a `userData` pointer. For bare metal, the host table lives in kernel-owned `Runtime`; its entries are direct kernel function pointers. The current callbacks cover:

| Service | Current behavior and protected migration requirement |
|---|---|
| Window creation/drawing | Creates one `NativeWindowOwner`; calls compositor/framebuffer code directly. A broker should return an owner-scoped, generation-tagged handle and validate bounded title/text strings and geometry. |
| Frame presentation | Reads an app buffer directly after frame-size/stride validation. A syscall must validate the complete user range, copy or pin the bounded XRGB8888 buffer, and preserve the current 16 MiB frame cap. |
| Input/events | Writes `gx_event` to an app pointer and pumps the desktop synchronously. A broker must copy to a validated user range, route by task-owned handle, and avoid running unbounded desktop work inside a task's kernel stack. |
| Package resources | Resolves package-relative paths and reads the read-only `/Apps` source. Copy the bounded path in, retain the package/source pin checks, and copy at most the bounded read length back out. Never accept an app-selected executable path. |
| Audio | Reads PCM from an app pointer and submits it to the kernel audio backend. Validate/copy the full bounded input, enforce audio permission, and track playback by owner handle for teardown. |
| Exit/time/log | `exit` records a request; ticks are read directly; logging writes serial output. Requests should update task state, return a documented error/result, and defer cleanup to a reaper. |
| Memory/resources | The current host table has no app allocation callback; the kernel runtime allocates the ELF image, verified copy, stack, and window owner. The new process layer needs explicit per-task user-page ownership and a kernel reaper. |

Pointers passed to the current callbacks are ordinary addresses in the shared kernel address space. A protected broker must validate nullability, overflow, alignment, page access, direction, and explicit byte limits; reject cross-task handles; and return documented `GX_ERROR_*` results without retaining unpinned pointers. Kernel function pointers and kernel `Runtime` addresses must never be mapped into CPL3.

The approved PacMan package declares `guidexos-c-abi-v1` and the public-preview policy accepts that exact ABI. Do not reuse that name/version for a syscall-backed table with incompatible layout or pointer semantics. Introduce a separately versioned mediated ABI (for example `guidexos-c-abi-v2`) and a matching app migration; keep v1 unsupported for CPL3 until its exact compatibility is implemented. The existing release gate must continue rejecting unsupported ABI versions before entry. Preserve the current ring-0 PacMan route while the isolated route is unfinished.

## Safe termination boundary

The new voluntary switcher does not make destruction safe. It has no scheduler state, task run queue, PID, user address space, interrupt-return context, lock ownership, service-reference registry, or reaper. A task must not be freed while executing on its own stack. A protected task may be marked `Dying` on a validated user-return boundary, then switched away; only a separate reaper may release its user/kernel stacks, page tables, window/audio handles, VFS references, and FPU buffer. Kernel callbacks must either be bounded and non-blocking or publish an explicit cancellation-safe point. Never kill a task while it owns an unrecoverable kernel lock.

## S22R26 implementation scope

1. Add a kernel-owned page allocator from the boot memory map plus address-space creation, supervisor kernel mappings, user page ownership, page permissions, W^X ELF mapping, user-stack guard pages, validated copy-in/copy-out, TLB rules, and teardown.
2. Add ring-3 selectors, set TSS `RSP0` on task entry/switch, and define per-CPU exception/IST behavior. Replace placeholder syscall entry with a complete, audited trap frame and bounded service dispatch; retain unsupported ABI rejection.
3. Add a scheduler-owned full interrupt context and round-robin runnable queue. Start with a kernel idle/desktop context and protected user tasks; defer rescheduling during kernel critical sections and switch only from verified return paths. Do not equate voluntary yield with preemption.
4. Initialize FPU/SSE policy and give each task explicit FXSAVE/XSAVE state before allowing code that can use SIMD.
5. Define the versioned mediated Native ELF ABI, bounded user-memory copy rules, capability checks, task-owned generation handles, and a resource reaper. Migrate PacMan only after its graphics, event, package-resource, audio, timing, and exit contracts have concrete broker implementations.
6. Add hosted permission/pointer/handle tests, freestanding layout and symbol checks, then a separate bounded diagnostic guest. Qualify timer progress, protected entry/return, controlled exception, and task teardown before testing a non-cooperative loop. Keep diagnostic evidence and any diagnostic ISO outside the PacMan-only release profile.

The accepted S22R23 candidate remains the release baseline until a new candidate is built and separately qualified. S22R18 security enforcement, S22R23 persistence behavior, Model B preview intent, QEMU reference environment, and outstanding signing/publication approvals remain unchanged.
