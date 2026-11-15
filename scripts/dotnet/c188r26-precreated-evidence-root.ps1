Set-StrictMode -Version Latest

function Test-C188R26EvidenceOwner([string]$Owner, [string]$ExpectedOwner) {
    return -not [string]::IsNullOrWhiteSpace($Owner) -and
        -not [string]::IsNullOrWhiteSpace($ExpectedOwner) -and
        [string]::Equals($Owner,$ExpectedOwner,[System.StringComparison]::OrdinalIgnoreCase)
}

function Test-C188R26PrecreatedR25EvidenceRoot(
    [string]$Root,
    [string]$CandidateRoot,
    [string]$AuthorizationGuardPath = '') {
    $rootPath = [System.IO.Path]::GetFullPath($Root)
    $candidatePath = [System.IO.Path]::GetFullPath($CandidateRoot)
    if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) {
        return [pscustomobject]@{ accepted=$false; reason='evidence root is not a directory' }
    }
    if ($rootPath -ceq $candidatePath -or
        [System.IO.Path]::GetDirectoryName($rootPath) -cne [System.IO.Path]::GetDirectoryName($candidatePath)) {
        return [pscustomobject]@{ accepted=$false; reason='evidence root is not a unique sibling of the candidate' }
    }
    try {
        $rootItem = Get-Item -LiteralPath $rootPath -Force -ErrorAction Stop
        if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            return [pscustomobject]@{ accepted=$false; reason='evidence root is a reparse point' }
        }
        $actualOwner = (Get-Acl -LiteralPath $rootPath -ErrorAction Stop).Owner
        $expectedOwner = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
        if (-not (Test-C188R26EvidenceOwner $actualOwner $expectedOwner)) {
            return [pscustomobject]@{ accepted=$false; reason='evidence root owner differs from the authorized parent identity' }
        }
    } catch {
        return [pscustomobject]@{ accepted=$false; reason='evidence root ownership could not be verified' }
    }
    $children = @(Get-ChildItem -LiteralPath $rootPath -Force)
    if ($children.Count -gt 1 -or ($children.Count -eq 1 -and
        ($children[0].PSIsContainer -or $children[0].Name -cne 'r25-failfast-capture.json'))) {
        return [pscustomobject]@{ accepted=$false; reason='precreated evidence root contains unexpected entries' }
    }
    $manifestPath = Join-Path $candidatePath 'candidate-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        return [pscustomobject]@{ accepted=$false; reason='candidate manifest is missing' }
    }
    try {
        $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
        $integrityPath = Join-Path $candidatePath 'candidate-integrity.json'
        if (-not (Test-Path -LiteralPath $integrityPath -PathType Leaf)) {
            return [pscustomobject]@{ accepted=$false; reason='candidate integrity inventory is missing' }
        }
        $integrityHash = (Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash
    } catch {
        return [pscustomobject]@{ accepted=$false; reason='candidate manifest or integrity inventory is unreadable' }
    }

    $phaseRoot = [System.IO.Path]::GetDirectoryName($candidatePath)
    $expectedGuardRoot = [System.IO.Path]::GetFullPath((Join-Path $phaseRoot 'one-boot-guards'))
    if ([string]::IsNullOrWhiteSpace($AuthorizationGuardPath)) {
        return [pscustomobject]@{ accepted=$false; reason='authorized parent one-boot guard was not supplied' }
    }
    $guardPath = [System.IO.Path]::GetFullPath($AuthorizationGuardPath)
    if (
        [System.IO.Path]::GetDirectoryName($guardPath) -cne $expectedGuardRoot -or
        [System.IO.Path]::GetFileName($guardPath) -notmatch '^attempt-r25-[A-Fa-f0-9]{32}\.json$' -or
        -not (Test-Path -LiteralPath $guardPath -PathType Leaf)) {
        return [pscustomobject]@{ accepted=$false; reason='authorized parent one-boot guard is missing or outside the candidate phase root' }
    }
    try {
        $guardItem = Get-Item -LiteralPath $guardPath -Force -ErrorAction Stop
        if (($guardItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            return [pscustomobject]@{ accepted=$false; reason='parent one-boot guard is a reparse point' }
        }
        $guard = Get-Content -LiteralPath $guardPath -Raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        return [pscustomobject]@{ accepted=$false; reason='parent one-boot guard is unreadable' }
    }
    if ($guard.schema -cne 'GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1' -or
        [System.IO.Path]::GetFullPath([string]$guard.candidateRoot) -cne $candidatePath -or
        [System.IO.Path]::GetFullPath([string]$guard.bootEvidencePath) -cne $rootPath -or
        [string]$guard.candidateManifestSha256 -ine $manifestHash -or
        [string]$guard.candidateIntegritySha256 -ine $integrityHash -or
        [int]$guard.attemptNumber -ne 1 -or [int]$guard.maxGuestBoots -ne 1 -or
        [bool]$guard.automaticRetry -or [bool]$guard.retryAllowed -or -not [bool]$guard.consumed) {
        return [pscustomobject]@{ accepted=$false; reason='parent one-boot guard does not authorize this candidate and evidence root' }
    }

    if ($children.Count -eq 1) {
        try { $config = Get-Content -LiteralPath $children[0].FullName -Raw | ConvertFrom-Json -ErrorAction Stop }
        catch { return [pscustomobject]@{ accepted=$false; reason='capture config is unreadable' } }
        if ($config.schema -cne 'GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1' -or
            [int]$config.recordBytes -ne 80 -or
            [string]$config.recordAddress -notmatch '^0x[0-9A-Fa-f]{8,16}$' -or
            [string]$config.candidateManifestSha256 -ine $manifestHash) {
            return [pscustomobject]@{ accepted=$false; reason='capture config does not bind the R25 record and candidate manifest' }
        }
    }
    $reason = if ($children.Count -eq 0) { 'empty evidence root is bound to the authorized consumed one-boot guard' }
        else { 'single R25 capture config and evidence root are bound to the authorized consumed one-boot guard' }
    return [pscustomobject]@{ accepted=$true; reason=$reason }
}
