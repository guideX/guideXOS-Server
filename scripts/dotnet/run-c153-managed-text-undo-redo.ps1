param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$PythonExe = "",
    [int]$TimeoutSeconds = 600,
    [switch]$ReuseProofKernel
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c153-managed-text-undo-redo"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C153 evidence must remain under $allowedRoot"
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
$proofRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c153.img'
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
        throw "Refusing to clear an ESP outside the C153 evidence folder: $espFull"
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
            throw "Refusing to clear a QEMU ESP outside the C153 evidence folder: $candidate"
        }
        if (Test-Path -LiteralPath $candidate) {
            $resolved = (Resolve-Path -LiteralPath $candidate).Path
            if (-not $resolved.StartsWith($evidenceRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to clear a resolved QEMU ESP outside the C153 evidence folder: $resolved"
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

function Send-QmpEvents([int]$Port, [object[]]$Events, [string]$LogPath) {
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
            Start-Sleep -Milliseconds 300
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
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C153-REGRESSIONS result=FAIL\r?$') {
            throw 'C153 focused history or save-point suite reported failure.'
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
    $observed = Wait-Serial $script:activeSerial '^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS' $before 10
    $script:cursor = Get-ClientPoint $observed.Text
}

function Click-Client([int]$X, [int]$Y) {
    Move-Client $X $Y
    Send-QmpEvents $script:activePort @((New-Button 'left' $true), (New-Button 'left' $false)) $script:activeMonitor
}

function Press-Key([string]$Code) {
    Send-QmpEvents $script:activePort @((New-Key $Code $true), (New-Key $Code $false)) $script:activeMonitor
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

function Invoke-C153Undo([string]$SerialPath, [switch]$UndoRemainsAvailable) {
    if (-not $script:historyCanUndo) { throw 'C153 script expected an available Undo command.' }
    $start = (Get-Serial $SerialPath).Length
    Click-Client $script:fileX $script:fileY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS' $start 12)
    $start = (Get-Serial $SerialPath).Length
    Press-Key 'ret'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C153-UNDO source=menu content-change=once result=PASS' $start 12)
    $script:historyCanUndo = [bool]$UndoRemainsAvailable
    $script:historyCanRedo = $true
}

function Invoke-C153Redo([string]$SerialPath, [switch]$RedoRemainsAvailable) {
    if (-not $script:historyCanRedo) { throw 'C153 script expected an available Redo command.' }
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

function Invoke-C153HistoryRoundTrip([string]$SerialPath,
    [switch]$UndoRemainsAvailable, [switch]$RedoRemainsAvailable) {
    Invoke-C153Undo $SerialPath -UndoRemainsAvailable:$UndoRemainsAvailable
    Invoke-C153Redo $SerialPath -RedoRemainsAvailable:$RedoRemainsAvailable
}

function Invoke-C153OpenDecisionProof([string]$SerialPath, [bool]$Dirty) {
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

function Type-C153Character([string]$SerialPath, [string]$Code) {
    $start = (Get-Serial $SerialPath).Length
    Press-Key $Code
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-ROUTING active=document result=PASS' $start 12)
    $script:historyCanUndo = $true
    $script:historyCanRedo = $false
}

function Focus-C153Document([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 100 80
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C120-POINTER target=document result=PASS' $start 12)
}

function Invoke-C153CloseDecisionProof([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Send-QmpEvents $script:activePort @((New-Key 'alt' $true),(New-Key 'f4' $true),(New-Key 'f4' $false),(New-Key 'alt' $false)) $script:activeMonitor
    [void](Wait-Serial $SerialPath '^\[C152-CLOSE\] request=vetoed dirty-prompt=active result=PASS' $start 15)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-CLOSE dirty-decision=Cancel window=preserved=true result=PASS' $start 12)
}

function Invoke-C153SettingsPromptProof([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active transition=deferred result=PASS' $start 15)
    Press-Key 'esc'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-decision=Cancel transition=blocked result=PASS' $start 12)
}

function Invoke-C153CleanSettingsTransition([string]$SerialPath) {
    $start = (Get-Serial $SerialPath).Length
    Click-Client 475 294
    [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $start 20)
    $tail = (Get-Serial $SerialPath).Substring($start)
    if ($tail -match '(?m)^\[C102-MANAGED-OUTPUT\] C152-SETTINGS dirty-prompt=active') {
        throw 'Settings unexpectedly prompted after Redo restored the saved revision.'
    }
}

function Invoke-C153SavePointScenario([string]$SerialPath) {
    Edit-C152Document $SerialPath
    Invoke-C153Undo $SerialPath
    Invoke-C153OpenDecisionProof $SerialPath $false
    Invoke-C153Redo $SerialPath
    Invoke-C153OpenDecisionProof $SerialPath $true

    # Real keyboard LF and Backspace edits remain distinct, undoable revisions.
    Focus-C153Document $SerialPath
    Type-C153Character $SerialPath 'ret'
    Type-C153Character $SerialPath 'backspace'
    Invoke-C153Undo $SerialPath -UndoRemainsAvailable
    Invoke-C153Redo $SerialPath

    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 12)
    Invoke-C153Undo $SerialPath -UndoRemainsAvailable
    Invoke-C153Redo $SerialPath
    Invoke-C153HistoryRoundTrip $SerialPath -UndoRemainsAvailable

    # Add multiple real LF revisions, move back to the saved state, redo them,
    # and send an actual QMP wheel event through the TextArea after the restore.
    Focus-C153Document $SerialPath
    Press-Key 'end'
    for ($index = 0; $index -lt 6; $index++) {
        Type-C153Character $SerialPath 'ret'
    }
    for ($index = 0; $index -lt 6; $index++) {
        Invoke-C153Undo $SerialPath -UndoRemainsAvailable
    }
    for ($index = 0; $index -lt 6; $index++) {
        Invoke-C153Redo $SerialPath -RedoRemainsAvailable:($index -lt 5)
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
    Invoke-C153Undo $SerialPath -UndoRemainsAvailable
    Invoke-C153Redo $SerialPath

    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 12)
    Invoke-C153Undo $SerialPath -UndoRemainsAvailable
    Invoke-C153Redo $SerialPath
}

function Invoke-C153SaveAsHistoryScenario([string]$SerialPath) {
    Edit-C152Document $SerialPath
    Type-C153Character $SerialPath 'y'
    Type-C153Character $SerialPath 'z'
    Invoke-C153Undo $SerialPath -UndoRemainsAvailable
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
    Focus-C153Document $SerialPath
    Invoke-C153HistoryRoundTrip $SerialPath -UndoRemainsAvailable -RedoRemainsAvailable

    # Save As marks the current XY revision clean while preserving the redo of Z.
    Invoke-C153Undo $SerialPath -UndoRemainsAvailable
    Invoke-C153Redo $SerialPath -RedoRemainsAvailable
    Type-C153Character $SerialPath 'q'
    Invoke-C153HistoryRoundTrip $SerialPath -UndoRemainsAvailable

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
    Invoke-C153HistoryRoundTrip $SerialPath -UndoRemainsAvailable

    Edit-C152Document $SerialPath
    Invoke-C153OpenDecisionProof $SerialPath $true

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

function Invoke-C153DirtyLifecycleScenario([string]$SerialPath) {
    Edit-C152Document $SerialPath
    Invoke-C153Undo $SerialPath
    Invoke-C153OpenDecisionProof $SerialPath $false
    Invoke-C153Redo $SerialPath
    Invoke-C153SettingsPromptProof $SerialPath
    Invoke-C153CloseDecisionProof $SerialPath

    $start = (Get-Serial $SerialPath).Length
    Press-Key 'f12'
    [void](Wait-Serial $SerialPath '^\[C152-FAILURE-INJECTION\] next-notes-write=io-error result=PASS' $start 12)
    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-FAILURE-INJECTION\] operation=write path=/system/apps/C152/alpha\.txt disk=unchanged restore=PASS result=FAIL' $start 15)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=FAIL document=preserved=true dirty=true path=preserved=true' $start 12)
    Press-Key 'ret'
    Invoke-C153Undo $SerialPath
    Invoke-C153Redo $SerialPath
    $start = Invoke-C152MenuCommand $SerialPath 1
    [void](Wait-Serial $SerialPath '^\[C152-VFS-WRITE\] verify=read-back exact=true backup=bounded flush=unavailable replace=non-atomic result=PASS' $start 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $start 15)
    Invoke-C153Undo $SerialPath
    Invoke-C153Redo $SerialPath
    Invoke-C153CleanSettingsTransition $SerialPath
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
        Write-Host ("C153 boot {0}/3 waiting for Notes launch marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C151-NOTES-LAUNCH\].*return-target=none result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C152-NOTES-LAUNCH\] identity=canonical context=c152-notes result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C153 boot {0}/3 waiting for C151, C152, and C153 regression markers." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C151-REGRESSIONS C145=49/49 C151-core=10/10 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-REGRESSIONS save-dialog=16/16 document-state=10/10 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C153-REGRESSIONS history-core=26/26 text-area=25/25 save-point=17/17 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-SAVE-STRESS operations=50 alternating=verified read-back=exact dirty-cleared=true result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS-STRESS paths=5 current-path=latest read-back=exact result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-EMPTY-SAVE create=read-back=PASS bytes=0 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-MAX-SAVE bytes=256 read-back=exact result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C153 boot {0}/3 waiting for application lifetime marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C150-MANAGED-OUTPUT\] C150-MANAGED-LIFECYCLE-TESTS cases=8 fresh=PASS active=one result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C153 boot {0}/3 waiting for return-target marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C150-RETURN-TARGET-TESTS\] cases=10 capacity=1 identity=canonical self=reject invalid=reject result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C153 boot {0}/3 waiting for wheel-host marker." -f $BootNumber)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C137-HOST registration=9 list=registered initial=runtime-offset result=PASS' 0 $TimeoutSeconds)
        Write-Host ("C153 boot {0}/3 startup serial markers passed." -f $BootNumber)
        $serialText = Get-Serial $serial
        $script:fileX = Get-Target $serialText 'fileX'; $script:fileY = Get-Target $serialText 'fileY'
        $script:menuX = Get-Target $serialText 'menuOpenX'; $script:menuY = Get-Target $serialText 'menuOpenY'
        Write-Host ("C153 boot {0}/3 launch and regression markers passed; calibrating QMP pointer." -f $BootNumber)
        # QEMU starts with a relative PS/2 pointer. Move it into the Notes client,
        # use the native managed input marker to recover client coordinates, then
        # all later moves are exact relative deltas in that same coordinate space.
        $calibrationStart = (Get-Serial $serial).Length
        Write-Host ("C153 boot {0}/3 sending QMP pointer calibration on port {1}." -f $BootNumber, $port)
        Send-QmpEvents $port (New-RelativeMove 1 1) $monitor
        Write-Host ("C153 boot {0}/3 waiting for native pointer marker." -f $BootNumber)
        $calibrated = Wait-Serial $serial '^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS' $calibrationStart 20
        $script:cursor = Get-ClientPoint $calibrated.Text
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C137-TESTS transport=12 text-area=16 list-box=18 cases=46 result=PASS' 0 15)

        switch ($Scenario) {
            'save-point' { Invoke-C153SavePointScenario $serial }
            'save-as-history' { Invoke-C153SaveAsHistoryScenario $serial }
            'dirty-lifecycle' { Invoke-C153DirtyLifecycleScenario $serial }
            default { throw "Unknown C153 scenario: $Scenario" }
        }
        Write-Host ("C153 boot {0}/3 scenario={1} passed." -f $BootNumber, $Scenario)
        $full = Get-Serial $serial
        if ($full -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false\r?$') {
            throw "C153 $Scenario boot did not record a verified Save."
        }
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C152-(?!SAVE result=FAIL document=preserved=true dirty=true path=preserved=true)[^\r\n]*result=FAIL') {
            throw "C153 $Scenario boot contains a failed workflow proof marker."
        }
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C151-(?:LOAD|CANCEL|INSTANCE|WHEEL|DIRECTORY|STRESS)[^\r\n]*result=FAIL') {
            throw "C153 $Scenario boot contains a failed legacy C151 managed proof marker."
        }
        Stop-Qemu $port $process $monitor
        $process.Refresh()
        $bootResult = [pscustomobject]@{ Boot = $BootNumber; Scenario = $Scenario; SerialPath = $serial; SerialHash = Get-Hash $serial; KernelHash = Get-Hash (Join-Path $esp 'kernel.elf'); RamdiskHash = Get-Hash (Join-Path $esp 'ramdisk.img'); ExitCode = $process.ExitCode }
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
    $index = (Get-Serial $Serial).Length
    Send-QmpEvents $script:activePort @((New-Key 'shift' $true), (New-Key 'tab' $true), (New-Key 'tab' $false), (New-Key 'shift' $false)) $script:activeMonitor
    [void](Wait-Serial $Serial '^\[C102-MANAGED-OUTPUT\] C151-KEYBOARD key=ShiftTab focus=1 modal=true result=PASS' $index 10)
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
    '-ManagedProjectMode','C153Composite','-HeapConfiguration','Primary4MiB','-PythonExe',$python)
        Write-Host 'C153 managed NativeAOT composite build passed.'
$compositeElf = Join-Path $compositeRoot 'artifacts\HostLogProof.elf'
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C152 NativeAOT ELF missing: $compositeElf" }
$generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
Invoke-Checked 'powershell' @('-ExecutionPolicy','Bypass','-File',$generator,
    '-OutputDir',$stageRoot,'-OutputImage',$proofRamdisk,
    '-C104AppAPath',$compositeElf,'-ProductionCompositeApplicationPath',$compositeElf,
    '-C114ManagedDirectoryServices','-C117ManagedTextArea','-C118ManagedListBox',
    '-C151ManagedOpenFileDialog','-C152ManagedNotesSaveWorkflow')
Write-Host 'C153 proof media and bounded fixtures generated.'

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
    '-DGXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW') -join ' '
if ($ReuseProofKernel) {
    if (-not (Test-Path -LiteralPath $proofKernel -PathType Leaf)) {
        throw "-ReuseProofKernel requested but the prior C153 proof kernel is missing: $proofKernel"
    }
    Write-Host "Reusing C153 proof kernel: $proofKernel"
} else {
    Write-Host 'C153 building fresh C112-C153-compatible production kernel.'
    Invoke-Checked $make @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',"EXTRA_CFLAGS=$flags",'-B')
    if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw 'C153 native kernel build did not produce kernel.elf.' }
    Copy-Item -LiteralPath $kernelPath -Destination $proofKernel -Force
}

$settingsRecord = Join-Path $EvidenceRoot 'GXSETT.BIN'
New-C151SettingsRecord $settingsRecord
$production = [System.Collections.Generic.List[object]]::new()
Write-Host 'C153 beginning three fresh production proof boots.'
$production.Add((Start-ProofBoot 1 'save-point' $qemu $ovmf $settingsRecord)) | Out-Null
$production.Add((Start-ProofBoot 2 'save-as-history' $qemu $ovmf $settingsRecord)) | Out-Null
$production.Add((Start-ProofBoot 3 'dirty-lifecycle' $qemu $ovmf $settingsRecord)) | Out-Null

Copy-Item -LiteralPath $proofBackup -Destination $kernelPath -Force
Copy-Item -LiteralPath $espKernelBackup -Destination $espKernelPath -Force
Copy-Item -LiteralPath $ramdiskBackup -Destination $protectedRamdiskPath -Force
$restored = (Get-Hash $kernelPath) -eq $canonicalKernelHash -and
    (Get-Hash $espKernelPath) -eq $espKernelHash -and
    (Get-Hash $protectedRamdiskPath) -eq $ramdiskHash
if (-not $restored) { throw 'C153 failed byte-for-byte restoration before ordinary boots.' }
Write-Host 'C153 protected kernel and ramdisk hashes restored; beginning ordinary boots.'

$ordinary = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    Write-Host ("C153 ordinary restoration boot {0}/3." -f $boot)
    $ordinary.Add((Invoke-OrdinaryBoot $boot $qemu $ovmf)) | Out-Null
}
if ((Get-Hash $kernelPath) -ne $canonicalKernelHash -or
    (Get-Hash $espKernelPath) -ne $espKernelHash -or
    (Get-Hash $protectedRamdiskPath) -ne $ramdiskHash) {
    throw 'C153 ordinary artifacts changed after restoration boots.'
}

$manifest = [ordered]@{
    schemaVersion = 1; phase = 'C153'; outcome = 'PASS'; branch = 'v1.1_DOTNET_SUPPORT'
    protected = [ordered]@{ canonicalKernelBefore = $canonicalKernelHash; espKernelBefore = $espKernelHash; ramdiskBefore = $ramdiskHash; restored = $restored; canonicalKernelAfter = Get-Hash $kernelPath; espKernelAfter = Get-Hash $espKernelPath; ramdiskAfter = Get-Hash $protectedRamdiskPath }
    nativeAot = [ordered]@{ compositeElf = $compositeElf; compositeSha256 = Get-Hash $compositeElf; proofKernel = $proofKernel; proofKernelSha256 = Get-Hash $proofKernel; kernelBuild = $(if ($ReuseProofKernel) { 'reused' } else { 'fresh' }); heap = 'Primary4MiB'; managedMode = 'C153Composite'; abiVersion = 1; abiTableBytes = 104 }
    fixtures = [ordered]@{ media = $proofRamdisk; mediaSha256 = Get-Hash $proofRamdisk; settings = $settingsRecord; settingsSha256 = Get-Hash $settingsRecord; directoryCapacity = 64; nestedFile = '/system/apps/C151/nested/gamma.txt'; maxBytes = 256; oversizedBytes = 257; overflowEntries = 66 }
    regressions = [ordered]@{ C145 = '49 cases per Notes launch'; C150 = 'return target identity and managed application lifetime suites per launch'; C137 = '46 cases per Notes launch with real wheel integration; production QMP wheel scrolls the TextArea after multi-line Undo/Redo'; C151 = '10 focused path/order/capacity/modal/ListBox cases per launch'; C152 = '16 Save File chooser plus 10 document state cases per launch; production saves use a bounded backup and verified read-back without flush or atomic-replace guarantees'; C153 = '26 history-core, 25 TextArea integration, and 17 save-point cases per Notes launch; 100-mutation core and TextArea stress; one content mutation per revision; fixed 16-snapshot ring' }
    productionBoots = @($production.ToArray()); ordinaryBoots = @($ordinary.ToArray())
    history = [ordered]@{ snapshotCapacity = 16; textCapacityChars = 256; metadataBytesPerSnapshot = 32; payloadBytes = 8704; caretAndAnchor = 'captured per revision'; viewport = 'caret-driven reveal and clamp'; savedRevision = '128-bit generation plus sequence identity'; savedRevisionEviction = 'unreachable save point keeps Notes dirty until next successful Save or Open'; saveRetainsHistory = $true; openResetsHistory = $true; appRelaunchPersistence = $false; shortcuts = 'menu only; managed input event exposes Shift but no Control modifier' }
    handles = 'managed directory bridge closes the VFS iterator on success, malformed entry, and name-length error; file read uses bounded stat plus synchronous VFS read with no retained file handle'
    capabilities = 'managed DirectoryList and FileStat for chooser; FileRead for Notes load and save verification; FileWrite for Notes save; paths remain confined to /system/apps'
    document = 'Notes TextArea capacity is 256 bytes and 32 lines; accepted content is printable ASCII plus LF; CR/control/non-ASCII content is rejected transactionally; path and dirty state commit only after verified read-back; write failure preserves the in-memory buffer and previous path'
}
$manifestPath = Join-Path $EvidenceRoot 'c153-proof-manifest.json'
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
Write-Host "C153 outcome=PASS evidence=$EvidenceRoot manifest=$manifestPath" -ForegroundColor Green
