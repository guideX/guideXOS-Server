param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($ArtifactRoot)) { $ArtifactRoot = Join-Path $root 'out\dotnet' }
$artifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
Import-Module (Join-Path $PSScriptRoot 'c188r12r4r5r4-qac-admission.psm1') -Force
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$testRoot = Join-Path $artifactRoot "c188r12r4r5r4-threadstore-readiness\hosted-qac-admission-$stamp"
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to overwrite hosted QAC evidence: $testRoot" }
New-Item -ItemType Directory -Path $testRoot | Out-Null

$script:caseCount = 0
function Assert-Qac([string]$Name,[bool]$Condition) {
    if (-not $Condition) { throw "C188R12R4R5R4 QAC admission test failed: $Name" }
    $script:caseCount++
}

$candidateRoot = Join-Path $root 'out\dotnet\c188r12r4r5r4-threadstore-readiness\candidate-20261031T174601065Z'
$phaseRoot = Join-Path $root 'out\dotnet\c188r12r4r5r4-threadstore-readiness'
Assert-Qac 'the existing candidate is contained by its phase root and is not a reparse point' (
    Test-QacCandidatePath -CandidateRoot $candidateRoot -PhaseRoot $phaseRoot -RequireExisting)
Assert-Qac 'the phase root itself is not accepted as a candidate' (
    -not (Test-QacCandidatePath -CandidateRoot $phaseRoot -PhaseRoot $phaseRoot))
Assert-Qac 'a sibling path sharing the phase-root prefix is rejected' (
    -not (Test-QacCandidatePath -CandidateRoot ($phaseRoot + '-outside\candidate') -PhaseRoot $phaseRoot))
Assert-Qac 'a parent traversal outside the phase root is rejected' (
    -not (Test-QacCandidatePath -CandidateRoot (Join-Path $phaseRoot '..\escape') -PhaseRoot $phaseRoot))

$clearStatus = @'
Coordinator: Online
QEMU processes: 0 (0 foreign)
Managed sessions: 0; queued: 0; blocked: 0
'@
$clearList = @'
Coordinator: Online (guideXOS QAC 2.0.0)
Reservations: 0
Managed sessions: 0
'@
$emptySnapshot = [pscustomobject]@{Status=$clearStatus;List=$clearList;StatusExit=0;ListExit=0}
Assert-Qac 'complete empty QAC lane is clear' (Test-QacClear $emptySnapshot)
Assert-Qac 'clear-lane preflight requires complete empty Windows QEMU census and matching inspection count' (
    Test-QacClearWithCensus -Snapshot $emptySnapshot -WindowsCensusComplete $true `
        -LocalQemuProcesses @() -InspectionsComplete $true -InspectionCount 0)
Assert-Qac 'local Windows QEMU process blocks clear-lane preflight' (-not (
    Test-QacClearWithCensus -Snapshot $emptySnapshot -WindowsCensusComplete $true `
        -LocalQemuProcesses @([pscustomobject]@{ProcessId=901;Name='qemu-system-x86_64.exe'}) `
        -InspectionsComplete $true -InspectionCount 0))
Assert-Qac 'incomplete Windows QEMU census fails closed' (-not (
    Test-QacClearWithCensus -Snapshot $emptySnapshot -WindowsCensusComplete $false `
        -LocalQemuProcesses @() -InspectionsComplete $true -InspectionCount 0))
Assert-Qac 'incomplete reservation inspection fails clear-lane preflight' (-not (
    Test-QacClearWithCensus -Snapshot $emptySnapshot -WindowsCensusComplete $true `
        -LocalQemuProcesses @() -InspectionsComplete $false -InspectionCount 0))
Assert-Qac 'an inspection without a listed reservation fails clear-lane preflight' (-not (
    Test-QacClearWithCensus -Snapshot $emptySnapshot -WindowsCensusComplete $true `
        -LocalQemuProcesses @() -InspectionsComplete $true -InspectionCount 1))

$reservationId = '0123456789abcdef0123456789abcdef'
$ownerPid = 100
$ownerUser = 'TEST\guideX'
$qemuPid = 200
$qemuPath = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ownerStart = [DateTimeOffset]::Parse('2026-11-01T00:00:00Z')
$childStart = [DateTimeOffset]::Parse('2026-11-01T00:00:01Z')
$qmpPort = 45678
$activeStatus = @'
Coordinator: Online
QEMU processes: 1 (0 foreign)
Managed sessions: 1; queued: 0; blocked: 0
'@
$activeList = @"
Coordinator: Online (guideXOS QAC 2.0.0)
QEMU PID $qemuPid | $qemuPath | $($childStart.ToString('o')) | managed
  Note: Reservation $reservationId · guideXOSServer · acceptance
Reservations: 1
  $reservationId | guideXOSServer | acceptance | Running | queue 0 | $ownerUser (PID $ownerPid) | owner Verified | child PID $qemuPid
Managed sessions: 1
Managed session: $reservationId | Running | guideXOSServer · $ownerUser (PID $ownerPid)
"@
$runningSnapshot = [pscustomobject]@{Status=$activeStatus;List=$activeList;StatusExit=0;ListExit=0}
$inspection = [pscustomobject]@{
    id=$reservationId;project='guideXOSServer';mode='acceptance';state='Running';queuePosition=0
    owner="$ownerUser (PID $ownerPid)";ownerLiveness='Verified';ownerProcessStartedAtUtc=$ownerStart
    processId=$qemuPid;processStartTimeUtc=$childStart;processIdentity=$qemuPath;degraded=$false
}
 $childlessList = @"
Coordinator: Online (guideXOS QAC 2.0.0)
Reservations: 1
  $reservationId | guideXOSServer | acceptance | Granted | queue 0 | $ownerUser (PID $ownerPid) | owner Verified
Managed sessions: 1
Managed session: $reservationId | Granted | guideXOSServer · $ownerUser (PID $ownerPid)
"@
$childlessStatus = "Coordinator: Online`nQEMU processes: 0 (0 foreign)`nManaged sessions: 1; queued: 0; blocked: 0"
$childless = [pscustomobject]@{Status=$childlessStatus;List=$childlessList;StatusExit=0;ListExit=0}
$childlessParsed = ConvertFrom-QacListSnapshot -Text $childless.List -ExitCode 0
$childlessResult = $childlessParsed.Valid -and $childlessParsed.QemuProcessCount -eq 0 -and
    $childlessParsed.ManagedSessionCount -eq 1 -and -not (Test-QacClear $childless) -and
    -not (Test-QacRunningOwned -Snapshot $childless -ReservationId $reservationId -OwnerPid $ownerPid `
        -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart)
Assert-Qac 'a childless exclusive reservation remains a valid distinct snapshot but is not clear or guest-admissible' $childlessResult
Assert-Qac 'verified running reservation, QEMU census row, inspection, and owner identity pass' (
    Test-QacRunningOwned -Snapshot $runningSnapshot -ReservationId $reservationId -OwnerPid $ownerPid `
        -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart)

$localChild = [pscustomobject]@{
    ProcessId=$qemuPid;ParentProcessId=$ownerPid;ExecutablePath=$qemuPath
    CommandLine="`"$qemuPath`" -accel tcg -qmp tcp:127.0.0.1:$qmpPort,server,nowait"
    CreationDate=[DateTime]::Parse($childStart.ToString('o'))
}
Assert-Qac 'local QEMU PID, parent, executable, start time, and QMP port match QAC inspection' (
    Test-QacLocalQemuChildIdentity -LocalProcess $localChild -Inspection $inspection -OwnerPid $ownerPid `
        -QemuPath $qemuPath -QmpPort $qmpPort)
$wrongLocalParent = [pscustomobject]@{
    ProcessId=$localChild.ProcessId;ParentProcessId=999;ExecutablePath=$localChild.ExecutablePath
    CommandLine=$localChild.CommandLine;CreationDate=$localChild.CreationDate
}
Assert-Qac 'a conflicting parent process prevents QMP control' (-not (
    Test-QacLocalQemuChildIdentity -LocalProcess $wrongLocalParent -Inspection $inspection -OwnerPid $ownerPid `
        -QemuPath $qemuPath -QmpPort $qmpPort))
$wrongLocalPort = $localChild.PSObject.Copy(); $wrongLocalPort.CommandLine = $wrongLocalPort.CommandLine.Replace([string]$qmpPort,'45679')
Assert-Qac 'a QMP port mismatch prevents QMP control' (-not (
    Test-QacLocalQemuChildIdentity -LocalProcess $wrongLocalPort -Inspection $inspection -OwnerPid $ownerPid `
        -QemuPath $qemuPath -QmpPort $qmpPort))

$retainedGuest = Join-Path $candidateRoot 'qac-guest-001'
$retainedStatus = Get-Content -LiteralPath (Join-Path $retainedGuest 'qac-during.status.txt') -Raw
$retainedList = Get-Content -LiteralPath (Join-Path $retainedGuest 'qac-during.list.txt') -Raw
$retainedSnapshot = [pscustomobject]@{Status=$retainedStatus;List=$retainedList;StatusExit=0;ListExit=0}
$retainedStatusFields = ConvertFrom-QacStatusSnapshot -Text $retainedStatus -ExitCode 0
$retainedListFields = ConvertFrom-QacListSnapshot -Text $retainedList -ExitCode 0
Assert-Qac 'retained R4R1 response is parsed as status 0/0 and list with one reservation, session, and QEMU process' (
    $retainedStatusFields.Valid -and $retainedStatusFields.QemuProcessCount -eq 0 -and
    $retainedStatusFields.ManagedSessionCount -eq 0 -and $retainedListFields.Valid -and
    $retainedListFields.ReservationCount -eq 1 -and $retainedListFields.ManagedSessionCount -eq 1 -and
    $retainedListFields.QemuProcessCount -eq 1)
Assert-Qac 'the exact retained status/list disagreement is rejected for admission' (
    -not (Test-QacRunningOwned -Snapshot $retainedSnapshot -ReservationId $reservationId -OwnerPid $ownerPid `
        -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart))

$transientSequence = @($retainedSnapshot,$runningSnapshot)
$transientAcceptedAt = $null
for ($i = 0; $i -lt $transientSequence.Count; $i++) {
    if (Test-QacRunningOwned -Snapshot $transientSequence[$i] -ReservationId $reservationId -OwnerPid $ownerPid `
            -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart) {
        $transientAcceptedAt = $i
        break
    }
}
Assert-Qac 'bounded polling rejects the first transition snapshot and accepts only the later complete snapshot' ($transientAcceptedAt -eq 1)

$foreignSnapshot = [pscustomobject]@{
    Status="Coordinator: Online`nQEMU processes: 2 (1 foreign)`nManaged sessions: 1; queued: 0; blocked: 0"
    List=$activeList.Replace("Reservations: 1", "QEMU PID 201 | C:\foreign\qemu-system-x86_64.exe | $($childStart.ToString('o')) | unmanaged/foreign`nReservations: 1")
    StatusExit=0;ListExit=0
}
Assert-Qac 'a foreign QEMU process blocks admission' (-not (Test-QacRunningOwned -Snapshot $foreignSnapshot `
    -ReservationId $reservationId -OwnerPid $ownerPid -OwnerUser $ownerUser -QemuPath $qemuPath `
    -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart))
$conflictingSnapshot = [pscustomobject]@{
    Status="Coordinator: Online`nQEMU processes: 2 (0 foreign)`nManaged sessions: 1; queued: 0; blocked: 0"
    List=$activeList.Replace("Reservations: 1", "QEMU PID 201 | C:\foreign\qemu-system-x86_64.exe | $($childStart.ToString('o')) | managed`nReservations: 1")
    StatusExit=0;ListExit=0
}
Assert-Qac 'an extra conflicting managed QEMU process blocks admission' (-not (Test-QacRunningOwned -Snapshot $conflictingSnapshot `
    -ReservationId $reservationId -OwnerPid $ownerPid -OwnerUser $ownerUser -QemuPath $qemuPath `
    -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart))

$missingSnapshot = [pscustomobject]@{
    Status="Coordinator: Online`nManaged sessions: 1; queued: 0; blocked: 0"
    List="Coordinator: Online (guideXOS QAC 2.0.0)`nReservations: 1`nManaged sessions: 1"
    StatusExit=0;ListExit=0
}
Assert-Qac 'missing or malformed census fields fail closed' (-not (Test-QacRunningOwned -Snapshot $missingSnapshot `
    -ReservationId $reservationId -OwnerPid $ownerPid -OwnerUser $ownerUser -QemuPath $qemuPath `
    -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart))
$incompleteSnapshot = [pscustomobject]@{
    Status=$activeStatus;List=$activeList + "`nCoordinator warning: PID path could not be inspected";StatusExit=0;ListExit=0
}
Assert-Qac 'an incomplete QAC process census with a warning fails closed' (-not (
    Test-QacRunningOwned -Snapshot $incompleteSnapshot -ReservationId $reservationId -OwnerPid $ownerPid `
        -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart))
$serviceFailure = [pscustomobject]@{Status=$activeStatus;List=$activeList;StatusExit=2;ListExit=0}
Assert-Qac 'QAC status service failure fails closed' (-not (Test-QacRunningOwned -Snapshot $serviceFailure `
    -ReservationId $reservationId -OwnerPid $ownerPid -OwnerUser $ownerUser -QemuPath $qemuPath `
    -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart))
$lostOwnerList = $activeList.Replace("$ownerUser (PID $ownerPid)", "$ownerUser (PID 101)")
$lostOwner = [pscustomobject]@{Status=$activeStatus;List=$lostOwnerList;StatusExit=0;ListExit=0}
Assert-Qac 'lost or changed reservation owner identity fails closed' (-not (
    Test-QacRunningOwned -Snapshot $lostOwner -ReservationId $reservationId -OwnerPid $ownerPid `
        -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $inspection -ExpectedOwnerStartTimeUtc $ownerStart))
$releasedInspection = [pscustomobject]@{
    id=$inspection.id;project=$inspection.project;mode=$inspection.mode;state='Released';queuePosition=$inspection.queuePosition
    owner=$inspection.owner;ownerLiveness=$inspection.ownerLiveness;ownerProcessStartedAtUtc=$inspection.ownerProcessStartedAtUtc
    processId=$inspection.processId;processStartTimeUtc=$inspection.processStartTimeUtc
    processIdentity=$inspection.processIdentity;degraded=$inspection.degraded
}
Assert-Qac 'a released inspect record is not an active admission' (-not (
    Test-QacRunningOwned -Snapshot $runningSnapshot -ReservationId $reservationId -OwnerPid $ownerPid `
        -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $releasedInspection -ExpectedOwnerStartTimeUtc $ownerStart))
$missingInspection = [pscustomobject]@{
    id=$inspection.id;project=$inspection.project;mode=$inspection.mode;state=$inspection.state;queuePosition=$inspection.queuePosition
    owner=$inspection.owner;ownerLiveness=$inspection.ownerLiveness;ownerProcessStartedAtUtc=$inspection.ownerProcessStartedAtUtc
    processId=$inspection.processId;processIdentity=$inspection.processIdentity;degraded=$inspection.degraded
}
Assert-Qac 'missing child start identity in inspect fails closed' (-not (
    Test-QacRunningOwned -Snapshot $runningSnapshot -ReservationId $reservationId -OwnerPid $ownerPid `
        -OwnerUser $ownerUser -QemuPath $qemuPath -Inspection $missingInspection -ExpectedOwnerStartTimeUtc $ownerStart))

$postrunRoot = Join-Path $phaseRoot 'postrun-recovery-20261101T002858953Z'
$postrunStatus = Get-Content -LiteralPath (Join-Path $postrunRoot 'qac-status.txt') -Raw
$postrunList = Get-Content -LiteralPath (Join-Path $postrunRoot 'qac-list.txt') -Raw
$postrunSnapshot = [pscustomobject]@{Status=$postrunStatus;List=$postrunList;StatusExit=0;ListExit=0}
Assert-Qac 'retained postrun status/list prove clean reservation release and process census' (Test-QacClear $postrunSnapshot)

$qmpValid = ConvertFrom-QmpQueryStatusResponse '{"return":{"status":"running","running":true}}'
Assert-Qac 'strict QMP query-status accepts a nonempty status and Boolean running field' (
    $qmpValid.Valid -and $qmpValid.Status -eq 'running' -and $qmpValid.Running)
Assert-Qac 'strict QMP query-status rejects missing fields' (-not (
    ConvertFrom-QmpQueryStatusResponse '{"return":{"status":"running"}}').Valid)
Assert-Qac 'strict QMP query-status rejects a string in place of Boolean running' (-not (
    ConvertFrom-QmpQueryStatusResponse '{"return":{"status":"running","running":"true"}}').Valid)
Assert-Qac 'strict QMP query-status rejects error responses' (-not (
    ConvertFrom-QmpQueryStatusResponse '{"error":{"class":"GenericError","desc":"bad"}}').Valid)
Assert-Qac 'strict QMP query-status rejects malformed JSON' (-not (
    ConvertFrom-QmpQueryStatusResponse '{not-json').Valid)

$result = [ordered]@{
    result='PASS';caseCount=$script:caseCount;hostedRoot=$testRoot
    actualQacConnected=$false;qemuLaunched=$false;qacReserved=$false
    retainedR4R1StatusListReplayed=$true;candidatePathCases=4
    sourceRamdiskModified=$false
}
[System.IO.File]::WriteAllText((Join-Path $testRoot 'hosted-test-manifest.json'),
    ($result | ConvertTo-Json -Depth 5),[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R4 QAC admission predicates: PASS ($script:caseCount cases; $testRoot)"
