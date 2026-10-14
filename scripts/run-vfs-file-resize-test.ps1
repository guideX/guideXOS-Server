[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path ([IO.Path]::GetTempPath()) "guidexos_vfs_file_resize_test.exe")
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $Compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $Compiler) { throw "g++ was not found." }

$Output = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
$Arguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2", "-DGXOS_HOSTED_TEST",
    "-iquote", "kernel\core\include", "-I", "kernel/arch/amd64/include",
    "tests\vfs_file_resize_test.cpp",
    "kernel\core\block_device.cpp", "kernel\core\fs_fat.cpp",
    "kernel\core\fs_ext4.cpp", "kernel\core\vfs.cpp", "-o", $Output
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "VFS file resize test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "VFS file resize tests failed with exit code $LASTEXITCODE." }
Write-Host "VFS file resize build and test PASS."
