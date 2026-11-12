# TOOLS13R1H — First Native AMD64 Guest Acceptance

**Outcome: Authorization hold — `TOOLS13R1H_READY_PENDING_AUTHORIZATION`**

**Date:** 2026-11-11 (Pacific)
**Scope:** Experimental compatibility and functionality preparation; not production release certification.

No guest boot was attempted. Static inputs and the QAC runner were prepared and checked. The independent QAC ownership gate is still unavailable: the live coordinator already has a verified `guideXOSServer` acceptance session, and the inspected records contain no newer .NET Support handoff or independent Tools-lane authorization. No reservation was requested, no QEMU was launched, and no unrelated session was touched.

## Repositories and initial state

| Role | Repository | Branch | HEAD at preflight | Initial worktree |
|---|---|---|---|---|
| Tools evidence and runner | `D:\dev\guideXOS_DeveloperStudio_Tools` | `DEVELOPER_STUDIO_TOOLS` | `849899f6a10279ef373006b839f7b8ae6303fd0a` | Clean; 4 commits ahead of `origin/DEVELOPER_STUDIO_TOOLS` |
| Developer Studio source and prepared ESP | `D:\dev\guideXOS_DeveloperStudio_Tools_Source` | `DEVELOPER_STUDIO_TOOLS` | `48f2da543517bd7238378c5a18769be148b10863` | Clean |
| Integrated Server candidate | `D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION` | `codex/tools13r1e-server-integration` | `e712106509473247eba959a494b65595769372a6` | Clean |
| QAC coordination/evidence | `D:\dev\guideXOS_QAC` | `main` | `1ea193258690f9e7b39390b101a117166ef8bbdd` | Clean |

The expected Server, source, and Tools evidence HEADs matched. The Server kernel header at this candidate declares `KERNEL_VERSION_STRING "0.1.0"`. Existing hosted integration evidence remains in [TOOLS13R1E](TOOLS13R1E_PLATFORM_VERSION_AND_INTEGRATION.md) and the image build recovery record [TOOLS13R1G](TOOLS13R1G_AMD64_BUILD_RECOVERY.md). No Developer Studio product source, Server source, release package, or production artifact was changed.

## Platform-version disposition

The manifest continues to declare application version `0.1.0`, minimum `minGuideXOSVersion` `0.5.0`, and ABI `guidexos-c-abi-v1`. The candidate kernel header says `0.1.0`. As previously established, the inspected loader stores the minimum-version field but does not compare it to a platform version. No authoritative mapping establishes that the Server satisfies `0.5.0`.

**`TOOLS13R1H_EXPERIMENTAL_NONRELEASE_APPROVED`** — The owner’s TOOLS13R1H request explicitly authorizes proceeding as an experimental, non-release compatibility test while the platform-version contract remains unresolved. It does not approve a claim that the Server satisfies `0.5.0`, certify production compatibility, change metadata, or bypass an enforced rejection. This disposition is specific to the user’s request for this phase.

## Prepared ESP and application identities

The exact TOOLS13R1G input is the directory-backed FAT volume at:

```text
D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image\ESP
```

Canonical audit result: **33 files**, **72,774,398 payload bytes**, tree SHA-256 **`DDB26BB2D986A331963971492C87EF2F5F9643AC07AB791E513DA966D2F59CDD`**. The established canonical procedure was applied: sort slash-normalized relative paths using ordinal ordering; for each file hash an eight-byte little-endian path-byte length, UTF-8 path bytes, an eight-byte little-endian file size, then the raw file bytes. The directory itself was not hashed as a filename and no archive digest was substituted.

Required inputs were present: `EFI/BOOT/BOOTX64.EFI`, `kernel.elf`, `ramdisk.img`, desktop/startup files, the Developer Studio manifest and both architecture packages, and the four-file C/C++ navigation fixture. The fixture is staged as `ESP/TOOLS13R1C/workspace`, matching the intended guest path `/TOOLS13R1C/workspace`. Static FAT path layout does not prove guest mount or file access.

| Artifact | Size | SHA-256 |
|---|---:|---|
| `EFI/BOOT/BOOTX64.EFI` | 46,592 | `D515E66BD4F59EB46171B88CCF8BF1DD27EACD62C2A9C8ACAECB0CE0D0E43998` |
| `kernel.elf` | 2,896,280 | `F1DA8EB275886FC5AA8126DE1CBFB38154FBA8C3509D971A93A4F2D89FACC6B5` |
| `ramdisk.img` | 67,108,864 | `7E93E34732526DBFFEE097912F776467B0DD03C989CBDA06F4AA24B0945A969B` |
| `Apps/DeveloperStudio/app.json` | 2,217 | `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE` |
| Accepted AMD64 `developerstudio.elf` | 1,262,228 | `9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16` |
| ARM64 `developerstudio.elf` | 1,448,964 | `9CEB966F4AD74EE70E138F7A2BA1797FCE9B50A007F3E57E74BD3C5EAD365AB7` |

The source package validator passed: exactly `app.json` and the AMD64/ARM64 ELF files; both are ELF64 little-endian `ET_EXEC`, have the expected machine type and no section metadata. AMD64 entry point: `0x500d36f0`. The manifest values and accepted AMD64 hash match the request. No raw image or ISO was derived, so a separate disk-image hash is not applicable. The prepared source ESP remains unchanged.

Fixture markers and intended navigation targets:

- `main.cpp:10`: `TODO: confirm Task List finds this source marker.`
- `helpers.cpp:3`: `FIXME: keep this marker discoverable by the Task List smoke.`
- `main.cpp` also contains `TOOLS13R1_KNOWN_SEARCH_ID` and a valid multi-line function for Go to Line.

The current source registers Tools commands in this order: Go to File, Go to Line, Find in Files, Replace in Files, Task List, Document Outline, Project Symbols. Essential acceptance was planned to use the menu where possible to avoid modifier-key state; no guest interaction occurred.

## QAC authorization and current ownership

QAC was discovered at `D:\dev\guideXOS_QAC`; its native CLI apphost is present at `src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`. `qac help` documents `status`, `list`, `queue`, `request`, and `run`; the supported managed launch form is `qac run --project DeveloperStudio --mode acceptance -- <qemu-system-*.exe> [arguments...]`. The Server’s existing QAC module supplies command-line construction, loopback QMP port allocation, and QMP shutdown conventions.

One read-only live snapshot at preflight reported:

- Coordinator Online, QAC 2.0.0, coordinator PID 8204.
- One managed acceptance session, zero queued and zero blocked requests, zero foreign QEMU processes.
- Active reservation `4fdb8b5c6cf54cb8a64413d004c409aa`, project `guideXOSServer`, QEMU PID 28664, owner PID 25408, owner `LEON2_RDT\guideX`, owner verified.
- QEMU executable: `C:\Program Files\qemu\qemu-system-x86_64.exe`.

The QAC list and status commands are read-only; this task issued no `request`, `run`, `cancel`, or `release`. The prior TOOLS13R1 acceptance records keep first QEMU priority with .NET Support and require an explicit handoff or independent Tools authorization. The current QAC record at the inspected QAC HEAD has no newer Tools authorization, and the active exclusive session is left intact. An idle coordinator or empty queue would not have granted permission either.

To allow a later runner launch, provide a real owner/handoff record as JSON with `scope: "TOOLS13R1H"`, `status: "approved"`, `authorizedBy`, `date`, `basis` set to `net-support-handoff` or `independent-tools-lane`, and a non-empty `evidence` reference. No such record was found, so there is no authorization file or fabricated approval in this checkout.

## Runner preparation and static validation

Runner source: [run-tools13r1h-guest-acceptance.ps1](../scripts/run-tools13r1h-guest-acceptance.ps1).

The default invocation is preparation-only:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools\scripts\run-tools13r1h-guest-acceptance.ps1
```

It recomputes the canonical ESP tree and accepted AMD64 hash, re-runs the read-only package validator, verifies the available QEMU/OVMF inputs, and exits before contacting QAC. **This invocation passed.** The runner parsed successfully with the PowerShell language parser. A separate `-RunGuest` invocation without an authorization record failed closed at the missing-QAC-authorization check, before private staging or QAC launch. It reports `PREPARE_ONLY`; no QAC reservation request or QEMU process was made.

The interactive `-RunGuest` path is gated by both this report’s experimental disposition marker and an existing QAC authorization JSON record. It copies the verified input into a unique run-private ESP and launches a single acceptance-mode QAC run; QEMU remains QAC’s direct child. It records serial, debugcon, QAC stdout/stderr, QMP responses, screenshots, observations, and a final QAC status/list/queue/inspect census. It uses QMP `send-key` and input events for paced guest input and QMP `screendump` for PPM screenshots. The QMP acknowledgment is recorded as transport evidence only. An operator must inspect the corresponding screenshot and serial evidence before recording a visible result; the run manifest remains incomplete pending evidence review. It never retries a guest boot, writes to the verified source ESP, forcibly kills QEMU, or releases someone else’s reservation.

Only after valid QAC authorization exists, the intended single-boot invocation is:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools\scripts\run-tools13r1h-guest-acceptance.ps1 `
  -RunGuest -QacAuthorizationPath <recorded-tools-lane-authorization.json>
```

The runner then uses the existing Server AMD64 QEMU baseline: `OVMF.fd`, 4096 MiB, `pc,usb=off`, standard VGA, a run-private directory-backed FAT volume, serial/debugcon files, and loopback QMP. The live QEMU path and Server firmware were found. No runner QMP command was executed against a guest in this phase, so QMP input/screenshot behavior remains unverified.

## Guest results and cleanup

No guest boot occurred. There are no TOOLS13R1H serial logs, screenshots, QMP traces, native exception diagnostics, guest filesystem observations, or guest resource measurements.

| Required milestone or check | Result |
|---|---|
| Bootloader, kernel, desktop/compositor, FAT availability, Native ELF loader | Not run |
| Package discovery, ELF validation/launch, real window, initial frame | Not run |
| Mouse and keyboard input | Not run; existing native window bridge lacks a keyboard key-up callback |
| A. Tools menu responds and expected commands visible | Not run |
| B. Go to File opens the fixture source and displays the actual file | Not run |
| C. Go to Line reaches a valid line and caret/content are verified | Not run |
| D. Task List finds TODO and FIXME, result opens the expected file/line | Not run |
| E. Back/Forward returns to two known locations | Not run |
| Optional tools | Not attempted |
| Developer Studio close, QMP shutdown, reservation release | Not applicable; this task owns no guest process or reservation |

The unrelated active `guideXOSServer` acceptance session remains under its existing verified owner. No QAC session or QEMU process created by this task remains. This outcome says nothing about guest runtime stability, Developer Studio’s guest functionality, or memory/release readiness. Existing hosted checks in TOOLS13R1E are supporting evidence only.

## Disposition and next scope

`TOOLS13R1H_READY_PENDING_AUTHORIZATION` applies: image, application package, QEMU, firmware, and offline runner preparation are ready, and the requested experimental version disposition is recorded, but QAC lane permission is missing and no guest run was attempted.

The next action is to obtain the explicit .NET Support handoff or independently authorized Tools lane, then execute this one-boot runner and review the genuine guest evidence. Keep the `0.5.0` minimum unchanged and unresolved unless the platform owner records an authoritative contract. Recommended TOOLS13R2 scope is limited to closing the version contract and addressing runtime or input defects actually evidenced by TOOLS13R1H. TOOLS13R2 was not started.
