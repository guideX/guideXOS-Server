param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter()][ValidateRange(60,300)][int]$MaxRuntimeSeconds = 120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R12R1 Settings handoff runner requires PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r11-managed-state-provenance'))
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$bootRoot = [System.IO.Path]::GetFullPath($BootEvidencePath)
foreach ($candidatePath in @($candidate,$bootRoot)) {
    $relative = [System.IO.Path]::GetRelativePath($phaseRoot,$candidatePath)
    if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
        [System.IO.Path]::IsPathRooted($relative)) {
        throw "C188R12R1 output must be a unique directory below $phaseRoot."
    }
}
if (-not (Test-Path -LiteralPath $candidate -PathType Container)) { throw "Candidate root is missing: $candidate" }
if (Test-Path -LiteralPath $bootRoot) { throw "Guest evidence root already exists: $bootRoot" }

$ownershipHelpers = Join-Path $PSScriptRoot 'c188r11qac-ownership-predicates.ps1'
. (Join-Path $PSScriptRoot 'c188r11-runner-predicates.ps1')
. (Join-Path $PSScriptRoot 'c188r11r4r2-runner-predicates.ps1')
. $ownershipHelpers
. (Join-Path $PSScriptRoot 'c188r12r1-settings-handoff-predicates.ps1')

$candidateEsp = Join-Path $candidate 'esp'
$stagingManifestPath = Join-Path $candidate 'ramdisk-staging.json'
foreach ($required in @(
    (Join-Path $candidateEsp 'kernel.elf'),
    (Join-Path $candidateEsp 'ramdisk.img'),
    (Join-Path $candidateEsp 'EFI\BOOT\BOOTX64.EFI'),
    $stagingManifestPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Validated C188R12R1 candidate input is missing: $required"
    }
}
$stagingManifest = Get-Content -LiteralPath $stagingManifestPath -Raw | ConvertFrom-Json
if ($stagingManifest.result -ne 'PASS' -or
    (Get-FileHash -LiteralPath (Join-Path $candidateEsp 'kernel.elf') -Algorithm SHA256).Hash -ne $ExpectedKernelSha256.ToUpperInvariant() -or
    (Get-FileHash -LiteralPath (Join-Path $candidateEsp 'ramdisk.img') -Algorithm SHA256).Hash -ne $stagingManifest.ramdiskSha256.ToUpperInvariant()) {
    throw 'Candidate staging manifest or kernel/ramdisk hashes do not match the qualified inputs.'
}
$sourceMediaHashes = [ordered]@{}
foreach ($relativeMedia in @('kernel.elf','ramdisk.img','EFI\BOOT\BOOTX64.EFI')) {
    $sourceMediaHashes[$relativeMedia.Replace('\','/')] =
        (Get-FileHash -LiteralPath (Join-Path $candidateEsp $relativeMedia) -Algorithm SHA256).Hash
}
$null = New-Item -ItemType Directory -Path $bootRoot
$runEsp = Join-Path $bootRoot 'esp'
Copy-Item -LiteralPath $candidateEsp -Destination $runEsp -Recurse
$copiedMediaHashes = [ordered]@{}
foreach ($relativeMedia in @('kernel.elf','ramdisk.img','EFI\BOOT\BOOTX64.EFI')) {
    $normalized = $relativeMedia.Replace('\','/')
    $copiedMediaHashes[$normalized] =
        (Get-FileHash -LiteralPath (Join-Path $runEsp $relativeMedia) -Algorithm SHA256).Hash
    if ($copiedMediaHashes[$normalized] -ne $sourceMediaHashes[$normalized]) {
        throw "QAC candidate copy changed $normalized."
    }
}
$runIdentityId = [guid]::NewGuid().ToString('N')
$runManifestPath = Join-Path $bootRoot 'r12r1-input-manifest.json'
[ordered]@{
    phase='C188R12R1-settings-handoff';runIdentityId=$runIdentityId
    candidateRoot=$candidate;stagingManifest=$stagingManifestPath
    stagingManifestSha256=(Get-FileHash -LiteralPath $stagingManifestPath -Algorithm SHA256).Hash
    expectedKernelSha256=$ExpectedKernelSha256.ToUpperInvariant()
    sourceMediaHashes=$sourceMediaHashes;copiedMediaHashes=$copiedMediaHashes
    originalRamdiskSha256=$stagingManifest.ramdiskSha256.ToUpperInvariant()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $runManifestPath -Encoding utf8

$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$control = Join-Path $PSScriptRoot 'c188-qmp-control.py'
$cursorFinder = Join-Path $PSScriptRoot 'find-c188r12r1-cursor.py'
$compare = Join-Path $PSScriptRoot 'compare-c188r12r1-screenshots.py'
$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1
$pwsh = Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1
$activePath = Join-Path $bootRoot 'active.json'
$serialPath = Join-Path $bootRoot 'serial.log'
$qmpControlPath = Join-Path $bootRoot 'qmp-control.log'
$baseRunnerPath = Join-Path $PSScriptRoot 'run-c188-qac-guest.ps1'
$qacStdoutPath = Join-Path $bootRoot 'r12r1-qac-runner.stdout.log'
$qacStderrPath = Join-Path $bootRoot 'r12r1-qac-runner.stderr.log'
$preInputOwnership = $null
$preCheckpointOwnership = $null
$preSettingsCheckpointOwnership = $null
$preShutdownOwnership = $null
$preClickOwnership = $null
$qacRunner = $null
$qacRunnerExitCode = $null
$clickAttempted = $false
$clickExitCode = $null
$clickTarget = $null
$pointerPositionEvidence = @()
$pointerTargetEvidence = $null
$runnerInputEvidence = $null
$ready = $false
$qmpQuitRequested = $false
$readinessTimeout = $false
$ownershipDegraded = $false
$runnerStdout = ''
$runnerStderr = ''
$settingsEvidence = $null
$failureText = ''

function Read-C188R12R1SharedText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = [System.IO.FileStream]::new($Path,[System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::UTF8,$true,4096,$true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Get-C188R12R1FreshQacOwnership([string]$Stage) {
    $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
    $reasons = [System.Collections.Generic.List[string]]::new()
    if (-not [bool]$active.qacAdmissionConfirmed -or
        [string]$active.runIdentityId -ne $runIdentityId -or
        [string]$active.bootEvidencePath -ne $bootRoot -or
        [int]$active.pid -ne [int]$active.qacOwnerProcessId -or
        [int]$active.qacQemuChildPid -le 0 -or
        [string]::IsNullOrWhiteSpace([string]$active.qacReservationId) -or
        $null -eq $active.qacOwnershipRunIdentity) {
        return [ordered]@{accepted=$false;reasons=@('active-run-identity-missing-or-contradictory');stage=$Stage}
    }
    $storedIdentity = $active.qacOwnershipRunIdentity
    $status = (& $qacCli status 2>&1 | Out-String)
    $statusExit = $LASTEXITCODE
    $list = (& $qacCli list 2>&1 | Out-String)
    $listExit = $LASTEXITCODE
    $inspectText = (& $qacCli inspect ([string]$active.qacReservationId) 2>&1 | Out-String)
    $inspectExit = $LASTEXITCODE
    foreach ($kind in @('status','list','inspect')) {
        $path = Join-Path $bootRoot ("qac-{0}-{1}.{2}" -f $Stage,$kind,($kind -eq 'inspect' ? 'json' : 'txt'))
        if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite QAC ownership evidence: $path" }
        $value = switch ($kind) { 'status' {$status}; 'list' {$list}; 'inspect' {$inspectText} }
        Set-Content -LiteralPath $path -Value $value -Encoding utf8
    }
    if ($statusExit -ne 0 -or $listExit -ne 0 -or $inspectExit -ne 0) {
        return [ordered]@{accepted=$false;reasons=@('qac-status-list-or-inspect-failed');stage=$Stage}
    }
    try { $inspection = $inspectText | ConvertFrom-Json -ErrorAction Stop }
    catch { return [ordered]@{accepted=$false;reasons=@('qac-inspection-json-invalid');stage=$Stage} }
    try {
        $ownerId = [int]$storedIdentity.ownerProcessId
        $runnerId = [int]$storedIdentity.runnerProcessId
        $childId = [int]$storedIdentity.childProcessId
        $ownerProcess = Get-Process -Id $ownerId -ErrorAction Stop
        $runnerProcess = Get-Process -Id $runnerId -ErrorAction Stop
        $childProcess = Get-Process -Id $childId -ErrorAction Stop
        $ownerCim = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $ownerId" -ErrorAction Stop
        $childCim = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $childId" -ErrorAction Stop
    } catch {
        return [ordered]@{accepted=$false;reasons=@('owner-runner-or-qemu-process-identity-unavailable');stage=$Stage}
    }
    $identity = [ordered]@{}
    foreach ($property in $storedIdentity.PSObject.Properties) {
        $identity[$property.Name] = $property.Value
    }
    $identity.ownerProcessId = $ownerId
    $identity.ownerProcessAlive = -not $ownerProcess.HasExited
    $identity.ownerProcessStartTimeUtc = $ownerProcess.StartTime.ToUniversalTime().ToString('O')
    $identity.ownerParentProcessId = [int]$ownerCim.ParentProcessId
    $identity.runnerProcessId = $runnerId
    $identity.runnerProcessAlive = -not $runnerProcess.HasExited
    $identity.runnerProcessStartTimeUtc = $runnerProcess.StartTime.ToUniversalTime().ToString('O')
    $identity.childProcessId = $childId
    $identity.childProcessAlive = -not $childProcess.HasExited
    $identity.childProcessStartTimeUtc = $childProcess.StartTime.ToUniversalTime().ToString('O')
    $identity.childParentProcessId = [int]$childCim.ParentProcessId
    $identity.childExecutableIdentity = [string]$childCim.ExecutablePath
    $identity.expectedQemuExecutableIdentity = [System.IO.Path]::GetFullPath('C:\Program Files\qemu\qemu-system-x86_64.exe')
    $ownership = Get-C188R11R4QacOwnershipEvidence $status $list $inspection $identity
    $ownership['stage'] = $Stage
    $ownershipPath = Join-Path $bootRoot "qac-$Stage-ownership.json"
    if (Test-Path -LiteralPath $ownershipPath) { throw "Refusing to overwrite QAC ownership evidence: $ownershipPath" }
    $ownership | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ownershipPath -Encoding utf8
    return $ownership
}

function Invoke-C188R12R1Control([string]$Name, [string[]]$Arguments) {
    $output = (& $python.Source $control '--active' $activePath @Arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $logPath = Join-Path $bootRoot ($Name + '.log')
    if (Test-Path -LiteralPath $logPath) { throw "Refusing to overwrite QMP evidence: $logPath" }
    Set-Content -LiteralPath $logPath -Value $output -Encoding utf8
    return [ordered]@{name=$Name;exitCode=$exitCode;output=$output;log=$logPath}
}

function Invoke-C188R12R1PointerProbe([string]$Name, [int]$TargetX, [int]$TargetY) {
    $capture = Invoke-C188R12R1Control ("capture-" + $Name) @('capture',$Name)
    $ppmPath = Join-Path $bootRoot ($Name + '.ppm')
    if ($capture.exitCode -ne 0 -or -not (Test-Path -LiteralPath $ppmPath -PathType Leaf)) {
        return [ordered]@{found=$false;matches=0;error='pointer-probe-capture-failed';capture=$capture}
    }
    $finderOutput = (& $python.Source $cursorFinder $ppmPath $TargetX $TargetY '128' 2>&1 | Out-String)
    $finderExitCode = $LASTEXITCODE
    $position = $null
    try { $position = $finderOutput | ConvertFrom-Json -ErrorAction Stop } catch { }
    if ($null -eq $position) {
        $position = [ordered]@{
            found=$false;matches=0;error='cursor-finder-output-invalid'
            finderExitCode=$finderExitCode;finderOutput=$finderOutput.Trim()
        }
    }
    $position | Add-Member -NotePropertyName probeName -NotePropertyValue $Name -Force
    $position | Add-Member -NotePropertyName ppmPath -NotePropertyValue $ppmPath -Force
    $position | Add-Member -NotePropertyName finderExitCode -NotePropertyValue $finderExitCode -Force
    $evidencePath = Join-Path $bootRoot ($Name + '-cursor.json')
    if (Test-Path -LiteralPath $evidencePath) { throw "Refusing to overwrite cursor evidence: $evidencePath" }
    $position | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $evidencePath -Encoding utf8
    return $position
}

try {
    if (-not (Test-Path -LiteralPath $qacCli -PathType Leaf)) { throw "QAC CLI is unavailable: $qacCli" }
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $pwsh.Source
    $psi.WorkingDirectory = $repo
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($argument in @(
        '-NoProfile','-File',$baseRunnerPath,
        '-BootEvidencePath',$bootRoot,
        '-ExpectedKernelSha256',$ExpectedKernelSha256,
        '-Scenario','settings-handoff-diagnostic',
        '-MaxRuntimeSeconds',[string]$MaxRuntimeSeconds,
        '-BootTimeoutSeconds','180','-AdmissionTimeoutSeconds','60',
        '-RunIdentityId',$runIdentityId)) {
        [void]$psi.ArgumentList.Add([string]$argument)
    }
    $qacRunner = [System.Diagnostics.Process]::Start($psi)
    if (-not $qacRunner) { throw 'Failed to start the bounded C188R12R1 QAC guest runner.' }
    $stdoutTask = $qacRunner.StandardOutput.ReadToEndAsync()
    $stderrTask = $qacRunner.StandardError.ReadToEndAsync()

    $readyDeadline = (Get-Date).AddSeconds(240)
    do {
        if ($qacRunner.HasExited) { break }
        if ((Test-Path -LiteralPath $activePath -PathType Leaf) -and
            (Test-Path -LiteralPath $serialPath -PathType Leaf)) {
            try {
                $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
                $serialText = Read-C188R12R1SharedText $serialPath
                $notesFocus = Get-C188R11R4R2NotesFocusEvidence $serialText
                if ([int]$active.screenWidth -gt 0 -and
                    [int]$active.screenHeight -gt 0 -and
                    (Test-C188R11NotesReady $serialText) -and
                    [bool]$notesFocus.notesWindowActive) {
                    $ready = $true
                    break
                }
            } catch { }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $readyDeadline)

    if (-not $ready) {
        $readinessTimeout = -not $qacRunner.HasExited
        $failureText = if ($qacRunner.HasExited) { 'guest-runner-exited-before-Notes-readiness' } else { 'Notes-readiness-timeout' }
    } else {
        $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
        $serialText = Read-C188R12R1SharedText $serialPath
        $readinessFailure = Get-C188R11ReadinessFailureReason $serialText
        if (-not [string]::IsNullOrWhiteSpace($readinessFailure)) {
            throw "C188R11 read-only readiness audit failed: $readinessFailure"
        }
        if ($serialText -match '(?m)^\[APPMODEL-MANAGED-LAUNCH\] source=DesktopLaunch appId=com\.guidexos\.apps\.managed\.settingscenter') {
            throw 'Settings had already been requested before the sole source-validated click.'
        }
        $clickTarget = Get-C188R12R1NotesSettingsClickTarget $serialText $repo `
            ([int]$active.screenWidth) ([int]$active.screenHeight)
        if (-not [bool]$clickTarget.accepted) {
            $failureText = "source-validated Settings target unavailable: $($clickTarget.reason)"
        } else {
            $preCheckpointOwnership = Get-C188R12R1FreshQacOwnership 'pre-click-capture'
            if (-not [bool]$preCheckpointOwnership.accepted) {
                $ownershipDegraded = $true
                $failureText = 'QAC ownership was not current and exclusive before the pre-transition capture.'
            } else {
                $beforeCapture = Invoke-C188R12R1Control 'capture-desktop-ready' @('capture','before-settings')
                if ($beforeCapture.exitCode -ne 0) { throw "Pre-transition screenshot capture failed: $($beforeCapture.output)" }
                $qmpBeforeClick = Read-C188R12R1SharedText $qmpControlPath
                $beforeClickCheck = Test-C188R12R1SingleClickQmpEvidence $qmpBeforeClick
                if ($beforeClickCheck.leftButtonDownCount -ne 0 -or
                    $beforeClickCheck.leftButtonUpCount -ne 0 -or
                    $beforeClickCheck.keyEventCount -ne 0) {
                    throw 'QMP log already contains a button or key event before the single Settings action.'
                }
                $preInputOwnership = Get-C188R12R1FreshQacOwnership 'pre-input'
                if (-not [bool]$preInputOwnership.accepted) {
                    $ownershipDegraded = $true
                    $failureText = 'QAC ownership was not current and exclusive at the input gate.'
                } else {
                    $placementOwnership = Get-C188R12R1FreshQacOwnership 'pre-pointer-placement'
                    if (-not [bool]$placementOwnership.accepted) {
                        $ownershipDegraded = $true
                        $failureText = 'QAC ownership was not current and exclusive before pointer placement.'
                    } else {
                        $placement = Invoke-C188R12R1Control 'place-settings-pointer' @(
                            'move',[string]$clickTarget.x,[string]$clickTarget.y)
                        if ($placement.exitCode -ne 0) {
                            $failureText = "source-derived Settings pointer placement failed: $($placement.output)"
                        } else {
                            $pointerPositionEvidence += Invoke-C188R12R1PointerProbe 'settings-pointer-probe-001' `
                                ([int]$clickTarget.x) ([int]$clickTarget.y)
                            $pointerPosition = $pointerPositionEvidence[-1]
                            $pointerTargetEvidence = Test-C188R12R1PointerOnSettingsButton $pointerPosition $clickTarget

                            if (-not [bool]$pointerTargetEvidence.accepted -and
                                [bool]$pointerPosition.found -and
                                $null -ne $pointerPosition.x -and $null -ne $pointerPosition.y) {
                                $correctionOwnership = Get-C188R12R1FreshQacOwnership 'pre-pointer-correction'
                                if ([bool]$correctionOwnership.accepted) {
                                    $correction = Invoke-C188R12R1Control 'correct-settings-pointer-once' @(
                                        'move',[string]$clickTarget.x,[string]$clickTarget.y,
                                        '--from-x',[string]$pointerPosition.x,
                                        '--from-y',[string]$pointerPosition.y)
                                    if ($correction.exitCode -eq 0) {
                                        $pointerPositionEvidence += Invoke-C188R12R1PointerProbe 'settings-pointer-probe-002' `
                                            ([int]$clickTarget.x) ([int]$clickTarget.y)
                                        $pointerPosition = $pointerPositionEvidence[-1]
                                        $pointerTargetEvidence = Test-C188R12R1PointerOnSettingsButton `
                                            $pointerPosition $clickTarget
                                    } else {
                                        $failureText = "bounded pointer correction failed: $($correction.output)"
                                    }
                                } else {
                                    $ownershipDegraded = $true
                                    $failureText = 'QAC ownership was not current and exclusive before the bounded pointer correction.'
                                }
                            }
                        }
                    }

                    if (-not $ownershipDegraded -and [bool]$pointerTargetEvidence.accepted) {
                        $preClickOwnership = Get-C188R12R1FreshQacOwnership 'pre-click'
                        $qmpBeforeClick = Read-C188R12R1SharedText $qmpControlPath
                        $beforeClickCheck = Test-C188R12R1SingleClickQmpEvidence $qmpBeforeClick
                        if (-not [bool]$preClickOwnership.accepted) {
                            $ownershipDegraded = $true
                            $failureText = 'QAC ownership was not current and exclusive at the verified Settings click gate.'
                        } elseif ($beforeClickCheck.leftButtonDownCount -ne 0 -or
                            $beforeClickCheck.leftButtonUpCount -ne 0 -or
                            $beforeClickCheck.keyEventCount -ne 0) {
                            throw 'QMP log contains a prior button or key event before the verified Settings click.'
                        } else {
                            [ordered]@{
                                clickAttempted=$true;clickCount=1;keyInputCount=0
                                sourceValidated=$true;pointerPositionVerified=$true
                                pointerX=[int]$pointerPosition.x;pointerY=[int]$pointerPosition.y
                                button='Managed Notes Settings'
                                screenX=[int]$clickTarget.x;screenY=[int]$clickTarget.y
                                pointerProbes=$pointerPositionEvidence.Count
                                notesWindow=[string]$clickTarget.window
                                qacReservationId=[string]$active.qacReservationId
                                qacQemuPid=[int]$active.qacQemuChildPid
                            } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootRoot 'r12r1-interaction-plan.json') -Encoding utf8
                            $clickAttempted = $true
                            $clickResult = Invoke-C188R12R1Control 'click-settings-once' @('click-current')
                            $clickExitCode = [int]$clickResult.exitCode
                            if ($clickExitCode -ne 0) { $failureText = "single verified Settings click command failed: $($clickResult.output)" }

                            $settingsDeadline = (Get-Date).AddSeconds(30)
                            do {
                                $serialText = Read-C188R12R1SharedText $serialPath
                                $settingsEvidence = Get-C188R12R1SettingsHandoffEvidence $serialText
                                if ([bool]$settingsEvidence.accepted) { break }
                                if ($qacRunner.HasExited) { break }
                                Start-Sleep -Milliseconds 200
                            } while ((Get-Date) -lt $settingsDeadline)

                            $preSettingsCheckpointOwnership = Get-C188R12R1FreshQacOwnership 'pre-settings-checkpoint'
                            if ([bool]$preSettingsCheckpointOwnership.accepted) {
                                $checkpoint = Invoke-C188R12R1Control 'capture-settings-checkpoint' @('capture','settings-check')
                                if ($checkpoint.exitCode -ne 0) { $failureText = "Settings checkpoint capture failed: $($checkpoint.output)" }
                            } else {
                                $ownershipDegraded = $true
                                $failureText = 'QAC ownership degraded after the click; no screenshot or further QMP command was sent.'
                            }
                        }
                    } elseif (-not $ownershipDegraded -and -not $failureText) {
                        $failureText = 'Observed pointer did not reach the source-defined Settings button after one bounded correction; no click was sent.'
                    }

                    if (-not $ownershipDegraded) {
                        $preShutdownOwnership = Get-C188R12R1FreshQacOwnership 'pre-shutdown'
                        if ([bool]$preShutdownOwnership.accepted) {
                            $quitResult = Invoke-C188R12R1Control 'qac-owned-guest-shutdown' @('quit')
                            $qmpQuitRequested = [int]$quitResult.exitCode -eq 0
                            if (-not $qmpQuitRequested) { $failureText = "QMP quit request failed: $($quitResult.output)" }
                        } else {
                            $ownershipDegraded = $true
                            $failureText = 'QAC ownership degraded before shutdown; bounded guest-runner cleanup remains in control.'
                        }
                    }
                }
            }
        }
    }

    if ($null -ne $qacRunner) {
        $runnerDeadline = (Get-Date).AddSeconds($MaxRuntimeSeconds + 210)
        while (-not $qacRunner.HasExited -and (Get-Date) -lt $runnerDeadline) {
            Start-Sleep -Milliseconds 500
        }
        if (-not $qacRunner.HasExited) {
            $failureText = if ($failureText) { $failureText + '; ' } else { '' } +
                'bounded QAC guest runner did not finish before its cleanup deadline.'
        } else {
            $qacRunnerExitCode = $qacRunner.ExitCode
            $runnerStdout = $stdoutTask.GetAwaiter().GetResult()
            $runnerStderr = $stderrTask.GetAwaiter().GetResult()
            Set-Content -LiteralPath $qacStdoutPath -Value $runnerStdout -Encoding utf8
            Set-Content -LiteralPath $qacStderrPath -Value $runnerStderr -Encoding utf8
        }
    }
} catch {
    $failureText = $_.Exception.Message
    if ($null -ne $qacRunner -and -not $qacRunner.HasExited) {
        $runnerDeadline = (Get-Date).AddSeconds($MaxRuntimeSeconds + 210)
        while (-not $qacRunner.HasExited -and (Get-Date) -lt $runnerDeadline) {
            Start-Sleep -Milliseconds 500
        }
        if ($qacRunner.HasExited) {
            $qacRunnerExitCode = $qacRunner.ExitCode
            $runnerStdout = $stdoutTask.GetAwaiter().GetResult()
            $runnerStderr = $stderrTask.GetAwaiter().GetResult()
            Set-Content -LiteralPath $qacStdoutPath -Value $runnerStdout -Encoding utf8
            Set-Content -LiteralPath $qacStderrPath -Value $runnerStderr -Encoding utf8
        }
    }
}

$serialText = Read-C188R12R1SharedText $serialPath
$settingsEvidence = Get-C188R12R1SettingsHandoffEvidence $serialText
$qmpControlText = Read-C188R12R1SharedText $qmpControlPath
$clickEvidence = Test-C188R12R1SingleClickQmpEvidence $qmpControlText
$runnerInputEvidence = Get-C188R12R1RunnerInputEvidence $serialText $qmpControlText `
    $clickAttempted $pointerTargetEvidence
$earliestBoundary = if (-not [bool]$settingsEvidence.appModelLaunch -and
    $runnerInputEvidence.earliestMissingBoundary -eq 'runner-input-target-delivery') {
    'runner-input-target-delivery'
} elseif (-not [bool]$settingsEvidence.appModelLaunch -and
    $runnerInputEvidence.earliestMissingBoundary -eq 'runner-pointer-position-unverified') {
    'runner-pointer-position-unverified'
} else { [string]$settingsEvidence.earliestMissingBoundary }
$baseManifestPath = Join-Path $bootRoot 'acceptance-manifest.json'
$baseManifest = if (Test-Path -LiteralPath $baseManifestPath) {
    Get-Content -LiteralPath $baseManifestPath -Raw | ConvertFrom-Json
} else { $null }
$qacAfterStatus = if (Test-Path -LiteralPath (Join-Path $bootRoot 'qac-after.status.txt')) {
    Get-Content -LiteralPath (Join-Path $bootRoot 'qac-after.status.txt') -Raw
} else { '' }
$qacAfterList = if (Test-Path -LiteralPath (Join-Path $bootRoot 'qac-after.list.txt')) {
    Get-Content -LiteralPath (Join-Path $bootRoot 'qac-after.list.txt') -Raw
} else { '' }
$qacReleaseVerified = $null -ne $baseManifest -and
    [bool]$baseManifest.qacReleaseVerified -and
    $qacAfterStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and
    $qacAfterList -match 'Reservations:\s+0'
$settingsRegistryRegression = [bool]$settingsEvidence.settingsRegistryReturn -and
    (-not [bool]$settingsEvidence.settingsRegistryReturnValid -or
     [bool]$settingsEvidence.settingsRegistryMemoryMismatch)
$runtimeRegressionProven = [regex]::IsMatch($serialText,
    '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=\d+ st=launch-return selector=4 .* n=(?!5\b)\d+ result=\d+ .*\r?$') -or
    $serialText -match '(?m)^\[NATIVEAOT-HEAP\] action=clear ' -or
    $serialText -match '(?m)^\[C188-IDENTITY-REGISTRY\].*result=FAIL' -or
    $settingsRegistryRegression -or
    [bool]$settingsEvidence.persistentHeapInitializationRegression
$qacExecutionQualified = $null -ne $baseManifest -and
    [bool]$baseManifest.qacAdmissionConfirmed -and
    [bool]$baseManifest.qacOwnershipProofAccepted -and $qacReleaseVerified -and
    -not $ownershipDegraded
$outcome = if ($runtimeRegressionProven) { 'E_MANAGED_RUNTIME_REGRESSION' }
    elseif (-not $qacExecutionQualified -or
        $earliestBoundary -in @('runner-input-target-delivery','runner-pointer-position-unverified',
            'runner-input-sequence-invalid','runner-input-not-attempted')) { 'D_QAC_OR_EXECUTION_BLOCKED' }
    elseif ([bool]$settingsEvidence.accepted -and [bool]$clickEvidence.accepted) { 'A_SETTINGS_HANDOFF_ACCEPTED' }
    else { 'C_HANDOFF_BOUNDARY_LOCALIZED' }

$screenshotComparison = $null
$beforePpm = Join-Path $bootRoot 'before-settings.ppm'
$afterPpm = Join-Path $bootRoot 'settings-check.ppm'
if ((Test-Path -LiteralPath $beforePpm -PathType Leaf) -and
    (Test-Path -LiteralPath $afterPpm -PathType Leaf)) {
    $comparisonOutput = (& $python.Source $compare $beforePpm $afterPpm 2>&1 | Out-String)
    if ($LASTEXITCODE -eq 0) {
        try { $screenshotComparison = $comparisonOutput | ConvertFrom-Json -ErrorAction Stop }
        catch { $screenshotComparison = [ordered]@{error='comparison-output-invalid';text=$comparisonOutput.Trim()} }
    } else { $screenshotComparison = [ordered]@{error='comparison-tool-failed';text=$comparisonOutput.Trim()} }
}

$finalEvidence = [ordered]@{
    schemaVersion=1;phase='C188R12R1-settings-handoff';outcome=$outcome
    candidateRoot=$candidate;bootEvidencePath=$bootRoot
    runIdentityId=$runIdentityId;expectedKernelSha256=$ExpectedKernelSha256.ToUpperInvariant()
    sourceMediaHashes=$sourceMediaHashes;copiedMediaHashes=$copiedMediaHashes
    originalRamdiskSha256=$stagingManifest.ramdiskSha256.ToUpperInvariant()
    runnerExitCode=$qacRunnerExitCode;runnerStdoutPath=$qacStdoutPath;runnerStderrPath=$qacStderrPath
    bootReady=$ready;readinessTimeout=$readinessTimeout;failure=$failureText
    clickAttempted=$clickAttempted;clickExitCode=$clickExitCode;clickTarget=$clickTarget
    clickEvidence=$clickEvidence;runnerInputEvidence=$runnerInputEvidence
    pointerPositionEvidence=@($pointerPositionEvidence);pointerTargetEvidence=$pointerTargetEvidence
    earliestMissingBoundary=$earliestBoundary;ownershipDegraded=$ownershipDegraded
    qacOwnershipBeforeClick=$preClickOwnership
    qacOwnershipBeforeInput=$preInputOwnership
    qacOwnershipBeforeCheckpoint=$preCheckpointOwnership
    qacOwnershipBeforeSettingsCheckpoint=$preSettingsCheckpointOwnership
    qacOwnershipBeforeShutdown=$preShutdownOwnership
    qmpQuitRequested=$qmpQuitRequested;qacReleaseVerified=$qacReleaseVerified
    qacAdmissionConfirmed=if ($null -ne $baseManifest) { [bool]$baseManifest.qacAdmissionConfirmed } else { $false }
    qacOwnershipProofAccepted=if ($null -ne $baseManifest) { [bool]$baseManifest.qacOwnershipProofAccepted } else { $false }
    settingsEvidence=$settingsEvidence
    settingsRegistryEntry=[bool]$settingsEvidence.settingsRegistryEntry
    settingsRegistryReturn=[bool]$settingsEvidence.settingsRegistryReturn
    settingsRegistryReturnValid=[bool]$settingsEvidence.settingsRegistryReturnValid
    settingsResidentMask=[int]$settingsEvidence.settingsResidentMask
    settingsRegistryMemoryStable=[bool]$settingsEvidence.settingsRegistryMemoryStable
    persistentHeapPreserved=[bool]$settingsEvidence.persistentHeapPreserved
    persistentHeapInitializationCount=[int]$settingsEvidence.persistentHeapInitializationCount
    runtimeRegressionProven=$runtimeRegressionProven
    screenshotComparison=$screenshotComparison
    screenshots=[ordered]@{
        desktopReadyPpm=Join-Path $bootRoot 'desktop-ready.ppm'
        beforeSettingsPpm=$beforePpm;beforeSettingsPng=Join-Path $bootRoot 'before-settings.png'
        settingsCheckPpm=$afterPpm;settingsCheckPng=Join-Path $bootRoot 'settings-check.png'
        beforeSettingsSha256=if (Test-Path -LiteralPath $beforePpm) { (Get-FileHash -LiteralPath $beforePpm -Algorithm SHA256).Hash } else { '' }
        settingsCheckSha256=if (Test-Path -LiteralPath $afterPpm) { (Get-FileHash -LiteralPath $afterPpm -Algorithm SHA256).Hash } else { '' }
    }
    serialSha256=if (Test-Path -LiteralPath $serialPath) { (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash } else { '' }
    qmpControlSha256=if (Test-Path -LiteralPath $qmpControlPath) { (Get-FileHash -LiteralPath $qmpControlPath -Algorithm SHA256).Hash } else { '' }
}
$finalEvidencePath = Join-Path $bootRoot 'r12r1-settings-handoff-evidence.json'
$finalEvidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $finalEvidencePath -Encoding utf8
Write-Output "C188R12R1_SETTINGS_HANDOFF=$outcome BOUNDARY=$earliestBoundary CLICK_ATTEMPTED=$clickAttempted QAC_RELEASE=$qacReleaseVerified"
Write-Output "C188R12R1_SETTINGS_EVIDENCE=$finalEvidencePath"
if ($null -ne $qacRunner -and -not $qacRunner.HasExited) {
    throw "C188R12R1 evidence is saved but its bounded QAC runner is still active: $bootRoot"
}
if ($outcome -eq 'D_QAC_OR_EXECUTION_BLOCKED' -or $outcome -eq 'E_MANAGED_RUNTIME_REGRESSION') {
    throw "C188R12R1 execution ended as $outcome; see $finalEvidencePath. $failureText"
}
