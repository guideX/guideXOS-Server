[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$IsoPath,
    [Parameter(Mandatory)][string]$OvmfPath,
    [string]$RunId = 's22r18-public-preview',
    [string]$OutputRoot,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedIsoSha256,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedOvmfSha256
)

$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'qac-uefi-artifacts.psm1'
Import-Module $modulePath -Force -ErrorAction Stop

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
        writableGuestDisks = @()
        guestHostFilesystemIntegrations = @()
    }
    network = [ordered]@{ guestNetwork = 'disabled'; exposure = 'none' }
    lifecycle = 'Stage and verify artifacts before QAC admission; QAC3C3 owns QEMU launch and release.'
}

$artifacts = @(
    @{
        Name = 'ovmf-firmware'; Role = 'read-only-uefi-firmware'; Kind = 'file'; Access = 'read-only'; Disposition = 'share';
        Path = $OvmfPath; Validation = 'raw-firmware'; ExpectedSha256 = $ExpectedOvmfSha256
    },
    @{
        Name = 'release-iso'; Role = 'read-only-public-preview-iso'; Kind = 'file'; Access = 'read-only'; Disposition = 'share';
        Path = $IsoPath; Validation = 'iso9660'; ExpectedSha256 = $ExpectedIsoSha256
    }
)
$arguments = @{
    Project = 'guideXOSServer'
    Workflow = 's22r18-public-preview-iso-only-qac'
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
    } | ConvertTo-Json -Compress
} catch {
    throw "S22R18 public-preview QAC artifact staging failed: $($_.Exception.Message)"
}
