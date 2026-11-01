param(
    [Parameter(Mandatory=$true)][string]$SerialPath,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$RecordHeaderPath = (Join-Path $PSScriptRoot '..\..\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.h'),
    [ValidateRange(1,48)][int]$OperationCapacity = 48
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$serialFull = [System.IO.Path]::GetFullPath($SerialPath)
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
if ($serialFull -eq $outputFull) { throw 'Derived semi-shared analysis must use a separate path from raw serial.' }
if (-not (Test-Path -LiteralPath $serialFull -PathType Leaf)) { throw "Raw serial is missing: $serialFull" }
if (Test-Path -LiteralPath $outputFull) { throw "Refusing to overwrite derived analysis: $outputFull" }
if (-not (Test-Path -LiteralPath $RecordHeaderPath -PathType Leaf)) { throw "Source-defined operation header is missing: $RecordHeaderPath" }

$headerText = Get-Content -LiteralPath $RecordHeaderPath -Raw
$operationNames = @{}
[regex]::Matches($headerText,'(?m)^\s*(GX_SEMISHARED_OP_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+)u,') | ForEach-Object {
    $operationNames[[Convert]::ToUInt32($_.Groups[2].Value.Substring(2),16)] = $_.Groups[1].Value
}
$statusSourceNames = @{}
[regex]::Matches($headerText,'(?m)^\s*(GX_SEMISHARED_STATUS_SOURCE_[A-Z0-9_]+)\s*=\s*(\d+)u,') | ForEach-Object {
    $statusSourceNames[[uint32]$_.Groups[2].Value] = $_.Groups[1].Value
}

$raw = [System.IO.File]::ReadAllBytes($serialFull)
$sha = [System.Security.Cryptography.SHA256]::Create()
try { $serialSha256 = ([BitConverter]::ToString($sha.ComputeHash($raw))).Replace('-','') } finally { $sha.Dispose() }
$text = [System.Text.Encoding]::UTF8.GetString($raw)
$lines = @($text -split "`r?`n")
$markerLines = [System.Collections.Generic.List[object]]::new()
$malformed = [System.Collections.Generic.List[string]]::new()
$pattern = '^\[C188R12R4R5R4R5R2_SEMISHARED\] invocation=([0-9A-F]{8}) sequence=([0-9A-F]{8}) field=([0-9A-F]{8}) value=([0-9A-F]{8}) auxiliary=([0-9A-F]{8})$'
for ($i=0; $i -lt $lines.Count; $i++) {
    $line = [string]$lines[$i]
    if (-not $line.StartsWith('[C188R12R4R5R4R5R2_SEMISHARED]',[System.StringComparison]::Ordinal)) { continue }
    $match = [regex]::Match($line,$pattern)
    if (-not $match.Success) { $malformed.Add("line $($i + 1): malformed semi-shared marker"); continue }
    $markerLines.Add([pscustomobject]@{
        line=$i + 1
        invocation=[Convert]::ToUInt32($match.Groups[1].Value,16)
        sequence=[Convert]::ToUInt32($match.Groups[2].Value,16)
        field=[Convert]::ToUInt32($match.Groups[3].Value,16)
        value=[Convert]::ToUInt32($match.Groups[4].Value,16)
        auxiliary=[Convert]::ToUInt32($match.Groups[5].Value,16)
    })
}

$issues = [System.Collections.Generic.List[string]]::new()
foreach ($item in $malformed) { $issues.Add($item) }
$summaryLines = @($markerLines | Where-Object { [uint64]$_.sequence -eq 4294967295 })
$operationLines = @($markerLines | Where-Object { [uint64]$_.sequence -ne 4294967295 })
$priorR1 = $text.Contains('[C188R12R4R5R1_INIT]')
$outerSemiFailure = $text -match '\[C188R12R4R5R1_INIT\] invocation=[0-9A-F]{8} stage=00000032 event=00000003 code=80004005'

$summary = $null
$operations = @()
$outcome = 'unresolved_internally'
if ($markerLines.Count -gt 0) {
    $summaryValues = @{}
    $summaryAux = @{}
    foreach ($line in $summaryLines) {
        if ($line.field -gt 6) { $issues.Add("summary marker field $($line.field) is not source-defined") ; continue }
        if ($summaryValues.ContainsKey($line.field)) { $issues.Add("duplicate summary marker field $($line.field)") ; continue }
        $summaryValues[$line.field] = $line.value
        $summaryAux[$line.field] = $line.auxiliary
    }
    if ($summaryLines.Count -ne 7 -or (@(0..6 | Where-Object { -not $summaryValues.ContainsKey([uint32]$_) }).Count -gt 0)) {
        $issues.Add('summary record is incomplete')
    }
    $invocations = @($markerLines | Select-Object -ExpandProperty invocation -Unique)
    if ($invocations.Count -ne 1) { $issues.Add('record contains conflicting invocation identifiers') }
    if ($summaryValues.ContainsKey(0u) -and $summaryValues[0u] -ne [uint32]0x53325232) { $issues.Add('summary magic does not match the source contract') }
    if ($summaryAux.ContainsKey(0u) -and $summaryAux[0u] -ne 1u) { $issues.Add('summary version does not match the source contract') }
    $count = if ($summaryValues.ContainsKey(1u)) { $summaryValues[1u] } else { 0u }
    $overflow = if ($summaryAux.ContainsKey(1u)) { $summaryAux[1u] } else { 1u }
    if ($count -gt $OperationCapacity) { $issues.Add("operation count $count exceeds capacity $OperationCapacity") }
    if ($overflow -ne 0u) { $issues.Add('bounded operation record overflow is set') }
    if ($operationLines.Count -ne ($count * 6)) { $issues.Add('one or more operation field markers are missing or duplicated') }

    $orderedLines = @($markerLines | Sort-Object line)
    $summarySeenOperation = $false
    $lastSequence = -1
    $lastField = -1
    foreach ($line in $orderedLines) {
        if ($line.sequence -eq [uint32]::MaxValue) {
            if ($summarySeenOperation) { $issues.Add('summary fields appear after operation fields') }
            continue
        }
        $summarySeenOperation = $true
        if ($line.sequence -gt $count -or $line.field -gt 5) { $issues.Add('operation sequence or field is outside the source contract'); continue }
        if ([int64]$line.sequence -lt $lastSequence -or
            ([int64]$line.sequence -eq $lastSequence -and [int64]$line.field -le $lastField) -or
            ([int64]$line.sequence -gt $lastSequence -and $line.field -ne 0u)) {
            $issues.Add('operation fields are out of sequence or out of field order')
        }
        $lastSequence = [int64]$line.sequence
        $lastField = [int64]$line.field
    }

    for ($index=0; $index -lt [Math]::Min([int]$count,$OperationCapacity); $index++) {
        $items = @($operationLines | Where-Object { [uint32]$_.sequence -eq [uint32]$index })
        $values = @{}
        foreach ($item in $items) {
            if ($item.field -gt 5) { continue }
            if ($values.ContainsKey($item.field)) { $issues.Add("operation $index has duplicate field $($item.field)"); continue }
            $values[$item.field] = $item
        }
        if ($items.Count -ne 6 -or (@(0..5 | Where-Object { -not $values.ContainsKey([uint32]$_) }).Count -gt 0)) {
            $issues.Add("operation $index is incomplete")
            continue
        }
        $id = $values[0u].value
        if (-not $operationNames.ContainsKey($id)) { $issues.Add("operation $index uses an ID absent from the source header") }
        $state = $values[0u].auxiliary
        $result = $values[1u].value
        $detail = $values[1u].auxiliary
        $resourcePair = $values[2u].value
        $resourceResult = $values[2u].auxiliary
        $nativeStatus = $values[5u].value
        $nativeSource = $values[5u].auxiliary
        if ($state -lt 1u -or $state -gt 4u) { $issues.Add("operation $index has an unknown state") }
        if ($state -eq 1u -and $result -ne [uint32]::MaxValue) { $issues.Add("operation $index has a result without completion") }
        if ($state -eq 2u -and $result -ne 1u) { $issues.Add("operation $index success state conflicts with its result") }
        if ($state -eq 3u -and $result -ne 0u) { $issues.Add("operation $index failure state conflicts with its result") }
        $operations += [pscustomobject]@{
            sequence=$index
            id=('0x{0:X4}' -f $id)
            name=[string]$operationNames[$id]
            state=$state
            result=('0x{0:X8}' -f $result)
            detail=('0x{0:X8}' -f $detail)
            resource=('0x{0:X4}' -f ($resourcePair -band 0xFFFFu))
            requestResource=('0x{0:X4}' -f (($resourcePair -shr 16) -band 0xFFFFu))
            resourceResult=('0x{0:X8}' -f $resourceResult)
            requestedSize=('0x{0:X16}' -f (([uint64]$values[3u].auxiliary -shl 32) -bor [uint64]$values[3u].value))
            requestedAlignment=('0x{0:X16}' -f (([uint64]$values[4u].auxiliary -shl 32) -bor [uint64]$values[4u].value))
            nativeStatus=('0x{0:X8}' -f $nativeStatus)
            nativeStatusSource=if ($statusSourceNames.ContainsKey($nativeSource)) { [string]$statusSourceNames[$nativeSource] } else { 'unknown' }
            sourceLine=$values[0u].line
        }
    }

    $firstFailed = if ($summaryValues.ContainsKey(2u)) { $summaryAux[2u] } else { 0u }
    $firstBranch = if ($summaryValues.ContainsKey(3u)) { $summaryValues[3u] } else { 0u }
    $cleanupFailed = if ($summaryValues.ContainsKey(4u)) { $summaryAux[4u] } else { 0u }
    $finalResult = if ($summaryValues.ContainsKey(2u)) { $summaryValues[2u] } else { [uint32]::MaxValue }
    $failureOperation = @($operations | Where-Object { $_.state -eq 3u } | Select-Object -First 1)
    if ($firstFailed -eq 0u -and $null -ne $failureOperation -and $failureOperation.Count -gt 0) { $issues.Add('summary omitted the first failed operation') }
    if ($firstFailed -ne 0u -and ($null -eq $failureOperation -or $failureOperation.Count -eq 0 -or $failureOperation[0].id -ne ('0x{0:X4}' -f $firstFailed))) {
        $issues.Add('summary first-failed operation conflicts with operation order')
    }
    if ($firstBranch -ne 0u -and (-not $operationNames.ContainsKey($firstBranch) -or
        @($operations | Where-Object { $_.id -eq ('0x{0:X4}' -f $firstBranch) -and $_.state -eq 3u }).Count -eq 0)) {
        $issues.Add('first failure branch is not a source-defined failed operation')
    }
    if ($cleanupFailed -ne 0u -and @($operations | Where-Object { $_.id -eq ('0x{0:X4}' -f $cleanupFailed) -and $_.state -eq 3u }).Count -eq 0) {
        $issues.Add('cleanup failure summary has no matching failed cleanup operation')
    }
    if ($summaryValues.ContainsKey(5u) -and $null -ne $failureOperation -and $failureOperation.Count -gt 0) {
        $expectedStatus = [Convert]::ToUInt32($failureOperation[0].nativeStatus.Substring(2),16)
        if ($summaryValues[3u] -ne $firstBranch) { $issues.Add('first failure summary fields conflict') }
        if ($summaryAux[3u] -ne $expectedStatus) { $issues.Add('first failure status was not preserved from the first failed operation') }
    }
    $cleanupEntered = if ($summaryValues.ContainsKey(6u)) { $summaryValues[6u] } else { 0u }
    $cleanupCompleted = if ($summaryAux.ContainsKey(6u)) { $summaryAux[6u] } else { 0u }
    if (($cleanupCompleted -band (-bnot $cleanupEntered)) -ne 0u) { $issues.Add('cleanup completion is recorded without cleanup entry') }
    if ($finalResult -ne 0u -and $finalResult -ne 1u) { $issues.Add('final initializer result is missing or invalid') }
    if (@($operations | Where-Object { $_.name -eq 'GX_SEMISHARED_OP_FINAL_RESULT' -and $_.result -eq ('0x{0:X8}' -f $finalResult) }).Count -ne 1) {
        $issues.Add('final result operation is missing or conflicts with the summary')
    }
    $summary = [ordered]@{
        invocation=if ($invocations.Count -eq 1) { '0x{0:X8}' -f $invocations[0] } else { $null }
        operationCount=$count
        overflow=$overflow
        finalResult=('0x{0:X8}' -f $finalResult)
        firstFailedOperation=if ($firstFailed -eq 0u) { $null } elseif ($operationNames.ContainsKey($firstFailed)) { [ordered]@{ id=('0x{0:X4}' -f $firstFailed); name=$operationNames[$firstFailed] } } else { ('0x{0:X4}' -f $firstFailed) }
        firstFailureBranch=if ($firstBranch -eq 0u) { $null } elseif ($operationNames.ContainsKey($firstBranch)) { [ordered]@{ id=('0x{0:X4}' -f $firstBranch); name=$operationNames[$firstBranch] } } else { ('0x{0:X4}' -f $firstBranch) }
        firstFailureStatus=('0x{0:X8}' -f $(if ($summaryValues.ContainsKey(3u)) {$summaryAux[3u]} else {[uint32]::MaxValue}))
        cleanupFailureOperation=if ($cleanupFailed -eq 0u) { $null } elseif ($operationNames.ContainsKey($cleanupFailed)) { [ordered]@{ id=('0x{0:X4}' -f $cleanupFailed); name=$operationNames[$cleanupFailed] } } else { ('0x{0:X4}' -f $cleanupFailed) }
        cleanupFailureStatus=('0x{0:X8}' -f $(if ($summaryValues.ContainsKey(5u)) {$summaryValues[5u]} else {[uint32]::MaxValue}))
        cleanupEnteredMask=('0x{0:X8}' -f $cleanupEntered)
        cleanupCompletedMask=('0x{0:X8}' -f $cleanupCompleted)
    }
    if ($issues.Count -eq 0) {
        $outcome = if ($finalResult -eq 1u) { 'init_semi_shared_succeeded' } elseif ($firstBranch -ne 0u) { 'failure_branch_attributed' } else { 'initializer_failed_without_explicit_branch' }
    } else { $outcome = 'malformed_or_incomplete_record' }
}

$analysis = [ordered]@{
    schema='C188R12R4R5R4R5R2_SEMISHARED_ANALYSIS_V1'
    marker='C188R12R4R5R4R5R2_SEMISHARED'
    serialPath=$serialFull
    serialSha256=$serialSha256
    sourceOperationHeader=$RecordHeaderPath
    sourceOperationHeaderSha256=(Get-FileHash -LiteralPath $RecordHeaderPath -Algorithm SHA256).Hash.ToUpperInvariant()
    sourceDefinedOperationIds=@($operationNames.GetEnumerator() | Sort-Object Value | ForEach-Object { [ordered]@{ id=('0x{0:X4}' -f $_.Key); name=$_.Value } })
    markerCount=$markerLines.Count
    priorR1TracePresent=$priorR1
    priorOuterInitSemiSharedFailureObserved=$outerSemiFailure
    outcome=$outcome
    resolvedCause=if ($outcome -eq 'failure_branch_attributed') { $summary.firstFailureBranch } else { $null }
    summary=$summary
    operations=@($operations)
    issues=@($issues)
    rawSerialPreserved=$true
}
$parent = Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
[System.IO.File]::WriteAllText($outputFull,($analysis | ConvertTo-Json -Depth 12),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R4R5R2_SEMISHARED_ANALYSIS=$outputFull OUTCOME=$outcome MARKERS=$($markerLines.Count)"
