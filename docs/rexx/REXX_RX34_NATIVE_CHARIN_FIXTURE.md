# RX34 Native CHARIN Fixture

## Qualification-only provider

The kernel shell’s `rexx` embedding installs the RX34 provider only when the AMD64 build sets `RX34_STREAM_FIXTURE=1`. The default is `0`; the fixture context, immutable byte arrays, and registration code are guarded by `GXOS_REXX_RX34_FIXTURE`. The ordinary production build has no fixture provider. It continues to fail closed for CHARIN when no provider is installed, and STREAM reports `UNAVAILABLE`.

The fixture context belongs to one synchronous shell invocation and remains alive until that invocation returns. The provider resolves only fixed names, maintains per-invocation cursors, returns bytes from immutable arrays, and supports one-based seeking on designated streams. It has:
- default name `default`, bytes `D0`;
- `rx34sample`, bytes `ABCDEF`;
- `rx34binary`, bytes `41 00 80 FF 42`;
- nonseekable `noseek`, bytes `NS`;
- `fault`, bytes `FA` with one injected read error.

Unknown names fail closed. The provider opens no files and reads no arbitrary kernel memory. No disks, network paths, host paths, or mutable kernel-global stream table are involved. The embedding owns the provider context and its cursor state.

## Guest proof scripts

The positive script runs the real REXX interpreter and CHARIN builtin in the guest. It checks:
- default read: `D0`;
- named one-based three-byte read: `ABC`;
- cursor continuation: `DE`;
- zero-length reposition followed by continuation: `AB`;
- the RX33-emphasized `CHARIN('rx34sample',5)` form: one byte `E`, followed by cursor continuation `F`;
- a successful read of the final byte with `EXHAUSTED requested=1 returned=1 eof=1`;
- NUL/high-bit bytes: length 5, hex `410080FF42`;
- successful short read: `CDEF`, with `SHORT_READ requested=8 returned=4 eof=1`;
- read after exhaustion: `EOF requested=1 returned=0 eof=1`;
- separate provider error, unknown/missing names, nonseekable seek rejection, invalid argument/position behavior, recovery, and shell responsiveness.

The production script checks `STREAM() == UNAVAILABLE`, catches the fail-closed CHARIN runtime error, and verifies the state remains unavailable. This is a separate no-provider kernel build.

## Native build and guest evidence

Production and fixture kernels are built into separate RX34 output roots. The native build evidence records source configuration, hashes, object list, and protected-artifact checks. Guest acceptance is admitted and owned through QAC; the runner pauses at Console readiness before it submits scripts. Serial logs, command outputs, QAC status/list snapshots, QMP shutdown, reservation release, final QEMU census, and guest artifact hashes are stored under `docs/rexx/evidence/rx34/native-guest`.

The completed production and positive fixture guest results, including the
over-16-MiB VM resource-limit boundary, QAC reservation release, exact kernel
hashes, and screenshots, are recorded in
[`REXX_RX34_NATIVE_STREAM_FIXTURE.md`](REXX_RX34_NATIVE_STREAM_FIXTURE.md).
Regina is `NOT_RUN` because the executable is unavailable.
