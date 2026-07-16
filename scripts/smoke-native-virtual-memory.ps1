$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $root 'out\runtime\native-virtual-memory'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

function Status([string]$Name, [string]$Value) {
    Write-Host "${Name}: $Value"
}

function Invoke-Compiler([string]$Compiler, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = @(& $Compiler @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($exitCode -ne 0) { $output | ForEach-Object { Write-Host $_ } }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function ContainsPass([string[]]$Lines, [string]$Label) {
    return $Lines -contains "${Label}: PASS"
}

$compilerCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    Status 'Hosted generic VM build' 'FAIL'
    Status 'Hosted generic VM run' 'FAIL'
    Status 'Inactive NativeAOT VM adapter probe' 'FAIL'
    exit 1
}
$compiler = $compilerCommand.Source
$quoteRoot = @('-iquote', $root)

$genericExe = Join-Path $outputRoot 'guidexos_virtual_memory_tests.exe'
$genericBuild = Invoke-Compiler $compiler (@(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\memory\guidexos_virtual_memory_region.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_virtual_memory_tests.cpp'),
    '-pthread', '-o', $genericExe
))
Status 'Hosted generic VM build' $(if ($genericBuild.ExitCode -eq 0) { 'PASS' } else { 'FAIL' })

$genericOutput = @()
$genericRun = $false
if ($genericBuild.ExitCode -eq 0) {
    $genericOutput = @(& $genericExe 2>&1 | ForEach-Object { $_.ToString() })
    $genericRun = $LASTEXITCODE -eq 0
}
$genericLabels = @(
    'Page and granularity', 'Reserve', 'Query reservation', 'Commit',
    'Zero initialization', 'Read/write access', 'Decommit', 'Recommit zeroing',
    'Partial commit', 'Partial decommit', 'Protection transitions',
    'Protection enforcement', 'Alignment and validation', 'Overlap rejection',
    'Release', 'Double release', 'Stale region usage', 'Query after release',
    'Range reuse', 'Repeated cleanup cycles', 'No leaked reservation or committed metadata'
)
foreach ($label in $genericLabels) {
    Status $label $(if ($genericRun -and (ContainsPass $genericOutput $label)) { 'PASS' } else { 'FAIL' })
}
Status 'Expected-fault guard test' 'BLOCKED (no safe hosted fault harness in this probe)'
Status 'Hosted generic VM run' $(if ($genericRun) { 'PASS' } else { 'FAIL' })

$adapterExe = Join-Path $outputRoot 'guidexos_nativeaot_virtual_memory_adapter_probe.exe'
$adapterBuild = Invoke-Compiler $compiler (@(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\memory\guidexos_virtual_memory_region.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_virtual_memory_adapter.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_nativeaot_virtual_memory_adapter_probe.cpp'),
    '-pthread', '-o', $adapterExe
))
$adapterOutput = @()
$adapterRun = $false
if ($adapterBuild.ExitCode -eq 0) {
    $adapterOutput = @(& $adapterExe 2>&1 | ForEach-Object { $_.ToString() })
    $adapterRun = $LASTEXITCODE -eq 0
}
foreach ($label in @('Page size', 'Allocation granularity', 'Adapter reserve',
                     'Adapter commit/zero', 'Adapter decommit/recommit',
                     'Adapter reset classification', 'Adapter release',
                     'Adapter stale release')) {
    Status $label $(if ($adapterRun -and (ContainsPass $adapterOutput $label)) { 'PASS' } else { 'FAIL' })
}
Status 'Inactive NativeAOT VM adapter probe' $(if ($adapterRun) { 'PASS' } else { 'FAIL' })

$genericHeader = Join-Path $root 'runtime\memory\guidexos_virtual_memory_region.h'
$genericCoupling = -not [bool](Select-String -LiteralPath $genericHeader -Pattern '(?i)\.NET|NativeAOT|\bGC\b|Win32|HostLogProof' -Quiet)
Status 'Generic API runtime-neutral coupling' $(if ($genericCoupling) { 'PASS' } else { 'FAIL' })

if (-not ($genericBuild.ExitCode -eq 0 -and $genericRun -and
          $adapterBuild.ExitCode -eq 0 -and $adapterRun -and $genericCoupling)) {
    exit 1
}
exit 0
