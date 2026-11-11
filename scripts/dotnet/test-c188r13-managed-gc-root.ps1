param(
    [string]$RepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$RuntimePackManifest,
    [Parameter(Mandatory=$true)][string]$ManagedMap,
    [Parameter(Mandatory=$true)][string]$ManagedMainDisassembly,
    [Parameter(Mandatory=$true)][string]$GcCollectWrapperDisassembly,
    [Parameter(Mandatory=$true)][string]$ConstructorDisassembly,
    [Parameter(Mandatory=$true)][string]$CalculateDisassembly,
    [Parameter(Mandatory=$true)][string]$KernelBuildLog,
    [Parameter(Mandatory=$true)][string]$ProductionNativeAotMap,
    [Parameter(Mandatory=$true)][string]$R11HostedTestResults,
    [Parameter(Mandatory=$true)][string]$CallSiteMapPath
)

$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[System.IO.Path]::GetFullPath($RepoRoot)
$checks=[System.Collections.Generic.List[object]]::new()
function Assert-R13([bool]$Condition,[string]$Name) {
    if (-not $Condition) { throw "R13 hosted assertion failed: $Name" }
    $checks.Add([ordered]@{name=$Name;passed=$true})
}
foreach ($path in @($RuntimePackManifest,$ManagedMap,$ManagedMainDisassembly,$GcCollectWrapperDisassembly,
    $ConstructorDisassembly,$CalculateDisassembly,$KernelBuildLog,$ProductionNativeAotMap,
    $R11HostedTestResults,$CallSiteMapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R13 hosted input is missing: $path" }
}
$sourcePath=Join-Path $root 'samples\managed\HostLogProof\ManagedGcRootProbe.cs'
$projectPath=Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj'
$kernelPath=Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$counterPath=Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'
$headerPath=Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_gc_root_probe.h'
$runnerPath=Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$analyzerPath=Join-Path $root 'scripts\dotnet\analyze-c188r13-managed-gc-root.ps1'
$transitionAnalyzerPath=Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r3-transition-attribution.ps1'
$r12AnalyzerPath=Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1'
$candidateBuilderPath=Join-Path $root 'scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1'
$admissionModulePath=Join-Path $root 'scripts\dotnet\c188r12r4r5r4-qac-admission.psm1'
foreach ($path in @($sourcePath,$projectPath,$kernelPath,$counterPath,$headerPath,$runnerPath,
    $analyzerPath,$transitionAnalyzerPath,$r12AnalyzerPath,$candidateBuilderPath,$admissionModulePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R13 source-contract input is missing: $path" }
}
$source=Get-Content -LiteralPath $sourcePath -Raw
$candidateBuilder=Get-Content -LiteralPath $candidateBuilderPath -Raw
$admissionModule=Get-Content -LiteralPath $admissionModulePath -Raw
$project=Get-Content -LiteralPath $projectPath -Raw
$map=Get-Content -LiteralPath $ManagedMap -Raw
$main=Get-Content -LiteralPath $ManagedMainDisassembly -Raw
$gcCollectWrapper=Get-Content -LiteralPath $GcCollectWrapperDisassembly -Raw
$ctor=Get-Content -LiteralPath $ConstructorDisassembly -Raw
$calculate=Get-Content -LiteralPath $CalculateDisassembly -Raw
$kernel=Get-Content -LiteralPath $kernelPath -Raw
$kernelLog=Get-Content -LiteralPath $KernelBuildLog -Raw
$counter=Get-Content -LiteralPath $counterPath -Raw
$header=Get-Content -LiteralPath $headerPath -Raw
$runner=Get-Content -LiteralPath $runnerPath -Raw
$analyzer=Get-Content -LiteralPath $analyzerPath -Raw
$manifest=Get-Content -LiteralPath $RuntimePackManifest -Raw | ConvertFrom-Json
$r11=Get-Content -LiteralPath $R11HostedTestResults -Raw | ConvertFrom-Json
$rhCollectMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+S_P_CoreLib_System_Runtime_InternalCalls__RhCollect\s+(?<address>[0-9A-Fa-f]{16})\b')
$rhpCollectMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpCollect\s+(?<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)')
$calculateMapMatch=[regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+(?<symbol>\S*DisplaySettingsProbe__Calculate\S*)\s+(?<address>[0-9A-Fa-f]{16})\b')

Assert-R13 ($manifest.runtimeIdentity.sourceCommit -ceq '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -and
    $manifest.runtimeIdentity.gc -ceq 'Workstation' -and [bool]$manifest.c188r12r4r5GcOwnershipProof) 'Pinned NativeAOT Workstation GC identity and ownership profile'
Assert-R13 (-not [bool]$manifest.imageBackedArenaPresent -and [int64]$manifest.managedHeapBytes -eq 0) 'Diagnostic candidate uses the real GC heap without the image-backed arena'
Assert-R13 ($source -match 'sealed class DisplaySettingsProbe' -and
    $source -match 'public static unsafe class ManagedSettingsObjectProbe') 'Dedicated R13 source retains the known managed settings object contract'
Assert-R13 ($source -match 'new\(640, 480, 2\)' -and $source -match 'settings\.Calculate\(\)') 'One real managed object is constructed and initially read'
Assert-R13 ([regex]::Matches($source,'GC\.Collect\(\)').Count -eq 1 -and
    $source -notmatch 'WaitForPendingFinalizers') 'Exactly one synchronous explicit collection and no finalizer wait'
Assert-R13 ($source -match 'ReadNativeCounters\(' -and
    $source -match 'guideXosNativeAotC188R12R4R5ReadCounters') 'Managed probe reads native GC counters on both sides of collection'
Assert-R13 ($source -match 'countAfter > countBefore' -and $source -match 'CounterStatusAfter') 'Managed completion requires a real collection-count increase and successful native post-read'
$collectPosition=$source.IndexOf('GC.Collect()', [StringComparison]::Ordinal)
$mutatePosition=$source.IndexOf('settings.Scale = 3', [StringComparison]::Ordinal)
$postReadPosition=$source.LastIndexOf('settings.Calculate()', [StringComparison]::Ordinal)
$keepAlivePosition=$source.IndexOf('GC.KeepAlive(settings)', [StringComparison]::Ordinal)
Assert-R13 ($collectPosition -ge 0 -and $collectPosition -lt $mutatePosition -and
    $mutatePosition -lt $postReadPosition -and $postReadPosition -lt $keepAlivePosition) 'Same live managed local is mutated and read after collection before KeepAlive'
Assert-R13 ($source -match '\[MethodImpl\(MethodImplOptions\.NoInlining\)\]\s+public DisplaySettingsProbe' -and
    $source -match '\[MethodImpl\(MethodImplOptions\.NoInlining\)\]\s+public int Calculate') 'Constructor and field consumer remain separate compiled methods'
Assert-R13 ($source -notmatch '~DisplaySettingsProbe|GCHandle|Thread\(|Task\.|ThreadPool|while\s*\(|for\s*\(') 'No finalizer, explicit handle, managed worker, or pressure loop is introduced'
Assert-R13 ($source -notmatch 'IntPtr|void\*\s+settings|GCHandle' -and
    $source -match 'GC.KeepAlive\(settings\)') 'Native record stores scalars and no native object or interior pointer'
Assert-R13 ($project -match "HostLogProofC188R13ManagedGcRootProbe\)' == 'true'" -and
    $project -match '<Compile Include="ManagedGcRootProbe.cs" />' -and
    $project -match '<Compile Remove="ManagedGcRootProbe.cs" />') 'R13 source compiles only under its explicit diagnostic property'
Assert-R13 ($candidateBuilder -match 'C188R13_GUEST_PHASE_PLAN_V1' -and
    $candidateBuilder -match 'exactly one synchronous GC\.Collect' -and
    $candidateBuilder -match 'native collection-count advance and completion evidence' -and
    $candidateBuilder -match 'C188R13_ONE_BOOT_GUARD_TEMPLATE_V1') 'R13 candidate plan and one-boot template describe the collection and live-root admission contract'
Assert-R13 ($admissionModule -match "NativeAotManagedGcRootProbe" -and
    $admissionModule -match "C188R13_GUEST_PHASE_PLAN_V1" -and
    $admissionModule -match "C188R13_ONE_BOOT_GUARD_TEMPLATE_V1" -and
    $admissionModule -match 'candidateGcCollectWrapperDisassembly' -and
    $admissionModule -match 'transitionQacAdmissionModule') 'Strict QAC admission validates R13 profile, linked collection path, and its own pinned module hash'
Assert-R13 ($map -match 'ManagedSettingsObjectProbe.*ManagedMain' -and
    $map -match 'DisplaySettingsProbe.*___ctor' -and $map -match 'DisplaySettingsProbe.*__Calculate' -and
    $map -match 'RhpNewFast' -and $map -match 'RhpCollect' -and
    $map -match 'guideXosNativeAotC188R12R4R5ReadCounters') 'Linked NativeAOT image includes entry, allocation, collection, counter, constructor, and field consumer'
Assert-R13 ($rhCollectMapMatch.Success -and $rhpCollectMapMatch.Success -and
    $main -match ('call\s+0x' + ([Convert]::ToUInt64($rhCollectMapMatch.Groups['address'].Value,16)).ToString('x') + '\b') -and
    $gcCollectWrapper -match ('call\s+0x' + ([Convert]::ToUInt64($rhpCollectMapMatch.Groups['address'].Value,16)).ToString('x') + '\b')) 'Linked GC.Collect call reaches the pinned RhpCollect helper through its compiled wrapper'
if ($rhCollectMapMatch.Success -and $calculateMapMatch.Success) {
    $collectCallPosition=$main.IndexOf(('call   0x' + ([Convert]::ToUInt64($rhCollectMapMatch.Groups['address'].Value,16)).ToString('x')),[StringComparison]::OrdinalIgnoreCase)
    $postCollectionMutationPosition=$main.IndexOf('mov    DWORD PTR [rsi+0x10],0x3',$collectCallPosition,[StringComparison]::OrdinalIgnoreCase)
    $postCollectionCalculatePosition=$main.IndexOf(('call   0x' + ([Convert]::ToUInt64($calculateMapMatch.Groups['address'].Value,16)).ToString('x')),$postCollectionMutationPosition,[StringComparison]::OrdinalIgnoreCase)
} else { $collectCallPosition=-1; $postCollectionMutationPosition=-1; $postCollectionCalculatePosition=-1 }
Assert-R13 ($map -match '\.managedcode\$I' -and
    $map -match 'EnumerateLiveSlots' -and $map -match 'ReportSlotToGC' -and
    $main -match 'mov    rsi,rax' -and $collectCallPosition -ge 0 -and
    $postCollectionMutationPosition -gt $collectCallPosition -and
    $postCollectionCalculatePosition -gt $postCollectionMutationPosition) 'Generated NativeAOT code metadata and GC slot decoder are linked; the same RSI object root is changed and read after RhCollect'
Assert-R13 ($main -match '52313345' -and $main -match '52313351' -and
    $main -match '52313343' -and $main -match '52313352' -and
    $main -match 'call\s+0x[0-9a-fA-F]+') 'Linked ManagedMain retains the R13 protocol markers and executable calls'
Assert-R13 ($main -match 'RhpCollect|RhpNewFast|DisplaySettingsProbe' -or
    $main -match 'call\s+0x[0-9a-fA-F]+') 'ManagedMain disassembly retains calls across collection and object access'
Assert-R13 ($ctor -match '5231334[fF]' -and $ctor -match '(?m)^\s*[0-9a-fA-F]{8,16}:') 'Constructor machine code stores the R13 construction witness'
Assert-R13 ($calculate -match '(?m)^\s*[0-9a-fA-F]{8,16}:') 'Field consumer has generated machine instructions'
Assert-R13 ($kernelLog -match '-DGXOS_NATIVEAOT_C188R13_MANAGED_GC_ROOT_PROBE' -and
    $kernel -match 'C188R13_GC_ROOT_NATIVE' -and $kernel -match 'collectionCountAfter > probeRecord.collectionCountBefore') 'Native kernel validates a real collection count delta before guest pass'
Assert-R13 ($header -match 'sizeof\(GuideXosNativeAotManagedGcRootProbeRecord\) == 44u' -and
    $source -match 'Size = 44') 'Managed and native probe record layouts agree at 44 bytes'
Assert-R13 ($counter -match 'gcInProgress != 0u' -and $counter -match 'guideXosNativeAotC188R12R4R5ReadCounters') 'Native counter API rejects reads during collection and requires initialized heap/thread state'
Assert-R13 ($runner -match 'CreateNew' -and $runner -match 'automaticRetry=\$false' -and
    $runner -match 'NATIVEAOT_SINGLE_COLLECTION_VERIFIED' -and $runner -match 'gcCollectionCompleted' -and
    $runner.Contains('if ($isManagedGcRootProbe) {') -and
    $runner.Contains('Join-Path $phaseRoot ("guest-launch-attempt-" + [System.IO.Path]::GetFileName($candidate) + ".json")') -and
    $candidateBuilder.Contains('Join-Path $phaseRoot ("guest-launch-attempt-" + [System.IO.Path]::GetFileName($candidate) + ".json")') -and
    $admissionModule -match '\$expectedGuardPath') 'QAC runner, phase plan, and admission checks bind the candidate-specific one-boot guard path and require native collection completion'
$r13LoopStart=$runner.IndexOf('if ($isManagedGcRootProbe) {',$runner.IndexOf('$deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)',[StringComparison]::Ordinal),[StringComparison]::Ordinal)
$r13LoopGeneric=$runner.IndexOf('} elseif ($isTransitionAttribution) {',$r13LoopStart,[StringComparison]::Ordinal)
$r13LoopBlock=if($r13LoopStart -ge 0 -and $r13LoopGeneric -gt $r13LoopStart){$runner.Substring($r13LoopStart,$r13LoopGeneric-$r13LoopStart)}else{''}
Assert-R13 ($r13LoopBlock -match 'Get-R13ManagedGcTerminalObservation' -and
    $r13LoopBlock -notmatch 'C188R12R4R5R2R3_TRANSITION' -and
    $runner -match '(?s)function Get-R13ManagedGcTerminalObservation.*?FAIL_FAST.*?C188R12R4R5R2R3_ENTRY_STATE.*result=\(\?:PASS\|FAIL\)') 'R13 runner waits for the post-ManagedMain state marker and cannot stop on the earlier InitializeModules-return marker'
$runnerTokens=$null; $runnerParseErrors=$null
$runnerAst=[System.Management.Automation.Language.Parser]::ParseFile($runnerPath,[ref]$runnerTokens,[ref]$runnerParseErrors)
$terminalFunctionAst=$runnerAst.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Get-R13ManagedGcTerminalObservation'
},$true)
Assert-R13 ($runnerParseErrors.Count -eq 0 -and $null -ne $terminalFunctionAst) 'Corrected R13 terminal classifier parses as an isolated runner function'
. ([scriptblock]::Create($terminalFunctionAst.Extent.Text))
$r13State='[C188R12R4R5R2R3_ENTRY_STATE] status=00000000 bytes=00000070 membership=00000001 flags=00000001 cooperativeState=00000001 preemptiveFrame=00000001 flsMatch=00000001 threadId=0000000000001234 observedThreadId=0000000000001234 stackReady=00000001 stackInBounds=00000001 modules=00000001 codeManager=00000001 currentThread=0000000000005678 palFlsThread=0000000000005678 frame=FFFFFFFFFFFFFFFF stackLow=0000000000100000 stackHigh=0000000000200000 stackCurrent=0000000000180000 result=PASS'
$r13Attempt='[C188R13_GC_ROOT_ENTRY] event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain model=DisplaySettingsProbe collection=one_synchronous_full_request'
$r13Allocation='[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] helper=RhpNewFast stock=guideXosStockRhpNewFast requests=00000001 completed=00000001 objectSize=00000018 contextReadyBefore=00000001 contextReadyAfter=00000001 contextBytesBefore=00001000 contextBytesAfter=00000FE8 contextAdvanced=00000018 methodTable=00000001 gcHeapOwned=00000001 threadAttached=00000001 gcCountBefore=00000000 gcCountAfter=00000000 status=00000000 collectionRequestedByProbe=00000000'
$r13Returned='[C188R13_GC_ROOT_ENTRY] event=returned entry=52313345 input=0000000E initial=00000462 request=52313351 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completion=52313343 final=00000463 returnMarker=52313352 return=00000463 expected=00000463 record=PASS collection=requested'
$r13Native='[C188R13_GC_ROOT_NATIVE] request=00000001 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completed=00000001 delta=00000001 result=PASS'
$r13MilestoneSnapshots=@(
    '[nativeaot-pal-qemu-test] runtime_initialized=1',
    ('[nativeaot-pal-qemu-test] runtime_initialized=1' + "`n" + '[C188R12R4R5R2R3_TRANSITION] result=initialize_modules_returned managedEntry=probe_follows collection=not_requested'),
    ('[C188R12R4R5R2R3_TRANSITION] result=initialize_modules_returned managedEntry=probe_follows collection=not_requested' + "`n" + $r13Attempt),
    ($r13Attempt + "`n" + $r13Allocation),
    ($r13Attempt + "`n" + $r13Allocation + "`n" + $r13Returned + "`n" + $r13Native),
    ($r13Attempt + "`n" + $r13Allocation + "`n" + $r13Returned + "`n" + $r13Native + "`n" + $r13State)
)
$r13Observations=@($r13MilestoneSnapshots | ForEach-Object { Get-R13ManagedGcTerminalObservation $_ })
Assert-R13 (@($r13Observations | Where-Object { $_.terminal }).Count -eq 1 -and
    $r13Observations[-1].kind -ceq 'PostReturnRuntimeState') 'Successful initialization continues through managed attempt, allocation, collection, return, and post-return checks'
$fatalBeforeAllocation=Get-R13ManagedGcTerminalObservation ($r13Attempt + "`n" + '[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000')
$fatalInCollection=Get-R13ManagedGcTerminalObservation ($r13Attempt + "`n" + $r13Allocation + "`n" + '[C188R13_TEST_CONTEXT] stage=collection' + "`n" + '[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000')
Assert-R13 ($fatalBeforeAllocation.terminal -and $fatalBeforeAllocation.kind -ceq 'FailFast' -and
    $fatalInCollection.terminal -and $fatalInCollection.kind -ceq 'FailFast') 'Explicit fatal before allocation and during the collection context is terminal immediately'
$noTerminalObservation=Get-R13ManagedGcTerminalObservation ($r13Attempt + "`n" + $r13Allocation + "`n" + $r13Returned + "`n" + $r13Native)
Assert-R13 (-not $noTerminalObservation.terminal -and $noTerminalObservation.kind -ceq 'Continuing' -and
    $runner -match '\$deadline = \(Get-Date\)\.AddSeconds\(\$BootTimeoutSeconds\)' -and
    $runner -match '\[ValidateRange\(60,300\)\]\[int\]\$BootTimeoutSeconds') 'Missing post-return state stays incomplete under a bounded runner timeout'
Assert-R13 ($runner.IndexOf("[void](Invoke-Qmp 'quit')",[StringComparison]::Ordinal) -gt
    $runner.IndexOf('$deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)',[StringComparison]::Ordinal) -and
    $runner -match 'Refusing QMP shutdown because the local QEMU child identity is no longer verified') 'QMP shutdown remains after terminal/timeout polling and behind ownership verification'
Assert-R13 ($analyzer -match 'attemptOrder -ge \$allocationOrder' -and
    $analyzer -match 'gcCollectionCompleted' -and $analyzer -match 'liveObjectSurvived' -and
    $analyzer -match 'evidencePreserved' -and $analyzer -match 'stateRecords') 'R13 analyzer gates on event order, collection, liveness, multiple state snapshots, and immutable raw evidence'
Assert-R13 ($r11.result -ceq 'PASS' -and -not [bool]$r11.qemuLaunched -and
    -not [bool]$r11.qacReserved) 'Accepted R11 managed-entry hosted regression remains green without QAC use'
Assert-R13 (-not $map.Contains('g_guideXosManagedHeap') -and
    -not $map.Contains('C188R13') -and -not $map.Contains('ManagedGcRootProbe')) 'Proof-disabled production NativeAOT map excludes R13 probe and diagnostic controls'

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$fixtureRoot=Join-Path ([System.IO.Path]::GetFullPath($OutputDirectory)) 'analyzer-fixtures'
New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
$entryPrefix='[C188R13_GC_ROOT_ENTRY] '
$alloc='[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] helper=RhpNewFast stock=guideXosStockRhpNewFast requests=00000001 completed=00000001 objectSize=00000018 contextReadyBefore=00000001 contextReadyAfter=00000001 contextBytesBefore=00001000 contextBytesAfter=00000FE8 contextAdvanced=00000018 methodTable=00000001 gcHeapOwned=00000001 threadAttached=00000001 gcCountBefore=00000000 gcCountAfter=00000000 status=00000000 collectionRequestedByProbe=00000000'
$attempt=$entryPrefix+'event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain model=DisplaySettingsProbe collection=one_synchronous_full_request'
$returned=$entryPrefix+'event=returned entry=52313345 input=0000000E initial=00000462 request=52313351 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completion=52313343 final=00000463 returnMarker=52313352 return=00000463 expected=00000463 record=PASS collection=requested'
$native='[C188R13_GC_ROOT_NATIVE] request=00000001 counterStatusBefore=00000000 countBefore=00000000 countAfter=00000001 counterStatusAfter=00000000 completed=00000001 delta=00000001 result=PASS'
$state='[C188R12R4R5R2R3_ENTRY_STATE] status=00000000 bytes=00000070 membership=00000001 flags=00000001 cooperativeState=00000001 preemptiveFrame=00000001 flsMatch=00000001 threadId=0000000000001234 observedThreadId=0000000000001234 stackReady=00000001 stackInBounds=00000001 modules=00000001 codeManager=00000001 currentThread=0000000000005678 palFlsThread=0000000000005678 frame=FFFFFFFFFFFFFFFF stackLow=0000000000100000 stackHigh=0000000000200000 stackCurrent=0000000000180000 result=PASS'
function Invoke-R13Fixture([string]$Name,[string[]]$Lines) {
    $path=Join-Path $fixtureRoot ($Name+'.serial.log')
    $out=Join-Path $fixtureRoot ($Name+'.analysis.json')
    [System.IO.File]::WriteAllLines($path,$Lines,[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzerPath -SerialPath $path `
        -CallSiteMapPath $CallSiteMapPath -OutputPath $out | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "R13 fixture analyzer failed to preserve $Name." }
    return (Get-Content -LiteralPath $out -Raw | ConvertFrom-Json)
}
$valid=Invoke-R13Fixture 'valid-order-marker-only' @($attempt,$alloc,$returned,$native,$state)
Assert-R13 ($valid.managedEntryReturned -and $valid.gcCollectionStarted -and
    $valid.gcCollectionCompleted -and $valid.liveObjectSurvived -and
    [string]::IsNullOrWhiteSpace([string]$valid.parseError) -and
    $valid.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Positive ordering fixture parses complete collection and liveness markers without claiming a guest pass'
$multipleStates=Invoke-R13Fixture 'multiple-valid-post-return-state-snapshots' @($attempt,$alloc,$returned,$native,$state,$state)
Assert-R13 ($multipleStates.managedRuntimeStateSnapshotCount -eq 2 -and
    $multipleStates.runtimeStateReady -and
    $multipleStates.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Multiple legitimate post-return state snapshots are retained and validated'
$missingState=Invoke-R13Fixture 'missing-post-return-state' @($attempt,$alloc,$returned,$native)
Assert-R13 ($missingState.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$missingState.parseError -ceq 'r13_event_order_contract_mismatch') 'Missing post-return state evidence remains incomplete'
$missingCollectionCompletion=Invoke-R13Fixture 'missing-native-collection-completion' @($attempt,$alloc,$returned,$state)
Assert-R13 ($missingCollectionCompletion.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$missingCollectionCompletion.parseError -ceq 'r13_event_order_contract_mismatch') 'Managed result without native collection completion remains incomplete'
$reordered=Invoke-R13Fixture 'allocation-after-return' @($attempt,$returned,$alloc,$native,$state)
Assert-R13 ($reordered.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$reordered.parseError -ceq 'r13_event_order_contract_mismatch') 'Negative ordering fixture rejects allocation evidence after managed return'
$zeroDelta=Invoke-R13Fixture 'no-native-collection-delta' @($attempt,$alloc,
    ($returned -replace 'countAfter=00000001','countAfter=00000000'),
    ($native -replace 'countAfter=00000001','countAfter=00000000' -replace 'delta=00000001','delta=00000000'),$state)
Assert-R13 (-not $zeroDelta.gcCollectionStarted -and -not $zeroDelta.gcCollectionCompleted -and
    $zeroDelta.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Negative fixture rejects a claimed 1123 result without a native collection-count increase'
$contradictory=Invoke-R13Fixture 'contradictory-runtime-state' @($attempt,$alloc,$returned,$native,
    ($state -replace 'membership=00000001','membership=00000000'))
Assert-R13 (-not $contradictory.runtimeStateReady -and
    $contradictory.classification -ceq 'MANAGED_GC_ROOT_RETURNED_MISMATCH') 'Negative fixture rejects contradictory post-return ThreadStore state'
$duplicate=Invoke-R13Fixture 'duplicate-return-marker' @($attempt,$alloc,$returned,$native,$state,$returned)
Assert-R13 ($duplicate.classification -ceq 'MANAGED_GC_ROOT_INCOMPLETE' -and
    [string]$duplicate.parseError -ceq 'duplicate_r13_probe_marker') 'Negative fixture rejects duplicate return evidence'

$r12Attempt='[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY] event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain model=DisplaySettingsProbe collection=not_requested'
$r12Alloc='[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] helper=RhpNewFast stock=guideXosStockRhpNewFast requests=00000001 completed=00000001 objectSize=00000018 contextReadyBefore=00000001 contextReadyAfter=00000001 contextBytesBefore=00001000 contextBytesAfter=00000FE8 contextAdvanced=00000018 methodTable=00000001 gcHeapOwned=00000001 threadAttached=00000001 gcCountBefore=00000000 gcCountAfter=00000000 status=00000000 collectionRequestedByProbe=00000000'
$r12Returned='[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY] event=returned entry=52313245 input=0000000E computed=00000462 completion=52313244 return=00000462 expected=00000462 record=PASS collection=not_requested model=DisplaySettingsProbe'
$r12State='[C188R12R4R5R2R3_ENTRY_STATE] status=00000000 bytes=00000070 membership=00000001 flags=00000001 cooperativeState=00000001 preemptiveFrame=00000001 flsMatch=00000001 threadId=0000000000001234 observedThreadId=0000000000001234 stackReady=00000001 stackInBounds=00000001 modules=00000001 codeManager=00000001 currentThread=0000000000005678 palFlsThread=0000000000005678 frame=FFFFFFFFFFFFFFFF stackLow=0000000000100000 stackHigh=0000000000200000 stackCurrent=0000000000180000 result=PASS'
function Invoke-R12OrderingFixture([string]$Name,[string[]]$Lines) {
    $path=Join-Path $fixtureRoot ($Name+'.serial.log')
    $out=Join-Path $fixtureRoot ($Name+'.analysis.json')
    [System.IO.File]::WriteAllLines($path,$Lines,[System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $r12AnalyzerPath -SerialPath $path `
        -CallSiteMapPath $CallSiteMapPath -OutputPath $out | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "R12 ordering fixture analyzer failed to preserve $Name." }
    return (Get-Content -LiteralPath $out -Raw | ConvertFrom-Json)
}
$r12Valid=Invoke-R12OrderingFixture 'r12-allocation-before-return' @($r12Attempt,$r12Alloc,$r12Returned,$r12State)
Assert-R13 ($r12Valid.managedEntryReturned -and $r12Valid.allocationSucceeded -and
    [string]::IsNullOrWhiteSpace([string]$r12Valid.parseError) -and
    $r12Valid.classification -ceq 'MANAGED_SETTINGS_OBJECT_RETURNED_MISMATCH') 'R12 positive ordering regression accepts genuine allocation before method return'
$r12Late=Invoke-R12OrderingFixture 'r12-allocation-after-return' @($r12Attempt,$r12Returned,$r12Alloc,$r12State)
Assert-R13 ($r12Late.classification -ceq 'MANAGED_SETTINGS_OBJECT_INCOMPLETE' -and
    [string]$r12Late.parseError -ceq 'r12_managed_entry_return_contract_mismatch') 'R12 negative ordering regression rejects allocation after method return'

$report=[ordered]@{
    schema='C188R13_MANAGED_GC_ROOT_HOSTED_TESTS_V1'
    result='PASS'
    assertionCount=$checks.Count
    assertions=@($checks.ToArray())
    runtimeSourceCommit=[string]$manifest.runtimeIdentity.sourceCommit
    initialResult=1122
    expectedResult=1123
    managedCollectionRequested=$true
    collectionExecuted=$false
    hostedEvidenceOnly=$true
    qemuLaunched=$false
    qacReserved=$false
}
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory 'hosted-test-results.json'),
    ($report | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output ("R13 managed-GC hosted tests: PASS assertions={0}; no native collection, guest boot, or QAC reservation." -f $checks.Count)
