[CmdletBinding()]
param(
    [string]$OutputRoot = "out\input-reliability-qemu-qualified"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$EspOutput = Join-Path $OutputRoot "ESP"
$KernelSource = Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
$BootloaderSource = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$EspSource = Join-Path $Root "ESP"
$AudioBeepSource = Join-Path $Root "Apps\AudioBeep"
$AudioBeepManifestSource = Join-Path $AudioBeepSource "app.json"
$AudioBeepElfSource = Join-Path $AudioBeepSource "bin\amd64\audiobeep.elf"

if (Test-Path -LiteralPath $OutputRoot) {
    throw "Refusing to overwrite existing qualification output: $OutputRoot"
}
foreach ($required in @($EspSource, $KernelSource, $BootloaderSource,
                       $AudioBeepManifestSource, $AudioBeepElfSource)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required input reliability image input is missing: $required"
    }
}

New-Item -ItemType Directory -Path $OutputRoot | Out-Null
Copy-Item -LiteralPath $EspSource -Destination $EspOutput -Recurse
$BootDirectory = Join-Path $EspOutput "EFI\BOOT"
New-Item -ItemType Directory -Path $BootDirectory -Force | Out-Null
$BootloaderOutput = Join-Path $BootDirectory "BOOTX64.EFI"
$KernelOutput = Join-Path $EspOutput "kernel.elf"
Copy-Item -LiteralPath $BootloaderSource -Destination $BootloaderOutput
Copy-Item -LiteralPath $KernelSource -Destination $KernelOutput -Force
$AudioBeepOutput = Join-Path $EspOutput "Apps\AudioBeep"
if (Test-Path -LiteralPath $AudioBeepOutput) {
    throw "Refusing to replace an existing AudioBeep package in the baseline ESP: $AudioBeepOutput"
}
Copy-Item -LiteralPath $AudioBeepSource -Destination $AudioBeepOutput -Recurse

$SourceCommit = (& git rev-parse HEAD).Trim()
$SourceBranch = (& git branch --show-current).Trim()
$TrackedChanges = @(& git status --porcelain --untracked-files=no)
$UntrackedFiles = @(& git ls-files --others --exclude-standard)
$SourceWorkingTreeState = if ($TrackedChanges.Count -gt 0) { "tracked_modified" } else { "tracked_clean" }
$KernelHash = (Get-FileHash -LiteralPath $KernelOutput -Algorithm SHA256).Hash.ToLowerInvariant()
$BootloaderHash = (Get-FileHash -LiteralPath $BootloaderOutput -Algorithm SHA256).Hash.ToLowerInvariant()
$AudioBeepManifestHash = (Get-FileHash -LiteralPath (Join-Path $AudioBeepOutput "app.json") -Algorithm SHA256).Hash.ToLowerInvariant()
$AudioBeepElfHash = (Get-FileHash -LiteralPath (Join-Path $AudioBeepOutput "bin\amd64\audiobeep.elf") -Algorithm SHA256).Hash.ToLowerInvariant()
$Identity = @(
    "identity=guideXOS Server SIR2 Input Reliability"
    "qualificationStatus=staged_not_guest_qualified"
    "sourceBranch=$SourceBranch"
    "sourceCommit=$SourceCommit"
    "sourceWorkingTreeState=$SourceWorkingTreeState"
    "sourceUntrackedFileCount=$($UntrackedFiles.Count)"
    "sourceBaseline=080928710398ec02f728e573838d76fc0b0bfe49"
    "architecture=amd64"
    "keyboardInterface=PS/2 Set 2, data=0x60, status=0x64, IRQ1 keyboard, IRQ12 mouse with keyboard-byte routing"
    "kernelSha256=$KernelHash"
    "bootloaderSha256=$BootloaderHash"
    "audioBeepManifestSha256=$AudioBeepManifestHash"
    "audioBeepElfSha256=$AudioBeepElfHash"
    "imageRoot=$EspOutput"
    "qemuOrPhysicalTest=not_run"
    "pacmanPackage=baseline ESP package copied without modification"
)
[IO.File]::WriteAllLines((Join-Path $EspOutput "input-reliability-build-identity.txt"), $Identity, [Text.Encoding]::ASCII)

Write-Output "INPUT_RELIABILITY_IMAGE=PASS"
Write-Output "INPUT_RELIABILITY_IMAGE_ESP=$EspOutput"
Write-Output "INPUT_RELIABILITY_KERNEL_SHA256=$KernelHash"
Write-Output "INPUT_RELIABILITY_BOOTLOADER_SHA256=$BootloaderHash"
Write-Output "INPUT_RELIABILITY_AUDIO_BEEP_MANIFEST_SHA256=$AudioBeepManifestHash"
Write-Output "INPUT_RELIABILITY_AUDIO_BEEP_ELF_SHA256=$AudioBeepElfHash"
Write-Output "INPUT_RELIABILITY_BUILD_IDENTITY=$(Join-Path $EspOutput 'input-reliability-build-identity.txt')"
