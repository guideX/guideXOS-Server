[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = '',
    [string]$DeveloperStudioRoot = 'D:\dev\guideXOS_Developer_Studio'
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase12' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$rootPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Refusing output outside repository out/: $OutputDirectory" }
if (!(Test-Path -LiteralPath (Join-Path $DeveloperStudioRoot 'build.ps1') -PathType Leaf)) { throw "Developer Studio build script not found: $DeveloperStudioRoot" }

& (Join-Path $PSScriptRoot 'build-aarch64-phase10.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $OutputDirectory -Phase12Proof
if ($LASTEXITCODE -ne 0) { throw 'Phase 12 kernel foundation build failed' }

Push-Location $DeveloperStudioRoot
try {
    & .\build.ps1 -TargetArchitecture arm64 -ServerRoot $repoRoot -Phase12Proof
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
