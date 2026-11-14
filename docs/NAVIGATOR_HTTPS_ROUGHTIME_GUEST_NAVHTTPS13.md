# NAVHTTPS13 — Roughtime guest execution and bounded UDP acquisition

**Repository:** `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
**Branch:** `NAVIGATOR_GENERAL_IMPROVEMENTS`
**Starting HEAD:** `1d3d5bd68f4f66e5dbcd29ce10cee7b6e13421b4`
**Outcome:** **A — fresh controlled Roughtime UDP acquisition and verification demonstrated in the guideXOS AMD64 guest.**

## Implementation

NAVHTTPS13 leaves the NAVHTTPS12 verifier and its strict Ed25519 backend intact. It adds an RFC 10049 v1 request builder, source-bound response dispatch, an IPv4/UDP client over the existing socket stack, a shell self-test, and a numeric-endpoint acquisition command. The request follows [RFC 10049](https://www.rfc-editor.org/rfc/rfc10049.html): a 1,024-byte message body in one 1,036-byte UDP payload, with `VER=1`, `SRV=SHA-512(0xff || public_key)[0:32]`, a 32-byte nonce, `TYPE=0`, and zero `ZZZZ` padding.

The client obtains the nonce from `secure_random::fill` and stops on entropy failure. It sends a single request payload, allows at most two UDP send attempts only when neighbor resolution times out, and uses a two-second total deadline with a finite receive-poll cap. It rejects replies above 1,024 bytes and verifies the datagram's source IPv4 address and port before calling the unchanged verifier. A success result contains the signed time interval, uncertainty, server identity, transport measurement, and a monotonic expiry 300 seconds after receipt. Socket and ephemeral-port ownership are released on every return path.

The normal kernel has no default time-server key and does not run the qualification hook. The interactive diagnostics are explicit:

```text
roughtime-selftest
roughtime-fetch <numeric-ipv4> <port> <64-hex-character-public-key>
```

`roughtime-fetch` treats verification and the NAVHTTPS8 authenticated-evidence contract as evidence only. It does not call a clock-setting API, change the OS wall clock, or authorize public HTTPS. The test-only compile flag `GXOS_NAVHTTPS13_GUEST_QUALIFICATION_ACTIVE` invokes the same self-test and one controlled UDP fetch at boot in the isolated qualification image.

## Fixture identity and guest evidence

Test A uses the immutable independently signed Roughenough 2.2.0 response already pinned in `tests/vectors/roughtime_roughenough_2_2_0.json`. Its guest route covers the valid response, wrong pinned key, bad signature, malformed framing, and nonce mismatch.

Test B uses `tests/roughtime_v1_guest_fixture.py`, a one-shot host signer reached through QEMU user networking at guest address `10.0.2.2:5319`. The deterministic fixture key is test-only; its public key is `d04ab232742bb4ab3a1368bd4615e4e6d0224ab71a016baf8520a332c9778737` and its Roughtime key identity is `dbedad6ee009db026021697e3cf0edeeb02c286db9f9247827379354be1882ce`. No production server key was added.

QEMU 11.0.0 was found at `C:\Program Files\qemu\qemu-system-x86_64.exe` even though it was not on `PATH`. The global `Global\guideXOS_QAC_QEMU_EXCLUSIVE` mutex was acquired after checking that no QEMU process was active, rechecked before launch, and released after both processes stopped. The guest boot used the normal guideXOS UEFI loader, a separate temporary ESP containing the qualification kernel, 1 GiB RAM, E1000, QEMU user networking, and the existing virtio RNG arguments. The ordinary `ESP` directory was not changed.

The accepted run is `logs/navhttps13-guest-20261113-172338/`:

| Test | Guest result |
| --- | --- |
| Test A — immutable verifier vectors | **PASS**; all five cases emitted `result=PASS`; aggregate `production_policy_accepted=0` and `clock_policy=not-applied` |
| Test B — fresh controlled UDP | **PASS**; request bytes `1036`, response bytes `416`, source-bound verification succeeded, and the host fixture signed the guest's fresh nonce |
| Test C — NAVHTTPS7 trust enforcement | **NOT RUN**; optional separate qualification remains pending |

The fresh request nonce was `21e636fedea2364e96594eeb1804a353e2a05ba22deaa1476d8d4f164d5a0f57`. The guest logged midpoint `2026-11-14 01:23:59 UTC`, interval `01:23:57–01:24:02 UTC`, uncertainty `3 seconds`, and measured transport delay `580 ms`. That delay includes the first bounded ARP-resolution attempt. The guest serial output ended with `trusted_time=not-accepted clock_unchanged=yes`; the clock-assurance contract accepted the evidence shape, but no time-source agreement or clock promotion was performed.

The packet capture contains the QEMU user-net ARP exchange followed by exactly one 1,036-byte request from `10.0.2.15:49152` to `10.0.2.2:5319` and one 416-byte response in the reverse direction. The host fixture reported a signed response and exited. QEMU and the fixture were stopped, and QAC was released.

The accepted guest run used qualification ELF SHA-256 `93560cecd1f47ac6acb464f291815b099540c340d7a56433cc2ba521f20c3403`. After that run, the shell's unsigned 64-bit Unix-seconds formatter was corrected; the acquisition and verifier code did not change. The normal and diagnostic kernels were rebuilt and the Roughtime suite passed after that change. A repeat guest run was blocked by an already-active, separate S22R23 QAC cold-boot process (PID 5884); the qualification runner made no changes to that process. The latest diagnostic ELF has not been re-executed in QEMU.

The first experimental serial-keyboard injection did not invoke either command and is not counted as guest evidence. A later test-only boot hook made the qualification entry explicit and repeatable. Earlier exploratory logs remain in `logs/navhttps13-guest-*`; only the `172338` run above is accepted.

## Hosted and neighboring regressions

| Qualification | Result |
| --- | --- |
| `python -B -m unittest discover -s tests -p 'test_roughtime*.py' -v` | **PASS**, 46 tests, including byte-for-byte request construction, pinned independent vector, secure-random failure, source/key/signature/nonce/replay rejection, malformed and oversized packets, timeout, and cleanup cases |
| `scripts/test-clock-assurance.ps1` | **PASS** |
| `scripts/test-navigator-https-trust-profile.ps1` | **PASS** |
| `scripts/test-navigator-https-qualification-contract.ps1` | **PASS**, 7 cases |
| `scripts/verify-mbedtls-profile.ps1` | **PASS**, Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches |
| `scripts/smoke-dns-resolution.ps1` | **PASS**, 13 protocol scenarios and 6 controlled fixture checks |
| `scripts/smoke-udp-wire-bounds.ps1` | **PASS**, 6 UDP/socket bounds cases |
| `scripts/smoke-tcp-connect-cleanup.ps1` | **PASS**, IPv4 attribution and TCP cleanup cases |
| `scripts/smoke-secure-random-contract.ps1` | **PASS** |
| `scripts/smoke-navigator-hosted.ps1 -Build` | **PASS**, 288 checks, 0 failures; both CSS inline-style checks passed in this run |
| `scripts/smoke-navigator-public-https-hosted.ps1 -TargetUrl https://example.com/ -TimeoutSeconds 25` | **PASS**, HTTP 200, TLS 1.2, hostname validation passed |

The NAVHTTPS8 report records earlier intermittent failures of `CSS inline styles load` and `CSS inline runtime counters`. They remain visible in that report; this NAVHTTPS13 hosted run passed both checks.

## AMD64 build, memory, and stack

Both the normal kernel and the compile-flag qualification kernel built and linked with `-fstack-usage -fcallgraph-info=su`. The current normal ELF is 2,887,840 bytes and PE is 2,992,628 bytes. Relative to the existing NAVHTTPS12 ELF, `.text` grew 13,376 bytes and `.rdata` grew 4,144 bytes; `.data` and `.bss` are unchanged. The normal build reports `.text=2,370,665`, `.data=67,552`, and `.bss=575,818,368` bytes. The existing QEMU setup uses 1 GiB because the kernel's static BSS is already above 512 MiB.

The PE import directory is empty (`objdump -p` reports a zero import-directory RVA and size); no DLL or runtime import was added. The new acquisition, vector, and guest-test symbols are linked into the normal kernel. The qualification hook itself is absent unless its explicit build flag is selected.

Static stack accounting is based on the final `.su` data plus the compiler-emitted IPv4 helper clones in the normal kernel ELF:

| Frame or path component | Bytes |
| --- | ---: |
| `kernel_main` | 272 |
| desktop key handling + shell key dispatch + `execute_command` | 128 + 8 + 1,888 |
| `acquire_ipv4` including nonce, 1,036-byte request, 1,024-byte response, and verifier workspace | 3,664 |
| socket UDP send + UDP send | 112 + 96 |
| IPv4 `send_packet` + raw-send helper clone | 1,616 + 1,680 |
| ARP-timeout receive/poll path (`poll_network` clone + receive + conservative packet-handler allowance) | 1,696 + 56 + 120 |
| **Conservative interactive request-send path** | **about 11,336** |

The normal startup stack is 16,384 bytes, leaving approximately 5,048 bytes on that modeled interactive send path. The verifier chain remains 4,816 bytes from NAVHTTPS12; adding the 3,664-byte acquisition frame, 96-byte dispatch frame, and 2,296-byte shell/input/main frames gives about 10,872 bytes before small UDP receive/dispatch frames. Test A's guest path is about 7,328 bytes (`kernel_main` + guest self-test + dispatch + verifier chain). These are static call-chain estimates, not runtime high-water measurements.

The exception/interrupt path switches to its separate 16,384-byte `gxos_exception_stack` through IST1, so PIT/NIC interrupt frames do not accumulate on the Roughtime caller's startup stack. The acquisition workspace is automatic storage and is wiped before return. The receive queue remains the pre-existing static socket pool: 16 sockets × 8 queued datagrams × 1,472 payload bytes (188,416 payload bytes, plus metadata). NAVHTTPS13 adds no global packet workspace or heap allocation.

## Trust state and remaining work

- A verified response proves signature, delegation, nonce binding, and Merkle inclusion. Test B additionally binds it to the caller-pinned test identity and exposes a freshness expiry.
- A single server signature does not establish a multi-source agreement. `Authenticated` time remains unaccepted, and CMOS time remains `Plausible`.
- Public HTTPS remains disabled, and the production CA candidate remains unapproved.
- NAVHTTPS7 guest trust-policy qualification remains pending. I219-LM remains `last_completed=PHASE26`, `next_expected=PHASE27`; no I219 source or phase record changed.
- Test C was left independent of this short Roughtime reservation. Run it under a separate QAC admission when the NAVHTTPS7 guest scenario is selected.

## SHA-256 evidence

Source hashes are from the accepted working tree; the normal and diagnostic ELF/PE hashes identify the built artifacts used for this phase. Serial, packet, and fixture logs are the captured final guest run.

| File | SHA-256 |
| --- | --- |
| `kernel/core/main.cpp` | `bc071e5155138330e8ef0018cf3b39797e9ac216dd47e24fff1a919056998ab8` |
| `kernel/core/shell.cpp` | `f1de0daa774578b7763d001cb0fad591e6129eec52ac550d1a85371f51819754` |
| `kernel/core/gxos_roughtime_acquisition_core.cpp` | `febb12e8bad6f5c06c8707be79641dded7e633665661af1e54bb479eb75c6f5f` |
| `kernel/core/gxos_roughtime_udp_client.cpp` | `eb076197cb9b3280e3823837accc08c825e80504e95e05a86adde1e8b42be382` |
| `kernel/core/gxos_roughtime_guest_test.cpp` | `ff989bf2bd59a4a39ba259407248f2d3d8ec4942969d2764feb804f6212d0cc2` |
| `kernel/core/roughtime_guest_fixture_key.h` | `271b917232a346aa30a67464a16e689e3cd1bd050a9678fc71829be97aa00267` |
| `kernel/core/roughtime_guest_vectors.h` | `db6ee511e7aa33f98e4beb8dfcb242f051b4bcfe88dd7fdaf0cedac412490606` |
| `kernel/core/include/gxos_roughtime_acquisition.h` | `d191e19f2eb8ce7da14a9ca8bf5d1ace059881a3bd72be24e4104074730f449f` |
| `kernel/core/include/gxos_roughtime_guest_test.h` | `b4ff58fe973a58ebee0d1815746d53268a2c133613b33dd483ee2a450bee94d4` |
| `tools/roughtime_v1.py` | `05428a171b048537b43a4cf8843b796a061c8dbc66769aa502dd0a1a72815fce` |
| `tests/test_roughtime_v1.py` | `ac0db89d4508932b6ae4d1f82af8f1f998db6c72984afd68755099211b6f6498` |
| `tests/test_roughtime_portable.py` | `ac3585db19e6ef1c5f7627498f886760edfb6614c9df7c4b6e25db2392e408e3` |
| `tests/roughtime_v1_guest_fixture.py` | `7089d0aaa291086f3510f0adc6c56302979ca1154ae128b04081783ec7292101` |
| Current normal `kernel.elf` | `14a117fb2feb8ef8f7088201f75ac9f1007f8b587c72dd57261c47af80216d08` |
| Current normal `kernel.pe` | `5609eb7b21a7f5d43b8cbe679f96a173c62bf1456942120c0c2cbfa0fafe0eac` |
| Current qualification `kernel.elf` (rebuilt, not guest-executed) | `4c9bf62713e4a2323f568b0f6a5ec319ab000496b4b3bdaee4e46e7303d6c614` |
| Current qualification `kernel.pe` | `9fd8d796f973c90b661f4f64a743f814626ffed18ac92f13c8e7935f134c1fb0` |
| Qualification `kernel.elf` used by accepted guest run | `93560cecd1f47ac6acb464f291815b099540c340d7a56433cc2ba521f20c3403` |
| Qualification `kernel.pe` from accepted guest run's build | `c111d42ac21f1e47f70af8833ce5bc2ab495703426efa8b4aba070bb706ab492` |
| `guest-serial.log` | `de5d4f23e20bcea720462ddae730181c9359eff88281c9f217c02e265e9d6ed8` |
| `guest-network.pcap` | `8df2f64e13066194eb2d3f5bf07e7ac9d07c1367f3b0853599f73f77791fe109` |
| `fixture-server.log` | `2462604e308f834c8146ed5861cf024b1076f07a14f5c182a87ce9dd73cd816b` |
