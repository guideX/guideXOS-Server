param(
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter()][string]$CandidateRoot = '',
    [Parameter()][switch]$MemoryWatchpoints,
    [Parameter()][ValidateRange(120,600)][int]$MaxRuntimeSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11R2 keyboard runner requires PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r11-managed-state-provenance'))
$bootRoot = [System.IO.Path]::GetFullPath($BootEvidencePath)
$relative = [System.IO.Path]::GetRelativePath($phaseRoot, $bootRoot)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) {
    throw "BootEvidencePath must be a unique directory below $phaseRoot."
}
if (Test-Path -LiteralPath $bootRoot) {
    throw "Refusing to overwrite C188R11R2 guest evidence: $bootRoot"
}
$null = New-Item -ItemType Directory -Path $bootRoot
$runIdentityId = [guid]::NewGuid().ToString('N')
$candidateRoot = if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    Join-Path $phaseRoot 'candidate\keyboard-dispatch-001'
} else {
    [System.IO.Path]::GetFullPath($CandidateRoot)
}
$candidateRelative = [System.IO.Path]::GetRelativePath($phaseRoot, $candidateRoot)
if ($candidateRelative -eq '.' -or
    $candidateRelative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($candidateRelative)) {
    throw "CandidateRoot must be a unique directory below $phaseRoot."
}
$candidateEsp = Join-Path $candidateRoot 'esp'
$candidateStageManifestPath = Join-Path $candidateRoot 'ramdisk-staging.json'
foreach ($requiredCandidatePath in @(
    $candidateEsp,
    (Join-Path $candidateEsp 'kernel.elf'),
    (Join-Path $candidateEsp 'ramdisk.img'),
    (Join-Path $candidateEsp 'EFI\BOOT\BOOTX64.EFI'),
    $candidateStageManifestPath)) {
    if (-not (Test-Path -LiteralPath $requiredCandidatePath -PathType Leaf) -and
        -not (Test-Path -LiteralPath $requiredCandidatePath -PathType Container)) {
        throw "Validated C188R11R2 candidate input is missing: $requiredCandidatePath"
    }
}
$candidateStageManifest = Get-Content -LiteralPath $candidateStageManifestPath -Raw | ConvertFrom-Json
if ($candidateStageManifest.result -ne 'PASS') {
    throw 'Retained C188R11R2 candidate staging manifest is not PASS.'
}
$candidateMediaHashes = [ordered]@{}
foreach ($relativeMediaPath in @('kernel.elf','ramdisk.img','EFI\BOOT\BOOTX64.EFI')) {
    $candidateMediaHashes[$relativeMediaPath.Replace('\','/')] =
        (Get-FileHash -LiteralPath (Join-Path $candidateEsp $relativeMediaPath) -Algorithm SHA256).Hash
}
if ($candidateMediaHashes['kernel.elf'] -ne $ExpectedKernelSha256.ToUpperInvariant() -or
    $candidateMediaHashes['ramdisk.img'] -ne $candidateStageManifest.ramdiskSha256.ToUpperInvariant()) {
    throw 'Retained C188R11R2 candidate hashes do not match the requested kernel or staging manifest.'
}
$runEsp = Join-Path $bootRoot 'esp'
Copy-Item -LiteralPath $candidateEsp -Destination $runEsp -Recurse
foreach ($relativeMediaPath in @('kernel.elf','ramdisk.img','EFI\BOOT\BOOTX64.EFI')) {
    $normalizedMediaPath = $relativeMediaPath.Replace('\','/')
    $copiedHash = (Get-FileHash -LiteralPath (Join-Path $runEsp $relativeMediaPath) -Algorithm SHA256).Hash
    if ($copiedHash -ne $candidateMediaHashes[$normalizedMediaPath]) {
        throw "C188R11R2 candidate copy hash mismatch for $normalizedMediaPath."
    }
}
[ordered]@{
    result='PASS'
    sourceEsp=$candidateEsp
    stagingManifest=$candidateStageManifestPath
    stagingManifestSha256=(Get-FileHash -LiteralPath $candidateStageManifestPath -Algorithm SHA256).Hash
    expectedKernelSha256=$ExpectedKernelSha256.ToUpperInvariant()
    sourceMediaHashes=$candidateMediaHashes
    copiedEsp=$runEsp
    copiedMediaHashes=$candidateMediaHashes
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootRoot 'r2-candidate-input-manifest.json') -Encoding utf8
$runner = Join-Path $PSScriptRoot 'run-c188-qac-guest.ps1'
$predicateHelpers = Join-Path $PSScriptRoot 'c188r11-runner-predicates.ps1'
$r2PredicateHelpers = Join-Path $PSScriptRoot 'c188r11r2-runner-predicates.ps1'
$qacOwnershipHelpers = Join-Path $PSScriptRoot 'c188r11qac-ownership-predicates.ps1'
. $predicateHelpers
. $r2PredicateHelpers
. $qacOwnershipHelpers
. (Join-Path $PSScriptRoot 'c188r11r4r2-runner-predicates.ps1')
if ($null -eq ('GuideXos.C188.LiveProcessCapture' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'C188LiveProcessCapture.cs')
}
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$control = Join-Path $PSScriptRoot 'c188-qmp-control.py'
$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1
$pwsh = Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1
$activePath = Join-Path $bootRoot 'active.json'
$serialPath = Join-Path $bootRoot 'serial.log'
$controllerStdout = Join-Path $bootRoot 'r2-controller.stdout.log'
$controllerStderr = Join-Path $bootRoot 'r2-controller.stderr.log'
$controllerManifest = Join-Path $bootRoot 'r2-keyboard-dispatch.json'
$gdbScriptPath = Join-Path $bootRoot 'r4-memory-watchpoints.gdb'
$gdbStdoutPath = Join-Path $bootRoot 'r4-gdb.stdout.log'
$gdbStderrPath = Join-Path $bootRoot 'r4-gdb.stderr.log'
$qmpActions = [System.Collections.Generic.List[object]]::new()
foreach ($path in @($controllerStdout,$controllerStderr,$controllerManifest,
    $gdbScriptPath,$gdbStdoutPath,$gdbStderrPath,
    (Join-Path $bootRoot 'qac-input.status.txt'),(Join-Path $bootRoot 'qac-input.list.txt'),
    (Join-Path $bootRoot 'qac-input.inspect.json'),(Join-Path $bootRoot 'qac-input-ownership.json'),
    (Join-Path $bootRoot 'qac-prekey.status.txt'),(Join-Path $bootRoot 'qac-prekey.list.txt'),
    (Join-Path $bootRoot 'qac-prekey.inspect.json'),(Join-Path $bootRoot 'qac-prekey-ownership.json'))) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite C188R11R2 evidence: $path" }
}

$localQemu = @(Get-Process -Name 'qemu-system-x86_64' -ErrorAction SilentlyContinue)
if ($localQemu.Count -ne 0) {
    throw 'Local QEMU process census is nonempty; the R2 guest was not attempted.'
}
$gdbCommand = if ($MemoryWatchpoints) {
    Get-Command 'gdb.exe' -ErrorAction Stop | Select-Object -First 1
} else { $null }

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $pwsh.Source
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$runnerArguments = @(
    '-NoProfile','-File',$runner,
    '-BootEvidencePath',$bootRoot,
    '-ExpectedKernelSha256',$ExpectedKernelSha256,
    '-Scenario','managed-state-provenance',
    '-MaxRuntimeSeconds',[string]$MaxRuntimeSeconds,
    '-BootTimeoutSeconds','180',
    '-AdmissionTimeoutSeconds','60',
    '-RunIdentityId',$runIdentityId)
if ($MemoryWatchpoints) { $runnerArguments += '-EnableGdbStub' }
foreach ($argument in $runnerArguments) {
    [void]$psi.ArgumentList.Add([string]$argument)
}
$qacRunner = [System.Diagnostics.Process]::Start($psi)
if (-not $qacRunner) { throw 'Failed to start the bounded C188R11R2 QAC guest runner.' }
$stdoutTask = $qacRunner.StandardOutput.ReadToEndAsync()
$stderrTask = $qacRunner.StandardError.ReadToEndAsync()
$inputSent = $false
$focusVerified = $false
$editorFocusVerified = $false
$editorFocusState = $null
$focusEvidence = [ordered]@{
    notesWindowActive=$false
    documentControlRouteEstablished=$false
    documentRouteCorrelatedToPointer=$false
    editableDocumentWidgetFocused=$false
    unrelatedControlFocused=$false
    unsupportedRouteObserved=$false
    focusUnknown=$true
    focusState='unknown'
}
$preKeyOwnership = $null
$editorFocusScreenPoint = $null
$pointerClearScreenPoint = $null
$notesTextEntryEvidence = $null
$editorFocusFailureReason = ''
$runnerExitCode = -1
$shutdownRequested = $false
$gdbProcess = $null
$watchpointStatus = if ($MemoryWatchpoints) { 'WAITING_FOR_REGISTRY_SNAPSHOT' } else { 'NOT_REQUESTED' }
$watchpointAttempted = $false

function Invoke-C188R11R2Control([string[]]$Arguments, [string]$Name) {
    $output = (& $python.Source $control '--active' $activePath @Arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $logPath = Join-Path $bootRoot ($Name + '.log')
    Set-Content -LiteralPath $logPath -Value $output -Encoding utf8
    if ($exitCode -ne 0) {
        throw "QMP action '$Name' failed with exit code $($exitCode): $output"
    }
    return [ordered]@{name=$Name;exitCode=$exitCode;log=$logPath}
}

function Start-C188R11R4MemoryWatchpoints($Snapshot, [int]$GdbPort) {
    if (-not $MemoryWatchpoints -or $GdbPort -le 0 -or
        $Snapshot.Groups['registry'].Value -eq '0000000000000000' -or
        $Snapshot.Groups['slot'].Value -eq '0000000000000000') {
        $script:watchpointStatus = 'INVALID_TARGET_ADDRESS'
        return
    }
    $registryAddress = [Convert]::ToUInt64(
        $Snapshot.Groups['registry'].Value,16)
    $registryLengthAddress = ($registryAddress + 8u).ToString('X16')
    $poolEntriesSlot = $Snapshot.Groups['slot'].Value
    $gdbText = @"
set pagination off
set confirm off
set architecture i386:x86-64
target remote 127.0.0.1:$GdbPort
set `$c188_hits = 0
set `$c188_len_addr = (unsigned int*)0x$registryLengthAddress
set `$c188_slot_addr = (unsigned long long*)0x$poolEntriesSlot
set `$c188_old_len = *`$c188_len_addr
set `$c188_old_entries = *`$c188_slot_addr
watch *`$c188_len_addr
set `$c188_len_bp = `$bpnum
watch *`$c188_slot_addr
set `$c188_slot_bp = `$bpnum
commands `$c188_len_bp
  silent
  set `$c188_hits = `$c188_hits + 1
  set `$c188_new_len = *`$c188_len_addr
  printf "[C188R11R4-WATCH] hit=%u field=registry-length old=%u new=%u pc=%p\n", `$c188_hits, `$c188_old_len, `$c188_new_len, `$pc
  x/8i `$pc-16
  info registers rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11
  set `$c188_old_len = `$c188_new_len
  if `$c188_hits >= 16
    disable `$c188_len_bp
    disable `$c188_slot_bp
    printf "[C188R11R4-WATCH] bounded-hit-limit reached=16\n"
  end
  continue
end
commands `$c188_slot_bp
  silent
  set `$c188_hits = `$c188_hits + 1
  set `$c188_new_entries = *`$c188_slot_addr
  printf "[C188R11R4-WATCH] hit=%u field=pool-entries old=%016llx new=%016llx pc=%p\n", `$c188_hits, `$c188_old_entries, `$c188_new_entries, `$pc
  x/8i `$pc-16
  info registers rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11
  set `$c188_old_entries = `$c188_new_entries
  if `$c188_hits >= 16
    disable `$c188_len_bp
    disable `$c188_slot_bp
    printf "[C188R11R4-WATCH] bounded-hit-limit reached=16\n"
  end
  continue
end
printf "[C188R11R4-WATCH] watchpoints-armed registry-length=0x$registryLengthAddress pool-entries=0x$poolEntriesSlot\n"
continue
"@
    Set-Content -LiteralPath $gdbScriptPath -Value $gdbText -Encoding ascii
    $script:watchpointAttempted = $true
    $script:watchpointStatus = 'STARTING'
    $script:gdbProcess = Start-Process -FilePath $gdbCommand.Source `
        -ArgumentList @('-q','-batch','-x',$gdbScriptPath) `
        -RedirectStandardOutput $gdbStdoutPath `
        -RedirectStandardError $gdbStderrPath `
        -WindowStyle Hidden -PassThru
    if (-not $script:gdbProcess) {
        $script:watchpointStatus = 'FAILED_TO_START'
        return
    }

    $armDeadline = (Get-Date).AddSeconds(20)
    do {
        if (Test-Path -LiteralPath $gdbStdoutPath -PathType Leaf) {
            $gdbTextNow = [System.IO.File]::ReadAllText($gdbStdoutPath)
            if ($gdbTextNow.Contains('[C188R11R4-WATCH] watchpoints-armed')) {
                $script:watchpointStatus = 'ARMED'
                return
            }
        }
        if ($script:gdbProcess.HasExited) { break }
        Start-Sleep -Milliseconds 50
    } while ((Get-Date) -lt $armDeadline)

    $script:watchpointStatus = if ($script:gdbProcess.HasExited) {
        'GDB_SETUP_FAILED'
    } else { 'GDB_SETUP_TIMEOUT' }
    if (-not $script:gdbProcess.HasExited) {
        Stop-Process -Id $script:gdbProcess.Id -Force -ErrorAction SilentlyContinue
    }
}

try {
    $readyDeadline = (Get-Date).AddSeconds(210)
    $active = $null
    $serialText = ''
    do {
        if ($qacRunner.HasExited) {
            throw "QAC runner exited before R2 input qualification (exit $($qacRunner.ExitCode))."
        }
        if ((Test-Path -LiteralPath $activePath -PathType Leaf) -and
            (Test-Path -LiteralPath $serialPath -PathType Leaf)) {
            try {
                $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
                $serialText = Get-Content -LiteralPath $serialPath -Raw
                if ($MemoryWatchpoints -and -not $watchpointAttempted) {
                    $registryInitSnapshot = Get-C188R11MemorySnapshot `
                        $serialText 'registry-init'
                    if ($registryInitSnapshot.Success) {
                        Start-C188R11R4MemoryWatchpoints $registryInitSnapshot `
                            ([int]$active.gdbPort)
                    }
                }
                if ([int]$active.screenWidth -gt 0 -and [int]$active.screenHeight -gt 0) {
                    $ready = Test-C188R11NotesReady $serialText
                    $startupFocusEvidence = Get-C188R11R4R2NotesFocusEvidence $serialText
                    $focusVerified = [bool]$startupFocusEvidence.notesWindowActive
                    if ($ready -and $focusVerified) { break }
                }
            } catch { $ready = $false; $focusVerified = $false }
        }
        Start-Sleep -Milliseconds $(if ($MemoryWatchpoints -and -not $watchpointAttempted) { 25 } else { 250 })
    } while ((Get-Date) -lt $readyDeadline)
    if (-not $ready) { throw 'C188R11 readiness predicate did not reach the corrected managed-return boundary.' }
    if (-not $focusVerified) {
        throw 'Notes was not the active compositor keyboard target at main-loop readiness; no keyboard input was sent.'
    }

    $beforeStatus = Get-Content -LiteralPath (Join-Path $bootRoot 'qac-before.status.txt') -Raw
    $beforeList = Get-Content -LiteralPath (Join-Path $bootRoot 'qac-before.list.txt') -Raw
    if ($beforeStatus -notmatch 'Coordinator:\s+Online' -or
        $beforeStatus -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
        $beforeStatus -notmatch 'Managed sessions:\s+0; queued:\s+0' -or
        $beforeList -notmatch 'Reservations:\s+0') {
        throw 'QAC pre-admission census was not exclusive and empty; no key event was sent.'
    }
    $duringListPath = Join-Path $bootRoot 'qac-during.list.txt'
    $duringStatusPath = Join-Path $bootRoot 'qac-during.status.txt'
    $duringCensusDeadline = (Get-Date).AddSeconds(15)
    while ((-not (Test-Path -LiteralPath $duringListPath -PathType Leaf) -or
            -not (Test-Path -LiteralPath $duringStatusPath -PathType Leaf)) -and
            -not $qacRunner.HasExited -and (Get-Date) -lt $duringCensusDeadline) {
        Start-Sleep -Milliseconds 50
    }
    if (-not (Test-Path -LiteralPath $duringListPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $duringStatusPath -PathType Leaf)) {
        throw 'QAC did not publish the during-run census before the input gate; no key event was sent.'
    }
    $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
    if ($null -eq $active.qacOwnershipRunIdentity) {
        throw 'QAC run omitted its inspected ownership identity; no keyboard event was sent.'
    }
    if (-not [bool]$active.qacAdmissionConfirmed -or
        [string]$active.runIdentityId -ne $runIdentityId -or
        [string]$active.bootEvidencePath -ne $bootRoot -or
        [int]$active.pid -ne [int]$active.qacOwnerProcessId -or
        [int]$active.qacOwnershipRunIdentity.runnerProcessId -ne $qacRunner.Id -or
        [int]$active.qacQemuChildPid -le 0) {
        throw 'QAC run identity did not match the current isolated guest run; no keyboard event was sent.'
    }

    $inputQacStatus = (& $qacCli status 2>&1 | Out-String)
    $inputQacStatusExitCode = $LASTEXITCODE
    $inputQacList = (& $qacCli list 2>&1 | Out-String)
    $inputQacListExitCode = $LASTEXITCODE
    $inputQacInspect = (& $qacCli inspect ([string]$active.qacReservationId) 2>&1 | Out-String)
    $inputQacInspectExitCode = $LASTEXITCODE
    if ($inputQacStatusExitCode -ne 0 -or $inputQacListExitCode -ne 0 -or $inputQacInspectExitCode -ne 0) {
        throw 'QAC could not provide a fresh status/list/inspect ownership snapshot; no keyboard event was sent.'
    }
    try { $inputQacInspection = $inputQacInspect | ConvertFrom-Json -ErrorAction Stop }
    catch { throw 'QAC input-gate inspection was not valid JSON; no keyboard event was sent.' }

    $inputRunIdentity = [ordered]@{}
    foreach ($property in $active.qacOwnershipRunIdentity.PSObject.Properties) {
        $inputRunIdentity[$property.Name] = $property.Value
    }
    $inputRunIdentity.runIdentityId = [string]$active.runIdentityId
    $inputRunIdentity.bootEvidencePath = [string]$active.bootEvidencePath
    $inputRunIdentity.expectedBootEvidencePath = $bootRoot
    $inputRunIdentity.localProcessCaptureError = $null
    try {
        $ownerProcess = Get-Process -Id ([int]$active.qacOwnerProcessId) -ErrorAction Stop
        $childProcess = Get-Process -Id ([int]$active.qacQemuChildPid) -ErrorAction Stop
        $runnerProcess = Get-Process -Id ([int]$inputRunIdentity.runnerProcessId) -ErrorAction Stop
        $ownerCim = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $($active.qacOwnerProcessId)" -ErrorAction Stop
        $childCim = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $($active.qacQemuChildPid)" -ErrorAction Stop
        $inputRunIdentity.ownerProcessAlive = ($null -ne $ownerCim)
        $inputRunIdentity.ownerProcessStartTimeUtc = $ownerProcess.StartTime.ToUniversalTime().ToString('O')
        $inputRunIdentity.ownerParentProcessId = if ($ownerCim) { [int]$ownerCim.ParentProcessId } else { 0 }
        $inputRunIdentity.runnerProcessAlive = $true
        $inputRunIdentity.expectedRunnerProcessStartTimeUtc = $runnerProcess.StartTime.ToUniversalTime().ToString('O')
        $inputRunIdentity.childProcessAlive = ($null -ne $childCim)
        $inputRunIdentity.childProcessStartTimeUtc = $childProcess.StartTime.ToUniversalTime().ToString('O')
        $inputRunIdentity.childExecutableIdentity = if ($childCim) { [string]$childCim.ExecutablePath } else { '' }
        $inputRunIdentity.childParentProcessId = if ($childCim) { [int]$childCim.ParentProcessId } else { 0 }
    } catch {
        $inputRunIdentity.localProcessCaptureError = $_.Exception.Message
        $inputRunIdentity.ownerProcessAlive = $false
        $inputRunIdentity.runnerProcessAlive = $false
        $inputRunIdentity.childProcessAlive = $false
    }
    $inputOwnership = Get-C188R11R4QacOwnershipEvidence $inputQacStatus $inputQacList $inputQacInspection $inputRunIdentity
    $qacInputStatusPath = Join-Path $bootRoot 'qac-input.status.txt'
    $qacInputListPath = Join-Path $bootRoot 'qac-input.list.txt'
    $qacInputInspectPath = Join-Path $bootRoot 'qac-input.inspect.json'
    $qacInputOwnershipPath = Join-Path $bootRoot 'qac-input-ownership.json'
    Set-Content -LiteralPath $qacInputStatusPath -Value $inputQacStatus -Encoding utf8
    Set-Content -LiteralPath $qacInputListPath -Value $inputQacList -Encoding utf8
    $inputQacInspection | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $qacInputInspectPath -Encoding utf8
    $inputOwnership | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $qacInputOwnershipPath -Encoding utf8
    if (-not [bool]$inputOwnership.accepted) {
        throw "QAC ownership proof failed closed at input gate: $($inputOwnership.reasons -join ', '); no key event was sent."
    }

    $qmpActions.Add((Invoke-C188R11R2Control @('capture','initial-notes') 'r2-qmp-capture-initial-notes'))
    $serialText = Get-Content -LiteralPath $serialPath -Raw
    $initialFocusEvidence = Get-C188R11R4R2NotesFocusEvidence $serialText
    if (-not [bool]$initialFocusEvidence.notesWindowActive) {
        throw 'Notes lost the active-window focus snapshot before input; no keyboard event was sent.'
    }

    # The window is centered by NativeAotApplication::open using its validated
    # 600x360 initial geometry. In the active C120 control-host route, the
    # document starts at (20,72) with 8x18 cells; local (24,99) is row 1,
    # column 0. It also remains inside the direct C175 editor bounds.
    $screenWidth = [int]$active.screenWidth
    $screenHeight = [int]$active.screenHeight
    $notesWindowX = [Math]::Max(0, [int][Math]::Floor(($screenWidth - 600) / 2))
    $notesWindowY = [Math]::Max(0, [int][Math]::Floor(($screenHeight - 360) / 2))
    $focusScreenX = $notesWindowX + 20 + 4
    $focusScreenY = $notesWindowY + 24 + 99
    $editorFocusScreenPoint = [ordered]@{
        x=$focusScreenX; y=$focusScreenY; windowX=$notesWindowX
        windowY=$notesWindowY; windowWidth=600; windowHeight=360
        localEditorX=24; localEditorY=99; target='C120 document row 1, column 0 center'
    }
    $qmpActions.Add((Invoke-C188R11R2Control @('click',[string]$focusScreenX,[string]$focusScreenY) 'r2-qmp-click-notes-editor'))
    $focusDeadline = (Get-Date).AddSeconds(8)
    do {
        $serialText = Get-Content -LiteralPath $serialPath -Raw
        $editorFocusState = Get-C188R11R4R2StageSnapshot $serialText 'after-pointer'
        $nativeEditorPointer = [regex]::IsMatch($serialText,
            '(?m)^\[C116-NATIVE-INPUT\] kind=pointer-down x=00000018 y=00000063 result=PASS\r?$')
        if ($null -ne $editorFocusState -and $nativeEditorPointer) { break }
        Start-Sleep -Milliseconds 50
    } while ((Get-Date) -lt $focusDeadline)
    $focusEvidence = Get-C188R11R4R2NotesFocusEvidence $serialText
    $editorFocusVerified = [bool]$focusEvidence.notesWindowActive -and
        [bool]$focusEvidence.documentControlRouteEstablished -and
        [bool]$focusEvidence.documentRouteCorrelatedToPointer -and
        [bool]$focusEvidence.editableDocumentWidgetFocused
    if ($editorFocusVerified) {
        # Move the QMP cursor to the title bar after the one editor click so
        # the pointer image cannot cover the first glyph in either screenshot.
        # A move without a button press does not change text-area focus/caret.
        $pointerClearScreenPoint = [ordered]@{
            x=$notesWindowX + 300; y=$notesWindowY + 12
            target='Notes title bar, pointer only'
        }
        $qmpActions.Add((Invoke-C188R11R2Control @('move',
            [string]$pointerClearScreenPoint.x,
            [string]$pointerClearScreenPoint.y) 'r2-qmp-move-pointer-clear'))
    }
    $qmpActions.Add((Invoke-C188R11R2Control @('capture','editor-focused') 'r2-qmp-capture-editor-focused'))

    if (-not $editorFocusVerified) {
        $editorFocusFailureReason = 'The single source-defined click did not prove the C120 document-control route and editable text-area focus (focusState=' + $focusEvidence.focusState + '); no keyboard event was sent.'
        $qmpActions.Add([ordered]@{name='skip-a-key';reason=$editorFocusFailureReason})
    } else {
        # Keep the insertion comparison free of the caret/focus redraw: this
        # baseline is taken after editor focus and immediately before ASCII a.
        $qmpActions.Add((Invoke-C188R11R2Control @('capture','before-input') 'r2-qmp-capture-before-input'))

        # Reconcile current QAC state and exact owner/child identity again at
        # the last possible point before the only keyboard transaction.
        $preKeyQacStatus = (& $qacCli status 2>&1 | Out-String)
        $preKeyQacStatusExitCode = $LASTEXITCODE
        $preKeyQacList = (& $qacCli list 2>&1 | Out-String)
        $preKeyQacListExitCode = $LASTEXITCODE
        $preKeyQacInspectText = (& $qacCli inspect ([string]$active.qacReservationId) 2>&1 | Out-String)
        $preKeyQacInspectExitCode = $LASTEXITCODE
        $preKeyStatusPath = Join-Path $bootRoot 'qac-prekey.status.txt'
        $preKeyListPath = Join-Path $bootRoot 'qac-prekey.list.txt'
        $preKeyInspectPath = Join-Path $bootRoot 'qac-prekey.inspect.json'
        $preKeyOwnershipPath = Join-Path $bootRoot 'qac-prekey-ownership.json'
        Set-Content -LiteralPath $preKeyStatusPath -Value $preKeyQacStatus -Encoding utf8
        Set-Content -LiteralPath $preKeyListPath -Value $preKeyQacList -Encoding utf8
        $preKeyInspection = $null
        try { $preKeyInspection = $preKeyQacInspectText | ConvertFrom-Json -ErrorAction Stop }
        catch { }
        if ($null -ne $preKeyInspection) {
            $preKeyInspection | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $preKeyInspectPath -Encoding utf8
        } else {
            Set-Content -LiteralPath $preKeyInspectPath -Value $preKeyQacInspectText -Encoding utf8
        }

        $preKeyActive = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
        if ([string]$preKeyActive.runIdentityId -ne $runIdentityId -or
            [string]$preKeyActive.bootEvidencePath -ne $bootRoot -or
            [string]$preKeyActive.qacReservationId -ne [string]$active.qacReservationId -or
            [int]$preKeyActive.qacOwnerProcessId -ne [int]$active.qacOwnerProcessId -or
            [int]$preKeyActive.qacQemuChildPid -ne [int]$active.qacQemuChildPid) {
            throw 'QAC run identity changed before the key gate; no key event was sent.'
        }
        $preKeyRunIdentity = [ordered]@{}
        foreach ($property in $preKeyActive.qacOwnershipRunIdentity.PSObject.Properties) {
            $preKeyRunIdentity[$property.Name] = $property.Value
        }
        $preKeyRunIdentity.runIdentityId = [string]$preKeyActive.runIdentityId
        $preKeyRunIdentity.bootEvidencePath = [string]$preKeyActive.bootEvidencePath
        $preKeyRunIdentity.expectedBootEvidencePath = $bootRoot
        $preKeyRunIdentity.localProcessCaptureError = $null
        try {
            $preKeyOwnerProcess = Get-Process -Id ([int]$preKeyActive.qacOwnerProcessId) -ErrorAction Stop
            $preKeyChildProcess = Get-Process -Id ([int]$preKeyActive.qacQemuChildPid) -ErrorAction Stop
            $preKeyRunnerProcess = Get-Process -Id ([int]$preKeyRunIdentity.runnerProcessId) -ErrorAction Stop
            $preKeyOwnerCim = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $($preKeyActive.qacOwnerProcessId)" -ErrorAction Stop
            $preKeyChildCim = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $($preKeyActive.qacQemuChildPid)" -ErrorAction Stop
            $preKeyRunIdentity.ownerProcessAlive = ($null -ne $preKeyOwnerCim)
            $preKeyRunIdentity.ownerProcessStartTimeUtc = $preKeyOwnerProcess.StartTime.ToUniversalTime().ToString('O')
            $preKeyRunIdentity.ownerParentProcessId = if ($preKeyOwnerCim) { [int]$preKeyOwnerCim.ParentProcessId } else { 0 }
            $preKeyRunIdentity.runnerProcessAlive = $true
            $preKeyRunIdentity.expectedRunnerProcessStartTimeUtc = $preKeyRunnerProcess.StartTime.ToUniversalTime().ToString('O')
            $preKeyRunIdentity.childProcessAlive = ($null -ne $preKeyChildCim)
            $preKeyRunIdentity.childProcessStartTimeUtc = $preKeyChildProcess.StartTime.ToUniversalTime().ToString('O')
            $preKeyRunIdentity.childExecutableIdentity = if ($preKeyChildCim) { [string]$preKeyChildCim.ExecutablePath } else { '' }
            $preKeyRunIdentity.childParentProcessId = if ($preKeyChildCim) { [int]$preKeyChildCim.ParentProcessId } else { 0 }
        } catch {
            $preKeyRunIdentity.localProcessCaptureError = $_.Exception.Message
            $preKeyRunIdentity.ownerProcessAlive = $false
            $preKeyRunIdentity.runnerProcessAlive = $false
            $preKeyRunIdentity.childProcessAlive = $false
        }
        $preKeyOwnership = Get-C188R11R4QacOwnershipEvidence $preKeyQacStatus $preKeyQacList $preKeyInspection $preKeyRunIdentity
        if ($preKeyQacStatusExitCode -ne 0 -or $preKeyQacListExitCode -ne 0 -or
            $preKeyQacInspectExitCode -ne 0) {
            $preKeyOwnership.accepted = $false
            $preKeyOwnership.reasons = @($preKeyOwnership.reasons) + 'qac-prekey-command-failed'
        }
        $preKeyOwnership | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $preKeyOwnershipPath -Encoding utf8
        if (-not [bool]$preKeyOwnership.accepted -or
            [string]$preKeyOwnership.reservationId -ne [string]$inputOwnership.reservationId -or
            [string]$preKeyOwnership.runIdentityId -ne $runIdentityId) {
            throw "QAC ownership proof failed immediately before key injection: $($preKeyOwnership.reasons -join ', '); no key event was sent."
        }

        # This is the only keyboard transaction in the one authorized guest:
        # QMP qcode a make followed by its matching break.
        $qmpActions.Add((Invoke-C188R11R2Control @('key','a') 'r2-qmp-key-a-down-up'))
        $inputSent = $true
        $keyRenderDeadline = (Get-Date).AddSeconds(8)
        do {
            $serialText = Get-Content -LiteralPath $serialPath -Raw
            $afterKeySnapshot = Get-C188R11R4R2StageSnapshot $serialText 'after-key'
            if ($null -ne $afterKeySnapshot -and $afterKeySnapshot.render) { break }
            Start-Sleep -Milliseconds 50
        } while ((Get-Date) -lt $keyRenderDeadline)
        if ($null -ne $afterKeySnapshot -and $afterKeySnapshot.render) {
            Start-Sleep -Milliseconds 250
        }
        $qmpActions.Add((Invoke-C188R11R2Control @('capture','after-input') 'r2-qmp-capture-after-input'))
    }
    $qmpActions | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootRoot 'r2-qmp-actions.json') -Encoding utf8
    [void](Invoke-C188R11R2Control @('quit') 'r2-qmp-owned-quit')
    $shutdownRequested = $true
    if (-not $qacRunner.WaitForExit(90000)) {
        throw 'QAC did not release the targeted guest after its owned QMP quit.'
    }
    $runnerExitCode = $qacRunner.ExitCode
} catch {
    if (-not $qacRunner.HasExited -and (Test-Path -LiteralPath $activePath -PathType Leaf)) {
        try {
            [void](Invoke-C188R11R2Control @('quit') 'r2-qmp-owned-quit-after-error')
            $shutdownRequested = $true
        } catch { }
        [void]$qacRunner.WaitForExit(90000)
    }
    if ($qacRunner.HasExited) { $runnerExitCode = $qacRunner.ExitCode }
    throw
} finally {
    if ($gdbProcess -and -not $gdbProcess.HasExited) {
        [void]$gdbProcess.WaitForExit(5000)
        if (-not $gdbProcess.HasExited) {
            Stop-Process -Id $gdbProcess.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($qacRunner.HasExited) {
        Set-Content -LiteralPath $controllerStdout -Value $stdoutTask.GetAwaiter().GetResult() -Encoding utf8
        Set-Content -LiteralPath $controllerStderr -Value $stderrTask.GetAwaiter().GetResult() -Encoding utf8
    }
}

$serialText = if (Test-Path -LiteralPath $serialPath) {
    Get-Content -LiteralPath $serialPath -Raw
} else { '' }
$qacStderrPath = Join-Path $bootRoot 'qac.stderr.log'
$qacStderr = if (Test-Path -LiteralPath $qacStderrPath) {
    [GuideXos.C188.LiveProcessCapture]::ReadAllTextShared($qacStderrPath)
} else { '' }
$reservation = [regex]::Match($qacStderr, 'QAC reservation requested: id=([0-9a-f]+)')
$released = [regex]::Match($qacStderr, 'QAC reservation released: id=([0-9a-f]+)')
$qemuChild = [regex]::Match($qacStderr,
    'QAC QEMU exited: reservation=([0-9a-f]+) pid=(\d+) exitCode=(\d+)')
$qacAfterStatusPath = Join-Path $bootRoot 'qac-after.status.txt'
$qacAfterListPath = Join-Path $bootRoot 'qac-after.list.txt'
$qacAfterStatus = if (Test-Path -LiteralPath $qacAfterStatusPath) {
    Get-Content -LiteralPath $qacAfterStatusPath -Raw
} else { '' }
$qacAfterList = if (Test-Path -LiteralPath $qacAfterListPath) {
    Get-Content -LiteralPath $qacAfterListPath -Raw
} else { '' }
$releaseVerified = $shutdownRequested -and $reservation.Success -and
    $released.Success -and $qemuChild.Success -and
    $reservation.Groups[1].Value -eq $released.Groups[1].Value -and
    $reservation.Groups[1].Value -eq $qemuChild.Groups[1].Value -and
    $qemuChild.Groups[3].Value -eq '0' -and
    $qacAfterStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and
    $qacAfterList -match 'Reservations:\s+0' -and
    $qacAfterList -notmatch [regex]::Escape($reservation.Groups[1].Value)
$ownedQemuExited = $qemuChild.Success -and
    -not (Get-Process -Id ([int]$qemuChild.Groups[2].Value) -ErrorAction SilentlyContinue)
$trace = Get-C188R11R2KeyboardEvidence $serialText
$notesTextEntryEvidence = Get-C188R11R4R2NotesTextEntryEvidence $serialText `
    ([bool]($trace.traceComplete -and $trace.notesCallbackEntry -and
        $trace.notesCallbackReturn -and $trace.notesCallbackReturnResult -eq 0))
$memoryInitialization = Get-C188R11MemorySnapshot $serialText 'registry-init'
$memoryLaunch = Get-C188R11MemorySnapshot $serialText 'managed-return'
$memoryInput = Get-C188R11MemorySnapshot $serialText 'input-dispatch-entry'
$memoryStorageStable = Test-C188R11MemoryStorageStable $serialText
$memoryStorageOwnedByPersistentHeap =
    Test-C188R11MemoryStorageInPersistentHeap $serialText
$memoryStorageLocalized = $false
if ($memoryInitialization.Success -and $memoryLaunch.Success -and
    $memoryInput.Success -and $trace.registryCount -eq 0 -and
    $trace.residentMask -eq 0) {
    $memoryStorageLocalized =
        $memoryInitialization.Groups['registry'].Value -eq $memoryLaunch.Groups['registry'].Value -and
        $memoryLaunch.Groups['registry'].Value -eq $memoryInput.Groups['registry'].Value -and
        $memoryInitialization.Groups['owner'].Value -eq $memoryLaunch.Groups['owner'].Value -and
        $memoryLaunch.Groups['owner'].Value -eq $memoryInput.Groups['owner'].Value -and
        $memoryInitialization.Groups['slot'].Value -eq $memoryLaunch.Groups['slot'].Value -and
        $memoryLaunch.Groups['slot'].Value -eq $memoryInput.Groups['slot'].Value -and
        $memoryInitialization.Groups['gc'].Value -eq $memoryLaunch.Groups['gc'].Value -and
        $memoryLaunch.Groups['gc'].Value -eq $memoryInput.Groups['gc'].Value -and
        $memoryInput.Groups['live'].Value -eq '0000000000000000' -and
        $memoryInput.Groups['held'].Value -eq $memoryLaunch.Groups['live'].Value -and
        $memoryInput.Groups['heldLen'].Value -eq '5'
}
$gdbText = if (Test-Path -LiteralPath $gdbStdoutPath -PathType Leaf) {
    [System.IO.File]::ReadAllText($gdbStdoutPath)
} else { '' }
$gdbErrorText = if (Test-Path -LiteralPath $gdbStderrPath -PathType Leaf) {
    [System.IO.File]::ReadAllText($gdbStderrPath)
} else { '' }
$watchpointHits = [regex]::Matches($gdbText,
    '(?m)^\[C188R11R4-WATCH\] hit=\d+ field=(?:registry-length|pool-entries)')
if ($MemoryWatchpoints -and $watchpointStatus -eq 'ARMED') {
    $watchpointStatus = if ($watchpointHits.Count -gt 0) {
        'ARMED_HITS_CAPTURED'
    } else { 'ARMED_NO_HITS' }
}
$baseManifestPath = Join-Path $bootRoot 'acceptance-manifest.json'
$baseManifest = if (Test-Path -LiteralPath $baseManifestPath) {
    Get-Content -LiteralPath $baseManifestPath -Raw | ConvertFrom-Json
} else { $null }
$degradedCensus = $qacStderr -match 'reservation .* is degraded.*QEMU process census is incomplete'
$rootCauseFixedAndQualified = $trace.traceComplete -and
    $trace.registryCount -eq 5 -and $trace.residentMask -eq 1 -and
    $trace.lookupResult -eq 0 -and $trace.notesCallbackEntry -and
    $memoryStorageStable -and $memoryStorageOwnedByPersistentHeap -and
    $baseManifest -and $baseManifest.result -eq 'PASS' -and
    [bool]$inputOwnership.accepted -and [bool]$baseManifest.qacOwnershipProofAccepted -and
    $releaseVerified -and $ownedQemuExited -and $runnerExitCode -eq 0
$outcome = if ($rootCauseFixedAndQualified) {
    'ROOT_CAUSE_FIXED_AND_QUALIFIED'
} elseif ($memoryStorageLocalized) {
    'MEMORY_FAILURE_LOCALIZED'
} elseif ($watchpointHits.Count -gt 0) {
    'ROOT_CAUSE_PROVEN'
} else {
    'BLOCKED_OR_INCONCLUSIVE'
}
$manifest = [ordered]@{
    outcome=$outcome
    inputSent=$inputSent
    selectedInput='QMP qcode a down/up; Set-2 scancode 0x1C maps to ASCII a'
    shiftExpectedManagedCallback=$false
    focusVerifiedBeforeInput=$focusVerified
    editorFocusVerified=$editorFocusVerified
    notesWindowActiveAtInput=[bool]$focusEvidence.notesWindowActive
    documentControlRouteEstablished=[bool]$focusEvidence.documentControlRouteEstablished
    documentRouteCorrelatedToPointer=[bool]$focusEvidence.documentRouteCorrelatedToPointer
    editableDocumentWidgetFocused=[bool]$focusEvidence.editableDocumentWidgetFocused
    unrelatedControlFocused=[bool]$focusEvidence.unrelatedControlFocused
    unsupportedRouteObserved=[bool]$focusEvidence.unsupportedRouteObserved
    focusUnknown=[bool]$focusEvidence.focusUnknown
    focusState=[string]$focusEvidence.focusState
    focusEvidence=$focusEvidence
    editorFocusFailureReason=$editorFocusFailureReason
    editorFocusScreenPoint=$editorFocusScreenPoint
    pointerClearScreenPoint=$pointerClearScreenPoint
    notesTextEntry=$notesTextEntryEvidence
    qacPreAdmissionExclusive=($beforeStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and $beforeList -match 'Reservations:\s+0')
    runIdentityId=$runIdentityId
    qacOwnedAcceptanceRunning=[bool]$inputOwnership.accepted
    qacOwnershipEvidence=$inputOwnership
    qacOwnershipEvidencePath=(Join-Path $bootRoot 'qac-input-ownership.json')
    qacPreKeyOwnershipAccepted=[bool]($null -ne $preKeyOwnership -and $preKeyOwnership.accepted)
    qacPreKeyOwnershipEvidence=$preKeyOwnership
    qacPreKeyStatusPath=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qac-prekey.status.txt')) { Join-Path $bootRoot 'qac-prekey.status.txt' } else { '' })
    qacPreKeyListPath=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qac-prekey.list.txt')) { Join-Path $bootRoot 'qac-prekey.list.txt' } else { '' })
    qacPreKeyInspectionPath=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qac-prekey.inspect.json')) { Join-Path $bootRoot 'qac-prekey.inspect.json' } else { '' })
    qacReservationId=[string]$inputOwnership.reservationId
    qemuPid=$(if ($qemuChild.Success) { $qemuChild.Groups[2].Value } else { '' })
    qacReleaseVerified=[bool]$releaseVerified
    ownedQemuExited=[bool]$ownedQemuExited
    qacDegradedProcessCensusWarning=[bool]$degradedCensus
    trace=$trace
    memorySnapshots=[ordered]@{
        registryInitialization=$(if ($memoryInitialization.Success) { $memoryInitialization.Value } else { $null })
        managedLaunchReturn=$(if ($memoryLaunch.Success) { $memoryLaunch.Value } else { $null })
        firstInput=$(if ($memoryInput.Success) { $memoryInput.Value } else { $null })
    }
    memoryFailureLocalized=[bool]$memoryStorageLocalized
    memoryStorageStable=[bool]$memoryStorageStable
    memoryStorageOwnedByPersistentHeap=[bool]$memoryStorageOwnedByPersistentHeap
    rootCauseFixedAndQualified=[bool]$rootCauseFixedAndQualified
    memoryWatchpointsRequested=[bool]$MemoryWatchpoints
    memoryWatchpointStatus=$watchpointStatus
    memoryWatchpointHitCount=$watchpointHits.Count
    gdbScriptPath=$(if (Test-Path -LiteralPath $gdbScriptPath) { $gdbScriptPath } else { '' })
    gdbStdoutPath=$(if (Test-Path -LiteralPath $gdbStdoutPath) { $gdbStdoutPath } else { '' })
    gdbStdoutSha256=$(if (Test-Path -LiteralPath $gdbStdoutPath) { (Get-FileHash -LiteralPath $gdbStdoutPath -Algorithm SHA256).Hash } else { '' })
    gdbStderrPath=$(if (Test-Path -LiteralPath $gdbStderrPath) { $gdbStderrPath } else { '' })
    gdbStderrSha256=$(if (Test-Path -LiteralPath $gdbStderrPath) { (Get-FileHash -LiteralPath $gdbStderrPath -Algorithm SHA256).Hash } else { '' })
    gdbStderr=$gdbErrorText
    notesCallbackReached=[bool]$trace.notesCallbackEntry
    registryCount=$trace.registryCount
    residentMask=$trace.residentMask
    registryPopulated=$trace.registryPopulated
    registryLookupResult=$trace.lookupResult
    serialPath=$serialPath
    serialSha256=$(if (Test-Path -LiteralPath $serialPath) { (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash } else { '' })
    qmpLogPath=(Join-Path $bootRoot 'qmp-control.log')
    qmpLogSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qmp-control.log')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'qmp-control.log') -Algorithm SHA256).Hash } else { '' })
    initialScreenshot=(Join-Path $bootRoot 'initial-notes.png')
    editorFocusedScreenshot=(Join-Path $bootRoot 'editor-focused.png')
    beforeScreenshot=(Join-Path $bootRoot 'before-input.png')
    afterScreenshot=(Join-Path $bootRoot 'after-input.png')
    initialScreenshotSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'initial-notes.png')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'initial-notes.png') -Algorithm SHA256).Hash } else { '' })
    editorFocusedScreenshotSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'editor-focused.png')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'editor-focused.png') -Algorithm SHA256).Hash } else { '' })
    beforeScreenshotSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'before-input.png')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'before-input.png') -Algorithm SHA256).Hash } else { '' })
    afterScreenshotSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'after-input.png')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'after-input.png') -Algorithm SHA256).Hash } else { '' })
    qacBaseManifest=$baseManifestPath
    runnerExitCode=$runnerExitCode
}
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $controllerManifest -Encoding utf8
Write-Output "C188R11R4_MEMORY=$outcome INPUT_SENT=$inputSent MANAGED_STATE=$($trace.managedStateQualified) CALLBACK=$($trace.notesCallbackEntry) REGISTRY_COUNT=$($trace.registryCount) RESIDENT_MASK=$($trace.residentMask) MEMORY_STABLE=$memoryStorageStable WATCHPOINTS=$watchpointStatus HITS=$($watchpointHits.Count) QAC_RELEASE=$releaseVerified QAC_DEGRADED=$degradedCensus"
Write-Output "C188R11R4R2_NOTES_TEXT_ENTRY=$($notesTextEntryEvidence.accepted) WINDOW_ACTIVE=$($focusEvidence.notesWindowActive) C120_ROUTE=$($focusEvidence.documentControlRouteEstablished) EDITABLE_WIDGET=$($focusEvidence.editableDocumentWidgetFocused) FOCUS_STATE=$($focusEvidence.focusState) BUFFER_INSERTED=$($notesTextEntryEvidence.oneCharacterInserted) REDRAW=$($notesTextEntryEvidence.redrawCompleted) QAC_PREKEY=$($null -ne $preKeyOwnership -and $preKeyOwnership.accepted)"
Write-Output "C188R11R2_KEYBOARD_MANIFEST=$controllerManifest"
if (-not $releaseVerified -or -not $ownedQemuExited) {
    throw 'The owned C188R11R2 QAC process or reservation did not complete verified shutdown.'
}
