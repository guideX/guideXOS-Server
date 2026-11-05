# S22R14R1R3 — Live QMP Listener Attribution and Stack Guest Qualification

Date: 2026-11-05 (Pacific)
Disposition: **Outcome B — QMP_LISTENER_CAUSE_ISOLATED; guest qualification remains pending**

## 1. Starting repository provenance and preservation

- Repository: D:\dev\guideXOSServer; branch main; starting HEAD 091ddba4eaa7c4afcdb66440de447722effb9830, matching the required HEAD and an ancestor of the starting checkout. The branch was already one commit ahead of origin/main; that commit was preserved.
- The tracked worktree was clean at phase start. The only pre-existing untracked file was assets/Backgrounds/troncity.png; its SHA-256 remained 444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda.
- All 13,029 paths in out/s22r14r1r1-stack-qualification/preflight/untracked-before.txt were present at preflight. New evidence is retained under out/s22r14r1r3-live-qmp-listener-attribution/.
- The QAC .phase marker remained unchanged (QAC3C3 is next); the server repository has no .phase file. No QAC source, dist, release-readiness marker, publication marker, branch, or worktree was changed. No image or kernel was rebuilt.

## 2. Candidate identity

The unchanged ServerV03PacManOnly candidate is:

| Input | Identity |
|---|---|
| ISO | out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso |
| ISO SHA-256 / size | 2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e / 90,245,120 bytes |
| Build ID / profile | 6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d / ServerV03PacManOnly |
| Canonical ESP tree identity | cf8fe881beb41ea5b3114a39d3dcd029cd9a96b129463bfe7dfffd9009bf4e4f (16 files) |
| Packaged kernel SHA-256 | b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd |
| Stack-audit kernel | out/s22r14r1-stack-audit/kernel/bin/kernel.elf; SHA-256 matches the packaged kernel |
| OVMF SHA-256 | 2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792 |

The existing receipt records one Native ELF package, PacMan, with 11 package files; Developer Studio is absent. The older accepted ISO remains unchanged at SHA-256 b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af. The runner staged a private writable ESP and used the ISO and OVMF read-only. Its manifest and post-run canonical-artifact check confirm that the source inputs remained unchanged.

## 3. Reconstruction of the earlier failures

The S22R14R1R2 audit records two November launches:

| Reservation / child | Endpoint and first failure |
|---|---|
| 48a521ae53b54531bba42dfc06f05565 / PID 25808 | QEMU was a direct QAC child with -S and -qmp tcp:127.0.0.1:53062,server=on,wait=off. The listener query was empty; passing that empty array to a mandatory PowerShell Connections parameter raised a binding exception before connection or QMP. |
| 62db03804b484717bd7534ede8a77a77 / PID 16104 | The run-specific helper accepted empty results and polled for 15 seconds, but retained evidence did not distinguish an absent socket, filtered row, ownership mismatch, or late listener. It used 127.0.0.1:62003, -S, and the same direct-child QAC architecture. No QMP connection or cont was recorded. |

The old runner queried Get-NetTCPConnection -State Listen -LocalPort <port> and required a loopback address and exact QAC child PID. The QAC CLI grants admission, starts QEMU as its direct child, and reports the reservation and process identity; the server runner supplies the QMP endpoint. The QAC contract was not bypassed or changed.

## 4. Windows listener-discovery findings

The hosted Windows fixture opened TcpListener on an ephemeral 127.0.0.1 port and queried the same PowerShell API used by the runner. On this host, Get-NetTCPConnection -State Listen -LocalPort 62392 returned no matching rows and a no-match error. Get-NetTCPConnection -LocalPort 62392 returned the fixture's exact loopback row with its owning PID, RemoteAddress=0.0.0.0, RemotePort=0, and State=$null. This reproduces a Windows discovery-filter mismatch with a real local socket; it is API evidence, not a QEMU/QAC reproduction. Windows documents the connection state and owning-process fields in [MSFT_NetTCPConnection](https://learn.microsoft.com/en-us/windows/win32/fwp/wmi/nettcpipprov/msft-nettcpconnection).

The runner now records a port-scoped state-filtered query and, when that query is empty or errors, cross-checks a port-only query and a broad local snapshot filtered in memory. It retains only rows for the configured port. A row is connection-eligible only for the exact configured IPv4 loopback and exact QAC child PID; a state-null row must also be an unconnected listener-shaped row. The runner rechecks the direct-child process identity and then requires a successful QMP greeting, capabilities negotiation, per-run QEMU name, and paused status. The operating-system query is diagnostic only; it does not authorize a connection to an arbitrary endpoint. Microsoft's command reference describes netsh interface ipv4 show excludedportrange as the query for current excluded port ranges ([Microsoft Learn](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/netsh-interface)).

## 5. Instrumented live QAC launch and first failure

Before admission, QAC was Online. An unrelated managed AArch64 acceptance session was active, so the runner used the normal queue. Reservation 4d262981eb1a4f8b963a1a1537a42073 entered queue position 1 and was granted about 51 seconds later. No other QAC owner was interrupted.

The runner used one QMP port attempt and retained the existing 15-second listener startup deadline; it did not extend that bound.

The single admitted attempt used QEMU 11.0.0 (v11.0.0-12122-ga4bb4b10c9) at C:\Program Files\qemu\qemu-system-x86_64.exe. QAC owner PID 23568 created direct child PID 27768, process start 2026-11-05T16:59:20.9504683Z. The actual argument vector included:

    -machine pc,accel=whpx -smp 2 -m 2048M -vga std -display none
    -drive if=pflash,format=raw,readonly=on,file=<verified OVMF>
    -drive if=ide,format=raw,file=fat:rw:<private ESP>,index=0
    -drive if=none,id=releasecdrom,file=<candidate ISO>,media=cdrom,readonly=on,format=raw
    -net none -monitor none
    -qmp tcp:127.0.0.1:59493,server=on,wait=off
    -S

The first live failure was **QEMU's QMP socket bind**, before listener polling. QEMU reported:

    -qmp tcp:127.0.0.1:59493,server=on,wait=off: Failed to bind socket: Input/output error

QEMU exited with code 1 about 117 ms after process creation. There were zero QMP listener probes, an empty QMP transcript, no listener observation, and no serial or QEMU debug output. The child never presented a listener for ownership matching, and no protocol command was sent. Thus the live failure in this phase was not caused by the state-filter query returning an empty collection.

Immediately after the run, netsh interface ipv4 show excludedportrange protocol=tcp reported the range 59462–59561; the selected port 59493 falls inside it. The range snapshot is post-run rather than a prelaunch snapshot, so the evidence strongly supports an excluded-port bind failure but does not prove the exclusion was unchanged at the exact bind instant. The random allocator used before this correction selected from 49152–65535 and did not check whether Windows could bind the chosen port. This is the leading root cause for this launch, with that timing limitation recorded in out/s22r14r1r3-live-qmp-listener-attribution/preflight/qmp-bind-failure-analysis.json.

## 6. Runner correction and regression coverage

Get-QacS22R10QmpPortCandidate now asks Windows to bind an exclusive loopback TcpListener to port 0, captures the OS-assigned ephemeral port, then releases the probe socket before constructing the QAC command. It retries the historical fixed port and caller-excluded candidates. This avoids choosing a port that is already occupied or excluded at selection time. The probe does not reserve the port through QAC's admission wait; final authorization still depends on exact child ownership and the QMP handshake. The runner's state-null discovery fallback, bounded probe log, continuation gate, and exact-child startup cleanup remain in place.

The final hosted runner suite passes **125/125** under the repository's PowerShell 7.6.5 host. The 102-assertion baseline is retained. Added coverage includes empty and delayed listener observations, filter misses, exact and mismatched owners, IPv4/IPv6 representations, child exit and ownership changes, connection refusal, malformed or missing greetings, capabilities failure, paused-state checks, one successful continuation, bounded timeout attribution, exact-child cleanup that leaves another same-executable process running, Windows API behavior, and bindable port selection. Tests verify that cont cannot precede ownership and protocol negotiation. A separate Windows PowerShell 5.1 invocation failed before tests because its compiler lacks System.Text.Json; the supported PowerShell 7 run passed, and both invocation logs are retained under the phase preflight directory. git diff --check passes.

## 7. QAC lifecycle and guest result

QAC logged the direct child identity, the bind error, child exit, and reservation release. Independent inspect shows reservation 4d262981eb1a4f8b963a1a1537a42073 as Released, with the exact QEMU PID, executable, and start time. The runner independently verified child exit and reservation release; RecoveryRequired is false. QAC needed no forced child cleanup because QEMU had exited itself.

The final post-commit read-only QAC snapshot at 2026-11-05T17:17:07.280Z was Online with zero reservations, managed sessions, queued requests, or QEMU processes. The QAC phase marker still says QAC3C3 is next.

No QMP greeting, qmp_capabilities, query-name, query-status, or cont occurred. The candidate guest boot count is **zero**. There was no UEFI transfer, kernel initialization, desktop, or guest progress. The final runner record and QAC logs are retained in the run directory named by the manifest under out/s22r14r1r3-live-qmp-listener-attribution/runs/.

## 8. Stack budget and guest coverage

The repaired candidate kernel matches the S22R14R1 stack-audit kernel. The existing stack guard reports:

| Path | Static usage | Headroom against 16,384 B | Guest coverage |
|---|---:|---:|---|
| Settings modern Display | 11,512 B | 4,872 B | Not executed |
| Settings legacy Display Options | 11,896 B | 4,488 B | Not executed |
| COM2 storage service | 7,768 B | 8,616 B | Not executed |

All three static estimates exceed the required 4,096 B headroom. They are compiler/call-path estimates, not runtime high-water measurements or a whole-kernel proof.

- **Settings modern:** no guest interaction was possible.
- **Settings legacy:** remains unqualified. Whether its runtime path is reachable through the supported guest workflow remains undetermined because no guest booted; no result is inferred from modern-path coverage.
- **COM2:** no service request was sent. The existing documented non-destructive system_service_runtime_client.exe path was not run because no guest booted.
- **PacMan:** no Start refresh, registration, launch, gameplay input, clean exit, or runtime-cleanup marker was observed.
- **Navigator:** no file:///Apps/PacMan/app.json lookup occurred.
- **VFS:** /Apps, five-mount integrity, and post-interaction desktop/VFS responsiveness were not observed.

## 9. Independent release blockers

This phase did not change readiness or publication status. Carry forward the existing issues: isolated-build _wcsicmp reproducibility failure; /Documents creation limitation; /.startmenu_recent persistence failure; privileged Native ELF execution; writable native application package discovery; inadequate cross-version ABI negotiation; and absent enforceable Native ELF application isolation.

## 10. Outcome and recommended next phase

**Outcome B — QMP_LISTENER_CAUSE_ISOLATED.** This launch isolates a live failure to QEMU's QMP bind stage and strongly implicates selection of a Windows-excluded port. The runner now obtains a bindable OS-assigned port and the hosted regression passes. The post-run timing of the exclusion snapshot limits certainty, and the earlier R1R2 listener-poll failure remains unexplained by this run. Guest acceptance is entirely pending.

Recommended next Main Integration phase: a separately authorized QAC3C3 follow-up using the corrected runner, with a prelaunch excluded-port snapshot, one bounded QAC attempt, and the same unchanged ISO. If QMP succeeds, continue that managed session directly into boot, Settings modern and legacy reachability, COM2, PacMan, Navigator, and VFS qualification. Do not advance the QAC phase marker or publication status on this evidence.

## Evidence index

- Preflight and preservation inventory: out/s22r14r1r3-live-qmp-listener-attribution/preflight/
- Windows listener API fixture: preflight/windows-listener-api-observation.json
- Post-run excluded-range snapshot and bind analysis: preflight/windows-excluded-tcp-port-ranges-postrun.txt, preflight/qmp-bind-failure-analysis.json
- Final hosted test output: preflight/hosted-runner-tests-final.txt
- Final read-only QAC ownership snapshot: preflight/qac-postrun-final-snapshot.json
- Instrumented QAC run: locate by RunId s22r14r1r3-qmp-20261105-d283c39254e547f481fcae997ac99767-b754364cca014716a8c766ee4d58a115 under out/s22r14r1r3-live-qmp-listener-attribution/runs/; it contains the manifest, exact QEMU arguments, QAC logs, execution events, QMP probe file, and final reservation inspection.
