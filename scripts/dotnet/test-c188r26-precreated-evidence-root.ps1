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
$null = New-Item -ItemType Directory -Path $candidate -Force
$null = New-Item -ItemType Directory -Path $evidence -Force
try {
    $candidateManifest = Join-Path $candidate 'candidate-manifest.json'
    Set-Content -LiteralPath $candidateManifest -Value '{"candidate":"frozen-r25"}' -Encoding utf8
    $manifestHash = (Get-FileHash -LiteralPath $candidateManifest -Algorithm SHA256).Hash
    $configPath = Join-Path $evidence 'r25-failfast-capture.json'
    $config = [ordered]@{
        schema='GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1'
        recordAddress='0x00000001001E14E0';recordBytes=80
        candidateManifestSha256=$manifestHash
    }
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $valid = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate
    Assert-R26 ([bool]$valid.accepted) 'the single manifest-bound R25 capture config is accepted'

    Set-Content -LiteralPath (Join-Path $evidence 'unexpected.txt') -Value 'extra' -Encoding utf8
    $extra = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate
    Assert-R26 (-not [bool]$extra.accepted) 'extra files in a precreated evidence root fail closed'
    Remove-Item -LiteralPath (Join-Path $evidence 'unexpected.txt') -Force

    $config.candidateManifestSha256 = ('0' * 64)
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $wrongHash = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate
    Assert-R26 (-not [bool]$wrongHash.accepted) 'a config bound to another candidate manifest is rejected'

    $config.candidateManifestSha256 = $manifestHash
    $config.recordBytes = 79
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $wrongSize = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate
    Assert-R26 (-not [bool]$wrongSize.accepted) 'an unexpected diagnostic record size is rejected'

    $config.schema = 'wrong-schema'
    $config.recordBytes = 80
    $config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
    $wrongSchema = Test-C188R26PrecreatedR25EvidenceRoot $evidence $candidate
    Assert-R26 (-not [bool]$wrongSchema.accepted) 'an unknown capture config schema is rejected'

    $absent = Test-C188R26PrecreatedR25EvidenceRoot (Join-Path $tempRoot 'absent') $candidate
    Assert-R26 (-not [bool]$absent.accepted) 'a missing directory is not treated as a precreated R25 root'
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
