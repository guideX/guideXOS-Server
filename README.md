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
