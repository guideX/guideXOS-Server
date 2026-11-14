Set-StrictMode -Version Latest

function Test-C188R26PrecreatedR25EvidenceRoot([string]$Root, [string]$CandidateRoot) {
    $rootPath = [System.IO.Path]::GetFullPath($Root)
    $candidatePath = [System.IO.Path]::GetFullPath($CandidateRoot)
    if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) {
        return [pscustomobject]@{ accepted=$false; reason='evidence root is not a directory' }
    }
    $children = @(Get-ChildItem -LiteralPath $rootPath -Force)
    if ($children.Count -ne 1 -or $children[0].PSIsContainer -or
        $children[0].Name -cne 'r25-failfast-capture.json') {
        return [pscustomobject]@{ accepted=$false; reason='precreated evidence root must contain only the R25 capture config' }
    }
    $manifestPath = Join-Path $candidatePath 'candidate-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        return [pscustomobject]@{ accepted=$false; reason='candidate manifest is missing' }
    }
    try {
        $config = Get-Content -LiteralPath $children[0].FullName -Raw | ConvertFrom-Json -ErrorAction Stop
        $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    } catch {
        return [pscustomobject]@{ accepted=$false; reason='capture config or candidate manifest is unreadable' }
    }
    if ($config.schema -cne 'GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1' -or
        [int]$config.recordBytes -ne 80 -or
        [string]$config.recordAddress -notmatch '^0x[0-9A-Fa-f]{8,16}$' -or
        [string]$config.candidateManifestSha256 -ine $manifestHash) {
        return [pscustomobject]@{ accepted=$false; reason='capture config does not bind the R25 record and candidate manifest' }
    }
    return [pscustomobject]@{ accepted=$true; reason='single R25 capture config is bound to the candidate manifest' }
}
