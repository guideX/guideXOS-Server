param(
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter()][ValidateRange(60,3600)][int]$MaxRuntimeSeconds = 1200,
    [Parameter()][ValidateRange(30,300)][int]$BootTimeoutSeconds = 180,
    [Parameter()][ValidateRange(30,1800)][int]$AdmissionTimeoutSeconds = 1750
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'C187 QAC guest runner requires PowerShell Core.' }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$bootRoot = [System.IO.Path]::GetFullPath($BootEvidencePath)
$esp = Join-Path $bootRoot 'esp'
$serial = Join-Path $bootRoot 'serial.log'
$qmpLog = Join-Path $bootRoot 'qmp.log'
$qacStdoutPath = Join-Path $bootRoot 'qac.stdout.log'
$qacStderrPath = Join-Path $bootRoot 'qac.stderr.log'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = Join-Path $repo 'OVMF.fd'
Import-Module (Join-Path $PSScriptRoot 'c181-qmp.psm1') -Force -ErrorAction Stop

foreach ($required in @($esp, (Join-Path $esp 'kernel.elf'), (Join-Path $esp 'ramdisk.img'), $qacCli, $qemu, $ovmf)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required C187 guest input is missing: $required" }
}
if (Test-Path -LiteralPath $serial) { throw "Refusing to overwrite guest evidence: $serial" }
New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null

$qacStatusBefore = (& $qacCli status 2>&1 | Out-String)
$qacListBefore = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $bootRoot 'qac-before.status.txt') -Value $qacStatusBefore -Encoding utf8
Set-Content -LiteralPath (Join-Path $bootRoot 'qac-before.list.txt') -Value $qacListBefore -Encoding utf8
if ($qacStatusBefore -notmatch 'Coordinator:\s+Online' -or $qacStatusBefore -notmatch 'QEMU processes:\s+\d+ \(0 foreign\)') {
    throw 'QAC is unavailable or reports a foreign QEMU process; C187 guest launch was not attempted.'
}

$mediaHashes = [ordered]@{}
foreach ($relative in @('kernel.elf','ramdisk.img','EFI/BOOT/BOOTX64.EFI')) {
    $path = Join-Path $esp $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidate ESP lacks $relative" }
    $mediaHashes[$relative] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
if ($mediaHashes['kernel.elf'] -ne $ExpectedKernelSha256.ToUpperInvariant()) {
    throw 'Candidate kernel SHA-256 does not match the caller-supplied isolated C187 build.'
}
$kernelText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes((Join-Path $esp 'kernel.elf')))
foreach ($marker in @('C171-WINDOW-CREATE','C172-COMPOSITOR-TRACE','C175-NATIVE-GEOMETRY','C187-NATIVE-CLOSE-ROUTE','C187-STALE-GENERATION')) {
    if (-not $kernelText.Contains($marker)) { throw "Candidate kernel is missing required marker $marker." }
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$listener.Start()
$qmpPort = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
$listener.Stop()
$qemuArgs = @(
    '-accel','tcg','-machine','pc','-smp','1',
    '-drive',('if=pflash,format=raw,readonly=on,file='+$ovmf),
    '-drive',('file=fat:rw:'+ $esp + ',format=raw,if=ide,index=0'),
    '-m','1024M','-vga','std','-display','none',
    '-serial',('file:'+ $serial),
    '-qmp',('tcp:127.0.0.1:{0},server,nowait' -f $qmpPort),
    '-boot','order=c','-no-shutdown','-rtc','base=utc,clock=host')
$argsEvidence = @('run','--project','guideXOSServer','--mode','acceptance','--',$qemu) + $qemuArgs
$reservationRequestUtc = [DateTime]::UtcNow
$argsEvidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bootRoot 'qac-run-arguments.json') -Encoding utf8
[ordered]@{qmpPort=$qmpPort;screenWidth=$null;screenHeight=$null;esp=$esp;serial=$serial} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bootRoot 'active.json') -Encoding utf8

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $qacCli
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($argument in $argsEvidence) { [void]$psi.ArgumentList.Add([string]$argument) }
$qacProcess = [System.Diagnostics.Process]::Start($psi)
$stdoutTask = $qacProcess.StandardOutput.ReadToEndAsync()
$stderrTask = $qacProcess.StandardError.ReadToEndAsync()
$qmpLogFile = [System.IO.File]::Open($qmpLog,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::ReadWrite)
$qmpWriter = [System.IO.StreamWriter]::new($qmpLogFile,[System.Text.Encoding]::UTF8)
$qmpWriter.AutoFlush = $true

function Read-GuestSerial {
    if (-not (Test-Path -LiteralPath $serial -PathType Leaf)) { return '' }
    $stream = [System.IO.FileStream]::new(
        $serial,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::UTF8,$true,4096,$true)
        try { return $reader.ReadToEnd() }
        finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Wait-QmpListener([int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $client = [System.Net.Sockets.TcpClient]::new()
        try { $client.Connect('127.0.0.1',$qmpPort); return }
        catch { if ($qacProcess.HasExited) { throw "QAC exited before QMP started: $($qacProcess.ExitCode)" }; Start-Sleep -Milliseconds 200 }
        finally { $client.Dispose() }
    } while ((Get-Date) -lt $deadline)
    throw 'QMP did not become available before the guest boot timeout.'
}

function Invoke-Qmp([string]$Command,[object]$Arguments=$null) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1',$qmpPort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 5000
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::ASCII,$false,1024,$true)
        $writer = [System.IO.StreamWriter]::new($stream,[System.Text.Encoding]::ASCII,1024,$true)
        $writer.AutoFlush = $true
        $greeting = $reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $capabilities = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command 'qmp_capabilities'
        $payload = [ordered]@{execute=$Command}
        if ($null -ne $Arguments) { $payload.arguments = $Arguments }
        $request = $payload | ConvertTo-Json -Compress -Depth 10
        $writer.WriteLine($request)
        $response = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command $Command
        $qmpWriter.WriteLine("command=$request`ngreeting=$greeting`ncapabilities=$capabilities`nresponse=$response`n")
        if ($response -match '"error"') { throw "QMP $Command failed: $response" }
        return $response
    } finally { $client.Dispose() }
}

try {
    # qac run owns admission and starts its QEMU child only after the exclusive
    # acceptance reservation is granted. If another managed reservation is
    # active, remain in QAC's FIFO queue rather than starting a parallel guest.
    $admissionDeadline = (Get-Date).AddSeconds($AdmissionTimeoutSeconds)
    $admitted = $false
    $lastQueueNotice = [DateTime]::MinValue
    do {
        if ($qacProcess.HasExited) { throw "QAC exited before acceptance admission: $($qacProcess.ExitCode)" }
        $qacListNow = (& $qacCli list 2>&1 | Out-String)
        $ownedLine = @($qacListNow -split "`r?`n" | Where-Object { $_ -match ('\(PID ' + $qacProcess.Id + '\)') }) | Select-Object -First 1
        if ($ownedLine -match 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running') {
            $admitted = $true
            break
        }
        if ((Get-Date) - $lastQueueNotice -gt [TimeSpan]::FromSeconds(30)) {
            Write-Output ("C187_QAC_ADMISSION_WAIT ownerPid={0} activeReservationCount={1}" -f $qacProcess.Id,([regex]::Matches($qacListNow,'(?m)^\s*[0-9a-f]{32}\s+\|')).Count)
            $lastQueueNotice = Get-Date
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $admissionDeadline)
    if (-not $admitted) {
        $qacListNow = (& $qacCli list 2>&1 | Out-String)
        $ownedLine = @($qacListNow -split "`r?`n" | Where-Object { $_ -match ('\(PID ' + $qacProcess.Id + '\)') }) | Select-Object -First 1
        if ($ownedLine -match '^\s*([0-9a-f]{32})\s+\|.*\|\s+Queued\b') {
            & $qacCli cancel $Matches[1] 2>&1 | Out-String | Set-Content -LiteralPath (Join-Path $bootRoot 'qac-cancel.log') -Encoding utf8
        }
        if (-not $qacProcess.HasExited) { [void]$qacProcess.WaitForExit(30000) }
        throw 'QAC did not grant exclusive acceptance admission before timeout.'
    }
    $admissionWaitSeconds = [Math]::Round(([DateTime]::UtcNow - $reservationRequestUtc).TotalSeconds,1)

    Wait-QmpListener $BootTimeoutSeconds
    $deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)
    $ready = $false
    do {
        $text = Read-GuestSerial
        if ($text -match '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$' -and
            $text -match '(?m)^\[KERNEL\] Input manager initialized') { $ready = $true; break }
        if ($qacProcess.HasExited) { throw "QAC/QEMU exited before guest readiness: $($qacProcess.ExitCode)" }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    if (-not $ready) { throw 'Guest did not reach the input loop before timeout.' }

    $text = Read-GuestSerial
    $widthMatch = [regex]::Match($text,'(?m)^\[DESKTOP CAP\] framebuffer_width=0x([0-9A-Fa-f]+)\r?$')
    $heightMatch = [regex]::Match($text,'(?m)^\[DESKTOP CAP\] framebuffer_height=0x([0-9A-Fa-f]+)\r?$')
    if (-not $widthMatch.Success -or -not $heightMatch.Success) { throw 'Guest omitted framebuffer dimensions.' }
    $width = [Convert]::ToInt32($widthMatch.Groups[1].Value,16)
    $height = [Convert]::ToInt32($heightMatch.Groups[1].Value,16)
    $active = [ordered]@{qmpPort=$qmpPort;screenWidth=$width;screenHeight=$height;esp=$esp;serial=$serial;pid=$qacProcess.Id}
    $active | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bootRoot 'active.json') -Encoding utf8
    $qacDuringStatus = (& $qacCli status 2>&1 | Out-String)
    $qacDuringList = (& $qacCli list 2>&1 | Out-String)
    Set-Content -LiteralPath (Join-Path $bootRoot 'qac-during.status.txt') -Value $qacDuringStatus -Encoding utf8
    Set-Content -LiteralPath (Join-Path $bootRoot 'qac-during.list.txt') -Value $qacDuringList -Encoding utf8
    if ($qacDuringList -notmatch 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running') {
        throw 'QAC did not report the isolated guest as an active acceptance reservation.'
    }
    [void](Invoke-Qmp 'screendump' ([ordered]@{filename=(Join-Path $bootRoot 'desktop-ready.ppm')}))
    Write-Output ("C187_GUEST_READY QMP_PORT={0} FRAMEBUFFER={1}x{2} PID={3}" -f $qmpPort,$width,$height,$qacProcess.Id)
    Write-Output 'Use scripts/dotnet/c187-qmp-control.py to send guest input; QMP quit ends this QAC reservation.'

    if (-not $qacProcess.WaitForExit($MaxRuntimeSeconds * 1000)) {
        try { [void](Invoke-Qmp 'quit') } catch { }
        if (-not $qacProcess.WaitForExit(90000)) { throw 'QAC did not complete after runtime limit cleanup request.' }
    }
} catch {
    if (-not $qacProcess.HasExited) {
        try { [void](Invoke-Qmp 'quit') } catch { }
        [void]$qacProcess.WaitForExit(90000)
    }
    throw
} finally {
    $qmpWriter.Dispose()
    $qmpLogFile.Dispose()
    if ($qacProcess.HasExited) {
        Set-Content -LiteralPath $qacStdoutPath -Value $stdoutTask.GetAwaiter().GetResult() -Encoding utf8
        $qacStderr = $stderrTask.GetAwaiter().GetResult()
        Set-Content -LiteralPath $qacStderrPath -Value $qacStderr -Encoding utf8
        Set-Content -LiteralPath (Join-Path $bootRoot 'qac-exit-code.txt') -Value $qacProcess.ExitCode -Encoding ascii
    }
}

$qacStatusAfter = (& $qacCli status 2>&1 | Out-String)
$qacListAfter = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $bootRoot 'qac-after.status.txt') -Value $qacStatusAfter -Encoding utf8
Set-Content -LiteralPath (Join-Path $bootRoot 'qac-after.list.txt') -Value $qacListAfter -Encoding utf8
$qacStderr = [System.IO.File]::ReadAllText($qacStderrPath)
$reservation = [regex]::Match($qacStderr,'QAC reservation requested: id=([0-9a-f]+)')
$released = [regex]::Match($qacStderr,'QAC reservation released: id=([0-9a-f]+)')
$childExit = [regex]::Match($qacStderr,'QAC QEMU exited: reservation=([0-9a-f]+) pid=(\d+) exitCode=(\d+)')
$releaseVerified = $reservation.Success -and $released.Success -and $childExit.Success -and
    $reservation.Groups[1].Value -eq $released.Groups[1].Value -and
    $reservation.Groups[1].Value -eq $childExit.Groups[1].Value -and
    [int]$childExit.Groups[3].Value -eq $qacProcess.ExitCode -and
    $qacListAfter -notmatch [regex]::Escape($reservation.Groups[1].Value)
if ($qacProcess.ExitCode -ne 0) { throw "QAC/QEMU exited with code $($qacProcess.ExitCode)." }
if ($qacStatusAfter -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or $qacListAfter -notmatch 'Reservations:\s+0') {
    throw 'QAC post-run census is not empty.'
}
if (-not $releaseVerified) { throw 'QAC did not confirm this QEMU process and reservation were released.' }

$serialText = Read-GuestSerial
$manifest = [ordered]@{
    result='PENDING';project='guideXOSServer';mode='acceptance';qacReleaseVerified=$true
    qacReservationId=$reservation.Groups[1].Value;qacQemuPid=$childExit.Groups[2].Value
    qemuExitCode=$qacProcess.ExitCode;qmpPort=$qmpPort;framebufferWidth=$width;framebufferHeight=$height
    qacAdmissionWaitSeconds=$admissionWaitSeconds
    mediaHashes=$mediaHashes;serialLog=$serial;qmpLog=$qmpLog
    bootReady=($serialText -match '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$')
    c187FactoryMarkers=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C187-FACTORY application=CounterApp factory=static\r?$')).Count
    c187WindowMarkers=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C187-WINDOW application=CounterApp result=PASS\r?$')).Count
    c187InputMarkers=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C187-INPUT application=CounterApp result=PASS\r?$')).Count
    c187NativeCloseMarkers=([regex]::Matches($serialText,'(?m)^\[C187-NATIVE-CLOSE-ROUTE\] selector=00000004 app=CounterApp result=PASS\r?$')).Count
    c187StaleGenerationMarkers=([regex]::Matches($serialText,'(?m)^\[C187-STALE-GENERATION\] selector=00000004 generation=[0-9A-F]{8} callback=input result=REJECTED proof=PASS\r?$')).Count
    applicationLaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000004 kind=normal\r?$')).Count
    surfaceMarkers=([regex]::Matches($serialText,'(?m)^\[C150-SURFACE\] action=(?:create|destroy).*\r?$')).Count
    serialSha256=(Get-FileHash -LiteralPath $serial -Algorithm SHA256).Hash
}
$manifest.result = if ($manifest.bootReady -and
    $manifest.applicationLaunchMarkers -ge 1 -and
    $manifest.c187FactoryMarkers -ge 1 -and
    $manifest.c187WindowMarkers -ge 1 -and
    $manifest.c187InputMarkers -ge 1 -and
    $manifest.c187NativeCloseMarkers -ge 1 -and
    $manifest.c187StaleGenerationMarkers -ge 1 -and
    $manifest.surfaceMarkers -ge 2) { 'PASS' } else { 'FAIL' }
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bootRoot 'acceptance-manifest.json') -Encoding utf8
if ($manifest.result -ne 'PASS') {
    throw 'C187 guest execution evidence is incomplete; see acceptance-manifest.json and serial.log.'
}
Write-Output ("C187_QAC_RELEASE=VERIFIED APP_LAUNCHES={0} FACTORY={1} WINDOW={2} INPUT={3} CLOSE_ROUTE={4} STALE_REJECT={5}" -f $manifest.applicationLaunchMarkers,$manifest.c187FactoryMarkers,$manifest.c187WindowMarkers,$manifest.c187InputMarkers,$manifest.c187NativeCloseMarkers,$manifest.c187StaleGenerationMarkers)
