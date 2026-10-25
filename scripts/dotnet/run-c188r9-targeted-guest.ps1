param(
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter()][ValidateRange(120,600)][int]$MaxRuntimeSeconds = 240
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R9 targeted guest runner requires PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$bootRoot = [System.IO.Path]::GetFullPath($BootEvidencePath)
$r9Root = [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r9-registry-input-reentry'))
$relative = [System.IO.Path]::GetRelativePath($r9Root, $bootRoot)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) {
    throw "BootEvidencePath must be a unique directory below $r9Root."
}
$runner = Join-Path $PSScriptRoot 'run-c188-qac-guest.ps1'
$control = Join-Path $PSScriptRoot 'c188-qmp-control.py'
$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1
$pwsh = Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1
$activePath = Join-Path $bootRoot 'active.json'
$serialPath = Join-Path $bootRoot 'serial.log'
$controllerStdout = Join-Path $bootRoot 'r9-controller.stdout.log'
$controllerStderr = Join-Path $bootRoot 'r9-controller.stderr.log'
$controllerManifest = Join-Path $bootRoot 'r9-targeted-run.json'
foreach ($path in @($controllerStdout,$controllerStderr,$controllerManifest)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite targeted guest evidence: $path" }
}
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
if (-not (Test-Path -LiteralPath $qacCli -PathType Leaf)) {
    throw "QAC CLI is unavailable: $qacCli"
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
if (-not $qacRunner) { throw 'Failed to start the bounded C188R9 QAC guest runner.' }
$stdoutTask = $qacRunner.StandardOutput.ReadToEndAsync()
$stderrTask = $qacRunner.StandardError.ReadToEndAsync()
$inputSent = $false
$runnerExitCode = -1
$serialText = ''
$controlResults = [System.Collections.Generic.List[object]]::new()

function Invoke-C188R9Control([string[]]$Arguments, [string]$Name) {
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
                $ready = ([int]$active.screenWidth) -gt 0 -and
                    ([int]$active.screenHeight) -gt 0 -and
                    (Test-Path -LiteralPath $serialPath -PathType Leaf)
            } catch { $ready = $false }
        }
        if (-not $ready) { Start-Sleep -Milliseconds 500 }
    } while (-not $ready -and (Get-Date) -lt $readyDeadline)
    if (-not $ready) { throw 'C188R9 guest did not publish its ready QMP context.' }

    $serialText = Get-Content -LiteralPath $serialPath -Raw
    if ($serialText -notmatch '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$' -or
        $serialText -notmatch '(?m)^\[C150-APP-LAUNCH\].*selector=00000004 kind=normal\r?$' -or
        $serialText -notmatch '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=00000001(?:\s|$)') {
        throw 'C188R9 guest did not reach Notes startup, identity, and input-loop readiness.'
    }

    $controlResults.Add((Invoke-C188R9Control @('status') 'qmp-status-before-input'))
    $controlResults.Add((Invoke-C188R9Control @('capture','before-input') 'qmp-capture-before-input'))
    # The R8 probe established (379,359) as an editable point in the ordinary
    # selector-4 Notes text area for the centered 600x360 window.
    $controlResults.Add((Invoke-C188R9Control @('click','379','359') 'qmp-notes-click'))
    $controlResults.Add((Invoke-C188R9Control @('key','a') 'qmp-notes-key-a'))
    $controlResults.Add((Invoke-C188R9Control @('capture','after-input') 'qmp-capture-after-input'))
    $controlResults.Add((Invoke-C188R9Control @('status') 'qmp-status-after-input'))
    $inputSent = $true
    $serialText = Get-Content -LiteralPath $serialPath -Raw
    $controlResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootRoot 'r9-qmp-actions.json') -Encoding utf8
    [void](Invoke-C188R9Control @('quit') 'qmp-owned-quit')
    if (-not $qacRunner.WaitForExit(90000)) {
        throw 'QAC did not release the targeted guest after the owned QMP quit.'
    }
    $runnerExitCode = $qacRunner.ExitCode
} catch {
    if (-not $qacRunner.HasExited -and (Test-Path -LiteralPath $activePath -PathType Leaf)) {
        try { [void](Invoke-C188R9Control @('quit') 'qmp-owned-quit-after-error') } catch { }
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

$serialText = if (Test-Path -LiteralPath $serialPath) {
    Get-Content -LiteralPath $serialPath -Raw
} else { '' }
$acceptancePath = Join-Path $bootRoot 'acceptance-manifest.json'
$acceptance = if (Test-Path -LiteralPath $acceptancePath) {
    Get-Content -LiteralPath $acceptancePath -Raw | ConvertFrom-Json
} else { $null }
$qacStderr = if (Test-Path -LiteralPath (Join-Path $bootRoot 'qac.stderr.log')) {
    Get-Content -LiteralPath (Join-Path $bootRoot 'qac.stderr.log') -Raw
} else { '' }
$reservation = [regex]::Match($qacStderr,'QAC reservation requested: id=([0-9a-f]+)')
$released = [regex]::Match($qacStderr,'QAC reservation released: id=([0-9a-f]+)')
$qemuChild = [regex]::Match($qacStderr,'QAC QEMU exited: reservation=([0-9a-f]+) pid=(\d+) exitCode=(\d+)')
$manifest = [ordered]@{
    result=$(if ($runnerExitCode -eq 0 -and $inputSent -and $null -ne $acceptance -and $acceptance.result -eq 'PASS') { 'PASS' } else { 'DIAGNOSTIC-COMPLETE' })
    scenario='registry-input-reentry';qacRunnerExitCode=$runnerExitCode;inputSequenceSent=$inputSent
    qacReservationId=$(if ($reservation.Success) { $reservation.Groups[1].Value } else { '' })
    qacReleaseVerified=($reservation.Success -and $released.Success -and $qemuChild.Success -and
        $reservation.Groups[1].Value -eq $released.Groups[1].Value -and
        $reservation.Groups[1].Value -eq $qemuChild.Groups[1].Value -and
        -not (Select-String -LiteralPath (Join-Path $bootRoot 'qac-after.list.txt') -Pattern ([regex]::Escape($reservation.Groups[1].Value)) -Quiet -ErrorAction SilentlyContinue))
    qemuPid=$(if ($qemuChild.Success) { $qemuChild.Groups[2].Value } else { '' })
    guestReady=($serialText -match '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.$')
    registryLaunchLookupSuccessCount=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R9-300 REG .*stage=descriptor-lookup .*input=0 .*result=0$')).Count
    registryLaunchLookupRejectedCount=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R9-300 REG .*stage=descriptor-lookup .*input=0 .*result=-4$')).Count
    registryInputEntryCount=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R9-300 REG .*stage=dispatch-entry .*input=1 .*')).Count
    registryInputLookupSuccessCount=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R9-300 REG .*stage=descriptor-lookup .*input=1 .*result=0$')).Count
    lookupRejectedCount=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R9-300 REG .*stage=descriptor-lookup .*input=1 .*result=-4$')).Count
    notesHandlerInvoked=([regex]::IsMatch($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188R8-300 INPUT-BOUNDARY stage=notes-handler-enter selector=4 generation=1 '))
    notesPointerFocus=([regex]::IsMatch($serialText,'(?m)^\[C117-MANAGED-OUTPUT\] C117-TEXT-AREA focus=PASS source=pointer$'))
    notesDocumentChanged=([regex]::IsMatch($serialText,'(?m)^\[C117-MANAGED-OUTPUT\] C117-TEXT-AREA edit=changed result=PASS$'))
    serialSha256=$(if (Test-Path -LiteralPath $serialPath) { (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash } else { '' })
    qmpLogSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qmp.log')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'qmp.log') -Algorithm SHA256).Hash } else { '' })
    qmpControlLogSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qmp-control.log')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'qmp-control.log') -Algorithm SHA256).Hash } else { '' })
    beforeCapture=$(Join-Path $bootRoot 'before-input.png');afterCapture=$(Join-Path $bootRoot 'after-input.png')
    acceptanceManifest=$acceptancePath
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $controllerManifest -Encoding utf8
Write-Output "C188R9_TARGETED_GUEST=$($manifest.result) READY=$($manifest.guestReady) HANDLER=$($manifest.notesHandlerInvoked) FOCUS=$($manifest.notesPointerFocus) EDIT=$($manifest.notesDocumentChanged) LOOKUP_REJECTIONS=$($manifest.lookupRejectedCount) QAC_RELEASE=$($manifest.qacReleaseVerified)"
Write-Output "C188R9_TARGETED_MANIFEST=$controllerManifest"
if (-not $manifest.qacReleaseVerified) { throw 'C188R9 targeted guest did not verify QAC release.' }
