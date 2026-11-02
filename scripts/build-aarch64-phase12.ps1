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
$alignmentLock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'phase12-sdk-alignment.json') | ConvertFrom-Json

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
if (($activationConsumer.Contains('get_document_activation_path') -or $activationAbiTest.Contains('get_document_activation_path')) -and
    !$aarch64AbiText.Contains('*get_document_activation_path')) {
    throw @"
PHASE12_SDK_CONTRACT_BLOCKED: the pinned Developer Studio source requires get_document_activation_path at offset $($alignmentLock.intendedV05Sdk.documentActivationPathOffset), but the selected AARCH64 Server SDK is the $($alignmentLock.aarch64ServerSdk.hostCallsSize)-byte contract. The v0.5 SDK at $($alignmentLock.intendedV05Sdk.contractRevision) is $($alignmentLock.intendedV05Sdk.hostCallsSize) bytes, but its bare_metal_build_project_start is at $($alignmentLock.intendedV05Sdk.bareMetalBuildStartOffset) while the AARCH64 contract places it at $($alignmentLock.aarch64ServerSdk.bareMetalBuildStartOffset). Passing the v0.5 headers to the AARCH64 application build would shift live host callbacks. No build or package write was started. See docs/aarch64/AARCH64_ABI1_SDK_ALIGNMENT.md.
"@
}

& (Join-Path $PSScriptRoot 'build-aarch64-phase10.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $OutputDirectory -Phase12Proof
if ($LASTEXITCODE -ne 0) { throw 'Phase 12 kernel foundation build failed' }

Push-Location $DeveloperStudioRoot
try {
    & .\build.ps1 -TargetArchitecture arm64 -ServerRoot $repoRoot -SdkInclude $aarch64SdkInclude -Phase12Proof
    if ($LASTEXITCODE -ne 0) { throw 'ARM64 Developer Studio build failed' }
}
finally { Pop-Location }

$stageRoot = Join-Path $OutputDirectory 'ramdisk-root'
$studioSource = Join-Path $repoRoot 'Apps\DeveloperStudio'
$studioStage = Join-Path $stageRoot 'Apps\DeveloperStudio'
$proofSource = Join-Path $repoRoot 'sdk\samples\phase12_ide_proof'
$proofStage = Join-Path $stageRoot 'Phase12IDEProof'
$packageSkeletonStage = Join-Path $stageRoot 'Apps\P12Proof'
New-Item -ItemType Directory -Path $studioStage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $studioSource 'app.json') -Destination (Join-Path $studioStage 'app.json') -Force
Copy-Item -LiteralPath (Join-Path $studioSource 'bin') -Destination $studioStage -Recurse -Force
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
