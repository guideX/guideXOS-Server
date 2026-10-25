[CmdletBinding()]
param(
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot 'out/comp3-stack-frame-validation'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
$compilerSource = Join-Path $repoRoot 'kernel/core/compiler/compiler_driver.cpp'
$objectPath = Join-Path $OutputDirectory 'compiler_driver-stack-audit.o'
$stackUsagePath = Join-Path $OutputDirectory 'compiler_driver-stack-audit.su'
$arguments = @(
    '-std=c++14', '-Wall', '-Wextra', '-O2', '-ffreestanding',
    '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics', '-fno-use-cxa-atexit',
    '-mno-red-zone', '-fstack-usage', '-DGXOS_BARE_METAL',
    "-I$repoRoot", "-I$(Join-Path $repoRoot 'kernel')",
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')",
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')",
    "-include$(Join-Path $repoRoot 'tests/compiler_driver_host_arch_config.h')",
    '-c', $compilerSource, '-o', $objectPath
)

& $gxx @arguments
if ($LASTEXITCODE -ne 0) { throw 'freestanding compiler-driver stack audit build failed' }
if (-not (Test-Path -LiteralPath $stackUsagePath -PathType Leaf)) {
    throw "compiler stack usage report was not produced: $stackUsagePath"
}

$frameLimitBytes = 24KB
$requiredFrames = @('compile_project_impl', 'load_cached_object')
$records = @(Get-Content -LiteralPath $stackUsagePath)
foreach ($symbol in $requiredFrames) {
    $symbolPattern = [regex]::Escape($symbol) + '\('
    $symbolRecords = @($records | Where-Object { $_ -match $symbolPattern })
    if ($symbolRecords.Count -ne 1) {
        throw "expected one static stack record for $symbol; found $($symbolRecords.Count)"
    }
    if ($symbolRecords[0] -notmatch "`t(\d+)`tstatic$") {
        throw "stack usage for $symbol is not a fixed static frame"
    }
    $frameBytes = [int]$Matches[1]
    if ($frameBytes -gt $frameLimitBytes) {
        throw "$symbol uses $frameBytes bytes; limit is $frameLimitBytes"
    }
    Write-Host "$symbol stack=$frameBytes bytes limit=$frameLimitBytes PASS"
}
