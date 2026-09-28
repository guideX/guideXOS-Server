# Settings Network Phase S3 — Transaction Foundation and Bounded Exposure

## Outcome

**Outcome B.** The kernel now has a validated, rollback-capable local static
IPv4 transaction and a bounded `SET_NETWORK_CONFIGURATION` protocol request.
The transaction was exercised against the authoritative network provider in a
disposable QEMU guest. The production-path client sent a mutation request and
the guest rejected it as unauthorized, then returned an unchanged authoritative
snapshot. Mutation remains disabled across COM2 because its TCP peer is not
authenticated. Settings therefore remains read-only; this phase does not claim
that Settings changed the guest network.

## Starting audit and ownership

The S2R read path crosses `SettingsNetworkService` → `SystemServiceClient` →
COM2 transport → `SystemServiceBridge` → dispatcher → kernel provider. The
bridge and provider remain in those roles; this phase does not make the bridge
the network owner.

| State | Owner and S3 behavior |
|---|---|
| Active address, subnet, gateway, and IPv4 routes | `kernel/core/ipv4.cpp` owns `NetworkConfig` and the fixed route table. `ipv4::configure()` previously wrote the four config fields and `configured=true`, then attempted to append the default route, then the local route. It ignored both `add_route()` results. A full route table could therefore leave new address fields with missing routes; repeated calls could append duplicates. There was no validation or rollback. S3 builds replacement routes off-line and publishes config and routes together through a double buffer. |
| DHCP lifecycle and lease | `kernel/core/dhcp.cpp` owns the client state and one `LeaseInfo`. Boot calls synchronous `discover()`; renewal is also performed by the DHCP owner. DHCP previously published lease fields before calling `ipv4::configure()`. S3 applies IPv4 first and only publishes the new lease after it succeeds. DHCP has no nonblocking Settings start/cancel operation, so transaction-to-DHCP is explicitly unsupported. |
| DNS | `kernel/core/dns.cpp` owns the primary resolver address through `dns::set_server()` / `get_server()`. A DHCP ACK supplies the primary DNS address to that owner; static transactions can set and verify that same primary address. There is no secondary DNS representation or durable DNS configuration. |
| NIC and identity | `kernel/core/nic.cpp` owns probe state and the active NIC. The provider copies PCI location and MAC into a bounded stable identity. A candidate must match both snapshot generation and identity, and the live NIC is checked again before transaction capture. |
| Snapshot generation | `network_settings_provider.cpp` derives a monotonic generation from changes in the copied authoritative snapshot. An Apply based on a different generation is rejected before capture or mutation. |
| Persistence | No authoritative network persistence store was found. IPv4, DHCP, and DNS configuration are volatile across reboot. S3 adds no persistence format or reboot semantics. |

## Candidate validation and transaction

`NetworkConfigurationCandidate` is a fixed-size value object: expected
generation, interface ID, a 32-byte stable identity, mode, DNS mode, and four
numeric IPv4 values. It contains no raw pointers or unbounded strings. The
system-service wire payload is exactly 64 bytes, and the largest request is 80
bytes including its fixed header.

Validation runs before state capture. It checks generation and bounded
identity, supported mode and DNS mode, unicast IPv4 values, contiguous /1–/30
masks, usable host/network/broadcast combinations, and same-subnet gateway.
DHCP candidates must have automatic DNS and zero static fields. Static
candidates with automatic DNS are structurally valid but currently return
`Unsupported`, because no automatic DNS policy exists for a static address.
Malformed candidates, stale generations, missing interfaces, and mismatched
slot/MAC identity cannot enter the mutation callbacks.

The transaction runner captures IPv4 config plus the complete route table,
DHCP lease/lifecycle state, and DNS primary value. For supported static/manual
DNS it invalidates the local DHCP lease, replaces IPv4 fields and its managed
routes, sets the DNS owner, then verifies IPv4 fields/routes, DNS, and static
DHCP state. A failure triggers restoration of all three owners and a separate
verification pass. Results distinguish validation, stale/missing interface,
unsupported, capture failure, apply failure with/without verified rollback,
and verification failure with/without verified rollback. A rollback is never
reported as successful unless every captured owner matches again.

IPv4 replacement validates and constructs the next fixed route table before
publishing it. The active configuration and routes switch together in one
byte-sized bank selection, so an interrupt sees the previous complete state or
the next complete state. Other active routes are preserved. No production
callers currently add routes outside the configuration path; if that changes,
route ownership should become explicit before relying on the current exact
route verification count.

The deterministic transaction suite exercises validation-before-mutation,
successful static and DHCP candidate handling (DHCP is explicitly unsupported
by the kernel operation), stale/missing identity, apply/verification failures,
rollback success/failure, rollback verification, generation, repeated Apply,
and Cancel/no-op behavior. A QEMU-only fault hook fails IPv4 replacement after
the DHCP owner has been staged, proving restoration of the previous IPv4,
routes, DHCP state, and DNS value. The transaction runner does not claim
rollback when its verification callback fails.

## DHCP, DNS, and Settings behavior

The current DHCP owner performs bounded but synchronous discover/request
operations and exposes no safe start/cancel hook for a transaction. A DHCP
candidate therefore returns `Unsupported`; Settings does not send it and does
not display “Obtaining address...” as if a lease operation had started. The
owner's boot and renewal paths remain authoritative. Static/manual primary DNS
is supported through the existing DNS owner. DNS2 and automatic DNS behavior
for static mode remain unsupported.

`SettingsNetworkService` exposes a mutation call through the existing
`SystemServiceClient` abstraction and retains the trusted hosted Settings
process identity check. The UI has no editing controls in S3, because the
service bridge deliberately denies mutation. Existing Settings navigation,
Display, and the 53/53 Settings model baseline were preserved. If editing is
enabled after the trust boundary is fixed, Apply must use this client, read a
fresh snapshot, and render only that snapshot; Cancel must discard local edits.

## Protocol and security boundary

Protocol v1 keeps `GET_NETWORK_SNAPSHOT` unchanged and adds request type 2,
`SET_NETWORK_CONFIGURATION`, with a fixed candidate and a fixed result payload.
The bridge now frames requests by their declared bounded payload length,
rejects malformed/oversized messages, and continues to serve the legacy
header-only snapshot request. The Settings client validates candidates and
checks response type, request ID, and result encoding.

The mutation authorization decision is intentionally fail-closed. The bridge's
“trusted system-service peer” marker is sufficient for the existing read-only
request but does not authenticate the COM2 TCP peer. The dispatcher returns
`Unauthorized` for mutation even when that marker is present. The Settings
identity check protects calls within the hosted App Model, but cannot authenticate
the remote QEMU COM2 endpoint. No request-supplied caller string is trusted and
no ordinary-app mutation permission was added. Until an authenticated broker or
channel exists, enabling remote mutation would widen a privileged unauthenticated
surface.

## Verification

- Network configuration transaction tests: **21/21 passed**.
- Network Settings contract tests: **45/45 passed**.
- System-service bridge tests: **53/53 passed**.
- Settings Center model tests: **53/53 passed**.
- AMD64 kernel Make build: **passed**.
- QEMU local transaction proof: static address/mask/gateway/DNS applied and
  read back from the kernel provider; prior IPv4 values restored; injected
  post-DHCP-stage failure rolled back and verified; stale candidate rejected
  with snapshot unchanged. Generations advanced through the successful
  transitions.
- Production-path QEMU client: three authoritative snapshots succeeded. Its
  intervening `SET_NETWORK_CONFIGURATION` request received `Unauthorized`,
  and the before/after snapshots matched field-for-field.
- Live Settings GUI smoke: not run; Settings remains read-only in this outcome.
- Physical hardware: not tested.

The full hosted Visual Studio build still has the previously documented CRT
include shadowing, duplicate-basename output, and GCC-specific kernel source
issues. This phase did not expand into a broad build-system repair. QEMU uses a
disposable guest network and did not change the developer host network.

## Remaining work

Outcome A requires an authenticated system-service mutation channel, a
transaction-aware asynchronous DHCP lifecycle (or another safe owner API), and
Settings editing controls with snapshot-based Apply/Cancel behavior. Manual
secondary DNS and persistent network settings are also not supported. Until
then the kernel-local transaction is available for internal use and QEMU proof,
while mutation through the production service boundary remains unauthorized.
