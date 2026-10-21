# REXX RX22 — Classic string and diagnostic built-ins

## Scope and result

RX22 implements `REVERSE(string)`, `SOURCELINE([n])`, and `ERRORTEXT(n)`. The RX21 inventory listed each as a distinct, unimplemented function. The runtime's VM status identifiers remain unchanged; `ERRORTEXT` exposes IBM z/OS 2.5 TSO/E REXX syntax-message numbers and text, not guideXOS VM status values.

`DATE` and `TIME` remain Partial. RX22 does not change RTC interpretation or live clock policy.

## Behavior

### REVERSE

`REVERSE` copies the input bytes in reverse order. It preserves embedded NULs, high-bit bytes, and spaces, leaves the source value untouched, and accepts an empty input. The existing 16 MiB result limit applies. The byte-oriented behavior follows the guideXOS runtime string representation; the implementation does not decode or normalize Unicode.

IBM describes `REVERSE` as returning the string with its ends swapped and shows the trailing-space case `REVERSE('XYZ ') -> ' ZYX'` ([IBM REVERSE reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-reverse)).

### SOURCELINE

The parser carries a borrowed pointer to the lexer's input only through compilation. Each resulting bytecode chunk then owns one exact source snapshot, bounded by the existing 16 MiB source limit. This snapshot is freed with the chunk and contributes to dynamic bytecode storage accounting. No process-global source buffer is used.

The VM's active chunk supplies the source context. Internal routines see their executing program's chunk. `INTERPRET` switches the active context to its dynamic chunk and restores the parent context afterward. Separate VMs therefore keep separate source snapshots. This remains valid after the original caller buffer is released.

With no argument, `SOURCELINE()` returns the available line count, or zero when source is unavailable. `SOURCELINE(n)` requires a positive whole number no greater than the line count; an invalid or out-of-range request raises the runtime's invalid-function-call status. If source is unavailable, a requested line returns the null string. LF and CRLF terminators are handled; CRLF's CR is excluded from returned text. Blank lines, comments, quote bytes, high-bit bytes, and other line content are retained. A final newline terminates the preceding line and does not create an extra source line; an empty program has zero lines.

IBM specifies that the no-argument call returns the final line number (or zero if source access is unavailable), and that a supplied `n` is a positive whole number within that count ([IBM SOURCELINE reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-sourceline)).

### ERRORTEXT

`ERRORTEXT(n)` accepts a whole number from 0 through 99. Codes 3 through 49 use the IBM 2.5 TSO/E message text table; all other in-range codes return the null string. Negative, fractional, nonnumeric, and above-99 inputs are rejected. The function does not expose host paths, kernel addresses, or VM implementation messages.

IBM documents the 0–99 range, null-string behavior for unassigned numbers, and the reference results `ERRORTEXT(16) -> 'Label not found'` and `ERRORTEXT(60) -> ''` ([IBM ERRORTEXT reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-errortext)). The IBM programming-services documentation says `SYNTXMSG` uses the same message text and that not every number is assigned ([IBM SYNTXMSG text-unit reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=irxtxt-functions-text-units)).

The implemented IBM mapping is:

| Code | Message |
| ---: | --- |
| 3 | Program is unreadable |
| 4 | Program interrupted |
| 5 | Machine storage exhausted |
| 6 | Unmatched `/*` or quote |
| 7 | WHEN or OTHERWISE expected |
| 8 | Unexpected THEN or ELSE |
| 9 | Unexpected WHEN or OTHERWISE |
| 10 | Unexpected or unmatched END |
| 11 | Control stack full |
| 12 | Clause too long |
| 13 | Invalid character in program |
| 14 | Incomplete DO/SELECT/IF |
| 15 | Invalid hexadecimal or binary string |
| 16 | Label not found |
| 17 | Unexpected PROCEDURE |
| 18 | THEN expected |
| 19 | String or symbol expected |
| 20 | Symbol expected |
| 21 | Invalid data at end of clause |
| 22 | Invalid character string |
| 23 | Invalid SBCS/DBCS mixed string |
| 24 | Invalid TRACE request |
| 25 | Invalid subkeyword found |
| 26 | Invalid whole number |
| 27 | Invalid DO syntax |
| 28 | Invalid LEAVE or ITERATE |
| 29 | Environment name too long |
| 30 | Name or string > 250 characters |
| 31 | Name starts with number or `"."` |
| 32 | Invalid use of stem |
| 33 | Invalid expression result |
| 34 | Logical value not 0 or 1 |
| 35 | Invalid expression |
| 36 | Unmatched `(` in expression |
| 37 | Unexpected `,` or `)` |
| 38 | Invalid template or pattern |
| 39 | Evaluation stack overflow |
| 40 | Incorrect call to routine |
| 41 | Bad arithmetic conversion |
| 42 | Arithmetic overflow/underflow |
| 43 | Routine not found |
| 44 | Function did not return data |
| 45 | No data specified on function RETURN |
| 46 | Invalid variable reference |
| 47 | Unexpected label |
| 48 | Failure in system service |
| 49 | Language processor failure |

## Qualification

The direct C suite uses an independent expected-message array for all 100 in-range codes. It checks reference examples, invalid arguments, repeated lookups, memory-failure recovery, and separate VM source contexts. `REVERSE` property cases use the fixed seed `0x52583232`, compare against a separately computed byte-reversal oracle, check byte multisets, ensure inputs remain unchanged, and verify double reversal. Source tests cover caller-buffer release, empty and final lines, LF/CRLF, blank lines, comments, quoted binary literals, high-bit bytes, internal routine calls, `INTERPRET`, missing source, and compiler-allocation failure recovery.

The hosted RX22 runner adds a CLI fixture and reruns the inherited regression suites. Sanitizer, native AMD64, Regina, QAC, guest, protected-file, and untracked-path results are recorded in [the RX22 scorecard](REXX_RX22_SCORECARD.json) and [RX22 evidence](evidence/rx22/).

## Deferred boundary

`DATE` and `TIME` remain Partial pending a supported persistent settings service and independently qualified clock provenance. RX22 verifies their existing fail-closed behavior and PIT elapsed-time support as inherited runtime behavior; it does not define or store RTC policy.
