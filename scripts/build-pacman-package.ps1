<#
.SYNOPSIS
    Build the guideXOS PacMan Native ELF and stage its package in this repo.

.DESCRIPTION
    The PacMan source is kept in the sibling D:\dev\pacman tree, but every
    generated object, ELF, manifest copy, and resource copy is written under
    guideXOSServer.  This keeps the server build reproducible without making
    the external source tree part of the server worktree.
##>

[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [string]$OutputPackage = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$ServerRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = if ([string]::IsNullOrWhiteSpace($env:GUIDEXOS_PACMAN_SOURCE_ROOT)) {
        "D:\dev\pacman\guidexos"
    } else {
        $env:GUIDEXOS_PACMAN_SOURCE_ROOT
    }
}
if ([string]::IsNullOrWhiteSpace($OutputPackage)) {
    $OutputPackage = Join-Path $ServerRoot "Apps\PacMan"
}

$clang = if ([string]::IsNullOrWhiteSpace($env:GUIDEXOS_PACMAN_CLANG)) {
    "C:\Program Files\LLVM\bin\clang++.exe"
} else { $env:GUIDEXOS_PACMAN_CLANG }
$lld = if ([string]::IsNullOrWhiteSpace($env:GUIDEXOS_PACMAN_LLD)) {
    "C:\Program Files\LLVM\bin\ld.lld.exe"
} else { $env:GUIDEXOS_PACMAN_LLD }
$sdkInclude = Join-Path $ServerRoot "sdk\include"
$buildRoot = Join-Path $ServerRoot "build\pacman-native\amd64"
$objectRoot = Join-Path $buildRoot "objects"
$builtElf = Join-Path $buildRoot "pacman.elf"
$outputElf = Join-Path $OutputPackage "bin\amd64\pacman.elf"

function Assert-File([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing $Description`: $Path"
    }
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments, [string]$Description) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

Assert-File $clang "PacMan clang++ compiler"
Assert-File $lld "PacMan lld linker"
Assert-File (Join-Path $sdkInclude "guidexos\abi.h") "guideXOS Native ELF ABI header"

$sourceFiles = @(
    @{ Name = "main"; Path = Join-Path $SourceRoot "src\main.cpp" },
    @{ Name = "game"; Path = Join-Path $SourceRoot "src\game.cpp" },
    @{ Name = "level"; Path = Join-Path $SourceRoot "src\level.cpp" },
    @{ Name = "renderer"; Path = Join-Path $SourceRoot "src\renderer.cpp" },
    @{ Name = "bitmap_loader"; Path = Join-Path $SourceRoot "src\bitmap_loader.cpp" }
)
foreach ($source in $sourceFiles) {
    Assert-File $source.Path "PacMan source file $($source.Name)"
}
Assert-File (Join-Path $SourceRoot "app.json") "PacMan App Model manifest"
Assert-File (Join-Path $SourceRoot "resources\generated\level1.gximg") "PacMan level asset"
Assert-File (Join-Path $SourceRoot "resources\generated\pacpics.gximg") "PacMan sprite asset"

if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $objectRoot -Force | Out-Null

$commonFlags = @(
    "--target=x86_64-unknown-elf",
    "-std=c++11",
    "-ffreestanding",
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-stack-protector",
    "-mno-red-zone",
    "-I$sdkInclude",
    "-I$(Join-Path $SourceRoot 'src')"
)

foreach ($source in $sourceFiles) {
    $object = Join-Path $objectRoot ($source.Name + ".o")
    $flags = @($commonFlags)
    if ($source.Name -eq "main") {
        $flags += "-DPACMAN_ENABLE_DIAGNOSTICS=0"
        $flags += "-DPACMAN_HOSTED_DANGER_TEST=0"
        $flags += "-DPACMAN_HOSTED_RED_MOVEMENT_TEST=0"
        $flags += "-DPACMAN_HOSTED_PINK_MOVEMENT_TEST=0"
        $flags += "-DPACMAN_HOSTED_CYAN_MOVEMENT_TEST=0"
        $flags += "-DPACMAN_HOSTED_ORANGE_MOVEMENT_TEST=0"
        $flags += "-DPACMAN_HOSTED_POWER_PILL_TEST=0"
    }
    Write-Host "      Compiling PacMan $($source.Name).cpp" -ForegroundColor Cyan
    Invoke-Native $clang ($flags + @("-c", $source.Path, "-o", $object)) "PacMan $($source.Name) compile"
}

$objects = $sourceFiles | ForEach-Object { Join-Path $objectRoot ($_.Name + ".o") }
Write-Host "      Linking genuine static AMD64 Native ELF" -ForegroundColor Cyan
Invoke-Native $lld (@("-m", "elf_x86_64", "-static", "-e", "gx_main") + $objects + @("-o", $builtElf)) "PacMan Native ELF link"
Assert-File $builtElf "fresh PacMan Native ELF build output"

New-Item -ItemType Directory -Path (Split-Path -Parent $outputElf), (Join-Path $OutputPackage "resources") -Force | Out-Null
Copy-Item -LiteralPath $builtElf -Destination $outputElf -Force
Copy-Item -LiteralPath (Join-Path $SourceRoot "app.json") -Destination (Join-Path $OutputPackage "app.json") -Force
Copy-Item -LiteralPath (Join-Path $SourceRoot "resources\generated\level1.gximg") -Destination (Join-Path $OutputPackage "resources\level1.gximg") -Force
Copy-Item -LiteralPath (Join-Path $SourceRoot "resources\generated\pacpics.gximg") -Destination (Join-Path $OutputPackage "resources\pacpics.gximg") -Force

$manifest = Get-Content -LiteralPath (Join-Path $OutputPackage "app.json") -Raw | ConvertFrom-Json
$entry = @($manifest.entries | Where-Object { $_.architecture -eq "amd64" })
if ($manifest.id -ne "com.guidexos.pacman" -or $manifest.kind -ne "NativeElf" -or
    $entry.Count -ne 1 -or $entry[0].path -ne "bin/amd64/pacman.elf" -or
    $entry[0].entryPoint -ne "gx_main" -or $entry[0].abi -ne "guidexos-c-abi-v1" -or
    $entry[0].runtime -ne "native-elf") {
    throw "PacMan manifest does not describe the canonical AMD64 NativeElf target."
}

$bytes = [IO.File]::ReadAllBytes($outputElf)
if ($bytes.Length -lt 20 -or $bytes[0] -ne 0x7f -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x4c -or
    $bytes[3] -ne 0x46 -or $bytes[4] -ne 2 -or $bytes[5] -ne 1 -or $bytes[18] -ne 0x3e -or $bytes[19] -ne 0) {
    throw "PacMan output is not a little-endian AMD64 ELF64 executable."
}

$elfHash = (Get-FileHash -LiteralPath $outputElf -Algorithm SHA256).Hash
$manifestHash = (Get-FileHash -LiteralPath (Join-Path $OutputPackage "app.json") -Algorithm SHA256).Hash
Write-Host "      PacMan package staged: $OutputPackage" -ForegroundColor Green
Write-Host "      ELF bytes=$($bytes.Length) sha256=$elfHash" -ForegroundColor Green
Write-Host "      manifest sha256=$manifestHash" -ForegroundColor Green
