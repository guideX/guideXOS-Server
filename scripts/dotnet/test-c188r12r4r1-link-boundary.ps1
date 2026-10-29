param(
    [string]$DotNetExe = 'dotnet'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$project = Join-Path $repo 'samples\managed\HostLogProof\HostLogProof.csproj'

function Get-DirectPInvokeItems([string[]]$Properties) {
    $output = (& $DotNetExe msbuild $project -getItem:DirectPInvoke @Properties 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild DirectPInvoke evaluation failed: $output"
    }
    try {
        $result = $output | ConvertFrom-Json
    } catch {
        throw "MSBuild did not return DirectPInvoke JSON: $output"
    }
    return @($result.Items.DirectPInvoke | ForEach-Object { [string]$_.Identity })
}

$guestItems = Get-DirectPInvokeItems @(
    '-p:PublishAot=true',
    '-p:RuntimeIdentifier=win-x64',
    '-p:HostLogProofMode=C160Composite',
    '-p:HostLogProofC188R12R4FactoryAttribution=true'
)
if ($guestItems -notcontains '__Internal') {
    throw 'Diagnostic NativeAOT build must bind the guest __Internal instrumentation imports directly.'
}

$hostItems = Get-DirectPInvokeItems @(
    '-p:PublishAot=false',
    '-p:RuntimeIdentifier=win-x64',
    '-p:HostLogProofMode=C160Composite',
    '-p:HostLogProofC188R12R4FactoryAttribution=true',
    '-p:HostLogProofC188R12R4FactoryTests=true'
)
if ($hostItems -contains '__Internal') {
    throw 'Windows-hosted factory tests must retain the managed capture backend without guest direct imports.'
}

$productionItems = Get-DirectPInvokeItems @(
    '-p:PublishAot=true',
    '-p:RuntimeIdentifier=win-x64',
    '-p:HostLogProofMode=C160Composite'
)
if ($productionItems -contains '__Internal') {
    throw 'Proof-disabled production must not enable the C188R12R4 guest instrumentation import.'
}

$runtimeSourcePath = Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'
$runtimeSource = [System.IO.File]::ReadAllText($runtimeSourcePath)
$emitterMatch = [regex]::Match($runtimeSource,
    'static void c188r12r4EmitAllocation\([\s\S]*?\n\}')
if (-not $emitterMatch.Success) {
    throw 'C188R12R4 allocation serial emitter was not found.'
}
$emitter = $emitterMatch.Value
if (-not $emitter.Contains('persistentSerialPutString(') -or
    -not $emitter.Contains('persistentSerialPutHex64(') -or
    $emitter -match '\b(new|malloc|calloc|realloc|RhpNew)\b') {
    throw 'C188R12R4 allocation evidence must use fixed serial primitives without managed/heap allocation.'
}

$callSiteMatches = [regex]::Matches($runtimeSource,
    'c188r12r4EmitAllocation\("(?<kind>[^"]+)",\s*"(?<stage>[^"]+)"')
$actualAllocationStages = @($callSiteMatches | ForEach-Object {
    "$($_.Groups['kind'].Value):$($_.Groups['stage'].Value)"
} | Sort-Object)
$expectedAllocationStages = @(
    'array:context-invalid',
    'array:fast-capacity-miss',
    'object:allocation-result',
    'object:fast-capacity-miss') | Sort-Object
if ($callSiteMatches.Count -ne 4 -or
    (Compare-Object $expectedAllocationStages $actualAllocationStages)) {
    throw "C188R12R4 allocation diagnostic call sites changed: $($actualAllocationStages -join ', ')"
}

# Every callsite uses fixed kind/stage/result literals. The largest current
# result label is included in the fixed-format line bound below.
$resultLabels = @('bounded-failfast', 'RhpGcAlloc-fallback-entered',
    'null', 'returned-in-arena', 'returned-outside-arena')
$maxResultLength = ($resultLabels | ForEach-Object { $_.Length } |
    Measure-Object -Maximum).Maximum
$maxAllocationLineBytes = 'C188R12R4_ALLOC invocation='.Length + 16 +
    ' selector='.Length + 16 + ' operation='.Length + 16 +
    ' kind='.Length + 'object'.Length + ' stage='.Length +
    'fast-capacity-miss'.Length + ' size='.Length + 16 +
    ' cursor='.Length + 16 + ' available='.Length + 16 +
    ' result='.Length + $maxResultLength + 1
if ($maxAllocationLineBytes -gt 256) {
    throw "C188R12R4 allocation diagnostic line exceeds its 256-byte bound: $maxAllocationLineBytes"
}

$predicatePath = Join-Path $PSScriptRoot 'c188r12r4-factory-predicates.ps1'
. $predicatePath
$sampleSerial = @(
    'C188R12R4_FACTORY invocation=123 selector=5 operation=0 stage=factory-invoke code=0 result=begin',
    'C188R12R4_FACTORY invocation=123 selector=5 operation=1 stage=type-init-enter code=0 result=begin',
    'C188R12R4_FACTORY invocation=123 selector=5 operation=2 stage=type-init-return code=0 result=complete',
    'C188R12R4_FACTORY invocation=123 selector=5 operation=2 stage=object-allocation-request code=0 result=new-expression',
    'C188R12R4_FACTORY invocation=123 selector=5 operation=2 stage=base-constructor-enter code=0 result=entered',
    'C188R12R4_FACTORY invocation=123 selector=5 operation=3 stage=constructor-enter code=0 result=entered',
    'C188R12R4_ALLOC invocation=123 selector=5 operation=3 kind=object stage=fast-capacity-miss size=0000000000000028 cursor=0000000010001000 available=0000000000000000 result=RhpGcAlloc-fallback-entered') -join "`n"
$parsedEvidence = Get-C188R12R4FactoryEvidence -SerialText $sampleSerial -CaptureState 'complete'
if ($parsedEvidence.allocationEvents.Count -ne 1 -or
    $parsedEvidence.allocationEvents[0].kind -ne 'object' -or
    $parsedEvidence.allocationEvents[0].stage -ne 'fast-capacity-miss' -or
    $parsedEvidence.allocationEvents[0].size -ne '0000000000000028' -or
    $parsedEvidence.firstIncompleteOperation -ne 'factory:model-init-enter') {
    throw 'C188R12R4 guest evidence parser did not preserve allocation data and first missing factory stage.'
}

$handoffSource = [System.IO.File]::ReadAllText(
    (Join-Path $PSScriptRoot 'run-c188r12r1-settings-handoff.ps1'))
if (-not $handoffSource.Contains('c188r12r4r1-diagnostic-link-repair') -or
    -not $handoffSource.Contains("'c188r12r4-factory-predicates.ps1'") -or
    -not $handoffSource.Contains('factoryEvidence=$factoryEvidence') -or
    -not $handoffSource.Contains('allocationEvents')) {
    throw 'The existing single-click Settings handoff runner does not accept or summarize the R4R1 factory candidate.'
}
$qacRunnerSource = [System.IO.File]::ReadAllText(
    (Join-Path $PSScriptRoot 'run-c188-qac-guest.ps1'))
if (-not $qacRunnerSource.Contains('c188r12r4r1-diagnostic-link-repair') -or
    -not $qacRunnerSource.Contains('$isC188R12R4R1Boot')) {
    throw 'The QAC guest runner does not accept the R4R1 diagnostic evidence root.'
}

Write-Output "C188R12R4R1-LINK-BOUNDARY cases=9 guestDirect=true hostedDirect=false productionDirect=false allocationLineMaxBytes=$maxAllocationLineBytes firstIncompleteParser=true oneClickRunner=true allocationSummary=true result=PASS"
