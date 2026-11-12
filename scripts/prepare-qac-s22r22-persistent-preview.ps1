[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$IsoPath,
    [Parameter(Mandatory)][string]$OvmfPath,
    [Parameter(Mandatory)][string]$DataImagePath,
    [Parameter(Mandatory)][string]$DataVolumeRoot,
    [string]$RunId = 's22r22-persistent-documents',
    [string]$OutputRoot,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedIsoSha256,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedOvmfSha256,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedDataImageSha256
)

$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'qac-uefi-artifacts.psm1'
Import-Module $modulePath -Force -ErrorAction Stop

function Get-FullPath {
    param([Parameter(Mandatory)][string]$Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-NoReparsePath {
    param([Parameter(Mandatory)][string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($fullPath)
    $current = $root
    foreach ($part in $fullPath.Substring($root.Length).Split([char[]]@('\','/'), [StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point in S22R22 QAC input path: $current"
            }
        } else {
            throw "Required S22R22 QAC input path does not exist: $current"
        }
    }
}

$iso = Get-FullPath $IsoPath
$ovmf = Get-FullPath $OvmfPath
$dataImage = Get-FullPath $DataImagePath
$dataRoot = Get-FullPath $DataVolumeRoot
Assert-NoReparsePath -Path $iso
Assert-NoReparsePath -Path $ovmf
Assert-NoReparsePath -Path $dataRoot
Assert-NoReparsePath -Path $dataImage

if (-not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
    throw "Dedicated data-volume root is not a directory: $dataRoot"
}
if ($dataRoot -notmatch '(?i)^[A-Z]:\\(?:.*\\)?CodexArtifacts\\S22R22\\persistent-data$') {
    throw 'The S22R22 persistent data-image root must be E:\CodexArtifacts\S22R22\persistent-data (or another local drive with the same directory contract).'
}
if (-not [string]::Equals((Split-Path -Parent $dataImage), $dataRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The writable guest disk must be a direct child of the dedicated S22R22 data-volume root.'
}
if ([IO.Path]::GetExtension($dataImage) -ine '.img') { throw 'The writable guest disk must be a regular .img file.' }
if (-not (Test-Path -LiteralPath $dataImage -PathType Leaf)) { throw "Dedicated writable data image is missing: $dataImage" }
if ($dataImage -notmatch '^[A-Za-z]:\\') { throw 'Raw-device and non-local paths are forbidden for the S22R22 writable guest disk.' }
$dataFile = Get-Item -LiteralPath $dataImage -Force -ErrorAction Stop
if ($dataFile.Attributes -band [IO.FileAttributes]::ReadOnly) {
    throw 'The dedicated writable data image is currently marked read-only.'
}
if ($dataFile.Length -lt 64MB -or $dataFile.Length -gt 2048MB -or ($dataFile.Length % 512) -ne 0) {
    throw 'The writable data image must be a 64..2048 MiB, 512-byte-aligned regular file.'
}
$dataSha = (Get-FileHash -LiteralPath $dataImage -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
if ($dataSha -cne $ExpectedDataImageSha256.ToLowerInvariant()) {
    throw "Writable data image identity mismatch: expected $ExpectedDataImageSha256, found $dataSha."
}

$launchContract = [ordered]@{
    executableFamily = 'qemu-system-x86_64.exe'
    arguments = @(
        '-machine', 'pc,accel=whpx',
        '-smp', '2',
        '-m', '2048M',
        '-vga', 'std',
        '-display', 'none',
        '-drive', 'if=pflash,format=raw,readonly=on,file={ovmfFirmware}',
        '-drive', 'if=none,id=releasecdrom,file={iso},media=cdrom,readonly=on,format=raw',
        '-device', 'ide-cd,drive=releasecdrom,bootindex=1',
        '-drive', 'if=none,id=userdata,file={dataVolume},format=raw,cache=writeback',
        '-device', 'ide-hd,drive=userdata,serial=GXOS-DATA-V1',
        '-boot', 'order=d',
        '-net', 'none',
        '-serial', 'file:{serialLog}',
        '-D', '{qemuDebugLog}',
        '-d', 'guest_errors,cpu_reset',
        '-rtc', 'base=utc,clock=host',
        '-no-reboot',
        '-no-shutdown',
        '-monitor', 'none',
        '-qmp', 'tcp:127.0.0.1:{QAC3C3-allocated-port},server=on,wait=off',
        '-name', 'guideXOS-{runId}'
    )
    qmp = [ordered]@{
        bindAddress = '127.0.0.1'
        port = $null
        allocation = 'QAC3C3 allocates the loopback QMP port and owns the direct QEMU child through verified exit.'
        guestAccessible = $false
    }
    firmwareVariableStore = [ordered]@{
        separateWritableStore = $false
        guestState = 'volatile; no writable UEFI variable store or ESP is attached'
    }
    storage = [ordered]@{
        readOnlyIsoArtifact = 'release-iso'
        readOnlyFirmwareArtifact = 'ovmf-firmware'
        dataVolumeRoot = $dataRoot
        writableGuestDisks = @([ordered]@{
            path = $dataImage
            role = 'dedicated-persistent-test-data-volume'
            access = 'writable'
            qemuFormat = 'raw'
            stableAtaSerial = 'GXOS-DATA-V1'
            bytes = [int64]$dataFile.Length
            sha256BeforeLaunch = $dataSha
            reuseAcrossReservations = $true
            notBootable = $true
        })
        guestHostFilesystemIntegrations = @()
    }
    network = [ordered]@{ guestNetwork = 'disabled'; exposure = 'none' }
    lifecycle = 'The one dedicated writable test disk stays outside each run workspace and is reused between separately QAC-owned cold boots.'
}

$artifacts = @(
    @{
        Name = 'ovmf-firmware'; Role = 'read-only-uefi-firmware'; Kind = 'file'; Access = 'read-only'; Disposition = 'share';
        Path = $ovmf; Validation = 'raw-firmware'; ExpectedSha256 = $ExpectedOvmfSha256
    },
    @{
        Name = 'release-iso'; Role = 'read-only-public-preview-iso'; Kind = 'file'; Access = 'read-only'; Disposition = 'share';
        Path = $iso; Validation = 'iso9660'; ExpectedSha256 = $ExpectedIsoSha256
    }
)
$arguments = @{
    Project = 'guideXOSServer'
    Workflow = 's22r22-persistent-data-volume-qac'
    RunId = $RunId
    Artifact = $artifacts
    LaunchContract = $launchContract
}
if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) { $arguments.OutputRoot = $OutputRoot }

try {
    $workspace = New-QacUefiArtifactWorkspace @arguments
    [PSCustomObject]@{
        prepared = $true
        qemuLaunched = $false
        runId = $workspace.RunId
        workspacePath = $workspace.WorkspacePath
        manifestPath = $workspace.ManifestPath
        writableDataImage = $dataImage
        writableDataImageSha256 = $dataSha
        writableDataImageBytes = [int64]$dataFile.Length
    } | ConvertTo-Json -Compress
} catch {
    throw "S22R22 persistent-preview QAC artifact staging failed: $($_.Exception.Message)"
}
