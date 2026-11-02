# AARCH64-BOOTDTB1 Native DTB Handoff Diagnosis

**Classification: ARM64_DTB_HANDOFF_UNRESOLVED (Outcome D).**

The authorized minimal guest reproduced the original DTB diagnostic 2 after accepting the FDT header and validating the loader-to-kernel handoff range. The new trace narrows the stop to a property value-format check in the structure block, but the log does not name the property or the failed predicate. No guest DTB blob was captured. There is therefore not enough evidence to label the firmware DTB malformed or to make a parser behavior change.

## Baseline and preserved evidence

At investigation start, branch AARCH64_SUPPORT was at 4648e91a4b711acf339fa3960c1c9ede76937020, equal to origin/AARCH64_SUPPORT; tracked worktree was clean. Generated files were kept under ignored out/ directories. No prior candidate or guest acceptance record was overwritten.

The original ABI3 Studio guest was built from server revision fe4e3ebf909f834f991ebd61d96607a6190dc80e. Its preserved candidate is out/abi3-studio-guest-final2-20261102; its kernel, EFI, and ramdisk hashes are respectively:

- Kernel: a993016d17ee4741d0d36570b5a3a0919e3ada368a9d292dd3cf2e33a54ac48a
- EFI: c6799231c67c594eb752183d611deaad67cd5ce9ef81c0c69e850e9cda7ba4ca
- Ramdisk: b2252465b2b4d72d64cbdb9b3e74e9ae485922ef30c0fe0fca5181ebf6ef0c8e

Its authorized QAC reservation was 51973721203c43b580806eefa6ccbcb7. The original serial log is out/abi3-studio-guest-final2-20261102/abi3-guest-20261102-084826-666/guest-final.log. It reports successful EL1 entry, stack validation, ExitBootServices, and firmware handoff, then DTB diagnostic=2, DTB: FAIL, and AARCH64_PHASE12_ERROR. It has no parser-stage trace and contains no DTB binary.

The separate ABI3 diagnostic Studio candidate remains preserved at out/abi3-studio-guest-diag-20261102; its queued QAC request was canceled before QEMU launch. It is not the minimal candidate tested below.

## Exact parser failure

GXOS_AARCH64_DTB_MALFORMED is enum value 2 in aarch64/phase2/phase2_platform.h. It is a broad parser status, not a one-to-one description of a header failure.

The minimal guest log reports:

    DTB header: status=0 magic=0xd00dfeed total=1052672 version=17
      struct=0x40+0x1bb8 strings=0x1bf8+0x1d5 reserve=0x30
    DTB diagnostic=2
    DTB parser trace: parsed=0 token=3 parser-stage=2 parser-error=8
      structure-offset=5572 platform=0 uart=0 gicd=0 gicc=0 ram-ranges=1 timer-irq=0

In aarch64/phase2/phase2_platform.h, parser stage 2 means STRUCTURE, error 8 means PROPERTY_VALUE_FORMAT, and token 3 means FDT_PROP. kernel/arch/arm64/phase2_platform.cpp records the current token and cursor after consuming the 4-byte token (around lines 454–456). The reported 5572 (0x15c4) is the structure-relative cursor at the property's length field; the FDT_PROP token itself starts at 5568 (0x15c0). It is not the property name or a blob-relative address.

The error is returned by several distinct checks in the property branch (around lines 560–604): malformed compatible string-list termination, invalid length/value for #address-cells, #size-cells, or #interrupt-cells, invalid length for #redistributor-regions or redistributor-stride, or a malformed status string. The trace does not preserve nameOffset, property name, value length, or the exact predicate. The property-name terminator check has its own PROPERTY_NAME error and was not the observed error. Thus the exact property and compatibility question remain unknown.

This is not evidence of invalid magic, truncated header, totalsize, block bounds, unsupported version, handoff arithmetic, or an invalid pointer: the shared header validator returned status 0, and the kernel's pre-parse range and memory-map checks passed. It also does not prove the FDT is valid in full.

## DTB provenance, addresses, and lifetime

The loader discovers the table through EFI_SYSTEM_TABLE.ConfigurationTable using the standard EFI_DTB_TABLE_GUID (b1b621d5-f19c-41a5-830b-d9152c69aae0), then reads VendorTable. It does not use a QEMU bare-metal DTB address or the Linux x0 = DTB convention. The loader enters the kernel through the project handoff structure in x0, stack top in x1, and zero in x2.

In the BOOTDTB1 guest, the loader logged configuration-table pointer 0x47ef2000, accepted the header above, allocated 0x101 4-KiB pages of EfiLoaderData, and copied 0x101000 bytes to physical 0x5865e000. The copied page allocation ends at 0x5875f000; the source and destination numeric ranges are disjoint. The loader log's probe-limit=0x01000000 is the maximum bound passed to the shared header validator, not a firmware-provided accessible-range length. UEFI supplies a table pointer but no separate byte-range length; in this run, reading the header and copying the declared totalsize succeeded. The source is used as the firmware-provided UEFI pointer before ExitBootServices with the firmware translation state active. The loader applies no Linux-style argument interpretation or explicit pointer translation. The handoff stores the allocated destination address and declared FDT totalsize.

Before ExitBootServices, the loader obtains the final UEFI memory map and requires the entire page-rounded copy allocation to be covered by EfiLoaderData. The diagnostic build made no firmware console calls after the final map acquisition. The kernel begins with MMU off and validates handoff address/length arithmetic and LoaderData map coverage before parsing. The guest proved range-valid=1 and EfiLoaderData-covered=1. The original diagnostic line also printed reserved-after-EBS=1, but that field was a hard-coded string; it did not mean the kernel allocator had already reserved the DTB. In source, the allocator adds the DTB reservation only after parsing succeeds. The failed guest did not reach allocator initialization, so actual kernel allocator exclusion was not observed. The diagnostic source is corrected to print allocator-reservation=deferred at this boundary; that post-run correction was not in the guest image. The parser ran with the identity physical address before the new MMU tables were enabled. If parsing succeeds, the kernel then checks every DTB allocation page against its normal-RAM translation descriptors. That post-MMU check was not reached by this guest.

The tested Phase 4 diagnostic candidate uses the packed v4 handoff: 160 bytes, dtb_base offset 128, dtb_size offset 136. Its observed CommonBootInfo is 448 bytes with DTB fields at offsets 112 and 120. The historical Phase 2 v2 handoff is 144 bytes with dtb_base at 128. ABI3's Phase 12 path selects the packed v6 handoff; the verified layout probe gives 212 bytes with the DTB fields still at 128 and 136, and Phase 6 CommonBootInfo is 464 bytes with offsets 112 and 120. The added ramdisk/framebuffer suffixes do not move the DTB fields. The BOOTDTB1 candidate's probe output and source are preserved under out/aarch64-bootdtb1-diag-final-20261102/.

The guest proves the v4 contract used by this minimal run. It does not by itself prove the original ABI3 v6 candidate's full pointer path, although the v6 prefix and DTB offsets are unchanged and the original guest also passed its handoff validation.

## Historical successful boots and comparison

Phase 2 documentation and preserved serial logs record three successful EDK2 UEFI boots using virt,gic-version=2,acpi=off, Cortex-A53, and 512 MiB. The firmware exposed EFI_DTB_TABLE_GUID; the loader copied the DTB into EfiLoaderData; the kernel parser reported DTB: OK, discovered QEMU virt RAM/PL011/GICv2 and the non-secure physical timer PPI 30, enabled the MMU, and observed timer IRQ 30 return before AARCH64_PHASE2_PASS. Phase 2 used the v2 handoff. Its historical record does not include a firmware binary hash.

Phase 7 records three fresh interactive-only UEFI/QEMU boots reaching virtio device discovery and common desktop input interactions. It does not claim AARCH64_PHASE7_PASS for the full stress workload, which stalled at the bounded QEMU 11 QMP producer path. The Phase 7 work uses GICv2 and DTB-described virtio-MMIO discovery. These logs show prior parser/platform progression but are not evidence about the property in the BOOTDTB1 blob.

The ABI3 and BOOTDTB1 profiles retain the documented virt,gic-version=2,acpi=off, Cortex-A53, 512-MiB UEFI profile. The BOOTDTB1 run recorded exact QEMU and EDK2 firmware hashes; the original ABI3 manifest did not record those hashes. ABI3 adds the v6 handoff, ramdisk/framebuffer data, and Studio package. GICv3 parser/support changes were introduced after early Phase 2 success, which is a regression window worth investigating, but no evidence ties those changes to this particular value-format rejection. The original ABI3 candidate and Phase 2/7 control artifacts were retained.

## Focused diagnostics and host controls

Changes add a shared bounded FDT header result, page-rounded allocation arithmetic, UEFI memory-map coverage checks, and compact loader/kernel serial fields. Parser failures now retain stage, error category, token, and structure-relative offset. The parser continues to reject malformed input; no failure was suppressed and no platform address was guessed.

scripts/build-aarch64-phase2.ps1 passed the production-backed Phase 2 host suite with 50 assertions. Its synthetic QEMU-virt GICv2 FDT exercises valid headers, invalid magic, truncated header, incorrect totalsize, overflowing and out-of-range blocks, overlapping blocks, unsupported version, property/structure errors including a known synthetic property-value-format failure, invalid handoff pointer/length, incomplete LoaderData coverage, and a valid loader-copy/kernel-parser transfer. The transferred synthetic DTB is accepted by the real production parser. This is not a substitute for the missing firmware DTB blob.

Other completed checks:

- GICv3 hosted regression: 62 assertions passed.
- ABI2 v0.5 adapter: 104/104 passed.
- ABI1 SDK alignment/unsafe-linkage negative control: passed; incompatible activation linkage is rejected.
- Phase 10, 11, and 12 hosted/build checks: passed on serial reruns. An initial parallel Phase 10/11 attempt collided in a shared temporary fixture; serial reruns passed.
- Phase 2 and Phase 4 freestanding AArch64 kernel and UEFI loader builds: passed.
- Phase 10–12 ARM64 kernel/loader/package builds: passed.
- Phase 4 and Phase 12 BootInfo layout probes passed their static assertions.

No source-matched guest DTB was preserved. A repository/output search found no captured .dtb in the preserved original or BOOTDTB1 guest records. The host FDT is synthetic and cannot identify the failed firmware property.

## Minimal candidate and authorized guest result

The no-Studio candidate is out/aarch64-bootdtb1-diag-final-20261102, built with scripts/build-aarch64-phase4.ps1 from baseline 4648e91a4b711acf339fa3960c1c9ede76937020 plus the diagnosis worktree files. The generated candidate-manifest.json records source fingerprints, tool versions, profile, and artifact hashes. The guest image inputs were the Phase 4 loader/kernel files built before later post-build compile-guard, runner-metadata, allocator-status wording, and host-test additions. After the guest failure, the corrected kernel was rebuilt without another QEMU run at out/aarch64-bootdtb1-final-20261102/kernel.elf; its SHA-256 is dd542a82117eed098012773d1ccd3a973ff8895e9523c2f1feb560ce4592468a. Its EFI and ramdisk hashes match the tested candidate. This final build verifies compilation of the truthful deferred-reservation diagnostic, not guest behavior.

| Artifact | SHA-256 |
| --- | --- |
| BOOTAA64.EFI | 6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d |
| kernel.elf | 5891290638129e86bda2f3b1b7ea269289b30c32c17f8f2ca564c3cbbd4840c0 |
| ramdisk.img | 74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9 |
| QEMU 11 executable | 8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748 |
| EDK2 code firmware | 47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d |
| EDK2 vars template | b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f |
| Guest serial log | 73cddc0f5e584a252de0b8204f8c43de68a79d3bb37d66cc3da987c27f84f132 |

QAC preflight found the coordinator online with no active managed sessions, reservations, or foreign QEMU process. The genuine QAC CLI granted exclusive acceptance reservation 62087e2d8f284475ba7cf80dd4441087 to project guideXOSServer-AARCH64-BOOTDTB1. The owned QEMU child (PID 33572) exited with code 0 after 8 seconds; QAC verified child exit and released the reservation. QEMU code 0 means orderly harness shutdown, not guest success. The runner source hash at launch was bea89f3303cbcd1057c79d65f7d3c57681c769c6753372ab6175b7dd50391fcf. Its post-run metadata correction is preserved with current hash fa0256d09e6b059f0f52571af6df864f3e9323a47166bfb7c45a23403694a4e9; it was not rerun. The serial log, session.json, and qac.log are under out/aarch64-bootdtb1-diag-final-20261102/bootdtb1-guest-20261102-105608-388/.

The real guest reached UEFI DTB discovery, validated header, DTB copy, memory-map acquisition, kernel entry at EL1, stack/EBS/handoff checks, and parser invocation. It stopped at the property-format rejection described above. It did not reach platform identification, GICv2 selection, timer initialization/delivery, scheduler progress, post-MMU DTB mapping validation, desktop, or Studio. No timer tick or scheduler evidence is claimed. No second QEMU request or Studio retry was made after this persistent failure.

## Production impact and next acceptance

No parser root cause was confirmed, so there is no production acceptance-rule fix in this change. The production changes improve safe range/lifetime checks and failure localization while preserving strict parser rejection, GICv2/GICv3 support, and the ABI2 adapter. One Phase 10/11 build guard was added so the Studio-v0.5-only entry diagnostic is compiled only in the Phase 12 profile; it changes no runtime behavior.

The next smallest useful guest observation is one separately authorized, bounded diagnostic boot that records the failing property's validated name offset/name, value length, and specific rejection predicate at the existing FDT_PROP failure site, and captures the exact bounded DTB bytes (or a safe hash plus controlled transfer artifact if capture is disallowed). Validate those bytes with the production parser on the host. If they pass there but fail in the guest, inspect memory/content divergence; if they fail identically, determine whether the firmware FDT violates the intended supported subset or the parser is too restrictive. Only then make a parser or handoff repair and rerun the minimal GICv2 guest. Timer delivery, scheduler progress, and any Studio qualification remain outstanding.
