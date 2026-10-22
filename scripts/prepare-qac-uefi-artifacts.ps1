[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$IsoPath,
    [Parameter(Mandatory)][string]$OvmfPath,
    [string]$RunId = 's22r10',
    [string]$OutputRoot,
    [string]$ExpectedEspSha256,
    [string]$ExpectedIsoSha256,
    [string]$ExpectedOvmfSha256
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
        '-drive', 'if=ide,format=raw,file=fat:rw:{esp},index=0',
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
        allocation = 'QAC3C3 must allocate one loopback port per admitted run and keep its listener ownership through verified QEMU child exit.'
        fixedPortReusable = $false
    }
    firmwareVariableStore = [ordered]@{
        separatePflashStore = $false
        path = $null
        guestStateLocation = 'private ESP directory'
        guestStateFilename = 'NvVars'
    }
    storage = [ordered]@{
        writableEspArtifact = 'esp'
        readOnlyIsoArtifact = 'release-iso'
        separateWritableGuestDisk = $false
    }
    lifecycle = 'Stage before QAC admission; QAC3C3 marks active before launch and released only after verified child exit.'
}

$artifacts = @(
    @{
        Name = 'esp'; Role = 'writable-uefi-esp'; Kind = 'directory'; Access = 'writable'; Disposition = 'copy';
        Path = $EspPath; Validation = 'uefi-esp-directory'; ExpectedSha256 = $ExpectedEspSha256
    },
    @{
        Name = 'ovmf-firmware'; Role = 'read-only-uefi-firmware'; Kind = 'file'; Access = 'read-only'; Disposition = 'share';
        Path = $OvmfPath; Validation = 'raw-firmware'; ExpectedSha256 = $ExpectedOvmfSha256
    },
    @{
        Name = 'release-iso'; Role = 'read-only-uefi-cdrom'; Kind = 'file'; Access = 'read-only'; Disposition = 'share';
        Path = $IsoPath; Validation = 'iso9660'; ExpectedSha256 = $ExpectedIsoSha256
    }
)
$arguments = @{
    Project = 'guideXOSServer'
    Workflow = 's22r10-pacman-uefi-qac'
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
    throw "UEFI artifact staging failed: $($_.Exception.Message)"
}
