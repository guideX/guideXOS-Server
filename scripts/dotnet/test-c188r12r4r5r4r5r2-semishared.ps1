param([string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = Join-Path $root "out\dotnet\c188r12r4r5r4r5r2-semishared-attribution\hosted-$stamp"
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Path (Join-Path $testRoot 'hosted') -Force | Out-Null
$recordHeader = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.h'
$recordSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.cpp'
$recordTest = Join-Path $root 'tools\dotnet\runtime-pack\tests\c188r12r4r5r4r5r2_semishared_record_test.cpp'
$analyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2-semishared.ps1'
$runtimeBuilder = Join-Path $root 'tools\dotnet\runtime-pack\build-runtime-pack.ps1'
$candidateBuilder = Join-Path $root 'scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1'
$guestRunner = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$pinnedGcSource = Join-Path $root 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\gc\gc.cpp'
$r1Serial = Join-Path $root 'out\dotnet\c188r12r4r5r4r5-gc-init-probes\qac-guest-r4r5r1-001\serial.log'
$compiler = Get-Command 'g++.exe' -ErrorAction SilentlyContinue
if ($null -eq $compiler) { $compiler = Get-Command 'clang++.exe' -ErrorAction SilentlyContinue }
if ($null -eq $compiler) { throw 'A hosted C++ compiler (g++ or clang++) is required for the bounded record tests.' }

function Assert-True([bool]$Value,[string]$Label) {
    if (-not $Value) { throw "C188R12R4R5R4R5R2 assertion failed: $Label" }
}
function Hash-File([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function U32([uint32]$Value) { return '{0:X8}' -f $Value }
function Add-RecordLines([System.Collections.Generic.List[string]]$Lines,[uint32]$Invocation,
                         [uint32]$FirstFailure,[uint32]$Branch,[uint32]$FailureStatus,
                         [uint32]$CleanupFailure,[uint32]$CleanupStatus,
                         [object[]]$Operations,[uint32]$FinalResult=0) {
    $summary = @(
        @([uint32]0,[uint32]0x53325232,[uint32]1),
        @([uint32]1,[uint32]$Operations.Count,[uint32]0),
        @([uint32]2,$FinalResult,$FirstFailure),
        @([uint32]3,$Branch,$FailureStatus),
        @([uint32]4,[uint32]0,$CleanupFailure),
        @([uint32]5,$CleanupStatus,[uint32]$(if ($CleanupFailure -eq 0) {0} else {1})),
        @([uint32]6,[uint32]$(if ($FirstFailure -eq 0) {0} else {1}),[uint32]$(if ($FirstFailure -eq 0) {0} else {1}))
    )
    foreach ($row in $summary) {
        $Lines.Add(('[C188R12R4R5R4R5R2_SEMISHARED] invocation=' + (U32 $Invocation) +
            ' sequence=FFFFFFFF field=' + (U32 $row[0]) + ' value=' + (U32 $row[1]) +
            ' auxiliary=' + (U32 $row[2])))
    }
    for ($index=0; $index -lt $Operations.Count; $index++) {
        $op = $Operations[$index]
        $fields = @(
            @([uint32]$op.id,[uint32]$op.state),
            @([uint32]$op.result,[uint32]0),
            @(([uint32]$op.resource -bor ([uint32]$op.requestResource -shl 16)),[uint32]$op.resourceResult),
            @([uint32]$op.sizeLow,[uint32]$op.sizeHigh),
            @([uint32]$op.alignmentLow,[uint32]$op.alignmentHigh),
            @([uint32]$op.nativeStatus,[uint32]$op.nativeSource)
        )
        for ($field=0; $field -lt 6; $field++) {
            $Lines.Add(('[C188R12R4R5R4R5R2_SEMISHARED] invocation=' + (U32 $Invocation) +
                ' sequence=' + (U32 ([uint32]$index)) + ' field=' + (U32 ([uint32]$field)) +
                ' value=' + (U32 ([uint32]$fields[$field][0])) +
                ' auxiliary=' + (U32 ([uint32]$fields[$field][1]))))
        }
    }
}
function New-Operation([uint32]$Id,[bool]$Success,[uint32]$Status=[uint32]::MaxValue,[uint32]$StatusSource=0) {
    return [pscustomobject]@{
        id=$Id; state=[uint32]$(if ($Success) {2} else {3}); result=[uint32]$(if ($Success) {1} else {0})
        resource=[uint32]0; requestResource=[uint32]0; resourceResult=[uint32]::MaxValue
        sizeLow=[uint32]::MaxValue; sizeHigh=[uint32]::MaxValue
        alignmentLow=[uint32]::MaxValue; alignmentHigh=[uint32]::MaxValue
        nativeStatus=$Status; nativeSource=$StatusSource
    }
}
function Invoke-Analysis([string]$Name,[string]$Text) {
    $serial = Join-Path $testRoot "$Name.serial.log"
    $analysisPath = Join-Path $testRoot "$Name.analysis.json"
    [System.IO.File]::WriteAllText($serial,$Text,[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $serial -OutputPath $analysisPath -RecordHeaderPath $recordHeader | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Semi-shared analyzer failed for $Name." }
    return [pscustomobject]@{ serial=$serial; analysis=(Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json) }
}

foreach ($path in @($recordHeader,$recordSource,$recordTest,$analyzer,$runtimeBuilder,$candidateBuilder,$guestRunner,$pinnedGcSource,$r1Serial)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required R2 qualification input is missing: $path" }
}
$recordExe = Join-Path $testRoot 'hosted\gc-semishared-record-test.exe'
& $compiler.Source '-std=c++17' '-Wall' '-Wextra' '-Werror' '-DGUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED' $recordSource $recordTest '-o' $recordExe
if ($LASTEXITCODE -ne 0) { throw 'The bounded semi-shared record test did not compile.' }
& $recordExe
if ($LASTEXITCODE -ne 0) { throw 'The bounded semi-shared record implementation failed its hosted assertions.' }

$headerText = Get-Content -LiteralPath $recordHeader -Raw
$gcText = Get-Content -LiteralPath $pinnedGcSource -Raw
$builderText = Get-Content -LiteralPath $runtimeBuilder -Raw
$candidateText = Get-Content -LiteralPath $candidateBuilder -Raw
$runnerText = Get-Content -LiteralPath $guestRunner -Raw
Assert-True ($headerText.Contains('GX_SEMISHARED_OPERATION_CAPACITY = 48u')) 'record capacity is bounded and agrees with the staged profile'
Assert-True ($gcText.Contains('gc_heap::init_semi_shared()') -and $gcText.Contains('if (!full_gc_approach_event.CreateManualEventNoThrow(FALSE))') -and $gcText.Contains('if (!full_gc_end_event.CreateManualEventNoThrow(FALSE))')) 'pinned GC source anchors the two full-GC event branches'
Assert-True ($gcText.Contains('if (!gc_time_info)') -and $gcText.Contains('if (!bgc_time_info)') -and $gcText.Contains('if (!loh_compact_info)')) 'pinned GC source anchors the three trace-array branches'
Assert-True ($gcText.Contains('if (!background_gc_done_event.CreateManualEventNoThrow(TRUE))') -and $gcText.Contains('if (!bgc_threads_sync_event.CreateManualEventNoThrow(FALSE))') -and $gcText.Contains('if (!ee_proceed_event.CreateAutoEventNoThrow(FALSE))') -and $gcText.Contains('if (!bgc_start_event.CreateManualEventNoThrow(FALSE))')) 'pinned GC source anchors each BGC event branch'
Assert-True ($gcText.Contains('if (!g_mark_list_copy)') -and $gcText.Contains('#ifdef MULTIPLE_HEAPS')) 'mark-list-copy branch is conditionally compiled and separately identified'
Assert-True ($builderText.Contains('GX_SEMISHARED_OP_SEGMENT_TABLE') -and
    $builderText.Contains('sorted_table* res = (sorted_table*)new (nothrow) char [allocationSize]') -and
    $builderText.Contains('GX_SEMISHARED_OP_CLEANUP_FULL_GC_APPROACH_EVENT') -and
    $builderText.Contains('RecordFailureBranch')) 'instrumented source rewrite records segment-table allocation result and cleanup branches'
Assert-True ($candidateText.Contains('C188R12R4R5R4R5R2_SEMISHARED') -and $candidateText.Contains('$stagedQualificationPath') -and $candidateText.Contains('qualification-manifest.json')) 'candidate packaging records R2 and source inventory'
Assert-True ($runnerText.Contains('qac-guest-r4r5r2r1-001') -and $runnerText.Contains('guest-launch-attempt-C188R12R4R5R4R5R2R1.json')) 'future R2R1 has a distinct single-boot evidence path and guard'

$opDefinitions = [regex]::Matches($headerText,'(?m)^\s*(GX_SEMISHARED_OP_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+)u,')
$branchCases = @(
    @{id=[uint32]0x0101;name='mark-list'},
    @{id=[uint32]0x0103;name='segment-table'},
    @{id=[uint32]0x0110;name='full-gc-approach-event'},
    @{id=[uint32]0x0111;name='full-gc-end-event'},
    @{id=[uint32]0x0120;name='bgc-done-event'},
    @{id=[uint32]0x0121;name='bgc-sync-event'},
    @{id=[uint32]0x0122;name='ee-proceed-event'},
    @{id=[uint32]0x0123;name='bgc-start-event'},
    @{id=[uint32]0x0130;name='gc-time-info'},
    @{id=[uint32]0x0131;name='bgc-time-info'},
    @{id=[uint32]0x0132;name='loh-compact-info'}
)
foreach ($branch in $branchCases) {
    $ops = @((New-Operation $branch.id $false), (New-Operation ([uint32]0x01FF) $false))
    $lines = [System.Collections.Generic.List[string]]::new()
    Add-RecordLines $lines 1 $branch.id $branch.id ([uint32]::MaxValue) 0 ([uint32]::MaxValue) $ops
    $capture = Invoke-Analysis "branch-$($branch.name)" (($lines -join "`n") + "`n")
    Assert-True ($capture.analysis.outcome -eq 'failure_branch_attributed') "$($branch.name) source-defined failure branch is attributed"
    Assert-True ([uint32]([Convert]::ToUInt32($capture.analysis.resolvedCause.id.Substring(2),16)) -eq $branch.id) "$($branch.name) cause ID is preserved"
}

# Successful completion and allocation request size/alignment decoding.
$successOp = New-Operation 0x0101 $true
$successOp.resource=1; $successOp.requestResource=1; $successOp.resourceResult=1
$successOp.sizeLow=8192*8; $successOp.sizeHigh=0; $successOp.alignmentLow=8; $successOp.alignmentHigh=0
$successOps=@($successOp,(New-Operation 0x01FF $true))
$successLines=[System.Collections.Generic.List[string]]::new()
Add-RecordLines $successLines 9 0 0 ([uint32]::MaxValue) 0 ([uint32]::MaxValue) $successOps 1
$success=Invoke-Analysis 'success-and-allocation-request' (($successLines -join "`n") + "`n")
Assert-True ($success.analysis.outcome -eq 'init_semi_shared_succeeded') 'successful initializer result is recognized'
Assert-True ($success.analysis.operations[0].requestedSize -eq '0x0000000000010000' -and $success.analysis.operations[0].requestedAlignment -eq '0x0000000000000008') 'allocation request bytes and alignment are decoded'

# A successfully acquired resource precedes the failing branch; a later cleanup
# failure reports its raw PAL status without replacing that first cause.
$acquiredResource=New-Operation 0x0101 $true
$acquiredResource.resource=1; $acquiredResource.requestResource=1; $acquiredResource.resourceResult=1
$cleanupOps=@($acquiredResource, (New-Operation 0x0110 $false),
    (New-Operation 0x0171 $false 5 1), (New-Operation 0x01FF $false))
$cleanupLines=[System.Collections.Generic.List[string]]::new()
Add-RecordLines $cleanupLines 10 0x0110 0x0110 ([uint32]::MaxValue) 0x0171 5 $cleanupOps
$cleanup=Invoke-Analysis 'cleanup-failure-after-resource-acquisition' (($cleanupLines -join "`n") + "`n")
Assert-True ($cleanup.analysis.outcome -eq 'failure_branch_attributed') 'original failure remains the outcome'
Assert-True ($cleanup.analysis.operations[0].state -eq 2 -and $cleanup.analysis.operations[0].resourceResult -eq '0x00000001') 'successful resource acquisition remains visible before failure'
Assert-True ($cleanup.analysis.summary.firstFailureBranch.name -eq 'GX_SEMISHARED_OP_FULL_GC_APPROACH_EVENT') 'first explicit cause survives cleanup'
Assert-True ($cleanup.analysis.summary.cleanupFailureOperation.name -eq 'GX_SEMISHARED_OP_CLEANUP_FULL_GC_APPROACH_EVENT' -and $cleanup.analysis.summary.cleanupFailureStatus -eq '0x00000005') 'cleanup failure and raw status are separately retained'

# Missing fields, incomplete summaries, conflicts, and the archived R1 trace fail closed.
$missingOps=[System.Collections.Generic.List[string]]::new()
Add-RecordLines $missingOps 11 0x0101 0x0101 ([uint32]::MaxValue) 0 ([uint32]::MaxValue) @((New-Operation 0x0101 $false),(New-Operation 0x01FF $false))
$missingText=($missingOps | Where-Object { $_ -notmatch 'sequence=00000000 field=00000002 ' }) -join "`n"
$missing=Invoke-Analysis 'missing-operation-result-field' ($missingText+"`n")
Assert-True ($missing.analysis.outcome -eq 'malformed_or_incomplete_record') 'missing operation result marker is incomplete'
$incompleteText=($missingOps | Where-Object { $_ -notmatch 'sequence=FFFFFFFF field=00000004 ' }) -join "`n"
$incomplete=Invoke-Analysis 'incomplete-summary-record' ($incompleteText+"`n")
Assert-True ($incomplete.analysis.issues -contains 'summary record is incomplete') 'incomplete record is reported without cause inference'
$conflictText=$missingOps -join "`n"
$conflictLine=$missingOps[0] -replace 'field=00000000 value=53325232','field=00000000 value=53325231'
$conflictText += "`n$conflictLine`n"
$conflict=Invoke-Analysis 'conflicting-summary-field' ($conflictText+"`n")
Assert-True ($conflict.analysis.issues.Count -gt 0) 'conflicting duplicate record fields are rejected'
$r1HashBefore=Hash-File $r1Serial
$r1AnalysisPath=Join-Path $testRoot 'original-r4r5r1.analysis.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $r1Serial -OutputPath $r1AnalysisPath -RecordHeaderPath $recordHeader | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Analyzer failed on the archived R4R5R1 guest trace.' }
$r1=Get-Content -LiteralPath $r1AnalysisPath -Raw | ConvertFrom-Json
$r1HashAfter=Hash-File $r1Serial
Assert-True ($r1.outcome -eq 'unresolved_internally' -and $r1.priorOuterInitSemiSharedFailureObserved) 'archived R1 trace retains init_semi_shared failure without an invented internal cause'
Assert-True ($r1HashBefore -eq $r1HashAfter) 'archived raw R1 serial is unchanged'

$manifest=[ordered]@{
    result='PASS'; testRoot=$testRoot; recordImplementationTest='PASS'; recordTestExecutable=$recordExe
    recordTestSha256=Hash-File $recordExe; analyzer=$analyzer; sourceOperationIdCount=$opDefinitions.Count
    reachableFailureBranchesTested=@($branchCases | ForEach-Object name)
    analyzerFixtures=@('success','each reachable init_semi_shared branch','resource acquisition followed by initializer and cleanup failure','first cause retained','raw native event-close status','missing operation-result field','incomplete record','conflicting duplicate summary field','archived R4R5R1 trace remains unresolved internally')
    originalR1SerialSha256Before=$r1HashBefore; originalR1SerialSha256After=$r1HashAfter
    noQemuLaunched=$true; noQacReserved=$true
}
$manifestPath=Join-Path $testRoot 'hosted-test-manifest.json'
[System.IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R4R5R2 semi-shared hosted tests: PASS ($testRoot)"
