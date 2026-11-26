[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RamdiskImage,
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) "guidexos_release_ramdisk_mount_test"),
    [switch]$FatMountDiagnostics
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $repoRoot

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$imagePath = (Resolve-Path -LiteralPath $RamdiskImage -ErrorAction Stop).Path
if ((Get-Item -LiteralPath $imagePath).Length -lt 512) {
    throw "Ramdisk image is smaller than one sector."
}

$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$executable = Join-Path $outputRoot "release_ramdisk_mount_test.exe"
$arguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2",
    "-iquote", "kernel/core/include",
    "-Ikernel/arch/amd64/include",
    "tests/release_ramdisk_mount_test.cpp",
    "kernel/core/block_device.cpp",
    "kernel/core/ramdisk.cpp",
    "kernel/core/fs_fat.cpp",
    "kernel/core/fs_ext4.cpp",
    "kernel/core/vfs.cpp",
    "-o", $executable
)
if ($FatMountDiagnostics) {
    $arguments += "-DGXOS_FAT_MOUNT_DIAGNOSTICS"
}

Write-Host ("Building release ramdisk mount test with {0}" -f $compiler.Source)
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Release ramdisk mount test build failed with exit code $LASTEXITCODE." }

Write-Host "Running production RAMDISK, FAT, and VFS sources against the supplied image."
& $executable $imagePath
if ($LASTEXITCODE -ne 0) { throw "Release ramdisk mount test failed with exit code $LASTEXITCODE." }
Write-Host "Release ramdisk mount test PASS."
