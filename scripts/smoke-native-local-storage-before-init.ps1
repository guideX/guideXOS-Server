[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
    [IO.Path]::GetFullPath($RepoRoot)
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root "out\runtime\native-local-storage-before-init"
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compiler) {
    Write-Host "FLS-before-init compiler: FAIL"
    exit 1
}

$exe = Join-Path $OutputRoot "guidexos_local_storage_before_init_tests.exe"
$log = Join-Path $OutputRoot "run.log"
$source = Join-Path $root "runtime\tests\guidexos_local_storage_before_init_tests.cpp"
$manager = Join-Path $root "runtime\local_storage\guidexos_local_storage.cpp"

$previous = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$buildOutput = @(& $compiler.Source -std=c++17 -O2 -Wall -Wextra -Wpedantic `
    -iquote $root $manager $source -pthread -o $exe 2>&1 |
    ForEach-Object { $_.ToString() })
$buildCode = $LASTEXITCODE
$ErrorActionPreference = $previous
$buildOutput | Set-Content -LiteralPath $log -Encoding UTF8
if ($buildCode -ne 0) {
    $buildOutput | ForEach-Object { Write-Host $_ }
    Write-Host "FLS-before-init build: FAIL"
    exit $buildCode
}

$previous = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$runOutput = @(& $exe 2>&1 | ForEach-Object { $_.ToString() })
$runCode = $LASTEXITCODE
$ErrorActionPreference = $previous
$runOutput | Add-Content -LiteralPath $log -Encoding UTF8
$required = @(
    "Allocate before initialization: PASS",
    "Get before initialization: PASS",
    "Set before initialization: PASS",
    "Release before initialization: PASS",
    "Thread attach before initialization: PASS",
    "Shutdown before initialization: PASS",
    "No hidden pre-init manager state: PASS",
    "Initialized lifecycle recovery: PASS",
    "FLS-before-init: ALL_PASS"
)
foreach ($marker in $required) {
    Write-Host "$marker"
}
if ($runCode -ne 0 -or @($required | Where-Object { $runOutput -notcontains $_ }).Count -ne 0) {
    Write-Host "FLS-before-init harness: FAIL"
    exit 1
}
Write-Host "FLS-before-init harness: PASS"
exit 0
