[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\native-filesystem-contract-test.exe"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$buildText = Get-Content -LiteralPath (Join-Path $Root "build-native-experimental.bat") -Raw
$sourceBlock = [regex]::Match($buildText, 'set SOURCES=\^(?<sources>.*?)(?:\r?\n\r?\nREM Output)', [Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $sourceBlock.Success) { throw "Could not derive the authoritative Server source list." }
$sources = @(
    $sourceBlock.Groups["sources"].Value -split "`r?`n" |
        ForEach-Object { $_.Trim().TrimEnd('^').Trim() } |
        Where-Object { $_ -and $_ -ne "server.cpp" }
)

$output = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$arguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2", "-idirafter", ".",
    "-Ithird_party/mbedtls/include",
    "-Ithird_party/mbedtls/tf-psa-crypto/include",
    "-DGX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION",
    "-DGX_NATIVE_FILESYSTEM_CONTRACT_TEST",
    "tests/native_filesystem_contract_test.cpp"
) + $sources + @(
    "-lws2_32", "-lsecur32", "-lcrypt32", "-lbcrypt", "-lgdi32", "-luser32", "-lmsimg32",
    "-o", $output
)

Write-Host ("Running: {0} {1}" -f $compiler.Source, ($arguments -join " "))
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Native filesystem contract build failed with exit code $LASTEXITCODE." }

& $output
if ($LASTEXITCODE -ne 0) { throw "Native filesystem contract test failed with exit code $LASTEXITCODE." }
Write-Host "Native filesystem contract build and test PASS."
