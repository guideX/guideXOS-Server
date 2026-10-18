param(
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '..\docs\rexx\evidence\rx12r1')
)

$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'rexx_artifact_guard.psm1'
Import-Module -Name $modulePath -Force
$result = Invoke-ReXXArtifactGuardSyntheticTest -EvidenceDirectory ([System.IO.Path]::GetFullPath($EvidenceDirectory))
if (-not $result.passed) { throw 'RX12R1 artifact guard synthetic self-test failed.' }
Write-Output "Synthetic fail-closed checks passed: $EvidenceDirectory"
