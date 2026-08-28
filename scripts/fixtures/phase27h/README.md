# Phase 27H comparisons and conditional-control-flow proof sources

These bounded sources are staged by `smoke-compiler-bootstrap.ps1 -Phase27H`.
They exercise value-producing signed comparisons, generic truthiness, real
`if`/`else` branches, nested conditionals, branch assignments, return-path
diagnostics, malformed-condition diagnostics, deterministic output, and the
Developer Studio edit/build/run path.

Locals are function-scoped for this phase. Duplicate local names are rejected
anywhere in `gx_main`; loops and user-defined functions remain unsupported.
