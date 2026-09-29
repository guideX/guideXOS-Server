<#
.SYNOPSIS
    Runs the DM16 private NVMe proof or one full NVMe storage lifecycle.

.DESCRIPTION
    Creates an isolated ESP copy and a fresh sparse 600 MiB raw secondary image, then
    Boots from a separate IDE-backed ESP and attaches a fresh raw image to a
    QEMU NVMe controller. PrivateWrite keeps shared writes disabled and runs
    100 write/flush/read/restore stress cycles before Lifecycle enables the
    common GPT/FAT32/VFS workflow and restart verification.
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
    [string]$DiskDirectory = "",
    [int]$AttemptNumber = 1,
    [switch]$QemuDebug,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
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

function Stop-ProofQemu([int]$ProcessId, [int]$Port,
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
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect([System.Net.IPAddress]::Loopback, $Port)
        $stream = $client.GetStream()
        $command = [Text.Encoding]::ASCII.GetBytes("quit`r`n")
        $stream.Write($command, 0, $command.Length)
        $stream.Flush()
    } finally {
        $client.Dispose()
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
    $arguments = @(
        "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfFull",
        "-machine", "q35,usb=off",
        "-drive", "if=none,id=dm16boot,format=raw,file=fat:rw:$EspPath",
        "-device", "ide-hd,drive=dm16boot,bus=ide.0",
        "-drive", "if=none,id=dm16secondary,format=raw,cache=directsync,file=$DiskPath",
        "-device", "nvme,id=dm16nvme,serial=GXOSDM16NVME,drive=dm16secondary",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-object", "rng-builtin,id=rng0",
        "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", "file:$serialPath",
        "-monitor", "tcp:127.0.0.1:$port,server,nowait",
        "-rtc", "base=utc,clock=host", "-no-reboot"
    )
    if ($QemuDebug) { $arguments += @("-d", "guest_errors,int,cpu_reset", "-D", $debugPath) }
    $process = Start-Process -FilePath $QemuFull -ArgumentList $arguments `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serialPath) {
            $serial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
            if ($serial -match '\[KERNEL-FAULT\]') {
                Stop-ProofQemu $process.Id $port $serialPath
                throw "$RunName encountered a kernel fault; see $serialPath"
            }
            if ($serial -match '(?m)^\[(?:DM16-QEMU|DM9-QEMU)\] (?:private-proof=FAIL|lifecycle=FAIL|proof=BLOCKED|reboot-rediscovery=FAIL|initialize=FAIL|create-partition=FAIL|format-fat32=FAIL)') {
                $failureLine = $Matches[0]
                Stop-ProofQemu $process.Id $port $serialPath
                throw "$RunName reported '$failureLine'; see $serialPath"
            }
            if ($serial -and $serial.Contains("[DM16-NVME-DMA] submit-pause=START") -and
                -not (Test-Path -LiteralPath (Join-Path $OutputPath "$RunName.dma-snapshot.txt"))) {
                try { Save-ProofDmaSnapshot $port $serial $OutputPath $RunName }
                catch {
                    Stop-ProofQemu $process.Id $port $serialPath
                    throw
                }
            }
            if ($serial -and $serial.Contains($SuccessMarker) -and
                $serial.Contains("[KERNEL] Entering main loop")) {
                return [pscustomobject]@{ ProcessId=$process.Id; Port=$port; SerialPath=$serialPath }
            }
        }
        if ($process.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
    if (-not $process.HasExited) { Stop-ProofQemu $process.Id $port $serialPath }
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

$DiskRoot = if ($DiskDirectory) {
    if ([IO.Path]::IsPathRooted($DiskDirectory)) {
        [IO.Path]::GetFullPath($DiskDirectory)
    } else { [IO.Path]::GetFullPath((Join-Path $Root $DiskDirectory)) }
} else { $WorkFull }
if (-not (Test-Path -LiteralPath $DiskRoot)) {
    New-Item -ItemType Directory -Path $DiskRoot -Force | Out-Null
}
$DiskPath = Join-Path $DiskRoot ("dm16-secondary-{0}-{1:D2}.raw" -f $Stage.ToLowerInvariant(), $AttemptNumber)
$EspPath = Join-Path $WorkFull "esp"
$manifestPath = Join-Path $WorkFull "dm16-manifest.txt"
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
        $flags = if ($Stage -eq "PrivateWrite") {
            "-DGXOS_DM16_QEMU_NVME_PROOF -DGXOS_DM16_NVME_PRIVATE_PROOF"
        } else { "-DGXOS_DM16_QEMU_NVME_PROOF" }
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
    & $sparseTool sparse setflag $DiskPath 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Unable to mark the new raw proof image sparse (fsutil exit $LASTEXITCODE)." }
    $diskStream = [IO.File]::Open($DiskPath, [IO.FileMode]::Open,
        [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    $diskStream.SetLength(600L * 1024L * 1024L)
    $diskStream.Dispose()
    $bootHash = (Get-FileHash -LiteralPath (Join-Path $bootPath "BOOTX64.EFI") -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath (Join-Path $EspPath "kernel.elf") -Algorithm SHA256).Hash
    $initialHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
    @(
        "proof=DM16-NVMe-$Stage",
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
        "secondaryCacheMode=directsync",
        "secondaryCapacityBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "secondaryInitialSha256=$initialHash",
        "secondaryPlacement=QEMU-NVMe-controller-namespace-1",
        "physicalHostDisksPassedToQemu=none",
        "qemu=$((& $QemuFull --version | Select-Object -First 1))"
    ) | Set-Content -LiteralPath $manifestPath -Encoding ascii
    @("identity=GUIDEXOS-DM16-QEMU-$Stage", "proofManifest=/dm16-manifest.txt") |
        Set-Content -LiteralPath (Join-Path $EspPath "build-identity.txt") -Encoding ascii

    if ($Stage -eq "PrivateWrite") {
        $activeBoot = Start-ProofBoot "private-write-boot" `
            "[DM16-QEMU] private-proof=PASS" $EspPath $DiskPath $WorkFull 300
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        $finalHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
        if ($initialHash -ne $finalHash) { throw "Private raw write proof did not restore the complete image hash." }
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
            "secondaryFinalSha256=$finalHash",
            "result=PASS tier=1-private-write-flush-read-restore-stress-100-cycles",
            "sharedWriteRegistration=disabled",
            "imageRestoredByteForByte=yes",
            "serial=private-write-boot.serial.log"
        )
    } else {
        $activeBoot = Start-ProofBoot "first-boot" `
            "[DM16-QEMU] lifecycle=PASS" $EspPath $DiskPath $WorkFull 300
        $firstSerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        $activeBoot = Start-ProofBoot "rediscovery-boot" `
            "[DM16-QEMU] reboot-rediscovery=PASS" $EspPath $DiskPath $WorkFull 180
        $rediscoverySerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
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
            "result=PASS tier=2-full-lifecycle-and-restart-rediscovery",
            "failedStage=none",
            "writesOccurred=yes",
            "inspection=PASS read-only-GPT-FAT32-independent-verifier"
        )
    }
    Write-Host "DM16 $Stage proof passed. Preserved artifacts: $WorkFull"
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
            "secondaryFinalSha256=$finalHash", "result=FAIL", "failure=$failureText"
        )
    }
    throw
}
