# HP Laptop Test — Intel I219-LM Phase 3

> Historical, unverified test plan. Phase 3 freeze isolation does not assume
> or enable I219-LM support until an actual AIDA_LPT diagnostic capture proves
> the PCI identity.

Target: wired Intel I219-LM `8086:156F`, subsystem `103C:8079`, revision `21`.

Boot the Phase-3 UEFI artifact with an Ethernet cable attached. At the guideXOS shell, run:

```text
lspci
netdiag
ipconfig /all
```

Only if `ipconfig /all` shows a DHCP gateway, run:

```text
ping <gateway shown by ipconfig /all>
```

Capture or transcribe the complete outputs of all commands. The most important lines are:

```text
8086:156F ... Class=02/00 ...
Driver: intel-i219-lm (PCH)
Driver Bound: YES
Init Stage: ...
BAR/MMIO Physical Address: ... Size: ...
PCI Command: ... CTRL: ... STATUS: ...
MMIO Mapped: YES
MAC Address: XX:XX:XX:XX:XX:XX
PHY access: ok|failed
Link State: UP|DOWN|UNKNOWN
RX ring: initialized|not initialized
TX ring: initialized|not initialized
NIC registration: YES|NO
Last initialization failure: ...
DHCP State: ...
IPv4: ...
```

Interpretation:

1. PCI binding: exact `8086:156F` and driver bound.
2. MMIO/init: valid BAR, mapped MMIO, PCI command enabled, no reset/init failure.
3. MAC/PHY: real unicast MAC and trustworthy PHY/link result.
4. NIC registration: adapter appears in `ipconfig /all`.
5. Ethernet: TX-completed and RX-accepted counters move after `ping`.
6. DHCP/IPv4: DHCP lease, address, mask, gateway, DNS, and successful gateway ping.

If initialization fails, report the exact `Last initialization failure` string and all register/PHY lines. Do not infer success from cable insertion alone. The wireless `8086:24F3` device is not part of this test.
