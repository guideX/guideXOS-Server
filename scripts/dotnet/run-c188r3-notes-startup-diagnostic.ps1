param(
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [Parameter(Mandatory=$true)][string]$ManagedAppPath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter()][ValidateRange(90,240)][int]$BootTimeoutSeconds = 150,
    [Parameter()][ValidateRange(1,60)][int]$AdmissionWindowSeconds = 60,
    [Parameter()][switch]$ProbeNotesInput,
    [Parameter()][ValidateSet('C188R3','C188R4','C188R5')][string]$Phase = 'C188R3'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'C188 Notes startup diagnostic guest requires PowerShell Core.' }
if ($Phase -eq 'C188R5' -and -not $ProbeNotesInput) {
    throw 'C188R5 requires a real Notes pointer and keyboard input probe.'
}
if ($ProbeNotesInput -and $Phase -ne 'C188R5') {
    throw 'The real Notes input probe is reserved for the C188R5 diagnostic guest.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRootName = if ($Phase -eq 'C188R5') { 'out/dotnet/c188r5-notes-proof-invocation' } elseif ($Phase -eq 'C188R4') { 'out/dotnet/c188r4-notes-startup-operation' } else { 'out/dotnet/c188r3-notes-startup-diagnostics' }
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo $phaseRootName))
$root = [System.IO.Path]::GetFullPath($RunRoot)
$relativeRoot = [System.IO.Path]::GetRelativePath($phaseRoot, $root)
if ($relativeRoot -eq '.' -or $relativeRoot.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relativeRoot)) {
    throw "RunRoot must be a unique directory below $phaseRoot."
}
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "RunRoot does not exist: $root" }
$esp = Join-Path $root 'esp'
$serial = Join-Path $root 'serial.log'
$qmpLog = Join-Path $root 'qmp.log'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = Join-Path $repo 'OVMF.fd'
$expectedPath = Join-Path $root 'expected-observations.json'
$manifestPath = Join-Path $root 'diagnostic-manifest.json'
$evidencePaths = @(
    $expectedPath, $manifestPath, $serial, $qmpLog,
    (Join-Path $root 'active.json'),
    (Join-Path $root 'qac-before.status.txt'), (Join-Path $root 'qac-before.list.txt'),
    (Join-Path $root 'qac-during.status.txt'), (Join-Path $root 'qac-during.list.txt'),
    (Join-Path $root 'qac-after.status.txt'), (Join-Path $root 'qac-after.list.txt'),
    (Join-Path $root 'qac.stdout.log'), (Join-Path $root 'qac.stderr.log'),
    (Join-Path $root 'qac-run-arguments.json'), (Join-Path $root 'observed-checkpoints.json'),
    (Join-Path $root 'qac-exit-code.txt'), (Join-Path $root 'notes-before-input.ppm'),
    (Join-Path $root 'final-stage.ppm'))
foreach ($path in $evidencePaths) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite C188 startup diagnostic evidence: $path" }
}

$managedApp = (Resolve-Path -LiteralPath $ManagedAppPath).Path
$stagingManifestPath = Join-Path $root 'ramdisk-staging.json'
foreach ($required in @(
    (Join-Path $esp 'kernel.elf'), (Join-Path $esp 'ramdisk.img'),
    (Join-Path $esp 'EFI/BOOT/BOOTX64.EFI'), $stagingManifestPath,
    $managedApp, $qacCli, $qemu, $ovmf)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required diagnostic input is missing: $required" }
}
$stage = Get-Content -LiteralPath $stagingManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($stage.result -cne 'PASS' -or $stage.stagedAppSha256 -cne
    (Get-FileHash -LiteralPath $managedApp -Algorithm SHA256).Hash) {
    throw 'The isolated ramdisk staging manifest does not identify the supplied managed composite.'
}
$mediaHashes = [ordered]@{}
foreach ($relative in @('kernel.elf','ramdisk.img','EFI/BOOT/BOOTX64.EFI')) {
    $media = Join-Path $esp $relative
    $mediaHashes[$relative] = (Get-FileHash -LiteralPath $media -Algorithm SHA256).Hash
}
if ($mediaHashes['kernel.elf'] -ne $ExpectedKernelSha256.ToUpperInvariant()) {
    throw 'The isolated ESP kernel SHA-256 differs from the qualified candidate kernel.'
}
$managedHash = (Get-FileHash -LiteralPath $managedApp -Algorithm SHA256).Hash

$python = Get-Command 'python.exe' -ErrorAction Stop | Select-Object -First 1
$checkpointValidator = Join-Path $PSScriptRoot 'c188-checkpoint-sequence.py'
$descriptionText = (& $python.Source $checkpointValidator describe --phase $Phase 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw "Unable to load authoritative $Phase checkpoint order: $descriptionText" }
$description = $descriptionText | ConvertFrom-Json -ErrorAction Stop
$expected = [ordered]@{
    phase=$Phase; outcome='pending'
    expectedOrderedCheckpoints=$description.expectedOrderedCheckpoints
    interpretation=$description.interpretation
    bootTimeoutSeconds=$BootTimeoutSeconds; admissionWindowSeconds=$AdmissionWindowSeconds
    startingKernelSha256=$mediaHashes['kernel.elf']; managedCompositeSha256=$managedHash
    ramdiskSha256=$mediaHashes['ramdisk.img']; uefiLoaderSha256=$mediaHashes['EFI/BOOT/BOOTX64.EFI']
}
$expected | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $expectedPath -Encoding utf8

Import-Module (Join-Path $PSScriptRoot 'c181-qmp.psm1') -Force -ErrorAction Stop
$qacStatusBefore = (& $qacCli status 2>&1 | Out-String)
$qacListBefore = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'qac-before.status.txt') -Value $qacStatusBefore -Encoding utf8
Set-Content -LiteralPath (Join-Path $root 'qac-before.list.txt') -Value $qacListBefore -Encoding utf8
$preflightClear = $qacStatusBefore -match 'Coordinator:\s+Online' -and
    $qacStatusBefore -match 'QEMU processes:\s+0 \(0 foreign\)' -and
    $qacListBefore -match 'Reservations:\s+0'
if ($Phase -eq 'C188R5') {
    $preflightClear = $preflightClear -and
        $qacListBefore -match 'Managed sessions:\s+0'
}
if (-not $preflightClear) {
    $expected.outcome = 'blocked-before-launch'
    $manifest = [ordered]@{result='BLOCKED';reason='QAC unavailable or acceptance ownership is already active';qacStatus=$qacStatusBefore.Trim();qacList=$qacListBefore.Trim();expectedObservations=$expectedPath}
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Write-Output "${Phase}_QAC_DIAGNOSTIC=BLOCKED QEMU_NOT_STARTED=true"
    exit 3
}

$qmpListener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$qmpListener.Start()
$qmpPort = ([System.Net.IPEndPoint]$qmpListener.LocalEndpoint).Port
$qmpListener.Stop()
$qemuArgs = @(
    '-accel','tcg','-machine','pc','-smp','1',
    '-drive',('if=pflash,format=raw,readonly=on,file='+$ovmf),
    '-drive',('file=fat:rw:'+ $esp + ',format=raw,if=ide,index=0'),
    '-m','1024M','-vga','std','-display','none',
    '-serial',('file:'+ $serial),
    '-qmp',('tcp:127.0.0.1:{0},server,nowait' -f $qmpPort),
    '-boot','order=c','-no-shutdown','-rtc','base=utc,clock=host')
$qacArgs = @('run','--project','guideXOSServer','--mode','acceptance','--',$qemu) + $qemuArgs
[ordered]@{qacCli=$qacCli;arguments=$qacArgs;qemu=$qemu;qemuArguments=$qemuArgs;qmpPort=$qmpPort;esp=$esp;serial=$serial;timeoutSeconds=$BootTimeoutSeconds} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'qac-run-arguments.json') -Encoding utf8
[ordered]@{qmpPort=$qmpPort;esp=$esp;serial=$serial;reservationId=$null;ownerPid=$null;scenario='notes-startup-diagnostic'} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'active.json') -Encoding utf8

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $qacCli
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($argument in $qacArgs) { [void]$psi.ArgumentList.Add([string]$argument) }
$qacProcess = [System.Diagnostics.Process]::Start($psi)
if (-not $qacProcess) { throw 'Failed to start the QAC-managed diagnostic command.' }
$stdoutTask = $qacProcess.StandardOutput.ReadToEndAsync()
$stderrTask = $qacProcess.StandardError.ReadToEndAsync()
$qmpFile = [System.IO.File]::Open($qmpLog,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::ReadWrite)
$qmpWriter = [System.IO.StreamWriter]::new($qmpFile,[System.Text.Encoding]::UTF8)
$qmpWriter.AutoFlush = $true
$admitted = $false
$qmpConnected = $false
$bootLoopReached = $false
$inputManagerInitialized = $false
$qemuStatus = $null
$screenshotPath = Join-Path $root 'final-stage.ppm'
$notesBeforeInputPath = Join-Path $root 'notes-before-input.ppm'
$notesPointerAccepted = $false
$notesKeyboardAccepted = $false
$notesSurfaceCreated = $false
$reservationId = $null
$reservationRequestUtc = [DateTime]::UtcNow
$admissionWaitSeconds = $null
$outcome = 'incomplete'
$runError = $null

function Read-DiagnosticSerial {
    if (-not (Test-Path -LiteralPath $serial -PathType Leaf)) { return '' }
    $file = [System.IO.FileStream]::new($serial,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try { $reader = [System.IO.StreamReader]::new($file,[System.Text.Encoding]::UTF8,$true,4096,$true); try { return $reader.ReadToEnd() } finally { $reader.Dispose() } }
    finally { $file.Dispose() }
}

function Invoke-DiagnosticQmp([string]$Command,[object]$Arguments=$null) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1',$qmpPort)
        $stream = $client.GetStream(); $stream.ReadTimeout = 5000
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::ASCII,$false,1024,$true)
        $writer = [System.IO.StreamWriter]::new($stream,[System.Text.Encoding]::ASCII,1024,$true); $writer.AutoFlush = $true
        $greeting = $reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $capabilities = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command 'qmp_capabilities'
        $payload = [ordered]@{execute=$Command}; if ($null -ne $Arguments) { $payload.arguments = $Arguments }
        $request = $payload | ConvertTo-Json -Compress -Depth 8
        $writer.WriteLine($request)
        $response = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command $Command
        $qmpWriter.WriteLine("command=$request`ngreeting=$greeting`ncapabilities=$capabilities`nresponse=$response`n")
        if ($response -match '"error"') { throw "QMP $Command failed: $response" }
        return $response
    } finally { $client.Dispose() }
}

function Send-DiagnosticInputEvent([string]$Type,[object]$Data) {
    $event = [ordered]@{type=$Type;data=$Data}
    [void](Invoke-DiagnosticQmp 'input-send-event' ([ordered]@{events=@($event)}))
}

function Send-DiagnosticNotesInput([string]$CurrentSerial) {
    $widthMatch = [regex]::Match($CurrentSerial,'(?m)^\[DESKTOP CAP\] framebuffer_width=0x([0-9A-Fa-f]+)\r?$')
    $heightMatch = [regex]::Match($CurrentSerial,'(?m)^\[DESKTOP CAP\] framebuffer_height=0x([0-9A-Fa-f]+)\r?$')
    if (-not $widthMatch.Success -or -not $heightMatch.Success) {
        throw 'C188R5 input probe requires framebuffer dimensions from the guest.'
    }
    $width = [Convert]::ToInt32($widthMatch.Groups[1].Value,16)
    $height = [Convert]::ToInt32($heightMatch.Groups[1].Value,16)
    if ($width -lt 600 -or $height -lt 360) {
        throw "C188R5 Notes input target does not fit the reported framebuffer ${width}x${height}."
    }
    # The centered Notes window is 600x360. Its editor begins at client
    # coordinate (20,84), below the 24-pixel title bar.
    $targetX = [int](($width - 600) / 2) + 40
    $targetY = [int](($height - 360) / 2) + 140
    $cursorX = [Math]::Min($width - 1,[int]($width / 2) + 1)
    $cursorY = [Math]::Min($height - 1,[int]($height / 2) + 1)
    foreach ($axis in @(@{name='x';current=$cursorX;target=$targetX},@{name='y';current=$cursorY;target=$targetY})) {
        $remaining = [int]($axis.target - $axis.current)
        while ($remaining -ne 0) {
            $step = [Math]::Sign($remaining) * [Math]::Min([Math]::Abs($remaining),40)
            Send-DiagnosticInputEvent 'rel' ([ordered]@{axis=$axis.name;value=$step})
            $remaining -= $step
        }
    }
    Send-DiagnosticInputEvent 'btn' ([ordered]@{button='left';down=$true})
    Start-Sleep -Milliseconds 100
    Send-DiagnosticInputEvent 'btn' ([ordered]@{button='left';down=$false})
    Start-Sleep -Milliseconds 250
    Send-DiagnosticInputEvent 'key' ([ordered]@{down=$true;key=[ordered]@{type='qcode';data='a'}})
    Start-Sleep -Milliseconds 80
    Send-DiagnosticInputEvent 'key' ([ordered]@{down=$false;key=[ordered]@{type='qcode';data='a'}})
    Start-Sleep -Milliseconds 400
    return Read-DiagnosticSerial
}

function Wait-DiagnosticQmp([int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $client = [System.Net.Sockets.TcpClient]::new()
        try { $client.Connect('127.0.0.1',$qmpPort); return $true }
        catch { if ($qacProcess.HasExited) { return $false }; Start-Sleep -Milliseconds 200 }
        finally { $client.Dispose() }
    } while ((Get-Date) -lt $deadline)
    return $false
}

try {
    $admissionDeadline = (Get-Date).AddSeconds($AdmissionWindowSeconds)
    do {
        if ($qacProcess.HasExited) { throw "QAC exited before admission: $($qacProcess.ExitCode)" }
        $listNow = (& $qacCli list 2>&1 | Out-String)
        $ownedLine = @($listNow -split "`r?`n" | Where-Object { $_ -match ('\(PID ' + $qacProcess.Id + '\)') }) | Select-Object -First 1
        if ($ownedLine -match '^\s*([0-9a-f]{32})\s+\|.*\|\s+Running\b') {
            $reservationId = $Matches[1]; $admitted = $true; break
        }
        if ($ownedLine -match '^\s*([0-9a-f]{32})\s+\|.*\|\s+Queued\b') { $reservationId = $Matches[1] }
        $otherReservations = @($listNow -split "`r?`n" | Where-Object {
            $_ -match '^\s*[0-9a-f]{32}\s+\|' -and $_ -notmatch ('\(PID ' + $qacProcess.Id + '\)')
        })
        if ($otherReservations.Count -gt 0 -and $null -ne $reservationId) {
            & $qacCli cancel $reservationId 2>&1 | Out-String | Set-Content -LiteralPath (Join-Path $root 'qac-cancel.log') -Encoding utf8
            if (-not $qacProcess.WaitForExit(90000)) { throw 'QAC did not finish after this queued diagnostic reservation was canceled.' }
            throw 'Another QAC reservation appeared during the bounded admission window; canceled only this queued diagnostic reservation.'
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $admissionDeadline)
    if (-not $admitted) {
        if ($null -ne $reservationId) {
            & $qacCli cancel $reservationId 2>&1 | Out-String | Set-Content -LiteralPath (Join-Path $root 'qac-cancel.log') -Encoding utf8
        }
        if (-not $qacProcess.WaitForExit(90000)) { throw 'QAC did not finish after the bounded admission window expired.' }
        throw 'QAC did not grant exclusive admission within the 60-second diagnostic window.'
    }
    $admissionWaitSeconds = [Math]::Round(([DateTime]::UtcNow - $reservationRequestUtc).TotalSeconds,1)
    $active = Get-Content -LiteralPath (Join-Path $root 'active.json') -Raw | ConvertFrom-Json
    $active.reservationId = $reservationId; $active.ownerPid = $qacProcess.Id
    $active | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'active.json') -Encoding utf8

    $duringStatus = (& $qacCli status 2>&1 | Out-String)
    $duringList = (& $qacCli list 2>&1 | Out-String)
    Set-Content -LiteralPath (Join-Path $root 'qac-during.status.txt') -Value $duringStatus -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'qac-during.list.txt') -Value $duringList -Encoding utf8
    if ($duringList -notmatch [regex]::Escape($reservationId) -or $duringList -notmatch 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running') {
        throw 'QAC did not show this diagnostic reservation as Running.'
    }

    if (-not (Wait-DiagnosticQmp 30)) { throw 'QMP did not start before the diagnostic startup timeout.' }
    $qmpConnected = $true
    $deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)
    do {
        $serialText = Read-DiagnosticSerial
        $inputManagerInitialized = $serialText -match '(?m)^\[KERNEL\] Input manager initialized\r?$'
        if ($serialText -match '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$') { $bootLoopReached = $true; break }
        if ($qacProcess.HasExited) { break }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)

    $serialText = Read-DiagnosticSerial
    if ($bootLoopReached) { $outcome = 'input-loop-reached' }
    else { $outcome = 'startup-timeout-or-guest-exit' }
    $notesSurfaceCreated = $serialText -match '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ result=PASS\r?$'
    try { $qemuStatus = Invoke-DiagnosticQmp 'query-status' } catch { $qemuStatus = "query-status failed: $($_.Exception.Message)" }
    if ($bootLoopReached -and $ProbeNotesInput) {
        if ($notesSurfaceCreated) {
            [void](Invoke-DiagnosticQmp 'screendump' ([ordered]@{filename=$notesBeforeInputPath}))
            $serialText = Send-DiagnosticNotesInput $serialText
            $notesPointerAccepted = $serialText -match '(?m)^C120-POINTER target=document result=PASS\r?$'
            $notesKeyboardAccepted = $serialText -match '(?m)^C117-TEXT-AREA edit=changed result=PASS\r?$'
        }
    }
    try { [void](Invoke-DiagnosticQmp 'screendump' ([ordered]@{filename=$screenshotPath})) } catch {
        $screenshotPath = $null
        Set-Content -LiteralPath (Join-Path $root 'screendump-error.txt') -Value $_.Exception.Message -Encoding utf8
    }
    if ($qmpConnected -and -not $qacProcess.HasExited) {
        try { [void](Invoke-DiagnosticQmp 'quit') } catch { Set-Content -LiteralPath (Join-Path $root 'qmp-quit-error.txt') -Value $_.Exception.Message -Encoding utf8 }
    }
    if (-not $qacProcess.HasExited) { [void]$qacProcess.WaitForExit(90000) }
    if (-not $qacProcess.HasExited) { throw 'QAC-managed diagnostic guest did not exit after the owned QMP quit request.' }
} catch {
    $runError = $_.Exception.Message
    if (-not $qacProcess.HasExited -and $admitted -and -not $qmpConnected) {
        if (Wait-DiagnosticQmp 10) { $qmpConnected = $true }
    }
    if ($qmpConnected -and -not $qacProcess.HasExited) {
        try { [void](Invoke-DiagnosticQmp 'query-status') } catch { }
        try { [void](Invoke-DiagnosticQmp 'screendump' ([ordered]@{filename=$screenshotPath})) } catch { }
        try { [void](Invoke-DiagnosticQmp 'quit') } catch { }
        [void]$qacProcess.WaitForExit(90000)
    }
    elseif (-not $qacProcess.HasExited -and -not $admitted -and $null -ne $reservationId) {
        $currentList = (& $qacCli list 2>&1 | Out-String)
        $ownLine = @($currentList -split "`r?`n" | Where-Object { $_ -match [regex]::Escape($reservationId) }) | Select-Object -First 1
        if ($ownLine -match '\bQueued\b') {
            & $qacCli cancel $reservationId 2>&1 | Out-String | Set-Content -LiteralPath (Join-Path $root 'qac-cancel.log') -Encoding utf8
        }
        [void]$qacProcess.WaitForExit(90000)
    }
    $outcome = 'runner-error'
} finally {
    $qmpWriter.Dispose(); $qmpFile.Dispose()
    if ($qacProcess.HasExited) {
        Set-Content -LiteralPath (Join-Path $root 'qac.stdout.log') -Value $stdoutTask.GetAwaiter().GetResult() -Encoding utf8
        $qacStderr = $stderrTask.GetAwaiter().GetResult()
        Set-Content -LiteralPath (Join-Path $root 'qac.stderr.log') -Value $qacStderr -Encoding utf8
        Set-Content -LiteralPath (Join-Path $root 'qac-exit-code.txt') -Value $qacProcess.ExitCode -Encoding ascii
    }
}

$afterStatus = (& $qacCli status 2>&1 | Out-String)
$afterList = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'qac-after.status.txt') -Value $afterStatus -Encoding utf8
Set-Content -LiteralPath (Join-Path $root 'qac-after.list.txt') -Value $afterList -Encoding utf8
$serialText = Read-DiagnosticSerial
$inputManagerInitialized = $serialText -match '(?m)^\[KERNEL\] Input manager initialized\r?$'
$reservationRequest = [regex]::Match($qacStderr,'QAC reservation requested: id=([0-9a-f]+)')
$reservationRelease = [regex]::Match($qacStderr,'QAC reservation released: id=([0-9a-f]+)')
$childExit = [regex]::Match($qacStderr,'QAC QEMU exited: reservation=([0-9a-f]+) pid=(\d+) exitCode=(\d+)')
$releaseVerified = $reservationRequest.Success -and $reservationRelease.Success -and $childExit.Success -and
    $reservationRequest.Groups[1].Value -eq $reservationRelease.Groups[1].Value -and
    $reservationRequest.Groups[1].Value -eq $childExit.Groups[1].Value -and
    ($null -eq $reservationId -or $reservationId -eq $reservationRequest.Groups[1].Value) -and
    $afterStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and $afterList -match 'Reservations:\s+0'
$captureState = if ($qacProcess.HasExited -and $releaseVerified) { 'complete' } else { 'incomplete' }
$checkpointText = (& $python.Source $checkpointValidator validate --phase $Phase --serial $serial --capture-state $captureState 2>&1 | Out-String)
$checkpointExitCode = $LASTEXITCODE
try { $checkpointAnalysis = $checkpointText | ConvertFrom-Json -ErrorAction Stop }
catch { throw "The checkpoint validator did not return JSON (exit $checkpointExitCode): $checkpointText" }
$checkpointAnalysis | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $root 'observed-checkpoints.json') -Encoding utf8
$observed = @($checkpointAnalysis.observedOrderedCheckpoints | ForEach-Object { $_.marker })
$firstMissing = $checkpointAnalysis.firstExpectedMissingCheckpoint
$serialHash = if (Test-Path -LiteralPath $serial) { (Get-FileHash -LiteralPath $serial -Algorithm SHA256).Hash } else { $null }
$qmpHash = if (Test-Path -LiteralPath $qmpLog) { (Get-FileHash -LiteralPath $qmpLog -Algorithm SHA256).Hash } else { $null }
$screenshotHash = if ($null -ne $screenshotPath -and (Test-Path -LiteralPath $screenshotPath)) { (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash } else { $null }
$notesInputProbePassed = -not $ProbeNotesInput -or ($notesSurfaceCreated -and $notesPointerAccepted -and $notesKeyboardAccepted)
$manifest = [ordered]@{
    result=if ($outcome -eq 'input-loop-reached' -and $releaseVerified -and $notesInputProbePassed) { 'PASS-DIAGNOSTIC-BOUNDARY-REACHED' } elseif ($outcome -eq 'runner-error') { 'RUNNER-ERROR' } elseif (-not $bootLoopReached) { 'GUEST-DID-NOT-REACH-INPUT-LOOP' } elseif ($ProbeNotesInput -and -not $notesInputProbePassed) { 'GUEST-REACHED-LOOP-BUT-NOTES-INPUT-PROBE-FAILED' } else { 'GUEST-DID-NOT-REACH-INPUT-LOOP' }
    outcome=$outcome;phase=$Phase;runRoot=$root;project='guideXOSServer';mode='acceptance'
    qacOwnerPid=$qacProcess.Id;qacReservationId=$reservationId;qacReservationRequestId=if ($reservationRequest.Success) { $reservationRequest.Groups[1].Value } else { $null }
    qacGuestPid=if ($childExit.Success) { $childExit.Groups[2].Value } else { $null }
    qacExitCode=if ($qacProcess.HasExited) { $qacProcess.ExitCode } else { $null };qemuExitCode=if ($childExit.Success) { $childExit.Groups[3].Value } else { $null }
    qacReleaseVerified=$releaseVerified;qacAdmissionWaitSeconds=$admissionWaitSeconds
    bootTimeoutSeconds=$BootTimeoutSeconds;bootLoopReached=$bootLoopReached;qemuStatus=$qemuStatus
    inputManagerInitialized=$inputManagerInitialized;acceptanceReadinessPredicateSatisfied=($inputManagerInitialized -and $bootLoopReached)
    notesInputProbeRequested=[bool]$ProbeNotesInput;notesSurfaceCreated=$notesSurfaceCreated
    notesPointerAccepted=$notesPointerAccepted;notesKeyboardAccepted=$notesKeyboardAccepted
    notesBeforeInputScreenshot=if (Test-Path -LiteralPath $notesBeforeInputPath) { $notesBeforeInputPath } else { $null }
    notesBeforeInputScreenshotSha256=if (Test-Path -LiteralPath $notesBeforeInputPath) { (Get-FileHash -LiteralPath $notesBeforeInputPath -Algorithm SHA256).Hash } else { $null }
    checkpointCaptureState=$captureState;checkpointValidationResult=$checkpointAnalysis.result;checkpointValidation=(Join-Path $root 'observed-checkpoints.json')
    kernelSha256=$mediaHashes['kernel.elf'];managedCompositeSha256=$managedHash;ramdiskSha256=$mediaHashes['ramdisk.img'];uefiLoaderSha256=$mediaHashes['EFI/BOOT/BOOTX64.EFI']
    serialLog=$serial;serialLogSha256=$serialHash;qmpLog=$qmpLog;qmpLogSha256=$qmpHash
    screenshot=$screenshotPath;screenshotSha256=$screenshotHash;expectedObservations=$expectedPath
    observedCheckpoints=(Join-Path $root 'observed-checkpoints.json');observedSequence=@($observed);lastCompletedCheckpoint=$checkpointAnalysis.lastExpectedCheckpointCompleted;lastObservedCheckpoint=$checkpointAnalysis.lastObservedCheckpoint;firstUncompletedCheckpoint=$firstMissing
    qacBeforeStatus=(Join-Path $root 'qac-before.status.txt');qacBeforeList=(Join-Path $root 'qac-before.list.txt')
    qacDuringStatus=(Join-Path $root 'qac-during.status.txt');qacDuringList=(Join-Path $root 'qac-during.list.txt')
    qacAfterStatus=(Join-Path $root 'qac-after.status.txt');qacAfterList=(Join-Path $root 'qac-after.list.txt')
    qacArguments=(Join-Path $root 'qac-run-arguments.json');error=$runError
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output ("{0}_DIAGNOSTIC outcome={1} last={2} firstMissing={3} qacReleaseVerified={4}" -f $Phase,$outcome,$manifest.lastCompletedCheckpoint,$firstMissing,$releaseVerified)
Write-Output "${Phase}_DIAGNOSTIC_MANIFEST=$manifestPath"
if (-not $releaseVerified) { exit 4 }
if (-not $bootLoopReached) { exit 2 }
if (-not $notesInputProbePassed) { exit 5 }
