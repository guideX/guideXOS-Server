[CmdletBinding()]
param(
    [ValidateRange(20, 300)]
    [int]$TimeoutSeconds = 90,
    [string]$ArtifactRoot = '',
    [string]$BootloaderPath = ''
)

$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$KernelRoot = Join-Path $Root 'kernel'
$KernelBuild = Join-Path $KernelRoot 'build\amd64\bin\kernel.elf'
$SeedEsp = Join-Path $Root 'ESP'
$OvmfCode = Join-Path $Root 'OVMF.fd'
$BootloaderCandidates = @(
    (Join-Path $SeedEsp 'EFI\BOOT\BOOTX64.EFI'),
    (Join-Path $Root 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'),
    (Join-Path $Root 'out\mc7-proof\esp\EFI\BOOT\BOOTX64.EFI')
)
$AppRoot = Join-Path $Root 'Apps\StellarNavigator'
$AppManifest = Join-Path $AppRoot 'app.json'
$AppElf = Join-Path $AppRoot 'bin\amd64\stellarnavigator.elf'
$Dataset = Join-Path $AppRoot 'Astronomy.Ephemeris\Data\ephemeris_de440s_chebyshev.sn4bin'
$Make = 'C:\mingw64\bin\mingw32-make.exe'
$Qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'

if (-not $ArtifactRoot) {
    $ArtifactRoot = Join-Path $env:USERPROFILE ('.codex\artifacts\SN10_' + (Get-Date -Format 'yyyyMMdd'))
}
if (-not $BootloaderPath) {
    $BootloaderPath = $BootloaderCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $BootloaderPath) {
    throw 'No UEFI bootloader found. Build the bootloader with build.ps1 or pass -BootloaderPath.'
}
$BootloaderSource = [IO.Path]::GetFullPath($BootloaderPath)
$RunId = Get-Date -Format 'yyyyMMddTHHmmssfff'
$RunRoot = Join-Path ([IO.Path]::GetFullPath($ArtifactRoot)) ("qemu\" + $RunId)
$Esp = Join-Path $RunRoot 'esp'
$SerialLog = Join-Path $RunRoot 'guest-serial.log'
$QemuLog = Join-Path $RunRoot 'qemu.log'
$GuestScreenshot = Join-Path $RunRoot 'guest.ppm'
$ResultPath = Join-Path $RunRoot 'result.json'

foreach ($path in @($Make, $Qemu, $AppManifest, $AppElf, $Dataset, $SeedEsp, $OvmfCode, $BootloaderSource)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required SN10 guest input is missing: $path" }
}
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

# There is no QAC coordinator on this host. Use a direct system-wide process
# census as the exclusive lane check, and never interact with a foreign QEMU.
$qacCommand = Get-Command qac -ErrorAction SilentlyContinue
$qacProcesses = @(Get-Process -Name 'qac*' -ErrorAction SilentlyContinue)
if ($qacCommand -or $qacProcesses.Count -gt 0) {
    throw 'A QAC coordinator or process is present; use its reservation lane instead of this direct-census runner.'
}
$existingQemu = @(Get-Process -Name 'qemu*' -ErrorAction SilentlyContinue)
if ($existingQemu.Count -gt 0) {
    $owners = $existingQemu | ForEach-Object { "PID=$($_.Id) Name=$($_.ProcessName)" }
    throw "QEMU lane is occupied; no process was changed. $($owners -join '; ')"
}

Write-Host '[SN10-QEMU] compiling guest proof kernel with the app-model launch driver'
& $Make '-C' $KernelRoot 'ARCH=amd64' 'EXTRA_CFLAGS=-DGXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF' '-B' '-j4'
if ($LASTEXITCODE -ne 0) { throw "SN10 proof-kernel build failed with exit code $LASTEXITCODE." }
if (-not (Test-Path -LiteralPath $KernelBuild -PathType Leaf)) { throw "Proof kernel output is missing: $KernelBuild" }

New-Item -ItemType Directory -Force -Path $Esp | Out-Null
Get-ChildItem -LiteralPath $SeedEsp -Force | Copy-Item -Destination $Esp -Recurse -Force
New-Item -ItemType Directory -Force -Path (Join-Path $Esp 'EFI\BOOT') | Out-Null
Copy-Item -LiteralPath $BootloaderSource -Destination (Join-Path $Esp 'EFI\BOOT\BOOTX64.EFI') -Force
Copy-Item -LiteralPath $KernelBuild -Destination (Join-Path $Esp 'kernel.elf') -Force

$StagedApp = Join-Path $Esp 'Apps\StellarNavigator'
New-Item -ItemType Directory -Force -Path (Join-Path $StagedApp 'bin\amd64'),
    (Join-Path $StagedApp 'Astronomy.Ephemeris\Data') | Out-Null
Copy-Item -LiteralPath $AppManifest -Destination (Join-Path $StagedApp 'app.json') -Force
Copy-Item -LiteralPath $AppElf -Destination (Join-Path $StagedApp 'bin\amd64\stellarnavigator.elf') -Force
Copy-Item -LiteralPath $Dataset -Destination (Join-Path $StagedApp 'Astronomy.Ephemeris\Data\ephemeris_de440s_chebyshev.sn4bin') -Force

$monitorProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$monitorProbe.Start()
$MonitorPort = ([Net.IPEndPoint]$monitorProbe.LocalEndpoint).Port
$monitorProbe.Stop()

$QemuArgs = @(
    '-machine', 'pc,usb=off',
    '-drive', "if=pflash,format=raw,readonly=on,file=`"$OvmfCode`"",
    '-drive', "file=fat:rw:`"$Esp`",format=raw,if=ide,index=0",
    '-m', '1024M',
    '-vga', 'std',
    '-display', 'none',
    '-serial', "file:$SerialLog",
    '-monitor', "tcp:127.0.0.1:$MonitorPort,server,nowait",
    '-D', $QemuLog,
    '-rtc', 'base=utc,clock=host',
    '-no-reboot'
)
$StartInfo = [Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.WorkingDirectory = $Root
$StartInfo.UseShellExecute = $false
$StartInfo.CreateNoWindow = $true
$StartInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
foreach ($argument in $QemuArgs) { $StartInfo.ArgumentList.Add($argument) }

$script:OwnedQemu = $null
$script:MonitorPort = $MonitorPort

function Send-OwnedMonitorCommand([string]$Command) {
    if (-not $script:OwnedQemu -or $script:OwnedQemu.HasExited) { return '' }
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $client.ReceiveTimeout = 500
        $client.SendTimeout = 1000
        $client.Connect([Net.IPAddress]::Loopback, $script:MonitorPort)
        $stream = $client.GetStream()
        $writer = [IO.StreamWriter]::new($stream)
        $writer.NewLine = "`n"
        $writer.AutoFlush = $true
        $writer.WriteLine($Command)
        Start-Sleep -Milliseconds 250
        $response = ''
        $buffer = New-Object byte[] 4096
        while ($stream.DataAvailable) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            $response += [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
        }
        return $response
    } catch {
        return ''
    } finally {
        $client.Dispose()
    }
}

function Get-SerialText {
    if (Test-Path -LiteralPath $SerialLog) {
        return [IO.File]::ReadAllText($SerialLog)
    }
    return ''
}

function Wait-SerialPattern([string]$Pattern, [int]$Seconds) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        if ($script:OwnedQemu.HasExited) { throw "QEMU exited before guest marker appeared: $Pattern" }
        $others = @(Get-Process -Name 'qemu*' -ErrorAction SilentlyContinue |
            Where-Object { $_.Id -ne $script:OwnedQemu.Id })
        if ($others.Count -gt 0) {
            Send-OwnedMonitorCommand 'quit' | Out-Null
            throw 'A foreign QEMU appeared during the guest run; this runner requested shutdown only for its own QEMU.'
        }
        $text = Get-SerialText
        if ($text -match $Pattern) { return $text }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for guest serial marker: $Pattern"
}

$check = [ordered]@{
    lane = 'direct-exclusive-qemu-process-census'
    qacCoordinator = 'unavailable'
    qemuForeignAtStart = 0
    appModelPackageDiscovered = $false
    appModelLaunchSucceeded = $false
    authenticatedEphemerisEvaluatedInGuest = $false
    nativeWindowCreated = $false
    nativeContentDrawn = $false
    appShutdownReturned = $false
    loaderCleanupPassed = $false
    guestScreenshot = $false
    candidateSha256 = (Get-FileHash -LiteralPath $AppElf -Algorithm SHA256).Hash.ToLowerInvariant()
    datasetSha256 = (Get-FileHash -LiteralPath $Dataset -Algorithm SHA256).Hash.ToLowerInvariant()
    kernelSha256 = ''
    serialLog = $SerialLog
    qemuLog = $QemuLog
    result = 'running'
}

try {
    Write-Host "[SN10-QEMU] isolated guest ESP: $Esp"
    Write-Host "[SN10-QEMU] serial evidence: $SerialLog"
    $check.kernelSha256 = (Get-FileHash -LiteralPath $KernelBuild -Algorithm SHA256).Hash.ToLowerInvariant()
    $script:OwnedQemu = [Diagnostics.Process]::Start($StartInfo)
    if (-not $script:OwnedQemu) { throw 'Could not start the SN10 QEMU process.' }

    $text = Wait-SerialPattern 'App Model package discovered.*Stellar Navigator|displayName=Stellar Navigator|STELLAR_NAVIGATOR_EPHEMERIS' $TimeoutSeconds
    $check.appModelPackageDiscovered = $text -match 'App Model package discovered.*Stellar Navigator|displayName=Stellar Navigator'
    $text = Wait-SerialPattern 'STELLAR_NAVIGATOR_UI status=initial-content-drawn' $TimeoutSeconds
    $check.authenticatedEphemerisEvaluatedInGuest = $text -match 'STELLAR_NAVIGATOR_EPHEMERIS status=available body=earth target_naif=399 et_tdb_s=0\.000000 frame=J2000_SSB units=km position_km=\(-27566740\.483,132361381\.154,57418653\.286\) sha256=d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a'
    $check.nativeWindowCreated = $text -match 'STELLAR_NAVIGATOR_UI status=window-created'
    $check.nativeContentDrawn = $text -match 'STELLAR_NAVIGATOR_UI status=initial-content-drawn'
    if (-not $check.authenticatedEphemerisEvaluatedInGuest -or -not $check.nativeWindowCreated -or -not $check.nativeContentDrawn) {
        throw 'Guest did not report the authenticated fixed-ET state and a successfully drawn native window.'
    }

    $screenshotResponse = Send-OwnedMonitorCommand ("screendump `"$GuestScreenshot`"")
    if (Test-Path -LiteralPath $GuestScreenshot -PathType Leaf) {
        $check.guestScreenshot = (Get-Item -LiteralPath $GuestScreenshot).Length -gt 1024
    }

    Send-OwnedMonitorCommand 'sendkey esc' | Out-Null
    $text = Wait-SerialPattern 'STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown' $TimeoutSeconds
    $check.appShutdownReturned = $true
    $text = Wait-SerialPattern '\[SN10-GUEST\] appmodel-launch result=success' $TimeoutSeconds
    $check.appModelLaunchSucceeded = $true
    $check.loaderCleanupPassed = $text -match 'lifecycle PASS window/resource cleanup complete'
    if (-not $check.loaderCleanupPassed) { throw 'Native ELF loader did not report window/resource cleanup PASS.' }

    Send-OwnedMonitorCommand 'quit' | Out-Null
    if (-not $script:OwnedQemu.WaitForExit(10000)) { throw 'Owned QEMU did not exit after its monitor quit command.' }

    $check.result = 'PASS'
    $text = Get-SerialText
    [IO.File]::WriteAllText((Join-Path $RunRoot 'guest-serial-final.log'), $text)
    $check | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ResultPath -Encoding utf8
    Write-Host '[SN10-QEMU] PASS: native App Model dispatch, authenticated ephemeris, drawn UI, app shutdown, loader cleanup'
    Write-Host "[SN10-QEMU] structured result: $ResultPath"
    if ($check.guestScreenshot) { Write-Host "[SN10-QEMU] guest screenshot: $GuestScreenshot" }
    else { Write-Host "[SN10-QEMU] screenshot unavailable from the headless QEMU display: $screenshotResponse" }
} catch {
    $check.result = 'FAIL'
    $check.error = $_.Exception.Message
    if ($script:OwnedQemu -and -not $script:OwnedQemu.HasExited) {
        Send-OwnedMonitorCommand 'quit' | Out-Null
        if (-not $script:OwnedQemu.WaitForExit(5000) -and -not $script:OwnedQemu.HasExited) {
            Stop-Process -Id $script:OwnedQemu.Id -Force -ErrorAction SilentlyContinue
        }
    }
    $text = Get-SerialText
    if ($text) { [IO.File]::WriteAllText((Join-Path $RunRoot 'guest-serial-final.log'), $text) }
    $check | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ResultPath -Encoding utf8
    Write-Host "[SN10-QEMU] FAIL: $($check.error)"
    Write-Host "[SN10-QEMU] evidence: $RunRoot"
    throw
} finally {
    if ($script:OwnedQemu) { $script:OwnedQemu.Dispose() }
}
