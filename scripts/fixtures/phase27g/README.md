# Phase 27G bootstrap compiler proof sources

These bounded sources are staged by `smoke-compiler-bootstrap.ps1 -Phase27G`.
They are compiled and executed by the guideXOS kernel compiler; the host only
builds the surrounding boot proof and audits emitted ELF bytes afterward.

The direct proofs cover literal expressions (`g27expr.c`), locals
(`g27local.c`), assignments (`g27assn.c`), precedence/parentheses
(`g27preca.c` and `g27precb.c`), unary negation (`g27unary.c`), three ordered
host calls (`g27logs.c`), unknown identifiers, duplicate locals, deterministic
rebuild, and recovery after a failed build. The IDE fixture in this directory
is opened and edited by the Phase 27G Developer Studio NativeElf proof app.
