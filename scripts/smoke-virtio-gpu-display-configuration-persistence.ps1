param(
    [int]$TimeoutSeconds = 120,
    [switch]$FormatOnly,
    [string]$ImagePath
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Batch = Join-Path $Root 'scripts\run-qemu-display-probe.bat'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunRoot = Join-Path $Root ("logs\qemu-display-configuration-persistence-" + $Stamp)
$BootArtifact = Join-Path $RunRoot 'boot-esp'
$Artifact = Join-Path $RunRoot 'persistent-config-store'
$CaptureRoot1 = Join-Path $RunRoot 'launch1\captures'
$CaptureRoot2 = Join-Path $RunRoot 'launch2\captures'
New-Item -ItemType Directory -Force -Path $RunRoot,$CaptureRoot1,$CaptureRoot2 | Out-Null

function Find-Qemu {
    $command = Get-Command 'qemu-system-x86_64' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        'C:\qemu\qemu-system-x86_64.exe')) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    try { $listener.Start(); return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Get-ArtifactFingerprint {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $file = Get-Item -LiteralPath $Path
        return ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() + "|" + $file.Length)
    }
    $records = @()
    foreach ($file in @(Get-ChildItem -LiteralPath $Path -File -Recurse -Force | Sort-Object FullName)) {
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $records += ((Resolve-Path -LiteralPath $file.FullName).Path.Substring($Path.Length).ToLowerInvariant() + "|" + $file.Length + "|" + $hash)
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($records -join "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Set-ImageUInt16 {
    param([byte[]]$Image,[int]$Offset,[int]$Value)
    $Image[$Offset] = [byte]($Value -band 0xFF)
    $Image[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
}

function Set-ImageUInt32 {
    param([byte[]]$Image,[int]$Offset,[uint32]$Value)
    $Image[$Offset] = [byte]($Value -band 0xFF)
    $Image[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
    $Image[$Offset + 2] = [byte](($Value -shr 16) -band 0xFF)
    $Image[$Offset + 3] = [byte](($Value -shr 24) -band 0xFF)
}

function Set-ImageText {
    param([byte[]]$Image,[int]$Offset,[int]$Length,[string]$Value)
    $bytes = [Text.Encoding]::ASCII.GetBytes($Value)
    for ($index = 0; $index -lt $Length; ++$index) {
        $Image[$Offset + $index] = if ($index -lt $bytes.Length) { $bytes[$index] } else { 0x20 }
    }
}

function New-Fat32Image {
    param([Parameter(Mandatory=$true)][string]$Path)
    $bytesPerSector = 512
    $totalSectors = 131072
    $reservedSectors = 32
    $fatCount = 2
    $sectorsPerCluster = 1
    $fatSectors = 1024
    $image = New-Object byte[] ($bytesPerSector * $totalSectors)

    $boot = 0
    $image[$boot] = 0xEB
    $image[$boot + 1] = 0x58
    $image[$boot + 2] = 0x90
    Set-ImageText $image ($boot + 3) 8 'GXOSPERS'
    Set-ImageUInt16 $image ($boot + 11) $bytesPerSector
    $image[$boot + 13] = $sectorsPerCluster
    Set-ImageUInt16 $image ($boot + 14) $reservedSectors
    $image[$boot + 16] = $fatCount
    Set-ImageUInt16 $image ($boot + 17) 0
    Set-ImageUInt16 $image ($boot + 19) 0
    $image[$boot + 21] = 0xF8
    Set-ImageUInt16 $image ($boot + 22) 0
    Set-ImageUInt16 $image ($boot + 24) 63
    Set-ImageUInt16 $image ($boot + 26) 255
    Set-ImageUInt32 $image ($boot + 28) 0
    Set-ImageUInt32 $image ($boot + 32) $totalSectors
    Set-ImageUInt32 $image ($boot + 36) $fatSectors
    Set-ImageUInt16 $image ($boot + 40) 0
    Set-ImageUInt16 $image ($boot + 42) 0
    Set-ImageUInt32 $image ($boot + 44) 2
    Set-ImageUInt16 $image ($boot + 48) 1
    Set-ImageUInt16 $image ($boot + 50) 6
    $image[$boot + 64] = 0x80
    $image[$boot + 66] = 0x29
    Set-ImageUInt32 $image ($boot + 67) 0x26071602
    Set-ImageText $image ($boot + 71) 11 'GXOS CONFIG '
    Set-ImageText $image ($boot + 82) 8 'FAT32   '
    $image[$boot + 510] = 0x55
    $image[$boot + 511] = 0xAA
    for ($index = 0; $index -lt $bytesPerSector; ++$index) {
        $image[(6 * $bytesPerSector) + $index] = $image[$index]
    }

    $freeClusters = $totalSectors - $reservedSectors - ($fatCount * $fatSectors) - 2
    $fsInfo = $bytesPerSector
    Set-ImageUInt32 $image $fsInfo 0x41615252
    Set-ImageUInt32 $image ($fsInfo + 484) 0x61417272
    Set-ImageUInt32 $image ($fsInfo + 488) $freeClusters
    Set-ImageUInt32 $image ($fsInfo + 492) 3
    Set-ImageUInt16 $image ($fsInfo + 510) 0xAA55
    for ($index = 0; $index -lt $bytesPerSector; ++$index) {
        $image[(7 * $bytesPerSector) + $index] = $image[$fsInfo + $index]
    }

    for ($fatIndex = 0; $fatIndex -lt $fatCount; ++$fatIndex) {
        $fatBase = ($reservedSectors + ($fatIndex * $fatSectors)) * $bytesPerSector
        Set-ImageUInt32 $image $fatBase 0x0FFFFFF8
        Set-ImageUInt32 $image ($fatBase + 4) 0x0FFFFFFF
        Set-ImageUInt32 $image ($fatBase + 8) 0x0FFFFFFF
    }

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($image, 0, $image.Length) }
    finally { $stream.Dispose() }
}

if ($FormatOnly) {
    if ([string]::IsNullOrWhiteSpace($ImagePath)) { throw '-ImagePath is required with -FormatOnly.' }
    New-Fat32Image -Path $ImagePath
    exit 0
}

function Get-SerialText {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    return (Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue)
}

function Read-QmpMessage {
    param([Parameter(Mandatory=$true)][System.IO.StreamReader]$Reader,[int]$TimeoutSeconds=10)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $line = $Reader.ReadLine()
            if ($null -eq $line) { Start-Sleep -Milliseconds 50; continue }
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try { return ($line | ConvertFrom-Json -ErrorAction Stop) } catch { continue }
        } catch { Start-Sleep -Milliseconds 50 }
    }
    throw 'Timed out waiting for QMP message.'
}

function New-QmpSession {
    param([Parameter(Mandatory=$true)][int]$Port)
    $client = [System.Net.Sockets.TcpClient]::new()
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        try { $client.Connect('127.0.0.1', $Port); break } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $client.Connected) { throw "QMP port $Port did not open." }
    $stream = $client.GetStream()
    $stream.ReadTimeout = 1000
    $stream.WriteTimeout = 1000
    $encoding = New-Object System.Text.UTF8Encoding $false
    $reader = [System.IO.StreamReader]::new($stream, $encoding, $false, 1024, $true)
    $writer = [System.IO.StreamWriter]::new($stream, $encoding, 1024, $true)
    $writer.NewLine = "`n"
    $writer.AutoFlush = $true
    $session = [pscustomobject]@{ Client=$client; Stream=$stream; Reader=$reader; Writer=$writer; Port=$Port }
    $greeting = Read-QmpMessage -Reader $reader
    if (-not ($greeting.PSObject.Properties.Name -contains 'QMP')) { throw 'QMP greeting missing.' }
    [void](Invoke-QmpCommand -Session $session -Execute 'qmp_capabilities')
    return $session
}

function Close-QmpSession {
    param([AllowNull()][object]$Session)
    if ($null -eq $Session) { return }
    foreach ($name in @('Writer','Reader','Stream','Client')) {
        try { if ($Session.$name) { $Session.$name.Dispose() } } catch {}
    }
}

function Invoke-QmpCommand {
    param([Parameter(Mandatory=$true)][object]$Session,[Parameter(Mandatory=$true)][string]$Execute,[hashtable]$Arguments=$null)
    $payload = [ordered]@{ execute=$Execute }
    if ($Arguments) { $payload.arguments = $Arguments }
    $Session.Writer.WriteLine(($payload | ConvertTo-Json -Compress -Depth 8))
    while ($true) {
        $message = Read-QmpMessage -Reader $Session.Reader
        if ($message.PSObject.Properties.Name -contains 'event') { continue }
        if ($message.PSObject.Properties.Name -contains 'return') { return $message.return }
        if ($message.PSObject.Properties.Name -contains 'error') { throw "QMP '$Execute' failed: $($message.error.desc)" }
    }
}

function Wait-ForText {
    param([Parameter(Mandatory=$true)][System.Diagnostics.Process]$Launcher,[Parameter(Mandatory=$true)][string]$SerialPath,[Parameter(Mandatory=$true)][string]$Pattern)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $text = Get-SerialText -Path $SerialPath
        if ($text -match $Pattern) { return $true }
        if ($Launcher.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Get-NewQemuPid {
    param([int[]]$Before)
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        $current = @(Get-Process -Name 'qemu-system-x86_64' -ErrorAction SilentlyContinue)
        $candidate = $current | Where-Object { $Before -notcontains $_.Id } | Select-Object -First 1
        if ($candidate) { return $candidate.Id }
        Start-Sleep -Milliseconds 100
    }
    throw 'QEMU process did not appear.'
}

function Capture-Heads {
    param([Parameter(Mandatory=$true)][object]$Session,[Parameter(Mandatory=$true)][string]$Destination)
    foreach ($head in @(0,1)) {
        $path = Join-Path $Destination ("head{0}.png" -f $head)
        [void](Invoke-QmpCommand -Session $Session -Execute 'screendump' -Arguments @{ filename=$path; device='gpu0'; head=$head; format='png' })
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline -and (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -le 0)) {
            Start-Sleep -Milliseconds 100
        }
        if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -le 0) { throw "head $head capture missing: $path" }
    }
}

function Stop-Launch {
    param([Parameter(Mandatory=$true)][System.Diagnostics.Process]$Launcher,[AllowNull()][object]$Session,[int]$QemuPid=0)
    if ($Session) {
        try { [void](Invoke-QmpCommand -Session $Session -Execute 'quit') } catch {}
        Close-QmpSession -Session $Session
    }
    if ($QemuPid -gt 0) {
        $qemu = Get-Process -Id $QemuPid -ErrorAction SilentlyContinue
        if ($qemu) {
            [void]$qemu.WaitForExit(10000)
            if (-not $qemu.HasExited) {
                try { Stop-Process -Id $QemuPid -Force -ErrorAction SilentlyContinue } catch {}
            }
        }
    }
    if (-not $Launcher.HasExited) {
        [void]$Launcher.WaitForExit(10000)
    }
    if (-not $Launcher.HasExited) {
        try { & taskkill.exe /T /F /PID $Launcher.Id | Out-Null } catch { Stop-Process -Id $Launcher.Id -Force -ErrorAction SilentlyContinue }
    }
}

function Start-Launch {
    param([Parameter(Mandatory=$true)][string]$LaunchName,[Parameter(Mandatory=$true)][string]$SerialPath,[Parameter(Mandatory=$true)][string]$CapturePath,[Parameter(Mandatory=$true)][string]$ConfigDirectory)
    $stdout = Join-Path $RunRoot ("{0}.launcher.stdout.log" -f $LaunchName)
    $stderr = Join-Path $RunRoot ("{0}.launcher.stderr.log" -f $LaunchName)
    $port = Get-FreeTcpPort
    $old = @{}
    foreach ($name in @('GXOS_QEMU_DISPLAY_PROBE_HEADLESS','GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE','GXOS_QEMU_DISPLAY_PROBE_CAPTURE','GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG','GXOS_QEMU_DISPLAY_PROBE_QMP_PORT','GXOS_QEMU_DISPLAY_PROBE_ESP_DIR','GXOS_QEMU_DISPLAY_PROBE_CONFIG_DIR')) {
        $old[$name] = (Get-Item -Path "Env:$name" -ErrorAction SilentlyContinue).Value
    }
    $env:GXOS_QEMU_DISPLAY_PROBE_HEADLESS = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_CAPTURE = '0'
    $env:GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG = $SerialPath
    $env:GXOS_QEMU_DISPLAY_PROBE_QMP_PORT = [string]$port
    $env:GXOS_QEMU_DISPLAY_PROBE_ESP_DIR = $BootArtifact
    if ([string]::IsNullOrWhiteSpace($ConfigDirectory) -or -not (Test-Path -LiteralPath $ConfigDirectory -PathType Leaf)) {
        throw "persistent config directory is unavailable: $ConfigDirectory"
    }
    $env:GXOS_QEMU_DISPLAY_PROBE_CONFIG_DIR = $ConfigDirectory
    $before = @(Get-Process -Name 'qemu-system-x86_64' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    $launcher = Start-Process -FilePath 'cmd.exe' -ArgumentList @('/c', $Batch, 'virtio-gpu', $ConfigDirectory) -PassThru -WindowStyle Hidden -WorkingDirectory $Root -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $qemuPid = Get-NewQemuPid -Before $before
    foreach ($entry in $old.GetEnumerator()) {
        if ([string]::IsNullOrWhiteSpace($entry.Value)) { Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
        else { Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value }
    }
    return [pscustomobject]@{ Name=$LaunchName; Launcher=$launcher; QemuPid=$qemuPid; Port=$port; Serial=$SerialPath; Capture=$CapturePath; Session=$null }
}

$qemu = Find-Qemu
if (-not $qemu) { throw 'qemu-system-x86_64 not found.' }
if (-not (Test-Path -LiteralPath $Batch)) { throw "QEMU launcher missing: $Batch" }

$oldExtra = $env:EXTRA_CFLAGS
$launch1 = $null
$launch2 = $null
try {
    $env:EXTRA_CFLAGS = '-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE_BOUNDED -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE'
    Write-Host '[display-persistence] building QEMU-only persistence proof kernel'
    $probeObjects = @(
        'mmio.o','mmio.d','virtio_gpu.o','virtio_gpu.d','main.o','main.d',
        'display_configuration_service.o','display_configuration_service.d',
        'qemu_display_configuration_persistence_proof.o','qemu_display_configuration_persistence_proof.d',
        'qemu_display_configuration_control_proof.o','qemu_display_configuration_control_proof.d',
        'qemu_display_resolution_persistence_proof.o','qemu_display_resolution_persistence_proof.d'
    ) | ForEach-Object { Join-Path $Root ("kernel\build\amd64\obj\core\" + $_) }
    Remove-Item -LiteralPath $probeObjects -ErrorAction SilentlyContinue
    & (Join-Path $Root 'build-kernel.bat')
    if ($LASTEXITCODE -ne 0) { throw "kernel build failed with exit code $LASTEXITCODE" }

    Copy-Item -LiteralPath (Join-Path $Root 'ESP') -Destination $BootArtifact -Recurse -Force
    New-Fat32Image -Path $Artifact
    $storageBeforeLaunch1 = Get-ArtifactFingerprint -Path $Artifact
    $storageIdentifier = "fat32-image:$Artifact"

    $launch1 = Start-Launch -LaunchName 'launch1' -SerialPath (Join-Path $RunRoot 'launch1.serial.log') -CapturePath $CaptureRoot1 -ConfigDirectory $Artifact
    $launch1Marker = Wait-ForText -Launcher $launch1.Launcher -SerialPath $launch1.Serial -Pattern 'Display persistence proof launch1: [^\r\n]*result=success'
    if (-not $launch1Marker) { throw "launch 1 proof did not complete: $($launch1.Serial)" }
    $launch1ResolutionMarker = Wait-ForText -Launcher $launch1.Launcher -SerialPath $launch1.Serial -Pattern 'VirtioGPU resolution persistence launch1: mixedExtend=ok[^\r\n]*result=success'
    if (-not $launch1ResolutionMarker) { throw "launch 1 per-output resolution persistence proof did not complete: $($launch1.Serial)" }
    $launch1.Session = New-QmpSession -Port $launch1.Port
    Capture-Heads -Session $launch1.Session -Destination $launch1.Capture
    Stop-Launch -Launcher $launch1.Launcher -Session $launch1.Session -QemuPid $launch1.QemuPid
    $launch1.Session = $null
    $storageAfterLaunch1 = Get-ArtifactFingerprint -Path $Artifact
    $launch1Text = Get-SerialText -Path $launch1.Serial
    if ($launch1Text -notmatch 'Display persistence commit: request=\d+ mode=Extend primary=display-2 outputs=2 result=success bytes=\d+ version=2') {
        throw 'launch 1 did not commit the versioned Display 2 configuration through the guest persistence service.'
    }
    if ($storageAfterLaunch1 -eq $storageBeforeLaunch1) { throw 'launch 1 did not change the shared persistent storage artifact.' }

    $storageBeforeLaunch2 = Get-ArtifactFingerprint -Path $Artifact
    $launch2 = Start-Launch -LaunchName 'launch2' -SerialPath (Join-Path $RunRoot 'launch2.serial.log') -CapturePath $CaptureRoot2 -ConfigDirectory $Artifact
    $launch2Marker = Wait-ForText -Launcher $launch2.Launcher -SerialPath $launch2.Serial -Pattern 'Display configuration persistence proof: [^\r\n]*result=success'
    if (-not $launch2Marker) { throw "launch 2 automatic restore proof did not complete: $($launch2.Serial)" }
    $launch2ResolutionMarker = Wait-ForText -Launcher $launch2.Launcher -SerialPath $launch2.Serial -Pattern 'VirtioGPU resolution persistence launch2: [^\r\n]*restored=yes[^\r\n]*result=success'
    if (-not $launch2ResolutionMarker) { throw "launch 2 per-output resolution restore proof did not complete: $($launch2.Serial)" }
    $launch2StartupRestoreMarker = Wait-ForText -Launcher $launch2.Launcher -SerialPath $launch2.Serial -Pattern 'Display configuration startup restore: source=persisted-store injectedByHost=no loaded=yes reconciled=yes applied=yes'
    if (-not $launch2StartupRestoreMarker) { throw "launch 2 startup restore evidence did not flush: $($launch2.Serial)" }
    $launch2.Session = New-QmpSession -Port $launch2.Port
    Capture-Heads -Session $launch2.Session -Destination $launch2.Capture
    $launch2Text = Get-SerialText -Path $launch2.Serial
    if ($launch2Text -match 'Display config command: request=\d+ origin=TestCoordinator type=4') { throw 'launch 2 used a test-coordinator ApplyConfiguration injection.' }
    if ($launch2Text -match 'Display persistence commit:') { throw 'launch 2 rewrote persisted configuration during startup restore.' }
    if ($launch2Text -notmatch 'Display configuration startup restore: source=persisted-store injectedByHost=no loaded=yes reconciled=yes applied=yes') { throw 'launch 2 startup restore evidence is missing.' }
    Stop-Launch -Launcher $launch2.Launcher -Session $launch2.Session -QemuPid $launch2.QemuPid
    $launch2.Session = $null
    $storageAfterLaunch2 = Get-ArtifactFingerprint -Path $Artifact

    $launch1Text = Get-SerialText -Path $launch1.Serial
    if ($storageBeforeLaunch2 -ne $storageAfterLaunch1) { throw 'launch 2 did not start from the exact storage state produced by launch 1.' }
    $launch1Layout = [regex]::Match($launch1Text, 'Display persistence proof launch1: [^\r\n]*mode=Extend primary=2 origins=([^ ]+)')
    $launch2Layout = [regex]::Match($launch2Text, 'Display persistence proof launch2: [^\r\n]*mode=Extend primary=2 taskbarMonitor=2 origins=([^ ]+)')
    if (-not $launch1Layout.Success -or -not $launch2Layout.Success -or $launch1Layout.Groups[1].Value -ne $launch2Layout.Groups[1].Value) { throw 'launch 1 and launch 2 arrangements did not match.' }
    foreach ($capture in @(Get-ChildItem -LiteralPath $CaptureRoot1,$CaptureRoot2 -Filter '*.png' -File)) {
        if ($capture.Length -le 0) { throw "empty display capture: $($capture.FullName)" }
    }
    $launch1Primary = $launch1Text -match 'Display persistence proof launch1: [^\r\n]*active=ok[^\r\n]*taskbarMonitor=2'
    $launch2Primary = $launch2Text -match 'Display persistence proof launch2: source=persisted-store injectedByHost=no loaded=yes reconciled=yes applied=yes mode=Extend primary=2 taskbarMonitor=2'
    $resolutionLaunch1 = $launch1Text -match 'VirtioGPU resolution persistence launch1: mixedExtend=ok primary=display-2 virtualDesktop=2304x800 persisted=yes result=success'
    $resolutionLaunch2 = $launch2Text -match 'VirtioGPU resolution proof: persistenceLaunch2=ok restoredModes=ok virtualDesktop=2304x800 primary=Display 2 taskbar=Display 2 gpuFailures=0 fallback=no result=success'
    $gpuHealthy = ($launch1Text -notmatch 'targetFailures=[1-9]|gpuFailures=[1-9]') -and ($launch2Text -notmatch 'targetFailures=[1-9]|gpuFailures=[1-9]')
    if (-not $launch1Primary -or -not $launch2Primary -or -not $resolutionLaunch1 -or -not $resolutionLaunch2 -or -not $gpuHealthy) { throw 'primary/taskbar/live GPU structural evidence failed.' }
    if ($launch1.QemuPid -eq $launch2.QemuPid) { throw 'launch 1 and launch 2 did not use distinct QEMU process IDs.' }

    $evidence = @(
        '[DisplayConfigurationPersistenceProof]'
        'result=success'
        "storageArtifact=$Artifact"
        "storageIdentifier=$storageIdentifier"
        "storageBeforeLaunch1=$storageBeforeLaunch1"
        "storageAfterLaunch1=$storageAfterLaunch1"
        "storageBeforeLaunch2=$storageBeforeLaunch2"
        "storageAfterLaunch2=$storageAfterLaunch2"
        'sameArtifactReused=yes'
        'guestWroteConfiguration=yes'
        'harnessModifiedBetweenLaunches=no'
        "launch1QemuPid=$($launch1.QemuPid)"
        "launch2QemuPid=$($launch2.QemuPid)"
        "launch1Serial=$($launch1.Serial)"
        "launch2Serial=$($launch2.Serial)"
        "launch1Head0=$($CaptureRoot1)\head0.png"
        "launch1Head1=$($CaptureRoot1)\head1.png"
        "launch2Head0=$($CaptureRoot2)\head0.png"
        "launch2Head1=$($CaptureRoot2)\head1.png"
        "arrangementLaunch1=$($launch1Layout.Groups[1].Value)"
        "arrangementLaunch2=$($launch2Layout.Groups[1].Value)"
        'launch2HostApplyConfigurationInjection=no'
        'mode=Extend'
        'primary=Display 2'
        'taskbar=Display 2'
        'virtualDesktop=2560x800-or-runtime-equivalent'
        'gpuFailures=0'
        'fallback=no'
        'resolutionLaunch1=mixedExtend-ok'
        'resolutionLaunch2=restoredModes-ok'
        'resolutionVirtualDesktop=2304x800'
    )
    Set-Content -LiteralPath (Join-Path $RunRoot 'summary.txt') -Value $evidence -Encoding UTF8
    $evidence | ForEach-Object { Write-Output $_ }
    Write-Output "Display configuration persistence smoke PASS. Evidence: $(Join-Path $RunRoot 'summary.txt')"
} finally {
    if ($launch1) { Stop-Launch -Launcher $launch1.Launcher -Session $launch1.Session -QemuPid $launch1.QemuPid }
    if ($launch2) { Stop-Launch -Launcher $launch2.Launcher -Session $launch2.Session -QemuPid $launch2.QemuPid }
    if ($null -eq $oldExtra -or $oldExtra -eq '') { Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue }
    else { $env:EXTRA_CFLAGS = $oldExtra }
}
