[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$CandidateDirectory,
    [Parameter(Mandatory = $true)] [string]$ArtifactRoot,
    [string]$QacCliPath = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [ValidateRange(1, 3600)] [int]$AdmissionTimeoutSeconds = 1800,
    [ValidateRange(30, 900)] [int]$RuntimeSeconds = 300,
    [ValidateRange(5, 180)] [int]$ChildCleanupSeconds = 45,
    [ValidateRange(5, 120)] [int]$ReservationCleanupSeconds = 20
)

$ErrorActionPreference = 'Stop'
$supervisor = Join-Path $PSScriptRoot 'run-aarch64-runsafe2-qac.ps1'
$arguments = @{
    CandidateDirectory = $CandidateDirectory
    ArtifactRoot = $ArtifactRoot
    QacCliPath = $QacCliPath
    QemuPath = $QemuPath
    FirmwareCode = $FirmwareCode
    FirmwareVarsTemplate = $FirmwareVarsTemplate
    AdmissionTimeoutSeconds = $AdmissionTimeoutSeconds
    RuntimeSeconds = $RuntimeSeconds
    ChildCleanupSeconds = $ChildCleanupSeconds
    ReservationCleanupSeconds = $ReservationCleanupSeconds
    ScheduledEl0Qualification = $true
    Runsafe4BQualification = $true
}
& $supervisor @arguments
if (!$?) { exit 1 }
exit 0
