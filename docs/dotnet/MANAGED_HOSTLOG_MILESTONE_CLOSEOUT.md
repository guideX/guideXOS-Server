# Managed HostLog execution milestone closeout

## 1. Milestone summary

The earlier pass proved a hosted NativeAOT managed-code execution path. A real C# `ManagedMain` was compiled with NativeAOT, converted to ELF64 AMD64 `ET_EXEC`, staged as an opt-in Native ELF application, loaded by the existing app model and ELF executor, entered through the Windows x64 trampoline, called the existing host `log` callback, and returned `0`.

This closeout pass adds provenance, semantic artifact assertions, opt-in staging checks, generic source-level ELF regression checks, dependency pinning, and a merge-oriented change map. It also found a reproducibility regression: after a clean NativeAOT rebuild, the current artifact reaches NativeAOT reverse-P/Invoke setup and exits with Windows status `0xC0000005` before the host callback. Therefore this pass ends as **Outcome C — Regression found during stabilization**, not as a stabilized milestone.

The regression is deliberately not addressed by adding allocation, GC, managed exceptions, threads, or full NativeAOT runtime initialization.

## 2. Exact proven execution path

The previously successful path was:

1. `HostLogProof.cs` supplies `ManagedMain(NativeGxAppContext*)`.
2. NativeAOT publishes a PE32+ AMD64 image with `/fixed /base:0x10000000` and `ManagedMain` at `0x10001900`.
3. The pinned proof converter emits an ELF64 little-endian `ET_EXEC` image.
4. The first `PT_LOAD` reserves the image-base page at `0x10000000`; the remaining load segments contain the PE sections.
5. The stage manifest is found only through `GXOS_NATIVE_ELF_STAGE_ROOT`.
6. The app registry resolves the staged `NativeElf` entry and the ELF loader validates it.
7. The executor maps the image at its preferred base, installs optional native TLS state, and invokes `ManagedMain` through `CallNativeElfWin64Entry`.
8. The managed method validates its context, builds `Hello from managed guideXOS code` in stack memory, calls `ctx->host->log`, and returns `0`.
9. The runtime records the host call, cleanup, and return result.

The path does not prove managed allocation, garbage collection, managed exceptions, threading, general CoreLib support, QEMU execution, or broad .NET compatibility.

## 3. Source and toolchain dependencies

- Target framework: `net9.0`, Runtime Identifier `win-x64`, NativeAOT publish, invariant globalization.
- NativeAOT package: `Microsoft.DotNet.ILCompiler` `9.0.0` from the local NuGet cache.
- Windows toolchain: Visual Studio MSVC `14.51.36231` was used by the proof run; the proof script resolves `vcvars64.bat` and requires `cl.exe` and `link.exe`.
- Native server toolchain: the repository's experimental MinGW C++ build, with the existing mbedTLS and image vendor trees restored as documented below.
- Converter: the attributed proof copy at `tools/dotnet/pe_to_elf_v2_fixed_base.py`, SHA-256 `EAAEFBC8862D6E1A4AC1A679073AF5311A3AF9CE96F45D258617BD4FE0977434`.
- Generated proof outputs are under ignored `out/dotnet/managed-hostlog/` and are not source dependencies.

## 4. External converter dependency

The canonical source is `D:/dev/guideXOSUEFI/Tools/pe_to_elf_v2.py`. Before the reported fixed-base patch its source revision was `e2bbb0ef6d3eb78eb316235ec4b5748ee95718b0`, Git blob `ac3f8ecef8451e471d486c4d96a54e2b039071f7`. The current canonical external file is unchanged by this closeout; its current file SHA-256 is `4B7EB3DB32297638EF47583051EEA6CD00E7046ACC31AB0CAF55995820B5D313`.

The reported external working-tree diff was exactly:

- reject a non-page-aligned PE image base;
- change `e_phnum` from `len(load_secs)` to `len(load_secs) + 1`;
- prepend `(PT_LOAD, R, offset 0, vaddr image_base, paddr image_base, filesz 0, memsz 0x1000, align 0x1000)`.

Cross-repository synchronization was not reliable in this workspace: the external working-tree patch reverted during later commands. The Server proof therefore uses Strategy B, an isolated, clearly attributed copy under `tools/dotnet/`. The copy is byte-equivalent to the canonical converter except for the documented header and fixed-base reservation patch. It retains the origin path, source revision, origin blob, and synchronization warning. It is not a second general-purpose converter and must be replaced or synchronized if the canonical tool changes.

The old six-program-header artifact had its first load at `0x10001000`, so the ELF envelope did not reserve the PE image-base page. Fixed-address execution could consequently use a base inconsistent with the PE's absolute image addresses. The corrected seven-program-header artifact has a first, read-only, fileless reservation at `0x10000000`; it contains no file-backed data and no copied headers. Its `R` flag and `0x1000` alignment make it address reservation only. The next load begins at file offset `0x1000`, virtual address `0x10001000`, and the other load segments retain their PE section data, flags, and zero-fill behavior.

This behavior is appropriate for the fixed-base `ET_EXEC` envelope, but it is intentionally kept proof-specific until the canonical UEFI converter receives the same separately reviewed change. No uncommitted external converter edit is required to reproduce the Server proof.

## 5. Vendor dependency restoration

Both vendor trees are ignored, not tracked, and not submodules. The normal build does not copy them.

| Dependency | Canonical source | Revision/version | Destination | Verification |
|---|---|---|---|---|
| stb | `D:/dev/guideXOSServerV0.2/third_party/stb/stb_image.h` | source revision `1692fe6e21ce7b7abbc6fcb6d1d3ff6ebe0b8537`, stb v2.30 | `third_party/stb/stb_image.h` | file SHA-256 `1F8C1B6B408F26E3B20CBFBBD4758AFB3DC9B837FF1E17C258928F406148A87C`; required-tree content hash `BDDA6FF3D2D6B2DD0D5A495DFDECF7E41C218EE85A5511D13DF5B2A24543789D` |
| mbedTLS | `D:/dev/guideXOSServer/third_party/mbedtls` | source revision `e04035d08c44f06d3102fb90202d17888f717602`, mbedTLS v4.1.0 | `third_party/mbedtls` | `build_info.h` SHA-256 `502BC326745B277A195A12410C4D33239CEE4A91FCD96058FAE11A333DF255E5`; `guidexos/mbedtls_config.h` `6574A28CA31769B575D289721C52CB842E950F92EA45F6A255C7089A33948607`; `guidexos/crypto_config.h` `B5EEC87A9BB051B1A06F531B58107EED60871DAEEC3C75E9FE37411AB68468D7`; required-tree content hash `ACAFB7BD539E9A93C592228BFC8DB1882428DDFF5AB39D8BD24BDB401954FCE9` |

To restore a clean experimental worktree, explicitly copy only these canonical trees into the destinations, preserve the source revisions above, and verify the listed file/tree hashes. A clean worktree should fail validation or produce a reviewable mismatch if the source revision, vendor version, configuration files, or content hash differs. Do not download arbitrary current versions and do not overwrite an existing vendor tree as part of the normal build. No automatic restoration helper was added in this pass.

## Provenance classification and commit placement

The classification below uses `P` pre-existing, `D` directly required by the managed execution proof, `G` generic native-ELF behavior, `T` tooling/documentation, and `U` uncertain. The current Server worktree has no unresolved `U` file; the attempted external converter working-tree patch is called out separately because it was not retained.

| File or change | Class | Placement |
|---|---|---|
| `build-native-experimental.bat` | P | Keep with pre-existing experimental build/vendor support; do not mix into the managed milestone without a separate build-support decision. |
| `desktop.json` | P | No commit; it was not changed by the proof. |
| `app_registry.cpp` stage-root hook from `f4eb45f` | D | `appmodel: add opt-in staged native ELF discovery`. |
| `native_app_runtime.cpp` TLS-hint propagation | D/G | `native-elf: propagate optional generic TLS hints`, or keep with the prior managed execution commit if that commit is not being split. |
| `native_elf_executor.cpp` TLS bootstrap | D/G | Split from the managed payload into the generic native-ELF commit; the current closeout diff only removes .NET-specific names. |
| `native_elf_image_loader.cpp` preferred-base derivation | G/P | Existing generic behavior from `421e926`/`f2b3adf`; no closeout commit. |
| `native_elf_executor.cpp` preferred-base acceptance/rejection | G/P | Existing generic behavior from `f2b3adf`; test or preserve separately from the managed payload. |
| `native_elf_trampoline_win64.cpp` and `.h` | D/G | Retain the proven ABI bridge in the generic native-ELF commit; it was not changed during closeout. |
| `scripts/dotnet/build-managed-hostlog-proof.ps1` | T | `dotnet: add managed HostLogProof artifact pipeline`. |
| `scripts/dotnet/stage-managed-hostlog-proof.ps1` | T | Same dotnet tooling commit. |
| `scripts/dotnet/managed-hostlog-artifact-assertions.ps1` | T | Same dotnet tooling commit. |
| `scripts/smoke-dotnet-managed-hostlog-artifact.ps1` | T | `tests: add static managed artifact smoke`. |
| `scripts/smoke-dotnet-managed-hostlog-execution.ps1` | T | `tests: add live managed execution smoke`; it must remain failing until the clean-build regression is fixed. |
| `scripts/smoke-native-elf-generic.ps1` | G/T | `tests: add focused generic native-ELF regression smoke`. |
| Existing .NET audit/boundary documents (`GUIDEXOS_CSHARP_NATIVEAOT_REUSE_AUDIT.md`, `DOTNET_INTEGRATION_BOUNDARY.md`) | P | Prior audit/boundary documentation; no closeout commit. |
| Existing proof documents (`MANAGED_HOSTLOG_ARTIFACT_PROOF.md`, `NATIVEAOT_PLATFORM_DEPENDENCY_ADAPTATION.md`, `MANAGED_HOSTLOG_EXECUTION_PROOF.md`) | D/T | Prior managed-proof documentation; update or preserve in the documentation commit. |
| `docs/dotnet/MANAGED_HOSTLOG_MILESTONE_CLOSEOUT.md` | T | `docs: record managed HostLog milestone closeout`. |
| `tools/dotnet/pe_to_elf_v2_fixed_base.py` | T | Separate `tools/dotnet: pin attributed fixed-base proof converter` commit; do not silently merge it into the external repository. |
| Canonical `D:/dev/guideXOSUEFI/Tools/pe_to_elf_v2.py` | P; prior patch U until committed | No Server commit. If promoted, create a separate external-repository commit, then remove or resynchronize the proof copy. |

The prior cce6a67 commit bundled some of these categories together. The table is the logical merge map, not a claim that Git's historical commit boundaries were already clean.

## 6. Server source changes

The minimal proof-specific Server behavior is:

- `app_registry.cpp`: `GXOS_NATIVE_ELF_STAGE_ROOT` adds one or more explicitly supplied package roots to the registry source list. With the variable absent, the previous default source list is unchanged.
- `native_app_runtime.cpp`: optional manifest hints are copied into generic runtime environment keys only when present.
- `native_elf_executor.cpp`: optional TLS hints are validated, a Windows TLS slot and zeroed per-thread block are installed, the mapped image's TLS index receives the slot number, and RAII cleanup clears and frees the slot. The preferred-base mapping policy predates this proof and remains fixed-address-only for non-relocatable `ET_EXEC` images.
- `native_elf_trampoline_win64.cpp/.h`: the proven Windows AMD64 entry bridge is an external assembly symbol that transfers the wrapper's `entry` and `context` registers to the managed/native entry ABI and supplies the Win64 shadow space.

No HostLogProof or .NET condition was added to generic loader code. No permanent .NET runtime type was added. No default release behavior changed.

## 7. Generic native ELF changes

The preferred-base policy is generic and originates in commit `f2b3adf` rather than this managed payload:

- image loading derives the preferred base from the minimum `PT_LOAD` virtual address;
- an `ET_EXEC` image whose declared preferred base does not match that minimum is rejected;
- the executor requests `ExecutableMemory::AllocateAt` at that address;
- allocation failure or a non-exact result is explicit failure because no relocation support exists.

Ordinary native ELF applications are affected only if they opt into the same experimental execution path and satisfy the same fixed-base `ET_EXEC` contract. No silent relocation fallback was added. The focused generic check is source-level because the repository has no narrow native-ELF unit-test harness; it checks the preferred-base rejection, allocation, optional TLS absence/reset, generic environment keys, and stage-root isolation.

## 8. .NET-specific changes

The .NET-specific surface is confined to the sample, its proof/staging scripts, semantic artifact assertions, and the staged manifest. It does not initialize the full NativeAOT runtime and does not add allocation, GC, exceptions, threads, filesystem, networking, reflection, GUI, or `System.Console` support.

The staged TLS hints are deliberately expressed as generic native-ELF metadata:

```text
gxos.nativeelf.tlsIndexAddress -> GX_NATIVE_ELF_TLS_INDEX_ADDRESS
gxos.nativeelf.tlsBlockSize    -> GX_NATIVE_ELF_TLS_BLOCK_SIZE
```

They are calculated from the NativeAOT map (`_tls_index`, `_tls_start`, `_tls_end`) and staged as external proof metadata. They are not parsed from an ELF `PT_TLS` program header; the final ELF has no `PT_TLS`. The executor initializes TLS only when both hints are present. Missing hints preserve the old native path. Incomplete or invalid hints fail explicitly. Repeated cleanup clears the thread value, frees the slot, and clears the vector, so a subsequent launch receives a new slot/block rather than stale state.

The clean-rebuild regression shows that this block alone is not sufficient for every current NativeAOT artifact: `RhpReversePInvoke` reaches a NativeAOT thread-state check before the host callback. Completing that state would be runtime initialization work and is outside this closeout.

## 9. Default-isolation proof

`GXOS_NATIVE_ELF_STAGE_ROOT` is process-local in the smoke script. The preflight process removes the variable and verifies `nativeapp.inspect` reports `Result: app not found`. The positive process sets only the staged `apps` root and discovers the proof app. `desktop.json` and the default inventory files are not modified. The proof app is not in the default application inventory.

## 10. Artifact envelope

The assertions intentionally validate the envelope rather than permanently requiring the earlier artifact hash `CDE78AD872795394D212E3AE0FCC9863B643C5D0B9414DAC4302D3A1A3AE0BF5`.

Required semantic properties:

- ELF64, little-endian, AMD64, `ET_EXEC`;
- entry associated with `ManagedMain` in the map/object evidence, normally `0x10001900` for this proof;
- first `PT_LOAD` is the `R`, fileless, `0x10000000` reservation page;
- every load segment stays within the expected PE image range;
- no `PT_INTERP`, dynamic section, `NEEDED` library, or ELF relocation;
- no writable-executable segment;
- writable zero-fill/BSS is represented correctly;
- TLS hints match `_tls_index` and `_tls_start`/`_tls_end` evidence;
- generated object evidence contains the indirect host-log call;
- native support source does not print the success message directly.

Source/staged SHA-256 equality remains a per-run staging integrity check. Generated code metadata such as timestamps and CodeView identifiers may change and is not a semantic acceptance criterion.

## 11. TLS behavior

TLS is optional, external-hint-driven bootstrap. It is not ELF `PT_TLS` parsing. The exact propagated values are the mapped-image virtual address of `_tls_index` and the byte size `_tls_end - _tls_start`. The address is written with the allocated Windows TLS slot index. The block is zero-filled and installed only on the launch thread. Ordinary native ELF applications with no hints take the old no-TLS branch. Invalid ranges, sizes, or incomplete hints fail before entry. The RAII object releases the value and slot after the entry returns or on any setup failure.

The current failure occurs after setup, in NativeAOT's reverse-P/Invoke thread-state path, and must not be described as evidence that managed allocation or general runtime initialization works.

## 12. Preferred-base behavior

The initial reservation page is required to make the minimum load address equal the PE image base. It is not a file-backed header segment. The executor already had generic preferred-address support; the converter change makes the artifact express the address that policy needs. Mapping failure remains explicit. Because relocations are unsupported, the executor must reject an unavailable preferred base rather than silently relocate the image. The preferred-base result is both execution policy and diagnostic evidence, not diagnostics-only.

## 13. Repeat-launch behavior

The earlier successful artifact was launched twice in one Server process with distinct runtime IDs and return code `0`, and the proof was also exercised from separate Server process invocations. The current clean-rebuild retry does not reach a successful first launch, so repeat-launch success is not re-certified by this closeout. TLS cleanup/reset remains designed for repeat launches and is covered by source-level generic checks.

## 14. Static smoke coverage

`scripts/smoke-dotnet-managed-hostlog-artifact.ps1` performs a clean proof build in an isolated output root, stages without rebuilding, verifies PE imports and the semantic ELF envelope, checks the map/object ABI and indirect host callback, verifies staging metadata and source/staged hash equality, checks default inventory isolation, and verifies generated roots are ignored. It is independent of stb and mbedTLS for the proof build. The final run result is recorded in the validation table below.

## 15. Live smoke coverage

`scripts/smoke-dotnet-managed-hostlog-execution.ps1` verifies the experimental server build, process-local stage root, absent/present inventory behavior, exact per-launch diagnostics, return `0`, executor-host log provenance, preferred-base success, TLS bootstrap diagnostics, distinct runtime IDs, CRLF-normalized matching, and a nonzero invalid-input probe. Its positive session issues two launches in one process. The final run result is recorded below.

## 16. Remaining limitations

- Clean rebuild reproducibility currently stops at NativeAOT reverse-P/Invoke thread state.
- No managed allocation, GC, exceptions, threads, or full runtime startup.
- No proof of QEMU or non-Windows execution.
- PE imports remain on the managed artifact side; the selected path is not a Win32 compatibility layer.
- The fixed-base converter correction is carried as a proof-specific Server copy until the canonical UEFI repository change is separately committed and synchronized.
- The generic regression coverage is intentionally narrow and source-level, not a new test framework.

## 17. Merge-risk assessment

High risk: the clean-build live regression and the incomplete NativeAOT thread-state contract. The proof should not be merged as a stabilized execution milestone until a clean artifact passes the live smoke.

Medium risk: the proof-specific converter copy can drift from the canonical UEFI converter; its hash gate and attribution reduce, but do not eliminate, that risk.

Low risk: opt-in stage-root discovery, absent-hint behavior, preferred-base rejection, source/staged integrity checks, and ignored-output rules are narrow and isolated. The trampoline is generic but ABI-sensitive and should remain in a separately reviewable native-ELF change.

## 18. Suggested commit grouping

The working tree is intentionally uncommitted. Suggested grouping is:

1. `tools/dotnet: pin attributed fixed-base PE-to-ELF proof converter` — the Strategy B copy, its source/hash documentation, and the converter hash gate.
2. `native-elf: keep fixed-base policy and optional TLS generic` — only if the existing cce TLS code is being split; include the generic key rename and focused source regression coverage. Preferred-base logic itself predates the managed proof and should normally remain in its existing generic commit.
3. `appmodel: add opt-in staged native ELF discovery` — the `app_registry.cpp` stage-root hook from `f4eb45f`, with no default inventory change.
4. `dotnet: add HostLogProof artifact and staging pipeline` — sample, build/stage scripts, manifest, and semantic artifact assertions.
5. `tests: add NativeAOT managed artifact and live smoke coverage` — static and live smoke scripts, plus the generic focused check.
6. `docs: record managed HostLog milestone closeout` — this document and any updates to the existing .NET proof/dependency documents.

The build-native experimental source-list and vendor include changes are pre-existing generic branch build support and should not be mixed into the managed milestone unless their own provenance is intentionally accepted.

## 19. Suggested cherry-pick order

Cherry-pick the converter/tooling pin first, then generic native-ELF prerequisites, then opt-in app-model discovery, then the managed artifact/staging pipeline, then smoke coverage, and documentation last. Keep the external UEFI converter commit separate from all Server commits.

## 20. Rollback instructions

Because no commit was created in this pass, rollback is by discarding only the closeout worktree files after reviewing them, while preserving unrelated user changes. At the commit level, remove the managed proof/stage/test commits in reverse order, then remove the opt-in stage-root and generic TLS changes if they are not used by another feature. Revert the external converter in its own repository separately. Do not reset the Server repository wholesale and do not delete the ignored vendor trees as part of rollback.

## 21. Exact next experiment

After the clean non-allocating entry path is restored, run one narrowly scoped allocation experiment:

> Execute one managed method that performs one controlled managed object or byte-array allocation, uses the allocated data to make one host log call, and returns successfully.

That pass must record whether runtime initialization is required before allocation, which allocator/GC entry points are reached, which platform services become reachable, whether allocation works without full threading support, whether the object remains valid through the host callback, whether a second launch succeeds, and whether reclamation can be observed or only allocation. Do not combine it with exceptions, threads, filesystem, GUI, networking, reflection, or `System.Console`.

## Validation record

| Command/check | Result in this closeout |
|---|---|
| `build-native-experimental.bat` | PASS; rebuilt experimental Server after the final source review |
| `build.bat` | PASS; exact `call build.bat` invocation returned `0` |
| `scripts/dotnet/build-managed-hostlog-proof.ps1 -Clean` | PASS; clean NativeAOT build and semantic artifact checks completed |
| `scripts/dotnet/stage-managed-hostlog-proof.ps1` | PASS; source/staged hashes matched and envelope was emitted |
| `scripts/smoke-dotnet-managed-hostlog-artifact.ps1` | PASS after final converter-copy normalization; semantic envelope and staging checks passed |
| `scripts/smoke-native-elf-generic.ps1` | PASS in the final focused source-level run |
| `scripts/smoke-dotnet-managed-hostlog-execution.ps1` | FAIL reproducibly in two separate Server processes: exit `-1073741819` (`0xC0000005`) before host log; the script's failure probe was not reached |
| default inventory isolation | PASS in preflight checks before the live positive launch |

## 22. Reverse-P/Invoke entry diagnosis follow-up

The original Outcome C history below is preserved. The exact clean-build fault, entry-symbol chain, TLS/FLS requirements, converter provenance, and artifact comparison are documented in [NATIVEAOT_REVERSE_PINVOKE_ENTRY_DIAGNOSIS.md](NATIVEAOT_REVERSE_PINVOKE_ENTRY_DIAGNOSIS.md).

Final status remains **Outcome C - Custom runtime pack required**. The current TLS envelope is not a runtime-correct NativeAOT reverse-P/Invoke initialization, and no allocation experiment is authorized until the custom runtime/platform pack establishes the managed transition and repeat-launch proof.

**Decision: Outcome C — Regression found during stabilization.**
