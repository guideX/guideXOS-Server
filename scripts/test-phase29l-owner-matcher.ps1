$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Phase29L.OwnershipEvidence.psm1') -Force

function Assert-MatchState([string]$Name, [string]$Text, [string]$ExpectedCheckpoint, [string]$ExpectedReason) {
    $match = Find-Phase29LOwnerMismatch -SerialText $Text
    if (!$match -or $match.Groups[1].Value -ne $ExpectedCheckpoint -or $match.Groups[3].Value -ne $ExpectedReason) {
        throw "Expected completed mismatch '$Name' to be recognized."
    }
}

function Assert-NoMatch([string]$Name, [string]$Text) {
    if (Find-Phase29LOwnerMismatch -SerialText $Text) {
        throw "Unexpected ownership mismatch for '$Name'."
    }
}

$prefix = 'NativeElf host log: DEVELOPER_STUDIO_PHASE29L_OWNER checkpoint=validated state=validated result='
$suffix = ' tx_active=1 in_progress=1 committed=0 commits=0 releases=0'

Assert-NoMatch 'partial CUR token' ($prefix + 'CUR')
Assert-NoMatch 'complete-looking token without record newline' ($prefix + 'STALE_OWNER' + $suffix)
Assert-NoMatch 'valid CURRENT record' ($prefix + 'CURRENT' + $suffix + "`n")
Assert-NoMatch 'valid transaction release record' ($prefix + 'TRANSACTION_NOT_ACTIVE' + $suffix + "`r`n")
Assert-NoMatch 'complete current record plus partial next record' (($prefix + 'CURRENT' + $suffix + "`n") + $prefix + 'CUR')
Assert-NoMatch 'line-array input keeps record boundaries' ([string[]]@($prefix + 'CURRENT' + $suffix, $prefix + 'CUR'))
Assert-MatchState 'complete failure record' ($prefix + 'STALE_OWNER' + $suffix + "`n") 'validated' 'STALE_OWNER'

Write-Host 'Phase29L owner matcher regression PASS (7 cases).'
