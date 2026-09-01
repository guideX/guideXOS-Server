# guideXOS Server Navigator — Intel I219-LM Bring-up Phase 10

Status: implementation and automated validation complete; physical AIDA_LPT
validation of the Phase 10 image is pending.

## 1. Phase 9 physical boundary carried forward

The Phase 9 image was booted on physical AIDA_LPT with Ethernet connected
before startup. guideXOS reached the desktop and remained usable. The
decisive `nicinfo brief` capture was:

```text
PHY id=0x0000:0x0000 addr=0x00
PHY status: 0x0000 mode=unknown

RX ring: NOT READY
TX ring: NOT READY

Registered: NO
active=NO
IRQ=not registered
mask=masked

Init: stage=PHY
hardware=INCOMPLETE

Failure:
PHY - I219 PHY MDIC read timed out or returned an invalid response

Driver Ready: NO

Link: UNKNOWN (cached)

DHCP: state=INIT
lease=none
```

This is the deepest physically proven boundary. RX/TX rings, registration,
link negotiation, DHCP, ARP, and IPv4 configuration remain downstream and
were not treated as primary Phase 10 problems.

## 2. Address-selection finding

Before Phase 10, the I219 path selected `I219_PHY_ADDRESS = 1` and passed that
value to all three standard-page reads: PHY ID1 register 2, PHY ID2 register
3, and PHY status register 26. The field was only copied into `NICDevice`
after all reads succeeded. Therefore the physical `addr=0x00` was not the
address issued on the MDIC command; it was the zero-initialized diagnostic
field left behind by the first failed read.

The Intel I219 datasheet places the standard IEEE PHY registers, including
PHY ID registers 2/3 and PHY status register 26, at MDI PHY address `0x02`.
The PCH general/high-page register view uses address `0x01`. The upstream
e1000e implementation also initializes a PCH PHY context with address 1 but
selects address 2 for the standard page-0 view and address 1 for the
high-page view. The existing code was therefore using a valid PCH address
for the wrong register view.

References:

- [Intel Ethernet Connection I219 datasheet](https://cdrdv2-public.intel.com/612523/ethernet-connection-i219-datasheet.pdf)
- [Linux e1000e PHY MDIC access](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/phy.c)
- [Linux e1000e ICH8/PCH initialization](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/ich8lan.c)
- [QEMU e1000 MDIC model](https://raw.githubusercontent.com/qemu/qemu/master/hw/net/e1000.c)

## 3. MDIC audit

The existing command bit positions were correct for the e1000 MDIC layout:

- data: bits 15:0;
- PHY register: bits 20:16;
- PHY address: bits 25:21;
- read opcode: bits 27:26 = `2`;
- READY: bit 28;
- ERROR: bit 30.

The implementation did not previously mask the two five-bit address fields,
record the initial command state, apply an inter-read delay, validate the
returned register fields, or expose poll count and READY/ERROR/timeout state.
Phase 10 now uses masked command encoding, captures initial/command/final
MDIC values, waits 50 microseconds before each bounded observation, checks
READY then ERROR and returned register/PHY fields, extracts the low 16-bit
data field, and rejects an all-ones data response. A zero data value remains
valid for an individual non-ID register; the PHY ID pair additionally rejects
zero or all-ones identifier values.

The poll is limited to 1,920 observations. With the existing bounded x86
port-80 delay approximation this is approximately the same order as the
upstream finite MDIC wait and cannot become an infinite boot loop. The
initial value is observed for diagnostics only; writing a new command
replaces the prior completion state, so no speculative clear or PHY write is
needed.

MMIO remains volatile and now has the existing compiler barrier on both sides
of writes and after reads. Interrupt causes remain masked and drained.

## 4. Implementation changes

- Added `DeviceFamily::E1000` and `DeviceFamily::I219Pch` helpers. The QEMU
  discrete E1000 path remains separate from I219/PCH behavior.
- Changed the I219 standard PHY address to fixed-family address `0x02` and
  retained address `0x01` as the documented general/high-page address.
- Added `PhyAddressSource`; the Phase 10 standard access is recorded as
  `fixed-family` before its first transaction.
- Kept the Phase 8 `CTRL.SLU | ASDE` preparation before MDIC access. It is a
  MAC link-control preparation and does not program a PHY register.
- Preserved fail-closed state projection. A PHY failure leaves rings
  uninitialized, registration/active/readiness false, and interrupts masked.
- Added compact MDIC transaction fields to `NICDevice` and the `nicinfo brief`
  MDIC line. The brief now has 19 logical lines, still below its 20-line
  bound; no hardware operation is performed by the command.
- No bounded address probe was implemented because the I219 standard-page
  address is established by the datasheet and upstream family behavior.
- No speculative PHY reset, autonegotiation, power-management, SWFLAG,
  semaphore, NVM, or undocumented PCH programming was introduced.

The Phase 10 source-level root cause is the incorrect use of address 1 for
standard page-0 PHY IDs/status. The previous tight polling loop and missing
transaction evidence were secondary correctness/diagnostic deficiencies.
Physical hardware must still confirm whether correcting the address crosses
the MDIC boundary on AIDA_LPT.

## 5. Host tests

`tests/network_diagnostics_test.cpp` covers:

- E1000 versus I219/PCH family identification and vendor rejection;
- fixed I219 standard/general address semantics and source naming;
- MDIC read opcode, field masking, and field extraction;
- READY and ERROR interpretation;
- response-field validation and bounded poll constants;
- zero versus all-ones data handling;
- PHY identifier validation for zero/all-ones values;
- PHY failure-stage projection and strict driver-ready gating;
- the 19-line brief contract and 20-line maximum.

Focused command and result:

```text
g++ -std=c++14 -Wall -Wextra -I kernel/core/include -I kernel/arch/amd64/include tests/network_diagnostics_test.cpp kernel/core/ethernet.cpp -o tmp/phase10-network-diagnostics-test.exe
tmp/phase10-network-diagnostics-test.exe
Result: PASS
```

The build-wrapper self-test also passed. `git diff --check` passed.

## 6. AMD64 build, ISO, and QEMU validation

The canonical build used `I219Phase5Stage=8`, `I219Phase6Stage=0`, and
`I219Phase7Stage=4` and passed as a full AMD64 freestanding build. The release
packager structurally verified the final image as bootable UEFI, platform
`0xEF`, no-emulation media.

Two fresh QEMU boots used the established emulated E1000 setup:

```text
-netdev user,id=net0 -device e1000,netdev=net0 -no-reboot
```

Both observed firmware entry, bootloader, kernel load, 64 MiB ramdisk load,
desktop readiness, and kernel main-loop readiness. Serial evidence:

- `out/release-iso/qemu-test-e2bf5db076bf45ee925ce21128296d3e/serial.log`
- `out/release-iso/qemu-test-34c90f41e50048e3a08db69eca980982/serial.log`

These are QEMU regression results only. They do not claim physical I219
success; the QEMU model accepts the discrete E1000 PHY access path.

## 7. Phase 10 artifact

- Filename: `guideXOS-Server-v0.1.0-phase10-aida-i219-mdic-amd64.iso`
- Path: `dist/guideXOS-Server-v0.1.0-phase10-aida-i219-mdic-amd64.iso`
- Size: 90,245,120 bytes
- SHA-256: `0256c48cb64756a31989cc47566afd29408dcefcd171634e5e52af97560a7f45`
- Checksum sidecar: `dist/guideXOS-Server-v0.1.0-phase10-aida-i219-mdic-amd64.iso.sha256`
- Manifest: `dist/guideXOS-Server-v0.1.0-phase10-aida-i219-mdic-amd64.manifest.json`
- Structural result: bootable UEFI, platform `0xEF`, no-emulation media.

## 8. Physical AIDA_LPT acceptance procedure

Physical validation is pending. Use the exact Phase 10 ISO and keep the Phase
9 artifact available for comparison:

1. Connect the Ethernet cable before powering on AIDA_LPT.
2. Boot the Phase 10 ISO and confirm the desktop appears.
3. Confirm mouse and keyboard operation.
4. Open Console.
5. Run only `nicinfo brief`.
6. Photograph the complete output before running any verbose command.
7. If desired, run `ipconfig /all` and `netdiag` only after the brief capture.

The expected decisive fields are `PHY ... addr=0x02 source=fixed-family` and
the compact `MDIC` transaction line showing register, initial value, command,
final value, READY, ERROR, timeout, polls, data, and data validity.

## 9. Phase 11 decision rule

- If PHY access succeeds and `Driver Ready: YES`, target physical link,
  DHCP, TX/RX, ARP, and IPv4 validation.
- If PHY access succeeds but a later initialization stage fails, target only
  that newly recorded stage; keep readiness false until the existing full
  gate passes.
- If MDIC still times out or errors, use the brief's recorded address/source,
  command, final value, READY/ERROR/timeout, and poll count to isolate the
  remaining I219/PCH prerequisite. Do not begin DHCP or ring debugging and
  do not add speculative PHY programming without new evidence.
