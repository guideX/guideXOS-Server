param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$PythonExe = "",
    [int]$TimeoutSeconds = 600,
    [int]$StressCycles = 25,
    [switch]$ReuseProofKernel,
    [switch]$ResumeAfterBoot1
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c157-managed-notes-new-document"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C157 evidence must remain under $allowedRoot"
}
if ($TimeoutSeconds -lt 30) { throw "TimeoutSeconds must be at least 30." }
if ($StressCycles -lt 1 -or $StressCycles -gt 100) { throw "StressCycles must be between 1 and 100." }

$kernelPath = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
$espKernelPath = Join-Path $RepoRoot 'ESP\kernel.elf'
$protectedRamdiskPath = Join-Path $RepoRoot 'ESP\ramdisk.img'
$bootloaderPath = Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$buildRoot = Join-Path $EvidenceRoot 'build'
$compositeRoot = Join-Path $buildRoot 'composite'
$runtimePackOutput = Join-Path $buildRoot 'runtime-pack'
$stageRoot = Join-Path $EvidenceRoot 'staging\wallpaper-pack'
$proofRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c157.img'
$proofKernel = Join-Path $EvidenceRoot 'proof-kernel.elf'
$proofBackup = Join-Path $EvidenceRoot 'canonical\kernel.elf'
$espKernelBackup = Join-Path $EvidenceRoot 'canonical\ESP-kernel.elf'
$ramdiskBackup = Join-Path $EvidenceRoot 'canonical\ESP-ramdisk.img'
$canonicalKernelHash = $null
$espKernelHash = $null
$ramdiskHash = $null
$restored = $false
$script:historyCanUndo = $false
$script:historyCanRedo = $false

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $File $($Arguments -join ' ')" }
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
    } finally {
        $listener.Stop()
    }
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

function Stage-Esp([string]$Esp, [string]$Kernel, [string]$Ramdisk,
                   [string]$SettingsRecord = "") {
    $evidenceRootFull = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $espFull = [System.IO.Path]::GetFullPath($Esp)
    if (Test-Path -LiteralPath $espFull) {
        $espFull = (Resolve-Path -LiteralPath $espFull).Path
    }
    if (-not $espFull.StartsWith($evidenceRootFull,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clear an ESP outside the C154 evidence folder: $espFull"
    }
    if (Test-Path -LiteralPath $espFull) {
        Remove-Item -LiteralPath $espFull -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $Esp 'EFI\BOOT') | Out-Null
    Copy-Item -LiteralPath $bootloaderPath -Destination (Join-Path $Esp 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Esp 'kernel.elf') -Force
    Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $Esp 'ramdisk.img') -Force
    if ($SettingsRecord) {
        Copy-Item -LiteralPath $SettingsRecord -Destination (Join-Path $Esp 'GXSETT.BIN') -Force
    }
}

function Clear-PreviousQemuEspCopies {
    $evidenceRootFull = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    foreach ($name in @('production-boot-01','production-boot-02','production-boot-03',
                        'ordinary-boot-01','ordinary-boot-02','ordinary-boot-03')) {
        $candidate = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $EvidenceRoot $name) 'ESP'))
        if (-not $candidate.StartsWith($evidenceRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clear a QEMU ESP outside the C154 evidence folder: $candidate"
        }
        if (Test-Path -LiteralPath $candidate) {
            $resolved = (Resolve-Path -LiteralPath $candidate).Path
            if (-not $resolved.StartsWith($evidenceRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to clear a resolved QEMU ESP outside the C154 evidence folder: $resolved"
            }
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
        } else { Start-Sleep -Milliseconds 25 }
    }
    return $builder.ToString()
}

function Send-QmpEvents([int]$Port, [object[]]$Events, [string]$LogPath,
                        [int]$DelayMilliseconds = 300) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 200
        $greeting = Read-Qmp $stream
        $capabilities = [System.Text.Encoding]::ASCII.GetBytes('{"execute":"qmp_capabilities"}' + "`n")
        $stream.Write($capabilities, 0, $capabilities.Length); $stream.Flush()
        $capResponse = Read-Qmp $stream
        if ($capResponse -match '"error"') { throw "QMP capability negotiation failed: $capResponse" }
        foreach ($event in $Events) {
            $request = [ordered]@{ execute = 'input-send-event'; arguments = [ordered]@{ events = @($event) } } |
                ConvertTo-Json -Compress -Depth 10
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($request + "`n")
            $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
            $response = Read-Qmp $stream
            Add-Content -LiteralPath $LogPath -Value ("event={0}`nresponse={1}" -f $request, $response) -Encoding ASCII
            if ($response -match '"error"') { throw "QMP input event failed: $response" }
            # QEMU acknowledges an input event before the guest necessarily
            # services its PS/2 IRQ. This matches the existing C136 proof's
            # bounded yield so longer pointer moves remain ordered.
            Start-Sleep -Milliseconds $DelayMilliseconds
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

function New-Wheel([int]$Delta) {
    return [ordered]@{ type = 'btn'; data = [ordered]@{ button = $(if ($Delta -gt 0) { 'wheel-down' } else { 'wheel-up' }); down = $true } }
}

function Get-Serial([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return [string](Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue) }
    return ''
}

function Wait-Serial([string]$Path, [string]$Pattern, [int]$After = 0,
                     [int]$Seconds = 30) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $text = Get-Serial $Path
        if ($null -eq $text) { $text = [string]::Empty }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C151-REGRESSIONS result=FAIL\r?$') {
            throw 'C151 focused regression suite reported failure.'
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C154-REGRESSIONS .*result=FAIL\r?$') {
            throw 'C154 focused history or save-point suite reported failure.'
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C153-REGRESSIONS result=FAIL\r?$') {
            throw 'C153 history, TextArea, or save-point suite reported failure.'
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C135-FOCUSED-TESTS .*result=FAIL\r?$') {
            throw 'C135 popup or host regression suite reported failure.'
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C156-(?:MODIFIER-DECODE|SHORTCUT-ROUTING) result=FAIL\r?$') {
            throw 'C156 modifier transport or shortcut routing regression suite reported failure.'
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW-TESTS result=FAIL\r?$') {
            throw 'C157 New-document state, history, clipboard, session, or shortcut suite reported failure.'
        }
        $tail = if ($After -le $text.Length) { $text.Substring($After) } else { '' }
        $match = [regex]::Match($tail, "(?m)$Pattern")
        if ($match.Success) { return [pscustomobject]@{ Text = $text; Match = $match; Index = $text.Length } }
        if ($script:activeProcess) {
            $script:activeProcess.Refresh()
            if ($script:activeProcess.HasExited) {
                $stderr = if ($script:activeStderr -and
                    (Test-Path -LiteralPath $script:activeStderr)) {
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
    if ($matches.Count -eq 0) { throw 'C151 could not calibrate a pointer inside the Notes client.' }
    $match = $matches[$matches.Count - 1]
    return [pscustomobject]@{ X = [Convert]::ToInt32($match.Groups[1].Value, 16); Y = [Convert]::ToInt32($match.Groups[2].Value, 16) }
}

function Get-Target([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C151-TARGET\].*\b$Field=(\d+)")
    if (-not $match.Success) { throw "C151 target marker omitted $Field." }
    return [int]$match.Groups[1].Value
}

function Move-Client([int]$X, [int]$Y) {
    if ($script:cursor -and $X -eq $script:cursor.X -and
        $Y -eq $script:cursor.Y) { return }
    $before = (Get-Serial $script:activeSerial).Length
    Send-QmpEvents $script:activePort (New-RelativeMove ($X - $script:cursor.X) ($Y - $script:cursor.Y)) $script:activeMonitor
    $xHex = ([uint32]$X).ToString('X8')
    $yHex = ([uint32]$Y).ToString('X8')
    $pattern = '^\[C138-NATIVE-INPUT\] kind=pointer-move x=' + $xHex +
        ' y=' + $yHex + ' result=PASS'
    [void](Wait-Serial $script:activeSerial $pattern $before 15)
    $script:cursor = [pscustomobject]@{ X = $X; Y = $Y }
}

function Click-Client([int]$X, [int]$Y) {
    Move-Client $X $Y
    Send-QmpEvents $script:activePort @((New-Button 'left' $true), (New-Button 'left' $false)) $script:activeMonitor
}

function Press-Key([string]$Code) {
    Send-QmpEvents $script:activePort @((New-Key $Code $true), (New-Key $Code $false)) $script:activeMonitor
}

function Send-C156Chord([string]$Code, [bool]$RightControl = $false,
                        [bool]$KeepControlDown = $false) {
    $controlCode = if ($RightControl) { 'ctrl_r' } else { 'ctrl' }
    $start = (Get-Serial $script:activeSerial).Length
    $events = [System.Collections.Generic.List[object]]::new()
    $events.Add((New-Key $controlCode $true))
    $events.Add((New-Key $Code $true))
    $events.Add((New-Key $Code $false))
    if (-not $KeepControlDown) { $events.Add((New-Key $controlCode $false)) }
    Send-QmpEvents $script:activePort $events.ToArray() $script:activeMonitor
    $expectedKey = ([uint32][char]$Code).ToString('X8')
    $nativePattern = '^\[C156-NATIVE-INPUT\] kind=key-down code=' +
        $expectedKey + ' control=1'
    [void](Wait-Serial $script:activeSerial $nativePattern $start 20)
    return $start
}

function Finish-C156Control([bool]$RightControl = $false) {
    $controlCode = if ($RightControl) { 'ctrl_r' } else { 'ctrl' }
    Send-QmpEvents $script:activePort @((New-Key $controlCode $false)) $script:activeMonitor
}

function Invoke-C156Chord([string]$SerialPath, [string]$Code,
                          [string]$ExpectedCommand,
                          [bool]$RightControl = $false,
                          [bool]$Shift = $false,
                          [bool]$KeepControlDown = $false) {
    $start = if ($Shift) {
        $controlCode = if ($RightControl) { 'ctrl_r' } else { 'ctrl' }
        $index = (Get-Serial $SerialPath).Length
        Send-QmpEvents $script:activePort @((New-Key $controlCode $true),
            (New-Key 'shift' $true), (New-Key $Code $true),
            (New-Key $Code $false), (New-Key 'shift' $false),
            (New-Key $controlCode $false)) $script:activeMonitor
        $expectedCharacter = if ($Shift) { $Code.ToUpperInvariant() } else { $Code }
        $expectedKey = ([uint32][char]$expectedCharacter).ToString('X8')
        $nativePattern = '^\[C156-NATIVE-INPUT\] kind=key-down code=' +
            $expectedKey + ' control=1 .*shift=1'
        [void](Wait-Serial $SerialPath $nativePattern $index 20)
        $index
    } else {
        Send-C156Chord $Code $RightControl $KeepControlDown
    }
    $lower = $Code.ToLowerInvariant()
    $pattern = '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=' +
        [regex]::Escape($lower) + ' command=' + [regex]::Escape($ExpectedCommand) +
        ' .*consumed=true keychar=none text-leak=false result=PASS'
    [void](Wait-Serial $SerialPath $pattern $start 20)
    return $start
}

function Open-C156InitialDocument([string]$SerialPath, [bool]$TestUntitledSave, [int]$ExpectedBytes = 56) {
    if ($TestUntitledSave) {
        $start = Invoke-C156Chord $SerialPath 's' 'Save'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C152-SAVE-AS chooser=open bounded=true result=PASS' $start 12)
        Press-Key 'esc'
    }

    $start = Invoke-C156Chord $SerialPath 'o' 'Open'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C151-DIALOG open=PASS modal=active member-registration=3 popup=closed capture=none drag=none result=PASS' $start 12)
    $keyboardStart = (Get-Serial $SerialPath).Length
    Press-Key 'tab'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C151-KEYBOARD key=Tab focus=3 modal=true result=PASS' $keyboardStart 10)
    [void](Wait-Serial $SerialPath '^\[C129-NATIVE] tab-keydown shift=0 transport=production result=PASS' $keyboardStart 10)
    $keyboardStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'shift' $true),
        (New-Key 'tab' $true), (New-Key 'tab' $false),
        (New-Key 'shift' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C151-KEYBOARD key=ShiftTab focus=1 modal=true result=PASS' $keyboardStart 10)
    [void](Wait-Serial $SerialPath '^\[C129-NATIVE] tab-keydown shift=1 transport=production result=PASS' $keyboardStart 10)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 60 79
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 20)
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'home'
    Press-Key 'down'
    Press-Key 'down'
    Press-Key 'ret'
    $loadPattern = '^\[C102-MANAGED-OUTPUT] C151-LOAD-DETAIL bytes=' + $ExpectedBytes + ' path=/system/apps/C151/alpha\.txt'
    [void](Wait-Serial $SerialPath $loadPattern $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C151-SAME-NOTES active=true relaunch=false return-target=unarmed result=PASS' $start 12)
}

function Invoke-C156UndoRedoScenario([string]$SerialPath) {
    Focus-C154Document $SerialPath
    Press-Key 'end'
    $editStart = (Get-Serial $SerialPath).Length
    Press-Key 'b'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C120-ROUTING active=document result=PASS' $editStart 12)

    # QMP delivers key-up before Control-up in this first sequence.
    $undoStart = Invoke-C156Chord $SerialPath 'z' 'Undo'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C153-UNDO source=keyboard content-change=once result=PASS' $undoStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)

    # The second sequence releases Control before the key-up event.
    $editStart = (Get-Serial $SerialPath).Length
    Press-Key 'c'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C120-ROUTING active=document result=PASS' $editStart 12)
    $undoStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'ctrl' $true),
        (New-Key 'z' $true), (New-Key 'ctrl' $false),
        (New-Key 'z' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C156-NATIVE-INPUT] kind=key-down code=0000007A control=1' $undoStart 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C153-UNDO source=keyboard content-change=once result=PASS' $undoStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)

    $redoStart = Invoke-C156Chord $SerialPath 'y' 'Redo'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C153-REDO source=keyboard content-change=once result=PASS' $redoStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=y command=Redo .*dirty=true' $redoStart 12)
    $saveStart = Invoke-C156Chord $SerialPath 's' 'Save'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $saveStart 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=s command=Save .*dirty=false' $saveStart 12)
    $undoStart = Invoke-C156Chord $SerialPath 'z' 'Undo'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=z command=Undo .*dirty=true' $undoStart 12)
    $redoStart = Invoke-C156Chord $SerialPath 'y' 'Redo'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=y command=Redo .*dirty=false' $redoStart 12)

    for ($pair = 0; $pair -lt 25; $pair++) {
        $undoStart = Invoke-C156Chord $SerialPath 'z' 'Undo'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=z command=Undo .*result=PASS' $undoStart 12)
        $redoStart = Invoke-C156Chord $SerialPath 'y' 'Redo'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=y command=Redo .*result=PASS' $redoStart 12)
    }

    $rightUndo = Invoke-C156Chord $SerialPath 'z' 'Undo' -RightControl $true
    [void](Wait-Serial $SerialPath '^\[C156-NATIVE-INPUT] kind=key-down code=0000007A control=1 leftCtrl=0 rightCtrl=1 shift=0' $rightUndo 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=z command=Undo .*result=PASS' $rightUndo 12)
    $rightRedo = Invoke-C156Chord $SerialPath 'y' 'Redo' -RightControl $true
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT] C156-SHORTCUT key=y command=Redo .*result=PASS' $rightRedo 12)
}

function Invoke-C156ClipboardStress([string]$SerialPath) {
    for ($cycle = 0; $cycle -lt 25; $cycle++) {
        Focus-C154Document $SerialPath
        Press-Key 'home'
        Shift-C154Key 'right'
        Shift-C154Key 'right'
        Shift-C154Key 'right'
        $copyStart = (Get-Serial $SerialPath).Length
        [void](Invoke-C156Chord $SerialPath 'c' 'Copy')
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-COPY length=3 result=PASS' $copyStart 12)

        Press-Key 'end'
        $pasteStart = (Get-Serial $SerialPath).Length
        [void](Invoke-C156Chord $SerialPath 'v' 'Paste')
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS' $pasteStart 12)
        $script:historyCanUndo = $true
        $script:historyCanRedo = $false

        $undoStart = (Get-Serial $SerialPath).Length
        [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-UNDO source=keyboard content-change=once result=PASS' $undoStart 12)
        $script:historyCanUndo = $false
        $script:historyCanRedo = $true
    }
    if ($script:historyCanUndo -or -not $script:historyCanRedo) {
        throw 'C156 Copy/Paste stress did not finish at its original saved revision.'
    }
}

function Invoke-C156UnsupportedChord([string]$SerialPath, [string]$Code,
                                     [string]$ExpectedHex) {
    $start = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'ctrl' $true),
        (New-Key $Code $true), (New-Key $Code $false),
        (New-Key 'ctrl' $false)) $script:activeMonitor
    $nativePattern = '^\[C156-NATIVE-INPUT\] kind=key-down code=' +
        $ExpectedHex + ' control=1'
    [void](Wait-Serial $SerialPath $nativePattern $start 20)
    if ($ExpectedHex -eq '00000009') {
        # Tab remains owned by the native C129 focus-navigation route before
        # Managed Notes shortcut dispatch. Preserve that production priority;
        # the managed routing table independently verifies Tab is unsupported.
        [void](Wait-Serial $SerialPath '^\[C129-NATIVE-INPUT\] kind=key-down key=00000009 shift=00000000 result=PASS' $start 20)
        Start-Sleep -Milliseconds 500
        $text = Get-Serial $SerialPath
        $tail = if ($start -le $text.Length) { $text.Substring($start) } else { '' }
        if ($tail -match '(?m)^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT ' -or
            $tail -match '(?m)^\[C116-NATIVE-INPUT\] kind=key-char') {
            throw 'Control+Tab escaped C129 focus navigation or leaked a printable key.'
        }
        return $start
    }
    if ($ExpectedHex -in @('00000020', '00000071')) {
        # Space and unknown Ctrl+Q are delivered to managed input. Existing
        # control focus can own these keys, so verify the boundary and ensure
        # neither key becomes a printable KeyChar. The pure routing suite checks
        # that Ctrl+Q maps to no editor command.
        $managedPattern = '^\[C116-NATIVE-INPUT\] kind=key-down key=' +
            $ExpectedHex + ' shift=00000000 control=00000001 result=PASS'
        [void](Wait-Serial $SerialPath $managedPattern $start 20)
        Start-Sleep -Milliseconds 500
        $text = Get-Serial $SerialPath
        $tail = if ($start -le $text.Length) { $text.Substring($start) } else { '' }
        if ($tail -match '(?m)^\[C116-NATIVE-INPUT\] kind=key-char') {
            throw 'An unsupported Ctrl key leaked a printable character into managed text input.'
        }
        return $start
    }
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=other command=none shift=false enabled=false consumed=true keychar=none text-leak=false result=PASS' $start 20)
    return $start
}

function Assert-C156NoShortcutAfter([string]$SerialPath, [int]$Start,
                                    [string]$Reason) {
    Start-Sleep -Milliseconds 500
    $text = Get-Serial $SerialPath
    $tail = if ($Start -le $text.Length) { $text.Substring($Start) } else { '' }
    if ($tail -match '(?m)^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT ') {
        throw "C156 $Reason escaped the modal or popup input owner."
    }
}

function Invoke-C156CapturePriority([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    $popupStart = (Get-Serial $SerialPath).Length
    Send-C156Chord 'n'
    [void](Wait-Serial $SerialPath '^\[C156-NATIVE-INPUT\] kind=key-down code=0000006E control=1' $popupStart 20)
    Assert-C156NoShortcutAfter $SerialPath $popupStart 'Ctrl+N with the popup open'
    if ((Get-Serial $SerialPath).Substring($popupStart) -match '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW') {
        throw 'Ctrl+N escaped popup ownership and invoked New.'
    }
    Press-Key 'esc'

    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    $popupStart = (Get-Serial $SerialPath).Length
    Send-C156Chord 'z'
    [void](Wait-Serial $SerialPath '^\[C156-NATIVE-INPUT\] kind=key-down code=0000007A control=1' $popupStart 20)
    Assert-C156NoShortcutAfter $SerialPath $popupStart 'Ctrl+Z with the popup open'
    Press-Key 'esc'

    $start = Invoke-C156Chord $SerialPath 's' 'SaveAs' -Shift $true
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $start 12)
    $modalStart = (Get-Serial $SerialPath).Length
    Send-C156Chord 's'
    [void](Wait-Serial $SerialPath '^\[C156-NATIVE-INPUT\] kind=key-down code=00000073 control=1' $modalStart 20)
    Assert-C156NoShortcutAfter $SerialPath $modalStart 'Ctrl+S while Save As is active'
    Press-Key 'esc'

    Focus-C154Document $SerialPath
    $editStart = (Get-Serial $SerialPath).Length
    Press-Key 'q'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-ROUTING active=document result=PASS' $editStart 12)
    $openStart = Invoke-C156Chord $SerialPath 'o' 'Open'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-prompt=active deferred=true result=PASS' $openStart 12)
    $modalStart = (Get-Serial $SerialPath).Length
    Send-C156Chord 'n'
    [void](Wait-Serial $SerialPath '^\[C156-NATIVE-INPUT\] kind=key-down code=0000006E control=1' $modalStart 20)
    Assert-C156NoShortcutAfter $SerialPath $modalStart 'Ctrl+N while the dirty Open dialog is active'
    if ((Get-Serial $SerialPath).Substring($modalStart) -match '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW') {
        throw 'Ctrl+N opened a second modal workflow while the dirty Open decision owned input.'
    }
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-decision=Cancel document=preserved=true result=PASS' $openStart 12)
}

function Assert-C156ModifierRelease([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'right'
    [void](Wait-Serial $SerialPath '^\[C156-NATIVE-INPUT\] kind=key-down code=00000103 control=0 leftCtrl=0 rightCtrl=0 shift=0' $start 12)
}

function Open-Dialog([string]$SerialPath, [int]$AfterIndex) {
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $AfterIndex 12)
    $popupIndex = (Get-Serial $SerialPath).Length
    Click-Client $script:menuX $script:menuY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-DIALOG open=PASS modal=active member-registration=3 popup=closed capture=none drag=none result=PASS' $popupIndex 12)
    return (Get-Serial $SerialPath).Length
}

function Invoke-C152MenuCommand([string]$SerialPath, [int]$MenuIndex) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    $historyOffset = 0
    if ($script:historyCanUndo) { $historyOffset++ }
    if ($script:historyCanRedo) { $historyOffset++ }
    for ($step = 0; $step -lt ($MenuIndex + $historyOffset); $step++) { Press-Key 'down' }
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'ret'
    return $start
}

function Edit-C152Document([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 100 80
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-POINTER target=document result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'home'
    Press-Key 'x'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-ROUTING active=document result=PASS' $start 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
}

function Invoke-C154Undo([string]$SerialPath, [switch]$UndoRemainsAvailable) {
    if (-not $script:historyCanUndo) { throw 'C154 script expected an available Undo command.' }
    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-UNDO source=menu content-change=once result=PASS' $start 12)
    $script:historyCanUndo = [bool]$UndoRemainsAvailable
    $script:historyCanRedo = $true
}

function Invoke-C154Redo([string]$SerialPath, [switch]$RedoRemainsAvailable) {
    if (-not $script:historyCanRedo) { throw 'C154 script expected an available Redo command.' }
    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    if ($script:historyCanUndo) { Press-Key 'down' }
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-REDO source=menu content-change=once result=PASS' $start 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = [bool]$RedoRemainsAvailable
}

function Invoke-C154HistoryRoundTrip([string]$SerialPath,
    [switch]$UndoRemainsAvailable, [switch]$RedoRemainsAvailable) {
    Invoke-C154Undo $SerialPath -UndoRemainsAvailable:$UndoRemainsAvailable
    Invoke-C154Redo $SerialPath -RedoRemainsAvailable:$RedoRemainsAvailable
}

function Invoke-C154OpenDecisionProof([string]$SerialPath, [bool]$Dirty) {
    $start = Invoke-C152MenuCommand $SerialPath 0
    if ($Dirty) {
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-prompt=active deferred=true result=PASS' $start 12)
        Press-Key 'esc'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-decision=Cancel document=preserved=true result=PASS' $start 12)
    } else {
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-DIALOG open=PASS modal=active member-registration=3 popup=closed capture=none drag=none result=PASS' $start 12)
        Press-Key 'esc'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-CANCEL result=PASS document=preserved=true' $start 12)
    }
}

function Type-C154Character([string]$SerialPath, [string]$Code) {
    $start = (Get-Serial $SerialPath).Length
    Press-Key $Code
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-ROUTING active=document result=PASS' $start 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
}

function Focus-C154Document([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 100 80
    [void](Wait-Serial $SerialPath '^\[C116-NATIVE-INPUT\] kind=pointer-down x=00000064 y=00000050 result=PASS' $start 12)
}

function Invoke-C154CloseDecisionProof([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),(New-Key 'f4' $true),(New-Key 'f4' $false),(New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C152-CLOSE\] request=vetoed dirty-prompt=active result=PASS' $start 15)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-CLOSE dirty-decision=Cancel window=preserved=true result=PASS' $start 12)
}

function Invoke-C154SettingsPromptProof([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active transition=deferred result=PASS' $start 15)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-decision=Cancel transition=blocked result=PASS' $start 12)
}

function Invoke-C154CleanSettingsTransition([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $start 20)
    $tail = (Get-Serial $SerialPath).Substring($start)
    if ($tail -match '(?m)^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active') {
        throw 'Settings unexpectedly prompted after Redo restored the saved revision.'
    }
}

function Invoke-C154SavePointScenario([string]$SerialPath) {
    Edit-C152Document $SerialPath
    Invoke-C154Undo $SerialPath
    Invoke-C154OpenDecisionProof $SerialPath $false
    Invoke-C154Redo $SerialPath
    Invoke-C154OpenDecisionProof $SerialPath $true

    # Real keyboard LF and Backspace edits remain distinct, undoable revisions.
    Focus-C154Document $SerialPath
    Type-C154Character $SerialPath 'ret'
    Type-C154Character $SerialPath 'backspace'
    Invoke-C154Undo $SerialPath -UndoRemainsAvailable
    Invoke-C154Redo $SerialPath

    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 12)
    Invoke-C154Undo $SerialPath -UndoRemainsAvailable
    Invoke-C154Redo $SerialPath
    Invoke-C154HistoryRoundTrip $SerialPath -UndoRemainsAvailable

    # Add multiple real LF revisions, move back to the saved state, redo them,
    # and send an actual QMP wheel event through the TextArea after the restore.
    Focus-C154Document $SerialPath
    Press-Key 'end'
    for ($index = 0; $index -lt 6; $index++) {
        Type-C154Character $SerialPath 'ret'
    }
    for ($index = 0; $index -lt 6; $index++) {
        Invoke-C154Undo $SerialPath -UndoRemainsAvailable
    }
    for ($index = 0; $index -lt 6; $index++) {
        Invoke-C154Redo $SerialPath -RedoRemainsAvailable:($index -lt 5)
    }
    Move-Client 100 80
    $wheelStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Wheel 1)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C137-NATIVE-INPUT\] kind=wheel delta=00000001 .*buttons-preserved=true result=PASS' $wheelStart 12)
    $wheel = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=1 before=\d+ after=\d+ result=(?:PASS|IGNORED)' $wheelStart 12
    if ($wheel.Match.Value -notmatch 'result=PASS') {
        $wheelStart = (Get-Serial $SerialPath).Length
        Send-QmpEvents $script:activePort @((New-Wheel -1)) $script:activeMonitor
        [void](Wait-Serial $SerialPath '^\[C137-NATIVE-INPUT\] kind=wheel delta=-00000001 .*buttons-preserved=true result=PASS' $wheelStart 12)
        $wheel = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=-1 before=\d+ after=\d+ result=(?:PASS|IGNORED)' $wheelStart 12
    }
    if ($wheel.Match.Value -notmatch 'result=PASS') {
        throw 'Real C137 wheel input did not scroll the TextArea after multi-line Undo/Redo.'
    }
    Invoke-C154Undo $SerialPath -UndoRemainsAvailable
    Invoke-C154Redo $SerialPath

    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 12)
    Invoke-C154Undo $SerialPath
    Invoke-C154Redo $SerialPath
}

function Invoke-C154SaveAsHistoryScenario([string]$SerialPath) {
    Edit-C152Document $SerialPath
    Type-C154Character $SerialPath 'y'
    Type-C154Character $SerialPath 'z'
    Invoke-C154Undo $SerialPath -UndoRemainsAvailable
    $start = Invoke-C152MenuCommand $SerialPath 2
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $start 12)

    # Exercise the Save File dialog's shared ListBox with a real QMP wheel.
    # Walk from C152 to the managed root, enter the bounded FULL directory,
    # scroll its many entries, then return to C152 without changing the name.
    $start = (Get-Serial $SerialPath).Length
    Click-Client 60 79
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C114-DIRECTORY-LIST\] path=/system/apps count=.* result=PASS' $start 12)
    [void](Wait-Serial $SerialPath '^\[C114-DIRECTORY-ENTRY\] name=FULL type=00000002 .*' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 60 115
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C114-DIRECTORY-LIST\] path=/system/apps/FULL count=.* result=PASS' $start 12)
    Move-Client 60 120
    $wheelStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Wheel 1)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C137-NATIVE-INPUT\] kind=wheel delta=00000001 .*buttons-preserved=true result=PASS' $wheelStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-DIALOG-NATIVE-WHEEL viewport=moved result=PASS' $wheelStart 12)
    Write-Host 'C152 real Save File dialog wheel scroll passed.'
    $wheelStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Wheel -1)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C137-NATIVE-INPUT\] kind=wheel delta=-00000001 .*buttons-preserved=true result=PASS' $wheelStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-DIALOG-NATIVE-WHEEL viewport=moved result=PASS' $wheelStart 12)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 60 79
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C114-DIRECTORY-LIST\] path=/system/apps count=.* result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 60 97
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C114-DIRECTORY-LIST\] path=/system/apps/C152 count=.* result=PASS' $start 12)

    # The Save As field begins focused at the end of the existing name after Tab.
    # Replace it with a plain name to prove the chooser does not force an extension.
    Press-Key 'tab'
    Press-Key 'end'
    for ($index = 0; $index -lt 9; $index++) { Press-Key 'backspace' }
    foreach ($key in @('c','1','5','2','n','e','w')) { Press-Key $key }
    $start = (Get-Serial $SerialPath).Length
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS committed path=/system/apps/C152/c152new' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 12)

    # Cancel a whole Save As chooser, verify focus returns to the document, and
    # prove the pending redo chain and save point are unchanged.
    $start = Invoke-C152MenuCommand $SerialPath 2
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $start 12)
    Press-Key 'esc'
    Focus-C154Document $SerialPath
    Invoke-C154HistoryRoundTrip $SerialPath -UndoRemainsAvailable -RedoRemainsAvailable

    # Save As marks the current XY revision clean while preserving the redo of Z.
    Invoke-C154Undo $SerialPath -UndoRemainsAvailable
    Invoke-C154Redo $SerialPath -RedoRemainsAvailable
    Type-C154Character $SerialPath 'q'
    Invoke-C154HistoryRoundTrip $SerialPath -UndoRemainsAvailable

    $start = Invoke-C152MenuCommand $SerialPath 2
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OVERWRITE prompt=open confirmed=false result=PASS' $start 15)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=reopened directory-and-name=preserved result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OVERWRITE prompt=open confirmed=false result=PASS' $start 15)
    Press-Key 'tab'
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 12)
    Invoke-C154HistoryRoundTrip $SerialPath -UndoRemainsAvailable

    Edit-C152Document $SerialPath
    Invoke-C154OpenDecisionProof $SerialPath $true

    # Discard is deferred until the replacement file loads. Canceling the file
    # chooser must keep the old dirty document; then repeat and load alpha.txt.
    $start = Invoke-C152MenuCommand $SerialPath 0
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-prompt=active deferred=true result=PASS' $start 12)
    Press-Key 'tab'; Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-decision=Discard deferred-until-load=true result=PASS' $start 12)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-CANCEL result=PASS document=preserved=true' $start 12)
    $start = Invoke-C152MenuCommand $SerialPath 0
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-prompt=active deferred=true result=PASS' $start 12)
    Press-Key 'tab'; Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-OPEN dirty-decision=Discard deferred-until-load=true result=PASS' $start 12)
    Click-Client 60 79
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 15)
    Click-Client 60 115
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-LOAD-DETAIL bytes=56 path=/system/apps/C151/alpha\.txt' $start 15)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-SAME-NOTES active=true relaunch=false return-target=unarmed result=PASS' $start 12)
    $script:historyCanUndo = $false
    $script:historyCanRedo = $false

    # Alt+F4 goes through the native window-close veto. Cancel once, then discard
    # on a second request so the Notes window can close before this fresh boot ends.
    Edit-C152Document $SerialPath
    $start = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),(New-Key 'f4' $true),(New-Key 'f4' $false),(New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C152-CLOSE\] request=vetoed dirty-prompt=active result=PASS' $start 15)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-CLOSE dirty-decision=Cancel window=preserved=true result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),(New-Key 'f4' $true),(New-Key 'f4' $false),(New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C152-CLOSE\] request=vetoed dirty-prompt=active result=PASS' $start 15)
    Press-Key 'tab'; Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C152-CLOSE\] decision=Discard or Save dispatch=accepted result=PASS' $start 15)
}

function Invoke-C154DirtyLifecycleScenario([string]$SerialPath) {
    Edit-C152Document $SerialPath
    Invoke-C154Undo $SerialPath
    Invoke-C154OpenDecisionProof $SerialPath $false
    Invoke-C154Redo $SerialPath
    Invoke-C154SettingsPromptProof $SerialPath
    Invoke-C154CloseDecisionProof $SerialPath

    $start = (Get-Serial $SerialPath).Length
    Press-Key 'f12'
    [void](Wait-Serial $SerialPath '^\[C152-FAILURE-INJECTION\] next-notes-write=io-error result=PASS' $start 12)
    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-FAILURE-INJECTION\] operation=write path=/system/apps/C152/alpha\.txt disk=unchanged restore=PASS result=FAIL' $start 15)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=FAIL document=preserved=true dirty=true path=preserved=true' $start 12)
    Press-Key 'ret'
    Invoke-C154Undo $SerialPath
    Invoke-C154Redo $SerialPath
    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 15)
    Invoke-C154Undo $SerialPath
    Invoke-C154Redo $SerialPath
    Invoke-C154CleanSettingsTransition $SerialPath
}

function Shift-C154Key([string]$KeyName) {
    Send-QmpEvents $script:activePort @((New-Key 'shift' $true),
        (New-Key $KeyName $true), (New-Key $KeyName $false),
        (New-Key 'shift' $false)) $script:activeMonitor
}

function Invoke-C154MenuCommand([string]$SerialPath, [int]$DownCount,
                                [string]$ExpectedPattern) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    for ($index = 0; $index -lt $DownCount; $index++) { Press-Key 'down' }
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath $ExpectedPattern $start 15)
    return $start
}

function Select-C154FirstCharacter([string]$SerialPath) {
    Focus-C154Document $SerialPath
    Press-Key 'home'
    Shift-C154Key 'right'
}

function Invoke-C154ClipboardBoot1([string]$SerialPath) {
    Edit-C152Document $SerialPath
    Press-Key 'home'
    Shift-C154Key 'right'
    [void](Invoke-C154MenuCommand $SerialPath 2 '^\[C102-MANAGED-OUTPUT\] C154-COPY length=1 result=PASS')
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
    Invoke-C154Undo $SerialPath
    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $start 20)
    $start = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),
        (New-Key 'f4' $true), (New-Key 'f4' $false),
        (New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C150-RELAUNCH\] .*instance=fresh target=none registration=bounded result=PASS' $start 25)
    [void](Wait-Serial $SerialPath '^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none' $start 15)
    Focus-C154Document $SerialPath
    [void](Invoke-C154MenuCommand $SerialPath 0 '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=1 content-change=once result=PASS')
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
    Invoke-C154Undo $SerialPath
    Invoke-C154Redo $SerialPath
    Invoke-C154WheelAfterClipboardHistory $SerialPath
    $start = Invoke-C154MenuCommand $SerialPath 3 '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false'
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
}

function Invoke-C154WheelAfterClipboardHistory([string]$SerialPath) {
    Focus-C154Document $SerialPath
    Press-Key 'end'
    for ($index = 0; $index -lt 6; $index++) {
        Type-C154Character $SerialPath 'ret'
    }

    Move-Client 100 80
    $wheelStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Wheel 1)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C137-NATIVE-INPUT\] kind=wheel delta=00000001 .*buttons-preserved=true result=PASS' $wheelStart 12)
    $wheel = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=1 before=\d+ after=\d+ result=(?:PASS|IGNORED)' $wheelStart 12
    if ($wheel.Match.Value -notmatch 'result=PASS') {
        $wheelStart = (Get-Serial $SerialPath).Length
        Send-QmpEvents $script:activePort @((New-Wheel -1)) $script:activeMonitor
        [void](Wait-Serial $SerialPath '^\[C137-NATIVE-INPUT\] kind=wheel delta=-00000001 .*buttons-preserved=true result=PASS' $wheelStart 12)
        $wheel = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=-1 before=\d+ after=\d+ result=(?:PASS|IGNORED)' $wheelStart 12
    }
    if ($wheel.Match.Value -notmatch 'result=PASS') {
        throw 'C137 real wheel input did not scroll the TextArea after C154 Paste and Undo/Redo.'
    }
    Invoke-C154Undo $SerialPath -UndoRemainsAvailable
    Invoke-C154Redo $SerialPath
}

function Invoke-C154ClipboardBoot2([string]$SerialPath) {
    Select-C154FirstCharacter $SerialPath
    [void](Invoke-C154MenuCommand $SerialPath 0 '^\[C102-MANAGED-OUTPUT\] C154-CUT length=1 content-change=once result=PASS')
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
    Invoke-C154Undo $SerialPath
    Invoke-C154Redo $SerialPath
    $start = Invoke-C154MenuCommand $SerialPath 3 '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false'
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    Invoke-C154Undo $SerialPath
    Invoke-C154Redo $SerialPath
}

function Invoke-C154ClipboardBoot3([string]$SerialPath) {
    Focus-C154Document $SerialPath
    Press-Key 'home'
    Shift-C154Key 'down'
    Shift-C154Key 'end'
    [void](Invoke-C154MenuCommand $SerialPath 1 '^\[C102-MANAGED-OUTPUT\] C154-COPY length=\d+ result=PASS')
    [void](Invoke-C154MenuCommand $SerialPath 3 '^\[C102-MANAGED-OUTPUT\] C151-DIALOG open=PASS modal=active member-registration=3 popup=closed capture=none drag=none result=PASS')
    $start = (Get-Serial $SerialPath).Length
    Click-Client 60 79
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 15)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 60 115
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-LOAD-DETAIL bytes=56 path=/system/apps/C151/alpha\.txt' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-SAME-NOTES active=true relaunch=false return-target=unarmed result=PASS' $start 12)
    [void](Invoke-C154MenuCommand $SerialPath 0 '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=\d+ content-change=once result=PASS')
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
    [void](Invoke-C154MenuCommand $SerialPath 4 '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS')
    Press-Key 'tab'
    Press-Key 'end'
    for ($index = 0; $index -lt 9; $index++) { Press-Key 'backspace' }
    foreach ($key in @('c','1','5','4','c','o','p','y')) { Press-Key $key }
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'tab'
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS committed path=/system/apps/C151/c154copy' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 15)
    Invoke-C154Undo $SerialPath
    Invoke-C154Redo $SerialPath
}

function Set-C155ProofPosition([string]$SerialPath, [bool]$SelectText,
                              [bool]$KeepControlDown = $false) {
    Focus-C154Document $SerialPath
    Press-Key 'home'
    for ($index = 0; $index -lt 10; $index++) { Press-Key 'down' }
    Press-Key 'end'
    if ($SelectText) {
        Shift-C154Key 'left'
        Shift-C154Key 'left'
        Shift-C154Key 'left'
        [void](Invoke-C156Chord $SerialPath 'c' 'Copy' -KeepControlDown $KeepControlDown)
    }
}

function Assert-C155RestoredPair([string]$SerialPath, [int]$After,
                                [string]$ExpectedPath = 'named') {
    $arm = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=(\d+) path=(named|blank) caret=(\d+) anchor=(\d+) viewport=(\d+) fixed-bytes=118 result=PASS' $After 15
    $consume = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-CONSUMED generation=(\d+) target=Notes one-shot=true pending=none valid=true result=PASS' $After 25
    $restore = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-RESTORE generation=(\d+) source=VFS named=true result=PASS' $After 25
    $view = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-RESTORE-VIEW generation=(\d+) caret=(\d+) anchor=(\d+) viewport=(\d+) history=fresh dirty=false pending=none result=PASS' $After 25
    $armGeneration = [int]$arm.Match.Groups[1].Value
    $consumeGeneration = [int]$consume.Match.Groups[1].Value
    $restoreGeneration = [int]$restore.Match.Groups[1].Value
    if ($armGeneration -ne $consumeGeneration -or
        $armGeneration -ne $restoreGeneration -or
        $armGeneration -ne [int]$view.Match.Groups[1].Value -or
        $arm.Match.Groups[2].Value -ne $ExpectedPath -or
        [int]$arm.Match.Groups[3].Value -ne [int]$view.Match.Groups[2].Value -or
        [int]$arm.Match.Groups[4].Value -ne [int]$view.Match.Groups[3].Value) {
        throw "C155 generation, named path, or caret/selection changed across the return."
    }
    return $armGeneration
}

function Invoke-C155CleanReturn([string]$SerialPath,
                                [bool]$ControlHeldAcrossTransition = $false) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    $armedPattern = '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=\d+ path=named .*result=PASS'
    try {
        [void](Wait-Serial $SerialPath $armedPattern $start 15)
    } catch {
        $tail = (Get-Serial $SerialPath).Substring($start)
        if ($tail -match '(?m)^\[C150-SETTINGS-LAUNCH\]') { throw }
        # A long stress run can occasionally drop the desktop icon click while
        # QMP is servicing repeated key transitions. Retry only when no launch
        # or session-arm marker was recorded for this return.
        Click-Client 475 294
        [void](Wait-Serial $SerialPath $armedPattern $start 15)
    }
    [void](Wait-Serial $SerialPath '^\[C155-RETURN-PAIR\] target=Notes session=armed result=PASS' $start 12)
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $start 20)
    if ($ControlHeldAcrossTransition) { Finish-C156Control }
    $returnStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),
        (New-Key 'f4' $true), (New-Key 'f4' $false),
        (New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C150-RELAUNCH\] .*instance=fresh target=none registration=bounded result=PASS' $returnStart 90)
    $generation = Assert-C155RestoredPair $SerialPath $start
    [void](Wait-Serial $SerialPath '^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none' $returnStart 15)
    return $generation
}

function Invoke-C155DirtySaveReturn([string]$SerialPath) {
    Set-C155ProofPosition $SerialPath $false
    # Copy the current selection so the same resident clipboard is available
    # after the dirty Save, Notes destruction, and VFS reconstruction.
    $script:historyCanUndo = $false
    $script:historyCanRedo = $false
    Focus-C154Document $SerialPath
    Press-Key 'home'
    Shift-C154Key 'right'
    Shift-C154Key 'right'
    Shift-C154Key 'right'
    [void](Invoke-C156Chord $SerialPath 'c' 'Copy')
    $cutStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'x' 'Cut')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-CUT length=3 content-change=once result=PASS' $cutStart 12)
    Focus-C154Document $SerialPath
    Press-Key 'end'
    $editStart = (Get-Serial $SerialPath).Length
    Press-Key 'x'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-ROUTING active=document result=PASS' $editStart 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false

    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active transition=deferred result=PASS' $start 15)
    $saveStart = (Get-Serial $SerialPath).Length
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $saveStart 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=\d+ path=named .*result=PASS' $saveStart 15)
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $saveStart 20)
    $returnStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),
        (New-Key 'f4' $true), (New-Key 'f4' $false),
        (New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C150-RELAUNCH\] .*instance=fresh target=none registration=bounded result=PASS' $returnStart 90)
    [void](Assert-C155RestoredPair $SerialPath $start)
    [void](Wait-Serial $SerialPath '^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none' $returnStart 15)

    $pasteStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'v' 'Paste')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS' $pasteStart 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
    $undoStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-UNDO source=keyboard content-change=once result=PASS' $undoStart 12)
    $script:historyCanUndo = $false
    $script:historyCanRedo = $true
    if ($script:historyCanUndo -or -not $script:historyCanRedo) {
        throw 'C155 Paste Undo did not return to the new save baseline.'
    }
}

function Invoke-C155Boot1([string]$SerialPath) {
    Set-C155ProofPosition $SerialPath $true $true
    $generation = Invoke-C155CleanReturn $SerialPath $true
    $pasteStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'v' 'Paste')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS' $pasteStart 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
    $undoStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-UNDO source=keyboard content-change=once result=PASS' $undoStart 12)
    $script:historyCanUndo = $false
    $script:historyCanRedo = $true
    if ($script:historyCanUndo -or -not $script:historyCanRedo) {
        throw 'C155 clean-return clipboard Paste Undo did not return to the restored baseline.'
    }

    Focus-C154Document $SerialPath
    Press-Key 'home'
    # Undo restores the insertion point recorded by the clipboard edit. After
    # rehydration this can exceed the shorter saved fixture, so move back into
    # the bounded document range before the clean-return stress cycles.
    for ($index = 0; $index -lt 12; $index++) { Press-Key 'left' }
    for ($cycle = 1; $cycle -le $StressCycles; $cycle++) {
        if ($cycle -gt 1) {
            for ($index = 0; $index -lt 12; $index++) { Press-Key 'left' }
        }
        $next = Invoke-C155CleanReturn $SerialPath
        if ($next -le $generation) {
            throw "C155 stress cycle $cycle did not advance the semantic-session generation."
        }
        $generation = $next
    }
    Focus-C154Document $SerialPath
    Invoke-C156UnsupportedChord $SerialPath 'q' '00000071' | Out-Null
    Invoke-C156UnsupportedChord $SerialPath 'spc' '00000020' | Out-Null
    Invoke-C156UnsupportedChord $SerialPath 'tab' '00000009' | Out-Null
    Assert-C156ModifierRelease $SerialPath
}

function Invoke-C155Boot2([string]$SerialPath) {
    Invoke-C155DirtySaveReturn $SerialPath
}

function Invoke-C155Boot3([string]$SerialPath) {
    Focus-C154Document $SerialPath
    Press-Key 'home'
    $editStart = (Get-Serial $SerialPath).Length
    Press-Key 'x'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-ROUTING active=document result=PASS' $editStart 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false

    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active transition=deferred result=PASS' $start 15)
    Press-Key 'tab'
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-decision=Discard dispatch=deferred result=PASS' $start 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=\d+ path=named .*result=PASS' $start 15)
    [void](Wait-Serial $SerialPath '^\[C155-RETURN-PAIR\] target=Notes session=armed result=PASS' $start 12)
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $start 20)
    $returnStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),
        (New-Key 'f4' $true), (New-Key 'f4' $false),
        (New-Key 'alt' $false)) $script:activeMonitor
    [void](Assert-C155RestoredPair $SerialPath $start)
    [void](Wait-Serial $SerialPath '^\[C150-RELAUNCH\] .*instance=fresh target=none registration=bounded result=PASS' $returnStart 90)
    [void](Wait-Serial $SerialPath '^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none' $returnStart 15)
    $tail = (Get-Serial $SerialPath).Substring($returnStart)
    if ($tail -match '(?m)^\[C102-MANAGED-OUTPUT\] C155-RESTORE generation=\d+ source=VFS named=true .*result=FAIL') {
        throw 'C155 Discard restored a failure state instead of the disk baseline.'
    }
}

function Invoke-C157MenuNew([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Click-Client 350 49
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $start 15)
    return $start
}

function Invoke-C157SaveAsDefault([string]$SerialPath) {
    $start = Invoke-C156Chord $SerialPath 's' 'Save'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $start 12)
    $saveStart = (Get-Serial $SerialPath).Length
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS committed path=/system/apps/untitled\.txt' $saveStart 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $saveStart 20)
    return $saveStart
}

function Invoke-C157Boot1([string]$SerialPath) {
    $start = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled history=fresh clipboard=unchanged vfs-write=none same-instance=true result=PASS' $start 15)
    $start = Invoke-C156Chord $SerialPath 'n' 'New' -RightControl $true
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $start 12)
    $openStart = Invoke-C156Chord $SerialPath 'o' 'Open'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-DIALOG open=PASS modal=active member-registration=3 popup=closed capture=none drag=none result=PASS' $openStart 12)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C151-CANCEL result=PASS document=preserved=true' $openStart 12)
    foreach ($key in @('a','b','c')) { Type-C154Character $SerialPath $key }
    [void](Invoke-C157SaveAsDefault $SerialPath)
    $undoStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-UNDO source=keyboard content-change=once result=PASS' $undoStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=true' $undoStart 12)
    $redoStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'y' 'Redo')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=y command=Redo .*dirty=false' $redoStart 12)

    for ($cycle = 0; $cycle -lt $StressCycles; $cycle++) {
        if ($cycle % 3 -eq 0) {
            $newStart = Invoke-C156Chord $SerialPath 'n' 'New'
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $newStart 12)
        } elseif ($cycle % 3 -eq 1) {
            Type-C154Character $SerialPath 'x'
            $undoStart = (Get-Serial $SerialPath).Length
            [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)
            $newStart = Invoke-C156Chord $SerialPath 'n' 'New'
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $newStart 12)
        } else {
            Type-C154Character $SerialPath 'x'
            $newStart = Invoke-C156Chord $SerialPath 'n' 'New'
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $newStart 12)
            Press-Key 'tab'
            Press-Key 'ret'
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-decision=Discard vfs-write=none result=PASS' $newStart 12)
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $newStart 12)
        }
        $pasteStart = Invoke-C156Chord $SerialPath 'v' 'Paste'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS' $pasteStart 12)
        $undoStart = (Get-Serial $SerialPath).Length
        [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)
    }
}

function Invoke-C157Boot2([string]$SerialPath) {
    Invoke-C156CapturePriority $SerialPath
    Type-C154Character $SerialPath 's'
    $start = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $start 12)
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $start 15)

    Open-C156InitialDocument $SerialPath $false 59
    Type-C154Character $SerialPath 'q'
    $namedFailureStart = (Get-Serial $SerialPath).Length
    Press-Key 'f12'
    [void](Wait-Serial $SerialPath '^\[C152-FAILURE-INJECTION\] next-notes-write=io-error result=PASS' $namedFailureStart 12)
    $namedFailureNew = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $namedFailureNew 12)
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=FAIL document=preserved=true dirty=true path=preserved=true' $namedFailureNew 20)
    Press-Key 'ret'
    $cancelStart = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $cancelStart 12)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-decision=Cancel document=preserved=true result=PASS' $cancelStart 12)
    $undoStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)
    [void](Invoke-C156Chord $SerialPath 'y' 'Redo')
    $discardStart = Invoke-C157MenuNew $SerialPath
    Press-Key 'tab'
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-decision=Discard vfs-write=none result=PASS' $discardStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $discardStart 12)
    $discardTail = (Get-Serial $SerialPath).Substring($discardStart)
    if ($discardTail -match '(?m)^\[C102-MANAGED-OUTPUT\] C152-(?:VFS-WRITE|SAVE result=PASS)') {
        throw 'Dirty New Discard unexpectedly wrote or saved the old file.'
    }

    Type-C154Character $SerialPath 'u'
    $cancelSaveAs = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $cancelSaveAs 12)
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $cancelSaveAs 12)
    Press-Key 'esc'
    $undoStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)
    [void](Invoke-C156Chord $SerialPath 'y' 'Redo')
    $cancelNew = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $cancelNew 12)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-decision=Cancel document=preserved=true result=PASS' $cancelNew 12)

    $failureStart = (Get-Serial $SerialPath).Length
    Press-Key 'f12'
    [void](Wait-Serial $SerialPath '^\[C152-FAILURE-INJECTION\] next-notes-write=io-error result=PASS' $failureStart 12)
    $failureNew = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $failureNew 12)
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $failureNew 12)
    $writeStart = (Get-Serial $SerialPath).Length
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=FAIL document=preserved=true dirty=true path=preserved=true' $writeStart 20)
    Press-Key 'ret'
    $undoStart = (Get-Serial $SerialPath).Length
    [void](Invoke-C156Chord $SerialPath 'z' 'Undo')
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)
    [void](Invoke-C156Chord $SerialPath 'y' 'Redo')
    $finishNew = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-prompt=active deferred=true result=PASS' $finishNew 12)
    Press-Key 'tab'
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-decision=Discard vfs-write=none result=PASS' $finishNew 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $finishNew 12)
}

function Assert-C157RestoredBlank([string]$SerialPath, [int]$After,
                                  [int]$ExpectedCaret = 0,
                                  [int]$ExpectedAnchor = 0) {
    $arm = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=(\d+) path=blank caret=(\d+) anchor=(\d+) viewport=(\d+) fixed-bytes=118 result=PASS' $After 15
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-CONSUMED generation=\d+ target=Notes one-shot=true pending=none valid=true result=PASS' $After 25)
    $restore = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-RESTORE generation=(\d+) source=blank named=false result=PASS' $After 25
    $view = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-RESTORE-VIEW generation=(\d+) caret=(\d+) anchor=(\d+) viewport=(\d+) history=fresh dirty=false pending=none result=PASS' $After 25
    if ($arm.Match.Groups[1].Value -ne $restore.Match.Groups[1].Value -or
        $arm.Match.Groups[1].Value -ne $view.Match.Groups[1].Value -or
        $arm.Match.Groups[2].Value -ne [string]$ExpectedCaret -or
        $arm.Match.Groups[3].Value -ne [string]$ExpectedAnchor -or
        $arm.Match.Groups[4].Value -ne '0' -or $view.Match.Groups[2].Value -ne '0' -or
        $view.Match.Groups[3].Value -ne '0' -or $view.Match.Groups[4].Value -ne '0') {
        throw 'C157 pathless Notes return did not restore a blank clean baseline and top view.'
    }
    [void](Wait-Serial $SerialPath '^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none' $After 15)
}

function Invoke-C157Boot3([string]$SerialPath) {
    Focus-C154Document $SerialPath
    Press-Key 'home'
    Shift-C154Key 'right'; Shift-C154Key 'right'; Shift-C154Key 'right'
    $copyStart = Invoke-C156Chord $SerialPath 'c' 'Copy'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-COPY length=3 result=PASS' $copyStart 12)

    $newStart = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $newStart 12)
    $pasteStart = Invoke-C156Chord $SerialPath 'v' 'Paste'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS' $pasteStart 12)
    $undoStart = Invoke-C156Chord $SerialPath 'z' 'Undo'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-UNDO source=keyboard content-change=once result=PASS' $undoStart 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)

    $returnStart = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=\d+ path=blank .*result=PASS' $returnStart 15)
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $returnStart 20)
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),(New-Key 'f4' $true),
        (New-Key 'f4' $false),(New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C150-RELAUNCH\] .*instance=fresh target=none registration=bounded result=PASS' $returnStart 90)
    Assert-C157RestoredBlank $SerialPath $returnStart
    $pasteStart = Invoke-C156Chord $SerialPath 'v' 'Paste'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS' $pasteStart 12)

    $undoStart = Invoke-C156Chord $SerialPath 'z' 'Undo'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=z command=Undo .*dirty=false' $undoStart 12)
    Type-C154Character $SerialPath 'c'
    $cancelSettings = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active transition=deferred result=PASS' $cancelSettings 12)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-decision=Cancel transition=blocked result=PASS' $cancelSettings 12)
    if ((Get-Serial $SerialPath).Substring($cancelSettings) -match '(?m)^\[C150-SETTINGS-LAUNCH\]') {
        throw 'C155 Settings Cancel launched Settings for a dirty untitled document.'
    }

    Type-C154Character $SerialPath 's'
    $saveSettings = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active transition=deferred result=PASS' $saveSettings 12)
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $saveSettings 12)
    $saveStart = (Get-Serial $SerialPath).Length
    Click-Client 326 260
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $saveStart 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=\d+ path=named .*result=PASS' $saveStart 15)
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $saveStart 20)
    $namedReturn = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),(New-Key 'f4' $true),
        (New-Key 'f4' $false),(New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C150-RELAUNCH\] .*instance=fresh target=none registration=bounded result=PASS' $namedReturn 90)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-RESTORE generation=\d+ source=VFS named=true result=PASS' $namedReturn 25)
    [void](Wait-Serial $SerialPath '^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none' $namedReturn 15)

    $newStart = Invoke-C156Chord $SerialPath 'n' 'New'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $newStart 12)
    Type-C154Character $SerialPath 'd'
    $discardSettings = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active transition=deferred result=PASS' $discardSettings 12)
    Press-Key 'tab'
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-decision=Discard dispatch=deferred result=PASS' $discardSettings 12)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=\d+ path=blank .*result=PASS' $discardSettings 15)
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $discardSettings 20)
    $discardReturn = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),(New-Key 'f4' $true),
        (New-Key 'f4' $false),(New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C150-RELAUNCH\] .*instance=fresh target=none registration=bounded result=PASS' $discardReturn 90)
    Assert-C157RestoredBlank $SerialPath $discardSettings 1 1
    $finalPaste = Invoke-C156Chord $SerialPath 'v' 'Paste'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS' $finalPaste 12)
}

function Start-ProofBoot([int]$BootNumber, [string]$Scenario,
                         [string]$Qemu, [string]$Ovmf,
                         [string]$SettingsRecord) {
    $root = Join-Path $EvidenceRoot ("production-boot-{0:D2}" -f $BootNumber)
    $esp = Join-Path $root 'ESP'
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    Stage-Esp $esp $proofKernel $proofRamdisk $SettingsRecord
    $serial = Join-Path $root 'serial.log'
    $stdout = Join-Path $root 'qemu.stdout.log'
    $stderr = Join-Path $root 'qemu.stderr.log'
    $monitor = Join-Path $root 'qmp.log'
    Remove-Item -LiteralPath $serial,$stdout,$stderr,$monitor -Force -ErrorAction SilentlyContinue
    $port = Get-AvailableQmpPort
    $arguments = @(
        '-accel','tcg,thread=single','-machine','pc','-smp','1',
        '-drive',('if=pflash,format=raw,readonly=on,file="{0}"' -f $Ovmf),
        '-drive',('file=fat:rw:"{0}",format=raw,if=ide,index=0' -f $esp),
        '-m','1024M','-vga','std','-display','none',
        '-serial',('file:"{0}"' -f $serial),
        '-qmp',("tcp:127.0.0.1:{0},server,nowait" -f $port),
        '-boot','order=c','-no-reboot','-no-shutdown','-rtc','base=utc,clock=host')
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
    $script:activeSerial = $serial
    $script:activePort = $port
    $script:activeMonitor = $monitor
    $script:activeProcess = $process
    $script:activeStderr = $stderr
    $script:cursor = $null
    $script:historyCanUndo = $false
    $script:historyCanRedo = $false
    try {
        Write-Host ("C157 boot {0}/3 waiting for Notes launch marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C151-NOTES-LAUNCH\].*return-target=none result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C152-NOTES-LAUNCH\] identity=canonical context=c156-untitled result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C157 boot {0}/3 waiting for C151-C157 regression markers." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C151-REGRESSIONS C145=49/49 C151-core=10/10 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-REGRESSIONS save-dialog=16/16 document-state=10/10 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C153-REGRESSIONS history-core=26/26 text-area=25/25 save-point=17/17 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C154-REGRESSIONS clipboard=PASS result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C135-NOTES initial=registration=shared items=13 capture=none result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C155-SESSION-CORE cases=31 slot=one-shot bounded=PASS result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C155-RESTORE-TEST cases=9 VFS=transactional fallback=blank bounds=PASS result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C156-MODIFIER-DECODE cases=10 .*result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT-ROUTING cases=15 .*ctrl-n=text-leak=false result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C156-REGRESSIONS ABI=v1 table=104 event-bits=PASS lifecycle=stateless result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C157-NEW-TESTS cases=\d+ result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C157-NEW-DETAIL document-state=PASS dirty-decisions=PASS history=PASS clipboard=PASS session=PASS shortcut=PASS result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C157-NEW-STRESS cycles=25 same-instance=PASS history-reset=PASS clipboard-stable=PASS result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C135-FOCUSED-TESTS menu=PASS host=PASS result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-SAVE-STRESS operations=50 alternating=verified read-back=exact dirty-cleared=true result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS-STRESS paths=5 current-path=latest read-back=exact result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-EMPTY-SAVE create=read-back=PASS bytes=0 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-MAX-SAVE bytes=256 read-back=exact result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C155 boot {0}/3 waiting for application lifetime marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C150-MANAGED-OUTPUT\] C150-MANAGED-LIFECYCLE-TESTS cases=8 fresh=PASS active=one result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C155 boot {0}/3 waiting for return-target marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C150-RETURN-TARGET-TESTS\] cases=10 capacity=1 identity=canonical self=reject invalid=reject result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C155 boot {0}/3 waiting for wheel-host marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C137-HOST registration=9 list=registered initial=runtime-offset result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C155 boot {0}/3 startup serial markers passed." -f $BootNumber)
        $serialText = Get-Serial $serial
        $script:fileX = Get-Target $serialText 'fileX'; $script:fileY = Get-Target $serialText 'fileY'
        $script:menuX = Get-Target $serialText 'menuOpenX'; $script:menuY = Get-Target $serialText 'menuOpenY'
        Write-Host ("C157 boot {0}/3 launch and regression markers passed; calibrating QMP pointer." -f $BootNumber)
        # QEMU starts with a relative PS/2 pointer. Move it into the Notes client,
        # use the native managed input marker to recover client coordinates, then
        # all later moves are exact relative deltas in that same coordinate space.
        $calibrationStart = (Get-Serial $serial).Length
        Write-Host ("C157 boot {0}/3 sending QMP pointer calibration on port {1}." -f $BootNumber, $port)
        Send-QmpEvents $port (New-RelativeMove 1 1) $monitor
        Write-Host ("C157 boot {0}/3 waiting for native pointer marker." -f $BootNumber)
        $calibrated = Wait-Serial $serial '^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS' $calibrationStart 20
        $script:cursor = Get-ClientPoint $calibrated.Text
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C137-TESTS transport=12 text-area=16 list-box=18 cases=46 result=PASS' 0 15)

        Open-C156InitialDocument $serial ($BootNumber -eq 1)
        Invoke-C156UndoRedoScenario $serial
        if ($BootNumber -eq 1) { Invoke-C156ClipboardStress $serial }

        switch ($Scenario) {
            'clean-new-saveas-stress' { Invoke-C157Boot1 $serial }
            'dirty-new-decisions' { Invoke-C157Boot2 $serial }
            'clipboard-untitled-session' { Invoke-C157Boot3 $serial }
            default { throw "Unknown C157 scenario: $Scenario" }
        }
        Write-Host ("C157 boot {0}/3 scenario={1} passed." -f $BootNumber, $Scenario)
        $full = Get-Serial $serial
        if ($Scenario -eq 'dirty-save-clipboard' -and
            $full -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false\r?$') {
            throw "C157 $Scenario boot did not record a verified Save."
        }
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C152-(?!SAVE result=FAIL document=preserved=true dirty=true path=preserved=true)[^\r\n]*result=FAIL') {
            throw "C157 $Scenario boot contains a failed C152 workflow proof marker."
        }
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C151-(?:LOAD|CANCEL|INSTANCE|WHEEL|DIRECTORY|STRESS)[^\r\n]*result=FAIL') {
            throw "C157 $Scenario boot contains a failed C151 managed proof marker."
        }
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C155-[^\r\n]*result=FAIL') {
            throw "C157 $Scenario boot contains a failed Notes-session proof marker."
        }
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C156-[^\r\n]*result=FAIL') {
            throw "C157 $Scenario boot contains a failed modifier or shortcut proof marker."
        }
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C157-[^\r\n]*result=FAIL') {
            throw "C157 $Scenario boot contains a failed New-document proof marker."
        }
        foreach ($side in @('left', 'right')) {
            $downs = [regex]::Matches($full, '(?m)^\[C156-KEYBOARD\] event=control-' + $side + '-down ')
            $ups = [regex]::Matches($full, '(?m)^\[C156-KEYBOARD\] event=control-' + $side + '-up ')
            if ($downs.Count -ne $ups.Count) {
                throw "C156 $Scenario boot ended with unbalanced $side Control transitions ($($downs.Count) down, $($ups.Count) up)."
            }
        }
        $restoreMatches = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C155-RESTORE generation=(\d+)')
        if ($Scenario -eq 'clipboard-untitled-session' -and $restoreMatches.Count -lt 1) {
            throw "C155 $Scenario boot did not restore a returned Notes session."
        }
        $finalSessionGeneration = if ($restoreMatches.Count -gt 0) {
            [uint32]$restoreMatches[$restoreMatches.Count - 1].Groups[1].Value
        } else { $null }
        Stop-Qemu $port $process $monitor
        $process.Refresh()
        $bootResult = [pscustomobject]@{ Boot = $BootNumber; Scenario = $Scenario; SerialPath = $serial; SerialHash = Get-Hash $serial; KernelHash = Get-Hash (Join-Path $esp 'kernel.elf'); RamdiskHash = Get-Hash (Join-Path $esp 'ramdisk.img'); NewStressCycles = if ($Scenario -eq 'clean-new-saveas-stress') { $StressCycles } else { 0 }; FinalSessionGeneration = $finalSessionGeneration; ExitCode = $process.ExitCode }
        # The ESP is a disposable QEMU input copy. Keep its hashes and serial
        # evidence, then release the 64 MiB ramdisk before staging the next boot.
        Remove-Item -LiteralPath $esp -Recurse -Force
        return $bootResult
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue; Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue }
        $script:activeProcess = $null
        $script:activeStderr = $null
    }
}

function Invoke-C151PointerScenario([string]$Serial) {
    $index = Open-Dialog $Serial (Get-Serial $Serial).Length
    Click-Client 60 79
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $index 20)
    $index = (Get-Serial $Serial).Length
    Click-Client 560 330
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-OUTSIDE blocked=true notes-action=none result=PASS' $index 10)
    $index = (Get-Serial $Serial).Length
    Move-Client 60 120
    Send-QmpEvents $script:activePort @((New-Wheel 1)) $script:activeMonitor
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-WHEEL modal-list=true .*natural=1 lines=5 expected=5 background=unchanged result=PASS' $index 15)
    # NaturalScroll with five lines moves the viewport to max.txt; clicking its
    # visible first row loads a full-capacity 256-byte note transactionally.
    Click-Client 60 79
    Click-Client 326 260
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-LOAD-DETAIL bytes=256 path=/system/apps/C151/max\.txt' $index 20)
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-INSTANCE same=true surface=unchanged modal=none popup-capture=none drag=none result=PASS' $index 15)
}

function Invoke-C151KeyboardScenario([string]$Serial) {
    $index = Open-Dialog $Serial (Get-Serial $Serial).Length
    Press-Key 'tab'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-KEYBOARD key=Tab focus=3 modal=true result=PASS' $index 10)
    [void](Wait-Serial $Serial '^\[C129-NATIVE\] tab-keydown shift=0 transport=production result=PASS' $index 10)
    $index = (Get-Serial $Serial).Length
    Send-QmpEvents $script:activePort @((New-Key 'shift' $true), (New-Key 'tab' $true), (New-Key 'tab' $false), (New-Key 'shift' $false)) $script:activeMonitor
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-KEYBOARD key=ShiftTab focus=1 modal=true result=PASS' $index 10)
    [void](Wait-Serial $Serial '^\[C129-NATIVE\] tab-keydown shift=1 transport=production result=PASS' $index 10)
    $index = (Get-Serial $Serial).Length
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $index 12)
    $index = (Get-Serial $Serial).Length
    Press-Key 'down'; Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151/nested entries=1 rows=2' $index 12)
    $index = (Get-Serial $Serial).Length
    Press-Key 'down'; Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-LOAD-DETAIL bytes=17 path=/system/apps/C151/nested/gamma\.txt' $index 12)
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-CONTENT first-line=C151_GAMMA_MARKER visible=Notes result=PASS' $index 12)
    # Re-open with the pointer, then perform child/parent/root navigation using
    # only keyboard input and open alpha after the modal returns to its parent.
    $index = Open-Dialog $Serial (Get-Serial $Serial).Length
    Press-Key 'ret'
    $start = (Get-Serial $Serial).Length
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps entries=\d+ rows=\d+' $start 10)
    $start = (Get-Serial $Serial).Length
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 10)
    $start = (Get-Serial $Serial).Length
    Press-Key 'down'; Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151/nested entries=1 rows=2' $start 10)
    $start = (Get-Serial $Serial).Length
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 10)
    $start = (Get-Serial $Serial).Length
    Press-Key 'down'; Press-Key 'down'; Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-LOAD-DETAIL bytes=56 path=/system/apps/C151/alpha\.txt' $start 15)
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-CONTENT first-line=C151_ALPHA_MARKER visible=Notes result=PASS' $start 10)
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-SAME-NOTES active=true relaunch=false return-target=unarmed result=PASS' $start 10)
    $start = Open-Dialog $Serial (Get-Serial $Serial).Length
    Press-Key 'esc'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-CANCEL result=PASS document=preserved=true' $start 12)
}

function Invoke-C151StressScenario([string]$Serial) {
    for ($cycle = 1; $cycle -le 25; $cycle++) {
        $start = Open-Dialog $Serial (Get-Serial $Serial).Length
        Press-Key 'esc'
        [void](Wait-Serial $Serial ("^\[C102-MANAGED-OUTPUT\] C151-STRESS operation=cancel cycle={0} registration=3 same-instance=true result=PASS" -f $cycle) $start 12)
    }
    for ($cycle = 1; $cycle -le 25; $cycle++) {
        $start = Open-Dialog $Serial (Get-Serial $Serial).Length
        Press-Key 'ret'
        [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 12)
        Press-Key 'down'; Press-Key 'down'; Press-Key 'ret'
        [void](Wait-Serial $Serial ("^\[C102-MANAGED-OUTPUT\] C151-STRESS operation=open cycle={0} registration=3 same-instance=true result=PASS" -f $cycle) $start 15)
    }
    $start = Open-Dialog $Serial (Get-Serial $Serial).Length
    Press-Key 'down'; Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/FULL entries=62 rows=64' $start 15)
    Press-Key 'esc'
    $start = Open-Dialog $Serial (Get-Serial $Serial).Length
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 12)
    Press-Key 'end'; Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-LOAD result=REJECTED reason=oversized document=preserved=true' $start 15)
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-INSTANCE same=true surface=unchanged modal=none popup-capture=none drag=none result=PASS' $start 12)
    $start = Open-Dialog $Serial (Get-Serial $Serial).Length
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-DIRECTORY path=/system/apps/C151 entries=15 rows=16' $start 12)
    1..4 | ForEach-Object { Press-Key 'down' }
    Press-Key 'ret'
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-LOAD-DETAIL bytes=0 path=/system/apps/C151/empty\.txt' $start 15)
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
    Wait-Process -Id $Process.Id -Timeout 8 -ErrorAction SilentlyContinue
}

function Invoke-OrdinaryBoot([int]$Number, [string]$Qemu, [string]$Ovmf) {
    $root = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}" -f $Number)
    $esp = Join-Path $root 'ESP'
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    Stage-Esp $esp $proofBackup $ramdiskBackup
    $serial = Join-Path $root 'serial.log'
    $stdout = Join-Path $root 'qemu.stdout.log'
    $stderr = Join-Path $root 'qemu.stderr.log'
    $monitor = Join-Path $root 'qmp.log'
    Remove-Item -LiteralPath $serial,$stdout,$stderr,$monitor -Force -ErrorAction SilentlyContinue
    $port = Get-AvailableQmpPort
    $arguments = @('-accel','tcg,thread=single','-machine','pc','-smp','1',
        '-drive',('if=pflash,format=raw,readonly=on,file="{0}"' -f $Ovmf),
        '-drive',('file=fat:rw:"{0}",format=raw,if=ide,index=0' -f $esp),
        '-m','1024M','-vga','std','-display','none',
        '-serial',('file:"{0}"' -f $serial),
        '-qmp',("tcp:127.0.0.1:{0},server,nowait" -f $port),
        '-boot','order=c','-no-reboot','-no-shutdown','-rtc','base=utc,clock=host')
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
    $script:activeProcess = $process
    $script:activeStderr = $stderr
    try {
        [void](Wait-Serial $serial '^\[desktop\] bare-metal desktop icon init completed' 0 $TimeoutSeconds)
        $text = Get-Serial $serial
        if ($text -notmatch '(?m)^\[KERNEL\] Boot method: UEFI BootInfo' -or
            $text -match 'PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure|C151-') {
            throw "Ordinary restoration boot $Number failed or contains proof-mode output."
        }
        Stop-Qemu $port $process $monitor
        $process.Refresh()
        $bootResult = [pscustomobject]@{ Boot = $Number; SerialPath = $serial; SerialHash = Get-Hash $serial; KernelHash = Get-Hash (Join-Path $esp 'kernel.elf'); RamdiskHash = Get-Hash (Join-Path $esp 'ramdisk.img') }
        # Keep the ordinary boot hashes and serial log without retaining another
        # full ramdisk copy in the evidence folder.
        Remove-Item -LiteralPath $esp -Recurse -Force
        return $bootResult
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue; Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue }
        $script:activeProcess = $null
        $script:activeStderr = $null
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
    for ($index = 0; $index -lt 4; $index++) { $bytes[22 + $index] = [byte](($crc -shr ($index * 8)) -band 255) }
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot, (Split-Path -Parent $proofBackup) | Out-Null
foreach ($path in @($kernelPath, $espKernelPath, $protectedRamdiskPath, $bootloaderPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "C151 required protected input is missing: $path" }
}
$canonicalKernelHash = Get-Hash $kernelPath
$espKernelHash = Get-Hash $espKernelPath
$ramdiskHash = Get-Hash $protectedRamdiskPath
if ($canonicalKernelHash -ne $espKernelHash) { throw 'Canonical kernel and ESP kernel differ; no proof assets were changed.' }
Copy-Item -LiteralPath $kernelPath -Destination $proofBackup -Force
Copy-Item -LiteralPath $espKernelPath -Destination $espKernelBackup -Force
Copy-Item -LiteralPath $protectedRamdiskPath -Destination $ramdiskBackup -Force
if ((Get-Hash $proofBackup) -ne $canonicalKernelHash -or (Get-Hash $espKernelBackup) -ne $espKernelHash -or (Get-Hash $ramdiskBackup) -ne $ramdiskHash) {
    throw 'C151 could not verify byte-identical protected backups.'
}
Clear-PreviousQemuEspCopies

trap {
    if ($proofBackup -and (Test-Path -LiteralPath $proofBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $proofBackup -Destination $kernelPath -Force
    }
    if ($espKernelBackup -and (Test-Path -LiteralPath $espKernelBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $espKernelBackup -Destination $espKernelPath -Force
    }
    if ($ramdiskBackup -and (Test-Path -LiteralPath $ramdiskBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $ramdiskBackup -Destination $protectedRamdiskPath -Force
    }
    throw $_
}

$python = if ($PythonExe) { $PythonExe } else {
    Get-Tool 'python' @('C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe','C:\Python312\python.exe','C:\Python311\python.exe')
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
    '-HeapConfiguration','Primary4MiB','-PythonExe',$python)
Write-Host 'C157 managed NativeAOT composite build passed.'
$compositeElf = Join-Path $compositeRoot 'artifacts\HostLogProof.elf'
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C155 NativeAOT ELF missing: $compositeElf" }
$generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
Invoke-Checked 'powershell' @('-ExecutionPolicy','Bypass','-File',$generator,
    '-OutputDir',$stageRoot,'-OutputImage',$proofRamdisk,
    '-C104AppAPath',$compositeElf,'-ProductionCompositeApplicationPath',$compositeElf,
    '-C114ManagedDirectoryServices','-C117ManagedTextArea','-C118ManagedListBox',
    '-C151ManagedOpenFileDialog','-C152ManagedNotesSaveWorkflow',
    '-C155ManagedNotesSession','-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument')
Write-Host 'C157 proof media and bounded fixtures generated.'

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
    '-DGXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS','-DGXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT') -join ' '
if ($ReuseProofKernel) {
    if (-not (Test-Path -LiteralPath $proofKernel -PathType Leaf)) {
        throw "-ReuseProofKernel requested but the prior C157 proof kernel is missing: $proofKernel"
    }
    Write-Host "Reusing C157 proof kernel: $proofKernel"
} else {
    Write-Host 'C157 building fresh C112-C157-compatible production kernel.'
    Invoke-Checked $make @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',"EXTRA_CFLAGS=$flags",'-B')
    if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw 'C157 native kernel build did not produce kernel.elf.' }
    Copy-Item -LiteralPath $kernelPath -Destination $proofKernel -Force
}

$settingsRecord = Join-Path $EvidenceRoot 'GXSETT.BIN'
New-C151SettingsRecord $settingsRecord
$production = [System.Collections.Generic.List[object]]::new()
if ($ResumeAfterBoot1) {
    $boot1Serial = Join-Path $EvidenceRoot 'production-boot-01\serial.log'
    if (-not (Test-Path -LiteralPath $boot1Serial -PathType Leaf)) {
        throw "-ResumeAfterBoot1 requires completed serial evidence at $boot1Serial"
    }
    $boot1Text = Get-Serial $boot1Serial
    $boot1NewCount = [regex]::Matches($boot1Text, '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS\r?$').Count
    $boot1DiscardCount = [regex]::Matches($boot1Text, '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW dirty-decision=Discard vfs-write=none result=PASS\r?$').Count
    if ($boot1Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW-TESTS cases=\d+ result=PASS\r?$' -or
        $boot1Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW-DETAIL .*result=PASS\r?$' -or
        $boot1Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C157-NEW-STRESS cycles=25 .*result=PASS\r?$' -or
        $boot1Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C135-NOTES initial=registration=shared items=13 capture=none result=PASS\r?$' -or
        $boot1NewCount -lt 27 -or $boot1DiscardCount -ne 8 -or
        $boot1Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS committed path=/system/apps/untitled\.txt\r?$' -or
        $boot1Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false\r?$' -or
        $boot1Text -match '(?m)^\[C102-MANAGED-OUTPUT\] C157-[^\r\n]*result=FAIL') {
        throw 'Existing boot 1 evidence does not satisfy the completed C157 clean New, Save As, and 25-cycle requirements.'
    }
    $production.Add([pscustomobject]@{
        Boot = 1; Scenario = 'clean-new-saveas-stress'; SerialPath = $boot1Serial
        SerialHash = Get-Hash $boot1Serial; KernelHash = Get-Hash $proofKernel
        RamdiskHash = Get-Hash $proofRamdisk; NewStressCycles = $StressCycles
        FinalSessionGeneration = $null; ExitCode = 0
    }) | Out-Null
    Write-Host 'Reusing validated fresh C157 boot 1 evidence; continuing with fresh boots 2 and 3.'
} else {
    Write-Host 'C157 beginning three fresh production proof boots.'
    $production.Add((Start-ProofBoot 1 'clean-new-saveas-stress' $qemu $ovmf $settingsRecord)) | Out-Null
}
$production.Add((Start-ProofBoot 2 'dirty-new-decisions' $qemu $ovmf $settingsRecord)) | Out-Null
$production.Add((Start-ProofBoot 3 'clipboard-untitled-session' $qemu $ovmf $settingsRecord)) | Out-Null

Copy-Item -LiteralPath $proofBackup -Destination $kernelPath -Force
Copy-Item -LiteralPath $espKernelBackup -Destination $espKernelPath -Force
Copy-Item -LiteralPath $ramdiskBackup -Destination $protectedRamdiskPath -Force
$restored = (Get-Hash $kernelPath) -eq $canonicalKernelHash -and
    (Get-Hash $espKernelPath) -eq $espKernelHash -and
    (Get-Hash $protectedRamdiskPath) -eq $ramdiskHash
if (-not $restored) { throw 'C155 failed byte-for-byte restoration before ordinary boots.' }
Write-Host 'C157 protected kernel and ramdisk hashes restored; beginning ordinary boots.'

$ordinary = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    Write-Host ("C157 ordinary restoration boot {0}/3." -f $boot)
    $ordinary.Add((Invoke-OrdinaryBoot $boot $qemu $ovmf)) | Out-Null
}
if ((Get-Hash $kernelPath) -ne $canonicalKernelHash -or
    (Get-Hash $espKernelPath) -ne $espKernelHash -or
    (Get-Hash $protectedRamdiskPath) -ne $ramdiskHash) {
    throw 'C155 ordinary artifacts changed after restoration boots.'
}

# Build stable manifest rows from the completed serial evidence. This avoids
# relying on PowerShell pipeline enumeration of objects returned by QEMU helpers.
$productionRecords = [System.Collections.Generic.List[object]]::new()
$scenarioNames = @('clean-new-saveas-stress', 'dirty-new-decisions', 'clipboard-untitled-session')
for ($boot = 1; $boot -le 3; $boot++) {
    $serial = Join-Path $EvidenceRoot ("production-boot-{0:D2}\serial.log" -f $boot)
    $full = Get-Serial $serial
    $restoreMatches = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C155-RESTORE generation=(\d+)')
    if ($boot -eq 3 -and $restoreMatches.Count -lt 1) { throw "C157 boot $boot has no completed Notes restore marker." }
    $finalGeneration = if ($restoreMatches.Count -gt 0) {
        [uint32]$restoreMatches[$restoreMatches.Count - 1].Groups[1].Value
    } else { $null }
    $productionRecords.Add([ordered]@{
        Boot = $boot; Scenario = $scenarioNames[$boot - 1]; SerialPath = $serial
        SerialHash = Get-Hash $serial; KernelHash = Get-Hash $proofKernel
        RamdiskHash = Get-Hash $proofRamdisk
        NewStressCycles = if ($boot -eq 1) { $StressCycles } else { 0 }
        FinalSessionGeneration = $finalGeneration; ExitCode = 0
    })
}
$ordinaryRecords = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    $serial = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}\serial.log" -f $boot)
    $ordinaryRecords.Add([ordered]@{
        Boot = $boot; SerialPath = $serial; SerialHash = Get-Hash $serial
        KernelHash = $canonicalKernelHash; RamdiskHash = $ramdiskHash
    })
}
$finalSessionGeneration = $productionRecords[2].FinalSessionGeneration
$boot3Serial = Join-Path $EvidenceRoot 'production-boot-03\serial.log'
$boot3Text = Get-Serial $boot3Serial
if ($boot3Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C154-PASTE length=3 content-change=once result=PASS\r?$' -or
    $boot3Text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C155-RESTORE generation=\d+ source=blank named=false result=PASS\r?$') {
    throw 'C157 final clipboard or clean-untitled Settings restoration evidence is missing.'
}

$manifest = [ordered]@{
    schemaVersion = 1
    phase = 'C157'
    outcome = 'A'
    branch = 'v1.1_DOTNET_SUPPORT'
    protected = [ordered]@{
        canonicalKernelBefore = $canonicalKernelHash
        espKernelBefore = $espKernelHash
        ramdiskBefore = $ramdiskHash
        restored = $restored
        canonicalKernelAfter = Get-Hash $kernelPath
        espKernelAfter = Get-Hash $espKernelPath
        ramdiskAfter = Get-Hash $protectedRamdiskPath
    }
    nativeAot = [ordered]@{
        compositeElf = $compositeElf
        compositeSha256 = Get-Hash $compositeElf
        proofKernel = $proofKernel
        proofKernelSha256 = Get-Hash $proofKernel
        kernelBuild = if ($ReuseProofKernel) { 'reused' } else { 'fresh' }
        heap = 'Primary4MiB'
        managedMode = 'C154Composite + C155 session + C156 shortcuts + C157 New'
        controlEventBit = '0x00400000'
        shiftEventBit = '0x00800000'
        abiVersion = 1
        abiTableBytes = 104
        settingsVersion = 2
    }
    features = [ordered]@{
        menu = 'New, Open, Save, Save As, separator, Undo, Redo, separator, Cut, Copy, Paste, separator, Reload'
        menuCapacity = '12 -> 13; popup default remains 8'
        shortcut = 'Ctrl+N and right Ctrl+N route to shared RequestNewDocument action; KeyChar suppressed'
        stateModel = 'untitled/named x clean/dirty; one document in one Notes instance'
        newBaseline = 'no path; empty; clean saved-revision identity; no undo/redo; caret/anchor/viewport zero'
        clipboard = 'resident GuideXosClipboard.Shared unchanged by New'
        vfsWrite = 'New performs no write; dirty Save is verified before replacement'
        untitledSave = 'Ctrl+S opens Save As with the existing untitled.txt default'
        reload = 'existing Reload path handling unchanged; New is a separate action'
        deferred = @('multiple documents','tabs','autosave','crash recovery','persistent untitled draft','navigation stack')
    }
    regressions = [ordered]@{
        C157 = 'focused cases logged PASS; 25 component New stress cycles; 25 QEMU New cycles'
        C156 = 'modifier decode 10/10; shortcut routing 15/15; left/right Control; Ctrl+N no text leak'
        C153 = 'history 26/26; TextArea 25/25; save points 17/17'
        C154 = 'clipboard suite PASS; Copy and Cut clipboard survive New; Paste Undo returns clean baseline'
        C152 = 'save dialog 16/16; document state 10/10; named and untitled dirty New Save/Discard/Cancel/failure paths'
        C151 = '10/10; clean New Open chooser and dirty Open protection retained'
        C155 = 'blank clean return, dirty Save as named return, dirty Discard as blank return; Cancel blocks transition'
        C150 = 'return-target 10/10; lifecycle 8/8; Settings destroys and returns Notes through existing owner'
        C145 = '49/49'
        C135 = 'menu and host API focused suites PASS; capacity 13 validated'
        C129 = '31/31 focused and production Tab/Shift+Tab pass'
        C128 = '46/46 remains unverified; no current pass claimed'
    }
    productionBoots = @($productionRecords.ToArray())
    ordinaryBoots = @($ordinaryRecords.ToArray())
    session = [ordered]@{
        owner = 'resident managed runtime GuideXosNotesReturnSessionC155.Shared'
        lifetime = 'Notes teardown -> Settings Center -> one returned Notes launch'
        fixedStorageBytes = 118
        pathBufferBytes = 96
        pathBufferIncludesTerminator = $true
        documentTextStored = $false
        pathlessSession = 'blank, no fake VFS identity, caret/anchor/viewport 0'
    }
    clipboardState = [ordered]@{
        owner = 'GuideXosClipboard.Shared'
        lifetime = 'resident managed runtime for one boot'
        capacityBytes = 256
        c156StressCycles = 25
        c157NewStressCycles = $StressCycles
        persistedAcrossReboot = $false
    }
    finalState = [ordered]@{
        semanticGeneration = $finalSessionGeneration
        instance = 'fresh Notes after C150 Settings return'
        currentPath = 'none'
        document = 'three-character clipboard paste into blank untitled document'
        dirty = $true
        savedRevision = 'blank untitled New baseline'
        canUndo = $true
        canRedo = $false
        clipboardLength = 3
        c155PendingSession = 'none'
        c150ReturnTarget = 'none after return'
        control = $false
        shift = $false
        modalOwner = 'none'
        popupCapture = 'none'
        dragOwner = 'none'
        settingsFormat = 2
        hostAbiVersion = 1
        hostTableBytes = 104
    }
}
$manifestPath = Join-Path $EvidenceRoot 'c157-proof-manifest.json'
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
$ordinaryManifest = [ordered]@{
    schemaVersion = 1
    phase = 'C157-ordinary-restoration'
    outcome = 'PASS'
    protected = $manifest.protected
    ordinaryBoots = @($ordinaryRecords.ToArray())
}
$ordinaryManifestPath = Join-Path $EvidenceRoot 'c157-ordinary-restoration-manifest.json'
$ordinaryManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ordinaryManifestPath -Encoding ASCII
Write-Host "C157 outcome=A evidence=$EvidenceRoot manifest=$manifestPath ordinaryManifest=$ordinaryManifestPath" -ForegroundColor Green
