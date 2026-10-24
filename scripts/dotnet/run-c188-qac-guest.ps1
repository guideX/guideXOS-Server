param(
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter(Mandatory=$true)][ValidateSet('identity','lifecycle','coexistence','production-startup','production-apps','production-lifecycle')][string]$Scenario,
    [Parameter()][switch]$ProductionCompatibility,
    [Parameter()][ValidateRange(60,3600)][int]$MaxRuntimeSeconds = 1200,
    [Parameter()][ValidateRange(30,300)][int]$BootTimeoutSeconds = 180,
    [Parameter()][ValidateRange(30,1800)][int]$AdmissionTimeoutSeconds = 1750
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'C188 QAC guest runner requires PowerShell Core.' }
$isProductionScenario = $Scenario.StartsWith('production-',[System.StringComparison]::Ordinal)
if ([bool]$ProductionCompatibility -ne $isProductionScenario) {
    throw 'Use -ProductionCompatibility only with a production-* scenario, and use it for every production compatibility boot.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoots = @(
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188-external-app-identity')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r5-notes-proof-invocation')))
$bootRoot = [System.IO.Path]::GetFullPath($BootEvidencePath)
$phaseRoot = @($phaseRoots | Where-Object {
    $relative = [System.IO.Path]::GetRelativePath($_, $bootRoot)
    $relative -ne '.' -and -not $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -and
        -not [System.IO.Path]::IsPathRooted($relative)
} | Select-Object -First 1)
if ($phaseRoot.Count -eq 0) {
    throw "C188 guest evidence must remain below one of: $($phaseRoots -join ', ')."
}
$phaseRoot = $phaseRoot[0]
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
    if (-not (Test-Path -LiteralPath $required)) { throw "Required C188 guest input is missing: $required" }
}
foreach ($newEvidence in @($serial,$qmpLog,(Join-Path $bootRoot 'active.json'),(Join-Path $bootRoot 'acceptance-manifest.json'))) {
    if (Test-Path -LiteralPath $newEvidence) { throw "Refusing to overwrite guest evidence: $newEvidence" }
}
New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null

$qacStatusBefore = (& $qacCli status 2>&1 | Out-String)
$qacListBefore = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $bootRoot 'qac-before.status.txt') -Value $qacStatusBefore -Encoding utf8
Set-Content -LiteralPath (Join-Path $bootRoot 'qac-before.list.txt') -Value $qacListBefore -Encoding utf8
if ($qacStatusBefore -notmatch 'Coordinator:\s+Online' -or $qacStatusBefore -notmatch 'QEMU processes:\s+\d+ \(0 foreign\)') {
    throw 'QAC is unavailable or reports a foreign QEMU process; C188 guest launch was not attempted.'
}
$c188r5Root = [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r5-notes-proof-invocation'))
$isC188R5Boot = $bootRoot.StartsWith($c188r5Root.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase)
if ($isC188R5Boot -and (
        $qacStatusBefore -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
        $qacListBefore -notmatch 'Reservations:\s+0' -or
        $qacListBefore -notmatch 'Managed sessions:\s+0')) {
    throw 'C188R5 requires exclusive QAC admission; an existing reservation or managed session was detected.'
}

$mediaHashes = [ordered]@{}
foreach ($relative in @('kernel.elf','ramdisk.img','EFI/BOOT/BOOTX64.EFI')) {
    $path = Join-Path $esp $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidate ESP lacks $relative" }
    $mediaHashes[$relative] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
if ($mediaHashes['kernel.elf'] -ne $ExpectedKernelSha256.ToUpperInvariant()) {
    throw 'Candidate kernel SHA-256 does not match the caller-supplied isolated C188 build.'
}
$kernelText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes((Join-Path $esp 'kernel.elf')))
$productionIdentityAbsent = $false
if ($ProductionCompatibility) {
    foreach ($identity in @('com.guidexos.apps.managed.notes','com.guidexos.apps.managed.settingscenter','com.guidexos.apps.managed.calculator','com.guidexos.apps.managed.taskmanager','com.guidexos.apps.managed.fileexplorer')) {
        if (-not $kernelText.Contains($identity)) { throw "Proof-disabled production kernel is missing registered identity $identity." }
    }
    if ($kernelText.Contains('com.guidexos.apps.managed.counterapp') -or
        $kernelText.Contains('C188-')) {
        throw 'Proof-disabled production kernel contains C188 development identity content.'
    }
    $productionIdentityAbsent = $true
} else {
    foreach ($marker in @('C171-WINDOW-CREATE','C172-COMPOSITOR-TRACE','C175-NATIVE-GEOMETRY','C188-IDENTITY-REGISTRY','C188-LAUNCHER-COUNTS','C188-STALE-GENERATION')) {
        if (-not $kernelText.Contains($marker)) { throw "Candidate kernel is missing required marker $marker." }
    }
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
            Write-Output ("C188_QAC_ADMISSION_WAIT ownerPid={0} activeReservationCount={1}" -f $qacProcess.Id,([regex]::Matches($qacListNow,'(?m)^\s*[0-9a-f]{32}\s+\|')).Count)
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
    $allProgramsCount = if ($ProductionCompatibility) { 21 } else { 22 }
    $active = [ordered]@{qmpPort=$qmpPort;screenWidth=$width;screenHeight=$height;allProgramsCount=$allProgramsCount;productionCompatibility=[bool]$ProductionCompatibility;esp=$esp;serial=$serial;pid=$qacProcess.Id}
    $active | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bootRoot 'active.json') -Encoding utf8
    $qacDuringStatus = (& $qacCli status 2>&1 | Out-String)
    $qacDuringList = (& $qacCli list 2>&1 | Out-String)
    Set-Content -LiteralPath (Join-Path $bootRoot 'qac-during.status.txt') -Value $qacDuringStatus -Encoding utf8
    Set-Content -LiteralPath (Join-Path $bootRoot 'qac-during.list.txt') -Value $qacDuringList -Encoding utf8
    if ($qacDuringList -notmatch 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running') {
        throw 'QAC did not report the isolated guest as an active acceptance reservation.'
    }
    [void](Invoke-Qmp 'screendump' ([ordered]@{filename=(Join-Path $bootRoot 'desktop-ready.ppm')}))
    Write-Output ("C188_GUEST_READY QMP_PORT={0} FRAMEBUFFER={1}x{2} PID={3}" -f $qmpPort,$width,$height,$qacProcess.Id)
    Write-Output 'Use scripts/dotnet/c188-qmp-control.py to send guest input; QMP quit ends this QAC reservation.'

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
    productionCompatibility=[bool]$ProductionCompatibility
    productionIdentityAbsent=$productionIdentityAbsent
    qacReservationId=$reservation.Groups[1].Value;qacQemuPid=$childExit.Groups[2].Value
    qemuExitCode=$qacProcess.ExitCode;qmpPort=$qmpPort;framebufferWidth=$width;framebufferHeight=$height
    qacAdmissionWaitSeconds=$admissionWaitSeconds
    mediaHashes=$mediaHashes;serialLog=$serial;qmpLog=$qmpLog
    bootReady=($serialText -match '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$')
    scenario=$Scenario
    identityRegistryMarkers=([regex]::Matches($serialText,'(?m)^\[C188-IDENTITY-REGISTRY\] counterapp=com\.guidexos\.apps\.managed\.counterapp selector=9 production-selectors=4-8-preserved result=PASS\r?$')).Count
    launcherCountMarkers=([regex]::Matches($serialText,'(?m)^\[C188-LAUNCHER-COUNTS\].* result=PASS\r?$')).Count
    taskManagerRegistryMarkers=([regex]::Matches($serialText,'(?m)^\[C188-TM-REGISTRY\] identity=com\.guidexos\.apps\.managed\.taskmanager selector=7 .* result=PASS\r?$')).Count
    c188FactoryMarkers=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188-FACTORY selector=9 identity=com\.guidexos\.apps\.managed\.counterapp factory=static\r?$')).Count
    c188WindowMarkers=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188-WINDOW selector=9 identity=com\.guidexos\.apps\.managed\.counterapp result=PASS\r?$')).Count
    c188InputMarkers=([regex]::Matches($serialText,'(?m)^\[C102-MANAGED-OUTPUT\] C188-INPUT selector=9 identity=com\.guidexos\.apps\.managed\.counterapp result=PASS\r?$')).Count
    c188LifetimeCloseMarkers=([regex]::Matches($serialText,'(?m)^\[C150-LIFETIME-CLOSE\] selector=00000009 result=PASS\r?$')).Count
    c188StaleGenerationMarkers=([regex]::Matches($serialText,'(?m)^\[C188-STALE-GENERATION\] selector=00000009 generation=[0-9A-F]{8} callback=input result=REJECTED proof=PASS\r?$')).Count
    counterAppLaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000009 kind=normal\r?$')).Count
    productionSelector4LaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000004 kind=normal\r?$')).Count
    productionSelector5LaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000005 kind=normal\r?$')).Count
    productionSelector6LaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000006 kind=normal\r?$')).Count
    productionSelector7LaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000007 kind=normal\r?$')).Count
    productionSelector8LaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000008 kind=normal\r?$')).Count
    productionNotesSurfaceMarkers=([regex]::Matches($serialText,'(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=')).Count
    productionSettingsSurfaceMarkers=([regex]::Matches($serialText,'(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.settingscenter generation=')).Count
    productionCalculatorSurfaceMarkers=([regex]::Matches($serialText,'(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.calculator generation=')).Count
    productionTaskManagerSurfaceMarkers=([regex]::Matches($serialText,'(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.taskmanager generation=')).Count
    productionFileExplorerSurfaceMarkers=([regex]::Matches($serialText,'(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.fileexplorer generation=')).Count
    productionNotesLifetimeCloseMarkers=([regex]::Matches($serialText,'(?m)^\[C150-LIFETIME-CLOSE\] selector=00000004 result=PASS\r?$')).Count
    productionNotesCloseApprovalMarkers=([regex]::Matches($serialText,'(?m)^\[C152-CLOSE\] request=accepted dirty=false result=PASS\r?$')).Count
    c188SerialMarkers=([regex]::Matches($serialText,'(?m)^.*C188[-A-Z].*$')).Count
    notesLaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000004 kind=normal\r?$')).Count
    taskManagerLaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000007 kind=normal\r?$')).Count
    fileExplorerLaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000008 kind=normal\r?$')).Count
    calculatorLaunchMarkers=([regex]::Matches($serialText,'(?m)^\[C150-APP-LAUNCH\].*selector=00000006 kind=normal\r?$')).Count
    surfaceMarkers=([regex]::Matches($serialText,'(?m)^\[C150-SURFACE\] action=(?:create|destroy).*\r?$')).Count
    serialSha256=(Get-FileHash -LiteralPath $serial -Algorithm SHA256).Hash
}
$commonPass = if ($ProductionCompatibility) {
    $manifest.bootReady -and $manifest.productionIdentityAbsent -and $manifest.c188SerialMarkers -eq 0
} else {
    $manifest.bootReady -and $manifest.identityRegistryMarkers -ge 1 -and
        $manifest.launcherCountMarkers -ge 1 -and $manifest.taskManagerRegistryMarkers -ge 1
}
$scenarioPass = switch ($Scenario) {
    'identity' {
        $manifest.counterAppLaunchMarkers -ge 1 -and $manifest.c188FactoryMarkers -ge 1 -and
            $manifest.c188WindowMarkers -ge 1 -and $manifest.c188InputMarkers -ge 1 -and
            $manifest.taskManagerLaunchMarkers -ge 1
        break
    }
    'lifecycle' {
        $manifest.counterAppLaunchMarkers -ge 2 -and $manifest.c188FactoryMarkers -ge 2 -and
            $manifest.c188WindowMarkers -ge 2 -and $manifest.c188LifetimeCloseMarkers -ge 1 -and
            $manifest.c188StaleGenerationMarkers -ge 1
        break
    }
    'coexistence' {
        $manifest.counterAppLaunchMarkers -ge 1 -and $manifest.notesLaunchMarkers -ge 1 -and
            $manifest.taskManagerLaunchMarkers -ge 1 -and $manifest.fileExplorerLaunchMarkers -ge 1 -and
            $manifest.calculatorLaunchMarkers -ge 1 -and $manifest.c188FactoryMarkers -ge 1 -and
            $manifest.c188WindowMarkers -ge 1 -and $manifest.c188InputMarkers -ge 1
        break
    }
    'production-startup' {
        $manifest.productionSelector4LaunchMarkers -ge 1 -and
            $manifest.productionNotesSurfaceMarkers -ge 1 -and
            $manifest.counterAppLaunchMarkers -eq 0
        break
    }
    'production-apps' {
        $manifest.productionSelector4LaunchMarkers -ge 1 -and
            $manifest.productionSelector5LaunchMarkers -ge 1 -and
            $manifest.productionSelector6LaunchMarkers -ge 1 -and
            $manifest.productionSelector7LaunchMarkers -ge 1 -and
            $manifest.productionSelector8LaunchMarkers -ge 1 -and
            $manifest.productionNotesSurfaceMarkers -ge 1 -and
            $manifest.productionSettingsSurfaceMarkers -ge 1 -and
            $manifest.productionCalculatorSurfaceMarkers -ge 1 -and
            $manifest.productionTaskManagerSurfaceMarkers -ge 1 -and
            $manifest.productionFileExplorerSurfaceMarkers -ge 1 -and
            $manifest.counterAppLaunchMarkers -eq 0
        break
    }
    'production-lifecycle' {
        $manifest.productionSelector4LaunchMarkers -ge 2 -and
            $manifest.productionNotesSurfaceMarkers -ge 2 -and
            $manifest.productionNotesLifetimeCloseMarkers -ge 1 -and
            $manifest.productionNotesCloseApprovalMarkers -ge 1 -and
            $manifest.counterAppLaunchMarkers -eq 0
        break
    }
}
$manifest.result = if ($commonPass -and $scenarioPass) { 'PASS' } else { 'FAIL' }
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bootRoot 'acceptance-manifest.json') -Encoding utf8
if ($manifest.result -ne 'PASS') {
    throw "C188 $Scenario guest evidence is incomplete; see acceptance-manifest.json and serial.log."
}
if ($ProductionCompatibility) {
    Write-Output ("C188_PRODUCTION_QAC_RELEASE=VERIFIED SCENARIO={0} SELECTORS=4-8 COUNTERAPP_LAUNCHES={1} NOTES_CLOSES={2}" -f $Scenario,$manifest.counterAppLaunchMarkers,$manifest.productionNotesLifetimeCloseMarkers)
} else {
    Write-Output ("C188_QAC_RELEASE=VERIFIED SCENARIO={0} COUNTERAPP_LAUNCHES={1} FACTORY={2} WINDOW={3} INPUT={4} CLOSE={5} STALE_REJECT={6}" -f $Scenario,$manifest.counterAppLaunchMarkers,$manifest.c188FactoryMarkers,$manifest.c188WindowMarkers,$manifest.c188InputMarkers,$manifest.c188LifetimeCloseMarkers,$manifest.c188StaleGenerationMarkers)
}
