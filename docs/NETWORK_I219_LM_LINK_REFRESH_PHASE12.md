# guideXOS Server Navigator — I219-LM Link Refresh Phase 12

Status: implementation complete; physical AIDA_LPT validation pending.

## Scope and starting evidence

Phase 12 continues the AIDA_LPT Intel I219-LM/PCH bring-up without changing
the already-proven PCI discovery, MMIO, reset, MDIC access, MAC acquisition,
RX/TX rings, registration, IRQ registration, or Driver Ready paths.

The starting repository state was branch `NAVIGATOR_GENERAL_IMPROVEMENTS`,
HEAD `596c3e390f15f7919f08355605cffc631092e6d8` (`Finalize Phase 11 artifact
metadata`), clean, at the upstream tip with ahead/behind `0/0`.

The exact Phase 11 physical evidence incorporated here is:

- AIDA_LPT has Intel I219-LM/PCH PCI `8086:156F`, subsystem `103C:8079`,
  revision `21`, previously observed MAC `EC-8E-B5-9F-36-38`.
- Phase 10 reported MDIC-valid PHY access, READY RX/TX rings, registration,
  active state, registered IRQ, hardware PASS, Driver Ready YES, and Link UP
  when Ethernet was connected before boot. A separate cable-after-boot test
  remained `Link: DOWN (cached)`.
- Phase 11 repeatedly reported Driver Ready YES but `Link: DOWN (cached)` on
  the same cable/path. `dhcp /discover` failed at the link gate with
  `built=0`, `attempts=0`, `submitted=0`, and `complete=0`.
- Immediately before the Phase 11 retest, that exact AIDA_LPT and cable were
  booted into Windows; Windows showed the Intel Ethernet interface connected
  and active, Wi-Fi was disabled, and the machine rebooted into guideXOS
  without unplugging or changing the cable. This is the external known-good
  carrier control used by this phase; it is not treated as a guideXOS link
  measurement.
- Phase 11's truthful physical configuration boundary is preserved: an I219
  with no lease/static configuration remains IPv4 not configured, with no
  mask, gateway, DNS, or QEMU `10.0.2.x` fixture values. QEMU defaults remain
  scoped to the discrete emulated E1000.

## Link-state architecture audit

Before Phase 12, the flow was:

1. I219 reset completed.
2. `CTRL.SLU | CTRL.ASDE` was prepared.
3. MDIC read PHY address 2, ID registers 2 and 3.
4. MDIC read vendor PHY Status register 26 once.
5. Register 26 bit 6 was copied into `NICDevice::link`.
6. MTA and RX/TX rings were initialized, the device was registered, and
   `get_link_state()` returned that cached value.
7. `nicinfo brief` and DHCP consumed the cache. There was no normal polling
   refresh. An I219 link-change IRQ only sampled MAC `STATUS`, without an
   MDIC transaction.

Phase 12 keeps the cheap cache, but adds a bounded read-only refresh. The
normal Stage 8 path performs one post-DMA refresh before readiness is
published. A DHCP invocation whose cached value is not UP performs exactly
one shorter preflight refresh before rejecting. `nicinfo brief` remains
cache-only; `nicinfo link` explicitly performs one bounded refresh.

For I219 the refresh reads PHY address 2, BMSR register 1 twice, then vendor
PHY Status register 26, and records MAC `STATUS` for comparison diagnostics.
The second BMSR result is the current-link result after the first RO/LL read.
If both PHY sources are valid and disagree, the result is `UNKNOWN` with
`CONFLICT`; it is not guessed DOWN. If no PHY source is valid, the result is
`ERROR`/`MDIC_ERROR`; it is not projected as cable-down. A valid register-26
result can be used as a PHY-only fallback when the BMSR transaction is not
available. For discrete E1000-family devices, the existing MAC `STATUS`
link-up bit remains the source and is read once during an explicit refresh.

The cache is updated only by a trusted UP or DOWN decision. A read error sets
`NIC_LINK_READ_ERROR`; a PHY disagreement sets `NIC_LINK_UNKNOWN`. The
interrupt path remains single-register and bounded; it records MAC evidence
but never starts a multi-transaction MDIC sequence from interrupt context.

## Register semantics and authoritative comparison

The Intel I219 datasheet identifies the standard PHY page-0 Status/BMSR at
register 1, including the link-status bit's RO/LL behavior, and identifies the
I219 PHY Status register at address 26 with its link bit at bit 6:

- [Intel Ethernet Connection I219 datasheet](https://cdrdv2-public.intel.com/612523/ethernet-connection-i219-datasheet.pdf)

The established upstream e1000e implementation reads `MII_BMSR` twice for
PHYs with sticky/latching link status and uses a finite polling interval. The
guideXOS implementation adopts only that status-read semantic and bounded
shape; it does not copy the full Linux driver or add unrelated PHY writes:

- [Linux e1000e PHY MDIC and generic link check](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/phy.c)
- [Linux e1000e PCH link handling](https://raw.githubusercontent.com/torvalds/linux/refs/heads/master/drivers/net/ethernet/intel/e1000e/ich8lan.c)

MAC `STATUS` bit 1 (`E1000_STATUS_LU`) is retained as a raw I219 diagnostic
and as the interrupt-time observation. It is not allowed to override a
valid, corroborated PHY decision during an explicit I219 refresh. For the
discrete E1000 path, MAC `STATUS` remains the existing authoritative source.

## Bounded timing

The I219 post-init settle uses at most 50 polls at a 10 ms interval, for a
nominal 500 ms upper settle bound, plus the existing finite MDIC transaction
polls. The DHCP/shell refresh uses at most 20 polls at the same interval, for
a nominal 200 ms link-refresh window. UP returns immediately when trusted.
DOWN is reported only after the bounded window, allowing post-reset
autonegotiation to become visible. MDIC failure returns as a read error; it
does not consume an uncontrolled retry loop.

## DHCP integration

The Phase 11 DHCP TX/provenance implementation remains intact after the link
gate. When the cached link is UP, discovery proceeds normally. When cached
link is DOWN, UNKNOWN, or READ ERROR, DHCP performs one bounded refresh. A
refreshed UP opens the existing packet-build/TX path. A confirmed refreshed
DOWN returns `DHCP_ERR_LINK_DOWN` with `interface link is down (confirmed by
bounded refresh)`. An MDIC error or PHY conflict returns
`DHCP_ERR_LINK_UNKNOWN` and directs the operator to the link diagnostic. In
the confirmed-down case the Phase 11 counters remain zero; after refreshed
UP, the existing built/attempted/submitted/completed counters describe the
actual TX path.

## Diagnostics

`nicinfo brief` remains 19 logical lines, within its declared 20-line bound.
Its link line now compactly shows cached state, last refresh state, source,
refresh result, poll count/limit, and timeout. It remains observational.

`nicinfo link` performs one refresh and shows:

- cached and last-refresh states;
- source and refresh result;
- poll count/limit and timeout;
- first and second raw BMSR values, validity, and read count;
- raw PHY Status value, validity, and interpreted link;
- raw MAC `STATUS` value, validity, and interpreted link;
- final MDIC ready/error/timeout flags and refresh failure reason.

`dhcp status` now distinguishes the link preflight attempt, result, and
resulting state from the Phase 11 packet/TX counters.

## Phase 10 → Phase 11 regression audit

The Phase 10-to-Phase 11 diff was inspected. The Phase 11 NIC changes added
TX provenance and related MMIO/barrier diagnostics; they did not change the
I219 link source, PHY register, or cache-refresh behavior. Phase 11's DHCP
link gate made the stale one-shot sample decisive by rejecting before packet
construction, which exposed the problem but was not itself a deterministic
link-source regression. The deepest proven problem was the earlier I219
architecture: one vendor-status read taken during initialization, cached
forever for ordinary callers, with no RO/LL BMSR double-read or bounded
post-init/pre-DHCP refresh. The Phase 10 UP observation is consistent with
that sample sometimes occurring after carrier negotiation; the Phase 11
repeated DOWN observation is consistent with a stale/early sample. Physical
refresh evidence is still required to establish the next hardware frontier.

No speculative PHY programming was added. No PHY reset, autonegotiation
write, SWFLAG/semaphore operation, NVM write, or undocumented Intel/PCH
register write was introduced by Phase 12.

## Verification

The focused Phase 12 test gate is:

```text
scripts/run-network-phase12-tests.ps1
```

It includes the Phase 11 network diagnostics, DHCP wire/state, and IPv4
configuration tests, plus `tests/network_link_state_test.cpp`, which covers
I219 source selection, BMSR second-read semantics, PHY/MAC interpretation,
UP, DOWN, UNKNOWN/conflict, MDIC/read-error separation, and bounded poll
exhaustion. The DHCP state test covers one-refresh cached-DOWN→UP, confirmed
DOWN with zero TX counters, and read-error separation.

Full AMD64 freestanding build command:

```text
build.ps1 -Arch amd64 -I219Phase5Stage 8 -I219Phase6Stage 0 -I219Phase7Stage 4
```

Result: PASS. The build produced the UEFI bootloader, kernel, ramdisk, and
ESP image. Existing compiler warnings remain outside this phase's scope.

QEMU validation is performed with the packaged ISO using the existing
release ISO smoke/boot verifier and multiple fresh E1000 boots. QEMU is
virtual discrete-E1000 evidence only and is not accepted as proof of I219
physical carrier. Final results and logs are recorded below after packaging.

## Phase 12 artifact

Preferred artifact:

```text
guideXOS-Server-v0.1.0-phase12-aida-i219-link-refresh-amd64.iso
```

Exact path, size, SHA-256, sidecar SHA-256, and manifest are recorded here
after the final packaging run:

- ISO path: pending packaging
- ISO filename: `guideXOS-Server-v0.1.0-phase12-aida-i219-link-refresh-amd64.iso`
- ISO size: pending packaging
- ISO SHA-256: pending packaging
- SHA-256 sidecar: pending packaging
- Manifest: pending packaging

## Physical AIDA_LPT acceptance procedure

Physical validation is pending. Use the known-good Ethernet cable connected
before power-on and leave it untouched. After boot:

1. Confirm desktop, keyboard, and mouse.
2. Run `nicinfo brief` and capture the complete output.
3. Run `nicinfo link` and capture the complete output.
4. If link is UP, immediately run `dhcp /discover`.
5. Run `dhcp status`.
6. Run `netdiag`.
7. Run `ipconfig /all`.
8. Photograph the decisive outputs, including raw BMSR1/BMSR2, PHY Status,
   MAC STATUS, source, refresh result, polls/timeout, and DHCP TX counters.

Interpretation:

- Refreshed UP and DHCP success: record source/evidence and built, attempt,
  submit, completion, OFFER/REQUEST/ACK; proceed to physical IPv4/ARP/ping/
  DNS validation.
- UP with DHCP TX but no OFFER: proceed to DHCP wire validity, physical RX
  filtering, and server-response investigation.
- Confirmed DOWN: use the raw PHY source and bounded poll evidence to explain
  why guideXOS still sees no carrier despite the Windows control.
- UNKNOWN/MDIC error: target the exact MDIC/status read failure; do not call
  it cable-down.
- UP only after settling: record the poll count/timeout evidence and decide
  whether the timing is expected I219 post-reset negotiation or an ordering
  defect.

