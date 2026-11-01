param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = Join-Path $root "out\dotnet\c188r12r4r5r4-threadstore-readiness\hosted-$stamp"
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

function Assert-True([bool]$Value, [string]$Label) {
    if (-not $Value) { throw "C188R12R4R5R4 assertion failed: $Label" }
}
function Invoke-Analyzer([string]$Name, [string]$Text, [int]$ExpectedExit = 0) {
    $serialPath = Join-Path $testRoot "$Name.serial.log"
    $analysisPath = Join-Path $testRoot "$Name.analysis.json"
    [System.IO.File]::WriteAllText($serialPath, $Text, [System.Text.UTF8Encoding]::new($false))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $serialPath -OutputPath $analysisPath | Out-Null
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne $ExpectedExit) { throw "Analyzer exit for $Name was $exitCode, expected $ExpectedExit." }
    return Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json
}
function Add-Marker([System.Collections.Generic.List[string]]$Lines, [string]$Event, [string]$Raw0, [string]$Raw1, [bool]$WithSequence = $false) {
    $sequence = if ($WithSequence) { ' seq=0000000000000000' } else { '' }
    $Lines.Add("[C188R12R4R5R4_THREAD]$sequence event=$Event raw0=$Raw0 raw1=$Raw1")
}

$analyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4-threadstore-readiness.ps1'
$runtimeRoot = Join-Path $root 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\nativeaot\Runtime'
$platformSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp'
$palSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_minwin_replacement.cpp'
$palCommonSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_common_replacement.cpp'
$startupImportsSource = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_startup_imports.cpp'
$runtimePackBuilder = Join-Path $root 'tools\dotnet\runtime-pack\build-runtime-pack.ps1'
$adapterSource = Join-Path $root 'tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.cpp'
$kernelSource = Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp'
$threadStoreHeader = Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_threadstore_readiness.h'
$transitionTest = Join-Path $root 'tools\dotnet\runtime-pack\tests\c188r12r4r5r4_threadstore_transition_test.cpp'
$guestRunner = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
foreach ($path in @($analyzer, $runtimeRoot, $platformSource, $palSource, $palCommonSource, $startupImportsSource, $runtimePackBuilder, $adapterSource, $kernelSource, $threadStoreHeader, $transitionTest, $guestRunner)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required ThreadStore readiness input is missing: $path" }
}

# Compile the exact allocation-free telemetry push/pop helper used by the R4
# platform object. The NativeAOT ThreadStore and PAL remain guest-only; this
# hosted test checks stock frame preservation and bounded LIFO telemetry.
$compiler = 'C:\mingw64\bin\g++.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    $compilerCommand = Get-Command 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $compilerCommand) { throw 'A hosted MinGW g++ compiler is required for the ThreadStore frame-contract test.' }
    $compiler = $compilerCommand.Source
}
$transitionExe = Join-Path $testRoot 'threadstore-transition-test.exe'
& $compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-O2' '-o' $transitionExe $transitionTest
if ($LASTEXITCODE -ne 0) { throw 'ThreadStore telemetry frame-contract test did not compile.' }
& $transitionExe
if ($LASTEXITCODE -ne 0) { throw "ThreadStore telemetry frame-contract test failed with exit code $LASTEXITCODE." }

$runtimeInstance = Get-Content -LiteralPath (Join-Path $runtimeRoot 'RuntimeInstance.cpp') -Raw
$threadStore = Get-Content -LiteralPath (Join-Path $runtimeRoot 'threadstore.cpp') -Raw
$threadSource = Get-Content -LiteralPath (Join-Path $runtimeRoot 'thread.cpp') -Raw
$startupSource = Get-Content -LiteralPath (Join-Path $runtimeRoot 'startup.cpp') -Raw
$runtimeThreadStore = Get-Content -LiteralPath (Join-Path $runtimeRoot 'threadstore.inl') -Raw
$platformText = Get-Content -LiteralPath $platformSource -Raw
$palText = Get-Content -LiteralPath $palSource -Raw
$palCommonText = Get-Content -LiteralPath $palCommonSource -Raw
$startupImportsText = Get-Content -LiteralPath $startupImportsSource -Raw
$runtimePackBuildText = Get-Content -LiteralPath $runtimePackBuilder -Raw
$adapterText = Get-Content -LiteralPath $adapterSource -Raw
$kernelText = Get-Content -LiteralPath $kernelSource -Raw
$guestRunnerText = Get-Content -LiteralPath $guestRunner -Raw

Assert-True ($runtimeInstance -match '(?s)ThreadStore::Create\(pRuntimeInstance\).*?m_pThreadStore\s*=\s*pThreadStore.*?g_pTheRuntimeInstance\s*=\s*pRuntimeInstance') 'ThreadStore is created and published before the RuntimeInstance singleton'
Assert-True ($threadStore -match '(?s)ThreadStore::AttachCurrentThread\(bool fAcquireThreadStoreLock\).*?RawGetCurrentThread\(\).*?if\s*\(pAttachingThread->IsInitialized\(\)\).*?PalAttachThread\(pAttachingThread\).*?pAttachingThread->Construct\(\).*?TSF_Attached.*?m_ThreadList.PushHead') 'stock attachment is lazy, duplicate-safe, PAL-backed, constructed, flagged, then linked'
Assert-True ($threadSource -match '(?s)void Thread::Construct\(\).*?m_threadId\s*=\s*PalGetCurrentOSThreadId\(\).*?PalGetMaximumStackBounds\(&m_pStackLow, &m_pStackHigh\)') 'stock construction records PAL thread identity and requires valid stack bounds'
Assert-True ($runtimeThreadStore -match 'tls_CurrentThread' -and $runtimeThreadStore -match '(?s)GetCurrentThreadIfAvailable\(\).*?IsInitialized\(\)') 'raw TLS object exists before attachment and the safe accessor returns null while uninitialized'
Assert-True ($threadSource -match '(?s)void Thread::ReversePInvokeAttachOrTrapThread\(.*?ThreadStore::AttachCurrentThread\(\)') 'stock reverse transition requests current-thread attachment on demand'
Assert-True ($threadSource -match '(?s)FCIMPL1\(void, RhpReversePInvoke, ReversePInvokeFrame \* pFrame\).*?RawGetCurrentThread\(\).*?InlineTryFastReversePInvoke\(pFrame\).*?RhpReversePInvokeAttachOrTrapThread2\(pFrame\)') 'stock reverse-P/Invoke helper owns the frame and selects the attached fast path or lazy attach path'
Assert-True ($startupSource -match '(?s)bool RhInitialize\(bool isDll\).*?PalInit\(\).*?InitDLL\(PalGetModuleHandleFromPointer') 'RhInitialize performs PAL and InitDLL before returning success'
Assert-True ($platformText -match '(?s)initializeNativeAotModules\(\);.*?guideXosStockRhpReversePInvoke\(frame\)') 'the candidate registers modules before delegating the untouched runtime frame to stock attachment'
Assert-True ($platformText -match 'guideXosStockRhpReversePInvokeReturn\(frame\)' -and $platformText -match 'GuideXosThreadStorePushTelemetryFrame\(' -and $platformText -match 'GuideXosThreadStorePopTelemetryFrame\(') 'reverse transition entry and return delegate to stock and keep telemetry outside its frame'
Assert-True ($platformText -match '(?s)RuntimeInstance\* runtime = GetRuntimeInstance\(\);.*?runtime == nullptr\s*\?\s*nullptr\s*:\s*runtime->GetThreadStore\(\)') 'R4 checks the runtime singleton before reading its ThreadStore pointer'
Assert-True ($palText -match '(?s)PalAttachThread\(void\* thread\).*?g_flsIndex == kFlsOutOfIndexes.*?existing != nullptr && existing != thread.*?guidexos_nativeaot_pal_fls_set') 'freestanding PAL attachment rejects invalid/conflicting FLS state and sets the runtime thread value'
Assert-True ($threadSource -match '(?s)PalGetMaximumStackBounds\(&m_pStackLow, &m_pStackHigh\).*?RhFailFast\(\)') 'Thread::Construct fails closed when the selected PAL stack bounds call fails'
Assert-True ($startupImportsText -match '(?s)guidexos_nativeaot_startup_VirtualQuery\(.*?return 0;') 'the Windows import shim returns zero from VirtualQuery in the freestanding image'
Assert-True ($palCommonText -match 'guidexos_nativeaot_pal_stack_bounds\(&low, &high, &current\)' -and
    $palCommonText -match 'GX_THREADSTORE_EVENT_PAL_STACK_BOUNDS' -and
    $palCommonText -match 'GX_THREADSTORE_EVENT_PAL_STACK_CURRENT') 'replacement common PAL uses the kernel bounds callback and records its raw outputs'
Assert-True ($runtimePackBuildText.Contains('$removedRuntimeMembers += $threadStoreCommonPalMember') -and
    $runtimePackBuildText.Contains('$threadStoreCommonPalObjectArgument')) 'R4 removes stock PalRedhawkCommon and inserts the stack-bounds replacement into the locked archive'
Assert-True ($adapterText -match 'const bool nativeResult = RhInitialize\(false\)' -and $adapterText -match 'GX_THREADSTORE_EVENT_RHINITIALIZE_RETURN') 'adapter records raw RhInitialize Boolean separately from mapped status'
Assert-True ($kernelText -match '(?s)if\s*\(startupResult\s*!=\s*0\)\s*\{.*?managed_entry_blocked_init_failure.*?return;') 'failed RhInitialize remains a hard ManagedMain gate'
Assert-True ($kernelText -match '(?s)#if defined\(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS\) && !defined\(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS\).*?managed_main_gate=blocked reason=runtime-threadstore-attachment-unqualified') 'only R4 releases the prior successful-init diagnostic gate'
Assert-True ($platformText -match 'GuideXosThreadStorePushTelemetryFrame\(' -and
    $platformText -match '(?s)#if !defined\(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS\).*?transitionFrame\[0\]') 'the legacy frame-slot instrumentation is compiled only without the stock-delegating R4 profile'
Assert-True ($palText -match 'existing != nullptr && existing != thread' -and $threadStore -match 'if\s*\(pAttachingThread->IsInitialized\(\)\)\s*\{\s*return;') 'duplicate attachment is a no-op for an initialized runtime thread and same-thread PAL FLS is accepted'
Assert-True ($guestRunnerText -match '\$isThreadStoreReadiness\s*=\s*\[string\]\$candidateManifest\.mode\s+-ceq\s+''ThreadStoreReadiness''' -and
    $guestRunnerText -match "\[System\.IO\.FileMode\]::CreateNew" -and
    $guestRunnerText -match 'maxGuestBoots=1;automaticRetry=\$false' -and
    $guestRunnerText -match 'function Test-QacRunningOwned\(' -and
    $guestRunnerText -match 'Test-QacRunningOwned \$snapshot \$qacReservationId \$qacProcess\.Id' -and
    $guestRunnerText -match 'archiveMembership\.threadStorePalCommonReplacement' -and
    $guestRunnerText -match 'removedCount\s*-ne\s*0' -and
    $guestRunnerText -match 'threadStoreReadinessPalCommonStockMember' -and
    $guestRunnerText -match 'analyze-c188r12r4r5r4-threadstore-readiness\.ps1' -and
    $guestRunnerText -match '\$rawSerialPreservedByAnalysis\s*=\s*\$null\s*-ne\s*\$serialSha256BeforeAnalysis' -and
    $guestRunnerText -match '\$candidateManifest\.managedPe' -and
    $guestRunnerText -match '\$candidateManifest\.threadStoreReadinessPalCommonReplacementObject' -and
    $guestRunnerText -match 'PSObject\.Properties\[''stages''\]' -and
    $guestRunnerText.Contains('event=(?:managed_entry_blocked_init_failure|transition_attach_incomplete)') -and
    $guestRunnerText.Contains('outcome=(?:PASS|FAIL) callbacks=') -and
    $guestRunnerText -match 'Test-QacClear \$before') 'future R4 runner requires the isolated phase, a CreateNew one-boot guard, normal exclusive QAC preflight, and the R4 analyzer'

$runningOwnershipFunction = [regex]::Match($guestRunnerText,
    '(?s)function Test-QacRunningOwned\([^\r\n]+\)\s*\{.*?\r?\n\}')
Assert-True $runningOwnershipFunction.Success 'runner contains a bounded active-session ownership consistency predicate'
Invoke-Expression $runningOwnershipFunction.Value
$syntheticReservationId = '0123456789abcdef0123456789abcdef'
$syntheticOwnerPid = 22980
$syntheticList = @(
    'Coordinator: Online (guideXOS QAC 2.0.0)',
    'Reservations: 1',
    "  $syntheticReservationId | guideXOSServer | acceptance | Running | queue 0 | LEON2_RDT\guideX (PID $syntheticOwnerPid) | owner Verified | child PID 27920",
    'Managed sessions: 1'
) -join "`n"
$staleStatus = "Coordinator: Online`nQEMU processes: 0 (0 foreign)`nManaged sessions: 0; queued: 0; blocked: 0"
$activeStatus = "Coordinator: Online`nQEMU processes: 1 (0 foreign)`nManaged sessions: 1; queued: 0; blocked: 0"
$syntheticSnapshot = [pscustomobject]@{StatusExit=0;ListExit=0;Status=$staleStatus;List=$syntheticList}
Assert-True (-not (Test-QacRunningOwned $syntheticSnapshot $syntheticReservationId $syntheticOwnerPid)) 'stale QAC status is rejected when the list already shows a running owned reservation'
$syntheticSnapshot.Status = $activeStatus
Assert-True (Test-QacRunningOwned $syntheticSnapshot $syntheticReservationId $syntheticOwnerPid) 'consistent status/list with a verified owner PID passes the active-session predicate'

# Parser fixtures exercise success, failure, and unfinished boundaries. These
# are synthetic serial traces and do not claim that NativeAOT ran on a guest.
$noMarkers = Invoke-Analyzer 'no-markers' ''
Assert-True ($noMarkers.outcome -eq 'INCOMPLETE_TRACE' -and -not $noMarkers.managedEntryReached) 'empty capture remains incomplete'

$failed = [System.Collections.Generic.List[string]]::new()
Add-Marker $failed 'adapter_enter' '000000000000AAAA' '0000000000000000' $true
Add-Marker $failed 'rhinitialize_return' '0000000000000000' '0000000000000001' $true
Add-Marker $failed 'kernel_init_result' '0000000000000000' '0000000000000001'
Add-Marker $failed 'managed_entry_blocked_init_failure' '0000000000000000' '0000000000000001'
$failedAnalysis = Invoke-Analyzer 'rhinitialize-false-blocks-entry' ($failed -join "`n")
Assert-True ($failedAnalysis.outcome -eq 'INIT_FAILURE_BLOCKED' -and -not $failedAnalysis.managedEntryReached) 'RhInitialize false blocks entry without manufacturing later markers'

$inverted = @(
    '[C188R12R4R5R4_THREAD] event=adapter_enter raw0=000000000000AAAA raw1=0000000000000000'
    '[C188R12R4R5R4_THREAD] event=rhinitialize_return raw0=0000000000000001 raw1=0000000000000001'
) -join "`n"
$invertedAnalysis = Invoke-Analyzer 'inconsistent-init-mapping' $inverted 2
Assert-True ($invertedAnalysis.outcome -eq 'INIT_RESULT_INCONSISTENT' -and -not $invertedAnalysis.managedEntryReached) 'raw native success with adapter failure is surfaced as inconsistent'

$moduleFailure = [System.Collections.Generic.List[string]]::new()
foreach ($entry in @(
    @('adapter_enter','000000000000AAAA','0000000000000000'),
    @('rhinitialize_return','0000000000000001','0000000000000000'),
    @('kernel_init_result','0000000000000001','0000000000000000'),
    @('managed_entry_gate_open','0000000000000001','0000000000000000'),
    @('tls_installed','000000000000BBBB','000000000000CCCC'),
    @('runtime_instance','000000000000DDDD','0000000000000001'),
    @('threadstore','000000000000EEEE','0000000000000001'),
    @('current_thread_tls','000000000000CCCC','0000000000000000'),
    @('current_thread_state','000000000000FFFF','0000000000000000'),
    @('module_registration_enter','0000000000001111','0000000000002222'))) {
    Add-Marker $moduleFailure $entry[0] $entry[1] $entry[2]
}
$moduleFailureAnalysis = Invoke-Analyzer 'module-registration-incomplete' ($moduleFailure -join "`n")
Assert-True ($moduleFailureAnalysis.outcome -eq 'MODULE_REGISTRATION_INCOMPLETE' -and -not $moduleFailureAnalysis.managedEntryReached) 'successful initialization followed by missing registration completion stays a distinct failure boundary'

$nullSingletons = [System.Collections.Generic.List[string]]::new()
foreach ($entry in @(
    @('adapter_enter','000000000000AAAA','0000000000000000'),
    @('rhinitialize_return','0000000000000001','0000000000000000'),
    @('kernel_init_result','0000000000000001','0000000000000000'),
    @('managed_entry_gate_open','0000000000000001','0000000000000000'),
    @('tls_installed','000000000000BBBB','000000000000CCCC'),
    @('runtime_instance','0000000000000000','0000000000000000'),
    @('threadstore','0000000000000000','0000000000000000'),
    @('current_thread_tls','0000000000000000','0000000000000000'),
    @('current_thread_state','0000000000000000','0000000000000000'))) {
    Add-Marker $nullSingletons $entry[0] $entry[1] $entry[2]
}
$nullSingletonAnalysis = Invoke-Analyzer 'null-runtime-threadstore-tls' ($nullSingletons -join "`n")
Assert-True ($nullSingletonAnalysis.outcome -eq 'THREADSTORE_STATE_INVALID' -and
    -not $nullSingletonAnalysis.threadStoreHandoffReady) 'null RuntimeInstance, ThreadStore, and current-thread TLS never qualify readiness'

$attachFailure = [System.Collections.Generic.List[string]]::new()
foreach ($entry in @(
    @('adapter_enter','000000000000AAAA','0000000000000000'),
    @('rhinitialize_return','0000000000000001','0000000000000000'),
    @('kernel_init_result','0000000000000001','0000000000000000'),
    @('managed_entry_gate_open','0000000000000001','0000000000000000'),
    @('tls_installed','000000000000BBBB','000000000000CCCC'),
    @('runtime_instance','000000000000DDDD','0000000000000001'),
    @('threadstore','000000000000EEEE','0000000000000001'),
    @('current_thread_tls','000000000000CCCC','0000000000000000'),
    @('current_thread_state','000000000000FFFF','0000000000000000'),
    @('module_registration_enter','0000000000001111','0000000000002222'),
    @('module_registration_complete','0000000000001111','0000000000000001'),
    @('module_initialize_enter','0000000000001111','0000000000000001'),
    @('module_initialize_complete','0000000000001111','0000000000000001'),
    @('transition_enter','0000000000003333','000000000000FFFF'),
    @('pal_attach_enter','000000000000FFFF','0000000000000000'))) {
    Add-Marker $attachFailure $entry[0] $entry[1] $entry[2]
}
$attachFailureAnalysis = Invoke-Analyzer 'pal-attachment-incomplete' ($attachFailure -join "`n")
Assert-True ($attachFailureAnalysis.outcome -eq 'ATTACHMENT_INCOMPLETE' -and -not $attachFailureAnalysis.managedEntryReached) 'an unfinished stock transition never qualifies managed entry'

$stackQueryFailure = [System.Collections.Generic.List[string]]::new()
foreach ($entry in @(
    @('adapter_enter','000000000000AAAA','0000000000000000'),
    @('rhinitialize_return','0000000000000001','0000000000000000'),
    @('kernel_init_result','0000000000000001','0000000000000000'),
    @('managed_entry_gate_open','0000000000000001','0000000000000000'),
    @('tls_installed','000000000000BBBB','000000000000CCCC'),
    @('runtime_instance','000000000000DDDD','0000000000000001'),
    @('threadstore','000000000000EEEE','0000000000000001'),
    @('current_thread_tls','000000000000CCCC','0000000000000000'),
    @('current_thread_state','000000000000FFFF','0000000000000000'),
    @('module_registration_enter','0000000000001111','0000000000002222'),
    @('module_registration_complete','0000000000001111','0000000000000001'),
    @('module_initialize_enter','0000000000001111','0000000000000001'),
    @('module_initialize_complete','0000000000001111','0000000000000001'),
    @('transition_enter','0000000000003333','000000000000FFFF'),
    @('pal_attach_enter','000000000000FFFF','0000000000000000'),
    @('pal_attach_complete','000000000000FFFF','0000000000000000'),
    @('pal_thread_id','0000000000000042','0000000000000000'),
    @('pal_stack_current','0000000000001800','0000000000000005'))) {
    Add-Marker $stackQueryFailure $entry[0] $entry[1] $entry[2]
}
$stackQueryFailureAnalysis = Invoke-Analyzer 'pal-stack-query-failure' ($stackQueryFailure -join "`n")
Assert-True ($stackQueryFailureAnalysis.outcome -eq 'PAL_STACK_QUERY_FAILED' -and
    -not $stackQueryFailureAnalysis.threadStoreHandoffReady) 'a failed stack-bounds callback is distinguished from successful managed entry'

$complete = [System.Collections.Generic.List[string]]::new()
foreach ($entry in @(
    @('adapter_enter','000000000000AAAA','0000000000000000'),
    @('rhinitialize_return','0000000000000001','0000000000000000'),
    @('kernel_init_result','0000000000000001','0000000000000000'),
    @('managed_entry_gate_open','0000000000000001','0000000000000000'),
    @('tls_installed','000000000000BBBB','000000000000CCCC'),
    @('tls_gsbase','000000000000BBBB','000000000000BBBB'),
    @('runtime_instance','000000000000DDDD','0000000000000001'),
    @('threadstore','000000000000EEEE','0000000000000001'),
    @('current_thread_tls','000000000000CCCC','0000000000000000'),
    @('current_thread_state','000000000000FFFF','0000000000000000'),
    @('module_registration_enter','0000000000001111','0000000000002222'),
    @('module_registration_complete','0000000000001111','0000000000000001'),
    @('module_initialize_enter','0000000000001111','0000000000000001'),
    @('module_initialize_complete','0000000000001111','0000000000000001'),
    @('transition_enter','0000000000003333','000000000000FFFF'),
    @('pal_attach_enter','000000000000FFFF','0000000000000000'),
    @('pal_attach_existing','0000000000000000','000000000000FFFF'),
    @('pal_attach_complete','000000000000FFFF','0000000000000000'),
    @('pal_thread_id','0000000000000042','0000000000000000'),
    @('pal_stack_bounds','0000000000001000','0000000000002000'),
    @('pal_stack_current','0000000000001800','0000000000000000'),
    @('transition_attach_complete','000000000000FFFF','0000000000000042'),
    @('managed_dispatch','0000000012340000','0000000000000001'),
    @('managed_entry','000000000000FFFF','0000000000000001'),
    @('transition_return','0000000000003333','0000000000001111'))) {
    Add-Marker $complete $entry[0] $entry[1] $entry[2]
}
$completeAnalysis = Invoke-Analyzer 'complete-supported-handoff' ($complete -join "`n")
Assert-True ($completeAnalysis.outcome -eq 'THREADSTORE_HANDOFF_TRACE_COMPLETE' -and
    $completeAnalysis.threadStoreHandoffReady -and $completeAnalysis.managedEntryReached -and
    $completeAnalysis.markerOrderConsistent) 'complete source-ordered trace qualifies the supported handoff only'

$result = [ordered]@{
    result = 'PASS'
    hostedTestRoot = $testRoot
    transitionTelemetryFrameSimulation = 'PASS'
    actualNativeAotThreadStoreExecuted = $false
    parserClassifiesInitFailureAndIncompleteStages = $true
    stockReversePInvokeSourceContract = $true
    palDuplicateAndFailureContractsSourceChecked = $true
    qemuLaunched = $false
    qacReserved = $false
    sourceRamdiskModified = $false
}
[System.IO.File]::WriteAllText(
    (Join-Path $testRoot 'hosted-test-manifest.json'),
    ($result | ConvertTo-Json -Depth 8),
    [System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R4 ThreadStore readiness tests: PASS ($testRoot)"
