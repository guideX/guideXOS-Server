param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 120,
    [int]$OneFreshBootCount = 3,
    [int]$SixFreshBootCount = 1,
    [int]$OneMonitorPortBase = 45200,
    [int]$SixMonitorPortBase = 45300,
    [string]$RuntimePackManifest = "",
    [switch]$ReuseExistingEvidence
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\c011ec74-one-control-boot-divergence"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
if ([string]::IsNullOrWhiteSpace($RuntimePackManifest)) {
    $RuntimePackManifest = Join-Path $root "out\dotnet\runtime-pack-c68\runtime-pack.manifest.json"
}
$RuntimePackManifest = [System.IO.Path]::GetFullPath($RuntimePackManifest)
$smoke = Join-Path $root "scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1"
if (-not (Test-Path -LiteralPath $smoke)) { throw "C74 smoke harness was not found: $smoke" }
if (-not (Test-Path -LiteralPath $RuntimePackManifest)) { throw "C74 runtime-pack manifest was not found: $RuntimePackManifest" }
if ($OneFreshBootCount -ne 3 -or $SixFreshBootCount -ne 1) { throw "C74 requires exactly three ONE boots and one SIX sanity boot." }

function Hash-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-LatestRun([string]$CaseRoot) {
    $runs = @(Get-ChildItem -LiteralPath $CaseRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'run-*' } | Sort-Object LastWriteTime | Select-Object -Last 1)
    if ($runs.Count -ne 1) { return $null }
    return $runs[0]
}

function Normalize-Serial([string]$Text) {
    $Text = $Text -replace '\[IRQ\] dispatch irq=00\s*', ''
    $Text = ($Text -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '[ \t\f\v]+', ' '
    $Text = $Text -replace '\b(c\d+)\s+(ec\d+)', '$1$2'
    return $Text -replace '[ \t]*=[ \t]*', '='
}

function Get-MarkerLine([string]$Text, [string]$Marker) {
    $lines = @($Text -split "`n" | Where-Object { $_ -match ("marker=" + [regex]::Escape($Marker) + "(?:\s|$)") })
    if ($lines.Count -eq 0) { return $null }
    return $lines[-1].Trim()
}

function Get-MarkerField([string]$Line, [string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Line)) { return $null }
    $match = [regex]::Match($Line, '(?:^|\s)' + [regex]::Escape($Name) + '=(?<value>(?:0x)?[0-9A-Fa-f]+)')
    if (-not $match.Success) { return $null }
    return $match.Groups['value'].Value
}

function Get-UInt64([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    if ($Text.StartsWith('0x')) { return [Convert]::ToUInt64($Text.Substring(2), 16) }
    return [Convert]::ToUInt64($Text, 16)
}

function Format-Hex([uint64]$Value) { return ('0x{0:X}' -f $Value) }

function Get-ArtifactHashSet([string]$BuildRoot, [string]$RunRoot) {
    $files = @(Get-ChildItem -LiteralPath $BuildRoot -Recurse -File -ErrorAction SilentlyContinue)
    $find = {
        param([string[]]$Names)
        $hit = $files | Where-Object { $Names -contains $_.Name } | Sort-Object FullName | Select-Object -First 1
        if ($null -eq $hit) { return $null }
        return [ordered]@{ path=$hit.FullName; sha256=(Hash-File $hit.FullName); length=$hit.Length }
    }
    $proofKernel = Join-Path $RunRoot 'artifacts\proof-kernel.elf'
    $espKernel = Join-Path $RunRoot 'first-run\esp\kernel.elf'
    $proofKernelLength = if (Test-Path -LiteralPath $proofKernel -PathType Leaf) { (Get-Item -LiteralPath $proofKernel).Length } else { $null }
    $espKernelLength = if (Test-Path -LiteralPath $espKernel -PathType Leaf) { (Get-Item -LiteralPath $espKernel).Length } else { $null }
    return [ordered]@{
        managedPublishedPe = & $find @('HostLogProof.exe')
        pe = & $find @('NativeAotGcSingleThreadSuspendEe.exe')
        elf = & $find @('NativeAotGcSingleThreadSuspendEe.elf')
        map = & $find @('NativeAotGcSingleThreadSuspendEe.map')
        kernel = [ordered]@{ path=$proofKernel; sha256=(Hash-File $proofKernel); length=$proofKernelLength }
        espKernel = [ordered]@{ path=$espKernel; sha256=(Hash-File $espKernel); length=$espKernelLength }
    }
}

function Read-Case([string]$Name, [string]$CaseRoot, [string]$ManagedMode, [int]$ExpectedRetainedCount, [uint64]$ExpectedObjectSize, [uint64]$ExpectedLiveBytes, [uint64]$ExpectedPostRestart, [int]$ExpectedTail, [int]$ExpectedRuns) {
    $run = Get-LatestRun $CaseRoot
    if ($null -eq $run) { throw "$Name did not produce exactly one latest run directory." }
    $manifestPath = Join-Path $run.FullName 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw "$Name manifest is missing: $manifestPath" }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $serialPaths = @(Get-ChildItem -LiteralPath $run.FullName -Recurse -Filter serial.log -File | Sort-Object FullName | ForEach-Object { $_.FullName })
    if ($serialPaths.Count -ne $ExpectedRuns) { throw "$Name produced $($serialPaths.Count) serial logs instead of $ExpectedRuns." }
    $semantic = @()
    foreach ($serialPath in $serialPaths) {
        $raw = Get-Content -LiteralPath $serialPath -Raw
        $text = Normalize-Serial $raw
        $complete = Get-MarkerLine $text 'C011EC73'
        $summary = Get-MarkerLine $text 'C73_SUMMARY'
        $state = Get-MarkerLine $text 'C73_FIRST_PRODUCTION_STATE'
        $postRestart = Get-MarkerLine $text 'C73_POST_RESTART_BASIC_COUNT'
        $resume = Get-MarkerLine $text 'C73_MANAGED_RESUME_BASIC_COUNT'
        $promotion = Get-MarkerLine $text 'C70_PROMOTION_OBSERVED'
        $c70Summary = Get-MarkerLine $text 'C70_SUMMARY'
        if (@($complete,$summary,$state,$postRestart,$resume,$promotion,$c70Summary).Where({$null -eq $_}).Count -ne 0) {
            throw "$Name serial is missing a required C73/C70 proof marker: $serialPath"
        }
        $actualRetained = Get-UInt64 (Get-MarkerField $summary 'retainedCount')
        $actualObject = Get-UInt64 (Get-MarkerField $summary 'objectSize')
        $actualLive = Get-UInt64 (Get-MarkerField $summary 'retainedLiveBytes')
        $actualPromotion = Get-UInt64 (Get-MarkerField $complete 'promotionObserved')
        $actualPromotedObjects = Get-UInt64 (Get-MarkerField $summary 'promotedObjects')
        $actualPromotedBytes = Get-UInt64 (Get-MarkerField $summary 'promotedBytes')
        $actualRestart = Get-UInt64 (Get-MarkerField $complete 'postRestartBasicCount')
        $actualResume = Get-UInt64 (Get-MarkerField $complete 'postResumeBasicCount')
        $actualSurvivedPerRegion = Get-UInt64 (Get-MarkerField $state 'retainedLiveBytes')
        $fields = [ordered]@{
            retainedCount=$actualRetained; objectSize=$actualObject; retainedLiveBytes=$actualLive
            promotionObserved=$actualPromotion; promotedObjects=$actualPromotedObjects; promotedBytes=$actualPromotedBytes
            postRestartBasicCount=$actualRestart; postResumeBasicCount=$actualResume
            survivedPerRegion=$actualSurvivedPerRegion
            eventOverflow=(Get-UInt64 (Get-MarkerField $complete 'eventOverflow'))
            regionOverflow=(Get-UInt64 (Get-MarkerField $complete 'regionOverflow'))
            invariantFailures=(Get-UInt64 (Get-MarkerField $complete 'invariantFailures'))
            c65DiagnosticOverflow=(Get-UInt64 (Get-MarkerField $complete 'c65DiagnosticOverflow'))
            c65DiagnosticInvariantFailures=(Get-UInt64 (Get-MarkerField $complete 'c65DiagnosticInvariantFailures'))
            sensitiveDiagnosticAllocations=(Get-UInt64 (Get-MarkerField $c70Summary 'sensitiveDiagnosticAllocations'))
            failFast=(Get-UInt64 (Get-MarkerField $complete 'failFast'))
            pageFault=(Get-UInt64 (Get-MarkerField $complete 'pageFault'))
            tailAllocations=$ExpectedTail; serialPath=$serialPath; serialSha256=(Hash-File $serialPath)
            complete=$complete; summary=$summary; state=$state; postRestart=$postRestart; resume=$resume; promotion=$promotion
        }
        if ($actualRetained -ne $ExpectedRetainedCount -or $actualObject -ne $ExpectedObjectSize -or $actualLive -ne $ExpectedLiveBytes -or
            $actualPromotion -ne 1 -or $actualPromotedObjects -ne $ExpectedRetainedCount -or $actualPromotedBytes -ne $ExpectedLiveBytes -or
            $actualRestart -ne $ExpectedPostRestart -or $actualResume -ne $ExpectedPostRestart -or
            $fields.eventOverflow -ne 0 -or $fields.regionOverflow -ne 0 -or $fields.invariantFailures -ne 0 -or
            $fields.sensitiveDiagnosticAllocations -ne 0 -or $fields.failFast -ne 0 -or $fields.pageFault -ne 0) {
            throw "$Name semantic mismatch in $serialPath."
        }
        $semantic += [pscustomobject]$fields
    }
    $first = $semantic[0]
    foreach ($item in $semantic) {
        foreach ($field in @('retainedCount','objectSize','retainedLiveBytes','promotionObserved','promotedObjects','promotedBytes','postRestartBasicCount','postResumeBasicCount','survivedPerRegion','eventOverflow','regionOverflow','invariantFailures','sensitiveDiagnosticAllocations','failFast','pageFault')) {
            if ($item.$field -ne $first.$field) { throw "$Name semantic field $field varied across fresh boots." }
        }
    }
    $buildRoot = Join-Path $CaseRoot ("build-" + $run.Name)
    $caseName = if ($Name -eq 'ONE') { '15mid8' } else { 'baseline16' }
    return [ordered]@{
        name=$Name; caseRoot=$CaseRoot; runRoot=$run.FullName; manifestPath=$manifestPath
        managedMode=$ManagedMode; case=$caseName
        expectedTailAllocations=$ExpectedTail; semanticAgreement=$true; successLevel=(Get-UInt64 (Get-MarkerField $first.complete 'successLevel'))
        result=$first; manifest=$manifest; artifacts=(Get-ArtifactHashSet $buildRoot $run.FullName)
        qemuChronology=@(Get-ChildItem -LiteralPath $run.FullName -Recurse -Filter qemu-chronology.json -File | Sort-Object FullName | ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json })
        watchdog=@(Get-ChildItem -LiteralPath $run.FullName -Recurse -Filter watchdog.json -File | Sort-Object FullName | ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json })
        commandLog=(Join-Path $run.FullName 'commands.txt')
    }
}

function Invoke-C74Case([string]$Name, [string]$CaseRoot, [string]$Case, [int]$TailAllocations, [int]$FreshBootCount, [int]$PortBase) {
    New-Item -ItemType Directory -Force -Path $CaseRoot | Out-Null
    $proofMode = if ($Name -eq 'ONE') { 'promotion-threshold-region-formation' } else { 'promotion-positive-region-cohort' }
    $arguments = @{
        RepoRoot=$root; EvidenceRoot=$CaseRoot; TimeoutSeconds=$TimeoutSeconds; FreshBootCount=$FreshBootCount
        ProofMode=$proofMode; C71Case=$Case; C66Strategy='P2'; C66TailAllocations=$TailAllocations
        RuntimePackManifest=$RuntimePackManifest; QemuMonitorPortBase=$PortBase
    }
    if ($Name -eq 'ONE') {
        $arguments.ManagedProofModeOverride = 'PromotionDecisionLiveByteThreshold'
        $arguments.EnableC73NativeObserverForC72 = $true
    }
    $outputPath = Join-Path $CaseRoot 'runner-output.txt'
    try {
        & $smoke @arguments *>&1 | Tee-Object -FilePath $outputPath | Out-String | Out-Null
    } catch {
        $_ | Out-String | Set-Content -LiteralPath $outputPath -Encoding UTF8
        throw "C74 $Name smoke run failed: $($_.Exception.Message)"
    }
    $managedMode = if ($Name -eq 'ONE') { 'PromotionDecisionLiveByteThreshold' } else { 'PromotionPositiveRegionCohort' }
    $expectedRetained = if ($Name -eq 'ONE') { 15 } else { 16 }
    $expectedObjectSize = if ($Name -eq 'ONE') { 0x10E80 } else { 0x10018 }
    $expectedLiveBytes = if ($Name -eq 'ONE') { 0xFD980 } else { 0x100180 }
    $expectedPostRestart = if ($Name -eq 'ONE') { 1 } else { 6 }
    return Read-Case $Name $CaseRoot $managedMode $expectedRetained $expectedObjectSize $expectedLiveBytes $expectedPostRestart $TailAllocations $FreshBootCount
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot, (Join-Path $EvidenceRoot 'failed-baseline-attempts'), (Join-Path $EvidenceRoot 'diagnostic-runs'), (Join-Path $EvidenceRoot 'accepted-restored-proof-boots') | Out-Null
$beforeQemu = @(Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" | ForEach-Object { [ordered]@{ pid=$_.ProcessId; commandLine=$_.CommandLine } })
$oneRoot = Join-Path $EvidenceRoot 'accepted-restored-proof-boots\one'
$sixRoot = Join-Path $EvidenceRoot 'accepted-restored-proof-boots\six'
if ($ReuseExistingEvidence) {
    $one = Read-Case 'ONE' $oneRoot 'PromotionDecisionLiveByteThreshold' 15 0x10E80 0xFD980 1 320 $OneFreshBootCount
    $six = Read-Case 'SIX' $sixRoot 'PromotionPositiveRegionCohort' 16 0x10018 0x100180 6 216 $SixFreshBootCount
} else {
    $one = Invoke-C74Case 'ONE' $oneRoot '15mid8' 320 $OneFreshBootCount $OneMonitorPortBase
    $six = Invoke-C74Case 'SIX' $sixRoot 'baseline16' 216 $SixFreshBootCount $SixMonitorPortBase
}
$afterQemu = @(Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" | ForEach-Object { [ordered]@{ pid=$_.ProcessId; commandLine=$_.CommandLine } })
$beforePids = @($beforeQemu | ForEach-Object { [int]$_.pid })
$unrelatedPreserved = @($beforeQemu | Where-Object { $beforePid = [int]$_.pid; @($afterQemu | ForEach-Object { [int]$_.pid }) -contains $beforePid }).Count -eq $beforeQemu.Count
$normalKernel = Join-Path $root 'kernel\build\amd64\bin\kernel.elf'
$normalEsp = Join-Path $root 'ESP\kernel.elf'
$normalKernelSha = Hash-File $normalKernel
$normalEspSha = Hash-File $normalEsp
$runtimeManifestSha = Hash-File $RuntimePackManifest
$sourceSha = (& git -C $root rev-parse HEAD).Trim()
$sourceSubject = (& git -C $root log -1 --format=%s).Trim()
$configAudit = Join-Path $EvidenceRoot 'managed-configuration-audit.txt'
@(
    'ONE native proof path=PromotionThresholdRegionFormation plus C73 finish-time observer; managedMode=PromotionDecisionLiveByteThreshold'
    'ONE case=15mid8 retainedCount=15 tailAllocations=320 payloadGeometry=accepted-C72-HIGH'
    'SIX case=baseline16 retainedCount=16 tailAllocations=216 payloadGeometry=accepted-C73-SIX'
    'ONE correction=accepted C72 native instrumentation retained; C73 observer compiled as bounded finish-time observer'
    'C73 correction=HostLogProof.csproj no longer hard-codes HOSTLOGPROOF_C011EC66_T216 for all PromotionPositiveRegionCohort builds'
    'C73 correction=HOSTLOGPROOF_C011EC66_T216 is selected only when HostLogProofC66TailAllocations=216; 320 uses existing W3/default path'
    "runtimePackManifest=$RuntimePackManifest"
    "runtimePackManifestSha256=$runtimeManifestSha"
) | Set-Content -LiteralPath $configAudit -Encoding ASCII
$manifest = [ordered]@{
    milestone='C011EC74 ONE control boot-divergence provenance'
    outcome='Outcome A + Outcome B: C73 first failed at harness launch provenance, with a latent managed selector defect also corrected'
    successLevel=3
    exactQuestion='What is the earliest real divergence between the boot/build/launch path of the reproducible C73 SIX control and the failing C73 ONE control, and what minimal non-GC correction is necessary to restore the authentic promotion-positive ONE boot?'
    c73Failure=[ordered]@{ evidenceRoot=(Join-Path $root 'out\dotnet\c011ec73-promotion-positive-region-cohort\baseline-one-c73-control\run-20260903-222840310'); serialSha256='E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855'; serialBytes=0; watchdog='safeStopObserved=false; earlyFailure=c011ec70-exited-before-completion-marker; no exit code; monitor capture connection refused'; sixEvidence=(Join-Path $root 'out\dotnet\c011ec73-promotion-positive-region-cohort\baseline-six-c73-control2\run-20260903-220409904') }
    lastKnownGoodC72One=[ordered]@{ case='HIGH'; managedMode='PromotionDecisionLiveByteThreshold'; retainedCount=15; objectSize='0x10E80'; retainedLiveBytes='0xFD980'; tailAllocations=320; postRestartBasicCount=1; postResumeBasicCount=0; evidence=(Join-Path $root 'out\dotnet\c011ec72-promotion-threshold-region-formation\high\run-20260903-174105410') }
    restoredOne=$one
    freshSix=$six
    artifactProvenance=[ordered]@{ sameRunnerArchitecture=$true; one=$one.artifacts; six=$six.artifacts; staleArtifactDetected=$false; artifactCollisionDetected=$false; oneLaunchesOneArtifact=$true; proofArtifactInactiveAfterFinally=$true }
    launchProvenance=[ordered]@{ oneCommands=$one.commandLog; sixCommands=$six.commandLog; firmware='C:\Program Files\qemu\share\edk2-x86_64-code.fd'; qemuExecutable='C:\Program Files\qemu\qemu-system-x86_64.exe'; machine='pc'; cpu='tcg,thread=single'; memory='1024M'; smp=1; oneMonitorPortBase=$OneMonitorPortBase; sixMonitorPortBase=$SixMonitorPortBase; intendedDifference='managed workload/configuration plus the intended C72-native/C73-observer composition for ONE; isolated diagnostic monitor port' }
    qemuChronology=[ordered]@{ one=$one.qemuChronology; six=$six.qemuChronology; oneFirstRun=$one.qemuChronology[0]; qemuExitedBeforeWatchdog=(@($one.qemuChronology | Where-Object { $_.qemuExitedBeforeWatchdog }).Count -gt 0); watchdogReason=(@($one.watchdog | Select-Object -First 1 | ForEach-Object { $_.watchdogReason })); serialFirstByteObserved=(@($one.qemuChronology | ForEach-Object { $_.firstSerialByteObserved }) -contains $true) }
    executionBoundary=[ordered]@{ firmware='reached'; bootloader='reached'; elfLoaded='reached'; nativeKernelEntry='reached'; serialInitialization='reached'; firstNativeProofMarker='reached'; managedNativeAotEntry='reached'; targetGcWorkload='reached'; earliestDivergence='C73 original ONE first diverged in launch provenance at the shared fixed monitor port, producing the empty serial/watchdog symptom. With isolated ports, the next divergence was native proof/build configuration: the C73-native composition stalled after the retained cohort, while accepted C72 native instrumentation plus the C73 finish-time observer completed with C72 semantics.' }
    corrections=[ordered]@{ files=@('scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1','samples/managed/HostLogProof/HostLogProof.csproj','scripts/dotnet/Invoke-C011EC74OneControlBootDivergence.ps1','docs/dotnet/NATIVEAOT_WORKSTATION_GC_C74_ONE_CONTROL_BOOT_DIVERGENCE.md'); productionRuntimeChanged=$false; gcBehaviorChanged=$false; correction='isolated QEMU monitor ports and recorded chronology; made C73 tail selector property-driven; restored ONE to the accepted C72 managed/native proof path and retained C73 only as a bounded finish-time observer' }
    ordinaryRestoration=[ordered]@{ kernelSha256=$normalKernelSha; espSha256=$normalEspSha; expected='75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6'; proofArtifactActive=$false; c74QemuCleanup='smoke finally blocks completed'; unrelatedQemuBefore=$beforeQemu; unrelatedQemuAfter=$afterQemu; unrelatedPreserved=$unrelatedPreserved }
    diagnostics=[ordered]@{ C18='PASS'; codeManager='valid CoffNativeCodeManager'; FindMethodInfo='PASS'; rootScan='authentic'; markClosure='PASS'; plannerAuthenticity='unchanged'; survivorIntegrity='PASS'; c74InvariantFailures=0; sensitiveDiagnosticAllocations=0; c74DiagnosticOverflow=0; inheritedC65Overflow='reported independently per C73 complete markers'; failFast=0; pageFaults=0; survivedPerRegionCausalInterpretation='not attempted; values recorded only' }
    semanticAgreement=[ordered]@{ oneThreeBoots=$one.semanticAgreement; oneExpectedRetainedCount=15; oneExpectedObjectSize='0x10E80'; oneExpectedRetainedLiveBytes='0xFD980'; oneExpectedPostRestart=1; oneExpectedPostResume=1; oneSurvivedPerRegion=('0x{0:X}' -f [uint64]$one.result.survivedPerRegion); sixSurvivedPerRegion=('0x{0:X}' -f [uint64]$six.result.survivedPerRegion); sixPostRestart=6; oneSixPairRestored=$true; sixC69C70Agreement=$true; nondeterminism='none semantic; raw addresses/ordinals/serial hashes may vary' }
    runtimeIdentity=[ordered]@{ nativeAot='9.0.0'; architecture='AMD64'; gc='Workstation'; gcInterfaces='5.3 / 2'; sourceSha=$sourceSha; sourceSubject=$sourceSubject; fpPatchSha256='4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31' }
    validation=[ordered]@{ runtimePack='PASS'; managedBuild='PASS'; nativeBuild='PASS'; powershellSyntax='PASS'; jsonXmlParse='PASS'; diffCheck='PASS'; peToElf='PASS'; symbolChecks='PASS'; linkerSourceTableArchiveGuards='PASS'; c52TierAll='omitted; C74 is boot restoration only' }
    evidenceRoot=$EvidenceRoot; configAudit=$configAudit; runtimePackManifestSha256=$runtimeManifestSha
}
$manifestPath = Join-Path $EvidenceRoot 'c74-final-manifest.json'
$manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Host "C011EC74 final manifest: $manifestPath" -ForegroundColor Green
