<#
.SYNOPSIS
    Runs the DM15 private AHCI proof or one full AHCI storage lifecycle.

.DESCRIPTION
    Creates an isolated ESP copy and a fresh sparse 600 MiB raw secondary image, then
    boots QEMU q35 with the built-in ICH9 AHCI controller. The PrivateWrite
    stage validates and restores one zero-filled sector while shared writes
    are disabled. The Lifecycle stage runs the common GPT/FAT32/VFS workflow,
    reboots the same image, and independently verifies the result.
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
    $ownedProcess.Kill()
    if (-not $ownedProcess.WaitForExit(10000)) {
        $ownedProcess.Dispose()
        throw "The QEMU process for this proof did not stop cleanly."
    }
    $ownedProcess.Dispose()
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
        "-drive", "if=none,id=dm15boot,format=raw,file=fat:rw:$EspPath",
        "-device", "ide-hd,drive=dm15boot,bus=ide.0",
        "-drive", "if=none,id=dm15secondary,format=raw,file=$DiskPath",
        "-device", "ide-hd,drive=dm15secondary,bus=ide.1",
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
            if ($serial -match '(?m)^\[(?:DM15-QEMU|DM9-QEMU)\] (?:private-proof=FAIL|lifecycle=FAIL|proof=BLOCKED|reboot-rediscovery=FAIL|initialize=FAIL|create-partition=FAIL|format-fat32=FAIL)') {
                $failureLine = $Matches[0]
                Stop-ProofQemu $process.Id $port $serialPath
                throw "$RunName reported '$failureLine'; see $serialPath"
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
if (-not $WorkDir) { $WorkDir = "out\dm15-$($Stage.ToLowerInvariant())-$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below $repoOut" }
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files; select a fresh output directory."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }

$DiskPath = Join-Path $WorkFull "secondary-600m.raw"
$EspPath = Join-Path $WorkFull "esp"
$manifestPath = Join-Path $WorkFull "dm15-manifest.txt"
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
        $flags = if ($Stage -eq "PrivateWrite") {
            "-DGXOS_DM15_QEMU_AHCI_PROOF -DGXOS_DM15_AHCI_PRIVATE_PROOF"
        } else { "-DGXOS_DM15_QEMU_AHCI_PROOF" }
        $kernelBuildLog = Join-Path $WorkFull "kernel-build-$($Stage.ToLowerInvariant()).log"
        & $makePath -C (Join-Path $Root "kernel") ARCH=amd64 "EXTRA_CFLAGS=$flags" -j4 `
            2>&1 | Out-File -LiteralPath $kernelBuildLog -Encoding utf8
        $kernelBuildExitCode = $LASTEXITCODE
        if ($kernelBuildExitCode -ne 0) {
            Get-Content -LiteralPath $kernelBuildLog -Tail 80
            throw "DM15 $Stage kernel build failed; see $kernelBuildLog."
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
    $kernelSource = Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
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
        "proof=DM15-AHCI-$Stage",
        "attemptNumber=$AttemptNumber",
        "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
        "bootMedium=isolated-ESP-directory-backend",
        "machine=q35-usb-off-built-in-ICH9-AHCI",
        "bootloaderSha256=$bootHash",
        "kernelSha256=$kernelHash",
        "secondaryImage=$DiskPath",
        "secondaryFormat=raw",
        "secondaryCapacityBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "secondaryInitialSha256=$initialHash",
        "secondaryPlacement=AHCI-port1",
        "bootDevicePlacement=AHCI-port0",
        "physicalHostDisksPassedToQemu=none",
        "qemu=$((& $QemuFull --version | Select-Object -First 1))"
    ) | Set-Content -LiteralPath $manifestPath -Encoding ascii
    @("identity=GUIDEXOS-DM15-QEMU-$Stage", "proofManifest=/dm15-manifest.txt") |
        Set-Content -LiteralPath (Join-Path $EspPath "build-identity.txt") -Encoding ascii

    if ($Stage -eq "PrivateWrite") {
        $activeBoot = Start-ProofBoot "private-write-boot" `
            "[DM15-QEMU] private-proof=PASS" $EspPath $DiskPath $WorkFull 300
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        $finalHash = (Get-FileHash -LiteralPath $DiskPath -Algorithm SHA256).Hash
        if ($initialHash -ne $finalHash) { throw "Private raw write proof did not restore the complete image hash." }
        Add-Content -LiteralPath $manifestPath -Encoding ascii -Value @(
            "secondaryFinalSha256=$finalHash",
            "result=PASS tier=1-private-write-readback-flush-restore",
            "sharedWriteRegistration=disabled",
            "imageRestoredByteForByte=yes",
            "serial=private-write-boot.serial.log"
        )
    } else {
        $activeBoot = Start-ProofBoot "first-boot" `
            "[DM15-QEMU] lifecycle=PASS" $EspPath $DiskPath $WorkFull 300
        $firstSerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        $activeBoot = Start-ProofBoot "rediscovery-boot" `
            "[DM15-QEMU] reboot-rediscovery=PASS" $EspPath $DiskPath $WorkFull 180
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
    Write-Host "DM15 $Stage proof passed. Preserved artifacts: $WorkFull"
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
