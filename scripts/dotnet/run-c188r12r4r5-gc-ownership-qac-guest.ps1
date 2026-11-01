param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [ValidateRange(60,300)][int]$BootTimeoutSeconds = 180,
    [ValidateRange(1,60)][int]$AdmissionWindowSeconds = 30,
    [ValidateSet('R1','R2','R3')][string]$ThreadStoreAttempt = 'R1'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'C188R12R4R5 QAC guest requires PowerShell Core.' }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $PSScriptRoot 'c188r12r4r5r4-qac-admission.psm1') -Force
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
if (-not (Test-Path -LiteralPath $candidateManifestPath -PathType Leaf)) {
    throw "Qualified candidate manifest is missing: $candidateManifestPath"
}
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$isInitAttribution = [string]$candidateManifest.mode -ceq 'InitAttribution'
$isInitReturnContract = [string]$candidateManifest.mode -ceq 'InitReturnContract'
$isThreadStoreReadiness = [string]$candidateManifest.mode -ceq 'ThreadStoreReadiness'
if ($ThreadStoreAttempt -in @('R2','R3') -and -not $isThreadStoreReadiness) {
    throw "ThreadStoreAttempt $ThreadStoreAttempt is valid only for the C188 R4 ThreadStore candidate."
}
$isInitDiagnostic = $isInitAttribution -or $isInitReturnContract -or $isThreadStoreReadiness
$r4RuntimePackManifest = $null
$r4PalCommonMembership = $null
if ($isThreadStoreReadiness) {
    $r4RuntimePackManifestPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.runtimePackManifest)
    if (-not (Test-Path -LiteralPath $r4RuntimePackManifestPath -PathType Leaf)) {
        throw "R4 runtime-pack manifest is missing: $r4RuntimePackManifestPath"
    }
    $r4RuntimePackManifest = Get-Content -LiteralPath $r4RuntimePackManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $r4PalCommonMembership = $r4RuntimePackManifest.archiveMembership.threadStorePalCommonReplacement
}
$phaseRoot = if ($isThreadStoreReadiness) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r4-threadstore-readiness'))
} elseif ($isInitReturnContract) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r3-init-return-contract'))
} elseif ($isInitAttribution) {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5r1-runtime-init-attribution'))
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5-gc-ownership-proof'))
}
if (-not (Test-QacCandidatePath -CandidateRoot $candidate -PhaseRoot $phaseRoot -RequireExisting)) {
    throw "CandidateRoot must be a unique candidate below $phaseRoot."
}
if ($isInitAttribution) {
    if ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_INIT_DIAGNOSTIC_QAC_PREFLIGHT' -or
        -not [bool]$candidateManifest.runtimeInitDiagnostics -or
        [bool]$candidateManifest.qemuLaunched -or [bool]$candidateManifest.qacReserved -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle -or
        $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
        throw 'R1 candidate manifest failed its one-guest initialization-diagnostics or production-isolation gate.'
    }
} elseif ($isInitReturnContract) {
    if ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_INIT_RETURN_CONTRACT_GUEST' -or
        -not [bool]$candidateManifest.runtimeInitDiagnostics -or
        -not [bool]$candidateManifest.c188R12R4R5R3InitReturnContract -or
        [bool]$candidateManifest.qemuLaunched -or [bool]$candidateManifest.qacReserved -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackC188ProofProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR1TraceProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR3ReturnProfile -or
        -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle -or
        [string]$candidateManifest.nativeRhInitialize.signature -cne 'extern "C" bool RhInitialize(bool isDll)' -or
        [int]$candidateManifest.nativeRhInitialize.successBoolean -ne 1 -or
        [int]$candidateManifest.nativeRhInitialize.failureBoolean -ne 0 -or
        [int]$candidateManifest.nativeRhInitialize.adapterStatusSuccess -ne 0 -or
        [int]$candidateManifest.nativeRhInitialize.adapterStatusFailure -ne 1 -or
        [bool]$candidateManifest.nativeRhInitialize.moduleRegistrationIncluded -or
        [bool]$candidateManifest.nativeRhInitialize.managedMainIncluded -or
        [string]$candidateManifest.runtimePackIdentity -cne 'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r3-init-return-contract-v1' -or
        $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
        throw 'R3 candidate manifest failed its one-guest return-contract, startup-gate, or production-isolation preflight.'
    }
} elseif ($isThreadStoreReadiness) {
    if ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_THREADSTORE_READINESS_GUEST' -or
        -not [bool]$candidateManifest.runtimeInitDiagnostics -or
        -not [bool]$candidateManifest.c188R12R4R5R3InitReturnContract -or
        -not [bool]$candidateManifest.c188R12R4R5R4ThreadStoreReadiness -or
        [bool]$candidateManifest.qemuLaunched -or [bool]$candidateManifest.qacReserved -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$candidateManifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        -not [bool]$candidateManifest.productionNativeAotPersistentCompositeLifecycle -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackC188ProofProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR1TraceProfile -or
        [bool]$candidateManifest.productionIsolationChecks.productionRuntimePackR3ReturnProfile -or
        $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [string]$candidateManifest.threadStoreReadinessPalCommonStockMember -notmatch '(?i)^nativeaot[\\/]+Runtime[\\/]+Full[\\/]+CMakeFiles[\\/]+Runtime\.WorkstationGC\.dir[\\/]+__+[\\/]+windows[\\/]+PalRedhawkCommon\.cpp\.obj$' -or
        [string]$r4RuntimePackManifest.c188r12r4r5r4PalCommonStockMember -cne [string]$candidateManifest.threadStoreReadinessPalCommonStockMember -or
        [string]$r4RuntimePackManifest.archiveMembership.result -cne 'PASS' -or
        [string]$r4PalCommonMembership.stockMember -cne [string]$candidateManifest.threadStoreReadinessPalCommonStockMember -or
        [int]$r4PalCommonMembership.removedCount -ne 0 -or
        [int]$r4PalCommonMembership.archiveMemberCount -ne 1 -or
        [string]$r4PalCommonMembership.replacementObject -cne [string]$candidateManifest.threadStoreReadinessPalCommonReplacementObject -or
        [string]$r4PalCommonMembership.replacementSha256 -cne [string]$candidateManifest.threadStoreReadinessPalCommonReplacementSha256) {
        throw 'R4 candidate manifest failed its one-boot ThreadStore, PAL stack-bounds, pinned-runtime, or production-isolation preflight.'
    }
} elseif ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_QAC_PREFLIGHT' -or
    $candidateManifest.mode -cne 'GcOwnershipProof' -or
    [bool]$candidateManifest.imageBackedArenaPresent -or
    [int64]$candidateManifest.managedHeapBytes -ne 0 -or
    $candidateManifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
    throw 'Candidate manifest failed the one-guest isolation, arena, or pinned-runtime gate.'
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Require-Hash([string]$Path,[string]$Expected,[string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Name is missing: $Path" }
    $actual = Get-Sha256 $Path
    if ($actual -cne ([string]$Expected).ToUpperInvariant()) { throw "$Name hash changed after qualification." }
    return $actual
}

$esp = [System.IO.Path]::GetFullPath([string]$candidateManifest.esp)
$runDirectory = if ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R2') { 'qac-guest-002' }
    elseif ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R3') { 'qac-guest-003' }
    else { 'qac-guest-001' }
$serial = Join-Path $candidate ($runDirectory + '\serial.log')
$qmpLog = Join-Path $candidate ($runDirectory + '\qmp.log')
$runRoot = Split-Path -Parent $serial
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = Join-Path $repo 'OVMF.fd'
$attemptPath = if ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R2') {
    Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R4R2.json'
} elseif ($isThreadStoreReadiness -and $ThreadStoreAttempt -eq 'R3') {
    Join-Path $phaseRoot 'guest-launch-attempt-C188R12R4R5R4R3.json'
} else { Join-Path $phaseRoot 'guest-launch-attempt.json' }
foreach ($required in @($qacCli,$qemu,$ovmf,(Join-Path $esp 'kernel.elf'),
        (Join-Path $esp 'ramdisk.img'),(Join-Path $esp 'EFI\BOOT\BOOTX64.EFI'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required guest input is missing: $required" }
}
if ((Get-Sha256 (Join-Path $esp 'kernel.elf')) -cne ([string]$candidateManifest.espKernelSha256).ToUpperInvariant() -or
    (Get-Sha256 (Join-Path $esp 'ramdisk.img')) -cne ([string]$candidateManifest.candidateRamdiskSha256).ToUpperInvariant() -or
    (Get-Sha256 (Join-Path $esp 'ramdisk.img')) -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8') {
    throw 'The isolated guest media no longer matches the qualified candidate and preserved source ramdisk.'
}
if ($isThreadStoreReadiness) {
    if ([string]$candidateManifest.sourceRamdiskSha256 -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8' -or
        [string]$candidateManifest.specializedKernelSha256 -cne [string]$candidateManifest.espKernelSha256) {
        throw 'R4 candidate source or staged-kernel identity does not match the qualified build manifest.'
    }
    [void](Require-Hash ([string]$candidateManifest.managedPe) ([string]$candidateManifest.managedPeSha256) 'R4 NativeAOT PE')
    [void](Require-Hash ([string]$candidateManifest.managedElf) ([string]$candidateManifest.managedElfSha256) 'R4 NativeAOT ELF')
    [void](Require-Hash ([string]$candidateManifest.specializedKernel) ([string]$candidateManifest.specializedKernelSha256) 'R4 isolated kernel')
    [void](Require-Hash ([string]$candidateManifest.runtimePackManifest) ([string]$candidateManifest.runtimePackManifestSha256) 'R4 runtime-pack manifest')
    [void](Require-Hash ([string]$candidateManifest.threadStoreReadinessPalCommonReplacementObject) `
        ([string]$candidateManifest.threadStoreReadinessPalCommonReplacementSha256) 'R4 stack-bounds PAL replacement')
}
if (Test-Path -LiteralPath $attemptPath) { throw "The candidate already has a QAC guest attempt; refusing any second launch: $attemptPath" }
if (Test-Path -LiteralPath $runRoot) { throw "Refusing to overwrite guest evidence: $runRoot" }
New-Item -ItemType Directory -Path $runRoot | Out-Null

$manifestPath = Join-Path $runRoot 'guest-manifest.json'
$qacBeforeStatusPath = Join-Path $runRoot 'qac-before.status.txt'
$qacBeforeListPath = Join-Path $runRoot 'qac-before.list.txt'
$qacDuringStatusPath = $null
$qacDuringListPath = $null
$qacAfterStatusPath = $null
$qacAfterListPath = $null
$qacSnapshotRecords = [System.Collections.Generic.List[object]]::new()

function Get-QacSnapshot([string]$Prefix) {
    $statusLines = & $qacCli status 2>&1
    $statusExit = $LASTEXITCODE
    $listLines = & $qacCli list 2>&1
    $listExit = $LASTEXITCODE
    $statusText = ($statusLines | Out-String)
    $listText = ($listLines | Out-String)
    $statusPath = Join-Path $runRoot "$Prefix.status.txt"
    $listPath = Join-Path $runRoot "$Prefix.list.txt"
    foreach ($path in @($statusPath,$listPath)) {
        if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite captured QAC snapshot: $path" }
    }
    [System.IO.File]::WriteAllText($statusPath,$statusText,[System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($listPath,$listText,[System.Text.UTF8Encoding]::new($false))
    $snapshot = [pscustomobject]@{
        Status=$statusText;List=$listText;StatusExit=$statusExit;ListExit=$listExit
        StatusPath=$statusPath;ListPath=$listPath;CapturedAtUtc=[DateTime]::UtcNow.ToString('o')
    }
    $qacSnapshotRecords.Add([pscustomobject]@{
        capturedAtUtc=$snapshot.CapturedAtUtc;statusExitCode=$statusExit;listExitCode=$listExit
        statusPath=$statusPath;listPath=$listPath
    })
    return $snapshot
}
function Get-QacInspection([string]$ReservationId,[string]$Prefix) {
    $inspectionPath = Join-Path $runRoot "$Prefix.inspect.json"
    if (Test-Path -LiteralPath $inspectionPath) { throw "Refusing to overwrite captured QAC reservation inspection: $inspectionPath" }
    $inspectionLines = & $qacCli inspect $ReservationId 2>&1
    $inspectionExit = $LASTEXITCODE
    $inspectionText = ($inspectionLines | Out-String)
    [System.IO.File]::WriteAllText($inspectionPath,$inspectionText,[System.Text.UTF8Encoding]::new($false))
    $inspection = $null
    if ($inspectionExit -eq 0) {
        try { $inspection = $inspectionText | ConvertFrom-Json -ErrorAction Stop } catch { $inspection = $null }
    }
    return [pscustomobject]@{
        ExitCode=$inspectionExit;Text=$inspectionText;Inspection=$inspection;Path=$inspectionPath
    }
}
function Get-WindowsQemuProcessCensus([string]$Prefix) {
    $censusPath = Join-Path $runRoot "$Prefix.windows-qemu-census.json"
    if (Test-Path -LiteralPath $censusPath) { throw "Refusing to overwrite Windows process census: $censusPath" }
    $complete = $false
    $processRows = @()
    $errorText = $null
    try {
        $allProcesses = @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)
        $processRows = @($allProcesses | Where-Object { [string]$_.Name -match '(?i)^qemu-system-.+\.exe$' } |
            ForEach-Object {
                [ordered]@{
                    processId=[int]$_.ProcessId
                    parentProcessId=[int]$_.ParentProcessId
                    name=[string]$_.Name
                    executablePath=[string]$_.ExecutablePath
                    creationDate=[string]$_.CreationDate
                    commandLine=[string]$_.CommandLine
                }
            })
        $complete = $true
    } catch { $errorText = $_.Exception.Message }
    $census = [ordered]@{
        capturedAtUtc=[DateTime]::UtcNow.ToString('o')
        complete=$complete
        processNameFilter='^qemu-system-.+\.exe$'
        processCount=$processRows.Count
        processes=@($processRows)
        error=$errorText
    }
    [System.IO.File]::WriteAllText($censusPath,($census | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{Complete=$complete;Processes=@($processRows);Path=$censusPath;Error=$errorText}
}
function Get-QacReservationInspections([object]$Snapshot,[string]$Prefix) {
    $summaryPath = Join-Path $runRoot "$Prefix.inspections.json"
    if (Test-Path -LiteralPath $summaryPath) { throw "Refusing to overwrite QAC inspection summary: $summaryPath" }
    $list = ConvertFrom-QacListSnapshot -Text ([string]$Snapshot.List) -ExitCode ([int]$Snapshot.ListExit)
    $complete = [bool]$list.Valid
    $records = [System.Collections.Generic.List[object]]::new()
    if ($list.Valid) {
        foreach ($reservation in $list.ReservationRows) {
            $capture = Get-QacInspection ([string]$reservation.Id) ("$Prefix-$($reservation.Id)")
            $inspectionId = Get-QacReservationProperty $capture.Inspection 'id'
            $valid = $capture.ExitCode -eq 0 -and $null -ne $capture.Inspection -and
                [string]$inspectionId -ceq [string]$reservation.Id
            if (-not $valid) { $complete = $false }
            $records.Add([pscustomobject]@{
                reservationId=[string]$reservation.Id
                exitCode=$capture.ExitCode
                inspectionPath=$capture.Path
                inspectionId=[string]$inspectionId
                valid=$valid
            })
        }
    }
    $summary = [ordered]@{
        capturedAtUtc=[DateTime]::UtcNow.ToString('o')
        complete=$complete
        listPath=$Snapshot.ListPath
        reservationCount=$list.ReservationCount
        inspections=@($records.ToArray())
    }
    [System.IO.File]::WriteAllText($summaryPath,($summary | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{Complete=$complete;Count=$records.Count;Records=@($records.ToArray());Path=$summaryPath}
}
function Test-ModuleRegistrationStageObserved([object]$Analysis) {
    if ($null -eq $Analysis) { return $null }
    $stageProperty = $Analysis.PSObject.Properties['stages']
    if ($null -eq $stageProperty) { return $false }
    return @($stageProperty.Value | Where-Object { $_.stageName -eq 'moduleRegistration' }).Count -gt 0
}
function Get-AnalysisBoolean([object]$Analysis,[string]$PropertyName) {
    if ($null -eq $Analysis) { return $false }
    $property = $Analysis.PSObject.Properties[$PropertyName]
    if ($null -eq $property) { return $false }
    return [bool]$property.Value
}
function Read-GuestSerial {
    if (-not (Test-Path -LiteralPath $serial -PathType Leaf)) { return '' }
    $stream = [System.IO.FileStream]::new($serial,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::UTF8,$true,4096,$true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}
function Read-QmpResponse([System.IO.StreamReader]$Reader,[string]$Command) {
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) { throw "QMP closed while waiting for $Command." }
        Add-Content -LiteralPath $qmpLog -Value "rx $line" -Encoding utf8
        try { $message = $line | ConvertFrom-Json -ErrorAction Stop } catch { continue }
        $qmpErrorField = $message.PSObject.Properties['error']
        if ($null -ne $qmpErrorField -and $null -ne $qmpErrorField.Value) {
            throw "QMP $Command error: $line"
        }
        $qmpReturnField = $message.PSObject.Properties['return']
        if ($null -ne $qmpReturnField -or $line -match '"return"\s*:') { return $line }
    }
}
function Invoke-Qmp([string]$Command) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1',$qmpPort)
        $stream = $client.GetStream(); $stream.ReadTimeout = 5000
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::UTF8,$false,1024,$true)
        $writer = [System.IO.StreamWriter]::new($stream,[System.Text.UTF8Encoding]::new($false),1024,$true)
        $writer.AutoFlush = $true
        $greeting = $reader.ReadLine()
        Add-Content -LiteralPath $qmpLog -Value "greeting $greeting" -Encoding utf8
        $greetingObject = $null
        try { $greetingObject = $greeting | ConvertFrom-Json -ErrorAction Stop } catch { }
        if ($null -eq $greetingObject -or $null -eq $greetingObject.PSObject.Properties['QMP']) {
            throw 'QMP greeting did not contain the required QMP object.'
        }
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        [void](Read-QmpResponse $reader 'qmp_capabilities')
        $request = '{"execute":"' + $Command + '"}'
        Add-Content -LiteralPath $qmpLog -Value "tx $request" -Encoding utf8
        $writer.WriteLine($request)
        return Read-QmpResponse $reader $Command
    } finally { $client.Dispose() }
}
function Wait-Qmp([int]$Seconds,[System.Diagnostics.Process]$Owner) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $client = [System.Net.Sockets.TcpClient]::new()
        try { $client.Connect('127.0.0.1',$qmpPort); return $true }
        catch { if ($Owner.HasExited) { return $false }; Start-Sleep -Milliseconds 200 }
        finally { $client.Dispose() }
    } while ((Get-Date) -lt $deadline)
    return $false
}

$qmpListener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$qmpListener.Start()
$qmpPort = ([System.Net.IPEndPoint]$qmpListener.LocalEndpoint).Port
$qmpListener.Stop()
$qemuArgs = @(
    '-accel','tcg','-machine','pc','-smp','1',
    '-drive',('if=pflash,format=raw,readonly=on,file='+$ovmf),
    '-drive',('file=fat:rw:'+$esp+',format=raw,if=ide,index=0'),
    '-m','1024M','-vga','std','-display','none',
    '-serial',('file:'+$serial),
    '-qmp',('tcp:127.0.0.1:{0},server,nowait' -f $qmpPort),
    '-boot','order=c','-no-shutdown','-rtc','base=utc,clock=host')
$qacArgs = @('run','--project','guideXOSServer','--mode','acceptance','--',$qemu) + $qemuArgs
[ordered]@{qacCli=$qacCli;arguments=$qacArgs;qemu=$qemu;qemuArguments=$qemuArgs;qmpPort=$qmpPort;
    candidateRoot=$candidate;esp=$esp;serial=$serial;timeoutSeconds=$BootTimeoutSeconds;
    candidateManifestSha256=Get-Sha256 $candidateManifestPath;
    threadStoreAttempt=$ThreadStoreAttempt;attemptGuardPath=$attemptPath;
    guestBootLimit=1;automaticRetry=$false} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'qac-run-arguments.json') -Encoding utf8

$before = Get-QacSnapshot 'qac-before'
$beforeWindowsCensus = Get-WindowsQemuProcessCensus 'qac-before'
$beforeInspectionCapture = Get-QacReservationInspections $before 'qac-before'
$preflightPassed = Test-QacClearWithCensus -Snapshot $before `
    -WindowsCensusComplete $beforeWindowsCensus.Complete -LocalQemuProcesses $beforeWindowsCensus.Processes `
    -InspectionsComplete $beforeInspectionCapture.Complete -InspectionCount $beforeInspectionCapture.Count
$qacProcess = $null
$stdoutTask = $null
$stderrTask = $null
$qmpConnected = $false
$admitted = $false
$qacReservationId = $null
$qacExitCode = $null
$qemuStatus = $null
$qemuStatusResponse = $null
$runError = $null
$serialText = ''
$guestPass = $false
$releaseVerified = $false
$during = $null
$duringInspection = $null
$duringInspectionPath = $null
$localGuestIdentity = $null
$ownedQemuLocalProcess = $null
$after = $null
$qacAfterWindowsCensusPath = $null
$qacAfterInspectionRecords = [System.Collections.Generic.List[object]]::new()
$attemptedUtc = $null

try {
    if (-not $preflightPassed) { throw 'BLOCKED_QAC_PREFLIGHT: coordinator/census is not online, empty, and internally consistent.' }

    # This CreateNew marker permanently prevents a second QAC invocation for this candidate.
    $attemptedUtc = [DateTime]::UtcNow.ToString('o')
    $attempt = [ordered]@{candidateRoot=$candidate;attemptedAtUtc=$attemptedUtc;threadStoreAttempt=$ThreadStoreAttempt;
        maxGuestBoots=1;automaticRetry=$false;
        candidateManifestSha256=Get-Sha256 $candidateManifestPath;
        qacProject='guideXOSServer';qacMode='acceptance';qemuArguments=$qemuArgs}
    $attemptJson = $attempt | ConvertTo-Json -Depth 6
    $attemptStream = [System.IO.File]::Open($attemptPath,[System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
    try {
        $attemptBytes = [System.Text.Encoding]::UTF8.GetBytes($attemptJson)
        $attemptStream.Write($attemptBytes,0,$attemptBytes.Length)
        $attemptStream.Flush($true)
    } finally { $attemptStream.Dispose() }

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $qacCli
    $psi.WorkingDirectory = $repo
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($argument in $qacArgs) { [void]$psi.ArgumentList.Add([string]$argument) }
    $qacProcess = [System.Diagnostics.Process]::Start($psi)
    if (-not $qacProcess) { throw 'Unable to start the QAC-owned QEMU command.' }
    $stdoutTask = $qacProcess.StandardOutput.ReadToEndAsync()
    $stderrTask = $qacProcess.StandardError.ReadToEndAsync()

    $admissionDeadline = (Get-Date).AddSeconds($AdmissionWindowSeconds)
    $admissionSample = 0
    $ownerUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    do {
        if ($qacProcess.HasExited) { throw "QAC exited before exclusive admission ($($qacProcess.ExitCode))." }
        $admissionSample++
        $prefix = 'qac-during-{0:D3}' -f $admissionSample
        $snapshot = Get-QacSnapshot $prefix
        if ($null -eq $qacDuringStatusPath) {
            $qacDuringStatusPath = $snapshot.StatusPath
            $qacDuringListPath = $snapshot.ListPath
        }
        $listFields = ConvertFrom-QacListSnapshot -Text $snapshot.List -ExitCode $snapshot.ListExit
        $ownerRows = @($listFields.ReservationRows | Where-Object {
            $owner = [regex]::Match($_.Owner, '^(?<user>.+?) \(PID (?<pid>\d+)\)$')
            $owner.Success -and [int]$owner.Groups['pid'].Value -eq $qacProcess.Id -and
                [string]::Equals($owner.Groups['user'].Value,$ownerUser,[System.StringComparison]::OrdinalIgnoreCase)
        })
        if ($ownerRows.Count -eq 1) {
            $qacReservationId = [string]$ownerRows[0].Id
            if ($ownerRows[0].State -ceq 'Running' -and $null -ne $ownerRows[0].ChildProcessId) {
                $inspectionCapture = Get-QacInspection $qacReservationId $prefix
                if ($inspectionCapture.ExitCode -eq 0 -and $null -ne $inspectionCapture.Inspection) {
                    $ownerStartUtc = [DateTimeOffset]$qacProcess.StartTime.ToUniversalTime()
                    $qacOwnedSnapshot = Test-QacRunningOwned -Snapshot $snapshot -ReservationId $qacReservationId `
                        -OwnerPid $qacProcess.Id -OwnerUser $ownerUser -QemuPath $qemu `
                        -Inspection $inspectionCapture.Inspection -ExpectedOwnerStartTimeUtc $ownerStartUtc
                    if ($qacOwnedSnapshot) {
                        $during = $snapshot
                        $qacDuringStatusPath = $snapshot.StatusPath
                        $qacDuringListPath = $snapshot.ListPath
                        $duringInspection = $inspectionCapture.Inspection
                        $duringInspectionPath = $inspectionCapture.Path
                        $localProcess = $null
                        try {
                            $localProcess = Get-CimInstance -ClassName Win32_Process `
                                -Filter ('ProcessId = {0}' -f [int]$inspectionCapture.Inspection.processId) -ErrorAction Stop
                        } catch { $localGuestIdentity = [ordered]@{verificationError=$_.Exception.Message} }
                        if ($null -ne $localProcess -and (Test-QacLocalQemuChildIdentity -LocalProcess $localProcess `
                                -Inspection $inspectionCapture.Inspection -OwnerPid $qacProcess.Id `
                                -QemuPath $qemu -QmpPort $qmpPort)) {
                            $admitted = $true
                            $during = $snapshot
                            $duringInspection = $inspectionCapture.Inspection
                            $duringInspectionPath = $inspectionCapture.Path
                            $ownedQemuLocalProcess = $localProcess
                            $localGuestIdentity = [ordered]@{
                                processId=[int]$localProcess.ProcessId
                                parentProcessId=[int]$localProcess.ParentProcessId
                                executablePath=[string]$localProcess.ExecutablePath
                                creationDate=[string]$localProcess.CreationDate
                                commandLine=[string]$localProcess.CommandLine
                                qmpPort=$qmpPort
                                qacInspectionPath=$inspectionCapture.Path
                            }
                            [System.IO.File]::WriteAllText((Join-Path $runRoot 'qemu-local-child-identity.json'),
                                ($localGuestIdentity | ConvertTo-Json -Depth 5),[System.Text.UTF8Encoding]::new($false))
                            break
                        } elseif ($null -ne $localProcess) {
                            $localGuestIdentity = [ordered]@{
                                verificationError='Local PID, parent, executable, creation time, or QMP command line did not match QAC inspection.'
                                processId=[int]$localProcess.ProcessId
                                parentProcessId=[int]$localProcess.ParentProcessId
                                executablePath=[string]$localProcess.ExecutablePath
                                creationDate=[string]$localProcess.CreationDate
                                qmpPort=$qmpPort
                            }
                        }
                    }
                } else {
                    $duringInspectionPath = $inspectionCapture.Path
                }
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $admissionDeadline)
    if (-not $admitted) { throw 'QAC did not provide a complete, stable, owner-verified exclusive process census inside the bounded admission window.' }

    if (-not (Wait-Qmp 30 $qacProcess)) { throw 'QMP did not become reachable within 30 seconds of QAC admission.' }
    $qmpConnected = $true
    $qemuStatusResponse = Invoke-Qmp 'query-status'
    $qemuStatus = ConvertFrom-QmpQueryStatusResponse -Text $qemuStatusResponse
    if (-not $qemuStatus.Valid) {
        throw 'QMP query-status response was missing the required status or Boolean running fields.'
    }
    if (-not $qemuStatus.running) { throw 'QEMU query-status did not report a running guest process.' }
    $deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)
    do {
        $serialText = Read-GuestSerial
        if ($isInitAttribution) {
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*rhResult=(?:0x)?[0-9A-F]{8}\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
        } elseif ($isInitReturnContract) {
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*adapterStatus=(?:0x)?[0-9A-F]{8}\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R3_INIT\] init_result nativeBool=(?:0x)?[0-9A-F]{8} adapterStatus=(?:0x)?[0-9A-F]{8} nativeOutcome=(?:success|failure|unknown) adapterOutcome=(?:success|failure)\s*$' -and
                $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
        } elseif ($isThreadStoreReadiness) {
            $r4FailureBoundary = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:managed_entry_blocked_init_failure|transition_attach_incomplete)\s+' -or
                $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=pal_stack_current\s+raw0=[0-9A-Fa-f]{16}\s+raw1=(?!0{16}\s)[0-9A-Fa-f]{16}\s*$'
            $r4GcTerminal = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(?:PASS|FAIL) callbacks=\S+ result1=\S+ result2=\S+ result3=\S+\s*$'
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=rhinitialize_return\s+' -and
                ($r4FailureBoundary -or $r4GcTerminal)
        } else {
            $guestPass = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=PASS callbacks=(?:0x)?00000003 result1=(?:0x)?00000000 result2=(?:0x)?00000000 result3=(?:0x)?00000000\s*$'
        }
        if ($guestPass) { break }
        if ($qacProcess.HasExited) { break }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    $serialText = Read-GuestSerial
    if ($isInitAttribution) {
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*rhResult=(?:0x)?[0-9A-F]{8}\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
    } elseif ($isInitReturnContract) {
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] trace_end .*adapterStatus=(?:0x)?[0-9A-F]{8}\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R3_INIT\] init_result nativeBool=(?:0x)?[0-9A-F]{8} adapterStatus=(?:0x)?[0-9A-F]{8} nativeOutcome=(?:success|failure|unknown) adapterOutcome=(?:success|failure)\s*$' -and
            $serialText -match '(?m)^\[C188R12R4R5R1_INIT\] managed_main_gate=blocked\b'
    } elseif ($isThreadStoreReadiness) {
        $r4FailureBoundary = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=(?:managed_entry_blocked_init_failure|transition_attach_incomplete)\s+' -or
            $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=pal_stack_current\s+raw0=[0-9A-Fa-f]{16}\s+raw1=(?!0{16}\s)[0-9A-Fa-f]{16}\s*$'
        $r4GcTerminal = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(?:PASS|FAIL) callbacks=\S+ result1=\S+ result2=\S+ result3=\S+\s*$'
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5R4_THREAD\].*event=rhinitialize_return\s+' -and
            ($r4FailureBoundary -or $r4GcTerminal)
    } else {
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=PASS callbacks=(?:0x)?00000003 result1=(?:0x)?00000000 result2=(?:0x)?00000000 result3=(?:0x)?00000000\s*$'
    }
    try {
        $qemuStatusResponse = Invoke-Qmp 'query-status'
        $qemuStatus = ConvertFrom-QmpQueryStatusResponse -Text $qemuStatusResponse
        if (-not $qemuStatus.Valid) {
            throw 'QMP query-status response was missing the required status or Boolean running fields.'
        }
    } catch { $qemuStatus = [pscustomobject]@{Valid=$false;Status=$null;Running=$null;Error=$_.Exception.Message} }
    if (-not $qacProcess.HasExited) {
        $currentLocalProcess = $null
        try {
            $currentLocalProcess = Get-CimInstance -ClassName Win32_Process `
                -Filter ('ProcessId = {0}' -f [int]$duringInspection.processId) -ErrorAction Stop
        } catch { }
        if ($null -eq $currentLocalProcess -or -not (Test-QacLocalQemuChildIdentity `
                -LocalProcess $currentLocalProcess -Inspection $duringInspection -OwnerPid $qacProcess.Id `
                -QemuPath $qemu -QmpPort $qmpPort)) {
            throw 'Refusing QMP shutdown because the local QEMU child identity is no longer verified.'
        }
        [void](Invoke-Qmp 'quit')
    }
    if (-not $qacProcess.WaitForExit(90000)) { throw 'QAC did not exit after the owned QMP quit request.' }
    $qacExitCode = $qacProcess.ExitCode
} catch {
    $runError = $_.Exception.Message
    if ($null -ne $qacProcess -and -not $qacProcess.HasExited) {
        if ($null -ne $duringInspection) {
            try {
                $ownedQemuLocalProcess = Get-CimInstance -ClassName Win32_Process `
                    -Filter ('ProcessId = {0}' -f [int]$duringInspection.processId) -ErrorAction Stop
            } catch { $ownedQemuLocalProcess = $null }
        }
        if ($null -ne $duringInspection -and $null -ne $ownedQemuLocalProcess -and
            (Test-QacLocalQemuChildIdentity -LocalProcess $ownedQemuLocalProcess -Inspection $duringInspection `
                -OwnerPid $qacProcess.Id -QemuPath $qemu -QmpPort $qmpPort)) {
            if (-not $qmpConnected -and (Wait-Qmp 10 $qacProcess)) { $qmpConnected = $true }
            if ($qmpConnected -and -not $qacProcess.HasExited) {
                try { [void](Invoke-Qmp 'query-status') } catch { }
                try { [void](Invoke-Qmp 'quit') } catch { }
                [void]$qacProcess.WaitForExit(90000)
            }
        } else {
            $latestListLines = & $qacCli list 2>&1
            $latestListExit = $LASTEXITCODE
            $latestList = ($latestListLines | Out-String)
            $cancelAllowed = $false
            $latestFields = ConvertFrom-QacListSnapshot -Text $latestList -ExitCode $latestListExit
            $ownerRows = @($latestFields.ReservationRows | Where-Object {
                $owner = [regex]::Match($_.Owner, '^(?<user>.+?) \(PID (?<pid>\d+)\)$')
                $owner.Success -and [int]$owner.Groups['pid'].Value -eq $qacProcess.Id -and
                    [string]::Equals($owner.Groups['user'].Value,[System.Security.Principal.WindowsIdentity]::GetCurrent().Name,
                        [System.StringComparison]::OrdinalIgnoreCase)
            })
            if ($ownerRows.Count -eq 1 -and $ownerRows[0].State -in @('Waiting','Blocked')) {
                $qacReservationId = [string]$ownerRows[0].Id
                $cancelAllowed = $true
            }
            if ($cancelAllowed) {
                $cancelOutput = (& $qacCli cancel $qacReservationId 2>&1 | Out-String)
                $cancelExit = $LASTEXITCODE
                $cancelLog = Join-Path $runRoot 'qac-cancel.log'
                if (Test-Path -LiteralPath $cancelLog) { throw "Refusing to overwrite QAC cancel evidence: $cancelLog" }
                [System.IO.File]::WriteAllText($cancelLog,$cancelOutput,[System.Text.UTF8Encoding]::new($false))
                if ($cancelExit -eq 0) { [void]$qacProcess.WaitForExit(90000) }
            }
        }
    }
} finally {
    if ($null -ne $qacProcess) {
        if ($qacProcess.HasExited) { $qacExitCode = $qacProcess.ExitCode }
        if ($null -ne $stdoutTask -and $stdoutTask.IsCompleted) {
            Set-Content -LiteralPath (Join-Path $runRoot 'qac.stdout.log') -Value $stdoutTask.Result -Encoding utf8
        }
        if ($null -ne $stderrTask -and $stderrTask.IsCompleted) {
            Set-Content -LiteralPath (Join-Path $runRoot 'qac.stderr.log') -Value $stderrTask.Result -Encoding utf8
        }
    }
    $afterDeadline = (Get-Date).AddSeconds(30)
    do {
        $afterSample = if ($null -eq $after) { 1 } else { $afterSample + 1 }
        $after = Get-QacSnapshot ('qac-after-{0:D3}' -f $afterSample)
        $afterWindowsCensus = Get-WindowsQemuProcessCensus ('qac-after-{0:D3}' -f $afterSample)
        $qacAfterWindowsCensusPath = $afterWindowsCensus.Path
        $afterInspectionCapture = Get-QacReservationInspections $after ('qac-after-{0:D3}' -f $afterSample)
        foreach ($record in $afterInspectionCapture.Records) { $qacAfterInspectionRecords.Add($record) }
        $qacAfterStatusPath = $after.StatusPath
        $qacAfterListPath = $after.ListPath
        if (Test-QacClearWithCensus -Snapshot $after `
                -WindowsCensusComplete $afterWindowsCensus.Complete -LocalQemuProcesses $afterWindowsCensus.Processes `
                -InspectionsComplete $afterInspectionCapture.Complete -InspectionCount $afterInspectionCapture.Count) {
            $releaseVerified = $true; break
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $afterDeadline)
}

$serialText = Read-GuestSerial
$serialAnalysisPath = Join-Path $runRoot 'serial-analysis.json'
$serialAnalyzer = if ($isThreadStoreReadiness) {
    Join-Path $PSScriptRoot 'analyze-c188r12r4r5r4-threadstore-readiness.ps1'
} else { Join-Path $PSScriptRoot 'analyze-c188r12r4r5r1-init-serial.ps1' }
$serialSha256BeforeAnalysis = if (Test-Path -LiteralPath $serial) { Get-Sha256 $serial } else { $null }
$serialAnalysis = $null
$serialAnalysisError = $null
$serialAnalysisExitCode = $null
if (Test-Path -LiteralPath $serial -PathType Leaf) {
    try {
        $analysisArgs = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$serialAnalyzer,
            '-SerialPath',$serial,'-OutputPath',$serialAnalysisPath)
        if (-not $isThreadStoreReadiness -and ($runError -match 'timeout|did not exit|not exit' -or
            ($null -ne $qacProcess -and -not $qacProcess.HasExited))) { $analysisArgs += '-GuestTimedOut' }
        & pwsh @analysisArgs | Out-Null
        $serialAnalysisExitCode = $LASTEXITCODE
        if ($serialAnalysisExitCode -eq 0 -and (Test-Path -LiteralPath $serialAnalysisPath -PathType Leaf)) {
            $serialAnalysis = Get-Content -LiteralPath $serialAnalysisPath -Raw | ConvertFrom-Json
        } else {
            $serialAnalysisError = "Analyzer exited $serialAnalysisExitCode."
        }
    } catch { $serialAnalysisError = $_.Exception.Message }
} else { $serialAnalysisError = 'Raw serial file is missing.' }
$serialSha256AfterAnalysis = if (Test-Path -LiteralPath $serial) { Get-Sha256 $serial } else { $null }
$rawSerialPreservedByAnalysis = $null -ne $serialSha256BeforeAnalysis -and
    $serialSha256BeforeAnalysis -ceq $serialSha256AfterAnalysis
$initCaptureComplete = $null -ne $serialAnalysis -and -not $isThreadStoreReadiness -and [bool]$serialAnalysis.initTraceComplete
if ($isInitAttribution) {
    $guestPass = $initCaptureComplete -and $serialAnalysis.rhInitializeResult -in @('success','failure') -and
        @($serialAnalysis.managedMainGateLines | Where-Object { $_ -match 'managed_main_gate=blocked' }).Count -gt 0
} elseif ($isInitReturnContract) {
    $guestPass = $initCaptureComplete -and
        $serialAnalysis.nativeRhInitializeBoolean -in @('00000000','00000001') -and
        [bool]$serialAnalysis.explicitReturnMappingConsistent -and
        [bool]$serialAnalysis.explicitReturnMarkerConsistent -and
        [bool]$serialAnalysis.returnStatusMarkerConsistent -and
        @($serialAnalysis.managedMainGateLines | Where-Object { $_ -match 'managed_main_gate=blocked' }).Count -gt 0
} elseif ($isThreadStoreReadiness) {
    $r4GcResult = [regex]::Match($serialText,
        '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=(PASS|FAIL) callbacks=(\S+) result1=(\S+) result2=(\S+) result3=(\S+)\s*$')
    $threadStoreOutcome = if ($null -ne $serialAnalysis) { [string]$serialAnalysis.outcome } else { '' }
    $threadStoreFailureOutcome = $threadStoreOutcome -in @(
        'INIT_FAILURE_BLOCKED','CURRENT_THREAD_TLS_INCOMPLETE','THREADSTORE_OBSERVATION_INCOMPLETE',
        'THREADSTORE_STATE_INVALID','MODULE_REGISTRATION_INCOMPLETE','MODULE_INITIALIZATION_INCOMPLETE',
        'ATTACHMENT_INCOMPLETE','PAL_ATTACHMENT_INCOMPLETE','PAL_THREAD_CONSTRUCTION_INCOMPLETE',
        'PAL_STACK_QUERY_FAILED','PAL_STACK_BOUNDS_INVALID','MANAGED_ENTRY_INCOMPLETE')
    $threadStoreHandoffAndProofCaptured = $null -ne $serialAnalysis -and
        $threadStoreOutcome -eq 'THREADSTORE_HANDOFF_TRACE_COMPLETE' -and $r4GcResult.Success
    $guestPass = $null -ne $serialAnalysis -and
        [bool]$serialAnalysis.rhInitializeAdapterMappingConsistent -and
        ($threadStoreFailureOutcome -or $threadStoreHandoffAndProofCaptured)
    $initCaptureComplete = $guestPass
}
$sourceRamdiskSha256 = Get-Sha256 (Join-Path $repo 'ESP\ramdisk.img')
$sourceRamdiskPreserved = $sourceRamdiskSha256 -ceq 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8'
$markerLines = @($serialText -split "`r?`n" | Where-Object { $_ -match '^marker=C188R12R4R5-GC-OWNERSHIP ' })
$markerPhases = @($markerLines | ForEach-Object { if ($_ -match '\bphase=([^ ]+)') { $Matches[1] } })
$startupFailure = $serialText -match '(?m)^\[nativeaot-gc-first-allocation\] RhInitialize return=(?:0x)?00000001\b' -or
    $serialText -match '(?m)^\[nativeaot-gc-first-allocation\] ALL_FAIL\s*$'
$initReturnFailure = $isInitReturnContract -and
    $null -ne $serialAnalysis -and $serialAnalysis.nativeRhInitializeBoolean -eq '00000000' -and
    $serialAnalysis.startupAdapterOutcome -eq 'failure'
$outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
    elseif (-not $preflightPassed) { 'EXPERIMENT_BLOCKED_QAC_CENSUS' }
    elseif ($guestPass -and $releaseVerified -and $qacExitCode -eq 0) { 'GC_GRAPH_GUEST_PROVEN' }
    elseif ($startupFailure) { 'EXPERIMENT_BLOCKED' }
    elseif ($admitted) { 'GC_ROOT_OR_COLLECTION_DEFECT_OR_GUEST_INCOMPLETE' }
    else { 'EXPERIMENT_READY_BUT_GUEST_NOT_ADMITTED' }
if ($isInitAttribution) {
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'INIT_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'INIT_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'INIT_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $guestPass) { 'INIT_TRACE_INCOMPLETE' }
        elseif ($serialAnalysis.rhInitializeResult -eq 'failure') { 'INIT_FAILURE_CAPTURED' }
        elseif ($serialAnalysis.rhInitializeResult -eq 'success') { 'INIT_RUNTIME_READY_GC_PROOF_SUPPRESSED' }
        else { 'INIT_CAPTURE_INCOMPLETE' }
} elseif ($isInitReturnContract) {
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'INIT_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'INIT_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'INIT_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $guestPass) { 'INIT_TRACE_OR_RETURN_CONTRACT_INCOMPLETE' }
        elseif ($initReturnFailure) { 'INIT_FAILURE_CAPTURED' }
        elseif ($serialAnalysis.nativeRhInitializeBoolean -eq '00000001') { 'INIT_RETURN_SUCCESS_GC_PROOF_SUPPRESSED' }
        else { 'INIT_CAPTURE_INCOMPLETE' }
} elseif ($isThreadStoreReadiness) {
    $outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
        elseif (-not $preflightPassed) { 'THREADSTORE_BLOCKED_QAC_CENSUS' }
        elseif (-not $admitted) { 'THREADSTORE_CANDIDATE_NOT_ADMITTED' }
        elseif (-not $releaseVerified) { 'THREADSTORE_CAPTURED_BUT_QAC_RELEASE_UNVERIFIED' }
        elseif (-not $guestPass) { 'THREADSTORE_TRACE_INCOMPLETE' }
        elseif ($serialAnalysis.threadStoreHandoffReady) { 'THREADSTORE_HANDOFF_PROVEN' }
        else { 'THREADSTORE_FAILURE_BOUNDARY_CAPTURED' }
    if ($guestPass -and $serialAnalysis.threadStoreHandoffReady) {
        $outcome = if ($r4GcResult.Groups[1].Value -eq 'PASS') {
            'THREADSTORE_HANDOFF_AND_GC_PROOF_CAPTURED'
        } else { 'THREADSTORE_HANDOFF_CAPTURED_GC_PROOF_FAILED' }
    }
}
$manifest = [ordered]@{
    result=$outcome;runRoot=$runRoot;candidateRoot=$candidate;candidateManifest=$candidateManifestPath
    candidateManifestSha256=Get-Sha256 $candidateManifestPath
    candidatePeSha256=[string]$candidateManifest.managedPeSha256
    candidateElfSha256=[string]$candidateManifest.managedElfSha256
    candidateKernelSha256=[string]$candidateManifest.specializedKernelSha256
    candidateRamdiskSha256=[string]$candidateManifest.candidateRamdiskSha256
    sourceRamdiskSha256=$sourceRamdiskSha256;sourceRamdiskPreserved=$sourceRamdiskPreserved
    qacPreflightPassed=$preflightPassed;qacAdmissionGranted=$admitted;qacReservationId=$qacReservationId
    qacBeforeWindowsProcessCensus=$beforeWindowsCensus.Path
    qacBeforeInspectionSummary=$beforeInspectionCapture.Path
    qacAfterWindowsProcessCensus=$qacAfterWindowsCensusPath
    qacAfterInspectionRecords=@($qacAfterInspectionRecords.ToArray())
    qacOwnerPid=if ($null -ne $qacProcess) { $qacProcess.Id } else { $null }
    qacExitCode=$qacExitCode;qacReleaseVerified=$releaseVerified;qacGuestBootLimit=1;automaticRetry=$false
    threadStoreAttempt=$ThreadStoreAttempt;attemptGuardPath=$attemptPath
    qemuStatus=$qemuStatus;qemuQueryStatusResponse=$qemuStatusResponse;qmpPort=$qmpPort;qemuArguments=$qemuArgs;guestPass=$guestPass
    qacOwnerUser=if ($null -ne $qacProcess) { [System.Security.Principal.WindowsIdentity]::GetCurrent().Name } else { $null }
    qacReservationInspection=$duringInspectionPath;qacSnapshotRecords=@($qacSnapshotRecords)
    localQemuChildIdentity=$localGuestIdentity
    initAttributionProfile=$isInitAttribution;initCaptureComplete=$initCaptureComplete
    initReturnContractProfile=$isInitReturnContract
    threadStoreReadinessProfile=$isThreadStoreReadiness
    threadStoreReadinessAnalysis=if ($isThreadStoreReadiness) { $serialAnalysisPath } else { $null }
    threadStoreReadinessOutcome=if ($isThreadStoreReadiness -and $null -ne $serialAnalysis) { $serialAnalysis.outcome } else { $null }
    threadStoreHandoffReady=if ($isThreadStoreReadiness -and $null -ne $serialAnalysis) { [bool]$serialAnalysis.threadStoreHandoffReady } else { $false }
    gcOwnershipProofOutcome=if ($isThreadStoreReadiness -and $r4GcResult.Success) { $r4GcResult.Groups[1].Value } else { $null }
    gcOwnershipProofCallbacks=if ($isThreadStoreReadiness -and $r4GcResult.Success) { $r4GcResult.Groups[2].Value } else { $null }
    nativeRhInitializeBoolean=if ($isInitReturnContract -and $null -ne $serialAnalysis) { $serialAnalysis.nativeRhInitializeBoolean } else { $null }
    nativeRhInitializeOutcome=if ($isInitReturnContract -and $null -ne $serialAnalysis) { $serialAnalysis.nativeRhInitializeOutcome } else { $null }
    startupAdapterStatusCode=if ($isInitReturnContract -and $null -ne $serialAnalysis) { $serialAnalysis.startupAdapterStatusCode } else { $null }
    startupAdapterOutcome=if ($isInitReturnContract -and $null -ne $serialAnalysis) { $serialAnalysis.startupAdapterOutcome } else { $null }
    moduleRegistrationAttemptedByCandidate=$isInitReturnContract -and -not [bool]$candidateManifest.nativeRhInitialize.moduleRegistrationIncluded
    moduleRegistrationStageObserved=Test-ModuleRegistrationStageObserved $serialAnalysis
    managedMainObserved=if ($isThreadStoreReadiness) {
        Get-AnalysisBoolean $serialAnalysis 'managedEntryReached'
    } else { Get-AnalysisBoolean $serialAnalysis 'managedMainDispatched' }
    managedMainGateReason=if ($isInitReturnContract) { [string]$candidateManifest.managedMainGateReason } else { $null }
    serialAnalysis=$serialAnalysisPath;serialAnalysisExitCode=$serialAnalysisExitCode
    serialAnalysisError=$serialAnalysisError;serialAnalysisComplete=if ($null -ne $serialAnalysis) { [bool]$serialAnalysis.initTraceComplete } else { $false }
    serialSha256BeforeAnalysis=$serialSha256BeforeAnalysis;serialSha256AfterAnalysis=$serialSha256AfterAnalysis
    rawSerialPreservedByAnalysis=$rawSerialPreservedByAnalysis
    managedMarkerCount=$markerLines.Count;managedMarkerPhases=$markerPhases;startupFailure=$startupFailure
    serialLog=$serial;serialLogSha256=if (Test-Path -LiteralPath $serial) { Get-Sha256 $serial } else { $null }
    qmpLog=$qmpLog;qmpLogSha256=if (Test-Path -LiteralPath $qmpLog) { Get-Sha256 $qmpLog } else { $null }
    qacBeforeStatus=$qacBeforeStatusPath;qacBeforeList=$qacBeforeListPath
    qacDuringStatus=$qacDuringStatusPath;qacDuringList=$qacDuringListPath
    qacAfterStatus=$qacAfterStatusPath;qacAfterList=$qacAfterListPath
    qacArguments=(Join-Path $runRoot 'qac-run-arguments.json');guestAttempt=$attemptPath;error=$runError
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8
if ($isInitAttribution) {
    Write-Output "C188R12R4R5R1_INIT_ATTRIBUTION=$outcome ADMITTED=$admitted CAPTURE_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R1_INIT_ATTRIBUTION_MANIFEST=$manifestPath"
} elseif ($isInitReturnContract) {
    Write-Output "C188R12R4R5R3_INIT_RETURN_CONTRACT=$outcome ADMITTED=$admitted CAPTURE_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5R3_INIT_RETURN_CONTRACT_MANIFEST=$manifestPath"
} elseif ($isThreadStoreReadiness) {
    $phaseLabel = if ($ThreadStoreAttempt -eq 'R2') { 'C188R12R4R5R4R2' }
        elseif ($ThreadStoreAttempt -eq 'R3') { 'C188R12R4R5R4R3' }
        else { 'C188R12R4R5R4' }
    Write-Output "${phaseLabel}_THREADSTORE_READINESS=$outcome ADMITTED=$admitted CAPTURE_COMPLETE=$initCaptureComplete QAC_RELEASE=$releaseVerified"
    Write-Output "${phaseLabel}_THREADSTORE_READINESS_MANIFEST=$manifestPath"
} else {
    Write-Output "C188R12R4R5_GC_OWNERSHIP=$outcome ADMITTED=$admitted PASS=$guestPass QAC_RELEASE=$releaseVerified"
    Write-Output "C188R12R4R5_GC_OWNERSHIP_MANIFEST=$manifestPath"
}
if (-not $releaseVerified -and $admitted) { exit 4 }
if (-not $sourceRamdiskPreserved) { exit 6 }
if (-not $preflightPassed) { exit 3 }
if ($isInitDiagnostic) {
    if (-not $guestPass) { exit 2 }
    if (-not $rawSerialPreservedByAnalysis) { exit 7 }
    if ($qacExitCode -ne 0) { exit 5 }
    exit 0
}
if (-not $guestPass) { exit 2 }
if (-not $releaseVerified -or $qacExitCode -ne 0) { exit 5 }
