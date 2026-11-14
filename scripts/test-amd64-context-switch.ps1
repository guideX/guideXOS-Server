param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$compiler = Get-Command g++ -ErrorAction Stop
$assembler = Get-Command nasm -ErrorAction Stop
$testSource = Join-Path $repoRoot 'tests\amd64_context_switch_test.cpp'
$contextSource = Join-Path $repoRoot 'kernel\arch\amd64\context_switch.cpp'
$archSource = Join-Path $repoRoot 'kernel\arch\amd64\arch.cpp'
$assemblySource = Join-Path $repoRoot 'kernel\arch\amd64\context_switch_asm.asm'
$probeSource = Join-Path $repoRoot 'tests\amd64_context_switch_probe.asm'
$assemblyObject = Join-Path $outputPath 'context_switch_asm.obj'
$probeObject = Join-Path $outputPath 'context_switch_probe.obj'
$testExecutable = Join-Path $outputPath 'amd64_context_switch_test.exe'

& $assembler.Source -f win64 $assemblySource -o $assemblyObject
if ($LASTEXITCODE -ne 0) { throw "NASM failed with exit code $LASTEXITCODE" }
& $assembler.Source -f win64 $probeSource -o $probeObject
if ($LASTEXITCODE -ne 0) { throw "NASM test-probe assembly failed with exit code $LASTEXITCODE" }

$compilerArgs = @(
    '-std=c++14', '-O2', '-Wall', '-Wextra', '-DGXOS_BARE_METAL',
    '-mno-red-zone', '-mno-mmx', '-mno-sse', '-mno-sse2',
    '-fno-exceptions', '-fno-rtti',
    '-I', (Join-Path $repoRoot 'kernel\core\include'),
    '-I', (Join-Path $repoRoot 'kernel\arch\amd64\include'),
    '-I', $repoRoot,
    $testSource, $contextSource, $archSource, $assemblyObject, $probeObject,
    '-o', $testExecutable
)
& $compiler.Source @compilerArgs
if ($LASTEXITCODE -ne 0) { throw "g++ failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) {
    throw "AMD64 context switch test failed with check code $LASTEXITCODE (1=small stack, 2=null entry, 3=stack overflow, 4=initial layout, 5=initial switch/GPRs, 6=resume, 7=repeated resume/task GPRs)"
}

Write-Output 'PASS: Microsoft x64 voluntary context entry, stack alignment, shadow space, repeated switches, and 8 nonvolatile GPRs'
Write-Output 'LIMIT: hosted ABI/context test only; no timer preemption, CPL3, FPU state, or guest scheduling claim'
Write-Output "TEST_EXE=$testExecutable"
Write-Output "TEST_EXE_SHA256=$((Get-FileHash -LiteralPath $testExecutable -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Output "CONTEXT_OBJECT=$assemblyObject"
Write-Output "CONTEXT_OBJECT_SHA256=$((Get-FileHash -LiteralPath $assemblyObject -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Output "PROBE_OBJECT=$probeObject"
Write-Output "PROBE_OBJECT_SHA256=$((Get-FileHash -LiteralPath $probeObject -Algorithm SHA256).Hash.ToLowerInvariant())"
