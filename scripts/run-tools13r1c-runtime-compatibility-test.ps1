[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ServerRoot,
    [Parameter(Mandatory = $true)]
    [string]$AcceptedElf
)

$ErrorActionPreference = 'Stop'
$ServerRoot = [IO.Path]::GetFullPath($ServerRoot)
$AcceptedElf = [IO.Path]::GetFullPath($AcceptedElf)
$expectedHash = '9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16'
$expectedBytes = 1262228

if (-not (Test-Path -LiteralPath $ServerRoot -PathType Container)) {
    throw "Server checkout not found: $ServerRoot"
}
if (-not (Test-Path -LiteralPath $AcceptedElf -PathType Leaf)) {
    throw "Accepted ELF not found: $AcceptedElf"
}
$artifact = Get-Item -LiteralPath $AcceptedElf
$artifactHash = (Get-FileHash -LiteralPath $AcceptedElf -Algorithm SHA256).Hash
if ($artifact.Length -ne $expectedBytes -or $artifactHash -ne $expectedHash) {
    throw "Accepted artifact identity mismatch: bytes=$($artifact.Length) SHA256=$artifactHash"
}

$gxxCommand = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $gxxCommand -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $gxxCommand = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $gxxCommand) { throw 'g++.exe was not found.' }
$gxx = $gxxCommand.Source

$outputDirectory = Join-Path (Join-Path ([IO.Path]::GetTempPath()) 'TOOLS13R1C') ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
Write-Host "Isolated hosted test output: $outputDirectory"

$abiTest = Join-Path $outputDirectory 'native_abi_layout_test.exe'
$abiSources = @(
    '-std=c++17', '-Wall', '-Wextra', '-O2',
    "-I$(Join-Path $ServerRoot 'sdk\include')",
    (Join-Path $ServerRoot 'tests\native_abi_layout_test.cpp'),
    '-o', $abiTest
)
& $gxx @abiSources
if ($LASTEXITCODE -ne 0) { throw "Server ABI layout test build failed: $LASTEXITCODE" }
& $abiTest
if ($LASTEXITCODE -ne 0) { throw "Server ABI layout test failed: $LASTEXITCODE" }

$elfTest = Join-Path $outputDirectory 'tools13r1c_accepted_elf_test.exe'
$elfTestSource = Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\TOOLS13R1C_native_elf_compatibility_host_test.cpp'
$validatorSource = Join-Path $ServerRoot 'kernel\core\native_elf\native_elf_validator.cpp'
$elfSources = @(
    '-std=c++14', '-Wall', '-Wextra', '-O2', '-DGXOS_BARE_METAL',
    "-I$(Join-Path $ServerRoot 'kernel')",
    "-iquote$(Join-Path $ServerRoot 'kernel\core\include')",
    "-I$(Join-Path $ServerRoot 'sdk\include')",
    $elfTestSource, $validatorSource, '-o', $elfTest
)
& $gxx @elfSources
if ($LASTEXITCODE -ne 0) { throw "Accepted ELF compatibility test build failed: $LASTEXITCODE" }
& $elfTest $AcceptedElf
if ($LASTEXITCODE -ne 0) { throw "Accepted ELF compatibility test failed: $LASTEXITCODE" }

Write-Host 'TOOLS13R1C hosted ELF/ABI checks PASS; no application execution was performed.'
