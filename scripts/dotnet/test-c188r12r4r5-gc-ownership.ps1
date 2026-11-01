param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$DotNetExe = 'dotnet'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = Join-Path $root "out\dotnet\c188r12r4r5-gc-ownership-proof\hosted-$runId"
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

function Require-Text([string]$Text, [string]$Pattern, [string]$Label) {
    if ($Text -notmatch $Pattern) { throw "C188R12R4R5 proof contract missing: $Label" }
}

function Test-HostedOutput([string]$Text) {
    $lines = @($Text -split "`r?`n" | Where-Object { $_ -match '^marker=C188R12R4R5-GC-OWNERSHIP ' })
    if ($lines.Count -ne 3) { return $false }
    $expectedPhases = @('created', 'collection-1', 'collection-2')
    for ($index = 0; $index -lt 3; $index++) {
        $line = $lines[$index]
        $expectedCallback = $index + 1
        if ($line -notmatch ('^marker=C188R12R4R5-GC-OWNERSHIP phase=' + $expectedPhases[$index] +
                ' callback=' + $expectedCallback + ' collectionCount=([0-9]+) heapAllocatedBytes=([0-9]+) threadAllocatedBytes=([0-9]+) root=([0-9A-F]{16}) child=([0-9A-F]{16}) references=([0-9A-F]{16}) primitive=([0-9A-F]{16}) payload=([0-9A-F]{16}) strongHandle=([0-9A-F]{16}) ownership=pass graph=pass collectionQualified=1 weakCleared=([01]) generation=([0-9]+) blocking=([01]) compacting=([01]) rootHandleUpdated=([01]) moved=([01]) outcome=PASS$')) {
            return $false
        }
        $match = [regex]::Match($line, 'collectionCount=([0-9]+).*root=([0-9A-F]{16}).*strongHandle=([0-9A-F]{16}).*weakCleared=([01]) generation=([0-9]+) blocking=([01]) compacting=([01]) rootHandleUpdated=([01]) moved=([01]) outcome=PASS$')
        if (-not $match.Success -or $match.Groups[2].Value -eq '0000000000000000' -or
            $match.Groups[3].Value -eq '0000000000000000') { return $false }
        $addresses = [regex]::Matches($line, '(?:root|child|references|primitive|payload|strongHandle)=([0-9A-F]{16})')
        if ($addresses.Count -ne 6) { return $false }
        foreach ($address in $addresses) {
            if ($address.Groups[1].Value -eq '0000000000000000') { return $false }
        }
        $collectionCount = [int]$match.Groups[1].Value
        if ($index -eq 0) {
            if ($collectionCount -ne 0 -or $match.Groups[4].Value -ne '0' -or
                $match.Groups[5].Value -ne '0' -or $match.Groups[6].Value -ne '0' -or
                $match.Groups[7].Value -ne '0' -or $match.Groups[8].Value -ne '0') { return $false }
        } else {
            if ($collectionCount -ne $index -or $match.Groups[4].Value -ne '1' -or
                $match.Groups[5].Value -ne '2' -or $match.Groups[6].Value -ne '1' -or
                $match.Groups[7].Value -ne '1' -or $match.Groups[8].Value -ne '1' -or
                $match.Groups[9].Value -notmatch '^[01]$') { return $false }
        }
    }
    return $Text.TrimEnd() -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP-HOSTED\] outcome=PASS callbacks=3 observations=17 collections=2 negativeCollectionGuards=3\r?$'
}

$proofSource = Get-Content -LiteralPath (Join-Path $root 'samples\managed\HostLogProof\GcOwnershipProofC188R12R4R5.cs') -Raw
$runtimeSource = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp') -Raw
$runtimeBuilder = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\build-runtime-pack.ps1') -Raw
$managedBuilder = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\build-managed-hostlog-proof.ps1') -Raw
$guestRunner = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1') -Raw
$csproj = Get-Content -LiteralPath (Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj') -Raw
$c188Array = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp') -Raw

Require-Text $proofSource 'GCHandleType\.Normal' 'registered strong GCHandle root'
Require-Text $proofSource 'GCHandleType\.Weak' 'weak-only lifetime control'
Require-Text $proofSource 'GC\.Collect\(2, GCCollectionMode\.Forced, blocking: true, compacting: true\)' 'forced blocking compacting Gen2 collection'
Require-Text $proofSource 'ReferenceEquals\(root\.Next\.Next, root\)' 'cyclic object reference validation'
Require-Text $proofSource 'ReferenceEquals\(root\.Links\[0\], root\)' 'reference-bearing array validation'
Require-Text $proofSource 'ObserveObject\(callbackOrdinal, ReferenceArray' 'array ownership observation'
Require-Text $runtimeSource 'guidexos_nativeaot_gc_describe_object' 'GC heap ownership provenance'
Require-Text $runtimeSource 'GUIDEXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_PROOF' 'proof-only native diagnostics compile guard'
Require-Text $runtimeSource 'guideXosStockRhpNewArray\(eeType, length\)' 'real Workstation GC array slow path'
$arenaDefinition = $runtimeSource.IndexOf('__declspec(align(16)) unsigned char g_guideXosManagedHeap', [System.StringComparison]::Ordinal)
$realGcArenaGuard = $runtimeSource.LastIndexOf('#if !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)', $arenaDefinition, [System.StringComparison]::Ordinal)
$realGcArenaElse = if ($realGcArenaGuard -ge 0) { $runtimeSource.IndexOf('#else', $realGcArenaGuard, [System.StringComparison]::Ordinal) } else { -1 }
if ($arenaDefinition -lt 0 -or $realGcArenaGuard -lt 0 -or $realGcArenaElse -lt 0 -or $arenaDefinition -gt $realGcArenaElse) {
    throw 'C188 bounded arena declaration is not excluded by the real-GC compile guard.'
}
Require-Text $runtimeBuilder 'C188R12R4R5GcOwnershipProof -and[\s\S]{0,300}PersistentCompositeLifecycle' 'proof profile excludes persistent arena lifecycle'
Require-Text $runtimeBuilder 'imageBackedArenaPresent = -not \[bool\]\$C188R12R4R5GcOwnershipProof' 'runtime manifest arena isolation bit'
Require-Text $managedBuilder 'C188R12R4R5 requires an isolated ProductionApplication' 'managed candidate isolation constraints'
Require-Text $csproj 'DirectPInvoke Include="__Internal"' 'direct static diagnostics imports'
Require-Text $guestRunner "\`$attemptPath = if \(\`$isThreadStoreReadiness -and \`$ThreadStoreAttempt -eq 'R2'\)[\s\S]{0,220}guest-launch-attempt-C188R12R4R5R4R2\.json[\s\S]{0,100}else \{ Join-Path \`$phaseRoot 'guest-launch-attempt\.json' \}" 'R4R2 gets a distinct one-guest guard while other phases retain their existing guard'
Require-Text $guestRunner '\[System\.Text\.UTF8Encoding\]::new\(\$false\)' 'QMP UTF-8 without a byte-order mark'
Require-Text $guestRunner '\$qacProcess\.WaitForExit\(90000\)' 'bounded QAC guest shutdown wait'
if ($guestRunner -match 'Set-Content[^\r\n]*\$serial\s+-Value') {
    throw 'QAC runner must not rewrite the serial file owned by QEMU.'
}

$hostOutputPath = Join-Path $testRoot 'hosted-output.txt'
$commandOutput = & $DotNetExe run --project (Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj') `
    -c Release -p:PublishAot=false -p:HostLogProofHostTest=true `
    -p:HostLogProofC188R12R4R5GcOwnershipTests=true `
    -p:HostLogProofMode=GcOwnershipProof `
    ("-p:BaseOutputPath={0}\bin\" -f $testRoot) `
    ("-p:BaseIntermediateOutputPath={0}\obj\" -f $testRoot) 2>&1
$dotnetExit = $LASTEXITCODE
$commandOutput | Set-Content -LiteralPath $hostOutputPath -Encoding utf8
$text = Get-Content -LiteralPath $hostOutputPath -Raw
if ($dotnetExit -ne 0) { throw "Hosted C188R12R4R5 graph test failed with dotnet exit code $dotnetExit. See $hostOutputPath" }
if (-not (Test-HostedOutput $text)) { throw "Hosted C188R12R4R5 output failed the deterministic proof predicate. See $hostOutputPath" }

$validLine = ($text -split "`r?`n" | Where-Object { $_ -match '^marker=C188R12R4R5-GC-OWNERSHIP phase=collection-1 ' } | Select-Object -First 1)
$invalidCases = @(
    ($text -replace [regex]::Escape($validLine), ''),
    ($text -replace 'outcome=PASS', 'outcome=FAIL'),
    ($text -replace ' references=[0-9A-F]{16}', ' references=0000000000000000'),
    ($text -replace ' weakCleared=1', ' weakCleared=0'),
    ($text -replace 'rootHandleUpdated=1', 'rootHandleUpdated=0')
)
foreach ($case in $invalidCases) {
    if (Test-HostedOutput $case) { throw 'Hosted output predicate accepted an injected negative case.' }
}
$nonMovingObservation = $text -replace ' moved=1 outcome=PASS', ' moved=0 outcome=PASS'
if (-not (Test-HostedOutput $nonMovingObservation)) {
    throw 'Hosted output predicate rejected a valid collection observation without object relocation.'
}

$sourceEvidence = [ordered]@{
    result = 'PASS'
    hostedOutput = $hostOutputPath
    hostedOutputSha256 = (Get-FileHash -LiteralPath $hostOutputPath -Algorithm SHA256).Hash.ToUpperInvariant()
    callbacks = 3
    heapObjectObservations = 17
    compactingCollections = 2
    negativePredicateCasesRejected = $invalidCases.Count
    nonMovingCollectionObservationAccepted = $true
    proofModeIsProductionIsolated = $true
    c188ArenaSourceExcludedByRealGcProfile = $true
}
$sourceEvidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $testRoot 'hosted-manifest.json') -Encoding utf8
Write-Output "C188R12R4R5 hosted ownership proof: PASS ($testRoot)"
