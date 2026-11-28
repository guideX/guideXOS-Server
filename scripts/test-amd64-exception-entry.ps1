param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    # Optional kernel object directory (kernel/obj) from a real AMD64 build.
    # When given, the IRQ stubs in the compiled interrupts object are checked too.
    [string]$KernelObjectDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$compiler = Get-Command g++ -ErrorAction Stop
$assembler = Get-Command nasm -ErrorAction Stop
$objdump = Get-Command objdump -ErrorAction Stop

$entrySource = Join-Path $repoRoot 'kernel\arch\amd64\exception_entry.asm'
$taskSource = Join-Path $repoRoot 'kernel\arch\amd64\protected_task.asm'
$probeSource = Join-Path $repoRoot 'tests\amd64_exception_entry_probe.asm'
$testSource = Join-Path $repoRoot 'tests\amd64_exception_entry_test.cpp'
$entryObject = Join-Path $outputPath 'exception_entry.obj'
$taskObject = Join-Path $outputPath 'protected_task.obj'
$probeObject = Join-Path $outputPath 'exception_entry_probe.obj'
$testExecutable = Join-Path $outputPath 'amd64_exception_entry_test.exe'

# The production object is assembled with the kernel's own NASM format.
& $assembler.Source -f win64 $entrySource -o $entryObject
if ($LASTEXITCODE -ne 0) { throw "NASM exception_entry failed with exit code $LASTEXITCODE" }
& $assembler.Source -f win64 $taskSource -o $taskObject
if ($LASTEXITCODE -ne 0) { throw "NASM protected_task failed with exit code $LASTEXITCODE" }
& $assembler.Source -f win64 $probeSource -o $probeObject
if ($LASTEXITCODE -ne 0) { throw "NASM probe failed with exit code $LASTEXITCODE" }

$compilerArgs = @(
    '-std=c++14', '-O2', '-Wall', '-Wextra', '-DGXOS_BARE_METAL',
    '-mno-red-zone', '-fno-exceptions', '-fno-rtti',
    '-I', $repoRoot,
    $testSource, $entryObject, $probeObject,
    '-o', $testExecutable
)
& $compiler.Source @compilerArgs
if ($LASTEXITCODE -ne 0) { throw "g++ failed with exit code $LASTEXITCODE" }

$runOutput = & $testExecutable
$runExit = $LASTEXITCODE
$runOutput | ForEach-Object { Write-Output $_ }
if ($runExit -ne 0) { throw "AMD64 exception entry test failed with exit code $runExit" }

# ---------------------------------------------------------------------------
# Emitted-assembly checks on the production objects (Intel syntax).
# ---------------------------------------------------------------------------
$entryDump = & $objdump.Source -dr -M intel --no-show-raw-insn $entryObject
$entryDump | Set-Content -LiteralPath (Join-Path $outputPath 'exception_entry.disassembly.txt') -Encoding ascii

function Get-SymbolBody([string[]]$dump, [string]$symbol) {
    $body = New-Object System.Collections.Generic.List[string]
    $inside = $false
    foreach ($line in $dump) {
        if ($line -match '^[0-9a-f]+ <(.+)>:$') {
            # NASM emits local labels as "<symbol>.<label>"; they stay in the body.
            $inside = ($Matches[1] -eq $symbol -or $Matches[1].StartsWith("$symbol."))
            continue
        }
        if ($inside -and $line -match '^\s+[0-9a-f]+:\s+(.+)$') {
            $insn = ($Matches[1] -replace '\s+', ' ').Trim()
            if ($insn -match '^(IMAGE_REL_AMD64_\S+|R_X86_64_\S+) (.+)$') {
                if ($body.Count -gt 0) { $body[$body.Count - 1] = $body[$body.Count - 1] + ' <reloc ' + $Matches[2] + '>' }
                continue
            }
            if ($insn -ne 'nop' -and $insn -notmatch '^(cs )?nop[wl]? ' -and $insn -notmatch '^xchg ax,ax') {
                $body.Add($insn)
            }
        }
    }
    return ,$body
}

$hardwareErrorVectors = @(8, 10, 11, 12, 13, 14, 17, 21, 29, 30)
for ($v = 0; $v -lt 32; $v++) {
    $body = Get-SymbolBody $entryDump "exception_stub_$v"
    $hex = '0x{0:x}' -f $v
    if ($hardwareErrorVectors -contains $v) {
        $expected = @("push $hex", 'jmp')
    } else {
        $expected = @('push 0x0', "push $hex", 'jmp')
    }
    if ($body.Count -ne $expected.Count) { throw "exception_stub_${v}: expected $($expected.Count) instructions, found $($body.Count): $($body -join ' | ')" }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        if (-not $body[$i].StartsWith($expected[$i])) { throw "exception_stub_${v}: instruction $i '$($body[$i])' does not start with '$($expected[$i])'" }
    }
    if ($body[$body.Count - 1] -notmatch 'gxos_amd64_exception_common') { throw "exception_stub_${v} does not jump to the common entry" }
}

$common = Get-SymbolBody $entryDump 'gxos_amd64_exception_common'
$commonText = $common -join "`n"
$pushOrder = @('r15', 'r14', 'r13', 'r12', 'r11', 'r10', 'r9', 'r8', 'rdi', 'rsi', 'rbp', 'rbx', 'rdx', 'rcx', 'rax')
for ($i = 0; $i -lt 15; $i++) {
    if ($common[$i] -ne "push $($pushOrder[$i])") { throw "common entry push $i is '$($common[$i])', expected push $($pushOrder[$i])" }
}
if ($common[15] -ne 'cld') { throw "common entry: CLD must immediately follow the GPR saves (found '$($common[15])')" }
$callIndex = [Array]::FindIndex($common.ToArray(), [Predicate[string]]{ param($x) $x -match '^call ' })
$cldIndex = $common.IndexOf('cld')
if ($callIndex -lt 0 -or $cldIndex -gt $callIndex) { throw 'common entry: CLD does not precede the dispatcher call' }
if (($common | Where-Object { $_ -match '^call ' }).Count -ne 1) { throw 'common entry: expected exactly one CALL' }
if ($common[$callIndex] -notmatch 'gxos_native_exception_dispatch') { throw 'common entry: CALL target is not the dispatcher' }
if ($commonText -notmatch 'and rsp,0xfffffffffffffff0' -or $commonText -notmatch 'sub rsp,0x20') { throw 'common entry: missing dispatcher stack alignment or shadow space' }
$iretIndex = $common.IndexOf('iretq')
if ($iretIndex -lt 1 -or $common[$iretIndex - 1] -ne 'add rsp,0x10') { throw 'common entry: resume path must drop exactly 16 bytes (vector + error) before IRETQ' }
$popOrder = @('rax', 'rcx', 'rdx', 'rbx', 'rbp', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15')
for ($i = 0; $i -lt 15; $i++) {
    $insn = $common[$iretIndex - 16 + $i]
    if ($insn -ne "pop $($popOrder[$i])") { throw "common entry: resume pop $i is '$insn', expected pop $($popOrder[$i])" }
}
if ($common[$iretIndex - 17] -ne 'mov rsp,r12') { throw 'common entry: resume path must restore the saved frame pointer from R12' }
if (($common | Where-Object { $_ -eq 'iretq' }).Count -ne 1) { throw 'common entry: expected exactly one IRETQ' }
if (($common | Where-Object { $_ -match '^hlt' }).Count -ne 1 -or ($common -notcontains 'cli')) { throw 'common entry: missing fail-stop CLI/HLT loop' }
if ($commonText -notmatch 'cmp rax,0x1' -or $commonText -notmatch 'test rcx,rcx' -or $commonText -notmatch 'test rdx,rdx') { throw 'common entry: recovery disposition or continuation checks missing' }

# CPL3 exit stub: CLD after the 15 GPR saves and before the dispatcher call.
$taskDump = & $objdump.Source -dr -M intel --no-show-raw-insn $taskObject
$taskDump | Set-Content -LiteralPath (Join-Path $outputPath 'protected_task.disassembly.txt') -Encoding ascii
$exit = Get-SymbolBody $taskDump 'gxos_amd64_user_exit_stub'
$exitPushes = @('rax', 'rcx', 'rdx', 'rbx', 'rbp', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15')
for ($i = 0; $i -lt 15; $i++) {
    if ($exit[$i] -ne "push $($exitPushes[$i])") { throw "user exit push $i is '$($exit[$i])'" }
}
if ($exit[15] -ne 'cld') { throw "user exit: CLD must follow the GPR saves (found '$($exit[15])')" }
$exitCall = [Array]::FindIndex($exit.ToArray(), [Predicate[string]]{ param($x) $x -match '^call ' })
if ($exitCall -lt 16 -or $exit[$exitCall] -notmatch 'gxos_amd64_user_exit_dispatch') { throw 'user exit: dispatcher call not found after CLD' }

$irqStatus = 'not-checked (no -KernelObjectDirectory)'
if ($KernelObjectDirectory) {
    $interruptsObject = Get-ChildItem -LiteralPath $KernelObjectDirectory -Recurse -Filter 'interrupts.o' | Select-Object -First 1
    if (-not $interruptsObject) { throw "interrupts.o not found under $KernelObjectDirectory" }
    $irqDump = & $objdump.Source -dr -M intel --no-show-raw-insn $interruptsObject.FullName
    $irqDump | Set-Content -LiteralPath (Join-Path $outputPath 'interrupts.disassembly.txt') -Encoding ascii
    for ($n = 0; $n -lt 16; $n++) {
        $irq = Get-SymbolBody $irqDump "irq_stub_$n"
        $call = [Array]::FindIndex($irq.ToArray(), [Predicate[string]]{ param($x) $x -match '^call ' })
        if ($call -lt 2) { throw "irq_stub_${n}: call not found" }
        if ($irq[$call - 2] -ne 'cld' -or $irq[$call - 1] -ne ('mov ecx,0x{0:x}' -f $n)) { throw "irq_stub_${n}: expected 'cld; mov ecx,$n' before the call, found '$($irq[$call - 2])' '$($irq[$call - 1])'" }
        if ($irq[$irq.Count - 1] -ne 'iretq') { throw "irq_stub_${n}: does not end with IRETQ" }
    }
    $exceptionInline = $irqDump | Where-Object { $_ -match '<exception_stub_\d+>:' }
    if ($exceptionInline) { throw 'interrupts.o still defines inline exception stubs' }
    $irqStatus = "checked $($interruptsObject.FullName)"
}

Write-Output 'PASS: 32-vector error-code classification, normalized frame, GPR preservation, DF clear in dispatcher and restored by IRETQ, ABI alignment/shadow space, exact stack cleanup, recovery continuation, malformed-frame rejection'
Write-Output 'PASS: emitted assembly of exception_entry.obj and the CPL3 exit stub'
Write-Output "IRQ_STUBS=$irqStatus"
Write-Output 'LIMIT: hosted CPL3 execution of the production stub object through a same-privilege IRETQ; not hardware exception delivery'
Write-Output "TEST_EXE_SHA256=$((Get-FileHash -LiteralPath $testExecutable -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Output "ENTRY_OBJECT_SHA256=$((Get-FileHash -LiteralPath $entryObject -Algorithm SHA256).Hash.ToLowerInvariant())"
