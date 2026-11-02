# AARCH64 BOOTDTB2 Property Attribution

**Outcome: `ARM64_DTB_REPAIRED_GUEST_PENDING`**

**Run date:** 2026-11-02

**Repository:** `D:\dev\guideXOSServer_AARCH64`
**Branch/base:** `AARCH64_SUPPORT` at `45add80b6ac02238feee8e955690828d478d228c`; upstream `origin/AARCH64_SUPPORT`; one commit ahead at preflight.

The exact firmware property and production predicate are established. A narrowly scoped parser repair accepts bus-defined address and size cell counts while retaining the existing bounds on resource decoding. The captured firmware source DTB passes the corrected production parser on the host. A repaired guest boot is still pending QAC admission; this record does not claim GIC, timer, or scheduler guest acceptance.

## Baseline and preserved evidence

The tracked worktree was clean at the expected HEAD before this task. Existing BOOTDTB1 and ABI3 qualification candidates were left intact. New artifacts are isolated under `out/aarch64-bootdtb2-*`.

The diagnostic candidate was built from the source state containing bounded parser diagnostics and `allocator-reservation=deferred`, before the targeted cell-count repair. Its Phase 4 guest inputs were:

| Artifact | SHA-256 |
| --- | --- |
| `BOOTAA64.EFI` | `6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d` |
| `kernel.elf` | `180240671db572bd88ce1e0944d5f3f318191664430eab84a670bdd1f249c341` |
| `esp/ramdisk.img` | `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |

The source-matched repaired Phase 4 candidate is `out/aarch64-bootdtb2-repaired-20261102`:

| Artifact | SHA-256 |
| --- | --- |
| `BOOTAA64.EFI` | `6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d` |
| `kernel.elf` | `fb0908e8511369284b440c054a946560d76b149903d57adb5c2590b75f006f3a` |
| `esp/ramdisk.img` | `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |

The guest configuration was verified on the installed tools: QEMU 11.0.0 (`8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748`), `virt,gic-version=2,acpi=off`, Cortex-A53, 512 MiB, EDK2 UEFI, copied fresh variables, and no Studio image in the minimal Phase 4 candidate. Firmware hashes were `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d` for `edk2-aarch64-code.fd` and `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f` for `edk2-arm-vars.fd`.

## Exact rejected property

The authorized diagnostic guest reported:

```text
predicate=P3
source condition: cells == 0 || cells > 2
node: /pcie@10000000
property: #address-cells
value length: 4
raw bytes: 00 00 00 03
decoded u32: 3
FDT_PROP token offset: 5568 (0x15c0)
value-length field: 5572 (0x15c4)
property-name-offset field: 5576 (0x15c8)
value start: 5580 (0x15cc)
strings-block-relative property-name offset: 11 (0x0b)
inherited #address-cells: 2
inherited #size-cells: 2
applicable #interrupt-cells: 1
```

The offsets above are relative to the FDT structure block, not to the start of the DTB. The parser trace's `structure-offset=5572` is the cursor immediately after consuming the token, at the value-length field.

`#address-cells` has the Devicetree value type `<u32>`. Its numeric meaning depends on the bus represented by the node. The PCI bus binding sets `#address-cells` to 3; the Linux PCI host-bridge documentation shows the same value for `pcie@10000000`. Therefore `00 00 00 03` is the valid PCI child-address width, not malformed FDT data. See the [Devicetree PCI bus binding](https://www.devicetree.org/open-firmware/bindings/pci/pci2_1.pdf), the [Devicetree specification's cell-count rules](https://github.com/devicetree-org/devicetree-specification/blob/main/source/chapter2-devicetree-basics.rst), and the [Linux PCI host-controller example](https://docs.kernel.org/PCI/controller/pci-controller-drivers.html#10-1-writing-pci-host-controller-drivers).

The parser incorrectly applied its one-or-two-cell resource-decoding limit as a universal format limit while walking an otherwise irrelevant PCI node. The confirmed defect category is **A: valid property rejected by guideXOS**. The property value observed by the guest parser is also present in the captured firmware source DTB. This rules out corruption at the rejected property; the full loader-copy blob was not captured, so this does not establish byte-for-byte equality of every source and copy byte.

The original predicate was `length == 4` followed by `cells == 0 || cells > 2`. The repaired property predicate keeps the four-byte `<u32>` encoding check and retains the full bus-defined cell count; it no longer applies the resource decoder's `1..2` width limit during generic property parsing. Resource consumers continue to reject widths they cannot safely decode.

## DTB capture and independent decoding

The diagnostic guest received genuine QAC reservation `6ad13e7ce7a24c7eb8b2e28eaec0edc4` for project `guideXOSServer-AARCH64-BOOTDTB2`. QEMU was launched and the reservation was released after the bounded diagnostic run.

The candidate runner paused the still-running guest through QMP and saved only the runtime-validated source DTB range. `info mtree` placed both the firmware source (`0x47ef2000`) and loader copy (`0x5865e000`) inside the live `mach-virt.ram` range `0x40000000–0x5fffffff`. The loader's validated header declared `totalsize=0x101000` (1,052,672 bytes). The source artifact was created at `2026-11-02T23:38:16.3721695Z`, before the QAC log's QEMU exit time `2026-11-02T23:38:16.5667514Z`.

The initial capture record says `CAPTURE_FAILED` because a PowerShell signed/unsigned magic comparison rejected the valid value `0xd00dfeed`. The complete source file itself was subsequently checked: its length is exactly the declared totalsize, its header is valid, and the independent decoder traversed the entire structure. The source blob is therefore recorded as captured and validated after this audit, with the original false-negative preserved in `capture.json` and the audit in `capture-audit.json`.

| Capture artifact | Result |
| --- | --- |
| Firmware source DTB | `out/aarch64-bootdtb2-minimal-20261102/bootdtb2-guest-20261102-153806-117/firmware-source.dtb` |
| Declared and actual length | 1,052,672 bytes (`0x101000`) |
| SHA-256 | `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311` |
| Independent decoded property count | 223 |
| Loader-copy artifact | Not captured; the first runner stopped after its false-negative source check |

The independent bounded decoder output is `firmware-source-decoded.json`. It confirms the rejected token at structure offset 5568, path `/pcie@10000000`, property `#address-cells`, length 4, name offset 11, value offset 5580, and bytes `00000003`. The authorized guest's bounded preview of the loader copy is the same four bytes.

The runner's one bounded QAC retry did not receive a grant within 12 seconds and did not launch QEMU; QAC reported a separately managed session. No more QEMU request was queued. The failed initial capture metadata and successful independent source audit are both preserved rather than replacing the run record.

## Production repair

`kernel/arch/arm64/phase2_platform.cpp` now treats the encoded `#address-cells` and `#size-cells` values as bus-defined `u32` counts. The parser retains those counts in bounded node ancestry and no longer rejects an unrelated bus solely because its count exceeds two. Resource parsing remains restricted to the widths that guideXOS currently decodes: `reg`, `ranges`, and CPU-affinity paths retain their explicit count, shape, and overflow checks. A relevant resource requiring an unsupported width still fails its resource validation; property-format validation was not disabled globally.

FDT property parsing also validates the complete descriptor, bounded payload, and required 4-byte alignment span before resolving or interpreting a property. Raw previews are limited to 16 bytes and only copied after the payload range has been validated. Node paths and property names are bounded.

## Regression and build results

- Phase 2 production parser controls: **51 assertions passed** (the earlier suite had 50; this adds a PCI `<u32>` value of 3 control).
- Original pre-repair production-parser executable: rejected the captured source blob with malformed diagnostic 2 at STRUCTURE/FDT_PROP/cursor 5572. The authorized guest detail identifies the exact P3 predicate.
- Corrected production parser on the captured source DTB: **accepted**, stage COMPLETE, FDT_END token, cursor 7096. The same host suite plus the source blob passed **52 assertions**.
- The synthetic PCI host-node regression accepts `#address-cells = <3>` while retaining GICv2 discovery and physical timer IRQ 30. Existing malformed-input negatives still pass.
- Phase 4 allocator/VFS hosted controls: PASS.
- GICv3 hosted suite: **62 assertions passed**.
- ABI2 adapter suite: **104/104 checks passed**.
- ABI1 incompatibility control and SDK layout checks: PASS; the 400-byte AARCH64 activation linkage remains rejected against the pinned v0.5 consumer.
- Phase 10 build and hosted resolver/ELF/ABI controls: PASS.
- Phase 11 build: PASS with the optional Developer Studio client omitted.
- Phase 12 source-copy, ABI adapter, host, and ARM64 build checks: PASS. The pinned Developer Studio checkout was only read; the build used a copied tree under the new `out/` candidate.
- Freestanding Phase 2 and Phase 4 AArch64 kernel/UEFI builds: PASS.

The Phase 4 diagnostic correctly reported `allocator-reservation=deferred` at DTB parse time. No allocator reservation is claimed from the loader's memory-type handoff.

## Guest progress and remaining acceptance

The authorized pre-repair guest reached UEFI DTB discovery, header validation, loader-owned copy, EL1 kernel entry, EfiLoaderData coverage validation, and the exact property rejection. It did not reach platform discovery, GICv2 initialization, timer initialization or delivery, or scheduler progress. The corrected Phase 4 candidate has not run in QEMU because the bounded QAC request was not granted.

There is no repaired-guest evidence for GIC acknowledgement/return, timer ticks, scheduler progress, unexpected IRQs, or exceptions. ABI3/Developer Studio qualification remains at its prior `STUDIO_GUEST_PARTIAL` status with 0 of 3 qualifying Studio boots; this task did not launch Studio.

**Next acceptance requirement:** obtain a genuine exclusive QAC grant for one bounded run of `out/aarch64-bootdtb2-repaired-20261102`. Verify the guest passes DTB parsing and GICv2 initialization, then collect actual timer interrupt acknowledgement/return and scheduler progress. A loader-copy DTB capture remains useful for full source/copy equality, but the confirmed property root cause and host repair are already established.
