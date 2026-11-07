# S22R14R1R6 — COM2 and Legacy Settings Coverage

**Outcome: `COM2_RUNTIME_QUALIFIED` (Outcome A).** One read-only COM2 `GetStorageSnapshot` request reached the unchanged candidate's real storage provider, returned a non-empty kernel snapshot, and completed with a matching successful response. Legacy Display Options still returns an explicit unavailable result; this is safe error handling, not functional display configuration. No release waiver or contract change is made here.

## 1. Repository and candidate provenance

- Repository: `D:\dev\guideXOSServer`.
- Required branch and starting HEAD: `main` / `47a5c19c1608b3ea91fbc5706b53ea2397f85322`.
- The existing untracked `assets/Backgrounds/troncity.png` is retained untouched. Generated output remains under the existing ignored `out/` tree.
- QAC phase file in the sibling QAC repository: QAC1–QAC3C2 accepted, QAC3 open, next QAC3C3. Read only.
- No branch/worktree switch, reset, clean, stash, rebase, merge, cherry-pick, push, or publication action was performed. No `dist`, readiness marker, release receipt, or candidate artifact was modified.

The receipt-selected candidate remains the unchanged PacMan-only Server v0.3 ISO:

| Item | Identity |
|---|---|
| ISO | `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso` |
| ISO SHA-256 | `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` |
| Staged kernel SHA-256 | `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` |
| Build/profile | `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d` / `ServerV03PacManOnly` |
| Receipt source commit | `5b62216efe78158fb12707c126ec38582faecf59` |
| Packages | PacMan only; Developer Studio not selected |

The ISO and kernel hashes were verified against the existing manifest and staged ESP. The relevant kernel source paths remain unchanged from the receipt source commit. No product kernel or ISO rebuild was made. S22R14R1R5 remains valid evidence for this exact candidate; its guest results and its COM2/legacy boundaries are recorded in [the R5 report](RELEASE_S22R14R1R5_GUEST_INTERACTION_AND_COM2_COVERAGE.md).

## 2. COM2 service contract and call graph

COM2 is an optional host-runtime service channel implemented as a 16550 UART endpoint at I/O base `0x2F8`, IRQ3. It is distinct from the guest's ordinary COM1 debug log. The bridge is compiled into the kernel and `kernel_main` calls `system_service_bridge::init()`. Its hardware setup and initialized marker are under `ARCH_HAS_PIC_8259`; on supported AMD64 PC/QEMU this configures 115200 8N1, enables UART receive interrupts, and registers IRQ3. The IRQ handler only drains bytes into a bounded RX ring and acknowledges IRQ3.

Normal boot reaches `system_service_bridge::poll()` from the ordinary `kernel_main` loop. Polling consumes complete frames and otherwise does not invoke a provider. The read-only storage request path is:

```text
COM2 UART RX IRQ3
  -> bounded RX ring
  -> kernel_main main loop: system_service_bridge::poll()
  -> consumeByte() / frame validation
  -> serveRequest()
  -> settings_inventory_provider::appModelProvider()
  -> system-service dispatcher (trusted system-service peer)
  -> readStorage callback
  -> settings_inventory_provider::readStorageSnapshot()
  -> each registered block device; readMbr() reads LBA 0
  -> block::read_sectors() through the device readFn
  -> response encoding, COM2 UART write, serial result markers
```

The operation is `GetStorageSnapshot`, request type `0004`, response type `8004`. It reads block-device metadata, reads the MBR sector to classify partitions, and enumerates current VFS mounts. It does not issue block writes. The call path is present only when the PIC/COM2 bridge is available and a host attaches a serial endpoint and submits a valid request. The regular R5 QEMU launch attached COM1 only and had no host COM2 requester; no ordinary user UI action emits this request. Thus COM2 init/polling during a regular boot did not establish request, dispatch, provider, or response execution.

The existing public hosted client API is in `system_service_client.h`; `SystemServiceClient::getStorageSnapshot()` uses the existing `QemuCom2TcpTransport` in `system_service_qemu_transport.h` (default TCP port `17771`). The QEMU runner can attach the second serial endpoint as a loopback TCP server without changing the ISO or kernel. This is a supported external host-runtime invocation of the existing read-only service, not a production guest UI action. Whether that external API is part of the v0.3 release contract remains a release-owner decision.

## 3. Static stack guard and its boundaries

The boot stack capacity is 16,384 bytes and the guard requires at least 4,096 bytes of estimated headroom. The accepted R1 guard output, replayed against the R1 object tree, reports:

| Guarded path | Static guarded usage | Estimated headroom |
|---|---:|---:|
| Modern Settings Display | 11,512 bytes | 4,872 bytes |
| Legacy Display Options | 11,896 bytes | 4,488 bytes |
| COM2 storage dispatch | 7,472 bytes | 8,912 bytes |
| COM2 storage decode | 7,768 bytes | 8,616 bytes |

For COM2 dispatch the guard includes the measured `kernel_main`, bridge poll/serve path (with dispatcher inlined into the serve frame), `readStorageSnapshot`, `readMbr`, generic `block::read_sectors`, snapshot validation, a 1,024-byte unmodeled-call reserve, 512-byte interrupt reserve, and 40-byte boot-entry reserve. The decode path is separately guarded. The source and `.su` records used for the replay match the candidate's receipt source.

The provider calls a per-device `readFn` indirectly. The callback's concrete driver frame is not enumerated as a distinct path in the static sum; the reserve is a bound, not proof of every dynamic call. The guard is a compiler-derived static path estimate. It is not runtime stack high-water telemetry, a whole-kernel call-graph proof, or whole-kernel memory-safety evidence. Hosted tests, guest initialization, guest request/provider execution, and runtime stack measurements are separate evidence categories.

## 4. Hosted service regressions

The existing hosted bridge suite was run using `scripts/run-system-service-bridge-test.ps1`:

- Device bridge: **14/14 passed**.
- Storage bridge: **15/15 passed**.
- System-service bridge: **82/82 passed**.

The suite exercises production protocol/client/dispatcher code, request validation, dispatch, test provider selection and invocation, snapshot encode/decode, repeated/cached generations, unavailable/invalid/timeout/error paths, and response handling. The provider callbacks are test fixtures; the suite does not execute the candidate's UART IRQ, kernel main loop, real block-device provider, MBR read, or candidate guest stack. It is meaningful hosted logic evidence but not candidate runtime qualification.

The captured R6 rerun output is `out/s22r14r1r6-com2-legacy-20261107/system-service-bridge-hosted-validation.txt`; its executable is `out/s22r14r1r6-com2-legacy-20261107/system_service_bridge_test-r1r6.exe`.

The repaired QAC launcher regression suite also passed **150/150** assertions after adding the optional COM2 serial argument, distinct-port validation, COM2 guest-marker result check, and runner contract assertions. The R5 runner baseline had passed 143/143 before these seven focused assertions were added. No freestanding code changed.

The COM2 manifest preflight defect was fixed with `Set-QacS22R10Com2ManifestTransport`; the full launcher suite then passed **151/151** assertions. The output is retained at `out/s22r14r1r6-com2-legacy-20261107/qac-launcher-hosted-validation-r6.txt`. This adds an explicit fixture proving metadata can be added to an existing staged JSON manifest and survives round-trip serialization.

## 5. Legacy Settings backend contract

The legacy `DisplayOptionsApp::queryDisplayConfiguration()` submits `QueryActiveConfiguration` to `DisplayConfigurationService`. A failed or unsuccessful response is rendered as `Display query failed`; the UI does not present a fabricated active configuration. The service asks `virtio::gpu::get_display_configuration_backend_snapshots()` for detected and active configurations and reports `BackendUnavailable` with a diagnostic when no backend is ready.

The intended legacy configuration backend is the QEMU VirtIO-GPU display configuration/compositor path. Its probe and live-compositor implementation are gated by `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE` and `GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE`; control UI behavior is additionally gated by `GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE`. The exact staged candidate kernel contains the literal `QEMU-only probe gate is disabled`, which is in the `#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)` branch of `kernel/core/virtio_gpu.cpp`; this confirms the probe gate was absent in the release kernel. The `get_display_configuration_backend_snapshots()` implementation returns false unless both probe and compositor gates are compiled in. The canonical receipt identifies a `ServerV03PacManOnly` build and not the diagnostic `EXTRA_CFLAGS` used by the QEMU VirtIO-GPU control/manual scripts. The production-representative QAC profile also used standard VGA and provided no VirtIO-GPU device. Changing QEMU's VGA device alone cannot enable the missing compile-time gate. Modern Settings exposes a read-only display/framebuffer snapshot and does not replace the legacy display configuration service.

The route is exposed to ordinary users through Control Panel → Display → Display Options and the Settings advanced-display entry. R5 directly opened the legacy Display Options page; its Display tab showed monitor/backend and resolution unavailable and `Display query failed`, with serial query-failure diagnostics. This proves the app route and query/error path ran. It does not prove successful display configuration, apply, refresh, or a pending topology transition.

For this candidate, the defensible behavioral classification is **correctly returns an explicitly unsupported/unavailable state**. That classification describes the observed and implemented failure semantics; it does not make the display feature functional or establish that the release contract excludes it. Because the route remains visible, the release owner must decide whether this capability is supported in v0.3. If required, the smallest genuine remediation is to deliver and initialize the intended production VirtIO-GPU backend in a separately identified candidate and then qualify query and configuration operations. No diagnostic-only ISO or mock backend is justified in this phase.

## 6. Targeted QAC plan — written before admission request

### Preflight and configuration

Before requesting admission on 2026-11-07, read-only QAC status/list/queue checks showed the coordinator online, one active managed acceptance session owned by `APP_MODEL` (reservation `cfbe003e2d594bb28b2f1eb5e40e8e6c`, QEMU child PID `30304`), zero foreign QEMU processes, and no queued or blocked requests. That owner's process is left untouched. The new acceptance request is allowed to wait in QAC's FIFO queue; no QEMU starts before QAC grants admission. Preflight captures are retained in `out/s22r14r1r6-com2-legacy-20261107/preflight/`.

Two local preflight attempts stopped before QAC admission. The first used the transport's default port `17771` and was rejected by runner parameter validation, which requires a port in `49152–65535`. The second staged the exact candidate, then exposed that the optional `com2Transport` property could not be assigned to the pre-existing JSON manifest object. Neither attempt requested QAC admission or launched QEMU; the staged `...-01-3c73022e46264e1d96d507288bc9e8ac` workspace is retained with its failure records. The runner now uses an exported manifest helper that adds the property safely. Its focused manifest round-trip regression and the full existing launcher suite pass **151/151**. The corrected qualification endpoint is `54177`; it was confirmed available before retry.

Immediately before the successful retry, QAC was `Online` with zero reservations, sessions, queued or blocked requests and zero QEMU processes. Port `54177` was exclusively bindable. The earlier APP_MODEL owner had released its reservation; it was not interrupted.

The exact planned invocation is:

```powershell
pwsh -NoProfile -File scripts/run-qac-s22r10-uefi.ps1 `
  -EspPath 'out\release-profile\6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d\ESP' `
  -IsoPath 'out\release-profile\6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d\artifacts\guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso' `
  -OvmfPath 'docs\evidence\settings-s22-v03-package-20261018\OVMF-s22-readonly.fd' `
  -RunId 's22r14r1r6-com2-20261107-02' `
  -OutputRoot 'out\s22r14r1r6-com2-legacy-20261107' `
  -QacCliPath 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe' `
  -QemuPath 'C:\Program Files\qemu\qemu-system-x86_64.exe' `
  -ExpectedEspSha256 'cf8fe881beb41ea5b3114a39d3dcd029cd9a96b129463bfe7dfffd9009bf4e4f' `
  -ExpectedIsoSha256 '2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e' `
  -ExpectedOvmfSha256 '2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792' `
  -AdmissionTimeoutSeconds 1800 `
  -GuestRuntimeTimeoutSeconds 600 `
  -Com2TcpPort 54177
```

This is a QAC acceptance run using the repaired existing runner. The candidate ISO and OVMF are read-only inputs; the runner stages the verified ESP into its private run workspace. The guest profile remains AMD64 PC, 2 CPUs, 2048 MiB, standard VGA, no network, COM1 file serial, and QMP loopback, with one additional loopback TCP COM2 serial endpoint at `127.0.0.1:54177`. QAC remains the only admission and direct-child owner.

### Single read-only request and direct evidence

After the runner confirms QAC ownership, QMP identity, guest running state, and that the COM2 listener belongs to the exact QEMU child PID, a focused host client will make exactly one call to the existing `SystemServiceClient::getStorageSnapshot()` API using `QemuCom2TcpTransport`. Its temporary source and executable stay under the ignored R6 `out/` directory. It checks `ClientResult::Ok`, `Backend::Kernel`, a non-zero generation, at least one disk, capacity and sector-size availability, and returns without issuing any mutation request.

Success requires all of the following direct evidence: COM2 initialized marker; exactly one guest request marker for type `0004`; non-empty `[STORAGESNAP]` provider marker; read-only authorization marker; matching request-ID response with status `0000`, result `ok`; host client confirms the returned storage snapshot; serial retains the five-mount boot table and no panic/fault or storage/VFS regression; and QAC records a normal result and verified shutdown/release. The COM2 marker gate alone is not sufficient for `result pass`; the host client must succeed first. A desktop screenshot is retained to confirm the guest remains responsive.

Stop on admission/ownership ambiguity, wrong TCP listener owner, missing initialization, client timeout/protocol failure, absent or empty provider snapshot, response-ID/status mismatch, panic/fault, or VFS/storage integrity regression. Preserve first-failure evidence; do not try other guest operations. Use QAC/QMP only through the exact reservation and runner ownership checks, then verify the exact child exit and QAC release. No broad process termination is permitted.

## 7. Actual guest qualification

The exact planned QAC acceptance run completed with the unchanged candidate:

| Item | Result |
|---|---|
| Run ID | `s22r14r1r6-com2-20261107-02-b3fe748722df40e28aecc3e27203269d` |
| QAC reservation | `5b982bfa75f14424b899629b04cd871f` — `Released` |
| Direct QEMU child | PID `19364`, `C:\Program Files\qemu\qemu-system-x86_64.exe`, exit 0 verified |
| QMP endpoint | `127.0.0.1:56910`; identity/capabilities succeeded; `cont` and final `quit` acknowledged |
| COM2 endpoint | `127.0.0.1:54177`, bound by the exact QAC-owned QEMU child |
| Candidate ISO | SHA-256 `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` |
| Staged kernel | SHA-256 `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` |

The private ESP staging hash matched the expected `cf8fe881beb41ea5b3114a39d3dcd029cd9a96b129463bfe7dfffd9009bf4e4f`; the read-only ISO and OVMF hashes matched their expected values. The launch retained the production-representative AMD64 PC, standard VGA, no-network, COM1 file log, read-only ISO/firmware, and private ESP configuration. Only the explicit loopback COM2 serial endpoint was added.

The guest serial log proves the full request path, with one request and a matching response:

```text
[SYSBRIDGE] initialized transport=uart-com2 protocol=1 max-response=110C
[SYSBRIDGE] request received type=0004 id=00000001
[STORAGESNAP] generation=0000000000000001 disks=00000003 volumes=00000005 truncated=00 ...
[SYSBRIDGE] authorized service=read_only_snapshot
[SYSBRIDGE] response sent id=00000001 status=0000 result=ok
```

The actual host client called `SystemServiceClient::getStorageSnapshot()` exactly once and returned `ClientResult::Ok`, `Backend::Kernel`, generation 1, 3 disks, 5 volumes, no truncation, first-disk capacity 528,482,304 bytes, 512-byte sectors, and one partition. Its output is retained at `out/s22r14r1r6-com2-legacy-20261107/s22r14r1r6-com2-20261107-02-b3fe748722df40e28aecc3e27203269d/output/com2-client.log`. The focused client issued no mutations.

Before the request, the boot log recorded the expected five mounts (`/`, `/system`, `/certs`, `/config`, `/config/navigator`). The provider returned five volumes, the request caused no VFS reinitialization or integrity failure, and no panic, triple-fault, or unhandled-exception marker appeared. The desktop remained available to QMP; the QMP screenshot was retained at `out/s22r14r1r6-com2-legacy-20261107/s22r14r1r6-com2-20261107-02-b3fe748722df40e28aecc3e27203269d/output/screenshots/com2-desktop-after.ppm`.

Only after the client result and guest markers passed did the runner record `result pass`. The runner then sent QMP `quit`, observed its acknowledgment, and QAC verified child exit and reservation release. The final QAC inspection for this run reported `Released`, child PID 19364, exit 0, and `childExitVerified=true`. The manifest records `qacExecution.status=released` and `com2Transport.endpoint=127.0.0.1:54177`.

The QAC snapshot taken after release showed this run fully released. A separate `APP_MODEL` acceptance reservation (`585e37effd8e4246a6e7410198c5c023`, child PID `11064`, owner PID `16720`) had started by that time; QAC showed zero foreign processes and zero queued/blocked requests. That concurrent owner was left untouched. Final status/list/queue and inspect captures are under `out/s22r14r1r6-com2-legacy-20261107/postflight/`.

## 8. Independent release blockers

The following remain independent of COM2 and legacy Settings and are carried forward without implementation in this phase:

1. `_wcsicmp` isolated-build reproducibility failure.
2. `/Documents` functionality limitation.
3. `/.startmenu_recent` persistence limitation.
4. Kernel-privileged Native ELF execution.
5. Writable runtime application package discovery.
6. Inadequate cross-version Native ABI negotiation.
7. Lack of enforceable Native ELF process isolation.

No readiness-matrix item or release marker is waived or removed here.

## 9. Release-owner decisions and S22R14R2 entry

- **COM2 classification:** Optional and runtime-qualified through the supported external read-only client when the QEMU COM2 endpoint is attached. The default PacMan-only boot has no COM2 client; release ownership must still decide whether the external API belongs to the v0.3 support contract.
- **Legacy Settings classification:** Correctly returns an explicitly unsupported/unavailable state in this candidate. The user-visible display configuration remains unavailable; if that route is required in v0.3, this is a missing required backend to remediate and qualify on a separate candidate.
- Decide whether the external read-only COM2 host-runtime service is part of the v0.3 supported contract. If it is, retain the exact-candidate runtime evidence below as a required acceptance check. The default PacMan-only guest launch has no COM2 client, so user-facing boot acceptance alone cannot cover it.
- Decide whether legacy Display Options is supported in v0.3. If required, plan the intended production VirtIO-GPU backend enablement/initialization and a separately identified candidate; if excluded, record that contract decision explicitly while preserving the visible unavailable behavior.
- Carry the seven independent blockers forward unchanged.
- Recommended S22R14R2 entry: record COM2's supported external invocation and exact-image runtime evidence; record the owner decision for legacy display; no broad desktop smoke or diagnostic-only ISO without a specific owner-approved requirement.
