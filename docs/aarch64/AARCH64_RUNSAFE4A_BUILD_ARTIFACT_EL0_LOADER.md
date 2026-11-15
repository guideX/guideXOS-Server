# AArch64 RUNSAFE4-A Build Artifact and Isolated ELF Loader

## Outcome

`RUNSAFE4_EL0_LOADER_HOSTED_READY`. Hosted Build identity, ELF validation, EL0 mapping, scheduler-lifecycle, ABI, fail-closed controls, and the committed Phase 12 source-matched candidate build pass. No Studio-compiled ELF has been executed by a native guest in this phase.

The RUNSAFE3-B acceptance reports and frozen candidate were left unchanged. This work does not launch QEMU, request QAC, or enable the Developer Studio Run action.

## Compiler output profile

The resident ARM64 compiler currently accepts a deliberately small C subset. The R2B1 example

```c
int gx_main(void* ctx) { return 42; }
```

produces a 4,108-byte ELF64, little-endian, `ET_EXEC`, `EM_AARCH64` file. The image starts at `0x51000000`, and `gx_main` begins at `0x51001000`. The function body materializes the integer result with `MOVZ W0`, `MOVK W0`, and `RET`. The hosted Build fixture independently rechecks this output and its SHA-256 (`76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2`). A separate hosted compiler fixture builds return value 43.

This is an integer-only constant-return profile. The resident grammar does not compile the non-cooperative loop used by the future diagnostic, and the runtime does not preserve SIMD/FPU state. TLS, external calls, dynamic linking, relocations, runtime initialization, and general system calls are unsupported. No arbitrary C application support is claimed.

## Immutable Build identity

`BareMetalBuildService` captures a runnable ARM64 result into a kernel-owned fixed-capacity snapshot. The identity binds:

- Studio runtime owner identity and monotonically distinct Build generation;
- project ID and logical artifact path;
- architecture, ABI, and (when a dual-target package exists) package path and package generation;
- source SHA-256, artifact size, artifact SHA-256, and ELF-profile validation result;
- a private copy of the VFS-readback artifact bytes.

The service reads the selected source file once, hashes those bytes, and passes those same bytes to the compiler. It reads the produced ELF back from VFS, hashes the buffer, and copies that buffer into the owned snapshot. The isolated child path verifies every supplied identity field and the source digest, then loads from the owned bytes; it never reopens the artifact pathname. A VFS replacement after capture cannot mutate the execution snapshot.

Each accepted Build invocation clears the preceding runnable snapshot before compiling. A compiler, publication, or later validation failure leaves no previous runnable generation. Rebuilding creates a new generation. Releasing the build-status handle does not release or alter the owned artifact. Its bytes remain stable until the next Build invocation invalidates/replaces the snapshot. The asynchronous Studio adapter starts Build with its runtime owner identity; the legacy ownerless start remains separate.

## ELF validation and mapping

The initial production validator is intentionally stricter than a general ELF loader. It accepts only the compiler profile described above:

- ELF64, little-endian, version 1, `ET_EXEC`, `EM_AARCH64`;
- exactly one 56-byte program header, with one `PT_LOAD` segment;
- fixed image base `0x51000000`, entry `0x51001000`, and matching physical/virtual address;
- a single read/execute segment (`PF_R | PF_X`) aligned to 4 KiB, with bounded file and memory sizes;
- at most 16 KiB of file data and eight image pages; no section table, dynamic segment, or relocation table;
- the supported integer return instruction sequence at the entry.

The child page-table root starts as a copy of the kernel root. The loader privately clones only the shared table path needed for the reserved low image window, then replaces an inherited normal-memory, EL1-only identity leaf with a child-owned mapping. It never edits the kernel's page tables. Each child's physical text pages are separately allocated and mapped EL0 read/execute, with both privileged execute-never and user execute enabled. Zeroed allocation plus the segment copy gives a zero-filled memory tail if the accepted segment's memory size exceeds its file size.

The startup trampoline occupies its own EL0 read/execute page near the end of the reserved 2 MiB image window. The initial image contains no writable data segment. A four-page EL0 read/write, execute-never stack is placed near the top of the user window, with unmapped pages above and below it. Kernel MMIO remains inaccessible at EL0. Construction failures tear down partial page and table allocations before returning.

## C entry, return, and scheduled lifecycle

The trampoline clears `x0` for the documented null `ctx`, calls `gx_main`, sign-extends its 32-bit C return through `w0`, loads the existing EL0 exit service into `x8`, and executes its controlled `SVC`. Its final instruction is a self-branch, so it cannot fall through if the exit service unexpectedly returns. No kernel pointer or callable EL1 address is exposed to the child.

`phase2_el0_task_create_studio_build` binds the exact Build identity to the existing RUNSAFE3 scheduler task and child address space. The caller separately starts the created task, queries its completion, requests cancellation through the existing scheduler, and reaps only after terminal resource cleanup. Execution stays asynchronous with a finite budget. Loader setup does not invoke child instructions synchronously from Studio.

Hosted EL0 tests compile the 42 and 43 ELF examples, validate their actual entry instructions, inspect each child's independent address space and page permissions, and exercise identity rejection and lifecycle transitions. Their scheduler/exception events are simulations; they do not establish native instruction execution.

## Studio and legacy loader boundary

The v0.5 464-byte Studio adapter and original 400-byte ABI layouts are unchanged. Studio Run remains fail-closed. The public Run request does not carry a Build generation or execution handle and cannot represent asynchronous completion safely, so the public callback is not connected to this internal foundation. The new typed bridge is kernel-internal. The Phase 9 privileged NativeElf route remains separate and is not a fallback for Studio Run.

## Verification

Completed hosted checks:

| Check | Result |
| --- | --- |
| RUNSAFE4-A Build identity and ELF-loader suite | PASS; Build service identity checks and 251 hosted EL0 assertions |
| ARM64 compiler and bootstrap regressions | PASS |
| RUNSAFE2 / RUNSAFE3 EL0 controls and source contracts | PASS; 251 hosted assertions, including invalid ELF and lifecycle cases |
| Phase 2 build and host controls | PASS; 51 assertions |
| Phase 3 compile/link and scheduler contract | PASS |
| Phase 4 compile/link, allocator/VFS controls, and scheduler repeatability | PASS |
| GICv3 hosted model | PASS; 62 assertions |
| Captured firmware DTB through production Phase 2 parser | PASS; 52 assertions; fixture SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311` |
| Studio v0.5 adapter | PASS; 181 checks |
| Studio Run fail-closed gate | PASS |
| RUNSAFE3-B / RUNSAFE4-B serial evidence parser fixture | PASS |
| Native filesystem contract | PASS |
| Full Phase 12 source-matched Runsafe4B candidate build | PASS from a clean committed tree; pinned Studio linked, ARM64/EFI kernel packaged, and the `runsafe4b-compiled-el0` manifest emitted |

Candidate output: `out/aarch64-runsafe4a-phase12-latest/candidate-manifest.json`. It records the exact clean server revision, pinned Studio revision, candidate profile, and all artifact hashes. The pinned Studio ELF SHA-256 is `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483`; the staged return-42 source SHA-256 is `79ed63fbc4052d8b92cc7ef065ecaf599e4c8409bd3e0dd31be877286acf3c97`. This is candidate-build evidence only; the build script did not launch QEMU or QAC.

## RUNSAFE4-B native acceptance still required

The source-matched candidate diagnostic is prepared to build a controlled return-42 source inside the guest, verify its owned Build identity, reject a wrong-machine ELF, create the scheduled EL0 child, inspect its actual result and cleanup, and confirm that a non-cooperative loop is rejected by the resident compiler and invalidates the prior generation. This is a future guest sequence, not a result established by this document's hosted tests.

RUNSAFE4-B must build the final committed source, inspect the candidate manifest and hashes, then perform the focused QAC-supervised native acceptance sequence. It must observe the trampoline, compiled instructions, actual return value, SVC completion, scheduler terminal state, resource reaping, and continuing kernel work in guest-originated evidence. QEMU must only be launched by the authorized QAC workflow. Studio Run remains disabled until that evidence and a separate ABI-compatible asynchronous Run contract are reviewed.
