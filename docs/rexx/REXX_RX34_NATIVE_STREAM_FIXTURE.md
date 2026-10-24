# REXX RX34 — Native Stream Fixture Qualification

## Qualification-only provider

The AMD64 shell installs the fixture only when `RX34_STREAM_FIXTURE=1` defines
`GXOS_REXX_RX34_FIXTURE`. The default production build has the switch off and
installs no byte-stream provider. The fixture is stack-owned by one synchronous
shell invocation, uses bounded immutable buffers, has per-invocation cursors,
and resolves only five predefined names. It opens no files and interprets no
stream name as a path.

| Name | Bytes | Capabilities | Special behavior |
|---|---|---|---|
| Omitted/default | `44 30` (`D0`) | Read, seek | Default stream |
| `rx34sample` | `41 42 43 44 45 46` (`ABCDEF`) | Read, seek | Six-byte cursor test |
| `rx34binary` | `41 00 80 FF 42` | Read, seek | NUL and high-bit bytes |
| `noseek` | `4E 53` (`NS`) | Read | Explicit seek returns unsupported |
| `fault` | `46 41` (`FA`) | Read, seek | First read reports one provider error |

## Production no-provider guest

The non-fixture AMD64 kernel was built with
`rx34_stream_fixture_enabled=false`; kernel SHA-256 is
`67edd7fb3bbcf2fc237a4c397d8fb06aec11ee56a8b617a382ff43146d4034d2`.
Under QAC acceptance reservation `a9f2bc645e104f6ba8860e430868e55f`, the
actual guest interpreter showed:

- `STREAM()` returned `UNAVAILABLE` before input.
- `CHARIN()` failed closed through the guest syntax trap.
- A second `STREAM()` still returned `UNAVAILABLE`.
- A following REXX script ran, confirming shell recovery.

The guest was shut down through its owned QMP endpoint. QAC reported QEMU child
exit 0, acknowledged reservation release, and returned to zero managed
sessions. See
[`run-712ceb28f5fe4f0d97fcdb857ae11987`](evidence/rx34/native-guest/run-712ceb28f5fe4f0d97fcdb857ae11987/).

## Positive fixture guest

The fixture AMD64 kernel was built with the qualification switch enabled;
kernel SHA-256 is
`bea97f4a01cacbd8a50fc10e7f4638497b1cb3778e66713832cff760f1b4fe33`.
Under QAC acceptance reservation `d53a2b417cf0477993808bafdee9fa38`, the
actual guideXOS guest ran these scripts through the native shell bridge and
REXX interpreter:

- Default read: `44` (`D`).
- Named reads: `414243` (`ABC`) then cursor continuation `4445` (`DE`).
- `CHARIN('rx34sample',5)` returned `45` (`E`), and the next omitted-start,
  one-byte call returned `46` (`F`). The detail query recorded a full one-byte
  read; the final-byte read reported `EXHAUSTED requested=1 returned=1 eof=1`.
- Explicit zero-length reposition followed by a read returned `4142` (`AB`).
- Binary result length was 5 and hex was `410080FF42`.
- A request for eight bytes at position three returned `43444546` (`CDEF`) and
  recorded `SHORT_READ requested=8 returned=4 eof=1`.
- Reading after that short read reported `EOF requested=1 returned=0 eof=1`.
- Injected provider error reported `ERROR requested=1 returned=0 eof=0`.
- Unknown and missing names reported `UNKNOWN`; seeking `noseek` reported
  `UNSUPPORTED`.
- Invalid numeric position and argument preserved the preceding `READY` detail.
- A 16 MiB + 1 request printed its marker and then raised
  `VM resource limit exceeded`. This VM resource-limit result aborts that REXX
  invocation before a syntax trap can observe later state. Hosted assertions
  separately verify that oversize rejection occurs before provider callbacks
  and preserves the stored state.
- A later REXX command ran and printed `RX34_POST_STREAM_RESPONSIVE`.

The guest returned to the shell after the oversized-request error. QMP shutdown
was acknowledged, QAC reported QEMU child exit 0 and released the reservation,
and the final census found no QEMU process. See
[`run-ffc708d20b0a419d8f4c9fcf8c673f51`](evidence/rx34/native-guest/run-ffc708d20b0a419d8f4c9fcf8c673f51/).
The three per-command screenshots there show the guest output; the serial log
records boot/heartbeat activity and access to every staged script. The network
was disabled with `-net none`, and disposable copied media was used.

The console screenshot from an earlier production attempt showed that the
runner had not opened the no-provider script; that attempt was not counted as
qualification. The redundant terminal-open action was removed from RX34's
script commands. The production retry and final fixture run both passed the
serial, heartbeat, QMP, QAC-release, and preservation checks.

## Scope limits

The fixture proves the synchronous embedded-provider path only. It does not
qualify production input, filesystem streams, disk access, asynchronous
NOTREADY delivery, `SIGNAL ON NOTREADY`, or the IBM STREAM Command/C interface.
The reference semantics and bounded state transitions are documented in
[`REXX_RX34_STREAM_STATE_CONTRACT.md`](REXX_RX34_STREAM_STATE_CONTRACT.md) and
[`REXX_RX34_CHARIN_COMPATIBILITY.md`](REXX_RX34_CHARIN_COMPATIBILITY.md).
