param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$PythonExe = "",
    [int]$TimeoutSeconds = 900,
    [switch]$ReuseBuiltProofKernel
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c158-managed-calculator"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C158 evidence must remain under $allowedRoot"
}
if ($TimeoutSeconds -lt 30) { throw "TimeoutSeconds must be at least 30." }

$kernelPath = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
$espKernelPath = Join-Path $RepoRoot 'ESP\kernel.elf'
$protectedRamdiskPath = Join-Path $RepoRoot 'ESP\ramdisk.img'
$bootloaderPath = Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$buildRoot = Join-Path $EvidenceRoot 'build'
$compositeRoot = Join-Path $buildRoot 'composite'
$runtimePackOutput = Join-Path $buildRoot 'runtime-pack'
$stageRoot = Join-Path $EvidenceRoot 'staging\wallpaper-pack'
$proofRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c158.img'
$proofKernel = Join-Path $EvidenceRoot 'proof-kernel.elf'
$proofBackup = Join-Path $EvidenceRoot 'canonical\kernel.elf'
$espKernelBackup = Join-Path $EvidenceRoot 'canonical\ESP-kernel.elf'
$ramdiskBackup = Join-Path $EvidenceRoot 'canonical\ESP-ramdisk.img'
$canonicalKernelHash = $null
$espKernelHash = $null
$ramdiskHash = $null
$restored = $false
$script:activeProcess = $null
$script:activeStderr = $null
$script:activeSerial = $null
$script:activePort = 0
$script:activeQmpLog = $null
$script:cursor = $null

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $File $($Arguments -join ' ')"
    }
}

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-AvailableQmpPort {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally { $listener.Stop() }
}

function Get-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    throw "Required tool '$Name' was not found."
}

function Assert-EvidencePath([string]$Path, [string]$Label) {
    $root = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $full = [System.IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside the C158 evidence folder: $full"
    }
}

function Stage-Esp([string]$Esp, [string]$Kernel, [string]$Ramdisk,
                   [string]$SettingsRecord = "") {
    Assert-EvidencePath $Esp 'QEMU ESP'
    if (Test-Path -LiteralPath $Esp) {
        $resolved = (Resolve-Path -LiteralPath $Esp).Path
        Assert-EvidencePath $resolved 'Resolved QEMU ESP'
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $Esp 'EFI\BOOT') | Out-Null
    Copy-Item -LiteralPath $bootloaderPath -Destination (Join-Path $Esp 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Esp 'kernel.elf') -Force
    Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $Esp 'ramdisk.img') -Force
    if ($SettingsRecord) {
        Copy-Item -LiteralPath $SettingsRecord -Destination (Join-Path $Esp 'GXSETT.BIN') -Force
    }
}

function Clear-QemuEspCopies {
    foreach ($name in @('production-boot-01', 'production-boot-02', 'production-boot-03',
                        'ordinary-boot-01', 'ordinary-boot-02', 'ordinary-boot-03')) {
        $candidate = Join-Path (Join-Path $EvidenceRoot $name) 'ESP'
        Assert-EvidencePath $candidate 'QEMU ESP'
        if (Test-Path -LiteralPath $candidate) {
            $resolved = (Resolve-Path -LiteralPath $candidate).Path
            Assert-EvidencePath $resolved 'Resolved QEMU ESP'
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}

function Read-Qmp([System.IO.Stream]$Stream, [int]$Seconds = 4) {
    $builder = [System.Text.StringBuilder]::new()
    $buffer = [byte[]]::new(4096)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        if ($Stream.DataAvailable) {
            $count = $Stream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) {
                [void]$builder.Append([System.Text.Encoding]::ASCII.GetString($buffer, 0, $count))
                if ($builder.ToString().TrimEnd().EndsWith('}')) { break }
            }
        } else { Start-Sleep -Milliseconds 20 }
    }
    return $builder.ToString()
}

function Send-QmpEvents([object[]]$Events, [int]$DelayMilliseconds = 45) {
    if ($Events.Count -eq 0) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $script:activePort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 200
        [void](Read-Qmp $stream)
        $capabilities = [System.Text.Encoding]::ASCII.GetBytes('{"execute":"qmp_capabilities"}' + "`n")
        $stream.Write($capabilities, 0, $capabilities.Length); $stream.Flush()
        $capResponse = Read-Qmp $stream
        if ($capResponse -match '"error"') { throw "QMP capability negotiation failed: $capResponse" }
        foreach ($event in $Events) {
            $request = [ordered]@{
                execute = 'input-send-event'
                arguments = [ordered]@{ events = @($event) }
            } | ConvertTo-Json -Compress -Depth 10
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($request + "`n")
            $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
            $response = Read-Qmp $stream
            Add-Content -LiteralPath $script:activeQmpLog -Value ("event={0}`nresponse={1}" -f $request, $response) -Encoding ASCII
            if ($response -match '"error"') { throw "QMP input event failed: $response" }
            if ($DelayMilliseconds -gt 0) { Start-Sleep -Milliseconds $DelayMilliseconds }
        }
    } finally { $client.Dispose() }
}

function New-RelativeMove([int]$Dx, [int]$Dy) {
    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($axis in @(@{ Name = 'x'; Delta = $Dx }, @{ Name = 'y'; Delta = $Dy })) {
        $remaining = [int]$axis.Delta
        while ($remaining -ne 0) {
            $step = [Math]::Sign($remaining) * [Math]::Min([Math]::Abs($remaining), 40)
            $events.Add([ordered]@{ type = 'rel'; data = [ordered]@{ axis = $axis.Name; value = $step } })
            $remaining -= $step
        }
    }
    return $events.ToArray()
}

function New-Button([string]$Name, [bool]$Down) {
    return [ordered]@{ type = 'btn'; data = [ordered]@{ button = $Name; down = $Down } }
}

function New-Key([string]$Code, [bool]$Down) {
    return [ordered]@{ type = 'key'; data = [ordered]@{ down = $Down; key = [ordered]@{ type = 'qcode'; data = $Code } } }
}

function Get-Serial([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        if ($null -eq $raw) { return [string]::Empty }
        return [string]$raw
    }
    return [string]::Empty
}

function Wait-Serial([string]$Path, [string]$Pattern, [int]$After = 0,
                     [int]$Seconds = 30) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $text = Get-Serial $Path
        if ($text -match '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?C158-CALC-[^\r\n]*result=FAIL') {
            throw 'Managed Calculator reported a C158 failure marker.'
        }
        if ($text -match '(?m)^\[C158-CALC-APP-REGISTRY\][^\r\n]*result=FAIL') {
            throw 'The Native Calculator preservation or managed registration check failed.'
        }
        $tail = if ($After -le $text.Length) { $text.Substring($After) } else { '' }
        $match = [regex]::Match($tail, "(?m)$Pattern")
        if ($match.Success) { return [pscustomobject]@{ Text = $text; Match = $match; Index = $text.Length } }
        if ($script:activeProcess) {
            $script:activeProcess.Refresh()
            if ($script:activeProcess.HasExited) {
                $stderr = if ($script:activeStderr -and (Test-Path -LiteralPath $script:activeStderr)) {
                    [string](Get-Content -LiteralPath $script:activeStderr -Raw -ErrorAction SilentlyContinue)
                } else { '' }
                throw "QEMU exited with code $($script:activeProcess.ExitCode) while waiting for '$Pattern'. stderr=$stderr"
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for serial marker: $Pattern"
}

function Get-ClientPoint([string]$Serial) {
    $matches = [regex]::Matches($Serial, '(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move x=([0-9A-Fa-f]+) y=([0-9A-Fa-f]+) result=PASS')
    if ($matches.Count -eq 0) { throw 'C158 could not calibrate a real QMP pointer position.' }
    $match = $matches[$matches.Count - 1]
    return [pscustomobject]@{
        X = [Convert]::ToInt32($match.Groups[1].Value, 16)
        Y = [Convert]::ToInt32($match.Groups[2].Value, 16)
    }
}

function Move-Pointer([int]$X, [int]$Y) {
    if (-not $script:cursor) { throw 'C158 QMP pointer has not been calibrated.' }
    if ($X -eq $script:cursor.X -and $Y -eq $script:cursor.Y) { return }
    Send-QmpEvents (New-RelativeMove ($X - $script:cursor.X) ($Y - $script:cursor.Y)) 30
    $script:cursor = [pscustomobject]@{ X = $X; Y = $Y }
}

function Click-Screen([int]$X, [int]$Y) {
    Move-Pointer $X $Y
    # The bare-metal input loop can poll slower than adjacent QMP events;
    # leave the button state stable long enough for each transition to arrive.
    Send-QmpEvents @((New-Button 'left' $true), (New-Button 'left' $false)) 180
}

function Press-Key([string]$Code) {
    Send-QmpEvents @((New-Key $Code $true), (New-Key $Code $false)) 180
}

function Send-CalculatorLaunch([bool]$Initial, [int]$ScreenWidth,
                               [int]$ScreenHeight, [string]$SerialPath) {
    $menuCount = 20 # 16 pinned entries plus 4 recent slots in the bounded table.
    $menuHeight = 30 + $menuCount * 22 + 36
    $workHeight = $ScreenHeight - 40
    $menuY = [Math]::Max(0, $workHeight - $menuHeight)
    $contentY = $menuY + 31
    Click-Screen 50 ($ScreenHeight - 20)
    if ($Initial) {
        $footerY = $menuY + 30 + $menuCount * 22
        Click-Screen 70 ($footerY + 18)
        # All Programs item 11 is Managed Calculator in the sorted, fixed list.
        $calculatorY = $contentY + 11 * 22 + 11
    } else {
        # Managed Calculator is promoted into Recent Programs after a successful
        # App Model launch, so each relaunch remains an ordinary Start-menu click.
        $calculatorY = $contentY + 11
    }
    $before = (Get-Serial $SerialPath).Length
    Click-Screen 115 $calculatorY
    [void](Wait-Serial $SerialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.calculator selector=00000006 image=/system/apps/GXOSAPP\.ELF metadata=valid' $before 35)
    [void](Wait-Serial $SerialPath '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.calculator generation=[0-9A-Fa-f]+ selector=00000006 kind=normal' $before 35)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator selector=6 controls=18 capacity=18 focus=18 fresh=PASS result=PASS' $before 35)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=0 phase=0 commands=0 result=PASS' $before 15)
}

function Switch-CalculatorToNotes([int]$ScreenWidth, [int]$ScreenHeight,
                                 [string]$SerialPath) {
    $menuCount = 20
    $menuHeight = 30 + $menuCount * 22 + 36
    $workHeight = $ScreenHeight - 40
    $menuY = [Math]::Max(0, $workHeight - $menuHeight)
    $contentY = $menuY + 31
    Click-Screen 50 ($ScreenHeight - 20)
    $footerY = $menuY + 30 + $menuCount * 22
    Click-Screen 70 ($footerY + 18)
    # Managed Notes is fixed All Programs entry 12; launch it while Calculator
    # is active to prove ordinary one-surface replacement through the App Model.
    $notesY = $contentY + 12 * 22 + 11
    $before = (Get-Serial $SerialPath).Length
    Click-Screen 115 $notesY
    [void](Wait-Serial $SerialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.notes selector=00000004 image=/system/apps/GXOSAPP\.ELF metadata=valid' $before 35)
    [void](Wait-Serial $SerialPath '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ selector=00000004 kind=normal' $before 35)
    # C150 suppresses the detailed destroy marker while a surface is being
    # replaced; C111 records the close before the next C150 surface is created.
    [void](Wait-Serial $SerialPath '^\[C111-SURFACE\] action=close result=PASS' $before 20)
    [void](Wait-Serial $SerialPath '^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ result=PASS' $before 20)
}

function Get-CalculatorScreenPoint([int]$Index, [int]$ScreenWidth,
                                   [int]$ScreenHeight) {
    $columnX = @(44, 112, 180, 248)
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 292) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 270) / 2))
    $row = [int][Math]::Floor($Index / 4.0)
    $column = $Index % 4
    return [pscustomobject]@{
        X = $windowX + $columnX[$column]
        Y = $windowY + 24 + 80 + 36 * $row
    }
}

function Click-CalculatorButton([int]$Index, [int]$ScreenWidth,
                                [int]$ScreenHeight, [string]$SerialPath,
                                [string]$ExpectedDisplay) {
    $point = Get-CalculatorScreenPoint $Index $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    Click-Screen $point.X $point.Y
    $pattern = '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=' +
        [regex]::Escape($ExpectedDisplay) + ' phase=\d+ commands=\d+ result=PASS'
    [void](Wait-Serial $SerialPath $pattern $before 20)
}

function Wait-Display([string]$SerialPath, [string]$Display, [int]$After,
                      [int]$Seconds = 20) {
    $pattern = '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=' +
        [regex]::Escape($Display) + ' phase=\d+ commands=\d+ result=PASS'
    return Wait-Serial $SerialPath $pattern $After $Seconds
}

function Send-Text([string]$Text) {
    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($character in $Text.ToCharArray()) {
        $code = [string]$character
        $events.Add((New-Key $code $true))
        $events.Add((New-Key $code $false))
    }
    if ($events.Count -gt 0) { Send-QmpEvents $events.ToArray() 110 }
}

function Send-ShiftedPlus {
    Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'equal' $true),
        (New-Key 'equal' $false), (New-Key 'shift' $false)) 110
}

function Close-Calculator([string]$SerialPath) {
    $before = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Key 'alt' $true), (New-Key 'f4' $true),
        (New-Key 'f4' $false), (New-Key 'alt' $false)) 110
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 active=none modal=none popup=none drag=none result=PASS' $before 30)
    [void](Wait-Serial $SerialPath '^\[C158-CALC-CLOSE-DISPATCH\] selector=6 controls=0 result=PASS' $before 15)
    [void](Wait-Serial $SerialPath '^\[C150-SURFACE\] action=destroy appId=com\.guidexos\.apps\.managed\.calculator generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ reason=close-or-replace' $before 15)
}

function Stop-Qemu([int]$Port, [System.Diagnostics.Process]$Process,
                   [string]$LogPath) {
    if ($Process.HasExited) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        [void](Read-Qmp $stream)
        $caps = [System.Text.Encoding]::ASCII.GetBytes('{"execute":"qmp_capabilities"}' + "`n")
        $stream.Write($caps, 0, $caps.Length); $stream.Flush(); [void](Read-Qmp $stream)
        $quit = [System.Text.Encoding]::ASCII.GetBytes('{"execute":"quit"}' + "`n")
        $stream.Write($quit, 0, $quit.Length); $stream.Flush()
        Add-Content -LiteralPath $LogPath -Value 'shutdown=quit' -Encoding ASCII
    } finally { $client.Dispose() }
    Wait-Process -Id $Process.Id -Timeout 12 -ErrorAction SilentlyContinue
}

function Start-Qemu([string]$SerialPath, [string]$StdoutPath,
                    [string]$StderrPath, [string]$Esp,
                    [string]$Qemu, [string]$Ovmf) {
    $port = Get-AvailableQmpPort
    $qmpLog = [System.IO.Path]::ChangeExtension($SerialPath, '.qmp.log')
    Remove-Item -LiteralPath $SerialPath,$StdoutPath,$StderrPath,$qmpLog -Force -ErrorAction SilentlyContinue
    $arguments = @(
        '-accel','tcg,thread=single','-machine','pc','-smp','1',
        '-drive',('if=pflash,format=raw,readonly=on,file="{0}"' -f $Ovmf),
        '-drive',('file=fat:rw:"{0}",format=raw,if=ide,index=0' -f $Esp),
        '-m','1024M','-vga','std','-display','none',
        '-serial',('file:"{0}"' -f $SerialPath),
        '-qmp',("tcp:127.0.0.1:{0},server,nowait" -f $port),
        '-boot','order=c','-no-reboot','-no-shutdown','-rtc','base=utc,clock=host')
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
    $script:activeProcess = $process
    $script:activeStderr = $StderrPath
    $script:activeSerial = $SerialPath
    $script:activePort = $port
    $script:activeQmpLog = $qmpLog
    $script:cursor = $null
    return [pscustomobject]@{ Process = $process; Port = $port; QmpLog = $qmpLog }
}

function Invoke-ProductionBoot([int]$Number, [string]$Scenario,
                               [string]$Qemu, [string]$Ovmf,
                               [string]$SettingsRecord) {
    $root = Join-Path $EvidenceRoot ("production-boot-{0:D2}" -f $Number)
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $esp = Join-Path $root 'ESP'
    Stage-Esp $esp $proofKernel $proofRamdisk $SettingsRecord
    $serial = Join-Path $root 'serial.log'
    $session = Start-Qemu $serial (Join-Path $root 'qemu.stdout.log') `
        (Join-Path $root 'qemu.stderr.log') $esp $Qemu $Ovmf
    $process = $session.Process
    try {
        Write-Host ("C158 production boot {0}/3: waiting for the Notes baseline." -f $Number)
        [void](Wait-Serial $serial '^\[desktop\] bare-metal desktop icon init completed' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[NATIVEAOT-PRODUCTION-LAUNCH\] applicationId=com\.guidexos\.apps\.managed\.notes recordId=com\.guidexos\.apps\.managed\.notes image=/system/apps/GXOSAPP\.ELF selector=00000004' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C158-CALC-APP-REGISTRY\] identity=com\.guidexos\.apps\.managed\.calculator selector=6 display=Managed-Calculator native-calculator=preserved result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C156-MODIFIER-DECODE cases=10 .*result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT-ROUTING cases=15 .*result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C150-MANAGED-OUTPUT\] C150-MANAGED-LIFECYCLE-TESTS cases=8 fresh=PASS active=one result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C150-RETURN-TARGET-TESTS\] cases=10 capacity=1 identity=canonical self=reject invalid=reject result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C157-NEW-TESTS cases=\d+ result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C154-REGRESSIONS clipboard=PASS result=PASS' 0 $TimeoutSeconds)
        $screen = Get-Serial $serial
        $widthMatch = [regex]::Match($screen, '(?m)^\[DESKTOP CAP\] framebuffer_width=0x([0-9A-Fa-f]+)')
        $heightMatch = [regex]::Match($screen, '(?m)^\[DESKTOP CAP\] framebuffer_height=0x([0-9A-Fa-f]+)')
        if (-not $widthMatch.Success -or -not $heightMatch.Success) { throw 'C158 could not read the real desktop dimensions.' }
        $screenWidth = [Convert]::ToInt32($widthMatch.Groups[1].Value, 16)
        $screenHeight = [Convert]::ToInt32($heightMatch.Groups[1].Value, 16)
        $calibrationStart = (Get-Serial $serial).Length
        Send-QmpEvents (New-RelativeMove 1 1) 50
        $calibrated = Wait-Serial $serial '^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS' $calibrationStart 20
        $notesClientPoint = Get-ClientPoint $calibrated.Text
        # C138 reports client-local pointer coordinates. Translate them once to
        # framebuffer coordinates so later moves may cross out of the Notes
        # window to the Start menu and back into the centered Calculator.
        $notesWindowX = [Math]::Max(0, [int](($screenWidth - 600) / 2))
        $notesWindowY = [Math]::Max(0, [int](($screenHeight - 360) / 2))
        $script:cursor = [pscustomobject]@{
            X = $notesClientPoint.X + $notesWindowX
            Y = $notesClientPoint.Y + $notesWindowY + 24
        }
        Write-Host ("C158 production boot {0}/3: launching Calculator through Start menu." -f $Number)
        Send-CalculatorLaunch $true $screenWidth $screenHeight $serial
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-CORE cases=\d+ state-bytes=21 result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-UI cases=\d+ pointer=PASS keyboard=PASS focus=PASS exact-once=PASS result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-REGISTRY cases=\d+ entries=3 controls=18 cap=18 teardown=PASS relaunch=PASS result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-LIFECYCLE cycles=25 fresh=PASS one-surface=PASS registration=PASS capture=none clipboard=unchanged result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-STRESS commands=\d+ bounded=PASS no-wrap=PASS result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-FOCUS events=\d+ index=valid exact-once=PASS modifiers=clear result=PASS' 0 30)

        if ($Scenario -eq 'pointer-arithmetic') {
            Click-CalculatorButton 12 $screenWidth $screenHeight $serial '1'
            Click-CalculatorButton 13 $screenWidth $screenHeight $serial '12'
            Click-CalculatorButton 15 $screenWidth $screenHeight $serial '12'
            Click-CalculatorButton 14 $screenWidth $screenHeight $serial '3'
            Click-CalculatorButton 8 $screenWidth $screenHeight $serial '34'
            Click-CalculatorButton 17 $screenWidth $screenHeight $serial '46'
            Click-CalculatorButton 0 $screenWidth $screenHeight $serial '0'
            Click-CalculatorButton 10 $screenWidth $screenHeight $serial '6'
            Click-CalculatorButton 7 $screenWidth $screenHeight $serial '6'
            Click-CalculatorButton 4 $screenWidth $screenHeight $serial '7'
            Click-CalculatorButton 17 $screenWidth $screenHeight $serial '42'
            Switch-CalculatorToNotes $screenWidth $screenHeight $serial
        } elseif ($Scenario -eq 'keyboard-focus') {
            $before = (Get-Serial $serial).Length
            Press-Key '7'; Send-QmpEvents @((New-Key 'shift' $true),
                (New-Key '8' $true), (New-Key '8' $false), (New-Key 'shift' $false)) 110
            Press-Key '8'; Press-Key 'ret'
            [void](Wait-Display $serial '56' $before 20)
            $before = (Get-Serial $serial).Length
            Press-Key 'esc'
            [void](Wait-Display $serial '0' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key '9'; Press-Key 'slash'; Press-Key '2'; Press-Key 'ret'
            [void](Wait-Display $serial '4' $before 20)
            $before = (Get-Serial $serial).Length
            Send-Text '123'; Press-Key 'backspace'
            [void](Wait-Display $serial '12' $before 20)
            Click-CalculatorButton 2 $screenWidth $screenHeight $serial '-12'
            $before = (Get-Serial $serial).Length
            Press-Key 'tab'
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-FOCUS active=4 shift=false result=PASS' $before 12)
            $before = (Get-Serial $serial).Length
            Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'tab' $true),
                (New-Key 'tab' $false), (New-Key 'shift' $false)) 110
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-FOCUS active=3 shift=true result=PASS' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key 'spc'
            [void](Wait-Display $serial '12' $before 12)
            # C156 Control+9 is transported but has no Calculator command.
            $before = (Get-Serial $serial).Length
            Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key '9' $true),
                (New-Key '9' $false), (New-Key 'ctrl' $false)) 110
            Start-Sleep -Milliseconds 250
            $tail = (Get-Serial $serial).Substring($before)
            if ($tail -notmatch '(?m)^\[C156-NATIVE-INPUT\] kind=key-down code=00000039 control=1' -or
                $tail -match '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE ') {
                throw 'Unsupported Ctrl+9 produced a Calculator command or was not transported.'
            }
            Close-Calculator $serial
        } elseif ($Scenario -eq 'error-recovery-lifecycle') {
            $before = (Get-Serial $serial).Length
            Press-Key '7'; Press-Key 'slash'; Press-Key '0'; Press-Key 'ret'
            [void](Wait-Display $serial 'Divide by zero' $before 20)
            $before = (Get-Serial $serial).Length
            Press-Key '3'
            [void](Wait-Display $serial '3' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key 'esc'; Send-Text '9223372036854775807'
            Send-ShiftedPlus; Press-Key '1'; Press-Key 'ret'
            [void](Wait-Display $serial 'Overflow' $before 30)
            # Keep Equals focused for the keyboard Enter check below; boot 1
            # already exercises the pointer Clear button and its focus change.
            $before = (Get-Serial $serial).Length
            Press-Key 'esc'
            [void](Wait-Display $serial '0' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key '2'; Send-ShiftedPlus; Press-Key '3'; Press-Key 'ret'
            [void](Wait-Display $serial '5' $before 20)
            Close-Calculator $serial
            Send-CalculatorLaunch $false $screenWidth $screenHeight $serial

            # Twenty-five real launch/use/close cycles. Each new object begins
            # with 18 registrations, zero state, and one active managed window.
            for ($cycle = 1; $cycle -le 25; $cycle++) {
                $digit = [string](($cycle - 1) % 9 + 1)
                $before = (Get-Serial $serial).Length
                Press-Key $digit
                [void](Wait-Display $serial $digit $before 12)
                Close-Calculator $serial
                if ($cycle -lt 25) {
                    Send-CalculatorLaunch $false $screenWidth $screenHeight $serial
                }
            }
            Send-CalculatorLaunch $false $screenWidth $screenHeight $serial
            $full = Get-Serial $serial
            $launchCount = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ').Count
            $closeCount = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ').Count
            if ($launchCount -lt 27 -or $closeCount -lt 26) {
                throw "C158 actual lifecycle stress was incomplete: launches=$launchCount closes=$closeCount."
            }
        } else {
            throw "Unknown C158 scenario '$Scenario'."
        }

        $full = Get-Serial $serial
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-[^\r\n]*result=FAIL' -or
            $full -match '(?m)^\[C158-CALC-APP-REGISTRY\][^\r\n]*result=FAIL') {
            throw "C158 $Scenario boot contains failed Calculator proof evidence."
        }
        $controlDown = [regex]::Matches($full, '(?m)^\[C156-KEYBOARD\] event=control-left-down ').Count
        $controlUp = [regex]::Matches($full, '(?m)^\[C156-KEYBOARD\] event=control-left-up ').Count
        $shiftDown = [regex]::Matches($full, '(?m)^\[C129-KEYBOARD\] shift=down side=left ').Count
        $shiftUp = [regex]::Matches($full, '(?m)^\[C129-KEYBOARD\] shift=up side=left ').Count
        if ($controlDown -ne $controlUp -or $shiftDown -ne $shiftUp) {
            throw "C158 $Scenario boot left a keyboard modifier unbalanced."
        }
        Stop-Qemu $session.Port $process $session.QmpLog
        $process.Refresh()
        $result = [pscustomobject]@{
            Boot = $Number
            Scenario = $Scenario
            Status = 'PASS'
            SerialPath = $serial
            SerialSha256 = Get-Hash $serial
            ProofKernelSha256 = Get-Hash $proofKernel
            ProofRamdiskSha256 = Get-Hash $proofRamdisk
            CalculatorLaunches = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ').Count
            CalculatorCloses = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ').Count
            ControlDown = $controlDown
            ControlUp = $controlUp
            ShiftDown = $shiftDown
            ShiftUp = $shiftUp
            ExitCode = $process.ExitCode
        }
        Remove-Item -LiteralPath $esp -Recurse -Force
        return $result
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
        $script:activeProcess = $null
        $script:activeStderr = $null
        $script:activeSerial = $null
        $script:activePort = 0
        $script:activeQmpLog = $null
    }
}

function Invoke-OrdinaryBoot([int]$Number, [string]$Qemu, [string]$Ovmf) {
    $root = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}" -f $Number)
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $esp = Join-Path $root 'ESP'
    Stage-Esp $esp $proofBackup $ramdiskBackup
    $serial = Join-Path $root 'serial.log'
    $session = Start-Qemu $serial (Join-Path $root 'qemu.stdout.log') `
        (Join-Path $root 'qemu.stderr.log') $esp $Qemu $Ovmf
    $process = $session.Process
    try {
        [void](Wait-Serial $serial '^\[desktop\] bare-metal desktop icon init completed' 0 $TimeoutSeconds)
        $text = Get-Serial $serial
        if ($text -notmatch '(?m)^\[KERNEL\] Boot method: UEFI BootInfo' -or
            $text -match 'PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure|C158-CALC') {
            throw "C158 ordinary restoration boot $Number failed or contains C158 proof output."
        }
        Stop-Qemu $session.Port $process $session.QmpLog
        $process.Refresh()
        $result = [pscustomobject]@{
            Boot = $Number
            Status = 'PASS'
            SerialPath = $serial
            SerialSha256 = Get-Hash $serial
            KernelSha256 = Get-Hash (Join-Path $esp 'kernel.elf')
            RamdiskSha256 = Get-Hash (Join-Path $esp 'ramdisk.img')
            ExitCode = $process.ExitCode
        }
        Remove-Item -LiteralPath $esp -Recurse -Force
        return $result
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
        $script:activeProcess = $null
        $script:activeStderr = $null
        $script:activeSerial = $null
        $script:activePort = 0
        $script:activeQmpLog = $null
    }
}

function Restore-CanonicalFiles {
    if ($proofBackup -and (Test-Path -LiteralPath $proofBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $proofBackup -Destination $kernelPath -Force
    }
    if ($espKernelBackup -and (Test-Path -LiteralPath $espKernelBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $espKernelBackup -Destination $espKernelPath -Force
    }
    if ($ramdiskBackup -and (Test-Path -LiteralPath $ramdiskBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $ramdiskBackup -Destination $protectedRamdiskPath -Force
    }
    if ($proofBackup -and (Test-Path -LiteralPath $proofBackup -PathType Leaf) -and
        $espKernelBackup -and (Test-Path -LiteralPath $espKernelBackup -PathType Leaf) -and
        $ramdiskBackup -and (Test-Path -LiteralPath $ramdiskBackup -PathType Leaf)) {
        $script:restored = (Get-Hash $kernelPath) -eq $canonicalKernelHash -and
            (Get-Hash $espKernelPath) -eq $espKernelHash -and
            (Get-Hash $protectedRamdiskPath) -eq $ramdiskHash
    }
}

function New-C151SettingsRecord([string]$Path) {
    [byte[]]$bytes = [byte[]]::new(26)
    [byte[]]$magic = [System.Text.Encoding]::ASCII.GetBytes('GXSC')
    [Array]::Copy($magic, 0, $bytes, 0, 4)
    $bytes[4] = 2; $bytes[6] = 10
    $bytes[12] = 0; $bytes[13] = 1; $bytes[14] = 0; $bytes[15] = 1
    $bytes[16] = 1; $bytes[17] = 1; $bytes[18] = 1; $bytes[19] = 0
    $bytes[20] = 0; $bytes[21] = 5
    [long]$crc = 4294967295L
    foreach ($value in $bytes[0..21]) {
        $crc = ($crc -bxor [long]$value) -band 4294967295L
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 1) -ne 0) { $crc = (($crc -shr 1) -bxor 3988292384L) -band 4294967295L }
            else { $crc = ($crc -shr 1) -band 4294967295L }
        }
    }
    $crc = ($crc -bxor 4294967295L) -band 4294967295L
    for ($index = 0; $index -lt 4; $index++) {
        $bytes[22 + $index] = [byte](($crc -shr ($index * 8)) -band 255)
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot, (Split-Path -Parent $proofBackup) | Out-Null
foreach ($path in @($kernelPath, $espKernelPath, $protectedRamdiskPath, $bootloaderPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "C158 required protected input is missing: $path" }
}
$canonicalKernelHash = Get-Hash $kernelPath
$espKernelHash = Get-Hash $espKernelPath
$ramdiskHash = Get-Hash $protectedRamdiskPath
if ($canonicalKernelHash -ne $espKernelHash) {
    throw 'Canonical kernel and ESP kernel differ; no proof assets were changed.'
}
Copy-Item -LiteralPath $kernelPath -Destination $proofBackup -Force
Copy-Item -LiteralPath $espKernelPath -Destination $espKernelBackup -Force
Copy-Item -LiteralPath $protectedRamdiskPath -Destination $ramdiskBackup -Force
if ((Get-Hash $proofBackup) -ne $canonicalKernelHash -or
    (Get-Hash $espKernelBackup) -ne $espKernelHash -or
    (Get-Hash $ramdiskBackup) -ne $ramdiskHash) {
    throw 'C158 could not verify byte-identical protected backups.'
}
Clear-QemuEspCopies

trap {
    if ($script:activeProcess) {
        $script:activeProcess.Refresh()
        if (-not $script:activeProcess.HasExited) {
            Stop-Process -Id $script:activeProcess.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $script:activeProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    Restore-CanonicalFiles
    throw $_
}

$python = if ($PythonExe) { $PythonExe } else {
    Get-Tool 'python' @('C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe', 'C:\Python312\python.exe', 'C:\Python311\python.exe')
}
$qemu = Get-Tool 'qemu-system-x86_64' @('C:\Program Files\qemu\qemu-system-x86_64.exe')
$ovmf = Get-Tool 'ovmf' @('C:\Program Files\qemu\share\edk2-x86_64-code.fd')
$make = Get-Tool 'mingw32-make' @('C:\mingw64\bin\mingw32-make.exe')

$managedBuild = Join-Path $RepoRoot 'scripts\dotnet\build-managed-hostlog-proof.ps1'
Invoke-Checked 'powershell' @('-ExecutionPolicy','Bypass','-File',$managedBuild,
    '-RepoRoot',$RepoRoot,'-OutputRoot',$compositeRoot,
    '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
    '-RuntimePackOutputRoot',$runtimePackOutput,'-UseGuideXosRuntimePack',
    '-ProductionApplication','-PersistentCompositeLifecycle','-AllocationMode','Allocating',
    '-ManagedProjectMode','C154Composite','-C155ManagedNotesSession',
    '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument',
    '-C158ManagedCalculator','-HeapConfiguration','Primary4MiB','-PythonExe',$python)
$compositeElf = Join-Path $compositeRoot 'artifacts\HostLogProof.elf'
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C158 NativeAOT ELF missing: $compositeElf" }
$compositeHash = Get-Hash $compositeElf
Write-Host "C158 production NativeAOT composite built: $compositeHash" -ForegroundColor Green

$generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
Invoke-Checked 'powershell' @('-ExecutionPolicy','Bypass','-File',$generator,
    '-OutputDir',$stageRoot,'-OutputImage',$proofRamdisk,
    '-C104AppAPath',$compositeElf,'-ProductionCompositeApplicationPath',$compositeElf,
    '-C114ManagedDirectoryServices','-C117ManagedTextArea','-C118ManagedListBox',
    '-C151ManagedOpenFileDialog','-C152ManagedNotesSaveWorkflow',
    '-C155ManagedNotesSession','-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument')
if (-not (Test-Path -LiteralPath $proofRamdisk -PathType Leaf)) { throw 'C158 proof ramdisk was not generated.' }
$proofRamdiskHash = Get-Hash $proofRamdisk

$flags = @(
    '-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION','-DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH',
    '-DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION','-DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES',
    '-DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES','-DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER',
    '-DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT','-DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA',
    '-DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX','-DGXOS_NATIVEAOT_C119_MANAGED_BUTTON',
    '-DGXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST','-DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX',
    '-DGXOS_NATIVEAOT_C122_MANAGED_LABEL','-DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR',
    '-DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON','-DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR',
    '-DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX','-DGXOS_NATIVEAOT_C127_MANAGED_PANEL',
    '-DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE','-DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT',
    '-DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX','-DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON',
    '-DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX','-DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING',
    '-DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU','-DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU',
    '-DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING','-DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR',
    '-DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT','-DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW',
    '-DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK','-DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK',
    '-DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX','-DGXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER',
    '-DGXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG','-DGXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE',
    '-DGXOS_NATIVEAOT_C147_RUNTIME_SETTINGS','-DGXOS_NATIVEAOT_C148_SETTINGS_V2',
    '-DGXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING','-DGXOS_NATIVEAOT_C150_MANAGED_APP_RETURN',
    '-DGXOS_NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG',
    '-DGXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW',
    '-DGXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION',
    '-DGXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS',
    '-DGXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT',
    '-DGXOS_NATIVEAOT_C158_MANAGED_CALCULATOR') -join ' '
if ($ReuseBuiltProofKernel) {
    if (-not (Test-Path -LiteralPath $proofKernel -PathType Leaf)) {
        throw "-ReuseBuiltProofKernel requires a prior proof kernel at $proofKernel"
    }
    Write-Host 'C158 reusing the already built and preserved C158 proof kernel.'
} else {
    Write-Host 'C158 building the production kernel with managed Calculator metadata and launch routing.'
    Invoke-Checked $make @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',"EXTRA_CFLAGS=$flags",'-B')
    if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw 'C158 native kernel build did not produce kernel.elf.' }
    Copy-Item -LiteralPath $kernelPath -Destination $proofKernel -Force
}
$proofKernelHash = Get-Hash $proofKernel
$settingsRecord = Join-Path $EvidenceRoot 'GXSETT.BIN'
New-C151SettingsRecord $settingsRecord
Write-Host "C158 proof kernel SHA-256: $proofKernelHash"

$production = [System.Collections.Generic.List[object]]::new()
$production.Add((Invoke-ProductionBoot 1 'pointer-arithmetic' $qemu $ovmf $settingsRecord)) | Out-Null
$production.Add((Invoke-ProductionBoot 2 'keyboard-focus' $qemu $ovmf $settingsRecord)) | Out-Null
$production.Add((Invoke-ProductionBoot 3 'error-recovery-lifecycle' $qemu $ovmf $settingsRecord)) | Out-Null

Restore-CanonicalFiles
if (-not $script:restored) { throw 'C158 failed byte-for-byte restoration before ordinary boots.' }
Write-Host 'C158 protected kernel and ramdisk hashes restored; beginning ordinary boots.' -ForegroundColor Green
$ordinary = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    Write-Host ("C158 ordinary restoration boot {0}/3." -f $boot)
    $ordinary.Add((Invoke-OrdinaryBoot $boot $qemu $ovmf)) | Out-Null
}
if ((Get-Hash $kernelPath) -ne $canonicalKernelHash -or
    (Get-Hash $espKernelPath) -ne $espKernelHash -or
    (Get-Hash $protectedRamdiskPath) -ne $ramdiskHash) {
    throw 'C158 ordinary boots changed a restored protected artifact.'
}

$productionRecords = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    $serial = Join-Path $EvidenceRoot ("production-boot-{0:D2}\serial.log" -f $boot)
    $text = Get-Serial $serial
    if ($boot -eq 3) {
        $launchMatches = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ')
        $closeMatches = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ')
        $instances = [regex]::Matches($text, '(?m)^\[C150-MANAGED-OUTPUT\] C150-APP-INSTANCE id=6 generation=(\d+)')
        $launches = [regex]::Matches($text, '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.calculator generation=([0-9A-Fa-f]+) selector=00000006 kind=normal')
        $lastState = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=([^\r\n]+)')
        if ($launchMatches.Count -lt 27 -or $closeMatches.Count -lt 26 -or
            $instances.Count -lt 27 -or $launches.Count -lt 27 -or
            $lastState.Count -eq 0 -or $lastState[$lastState.Count - 1].Groups[1].Value -notmatch '^0 phase=0 commands=0 result=PASS$') {
            throw 'C158 final boot is missing 25 actual launch/close cycles or a fresh zero state.'
        }
        $script:finalCalculatorGeneration = [uint32]$instances[$instances.Count - 1].Groups[1].Value
        $script:finalNativeLaunchGeneration = $launches[$launches.Count - 1].Groups[1].Value
    }
    $productionRecords.Add([ordered]@{
        Boot = $boot
        Scenario = @('pointer-arithmetic','keyboard-focus','error-recovery-lifecycle')[$boot - 1]
        Status = 'PASS'
        SerialPath = $serial
        SerialSha256 = Get-Hash $serial
        CalculatorLaunches = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ').Count
        CalculatorCloses = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ').Count
        ProofKernelSha256 = $proofKernelHash
        ProofRamdiskSha256 = $proofRamdiskHash
    }) | Out-Null
}
$ordinaryRecords = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    $serial = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}\serial.log" -f $boot)
    $ordinaryRecords.Add([ordered]@{
        Boot = $boot
        Status = 'PASS'
        SerialPath = $serial
        SerialSha256 = Get-Hash $serial
        KernelSha256 = $canonicalKernelHash
        RamdiskSha256 = $ramdiskHash
    }) | Out-Null
}

$boot2Text = Get-Serial (Join-Path $EvidenceRoot 'production-boot-02\serial.log')
$boot3Text = Get-Serial (Join-Path $EvidenceRoot 'production-boot-03\serial.log')
$controlDownEvents = [regex]::Matches($boot2Text, '(?m)^\[C156-KEYBOARD\] event=control-(?:left|right)-down ')
$controlUpEvents = [regex]::Matches($boot2Text, '(?m)^\[C156-KEYBOARD\] event=control-(?:left|right)-up ')
$lastControlAggregate = [regex]::Matches($boot2Text,
    '(?m)^\[C156-KEYBOARD\] event=control-(?:left|right)-(?:down|up) [^\r\n]*aggregate=([01])')
$shiftDownEvents = [regex]::Matches($boot3Text, '(?m)^\[C129-KEYBOARD\] shift=down side=left ')
$shiftUpEvents = [regex]::Matches($boot3Text, '(?m)^\[C129-KEYBOARD\] shift=up side=left ')
$lastShiftAggregate = [regex]::Matches($boot3Text,
    '(?m)^\[C129-KEYBOARD\] shift=(?:down|up) side=left aggregate=([01])')
$controlBalanced = $controlDownEvents.Count -gt 0 -and
    $controlDownEvents.Count -eq $controlUpEvents.Count -and
    $lastControlAggregate.Count -gt 0 -and
    $lastControlAggregate[$lastControlAggregate.Count - 1].Groups[1].Value -eq '0'
$shiftBalanced = $shiftDownEvents.Count -gt 0 -and
    $shiftDownEvents.Count -eq $shiftUpEvents.Count -and
    $lastShiftAggregate.Count -gt 0 -and
    $lastShiftAggregate[$lastShiftAggregate.Count - 1].Groups[1].Value -eq '0'
if (-not $controlBalanced -or -not $shiftBalanced) {
    throw 'C158 production keyboard modifier events are unbalanced or finish pressed.'
}
$manifest = [ordered]@{
    schemaVersion = 1
    phase = 'C158'
    outcome = 'A'
    branch = 'v1.1_DOTNET_SUPPORT'
    application = [ordered]@{
        displayName = 'Managed Calculator'
        applicationId = 'com.guidexos.apps.managed.calculator'
        selector = 6
        managedRegistryCount = '2 -> 3 fixed descriptors'
        startMenuPinnedEntries = '15 -> 16'
        allProgramsEntries = '18 -> 19'
        nativeCalculatorId = 'gxos.builtin.calculator'
        nativeCalculatorPreserved = $true
        managedAppSwitch = 'Notes -> Calculator -> Notes through Start Menu; Calculator surface replaced'
        controlCount = 18
        perAppControlCapacity = 18
        sharedControlHostMaximum = '10 -> 20'
        statePayloadBytes = 21
        numericType = 'signed Int64 only'
        maximumEntryDigits = 19
        division = 'integer truncation toward zero'
        precedence = 'left-to-right immediate evaluation'
        repeatedEquals = 'no-op; retain current result'
        clipboard = 'GuideXosClipboard.Shared unchanged across Calculator test, launch, and teardown'
        lifecycleCloseAction = 'existing Host ABI action dispatch; ABI unchanged'
    }
    nativeAot = [ordered]@{
        compositeElf = $compositeElf
        compositeSha256 = $compositeHash
        proofKernel = $proofKernel
        proofKernelSha256 = $proofKernelHash
        proofRamdisk = $proofRamdisk
        proofRamdiskSha256 = $proofRamdiskHash
        heap = 'Primary4MiB'
        abiVersion = 1
        abiTableBytes = 104
        settingsFormatVersion = 2
        floatingPointCalculatorSupport = $false
        c128LifecycleSuite = 'unverified; no result claimed'
    }
    regressions = [ordered]@{
        C156 = 'modifier decode 10/10; shortcut routing 15/15; live Calculator Ctrl+9 ignored; modifier balance verified'
        C129 = 'focused 31/31 passed; standalone runner timed out before QMP input marker; production Calculator Tab and Shift+Tab passed with Shift released'
        C150 = 'managed lifecycle 8/8; canonical return target 10/10; Calculator clean close/fresh launch and Notes -> Calculator -> Notes replacement verified'
        C154 = 'clipboard test PASS; Calculator lifetime kept shared clipboard unchanged'
        C157 = 'Notes New-document suite PASS before App Model transition'
        ButtonFocus = 'C158 focused routing suite covers all 18 controls, exact-once Space/Enter behavior, Tab and Shift+Tab'
    }
    productionBoots = @($productionRecords.ToArray())
    ordinaryBoots = @($ordinaryRecords.ToArray())
    restoration = [ordered]@{
        canonicalKernelBefore = $canonicalKernelHash
        espKernelBefore = $espKernelHash
        protectedRamdiskBefore = $ramdiskHash
        canonicalKernelAfter = Get-Hash $kernelPath
        espKernelAfter = Get-Hash $espKernelPath
        protectedRamdiskAfter = Get-Hash $protectedRamdiskPath
        restoredByteForByte = $script:restored
        proofMediaIsolated = $true
    }
    finalState = [ordered]@{
        calculatorManagedGeneration = $script:finalCalculatorGeneration
        nativeLaunchGenerationHex = $script:finalNativeLaunchGeneration
        activeApplicationId = 'com.guidexos.apps.managed.calculator'
        display = '0'
        phase = 'EnteringLeft'
        registeredControls = 18
        control = 'released'
        controlEventsBalanced = $controlBalanced
        shift = 'released'
        shiftEventsBalanced = $shiftBalanced
        modalOwner = 'none'
        popupCapture = 'none'
        dragOwner = 'none'
        settingsFormatVersion = 2
        hostAbiVersion = 1
        hostAbiTableBytes = 104
    }
}
$manifestPath = Join-Path $EvidenceRoot 'c158-proof-manifest.json'
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
$ordinaryManifest = [ordered]@{
    schemaVersion = 1
    phase = 'C158-ordinary-restoration'
    status = 'PASS'
    protected = $manifest.restoration
    ordinaryBoots = @($ordinaryRecords.ToArray())
}
$ordinaryManifestPath = Join-Path $EvidenceRoot 'c158-ordinary-restoration-manifest.json'
$ordinaryManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ordinaryManifestPath -Encoding ASCII
Write-Host "C158 outcome=A production=3/3 ordinary=3/3 evidence=$EvidenceRoot manifest=$manifestPath ordinaryManifest=$ordinaryManifestPath" -ForegroundColor Green
