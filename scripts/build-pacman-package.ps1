<#
.SYNOPSIS
    Build the guideXOS PacMan Native ELF and stage its package in this repo.

.DESCRIPTION
    PacMan source is exported from the pinned revision in
    scripts\pacman-revision.txt. Generated objects and evidence go into a
    fresh run directory under ArtifactRoot (the Server build directory by
    default); the staged package goes to OutputPackage.
##>

[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [string]$OutputPackage = "",
    [string]$ArtifactRoot = "",
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
if ([string]::IsNullOrWhiteSpace($ArtifactRoot)) {
    $ArtifactRoot = if ([string]::IsNullOrWhiteSpace($env:GUIDEXOS_PACMAN_ARTIFACT_ROOT)) {
        Join-Path $ServerRoot "build\pacman-native\amd64"
    } else {
        $env:GUIDEXOS_PACMAN_ARTIFACT_ROOT
    }
}

$clang = if ([string]::IsNullOrWhiteSpace($env:GUIDEXOS_PACMAN_CLANG)) {
    "C:\Program Files\LLVM\bin\clang++.exe"
} else { $env:GUIDEXOS_PACMAN_CLANG }
$lld = if ([string]::IsNullOrWhiteSpace($env:GUIDEXOS_PACMAN_LLD)) {
    "C:\Program Files\LLVM\bin\ld.lld.exe"
} else { $env:GUIDEXOS_PACMAN_LLD }
$llvmNm = if ([string]::IsNullOrWhiteSpace($env:GUIDEXOS_PACMAN_NM)) {
    Join-Path (Split-Path -Parent $clang) "llvm-nm.exe"
} else { $env:GUIDEXOS_PACMAN_NM }
$sdkInclude = Join-Path $ServerRoot "sdk\include"
$runId = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$buildRoot = Join-Path $ArtifactRoot ("run-" + $runId)
$objectRoot = Join-Path $buildRoot "objects"
$builtElf = Join-Path $buildRoot "pacman.elf"
$outputElf = Join-Path $OutputPackage "bin\amd64\pacman.elf"
$revisionFile = Join-Path $PSScriptRoot "pacman-revision.txt"

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
Assert-File $llvmNm "PacMan LLVM symbol inspector"
Assert-File (Join-Path $sdkInclude "guidexos\abi.h") "guideXOS Native ELF ABI header"
Assert-File $revisionFile "pinned PacMan source revision"

$pinnedRevision = (Get-Content -LiteralPath $revisionFile -Raw).Trim()
if ($pinnedRevision -notmatch '^[0-9a-fA-F]{40}$') {
    throw "Pinned PacMan revision must be a full 40-character commit SHA: $revisionFile"
}
if (!(Test-Path -LiteralPath $SourceRoot -PathType Container)) {
    throw "PacMan source directory not found: $SourceRoot"
}
$repositoryRoot = & git -C $SourceRoot rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace(($repositoryRoot | Out-String).Trim())) {
    throw "PacMan source is not inside a Git repository: $SourceRoot"
}
$repositoryRoot = ($repositoryRoot | Out-String).Trim()
$resolvedRevision = & git -C $repositoryRoot rev-parse --verify "$pinnedRevision^{commit}" 2>$null
if ($LASTEXITCODE -ne 0 -or ($resolvedRevision | Out-String).Trim() -ne $pinnedRevision.ToLowerInvariant()) {
    throw "Required PacMan commit $pinnedRevision is unavailable in '$repositoryRoot'. Obtain it locally; normal builds do not fetch from the network."
}
$siblingHead = & git -C $repositoryRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read PacMan repository HEAD: $repositoryRoot"
}

$repositoryRootFull = [System.IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\')
$sourceRootFull = [System.IO.Path]::GetFullPath($SourceRoot).TrimEnd('\')
if ($sourceRootFull.Equals($repositoryRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    $sourceSubdirectory = ""
} elseif ($sourceRootFull.StartsWith($repositoryRootFull + "\", [System.StringComparison]::OrdinalIgnoreCase)) {
    $sourceSubdirectory = $sourceRootFull.Substring($repositoryRootFull.Length).TrimStart('\')
} else {
    throw "PacMan source directory is not within its Git repository root: $SourceRoot"
}

# Each invocation gets a fresh run directory. Keep prior objects, source
# archives, and validation evidence even when the top-level build is clean.
if (Test-Path -LiteralPath $buildRoot) {
    throw "PacMan build run directory already exists; preserve it and retry: $buildRoot"
}
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$sourceArchive = Join-Path $buildRoot "pacman-source-$pinnedRevision.zip"
$sourceStageRoot = Join-Path $buildRoot "source-$pinnedRevision"
Write-Host "      PacMan pinned revision: $pinnedRevision" -ForegroundColor Cyan
Write-Host "      PacMan repository HEAD: $(($siblingHead | Out-String).Trim())" -ForegroundColor DarkGray
Write-Host "      Exporting committed PacMan source to: $sourceStageRoot" -ForegroundColor Cyan
Invoke-Native "git" @("-C", $repositoryRoot, "archive", "--format=zip", "--output=$sourceArchive", $pinnedRevision) "PacMan pinned source export"
Expand-Archive -LiteralPath $sourceArchive -DestinationPath $sourceStageRoot
$sourceArchiveHash = (Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash
$SourceRoot = if ([string]::IsNullOrWhiteSpace($sourceSubdirectory)) {
    $sourceStageRoot
} else {
    Join-Path $sourceStageRoot $sourceSubdirectory
}
Assert-File (Join-Path $SourceRoot "src\main.cpp") "PacMan main source at pinned revision"

$registrationModule = Join-Path $PSScriptRoot "pacman-source-registration.psm1"
Assert-File $registrationModule "PacMan source registration rules"
Import-Module -Name $registrationModule -Force
$registration = Get-PacManSourceRegistration -SourceRoot $SourceRoot
$sourceFiles = @($registration.Sources)
foreach ($source in $sourceFiles) {
    Assert-File $source.Path "PacMan source file $($source.Name)"
}
Assert-File (Join-Path $SourceRoot "app.json") "PacMan App Model manifest"
Assert-File (Join-Path $SourceRoot "resources\generated\level1.gximg") "PacMan level asset"
Assert-File (Join-Path $SourceRoot "resources\generated\pacpics.gximg") "PacMan sprite asset"

$audioResourceNames = @(
    "EatPill.wav",
    "StartMusic.wav",
    "extralife.wav",
    "fruiteat.wav",
    "ghosteat.wav",
    "killed.wav",
    "startmusicold.wav"
)
if ($registration.AudioRequired) {
    foreach ($audioResourceName in $audioResourceNames) {
        Assert-File (Join-Path $sourceStageRoot $audioResourceName) "PacMan audio resource $audioResourceName"
    }
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

$compileEvidence = @()
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
    $compileArguments = $flags + @("-c", $source.Path, "-o", $object)
    Write-Host "        $clang $($compileArguments -join ' ')" -ForegroundColor DarkGray
    Invoke-Native $clang $compileArguments "PacMan $($source.Name) compile"
    $compileEvidence += [pscustomobject]@{
        name = $source.Name
        sourcePath = $source.Path
        sourceSha256 = (Get-FileHash -LiteralPath $source.Path -Algorithm SHA256).Hash
        objectPath = $object
        objectSha256 = (Get-FileHash -LiteralPath $object -Algorithm SHA256).Hash
        arguments = @($compileArguments)
        exitCode = $LASTEXITCODE
    }
}

$objects = $sourceFiles | ForEach-Object { Join-Path $objectRoot ($_.Name + ".o") }
if (@($objects | Select-Object -Unique).Count -ne @($objects).Count) {
    throw "PacMan source registration contains duplicate object paths."
}
Write-Host "      Linking genuine static AMD64 Native ELF" -ForegroundColor Cyan
$linkArguments = @("-m", "elf_x86_64", "-static", "-e", "gx_main") + $objects + @("-o", $builtElf)
Write-Host "        $lld $($linkArguments -join ' ')" -ForegroundColor DarkGray
Invoke-Native $lld $linkArguments "PacMan Native ELF link"
$linkExitCode = $LASTEXITCODE
Assert-File $builtElf "fresh PacMan Native ELF build output"

New-Item -ItemType Directory -Path (Split-Path -Parent $outputElf), (Join-Path $OutputPackage "resources") -Force | Out-Null
Copy-Item -LiteralPath $builtElf -Destination $outputElf -Force
Copy-Item -LiteralPath (Join-Path $SourceRoot "app.json") -Destination (Join-Path $OutputPackage "app.json") -Force
Copy-Item -LiteralPath (Join-Path $SourceRoot "resources\generated\level1.gximg") -Destination (Join-Path $OutputPackage "resources\level1.gximg") -Force
Copy-Item -LiteralPath (Join-Path $SourceRoot "resources\generated\pacpics.gximg") -Destination (Join-Path $OutputPackage "resources\pacpics.gximg") -Force
if ($registration.AudioRequired) {
    $outputAudioRoot = Join-Path $OutputPackage "resources\audio"
    New-Item -ItemType Directory -Path $outputAudioRoot -Force | Out-Null
    foreach ($audioResourceName in $audioResourceNames) {
        Copy-Item -LiteralPath (Join-Path $sourceStageRoot $audioResourceName) -Destination (Join-Path $outputAudioRoot $audioResourceName) -Force
    }
}

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
    $bytes[3] -ne 0x46 -or $bytes[4] -ne 2 -or $bytes[5] -ne 1 -or $bytes[16] -ne 2 -or $bytes[17] -ne 0 -or
    $bytes[18] -ne 0x3e -or $bytes[19] -ne 0) {
    throw "PacMan output is not a little-endian AMD64 ELF64 executable."
}

$definedSymbols = & $llvmNm --defined-only --demangle $outputElf
if ($LASTEXITCODE -ne 0) {
    throw "PacMan ELF symbol inspection failed with exit code $LASTEXITCODE"
}
if (-not (@($definedSymbols) | Where-Object { $_ -match '\bgx_main\b' })) {
    throw "PacMan Native ELF does not define the expected gx_main entry point."
}
$undefinedSymbols = @(& $llvmNm --undefined-only --demangle $outputElf)
if ($LASTEXITCODE -ne 0) {
    throw "PacMan ELF undefined-symbol inspection failed with exit code $LASTEXITCODE"
}
if ($undefinedSymbols.Count -gt 0) {
    throw "PacMan Native ELF has unresolved symbols: $($undefinedSymbols -join '; ')"
}
if ($registration.AudioRequired) {
    foreach ($audioSymbol in @("pacman_audio_load_resources", "pacman_audio_submit")) {
        if (-not (@($definedSymbols) | Where-Object { $_ -match "\b$audioSymbol\b" })) {
            throw "PacMan Native ELF does not define required audio symbol $audioSymbol."
        }
    }
    foreach ($audioResourceName in $audioResourceNames) {
        $sourceAudio = Join-Path $sourceStageRoot $audioResourceName
        $packageAudio = Join-Path $OutputPackage ("resources\audio\" + $audioResourceName)
        if ((Get-FileHash -LiteralPath $sourceAudio -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $packageAudio -Algorithm SHA256).Hash) {
            throw "PacMan audio resource mismatch after packaging: $audioResourceName"
        }
    }
}

$elfHash = (Get-FileHash -LiteralPath $outputElf -Algorithm SHA256).Hash
$manifestHash = (Get-FileHash -LiteralPath (Join-Path $OutputPackage "app.json") -Algorithm SHA256).Hash
$audioResourceEvidence = @()
if ($registration.AudioRequired) {
    foreach ($audioResourceName in $audioResourceNames) {
        $packageAudio = Join-Path $OutputPackage ("resources\audio\" + $audioResourceName)
        $audioResourceEvidence += [pscustomobject]@{
            name = $audioResourceName
            path = $packageAudio
            sha256 = (Get-FileHash -LiteralPath $packageAudio -Algorithm SHA256).Hash
        }
    }
}
$evidence = [ordered]@{
    sourceRevision = $pinnedRevision
    sourceRepositoryHead = ($siblingHead | Out-String).Trim()
    sourceArchive = $sourceArchive
    sourceArchiveSha256 = $sourceArchiveHash
    compiler = $clang
    compilerFlags = @($commonFlags)
    compiledSources = @($compileEvidence)
    linker = $lld
    linkArguments = @($linkArguments)
    linkExitCode = $linkExitCode
    outputElf = $outputElf
    outputElfSha256 = $elfHash
    outputElfBytes = $bytes.Length
    entryPoint = "gx_main"
    undefinedSymbols = @($undefinedSymbols)
    audioRequired = [bool]$registration.AudioRequired
    audioDefinitions = @($definedSymbols | Where-Object { $_ -match 'pacman_audio_(load_resources|submit)' })
    audioResources = @($audioResourceEvidence)
    manifest = (Join-Path $OutputPackage "app.json")
    manifestSha256 = $manifestHash
}
$evidencePath = Join-Path $buildRoot "build-evidence.json"
$evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
Write-Host "      PacMan package staged: $OutputPackage" -ForegroundColor Green
Write-Host "      PacMan source revision: $pinnedRevision" -ForegroundColor Green
Write-Host "      PacMan registered sources: $(($sourceFiles | ForEach-Object { $_.Name }) -join ', ')" -ForegroundColor Green
Write-Host "      PacMan audio resource set included: $($registration.AudioRequired)" -ForegroundColor Green
Write-Host "      ELF bytes=$($bytes.Length) sha256=$elfHash" -ForegroundColor Green
Write-Host "      manifest sha256=$manifestHash" -ForegroundColor Green
Write-Host "      build evidence: $evidencePath" -ForegroundColor Green
