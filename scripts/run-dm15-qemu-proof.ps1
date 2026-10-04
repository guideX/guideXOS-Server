<#
.SYNOPSIS
    Runs DM15/DM22 AHCI proofs or the DM24 4Kn FAT32 storage proof.

.DESCRIPTION
    Creates an isolated ESP copy and a fresh disposable raw secondary image.
    DM15/DM22 use Q35 ICH9 AHCI; DM24 uses PIIX3 UHCI USB mass storage with
    4096-byte blocks because the QEMU ide-hd device class used by the AHCI
    runner rejects 4Kn logical blocks. PrivateWrite validates and restores a zero-filled sector while
    shared writes are disabled. Lifecycle runs GPT/FAT32/VFS, restarts the
    same image, and independently verifies it.
#>
[CmdletBinding()]
param(
    [ValidateSet("PrivateWrite", "Lifecycle")]
    [string]$Stage = "PrivateWrite",
    [string]$EspSource = "ESP",
    [string]$WorkDir = "",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
    [string]$EspCacheDirectory = "",
    [string]$KernelImage = "",
    [string]$PreparedImagePath = "",
    [int]$AttemptNumber = 1,
    [UInt64]$DiskSizeBytes = 629145600,
    [ValidateRange(0, 86400)]
    [int]$FirstBootTimeoutSeconds = 0,
    [ValidateRange(0, 86400)]
    [int]$RediscoveryTimeoutSeconds = 0,
    [switch]$Dm22LargeProof,
    [switch]$Dm24FourKnProof,
    [switch]$Dm25PartitionDeleteProof,
    [switch]$Dm27QuickReformatProof,
    [switch]$Dm28InterruptProof,
    [switch]$QemuDebug,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

function Find-MSBuild {
    $onPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installPath) {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    throw "MSBuild was not found."
}

function Get-FreeLoopbackPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $listener.Stop()
    return $port
}

function Get-EspTreeHash([string]$Source) {
    $excluded = @("EFI\BOOT\BOOTX64.EFI", "kernel.elf", "build-identity.txt")
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($item in Get-ChildItem -LiteralPath $Source -File -Force -Recurse |
            Sort-Object FullName) {
        $relative = $item.FullName.Substring($Source.Length).TrimStart([char]'\')
        if ($excluded -contains $relative) { continue }
        $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        $lines.Add("$relative=$hash")
    }
    $payload = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash($payload) } finally { $sha.Dispose() }
    return [BitConverter]::ToString($digest).Replace("-", "")
}

function Save-Dm22ImageAllocation([string]$DiskPath, [string]$OutputPath,
                                  [string]$Phase) {
    $qemuImg = Join-Path (Split-Path -Parent $QemuFull) "qemu-img.exe"
    $fsutil = Join-Path $env:SystemRoot "System32\fsutil.exe"
    if (-not (Test-Path -LiteralPath $qemuImg) -or
        -not (Test-Path -LiteralPath $fsutil)) {
        throw "DM22 allocation evidence requires qemu-img.exe and fsutil.exe."
    }
    $infoLines = & $qemuImg info --output=json -f raw $DiskPath 2>&1
    if ($LASTEXITCODE -ne 0) { throw "qemu-img could not inspect the DM22 raw image." }
    $info = ($infoLines -join [Environment]::NewLine) | ConvertFrom-Json
    $actualBytes = [UInt64]$info.'actual-size'
    $queryFlag = & $fsutil sparse queryflag $DiskPath 2>&1
    if ($LASTEXITCODE -ne 0) { throw "fsutil could not inspect the DM22 sparse-image flag." }
    $ranges = & $fsutil sparse queryrange $DiskPath 2>&1
    if ($LASTEXITCODE -ne 0) { throw "fsutil could not inspect the DM22 allocated ranges." }
    $item = Get-Item -LiteralPath $DiskPath
    @(
        "phase=$Phase",
        "path=$DiskPath",
        "logicalBytes=$($item.Length)",
        "qemuImgActualBytes=$actualBytes",
        "sparseAttributes=$($item.Attributes)",
        "sparseFlag=$($queryFlag -join ' ')",
        $ranges
    ) | Set-Content -LiteralPath (Join-Path $OutputPath "image-allocation-$Phase.txt") -Encoding ascii
    return $actualBytes
}

function Copy-RawImageSparse([string]$SourcePath, [string]$DestinationPath) {
    if (Test-Path -LiteralPath $DestinationPath) {
        throw "Refusing to overwrite image checkpoint $DestinationPath"
    }
    $qemuImg = Join-Path (Split-Path -Parent $QemuFull) "qemu-img.exe"
    if (-not (Test-Path -LiteralPath $qemuImg)) {
        throw "qemu-img.exe is required to preserve sparse proof checkpoints."
    }
    & $qemuImg convert -f raw -O raw -S 4096 $SourcePath $DestinationPath 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "qemu-img could not create sparse checkpoint $DestinationPath."
    }
    if ((Get-Item -LiteralPath $SourcePath).Length -ne
        (Get-Item -LiteralPath $DestinationPath).Length) {
        throw "Sparse proof checkpoint length differs from its source."
    }
}

function Ensure-EspCache([string]$Source, [string]$Cache,
                         [string]$Bootloader, [string]$Kernel) {
    $sourceHash = Get-EspTreeHash $Source
    $bootHash = (Get-FileHash -LiteralPath $Bootloader -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath $Kernel -Algorithm SHA256).Hash
    $cacheManifest = Join-Path $Cache "dm15-esp-cache.txt"
    if (Test-Path -LiteralPath $Cache) {
        if (-not (Test-Path -LiteralPath $cacheManifest)) {
            throw "Refusing to reuse ESP cache without its manifest: $Cache"
        }
        $cacheLines = Get-Content -LiteralPath $cacheManifest
        foreach ($expected in @("sourceEspSha256=$sourceHash", "bootloaderSha256=$bootHash", "kernelSha256=$kernelHash")) {
            if ($cacheLines -notcontains $expected) {
                throw "Existing ESP cache does not match current inputs: $Cache"
            }
        }
        return
    }
    New-Item -ItemType Directory -Path $Cache -Force | Out-Null
    $excluded = @("EFI\BOOT\BOOTX64.EFI", "kernel.elf", "build-identity.txt")
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force -Recurse) {
        $relative = $item.FullName.Substring($Source.Length).TrimStart([char]'\')
        if ($excluded -contains $relative) { continue }
        $target = Join-Path $Cache $relative
        if ($item.PSIsContainer) {
            New-Item -ItemType Directory -Path $target -Force | Out-Null
            continue
        }
        $parent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        Copy-Item -LiteralPath $item.FullName -Destination $target
    }
    $bootPath = Join-Path $Cache "EFI\BOOT"
    New-Item -ItemType Directory -Path $bootPath -Force | Out-Null
    Copy-Item -LiteralPath $Bootloader -Destination (Join-Path $bootPath "BOOTX64.EFI")
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Cache "kernel.elf")
    @("sourceEspSha256=$sourceHash", "bootloaderSha256=$bootHash", "kernelSha256=$kernelHash") |
        Set-Content -LiteralPath $cacheManifest -Encoding ascii
}

function New-EspCopy([string]$Cache, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Cache -File -Force -Recurse) {
        $relative = $item.FullName.Substring($Cache.Length).TrimStart([char]'\')
        if ($relative -ieq "dm15-esp-cache.txt") { continue }
        $target = Join-Path $Destination $relative
        $parent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        Copy-Item -LiteralPath $item.FullName -Destination $target
    }
}

function Stop-ProofQemu([int]$ProcessId, [int]$Port,
                        [string]$ExpectedSerialPath) {
    if ($ProcessId -le 0) { throw "QEMU process ID is missing during proof cleanup." }
    $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId"
    if (-not $owned) { return }
    if ($owned.Name -ne "qemu-system-x86_64.exe" -or
        $owned.CommandLine -notlike "*$ExpectedSerialPath*") {
        throw "Refusing to stop PID $ProcessId because its QEMU command line does not match this proof."
    }
    Write-Host "DM15 cleanup: stopping verified QEMU PID=$ProcessId"
    $ownedProcess = [System.Diagnostics.Process]::GetProcessById($ProcessId)
    $monitor = [System.Net.Sockets.TcpClient]::new()
    try {
        $monitor.Connect("127.0.0.1", $Port)
        $stream = $monitor.GetStream()
        $quit = [Text.Encoding]::ASCII.GetBytes("quit`n")
        $stream.Write($quit, 0, $quit.Length)
        $stream.Flush()
    } catch {
        # The owned process may have stopped between the process check and monitor connect.
    } finally {
        if ($monitor) { $monitor.Dispose() }
    }
    if (-not $ownedProcess.WaitForExit(10000)) {
        $ownedProcess.Refresh()
        if (-not $ownedProcess.HasExited) { $ownedProcess.Kill() }
    }
    if (-not $ownedProcess.WaitForExit(10000)) {
        $ownedProcess.Dispose()
        throw "The QEMU process for this proof did not stop."
    }
    $ownedProcess.Dispose()
}

function Start-ProofBoot([string]$RunName, [string]$SuccessMarker,
                         [string]$EspPath, [string]$DiskPath,
                         [string]$OutputPath, [int]$TimeoutSeconds,
                         [string]$ManifestPath,
                         [string]$WaitForStageMarker = "") {
    $serialPath = Join-Path $OutputPath "$RunName.serial.log"
    $stderrPath = Join-Path $OutputPath "$RunName.stderr.log"
    $stdoutPath = Join-Path $OutputPath "$RunName.stdout.log"
    $debugPath = Join-Path $OutputPath "$RunName.qemu-debug.log"
    Remove-Item -LiteralPath $serialPath,$stderrPath,$stdoutPath -Force -ErrorAction SilentlyContinue
    $port = Get-FreeLoopbackPort
    $qmpPort = 0
    if ($Dm28InterruptProof) {
        do { $qmpPort = Get-FreeLoopbackPort } while ($qmpPort -eq $port)
    }
    $arguments = @(
        "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfFull",
        "-machine", $(if ($Dm24FourKnProof) { "pc,usb=off" } else { "q35,usb=off" }),
        "-drive", "if=none,id=dm15boot,format=raw,file=fat:rw:$EspPath",
        "-device", "ide-hd,drive=dm15boot,bus=ide.0",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-object", "rng-builtin,id=rng0",
        "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", "file:$serialPath",
        "-monitor", "tcp:127.0.0.1:$port,server,nowait",
        "-rtc", "base=utc,clock=host", "-no-reboot"
    )
    if ($Dm28InterruptProof) {
        $arguments += @("-qmp", "tcp:127.0.0.1:$qmpPort,server,nowait")
    }
    if ($Dm24FourKnProof) {
        $arguments += @(
            "-device", "piix3-usb-uhci,id=uhci",
            "-drive", "if=none,id=dm24secondary,format=raw,file=$DiskPath",
            "-device", "usb-storage,id=dm24disk,bus=uhci.0,drive=dm24secondary,removable=on,serial=DM24USB01,logical_block_size=4096,physical_block_size=4096,discard_granularity=4096"
        )
    } else {
        $arguments += @(
            "-drive", "if=none,id=dm15secondary,format=raw,file=$DiskPath",
            "-device", "ide-hd,drive=dm15secondary,bus=ide.1"
        )
    }
    if ($QemuDebug) { $arguments += @("-d", "guest_errors,int,cpu_reset", "-D", $debugPath) }
    $process = Start-Process -FilePath $QemuFull -ArgumentList $arguments `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    $otherQemu = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" |
        Where-Object { $_.ProcessId -ne $process.Id })
    if ($ManifestPath -and (Test-Path -LiteralPath $ManifestPath)) {
        Add-Content -LiteralPath $ManifestPath -Encoding ascii -Value @(
            "qemu.$RunName.pid=$($process.Id)",
            "qemu.$RunName.commandLine=$($processInfo.CommandLine)",
            "qemu.$RunName.serial=$serialPath",
            "qemu.$RunName.monitorPort=$port",
            "qemu.$RunName.qmpPort=$(if ($qmpPort -gt 0) { $qmpPort } else { 'not-applicable' })",
            "qemu.$RunName.otherProcessesAtStart=$($otherQemu.Count)",
            "qemu.$RunName.otherPidsAtStart=$(($otherQemu | ForEach-Object { $_.ProcessId }) -join ',')"
        )
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serialPath) {
            $serial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
            if ($serial -match '\[KERNEL-FAULT\]') {
                Stop-ProofQemu $process.Id $port $serialPath
                throw "$RunName encountered a kernel fault; see $serialPath"
            }
            if ($serial -match '(?m)^\[(?:DM27-QEMU|DM25-QEMU|DM24-QEMU|DM22-QEMU|DM15-QEMU|DM9-QEMU)\] (?:private-proof=FAIL|lifecycle=FAIL|quick-reformat=FAIL|delete-ready=FAIL|delete=FAIL|proof=BLOCKED|reboot-rediscovery=FAIL|initialize=FAIL|create-partition=FAIL|format-fat32=FAIL|large-volume=FAIL|allocation-hint=FAIL|geometry=FAIL)' -or
                $serial -match '(?m)^\[DM28-QRF\] (?:cold-restart|retry-after-cold-restart|interruption)=FAIL') {
                $failureLine = $Matches[0]
                Stop-ProofQemu $process.Id $port $serialPath
                throw "$RunName reported '$failureLine'; see $serialPath"
            }
            if ($WaitForStageMarker -and $serial -and
                $serial.Contains($WaitForStageMarker)) {
                return [pscustomobject]@{ ProcessId=$process.Id; Port=$port;
                    QmpPort=$qmpPort; SerialPath=$serialPath }
            }
            if ($serial -and $serial.Contains($SuccessMarker) -and
                $serial.Contains("[KERNEL] Entering main loop")) {
                return [pscustomobject]@{ ProcessId=$process.Id; Port=$port;
                    QmpPort=$qmpPort; SerialPath=$serialPath }
            }
        }
        if ($process.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
    if (-not $process.HasExited) { Stop-ProofQemu $process.Id $port $serialPath }
    throw "$RunName timed out or exited before '$SuccessMarker'. $stderrPath $serialPath"
}

$script:ProofQmp = $null
$script:ProofQmpId = 0
function Read-ProofQmpMessage($Run, [int]$TimeoutMilliseconds = 1000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $Run.Reader.ReadLine()
            if ($null -ne $line) {
                Add-Content -LiteralPath $Run.LogPath -Encoding utf8 -Value $line
                return $line
            }
            throw "QMP socket closed."
        } catch [IO.IOException] {
            if ($Run.Process.HasExited) { throw "QEMU exited while reading QMP." }
        }
    }
    return $null
}

function Send-ProofQmp([string]$Execute, [hashtable]$Arguments = @{}) {
    $script:ProofQmpId++
    $id = "dm28-$($script:ProofQmpId)"
    $command = [ordered]@{ execute=$Execute; id=$id }
    if ($Arguments.Count) { $command.arguments=$Arguments }
    $json = $command | ConvertTo-Json -Compress -Depth 12
    Add-Content -LiteralPath $script:ProofQmp.LogPath -Encoding utf8 -Value $json
    $script:ProofQmp.Writer.WriteLine($json)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        $line = Read-ProofQmpMessage $script:ProofQmp 1000
        if ($null -eq $line) { continue }
        $message = $line | ConvertFrom-Json
        if ($message.event) { $script:ProofQmp.Events.Enqueue($message); continue }
        if ($message.id -eq $id) {
            if ($message.error) {
                throw "QMP $Execute failed: $($message.error | ConvertTo-Json -Compress)"
            }
            return $message.return
        }
    }
    throw "Timed out waiting for QMP command $Execute."
}

function Connect-ProofQmp($Boot, [string]$LogPath) {
    $client = [Net.Sockets.TcpClient]::new()
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $client.Connect("127.0.0.1", $Boot.QmpPort); break }
        catch {
            if (-not (Get-Process -Id $Boot.ProcessId -ErrorAction SilentlyContinue)) {
                throw "QEMU exited before opening its QMP endpoint."
            }
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $client.Connected) { throw "QMP did not listen before the bounded deadline." }
    $stream = $client.GetStream()
    $stream.ReadTimeout = 1000
    $reader = [IO.StreamReader]::new($stream,[Text.Encoding]::ASCII,$false,4096,$true)
    $writer = [IO.StreamWriter]::new($stream,[Text.Encoding]::ASCII,4096,$true)
    $writer.AutoFlush = $true
    $script:ProofQmp = [pscustomobject]@{
        Process=[System.Diagnostics.Process]::GetProcessById($Boot.ProcessId)
        Client=$client; Stream=$stream; Reader=$reader; Writer=$writer
        Events=[Collections.Generic.Queue[object]]::new(); LogPath=$LogPath
    }
    $greeting = Read-ProofQmpMessage $script:ProofQmp 5000
    if (-not $greeting -or -not ($greeting | ConvertFrom-Json).QMP) {
        throw "Invalid QMP greeting."
    }
    [void](Send-ProofQmp "qmp_capabilities")
}

function Wait-ProofDeviceDeleted([string]$DeviceId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        while ($script:ProofQmp.Events.Count -gt 0) {
            $event = $script:ProofQmp.Events.Dequeue()
            if ($event.event -eq "DEVICE_DELETED" -and
                $event.data.device -eq $DeviceId) { return $event }
        }
        $line = Read-ProofQmpMessage $script:ProofQmp 1000
        if ($null -eq $line) { continue }
        $message = $line | ConvertFrom-Json
        if ($message.event) { $script:ProofQmp.Events.Enqueue($message) }
        elseif ($message.error) {
            throw "QMP event read failed: $($message.error | ConvertTo-Json -Compress)"
        }
    }
    throw "Timed out waiting for DEVICE_DELETED for $DeviceId."
}

function Close-ProofQmp {
    if ($script:ProofQmp) {
        $script:ProofQmp.Client.Dispose()
        $script:ProofQmp = $null
    }
}

function Wait-ProofSerialMarker($Boot, [string]$Marker,
                                [int]$TimeoutSeconds = 30) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $serial = Get-Content -LiteralPath $Boot.SerialPath -Raw `
            -ErrorAction SilentlyContinue
        if ($serial -and $serial.Contains($Marker)) { return $serial }
        if (-not (Get-Process -Id $Boot.ProcessId -ErrorAction SilentlyContinue)) {
            throw "QEMU exited before serial marker '$Marker'."
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for serial marker '$Marker'."
}

if (($Dm22LargeProof -or $Dm24FourKnProof) -and $Stage -ne "Lifecycle") {
    throw "DM22 large FAT32 proof requires -Stage Lifecycle."
}
if ($Dm22LargeProof -and $Dm24FourKnProof) {
    throw "Select only one of -Dm22LargeProof or -Dm24FourKnProof."
}
if ($Dm25PartitionDeleteProof -and ($Stage -ne "Lifecycle" -or
        $Dm22LargeProof -or $Dm24FourKnProof -or $Dm27QuickReformatProof)) {
    throw "DM25 partition deletion uses its own AHCI Lifecycle proof mode."
}
if ($Dm27QuickReformatProof -and $Stage -ne "Lifecycle") {
    throw "DM27 Quick Reformat requires the Lifecycle proof mode."
}
if ($Dm28InterruptProof -and ($Stage -ne "Lifecycle" -or
        -not $Dm27QuickReformatProof -or -not $Dm24FourKnProof -or
        $Dm22LargeProof)) {
    throw "DM28 interruption proof requires the 4Kn USB Quick Reformat lifecycle."
}
if ($PreparedImagePath -and -not $Dm27QuickReformatProof) {
    throw "PreparedImagePath requires a Quick Reformat proof."
}
if ($Dm22LargeProof -and $DiskSizeBytes -lt [UInt64]::Parse("9663676416")) {
    throw "DM22 image must be at least 9 GiB so the GPT partition can exceed 8 GiB."
}
$minimumFourKnBytes = [UInt64]::Parse("671088640")
if ($Dm27QuickReformatProof -and $Dm24FourKnProof) {
    # A 320 MiB 4Kn volume leaves more than 65,525 data clusters after GPT and FAT metadata.
    $minimumFourKnBytes = [UInt64]::Parse("335544320")
}
if ($Dm24FourKnProof -and ($DiskSizeBytes -lt $minimumFourKnBytes -or
        ($DiskSizeBytes % 4096) -ne 0)) {
    throw "The 4Kn image is below the proof profile minimum or is not aligned to 4096 bytes."
}
if (-not (Test-Path -LiteralPath $QemuExecutable)) { throw "QEMU was not found at $QemuExecutable" }
if ($AttemptNumber -lt 1) { throw "AttemptNumber must be positive." }
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
$EspFull = (Resolve-Path -LiteralPath $EspSource).Path
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $WorkDir) {
$workLabel = if ($Dm28InterruptProof) { "dm28-usb-quick-reformat-interruption" }
    elseif ($Dm27QuickReformatProof -and $Dm22LargeProof) { "dm28-10g-ahci-quick-reformat" }
        elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { "dm27-4kn-usb-quick-reformat" }
        elseif ($Dm27QuickReformatProof) { "dm27-ahci-quick-reformat" }
        elseif ($Dm25PartitionDeleteProof) { "dm25-ahci-partition-delete" }
        elseif ($Dm24FourKnProof) { "dm24-4kn-fat32" }
        elseif ($Dm22LargeProof) { "dm22-large-fat32" }
        else { "dm15-$($Stage.ToLowerInvariant())" }
    $WorkDir = "out\$workLabel-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below $repoOut" }
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files; select a fresh output directory."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }

$diskLabel = if ($Dm28InterruptProof) { "secondary-dm28-interrupt-4kn.raw" }
    elseif ($Dm27QuickReformatProof -and $Dm22LargeProof) { "secondary-quick-reformat-10g.raw" }
    elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { "secondary-quick-reformat-4kn-$([uint64]($DiskSizeBytes / 1048576))m.raw" }
    elseif ($Dm27QuickReformatProof) { "secondary-quick-reformat-600m.raw" }
    elseif ($Dm25PartitionDeleteProof) { "secondary-delete-600m.raw" }
    elseif ($Dm24FourKnProof) { "secondary-4kn-640m.raw" }
    elseif ($Dm22LargeProof) { "secondary-large.raw" }
    else { "secondary-600m.raw" }
$DiskPath = Join-Path $WorkFull $diskLabel
$EspPath = Join-Path $WorkFull "esp"
$manifestName = if ($Dm28InterruptProof) { "dm28-interruption-manifest.txt" }
    elseif ($Dm27QuickReformatProof -and $Dm22LargeProof) { "dm28-10g-manifest.txt" }
    elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { "dm27-4kn-manifest.txt" }
    elseif ($Dm27QuickReformatProof) { "dm27-manifest.txt" }
    elseif ($Dm25PartitionDeleteProof) { "dm25-manifest.txt" }
    elseif ($Dm24FourKnProof) { "dm24-manifest.txt" }
    elseif ($Dm22LargeProof) { "dm22-manifest.txt" }
    else { "dm15-manifest.txt" }
$manifestPath = Join-Path $WorkFull $manifestName
$activeBoot = $null

try {
    if (-not $SkipBuild) {
        $make = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue
        if (-not $make) { $make = Get-Command make.exe -ErrorAction SilentlyContinue }
        if (-not $make -and (Test-Path -LiteralPath "C:\mingw64\bin\mingw32-make.exe")) {
            $makePath = "C:\mingw64\bin\mingw32-make.exe"
        } elseif ($make) { $makePath = $make.Source } else { throw "MinGW make was not found." }
        foreach ($object in @("main.o", "qemu_dm9_storage_proof.o", "ahci.o")) {
            $objectPath = Join-Path $Root "kernel\build\amd64\obj\core\$object"
            if (Test-Path -LiteralPath $objectPath) { Remove-Item -LiteralPath $objectPath -Force }
        }
        $flags = if ($Dm28InterruptProof) {
            "-DGXOS_DM24_QEMU_FAT32_4KN_PROOF -DGXOS_DM27_QEMU_QUICK_REFORMAT_PROOF -DGXOS_DM28_QEMU_REFORMAT_INTERRUPT_PROOF"
        } elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) {
            "-DGXOS_DM24_QEMU_FAT32_4KN_PROOF -DGXOS_DM27_QEMU_QUICK_REFORMAT_PROOF"
        } elseif ($Dm27QuickReformatProof) {
            "-DGXOS_DM15_QEMU_AHCI_PROOF -DGXOS_DM27_QEMU_QUICK_REFORMAT_PROOF"
        } elseif ($Dm25PartitionDeleteProof) {
            "-DGXOS_DM15_QEMU_AHCI_PROOF -DGXOS_DM25_QEMU_PARTITION_DELETE_PROOF"
        } elseif ($Dm24FourKnProof) {
            "-DGXOS_DM24_QEMU_FAT32_4KN_PROOF"
        } elseif ($Stage -eq "PrivateWrite") {
            "-DGXOS_DM15_QEMU_AHCI_PROOF -DGXOS_DM15_AHCI_PRIVATE_PROOF"
        } else { "-DGXOS_DM15_QEMU_AHCI_PROOF" }
        if ($Dm22LargeProof) {
            $flags += " -DGXOS_DM22_QEMU_FAT32_PROOF"
        }
        $kernelBuildLog = Join-Path $WorkFull "kernel-build-$($Stage.ToLowerInvariant()).log"
        $buildErrorPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $makePath -C (Join-Path $Root "kernel") ARCH=amd64 "EXTRA_CFLAGS=$flags" -j4 2>&1 | Out-File -LiteralPath $kernelBuildLog -Encoding utf8
        } finally {
            $ErrorActionPreference = $buildErrorPreference
        }
        $kernelBuildExitCode = $LASTEXITCODE
        if ($kernelBuildExitCode -ne 0) {
            Get-Content -LiteralPath $kernelBuildLog -Tail 80
            throw "DM proof $Stage kernel build failed; see $kernelBuildLog."
        }
        $msbuild = Find-MSBuild
        & $msbuild (Join-Path $Root "guideXOSBootLoader\guideXOSBootLoader.vcxproj") `
            /t:Build /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal `
            2>&1 | Out-File -LiteralPath (Join-Path $WorkFull "uefi-release-build.log") -Encoding utf8
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath (Join-Path $WorkFull "uefi-release-build.log") -Tail 80
            throw "UEFI bootloader build failed."
        }
    }

    if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) {
        throw "EspSource must contain guideXOS runtime files, including ramdisk.img."
    }
    $bootloaderSource = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
    $kernelSource = if ($KernelImage) {
        if ([IO.Path]::IsPathRooted($KernelImage)) { $KernelImage }
        else { Join-Path $Root $KernelImage }
    } else { Join-Path $Root "kernel\build\amd64\bin\kernel.elf" }
    if (-not (Test-Path -LiteralPath $bootloaderSource) -or
        -not (Test-Path -LiteralPath $kernelSource)) { throw "Built UEFI bootloader or amd64 kernel is missing." }

    if (-not $EspCacheDirectory) { $EspCacheDirectory = Join-Path $WorkFull "esp-source-cache" }
    $EspCacheFull = if ([IO.Path]::IsPathRooted($EspCacheDirectory)) {
        [IO.Path]::GetFullPath($EspCacheDirectory)
    } else { [IO.Path]::GetFullPath((Join-Path $Root $EspCacheDirectory)) }
    Ensure-EspCache $EspFull $EspCacheFull $bootloaderSource $kernelSource
    New-EspCopy $EspCacheFull $EspPath
    $bootPath = Join-Path $EspPath "EFI\BOOT"
    $kernelEvidencePath = Join-Path $WorkFull "kernel-$($Stage.ToLowerInvariant()).elf"
    if (-not (Test-Path -LiteralPath $kernelEvidencePath)) {
        New-Item -ItemType HardLink -Path $kernelEvidencePath `
            -Target (Join-Path $EspCacheFull "kernel.elf") | Out-Null
    }

    if (Test-Path -LiteralPath $DiskPath) { throw "Refusing to overwrite proof image $DiskPath" }
    $preparedImageFull = $null
    if ($PreparedImagePath) {
        $preparedImageFull = if ([IO.Path]::IsPathRooted($PreparedImagePath)) {
            [IO.Path]::GetFullPath($PreparedImagePath)
        } else {
            [IO.Path]::GetFullPath((Join-Path $Root $PreparedImagePath))
        }
        if (-not (Test-Path -LiteralPath $preparedImageFull -PathType Leaf)) {
            throw "PreparedImagePath does not name an existing raw image."
        }
        if ((Get-Item -LiteralPath $preparedImageFull).Length -ne [int64]$DiskSizeBytes) {
            throw "Prepared image length must match DiskSizeBytes exactly."
        }
        Copy-RawImageSparse $preparedImageFull $DiskPath
    } else {
        $sparseTool = Join-Path $env:SystemRoot "System32\fsutil.exe"
        if (-not (Test-Path -LiteralPath $sparseTool)) { throw "fsutil.exe is required to create a sparse raw proof image." }
        $diskStream = [IO.File]::Open($DiskPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
        $diskStream.Dispose()
        & $sparseTool sparse setflag $DiskPath 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to mark the new raw proof image sparse (fsutil exit $LASTEXITCODE)." }
        $diskStream = [IO.File]::Open($DiskPath, [IO.FileMode]::Open,
            [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
        $diskStream.SetLength([int64]$DiskSizeBytes)
        $diskStream.Dispose()
    }
    $bootHash = (Get-FileHash -LiteralPath (Join-Path $bootPath "BOOTX64.EFI") -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath (Join-Path $EspPath "kernel.elf") -Algorithm SHA256).Hash
    $kernelBytes = (Get-Item -LiteralPath (Join-Path $EspPath "kernel.elf")).Length
    $initialHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
    $initialActualBytes = if ($Dm22LargeProof) {
        Save-Dm22ImageAllocation $DiskPath $WorkFull "initial"
    } else { 0 }
    $qemuVersion = (& $QemuFull --version | Select-Object -First 1)
    $qemuHash = (Get-FileHash -LiteralPath $QemuFull -Algorithm SHA256).Hash
    $ovmfHash = (Get-FileHash -LiteralPath $OvmfFull -Algorithm SHA256).Hash
    $qemuAtStart = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'")
    $proofName = if ($Dm28InterruptProof) { "DM28-4KN-USB-QUICK-REFORMAT-INTERRUPTION-RETRY" }
        elseif ($Dm27QuickReformatProof -and $Dm22LargeProof) { "DM28-10G-AHCI-QUICK-REFORMAT" }
        elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { "DM27-4KN-USB-QUICK-REFORMAT" }
        elseif ($Dm27QuickReformatProof) { "DM27-AHCI-QUICK-REFORMAT" }
        elseif ($Dm25PartitionDeleteProof) { "DM25-AHCI-PARTITION-DELETE" }
        elseif ($Dm24FourKnProof) { "DM24-4KN-FAT32-USB" }
        elseif ($Dm22LargeProof) { "DM22-LARGE-FAT32-AHCI" }
        else { "DM15-AHCI-$Stage" }
    $manifestSchema = if ($Dm28InterruptProof) { "DM28-4KN-USB-INTERRUPTION-RETRY-1" }
        elseif ($Dm27QuickReformatProof -and $Dm22LargeProof) { "DM28-10G-AHCI-QUICK-REFORMAT-1" }
        elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { "DM27-4KN-USB-QUICK-REFORMAT-1" }
        elseif ($Dm27QuickReformatProof) { "DM27-AHCI-QUICK-REFORMAT-1" }
        elseif ($Dm25PartitionDeleteProof) { "DM25-AHCI-PARTITION-DELETE-1" }
        elseif ($Dm24FourKnProof) { "DM24-4KN-FAT32-1" }
        elseif ($Dm22LargeProof) { "DM22-LARGE-FAT32-1" }
        else { "DM19-TRANSPORT-1" }
    $proofIdentity = if ($Dm28InterruptProof) { "GUIDEXOS-DM28-QEMU-4Kn-USB-QuickReformat-InterruptionRetry" }
        elseif ($Dm27QuickReformatProof -and $Dm22LargeProof) { "GUIDEXOS-DM28-QEMU-10GiB-AHCI-QuickReformat" }
        elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { "GUIDEXOS-DM27-QEMU-4Kn-USB-QuickReformat" }
        elseif ($Dm27QuickReformatProof) { "GUIDEXOS-DM27-QEMU-AHCI-QuickReformat" }
        elseif ($Dm25PartitionDeleteProof) { "GUIDEXOS-DM25-QEMU-AHCI-PartitionDelete" }
        elseif ($Dm24FourKnProof) { "GUIDEXOS-DM24-QEMU-4KnFAT32" }
        elseif ($Dm22LargeProof) { "GUIDEXOS-DM22-QEMU-LargeFAT32" }
        else { "GUIDEXOS-DM15-QEMU-$Stage" }
    @(
        "proof=$proofName",
        "manifestSchema=$manifestSchema",
        "attemptNumber=$AttemptNumber",
        "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
        "bootMedium=isolated-ESP-directory-backend",
        "machine=$(if ($Dm24FourKnProof) { 'pc-usb-off' } else { 'q35-usb-off-built-in-ICH9-AHCI' })",
        "cpu=QEMU-default (no -cpu argument)",
        "controller=$(if ($Dm24FourKnProof) { 'PIIX3-UHCI' } else { 'ICH9-AHCI on Q35' })",
        "controllerArguments=$(if ($Dm24FourKnProof) { '-machine pc,usb=off -device piix3-usb-uhci,id=uhci' } else { '-machine q35,usb=off (built-in ICH9 AHCI)' })",
        "secondaryDeviceArguments=$(if ($Dm24FourKnProof) { 'usb-storage,logical_block_size=4096,physical_block_size=4096,discard_granularity=4096,serial=DM24USB01' } else { 'ide-hd defaults' })",
        "secondaryTransport=$(if ($Dm24FourKnProof) { 'USB-MASS / SCSI READ CAPACITY' } else { 'AHCI / ATA IDENTIFY' })",
        "secondaryLogicalSectorSize=$(if ($Dm24FourKnProof) { 4096 } else { 'transport-reported' })",
        "secondaryPhysicalSectorSize=$(if ($Dm24FourKnProof) { '4096 configured in QEMU; physical geometry is not reported by USB mass storage' } else { 'transport-reported' })",
        "secondaryCapacityLba=$(if ($Dm24FourKnProof) { [uint64]($DiskSizeBytes / 4096) } else { 'transport-reported' })",
        "accelerator=QEMU default (no -accel argument)",
        "bootloaderSha256=$bootHash",
        "kernelSha256=$kernelHash",
        "kernelBytes=$kernelBytes",
        "secondaryImage=$DiskPath",
        "storageImagePath=$DiskPath",
        "secondaryFormat=raw",
        "secondaryCapacityBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "secondaryRequestedBytes=$DiskSizeBytes",
        "secondaryInitialSha256=$initialHash",
        "preparedImagePath=$(if ($preparedImageFull) { $preparedImageFull } else { 'not-used' })",
        "preparedImageSha256=$(if ($preparedImageFull) { $initialHash } else { 'not-used' })",
        "secondaryInitialActualBytes=$(if ($Dm22LargeProof) { $initialActualBytes } else { 'not-recorded' })",
        "storageImageBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "storageImageSha256Before=$initialHash",
        "storageImageAccess=writable-disposable-only",
        "storageCacheMode=QEMU default (cache option omitted from argv)",
        "uefiImagePath=$OvmfFull",
        "uefiSha256=$ovmfHash",
        "secondaryPlacement=$(if ($Dm24FourKnProof) { 'PIIX3-UHCI USB storage' } else { 'AHCI-port1' })",
        "bootDevicePlacement=$(if ($Dm24FourKnProof) { 'PIIX IDE port0' } else { 'AHCI-port0' })",
        "physicalHostDisksPassedToQemu=none",
        "qemu=$qemuVersion",
        "qemuSha256=$qemuHash",
        "timeoutPrivateOrFirstBootSeconds=$(if ($Dm22LargeProof) { 1800 } elseif ($Dm24FourKnProof -and $FirstBootTimeoutSeconds -gt 0) { $FirstBootTimeoutSeconds } elseif ($Dm24FourKnProof) { 2700 } elseif ($FirstBootTimeoutSeconds -gt 0) { $FirstBootTimeoutSeconds } else { 300 })",
        "timeoutRediscoveryBootSeconds=$(if ($Dm22LargeProof) { 300 } elseif ($Dm24FourKnProof -and $RediscoveryTimeoutSeconds -gt 0) { $RediscoveryTimeoutSeconds } elseif ($Dm24FourKnProof) { 600 } elseif ($RediscoveryTimeoutSeconds -gt 0) { $RediscoveryTimeoutSeconds } else { 180 })",
        "hostQemuProcessesBefore=$($qemuAtStart.Count)",
        "hostQemuPidsBefore=$(($qemuAtStart | ForEach-Object { $_.ProcessId }) -join ',')"
    ) | Set-Content -LiteralPath $manifestPath -Encoding ascii
    @("identity=$proofIdentity", "proofManifest=/$manifestName") |
        Set-Content -LiteralPath (Join-Path $EspPath "build-identity.txt") -Encoding ascii

    if ($Stage -eq "PrivateWrite") {
        $activeBoot = Start-ProofBoot "private-write-boot" `
            "[DM15-QEMU] private-proof=PASS" $EspPath $DiskPath $WorkFull 300 $manifestPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        $finalHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
        if ($initialHash -ne $finalHash) { throw "Private raw write proof did not restore the complete image hash." }
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
            "secondaryFinalSha256=$finalHash",
            "transportResult=PASS",
            "result=PASS tier=1-private-write-readback-flush-restore",
            "sharedWriteRegistration=disabled",
            "imageRestoredByteForByte=yes",
            "serial=private-write-boot.serial.log"
        )
    } else {
        $lifecycleMarker = if ($Dm25PartitionDeleteProof) { "[DM25-QEMU] delete-ready=PASS" }
            elseif ($Dm27QuickReformatProof) { "[DM27-QEMU] lifecycle=PASS" }
            elseif ($Dm24FourKnProof) { "[DM24-QEMU] lifecycle=PASS" }
            elseif ($Dm22LargeProof) { "[DM22-QEMU] lifecycle=PASS" }
            else { "[DM15-QEMU] lifecycle=PASS" }
        $firstTimeout = if ($Dm22LargeProof) { 1800 }
            elseif ($Dm24FourKnProof -and $FirstBootTimeoutSeconds -gt 0) { $FirstBootTimeoutSeconds }
            elseif ($Dm24FourKnProof) { 2700 }
            elseif ($FirstBootTimeoutSeconds -gt 0) { $FirstBootTimeoutSeconds }
            else { 300 }
        $preReformatDiskPath = $null
        if ($Dm27QuickReformatProof) {
            $preReformatDiskPath = Join-Path $WorkFull "secondary-pre-reformat.raw"
            if (Test-Path -LiteralPath $preReformatDiskPath) {
                throw "Refusing to overwrite pre-reformat image evidence $preReformatDiskPath"
            }
            if ($preparedImageFull) {
                Copy-RawImageSparse $DiskPath $preReformatDiskPath
                $firstSerial = "not-run-prepared-fixture"
                Add-Content -LiteralPath $manifestPath -Encoding ascii -Value `
                    "firstBoot=skipped-prepared-image-source=$preparedImageFull"
            } else {
                $activeBoot = Start-ProofBoot "first-boot" $lifecycleMarker $EspPath $DiskPath $WorkFull $firstTimeout $manifestPath
                $firstSerial = $activeBoot.SerialPath
                Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
                $activeBoot = $null
                Copy-RawImageSparse $DiskPath $preReformatDiskPath
            }
            Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
                "preReformatImage=$preReformatDiskPath",
                "preReformatImageSha256=$((Get-FileHash -LiteralPath $preReformatDiskPath -Algorithm SHA256).Hash)"
            )
        } else {
            $activeBoot = Start-ProofBoot "first-boot" $lifecycleMarker $EspPath $DiskPath $WorkFull $firstTimeout $manifestPath
            $firstSerial = $activeBoot.SerialPath
            Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
            $activeBoot = $null
        }
        $preDeleteDiskPath = $null
        if ($Dm25PartitionDeleteProof) {
            $preDeleteDiskPath = Join-Path $WorkFull "secondary-pre-delete.raw"
            if (Test-Path -LiteralPath $preDeleteDiskPath) {
                throw "Refusing to overwrite pre-delete image evidence $preDeleteDiskPath"
            }
            Copy-RawImageSparse $DiskPath $preDeleteDiskPath
            Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
                "preDeleteImage=$preDeleteDiskPath",
                "preDeleteImageSha256=$((Get-FileHash -LiteralPath $preDeleteDiskPath -Algorithm SHA256).Hash)"
            )
        }
        $rediscoveryMarker = if ($Dm28InterruptProof) { "[DM28-QRF] retry-after-cold-restart=PASS" }
            elseif ($Dm25PartitionDeleteProof) { "[DM25-QEMU] delete=PASS" }
            elseif ($Dm27QuickReformatProof) { "[DM27-QEMU] quick-reformat=PASS" }
            elseif ($Dm24FourKnProof) { "[DM24-QEMU] reboot-rediscovery=PASS" }
            elseif ($Dm22LargeProof) { "[DM22-QEMU] reboot-rediscovery=PASS" }
            else { "[DM15-QEMU] reboot-rediscovery=PASS" }
        $rediscoveryTimeout = if ($Dm22LargeProof) { 300 }
            elseif ($Dm24FourKnProof -and $RediscoveryTimeoutSeconds -gt 0) { $RediscoveryTimeoutSeconds }
            elseif ($Dm24FourKnProof) { 600 }
            elseif ($RediscoveryTimeoutSeconds -gt 0) { $RediscoveryTimeoutSeconds }
            else { 180 }
        if ($Dm28InterruptProof) {
            $replacementPath = Join-Path $WorkFull "dm28-replacement.raw"
            if (Test-Path -LiteralPath $replacementPath) {
                throw "Refusing to overwrite replacement-media evidence $replacementPath"
            }
            $replacementStream = [IO.File]::Open($replacementPath,
                [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
                [IO.FileShare]::ReadWrite)
            $replacementStream.Dispose()
            $sparseTool = Join-Path $env:SystemRoot "System32\fsutil.exe"
            & $sparseTool sparse setflag $replacementPath 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to mark replacement image sparse."
            }
            $replacementStream = [IO.File]::Open($replacementPath,
                [IO.FileMode]::Open, [IO.FileAccess]::Write,
                [IO.FileShare]::ReadWrite)
            $replacementStream.SetLength([int64]$DiskSizeBytes)
            $replacementStream.Dispose()
            $replacementHashBefore = (Get-FileHash -LiteralPath $replacementPath `
                -Algorithm SHA256).Hash

            $activeBoot = Start-ProofBoot "interrupt-reformat-boot" "" `
                $EspPath $DiskPath $WorkFull $rediscoveryTimeout $manifestPath `
                "QRF_FAT1_BEGIN"
            $interruptedSerial = $activeBoot.SerialPath
            $qmpLogPath = Join-Path $WorkFull "interruption-qmp.jsonl"
            Set-Content -LiteralPath $qmpLogPath -Value "" -Encoding utf8
            Connect-ProofQmp $activeBoot $qmpLogPath
            [void](Send-ProofQmp "device_del" @{ id="dm24disk" })
            $deviceDeleted = Wait-ProofDeviceDeleted "dm24disk"
            Add-Content -LiteralPath $manifestPath -Encoding ascii -Value `
                "qmp.interruption.deviceDeleted=$($deviceDeleted | ConvertTo-Json -Compress -Depth 8)"
            $interruptResult = Wait-ProofSerialMarker $activeBoot `
                "[DM28-QRF] interruption=PASS" 45

            $driveLine = "drive_add 0 if=none,id=dm28replacementdrive,file=$replacementPath,format=raw,cache=writeback"
            $driveResult = Send-ProofQmp "human-monitor-command" `
                @{ "command-line"=$driveLine }
            if ($driveResult -match "Error:") {
                throw "QMP could not add replacement backing: $driveResult"
            }
            [void](Send-ProofQmp "device_add" @{
                driver="usb-storage"; id="dm28replacement"; bus="uhci.0";
                port="1"; drive="dm28replacementdrive"; removable=$true;
                serial="DM28REPLACEMENT"; logical_block_size=4096;
                physical_block_size=4096
            })
            Start-Sleep -Seconds 2
            $replacementBlockStats = @(
                Send-ProofQmp "query-blockstats" |
                    Where-Object { $_.device -eq "dm28replacementdrive" }
            )
            if ($replacementBlockStats.Count -ne 1 -or
                -not $replacementBlockStats[0].stats) {
                throw "QMP did not report block statistics for replacement media."
            }
            $replacementWriteOperations = [uint64]$replacementBlockStats[0].stats.wr_operations
            $replacementWriteBytes = [uint64]$replacementBlockStats[0].stats.wr_bytes
            if ($replacementWriteOperations -ne 0 -or $replacementWriteBytes -ne 0) {
                throw "QEMU recorded writes to the replacement media."
            }
            Close-ProofQmp
            Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port `
                $activeBoot.SerialPath
            $activeBoot = $null
            $replacementHashAfter = (Get-FileHash -LiteralPath $replacementPath `
                -Algorithm SHA256).Hash
            if ($replacementHashAfter -ne $replacementHashBefore) {
                throw "The detached Quick Reformat changed replacement media."
            }
            Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
                "interruptionStage=QRF_FAT1_BEGIN after-invalidation-flush",
                "interruptionSerial=$([IO.Path]::GetFileName($interruptedSerial))",
                "interruptionResult=PASS guest-reported-incomplete-no-success-claim",
                "replacementImage=$replacementPath",
                "replacementSha256Before=$replacementHashBefore",
                "replacementSha256After=$replacementHashAfter",
                "replacementWriteOperations=$replacementWriteOperations",
                "replacementWriteBytes=$replacementWriteBytes",
                "replacementBlockStats=$($replacementBlockStats[0] | ConvertTo-Json -Compress -Depth 12)",
                "replacementMediaUnchanged=yes",
                "qmpTranscript=$qmpLogPath"
            )

            $interruptedDiskPath = Join-Path $WorkFull "secondary-interrupted.raw"
            $interruptedSourceHash = (Get-FileHash -LiteralPath $DiskPath `
                -Algorithm SHA256).Hash
            Copy-RawImageSparse $DiskPath $interruptedDiskPath
            $interruptedImageHash = (Get-FileHash -LiteralPath $interruptedDiskPath `
                -Algorithm SHA256).Hash
            if ($interruptedImageHash -ne $interruptedSourceHash) {
                throw "Interrupted checkpoint is not byte-identical to the backing image used for cold restart."
            }
            Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
                "interruptedImage=$interruptedDiskPath",
                "interruptedImageSha256=$interruptedImageHash",
                "coldRestartSourceImageSha256=$interruptedSourceHash",
                "coldRestartUsesSameInterruptedBacking=yes"
            )
            $interruptedInspectionPath = Join-Path $WorkFull `
                "interrupted-image-inspection.txt"
            if (-not $PythonExecutable) {
                $python = Get-Command python.exe -ErrorAction SilentlyContinue
                if (-not $python) { throw "Python 3 was not found; specify -PythonExecutable." }
                $PythonExecutable = $python.Source
            }
            $interruptedInspection = & $PythonExecutable `
                (Join-Path $Root "scripts\verify-dm28-qemu-interrupted.py") `
                $preReformatDiskPath $interruptedDiskPath 2>&1
            $interruptedInspection | Set-Content -LiteralPath `
                $interruptedInspectionPath -Encoding utf8
            if ($LASTEXITCODE -ne 0) {
                throw "Independent interrupted-image verification failed; see $interruptedInspectionPath"
            }
            Add-Content -LiteralPath $manifestPath -Encoding ascii -Value `
                "interruptedImageInspection=PASS path=$interruptedInspectionPath"

            $activeBoot = Start-ProofBoot "recovery-retry-boot" `
                $rediscoveryMarker $EspPath $DiskPath $WorkFull `
                $rediscoveryTimeout $manifestPath
        } else {
            $activeBoot = Start-ProofBoot "rediscovery-boot" `
                $rediscoveryMarker $EspPath $DiskPath $WorkFull `
                $rediscoveryTimeout $manifestPath
        }
        $rediscoverySerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        $postReformatDiskPath = $null
        $rebootRediscoverySerial = $null
        if ($Dm27QuickReformatProof) {
            $postReformatDiskPath = Join-Path $WorkFull "secondary-post-reformat.raw"
            if (Test-Path -LiteralPath $postReformatDiskPath) {
                throw "Refusing to overwrite post-reformat image evidence $postReformatDiskPath"
            }
            Copy-RawImageSparse $DiskPath $postReformatDiskPath
            Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
                "postReformatImage=$postReformatDiskPath",
                "postReformatImageSha256=$((Get-FileHash -LiteralPath $postReformatDiskPath -Algorithm SHA256).Hash)"
            )
            $activeBoot = Start-ProofBoot "cold-restart-boot" `
                "[DM27-QEMU] reboot-rediscovery=PASS" $EspPath $DiskPath $WorkFull `
                $rediscoveryTimeout $manifestPath
            $rebootRediscoverySerial = $activeBoot.SerialPath
            Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
            $activeBoot = $null
        }
        $deleteRestartSerial = $null
        if ($Dm25PartitionDeleteProof) {
            $activeBoot = Start-ProofBoot "delete-restart-boot" `
                "[DM25-QEMU] delete-restart=PASS" $EspPath $DiskPath $WorkFull `
                $rediscoveryTimeout $manifestPath
            $deleteRestartSerial = $activeBoot.SerialPath
            Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
            $activeBoot = $null
        }
        if (-not $PythonExecutable) {
            $python = Get-Command python.exe -ErrorAction SilentlyContinue
            if (-not $python) { throw "Python 3 was not found; specify -PythonExecutable." }
            $PythonExecutable = $python.Source
        }
        $inspectionPath = Join-Path $WorkFull "disk-inspection.txt"
        $verifier = if ($Dm27QuickReformatProof) {
            Join-Path $Root "scripts\verify-dm27-qemu-reformat.py"
        } elseif ($Dm25PartitionDeleteProof) {
            Join-Path $Root "scripts\verify-dm25-qemu-delete.py"
        } elseif ($Dm24FourKnProof -or $Dm22LargeProof) {
            Join-Path $Root "scripts\verify-dm22-qemu-image.py"
        } else { Join-Path $Root "scripts\verify-dm9-qemu-image.py" }
        if ($Dm27QuickReformatProof) {
            if ($Dm22LargeProof) {
                $inspection = & $PythonExecutable $verifier $preReformatDiskPath $postReformatDiskPath $DiskPath --large-payload 2>&1
            } elseif ($Dm24FourKnProof) {
                $inspection = & $PythonExecutable $verifier $preReformatDiskPath $postReformatDiskPath $DiskPath --sector-size 4096 2>&1
            } else {
                $inspection = & $PythonExecutable $verifier $preReformatDiskPath $postReformatDiskPath $DiskPath 2>&1
            }
        } elseif ($Dm25PartitionDeleteProof) {
            $inspection = & $PythonExecutable $verifier $preDeleteDiskPath $DiskPath 2>&1
        } elseif ($Dm24FourKnProof) {
            $inspection = & $PythonExecutable $verifier $DiskPath --sector-size 4096 2>&1
        } else {
            $inspection = & $PythonExecutable $verifier $DiskPath 2>&1
        }
        $inspection | Set-Content -LiteralPath $inspectionPath -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Independent raw-image verification failed; see $inspectionPath" }
        $finalActualBytes = if ($Dm22LargeProof) {
            Save-Dm22ImageAllocation $DiskPath $WorkFull "final"
        } else { 0 }
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
            "secondaryFinalSha256=$((Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash)",
            "secondaryFinalActualBytes=$(if ($Dm22LargeProof) { $finalActualBytes } else { 'not-recorded' })",
            "firstBootSerial=$([IO.Path]::GetFileName($firstSerial))",
            "rediscoverySerial=$([IO.Path]::GetFileName($rediscoverySerial))",
            "coldRestartSerial=$(if ($rebootRediscoverySerial) { [IO.Path]::GetFileName($rebootRediscoverySerial) } else { 'not-applicable' })",
            "deleteRestartSerial=$(if ($deleteRestartSerial) { [IO.Path]::GetFileName($deleteRestartSerial) } else { 'not-applicable' })",
            "result=PASS tier=$(if ($Dm28InterruptProof) { 'DM28-USB-real-removal-cold-restart-marker-retry-post-retry-restart' } elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { 'DM27-4Kn-USB-quick-reformat-cold-restart-byte-verified' } elseif ($Dm27QuickReformatProof) { 'DM27-AHCI-quick-reformat-cold-restart-byte-verified' } elseif ($Dm25PartitionDeleteProof) { 'DM25-AHCI-GPT-delete-restart-byte-identical-partition-data' } elseif ($Dm24FourKnProof) { 'DM24-4Kn-FAT32-USB-high-cluster-96KiB-file-restart' } elseif ($Dm22LargeProof) { 'DM22-large-FAT32-AHCI-high-cluster-96KiB-file-restart' } else { '2-full-lifecycle-and-restart-rediscovery' })",
            "failedStage=none",
            "writesOccurred=yes",
            "inspection=PASS read-only-GPT-FAT32-independent-verifier",
            "transportResult=PASS"
        )
    }
    $passedProofName = if ($Dm28InterruptProof) { "DM28 4Kn USB Quick Reformat interruption and recovery" }
        elseif ($Dm27QuickReformatProof -and $Dm22LargeProof) { "DM28 10 GiB AHCI Quick Reformat" }
        elseif ($Dm27QuickReformatProof -and $Dm24FourKnProof) { "DM27 4Kn USB Quick Reformat" }
        elseif ($Dm27QuickReformatProof) { "DM27 AHCI Quick Reformat" }
        elseif ($Dm25PartitionDeleteProof) { "DM25 AHCI partition delete" }
        elseif ($Dm24FourKnProof) { "DM24 4Kn FAT32 USB" }
        elseif ($Dm22LargeProof) { "DM22 large FAT32 AHCI" }
        else { "DM15 $Stage" }
    Write-Host "$passedProofName proof passed. Preserved artifacts: $WorkFull"
} catch {
    $failureText = $_.Exception.Message -replace '[\r\n]+', ' '
    if ($activeBoot) {
        try { Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath }
        catch { $failureText += " cleanup=$($_.Exception.Message -replace '[\r\n]+', ' ')" }
        $activeBoot = $null
    }
    @("result=FAIL", "detail=$failureText") | Set-Content -LiteralPath (Join-Path $WorkFull "failure.txt") -Encoding utf8
    if (Test-Path -LiteralPath $manifestPath) {
        $finalHash = "unavailable"
        if (Test-Path -LiteralPath $DiskPath) {
            try { $finalHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash }
            catch { $failureText += " imageHash=locked-or-unavailable" }
        }
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
            "secondaryFinalSha256=$finalHash", "transportResult=FAIL",
            "result=FAIL", "failure=$failureText"
        )
    }
    throw
}
