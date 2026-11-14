param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [Parameter(Mandatory=$true)][string]$R25ManifestPath,
    [string]$ArtifactRoot='E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [ValidateRange(60,300)][int]$MaxRuntimeSeconds=120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$manifest = Get-Content -LiteralPath $R25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($manifest.schema -cne 'GUIDEXOS_C188R25_CANDIDATE_V1' -or
    [System.IO.Path]::GetFullPath([string]$manifest.candidateRoot) -cne [System.IO.Path]::GetFullPath($CandidateRoot)) {
    throw 'R25 candidate manifest does not bind the requested candidate.'
}
$candidateManifestPath = [string]$manifest.candidateManifestPath
$candidateIntegrityPath = [string]$manifest.candidateIntegrityPath
if ((Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash -ne [string]$manifest.candidateManifestSha256 -or
    (Get-FileHash -LiteralPath $candidateIntegrityPath -Algorithm SHA256).Hash -ne [string]$manifest.candidateIntegritySha256) {
    throw 'R25 baseline candidate manifest or integrity inventory changed.'
}
$runner = Join-Path $PSScriptRoot 'run-c188r24-initializemodules.ps1'
& $runner -CandidateRoot $CandidateRoot -ArtifactRoot $ArtifactRoot `
    -MaxRuntimeSeconds $MaxRuntimeSeconds -R25ManifestPath $R25ManifestPath
exit $LASTEXITCODE
