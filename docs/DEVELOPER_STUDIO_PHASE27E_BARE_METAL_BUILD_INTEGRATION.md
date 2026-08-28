# Developer Studio Phase 27E — Bare-Metal Build Integration

Phase 27E adds a bare-metal Developer Studio build route while preserving the
existing hosted PowerShell/LLVM route. The two routes share the
`BuildController` request, lifecycle, snapshot, and `OutputService` contracts.

## Runtime boundary

The bare-metal route is selected only when the appended NativeElf host-call
slots are advertised and size-gated. It uses:

- the kernel VFS for project metadata, manifests, source reads, output writes,
  directory creation, and stale-artifact removal;
- `kernel/core/compiler/compiler_build_service.*` for the bounded build job;
- the existing compiler driver and NativeElf validator;
- no Windows process creation, PowerShell, CMake, MSBuild, clang, GCC, LLVM,
  `ld`, or `lld` calls in the guest runtime.

The service is single-job and non-reentrant. `start`, `poll`, and `release`
are exposed through the ABI. A second start while a job is retained returns
`GX_ERROR_BUSY`; the job remains retained until the controller releases it.

## Supported compiler subset

The Phase 27E kernel compiler accepts one deterministically selected `.c` or
`.cpp` source, AMD64, `guidexos-c-abi-v1`, and the `gx_main(void* ctx)` entry
point. The current bootstrap subset supports the integer return form and the
bounded `log(ctx, "...")` host-call form used by the proof application. It
emits a NativeElf image at:

```text
build/bin/amd64/<outputName>.elf
```

An optional `sourceEntry` project field selects an exact source relative to
`sourceRoot`. Without it, the service lists `sourceRoot`, sorts candidates,
and requires exactly one `.c`/`.cpp` file. Compiler diagnostics retain source
location data and are converted to the existing GCC-style OutputService
parser.

## Project and save contract

The bare-metal target profile is:

```text
guidexos.amd64.baremetal.bootstrap.native
```

The IDE saves all dirty project documents before submitting a build. The
kernel compiler then rereads the saved source through VFS, so an unsaved editor
buffer cannot affect the generated artifact. Artifact details include the
relative path, byte size, SHA-256, AMD64 architecture, entry point, and
NativeElf validation status.

## ABI compatibility

All Phase 27E host-call fields are appended after the existing v1 table. Older
hosts remain valid. Every new dereference is guarded by the host table's
advertised `size`; hosted workspace/build fields and the new VFS/build fields
remain separate.

## Proof harness

Build the proof application and run one fresh QEMU boot from the server repo:

```powershell
.\scripts\smoke-compiler-bootstrap.ps1 -BootCount 1 -Phase27E
```

The Phase 27E proof opens the real project/document controllers, performs an
initial build, edits the source to an invalid return expression, verifies
Save All and mapped diagnostics, edits it back to return 41, verifies a
successful rebuild with a changed hash, checks the output artifact, and
returns through NativeElf teardown. The harness requires these markers:

```text
phase27e_build_backend=PASS
phase27e_ide_build=PASS
phase27e_source_edit_build=PASS
phase27e_ide_diagnostics=PASS
phase27e_rebuild_after_failure=PASS
phase27e_kernel_survival=PASS
phase27e=PASS
phase27e_app_launch=PASS
ELF Loader: Phase 27E smoke PASS
```

For the preferred fresh-boot evidence, use `-BootCount 3`.
