# QAC3C2 — UEFI Artifact Isolation

## Scope and pilot

QAC3C2 prepares one private, verified writable artifact workspace for the selected UEFI workflow. Staging is independent of QAC admission and does not start QEMU. QAC3C3 owns the later admission, direct-child launch, QMP allocation, and verified-exit lifecycle.

The pilot is the completed S22R10 PacMan UEFI qualification, boot 2 (storage-backed), recorded under docs/evidence/s22r10-pacman-guest-qualification-20261020/. Its entry point was the evidence runner qac-launch.py, which invoked qac run --project guideXOSServer --mode acceptance -- <QEMU> <arguments>. The boot used a directory-backed FAT ESP as its writable IDE disk, the candidate ISO as a read-only CD-ROM, and a read-only OVMF firmware image. It produced serial, QEMU debug, QMP, and screenshot evidence in that boot's evidence directory. Its QMP listener used 127.0.0.1:65253; that fixed endpoint is not reused by the staging helper.

This is a historical completed qualification build with a unique release-profile build ID. QAC3C2 read the existing source artifacts and wrote only to a new local scratch workspace. It did not rebuild, publish, replace, or mutate the candidate.

## Artifact collision inventory

| Workflow or input | Observed behavior and collision risk | Classification |
|---|---|---|
| S22R10 boot 2 pilot | Source is out/release-profile/<build-id>/ESP. The recorded QEMU run mounted a disposable copy with fat:rw; it attached the candidate ISO and OVMF image read-only. There was no separate pflash VARS image or additional writable guest disk. Its historical QMP port was fixed. | Copy ESP per run; share verified ISO and firmware read-only; output directory per run; QAC3C3 must allocate QMP dynamically. |
| scripts/test-release-iso.ps1 | Attaches the ISO read-only. It creates a GUID-named out/release-iso/qemu-test-* directory for logs and, when split OVMF is used, a private VARS copy. Combined OVMF is shared read-only. It uses fixed VNC :0, no QAC admission or QMP, and directly stops its own QEMU PID at timeout. | Existing logs and split VARS are per-run; fixed VNC can collide; QAC integration and endpoint/process lifecycle are future work. |
| scripts/smoke-live-directory-desktop-runtime.ps1 | Mounts repository ESP through fat:rw. Its optional build deletes shared kernel object files and rebuilds; the serial name has second precision and the evidence filename is static. It starts QEMU directly and stops its PID on timeout. | Shared writable ESP and build outputs require coordination or per-run copies; outputs need independent names; future architectural work. |
| scripts/smoke-imageviewer-baremetal-runtime.ps1 | May rebuild shared outputs and writes the ramdisk into repository ESP. It deletes ESP/NvVars to reset the firmware state, then mounts that same ESP through fat:rw. Its serial log has second precision and its evidence name is stable per smoke label. | Requires private ESP, ramdisk, and NvVars state plus independent outputs; concurrent runs can collide today; future architectural work. |
| scripts/smoke-desktop-startup-sync.ps1 | Uses the shared repository ESP/OVMF path and timestamp-based logs, launches QEMU directly, and has no QAC admission. | Requires a private writable ESP and independent output paths before concurrent use; future architectural work. |
| scripts/create-release-iso.ps1 and shared build tree | The PacMan-only release profile stages under a build-ID directory. Other build flows consume repository ESP and shared kernel/build products. Inputs may be rewritten by a build while a snapshot is being prepared. | Use a completed unique build as the pilot source; hash before and after copy; coordinate before staging from shared mutable build outputs. |
| scripts/run-qemu-x86-with-build.bat and scripts/run-qemu-x86.bat | These are accepted Server workflows with no ESP. Build-and-run writes the shared x86 kernel output before QAC execution; the prebuilt QAC launcher validates and hashes its kernel but does not modify it. | Outside this ESP-specific helper. QAC process coordination does not serialize the earlier shared build step. |

For the pilot, the writable ESP's bootloader, kernel, ramdisk, and application files are all part of the directory copy. QEMU may create or update NvVars in that per-run FAT directory. The ISO and OVMF image remain read-only shared inputs. The workflow has no second writable disk. There is no VNC or monitor endpoint; QMP is the only fixed historical endpoint. Existing pilot output names were isolated by the per-boot evidence directory.

## Staging interface and workspace contract

The operator-facing entry point is scripts/prepare-qac-uefi-artifacts.ps1; the implementation is scripts/qac-uefi-artifacts.psm1. The entry point requires explicit ESP, ISO, and OVMF image paths and can take an expected source SHA-256 for each. It returns JSON containing the unique run ID, workspace, and manifest paths. It never invokes QEMU or QAC.

The default scratch directory is %LOCALAPPDATA%\guideXOS\QAC\artifacts, outside the repository and release directories. Scratch roots must be dedicated local-volume directories and may not overlap any source path. Before creating a workspace the helper rejects missing or malformed sources, unexpected formats, source or destination reparse points, traversal labels, insufficient free space, and supplied source-hash mismatches. The default free-space reserve is 256 MiB beyond the exact bytes to copy.

Each workspace contains:

    <scratch-root>\<run-label>-<random-guid>\
      .qac3c2-owner.json
      .complete
      manifest.json
      artifacts\
        esp\                 # private writable FAT directory backing
      output\                # per-run serial/debug/QMP/screenshot destinations

Only artifacts declared writable are copied. The ISO and OVMF image are marked read-only/share and retain their verified source paths. A requested run label is not used as the workspace identity by itself; every invocation receives a new GUID suffix. Creation uses exclusive owner-marker creation, so an existing workspace is never reused or overwritten.

The JSON manifest schema version is 1. It records:

- run ID, requested label, project, selected workflow, UTC creation time, workspace path and root, Windows owner, and random owner token;
- completionStatus (staging, complete, or failed);
- source path, size, SHA-256, per-file inventory, access mode, disposition, private path, private identity, and relative path for each artifact;
- output and log paths relative to the workspace;
- the QEMU argument template and QMP allocation contract;
- QAC execution state, initially not-started.

The helper inventories and hashes sources before copying, copies only into a freshly owned workspace, hashes the staged copies, then re-inventories the canonical inputs. A mismatch leaves completionStatus: failed, omits .complete, and preserves the partial workspace. Successful work writes the completion manifest and marker only after all identities match. The hashes detect ordinary concurrent mutation but do not provide a transactional filesystem snapshot or exclude a writer.

Test-QacUefiArtifactManifest validates the completion marker, owner identity, current Windows user, root containment, workspace-relative paths, reparse-point boundaries, artifact hashes, and shared read-only source hashes. It rejects malformed or incomplete manifests. A future launcher must validate again immediately before constructing QEMU arguments.

## Firmware, storage, and endpoint decisions

The pilot's QEMU configuration points -drive if=pflash,format=raw,readonly=on at the OVMF firmware image. It has no separate writable pflash VARS image. Its ESP source contains the bootloader, kernel, ramdisk, and application payload; a fresh source tree has no NvVars. The historical guest created NvVars only in the disposable fat:rw copy, so the new helper copies the whole source ESP directory per run and contains that state there.

The candidate ISO is attached as media=cdrom,readonly=on,format=raw. It is shared after SHA-256 checks because QEMU does not write it. There is no additional writable guest disk or backing-file chain in this workflow. No overlay is needed.

The manifest contains the pilot machine, storage, firmware, media, network, serial, and debug arguments, using placeholders for artifact/output paths. It intentionally leaves QMP port unset. QAC3C3 must allocate a per-run loopback port after the run obtains the appropriate QAC admission, retain its ownership through the direct QEMU child lifetime, and record verified child exit before marking the workspace released. A port observed free during staging is never treated as reserved.

## Cleanup and ownership

No background service or stale-workspace purge is added. Failed or abandoned workspaces remain available. Explicit cleanup requires the full generated run ID and a valid owner marker matching the current Windows user and declared scratch root. Cleanup accepts a workspace that has never been launched, or one marked released with verified child exit. It refuses active, unknown, malformed, or unverified release states, rejects paths outside the scratch root, and refuses any reparse point in the workspace tree. Cleanup affects only the exact verified workspace directory.

QAC3C3 must mark the manifest active before launching and must not mark it released until QAC verifies the direct QEMU child has exited. A future workflow must not bypass that lifecycle and then rely on QAC3C2 cleanup state.

## Qualification results

The real input source was the S22R10 build-ID ESP directory, the exact candidate ISO from that build ID, and the OVMF firmware image used by the prior guest runs. Before staging:

- all 16 source ESP files matched the saved pre-boot inventory (71,435,180 bytes; tree SHA-256 5ffa5777faf76e10633bae441e61e81989d45ffb660aff42feff49674060efe7);
- the ISO was 90,245,120 bytes, SHA-256 e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b, matching its recorded candidate hash;
- OVMF firmware image was 3,653,632 bytes, SHA-256 2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792.

Two invocations ran concurrently through the public preparation script. They received distinct GUID-suffixed run IDs, workspace roots, writable ESP paths, and output directories. Each private ESP contained 71,435,180 bytes and matched the source tree identity. Both manifests validated. A test-owned marker file was added to the first private ESP and confirmed absent from the second private ESP and canonical source; it was removed afterward, and both manifests validated again. The read-only ISO and OVMF paths were safely shared. Post-staging per-file ESP hashes, the ISO hash, and the OVMF hash matched their preflight values.

The deterministic suite covers valid and invalid sources, exact identities, canonical immutability, private writes, concurrent workspaces, output separation, generic VARS/disk copies, missing sources, synthetic insufficient space, expected-hash mismatch, mid-copy source mutation, incomplete staging, run-ID reuse, traversal, junctions, malformed manifests, cleanup bounds/ownership, active or uncertain cleanup refusal, and canonical integrity after the suite. Its exact assertion count and the existing launcher regression totals are recorded in docs/phase-validation.md in the QAC repository.

Real staging verifies payload structure and isolation; it did not boot QEMU. The prior S22R10 boot qualification remains the boot evidence. This phase does not qualify every UEFI launcher and does not claim that shared mutable paths in the other smoke scripts are safe.

The retained source inventories, pre/post identities, manifests, concurrent write proof, and test transcript are in the local scratch evidence directory recorded by the QAC validation log. The large private ESP copies and test workspaces are not Git artifacts.

## QAC3C3 handoff

QAC3C3 can consume manifest.json, validate it, use its per-run ESP path and shared read-only ISO/OVMF paths, substitute the manifest output paths, allocate QMP under QAC ownership, and launch QEMU as the direct child only after admission. QAC3C3 still needs to implement the execution-state update and verified-exit recording. No QAC scheduler, WPF, x86 launcher, or Developer Studio acceptance contract was changed here.
