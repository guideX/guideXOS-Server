$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$registry = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\GuideXos\GuideXosApplication.cs') -Raw
$project = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\HostLogProof.csproj') -Raw
$factory = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\GuideXos\C188R12R4FactoryDiagnostics.cs') -Raw
$settings = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\Applications\ManagedSettingsCenter.cs') -Raw
$runtimeBuilder = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\build-runtime-pack.ps1') -Raw
$managedBuilder = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\build-managed-hostlog-proof.ps1') -Raw
$startupShim = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\c102_startup.c') -Raw
$runtimePlatform = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp') -Raw
$kernelStartupMarkers = Get-Content -LiteralPath (Join-Path $repo 'kernel\core\nativeaot_application.cpp') -Raw
$runtimeStartupContract = Get-Content -LiteralPath (Join-Path $repo 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\nativeaot\Common\src\Internal\Runtime\CompilerHelpers\StartupCodeHelpers.cs') -Raw
$candidateBuilder = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\build-c188r22-settings-candidate.ps1') -Raw
$runner = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\run-c188r12r1-settings-handoff.ps1') -Raw
$unwindSource = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_startup_imports.cpp') -Raw
$r20Report = Get-Content -LiteralPath (Join-Path $repo 'docs\dotnet\NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R20_ITERATOR_ADVANCEMENT_ROOT_CAUSE.md') -Raw
$r20Serial = Get-Content -LiteralPath 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261114T034806846Z-7856c3fb75104ce082dd6748ba7e3551\serial.log' -Raw
$unwindSource = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_startup_imports.cpp') -Raw
$unwindRegression = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\test-c188r20-iterator-advancement.py') -Raw
$cases = 0
function Assert-R22([bool]$Condition, [string]$Name) {
    $script:cases++
    if (-not $Condition) { throw "R22 Settings bootstrap regression failed: $Name" }
}

Assert-R22 ($registry -match '(?s)RegisterC188R11Descriptor\(5u, "Managed Settings Center"u8,\s*#if HOSTLOGPROOF_C188R12R4_FACTORY\s*static \(\) => C188R12R4FactoryDiagnostics\.CreateSettingsCenter\(\)') 'selector 5 resolves to the actual Settings factory'
$ownershipProofCondition = @'
Condition="'$(HostLogProofC188R12R4R5GcOwnershipProof)' == 'true' and '$(HostLogProofC188R21ManagedSettingsFactory)' != 'true'"
'@
Assert-R22 ($project.Contains($ownershipProofCondition)) 'R21 keeps allocator authorization separate from the standalone GC proof entry point'
Assert-R22 ($settings.Contains('public sealed class ManagedSettingsCenter') -and
    $settings.Contains('C188R12R4FactoryDiagnostics.Emit("constructor-enter"u8') -and
    $settings.Contains('C188R12R4FactoryDiagnostics.Emit("controls-init-return"u8')) 'existing Settings constructor and control initialization remain in the path'
Assert-R22 ($factory.Contains('NativeGetMaximumStackBounds') -and
    $factory.Contains('currentStackPointer - stackLow') -and
    $factory.Contains('C188R21-STACK stage=settings-factory-entry')) 'factory-entry stack budget comes from live PAL bounds and a stack marker'
Assert-R22 ($registry.Contains('new WeakReference<GuideXosApplication>(application)') -and
    $registry.Contains('application = null;') -and
    $registry.Contains('GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced') -and
    $registry.Contains('s_lifetime.TryGet(selector, generation') -and
    $registry.Contains('ReferenceEquals(weakTarget, resident)')) 'temporary weak reference proves the existing App Model root across a real collection'
Assert-R22 ($managedBuilder.Contains('C188R21ManagedSettingsFactory') -and
    $managedBuilder.Contains('$managedProjectMode -ne ''C160Composite''') -and
    $runtimeBuilder.Contains('experimental-nativeaot-workstation-gc-object-and-array-allocation-no-c188-arena')) 'R21 profile binds C160 application ownership to real Workstation GC without the arena'
Assert-R22 ($unwindSource -match '(?s)guidexos_nativeaot_startup_RtlVirtualUnwind\s*\(.*?\)\s*\{.*?guideXosRtlVirtualUnwindWindows\(' -and
    $unwindRegression.Contains('xor eax,eax; ret') -and
    $unwindRegression.Contains('guideXosRtlVirtualUnwindWindows(')) 'R20 startup import regression rejects the known no-op unwind path'

. (Join-Path $PSScriptRoot 'c188r12r1-settings-handoff-predicates.ps1')
$validR21Capture = @'
[C102-MANAGED-OUTPUT] C188R12R3_LIFETIME invocation=17 stage=exit-success selector=5 identity=com.guidexos.apps.managed.settingscenter count=1 mask=1 generation=4 code=0 state=resident
[C102-MANAGED-OUTPUT] C188R21-ROOT stage=post-gc selector=5 generation=4 gc=1 weak=1 owner=1 same=1 result=PASS
[C102-MANAGED-OUTPUT] C188R11 state seq=8 st=settings-launch-return selector=5 reset=0 m=1 g=4 td=0 n=5 result=0 gc=1 w=0:0
'@
$rootEvidence = Get-C188R12R1SettingsHandoffEvidence $validR21Capture
Assert-R22 ($rootEvidence.r21ManagedRootVerified -and
    $rootEvidence.settingsRegistryReturnValid -and
    $rootEvidence.settingsRegistryMemoryStable -and
    $rootEvidence.persistentHeapPreserved) 'R21 compacting-GC root evidence satisfies the settings handoff ownership predicate'
$invalidR21Capture = $validR21Capture.Replace('generation=4 gc=1 weak=1 owner=1 same=1 result=PASS',
    'generation=3 gc=1 weak=1 owner=1 same=1 result=PASS')
$invalidEvidence = Get-C188R12R1SettingsHandoffEvidence $invalidR21Capture
Assert-R22 (-not $invalidEvidence.r21ManagedRootVerified -and
    -not $invalidEvidence.settingsRegistryReturnValid) 'generation mismatch fails closed in the R21 root predicate'

. (Join-Path $PSScriptRoot 'c188r22-bootstrap-handoff-predicates.ps1')
$tlsPosition = $startupShim.IndexOf('context->startupMarker(7u)')
$prepareBeginPosition = $startupShim.IndexOf('context->startupMarker(C188R11_STARTUP_BOOTSTRAP_BEGIN)')
$prepareCallPosition = $startupShim.IndexOf('guidexos_nativeaot_prepare_managed_entry()')
$readyStatePosition = $startupShim.IndexOf('g_startupState = kStartupReady;', $prepareCallPosition)
$readyMarkerPosition = $startupShim.IndexOf('C188R11_STARTUP_INITIALIZE_READY', $prepareCallPosition)
$managedMainPosition = $startupShim.IndexOf('ManagedMain(context->managedContext)', $readyMarkerPosition)
Assert-R22 ($tlsPosition -ge 0 -and $tlsPosition -lt $prepareBeginPosition -and
    $prepareBeginPosition -lt $prepareCallPosition) 'shared module preparation begins only after current-thread TLS installation'
Assert-R22 ($prepareCallPosition -ge 0 -and $prepareCallPosition -lt $readyStatePosition -and
    $readyStatePosition -lt $readyMarkerPosition -and $readyMarkerPosition -lt $managedMainPosition) 'C188B002 and ManagedMain follow successful shared preparation and Ready publication'
Assert-R22 ($startupShim.Contains('context->startupMarker(C188R11_STARTUP_BOOTSTRAP_READY)') -and
    $startupShim.Contains('return -105;') -and $startupShim.Contains('g_startupState = kStartupFailed;')) 'bootstrap begin/return markers bracket a fail-closed helper result'
Assert-R22 ($startupShim.Contains('#define C188R11_STARTUP_BOOTSTRAP_BEGIN 0xC188B004u') -and
    $startupShim.Contains('#define C188R11_STARTUP_BOOTSTRAP_READY 0xC188B005u') -and
    $kernelStartupMarkers.Contains('bootstrap-handoff-begin') -and
    $kernelStartupMarkers.Contains('bootstrap-handoff-ready')) 'native serial names distinguish helper entry from helper return'
Assert-R22 ($runtimePlatform -match '(?s)guidexos_nativeaot_prepare_managed_entry\(void\)\s*\{.*?currentTlsBlock\(\).*?_tls_index == kFlsOutOfIndexes.*?initializeRuntimeState\(block\).*?g_guideXosNativeAotModulesInitialized.*?g_guideXosNativeAotCodeManagerRegistered') 'shared helper validates TLS, runs real runtime initialization, and checks module/code-manager state'
Assert-R22 ($runtimePlatform -match '(?s)extern "C" void InitializeModules\(\s*void\* osModule, void\*\* pModuleHeaders, int count,\s*void\*\* pClasslibFunctions, int nClasslibFunctions\);' -and
    $runtimePlatform -match '(?s)InitializeModules\(\s*osModule,\s*__modules_a,\s*static_cast<int>\(__modules_z - __modules_a\),\s*g_guideXosNativeAotClasslibFunctions,\s*16\s*\);') 'runtime calls the generated five-argument InitializeModules ABI with the real module and function table'
Assert-R22 ($runtimeStartupContract -match '(?s)UnmanagedCallersOnly\(EntryPoint = "InitializeModules"\).*?InitializeModules\(IntPtr osModule, IntPtr\* pModuleHeaders, int count, IntPtr\* pClasslibFunctions, int nClasslibFunctions\)') 'locked NativeAOT source confirms the five-argument unmanaged entry contract'
Assert-R22 ($r20Serial -match '(?s)transition_initialize_modules_returned.*?before_managed_main.*?modules=00000001 codeManager=00000001' -and
    $candidateBuilder.Contains('r20-unwind-regression') -and
    $runtimePlatform.Contains('guideXosStockRhpReversePInvoke(frame)')) 'R20 accepted ordering and pinned reverse-P/Invoke remain part of candidate provenance'
Assert-R22 ($candidateBuilder.Contains('guidexos_nativeaot_prepare_managed_entry') -and
    $candidateBuilder.Contains('ManagedSettingsCenter___ctor_0') -and
    $candidateBuilder.Contains('candidate-integrity.json')) 'candidate gate requires the shared helper, real factory, and full integrity inventory'
$missingFactory = @(Get-C188R22MissingCandidateSymbols 'ManagedMain InitializeModules' @('ManagedMain','InitializeModules','ManagedSettingsCenter___ctor_0'))
Assert-R22 ($missingFactory.Count -eq 1 -and $missingFactory[0] -eq 'ManagedSettingsCenter___ctor_0') 'candidate validation rejects an unresolved real Settings factory symbol'
$wrongInitializeModulesAbi = $runtimePlatform.Replace('int nClasslibFunctions);','void* nClasslibFunctions);')
Assert-R22 (-not ($wrongInitializeModulesAbi -match '(?s)extern "C" void InitializeModules\(\s*void\* osModule, void\*\* pModuleHeaders, int count,\s*void\*\* pClasslibFunctions, int nClasslibFunctions\);')) 'candidate ABI contract rejects a wrong InitializeModules argument type'
Assert-R22 ($unwindSource -match '(?s)guidexos_nativeaot_startup_RtlVirtualUnwind\s*\(.*?\)\s*\{.*?guideXosRtlVirtualUnwindWindows\(' -and
    $candidateBuilder.Contains('linkedByManagedProof')) 'R20 real unwind import is retained and link provenance remains required'
$r21TerminalCapture = Get-C188R22BootstrapBoundaryEvidence @'
[C102-STARTUP] stage=00000006
[C102-STARTUP] stage=00000007
[C102-STARTUP] stage=C188B002
'@
Assert-R22 ($r21TerminalCapture.c188b002Observed -and
    -not $r21TerminalCapture.managedSettingsLaunchEntered -and
    -not $r21TerminalCapture.c188b002IsManagedEntry -and
    $r21TerminalCapture.firstMissingMilestone -eq 'shared-NativeAOT-entry-preparation-began') 'historical C188B002 alone is not interpreted as managed entry or module initialization'
$preparedOnlyCapture = Get-C188R22BootstrapBoundaryEvidence @'
[C102-STARTUP] stage=00000006
[C102-STARTUP] stage=00000007
[C102-STARTUP] stage=C188B004
[C102-STARTUP] stage=C188B005
[C102-STARTUP] stage=C188B002
'@
Assert-R22 ($preparedOnlyCapture.prepareReturned -and $preparedOnlyCapture.c188b002Observed -and
    -not $preparedOnlyCapture.managedSettingsLaunchEntered -and
    -not $preparedOnlyCapture.realSettingsFactoryEntered -and
    $preparedOnlyCapture.firstMissingMilestone -eq 'managed-Settings-launch-entry') 'module preparation and native-ready markers still do not fabricate managed factory entry'
$enteredCapture = Get-C188R22BootstrapBoundaryEvidence @'
[C102-STARTUP] stage=00000006
[C102-STARTUP] stage=00000007
[C102-STARTUP] stage=C188B004
[C102-STARTUP] stage=C188B005
[C102-STARTUP] stage=C188B002
[C102-MANAGED-OUTPUT] C188R11 state seq=8 st=launch-entry selector=5 reset=0 m=1 g=4 td=0 n=5 result=0
[C102-MANAGED-OUTPUT] C188R21-STACK stage=settings-factory-entry remaining=00001000
'@
Assert-R22 ($enteredCapture.managedSettingsLaunchEntered -and
    $enteredCapture.realSettingsFactoryEntered -and
    $enteredCapture.firstMissingMilestone -eq 'none-before-factory-entry') 'only actual managed selector-5 and factory markers qualify managed Settings entry'
$timeoutTerminal = Get-C188R22RunnerTerminalEvidence '' 'run-c188-qac-guest.ps1:287 Guest did not reach the input loop before timeout.' 1 '{"execute":"quit"}'
Assert-R22 ($timeoutTerminal.readinessTimeout -and
    -not $timeoutTerminal.guestExitedBeforeReadiness -and
    $timeoutTerminal.runnerQuitAfterReadinessTimeout -and
    $timeoutTerminal.terminalCause -eq 'guest-input-loop-readiness-timeout') 'nested timeout remains a readiness timeout after runner exit and normal QMP cleanup'
$qemuExitTerminal = Get-C188R22RunnerTerminalEvidence '' 'QAC/QEMU exited before guest readiness: 0' 1 ''
Assert-R22 (-not $qemuExitTerminal.readinessTimeout -and
    $qemuExitTerminal.guestExitedBeforeReadiness -and
    $qemuExitTerminal.terminalCause -eq 'qac-qemu-exited-before-guest-readiness') 'actual QAC/QEMU exit before readiness is classified separately from timeout'
$genericRunnerExit = Get-C188R22RunnerTerminalEvidence '' 'unclassified runner error' 1 ''
Assert-R22 (-not $genericRunnerExit.readinessTimeout -and
    -not $genericRunnerExit.guestExitedBeforeReadiness -and
    $genericRunnerExit.terminalCause -eq 'runner-failed-unclassified') 'generic runner failure is not promoted to a guest exit diagnosis'
Assert-R22 ($runner.Contains('Get-C188R22RunnerTerminalEvidence') -and
    $runner.Contains('runnerTerminalEvidence=$runnerTerminalEvidence') -and
    $runner.Contains("Join-Path `$bootRoot 'qmp.log'") -and
    $runner.Contains('The nested guest runner timed out')) 'R21 evidence records nested terminal cause and preserves timeout provenance'
Assert-R22 ($runtimeBuilder.Contains('C188R22SettingsBootstrapHandoff') -and
    $runtimeBuilder.Contains('c188r22SettingsBootstrapHandoff') -and
    $managedBuilder.Contains('-C188R22SettingsBootstrapHandoff')) 'runtime manifest and managed build carry a distinct R22 profile identity'

if ($cases -ne 29) { throw "R22 regression case count mismatch: $cases" }
Write-Output "C188R22-SETTINGS-BOOTSTRAP cases=$cases result=PASS"
