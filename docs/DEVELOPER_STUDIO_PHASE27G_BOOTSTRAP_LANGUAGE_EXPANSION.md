# Developer Studio Phase 27G — Bootstrap Language Expansion

Phase 27G expands the in-guideXOS bootstrap compiler from a literal-return
proof into a small, bounded imperative language. The compiler still accepts
one source file and one `gx_main` function; it is a bootstrap subset, not full
C or C++.

## Supported grammar

The implemented grammar is:

```text
function := "int" "gx_main"
            "(" ("void" | "gx_app_context") "*" identifier ")"
            "{" statement* "}"

statement := declaration | assignment | log_statement | return_statement
declaration := "int" identifier [ "=" expression ] ";"
assignment := identifier "=" expression ";"
log_statement := "log" "(" identifier "," string_literal ")" ";"
return_statement := "return" expression ";"

expression := additive
additive := multiplicative { ("+" | "-") multiplicative }
multiplicative := unary { "*" unary }
unary := "-" unary | primary
primary := integer_literal | identifier | "(" expression ")"
```

The context parameter used by `log` must be the function's declared parameter.
`return` is required and must be the final statement. Comments and the prior
whitespace/line-column behavior remain supported.

## Lexer, symbols, and IR

The lexer emits explicit identifier, integer, string, keyword, operator, and
delimiter tokens. Identifiers follow `[A-Za-z_][A-Za-z0-9_]*` and are capped at
63 bytes. The parser uses a fixed 32-entry local symbol table containing the
name, deterministic stack slot, and initialized state. Duplicate declarations
and unknown identifiers are rejected with source location diagnostics.

Declarations without an initializer are deterministically initialized to zero;
there is no observable uninitialized stack state. The parser resolves the
source into target-neutral `FunctionIR` arrays:

```text
Expression: Constant, LoadLocal, Add, Subtract, Multiply, Negate
Statement:  DeclareLocal, StoreLocal, HostLog, Return
```

Expression nodes and statements are fixed-capacity arrays. Recursive descent
has an explicit expression nesting limit of 16. The AMD64 backend consumes
this IR; parser structures contain no registers or opcodes.

## Integer semantics and precedence

Values are signed 32-bit integers. The bootstrap language defines arithmetic to
wrap modulo 2^32/two's-complement, matching the generated AMD64 32-bit
operations. Multiplication binds more tightly than addition/subtraction;
operators at one level associate left-to-right. Parentheses override the normal
precedence, and unary minus binds at the unary level. Division is intentionally
not implemented.

## Stack frame and AMD64 lowering

Locals use four-byte stack slots at `[rbp-4]`, `[rbp-8]`, and so on. The saved
incoming context is below the locals at `[rbp-(localBytes+8)]`. Framed
functions use:

```text
push rbp
mov  rbp, rsp
sub  rsp, align16(40 + localBytes)
```

The 40-byte base accounts for the 32-byte Microsoft x64 shadow/home area and
the saved context slot. Each host call reserves the 32-byte home area while
the frame remains active; locals and the saved context cannot overlap it.
The epilogue restores `rsp`, pops `rbp`, and returns. The old literal and
single-log constant-return byte shapes remain compatible fast paths.

Expression lowering uses a small stack temporary for binary operands, then
`add`, fixed-order subtraction, or `imul`; unary negation uses 32-bit `neg`.
No general register allocator is attempted.

For the local-variable proof, external raw-code disassembly of the guest-built
ELF showed the equivalent of:

```text
55 48 89 E5                 push rbp; mov rbp,rsp
48 83 EC 40                 sub rsp,0x40
48 89 8D EC FF FF FF        mov [rbp-0x14],rcx
B8 14 00 00 00              mov eax,20
89 85 FC FF FF FF           mov [rbp-4],eax
B8 16 00 00 00              mov eax,22
89 85 F8 FF FF FF           mov [rbp-8],eax
8B 85 FC FF FF FF           mov eax,[rbp-4]
50 ... 01 C8                push rax ... add eax,ecx
89 85 F4 FF FF FF           mov [rbp-12],eax
8B 85 F4 FF FF FF           mov eax,[rbp-12]
```

## Multiple host calls and string data

Every `log(ctx, "...")` is a separate `HostLog` statement. String literals
are retained in source order, NUL-terminated, and addressed by deterministic
offsets in the read-only ELF data segment. Calls reload the saved context
before each call because Microsoft x64 `RCX` is volatile. The data segment is
readable and non-executable; duplicate pooling is not required.

## Resource limits and diagnostics

The final fixed limits are:

```text
source                 64 KiB
tokens                 1024
diagnostics            16 (128-byte messages)
identifier             63 bytes
locals                 32
statements             128
expression nodes       512
expression nesting     16
string literals        16
string length          255 bytes each
string data             2048 bytes total
generated code          4096 bytes
generated ELF           12288 bytes
```

Examples of emitted diagnostics include `unknown identifier 'missing'`,
`duplicate local 'x'`, `too many local variables`, `expected expression`, and
`expression nesting limit exceeded`. The diagnostics retain source path,
line, column, byte offset, and token kind as they flow through OutputService.

## Developer Studio proof

The real bare-metal Build-before-Run path was exercised with:

```text
edit -> Save All -> BuildController -> in-kernel compiler -> NativeElf
     -> RunController -> NativeElf runtime -> OutputService -> exit code
```

The primary source was:

```cpp
int gx_main(gx_app_context* ctx)
{
    int x = 20;
    int y = 22;
    int result = x + y;
    log(ctx, "Calculating inside guideXOS...");
    log(ctx, "Done.");
    return result;
}
```

Observed application output was:

```text
Calculating inside guideXOS...
Done.
```

The Run lifecycle terminal record reported `Run Succeeded exit_code=42`.

The in-session edit changed the program to `x = 7`, `y = 6`,
`result = x * y`, `log(ctx, "Recompiled expression program.")`, and
`return result - 1`; the source FNV-1a evidence and BuildResult SHA-256
artifact identity both changed, output changed, and the run returned 41.
Repeating the same edit produced byte-identical artifact identity. Unknown
identifier and duplicate-local edits failed the build, blocked Run, and a
restored valid source ran again. The kernel and VFS remained operational after
the repeated cycles.

## Validation

Focused host checks:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-compiler-bootstrap-host-test.ps1
powershell -ExecutionPolicy Bypass -File scripts/run-native-elf-runtime-host-test.ps1
```

The QEMU proof command was:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke-compiler-bootstrap.ps1 `
  -BootCount 3 -TimeoutSeconds 90 -Phase27G
```

All Phase 27B–27G markers passed on three fresh boots. External `readelf` and
`objdump` were used only after guest compilation for ELF/code audit; neither
tool participates in the compiler operation.

## Hosted compatibility and limitations

Hosted Developer Studio continues to use its existing PowerShell/LLVM route.
The expanded language applies only to the bare-metal bootstrap target. Known
bootstrap limitations are one `gx_main`, one source file, signed integer
subset only, no `if`, loops, user functions, arrays, general pointers, full
C/C++, general linker, or debugger attachment. The runtime is trusted and
kernel-owned, and the current code generator is AMD64-only.

The next bounded compiler phase should be comparisons and `if` / `else`,
introducing control flow without widening the language surface prematurely.
