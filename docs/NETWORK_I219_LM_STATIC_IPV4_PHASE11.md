# guideXOS Server — AIDA_LPT I219-LM Networking Phase 11

Status: implementation, host validation, AMD64 build, ISO packaging, and two
fresh QEMU boots complete. Physical validation of the Phase 11 image remains
pending.

## 1. Outcome and physical evidence

The Phase 10 physical capture is Outcome C at the pre-fix software boundary:
the manual DHCP operation did not reach NIC TX submission. It is not evidence
that a DHCP server declined a transmitted packet. The new Phase 11 image adds
the missing bootstrap path and provenance, but has not yet been run on
AIDA_LPT.

Phase 10 established the hardware prerequisites on AIDA_LPT with Ethernet
connected before boot:

- Intel I219-LM/PCH, PCI `8086:156F`, MAC `EC-8E-B5-9F-36-38`;
- MDIC PHY access valid;
- RX ring READY and TX ring READY;
- interface registered and active, IRQ registered, init stage ready;
- hardware PASS, failure none, Driver Ready YES, Link UP.

The same physical run recorded `RX Accepted: 27`, `RX Malformed: 0`, and
`RX Dropped: 0`. Those 27 frames are preserved as meaningful RX evidence, but
they are not classified as DHCP: the old path had no frame-level DHCP
provenance.

The fresh Phase 10 manual test was:

```text
DHCP State: INIT
discover=0 offer=0 request=0 ack=0 nak=0 timeout=0
DHCP Lease: none

dhcp /discover
Requesting IP address from DHCP server...
DHCP configuration failed.
Error: No DHCP server responded.

DHCP State: ERROR
discover=0 offer=0 request=0 ack=0 nak=0 timeout=0
DHCP Lease: none
RX Accepted: 27
RX Malformed: 0
RX Dropped: 0
ARP: requests=0 replies=0 malformed=0

ipconfig /all:
IPv4 Address: 10.0.2.15 (Preferred)
Subnet Mask: 255.255.255.0
Default Gateway: 10.0.2.2
DNS Servers: 10.0.2.3
DHCP State: ERROR
```

## 2. Root causes answered

### Did `/discover` submit a DISCOVER to the physical NIC?

No, not in the Phase 10 image. The exact pre-fix path was:

```text
shell cmd_dhcp(/discover)
  -> dhcp::discover()
  -> interface/MAC selection and xid generation
  -> do_discover()
  -> build_discover()
  -> dhcp_send()
  -> udp::send(68, 255.255.255.255, 67, ...)
  -> udp::send() rejects !ipv4::is_configured()
  -> no IPv4 header, Ethernet frame, nic::send_frame(), TDT write,
     descriptor ownership, completion, or offer wait
```

The old `dhcp_receive()` had the same bootstrap defect in the opposite
direction: it created a UDP socket, but `socket::udp_socket()` also rejected
an unconfigured IPv4 layer. Thus the old implementation could not perform a
complete DHCP exchange from an unconfigured physical interface.

`discover=0` meant no successfully sent/completed DISCOVER (`discoversSent`),
not that the command had proven a packet left the host. The old generic error
string incorrectly collapsed the pre-submit failure into “No DHCP server
responded.” Its receive-timeout counter stayed zero because the code never
entered the offer wait after the UDP send failed. The old attempt counter was
not exposed with a truthful stage label in the physical capture.

### Why were the `10.0.2.x` values projected?

They originated from the intentional QEMU fixture/default configuration in
`kernel/core/main.cpp`, not from a DHCP lease. Before Phase 11, the runtime
configuration model allowed those values to remain visible while DHCP was
`INIT` or `ERROR`. That made a test/QEMU source look like a real preferred
address on a physical path.

Phase 11 keeps the values only for the emulated discrete E1000 device
(`8086:100E`) and labels the mode `qemu-default`. The physical I219/PCH path
starts `unconfigured`; `dhcp::set_automatic_mode()` clears any stale active
address, gateway, DNS, routes, ARP cache, and lease while retaining the mode
`dhcp`. `ipconfig /all`, `netdiag`, and `dhcp status` now report `none` for
IPv4, mask, gateway, and DNS until DHCP applies a real lease or the operator
selects explicit static configuration.

No machine-name special case is used. QEMU fixture behavior is selected by
the established discrete E1000 device identity; I219 uses the normal
unconfigured physical state.

## 3. Complete Phase 11 transmit path

The corrected manual path is:

```text
dhcp /discover
  -> shell parser records command invocation
  -> dhcp::discover()
  -> find bound interface, verify driver-ready, verify cached Link UP
  -> select automatic/DHCP mode and clear stale configuration
  -> generate one bounded transaction ID
  -> build_discover()                  [DISCOVER payload]
  -> udp::build_datagram()             [68 -> 67, checksum]
  -> ipv4::build_packet_from_source()  [0.0.0.0 -> 255.255.255.255]
  -> ethernet::build_broadcast_frame() [FF:FF:FF:FF:FF:FF]
  -> nic::send_frame()
  -> check current TX descriptor owns DD
  -> copy to DMA-safe static TX buffer
  -> write legacy descriptor address/length/command/status
  -> memory barrier, write TDT, snapshot TDH/TDT/TCTL
  -> bounded poll for descriptor DD completion
  -> begin bounded offer wait and poll RX descriptors directly
  -> validate Ethernet/IPv4/UDP/DHCP, xid, BOOTP reply, and OFFER
  -> build/send broadcast REQUEST, wait for ACK/NAK
  -> apply validated lease as configuration mode DHCP
```

The initial broadcast path deliberately does not call ARP, route lookup,
`udp::send()`, `ipv4::send_packet()`, or the configured-only socket factory.
Ordinary configured IPv4 traffic retains those existing gates.

## 4. DHCP packet construction audit

The DISCOVER builder produces a minimum 300-byte BOOTP/DHCP payload in the
existing packed `Packet` format. It sets:

- `op=BOOTREQUEST`, Ethernet hardware type, hardware length 6, and `xid` in
  network byte order;
- broadcast flag `0x8000`, zero `ciaddr`, and the physical station MAC in
  `chaddr`;
- DHCP magic cookie `63 82 53 63`;
- message type DISCOVER, parameter-request-list, and END option.

The bootstrap UDP datagram uses source port 68, destination port 67, source
IPv4 `0.0.0.0`, destination IPv4 `255.255.255.255`, and a pseudo-header
checksum over those explicit addresses. The IPv4 header uses the same source
and destination and UDP protocol. The Ethernet frame uses the physical NIC
MAC as source and `FF:FF:FF:FF:FF:FF` as destination with EtherType IPv4.
This does not require a fabricated `10.0.2.15` address or ARP.

## 5. TX ring and completion audit

The existing E1000 legacy descriptor remains 16 bytes:

```text
buffer address: 64-bit DMA address
length:        16-bit frame length
cso/cmd:       command fields
status:        volatile NIC-owned status
css/special:   legacy fields
```

`init_tx()` still validates DMA layout, initializes all descriptors with DD,
programs TDBAL/TDBAH/TDLEN/TDH/TDT, enables TCTL, and publishes TX ring ready.
`send_frame()` still uses EOP|IFCS|RS, a static DMA buffer, barriers, TDT
advancement, and bounded completion polling for DD. Phase 11 does not rewrite
the proven I219 initialization, PHY, reset, NVM, or PCH path.

The added per-send evidence records descriptor index, frame length, DMA
address, command/status, tail before/after, descriptor submissions,
completions, hardware timeouts, driver errors, and the last TDH/TDT/TCTL
snapshot. DHCP derives its compact provenance from those deltas:

- descriptor accepted: submission count advanced;
- tail advanced: accepted submission changed the observed TDT;
- completion observed: completion count advanced after DD;
- completion timeout: hardware-timeout count advanced;
- driver error: NIC error or pre-submit failure was recorded.

The current bounded behavior leaves no infinite wait. A TX completion timeout
is reported as `TX completion`; a descriptor/full or DMA/pre-submit failure is
reported as `TX submit`; a successfully completed DISCOVER with no valid OFFER
is reported as `offer wait`.

## 6. Counter semantics

The DHCP counters now mean:

| Counter | Truthful event |
|---|---|
| `discoverBuilt` | A valid DISCOVER payload was constructed. |
| `discoverAttempts` | A submission attempt was made for that built DISCOVER. |
| `discoverSubmissions` | The NIC accepted the descriptor and TDT advanced. |
| `discoverCompletions` | NIC descriptor DD completion was observed. |
| `discoversSent` | The NIC send call returned successful completion. |
| `discoverSendFailures` | A DISCOVER NIC submission/completion failed. |
| `discoverTxTimeouts` | A DISCOVER descriptor was submitted but completion timed out. |
| `offersReceived` | A matching, valid DHCP OFFER was parsed. |
| `requestsSent` | A REQUEST was submitted and completed. |
| `acksReceived` | A matching, valid DHCP ACK was parsed. |
| `naksReceived` | A matching, valid DHCP NAK was parsed. |
| `timeouts` | A bounded OFFER or ACK receive wait expired. |

The compact `dhcp status` command and full `netdiag` use the same accounting
for manual discovery and automatic/renewal paths. `dhcp status` also prints
the last failure stage/reason, xid, packet/frame lengths, ports, MACs, TX
evidence, wait stages, and bounded RX protocol counters.

## 7. RX evidence

The physical `RX Accepted: 27`, malformed 0, dropped 0 evidence remains in the
generic NIC counters. The DHCP receive path adds bounded protocol counters:

- Ethernet broadcast frames seen while waiting;
- IPv4 frames;
- UDP frames;
- UDP datagrams addressed to DHCP client port 68;
- malformed DHCP payloads after the envelope passes validation.

These counters do not classify arbitrary accepted frames as DHCP. An accepted
frame is counted as DHCP only after Ethernet, IPv4, UDP, checksum, destination
port, payload length, and later BOOTP/options checks pass. No packet dump is
added.

## 8. Configuration and startup semantics

The shared IPv4 model has explicit modes: `unconfigured`, `static`, `dhcp`,
and `qemu-default`. `configure_static()` and `configure_dhcp()` validate the
address/mask/gateway/DNS tuple before installing routes and clearing stale
ARP state. Static configuration remains authoritative until the operator
explicitly requests DHCP. A DHCP request selects automatic mode and therefore
removes stale static/QEMU projection before attempting the exchange; a failed
request leaves `mode=dhcp`, `configured=false`, and no lease.

The current policy remains manual startup for this phase. Boot initializes the
client and logs that discovery is deferred; the operator runs `dhcp /discover`
or `ipconfig /renew`. Phase 11 does not silently add automatic startup on link
up. If a later phase chooses automatic startup, it should use the same
ready/link gates and accounting entry points.

The `10.0.2.x` QEMU fixture remains intentional for the discrete E1000 boot
path. Phase 11 QEMU boots therefore show `mode=qemu-default`, not a DHCP
lease. A QEMU run that executes `dhcp /discover` must be judged from
`discoversSent`, descriptor completion, `offersReceived`, lease validity, and
`mode=dhcp`; the familiar address alone is not evidence of DHCP.

## 9. Host tests

The focused command is:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-network-phase11-tests.ps1
```

It passes deterministic checks for:

- interface/driver-ready/link state helpers and I219 identity;
- 16-byte RX/TX descriptor ABI and bounded TX evidence states;
- DHCP command/accounting support;
- DISCOVER xid, BOOTP broadcast flag, MAC, cookie, message/options;
- UDP 68-to-67, explicit unconfigured source/destination, and checksum;
- IPv4 `0.0.0.0` to broadcast bootstrap construction;
- ordinary configured-only IPv4 send rejection while unconfigured;
- Ethernet broadcast destination and source MAC;
- static, DHCP, QEMU-default, and cleared configuration provenance.

Result: `Phase 11 network tests PASS.`

## 10. AMD64 build, ISO, and QEMU validation

The canonical full build passed with:

```text
build.ps1 -Arch amd64 -I219Phase5Stage 8 -I219Phase6Stage 0 -I219Phase7Stage 4
```

The release ISO was structurally verified as bootable UEFI, platform `0xEF`,
no-emulation media, with the expected 64 MiB ramdisk. Two fresh QEMU instances
used the established network configuration:

```text
-netdev user,id=net0 -device e1000,netdev=net0 -no-reboot
```

Both observed firmware entry, bootloader, kernel load, ramdisk load, desktop
readiness, E1000 initialization, and kernel main-loop readiness. Their serial
logs were:

- `out/release-iso/qemu-test-2077bc71a1ed408d8f0594b51c6adc3b/serial.log`
- `out/release-iso/qemu-test-608d536c0a95413fa82208e531b6fcc7/serial.log`

Both logs explicitly show:

```text
[IPv4] Configured mode=qemu-default IP=10.0.2.15 Mask=255.255.255.0 GW=10.0.2.2
[KERNEL] DHCP boot discovery deferred; use 'dhcp /discover' after startup
```

These QEMU runs did not execute the interactive manual command, so they do
not claim a real QEMU DHCP exchange. They prove that the deliberate fixture
still boots and remains separate from physical DHCP state.

## 11. Phase 11 artifact

- Path: `dist/guideXOS-Server-v0.1.0-phase11-aida-dhcp-tx-amd64.iso`
- Filename: `guideXOS-Server-v0.1.0-phase11-aida-dhcp-tx-amd64.iso`
- Size: 90,245,120 bytes
- SHA-256: `decf2d39d92d45e7798b14fd2b5b2e07376ac2e88fc438e57df0d28a1f1175bd`
- Checksum: `dist/guideXOS-Server-v0.1.0-phase11-aida-dhcp-tx-amd64.iso.sha256`
- Manifest: `dist/guideXOS-Server-v0.1.0-phase11-aida-dhcp-tx-amd64.manifest.json`
- Structural result: bootable UEFI, `0xEF`, no-emulation media.

The final artifact should be regenerated with `-SkipBuild` after the local
Phase 11 commit if a clean manifest source-commit record is required; the
artifact path, content, and checksum above are the validated Phase 11 build
from this implementation pass.

## 12. Physical AIDA_LPT procedure

Use the exact Phase 11 ISO with Ethernet connected before startup:

1. Boot AIDA_LPT.
2. Run `nicinfo brief`; confirm `Driver Ready YES` and `Link UP`.
3. Run `netdiag` and record baseline generic RX/TX, DHCP, ARP, and IPv4 fields.
4. Run `dhcp status` for the compact baseline.
5. Run `dhcp /discover`.
6. Immediately run `netdiag`, then `dhcp status`.
7. Run `ipconfig /all` and record `Configuration Provenance`.
8. Treat only `mode=dhcp`, `Lease: active`, a valid address, and the matching
   DHCP counters as a real lease. `qemu-default` is not a lease.
9. If a lease is real, optionally test the reported gateway with `ping`, then
   test DNS/public networking.

Capture these compact fields:

```text
DHCP State / Lease
DISCOVER built / xid / DHCP length / frame length
Ethernet source / broadcast destination
UDP 68 -> 67
TX attempted / descriptor / tail / completion / timeout / error
wait-for-offer begun / timeout
RX broadcast / IPv4 / UDP / DHCP / malformed
IPv4 mode / active versus not configured
```

## 13. Physical decision rule and next phase

- **Outcome A:** DISCOVER TX-completes, OFFER arrives, REQUEST/ACK completes,
  and DHCP mode applies a real lease. Proceed to Phase 12 IPv4/ARP/ping/DNS.
- **Outcome B:** DISCOVER TX-completes with broadcast provenance, but no OFFER
  is observed. Investigate wire behavior, filtering, or packet validity;
  do not blame the server until the frame evidence is captured.
- **Outcome C:** DISCOVER does not reach TX submission. Target only the
  software stage named by `dhcp status` (`interface`, `ready`, `link`, build,
  or TX submit). This is the pre-fix Phase 10 result.
- **Outcome D:** Descriptor is accepted/TDT advances but completion times out.
  Target the physical I219 descriptor ownership, DMA address, TDT/TDH, or
  completion path.
- **Outcome E:** DHCP completes but IPv4/lease provenance is wrong. Target
  lease-to-interface projection only.

No speculative PHY writes, resets, SWFLAG/NVM changes, autonegotiation work,
Wi-Fi, hot-plug polish, or unrelated Navigator work is part of Phase 11.
