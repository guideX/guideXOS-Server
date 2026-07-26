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
    [int]$TimeoutSeconds = 100,
    [int]$Cycles = 3
)

$ErrorActionPreference = "Stop"

$ServerRoot = Split-Path -Parent $PSScriptRoot
$PacmanRoot = Join-Path (Split-Path -Parent $ServerRoot) "pacman"
$EspDir = Join-Path $ServerRoot "ESP"
$KernelElf = Join-Path $ServerRoot "kernel\build\amd64\bin\kernel.elf"
$ProductionPackage = Join-Path $env:ProgramData "guideXOS\PacMan"
if (!(Test-Path $ProductionPackage)) {
    $ProductionPackage = "D:\Apps\PacMan"
}
$RunId = Get-Date -Format "yyyyMMdd-HHmmss"
$RunDir = Join-Path $ServerRoot "logs\baremetal-pacman\$RunId"
$SerialLog = Join-Path $RunDir "qemu-serial.log"
$QemuErrLog = Join-Path $RunDir "qemu-stderr.log"
$ValidationLog = Join-Path $RunDir "validation.log"
$StageDir = Join-Path $EspDir "Apps\PacMan"
$Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$Ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$QemuProcess = $null
$QmpPort = 0

New-Item -ItemType Directory -Path $RunDir -Force | Out-Null

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

function Wait-SerialCount([string]$Needle, [int]$Minimum, [int]$Seconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $content = Read-Serial
        if ((Text-Count $content $Needle) -ge $Minimum) { return $true }
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
    Start-Sleep -Milliseconds 45
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
    New-Item -ItemType Directory -Path (Join-Path $EspDir "Apps") -Force | Out-Null

    $appsRoot = (Resolve-Path (Join-Path $EspDir "Apps")).Path.TrimEnd('\')
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
    Write-Validation "package.staged root=/Apps/PacMan source=$ProductionPackage files=$($files -join ',')"
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

    if (!$SkipBuild) {
        Write-Validation "build.pacman=production Native ELF diagnostics=ON danger-validation=OFF"
        & "C:\mingw64\bin\cmake.exe" -S (Join-Path $PacmanRoot "guidexos") -B (Join-Path $PacmanRoot "guidexos\build-baremetal") -G Ninja `
            -DGUIDEXOS_SERVER_ROOT=$ServerRoot -DGUIDEXOS_PACKAGE_ROOT="D:\Apps" `
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
    Copy-Item -LiteralPath $KernelElf -Destination (Join-Path $EspDir "kernel.elf") -Force
    Stage-ProductionPackage

    $qmpProbe = New-Object System.Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
    $qmpProbe.Start()
    $QmpPort = ([Net.IPEndPoint]$qmpProbe.LocalEndpoint).Port
    $qmpProbe.Stop()
    # Start-Process passes an ArgumentList array through Windows command-line
    # tokenization. Keep the two drive specifications quoted as whole tokens;
    # the OVMF path contains a space.
    $qemuArgumentString = `
        '-machine "pc,accel=tcg" ' +
        '-drive "if=pflash,format=raw,readonly=on,file=' + $Ovmf + '" ' +
        '-drive "file=fat:rw:' + $EspDir + ',format=raw,if=ide,index=0,media=disk" ' +
        '-m 1024M -vga std -display none ' +
        '-serial "file:' + $SerialLog + '" ' +
        '-qmp "tcp:127.0.0.1:' + $QmpPort + ',server=on,wait=off" ' +
        '-no-reboot -rtc base=utc,clock=host ' +
        '-netdev user,id=net0 -device e1000,netdev=net0'
    Write-Validation "qemu.start qmpPort=$QmpPort serial=$SerialLog"
    $QemuProcess = Start-Process -FilePath $Qemu -ArgumentList $qemuArgumentString -WorkingDirectory (Split-Path -Parent $Qemu) `
        -RedirectStandardOutput (Join-Path $RunDir "qemu-stdout.log") -RedirectStandardError $QemuErrLog -PassThru
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

        Send-Key "right"
        Send-Key "right"
        Send-Key "down"
        Send-Key "left"
        Start-Sleep -Seconds 2
        Capture-Screenshot (Join-Path $RunDir "cycle-$cycle-input.ppm")
        Write-Validation "cycle.$cycle gameplay.input=right,right,down,left screenshot=captured"

        $beforeExit = Text-Count (Read-Serial) "lifecycle PASS window/resource cleanup complete"
        Send-Key "esc"
        if (!(Wait-SerialCount "lifecycle PASS window/resource cleanup complete" ($beforeExit + 1) 20)) { throw "Cycle $cycle did not cleanly exit" }
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
    if ($QemuProcess -and !$QemuProcess.HasExited) {
        Write-Validation "cleanup stopping owned qemu pid=$($QemuProcess.Id)"
        Stop-Process -Id $QemuProcess.Id -Force -ErrorAction SilentlyContinue
        $QemuProcess.WaitForExit(3000)
    }
}
