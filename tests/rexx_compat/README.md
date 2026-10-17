# RX11 portable REXX compatibility corpus

This corpus exercises deterministic classic-language behavior through the hosted guideXOS CLI. It contains 78 scripts: core (17), decimal arithmetic (14), PARSE (12), routines (10), conditions (11), and INTERPRET (14). `cases/host_specific/README.md` documents why shell, file-system, and network behavior is excluded from direct differential comparison.

## Run

From the repository root, build the hosted CLI with GCC/`CC`, run every corpus case, and save the JSON report and raw stdout/stderr bytes:

```powershell
python tests/rexx_compat/run_differential.py --results-dir tmp/rexx-rx11-differential
```

Regina is discovered only as `regina`/`regina.exe`; the harness never guesses that a generic `rexx.exe` is Regina. To use a known installation, pass `--regina C:\path\to\regina.exe` or set `GUIDEXOS_REGINA`. The harness checks `regina -v` for Regina identity and executes a marker script in restricted `-r` mode before comparing any cases.

Run one case and preserve a separate report with:

```powershell
python tests/rexx_compat/run_differential.py --case interpret/interpret_nested.rex --results-dir tmp/rx11-one-case
```

Each JSON case records a fully expanded reproduction command, input identity, interpreter argv/cwd, exit status, timeout/output-limit state, duration, output sizes and SHA-256 digests. Raw output is under `raw/<case-id>/`. Normalization changes only CRLF and lone CR line endings to LF. Exit status, stderr, letter case, numeric formatting, spaces, and output order remain significant.

The reference process receives only corpus-controlled scripts with stdin closed, no shell, a five-second per-interpreter timeout, a one MiB limit per output stream, and process-tree cleanup. There are no `ADDRESS` commands, shell execution, file mutations, or networking operations in the portable corpus. `NOT_RUN` means the reference was unavailable; it is never counted as a match. `HOST_SPECIFIC` is reserved for cases outside the portable corpus.

The local corpus uses expected process statuses from `cases/expectations.json`; the dynamic `EXIT 5` case is expected to return 5. A failed local expectation makes the runner fail. The reference classifications remain independent of that local result.

## Sanitizers

Run the hosted C suites and the same 78-case corpus under the locally supported x64 runtimes:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_rexx_sanitizers.ps1 -Sanitizer Both
```

The script discovers Visual Studio with `vswhere`, imports its developer environment into the current PowerShell process only, uses MSVC AddressSanitizer and Clang/LLVM UBSan with the bundled MSVC runtime, and keeps all builds in fresh `tmp/rexx-rx11-sanitizer-*` directories. It does not add sanitizer flags to the freestanding kernel build. The ordinary `tests/run_rexx_tests.ps1` remains the complete inherited hosted regression command, including the C++ shell bridge and freestanding-specific tests that are not part of the sanitizer matrix.

## Results interpretation

The current committed evidence records 78 local expected exits and 78 `NOT_RUN` Regina comparisons because no verified Regina executable was installed. The harness is ready to produce `MATCH`, `MISMATCH`, `GUIDEXOS_UNSUPPORTED`, `REFERENCE_UNSUPPORTED`, or `INCONCLUSIVE` outcomes when Regina is configured. A comparison difference is an observation requiring review, not an automatic guideXOS defect.
