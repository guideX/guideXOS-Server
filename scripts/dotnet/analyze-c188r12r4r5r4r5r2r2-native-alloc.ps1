param(
    [Parameter(Mandatory=$true)][string]$SerialPath,
    [string]$OutputPath = '',
    [ValidateSet('guest_serial','hosted_fixture')][string]$EvidenceKind = 'guest_serial'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$marker = 'C188R12R4R5R4R5R2R2_NATIVE_ALLOC'
if (-not (Test-Path -LiteralPath $SerialPath -PathType Leaf)) { throw "Serial input not found: $SerialPath" }
$raw = Get-Content -LiteralPath $SerialPath -Raw
if ($null -eq $raw) { $raw = '' }
$pattern = '\[' + [regex]::Escape($marker) + '\] source=(runtime|backend) invocation=([0-9A-Fa-f]{8}) sequence=([0-9A-Fa-f]{8}) field=([0-9A-Fa-f]{8}) value=([0-9A-Fa-f]{8}) auxiliary=([0-9A-Fa-f]{8})'
$rows = @{}
$auxiliaryRows = @{}
foreach ($match in [regex]::Matches($raw, $pattern)) {
    $source = $match.Groups[1].Value
    $invocation = $match.Groups[2].Value.ToUpperInvariant()
    $sequence = [Convert]::ToUInt32($match.Groups[3].Value, 16)
    $field = [Convert]::ToUInt32($match.Groups[4].Value, 16)
    $value = [Convert]::ToUInt32($match.Groups[5].Value, 16)
    $auxiliary = [Convert]::ToUInt32($match.Groups[6].Value, 16)
    $key = "$source/$invocation/$sequence/$field"
    if ($rows.ContainsKey($key)) { throw "Duplicate native-allocation trace field: $key" }
    $rows[$key] = $value
    $auxiliaryRows[$key] = $auxiliary
}

function Get-TraceField([string]$Source, [string]$Invocation, [uint32]$Sequence, [uint32]$Field) {
    $key = "$Source/$Invocation/$Sequence/$Field"
    if ($rows.ContainsKey($key)) { return [uint32]$rows[$key] }
    return [uint32]::MaxValue
}
function Get-TraceSummary([string]$Source, [string]$Invocation, [uint32]$Field) {
    $summaryField = if ($Source -ceq 'runtime') { $Field } else { 0x80000000u -bor $Field }
    return Get-TraceField $Source $Invocation ([uint32]::MaxValue) $summaryField
}
function Test-TraceFieldPresent([string]$Source,[string]$Invocation,[uint32]$Sequence,[uint32]$Field) {
    return $rows.ContainsKey("$Source/$Invocation/$Sequence/$Field")
}

$runtimeInvocations = @($rows.Keys | Where-Object { $_ -like 'runtime/*' } | ForEach-Object { ($_ -split '/')[1] } | Sort-Object -Unique)
$results = @()
if ($runtimeInvocations.Count -eq 0) {
    $results += [ordered]@{ invocation=$null; classification='INCOMPLETE_MISSING_RUNTIME_RECORD'; detail='No runtime marker rows were found.'; firstFailure=$null }
} else {
    foreach ($invocation in $runtimeInvocations) {
        $count = Get-TraceSummary 'runtime' $invocation 2
        $overflow = Get-TraceSummary 'runtime' $invocation 3
        $summaryMissing = $count -eq [uint32]::MaxValue -or $overflow -eq [uint32]::MaxValue
        $backendCount = Get-TraceSummary 'backend' $invocation 0
        $backendFirstFailureIndex = Get-TraceSummary 'backend' $invocation 1
        $backendSummaryField = [Convert]::ToUInt32('80000000',16)
        $backendSummaryKey = "backend/$invocation/$([uint32]::MaxValue)/$backendSummaryField"
        $backendOverflow = if ($auxiliaryRows.ContainsKey($backendSummaryKey)) { [uint32]$auxiliaryRows[$backendSummaryKey] } else { [uint32]::MaxValue }
        $runtime = @()
        if (-not $summaryMissing -and $count -le 8) {
            for ($index=0; $index -lt $count; $index++) {
                $fields = @{}
                for ($field=0; $field -lt 25; $field++) {
                    $fields[$field] = Get-TraceField 'runtime' $invocation ([uint32]$index) ([uint32]$field)
                }
                $runtime += ,$fields
            }
        }
        $backend = @()
        if ($backendCount -ne [uint32]::MaxValue -and $backendCount -le 8) {
            for ($index=0; $index -lt $backendCount; $index++) {
                $fields = @{}
                for ($field=0; $field -lt 34; $field++) {
                    $fields[$field] = Get-TraceField 'backend' $invocation ([uint32]$index) ([uint32]$field)
                }
                $backend += ,$fields
            }
        }

        $backendRecordComplete = ($backendCount -ne [uint32]::MaxValue -and
            $backendCount -le 8 -and $backendOverflow -eq 0 -and
            $backend.Count -eq $backendCount)
        $backendFailureIndexes = @()
        for ($backendIndex=0; $backendIndex -lt $backend.Count; $backendIndex++) {
            $backendAttempt = $backend[$backendIndex]
            for ($field=0; $field -lt 34; $field++) {
                if (-not (Test-TraceFieldPresent 'backend' $invocation ([uint32]$backendIndex) ([uint32]$field))) {
                    $backendRecordComplete = $false
                }
            }
            if ($backendAttempt[0] -ne $backendIndex -or $backendAttempt[1] -ne 24 -or
                $backendAttempt[2] -ne 0x3000 -or $backendAttempt[3] -ne 0x1000 -or
                $backendAttempt[7] -ne 1) {
                $backendRecordComplete = $false
            }
            if ($backendAttempt[14] -ne [uint32]::MaxValue) {
                $backendFailureIndexes += $backendIndex
            } elseif ($backendAttempt[9] -ne 0 -or $backendAttempt[10] -ne 0x1000 -or
                      $backendAttempt[11] -ne 0 -or $backendAttempt[13] -ne 1) {
                $backendRecordComplete = $false
            }
        }
        if (($backendFirstFailureIndex -eq [uint32]::MaxValue -and $backendFailureIndexes.Count -ne 0) -or
            ($backendFirstFailureIndex -ne [uint32]::MaxValue -and
                ($backendFirstFailureIndex -ge $backend.Count -or $backendFailureIndexes.Count -ne 1 -or
                 $backendFailureIndexes[0] -ne $backendFirstFailureIndex))) {
            $backendRecordComplete = $false
        }

        $classification = 'INCOMPLETE_DIAGNOSTIC_RECORD'
        $detail = 'A summary, bounded attempt, or required source field is missing or overflowed.'
        $firstFailure = $null
        $allocationResults = @()
        if (-not $summaryMissing -and $overflow -eq 0 -and $count -gt 0 -and $count -le 8 -and
            $backendRecordComplete) {
            $classification = 'SUCCESS'
            $detail = 'All captured 8-byte event objects completed through the configured PAL and true-VM reserve/commit path.'
            $firstRuntimeFailure = [uint32]::MaxValue
            $runtimeRecordComplete = $true
            for ($index=0; $index -lt $count; $index++) {
                $attempt = $runtime[$index]
                $requiredRuntimeFields = @(1,2,3,4,5,6,7,10,11,12,13,14,15,16,17,18,19,20)
                foreach ($field in $requiredRuntimeFields) { if ($attempt[$field] -eq [uint32]::MaxValue) { $runtimeRecordComplete=$false } }
                if ($attempt[21] -ne [uint32]::MaxValue -and
                    @($attempt[22],$attempt[24]).Contains([uint32]::MaxValue)) { $runtimeRecordComplete=$false }
                if ($attempt[1] -ne 0x0200 -or $attempt[2] -ne 2 -or
                    $attempt[3] -ne 8 -or $attempt[4] -ne 0 -or
                    $attempt[5] -ne 8 -or $attempt[6] -ne 0 -or
                    $attempt[7] -ne 0x0201 -or $attempt[10] -ne 0x0202 -or
                    $attempt[11] -ne 8 -or $attempt[12] -ne 0 -or
                    $attempt[13] -ne 24 -or $attempt[14] -ne 0) { $runtimeRecordComplete=$false }

                $attemptClass = 'SUCCESS'
                $attemptFailure = $null
                $palState = $attempt[15]
                $callbackEntered = $attempt[16]
                $runtimePointer = $attempt[19]
                $constructorEntered = $attempt[20]
                $failureOperation = $attempt[21]
                $failureBranch = $attempt[22]
                $failureStatus = $attempt[23]
                $failureStatusSource = $attempt[24]
                if ($failureOperation -ne [uint32]::MaxValue -and $firstRuntimeFailure -eq [uint32]::MaxValue) {
                    $firstRuntimeFailure = [uint32]$index
                }

                if ($palState -eq 1 -and $callbackEntered -eq 0 -and $runtimePointer -eq 0 -and $constructorEntered -eq 0) {
                    $attemptClass = 'PAL_NOT_READY'
                    $attemptFailure = [ordered]@{ operation='0x00000203'; branch='0x00000303'; status='0xFFFFFFFF'; statusSource='0x00000000' }
                } elseif ($failureOperation -eq 0x0202 -and $failureBranch -eq 0x0302) {
                    $attemptClass = 'NATIVE_ALLOC_SIZE_OVERFLOW'
                    $attemptFailure = [ordered]@{ operation=('0x{0:X8}' -f $failureOperation); branch=('0x{0:X8}' -f $failureBranch); status=('0x{0:X8}' -f $failureStatus); statusSource=('0x{0:X8}' -f $failureStatusSource) }
                } elseif ($callbackEntered -eq 1) {
                    if ($runtimePointer -eq 1 -and $constructorEntered -eq 1 -and
                        $failureOperation -eq [uint32]::MaxValue -and
                        $failureBranch -eq [uint32]::MaxValue) {
                        $attemptClass = 'SUCCESS'
                    } elseif ($runtimePointer -eq 0 -and $constructorEntered -eq 0 -and
                              $failureOperation -eq 0x0203 -and $failureBranch -eq 0x0304 -and
                              $backendFirstFailureIndex -ne [uint32]::MaxValue -and
                              $backendFirstFailureIndex -lt $backend.Count) {
                        $backAttempt = $backend[$backendFirstFailureIndex]
                        $operation = $backAttempt[14]
                        $branch = $backAttempt[15]
                        $status = $backAttempt[16]
                        $statusSource = $backAttempt[17]
                        if ($operation -eq 0x0204 -and $branch -eq 0x0310) {
                            if ($backAttempt[4] -eq 0 -or $backAttempt[5] -ne $backAttempt[4] -or
                                $backAttempt[6] -ne [uint32]::MaxValue -or $backAttempt[7] -ne 1 -or
                                $backAttempt[9] -ne [uint32]::MaxValue -or $backAttempt[10] -ne 0 -or
                                $backAttempt[11] -ne [uint32]::MaxValue -or $backAttempt[13] -ne 0) {
                                $attemptClass='INCOMPLETE_BACKEND_RECORD'
                            } else { $attemptClass='KERNEL_PAL_SLOT_CAPACITY' }
                        } elseif ($operation -eq 0x0205 -and $branch -ne [uint32]::MaxValue) {
                            $attemptClass=('VM_RESERVE_BRANCH_0x{0:X4}' -f ($branch -band 0xFFFF))
                        } elseif ($operation -eq 0x0206 -and $branch -ne [uint32]::MaxValue) {
                            $attemptClass=('VM_COMMIT_BRANCH_0x{0:X4}' -f ($branch -band 0xFFFF))
                        } elseif ($operation -eq 0x0207 -and $branch -ne [uint32]::MaxValue) {
                            $attemptClass='VM_ROLLBACK_RELEASE_FAILURE'
                        } else {
                            $attemptClass='INCOMPLETE_BACKEND_FAILURE_BRANCH'
                        }
                        if ($attemptClass -notin @('INCOMPLETE_BACKEND_FAILURE_BRANCH','INCOMPLETE_BACKEND_RECORD')) {
                            $attemptFailure = [ordered]@{
                                operation=('0x{0:X8}' -f $operation); branch=('0x{0:X8}' -f $branch)
                                status=('0x{0:X8}' -f $status); statusSource=('0x{0:X8}' -f $statusSource)
                                backendAttemptIndex=[int]$backendFirstFailureIndex
                                requestSize=('0x{0:X}' -f $backAttempt[1])
                                slotCapacity=[int]$backAttempt[4]; activeSlots=[int]$backAttempt[5]
                                freeSlotIndex=('0x{0:X8}' -f $backAttempt[6])
                                addressSpaceReady=[bool]$backAttempt[7]
                                vmRegionCapacity=[int]$backAttempt[8]
                            }
                        }
                    } elseif ($runtimePointer -eq 0 -and $constructorEntered -eq 0 -and
                              $failureOperation -eq 0x0203 -and $failureBranch -eq 0x0304) {
                        $attemptClass = 'BACKEND_NULL_CAUSE_UNRECORDED'
                    }
                } else {
                    $attemptClass = 'INCOMPLETE_DIAGNOSTIC_RECORD'
                }
                $allocationResults += [ordered]@{ sequence=$index; classification=$attemptClass; firstFailure=$attemptFailure }
                if ($classification -eq 'SUCCESS' -and $attemptClass -ne 'SUCCESS') {
                    $classification = $attemptClass
                    $detail = "Allocation attempt $index classified as $attemptClass."
                    $firstFailure = $attemptFailure
                }
            }
            if (-not $runtimeRecordComplete) {
                $classification='INCOMPLETE_DIAGNOSTIC_RECORD'
                $detail='At least one runtime or backend record is missing, inconsistent, or outside the bounded contract.'
            }
            $summaryFirstFailure = Get-TraceSummary 'runtime' $invocation 4
            if ($firstRuntimeFailure -ne [uint32]::MaxValue) {
                $sourceAttempt = $runtime[$firstRuntimeFailure]
                if ($summaryFirstFailure -ne $firstRuntimeFailure -or
                    (Get-TraceSummary 'runtime' $invocation 5) -ne $sourceAttempt[21] -or
                    (Get-TraceSummary 'runtime' $invocation 6) -ne $sourceAttempt[22] -or
                    (Get-TraceSummary 'runtime' $invocation 7) -ne $sourceAttempt[23] -or
                    (Get-TraceSummary 'runtime' $invocation 8) -ne $sourceAttempt[24]) {
                    $classification='INCOMPLETE_DIAGNOSTIC_RECORD'
                    $detail='Runtime first-failure summary conflicts with the first failing allocation attempt.'
                }
            } elseif ($summaryFirstFailure -ne [uint32]::MaxValue) {
                $classification='INCOMPLETE_DIAGNOSTIC_RECORD'
                $detail='Runtime summary reports a first failure but no attempt does.'
            }
            if ($backendCount -gt 0 -and (Get-TraceSummary 'backend' $invocation 1) -eq [uint32]::MaxValue) {
                $backendHasFailure = $false
                foreach ($backAttempt in $backend) { if ($backAttempt[14] -ne [uint32]::MaxValue) { $backendHasFailure=$true } }
                if ($backendHasFailure) {
                    $classification='INCOMPLETE_DIAGNOSTIC_RECORD'
                    $detail='Backend failure branch exists but its first-failure summary is missing.'
                }
            }
        }
        if ($overflow -ne [uint32]::MaxValue -and $overflow -ne 0) {
            $classification='INCOMPLETE_DIAGNOSTIC_RECORD'
            $detail='The bounded runtime recorder overflowed.'
        }
        $results += [ordered]@{ invocation=$invocation; classification=$classification; detail=$detail; firstFailure=$firstFailure; allocations=$allocationResults; runtimeAttemptCount=$count; backendAttemptCount=$backendCount }
    }
}

$report = [ordered]@{
    marker=$marker
    evidenceKind=$EvidenceKind
    guestRootCauseAccepted=($EvidenceKind -eq 'guest_serial' -and $results.Count -eq 1 -and $results[0].classification -ne 'INCOMPLETE_DIAGNOSTIC_RECORD')
    sourceRamdiskSha256='AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8'
    serialPath=[System.IO.Path]::GetFullPath($SerialPath)
    serialSha256=(Get-FileHash -LiteralPath $SerialPath -Algorithm SHA256).Hash.ToUpperInvariant()
    results=$results
}
$json = $report | ConvertTo-Json -Depth 8
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $json }
else { [System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath($OutputPath), $json, [System.Text.UTF8Encoding]::new($false)) }
