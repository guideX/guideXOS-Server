param(
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter()][ValidateRange(120,600)][int]$MaxRuntimeSeconds = 240
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R10 targeted guest runner requires PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r10-nativeaot-state-reentry'))
$bootRoot = [System.IO.Path]::GetFullPath($BootEvidencePath)
$relative = [System.IO.Path]::GetRelativePath($phaseRoot, $bootRoot)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) {
    throw "BootEvidencePath must be a unique directory below $phaseRoot."
}
$runner = Join-Path $PSScriptRoot 'run-c188-qac-guest.ps1'
$predicateHelpers = Join-Path $PSScriptRoot 'c188r10-runner-predicates.ps1'
. $predicateHelpers
$control = Join-Path $PSScriptRoot 'c188-qmp-control.py'
$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1
$pwsh = Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1
$activePath = Join-Path $bootRoot 'active.json'
$serialPath = Join-Path $bootRoot 'serial.log'
$controllerStdout = Join-Path $bootRoot 'r10-controller.stdout.log'
$controllerStderr = Join-Path $bootRoot 'r10-controller.stderr.log'
$controllerManifest = Join-Path $bootRoot 'r10-targeted-run.json'
foreach ($path in @($controllerStdout,$controllerStderr,$controllerManifest)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite targeted guest evidence: $path" }
}

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $pwsh.Source
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($argument in @(
    '-NoProfile','-File',$runner,
    '-BootEvidencePath',$bootRoot,
    '-ExpectedKernelSha256',$ExpectedKernelSha256,
    '-Scenario','registry-input-reentry',
    '-MaxRuntimeSeconds',[string]$MaxRuntimeSeconds,
    '-BootTimeoutSeconds','180',
    '-AdmissionTimeoutSeconds','60')) {
    [void]$psi.ArgumentList.Add([string]$argument)
}
$qacRunner = [System.Diagnostics.Process]::Start($psi)
if (-not $qacRunner) { throw 'Failed to start the bounded C188R10 QAC guest runner.' }
$stdoutTask = $qacRunner.StandardOutput.ReadToEndAsync()
$stderrTask = $qacRunner.StandardError.ReadToEndAsync()
$inputSent = $false
$runnerExitCode = -1
$serialText = ''
$controlResults = [System.Collections.Generic.List[object]]::new()

function Invoke-C188R10Control([string[]]$Arguments, [string]$Name) {
    $output = (& $python.Source $control '--active' $activePath @Arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $logPath = Join-Path $bootRoot ($Name + '.log')
    Set-Content -LiteralPath $logPath -Value $output -Encoding utf8
    if ($exitCode -ne 0) { throw "QMP action '$Name' failed with exit code $($exitCode): $output" }
    return [ordered]@{name=$Name;exitCode=$exitCode;log=$logPath}
}

try {
    $readyDeadline = (Get-Date).AddSeconds(240)
    $ready = $false
    do {
        if ($qacRunner.HasExited) {
            throw "QAC runner exited before target input (exit $($qacRunner.ExitCode))."
        }
        if (Test-Path -LiteralPath $activePath -PathType Leaf) {
            try {
                $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
                $qmpReady = ([int]$active.screenWidth) -gt 0 -and
                    ([int]$active.screenHeight) -gt 0 -and
                    (Test-Path -LiteralPath $serialPath -PathType Leaf)
                if ($qmpReady) {
                    $candidateSerial = Get-Content -LiteralPath $serialPath -Raw
                    $ready = Test-C188R10NotesReady $candidateSerial
                }
            } catch { $ready = $false }
        }
        if (-not $ready) { Start-Sleep -Milliseconds 500 }
    } while (-not $ready -and (Get-Date) -lt $readyDeadline)
    if (-not $ready) { throw 'C188R10 guest did not publish its ready QMP context.' }

    $serialText = Get-Content -LiteralPath $serialPath -Raw
    $controlResults.Add((Invoke-C188R10Control @('status') 'r10-qmp-status-before-input'))
    $controlResults.Add((Invoke-C188R10Control @('capture','before-input') 'r10-qmp-capture-before-input'))
    # R8 established this point as editable content in the ordinary Notes text area.
    $controlResults.Add((Invoke-C188R10Control @('click','379','359') 'r10-qmp-notes-click'))
    $controlResults.Add((Invoke-C188R10Control @('key','a') 'r10-qmp-notes-key-a'))
    $controlResults.Add((Invoke-C188R10Control @('capture','after-input') 'r10-qmp-capture-after-input'))
    $controlResults.Add((Invoke-C188R10Control @('status') 'r10-qmp-status-after-input'))
    $inputSent = $true
    $serialText = Get-Content -LiteralPath $serialPath -Raw
    $controlResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootRoot 'r10-qmp-actions.json') -Encoding utf8
    [void](Invoke-C188R10Control @('quit') 'r10-qmp-owned-quit')
    if (-not $qacRunner.WaitForExit(90000)) {
        throw 'QAC did not release the targeted guest after the owned QMP quit.'
    }
    $runnerExitCode = $qacRunner.ExitCode
} catch {
    if (-not $qacRunner.HasExited -and (Test-Path -LiteralPath $activePath -PathType Leaf)) {
        try { [void](Invoke-C188R10Control @('quit') 'r10-qmp-owned-quit-after-error') } catch { }
        [void]$qacRunner.WaitForExit(90000)
    }
    if ($qacRunner.HasExited) { $runnerExitCode = $qacRunner.ExitCode }
    throw
} finally {
    if ($qacRunner.HasExited) {
        Set-Content -LiteralPath $controllerStdout -Value $stdoutTask.GetAwaiter().GetResult() -Encoding utf8
        Set-Content -LiteralPath $controllerStderr -Value $stderrTask.GetAwaiter().GetResult() -Encoding utf8
    }
}

$serialText = if (Test-Path -LiteralPath $serialPath) { Get-Content -LiteralPath $serialPath -Raw } else { '' }
$qacStderrPath = Join-Path $bootRoot 'qac.stderr.log'
$qacStderr = if (Test-Path -LiteralPath $qacStderrPath) { Get-Content -LiteralPath $qacStderrPath -Raw } else { '' }
$reservation = [regex]::Match($qacStderr,'QAC reservation requested: id=([0-9a-f]+)')
$released = [regex]::Match($qacStderr,'QAC reservation released: id=([0-9a-f]+)')
$qemuChild = [regex]::Match($qacStderr,'QAC QEMU exited: reservation=([0-9a-f]+) pid=(\d+) exitCode=(\d+)')
$qacAfterList = Join-Path $bootRoot 'qac-after.list.txt'
$qacReleaseVerified = $reservation.Success -and $released.Success -and $qemuChild.Success -and
    $reservation.Groups[1].Value -eq $released.Groups[1].Value -and
    $reservation.Groups[1].Value -eq $qemuChild.Groups[1].Value -and
    -not (Select-String -LiteralPath $qacAfterList -Pattern ([regex]::Escape($reservation.Groups[1].Value)) -Quiet -ErrorAction SilentlyContinue)
$managedLaunchReturn = [regex]::Match($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R10 state stage=launch-return .*n=5 .*mask=1 .*id=4 gen=1 valid=1 .*rc=0\r?$')
$managedLaunchExit = Test-C188R10ManagedLaunchExit $serialText
$nativeLaunchReturn = Test-C188R10NativeLaunchReturn $serialText
$manifest = [ordered]@{
    result=$(if ($qacReleaseVerified -and $inputSent) { 'DIAGNOSTIC-COMPLETE' } else { 'INCOMPLETE' })
    scenario='managed-state-reentry';qacRunnerExitCode=$runnerExitCode;inputSequenceSent=$inputSent
    qacReservationId=$(if ($reservation.Success) { $reservation.Groups[1].Value } else { '' })
    qacReleaseVerified=$qacReleaseVerified
    qemuPid=$(if ($qemuChild.Success) { $qemuChild.Groups[2].Value } else { '' })
    guestReady=(Test-C188R10NotesReady $serialText)
    launchStateAfterNotesReturn=$managedLaunchReturn.Success
    managedLaunchExit=$managedLaunchExit
    nativeLaunchReturn=$nativeLaunchReturn
    nativeInvocationMarkers=([regex]::Matches($serialText,'(?m)^\[C188R10-NATIVE\] .*\r?$')).Count
    managedEntryMarkers=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R10-MANAGED .*\r?$')).Count
    registrySnapshots=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R10 state .*\r?$')).Count
    inputManagedEntry=(Test-C188R10InputManagedEntry $serialText)
    inputRegistryPresent=(Test-C188R10InputRegistrySnapshot $serialText)
    inputHandlerInvoked=(Test-C188R10InputHandler $serialText)
    inputReturnedSuccess=(Test-C188R10InputReturn $serialText)
    inputLookupRejected=([regex]::IsMatch($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R9-300 REG .*stage=descriptor-lookup .*input=1 .*result=-4\r?$'))
    notesPointerFocus=([regex]::IsMatch($serialText,'(?m)^\[C117-MANAGED-OUTPUT\] C117-TEXT-AREA focus=PASS source=pointer\r?$'))
    notesDocumentChanged=([regex]::IsMatch($serialText,'(?m)^\[C117-MANAGED-OUTPUT\] C117-TEXT-AREA edit=changed result=PASS\r?$'))
    notesTextMutation=([regex]::IsMatch($serialText,'(?m)^\[C117-MANAGED-OUTPUT\] C117-TEXT-AREA edit=changed result=PASS\r?$'))
    qacReservationIdReleased=$qacReleaseVerified
    serialSha256=$(if (Test-Path -LiteralPath $serialPath) { (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash } else { '' })
    qmpLogSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qmp.log')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'qmp.log') -Algorithm SHA256).Hash } else { '' })
    qmpControlLogSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qmp-control.log')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'qmp-control.log') -Algorithm SHA256).Hash } else { '' })
    beforeCapture=(Join-Path $bootRoot 'before-input.png');afterCapture=(Join-Path $bootRoot 'after-input.png')
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $controllerManifest -Encoding utf8
Write-Output "C188R10_TARGETED_GUEST=$($manifest.result) READY=$($manifest.guestReady) HANDLER=$($manifest.inputHandlerInvoked) FOCUS=$($manifest.notesPointerFocus) EDIT=$($manifest.notesDocumentChanged) REJECTED=$($manifest.inputLookupRejected) QAC_RELEASE=$($manifest.qacReleaseVerified)"
Write-Output "C188R10_TARGETED_MANIFEST=$controllerManifest"
if (-not $qacReleaseVerified) { throw 'C188R10 targeted guest did not verify QAC release.' }

\n