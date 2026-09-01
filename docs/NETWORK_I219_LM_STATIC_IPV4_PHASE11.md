# guideXOS Server — AIDA_LPT I219-LM Networking Phase 11

Phase 11 adds a shared static/DHCP IPv4 configuration path and bounded
transmit diagnostics for the existing I219/PCH driver. The implementation
does not claim physical Ethernet success; the AIDA_LPT result is pending the
operator test below.

## Configuration paths

The IPv4 layer owns the single active configuration model. Each state carries a
mode: `unconfigured`, `static`, `dhcp`, or `qemu-default`. Static and DHCP
lease application both validate the address, contiguous mask, gateway, and
DNS values before replacing routes and the ARP cache. Automatic mode selects
DHCP without claiming an address or lease.

The framebuffer TCP/IPv4 Properties dialog now uses fixed-storage text fields
with click hit-testing, focus, caret placement, numeric input, Backspace,
Delete, arrows, Home/End, and Tab traversal. Invalid values remain in the
dialog and do not change the shared state. DHCP mode disables the manual
fields.

The bounded shell command is:

```text
netconfig show
netconfig static <ipv4> <mask-or-prefix> <gateway> <dns>
netconfig dhcp
```

Examples:

```text
netconfig static 192.168.0.50 24 192.168.0.1 192.168.0.1
netconfig static 192.168.0.50 255.255.255.0 192.168.0.1 192.168.0.1
netconfig show
netconfig dhcp
```

The operator must choose a known-unused address. No AIDA_LPT address is
hardcoded into product behavior.

## QEMU versus physical state

The historical `10.0.2.15/24`, gateway `10.0.2.2`, DNS `10.0.2.3` values are
now installed only for the emulated discrete Intel E1000 (`8086:100E`) path
and are labelled `qemu-default`; they are not a DHCP lease. An I219-LM starts
with `IPv4 unconfigured` and no active address. DNS also starts empty until
configuration supplies a server.

## Bounded diagnostic semantics

`netdiag`/full `nicinfo` reports:

| Counter/state | Meaning |
|---|---|
| IPv4 upper attempts | Calls entering `ipv4::send_packet` |
| ARP requests generated | Valid ARP request frame constructed |
| NIC TX attempted | Valid frame entering the generic NIC TX path |
| NIC frame submitted | Descriptor accepted and TDT advanced |
| I219 descriptors submitted/completed | Ring submission and descriptor DD observation |
| I219 TDH/TDT/TCTL | Last bounded register snapshot |
| TX timeout/errors | Submission/completion failures |
| ARP replies | Valid ARP replies received and cached |
| DHCP discover generated/submitted/send-fail | Generated attempt, successful UDP submission, and send failure |
| DHCP timeout | A successful DISCOVER/REQUEST submission with no response |

This explains the earlier zero DHCP DISCOVER/timeout display: the old
`discoversSent` counter was incremented only after `dhcp_send` succeeded, so a
failure before UDP/NIC submission left both it and the later receive-timeout
counter at zero. Phase 11 counts the generated attempt and its send failure
separately.

## AIDA_LPT physical test

1. Connect Ethernet and boot the Phase 11 ISO.
2. Run `netdiag`; record the I219 identity, MAC, link, and baseline counters.
3. Select a known-unused address on the real `192.168.0.0/24` LAN. Do not
   choose an address automatically. For example, use an operator-selected
   unused `192.168.0.x` address with mask `255.255.255.0`, gateway
   `192.168.0.1`, and DNS `192.168.0.1`.
4. Configure it with either the TCP/IPv4 Properties dialog or:
   `netconfig static <chosen-ip> 24 192.168.0.1 192.168.0.1`.
5. Run `netconfig show` and `netdiag` to verify the active state.
6. Run `ping 192.168.0.1`.
7. Run `netdiag` again and record changes in IPv4 attempts, ARP generated and
   replies, NIC frame submission, I219 descriptor submission/completion,
   TDH/TDT/TCTL, TX errors/timeouts, and RX counters.

Classify only from those observations:

- **A:** ARP gateway resolution and/or ping succeeds.
- **B:** I219 TX submits/completes, but no valid gateway response is observed.
- **C:** TX is submitted but I219 completion fails.
- **D:** IPv4/ARP attempts occur but no I219 submission is reached.
- **E:** Static configuration cannot be established through GUI, CLI, or the
  shared backend.

QEMU boot and host tests are regression evidence only and must not be used to
assign A–E for the physical I219 link.
