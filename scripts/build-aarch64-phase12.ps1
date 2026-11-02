[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = '',
    [string]$DeveloperStudioRoot = 'D:\dev\guideXOS_Developer_Studio',
    [string]$V05ServerRoot = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase12' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$rootPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Refusing output outside repository out/: $OutputDirectory" }
$DeveloperStudioRoot = [IO.Path]::GetFullPath($DeveloperStudioRoot)
if ([string]::IsNullOrWhiteSpace($V05ServerRoot)) {
    $V05ServerRoot = Join-Path (Split-Path -Parent $repoRoot) 'guideXOSServerV0.5_PHASE4R1_QAC_ADMISSION'
}
$V05ServerRoot = [IO.Path]::GetFullPath($V05ServerRoot)
$preflightRoot = $OutputDirectory.TrimEnd('\') + '-preflight'
$adapterTestRoot = $OutputDirectory.TrimEnd('\') + '-adapter-tests'
$studioBuildRoot = Join-Path $OutputDirectory 'studio-v05-build'
$studioBuildServerRoot = Join-Path $studioBuildRoot 'server-root'
$alignmentLock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'phase12-sdk-alignment.json') | ConvertFrom-Json

if (Test-Path -LiteralPath $OutputDirectory) { throw "Refusing to overwrite existing Phase 12 output: $OutputDirectory" }
foreach ($reserved in @($preflightRoot, $adapterTestRoot)) {
    if (Test-Path -LiteralPath $reserved) { throw "Refusing to overwrite existing Phase 12 preflight output: $reserved" }
}

function Get-GitText([string]$Root, [string[]]$Arguments, [string]$Purpose) {
    $result = & git -C $Root @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "PHASE12_SDK_DEPENDENCY_INVALID: $Purpose; git $($Arguments -join ' ') failed: $($result -join ' ')"
    }
    return (($result -join "`n").Trim())
}

if (!(Test-Path -LiteralPath (Join-Path $DeveloperStudioRoot 'build.ps1') -PathType Leaf)) {
    throw "PHASE12_STUDIO_DEPENDENCY_MISSING: Developer Studio build script not found: $DeveloperStudioRoot"
}
$studioRevision = Get-GitText $DeveloperStudioRoot @('rev-parse', 'HEAD') 'Developer Studio revision lookup'
if ($studioRevision -ne $alignmentLock.developerStudio.revision) {
    throw "PHASE12_STUDIO_REVISION_MISMATCH: expected $($alignmentLock.developerStudio.revision), found $studioRevision. Update the dependency lock only after a new ABI audit."
}
$studioState = Get-GitText $DeveloperStudioRoot @('status', '--porcelain', '--untracked-files=normal') 'Developer Studio worktree check'
if ($studioState.Length -ne 0) { throw 'PHASE12_STUDIO_DEPENDENCY_DIRTY: the pinned Developer Studio checkout must be clean.' }
foreach ($source in $alignmentLock.developerStudio.sources) {
    $pinnedBlob = Get-GitText $DeveloperStudioRoot @('rev-parse', "$($alignmentLock.developerStudio.revision):$($source.path)") 'Developer Studio source pin'
    $workingBlob = Get-GitText $DeveloperStudioRoot @('hash-object', "--path=$($source.path)", '--', $source.path) 'Developer Studio source selection'
    if ($pinnedBlob -ne $source.blob -or $workingBlob -ne $source.blob) {
        throw "PHASE12_STUDIO_SOURCE_MISMATCH: $($source.path) does not match pinned blob $($source.blob)."
    }
}

if (!(Test-Path -LiteralPath $V05ServerRoot -PathType Container)) {
    throw "PHASE12_SDK_DEPENDENCY_MISSING: $($alignmentLock.intendedV05Sdk.repository) revision $($alignmentLock.intendedV05Sdk.contractRevision) is required to reproduce the Developer Studio ABI test. Expected v0.5 SDK root: $V05ServerRoot"
}
$null = Get-GitText $V05ServerRoot @('cat-file', '-e', "$($alignmentLock.intendedV05Sdk.contractRevision)^{commit}") 'v0.5 SDK pinned revision'
foreach ($source in $alignmentLock.intendedV05Sdk.headers) {
    $pinnedBlob = Get-GitText $V05ServerRoot @('rev-parse', "$($alignmentLock.intendedV05Sdk.contractRevision):$($source.path)") 'v0.5 SDK source pin'
    $workingBlob = Get-GitText $V05ServerRoot @('hash-object', "--path=$($source.path)", '--', $source.path) 'v0.5 SDK source selection'
    if ($pinnedBlob -ne $source.blob -or $workingBlob -ne $source.blob) {
        throw "PHASE12_SDK_DEPENDENCY_CONTENT_MISMATCH: $($source.path) does not match pinned v0.5 blob $($source.blob)."
    }
}

$aarch64SdkInclude = Join-Path $repoRoot ($alignmentLock.aarch64ServerSdk.includeRoot.Replace('/', '\'))
$aarch64AbiPath = Join-Path $repoRoot ($alignmentLock.aarch64ServerSdk.abiHeaderPath.Replace('/', '\'))
$aarch64PinnedBlob = Get-GitText $repoRoot @('rev-parse', "$($alignmentLock.aarch64ServerSdk.baselineRevision):$($alignmentLock.aarch64ServerSdk.abiHeaderPath)") 'AARCH64 SDK baseline source'
$aarch64WorkingBlob = Get-GitText $repoRoot @('hash-object', "--path=$($alignmentLock.aarch64ServerSdk.abiHeaderPath)", '--', $alignmentLock.aarch64ServerSdk.abiHeaderPath) 'AARCH64 SDK selected source'
if ($aarch64PinnedBlob -ne $alignmentLock.aarch64ServerSdk.abiHeaderBlob -or $aarch64WorkingBlob -ne $alignmentLock.aarch64ServerSdk.abiHeaderBlob) {
    throw "PHASE12_AARCH64_ABI_BASELINE_CHANGED: expected Git blob $($alignmentLock.aarch64ServerSdk.abiHeaderBlob), baseline=$aarch64PinnedBlob selected=$aarch64WorkingBlob; review and update the ABI contract lock explicitly"
}
$activationConsumer = Get-Content -Raw -LiteralPath (Join-Path $DeveloperStudioRoot 'src\developer_studio_app_activation.cpp')
$activationAbiTest = Get-Content -Raw -LiteralPath (Join-Path $DeveloperStudioRoot 'tests\document_activation_abi_test.cpp')
$aarch64AbiText = Get-Content -Raw -LiteralPath $aarch64AbiPath
if (!$activationConsumer.Contains('get_document_activation_path') -or !$activationAbiTest.Contains('get_document_activation_path') -or
    $aarch64AbiText.Contains('*get_document_activation_path') -or
    $alignmentLock.intendedV05Sdk.bareMetalBuildStartOffset -eq $alignmentLock.aarch64ServerSdk.bareMetalBuildStartOffset) {
    throw 'PHASE12_ABI1_NEGATIVE_CONTROL_INVALID: the locked source/header contracts no longer prove the direct-link incompatibility.'
}

$studioManifest = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'Apps\DeveloperStudio\app.json') | ConvertFrom-Json
$arm64Entry = @($studioManifest.entries | Where-Object { $_.architecture -eq 'arm64' })
if ($arm64Entry.Count -ne 1 -or $arm64Entry[0].abi -ne 'guidexos-c-abi-v1-studio-v05-464') {
    throw 'PHASE12_ADAPTER_IDENTITY_INVALID: the ARM64 package must select the pinned v0.5 464-byte adapter profile explicitly.'
}

# Keep the ABI1 rejection as a mandatory negative control. The alternative path
# is authorized only after both pinned layouts and hosted adapter checks pass.
& (Join-Path $PSScriptRoot 'test-aarch64-phase12-sdk-alignment.ps1') `
    -DeveloperStudioRoot $DeveloperStudioRoot -V05ServerRoot $V05ServerRoot -OutputDirectory $preflightRoot
& (Join-Path $PSScriptRoot 'test-aarch64-studio-v05-adapter.ps1') `
    -DeveloperStudioRoot $DeveloperStudioRoot -V05ServerRoot $V05ServerRoot -OutputDirectory $adapterTestRoot

$v05SdkInclude = Join-Path $V05ServerRoot ($alignmentLock.intendedV05Sdk.includeRoot.Replace('/', '\'))
& (Join-Path $PSScriptRoot 'build-aarch64-phase10.ps1') -LlvmRoot $LlvmRoot `
    -OutputDirectory $OutputDirectory -Phase12Proof -StudioV05SdkInclude $v05SdkInclude

$clang = Join-Path $LlvmRoot 'clang++.exe'
if (!(Test-Path -LiteralPath $clang -PathType Leaf)) {
    throw "PHASE12_BUILD_TOOL_MISSING: selected LLVM clang++ was not found: $clang"
}
New-Item -ItemType Directory -Path (Join-Path $studioBuildServerRoot 'sdk') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $studioBuildServerRoot 'Apps\DeveloperStudio') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $V05ServerRoot 'sdk\include') `
    -Destination (Join-Path $studioBuildServerRoot 'sdk\include') -Recurse
$studioSourceCopy = Join-Path $studioBuildRoot 'pinned-source'
New-Item -ItemType Directory -Path $studioSourceCopy -Force | Out-Null
foreach ($directory in @('src','tests','app')) {
    Copy-Item -LiteralPath (Join-Path $DeveloperStudioRoot $directory) `
        -Destination (Join-Path $studioSourceCopy $directory) -Recurse
}
Copy-Item -LiteralPath (Join-Path $DeveloperStudioRoot 'build.ps1') -Destination $studioSourceCopy
$copiedBuildScript = Join-Path $studioSourceCopy 'build.ps1'
$buildScriptText = [IO.File]::ReadAllText($copiedBuildScript)
$runTestSourceList = '"-Isrc", "src\developer_studio_find.cpp", "src\developer_studio_models.cpp", "src\developer_studio_output.cpp", "src\developer_studio_projects.cpp", "src\developer_studio_run.cpp", "src\developer_studio_syntax.cpp", "tests\run_test.cpp",'
$runTestSourceListFixed = '"-Isrc", "src\developer_studio_find.cpp", "src\developer_studio_models.cpp", "src\developer_studio_output.cpp", "src\developer_studio_build.cpp", "src\developer_studio_projects.cpp", "src\developer_studio_symbols.cpp", "src\developer_studio_workspace.cpp", "src\developer_studio_run.cpp", "src\developer_studio_syntax.cpp", "tests\run_test.cpp",'
$debugTestSourceList = '"-Isrc", "src\developer_studio_find.cpp", "src\developer_studio_syntax.cpp", "src\developer_studio_models.cpp", "src\developer_studio_output.cpp", "src\developer_studio_projects.cpp", "src\developer_studio_run.cpp", "src\developer_studio_debug_symbols.cpp", "src\developer_studio_debug_variables.cpp", "src\developer_studio_debug_watches.cpp", "src\developer_studio_debugger.cpp", "src\developer_studio_debugger_stack.cpp", "tests\debugger_test.cpp",'
$debugTestSourceListFixed = '"-Isrc", "src\developer_studio_find.cpp", "src\developer_studio_syntax.cpp", "src\developer_studio_models.cpp", "src\developer_studio_output.cpp", "src\developer_studio_build.cpp", "src\developer_studio_projects.cpp", "src\developer_studio_symbols.cpp", "src\developer_studio_workspace.cpp", "src\developer_studio_run.cpp", "src\developer_studio_debug_symbols.cpp", "src\developer_studio_debug_variables.cpp", "src\developer_studio_debug_watches.cpp", "src\developer_studio_debugger.cpp", "src\developer_studio_debugger_stack.cpp", "tests\debugger_test.cpp",'
foreach ($sourcePatch in @(
    @{ Old = $runTestSourceList; New = $runTestSourceListFixed; Purpose = 'run-test' },
    @{ Old = $debugTestSourceList; New = $debugTestSourceListFixed; Purpose = 'debugger-test' }
)) {
    if (!$buildScriptText.Contains($sourcePatch.Old) -or
        $buildScriptText.IndexOf($sourcePatch.Old) -ne $buildScriptText.LastIndexOf($sourcePatch.Old)) {
        throw "Pinned Studio $($sourcePatch.Purpose) driver block changed; refusing to patch a different source selection."
    }
    $buildScriptText = $buildScriptText.Replace($sourcePatch.Old, $sourcePatch.New)
}
$sharedDebugSources = '"src\developer_studio_output.cpp", "src\developer_studio_projects.cpp", "src\developer_studio_run.cpp"'
$sharedDebugSourcesFixed = '"src\developer_studio_output.cpp", "src\developer_studio_build.cpp", "src\developer_studio_projects.cpp", "src\developer_studio_symbols.cpp", "src\developer_studio_workspace.cpp", "src\developer_studio_run.cpp"'
if (!$buildScriptText.Contains($sharedDebugSources)) {
    throw 'Pinned Studio debugger test source lists changed; refusing to patch a different source selection.'
}
$buildScriptText = $buildScriptText.Replace($sharedDebugSources, $sharedDebugSourcesFixed)
$studioCompatHeader = @'
#pragma once

// Build-only source compatibility for the pinned v0.5 SDK. The v0.5 table
// renamed the same-signature window destruction slot to native_window_destroy.
// Import the two build helpers that main.cpp otherwise refers to unqualified.
#include "developer_studio_build.h"
using guidexos::developer_studio::ClassifyBuildFailureDetail;
using guidexos::developer_studio::ClassifyBuildDiagnosticLine;

#if defined(GXOS_DEVELOPER_STUDIO_BARE_METAL) && defined(GXOS_DEVELOPER_STUDIO_AARCH64)
#define window_destroy native_window_destroy
#endif
'@
$studioCompatHeaderPath = Join-Path $studioSourceCopy 'src\studio_v05_compile_compat.h'
[IO.File]::WriteAllText($studioCompatHeaderPath, $studioCompatHeader, [Text.UTF8Encoding]::new($false))
$compileFlagsAnchor = 'if ($TargetArchitecture -eq "arm64") { $compileFlags += "-DGXOS_DEVELOPER_STUDIO_AARCH64" }'
$compileFlagsOverlay = $compileFlagsAnchor + "`r`n    if (`$TargetArchitecture -eq `"arm64`" -and `$Phase12Proof) { `$compileFlags += @(`"-include`", `"src\studio_v05_compile_compat.h`" ) }"
if (!$buildScriptText.Contains($compileFlagsAnchor) -or $buildScriptText.IndexOf($compileFlagsAnchor) -ne $buildScriptText.LastIndexOf($compileFlagsAnchor)) {
    throw 'Pinned Studio compiler flag block changed; refusing to apply the v0.5 source compatibility overlay.'
}
$buildScriptText = $buildScriptText.Replace($compileFlagsAnchor, $compileFlagsOverlay)
[IO.File]::WriteAllText($copiedBuildScript, $buildScriptText, [Text.UTF8Encoding]::new($false))
Write-Host '[guideXOS] Building copied pinned Studio sources against the staged v0.5 SDK...' -ForegroundColor Yellow
Push-Location $studioSourceCopy
try {
    & .\build.ps1 -TargetArchitecture arm64 -ServerRoot $studioBuildServerRoot `
        -SdkInclude $v05SdkInclude -ToolchainRoot $LlvmRoot -Phase12Proof
    if ($LASTEXITCODE -ne 0) { throw 'Pinned Developer Studio ARM64 hosted/build/link recipe failed' }
}
finally { Pop-Location }
$studioElf = Join-Path $studioBuildServerRoot 'Apps\DeveloperStudio\bin\arm64\developerstudio.elf'
if (!(Test-Path -LiteralPath $studioElf -PathType Leaf)) { throw 'Authentic Studio target did not produce developerstudio.elf' }
$studioElfBytes = [IO.File]::ReadAllBytes($studioElf)
if ($studioElfBytes.Length -lt 64 -or $studioElfBytes[0] -ne 0x7f -or $studioElfBytes[1] -ne 0x45 -or
    $studioElfBytes[2] -ne 0x4c -or $studioElfBytes[3] -ne 0x46 -or $studioElfBytes[4] -ne 2 -or
    $studioElfBytes[5] -ne 1 -or $studioElfBytes[18] -ne 0xb7 -or $studioElfBytes[19] -ne 0) {
    throw 'Authentic Developer Studio output is not ELF64 little-endian EM_AARCH64.'
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'Apps\DeveloperStudio\app.json') `
    -Destination (Join-Path $studioBuildServerRoot 'Apps\DeveloperStudio\app.json') -Force
Write-Host "[guideXOS] Pinned Studio AARCH64 NativeElf compile/link: PASS ($((Get-Item $studioElf).Length) bytes)" -ForegroundColor Green

$stageRoot = Join-Path $OutputDirectory 'ramdisk-root'
$studioSource = Join-Path $repoRoot 'Apps\DeveloperStudio'
$studioStage = Join-Path $stageRoot 'Apps\DeveloperStudio'
$proofSource = Join-Path $repoRoot 'sdk\samples\phase12_ide_proof'
$proofStage = Join-Path $stageRoot 'Phase12IDEProof'
$packageSkeletonStage = Join-Path $stageRoot 'Apps\P12Proof'
New-Item -ItemType Directory -Path $studioStage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $studioSource 'app.json') -Destination (Join-Path $studioStage 'app.json') -Force
if (Test-Path -LiteralPath (Join-Path $studioSource 'bin\amd64') -PathType Container) {
    Copy-Item -LiteralPath (Join-Path $studioSource 'bin\amd64') -Destination (Join-Path $studioStage 'bin') -Recurse
}
New-Item -ItemType Directory -Path (Join-Path $studioStage 'bin\arm64') -Force | Out-Null
Copy-Item -LiteralPath $studioElf -Destination (Join-Path $studioStage 'bin\arm64\developerstudio.elf')
New-Item -ItemType Directory -Path (Join-Path $proofStage 'app') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $proofStage 'src') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $proofSource 'guidexos.project') -Destination (Join-Path $proofStage 'guidexos.project') -Force
foreach ($required in @('CMakeLists.txt','build.ps1','README.md')) {
    Copy-Item -LiteralPath (Join-Path $proofSource $required) -Destination (Join-Path $proofStage $required) -Force
}
Copy-Item -LiteralPath (Join-Path $proofSource 'app\app.json') -Destination (Join-Path $proofStage 'app\app.json') -Force
Copy-Item -LiteralPath (Join-Path $proofSource 'src\main.c') -Destination (Join-Path $proofStage 'src\main.c') -Force
New-Item -ItemType Directory -Path $packageSkeletonStage -Force | Out-Null
# The guest FAT writer has an 8.3-only create path. Keep the canonical package
# manifest pre-created so the resident compiler overwrites it after publishing
# both freshly built target ELFs; no target ELF is present before the guest build.
Copy-Item -LiteralPath (Join-Path $proofSource 'app\app.json') -Destination (Join-Path $packageSkeletonStage 'app.json') -Force

if (Test-Path -LiteralPath (Join-Path $proofStage 'bin') -PathType Container) { throw 'Phase 12 staging unexpectedly contains target package outputs before the guest build' }
Write-Host '[guideXOS] Phase12 target ELFs absent before guest build: PASS' -ForegroundColor Green

$ramdisk = Join-Path $OutputDirectory 'esp\ramdisk.img'
& (Join-Path $PSScriptRoot 'stage-aarch64-phase5-ramdisk.ps1') -InputRoot $stageRoot -OutputImage $ramdisk
if ($LASTEXITCODE -ne 0) { throw 'Phase 12 FAT32 ramdisk staging failed' }
Copy-Item -LiteralPath (Join-Path $OutputDirectory 'BOOTAA64.EFI') -Destination (Join-Path $OutputDirectory 'esp\EFI\BOOT\BOOTAA64.EFI') -Force
Copy-Item -LiteralPath (Join-Path $OutputDirectory 'kernel.elf') -Destination (Join-Path $OutputDirectory 'esp\kernel.elf') -Force
foreach ($required in @('esp\EFI\BOOT\BOOTAA64.EFI','esp\kernel.elf','esp\ramdisk.img')) {
    if (!(Test-Path -LiteralPath (Join-Path $OutputDirectory $required) -PathType Leaf)) { throw "Phase 12 artifact is missing: $required" }
}
Write-Host '[guideXOS] Phase 12 package staging: PASS' -ForegroundColor Green
Write-Host "      output=$OutputDirectory" -ForegroundColor Cyan
