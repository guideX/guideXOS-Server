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
function Get-TraceSummaryAuxiliary([string]$Source, [string]$Invocation, [uint32]$Field) {
    $summaryField = if ($Source -ceq 'runtime') { $Field } else { 0x80000000u -bor $Field }
    $key = "$Source/$Invocation/$([uint32]::MaxValue)/$summaryField"
    if ($auxiliaryRows.ContainsKey($key)) { return [uint32]$auxiliaryRows[$key] }
    return [uint32]::MaxValue
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
        $backendSchemaV2 = Test-TraceFieldPresent 'backend' $invocation ([uint32]::MaxValue) 0x80000002u
        $backendAttemptCounts = if ($backendSchemaV2) {
            [ordered]@{ attempted=(Get-TraceSummary 'backend' $invocation 2); completed=(Get-TraceSummaryAuxiliary 'backend' $invocation 2) }
        } else { $null }
        $backendOutcomeCounts = if ($backendSchemaV2) {
            [ordered]@{ succeeded=(Get-TraceSummary 'backend' $invocation 3); failed=(Get-TraceSummaryAuxiliary 'backend' $invocation 3) }
        } else { $null }
        $backendOperationCounts = if ($backendSchemaV2) {
            [ordered]@{
                reserve=(Get-TraceSummary 'backend' $invocation 4)
                commit=(Get-TraceSummaryAuxiliary 'backend' $invocation 4)
                rollbackRelease=(Get-TraceSummary 'backend' $invocation 5)
            }
        } else { $null }
        $backendFirstDroppedAttempt = if ($backendSchemaV2) {
            Get-TraceSummaryAuxiliary 'backend' $invocation 5
        } elseif ($backendOverflow -gt 0 -and $backendCount -eq 9) {
            [uint32]$backendCount
        } else { [uint32]::MaxValue }
        $backendTerminalStatus = if ($backendSchemaV2) {
            Get-TraceSummary 'backend' $invocation 6
        } else { [uint32]::MaxValue }
        $backendAggregateComplete = if ($backendSchemaV2) {
            Get-TraceSummaryAuxiliary 'backend' $invocation 6
        } else { [uint32]::MaxValue }
        $backendFirstFailureCode = if ($backendSchemaV2) {
            [ordered]@{
                operation=(Get-TraceSummary 'backend' $invocation 7)
                branch=(Get-TraceSummaryAuxiliary 'backend' $invocation 7)
                status=(Get-TraceSummary 'backend' $invocation 8)
                statusSource=(Get-TraceSummaryAuxiliary 'backend' $invocation 8)
            }
        } else { $null }
        $backendDistinctFailureCount = if ($backendSchemaV2) {
            Get-TraceSummary 'backend' $invocation 9
        } else { [uint32]::MaxValue }
        $backendDistinctFailureOverflow = if ($backendSchemaV2) {
            Get-TraceSummaryAuxiliary 'backend' $invocation 9
        } else { [uint32]::MaxValue }
        $backendCounterSaturated = if ($backendSchemaV2) {
            Get-TraceSummary 'backend' $invocation 10
        } else { [uint32]::MaxValue }
        $backendOutcomeTotal = if ($backendSchemaV2 -and
            $backendOutcomeCounts.succeeded -ne [uint32]::MaxValue -and
            $backendOutcomeCounts.failed -ne [uint32]::MaxValue) {
            [UInt64]$backendOutcomeCounts.succeeded + [UInt64]$backendOutcomeCounts.failed
        } else { [UInt64]::MaxValue }
        $backendAttemptedWide = if ($backendSchemaV2 -and
            $backendAttemptCounts.attempted -ne [uint32]::MaxValue) {
            [UInt64]$backendAttemptCounts.attempted
        } else { [UInt64]::MaxValue }
        $backendOperationCountsConsistent = $false
        if ($backendSchemaV2 -and $backendAttemptedWide -ne [UInt64]::MaxValue -and
            $backendOperationCounts.reserve -ne [uint32]::MaxValue -and
            $backendOperationCounts.commit -ne [uint32]::MaxValue -and
            $backendOperationCounts.rollbackRelease -ne [uint32]::MaxValue) {
            $backendOperationCountsConsistent =
                [UInt64]$backendOperationCounts.reserve -le $backendAttemptedWide -and
                [UInt64]$backendOperationCounts.commit -le [UInt64]$backendOperationCounts.reserve -and
                [UInt64]$backendOperationCounts.rollbackRelease -le [UInt64]$backendOperationCounts.commit
        }
        $backendTerminalConsistent = $false
        if ($backendSchemaV2 -and $backendAttemptedWide -ne [UInt64]::MaxValue -and
            $backendOutcomeTotal -ne [UInt64]::MaxValue -and
            $backendDistinctFailureCount -ne [uint32]::MaxValue) {
            if ($backendTerminalStatus -eq 1u) {
                $backendTerminalConsistent = $backendOutcomeCounts.failed -eq 0u -and
                    $backendOutcomeCounts.succeeded -eq $backendAttemptCounts.attempted -and
                    $backendDistinctFailureCount -eq 0u
            } elseif ($backendTerminalStatus -eq 2u) {
                $backendTerminalConsistent = $backendOutcomeCounts.failed -gt 0u -and
                    $backendDistinctFailureCount -gt 0u
            }
        }
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
        if ($backendCount -ne [uint32]::MaxValue -and $backendCount -le 9) {
            for ($index=0; $index -lt $backendCount; $index++) {
                $fields = @{}
                for ($field=0; $field -lt 34; $field++) {
                    $fields[$field] = Get-TraceField 'backend' $invocation ([uint32]$index) ([uint32]$field)
                }
                $backend += ,$fields
            }
        }

        $backendRecordComplete = ($backendCount -ne [uint32]::MaxValue -and
            $backendCount -le 9 -and $backend.Count -eq $backendCount)
        if ($backendSchemaV2) {
            $backendRecordComplete = $backendRecordComplete -and
                $backendOverflow -ne [uint32]::MaxValue -and
                $backendAttemptCounts.attempted -ne [uint32]::MaxValue -and
                $backendAttemptCounts.completed -eq $backendAttemptCounts.attempted -and
                [UInt64]$backendAttemptCounts.attempted -eq ([UInt64]$backendCount + [UInt64]$backendOverflow) -and
                $backendOutcomeCounts.succeeded -ne [uint32]::MaxValue -and
                $backendOutcomeCounts.failed -ne [uint32]::MaxValue -and
                $backendOutcomeTotal -eq $backendAttemptedWide -and
                $backendOperationCountsConsistent -and
                $backendFirstDroppedAttempt -eq $(if ($backendOverflow -gt 0) { [uint32]$backendCount } else { [uint32]::MaxValue }) -and
                $backendTerminalStatus -in @(1u,2u) -and $backendTerminalConsistent -and
                $backendAggregateComplete -eq 1u -and
                $backendDistinctFailureCount -le 32u -and
                $backendDistinctFailureOverflow -eq 0u -and $backendCounterSaturated -eq 0u
        } else {
            $backendRecordComplete = $backendRecordComplete -and $backendOverflow -eq 0
        }
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
                if ($backendAttempt[15] -eq [uint32]::MaxValue -or
                    $backendAttempt[16] -eq [uint32]::MaxValue) {
                    $backendRecordComplete = $false
                }
            } elseif ($backendAttempt[9] -ne 0 -or $backendAttempt[10] -ne 0x1000 -or
                      $backendAttempt[11] -ne 0 -or $backendAttempt[13] -ne 1) {
                $backendRecordComplete = $false
            }
        }
        if ($backendFirstFailureIndex -eq [uint32]::MaxValue) {
            if ($backendFailureIndexes.Count -ne 0 -or
                ($backendSchemaV2 -and ($backendDistinctFailureCount -ne 0u -or
                    $backendFirstFailureCode.operation -ne [uint32]::MaxValue -or
                    $backendFirstFailureCode.branch -ne [uint32]::MaxValue -or
                    $backendFirstFailureCode.status -ne [uint32]::MaxValue -or
                    $backendFirstFailureCode.statusSource -ne 0u))) {
                $backendRecordComplete = $false
            }
        } elseif ($backendSchemaV2) {
            if ($backendFirstFailureIndex -ge $backendAttemptCounts.attempted -or
                $backendDistinctFailureCount -eq 0u -or
                $backendFirstFailureCode.operation -eq [uint32]::MaxValue -or
                $backendFirstFailureCode.branch -eq [uint32]::MaxValue -or
                $backendFirstFailureCode.status -eq [uint32]::MaxValue -or
                $backendFirstFailureCode.statusSource -eq [uint32]::MaxValue -or
                ($backendFirstFailureIndex -lt $backend.Count -and
                    ($backend[$backendFirstFailureIndex][14] -ne $backendFirstFailureCode.operation -or
                     $backend[$backendFirstFailureIndex][15] -ne $backendFirstFailureCode.branch -or
                     $backend[$backendFirstFailureIndex][16] -ne $backendFirstFailureCode.status -or
                     $backend[$backendFirstFailureIndex][17] -ne $backendFirstFailureCode.statusSource)) -or
                ($backendFailureIndexes.Count -gt 0 -and
                    ($backendFailureIndexes | Measure-Object -Minimum).Minimum -ne $backendFirstFailureIndex)) {
                $backendRecordComplete = $false
            }
        } elseif ($backendFirstFailureIndex -ge $backend.Count -or
            $backendFailureIndexes.Count -ne 1 -or
            $backendFailureIndexes[0] -ne $backendFirstFailureIndex) {
            $backendRecordComplete = $false
        }

        $backendFailureCodes = @()
        if ($backendSchemaV2 -and $backendDistinctFailureCount -le 32u) {
            for ($failureIndex=0; $failureIndex -lt $backendDistinctFailureCount; $failureIndex++) {
                $baseField = [uint32](0x80000100u + (2u * [uint32]$failureIndex))
                $operationKey = "backend/$invocation/$([uint32]::MaxValue)/$baseField"
                $statusKey = "backend/$invocation/$([uint32]::MaxValue)/$([uint32]($baseField + 1u))"
                if (-not $rows.ContainsKey($operationKey) -or -not $auxiliaryRows.ContainsKey($operationKey) -or
                    -not $rows.ContainsKey($statusKey) -or -not $auxiliaryRows.ContainsKey($statusKey)) {
                    $backendRecordComplete = $false
                    continue
                }
                $backendFailureCodes += [ordered]@{
                    operation=[uint32]$rows[$operationKey]; branch=[uint32]$auxiliaryRows[$operationKey]
                    status=[uint32]$rows[$statusKey]; statusSource=[uint32]$auxiliaryRows[$statusKey]
                }
            }
            if ($backendFailureCodes.Count -ne $backendDistinctFailureCount) { $backendRecordComplete = $false }
            $distinctFailureKeys = @($backendFailureCodes | ForEach-Object {
                '{0:X8}/{1:X8}/{2:X8}/{3:X8}' -f $_.operation,$_.branch,$_.status,$_.statusSource
            })
            if (@($distinctFailureKeys | Sort-Object -Unique).Count -ne $distinctFailureKeys.Count) {
                $backendRecordComplete = $false
            }
            if ($backendFirstFailureIndex -ne [uint32]::MaxValue -and
                ($backendFailureCodes.Count -eq 0 -or
                 $backendFailureCodes[0].operation -ne $backendFirstFailureCode.operation -or
                 $backendFailureCodes[0].branch -ne $backendFirstFailureCode.branch -or
                 $backendFailureCodes[0].status -ne $backendFirstFailureCode.status -or
                 $backendFailureCodes[0].statusSource -ne $backendFirstFailureCode.statusSource)) {
                $backendRecordComplete = $false
            }
        }

        $classification = 'INCOMPLETE_DIAGNOSTIC_RECORD'
        $detail = 'A summary, bounded attempt, or required source field is missing or overflowed.'
        $firstFailure = $null
        $allocationResults = @()
        if (-not $summaryMissing -and $overflow -eq 0 -and $count -gt 0 -and $count -le 8 -and
            $backendRecordComplete) {
            $classification = 'SUCCESS'
            $detail = if ($backendOverflow -gt 0 -and $backendSchemaV2) {
                'All recorded 8-byte event-object attempts completed; detailed backend rows were bounded and the aggregate summary accounts for every omitted row.'
            } else {
                'All captured 8-byte event objects completed through the configured PAL and true-VM reserve/commit path.'
            }
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
                              ($backendFirstFailureIndex -lt $backend.Count -or $backendSchemaV2)) {
                        $backAttempt = if ($backendFirstFailureIndex -lt $backend.Count) {
                            $backend[$backendFirstFailureIndex]
                        } else { $null }
                        $operation = if ($null -ne $backAttempt) { $backAttempt[14] } else { $backendFirstFailureCode.operation }
                        $branch = if ($null -ne $backAttempt) { $backAttempt[15] } else { $backendFirstFailureCode.branch }
                        $status = if ($null -ne $backAttempt) { $backAttempt[16] } else { $backendFirstFailureCode.status }
                        $statusSource = if ($null -ne $backAttempt) { $backAttempt[17] } else { $backendFirstFailureCode.statusSource }
                        if ($operation -eq 0x0204 -and $branch -eq 0x0310) {
                            if ($null -eq $backAttempt) {
                                $attemptClass='KERNEL_PAL_SLOT_CAPACITY_SUMMARIZED'
                            } elseif ($backAttempt[4] -eq 0 -or $backAttempt[5] -ne $backAttempt[4] -or
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
                                backendAttemptDetails=if ($null -eq $backAttempt) { 'bounded summary; row not retained' } else { 'detailed row retained' }
                                requestSize=if ($null -ne $backAttempt) { '0x{0:X}' -f $backAttempt[1] } else { $null }
                                slotCapacity=if ($null -ne $backAttempt) { [int]$backAttempt[4] } else { $null }
                                activeSlots=if ($null -ne $backAttempt) { [int]$backAttempt[5] } else { $null }
                                freeSlotIndex=if ($null -ne $backAttempt) { '0x{0:X8}' -f $backAttempt[6] } else { $null }
                                addressSpaceReady=if ($null -ne $backAttempt) { [bool]$backAttempt[7] } else { $null }
                                vmRegionCapacity=if ($null -ne $backAttempt) { [int]$backAttempt[8] } else { $null }
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
        $backendTerminalName = switch ($backendTerminalStatus) {
            1u { 'success' }
            2u { 'runtime_failure' }
            3u { 'incomplete' }
            default { 'unavailable' }
        }
        $results += [ordered]@{
            invocation=$invocation; classification=$classification; detail=$detail
            firstFailure=$firstFailure; allocations=$allocationResults
            runtimeAttemptCount=$count; backendAttemptCount=$backendCount
            backendRecordSchema=if ($backendSchemaV2) { 'bounded_summary_v2' } else { 'legacy_rows_v1' }
            backendSummary=[ordered]@{
                detailedRows=$backendCount
                droppedDetailCount=if ($backendSchemaV2) { $backendOverflow } else { $null }
                attemptedCount=if ($backendSchemaV2) { $backendAttemptCounts.attempted }
                    elseif ($backendOverflow -eq 0) { $backendCount } else { $null }
                attemptedCountExact=[bool]($backendSchemaV2 -or $backendOverflow -eq 0)
                minimumAttemptCount=if ($backendSchemaV2) { $backendAttemptCounts.attempted }
                    elseif ($backendOverflow -gt 0) { [int]$backendCount + 1 } else { $backendCount }
                firstDroppedAttempt=if ($backendFirstDroppedAttempt -eq [uint32]::MaxValue) { $null } else { $backendFirstDroppedAttempt }
                firstFailureAttempt=if ($backendFirstFailureIndex -eq [uint32]::MaxValue) { $null } else { $backendFirstFailureIndex }
                successfulCount=if ($backendSchemaV2) { $backendOutcomeCounts.succeeded } else { $null }
                failedCount=if ($backendSchemaV2) { $backendOutcomeCounts.failed } else { $null }
                operationCounts=$backendOperationCounts
                firstFailureCode=if ($backendSchemaV2 -and $backendFirstFailureIndex -ne [uint32]::MaxValue) { $backendFirstFailureCode } else { $null }
                distinctFailureCount=if ($backendSchemaV2) { $backendDistinctFailureCount } else { $null }
                distinctFailureCodes=$backendFailureCodes
                terminalStatus=$backendTerminalName
                aggregateComplete=[bool]($backendSchemaV2 -and $backendAggregateComplete -eq 1u -and $backendRecordComplete)
                legacyOverflowFlag=if (-not $backendSchemaV2 -and $backendOverflow -ne [uint32]::MaxValue) { $backendOverflow } else { $null }
            }
        }
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
