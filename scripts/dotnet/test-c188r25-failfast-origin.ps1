$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_failfast_origin.h') -Raw
$analyzer = Join-Path $PSScriptRoot 'analyze-c188r25-failfast-origin.py'
$analyzerSource = Get-Content -LiteralPath $analyzer -Raw
$guestRunner = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'run-c188-qac-guest.ps1') -Raw
$runWrapper = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'run-c188r25-failfast-origin.ps1') -Raw
$orchestrator = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'run-c188r24-initializemodules.ps1') -Raw
$candidateBuilder = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-c188r25-failfast-origin-candidate.ps1') -Raw
. (Join-Path $PSScriptRoot 'c188r25-failfast-runner-predicates.ps1')
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$checks = 0
function Assert-R25([bool]$Condition,[string]$Name) {
    $script:checks++
    if (-not $Condition) { throw "R25 fail-fast assertion failed: $Name" }
}

Assert-R25 ($header -match 'sizeof\(GuideXosR25FailFastOriginRecord\) == 80u') 'fixed ten-word record size'
Assert-R25 ($header -match 'kGuideXosR25FailFastFastFailCode = 7u') 'exact FAST_FAIL_FATAL_APP_EXIT mapping'
Assert-R25 ($source -match 'return reinterpret_cast<uintptr_t>\(_ReturnAddress\(\)\)') 'caller capture helper uses compiler return-address intrinsic'
Assert-R25 ($source -match '_InterlockedCompareExchange\(&g_guideXosR25FailFastClaim, 1, 0\) == 0') 'first failure claims record once'
Assert-R25 ($source -match 'record\.originalReason = reason;') 'source reason is retained before __fastfail overwrites RCX'
Assert-R25 ($source -match 'record\.callerCaptureReturnAddress = callerCaptureReturnAddress;') 'capture return PC is retained'
Assert-R25 ($source -match '_InterlockedExchange64\([\s\S]*record\.published') 'publication marker is written last with interlocked ordering'
Assert-R25 ($source -match 'guideXosR25TryEmitCapturedMarker\(\);[\s\S]*guideXosFailFastImpl\(reason\);') 'serial marker follows safe record publication and fail-fast remains terminal'
Assert-R25 ($source -match 'if \(\(__inbyte\(0x3FDu\) & 0x20u\) == 0u\) return;') 'marker output is bounded and nonblocking'
Assert-R25 ($source -match 'g_guideXosR25InitializationStage =\s*kGuideXosR25FailFastStageBeforeInitializeModules;') 'initialization stage is recorded before generated call'
Assert-R25 ($source -match 'g_guideXosR25InitializationStage =\s*kGuideXosR25FailFastStageInitializeModulesReturned;') 'successful return has its own stage'
Assert-R25 ($source -match '__fastfail\(kFastFailFatalAppExit\);\s*for \(;;\)') 'existing runtime fail-fast behavior remains intact'
Assert-R25 ($source -notmatch 'g_guideXosR25FailFastOriginRecord[^;]*Thread\s*\*') 'record does not alias a NativeAOT Thread'
Assert-R25 (Test-Path -LiteralPath $analyzer -PathType Leaf) 'offline analyzer is present'
Assert-R25 ((@(Get-C188R25HmpWords '00000001001e14e0: 0x0000000000000001 0x0000000000000002')).Count -eq 2) 'QEMU HMP record lines without a 0x address prefix retain both fixed-width words'
Assert-R25 ($source.IndexOf('#define guideXosFailFast(reason)') -ge 0 -and
    $source.IndexOf('#define guideXosFailFast(reason)') -lt $source.IndexOf('#if defined(GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION)')) 'capture wrapper covers every fail-fast caller, independent of the optional suspension profile'
Assert-R25 ($header -match 'originalReason' -and $header -match 'callerCaptureReturnAddress' -and
    $header -match 'failFastImplementationAddress' -and $header -match 'initializationStage' -and
    $header -match 'published') 'bounded record carries reason, caller, implementation, stage, and validity fields'
Assert-R25 ($guestRunner -match 'r25CaptureConfig\.recordBytes\s*-ne 80' -and
    $guestRunner -match 'Get-C188R25HmpWords \$snapshot\.recordText' -and
    $guestRunner -match 'x/10gx ' -and $guestRunner -match 'x/8gx ' ) 'guest capture validates the configured record size and bounds record/stack reads'
$pauseIndex = $guestRunner.IndexOf("Invoke-Qmp 'stop'")
$registerIndex = $guestRunner.IndexOf("Invoke-Qmp 'human-monitor-command' ([ordered]@{'command-line'='info registers'})")
$recordReadIndex = $guestRunner.IndexOf("'x/10gx '")
Assert-R25 ($pauseIndex -ge 0 -and $registerIndex -gt $pauseIndex -and $recordReadIndex -gt $registerIndex) 'QMP confirms pause before register and first-failure memory capture'
Assert-R25 ($guestRunner -match 'Refusing to overwrite R25 fail-fast evidence' -and
    $guestRunner -match 'record\.published|words\[9\] -eq 0x5055424C49534844') 'capture refuses evidence overwrite and validates last-word publication'
Assert-R25 ($guestRunner.IndexOf('$text.Contains(''[C188R25-FAILFAST-CAPTURED]'')') -ge 0 -and
    $guestRunner.IndexOf("Save-C188R25FailFastOriginSnapshot 'fatal-marker'") -lt
    $guestRunner.IndexOf("throw 'R25 captured a published first-failure record and paused CPU/stack evidence.'")) 'an explicit fatal marker captures before the runner exits its readiness monitor'
Assert-R25 ($orchestrator -match "FAILFAST_QUALIFICATION_GUEST_DEFERRED" -and
    $orchestrator -match 'maxGuestBoots=1;automaticRetry=\$false' -and
    $orchestrator -match 'retryAllowed=\$false') 'QAC deferral and a single non-retrying boot guard are explicit'
Assert-R25 ($orchestrator -match '\$final\.r25FirstFailureRecordValid' -and
    $orchestrator -match 'FAILFAST_ORIGIN_ATTRIBUTED' -and
    $analyzerSource -match 'failureContractProven.*False') 'runner distinguishes linked caller attribution from proof of the violated contract'
Assert-R25 ($orchestrator -match 'The boot produced no valid first-failure record and no accepted initialization handoff' -and
    $orchestrator -match 'r25-final-artifact-hashes\.json' -and
    $orchestrator.IndexOf('$process.WaitForExit()') -lt $orchestrator.IndexOf('$r25HashManifestPath = Join-Path $bootRoot ''r25-final-artifact-hashes.json''')) 'missing diagnostics cannot become success, and final capture hashes follow child exit'
$attributedOutcomeIndex = $orchestrator.IndexOf('elseif ($isR25 -and [bool]$final.r25FirstFailureRecordValid')
$initializerReturnIndex = $orchestrator.IndexOf('} elseif ($m7)')
Assert-R25 ($attributedOutcomeIndex -ge 0 -and $initializerReturnIndex -gt $attributedOutcomeIndex) 'a proven R25 caller outcome is preserved before ordinary milestone fallbacks'
Assert-R25 ($runWrapper -match 'candidateIntegritySha256' -and
    $runWrapper -match 'R25ManifestPath' -and $orchestrator -match 'if \(-not \$clear\)') 'runner binds R25 evidence to the candidate and gates launch on exclusive QAC preflight'
Assert-R25 ($candidateBuilder -match 'candidate-' -and
    $candidateBuilder -match 'Test-Path -LiteralPath \$candidate' -and
    $candidateBuilder -match 'candidateIntegritySha256') 'candidate packaging uses a fresh path and binds its integrity inventory'
$clearIndex = $orchestrator.IndexOf('if (-not $clear)')
$guardIndex = $orchestrator.IndexOf('$null = New-Item -ItemType Directory -Path $guardRoot -Force')
$launchIndex = $orchestrator.IndexOf('[System.Diagnostics.Process]::Start($psi)')
Assert-R25 ($clearIndex -ge 0 -and $guardIndex -gt $clearIndex -and $launchIndex -gt $guardIndex) 'a non-clear exclusive census returns before consuming the one-boot guard or starting QAC'
Assert-R25 ((Get-C188R25FatalCaptureAction $false $false $false) -eq 'MONITOR_READINESS') 'normal startup keeps monitoring for readiness'
Assert-R25 ((Get-C188R25FatalCaptureAction $true $true $true) -eq 'CAPTURED_FATAL') 'valid published evidence plus a confirmed pause ends readiness immediately'
Assert-R25 ((Get-C188R25FatalCaptureAction $true $false $true) -eq 'RESUME_AND_MONITOR') 'invalid fatal evidence resumes a paused guest and continues monitoring'
Assert-R25 ((Get-C188R25FatalCaptureAction $true $true $false) -eq 'RESUME_AND_MONITOR') 'a record without a verified pause cannot trigger early guest termination'
$saveIndex = $guestRunner.IndexOf("Save-C188R25FailFastOriginSnapshot 'fatal-marker'")
$fatalThrowIndex = $guestRunner.IndexOf("throw 'R25 captured a published first-failure record and paused CPU/stack evidence.'")
Assert-R25 ($saveIndex -ge 0 -and $fatalThrowIndex -gt $saveIndex) 'runner persists the diagnostic snapshot before declaring captured fatal'

$pythonOutput = & $python $analyzer 2>&1 | Out-String
Assert-R25 ($LASTEXITCODE -eq 0 -and $pythonOutput -match '"result": "PASS"') 'analyzer hosted invocation succeeds'
Write-Output "R25_FAILFAST_ORIGIN_ASSERTIONS=$checks RESULT=PASS"
