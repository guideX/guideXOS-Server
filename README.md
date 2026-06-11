# guideXOS Server

A multi-architecture operating system exploring a future where applications are not tied to a single CPU architecture.

---

## At a Glance

* 11 CPU architectures supported (x86 -> RISC-V -> IA-64)
* Strict layered OS design
* UEFI-first modern boot path
* Kernel-level networking stack
* Desktop environment already running
* Future universal app format (`.gxapp`)

---

## What is guideXOS?

guideXOS Server is an experimental operating system focused on one big idea:

**What if apps could run across completely different CPU architectures... natively?**

Instead of locking software to x86 or ARM, guideXOS is building toward a universal application platform backed by a multi-architecture kernel.

---

## Architecture

```text
Firmware -> Bootloader -> Kernel -> guideXOSServer -> Applications
```

### Core Principles

* Bootloader loads kernel only
* Kernel is the only boot-aware layer
* GUI lives in user space
* No shortcuts, no layer violations

---

## Supported Architectures

| Tier     | Architectures                          |
| -------- | -------------------------------------- |
| MVP      | x86, amd64, riscv64                    |
| Next     | arm64, ia64, sparc64                   |
| Extended | arm, sparc, ppc64, mips64, loongarch64 |

Total: 11 architectures in-tree

---

## What Already Works

### Boot and Platform

* UEFI bootloader (primary path)
* BIOS / legacy support
* ACPI and OpenSBI support

### Storage

* ATA / AHCI / NVMe
* USB storage
* FAT32, exFAT, ext2/4, UFS

### Networking

* IPv4 stack (TCP, UDP, ICMP)
* DHCP and DNS
* Kernel socket layer

### Graphics and Input

* Framebuffer rendering
* PS/2 and USB input
* Multi-platform display backends

---

## Current Gaps

* IPv6
* GPU acceleration
* VirtIO
* Full ARM64 maturity
* Security features (ASLR, TPM, Secure Boot)

---

## Build and Run

### Recommended (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -RunQemu
```

### Quick Dev Loop

```bash
make amd64
make qemu
```

### Opt-In Public HTTPS Probe

The bare-metal Navigator public HTTPS probe is a separate proof workflow, not a default browsing mode.
Current milestone: bare-metal Navigator can render a reviewed real public HTTPS page through opt-in public proof mode.

* Deterministic kernel smoke: `.\scripts\smoke-navigator-kernel.ps1`
* Dedicated public HTTPS proof: `.\scripts\smoke-navigator-public-https.ps1`
* Reviewed target matrix helper: `.\scripts\smoke-navigator-public-https-allowlist.ps1`
* Optional combined debug matrix: `.\scripts\smoke-navigator-kernel.ps1 -IncludePublicPilot`
* Workflow: `.github/workflows/navigator-public-https-probe.yml`
* Secret: `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM`
* Default target: `https://sha256.badssl.com/`
* Reviewed public target allowlist v0.4:
  * `https://sha256.badssl.com/` because it is a stable badssl DNS-hosted HTTPS endpoint that proves DNS, TCP, TLS, certificate, and hostname validation without opening general public browsing
* Candidate prep helper: `scripts/prepare-navigator-shipped-root-candidate.ps1`
* Candidate evidence linker: `scripts/promote-navigator-public-https-evidence.ps1`
* Automated artifact assertion: `scripts/assert-navigator-public-https-pass.ps1`
* Structured evidence export: `scripts/export-navigator-public-https-evidence.ps1`
* Public-root validator/manifest helper: `scripts/validate-navigator-ca-bundle.ps1`
* Review/operator contract: `scripts/fixtures/README.md`
* Interactive screenshot launch: `.\scripts\run-navigator-public-https-screenshot.ps1`

The screenshot launcher defaults to `https://sha256.badssl.com/`, rejects targets outside the reviewed allowlist, and stages the same explicit proof policy and trust material as the passing proof:

```powershell
.\scripts\run-navigator-public-https-screenshot.ps1
```

It requires the untracked local public root bundle at `scripts/fixtures/public-roots/ca-bundle.pem.local`. For a non-interactive staging check, use `.\scripts\run-navigator-public-https-screenshot.ps1 -StageOnly`.

Reviewed real-root proof packs are written under `logs/navigator-public-https-proof-pack-<timestamp>/`. The pack keeps the summary log, serial log, evidence JSON, optional candidate metadata, optional promotion record, and the CA bundle manifest sidecar together without copying PEM contents or secret source paths. If a real public-root PEM is unavailable, the dedicated proof path stays `SETUP_BLOCKED` and the proof pack records that blocked state instead of claiming PASS.

Normal hosted and default kernel smoke remain deterministic and internet-independent unless you explicitly opt into the public-pilot lane or the dedicated proof path. Automated PASS assertion and evidence JSON promotion complement, but do not replace, human artifact review. The local dedicated proof script, reviewed-target matrix helper, candidate evidence linker, and manual GitHub Actions workflow all now fail-closed to the reviewed public target allowlist unless you intentionally use the explicit reviewed override path for a one-off run.

The default bare-metal kernel smoke now also runs a deterministic HTTPS compatibility matrix against controlled fixtures, covering friendly `404`/`500` pages, text/plain rendering, redirects, unsupported content-encoding UX, unsupported-content downloads, and response/header safety caps without enabling default public browsing. Dedicated public-proof artifacts separately classify TLS transport proof versus content compatibility, so a run can prove real-world DNS/TCP/TLS/certificate/hostname behavior even when unsupported compression or response caps prevent a full page-render success.

Public pilot hardening v0.4 adds compact transport diagnostics to the dedicated proof lane, including TLS connect/retry counts, retry reason, pre-write retry bytes, handshake/transport error codes, failure classification, TCP-abort visibility, redirect-hop context, and reviewed allowlist version reporting. The reviewed-target matrix helper now also preserves one copied summary/evidence pair per reviewed target plus an aggregate allowlist summary log, while keeping the public lane explicit and separate from deterministic smoke.

Public-root provisioning is still explicit and operator-driven in this pass. The harness now validates supplied CA bundles, writes a manifest sidecar with SHA-256, root count, bundle type, and `production_ready` / `test_only` flags, and expects dedicated public probe evidence to archive those facts before any trust-bundle rotation is treated as complete. Default public HTTPS browsing remains off, and the repo still does not download public roots automatically.

Shipped-root candidate review is also explicit. Candidate preparation and evidence linking can validate, fingerprint, and archive a proposed bundle for later review without enabling default public HTTPS.

Public HTTPS remains opt-in because DHCP cleanup, root lifecycle and rotation, revocation, broader content compatibility, decompression, cookies, cache, JavaScript, and HTTP/2 remain future work. The next planned visible GUI pass is toolbar button icons, a loading throbber, and Roboto/font descender cleanup.

### Experimental Native ELF hosted runtime

Native ELF execution in the hosted runtime is experimental. Normal `build.bat` does not enable execution. `build-native-experimental.bat` builds `guideXOSServer.experimental.exe` with experimental execution enabled for trusted local validation only.

Current support is intentionally narrow: amd64 host, amd64 app, static `ET_EXEC`, no `PT_INTERP`, no dynamic linking, no relocations, preferred-base mapping must succeed, and guideXOS C ABI v1 (`guidexos-c-abi-v1`) only.

Unsupported: `ET_DYN`/PIE, shared libraries, libc-heavy apps, cross-architecture execution, dynamic linker, and arbitrary host filesystem access.

### Kernel Only

```bash
cd kernel
make ARCH=amd64
```

---

## Run in QEMU

```bash
run-qemu.bat
```

Uses:

* UEFI (OVMF)
* q35 machine
* FAT ESP
* serial debug output

---

## Roadmap (Realistic)

### Phase 8 (Current Focus)

* Developer SDK
* `.gxapp` universal format
* Cross-architecture toolchains
* musl libc integration
* Package management

### Path Forward

1. Stabilize kernel and builds
2. Finish syscall and SDK foundation
3. Deliver `.gxapp` system
4. MVP on:

   * x86
   * amd64
   * riscv64
5. Expand architecture support

---

## Why This Project Exists

Most operating systems optimize for one architecture.

guideXOS asks:

**What if architecture did not matter anymore?**

---

## Project Status

* Active development
* Research-focused
* Not production-ready

Most stable path today:
Windows -> amd64 -> UEFI -> QEMU

---

## Project Layout

```text
guideXOS.SERVER/
├── guideXOSBootLoader/   UEFI bootloader
├── kernel/               multi-arch kernel
│   ├── core/
│   ├── arch/
│   └── docs/
├── docs/                 planning and SDK
├── scripts/              build and run tools
├── ESP/                  boot output
├── build.ps1             main build script
└── README.md
```

---

## Contributing

Before adding anything, ask:

* Which layer does this belong in?
* Can this be done in user mode?
* Does this break layering rules?

Avoid:

* GUI in kernel
* Bootloader loading user-mode
* Layer shortcuts

---

## License

Copyright (c) 2024-2026 guideX

---

## Final Thought

guideXOS is not trying to compete with existing operating systems.

It is exploring what comes after them.
