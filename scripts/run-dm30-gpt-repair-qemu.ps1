[CmdletBinding()]
param(
    [ValidateSet("AHCI512", "USB4Kn")]
    [string]$Transport = "AHCI512",
    [string]$EspSource = "ESP",
    [string]$WorkDir = "",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
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
        $install = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $install) {
            $candidate = Join-Path $install "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    throw "MSBuild was not found."
}

function Get-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $listener.Stop()
    return $port
}

function Invoke-PythonJson([string[]]$Arguments, [string]$OutputPath,
                           [int[]]$ExpectedExitCodes = @(0)) {
    $output = & $PythonFull @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $text = $output -join [Environment]::NewLine
    $text | Set-Content -LiteralPath $OutputPath -Encoding utf8
    if ($ExpectedExitCodes -notcontains $exitCode) {
        throw "Python command exited $exitCode; see $OutputPath"
    }
    try { return ($text | ConvertFrom-Json) }
    catch { throw "Could not parse JSON from $OutputPath" }
}

function Copy-RawSparse([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) {
        throw "Refusing to overwrite existing image evidence: $Destination"
    }
    $qemuImg = Join-Path (Split-Path -Parent $QemuFull) "qemu-img.exe"
    if (-not (Test-Path -LiteralPath $qemuImg)) {
        throw "qemu-img.exe was not found beside QEMU."
    }
    & $qemuImg convert -f raw -O raw -S 4k $Source $Destination 2>&1 |
        Out-File -LiteralPath (Join-Path $WorkFull "qemu-img-copy.log") -Append -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "qemu-img failed to checkpoint $Source" }
}

function Wait-SerialMarker([System.Diagnostics.Process]$Process,
                           [string]$SerialPath, [string]$Marker,
                           [int]$TimeoutSeconds) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        if (Test-Path -LiteralPath $SerialPath) {
            $text = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
            if ($text -and $text.Contains($Marker)) { return $text }
            if ($text -match '(?m)^\[DM29-QEMU\] (?:repair|restart|multi-partition)=FAIL' -or
                $text -match '(?m)^\[DM29-QEMU\] canary-(?:read|signature)=FAIL') {
                Get-Content -LiteralPath $SerialPath -Tail 100
                throw "The guest emitted a DM30 proof failure before $Marker"
            }
        }
        if ($Process.HasExited) {
            if (Test-Path -LiteralPath $SerialPath) {
                Get-Content -LiteralPath $SerialPath -Tail 100
            }
            throw "QEMU exited before serial marker: $Marker"
        }
        Start-Sleep -Milliseconds 250
    }
    if (Test-Path -LiteralPath $SerialPath) {
        Get-Content -LiteralPath $SerialPath -Tail 100
    }
    throw "Timed out waiting for serial marker: $Marker"
}

function Stop-ProofVm($Vm) {
    if (-not $Vm -or $Vm.Process.HasExited) { return }
    $monitor = [System.Net.Sockets.TcpClient]::new()
    try {
        $monitor.Connect("127.0.0.1", $Vm.MonitorPort)
        $stream = $monitor.GetStream()
        $quit = [Text.Encoding]::ASCII.GetBytes("quit`n")
        $stream.Write($quit, 0, $quit.Length)
        $stream.Flush()
    } finally {
        $monitor.Dispose()
    }
    if (-not $Vm.Process.WaitForExit(10000)) {
        $Vm.Process.Refresh()
        if (-not $Vm.Process.HasExited) { $Vm.Process.Kill() }
    }
    if (-not $Vm.Process.WaitForExit(10000)) {
        throw "The proof QEMU process did not stop."
    }
}

$script:Qmp = $null
$script:QmpId = 0
function Send-Qmp([string]$Execute) {
    $script:QmpId++
    $id = "dm30-$($script:QmpId)"
    $command = [ordered]@{ execute = $Execute; id = $id } |
        ConvertTo-Json -Compress
    Add-Content -LiteralPath $script:Qmp.LogPath -Encoding utf8 -Value $command
    $script:Qmp.Writer.WriteLine($command)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        $line = $script:Qmp.Reader.ReadLine()
        if ($null -eq $line) { throw "QMP connection closed during $Execute." }
        Add-Content -LiteralPath $script:Qmp.LogPath -Encoding utf8 -Value $line
        $message = $line | ConvertFrom-Json
        if ($message.event) { continue }
        if ($message.id -eq $id) {
            if ($message.error) {
                throw "QMP $Execute failed: $($message.error | ConvertTo-Json -Compress)"
            }
            return $message.return
        }
    }
    throw "Timed out waiting for QMP command $Execute."
}

function Connect-Qmp($Vm, [string]$LogPath) {
    $client = [Net.Sockets.TcpClient]::new()
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $client.Connect("127.0.0.1", $Vm.QmpPort); break }
        catch {
            if ($Vm.Process.HasExited) { throw "QEMU exited before QMP connected." }
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $client.Connected) { throw "QMP did not listen before the bounded deadline." }
    $stream = $client.GetStream()
    $stream.ReadTimeout = 20000
    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
    $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 4096, $true)
    $writer.AutoFlush = $true
    $script:Qmp = [pscustomobject]@{
        Client = $client; Reader = $reader; Writer = $writer; LogPath = $LogPath
    }
    $greeting = $reader.ReadLine()
    Add-Content -LiteralPath $LogPath -Encoding utf8 -Value $greeting
    if (-not $greeting -or -not ($greeting | ConvertFrom-Json).QMP) {
        throw "Invalid QMP greeting."
    }
    [void](Send-Qmp "qmp_capabilities")
}

function Close-Qmp {
    if ($script:Qmp) {
        $script:Qmp.Client.Dispose()
        $script:Qmp = $null
    }
}

function Start-ProofVm([string]$Label, [string]$ImagePath) {
    $serialPath = Join-Path $WorkFull "$Label.serial.log"
    $stderrPath = Join-Path $WorkFull "$Label.stderr.log"
    $stdoutPath = Join-Path $WorkFull "$Label.stdout.log"
    $monitorPort = Get-FreePort
    do { $qmpPort = Get-FreePort } while ($qmpPort -eq $monitorPort)
    $qemuArgs = @(
        "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfFull",
        "-machine", $(if ($Transport -eq "USB4Kn") { "pc,usb=off" } else { "q35,usb=off" }),
        "-drive", "if=none,id=dm30boot,format=raw,file=fat:rw:$EspPath",
        "-device", "ide-hd,drive=dm30boot,bus=ide.0",
        "-object", "rng-builtin,id=rng0",
        "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", "file:$serialPath",
        "-monitor", "tcp:127.0.0.1:$monitorPort,server,nowait",
        "-qmp", "tcp:127.0.0.1:$qmpPort,server,nowait",
        "-rtc", "base=utc,clock=host", "-no-reboot"
    )
    if ($Transport -eq "USB4Kn") {
        $qemuArgs += @(
            "-device", "piix3-usb-uhci,id=uhci",
            "-drive", "if=none,id=dm24secondary,format=raw,file=$ImagePath",
            "-device", "usb-storage,id=dm24disk,bus=uhci.0,drive=dm24secondary,removable=on,serial=DM24USB01,logical_block_size=4096,physical_block_size=4096,discard_granularity=4096"
        )
    } else {
        $qemuArgs += @(
            "-drive", "if=none,id=dm15secondary,format=raw,file=$ImagePath",
            "-device", "ide-hd,drive=dm15secondary,bus=ide.1"
        )
    }
    $process = Start-Process -FilePath $QemuFull -ArgumentList $qemuArgs `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $commandLine = (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)").CommandLine
    $commandLine | Set-Content -LiteralPath (Join-Path $WorkFull "$Label.command-line.txt") -Encoding utf8
    return [pscustomobject]@{
        Process = $process
        SerialPath = $serialPath
        MonitorPort = $monitorPort
        QmpPort = $qmpPort
        CommandLine = $commandLine
    }
}

$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $WorkDir) {
    $WorkDir = "out\dm30-qemu-$($Transport.ToLowerInvariant())-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "WorkDir must be below the repository out directory."
}
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files; choose a new evidence directory."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }

if (-not (Test-Path -LiteralPath $QemuExecutable)) { throw "QEMU was not found: $QemuExecutable" }
if (-not (Test-Path -LiteralPath $OvmfCode)) { throw "OVMF code image was not found: $OvmfCode" }
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
$EspFull = (Resolve-Path -LiteralPath $EspSource).Path
if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) {
    throw "EspSource must contain guideXOS runtime files, including ramdisk.img."
}
if ($PythonExecutable) {
    $PythonFull = (Resolve-Path -LiteralPath $PythonExecutable).Path
} else {
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }
    if (-not $python) { throw "Python 3 was not found. Use -PythonExecutable." }
    $PythonFull = $python.Source
}

$EspPath = Join-Path $WorkFull "esp"
New-Item -ItemType Directory -Path $EspPath -Force | Out-Null
Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $EspPath -Recurse -Force
$bootloader = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$kernel = if ($Transport -eq "USB4Kn") {
    Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
} else { Join-Path $Root "kernel\build\amd64\bin\kernel.elf" }

if (-not $SkipBuild) {
    $make = Get-Command mingw32-make.exe -ErrorAction Stop
    $objectDir = Join-Path $Root "kernel\build\amd64\obj\core"
    foreach ($object in @("main.o", "qemu_dm9_storage_proof.o")) {
        $path = Join-Path $objectDir $object
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    $flags = if ($Transport -eq "USB4Kn") {
        "-DGXOS_DM24_QEMU_FAT32_4KN_PROOF -DGXOS_DM30_QEMU_GPT_REPAIR_PROOF"
    } else {
        "-DGXOS_DM15_QEMU_AHCI_PROOF -DGXOS_DM30_QEMU_GPT_REPAIR_PROOF"
    }
    & $make.Source -C (Join-Path $Root "kernel") ARCH=amd64 `
        "EXTRA_CFLAGS=$flags" -j4 2>&1 |
        Out-File -LiteralPath (Join-Path $WorkFull "kernel-build.log") -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "DM30 proof kernel build failed." }
    $msbuild = Find-MSBuild
    & $msbuild (Join-Path $Root "guideXOSBootLoader\guideXOSBootLoader.vcxproj") `
        /t:Build /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal `
        2>&1 | Out-File -LiteralPath (Join-Path $WorkFull "uefi-release-build.log") -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "UEFI x64 Release build failed." }
}
if (-not (Test-Path -LiteralPath $bootloader) -or -not (Test-Path -LiteralPath $kernel)) {
    throw "The UEFI bootloader or DM30 proof kernel is missing."
}
$bootDir = Join-Path $EspPath "EFI\BOOT"
New-Item -ItemType Directory -Path $bootDir -Force | Out-Null
Copy-Item -LiteralPath $bootloader -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force
Copy-Item -LiteralPath $kernel -Destination (Join-Path $EspPath "kernel.elf") -Force
Copy-Item -LiteralPath $kernel -Destination (Join-Path $WorkFull "kernel-proof.elf") -Force
$QemuVersion = (& $QemuFull --version | Select-Object -First 1)
$sectorSize = if ($Transport -eq "USB4Kn") { 4096 } else { 512 }
$healthyPath = Join-Path $WorkFull "three-partition-healthy.raw"
$fixtureReport = Invoke-PythonJson @(
    (Join-Path $Root "scripts\create-dm30-gpt-fixture.py"),
    $healthyPath, "--sector-size", "$sectorSize") `
    (Join-Path $WorkFull "fixture.json")
$healthyInspection = Invoke-PythonJson @(
    (Join-Path $Root "scripts\verify-dm29-gpt.py"),
    $healthyPath, "--sector-size", "$sectorSize") `
    (Join-Path $WorkFull "fixture-verification.json")
if ($healthyInspection.redundancy -ne "Healthy" -or
    $healthyInspection.partitions.Count -ne 3) {
    throw "The healthy DM30 fixture failed independent GPT verification."
}

$results = [System.Collections.Generic.List[object]]::new()
$activeVm = $null
try {
    foreach ($side in @("Backup", "Primary")) {
        $direction = if ($side -eq "Backup") { "BackupFromPrimary" } else { "PrimaryFromBackup" }
        $offlinePath = Join-Path $WorkFull "$($side.ToLowerInvariant())-degraded-reference.raw"
        Copy-RawSparse $healthyPath $offlinePath
        $offlineCorruption = Invoke-PythonJson @(
            (Join-Path $Root "scripts\corrupt-dm29-qemu-backup-array.py"),
            $offlinePath, "--side", $side, "--sector-size", "$sectorSize") `
            (Join-Path $WorkFull "$($side.ToLowerInvariant())-reference-corruption.json")
        if ($offlineCorruption.array_sectors -ne $fixtureReport.gpt_entry_array_sectors) {
            throw "The corruption helper used unexpected GPT entry-array geometry."
        }
        $degradedReference = Invoke-PythonJson @(
            (Join-Path $Root "scripts\verify-dm29-gpt.py"),
            $offlinePath, "--sector-size", "$sectorSize") `
            (Join-Path $WorkFull "$($side.ToLowerInvariant())-reference-verification.json") @(1)
        $expectedAuthority = if ($side -eq "Backup") { "Primary" } else { "Backup" }
        $expectedBadState = "ArrayCrcInvalid"
        $badCopy = if ($side -eq "Backup") { $degradedReference.backup } else { $degradedReference.primary }
        if ($degradedReference.redundancy -ne "Degraded" -or
            $degradedReference.authoritative_copy -ne $expectedAuthority -or
            $badCopy.state -ne $expectedBadState -or
            $degradedReference.partitions.Count -ne 3) {
            throw "Offline $side array damage failed the independent degraded-state check."
        }

        # OVMF probes disks before the kernel and can normalize a pre-corrupted
        # GPT. Boot from a clean fixture, pause before AHCI/USB registration,
        # then inject the same independently verified damage into live media.
        $damagedPath = Join-Path $WorkFull "$($side.ToLowerInvariant())-degraded.raw"
        Copy-RawSparse $healthyPath $damagedPath
        $repairLabel = "$($side.ToLowerInvariant())-repair"
        $activeVm = Start-ProofVm $repairLabel $damagedPath
        [void](Wait-SerialMarker $activeVm.Process $activeVm.SerialPath `
            "[DM29-QEMU] host-corruption-window=READY" 300)
        Connect-Qmp $activeVm (Join-Path $WorkFull "$repairLabel-qmp.jsonl")
        [void](Send-Qmp "stop")
        $pausedStatus = Send-Qmp "query-status"
        if ($pausedStatus.status -ne "paused") {
            throw "QEMU did not pause at the live-media corruption window."
        }
        $corruption = Invoke-PythonJson @(
            (Join-Path $Root "scripts\corrupt-dm29-qemu-backup-array.py"),
            $damagedPath, "--side", $side, "--sector-size", "$sectorSize") `
            (Join-Path $WorkFull "$($side.ToLowerInvariant())-corruption.json")
        $degraded = Invoke-PythonJson @(
            (Join-Path $Root "scripts\verify-dm29-gpt.py"),
            $damagedPath, "--sector-size", "$sectorSize") `
            (Join-Path $WorkFull "$($side.ToLowerInvariant())-degraded-verification.json") @(1)
        $badCopy = if ($side -eq "Backup") { $degraded.backup } else { $degraded.primary }
        $authoritativeCopy = if ($side -eq "Backup") { $degraded.primary } else { $degraded.backup }
        $referenceAuthorityCopy = if ($side -eq "Backup") { $degradedReference.primary } else { $degradedReference.backup }
        if ($degraded.redundancy -ne "Degraded" -or
            $degraded.authoritative_copy -ne $expectedAuthority -or
            $badCopy.state -ne $expectedBadState -or
            $authoritativeCopy.entry_array_sha256 -ne $referenceAuthorityCopy.entry_array_sha256 -or
            $degraded.disk_guid -ne $degradedReference.disk_guid -or
            (($degraded.partitions | ConvertTo-Json -Compress -Depth 8) -ne
             ($degradedReference.partitions | ConvertTo-Json -Compress -Depth 8))) {
            throw "Live $side array damage differs from the independently verified reference."
        }
        $beforeRepairPath = Join-Path $WorkFull "$($side.ToLowerInvariant())-before-repair.raw"
        Copy-RawSparse $damagedPath $beforeRepairPath
        [void](Send-Qmp "cont")
        Close-Qmp

        $expectedRepairMarker = "[DM29-QEMU] repair=PASS direction=$direction"
        $serial = Wait-SerialMarker $activeVm.Process $activeVm.SerialPath `
            $expectedRepairMarker 300
        Stop-ProofVm $activeVm
        $activeVm = $null
        $repairedPath = Join-Path $WorkFull "$($side.ToLowerInvariant())-repaired.raw"
        Copy-RawSparse $damagedPath $repairedPath
        $after = Invoke-PythonJson @(
            (Join-Path $Root "scripts\verify-dm29-gpt.py"),
            $repairedPath, "--sector-size", "$sectorSize", "--before", $beforeRepairPath) `
            (Join-Path $WorkFull "$($side.ToLowerInvariant())-repair-verification.json")
        if ($after.redundancy -ne "Healthy" -or
            -not $after.normalized_copies_equal -or
            -not $after.protective_mbr_unchanged -or
            -not $after.changed_ranges.metadata_only -or
            $after.partitions.Count -ne 3 -or
            (($after.partitions | ConvertTo-Json -Compress -Depth 8) -ne
             ($healthyInspection.partitions | ConvertTo-Json -Compress -Depth 8))) {
            throw "The $side repair failed normalized-copy, identity, PMBR, or metadata-only verification."
        }
        $activeVm = Start-ProofVm "$($side.ToLowerInvariant())-cold-restart" $repairedPath
        $restartSerial = Wait-SerialMarker $activeVm.Process $activeVm.SerialPath `
            "[DM29-QEMU] restart=PASS" 300
        Stop-ProofVm $activeVm
        $activeVm = $null
        $results.Add([pscustomobject]@{
            damagedSide = $side
            direction = $direction
            authoritativeCopy = $expectedAuthority
            corruptedEntry = $corruption.corrupted_entry_number
            corruptedLba = $corruption.corrupted_byte_lba
            logicalSectorSize = $sectorSize
            entryArraySectors = $fixtureReport.gpt_entry_array_sectors
            offlineReference = [IO.Path]::GetFileName($offlinePath)
            runtimeDamagedImage = [IO.Path]::GetFileName($damagedPath)
            repairSerial = [IO.Path]::GetFileName((Join-Path $WorkFull "$($side.ToLowerInvariant())-repair.serial.log"))
            restartSerial = [IO.Path]::GetFileName((Join-Path $WorkFull "$($side.ToLowerInvariant())-cold-restart.serial.log"))
            healthyPartitionEntries = $after.partitions
            changedRanges = $after.changed_ranges.ranges
            result = "PASS"
        })
        $serial | Set-Content -LiteralPath (Join-Path $WorkFull "$($side.ToLowerInvariant())-repair-serial-capture.txt") -Encoding utf8
        $restartSerial | Set-Content -LiteralPath (Join-Path $WorkFull "$($side.ToLowerInvariant())-restart-serial-capture.txt") -Encoding utf8
    }
} catch {
    Close-Qmp
    if ($activeVm) {
        try { Stop-ProofVm $activeVm } catch { }
        if ($damagedPath -and (Test-Path -LiteralPath $damagedPath)) {
            (Get-FileHash -LiteralPath $damagedPath -Algorithm SHA256).Hash |
                Set-Content -LiteralPath (Join-Path $WorkFull "$($side.ToLowerInvariant())-post-qemu-sha256.txt") -Encoding ascii
        }
    }
    throw
}

$manifest = [pscustomobject]@{
    phase = "DM30"
    transport = $Transport
    logicalSectorSize = $sectorSize
    qemu = $QemuVersion
    fixture = $fixtureReport
    results = $results
    physicalHostDisksPassedToQemu = "none"
    outcome = "PASS"
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $WorkFull "manifest.json") -Encoding utf8
Write-Host "DM30 $Transport bidirectional multi-partition GPT repair passed. Evidence: $WorkFull"
