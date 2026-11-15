$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r26-precreated-evidence-root.ps1')

$script:checks = 0
function Assert-R26([bool]$Condition, [string]$Name) {
    $script:checks++
    if (-not $Condition) { throw "R26 precreated evidence root failed: $Name" }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('c188r26-precreated-root-' + [guid]::NewGuid().ToString('N'))
$candidate = Join-Path $tempRoot 'candidate'
$evidence = Join-Path $tempRoot 'evidence'
$guardRoot = Join-Path $tempRoot 'one-boot-guards'
$null = New-Item -ItemType Directory -Path $candidate,$evidence,$guardRoot -Force
try {
    $candidateManifest = Join-Path $candidate 'candidate-manifest.json'
    $candidateIntegrity = Join-Path $candidate 'candidate-integrity.json'
    Set-Content -LiteralPath $candidateManifest -Value '{"candidate":"frozen-r25"}' -Encoding utf8
    Set-Content -LiteralPath $candidateIntegrity -Value '{"candidate":"frozen-r25-integrity"}' -Encoding utf8
    $manifestHash = (Get-FileHash -LiteralPath $candidateManifest -Algorithm SHA256).Hash
    $integrityHash = (Get-FileHash -LiteralPath $candidateIntegrity -Algorithm SHA256).Hash
    $guardPath = Join-Path $guardRoot ('attempt-r25-' + [guid]::NewGuid().ToString('N') + '.json')
    $guard = [ordered]@{
        schema='GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1';candidateRoot=$candidate
        candidateManifestSha256=$manifestHash;candidateIntegritySha256=$integrityHash
        bootEvidencePath=$evidence;attemptNumber=1;maxGuestBoots=1
        automaticRetry=$false;retryAllowed=$false;consumed=$true
    }
    $configPath = Join-Path $evidence 'r25-failfast-capture.json'
    $config = [ordered]@{
        schema='GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1'
        recordAddress='0x00000001001E14E0';recordBytes=80
        candidateManifestSha256=$manifestHash
    }
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $validMetadata = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 ([bool]$validMetadata.accepted) 'parent-authorized R25 capture metadata is accepted'

    Remove-Item -LiteralPath $configPath -Force
    $empty = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 ([bool]$empty.accepted -and $empty.reason -match 'empty evidence root') 'an empty sibling evidence directory is accepted only with its bound consumed guard'

    $withoutGuard = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate ''
    Assert-R26 (-not [bool]$withoutGuard.accepted) 'an empty directory without parent authorization is rejected'

    Set-Content -LiteralPath $configPath -Value ($config | ConvertTo-Json) -Encoding utf8
    Set-Content -LiteralPath (Join-Path $evidence 'unexpected.txt') -Value 'extra' -Encoding utf8
    $extra = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 (-not [bool]$extra.accepted) 'unexpected nonempty evidence directories fail closed'
    Remove-Item -LiteralPath (Join-Path $evidence 'unexpected.txt') -Force

    $config.candidateManifestSha256 = ('0' * 64)
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $wrongHash = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 (-not [bool]$wrongHash.accepted) 'capture config bound to another manifest is rejected'

    $config.candidateManifestSha256 = $manifestHash
    $config.recordBytes = 79
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $wrongSize = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 (-not [bool]$wrongSize.accepted) 'unexpected diagnostic record size is rejected'

    $config.schema = 'wrong-schema'
    $config.recordBytes = 80
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $wrongSchema = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 (-not [bool]$wrongSchema.accepted) 'unknown capture config schema is rejected'

    $config.schema = 'GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1'
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $guard.bootEvidencePath = Join-Path $tempRoot 'historical-evidence'
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $historical = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 (-not [bool]$historical.accepted) 'historical attempt authorization cannot be replayed at another evidence root'

    $guard.bootEvidencePath = $evidence
    $guard.candidateRoot = Join-Path $tempRoot 'other-candidate'
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $candidateMismatch = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate $guardPath
    Assert-R26 (-not [bool]$candidateMismatch.accepted) 'a foreign candidate guard is rejected'

    $guard.candidateRoot = $candidate
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $outside = Join-Path ([System.IO.Path]::GetTempPath()) ('c188r26-unsafe-root-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $outside
    try {
        $unsafe = Test-C188R26PrecreatedR25EvidenceRoot $outside $candidate $guardPath
        Assert-R26 (-not [bool]$unsafe.accepted) 'evidence roots outside the candidate phase sibling are rejected'
    } finally { Remove-Item -LiteralPath $outside -Recurse -Force }

    Assert-R26 (-not (Test-C188R26EvidenceOwner 'DOMAIN\other' 'DOMAIN\current')) 'foreign-owned directory identity is rejected'
    Assert-R26 ((Test-C188R26EvidenceOwner 'DOMAIN\current' 'domain\CURRENT')) 'the authorized parent directory owner is accepted case-insensitively'

    $absent = Test-C188R26PrecreatedR25EvidenceRoot (Join-Path $tempRoot 'absent') $candidate $guardPath
    Assert-R26 (-not [bool]$absent.accepted) 'a missing directory is left for the runner to create'
    Write-Output "R26_PRECREATED_EVIDENCE_ROOT_ASSERTIONS=$script:checks RESULT=PASS"
}
finally {
    $resolvedTempRoot = [System.IO.Path]::GetFullPath($tempRoot)
    $tempPrefix = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
    if ($resolvedTempRoot.StartsWith($tempPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTempRoot) -like 'c188r26-precreated-root-*') {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force
    }
}
