[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PreparedImage,
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$scriptPath = $MyInvocation.MyCommand.Path
$scriptsDirectory = Split-Path -Parent $scriptPath
$repoRoot = Split-Path -Parent $scriptsDirectory
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "out/validation/fs-fat-persistence"
}
Set-Location -LiteralPath $repoRoot

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$imageSource = (Resolve-Path -LiteralPath $PreparedImage -ErrorAction Stop).Path
if ((Get-Item -LiteralPath $imageSource).Length -lt 64MB) {
    throw "Prepared image is smaller than the supported provisioning minimum."
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$testImage = Join-Path $outputRoot "hosted-data-volume-copy.img"
Copy-Item -LiteralPath $imageSource -Destination $testImage -Force

$executable = Join-Path $outputRoot "fs_fat_persistence_test.exe"
$arguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2",
    "-iquote", "kernel/core/include",
    "-Ikernel/arch/amd64/include",
    "tests/fs_fat_persistence_test.cpp",
    "kernel/core/block_device.cpp",
    "kernel/core/fs_fat.cpp",
    "kernel/core/vfs.cpp",
    "-o", $executable
)

Write-Host ("Building hosted test with {0}" -f $compiler.Source)
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "FAT persistence test build failed with exit code $LASTEXITCODE." }

Write-Host "Running actual FAT/VFS sources against a writable image copy."
& $executable $testImage
if ($LASTEXITCODE -ne 0) { throw "FAT persistence test failed with exit code $LASTEXITCODE." }
Write-Host ("Hosted FAT persistence test PASS. Image copy: {0}" -f $testImage)
