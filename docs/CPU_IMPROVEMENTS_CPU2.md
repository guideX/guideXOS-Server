# CPU2 · AMD64 ACPI/MADT processor inventory

**Outcome B — Implementation and builds verified. Guest and hardware qualification pending.** Hosted fixtures exercise firmware-table traversal, the UEFI memory-range policy, the BootInfo extension, and boot processor correlation. The UEFI loader and three freestanding AMD64 configurations linked successfully.

## Starting state and source inventory

The worktree at `D:\dev\guideXOSServer_CPU_IMPROVEMENTS` started clean on `CPU_IMPROVEMENTS` at CPU1 commit `f85566375c483fca04230408b64878c8327eac65`. At preflight the local branch was one commit ahead of its tracking ref at `14be308b`. The checked-out CPU1 report identifies the active UEFI, CPUID, PIC/PIT, and protected-task paths. [CPU1 audit](CPU_IMPROVEMENTS_CPU1.md)

| Source | Role |
| --- | --- |
| UEFI loader and paging | The loader locates the RSDP in the EFI configuration table and publishes its physical address in `BootInfo::AcpiRsdp`. CPU1's loader mapped four pages near that address. The root and MADT may reside elsewhere. The page-table switch takes place in the handoff trampoline, and the one-page BootInfo allocation already has an explicit identity mapping. [Loader](../guideXOSBootLoader/main.cpp) · [Paging](../guideXOSBootLoader/paging.cpp) |
| ACPI discovery | CPU2 gives the ACPI 2.0 GUID priority over the ACPI 1.0 GUID. The loader captures the inventory with the first successful final UEFI memory-map fetch, before the initial `ExitBootServices` attempt. It uses firmware's existing mappings and the existing memory-map buffer. The capture replaces the heuristic RSDP mapping and the early revision read from the configuration pointer. [Capture and parser](../guideXOSBootLoader/acpi_processor_inventory.cpp) |
| BootInfo delivery | A versioned, checksummed extension carries the result in the mapped BootInfo page. The original 572-byte v3 header and its checksum contract are preserved. Flag bit 4 advertises the extension at byte 576. The extension is 2,640 bytes and has a separate 8-bit zero-sum checksum. Source addresses are numeric provenance. Discovery failures also use this handoff and boot continues. [Base handoff](../guideXOSBootLoader/guidexOSBootInfo.h) · [Extension](../guideXOSBootLoader/bootinfo_acpi.h) |
| AMD64 kernel | `CPU_INVENTORY_DIAGNOSTIC=1` enables the one-shot serial report near AMD64 kernel entry. The reporter validates the base header, page alignment, extension version, size and checksum, RSDP provenance, and record bound before consuming the inventory. Earlier UEFI loaders and Multiboot yield `handoff-unavailable`. The default kernel skips the reporter. [Build gate](../kernel/Makefile) · [Boot call](../kernel/core/main.cpp) · [Reporter](../kernel/arch/amd64/boot_processor_inventory.cpp) |

## ACPI and MADT parsing contract

1. Each requested physical span must be nonzero and have a representable exclusive end. The production reader accepts spans below 128 TiB wholly contained in one page-aligned, readable `EfiACPIReclaimMemory` or `EfiACPIMemoryNVS` descriptor from a successful UEFI version-1 memory map. It rejects read-protected descriptors and checks page counts and span ends before casting any address to a pointer. The descriptor buffer has a 256 KiB limit and a 40–256 byte stride, divisible by eight, with no trailing partial descriptor. AMD64 UEFI guarantees identity mappings for the eligible memory while firmware paging is active. [UEFI Specification §2.3.4](https://uefi.org/specs/UEFI/2.11/02_Overview.html#x64-platforms)
2. Require the RSDP's eight-byte signature and 20-byte checksum. Revision 0 uses the 32-bit RSDT pointer at byte 16. Revision 1 is classified as unsupported. Revision 2 or later also requires an accessible extended prefix, a declared length of 36–4,096 bytes, and a checksum over the full declared span. Use the 64-bit XSDT pointer at byte 24 when present. An absent XSDT pointer allows the RSDT. An invalid selected XSDT fails discovery.
3. Require the selected root's signature, 36-byte header, valid full-table checksum, and a payload divisible by its entry width of four or eight bytes. Scan at most 256 entries. By design, discovery reads just the four-byte signature of each candidate before selecting the MADT. An unreadable candidate prevents a complete search. A second `APIC` entry, including an alias of the first, is reported as `multiple-madts`.
4. Require a readable 44–65,536 byte MADT with `APIC` signature and full-table checksum. Walk records from byte 44. Each record must have at least its two-byte header, and its declared length must fit the remaining table bytes. Multi-byte fields use explicit little-endian decoding. Record lengths greater than the known minimum are accepted. Other record types advance by their validated length. [ACPI Specification §§5.2.5–5.2.8 and §5.2.12](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html)

| MADT processor record | Minimum bytes | Fields retained in addition to the type and table offset |
| --- | ---: | --- |
| Type 0, Processor Local APIC | 8 | 8-bit ACPI processor identifier at byte 2, 8-bit APIC ID at byte 3, 32-bit flags at byte 4 |
| Type 9, Processor Local x2APIC | 16 | 32-bit x2APIC ID at byte 4, 32-bit flags at byte 8, 32-bit ACPI processor UID at byte 12 |

Flag bit 0 identifies a firmware-enabled record. For MADT revision 5 or later, bit 1 identifies a firmware-online-capable record when bit 0 is clear. Raw flags are retained at every revision. The counts describe all type 0 and type 9 records, including disabled or duplicate entries. The first 128 records are retained in firmware order. A longer inventory still validates every record and returns total counts with `truncated` status. A structural error anywhere suppresses counts and records. [ACPI 6.3, Processor Local APIC flags](https://uefi.org/sites/default/files/resources/ACPI_Spec_6_3_A_Oct_6_2020.pdf)

Equal APIC ID, numeric UID, type, and flags mark both retained records as duplicates. Otherwise a shared APIC ID or numeric UID marks the retained records as conflicting. All-ones APIC IDs at their respective widths remain visible as invalid identifiers and are excluded from identity comparison. The inventory records RSDP and MADT revisions, RSDP/root/MADT physical addresses, root kind, MADT length, and the failing table, physical address, and offset when available.

| Discovery status | Meaning |
| --- | --- |
| `no-rsdp`, `memory-map-unavailable` | Missing configuration pointer or unsupported memory-map format |
| `invalid-address`, `unreadable` | Null or overflowing span, or no eligible UEFI mapping for the full span |
| `invalid-signature`, `invalid-checksum`, `invalid-length`, `unsupported-rsdp-revision` | Table contract violation |
| `no-madt`, `multiple-madts` | Missing or ambiguous MADT reference |
| `limit-exceeded`, `invalid-record` | Table or root bound exceeded, or malformed MADT record |
| `complete`, `truncated` | Structurally valid inventory, with complete or limited record retention |

## CPU1 identity and single-processor operation

CPU2 uses CPU1's injectable CPUID API and native adapter. Intel-style reporting prefers usable subleaf 0 of `0x1F`, then `0x0B`. AMD prefers `0x80000026`, then `0x0B`, then the APIC ID in `0x8000001E:EAX` when `TOPOEXT` is reported. Hygon uses the AMD-compatible `0x0B` and gated `0x8000001E` path. The eight-bit initial APIC ID from leaf 1 requires its APIC feature bit. Basic and extended maximum-leaf checks apply to each read. CPU1's NX query retains its existing callers and policy. [CPU1 decoder](../kernel/arch/amd64/cpu_capabilities.cpp) · [Intel instruction reference](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) · [AMD APM Volume 3, Appendix E](https://docs.amd.com/api/khub/documents/j44LvPXzuuXgM0WyHKQfeQ/content) · [AMD CPUID specification](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/design-guides/25481.pdf)

Full-width IDs compare directly with valid IDs from either record type. A leaf-1 ID can establish a type-0 match when there is no x2APIC record with an aliasing low byte. An alias yields `identity-insufficient`. A unique matched ID is reported with its firmware enablement state. Duplicate or conflicting matches yield `ambiguous`, and a complete usable identity search with no candidate yields `mismatch`. Missing identity, unavailable inventory, and truncation have separate results. The CPUID logical-count hint stays independent of all MADT counts. [Correlation](../kernel/arch/amd64/boot_processor_inventory.cpp)

The serial summary identifies the executing boot processor through CPUID where available and reports `guidexos-running-processors=1` for the existing single-processor boot path. Firmware counts describe inventory records. The default path still loads the IDT, remaps the PIC to IRQ vectors 32–47, configures 100 Hz PIT IRQ0 and emits heartbeat records every 100 ticks. The source and object checks cover continued use of the PIC/PIT entry points. Timer delivery, the IRQ-to-input path, and desktop continuity await a guest run. [Boot and desktop loop](../kernel/core/main.cpp) · [Interrupt setup](../kernel/core/interrupts.cpp) · [PIT handler](../kernel/core/pit.cpp)

## Verification

### Hosted tests

| Check | Result |
| --- | --- |
| `scripts/test-amd64-processor-inventory.ps1` with MinGW GCC 15.2.0 and `-Werror`. Same sources with Visual Studio x64 `cl.exe /W4 /WX`. | PASS on both. Six fixture groups cover single and mixed inventories, RSDT/XSDT, firmware states, wide IDs, absent or malformed tables, capacity, duplicate and conflicting identities, CPUID leaf preference and correlation, UEFI memory ranges, and the handoff. |
| `scripts/test-amd64-cpu-capabilities.ps1` and the same sources with Visual Studio x64 `cl.exe /W4 /WX` | PASS on both. Eight CPU1 fixture groups plus the hosted native CPUID and NX query check. The AMD fixture now expects the AMD leaf sequence. |
| `scripts/test-amd64-protected-task-contract.ps1`, `scripts/test-amd64-context-switch.ps1`, and `scripts/test-amd64-memory-foundation.ps1` | PASS. Protected-task contract, voluntary Microsoft x64 context transitions, and frame ownership and private address-space checks. |
| `scripts/test-native-elf-security-policy.ps1` and `scripts/run-native-abi-layout-test.ps1` | PASS. Native ELF policy 29/29, `failures=0 nativeEntryCalls=4`, and Native ABI layout. |
| `scripts/run-ps2keyboard-modifier-test.ps1` and `scripts/run-compositor-pointer-buttons-test.ps1` | PASS. PS/2 modifier cases 39/39 with the existing unsigned-comparison warning, and compositor pointer-button checks. |
| `scripts/test-release-build-integrity.ps1`, `tests/qac_s22r18_public_preview_test.ps1`, and `tests/pacman_source_registration_test.ps1` | PASS. 37 release-integrity assertions, seven synthetic preview staging and argument assertions, and four PacMan source-registration fixtures. |
| `scripts/smoke-mmio-mapping.ps1` | PASS. Existing MMIO mapping source checks. |

### Build and artifact checks

All builds used new or CPU2-only output paths under `%TEMP%\gxos-cpu2-870fd49b`. The repository bootstrap reconstructed and verified the pinned Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 trees with three profile patches in scratch. The AMD64 make builds used separate `BUILD_DIR` values, the scratch `GUIDEXOS_REPO_ROOT` for the TLS dependency and verifier, and the checkout's `settings_presentation_core.cpp` through `SETTINGS_PRESENTATION_SOURCE`. The first build attempt lacked the verifier's tracked patch files in scratch. Copying those files into the scratch profile resolved the failure.

| Build | Result | Bytes and SHA-256 |
| --- | --- | --- |
| AMD64 UEFI loader, MSBuild x64 Release, v143 | PASS, PE32+ EFI application | 61,952 bytes, `499d5ed9838d6e3602041f6a9d72db288a4052de3a584cef42a277e420464cd7` |
| Default, `CPU_INVENTORY_DIAGNOSTIC=0 CPL3_DIAGNOSTIC=0` | PASS, ELF64 | 2,839,000 bytes, `123d1d9f8aa4e5df47d8562ef574ab520ef50d610e50ae16731d15e280119519` |
| Inventory, `CPU_INVENTORY_DIAGNOSTIC=1 CPL3_DIAGNOSTIC=0` | PASS, ELF64 | 2,847,632 bytes, `e881f47ceeabcebabbd84cd8c7d6eb24641c95623d5baebcf0fed53cdf820719` |
| Combined diagnostic, `CPU_INVENTORY_DIAGNOSTIC=1 CPL3_DIAGNOSTIC=1` | PASS, ELF64 | 2,854,176 bytes, `5daa08d6afff37d777c585ba509119cdf49781efd2829b93ccabb84883d4e34b` |

All three kernels passed TLS profile verification and the freestanding TLS compile probe. `readelf` reported x86-64 ELF64 executables with entry `0x100000` and zero ELF relocations. All three `main.o` files reference the PIC/PIT path. The inventory reporter is referenced only by the two opted-in variants. The CPL3 diagnostic is referenced only by the combined variant. The accepted S22R23 public-preview ISO was verified at SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`. [Accepted ISO identity](RELEASE_S22R28_CPL3_GUEST_QUALIFICATION.md#artifact-and-repository-identity)

### Guest and hardware qualification

Pending. The requested acceptance lane restriction limited this work to hosted fixtures, source checks, artifact inspection, and compilation/linking. The synthetic preview test operates on staging fixtures and argument construction. The live firmware map, handoff capture, serial inventory, PIT heartbeat, physical input, and desktop behavior are unqualified on the CPU2 binaries.

## Coverage limits and cross-branch integration

- The adapter covers AMD64 UEFI boot-time ACPI table memory. A table span crossing descriptors, a map with another descriptor format, or an inaccessible root candidate can yield an unavailable inventory. The parser supports APIC processor records and uses the MADT revision to interpret online-capable flags. AML namespace validation, FADT version reconciliation, hotplug, NUMA topology, and other interrupt models are outside this inventory.
- Two diagnostics use the same base BootInfo page. Coordinate any branch that changes the base struct size, uses flag bit 4, or adds fields to the page tail. Carry the updated UEFI loader with CPU2 when an inventory is required. A branch that consumes raw ACPI addresses needs its own mapping contract because CPU2 carries data through the page extension.
- The single-CPU assumptions in the frame allocator, TSS, exception stacks, Native ELF entry, desktop event loop, and protected-task diagnostic remain the integration baseline. Controller routing needs independent review of IRQ0/1/12, EOI ownership, MSI destination widths, and driver interrupt registration. [CPU1 integration risks](CPU_IMPROVEMENTS_CPU1.md#remaining-risks-and-integration-points)

## Recommended CPU3 scope

Qualify the CPU2 handoff with the matching loader and inventory-only kernel in an explicitly approved isolated guest lane. Exercise one vCPU and several vCPUs, capture firmware counts and boot APIC correlation, and verify PIC/PIT heartbeat, keyboard, pointer, desktop startup, and synchronous Native ELF return. Then add a read-only interrupt routing inventory for the MADT's IOAPIC and override records. Keep controller enablement and AP startup in later milestones with per-CPU ownership and driver qualification.
