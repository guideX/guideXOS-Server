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

Write-Output "C188R12R4R1-LINK-BOUNDARY cases=6 guestDirect=true hostedDirect=false productionDirect=false allocationLineMaxBytes=$maxAllocationLineBytes result=PASS"
