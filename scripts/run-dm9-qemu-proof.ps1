<#
.SYNOPSIS
    Runs the compile-time DM10 storage lifecycle proof on an isolated QEMU disk.

.DESCRIPTION
    Copies the selected ESP directory, builds/stages guideXOS, creates a new
    600 MiB raw secondary disk, runs the first-boot storage lifecycle, boots
    the same image again for rediscovery/remount, and independently inspects
    the resulting GPT/FAT32 structures. Only the copied ESP directory and the
    newly created raw file are attached to QEMU; no host physical disk is used.
    Artifacts are preserved under out/dm10-qemu-proof-<timestamp> by default.
#>
[CmdletBinding()]
param(
    [string]$EspSource = "ESP",
    [string]$WorkDir = "",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
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
    throw "MSBuild was not found. Install Visual Studio Build Tools or provide its MSBuild.exe on PATH."
}

function Get-FreeLoopbackPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $listener.Stop()
    return $port
}

function Stop-ProofQemu([System.Diagnostics.Process]$Process, [int]$Port,
                        [string]$ExpectedSerialPath) {
    if (-not $Process) { return }

    $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
    if (-not $current) { return }
    if ($current.Name -ne "qemu-system-x86_64.exe" -or
        $current.CommandLine -notlike "*$ExpectedSerialPath*") {
        throw "Refusing to stop a QEMU process whose PID/serial path no longer matches this proof run."
    }

    if (-not $Process.HasExited) {
        try {
            $client = [System.Net.Sockets.TcpClient]::new()
            $client.Connect("127.0.0.1", $Port)
            $stream = $client.GetStream()
            $buffer = [byte[]]::new(4096)
            Start-Sleep -Milliseconds 100
            if ($stream.DataAvailable) { [void]$stream.Read($buffer, 0, $buffer.Length) }
            $quit = [System.Text.Encoding]::ASCII.GetBytes("quit`n")
            $stream.Write($quit, 0, $quit.Length)
            $stream.Dispose()
            $client.Dispose()
        } catch {
            # The exact process and its serial path were checked above. If its
            # monitor is unavailable, stop only the process created by this script.
        }
    }

    try { [void]$Process.WaitForExit(5000) } catch { }
    $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
    if ($current -and $current.Name -eq "qemu-system-x86_64.exe" -and
        $current.CommandLine -like "*$ExpectedSerialPath*") {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        try { [void]$Process.WaitForExit(10000) } catch { }
    }
    $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
    if ($current -and $current.Name -eq "qemu-system-x86_64.exe" -and
        $current.CommandLine -like "*$ExpectedSerialPath*") {
        throw "The QEMU process for this proof run did not stop cleanly."
    }
}

function Start-ProofBoot([string]$RunName, [string]$SuccessMarker,
                         [string]$EspPath, [string]$DiskPath,
                         [string]$OutputPath, [int]$TimeoutSeconds,
                         [string]$ManifestPath, [int]$ProofAttempt,
                         [switch]$EnableQemuDebug) {
    for ($attempt = 1; $attempt -le 5; ++$attempt) {
        $serialPath = Join-Path $OutputPath "$RunName.serial.log"
        $stderrPath = Join-Path $OutputPath "$RunName.stderr.log"
        $stdoutPath = Join-Path $OutputPath "$RunName.stdout.log"
        Remove-Item -LiteralPath $serialPath,$stderrPath,$stdoutPath -Force -ErrorAction SilentlyContinue
        $port = Get-FreeLoopbackPort
        $arguments = @(
            "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfFull",
            "-machine", "pc,usb=off",
            "-drive", "file=fat:rw:$EspPath,format=raw",
            "-drive", "file=$DiskPath,format=raw,if=ide,index=1",
            "-netdev", "user,id=net0",
            "-device", "e1000,netdev=net0",
            "-object", "rng-builtin,id=rng0",
            "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
            "-m", "1024M", "-vga", "std", "-display", "none",
            "-serial", "file:$serialPath",
            "-monitor", "tcp:127.0.0.1:$port,server,nowait",
            "-rtc", "base=utc,clock=host", "-no-reboot"
        )
        $debugPath = Join-Path $OutputPath "$RunName.qemu-debug.log"
        if ($EnableQemuDebug) {
            $arguments += @("-d", "guest_errors,int,cpu_reset", "-D", $debugPath)
        }
        $process = Start-Process -FilePath $QemuFull -ArgumentList $arguments `
            -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
        $otherQemu = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" |
            Where-Object { $_.ProcessId -ne $process.Id })
        if ($processInfo -and $ManifestPath -and
            (Test-Path -LiteralPath $ManifestPath)) {
            Add-Content -LiteralPath $ManifestPath -Encoding ascii -Value @(
                "qemuAttempt=$ProofAttempt boot=$RunName launchTry=$attempt pid=$($process.Id)",
                "qemuCommandLine.$RunName.$attempt=$($processInfo.CommandLine)",
                "qemu.$RunName.$attempt.otherProcessesAtStart=$($otherQemu.Count)",
                "qemu.$RunName.$attempt.otherPidsAtStart=$(($otherQemu | ForEach-Object { $_.ProcessId }) -join ',')",
                "qemuSerial.$RunName.$attempt=$serialPath",
                "qemuDebug.$RunName.$attempt=$debugPath"
            )
        }
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $bootSucceeded = $false
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $serialPath) {
                $serial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
                if ($serial -and $serial.Contains("[KERNEL-FAULT]")) {
                    Stop-ProofQemu $process $port $serialPath
                    throw "$RunName encountered a kernel fault; see $serialPath"
                }
                if ($serial -match '(?m)^\[DM9-QEMU\] (?:initialize=FAIL|create-partition=FAIL|format-fat32=FAIL|lifecycle=FAIL|proof=BLOCKED|reboot-rediscovery=FAIL)') {
                    $failureLine = $Matches[0]
                    Stop-ProofQemu $process $port $serialPath
                    throw "$RunName reported '$failureLine'; see $serialPath"
                }
                if ($serial -and $serial.Contains($SuccessMarker) -and
                    $serial.Contains("[KERNEL] Entering main loop")) {
                    $bootSucceeded = $true
                    break
                }
            }
            if ($process.HasExited) { break }
            Start-Sleep -Milliseconds 500
        }

        if ($bootSucceeded) {
            return [pscustomobject]@{
                Process = $process
                Port = $port
                SerialPath = $serialPath
                Attempt = $attempt
                CommandLine = if ($processInfo) { $processInfo.CommandLine } else { "unavailable" }
            }
        }

        $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
        $serial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
        if (-not $process.HasExited) { Stop-ProofQemu $process $port $serialPath }
        if ($stderr -match "process cannot access|used by another process" -and $attempt -lt 5) {
            Start-Sleep -Seconds 3
            continue
        }
        if ([DateTime]::UtcNow -ge $deadline) {
            throw "$RunName timed out waiting for '$SuccessMarker'; see $serialPath"
        }
        throw "$RunName exited before '$SuccessMarker'. See $serialPath and $stderrPath."
    }
    throw "$RunName could not open its disposable disk after five attempts."
}

    if (-not (Test-Path -LiteralPath $QemuExecutable)) { throw "QEMU was not found at $QemuExecutable" }
if ($AttemptNumber -lt 1) { throw "AttemptNumber must be positive." }
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
$EspFull = (Resolve-Path -LiteralPath $EspSource).Path
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $WorkDir) { $WorkDir = "out\dm10-qemu-proof-$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "WorkDir must be a new directory below $repoOut"
}
if ($WorkFull.StartsWith($EspFull + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    $EspFull.StartsWith($WorkFull + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "WorkDir and EspSource must not contain one another."
}
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files. Choose a new output directory to preserve prior evidence."
    }
} else {
    New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null
}

$DiskPath = Join-Path $WorkFull "secondary-600m.raw"
$EspPath = Join-Path $WorkFull "esp"
$manifestPath = Join-Path $WorkFull "dm10-manifest.txt"
$inspectionPath = Join-Path $WorkFull "disk-inspection.txt"
$activeBoot = $null

try {
    if (-not $SkipBuild) {
        $make = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue
        if (-not $make) { $make = Get-Command make.exe -ErrorAction SilentlyContinue }
        if (-not $make -and (Test-Path -LiteralPath "C:\mingw64\bin\mingw32-make.exe")) {
            $makePath = "C:\mingw64\bin\mingw32-make.exe"
        } elseif ($make) { $makePath = $make.Source }
        else { throw "MinGW make was not found." }

        $mainObject = Join-Path $Root "kernel\build\amd64\obj\core\main.o"
        if (Test-Path -LiteralPath $mainObject) { Remove-Item -LiteralPath $mainObject -Force }
        $proofObject = Join-Path $Root "kernel\build\amd64\obj\core\qemu_dm9_storage_proof.o"
        if (Test-Path -LiteralPath $proofObject) { Remove-Item -LiteralPath $proofObject -Force }
        & $makePath -C (Join-Path $Root "kernel") ARCH=amd64 `
            "EXTRA_CFLAGS=-DGXOS_DM9_QEMU_STORAGE_PROOF" -j4
        if ($LASTEXITCODE -ne 0) { throw "DM9 proof kernel build failed." }

        $msbuild = Find-MSBuild
        & $msbuild (Join-Path $Root "guideXOSBootLoader\guideXOSBootLoader.vcxproj") `
            /t:Build /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal
        if ($LASTEXITCODE -ne 0) { throw "UEFI bootloader build failed." }
    }

    if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) {
        throw "EspSource must contain the guideXOS runtime files, including ramdisk.img."
    }
    $bootloaderSource = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
    $kernelSource = Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
    if (-not (Test-Path -LiteralPath $bootloaderSource) -or
        -not (Test-Path -LiteralPath $kernelSource)) {
        throw "Built UEFI bootloader or amd64 kernel is missing. Run without -SkipBuild."
    }

    New-Item -ItemType Directory -Path $EspPath -Force | Out-Null
    Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $EspPath -Recurse -Force
    $bootPath = Join-Path $EspPath "EFI\BOOT"
    New-Item -ItemType Directory -Path $bootPath -Force | Out-Null
    Copy-Item -LiteralPath $bootloaderSource -Destination (Join-Path $bootPath "BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $kernelSource -Destination (Join-Path $EspPath "kernel.elf") -Force

    if (Test-Path -LiteralPath $DiskPath) { throw "Refusing to overwrite an existing proof disk: $DiskPath" }
    $diskStream = [IO.File]::Open($DiskPath, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    $diskStream.SetLength(600L * 1024L * 1024L)
    $diskStream.Dispose()

    $bootHash = (Get-FileHash -LiteralPath (Join-Path $bootPath "BOOTX64.EFI") -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath (Join-Path $EspPath "kernel.elf") -Algorithm SHA256).Hash
    $kernelBytes = (Get-Item -LiteralPath (Join-Path $EspPath "kernel.elf")).Length
    $initialHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
    $qemuVersion = (& $QemuFull --version | Select-Object -First 1)
    $qemuHash = (Get-FileHash -LiteralPath $QemuFull -Algorithm SHA256).Hash
    $ovmfHash = (Get-FileHash -LiteralPath $OvmfFull -Algorithm SHA256).Hash
    $qemuAtStart = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'")
    @(
        "proof=DM10-QEMU-SECONDARY-DISK",
        "manifestSchema=DM19-TRANSPORT-1",
        "attemptNumber=$AttemptNumber",
        "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
        "bootMedium=isolated-ESP-directory-backend",
        "bootloaderSha256=$bootHash",
        "kernelSha256=$kernelHash",
        "secondaryImage=$DiskPath",
        "storageImagePath=$DiskPath",
        "secondaryFormat=raw",
        "secondaryCapacityBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "secondaryInitialSha256=$initialHash",
        "storageImageBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "storageImageSha256Before=$initialHash",
        "storageImageAccess=writable-disposable-only",
        "storageCacheMode=QEMU default (cache option omitted from argv)",
        "secondaryPlacement=IDE-channel0-target1-primary-slave",
        "bootDevicePlacement=IDE-channel0-target0-primary-master",
        "secondarySelection=ATA-channel-target-and-QEMU-model-plus-DefinitelyNotBoot; global-index-scanned",
        "machine=pc,usb=off",
        "cpu=QEMU-default (no -cpu argument)",
        "controller=QEMU legacy IDE on machine pc",
        "controllerArguments=-drive file=$DiskPath,format=raw,if=ide,index=1",
        "uefiImagePath=$OvmfFull",
        "uefiSha256=$ovmfHash",
        "physicalHostDisksPassedToQemu=none",
        "kernelBytes=$kernelBytes",
        "kernelSha256=$kernelHash",
        "bootloaderSha256=$bootHash",
        "qemu=$qemuVersion",
        "qemuSha256=$qemuHash",
        "timeoutFirstBootSeconds=300",
        "timeoutRediscoveryBootSeconds=180",
        "timeoutLaunchRetries=5",
        "hostQemuProcessesBefore=$($qemuAtStart.Count)",
        "hostQemuPidsBefore=$(($qemuAtStart | ForEach-Object { $_.ProcessId }) -join ',')"
    ) | Set-Content -LiteralPath $manifestPath -Encoding ascii
    @(
        "identity=GUIDEXOS-DM10-QEMU-PROOF-V1",
        "bootloaderSha256=$bootHash",
        "kernelSha256=$kernelHash",
        "proofManifest=/dm10-manifest.txt"
    ) | Set-Content -LiteralPath (Join-Path $EspPath "build-identity.txt") -Encoding ascii

    $activeBoot = Start-ProofBoot "first-boot" "[DM9-QEMU] lifecycle=PASS" `
        $EspPath $DiskPath $WorkFull 300 $manifestPath $AttemptNumber `
        -EnableQemuDebug:$QemuDebug
    $firstBootSerialPath = $activeBoot.SerialPath
    Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
        "firstBootCommandLine=$($activeBoot.CommandLine)",
        "firstBootSerial=$firstBootSerialPath"
    )
    Stop-ProofQemu $activeBoot.Process $activeBoot.Port $activeBoot.SerialPath
    $activeBoot = $null

    $activeBoot = Start-ProofBoot "rediscovery-boot" `
        "[DM9-QEMU] reboot-rediscovery=PASS" $EspPath $DiskPath $WorkFull 180 `
        $manifestPath $AttemptNumber -EnableQemuDebug:$QemuDebug
    $rediscoverySerialPath = $activeBoot.SerialPath
    Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
        "rediscoveryCommandLine=$($activeBoot.CommandLine)",
        "rediscoverySerial=$rediscoverySerialPath"
    )
    Stop-ProofQemu $activeBoot.Process $activeBoot.Port $activeBoot.SerialPath
    $activeBoot = $null

    if (-not $PythonExecutable) {
        $python = Get-Command python.exe -ErrorAction SilentlyContinue
        if (-not $python) { throw "Python 3 was not found; specify -PythonExecutable." }
        $PythonExecutable = $python.Source
    }
    $verifier = Join-Path $Root "scripts\verify-dm9-qemu-image.py"
    $inspection = & $PythonExecutable $verifier $DiskPath 2>&1
    $inspection | Set-Content -LiteralPath $inspectionPath -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Independent raw-image verification failed; see $inspectionPath" }
    Add-Content -LiteralPath $manifestPath -Value @(
        "secondaryFinalSha256=$((Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash)",
        "firstBootSerial=$([IO.Path]::GetFileName($firstBootSerialPath))",
        "rediscoverySerial=$([IO.Path]::GetFileName($rediscoverySerialPath))",
        "result=PASS tier=2 full-lifecycle-and-restart-rediscovery",
        "failedStage=none",
        "writesOccurred=yes",
        "inspection=PASS read-only-GPT-FAT32-independent-verifier",
        "transportResult=PASS"
    ) -Encoding ascii
    Write-Host "DM9 QEMU proof passed. Preserved artifacts: $WorkFull"
    Write-Host "Secondary raw image: $DiskPath"
    Write-Host "Inspection report: $inspectionPath"
} catch {
    $failureText = $_.Exception.Message -replace '[\r\n]+', ' '
    if ($activeBoot) {
        Stop-ProofQemu $activeBoot.Process $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
    }
    $initialImageHash = "unavailable"
    $finalImageHash = "unavailable"
    if (Test-Path -LiteralPath $manifestPath) {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw
        if ($manifest -match '(?m)^secondaryInitialSha256=([A-Fa-f0-9]{64})\r?$') {
            $initialImageHash = $Matches[1].ToUpperInvariant()
        }
    }
    if (Test-Path -LiteralPath $DiskPath) {
        $finalImageHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
    }
    $imageUnchanged = $initialImageHash -ne "unavailable" -and
        $initialImageHash -eq $finalImageHash
    $serialLogs = Get-ChildItem -LiteralPath $WorkFull -Filter "*.serial.log" -File `
        -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }
    $failedStage = "unknown"
    $writesOccurred = -not $imageUnchanged
    foreach ($serialLog in $serialLogs) {
        $serialText = Get-Content -LiteralPath (Join-Path $WorkFull $serialLog) `
            -Raw -ErrorAction SilentlyContinue
        if ($serialText -match 'firstFailedStage=([^\s]+)') { $failedStage = $Matches[1] }
        if ($serialText -match '(?:writesCompleted|sectorsWritten)=([0-9A-Fa-f]+)') {
            $writesOccurred = $writesOccurred -or ([Convert]::ToUInt64($Matches[1], 16) -gt 0)
        }
        if ($serialText -match 'writeMayHaveReachedMedia=(yes|no)') {
            if ($Matches[1] -eq 'yes') { $writesOccurred = $true }
        }
    }
    if (Test-Path -LiteralPath $manifestPath) {
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
            "secondaryFinalSha256=$finalImageHash",
            "transportResult=FAIL",
            "result=FAIL",
            "failedStage=$failedStage",
            "writesOccurred=$($writesOccurred.ToString().ToLowerInvariant())",
            "writeMayHaveReachedMedia=$($writesOccurred.ToString().ToLowerInvariant())",
            "failure=$failureText"
        )
    }
    @(
        "result=FAIL",
        "detail=$failureText",
        "initialImageSha256=$initialImageHash",
        "finalImageSha256=$finalImageHash",
        "imageUnchanged=$($imageUnchanged.ToString().ToLowerInvariant())",
        "serialLogs=$($serialLogs -join ',')",
        "failedStage=$failedStage",
        "writesOccurred=$($writesOccurred.ToString().ToLowerInvariant())",
        "writeMayHaveReachedMedia=$($writesOccurred.ToString().ToLowerInvariant())",
        "hostPhysicalDiskAttached=no"
    ) | Set-Content -LiteralPath (Join-Path $WorkFull "run-result.txt") -Encoding ascii
    throw
} finally {
    if ($activeBoot) {
        Stop-ProofQemu $activeBoot.Process $activeBoot.Port $activeBoot.SerialPath
    }
}
