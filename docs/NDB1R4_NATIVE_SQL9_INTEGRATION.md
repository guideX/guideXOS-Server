# NDB1R4 — Freestanding SQL9 Integration

## Result

**Outcome B — NATIVE_SQL9_INTEGRATION_PARTIAL.** The repository gates pass, and the SQL9 runtime dependency audit reached a reproducible Gate 1 blocker before any native execution work: the current AMD64 kernel configuration deliberately excludes C++ standard library headers and libraries, while SQL9's authoritative engine sources depend throughout on `std::map`, `std::set`, `std::vector`, `std::string`, and `std::unique_ptr`. The existing kernel runtime does not supply those facilities, and its `operator new` failure behavior cannot safely back standard containers with exceptions disabled.

Gates 2–4 were not started. No SQL9 source, database format, phase marker, kernel source, test disk, or retained NDB1R1–NDB1R3 fixture was changed. This is an integration feasibility result, not a finding that the NDB1R3 storage path is untrustworthy.

## Repository and starting snapshot

Initial read-only checks on 2026-10-15 confirmed:

| Checkout | Branch / starting HEAD | Phase before work | Initial tracked and untracked status |
|---|---|---|---|
| `D:\dev\guideXOSServer_NATIVE_DB_STORAGE` | `main`, `f20d19597857eb784570ffcbf4490497722c1ec5` | NDB1R3 → NDB1R4 | Clean; branch is 9 commits ahead of its local `origin/main` |
| `D:\dev\guideXOS_SqlDatabase` | `main`, `47862b78d3d643483798d8874b5bc321d782df51` | SQL9 → SQL10 | Clean; tracks `origin/main` |

Both paths, branches, exact HEADs, and phase marker contents matched the prompt. NDB1R1, NDB1R2, and NDB1R3 acceptance reports were read. The SQL `.phase` remains `last_completed=SQL9`, `next_expected=SQL10`. The native marker remains `last_completed=NDB1R3`, `next_expected=NDB1R4`. No branch or remote operation was performed. The Server checkout was not pushed.

The SQL source baseline remains the authoritative source at the SQL checkout above. No SQL build-root configuration or pinned source revision integration was added because Gate 1 stopped before engine integration.

## SQL9 source dependency graph

The hosted CMake target `gxos_database` contains these 20 authoritative translation units:

`database_format`, `database_checksum`, `database_io`, `database_native_io`, `database_header`, `database_page`, `database_file`, `database_engine`, `database_buffer`, `database_catalog`, `database_index`, `database_heap`, `database_wal`, `database_transaction`, `database_relational`, `database_sql_token`, `database_sql_tokenizer`, `database_sql_parser`, `database_sql_expr`, and `database_sql` (the corresponding `.cpp` files and their included headers).

The execution dependency flow is:

```text
SQL engine / prepared statements
  -> tokenizer, parser, AST and expression binder
  -> relational execution and constraints
  -> catalog, heap, PRIMARY KEY / secondary / FK indexes, transactions
  -> page store, database file, WAL and recovery
  -> IDatabaseFile / IDatabaseFileSystem
  -> hosted stdio provider or NDB1R3 native adapter
```

The NDB1R3 bridge is a narrow C ABI without STL. SQL's `database_native_io.cpp` adapter above it still uses SQL-facing C++ types and is not a freestanding bridge by itself. The hosted `database_io.cpp` provider uses stdio and must not be included for guest database I/O. The engine already has injected file-system interfaces that can select a provider; no provider change was made in this phase.

## Runtime dependency audit

| Facility | SQL9 use and source areas | Current guideXOS support | Freestanding result / minimum safe work |
|---|---|---|---|
| `std::string`, `std::vector` | Present across value, parser, catalog, heap, page, WAL, diagnostics, results, and SQL execution types | No STL implementation in the kernel; C++ headers are excluded by `-nostdinc++` | Requires an explicitly qualified freestanding implementation or carefully scoped SQL portability work. |
| `std::unique_ptr`, `std::map`, `std::set`, `std::pair` | Engine/file/WAL/transaction ownership; transaction and execution maps; heap/index visited sets and paths | No matching kernel containers or smart pointers | These are required by core SQL9 paths; replacing them broadly with a new custom STL is not acceptable. |
| Algorithms and utility | `std::sort`, `std::find`, `std::move`, `std::pair`; also numeric limits and string conversion | No C++ standard library headers or linked `libstdc++` | The flags cannot simply be relaxed: the kernel links with `-nostdlib` and only the compiler runtime archive. Runtime/header compatibility must be qualified first. |
| Dynamic allocation | Explicit `new` in the database buffer and ownership paths; implicit allocations in strings, vectors, maps, sets, result rows, catalog loading, index traversal, and WAL scan | AMD64 has a fixed 512 MiB kernel heap in `cxx_runtime.cpp`; global `operator new` delegates to `kernel_alloc`, which returns null when exhausted | This does not provide a per-database budget or standard-container allocation-failure contract. SQL operations cannot currently turn allocation exhaustion into a checked `DbResult`. |
| Exceptions / RTTI | No explicit SQL `throw`, `try/catch`, `dynamic_cast`, or `typeid` use was found | Kernel compiles with `-fno-exceptions -fno-rtti` | The syntax flags alone are not the blocker. Standard container allocation/error helpers still need defined behavior; null-returning `operator new` is not proof of safe `std::bad_alloc` handling. |
| C runtime | SQL uses memory/string helpers, `std::to_string`, `std::strtod`, and floating-point classification; native kernel runtime implements a subset including memory and basic string functions | No complete hosted CRT or qualified C++ runtime is linked; `strtod` and `to_string` are not provided by the inspected kernel runtime | Requires a bounded implementation or an explicitly qualified runtime subset, with error behavior verified. |
| Floating point | SQL9 has `double` values and floating-point parsing, comparison, aggregates, and index encoding | AMD64 flags disable SSE/SSE2; existing SQL hosted behavior is not evidence of kernel FPU/runtime compatibility | The native engine must compile and execute with the kernel's actual floating-point policy or define a reviewed compatible policy. |
| Static initialization / TLS | No `thread_local` or SQL global engine singleton was identified in the source scan; SQL's ordinary ownership is instance-based | Kernel provides partial C++ guard/atexit stubs; startup behavior is separate from STL availability | Audit any selected runtime's constructors and destructors before linking it. No SQL static-constructor or TLS claim is made from this blocked build. |
| Hosted I/O / locale / streams | Hosted provider uses stdio; no SQL engine dependency on iostreams or locale was identified | Hosted provider is not suitable for guest database files | Keep the NDB1R3 provider path and exclude hosted file implementation from the guest target. |

SQL's existing logical bounds include a 64 KiB maximum page, 4,096 pages per transaction, and a 64 MiB WAL cap. The NDB1R3 bridge also enforces the FAT/VFS 32-bit file extent. These bounds do not impose a total SQL heap/result-memory budget: query results and multiple intermediate vectors are dynamically materialized, and the engine has no allocator quota tied to a native qualification workload.

## Gate 1 compiler evidence

The actual current AMD64 Makefile flags were checked in `kernel/Makefile` and `kernel/arch/amd64/Makefile.arch`:

```text
-std=c++14 -ffreestanding -fno-exceptions -fno-rtti
-nostdlib -nostdinc++ -fno-builtin -DGXOS_BARE_METAL
-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
-fno-pic -fno-pie -mno-stack-arg-probe -fno-stack-check -fno-stack-protector
```

The configured compiler is MinGW-W64 `g++ 15.2.0`; the AMD64 build links with `-nostdlib` and the configured `libgcc` archive. There is no configured STL or hosted CRT library.

A syntax-only compile of the real `database/database_sql.cpp` with those flags and the SQL/kernel include roots failed before code generation:

```text
database/database_sql.h:16:10: fatal error: map: No such file or directory
```

This is the exact configured `-nostdinc++` behavior, not a SQL syntax failure. Removing that restriction or linking host MinGW `libstdc++` would bypass the kernel's current runtime contract without establishing freestanding allocator, error, CRT, constructor, floating-point, or link safety. No such flag relaxation or link attempt was made.

The existing kernel heap is bounded in total size but the allocation interface returns null, while standard containers are designed around an allocator that reports allocation failure through the library's allocation error path. With kernel exceptions disabled and no qualified C++ library, unchecked null handling or termination cannot be ruled out. Consequently, the requested allocation-failure and bounded-memory guarantees are not met.

## Gate status and regression evidence

| Gate / check | Result |
|---|---|
| Gate 1: SQL9 C++ source under current freestanding flags | **Blocked** at missing `<map>` header |
| Gate 1: qualified runtime, native adapter + SQL9 engine link, integrated kernel build | Not attempted after the required source compile failed |
| Gate 2: actual SQL9 guest CREATE/INSERT/SELECT/prepared/FK/transaction execution | Not started |
| Gate 3: independent QEMU Boot A/B/C on a dedicated persistent image | Not started; no NDB1R4 disk was created or attached |
| Gate 4: native WAL cut points and guest recovery | Not started |
| SQL hosted CTest rerun | **10/10 targets passed; 92,655 checks, 0 failures** |
| Hosted SQL native-provider adapter | **19/19 checks passed** (included in CTest total) |
| Hosted Server native file bridge | **53/53 checks passed** |
| Hosted Server VFS resize/fault suite | **171/171 checks passed** |
| ATA status / flush propagation | **21/21 and 12/12 checks passed** |
| Block generation / hosted VFS read | **5/5 and 4/4 checks passed** |
| App Model identity / file activation | **25/25 and 60/60 checks passed** |
| Normal AMD64 kernel target | `mingw32-make -C kernel ARCH=amd64 -j4` exited 0; current production target was up to date and the Mbed TLS profile check passed |

The SQL hosted run used `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_hosted_tests.ps1`. Its per-target check counts were: storage 172; relational 10,398; transaction/recovery 2,553; SQL 1,779; indexes 2,752; SQL7 1,307; SQL8 4,503; SQL9 15,274; SQL9R prepared-plan reuse 53,898; native-provider adapter 19. The suite completed in 1,773.14 seconds with all targets passing.

The hosted Server regressions were rerun without guest execution. ATA propagation and VFS resize builds emitted the existing `fs_fat.cpp` unused-function warning and `fs_ext4.cpp` always-false range-check warning; both test executables passed. No new kernel compile warnings were emitted because the normal kernel target was already current. No Server source or build configuration changed.

The broader hosted Server filesystem-contract runner remains a separate known dependency issue: the NDB1R2 report records its failure on missing `third_party/mbedtls/tf-psa-crypto/include/mbedtls/private/crypto_adjust_config_enable_builtins.h`. It was not rerun here because the SQL9 runtime blocker had already stopped native integration, and that runner has no direct dependency on SQL9. Prior NDB1R1–NDB1R3 reports remain the source for the accepted QEMU storage results.

No QEMU command, disk identity, Boot A/B/C log, WAL interruption result, or recovery integrity result exists for NDB1R4. No persistence or durability claim is made. FAT32 remains nonjournaled; its arbitrary power-loss metadata behavior is unqualified.

## Smallest next repair

Before adding SQL to a kernel target, qualify a freestanding C++ library/runtime choice for this exact AMD64 toolchain that provides the SQL9-required containers and algorithms, uses a bounded allocator, and reports allocation failure without exception-based termination. Include its C runtime and floating-point requirements in that qualification. If no such subset can satisfy the kernel contract, estimate the required SQL source portability work and stop before native execution until it can propagate allocation failures through the existing SQL error model.

After that prerequisite passes, add a configurable SQL source-root integration pinned to the SQL revision, omit hosted file I/O from the guest target, and prove a full kernel link before creating any NDB1R4 disk. The NDB1R3 C file bridge and the SQL9 implementation remain authoritative.

## Closeout

The native phase is intentionally incomplete and remains at NDB1R3 → NDB1R4. SQL remains at SQL9 → SQL10. This report records the Gate 1 blocker as Outcome B; it does not advance either phase marker.
