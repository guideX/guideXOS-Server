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

    $candidateR28 = Join-Path $tempRoot 'candidate-r28'
    $evidenceR28 = Join-Path $tempRoot 'evidence-r28'
    $null = New-Item -ItemType Directory -Path $candidateR28,$evidenceR28
    $runtimeManifestR28 = Join-Path $candidateR28 'runtime-pack.manifest.json'
    [ordered]@{
        c188r25FailFastOriginCapture=$true
        c188r28GcStateGuardDiscriminator=$true
        c188r28GcStateGuardDiscriminatorMarker='GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR'
        failFastOriginRecordSchema=2
        failFastOriginRecordBytes=144
    } | ConvertTo-Json | Set-Content -LiteralPath $runtimeManifestR28 -Encoding utf8
    $candidateManifestR28 = Join-Path $candidateR28 'candidate-manifest.json'
    [ordered]@{candidate='frozen-r28';runtimePackManifest=$runtimeManifestR28} |
        ConvertTo-Json | Set-Content -LiteralPath $candidateManifestR28 -Encoding utf8
    $candidateIntegrityR28 = Join-Path $candidateR28 'candidate-integrity.json'
    Set-Content -LiteralPath $candidateIntegrityR28 -Value '{"candidate":"frozen-r28-integrity"}' -Encoding utf8
    $manifestHashR28 = (Get-FileHash -LiteralPath $candidateManifestR28 -Algorithm SHA256).Hash
    $integrityHashR28 = (Get-FileHash -LiteralPath $candidateIntegrityR28 -Algorithm SHA256).Hash
    $guardPathR28 = Join-Path $guardRoot ('attempt-r25-' + [guid]::NewGuid().ToString('N') + '.json')
    $recordAddressR28 = '0x00000001001E14E0'
    [ordered]@{
        schema='GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1';candidateRoot=$candidateR28
        candidateManifestSha256=$manifestHashR28;candidateIntegritySha256=$integrityHashR28
        bootEvidencePath=$evidenceR28;attemptNumber=1;maxGuestBoots=1
        automaticRetry=$false;retryAllowed=$false;consumed=$true
    } | ConvertTo-Json | Set-Content -LiteralPath $guardPathR28 -Encoding utf8
    [ordered]@{
        schema='GUIDEXOS_C188R25_CANDIDATE_V1'
        candidateRoot=$candidateR28
        candidateManifestSha256=$manifestHashR28
        runtimePackManifest=$runtimeManifestR28
        diagnostics=[ordered]@{
            recordAddress=$recordAddressR28;recordBytes=144;schemaVersion=2
            gcStateGuardDiscriminator=$true
        }
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $tempRoot 'candidate-r28-r25-candidate-manifest.json') -Encoding utf8
    $capturePathR28 = Join-Path $evidenceR28 'r25-failfast-capture.json'
    [ordered]@{
        schema='GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1'
        recordAddress=$recordAddressR28;recordBytes=144
        candidateManifestSha256=$manifestHashR28
    } | ConvertTo-Json | Set-Content -LiteralPath $capturePathR28 -Encoding utf8
    $validR28 = Test-C188R26PrecreatedR25EvidenceRoot $evidenceR28 $candidateR28 $guardPathR28
    Assert-R26 ([bool]$validR28.accepted) 'R28 capture is accepted when its 144-byte record is bound to both candidate manifests'

    $captureR28 = Get-Content -LiteralPath $capturePathR28 -Raw | ConvertFrom-Json
    $captureR28.recordBytes = 80
    $captureR28 | ConvertTo-Json | Set-Content -LiteralPath $capturePathR28 -Encoding utf8
    $legacyOnR28 = Test-C188R26PrecreatedR25EvidenceRoot $evidenceR28 $candidateR28 $guardPathR28
    Assert-R26 (-not [bool]$legacyOnR28.accepted) 'R28 candidates cannot use the legacy 80-byte capture schema'

    $captureR28.recordBytes = 144
    $captureR28 | ConvertTo-Json | Set-Content -LiteralPath $capturePathR28 -Encoding utf8
    $r25ManifestR28Path = Join-Path $tempRoot 'candidate-r28-r25-candidate-manifest.json'
    $r25ManifestR28 = Get-Content -LiteralPath $r25ManifestR28Path -Raw | ConvertFrom-Json
    $r25ManifestR28.diagnostics.recordAddress = '0x00000001001E14E1'
    $r25ManifestR28 | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $r25ManifestR28Path -Encoding utf8
    $wrongR28Address = Test-C188R26PrecreatedR25EvidenceRoot $evidenceR28 $candidateR28 $guardPathR28
    Assert-R26 (-not [bool]$wrongR28Address.accepted) 'R28 capture address must agree with the R25 candidate manifest'

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
