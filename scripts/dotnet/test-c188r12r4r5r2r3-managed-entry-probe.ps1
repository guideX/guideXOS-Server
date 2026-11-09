param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$RuntimePackManifest,
    [Parameter(Mandatory=$true)][string]$ManagedMap,
    [Parameter(Mandatory=$true)][string]$ManagedDisassembly,
    [Parameter(Mandatory=$true)][string]$KernelBuildLog,
    [string]$RuntimeSourceRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$results = [System.Collections.Generic.List[object]]::new()
function Assert-Probe([bool]$Condition,[string]$Name) {
    $results.Add([ordered]@{name=$Name;passed=$Condition})
    if (-not $Condition) { throw "R7 managed-entry hosted check failed: $Name" }
}
foreach ($path in @($RuntimePackManifest,$ManagedMap,$ManagedDisassembly,$KernelBuildLog)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R7 hosted-test input is missing: $path" }
}
$probePath = Join-Path $root 'samples\managed\HostLogProof\ManagedEntryProbe.cs'
$projectPath = Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj'
$platformPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'
$threadReadinessHeaderPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_threadstore_readiness.h'
$nativeHeaderPath = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_entry_probe.h'
$kernelPath = Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$runnerPath = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
if ([string]::IsNullOrWhiteSpace($RuntimeSourceRoot)) {
    $RuntimeSourceRoot = Join-Path $root 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime'
}
$runtimeSource = Join-Path ([System.IO.Path]::GetFullPath($RuntimeSourceRoot)) 'src\coreclr\nativeaot'
$startupHelpersPath = Join-Path $runtimeSource 'Common\src\Internal\Runtime\CompilerHelpers\StartupCodeHelpers.cs'
$threadPath = Join-Path $runtimeSource 'Runtime\thread.cpp'
$threadInlinePath = Join-Path $runtimeSource 'Runtime\thread.inl'
$threadHeaderPath = Join-Path $runtimeSource 'Runtime\thread.h'
foreach ($path in @($probePath,$projectPath,$platformPath,$threadReadinessHeaderPath,$nativeHeaderPath,$kernelPath,$runnerPath,$startupHelpersPath,$threadPath,$threadInlinePath,$threadHeaderPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R7 source-contract input is missing: $path" }
}
$probe = Get-Content -LiteralPath $probePath -Raw
$project = Get-Content -LiteralPath $projectPath -Raw
$platform = Get-Content -LiteralPath $platformPath -Raw
$threadReadinessHeader = Get-Content -LiteralPath $threadReadinessHeaderPath -Raw
$nativeHeader = Get-Content -LiteralPath $nativeHeaderPath -Raw
$kernel = Get-Content -LiteralPath $kernelPath -Raw
$runner = Get-Content -LiteralPath $runnerPath -Raw
$startupHelpers = Get-Content -LiteralPath $startupHelpersPath -Raw
$threadSource = Get-Content -LiteralPath $threadPath -Raw
$threadInline = Get-Content -LiteralPath $threadInlinePath -Raw
$threadHeader = Get-Content -LiteralPath $threadHeaderPath -Raw
$telemetryPush = [regex]::Match($threadReadinessHeader,'(?s)static inline bool GuideXosThreadStorePushTelemetryFrame\(.*?(?=static inline bool GuideXosThreadStorePopTelemetryFrame)').Value
$telemetryPop = [regex]::Match($threadReadinessHeader,'(?s)static inline bool GuideXosThreadStorePopTelemetryFrame\(.*?(?=\n\})').Value
$map = Get-Content -LiteralPath $ManagedMap -Raw
$disassembly = Get-Content -LiteralPath $ManagedDisassembly -Raw
$kernelLog = Get-Content -LiteralPath $KernelBuildLog -Raw
$runtimeManifest = Get-Content -LiteralPath $RuntimePackManifest -Raw | ConvertFrom-Json -ErrorAction Stop
$managedEntryAnalyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r3-managed-entry-probe.ps1'
$transitionAnalyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r3-transition-attribution.ps1'
$fatalFixtureRoot = Join-Path $root ('out\dotnet\managed-entry-fatal-fixture-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fatalFixtureRoot | Out-Null
$fatalFixtureMapPath = Join-Path $fatalFixtureRoot 'candidate-callsite-map.json'
$fatalFixtureSerialPath = Join-Path $fatalFixtureRoot 'serial.log'
$fatalFixtureAnalysisPath = Join-Path $fatalFixtureRoot 'managed-entry-analysis.json'
$fatalFixtureMap = [ordered]@{
    schemaVersion=1
    lockedRuntimeCommit='synthetic-managed-entry-fatal-fixture'
    sites=@([ordered]@{
        id=6;key='THREAD_REVERSE_PINVOKE_ATTACH_OR_TRAP_THREAD2'
        name='thread.reverse-pinvoke.invalid-unmanagedcallersonly.inlined-helper'
        message='Fatal error. Invalid Program: attempted to call a UnmanagedCallersOnly method from managed code.'
        linkedInCandidate=$true;resolutionStatus='exact-direct-call-mapped'
        callerReturnAddress='0x0000000100001006'
    })
}
[System.IO.File]::WriteAllText($fatalFixtureMapPath,($fatalFixtureMap | ConvertTo-Json -Depth 6),[System.Text.UTF8Encoding]::new($false))
$fatalFixtureRecord = '[C188R12R4R5R2R3_FATAL] valid=1 seq=0000000000000001 snapshot=00000001 attempted=00000001 completed=00000001 gate=00000000 fatal=00000001 reason=50414C06 callSite=FFFFFFFE callerRip=0000000100001006 msgState=00000000 msgBytes=00000000 msgTruncated=00000000 originalStatusAvailable=00000000 originalStatus=00000000 firstFailureStage=FFFFFFFF firstFailureStatusAvailable=00000000 firstFailureStatus=00000000 lastSuccess=00000063 initOverflow=00000000 threadOverflow=00000000'
$fatalFixtureState = '[C188R12R4R5R2R3_STATE] runtime=00000001 rhInitialize=00000001 threadStore=00000001 rawThread=0000000000002000 currentThread=0000000000002000 attachment=00000002 nativeThreadId=0000000000000042 observedOsThreadId=0000000000000042 coop=00000002 tlsReady=00000001 tlsIndex=0000000000000000 palFlsIndex=0000000000000001 palFlsValue=0000000000002000 flsState=00000001 stackReady=00000001 stackCurrent=0000000000003000 stackLow=0000000000002000 stackHigh=0000000000004000 frame=0000000000000000 frameReady=00000000 frameAligned=00000001 frameInStack=00000000 stockWillAttach=00000000 gcReady=00000001 module=00000001 codeManager=00000001 modulesNotInitialized=00000001'
$fatalFixtureLines = @(
    '[C188R12R4R5R2R3_HANDOFF] checkpoint=before_managed_main status=00000008 bytes=00000070 thread=0000000000002000 membership=00000001 flags=00000002 coop=00000002 frame=0000000000000000 frameTop=00000000 fls=0000000000002000 flsMatch=00000001 threadId=0000000000000042 observedThreadId=0000000000000042 stackReady=00000001 stackInBounds=00000001 stackLow=0000000000002000 stackHigh=0000000000004000 stackCurrent=0000000000003000 modules=00000001 codeManager=00000001',
    '[C188R12R4R5R2R3_ENTRY] event=attempt abi=UnmanagedCallersOnly-Cdecl seed=0000000E symbol=ManagedMain collection=not_requested',
    '[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000',
    $fatalFixtureRecord,
    $fatalFixtureState
)
[System.IO.File]::WriteAllLines($fatalFixtureSerialPath,$fatalFixtureLines,[System.Text.UTF8Encoding]::new($false))
& pwsh -NoProfile -ExecutionPolicy Bypass -File $managedEntryAnalyzerPath `
    -SerialPath $fatalFixtureSerialPath -CallSiteMapPath $fatalFixtureMapPath `
    -TransitionAnalyzerPath $transitionAnalyzerPath -OutputPath $fatalFixtureAnalysisPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Managed-entry fatal-after-dispatch hosted fixture failed to analyze.' }
$fatalFixtureAnalysis = Get-Content -LiteralPath $fatalFixtureAnalysisPath -Raw | ConvertFrom-Json
$reversePInvokeMap = [regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpReversePInvoke\s+(?<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)')
$reversePInvokeReturnMap = [regex]::Match($map,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpReversePInvokeReturn\s+(?<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)')
$expectedProbeCalls = @()
if ($reversePInvokeMap.Success -and $reversePInvokeReturnMap.Success) {
    $expectedProbeCalls += ('0x' + ([Convert]::ToUInt64($reversePInvokeMap.Groups['address'].Value,16)).ToString('x'))
    $expectedProbeCalls += ('0x' + ([Convert]::ToUInt64($reversePInvokeReturnMap.Groups['address'].Value,16)).ToString('x'))
}
$actualProbeCalls = @([regex]::Matches($disassembly,'(?m)\bcall\s+(?<target>0x[0-9a-fA-F]+)\b') | ForEach-Object { $_.Groups['target'].Value.ToLowerInvariant() })

$managedMethod = [regex]::Match($probe,'(?s)public static int ManagedMain\(ManagedEntryProbeRecord\* record, uint seed\)\s*\{(?<body>.*?)\n    \}')
$managedBody = if ($managedMethod.Success) { $managedMethod.Groups['body'].Value } else { '' }
$assertion = @(
    @{ok=$probe -match '\[StructLayout\(LayoutKind\.Sequential, Pack = 4, Size = 16\)\]' -and $nativeHeader -match 'sizeof\(GuideXosNativeAotManagedEntryProbeRecord\) == 16'; name='managed and native records have an explicit 16-byte ABI'},
    @{ok=$probe -match '\[UnmanagedCallersOnly\(\s*EntryPoint = "ManagedMain",\s*CallConvs = new\[\] \{ typeof\(CallConvCdecl\) \}\)\]' -and $probe -match 'MethodImplOptions\.NoInlining'; name='probe has a distinct Cdecl UCO entry and stays out of line'},
    @{ok=$managedMethod.Success -and $managedBody.IndexOf('record->EntryMarker', [StringComparison]::Ordinal) -lt $managedBody.IndexOf('record->Input', [StringComparison]::Ordinal) -and $managedBody.IndexOf('record->Input', [StringComparison]::Ordinal) -lt $managedBody.IndexOf('record->Computed', [StringComparison]::Ordinal) -and $managedBody.IndexOf('record->Computed', [StringComparison]::Ordinal) -lt $managedBody.IndexOf('record->CompletionMarker', [StringComparison]::Ordinal) -and $probe.Contains('unchecked(seed * 3u + 7u)'); name='probe writes ordered markers and computes the fixed result'},
    @{ok=$managedMethod.Success -and $managedBody -notmatch '\b(?:new|throw|try|catch|fixed|stackalloc|GC\.|RhHandleAlloc|RhpNewArray|RhpCollect)\b' -and $managedBody -notmatch '\b(?:DllImport|LibraryImport|extern)\b'; name='managed probe body has no allocation, P/Invoke, exception, or collection path'},
    @{ok=$project -match 'EnableDefaultCompileItems>false</EnableDefaultCompileItems>' -and $project -match '<Compile Include="ManagedEntryProbe.cs" />' -and $project -match '<ItemGroup Condition="[^"]*ManagedEntryProbe[^"]*!=[^"]*">' -and $project -match '<Compile Remove="ManagedEntryProbe.cs" />'; name='probe project mode compiles only the probe source and production excludes it'},
    @{ok=$startupHelpers -match 'UnmanagedCallersOnly\(EntryPoint\s*=\s*"InitializeModules"' -and $startupHelpers -match 'new object\[count\]' -and $startupHelpers -match 'RhHandleAlloc'; name='pinned InitializeModules body is real runtime initialization and may allocate'},
    @{ok=$threadSource -match 'Thread::ReversePInvokeAttachOrTrapThread' -and $threadSource -match 'ThreadStore::AttachCurrentThread' -and $threadSource -match 'm_pTransitionFrame' -and $threadSource -match 'm_pTransitionFrame = TOP_OF_STACK_MARKER' -and $threadHeader -match 'TOP_OF_STACK_MARKER'; name='pinned reverse-PInvoke path owns attachment and preemptive thread construction'},
    @{ok=$threadInline -match '(?s)InlineTryFastReversePInvoke\(ReversePInvokeFrame\* pFrame\).*?pFrame->m_savedPInvokeTransitionFrame = m_pTransitionFrame;.*?if \(!IsStateSet\(TSF_Attached\)\).*?if \(IsCurrentThreadInCooperativeMode\(\)\).*?return false;.*?m_pTransitionFrame, \(PInvokeTransitionFrame\*\)nullptr' -and $threadInline -match '(?s)InlineReversePInvokeReturn\(ReversePInvokeFrame\* pFrame\).*?m_pTransitionFrame, pFrame->m_savedPInvokeTransitionFrame'; name='pinned second reverse-P/Invoke requires attached preemptive mode and restores the saved frame'},
    @{ok=[bool]$runtimeManifest.c188r12r4r5r2r3ManagedEntryProbe -and [string]$runtimeManifest.identity -ceq 'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-managed-entry-v1' -and [string]$runtimeManifest.runtimeIdentity.sourceCommit -ceq '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -and [string]$runtimeManifest.c188r12r4r5r2r3ManagedEntryProbeContract.ABI -ceq 'UnmanagedCallersOnly(CallConvCdecl)' -and [int]$runtimeManifest.c188r12r4r5r2r3ManagedEntryProbeContract.seed -eq 14 -and [int]$runtimeManifest.c188r12r4r5r2r3ManagedEntryProbeContract.expectedResult -eq 49; name='runtime manifest selects the R7 identity and fixed ABI contract'},
    @{ok=[bool]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnostics -and [string]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.profile -ceq 'ManagedEntryProbe' -and [string]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.telemetryStorage -ceq 'fixed native side table indexed by reverse-P/Invoke nesting depth' -and [bool]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.doesNotWriteThreadTransitionFrame -and -not [bool]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.nativePostFixExecutionVerified -and [bool]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.noFrameSynthesis -and [bool]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.noGcPolicyMutation -and [bool]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.noFatalSuppression -and [string]$runtimeManifest.c188r12r4r5r2r3ManagedEntryHandoffDiagnosticContract.sourceFiles.platformSha256 -match '^[A-F0-9]{64}$'; name='runtime manifest pins side-table transition diagnostics, source provenance, and unverified post-fix guest status'},
    @{ok=$telemetryPush -match 'records\[\*depth\]\.frame\s*=\s*frame' -and $telemetryPush -match 'previousTelemetryFrame\s*=\s*previous' -and $telemetryPush -notmatch 'runtimeCell|telemetryOffset|\*telemetry\s*=' -and $telemetryPop -match 'records\[\*depth\s*-\s*1u\]\.previousTelemetryFrame' -and $telemetryPop -notmatch 'runtimeCell|telemetryOffset|\*telemetry\s*=' -and $platform -match 'GuideXosThreadStorePopTelemetryFrame\(\s*frame,\s*g_threadReadinessTransitions'; name='transition telemetry nesting uses only the side table and never writes Thread::m_pTransitionFrame'},
    @{ok=$platform -match 'GuideXosNativeAotC188R12R4R5R2R3ManagedEntryProbeCaptureState' -and $platform -match 'ThreadStore::GetCurrentThreadIfAvailable\(\)' -and $platform -match 'ThreadStore::Iterator iterator' -and $platform -match 'GuideXosNativeAotTransitionGetPalFlsValue\(\)' -and $platform -match 'PalGetMaximumStackBounds\(&stackLow, &stackHigh\)' -and $platform -match 'GX_MANAGED_ENTRY_PROBE_RUNTIME_READY'; name='post-call capture checks linked ThreadStore, FLS, identity, frame, and stack state'},
    @{ok=$platform -match '(?s)recordManagedEntryModeCheckpoint\(\s*GX_THREADSTORE_EVENT_MANAGED_ENTRY_BRIDGE_BEFORE_THREAD.*?initializeRuntimeState\(block\);.*?recordManagedEntryModeCheckpoint\(\s*GX_THREADSTORE_EVENT_MANAGED_ENTRY_BRIDGE_AFTER_THREAD.*?guideXosStockRhpReversePInvoke\(frame\)' -and $platform -match '(?s)recordManagedEntryModeCheckpoint\(\s*GX_THREADSTORE_EVENT_INITIALIZEMODULES_RETURN_THREAD.*?recordManagedEntryModeCheckpoint\(\s*GX_THREADSTORE_EVENT_ATTRIBUTION_PREPARE_AFTER_INIT_THREAD.*?recordManagedEntryModeCheckpoint\(\s*GX_THREADSTORE_EVENT_ATTRIBUTION_PREPARE_BEFORE_RETURN_THREAD' -and $platform -match 'locals->m_pTransitionFrame\)'; name='read-only checkpoints bracket initializer return, native preparation, and reverse-P/Invoke attachment'},
    @{ok=$nativeHeader -match 'offsetof\(GuideXosNativeAotManagedEntryProbeRecord, completionMarker\) == 12' -and $nativeHeader -match 'sizeof\(GuideXosNativeAotManagedEntryProbeRuntimeState\) == 112' -and $nativeHeader -match 'kGuideXosManagedEntryProbePreemptive'; name='native layout and post-call state constants are pinned by assertions'},
    @{ok=$map -match 'ManagedMain\s+[0-9A-Fa-f]{16}' -and $map.Contains('GuideXosNativeAotC188R12R4R5R2R3ManagedEntryProbeCaptureState') -and $map -match 'ManagedEntryProbe'; name='linked NativeAOT map contains ManagedMain probe and capture exports'},
    @{ok=$disassembly -match '5237454[eE]' -and $disassembly -match '5237444[eE]' -and @($expectedProbeCalls).Count -eq 2 -and @($actualProbeCalls).Count -eq 2 -and @($actualProbeCalls | Where-Object { $_ -notin $expectedProbeCalls }).Count -eq 0; name='linked probe machine code stores markers and uses only its stock reverse-P/Invoke prologue/epilogue'},
    @{ok=$kernel -match '(?s)if \(transitionStatus != 0u\).*?result=safe_stop.*?handoffState\{\}.*?emitManagedEntryHandoffSnapshot\("before_managed_main".*?managedMainAddress\)\(&probeRecord,\s*kGuideXosManagedEntryProbeSeed\);' -and $kernel -match 'managedEntryProbeCaptureStateAddress' -and $kernel -match 'runtimeStatePassed'; name='kernel records the complete read-only mode snapshot immediately before the R7 ManagedMain call'},
    @{ok=$runner.Contains("Get-AnalysisBoolean `$transitionAttributionAnalysis 'managedEntryReturned'") -and $runner.Contains("Get-AnalysisBoolean `$transitionAttributionAnalysis 'runtimeStateReady'"); name='runner reports a blocked preflight safely when no serial analyzer result exists'},
    @{ok=$runner -match '(?s)\$attemptPath = if \(\$isManagedEntryProbe\) \{\s*Join-Path \$phaseRoot \("guest-launch-attempt-" \+ \$candidateManifestSha256 \+ "\.json"\)' -and $runner.Contains('[System.IO.FileMode]::CreateNew') -and $runner.Contains('maxGuestBoots=1;automaticRetry=$false'); name='runner binds one atomic no-retry attempt guard to the candidate manifest'},
    @{ok=$runner.Contains("`$transitionAttributionAnalysis.PSObject.Properties['failFast']") -and $runner.Contains('$transitionFailFastProperty.Value'); name='runner safely records transition analyses without an optional failFast field'},
    @{ok=$fatalFixtureAnalysis.classification -eq 'RUNTIME_FAIL_FAST_ATTRIBUTED' -and $fatalFixtureAnalysis.managedEntryAttempted -and -not $fatalFixtureAnalysis.managedEntryReturned -and $fatalFixtureAnalysis.managedEntryRequested -and $fatalFixtureAnalysis.callSite.id -eq 6 -and $fatalFixtureAnalysis.callSiteResolutionMethod -eq 'exact-captured-return-rip' -and $fatalFixtureAnalysis.evidencePreserved -and @($fatalFixtureAnalysis.handoffCheckpoints).Count -eq 1 -and -not [bool]$fatalFixtureAnalysis.preDispatchHandoffReady -and -not $fatalFixtureAnalysis.qemuLaunched -and -not $fatalFixtureAnalysis.qacReserved; name='mapped PAL fatal includes its cooperative pre-dispatch state without claiming managed entry'},
    @{ok=$kernelLog -match '-DGXOS_NATIVEAOT_C188R12R4R5R2R3_MANAGED_ENTRY_PROBE'; name='isolated candidate kernel was built with the R7-only selector'},
    @{ok=$kernel -match 'collection=not_requested' -and $managedBody -notmatch 'GC\.Collect|RhpCollect'; name='probe path does not request managed GC collection'}
)
foreach ($check in $assertion) { Assert-Probe ([bool]$check.ok) ([string]$check.name) }
if (Test-Path -LiteralPath $OutputDirectory) {
    $existingEntries = @(Get-ChildItem -LiteralPath $OutputDirectory -Force)
    $unexpectedEntries = @($existingEntries | Where-Object { $_.Name -cne 'hosted-test-results.json' })
    if ($unexpectedEntries.Count -gt 0) {
        throw "Refusing to overwrite unexpected R7 hosted-test output in: $OutputDirectory"
    }
    $existingReportPath = Join-Path $OutputDirectory 'hosted-test-results.json'
    if (Test-Path -LiteralPath $existingReportPath -PathType Leaf) {
        $existingReport = Get-Content -LiteralPath $existingReportPath -Raw | ConvertFrom-Json -ErrorAction Stop
        if ($existingReport.schema -cne 'C188R12R4R5R2R3_MANAGED_ENTRY_PROBE_HOSTED_TESTS_V1' -or
            $existingReport.result -cne 'PASS' -or [bool]$existingReport.qemuLaunched -or
            [bool]$existingReport.qacReserved) {
            throw "Refusing to replace an unrecognized or non-hosted R7 test result: $existingReportPath"
        }
    }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$report = [ordered]@{
    schema='C188R12R4R5R2R3_MANAGED_ENTRY_PROBE_HOSTED_TESTS_V1'
    result='PASS'
    assertionCount=$results.Count
    assertions=@($results.ToArray())
    runtimeSourceCommit=[string]$runtimeManifest.runtimeIdentity.sourceCommit
    managedProbeAllocationFree=$true
    runtimeInitializationMayAllocate=$true
    managedCollectionRequested=$false
    qemuLaunched=$false
    qacReserved=$false
}
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory 'hosted-test-results.json'),
    ($report | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
Write-Output ("R7 managed-entry hosted tests: PASS assertions={0}; no guest boot or QAC reservation." -f $results.Count)
