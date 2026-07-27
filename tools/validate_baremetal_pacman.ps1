<#
.SYNOPSIS
    Rebuild, stage, and validate the production PacMan Native ELF on the
    guideXOS bare-metal kernel path under QEMU.

.DESCRIPTION
    QEMU is used as the bare-metal/UEFI execution target.  This harness never
    uses the hosted Native ELF executor.  It stages /Apps/PacMan on the same
    ESP directory exposed to the kernel FAT VFS, then launches the package by
    typing the normal desktop shell command: desktop.launch Nexgen PacMan.

    The run is bounded and owns one QEMU process.  Its serial log and PPM
    screenshots are kept under logs/baremetal-pacman/<run-id>.
#>

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipGameplayInput,
    [string]$GameplayKeys = "right",
    [int]$TimeoutSeconds = 100,
    [int]$Cycles = 3,
    [int]$HoldAfterFrameSeconds = 0,
    [switch]$ReadOnlyDisk,
    [string]$KernelElfPath = ""
)

$ErrorActionPreference = "Stop"

$ServerRoot = Split-Path -Parent $PSScriptRoot
$PacmanRoot = Join-Path (Split-Path -Parent $ServerRoot) "pacman"
$EspDir = Join-Path $ServerRoot "ESP"
$KernelElf = if ([string]::IsNullOrWhiteSpace($KernelElfPath)) {
    Join-Path $ServerRoot "kernel\build\amd64\bin\kernel.elf"
} else {
    $KernelElfPath
}
$ProductionPackage = Join-Path $env:ProgramData "guideXOS\PacMan"
if (!(Test-Path $ProductionPackage)) {
    $ProductionPackage = "D:\Apps\PacMan"
}
$RunId = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss-fff"), ([guid]::NewGuid().ToString("N").Substring(0, 8))
$RunDir = Join-Path $ServerRoot "logs\baremetal-pacman\$RunId"
$SerialLog = Join-Path $RunDir "qemu-serial.log"
$SerialTailLog = Join-Path $RunDir "qemu-serial-tail-200.log"
$QemuErrLog = Join-Path $RunDir "qemu-stderr.log"
$QemuStdoutLog = Join-Path $RunDir "qemu-stdout.log"
$QemuDebugLog = Join-Path $RunDir "qemu-debug.log"
$QemuCommandLog = Join-Path $RunDir "qemu-command-line.txt"
$QemuPidLog = Join-Path $RunDir "qemu-pid.txt"
$QemuExitLog = Join-Path $RunDir "qemu-exit-code.txt"
$QemuTailLog = Join-Path $RunDir "qemu-debug-tail-200.log"
$OutcomeLog = Join-Path $RunDir "outcome.txt"
$ValidationLog = Join-Path $RunDir "validation.log"
$Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$Ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$QemuProcess = $null
$QmpPort = 0

New-Item -ItemType Directory -Path $RunDir -Force | Out-Null
$QemuEspDir = Join-Path $RunDir "esp"
$StageDir = Join-Path $QemuEspDir "Apps\PacMan"

function Write-Validation([string]$Message) {
    $line = "[$(Get-Date -Format o)] $Message"
    Write-Host $line
    Add-Content -LiteralPath $ValidationLog -Value $line
}

function Assert-Path([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path)) {
        throw "Missing $Description`: $Path"
    }
}

function Text-Count([string]$Text, [string]$Needle) {
    if (!$Text -or !$Needle) { return 0 }
    $count = 0
    $offset = 0
    while (($index = $Text.IndexOf($Needle, $offset, [System.StringComparison]::Ordinal)) -ge 0) {
        $count++
        $offset = $index + $Needle.Length
    }
    return $count
}

function Read-Serial {
    if (!(Test-Path -LiteralPath $SerialLog)) { return "" }
    try { return Get-Content -LiteralPath $SerialLog -Raw -ErrorAction Stop } catch { return "" }
}

function Save-Tails {
    if (Test-Path -LiteralPath $SerialLog) {
        Get-Content -LiteralPath $SerialLog -Tail 200 | Set-Content -LiteralPath $SerialTailLog
    }
    if (Test-Path -LiteralPath $QemuDebugLog) {
        Get-Content -LiteralPath $QemuDebugLog -Tail 200 | Set-Content -LiteralPath $QemuTailLog
    }
}

function Classify-QemuOutcome {
    $serial = Read-Serial
    $debug = if (Test-Path -LiteralPath $QemuDebugLog) { Get-Content -LiteralPath $QemuDebugLog -Raw } else { "" }
    $stderr = if (Test-Path -LiteralPath $QemuErrLog) { Get-Content -LiteralPath $QemuErrLog -Raw } else { "" }
    $exitCode = if (Test-Path -LiteralPath $QemuExitLog) { ((Get-Content -LiteralPath $QemuExitLog -Raw) -as [string]).Trim() } else { "unknown" }
    if ([string]::IsNullOrWhiteSpace($exitCode)) { $exitCode = "unknown" }
    $resetCount = [regex]::Matches($debug, '(?m)^CPU Reset \(CPU \d+\)').Count
    $classification = "unknown"
    if ($serial -match "(?i)(triple fault|triple-fault)") {
        $classification = "guest triple fault/reset"
    } elseif ($serial -match "(?i)(debug.?exit|emulator.?exit|qemu.?exit)") {
        $classification = "guest debug exit"
    } elseif ($serial -match "(?i)(acpi.*shutdown|poweroff|system shutdown|shutdown requested)") {
        $classification = "guest shutdown"
    } elseif ($serial -match "(?i)(guest reboot|reboot requested|reset requested)") {
        $classification = "guest reboot/reset"
    } elseif ($stderr -match "(?i)(assertion failed|Bail out|qemu: fatal|abort") {
        $classification = "host-side QEMU crash"
    } elseif ($debug -match "(?i)(triple fault|shutdown)") {
        $classification = "QEMU diagnostic indicates reset/shutdown"
    } elseif ($resetCount -gt 2 -and $serial -notmatch "(?i)lifecycle PASS") {
        $classification = "guest reboot/reset"
    } elseif ($exitCode -ne "0" -and $exitCode -ne "unknown") {
        $classification = "host-side QEMU exit/crash"
    } elseif (!$QemuProcess -or !$QemuProcess.HasExited) {
        $classification = "harness timeout/owned QEMU still running"
    } elseif ($serial -match "(?i)Entering main loop") {
        $classification = "harness stopped healthy guest"
    } else {
        $classification = "QEMU exited without guest shutdown marker"
    }
    Set-Content -LiteralPath $OutcomeLog -Value ("classification={0}`nqemuExitCode={1}`nqemuCpuResetRecords={2}" -f $classification, $exitCode, $resetCount)
    Write-Validation "qemu.outcome classification=$classification exitCode=$exitCode cpuResetRecords=$resetCount"
}

function Wait-SerialCount([string]$Needle, [int]$Minimum, [int]$Seconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $content = Read-Serial
        if ((Text-Count $content $Needle) -ge $Minimum) { return $true }
        if ($QemuProcess -and $QemuProcess.HasExited) {
            Write-Validation "qemu.exited.while.waiting needle=$Needle exitCode=$($QemuProcess.ExitCode)"
            return $false
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Send-Qmp([string]$CommandLine) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect("127.0.0.1", $QmpPort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 1200
        Start-Sleep -Milliseconds 100
        $buffer = New-Object byte[] 8192
        while ($stream.DataAvailable) { [void]$stream.Read($buffer, 0, $buffer.Length) }
        $capabilities = '{"execute":"qmp_capabilities"}' + "`r`n"
        $capabilityBytes = [Text.Encoding]::UTF8.GetBytes($capabilities)
        $stream.Write($capabilityBytes, 0, $capabilityBytes.Length)
        Start-Sleep -Milliseconds 60
        while ($stream.DataAvailable) { [void]$stream.Read($buffer, 0, $buffer.Length) }
        $request = @{
            execute = "human-monitor-command"
            arguments = @{ "command-line" = $CommandLine }
        } | ConvertTo-Json -Compress
        $request = $request + "`r`n"
        $requestBytes = [Text.Encoding]::UTF8.GetBytes($request)
        $stream.Write($requestBytes, 0, $requestBytes.Length)
        Start-Sleep -Milliseconds 80
        $response = New-Object Text.StringBuilder
        while ($stream.DataAvailable) {
            $read = $stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            [void]$response.Append([Text.Encoding]::UTF8.GetString($buffer, 0, $read))
        }
        return $response.ToString()
    } finally {
        if ($client) { $client.Close() }
    }
}

function Send-Key([string]$Key) {
    $response = Send-Qmp "sendkey $Key"
    if ($response -match "invalid parameter|unknown command|error") {
        throw "QMP sendkey failed for $Key`: $response"
    }
    # Leave enough time for QEMU's emulated PS/2 controller and the kernel
    # IRQ path to deliver both make and break transitions before the next key.
    Start-Sleep -Milliseconds 180
}

function Send-ShellText([string]$Text) {
    foreach ($character in $Text.ToLowerInvariant().ToCharArray()) {
        $key = switch ($character) {
            ' ' { 'spc'; break }
            '.' { 'dot'; break }
            '-' { 'minus'; break }
            '/' { 'slash'; break }
            default { [string]$character }
        }
        Send-Key $key
    }
}

function Capture-Screenshot([string]$Path) {
    $qemuPath = $Path -replace '\\', '/'
    [void](Send-Qmp "screendump $qemuPath")
    Start-Sleep -Milliseconds 150
}

function Stage-ProductionPackage {
    Assert-Path (Join-Path $ProductionPackage "app.json") "production manifest"
    Assert-Path (Join-Path $ProductionPackage "bin\amd64\pacman.elf") "production Native ELF"
    Assert-Path (Join-Path $ProductionPackage "resources\level1.gximg") "production level resource"
    Assert-Path (Join-Path $ProductionPackage "resources\pacpics.gximg") "production sprite resource"
    New-Item -ItemType Directory -Path (Join-Path $QemuEspDir "Apps") -Force | Out-Null

    $appsRoot = (Resolve-Path (Join-Path $QemuEspDir "Apps")).Path.TrimEnd('\')
    if (Test-Path -LiteralPath $StageDir) {
        $resolvedStage = (Resolve-Path $StageDir).Path
        if (!$resolvedStage.StartsWith($appsRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove staging path outside ESP\Apps: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Join-Path $StageDir "bin\amd64") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $StageDir "resources") -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $ProductionPackage "app.json") -Destination (Join-Path $StageDir "app.json") -Force
    Copy-Item -LiteralPath (Join-Path $ProductionPackage "bin\amd64\pacman.elf") -Destination (Join-Path $StageDir "bin\amd64\pacman.elf") -Force
    Copy-Item -LiteralPath (Join-Path $ProductionPackage "resources\level1.gximg") -Destination (Join-Path $StageDir "resources\level1.gximg") -Force
    Copy-Item -LiteralPath (Join-Path $ProductionPackage "resources\pacpics.gximg") -Destination (Join-Path $StageDir "resources\pacpics.gximg") -Force

    $files = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File | ForEach-Object { $_.FullName.Substring($StageDir.Length + 1).Replace('\', '/') })
    $expected = @("app.json", "bin/amd64/pacman.elf", "resources/level1.gximg", "resources/pacpics.gximg")
    if ((Compare-Object $expected $files).Count -ne 0) {
        throw "ESP package tree is not the exact production package: $($files -join ', ')"
    }
    Write-Validation "package.staged root=/Apps/PacMan image=$QemuEspDir source=$ProductionPackage files=$($files -join ',')"
    foreach ($file in $files) {
        $hash = Get-FileHash -LiteralPath (Join-Path $StageDir $file.Replace('/', '\')) -Algorithm SHA256
        Write-Validation "package.hash path=/Apps/PacMan/$file sha256=$($hash.Hash)"
    }
}

try {
    Write-Validation "classification.target=bare-metal/UEFI QEMU (not physical hardware)"
    Write-Validation "hosted.runtime=excluded; execution.path=UEFI bootloader -> kernel -> FAT VFS -> App Model -> Native ELF"
    Assert-Path $EspDir "ESP directory"
    Assert-Path $Qemu "QEMU"
    Assert-Path $Ovmf "OVMF firmware"

    # Give QEMU an isolated writable FAT tree for this bounded run. The
    # original ESP remains untouched, including concurrent desktop changes.
    New-Item -ItemType Directory -Path $QemuEspDir -Force | Out-Null
    Get-ChildItem -LiteralPath $EspDir -Force | Copy-Item -Destination $QemuEspDir -Recurse -Force

    if (!$SkipBuild) {
        Write-Validation "build.pacman=production Native ELF diagnostics=ON danger-validation=OFF"
        $ServerRootCmake = $ServerRoot.Replace('\', '/')
        & "C:\mingw64\bin\cmake.exe" -S (Join-Path $PacmanRoot "guidexos") -B (Join-Path $PacmanRoot "guidexos\build-baremetal") -G Ninja `
            "-DGUIDEXOS_SERVER_ROOT=$ServerRootCmake" "-DGUIDEXOS_PACKAGE_ROOT=D:/Apps" `
            -DPACMAN_ENABLE_DIAGNOSTICS=ON -DPACMAN_HOSTED_DANGER_TEST=OFF `
            -DPACMAN_HOSTED_RED_MOVEMENT_TEST=OFF -DPACMAN_HOSTED_PINK_MOVEMENT_TEST=OFF `
            -DPACMAN_HOSTED_CYAN_MOVEMENT_TEST=OFF -DPACMAN_HOSTED_ORANGE_MOVEMENT_TEST=OFF `
            -DPACMAN_HOSTED_POWER_PILL_TEST=OFF
        if ($LASTEXITCODE -ne 0) { throw "PacMan CMake configure failed" }
        & "C:\mingw64\bin\cmake.exe" --build (Join-Path $PacmanRoot "guidexos\build-baremetal") --target pacman-native -j2
        if ($LASTEXITCODE -ne 0) { throw "PacMan Native ELF build failed" }
        Write-Validation "build.server=kernel amd64"
        & "C:\mingw64\bin\mingw32-make.exe" -C (Join-Path $ServerRoot "kernel") ARCH=amd64 -j2
        if ($LASTEXITCODE -ne 0) { throw "guideXOS kernel build failed" }
    }

    Assert-Path $KernelElf "built kernel ELF"
    Assert-Path (Join-Path $ServerRoot "ESP\EFI\BOOT\BOOTX64.EFI") "UEFI bootloader"
    Copy-Item -LiteralPath $KernelElf -Destination (Join-Path $QemuEspDir "kernel.elf") -Force
    Stage-ProductionPackage

    $qmpProbe = New-Object System.Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
    $qmpProbe.Start()
    $QmpPort = ([Net.IPEndPoint]$qmpProbe.LocalEndpoint).Port
    $qmpProbe.Stop()
    # Start-Process passes an ArgumentList array through Windows command-line
    # tokenization. Keep the two drive specifications quoted as whole tokens;
    # the OVMF path contains a space.
    $diskMode = if ($ReadOnlyDisk) { "ro" } else { "rw" }
    $diskSpec = "fat:{0}:{1}" -f $diskMode, $QemuEspDir
    $diskReadOnlyOption = if ($ReadOnlyDisk) { ",readonly=on" } else { "" }
    $qemuArgumentString = `
        '-machine "pc,accel=tcg" ' +
        '-drive "if=pflash,format=raw,readonly=on,file=' + $Ovmf + '" ' +
        '-drive "file=' + $diskSpec + ',format=raw,if=ide,index=0,media=disk' + $diskReadOnlyOption + '" ' +
        '-m 1024M -vga std -display none ' +
        '-serial "file:' + $SerialLog + '" ' +
        '-qmp "tcp:127.0.0.1:' + $QmpPort + ',server=on,wait=off" ' +
        '-d guest_errors,int,cpu_reset -D "' + $QemuDebugLog + '" ' +
        '-no-reboot -no-shutdown -rtc base=utc,clock=host ' +
        '-netdev user,id=net0 -device e1000,netdev=net0'
    Set-Content -LiteralPath $QemuCommandLog -Value ("`"{0}`" {1}" -f $Qemu, $qemuArgumentString)
    Write-Validation "qemu.disk.mode=$diskMode"
    Write-Validation "qemu.command=$Qemu $qemuArgumentString"
    Write-Validation "qemu.start qmpPort=$QmpPort serial=$SerialLog debug=$QemuDebugLog"
    $QemuProcess = Start-Process -FilePath $Qemu -ArgumentList $qemuArgumentString -WorkingDirectory (Split-Path -Parent $Qemu) `
        -RedirectStandardOutput $QemuStdoutLog -RedirectStandardError $QemuErrLog -PassThru
    Set-Content -LiteralPath $QemuPidLog -Value $QemuProcess.Id
    Write-Validation "qemu.pid=$($QemuProcess.Id)"
    Start-Sleep -Milliseconds 250
    if ($QemuProcess.HasExited) {
        $qemuError = if (Test-Path -LiteralPath $QemuErrLog) { Get-Content -LiteralPath $QemuErrLog -Raw } else { "(no stderr)" }
        throw "QEMU exited immediately with code $($QemuProcess.ExitCode): $qemuError"
    }
    if (!(Wait-SerialCount "[KERNEL] Entering main loop" 1 $TimeoutSeconds)) {
        throw "Kernel did not reach the interactive main loop"
    }
    Write-Validation "boot.PASS kernel reached main loop"
    $qmpStatus = Send-Qmp "info status"
    Write-Validation "qmp.probe response=$qmpStatus"
    Capture-Screenshot (Join-Path $RunDir "desktop-after-boot.ppm")

    for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
        $beforeLaunch = Text-Count (Read-Serial) "launch begin"
        Send-Key "f12"
        if (!(Wait-SerialCount "launch begin" ($beforeLaunch + 1) 30)) { throw "Cycle $cycle did not enter Native ELF launch" }
        $beforeFrame = Text-Count (Read-Serial) "[NATIVE-ELF] frame PASS"
        $frameWaitSeconds = [Math]::Min(60, [Math]::Max(20, $TimeoutSeconds))
        if (!(Wait-SerialCount "[NATIVE-ELF] frame PASS" ($beforeFrame + 1) $frameWaitSeconds)) { throw "Cycle $cycle did not present a PacMan frame" }
        Capture-Screenshot (Join-Path $RunDir "cycle-$cycle-initial.ppm")

        if (!$SkipGameplayInput) {
            foreach ($gameplayKey in ($GameplayKeys -split ',' | Where-Object { $_.Trim().Length -gt 0 })) {
                Send-Key $gameplayKey.Trim()
            }
            Start-Sleep -Seconds 2
            Capture-Screenshot (Join-Path $RunDir "cycle-$cycle-input.ppm")
            Write-Validation "cycle.$cycle gameplay.input=$GameplayKeys screenshot=captured"
        } else {
            Write-Validation "cycle.$cycle gameplay.input=skipped"
        }

        if ($HoldAfterFrameSeconds -gt 0) {
            Write-Validation "cycle.$cycle hold.begin seconds=$HoldAfterFrameSeconds"
            for ($holdSecond = 0; $holdSecond -lt $HoldAfterFrameSeconds; $holdSecond++) {
                if ($QemuProcess.HasExited) {
                    throw "Cycle $cycle QEMU exited during hold at second $holdSecond"
                }
                Start-Sleep -Seconds 1
            }
            if ($QemuProcess.HasExited) {
                throw "Cycle $cycle QEMU exited at end of hold"
            }
            Capture-Screenshot (Join-Path $RunDir "cycle-$cycle-hold.ppm")
            Write-Validation "cycle.$cycle hold.PASS seconds=$HoldAfterFrameSeconds screenshot=captured"
        }

        $beforeExit = Text-Count (Read-Serial) "lifecycle PASS window/resource cleanup complete"
        $exitObserved = $false
        for ($escapeAttempt = 1; $escapeAttempt -le 3; $escapeAttempt++) {
            Write-Validation "cycle.$cycle escape.attempt=$escapeAttempt"
            Send-Key "esc"
            if (Wait-SerialCount "lifecycle PASS window/resource cleanup complete" ($beforeExit + 1) 6) {
                $exitObserved = $true
                break
            }
        }
        if (!$exitObserved) { throw "Cycle $cycle did not cleanly exit" }
        Write-Validation "cycle.$cycle PASS launch/frame/input/escape/lifecycle"
    }

    $serial = Read-Serial
    $required = @(
        "App Model package discovered",
        "source=bare-metal-VFS",
        "load PASS format=ELF64 machine=amd64",
        "file_read app=Nexgen PacMan relative=resources/level1.gximg offset=0x",
        "file_read app=Nexgen PacMan relative=resources/pacpics.gximg offset=0x",
        "frame PASS app=Nexgen PacMan window=0x",
        "PacMan ghosts initialized: Red, Pink, Cyan, and Orange moving",
        "PacMan requested direction",
        "lifecycle PASS window/resource cleanup complete"
    )
    foreach ($needle in $required) {
        if ($serial.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) { throw "Missing required serial evidence: $needle" }
    }
    Write-Validation "evidence.PASS discovery/ELF/VFS-resources/frame/input/ghosts/lifecycle"
    Write-Validation "VALIDATION_RESULT=PASS"
    exit 0
} catch {
    try { Write-Validation "qmp.registers=$((Send-Qmp 'info registers').Trim())" } catch { Write-Validation "qmp.registers=unavailable" }
    Write-Validation "VALIDATION_RESULT=FAIL reason=$($_.Exception.Message)"
    exit 1
} finally {
    if ($QemuProcess -and $QemuProcess.HasExited) {
        $QemuProcess.Refresh()
        try { $finalExitCode = [int]$QemuProcess.ExitCode } catch { $finalExitCode = "unknown" }
        Set-Content -LiteralPath $QemuExitLog -Value $finalExitCode
        Write-Validation "qemu.exit code=$finalExitCode"
    }
    if ($QemuProcess -and !$QemuProcess.HasExited) {
        Write-Validation "cleanup stopping owned qemu pid=$($QemuProcess.Id)"
        Stop-Process -Id $QemuProcess.Id -Force -ErrorAction SilentlyContinue
        $QemuProcess.WaitForExit(3000)
        if ($QemuProcess.HasExited) {
            $QemuProcess.Refresh()
            try { $finalExitCode = [int]$QemuProcess.ExitCode } catch { $finalExitCode = "unknown" }
            Set-Content -LiteralPath $QemuExitLog -Value $finalExitCode
        }
    }
    Save-Tails
    Classify-QemuOutcome
}
