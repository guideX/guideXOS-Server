<#
.SYNOPSIS
    Runs DM16/DM17 private NVMe proofs or one full NVMe storage lifecycle.

.DESCRIPTION
    Creates an isolated ESP copy and a fresh 600 MiB raw secondary image, then
    boots from a separate IDE-backed ESP and attaches the raw image to a QEMU
    NVMe controller. DM17's private integrity gate requires a dense image and
    writeback cache, pauses after each write/restore for host-side inspection,
    and leaves common NVMe writes and Flush disabled.
#>
[CmdletBinding()]
param(
    [ValidateSet("PrivateWrite", "Lifecycle")]
    [string]$Stage = "PrivateWrite",
    [string]$EspSource = "ESP",
    [string]$WorkDir = "",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [ValidateSet("writeback", "writethrough", "none", "directsync", "unsafe", "default")]
    [string]$CacheMode = "writeback",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
    [string]$EspCacheDirectory = "",
    [string]$DiskDirectory = "",
    [int]$AttemptNumber = 1,
    [switch]$Dm17Proof,
    [switch]$Dm17CommonWriteProof,
    [switch]$EnableNvmeCallbacks,
    [switch]$DenseImage,
    [switch]$QemuDebug,
    [switch]$QemuTrace,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

if ($Dm17Proof -and ($Stage -ne "PrivateWrite" -or
        $CacheMode -ne "writeback" -or -not $DenseImage)) {
    throw "DM17 proof requires PrivateWrite, cache=writeback, and a dense raw image."
}
if ($Dm17CommonWriteProof -and (-not $Dm17Proof -or
        $Stage -ne "PrivateWrite")) {
    throw "DM17 common-write proof requires the DM17 PrivateWrite proof."
}
if ($EnableNvmeCallbacks -and ($Stage -ne "Lifecycle" -or
        $CacheMode -ne "writeback" -or -not $DenseImage)) {
    throw "NVMe callback lifecycle proof requires Lifecycle, cache=writeback, and a dense raw image."
}

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

function Send-ProofQmpCommand([int]$QmpPort, [string]$CommandName) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.ReceiveTimeout = 2000
        $client.Connect([System.Net.IPAddress]::Loopback, $QmpPort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 2000
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII,
            $false, 1024, $true)
        $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII,
            1024, $true)
        $writer.NewLine = "`r`n"
        [void]$reader.ReadLine() # QMP greeting.
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $writer.Flush()
        $capabilitiesReply = $reader.ReadLine()
        if (-not $capabilitiesReply -or $capabilitiesReply -notmatch '"return"') {
            throw "QMP capability negotiation failed: $capabilitiesReply"
        }
        $writer.WriteLine("{`"execute`":`"$CommandName`"}")
        $writer.Flush()
        if ($CommandName -eq "quit") { return "sent" }
        $reply = $null
        for ($i = 0; $i -lt 16; $i++) {
            $line = $reader.ReadLine()
            if ($line -match '"return"') { $reply = $line; break }
            if ($line -match '"error"') { throw "QMP $CommandName failed: $line" }
        }
        if (-not $reply) {
            throw "QMP $CommandName did not return a command result."
        }
        return $reply
    } finally {
        $client.Dispose()
    }
}

function Get-Dm17ExpectedRange([UInt64]$Lba, [UInt32]$Blocks,
                               [UInt32]$Generation, [UInt32]$BlockSize = 512) {
    $bytes = New-Object byte[] ($Blocks * $BlockSize)
    for ($blockIndex = 0; $blockIndex -lt $Blocks; $blockIndex++) {
        $blockLba = $Lba + [UInt64]$blockIndex
        $baseOffset = $blockIndex * $BlockSize
        $bytes[$baseOffset + 0] = [byte][char]'D'
        $bytes[$baseOffset + 1] = [byte][char]'M'
        $bytes[$baseOffset + 2] = [byte][char]'1'
        $bytes[$baseOffset + 3] = [byte][char]'7'
        for ($i = 0; $i -lt 4; $i++) {
            $bytes[$baseOffset + 4 + $i] = [byte](($Generation -shr (8 * $i)) -band 0xff)
            $bytes[$baseOffset + 16 + $i] = [byte](($blockIndex -shr (8 * $i)) -band 0xff)
        }
        for ($i = 0; $i -lt 8; $i++) {
            $bytes[$baseOffset + 8 + $i] = [byte](($blockLba -shr (8 * $i)) -band 0xff)
        }
        for ($i = 20; $i -lt $BlockSize; $i++) {
            $value = ([UInt64]$Generation * 29 + $blockLba * 17 +
                [UInt64]$blockIndex * 53 + [UInt64]$i * 37 + 0xC3) -band 0xff
            $bytes[$baseOffset + $i] = [byte]$value
        }
    }
    return ,$bytes
}

function Get-Dm17Sha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToHexString($sha.ComputeHash($Bytes)) }
    finally { $sha.Dispose() }
}

function Save-Dm17HostInspection([string]$DiskPath, [string]$OutputPath,
                                 [string]$Phase, [UInt32]$Generation,
                                 [UInt64]$Lba, [UInt32]$Blocks,
                                 [UInt32]$BlockSize) {
    $length = [int]($Blocks * $BlockSize)
    $offset = [UInt64]$Lba * [UInt64]$BlockSize
    $expected = if ($Phase -eq "write") {
        Get-Dm17ExpectedRange $Lba $Blocks $Generation $BlockSize
    } else { New-Object byte[] $length }
    $actual = New-Object byte[] $length
    $before = New-Object byte[] $BlockSize
    $after = New-Object byte[] $BlockSize
    $stream = [IO.File]::Open($DiskPath, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($offset + [UInt64]$length -gt [UInt64]$stream.Length) {
            throw "DM17 host inspection range exceeds the backing image."
        }
        $stream.Position = [Int64]$offset
        $read = $stream.Read($actual, 0, $actual.Length)
        if ($read -ne $actual.Length) { throw "Short host read for DM17 target range." }
        if ($offset -ge [UInt64]$BlockSize) {
            $stream.Position = [Int64]($offset - [UInt64]$BlockSize)
            $read = $stream.Read($before, 0, $before.Length)
            if ($read -ne $before.Length) { throw "Short host read for preceding block." }
        }
        if ($offset + [UInt64]$length + [UInt64]$BlockSize -le [UInt64]$stream.Length) {
            $stream.Position = [Int64]($offset + [UInt64]$length)
            $read = $stream.Read($after, 0, $after.Length)
            if ($read -ne $after.Length) { throw "Short host read for following block." }
        }
    } finally { $stream.Dispose() }

    $targetMatches = $true
    for ($i = 0; $i -lt $length; $i++) {
        if ($actual[$i] -ne $expected[$i]) { $targetMatches = $false; break }
    }
    $neighborsZero = $true
    foreach ($value in $before) { if ($value -ne 0) { $neighborsZero = $false; break } }
    if ($neighborsZero) {
        foreach ($value in $after) { if ($value -ne 0) { $neighborsZero = $false; break } }
    }
    $expectedHash = Get-Dm17Sha256 $expected
    $actualHash = Get-Dm17Sha256 $actual
    $regionName = "dm17-host-region-g{0:X4}-lba{1:X}-{2}.bin" -f $Generation, $Lba, $Phase
    [IO.File]::WriteAllBytes((Join-Path $OutputPath $regionName), $actual)
    if ($Phase -eq "write") {
        [IO.File]::WriteAllBytes((Join-Path $OutputPath ("dm17-intended-g{0:X4}-lba{1:X}.bin" -f $Generation, $Lba)), $expected)
    }
    $first64 = ($actual[0..([Math]::Min(63, $actual.Length - 1))] |
        ForEach-Object { $_.ToString('X2') }) -join ''
    $line = "phase=$Phase generation=$Generation lba=0x{0:X} offset=0x{1:X} blocks=$Blocks bytes=$length expectedSha256=$expectedHash actualSha256=$actualHash targetMatches=$targetMatches adjacentBlocksZero=$neighborsZero targetFirst64=$first64" -f $Lba, $offset
    Add-Content -LiteralPath $script:Dm17InspectionPath -Value $line -Encoding ascii
    Write-Host "DM17 host image: $line"
    if (-not $targetMatches -or -not $neighborsZero) { $script:Dm17HostInspectionFailures++ }
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

function Ensure-EspCache([string]$Source, [string]$Cache,
                         [string]$Bootloader, [string]$Kernel) {
    $sourceHash = Get-EspTreeHash $Source
    $bootHash = (Get-FileHash -LiteralPath $Bootloader -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath $Kernel -Algorithm SHA256).Hash
    $cacheManifest = Join-Path $Cache "dm16-esp-cache.txt"
    $excluded = @("EFI\BOOT\BOOTX64.EFI", "kernel.elf", "build-identity.txt")
    if (Test-Path -LiteralPath $Cache) {
        if (Test-Path -LiteralPath $cacheManifest) {
            $cacheLines = Get-Content -LiteralPath $cacheManifest
            foreach ($expected in @("sourceEspSha256=$sourceHash", "bootloaderSha256=$bootHash", "kernelSha256=$kernelHash")) {
                if ($cacheLines -notcontains $expected) {
                    throw "Existing ESP cache does not match current inputs: $Cache"
                }
            }
            return
        }

        # Recover an interrupted task-owned cache. Existing files are verified;
        # missing files are linked so this work does not duplicate large assets.
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
            if (Test-Path -LiteralPath $target) {
                if ((Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash -ne
                    (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
                    throw "Incomplete ESP cache contains a mismatched file: $target"
                }
            } else {
                New-Item -ItemType HardLink -Path $target -Target $item.FullName | Out-Null
            }
        }
        $bootPath = Join-Path $Cache "EFI\BOOT"
        New-Item -ItemType Directory -Path $bootPath -Force | Out-Null
        foreach ($pair in @(
            [pscustomobject]@{Path=(Join-Path $bootPath "BOOTX64.EFI"); Source=$Bootloader; Hash=$bootHash},
            [pscustomobject]@{Path=(Join-Path $Cache "kernel.elf"); Source=$Kernel; Hash=$kernelHash}
        )) {
            if (Test-Path -LiteralPath $pair.Path) {
                if ((Get-FileHash -LiteralPath $pair.Path -Algorithm SHA256).Hash -ne $pair.Hash) {
                    throw "Incomplete ESP cache contains a mismatched boot artifact: $($pair.Path)"
                }
            } else {
                New-Item -ItemType HardLink -Path $pair.Path -Target $pair.Source | Out-Null
            }
        }
        @("sourceEspSha256=$sourceHash", "bootloaderSha256=$bootHash", "kernelSha256=$kernelHash") |
            Set-Content -LiteralPath $cacheManifest -Encoding ascii
        return
    }
    New-Item -ItemType Directory -Path $Cache -Force | Out-Null
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
        New-Item -ItemType HardLink -Path $target -Target $item.FullName | Out-Null
    }
    $bootPath = Join-Path $Cache "EFI\BOOT"
    New-Item -ItemType Directory -Path $bootPath -Force | Out-Null
    New-Item -ItemType HardLink -Path (Join-Path $bootPath "BOOTX64.EFI") `
        -Target $Bootloader | Out-Null
    New-Item -ItemType HardLink -Path (Join-Path $Cache "kernel.elf") `
        -Target $Kernel | Out-Null
    @("sourceEspSha256=$sourceHash", "bootloaderSha256=$bootHash", "kernelSha256=$kernelHash") |
        Set-Content -LiteralPath $cacheManifest -Encoding ascii
}

function New-EspCopy([string]$Cache, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Cache -File -Force -Recurse) {
        $relative = $item.FullName.Substring($Cache.Length).TrimStart([char]'\')
        if ($relative -ieq "dm16-esp-cache.txt") { continue }
        $target = Join-Path $Destination $relative
        $parent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        New-Item -ItemType HardLink -Path $target -Target $item.FullName | Out-Null
    }
}

function Stop-ProofQemu([int]$ProcessId, [int]$Port, [int]$QmpPort,
                        [string]$ExpectedSerialPath) {
    if ($ProcessId -le 0) { throw "QEMU process ID is missing during proof cleanup." }
    $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId"
    if (-not $owned) { return }
    if ($owned.Name -ne "qemu-system-x86_64.exe" -or
        $owned.CommandLine -notlike "*$ExpectedSerialPath*") {
        throw "Refusing to stop PID $ProcessId because its QEMU command line does not match this proof."
    }
    Write-Host "DM16 cleanup: requesting graceful QEMU shutdown PID=$ProcessId"
    $ownedProcess = [System.Diagnostics.Process]::GetProcessById($ProcessId)
    try {
        [void](Send-ProofQmpCommand $QmpPort "quit")
    } catch {
        if (-not $ownedProcess.HasExited) { throw }
    }
    if (-not $ownedProcess.WaitForExit(15000)) {
        $ownedProcess.Kill()
        [void]$ownedProcess.WaitForExit(10000)
        $ownedProcess.Dispose()
        throw "QEMU did not complete graceful shutdown; forced termination may leave pending backend I/O."
    }
    $ownedProcess.Dispose()
}

function Save-ProofDmaSnapshot([int]$Port, [string]$Serial,
                               [string]$OutputPath, [string]$RunName) {
    $queueMatch = [regex]::Match($Serial, 'sqPhysical=([0-9A-Fa-f]+)')
    $physicalMatch = [regex]::Match($Serial, 'bouncePhysical=([0-9A-Fa-f]+)')
    $virtualMatch = [regex]::Match($Serial, 'bounceVirtual=([0-9A-Fa-f]+)')
    if (-not $queueMatch.Success -or -not $physicalMatch.Success -or
        -not $virtualMatch.Success) { return }
    $queueAddress = $queueMatch.Groups[1].Value
    $physicalAddress = $physicalMatch.Groups[1].Value
    $virtualAddress = $virtualMatch.Groups[1].Value
    $snapshotPath = Join-Path $OutputPath "$RunName.dma-snapshot.txt"
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.ReceiveTimeout = 1000
        $client.Connect([System.Net.IPAddress]::Loopback, $Port)
        $stream = $client.GetStream()
        $readMonitor = {
            $buffer = New-Object byte[] 4096
            $output = [Text.StringBuilder]::new()
            Start-Sleep -Milliseconds 100
            while ($stream.DataAvailable) {
                try {
                    $read = $stream.Read($buffer, 0, $buffer.Length)
                    if ($read -le 0) { break }
                    [void]$output.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $read))
                } catch [System.IO.IOException] { break }
            }
            return $output.ToString()
        }
        $greeting = & $readMonitor
        $commands = @("xp /64bx 0x$queueAddress", "xp /16bx 0x$physicalAddress", "x /16bx 0x$virtualAddress")
        $results = [System.Collections.Generic.List[string]]::new()
        foreach ($command in $commands) {
            $bytes = [Text.Encoding]::ASCII.GetBytes("$command`r`n")
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
            $results.Add((& $readMonitor))
        }
        @("qemuMonitorSubmissionQueueAddress=0x$queueAddress", "qemuMonitorPhysicalAddress=0x$physicalAddress", "qemuMonitorVirtualAddress=0x$virtualAddress", "greeting=$greeting", "submissionEntry=$($results[0])", "physicalRead=$($results[1])", "virtualRead=$($results[2])") |
            Set-Content -LiteralPath $snapshotPath -Encoding utf8
    } finally { $client.Dispose() }
}

function Start-ProofBoot([string]$RunName, [string]$SuccessMarker,
                         [string]$EspPath, [string]$DiskPath,
                         [string]$OutputPath, [int]$TimeoutSeconds) {
    $serialPath = Join-Path $OutputPath "$RunName.serial.log"
    $stderrPath = Join-Path $OutputPath "$RunName.stderr.log"
    $stdoutPath = Join-Path $OutputPath "$RunName.stdout.log"
    $debugPath = Join-Path $OutputPath "$RunName.qemu-debug.log"
    Remove-Item -LiteralPath $serialPath,$stderrPath,$stdoutPath -Force -ErrorAction SilentlyContinue
    $port = Get-FreeLoopbackPort
    $qmpPort = Get-FreeLoopbackPort
    $arguments = @(
        "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfFull",
        "-machine", "q35,usb=off",
        "-drive", "if=none,id=dm16boot,format=raw,file=fat:rw:$EspPath",
        "-device", "ide-hd,drive=dm16boot,bus=ide.0",
        "-drive", "if=none,id=dm16secondary,format=raw,cache=$CacheMode,file=$DiskPath",
        "-device", "nvme,id=dm16nvme,serial=GXOSDM16NVME,drive=dm16secondary",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-object", "rng-builtin,id=rng0",
        "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", "file:$serialPath",
        "-monitor", "tcp:127.0.0.1:$port,server,nowait",
        "-qmp", "tcp:127.0.0.1:$qmpPort,server,nowait",
        "-rtc", "base=utc,clock=host", "-no-reboot"
    )
    if ($QemuDebug) { $arguments += @("-d", "guest_errors,int,cpu_reset", "-D", $debugPath) }
    if ($QemuTrace) {
        $tracePath = Join-Path $OutputPath "$RunName.nvme-trace.log"
        $arguments += @("-trace", "enable=pci_nvme_io_cmd,file=$tracePath")
    }
    $process = Start-Process -FilePath $QemuFull -ArgumentList $arguments `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $seenDm17InspectionMessages = 0
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serialPath) {
            $serial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
            if ($serial -match '\[KERNEL-FAULT\]') {
                Stop-ProofQemu $process.Id $port $qmpPort $serialPath
                throw "$RunName encountered a kernel fault; see $serialPath"
            }
            if ($serial -match '(?m)^\[(?:DM17-QEMU|DM16-QEMU|DM9-QEMU)\] (?:private-proof=FAIL|lifecycle=FAIL|proof=BLOCKED|reboot-rediscovery=FAIL|initialize=FAIL|create-partition=FAIL|format-fat32=FAIL)') {
                $failureLine = $Matches[0]
                Stop-ProofQemu $process.Id $port $qmpPort $serialPath
                throw "$RunName reported '$failureLine'; see $serialPath"
            }
            if ($Dm17Proof -and $serial) {
                $inspectionMatches = [regex]::Matches($serial,
                    '(?m)^\[DM17-QEMU\] host-inspect phase=(write|restore) generation=([0-9A-Fa-f]+) lba=([0-9A-Fa-f]+) blocks=([0-9A-Fa-f]+) blockSize=([0-9A-Fa-f]+)')
                while ($seenDm17InspectionMessages -lt $inspectionMatches.Count) {
                    $gate = $inspectionMatches[$seenDm17InspectionMessages]
                    $seenDm17InspectionMessages++
                    [void](Send-ProofQmpCommand $qmpPort "stop")
                    try {
                        Save-Dm17HostInspection $DiskPath $OutputPath `
                            $gate.Groups[1].Value `
                            ([Convert]::ToUInt32($gate.Groups[2].Value, 16)) `
                            ([Convert]::ToUInt64($gate.Groups[3].Value, 16)) `
                            ([Convert]::ToUInt32($gate.Groups[4].Value, 16)) `
                            ([Convert]::ToUInt32($gate.Groups[5].Value, 16))
                    } finally {
                        [void](Send-ProofQmpCommand $qmpPort "cont")
                    }
                }
            }
            if ($serial -and $serial.Contains("[DM16-NVME-DMA] submit-pause=START") -and
                -not (Test-Path -LiteralPath (Join-Path $OutputPath "$RunName.dma-snapshot.txt"))) {
                try { Save-ProofDmaSnapshot $port $serial $OutputPath $RunName }
                catch {
                    Stop-ProofQemu $process.Id $port $qmpPort $serialPath
                    throw
                }
            }
            if ($serial -and $serial.Contains($SuccessMarker) -and
                $serial.Contains("[KERNEL] Entering main loop")) {
                return [pscustomobject]@{ ProcessId=$process.Id; Port=$port; QmpPort=$qmpPort; SerialPath=$serialPath }
            }
        }
        if ($process.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
    if (-not $process.HasExited) { Stop-ProofQemu $process.Id $port $qmpPort $serialPath }
    throw "$RunName timed out or exited before '$SuccessMarker'. $stderrPath $serialPath"
}

if (-not (Test-Path -LiteralPath $QemuExecutable)) { throw "QEMU was not found at $QemuExecutable" }
if ($AttemptNumber -lt 1) { throw "AttemptNumber must be positive." }
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
$EspFull = (Resolve-Path -LiteralPath $EspSource).Path
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $WorkDir) { $WorkDir = "out\dm16-$($Stage.ToLowerInvariant())-$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below $repoOut" }
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files; select a fresh output directory."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }

$proofPrefix = if ($Dm17Proof -or $EnableNvmeCallbacks) { "dm17" } else { "dm16" }
$manifestName = "$proofPrefix-manifest.txt"
$script:Dm17HostInspectionFailures = 0
$script:Dm17InspectionPath = Join-Path $WorkFull "dm17-host-image-inspection.txt"

    $DiskRoot = if ($DiskDirectory) {
    if ([IO.Path]::IsPathRooted($DiskDirectory)) {
        [IO.Path]::GetFullPath($DiskDirectory)
    } else { [IO.Path]::GetFullPath((Join-Path $Root $DiskDirectory)) }
} else { $WorkFull }
if (-not (Test-Path -LiteralPath $DiskRoot)) {
    New-Item -ItemType Directory -Path $DiskRoot -Force | Out-Null
}
$DiskPath = Join-Path $DiskRoot ("{0}-secondary-{1}-{2:D2}.raw" -f $proofPrefix, $Stage.ToLowerInvariant(), $AttemptNumber)
$EspPath = Join-Path $WorkFull "esp"
$manifestPath = Join-Path $WorkFull $manifestName
$activeBoot = $null

try {
    if (-not $SkipBuild) {
        $make = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue
        if (-not $make) { $make = Get-Command make.exe -ErrorAction SilentlyContinue }
        if (-not $make -and (Test-Path -LiteralPath "C:\mingw64\bin\mingw32-make.exe")) {
            $makePath = "C:\mingw64\bin\mingw32-make.exe"
        } elseif ($make) { $makePath = $make.Source } else { throw "MinGW make was not found." }
        foreach ($object in @("main.o", "qemu_dm9_storage_proof.o", "nvme.o")) {
            $objectPath = Join-Path $Root "kernel\build\amd64\obj\core\$object"
            if (Test-Path -LiteralPath $objectPath) { Remove-Item -LiteralPath $objectPath -Force }
        }
        $flags = if ($Dm17Proof) {
            "-DGXOS_DM16_QEMU_NVME_PROOF -DGXOS_DM16_NVME_PRIVATE_PROOF -DGXOS_DM17_SINGLE_BLOCK_PROOF"
        } elseif ($Stage -eq "PrivateWrite") {
            "-DGXOS_DM16_QEMU_NVME_PROOF -DGXOS_DM16_NVME_PRIVATE_PROOF"
        } else { "-DGXOS_DM16_QEMU_NVME_PROOF" }
        if ($Dm17CommonWriteProof) {
            $flags += " -DGXOS_DM17_COMMON_WRITE_PROOF -DGXOS_DM16_NVME_WRITE_PROVEN"
        }
        if ($EnableNvmeCallbacks) {
            $flags += " -DGXOS_DM16_NVME_WRITE_PROVEN -DGXOS_DM16_NVME_FLUSH_PROVEN -DGXOS_DM17_QEMU_NVME_LIFECYCLE_PROOF -DGXOS_DM17_NVME_WRITE_TRACE"
        }
        $kernelBuildLog = Join-Path $WorkFull "kernel-build-$($Stage.ToLowerInvariant()).log"
        $priorErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $makePath -C (Join-Path $Root "kernel") ARCH=amd64 "EXTRA_CFLAGS=$flags" -j4 `
                2>&1 | Out-File -LiteralPath $kernelBuildLog -Encoding utf8
            $kernelBuildExitCode = $LASTEXITCODE
        } finally { $ErrorActionPreference = $priorErrorActionPreference }
        if ($kernelBuildExitCode -ne 0) {
            Get-Content -LiteralPath $kernelBuildLog -Tail 80
            throw "DM16 $Stage kernel build failed; see $kernelBuildLog."
        }
        $msbuild = Find-MSBuild
        try {
            $ErrorActionPreference = "Continue"
            & $msbuild (Join-Path $Root "guideXOSBootLoader\guideXOSBootLoader.vcxproj") `
                /t:Build /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal `
                2>&1 | Out-File -LiteralPath (Join-Path $WorkFull "uefi-release-build.log") -Encoding utf8
            $uefiBuildExitCode = $LASTEXITCODE
        } finally { $ErrorActionPreference = $priorErrorActionPreference }
        if ($uefiBuildExitCode -ne 0) {
            Get-Content -LiteralPath (Join-Path $WorkFull "uefi-release-build.log") -Tail 80
            throw "UEFI bootloader build failed."
        }
    }

    if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) {
        throw "EspSource must contain guideXOS runtime files, including ramdisk.img."
    }
    $bootloaderSource = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
    $kernelSource = Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
    if (-not (Test-Path -LiteralPath $kernelSource) -and $SkipBuild -and $EspCacheDirectory) {
        $candidateCache = if ([IO.Path]::IsPathRooted($EspCacheDirectory)) {
            [IO.Path]::GetFullPath($EspCacheDirectory)
        } else { [IO.Path]::GetFullPath((Join-Path $Root $EspCacheDirectory)) }
        $cachedKernel = Join-Path $candidateCache "kernel.elf"
        if (Test-Path -LiteralPath $cachedKernel) { $kernelSource = $cachedKernel }
    }
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
    $sparseTool = Join-Path $env:SystemRoot "System32\fsutil.exe"
    if (-not (Test-Path -LiteralPath $sparseTool)) { throw "fsutil.exe is required to create a sparse raw proof image." }
    $diskStream = [IO.File]::Open($DiskPath, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    $diskStream.Dispose()
    if (-not $DenseImage) {
        & $sparseTool sparse setflag $DiskPath 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to mark the new raw proof image sparse (fsutil exit $LASTEXITCODE)." }
    }
    $diskStream = [IO.File]::Open($DiskPath, [IO.FileMode]::Open,
        [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    $diskStream.SetLength(600L * 1024L * 1024L)
    $diskStream.Dispose()
    $bootHash = (Get-FileHash -LiteralPath (Join-Path $bootPath "BOOTX64.EFI") -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath (Join-Path $EspPath "kernel.elf") -Algorithm SHA256).Hash
    $initialHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
    @(
        "proof=$($proofPrefix.ToUpperInvariant())-NVMe-$Stage",
        "attemptNumber=$AttemptNumber",
        "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
        "bootMedium=isolated-ESP-directory-backend",
        "machine=q35-usb-off",
        "bootDevice=IDE-backed-isolated-ESP-directory",
        "nvmeController=QEMU-nvme,id=dm16nvme,serial=GXOSDM16NVME",
        "nvmeNamespace=nsid-1,logical-block-size-default-512",
        "bootloaderSha256=$bootHash",
        "kernelSha256=$kernelHash",
        "secondaryImage=$DiskPath",
        "secondaryFormat=raw",
        "secondaryCacheMode=$CacheMode",
        "secondarySparse=$(-not $DenseImage)",
        "secondaryCapacityBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "secondaryInitialSha256=$initialHash",
        "secondaryPlacement=QEMU-NVMe-controller-namespace-1",
        "physicalHostDisksPassedToQemu=none",
        "qemu=$((& $QemuFull --version | Select-Object -First 1))"
    ) | Set-Content -LiteralPath $manifestPath -Encoding ascii
    @("identity=GUIDEXOS-$($proofPrefix.ToUpperInvariant())-QEMU-$Stage", "proofManifest=/$manifestName") |
        Set-Content -LiteralPath (Join-Path $EspPath "build-identity.txt") -Encoding ascii

    if ($Stage -eq "PrivateWrite") {
        $successMarker = if ($Dm17Proof) {
            "[DM17-QEMU] restart-proof=READY"
        } else { "[DM16-QEMU] private-proof=PASS" }
        $activeBoot = Start-ProofBoot "private-write-boot" `
            $successMarker $EspPath $DiskPath $WorkFull 900
        $firstSerialPath = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.QmpPort $activeBoot.SerialPath
        $activeBoot = $null
        $restartSerialPath = ""
        if ($Dm17Proof) {
            $firstSerial = Get-Content -LiteralPath $firstSerialPath -Raw
            if (-not $firstSerial.Contains("[DM17-QEMU] data-integrity-gate=PASS") -or
                -not $firstSerial.Contains("[DM17-QEMU] flush-gate=PASS cycles=00000064") -or
                -not $firstSerial.Contains("[DM17-QEMU] restart-proof=READY")) {
                throw "DM17 first boot did not complete data, Flush, and restart-setup gates."
            }
            if ($Dm17CommonWriteProof -and
                -not $firstSerial.Contains("[DM17-QEMU] common-write-stage=PASS")) {
                throw "DM17 common write callback stage did not pass."
            }
            $activeBoot = Start-ProofBoot "restart-boot" `
                "[DM17-QEMU] restart-proof=PASS" $EspPath $DiskPath $WorkFull 300
            $restartSerialPath = $activeBoot.SerialPath
            Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port `
                $activeBoot.QmpPort $activeBoot.SerialPath
            $activeBoot = $null
            $restartSerial = Get-Content -LiteralPath $restartSerialPath -Raw
            if (-not $restartSerial.Contains("[DM17-QEMU] restart-read=PASS") -or
                -not $restartSerial.Contains("[DM17-QEMU] restart-proof=PASS")) {
                throw "DM17 restart boot did not read back and restore the flushed pattern."
            }
        }
        $finalHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
        if ($initialHash -ne $finalHash) { throw "Private raw write proof did not restore the complete image hash." }
        if ($Dm17Proof -and $script:Dm17HostInspectionFailures -ne 0) {
            throw "DM17 host-side target or neighbor inspection failed $script:Dm17HostInspectionFailures time(s)."
        }
        $resultLines = if ($Dm17Proof) {
            $writeRegistration = if ($Dm17CommonWriteProof) { "enabled-stage2-private-proof" } else { "disabled" }
            @("secondaryFinalSha256=$finalHash", "result=PASS tier=private-data-integrity-and-flush-restart", "flushStressCycles=100", "restartReadback=PASS", "hostInspectionFailures=0", "sharedWriteRegistration=$writeRegistration", "sharedFlushRegistration=disabled", "imageRestoredByteForByte=yes", "firstBootSerial=$([IO.Path]::GetFileName($firstSerialPath))", "restartSerial=$([IO.Path]::GetFileName($restartSerialPath))", "hostInspection=dm17-host-image-inspection.txt")
        } else {
            @("secondaryFinalSha256=$finalHash", "result=PASS tier=1-private-write-flush-read-restore-stress-100-cycles", "sharedWriteRegistration=disabled", "imageRestoredByteForByte=yes", "serial=private-write-boot.serial.log")
        }
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value $resultLines
    } else {
        $lifecycleMarker = if ($EnableNvmeCallbacks) {
            "[DM17-QEMU] lifecycle=PASS"
        } else { "[DM16-QEMU] lifecycle=PASS" }
        $rediscoveryMarker = if ($EnableNvmeCallbacks) {
            "[DM17-QEMU] reboot-rediscovery=PASS"
        } else { "[DM16-QEMU] reboot-rediscovery=PASS" }
        $activeBoot = Start-ProofBoot "first-boot" `
            $lifecycleMarker $EspPath $DiskPath $WorkFull 300
        $firstSerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.QmpPort $activeBoot.SerialPath
        $activeBoot = $null
        $activeBoot = Start-ProofBoot "rediscovery-boot" `
            $rediscoveryMarker $EspPath $DiskPath $WorkFull 180
        $rediscoverySerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.QmpPort $activeBoot.SerialPath
        $activeBoot = $null
        if (-not $PythonExecutable) {
            $python = Get-Command python.exe -ErrorAction SilentlyContinue
            if (-not $python) { throw "Python 3 was not found; specify -PythonExecutable." }
            $PythonExecutable = $python.Source
        }
        $inspectionPath = Join-Path $WorkFull "disk-inspection.txt"
        $inspection = & $PythonExecutable (Join-Path $Root "scripts\verify-dm9-qemu-image.py") $DiskPath 2>&1
        $inspection | Set-Content -LiteralPath $inspectionPath -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Independent raw-image verification failed; see $inspectionPath" }
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
            "secondaryFinalSha256=$((Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash)",
            "firstBootSerial=$([IO.Path]::GetFileName($firstSerial))",
            "rediscoverySerial=$([IO.Path]::GetFileName($rediscoverySerial))",
            "result=PASS tier=DM17-full-lifecycle-and-restart-rediscovery",
            "failedStage=none",
            "writesOccurred=yes",
            "inspection=PASS read-only-GPT-FAT32-independent-verifier",
            "sharedWriteRegistration=$(if ($EnableNvmeCallbacks) { 'enabled-stage2' } else { 'disabled' })",
            "sharedFlushRegistration=$(if ($EnableNvmeCallbacks) { 'enabled-stage3' } else { 'disabled' })"
        )
    }
    $proofLabel = if ($Dm17Proof -or $EnableNvmeCallbacks) { "DM17" } else { "DM16" }
    Write-Host "$proofLabel $Stage proof passed. Preserved artifacts: $WorkFull"
} catch {
    $failureText = $_.Exception.Message -replace '[\r\n]+', ' '
    if ($activeBoot) {
        try { Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.QmpPort $activeBoot.SerialPath }
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
            "secondaryFinalSha256=$finalHash", "result=FAIL", "failure=$failureText"
        )
    }
    throw
}
