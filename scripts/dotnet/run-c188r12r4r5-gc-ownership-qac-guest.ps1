param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [ValidateRange(60,300)][int]$BootTimeoutSeconds = 180,
    [ValidateRange(1,60)][int]$AdmissionWindowSeconds = 30
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'C188R12R4R5 QAC guest requires PowerShell Core.' }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r12r4r5-gc-ownership-proof'))
$relative = [System.IO.Path]::GetRelativePath($phaseRoot, $candidate)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) {
    throw "CandidateRoot must be a unique candidate below $phaseRoot."
}

$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
if (-not (Test-Path -LiteralPath $candidateManifestPath -PathType Leaf)) {
    throw "Qualified candidate manifest is missing: $candidateManifestPath"
}
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($candidateManifest.result -cne 'PASS_BUILD_READY_FOR_QAC_PREFLIGHT' -or
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
$serial = Join-Path $candidate 'qac-guest-001\serial.log'
$qmpLog = Join-Path $candidate 'qac-guest-001\qmp.log'
$runRoot = Split-Path -Parent $serial
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = Join-Path $repo 'OVMF.fd'
$attemptPath = Join-Path $phaseRoot 'guest-launch-attempt.json'
foreach ($required in @($qacCli,$qemu,$ovmf,(Join-Path $esp 'kernel.elf'),
        (Join-Path $esp 'ramdisk.img'),(Join-Path $esp 'EFI\BOOT\BOOTX64.EFI'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required guest input is missing: $required" }
}
if ((Get-Sha256 (Join-Path $esp 'kernel.elf')) -cne ([string]$candidateManifest.espKernelSha256).ToUpperInvariant() -or
    (Get-Sha256 (Join-Path $esp 'ramdisk.img')) -cne ([string]$candidateManifest.candidateRamdiskSha256).ToUpperInvariant() -or
    (Get-Sha256 (Join-Path $esp 'ramdisk.img')) -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8') {
    throw 'The isolated guest media no longer matches the qualified candidate and preserved source ramdisk.'
}
if (Test-Path -LiteralPath $attemptPath) { throw "The candidate already has a QAC guest attempt; refusing any second launch: $attemptPath" }
if (Test-Path -LiteralPath $runRoot) { throw "Refusing to overwrite guest evidence: $runRoot" }
New-Item -ItemType Directory -Path $runRoot | Out-Null

$manifestPath = Join-Path $runRoot 'guest-manifest.json'
$qacBeforeStatusPath = Join-Path $runRoot 'qac-before.status.txt'
$qacBeforeListPath = Join-Path $runRoot 'qac-before.list.txt'
$qacDuringStatusPath = Join-Path $runRoot 'qac-during.status.txt'
$qacDuringListPath = Join-Path $runRoot 'qac-during.list.txt'
$qacAfterStatusPath = Join-Path $runRoot 'qac-after.status.txt'
$qacAfterListPath = Join-Path $runRoot 'qac-after.list.txt'

function Get-QacSnapshot([string]$Prefix) {
    $statusLines = & $qacCli status 2>&1
    $statusExit = $LASTEXITCODE
    $listLines = & $qacCli list 2>&1
    $listExit = $LASTEXITCODE
    $statusText = ($statusLines | Out-String)
    $listText = ($listLines | Out-String)
    Set-Content -LiteralPath (Join-Path $runRoot "$Prefix.status.txt") -Value $statusText -Encoding utf8
    Set-Content -LiteralPath (Join-Path $runRoot "$Prefix.list.txt") -Value $listText -Encoding utf8
    return [pscustomobject]@{Status=$statusText;List=$listText;StatusExit=$statusExit;ListExit=$listExit}
}
function Test-QacClear([object]$Snapshot) {
    return $Snapshot.StatusExit -eq 0 -and $Snapshot.ListExit -eq 0 -and
        $Snapshot.Status -match '(?m)^Coordinator:\s+Online\b' -and
        $Snapshot.Status -match '(?m)^QEMU processes:\s+0 \(0 foreign\)\s*$' -and
        $Snapshot.Status -match '(?m)^Managed sessions:\s+0;\s+queued:\s+0;\s+blocked:\s+0\s*$' -and
        $Snapshot.List -match '(?m)^Coordinator:\s+Online\b' -and
        $Snapshot.List -match '(?m)^Reservations:\s+0\s*$' -and
        $Snapshot.List -match '(?m)^Managed sessions:\s+0\s*$'
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
        if ($null -ne $message.error) { throw "QMP $Command error: $line" }
        if ($null -ne $message.return -or $line -match '"return"\s*:') { return $line }
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
    guestBootLimit=1;automaticRetry=$false} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'qac-run-arguments.json') -Encoding utf8

$before = Get-QacSnapshot 'qac-before'
$preflightPassed = Test-QacClear $before
$qacProcess = $null
$stdoutTask = $null
$stderrTask = $null
$qmpConnected = $false
$admitted = $false
$qacReservationId = $null
$qacExitCode = $null
$qemuStatus = $null
$runError = $null
$serialText = ''
$guestPass = $false
$releaseVerified = $false
$during = $null
$after = $null
$attemptedUtc = $null

try {
    if (-not $preflightPassed) { throw 'BLOCKED_QAC_PREFLIGHT: coordinator/census is not online, empty, and internally consistent.' }

    # This CreateNew marker permanently prevents a second QAC invocation for this candidate.
    $attemptedUtc = [DateTime]::UtcNow.ToString('o')
    $attempt = [ordered]@{candidateRoot=$candidate;attemptedAtUtc=$attemptedUtc;maxGuestBoots=1;automaticRetry=$false;
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
    do {
        if ($qacProcess.HasExited) { throw "QAC exited before exclusive admission ($($qacProcess.ExitCode))." }
        $snapshot = Get-QacSnapshot 'qac-during'
        $ownerPattern = '\(PID\s+' + $qacProcess.Id + '\)'
        $ownedLine = @($snapshot.List -split "`r?`n" | Where-Object { $_ -match $ownerPattern }) | Select-Object -First 1
        if ($ownedLine -match '^\s*([0-9a-f]{32})\s+\|.*\|\s+Running\b') {
            $qacReservationId = $Matches[1]
            $admitted = $true
            $during = $snapshot
            break
        }
        if ($ownedLine -match '^\s*([0-9a-f]{32})\s+\|.*\|\s+Queued\b') { $qacReservationId = $Matches[1] }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $admissionDeadline)
    if (-not $admitted) { throw 'QAC did not grant its exclusive acceptance reservation inside the bounded admission window.' }

    $statusDuringConsistent = $during.Status -match '(?m)^Coordinator:\s+Online\b' -and
        $during.Status -match '(?m)^QEMU processes:\s+1 \(0 foreign\)\s*$' -and
        $during.Status -match '(?m)^Managed sessions:\s+1;\s+queued:\s+0;\s+blocked:\s+0\s*$'
    $listDuringConsistent = $during.List -match '(?m)^Reservations:\s+1\s*$' -and
        $during.List -match '(?m)^Managed sessions:\s+1\s*$' -and
        $during.List -match [regex]::Escape($qacReservationId) -and
        $during.List -match ('(?m)^\s*' + [regex]::Escape($qacReservationId) + '\s+\|\s+guideXOSServer\s+\|\s+acceptance\s+\|\s+Running\b')
    if (-not $statusDuringConsistent -or -not $listDuringConsistent) {
        throw 'QAC admitted the run but status/list did not provide a complete, consistent exclusive census.'
    }

    if (-not (Wait-Qmp 30 $qacProcess)) { throw 'QMP did not become reachable within 30 seconds of QAC admission.' }
    $qmpConnected = $true
    $deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)
    do {
        $serialText = Read-GuestSerial
        $guestPass = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=PASS callbacks=(?:0x)?00000003 result1=(?:0x)?00000000 result2=(?:0x)?00000000 result3=(?:0x)?00000000\s*$'
        if ($guestPass) { break }
        if ($qacProcess.HasExited) { break }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    $serialText = Read-GuestSerial
    $guestPass = $serialText -match '(?m)^\[C188R12R4R5-GC-OWNERSHIP\] outcome=PASS callbacks=(?:0x)?00000003 result1=(?:0x)?00000000 result2=(?:0x)?00000000 result3=(?:0x)?00000000\s*$'
    try { $qemuStatus = Invoke-Qmp 'query-status' } catch { $qemuStatus = 'query-status failed: ' + $_.Exception.Message }
    if (-not $qacProcess.HasExited) { [void](Invoke-Qmp 'quit') }
    if (-not $qacProcess.WaitForExit(90000)) { throw 'QAC did not exit after the owned QMP quit request.' }
    $qacExitCode = $qacProcess.ExitCode
} catch {
    $runError = $_.Exception.Message
    if ($null -ne $qacProcess -and -not $qacProcess.HasExited -and $admitted) {
        if (-not $qmpConnected -and (Wait-Qmp 10 $qacProcess)) { $qmpConnected = $true }
        if ($qmpConnected -and -not $qacProcess.HasExited) {
            try { [void](Invoke-Qmp 'query-status') } catch { }
            try { [void](Invoke-Qmp 'quit') } catch { }
            [void]$qacProcess.WaitForExit(90000)
        }
    }
    elseif ($null -ne $qacProcess -and -not $qacProcess.HasExited) {
        if ([string]::IsNullOrWhiteSpace($qacReservationId)) {
            $latestList = (& $qacCli list 2>&1 | Out-String)
            $ownerPattern = '\(PID\s+' + $qacProcess.Id + '\)'
            $ownedLine = @($latestList -split "`r?`n" | Where-Object { $_ -match $ownerPattern }) | Select-Object -First 1
            if ($ownedLine -match '^\s*([0-9a-f]{32})\s+\|.*\|\s+Queued\b') {
                $qacReservationId = $Matches[1]
            }
        }
        if (-not [string]::IsNullOrWhiteSpace($qacReservationId)) {
            $cancelOutput = (& $qacCli cancel $qacReservationId 2>&1 | Out-String)
            Set-Content -LiteralPath (Join-Path $runRoot 'qac-cancel.log') -Value $cancelOutput -Encoding utf8
            [void]$qacProcess.WaitForExit(90000)
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
        $after = Get-QacSnapshot 'qac-after'
        if (Test-QacClear $after) { $releaseVerified = $true; break }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $afterDeadline)
}

$serialText = Read-GuestSerial
$sourceRamdiskSha256 = Get-Sha256 (Join-Path $repo 'ESP\ramdisk.img')
$sourceRamdiskPreserved = $sourceRamdiskSha256 -ceq 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8'
$markerLines = @($serialText -split "`r?`n" | Where-Object { $_ -match '^marker=C188R12R4R5-GC-OWNERSHIP ' })
$markerPhases = @($markerLines | ForEach-Object { if ($_ -match '\bphase=([^ ]+)') { $Matches[1] } })
$startupFailure = $serialText -match '(?m)^\[nativeaot-gc-first-allocation\] RhInitialize return=(?:0x)?00000001\b' -or
    $serialText -match '(?m)^\[nativeaot-gc-first-allocation\] ALL_FAIL\s*$'
$outcome = if (-not $sourceRamdiskPreserved) { 'SOURCE_RAMDISK_CHANGED_DURING_GUEST' }
    elseif (-not $preflightPassed) { 'EXPERIMENT_BLOCKED_QAC_CENSUS' }
    elseif ($guestPass -and $releaseVerified -and $qacExitCode -eq 0) { 'GC_GRAPH_GUEST_PROVEN' }
    elseif ($startupFailure) { 'EXPERIMENT_BLOCKED' }
    elseif ($admitted) { 'GC_ROOT_OR_COLLECTION_DEFECT_OR_GUEST_INCOMPLETE' }
    else { 'EXPERIMENT_READY_BUT_GUEST_NOT_ADMITTED' }
$manifest = [ordered]@{
    result=$outcome;runRoot=$runRoot;candidateRoot=$candidate;candidateManifest=$candidateManifestPath
    candidateManifestSha256=Get-Sha256 $candidateManifestPath
    candidatePeSha256=[string]$candidateManifest.managedPeSha256
    candidateElfSha256=[string]$candidateManifest.managedElfSha256
    candidateKernelSha256=[string]$candidateManifest.specializedKernelSha256
    candidateRamdiskSha256=[string]$candidateManifest.candidateRamdiskSha256
    sourceRamdiskSha256=$sourceRamdiskSha256;sourceRamdiskPreserved=$sourceRamdiskPreserved
    qacPreflightPassed=$preflightPassed;qacAdmissionGranted=$admitted;qacReservationId=$qacReservationId
    qacOwnerPid=if ($null -ne $qacProcess) { $qacProcess.Id } else { $null }
    qacExitCode=$qacExitCode;qacReleaseVerified=$releaseVerified;qacGuestBootLimit=1;automaticRetry=$false
    qemuStatus=$qemuStatus;qmpPort=$qmpPort;qemuArguments=$qemuArgs;guestPass=$guestPass
    managedMarkerCount=$markerLines.Count;managedMarkerPhases=$markerPhases;startupFailure=$startupFailure
    serialLog=$serial;serialLogSha256=if (Test-Path -LiteralPath $serial) { Get-Sha256 $serial } else { $null }
    qmpLog=$qmpLog;qmpLogSha256=if (Test-Path -LiteralPath $qmpLog) { Get-Sha256 $qmpLog } else { $null }
    qacBeforeStatus=$qacBeforeStatusPath;qacBeforeList=$qacBeforeListPath
    qacDuringStatus=$qacDuringStatusPath;qacDuringList=$qacDuringListPath
    qacAfterStatus=$qacAfterStatusPath;qacAfterList=$qacAfterListPath
    qacArguments=(Join-Path $runRoot 'qac-run-arguments.json');guestAttempt=$attemptPath;error=$runError
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188R12R4R5_GC_OWNERSHIP=$outcome ADMITTED=$admitted PASS=$guestPass QAC_RELEASE=$releaseVerified"
Write-Output "C188R12R4R5_GC_OWNERSHIP_MANIFEST=$manifestPath"
if (-not $releaseVerified -and $admitted) { exit 4 }
if (-not $sourceRamdiskPreserved) { exit 6 }
if (-not $preflightPassed) { exit 3 }
if (-not $guestPass) { exit 2 }
if (-not $releaseVerified -or $qacExitCode -ne 0) { exit 5 }
