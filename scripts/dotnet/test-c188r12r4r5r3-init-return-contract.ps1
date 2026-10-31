param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$CandidateRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = Join-Path $root "out\dotnet\c188r12r4r5r3-init-return-contract\hosted-$stamp"
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted evidence: $testRoot" }
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

function Assert-True([bool]$Value, [string]$Label) {
    if (-not $Value) { throw "C188R12R4R5R3 assertion failed: $Label" }
}
function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Invoke-Analysis([string]$Name, [string]$Text, [string]$ExistingSerialPath = '') {
    $serial = Join-Path $testRoot "$Name.serial.log"
    $analysisPath = Join-Path $testRoot "$Name.analysis.json"
    if ([string]::IsNullOrWhiteSpace($ExistingSerialPath)) {
        [System.IO.File]::WriteAllText($serial, $Text, [System.Text.UTF8Encoding]::new($false))
    } else {
        $serial = [System.IO.Path]::GetFullPath($ExistingSerialPath)
    }
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $analyzer -SerialPath $serial -OutputPath $analysisPath | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Serial analyzer failed for $Name." }
    return [pscustomobject]@{
        serial = $serial
        analysisPath = $analysisPath
        analysis = Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json
    }
}

$contractHeader = Join-Path $root 'tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_rh_initialize_contract.h'
$adapterSource = Join-Path $root 'tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.cpp'
$analyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r1-init-serial.ps1'
$fixture = Join-Path $root 'tests\dotnet\fixtures\nativeaot\C188R12R4R5R2-guest.serial.log'
$testSource = Join-Path $root 'tools\dotnet\runtime-pack\tests\c188r12r4r5r3_rh_initialize_contract_test.cpp'
foreach ($path in @($contractHeader, $adapterSource, $analyzer, $fixture, $testSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
}

# Compile and run the same pure mapping helper used in the freestanding adapter.
$compiler = 'C:\mingw64\bin\g++.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    $compilerCommand = Get-Command 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $compilerCommand) { throw 'A hosted MinGW g++ compiler is required for the return-contract regression.' }
    $compiler = $compilerCommand.Source
}
$unitExe = Join-Path $testRoot 'rh-initialize-contract-test.exe'
& $compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-O2' '-o' $unitExe $testSource
if ($LASTEXITCODE -ne 0) { throw 'The RhInitialize return-contract unit test did not compile.' }
& $unitExe
if ($LASTEXITCODE -ne 0) { throw "The RhInitialize return-contract unit test failed with exit code $LASTEXITCODE." }

# The R2 serial is copied byte-for-byte as a fixture; its missing stage records
# stay missing, and its legacy value remains classified only as adapter status.
$fixtureHashBefore = Get-Sha256 $fixture
Assert-True ($fixtureHashBefore -eq '0F40D24DEB8BB9E527961BC47F8698E111DCD74902A074BE8210ECC1F4DEC53E') 'R4R5R2 raw serial fixture retains its original SHA-256'
$fixtureAnalysis = Invoke-Analysis 'r2-raw-guest-fixture' '' $fixture
$fixtureResult = $fixtureAnalysis.analysis
Assert-True (-not $fixtureResult.initTraceComplete) 'R2 raw trace remains incomplete'
Assert-True (-not $fixtureResult.traceTransportAvailable) 'R2 missing stage markers are not fabricated'
Assert-True ($null -eq $fixtureResult.rhInitializeResult) 'missing stage events do not fabricate a native result'
Assert-True ($null -eq $fixtureResult.nativeRhInitializeBoolean) 'legacy R2 status is not presented as an unmodified native Boolean'
Assert-True ($fixtureResult.startupAdapterStatusCode -eq '00000001') 'legacy R2 value is preserved as adapter status code 1'
Assert-True ($fixtureResult.startupAdapterOutcome -eq 'failure') 'source-defined adapter status 1 is classified as startup failure'
Assert-True $fixtureResult.managedMainBlockedOnAdapterFailure 'R2 adapter failure and absent ManagedMain call are reported separately'
Assert-True ((Get-Sha256 $fixture) -eq $fixtureHashBefore) 'analysis did not rewrite the preserved R2 fixture'

# Explicit R3 markers prove both directions while missing stages remain missing.
$nativeSuccess = Invoke-Analysis 'native-true-maps-to-status-zero' (
    "[C188R12R4R5R3_INIT] init_result nativeBool=00000001 adapterStatus=00000000 nativeOutcome=success adapterOutcome=success`n")
Assert-True ($nativeSuccess.analysis.nativeRhInitializeBoolean -eq '00000001') 'native true is recorded as raw Boolean 1'
Assert-True ($nativeSuccess.analysis.nativeRhInitializeOutcome -eq 'success') 'native true is classified as success'
Assert-True ($nativeSuccess.analysis.startupAdapterStatusCode -eq '00000000') 'native true maps to adapter status 0'
Assert-True $nativeSuccess.analysis.explicitReturnMarkerConsistent 'successful raw and adapter outcomes agree with serialized labels'
Assert-True $nativeSuccess.analysis.explicitReturnMappingConsistent 'successful native Boolean agrees with the zero-success adapter status'
Assert-True ($null -eq $nativeSuccess.analysis.rhInitializeResult) 'missing stage records remain unknown despite a separate raw-result marker'
Assert-True (-not $nativeSuccess.analysis.initTraceComplete) 'raw result does not stand in for a complete stage trace'

$nativeFailure = Invoke-Analysis 'native-false-maps-to-status-one' (
    "[C188R12R4R5R3_INIT] init_result nativeBool=00000000 adapterStatus=00000001 nativeOutcome=failure adapterOutcome=failure`n")
Assert-True ($nativeFailure.analysis.nativeRhInitializeBoolean -eq '00000000') 'native false is recorded as raw Boolean 0'
Assert-True ($nativeFailure.analysis.nativeRhInitializeOutcome -eq 'failure') 'native false is classified as failure'
Assert-True ($nativeFailure.analysis.startupAdapterStatusCode -eq '00000001') 'native false maps to adapter status 1'
Assert-True $nativeFailure.analysis.explicitReturnMarkerConsistent 'failed raw and adapter outcomes agree with serialized labels'
Assert-True $nativeFailure.analysis.explicitReturnMappingConsistent 'failed native Boolean agrees with the nonzero-failure adapter status'
Assert-True $nativeFailure.analysis.managedMainBlockedOnAdapterFailure 'failed initialization keeps managed entry blocked'

$invertedStatus = Invoke-Analysis 'inconsistent-native-status-pair' (
    "[C188R12R4R5R3_INIT] init_result nativeBool=00000001 adapterStatus=00000001 nativeOutcome=success adapterOutcome=failure`n")
Assert-True (-not $invertedStatus.analysis.explicitReturnMappingConsistent) 'an impossible raw-success/status-failure pair is surfaced'
$invertedFooter = Invoke-Analysis 'inconsistent-trace-footer-status' (
    "[C188R12R4R5R1_INIT] trace_end invocation=00000001 count=00000000 overflow=00000000 serialDropped=00000000 adapterStatus=00000001`n" +
    "[C188R12R4R5R3_INIT] init_result nativeBool=00000001 adapterStatus=00000000 nativeOutcome=success adapterOutcome=success`n")
Assert-True (-not $invertedFooter.analysis.returnStatusMarkerConsistent) 'a conflicting trace footer status is surfaced'

# Stage B remains independent: a successful RhInitialize return followed by
# failed module registration is not rewritten as an initialization failure.
$moduleFailureText = @(
    '[C188R12R4R5R1_INIT] invocation=00000001 stage=00000001 event=00000001 code=00000000 singleton=00000000'
    '[C188R12R4R5R1_INIT] invocation=00000001 stage=00000001 event=00000002 code=00000000 singleton=00000000'
    '[C188R12R4R5R1_INIT] invocation=00000001 stage=00000002 event=00000001 code=00000000 singleton=00000000'
    '[C188R12R4R5R1_INIT] invocation=00000001 stage=00000002 event=00000002 code=00000000 singleton=0000001F'
    '[C188R12R4R5R1_INIT] invocation=00000001 stage=00000061 event=00000001 code=00000000 singleton=0000001F'
    '[C188R12R4R5R1_INIT] invocation=00000001 stage=00000061 event=00000003 code=FFFFFFFF singleton=0000001F'
    '[C188R12R4R5R1_INIT] trace_end invocation=00000001 count=00000006 overflow=00000000 serialDropped=00000000 adapterStatus=00000000'
    '[C188R12R4R5R3_INIT] init_result nativeBool=00000001 adapterStatus=00000000 nativeOutcome=success adapterOutcome=success'
) -join "`n"
$moduleFailure = Invoke-Analysis 'module-registration-failure-after-init-success' ($moduleFailureText + "`n")
Assert-True ($moduleFailure.analysis.rhInitializeResult -eq 'success') 'module-registration failure does not change RhInitialize classification'
Assert-True ($moduleFailure.analysis.startupAdapterOutcome -eq 'success') 'adapter status remains successful when later module registration fails'
Assert-True $moduleFailure.analysis.returnStatusMarkerConsistent 'trace footer status agrees with the separate raw/status marker'
Assert-True (@($moduleFailure.analysis.stages | Where-Object {
    $_.stageName -eq 'moduleRegistration' -and $_.status -eq 'returned_failure'
}).Count -eq 1) 'module registration failure is independently classified'
Assert-True ($moduleFailure.analysis.earliestExplicitFailure.stageName -eq 'moduleRegistration') 'module registration is the first explicit failing boundary'
Assert-True (-not $moduleFailure.analysis.managedMainDispatched) 'failed module registration has no managed-entry evidence'

$adapterText = Get-Content -LiteralPath $adapterSource -Raw
Assert-True ($adapterText -match 'extern\s+"C"\s+bool\s+RhInitialize\(bool\s+isDll\)') 'probe declaration matches the pinned bool(bool) signature'
Assert-True ($adapterText -match 'const\s+bool\s+nativeResult\s*=\s*RhInitialize\(false\)') 'probe passes the Boolean false argument directly'
Assert-True ($adapterText -match 'g_rhInitializeRawResult\s*=\s*observation\.native_boolean') 'raw Boolean is serialized through its own accessor'
Assert-True ($adapterText -match 'g_startupResult\s*=\s*static_cast<unsigned int>\(observation\.adapter_status\)') 'integer status is mapped separately from the native Boolean'
Assert-True ($adapterText -notmatch 'RhInitialize\(0\)\s*\?\s*1u\s*:\s*0u') 'probe does not invert the upstream success Boolean'
Assert-True ($adapterText -notmatch '\b(?:RhRegisterOSModule|InitializeModules|ManagedMain)\b') 'isolated adapter does not bypass module registration or dispatch managed entry'

$kernelHarness = Get-Content -LiteralPath (Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp') -Raw
Assert-True ($kernelHarness -match 'using\s+StartupMain\s*=\s*int32_t\s*\(GUIDEXOS_NATIVEAOT_PAL_CALL\s*\*\)\(void\)') 'kernel dispatch pointer matches the adapter C ABI and int32 status return'
Assert-True (($kernelHarness -match '(?s)if\s*\(startupResult\s*!=\s*0\)\s*\{.*?managed_main_gate=blocked adapterStatus=.*?return;\s*\}') -and
    ($kernelHarness -match '(?s)#if defined\(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS\) && !defined\(GXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS\)\s*serial::puts\("\[C188R12R4R5R1_INIT\] managed_main_gate=blocked reason=runtime-threadstore-attachment-unqualified')) 'failed initialization remains blocked; the successful diagnostic gate is released only by the R4 readiness profile'
$kernelMain = Get-Content -LiteralPath (Join-Path $root 'kernel\core\main.cpp') -Raw
Assert-True ($kernelMain.IndexOf('GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_OVERFLOW_ADDRESS') -lt
    $kernelMain.IndexOf('GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_RH_INITIALIZE_RAW_RESULT_ADDRESS') -and
    $kernelMain.Contains('#if defined(GXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT)')) 'raw-result getter follows the corrected R1 trace argument slots and remains R3-gated'
$palContract = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_contract.h') -Raw
Assert-True ($palContract -match '#define\s+GUIDEXOS_NATIVEAOT_PAL_CALL\s+__cdecl') 'kernel startup function pointer uses the x64 cdecl spelling'
$startupProbe = Get-Content -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_startup_probe.cpp') -Raw
Assert-True ($startupProbe -match 'extern\s+"C"\s+bool\s+RhInitialize\(bool\s+isDll\)') 'separate startup probe also declares the native bool(bool) contract'
Assert-True ($startupProbe -match 'return\s+RhInitialize\(false\)\s*\?\s*0\s*:\s*-1') 'separate startup probe maps native true to zero success'
$productionWrapper = Get-Content -LiteralPath (Join-Path $root 'samples\managed\HostLogProof\c102_startup.c') -Raw
Assert-True ($productionWrapper -match 'extern\s+unsigned\s+char\s+RhInitialize\(int\s+isDll\)') 'production C wrapper retains its byte-wide Boolean ABI bridge'
Assert-True ($productionWrapper -match 'if\s*\(!RhInitialize\(0\)\)\s*\{') 'production startup blocks on native false and does not share experimental adapter status'
$guestRunner = Get-Content -LiteralPath (Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1') -Raw
Assert-True ($guestRunner -match '\$isInitReturnContract\s*=\s*\[string\]\$candidateManifest\.mode\s+-ceq\s+''InitReturnContract''') 'one-guest runner recognizes the R3 candidate profile'
Assert-True ($guestRunner -match 'out\\dotnet\\c188r12r4r5r3-init-return-contract') 'R3 uses its own phase-wide one-boot guard root'
Assert-True ($guestRunner.Contains('C188R12R4R5R3_INIT\] init_result nativeBool=')) 'one-guest capture requires the separate raw Boolean marker'
Assert-True ($guestRunner -match '\$serialAnalysis\.explicitReturnMappingConsistent') 'one-guest result requires raw Boolean and adapter-status consistency'
Assert-True ($guestRunner -match '\[System\.IO\.FileMode\]::CreateNew') 'later guest runner creates its one-boot guard atomically'
Assert-True ($guestRunner -match 'INIT_RETURN_SUCCESS_GC_PROOF_SUPPRESSED') 'successful initialization is recorded with the downstream proof still suppressed'

$result = [ordered]@{
    result = 'PASS'
    hostedTestRoot = $testRoot
    compiler = $compiler
    nativeTrueMapsToAdapterStatusZero = $true
    nativeFalseMapsToAdapterStatusOne = $true
    moduleRegistrationFailureClassifiedSeparately = $true
    managedMainRequiresQualifiedHandoff = $true
    oneGuestRunnerSupportsR3WithoutGuestAdmission = $true
    boolWidthAndPrototypeCompile = $true
    linkedCandidateChecked = $false
    rawGuestFixture = $fixture
    rawGuestFixtureSha256Before = $fixtureHashBefore
    rawGuestFixtureSha256After = Get-Sha256 $fixture
    rawGuestFixtureUnmodified = $true
    missingStageMarkersRemainUnknown = $true
    qemuLaunched = $false
    qacReserved = $false
}

if (-not [string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
    $candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
    if (-not (Test-Path -LiteralPath $candidateManifestPath -PathType Leaf)) {
        throw "Candidate manifest is missing: $candidateManifestPath"
    }
    $candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json
    $adapterObject = Join-Path $candidate 'managed\artifacts\guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.obj'
    $linkedMap = Join-Path $candidate 'managed\artifacts\HostLogProof.map'
    $runtimeStartupSource = Join-Path $candidate 'runtime-pack\guidexos_nativeaot_startup_replacement.cpp'
    foreach ($path in @($adapterObject, $linkedMap, $runtimeStartupSource)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidate ABI input is missing: $path" }
    }
    Assert-True ($candidateManifest.result -eq 'PASS_BUILD_READY_FOR_INIT_RETURN_CONTRACT_GUEST') 'candidate is build-ready and unbooted'
    Assert-True (-not $candidateManifest.qemuLaunched -and -not $candidateManifest.qacReserved) 'candidate has no guest admission or boot'
    Assert-True (-not $candidateManifest.managedMainEnteredByCandidate) 'candidate keeps managed entry gated'
    Assert-True ($candidateManifest.nativeRhInitialize.adapterStatusSuccess -eq 0 -and
        $candidateManifest.nativeRhInitialize.adapterStatusFailure -eq 1) 'candidate manifest records wrapper status mapping'

    $mapText = Get-Content -LiteralPath $linkedMap -Raw
    Assert-True ($mapText -match '(?m)^\s+\S+\s+RhInitialize\s+\S+\s+f\s+Runtime\.WorkstationGC:guidexos_nativeaot_startup_replacement\.production\.obj\r?$') 'linked RhInitialize resolves to the pinned startup replacement object'
    Assert-True ($mapText -match '(?m)^\s+\S+\s+GuideXosNativeAotGcStartupMain\s+\S+\s+f\s+guidexos_nativeaot_c188r12r4r5_gc_startup_adapter\.obj\r?$') 'linked startup entry resolves to the guideXOS adapter object'
    Assert-True ($mapText.Contains('GuideXosNativeAotGcStartupGetRhInitializeRawResult')) 'linked artifact exports raw Boolean accessor'

    $startupText = Get-Content -LiteralPath $runtimeStartupSource -Raw
    Assert-True ($startupText -match 'extern\s+"C"\s+bool\s+RhInitialize\(bool\s+isDll\)') 'linked RhInitialize source has the pinned return and argument types'
    Assert-True ($startupText -match 'if\s*\(!palInitialized\)\s*return\s+false') 'linked initialization returns false on PAL failure'
    Assert-True ($startupText -match 'if\s*\(!initDllSucceeded\)\s*return\s+false') 'linked initialization returns false on InitDLL failure'
    Assert-True ($startupText -match 'return\s+true\s*;') 'linked initialization returns true after successful startup'

    $objdump = 'C:\mingw64\bin\objdump.exe'
    if (-not (Test-Path -LiteralPath $objdump -PathType Leaf)) { throw "objdump is missing: $objdump" }
    $adapterDisassembly = (& $objdump -dr -C $adapterObject 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Could not disassemble the linked adapter object.' }
    $disassemblyPath = Join-Path $testRoot 'startup-adapter-object.disassembly.txt'
    [System.IO.File]::WriteAllText($disassemblyPath, $adapterDisassembly, [System.Text.UTF8Encoding]::new($false))
    Assert-True ($adapterDisassembly -match 'GuideXosNativeAotGcStartupMain') 'adapter disassembly contains the actual linked caller'
    Assert-True ($adapterDisassembly -match 'RhInitialize') 'adapter object has a relocation to RhInitialize'
    Assert-True ($adapterDisassembly -match '(?i)movzbl\s+%al,%[a-z]+') 'adapter captures the byte-wide Boolean from the x64 AL return register'
    Assert-True ($adapterDisassembly -match '(?is)IMAGE_REL_AMD64_REL32\s+RhInitialize.*?movzbl\s+%al,%ebx.*?mov\s+%ebx,%ecx.*?xor\s+%rsi,%rcx.*?IMAGE_REL_AMD64_REL32\s+\?g_startupResult') 'compiled caller maps native Boolean 1 to status 0 and Boolean 0 to status 1'
    Assert-True ($adapterDisassembly -match '(?i)IMAGE_REL_AMD64_REL32\s+\?g_rhInitializeRawResult') 'compiled caller stores a separate raw-return observation'
    $result.linkedCandidateChecked = $true
    $result.candidateRoot = $candidate
    $result.candidateManifestSha256 = Get-Sha256 $candidateManifestPath
    $result.candidateNativeAotElfSha256 = $candidateManifest.managedElfSha256
    $result.candidateKernelSha256 = $candidateManifest.specializedKernelSha256
    $result.adapterObjectSha256 = Get-Sha256 $adapterObject
    $result.adapterDisassembly = $disassemblyPath
    $result.linkedRhInitializeReturnRegister = 'AL, the low byte of RAX'
}

$manifestPath = Join-Path $testRoot 'hosted-test-manifest.json'
[System.IO.File]::WriteAllText($manifestPath, ($result | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R3 return-contract tests: PASS ($testRoot)"
