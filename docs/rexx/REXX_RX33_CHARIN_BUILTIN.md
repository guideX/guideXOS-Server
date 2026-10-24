# REXX RX33 — CHARIN Built-in

## Selected profile

CHARIN follows the byte-oriented function contract documented for IBM z/OS 2.5, adapted to an explicitly installed embedded provider. The IBM documentation defines characters as bytes from X'00' through X'FF', says that an omitted name selects standard input, and defines start as a persistent-stream byte position. It also defines length zero as an empty result that can reposition the read pointer. See [IBM z/OS 2.5 CHARIN](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-charin) and [IBM z/OS 2.5 stream I/O functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=implementation-stream-io-functions).

RX33 maps “standard input” to the provider's configured default identity. The VM does not create that identity or access stdin itself. The provider's seekable capability is the embedded equivalent of a persistent stream for this call; no z/VM, TSO/E, CMS, or filesystem behavior is mixed into the profile.

## Signature and validation

The dispatcher registers CHARIN with zero through three arguments:

    CHARIN([name][,[start][,length]])

The default length is one byte. An omitted start uses the provider-owned current position. An explicit start is a positive whole decimal number from 1 through 4,294,967,295 and is passed as a one-based byte position. The second argument remains a position: CHARIN('sample',5) seeks to byte five and reads one byte. A zero start, negative value, fractional value, or nonnumeric value is an invalid function call. Values outside the supported 32-bit position range are a resource-limit error rather than being narrowed.

Length is a whole decimal number from zero through 16 MiB. Omitted length defaults to one. Zero is valid. Negative, fractional, and nonnumeric values are rejected; values greater than the VM's string limit return a resource-limit status. Extra arguments are rejected. Decimal values are converted exactly rather than rounded according to NUMERIC DIGITS.

An omitted name selects the provider default. A supplied name is converted to a length-bearing string view and passed byte-for-byte to the resolver. An explicit empty name is distinct from omission and resolves as a named identity; it is unknown unless the provider registered that identity.

## Read and position behavior

On a successful read, the provider advances its cursor once by the actual returned byte length. CHARIN returns exactly that span, preserving NUL, high-bit bytes, blanks, case, CR, and LF. It performs no line splitting, terminator insertion, or character-set conversion. Reads through nested internal routines and INTERPRET share the VM's provider context and cursor. Independent VMs remain isolated unless an embedding shares a host context intentionally.

An explicit start requires a seekable provider identity. If length is zero, CHARIN skips the reader and returns the empty string; if start was supplied, it still seeks to that one-based position. Without an explicit start, a zero-length call leaves the current cursor unchanged. An explicit start followed by a read failure leaves the cursor at the successfully selected start; the failed read itself must not advance it.

A positive short read returns all bytes actually supplied and advances by that count. The next call observes the provider's resulting state. EOF before any bytes is a distinct VM status and produces the generic stream-I/O error path, not a fabricated byte or a successful empty string. A provider can report a successful span that ends at EOF; CHARIN returns the bytes and the next read reports EOF.

## Errors and compatibility limits

Unknown names, no installed provider/default, unavailable data, EOF, provider errors, pending reads, unsupported seeks, and invalid provider metadata remain distinct VM statuses. They are surfaced through the interpreter's existing generic SYNTAX error handling, not a NOTREADY stream condition. Pending callbacks fail immediately; no wait or busy loop is permitted.

IBM z/OS 2.5 CHARIN returns available bytes for a short read and marks an error condition on the stream. RX33 returns the bytes but does not retain a stream condition, and the runtime has no STREAM function to query one. This is a compatibility gap, so inventory status is Partial. The native guest has no installed byte-stream provider, so native evidence covers argument rejection, fail-closed default access, and recovery, but not a positive native read.

The test provider qualifies the successful named/default path, binary bytes, cursor movement, seeks, short reads, EOF, and failure behavior. It has no host filesystem dependency. No unrestricted path access is enabled by CHARIN.

## Qualification

The direct suite uses sample bytes ABCDEF, mixed-case and space data, NUL and high-bit bytes, CR/LF, an empty stream, and a fixed-seed repeated stream. The fixed model seed is 0x52583333. The scorecard records direct assertion and property-case totals, failure injection, inherited regressions, both sanitizers, freestanding build, QAC guest status, and protected-artifact checks.
