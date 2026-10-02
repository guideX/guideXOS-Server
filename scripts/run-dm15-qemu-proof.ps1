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
    [UInt64]$DiskSizeBytes = 629145600,
    [ValidateRange(0, 86400)]
    [int]$FirstBootTimeoutSeconds = 0,
    [ValidateRange(0, 86400)]
    [int]$RediscoveryTimeoutSeconds = 0,
    [switch]$Dm22LargeProof,
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
                         [string]$OutputPath, [int]$TimeoutSeconds,
                         [string]$ManifestPath) {
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
    $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    $otherQemu = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" |
        Where-Object { $_.ProcessId -ne $process.Id })
    if ($ManifestPath -and (Test-Path -LiteralPath $ManifestPath)) {
        Add-Content -LiteralPath $ManifestPath -Encoding ascii -Value @(
            "qemu.$RunName.pid=$($process.Id)",
            "qemu.$RunName.commandLine=$($processInfo.CommandLine)",
            "qemu.$RunName.serial=$serialPath",
            "qemu.$RunName.monitorPort=$port",
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
            if ($serial -match '(?m)^\[(?:DM22-QEMU|DM15-QEMU|DM9-QEMU)\] (?:private-proof=FAIL|lifecycle=FAIL|proof=BLOCKED|reboot-rediscovery=FAIL|initialize=FAIL|create-partition=FAIL|format-fat32=FAIL|large-volume=FAIL|allocation-hint=FAIL)') {
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

if ($Dm22LargeProof -and $Stage -ne "Lifecycle") {
    throw "DM22 large FAT32 proof requires -Stage Lifecycle."
}
if ($Dm22LargeProof -and $DiskSizeBytes -lt [UInt64]::Parse("9663676416")) {
    throw "DM22 image must be at least 9 GiB so the GPT partition can exceed 8 GiB."
}
if (-not (Test-Path -LiteralPath $QemuExecutable)) { throw "QEMU was not found at $QemuExecutable" }
if ($AttemptNumber -lt 1) { throw "AttemptNumber must be positive." }
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
$EspFull = (Resolve-Path -LiteralPath $EspSource).Path
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $WorkDir) {
    $workLabel = if ($Dm22LargeProof) { "dm22-large-fat32" } else { "dm15-$($Stage.ToLowerInvariant())" }
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

$diskLabel = if ($Dm22LargeProof) { "secondary-large.raw" } else { "secondary-600m.raw" }
$DiskPath = Join-Path $WorkFull $diskLabel
$EspPath = Join-Path $WorkFull "esp"
$manifestName = if ($Dm22LargeProof) { "dm22-manifest.txt" } else { "dm15-manifest.txt" }
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
        $flags = if ($Stage -eq "PrivateWrite") {
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
    $diskStream.SetLength([int64]$DiskSizeBytes)
    $diskStream.Dispose()
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
    $proofName = if ($Dm22LargeProof) { "DM22-LARGE-FAT32-AHCI" } else { "DM15-AHCI-$Stage" }
    $manifestSchema = if ($Dm22LargeProof) { "DM22-LARGE-FAT32-1" } else { "DM19-TRANSPORT-1" }
    $proofIdentity = if ($Dm22LargeProof) { "GUIDEXOS-DM22-QEMU-LargeFAT32" } else { "GUIDEXOS-DM15-QEMU-$Stage" }
    @(
        "proof=$proofName",
        "manifestSchema=$manifestSchema",
        "attemptNumber=$AttemptNumber",
        "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
        "bootMedium=isolated-ESP-directory-backend",
        "machine=q35-usb-off-built-in-ICH9-AHCI",
        "cpu=QEMU-default (no -cpu argument)",
        "controller=ICH9-AHCI on Q35",
        "controllerArguments=-machine q35,usb=off (built-in ICH9 AHCI)",
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
        "secondaryInitialActualBytes=$(if ($Dm22LargeProof) { $initialActualBytes } else { 'not-recorded' })",
        "storageImageBytes=$((Get-Item -LiteralPath $DiskPath).Length)",
        "storageImageSha256Before=$initialHash",
        "storageImageAccess=writable-disposable-only",
        "storageCacheMode=QEMU default (cache option omitted from argv)",
        "uefiImagePath=$OvmfFull",
        "uefiSha256=$ovmfHash",
        "secondaryPlacement=AHCI-port1",
        "bootDevicePlacement=AHCI-port0",
        "physicalHostDisksPassedToQemu=none",
        "qemu=$qemuVersion",
        "qemuSha256=$qemuHash",
        "timeoutPrivateOrFirstBootSeconds=$(if ($Dm22LargeProof) { 1800 } elseif ($FirstBootTimeoutSeconds -gt 0) { $FirstBootTimeoutSeconds } else { 300 })",
        "timeoutRediscoveryBootSeconds=$(if ($Dm22LargeProof) { 300 } elseif ($RediscoveryTimeoutSeconds -gt 0) { $RediscoveryTimeoutSeconds } else { 180 })",
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
        $lifecycleMarker = if ($Dm22LargeProof) { "[DM22-QEMU] lifecycle=PASS" } else { "[DM15-QEMU] lifecycle=PASS" }
        $firstTimeout = if ($Dm22LargeProof) { 1800 }
            elseif ($FirstBootTimeoutSeconds -gt 0) { $FirstBootTimeoutSeconds }
            else { 300 }
        $activeBoot = Start-ProofBoot "first-boot" $lifecycleMarker $EspPath $DiskPath $WorkFull $firstTimeout $manifestPath
        $firstSerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        $rediscoveryMarker = if ($Dm22LargeProof) { "[DM22-QEMU] reboot-rediscovery=PASS" } else { "[DM15-QEMU] reboot-rediscovery=PASS" }
        $rediscoveryTimeout = if ($Dm22LargeProof) { 300 }
            elseif ($RediscoveryTimeoutSeconds -gt 0) { $RediscoveryTimeoutSeconds }
            else { 180 }
        $activeBoot = Start-ProofBoot "rediscovery-boot" $rediscoveryMarker $EspPath $DiskPath $WorkFull $rediscoveryTimeout $manifestPath
        $rediscoverySerial = $activeBoot.SerialPath
        Stop-ProofQemu $activeBoot.ProcessId $activeBoot.Port $activeBoot.SerialPath
        $activeBoot = $null
        if (-not $PythonExecutable) {
            $python = Get-Command python.exe -ErrorAction SilentlyContinue
            if (-not $python) { throw "Python 3 was not found; specify -PythonExecutable." }
            $PythonExecutable = $python.Source
        }
        $inspectionPath = Join-Path $WorkFull "disk-inspection.txt"
        $verifier = if ($Dm22LargeProof) {
            Join-Path $Root "scripts\verify-dm22-qemu-image.py"
        } else { Join-Path $Root "scripts\verify-dm9-qemu-image.py" }
        $inspection = & $PythonExecutable $verifier $DiskPath 2>&1
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
            "result=PASS tier=$(if ($Dm22LargeProof) { 'DM22-large-FAT32-AHCI-high-cluster-96KiB-file-restart' } else { '2-full-lifecycle-and-restart-rediscovery' })",
            "failedStage=none",
            "writesOccurred=yes",
            "inspection=PASS read-only-GPT-FAT32-independent-verifier",
            "transportResult=PASS"
        )
    }
    $passedProofName = if ($Dm22LargeProof) { "DM22 large FAT32 AHCI" } else { "DM15 $Stage" }
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
