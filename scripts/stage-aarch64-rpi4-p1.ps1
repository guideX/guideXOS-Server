[CmdletBinding()]
param(
    [string]$ArtifactDirectory = '',
    [string]$OutputDirectory = '',
    [string]$FirmwareDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    $ArtifactDirectory = Join-Path $repoRoot 'out\aarch64-rpi4-p1'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = $ArtifactDirectory
}
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
foreach ($path in @($ArtifactDirectory, $OutputDirectory)) {
    if (!$path.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing staging path outside repository out/: $path"
    }
}
$efiSource = Join-Path $ArtifactDirectory 'BOOTAA64.EFI'
$kernelSource = Join-Path $ArtifactDirectory 'kernel.elf'
foreach ($path in @($efiSource, $kernelSource)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Physical P1 artifact is missing: $path"
    }
}
if (!(Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}
$boot = Join-Path $OutputDirectory 'boot'
$outputPrefix = ([IO.Path]::GetFullPath($OutputDirectory)).TrimEnd('\') + '\'
function Get-StageRelativePath([string]$Path) {
    return $Path.Substring($outputPrefix.Length).Replace('\', '/')
}
if (Test-Path -LiteralPath $boot) {
    Remove-Item -LiteralPath $boot -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $boot 'EFI\BOOT') -Force | Out-Null

if (![string]::IsNullOrWhiteSpace($FirmwareDirectory)) {
    $FirmwareDirectory = [IO.Path]::GetFullPath($FirmwareDirectory)
    if (!(Test-Path -LiteralPath $FirmwareDirectory -PathType Container)) {
        throw "Firmware directory not found: $FirmwareDirectory"
    }
    # This copies into staging only. The supplied firmware tree is never
    # modified, flashed, or treated as an implicit download source.
    Copy-Item -Path (Join-Path $FirmwareDirectory '*') -Destination $boot -Recurse -Force
}

Copy-Item -LiteralPath $efiSource -Destination (Join-Path $boot 'EFI\BOOT\BOOTAA64.EFI') -Force
Copy-Item -LiteralPath $kernelSource -Destination (Join-Path $boot 'kernel.elf') -Force

$manifestLines = @(
    '# guideXOS AARCH64-P1 copy manifest',
    '# Copy these staged files to the root of an already prepared FAT32 UEFI boot filesystem.',
    '# This staging script does not format, partition, write, or modify removable media.',
    'boot/EFI/BOOT/BOOTAA64.EFI',
    'boot/kernel.elf'
)
if (![string]::IsNullOrWhiteSpace($FirmwareDirectory)) {
    $manifestLines += '# Firmware files below were copied from the explicitly supplied FirmwareDirectory:'
    $firmwareFiles = Get-ChildItem -LiteralPath $boot -File -Recurse |
        Where-Object { $_.FullName -notmatch '\\EFI\\BOOT\\BOOTAA64\.EFI$' -and $_.FullName -notmatch '\\kernel\.elf$' } |
        Sort-Object FullName
    foreach ($file in $firmwareFiles) {
        $relative = Get-StageRelativePath $file.FullName
        $manifestLines += $relative
    }
}
$manifestLines | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.txt') -Encoding utf8

$hashLines = @('# SHA-256 hashes for the deterministic P1 staging tree')
Get-ChildItem -LiteralPath $boot -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative = Get-StageRelativePath $_.FullName
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashLines += "$hash  $relative"
}
$hashLines | Set-Content -LiteralPath (Join-Path $OutputDirectory 'hashes.txt') -Encoding ascii

$firmwareText = if ([string]::IsNullOrWhiteSpace($FirmwareDirectory)) {
    'FirmwareDirectory: not supplied; no firmware was copied or downloaded.'
} else {
    "FirmwareDirectory: $FirmwareDirectory"
}
@(
    '# guideXOS AARCH64-P1 firmware provenance',
    $firmwareText,
    'UEFI implementation: externally selected PFTF Raspberry Pi 4 UEFI; record exact release before testing.',
    'EEPROM/bootloader policy: not changed by guideXOS tooling.',
    'The hashes.txt file covers every file present in the staged boot tree.'
) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'firmware-provenance.txt') -Encoding utf8

@(
    '# guideXOS AARCH64-P1 physical test checklist',
    '',
    '1. Power the Raspberry Pi off.',
    '2. Prepare a FAT32 UEFI boot medium independently of this repository.',
    '3. Install/preserve the selected PFTF UEFI files; do not upgrade Pi EEPROM automatically.',
    '4. Copy the files listed in manifest.txt from boot/ to the prepared FAT32 filesystem.',
    '5. Connect HDMI.',
    '6. Connect a USB-TTL 3.3-V serial adapter if available; never use RS-232 voltage levels.',
    '7. Insert the boot medium.',
    '8. Power on.',
    '9. Record the deepest exact P1 stage reached.',
    '10. Photograph the screen if boot stops.',
    '11. Save the complete serial log if available.',
    '12. Repeat only after recording the evidence from the prior boot.',
    '',
    'The P1 toolchain never formats, repartitions, writes a removable disk, or modifies EEPROM firmware.'
) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'PHYSICAL_TEST_CHECKLIST.md') -Encoding utf8

Write-Host "[guideXOS] AARCH64-P1 staging complete: $OutputDirectory" -ForegroundColor Green
Write-Host "      boot tree: $boot" -ForegroundColor Cyan
Write-Host "      copy manifest: $(Join-Path $OutputDirectory 'manifest.txt')" -ForegroundColor Cyan
