param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$compiler = Get-Command g++ -ErrorAction Stop
$target = & $compiler.Source -dumpmachine
if ($LASTEXITCODE -ne 0 -or $target -notmatch '^x86_64-.*mingw') {
    throw "The hosted BSP APIC_BASE probe test requires AMD64 MinGW, found '$target'."
}

$fixupObject = Join-Path $outputPath 'bsp_apic_base_fixup_test.o'
& nasm -f win64 -DGXOS_APIC_BASE_HOSTED_TEST (Join-Path $repoRoot 'kernel/arch/amd64/bsp_apic_base_fixup.asm') -o $fixupObject
if ($LASTEXITCODE -ne 0) { throw "APIC_BASE fixup assembly failed with exit code $LASTEXITCODE" }

$objdump = Get-Command objdump -ErrorAction Stop
$disassembly = (& $objdump.Source -dr $fixupObject 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw "APIC_BASE fixup disassembly failed with exit code $LASTEXITCODE" }
$disassemblyPath = Join-Path $outputPath 'bsp_apic_base_fixup_test.disassembly.txt'
[IO.File]::WriteAllText($disassemblyPath, $disassembly, (New-Object Text.UTF8Encoding($false)))
$gpStart = $disassembly.IndexOf('<gxos_amd64_apic_base_gp>:')
$gpEnd = $disassembly.IndexOf('<gxos_amd64_apic_base_emergency_stop>:')
if ($gpStart -lt 0 -or $gpEnd -le $gpStart) { throw 'The emitted object is missing the bounded #GP handler symbols.' }
$gpAssembly = $disassembly.Substring($gpStart, $gpEnd - $gpStart)
foreach ($pattern in @('pushf', 'push\s+%rax', 'push\s+%r10', 'pop\s+%r10', 'pop\s+%rax', 'popf', 'iretq', 'exception_stub_13', 'gxos_amd64_apic_base_fault', 'mov\s+%rax,0x8\(%r10\)')) {
    if ($gpAssembly -notmatch $pattern) { throw "Emitted #GP handler is missing required state/frame behavior: $pattern" }
}
if ($gpAssembly -match '\bcall\b|%(rbx|rbp|rsi|rdi|r12|r13|r14|r15)\b') {
    throw 'Emitted #GP handler calls code or uses a Microsoft x64 nonvolatile register.'
}
if (([regex]::Matches($disassembly, '\brdmsr\b')).Count -ne 1) {
    throw 'Emitted APIC_BASE fixup object must contain exactly one authorized RDMSR site.'
}

$sources = @(
    'tests/amd64_bsp_apic_base_probe_test.cpp',
    'kernel/arch/amd64/bsp_apic_base_probe.cpp',
    'kernel/arch/amd64/bsp_xapic_admission.cpp',
    'kernel/arch/amd64/bsp_apic.cpp',
    'kernel/arch/amd64/per_cpu.cpp',
    'kernel/arch/amd64/boot_processor_inventory.cpp',
    'kernel/arch/amd64/cpu_capabilities.cpp'
) | ForEach-Object { Join-Path $repoRoot $_ }
$testExecutable = Join-Path $outputPath 'amd64_bsp_apic_base_probe_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra -Werror -pthread `
    -idirafter (Join-Path $repoRoot 'kernel/core/include') -I (Join-Path $repoRoot 'kernel/arch/amd64/include') `
    @sources $fixupObject -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw "APIC_BASE probe test compilation failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "APIC_BASE probe tests failed with exit code $LASTEXITCODE" }
