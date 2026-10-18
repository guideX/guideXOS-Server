# Portable REXX compatibility corpus

This corpus exercises deterministic classic-language behavior through the hosted guideXOS CLI. It contains 183 scripts: core (17), decimal arithmetic (14), PARSE (12), routines (10), conditions (11), INTERPRET (14), RX12 string built-ins (30), RX13 string editing/word utilities (33), RX14 numeric inspection (29), RX15 conversions (3), and RX16 binary/hex literals (10). `cases/host_specific/README.md` documents why shell, file-system, and network behavior is excluded from direct differential comparison.

## Run

From the repository root, build the hosted CLI with GCC/`CC`, run every corpus case, and save the JSON report and raw stdout/stderr bytes. Use a fresh phase-owned directory for each qualification run:

```powershell
python tests/rexx_compat/run_differential.py --results-dir build/rexx/rx16/differential-example
```

Regina is discovered only as `regina`/`regina.exe`; the harness never guesses that a generic `rexx.exe` is Regina. To use a known installation, pass `--regina C:\path\to\regina.exe` or set `GUIDEXOS_REGINA`. The harness checks `regina -v` for Regina identity and executes a marker script in restricted `-r` mode before comparing any cases.

Run one case and preserve a separate report under an RX16-owned directory with:

```powershell
python tests/rexx_compat/run_differential.py --case literals/hex_suffix.rex --results-dir build/rexx/rx16/differential-one-case
```

Each JSON case records a fully expanded reproduction command, input identity, interpreter argv/cwd, exit status, timeout/output-limit state, duration, output sizes and SHA-256 digests. Raw output is under `raw/<case-id>/`. Normalization changes only CRLF and lone CR line endings to LF. Exit status, stderr, letter case, numeric formatting, spaces, and output order remain significant.

The reference process receives only corpus-controlled scripts with stdin closed, no shell, a five-second per-interpreter timeout, a one MiB limit per output stream, and process-tree cleanup. There are no `ADDRESS` commands, shell execution, file mutations, or networking operations in the portable corpus. `NOT_RUN` means the reference was unavailable; it is never counted as a match. `HOST_SPECIFIC` is reserved for cases outside the portable corpus.

The local corpus uses expected process statuses from `cases/expectations.json`; the dynamic `EXIT 5` case is expected to return 5. A failed local expectation makes the runner fail. The reference classifications remain independent of that local result.

## Sanitizers

Run the hosted C suites and the full 183-case corpus under the locally supported x64 runtimes:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_rexx_sanitizers.ps1 -Phase RX16 -Sanitizer Both
```

The script discovers Visual Studio with `vswhere`, imports its developer environment into the current PowerShell process only, uses MSVC AddressSanitizer and Clang/LLVM UBSan with the bundled MSVC runtime, and keeps RX16 builds in fresh `build/rexx/rx16` run directories. It does not add sanitizer flags to the freestanding kernel build. The ordinary `tests/run_rexx_tests.ps1 -Phase RX16` remains the complete inherited hosted regression command, including the C++ shell bridge and freestanding-specific tests that are not part of the sanitizer matrix.

## Results interpretation

RX16 qualification runs the 183-case expected-exit corpus and records independent Regina comparisons separately. When no verified Regina executable is installed, those comparisons remain `NOT_RUN`; local expected-output fixtures still run through the hosted qualification runner. The harness is ready to produce `MATCH`, `MISMATCH`, `GUIDEXOS_UNSUPPORTED`, `REFERENCE_UNSUPPORTED`, or `INCONCLUSIVE` outcomes when Regina is configured. A comparison difference is an observation requiring review, not an automatic guideXOS defect.
