param(
    [Parameter(Mandatory=$true)][ValidateSet('focused','production')][string]$Lane,
    [Parameter()][ValidatePattern('^[a-z0-9-]+$')][string]$RunLabel = 'attempt-01',
    [Parameter()][string]$EvidenceRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$artifactRoot = Join-Path $repo 'out/dotnet/c181-managed-application-residency'
$root = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $artifactRoot
} else {
    [System.IO.Path]::GetFullPath($EvidenceRoot)
}
$laneRoot = Join-Path $root ('guest-runs/' + $Lane + '-' + $RunLabel + '-qac-sequence')
$baseEsp = Join-Path $artifactRoot ('media/esp-' + $Lane + '-c181-qac')
$esp = Join-Path $laneRoot 'esp'
$serial = Join-Path $laneRoot 'serial.log'
$qmpLog = Join-Path $laneRoot 'qmp.log'
$qacStdoutPath = Join-Path $laneRoot 'qac.stdout.log'
$qacStderrPath = Join-Path $laneRoot 'qac.stderr.log'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = Join-Path $repo 'OVMF.fd'

if (-not (Test-Path -LiteralPath $baseEsp -PathType Container)) { throw "Missing staged ESP: $baseEsp" }
if (-not (Test-Path -LiteralPath $qacCli -PathType Leaf)) { throw "QAC CLI is unavailable: $qacCli" }
if (-not (Test-Path -LiteralPath $qemu -PathType Leaf)) { throw "QEMU is unavailable: $qemu" }
if (-not (Test-Path -LiteralPath $ovmf -PathType Leaf)) { throw "OVMF is unavailable: $ovmf" }
if (Test-Path -LiteralPath $laneRoot) { throw "Refusing to overwrite sequence evidence: $laneRoot" }
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $root | Out-Null
}
New-Item -ItemType Directory -Force -Path $laneRoot | Out-Null
Copy-Item -LiteralPath $baseEsp -Destination $esp -Recurse

function Read-Serial {
    if (-not (Test-Path -LiteralPath $serial -PathType Leaf)) { return '' }
    $stream = [System.IO.FileStream]::new(
        $serial,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::UTF8,$true,4096,$true)
        try { return $reader.ReadToEnd() }
        finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Count-Matches([string]$Text,[string]$Pattern) {
    if ($Pattern.EndsWith('$')) { $Pattern = $Pattern.Substring(0,$Pattern.Length - 1) + '\r?$' }
    return [System.Text.RegularExpressions.Regex]::Matches(
        $Text,$Pattern,[System.Text.RegularExpressions.RegexOptions]::Multiline).Count
}

function Wait-Pattern([string]$Pattern,[int]$After,[int]$TimeoutSeconds,[string]$Description) {
    if ($Pattern.EndsWith('$')) { $Pattern = $Pattern.Substring(0,$Pattern.Length - 1) + '\r?$' }
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $text = Read-Serial
        $tail = if ($After -le $text.Length) { $text.Substring($After) } else { '' }
        $match = [System.Text.RegularExpressions.Regex]::Match($tail,$Pattern,[System.Text.RegularExpressions.RegexOptions]::Multiline)
        if ($match.Success) { return $match }
        if ($script:qacProcess -and $script:qacProcess.HasExited) {
            throw "QAC/QEMU exited before $Description; QAC exit=$($script:qacProcess.ExitCode)."
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for $Description; pattern=$Pattern"
}

function Wait-ReadyCount([int]$Count,[int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $readyPattern = '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$'
    $inputPattern = '(?m)^\[KERNEL\] Input manager initialized'
    do {
        $text = Read-Serial
        if ((Count-Matches $text $readyPattern) -ge $Count -and
            (Count-Matches $text $inputPattern) -ge $Count) { return }
        if ($script:qacProcess -and $script:qacProcess.HasExited) {
            throw "QAC/QEMU exited before guest boot $Count became ready; QAC exit=$($script:qacProcess.ExitCode)."
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    throw "Guest boot $Count did not reach the input loop."
}

function Read-QmpResponse([System.IO.StreamReader]$Reader) {
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) { throw 'QMP closed its connection.' }
        if ($line -match '"return"|"error"') { return $line }
    }
}

function Invoke-Qmp([string]$Command,[object]$Arguments=$null) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1',$script:qmpPort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 5000
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::ASCII,$false,1024,$true)
        $writer = [System.IO.StreamWriter]::new($stream,[System.Text.Encoding]::ASCII,1024,$true)
        $writer.AutoFlush = $true
        $greeting = $reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $capabilities = Read-QmpResponse $reader
        if ($capabilities -match '"error"') { throw "QMP capability negotiation failed: $capabilities" }
        $payload = [ordered]@{execute=$Command}
        if ($null -ne $Arguments) { $payload.arguments = $Arguments }
        $request = $payload | ConvertTo-Json -Compress -Depth 10
        $writer.WriteLine($request)
        $response = Read-QmpResponse $reader
        Add-Content -LiteralPath $qmpLog -Encoding ascii -Value ("command="+$request+"`ngreeting="+$greeting+"`ncapabilities="+$capabilities+"`nresponse="+$response+"`n")
        if ($response -match '"error"') { throw "QMP $Command failed: $response" }
        return $response
    } finally { $client.Dispose() }
}

function Send-QmpEvents([object[]]$Events,[int]$DelayMilliseconds=35) {
    if ($Events.Count -eq 0) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1',$script:qmpPort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 5000
        $reader = [System.IO.StreamReader]::new($stream,[System.Text.Encoding]::ASCII,$false,1024,$true)
        $writer = [System.IO.StreamWriter]::new($stream,[System.Text.Encoding]::ASCII,1024,$true)
        $writer.AutoFlush = $true
        $greeting = $reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $capabilities = Read-QmpResponse $reader
        if ($capabilities -match '"error"') { throw "QMP capability negotiation failed: $capabilities" }
        foreach ($event in $Events) {
            $request = [ordered]@{execute='input-send-event';arguments=[ordered]@{events=@($event)}} | ConvertTo-Json -Compress -Depth 10
            $writer.WriteLine($request)
            $response = Read-QmpResponse $reader
            Add-Content -LiteralPath $qmpLog -Encoding ascii -Value ("event="+$request+"`ngreeting="+$greeting+"`ncapabilities="+$capabilities+"`nresponse="+$response+"`n")
            if ($response -match '"error"') { throw "QMP input event failed: $response" }
            if ($DelayMilliseconds -gt 0) { Start-Sleep -Milliseconds $DelayMilliseconds }
        }
    } finally { $client.Dispose() }
}

function New-RelativeMove([int]$Dx,[int]$Dy) {
    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($axis in @(@{Name='x';Delta=$Dx},@{Name='y';Delta=$Dy})) {
        $remaining = [int]$axis.Delta
        while ($remaining -ne 0) {
            $step = [Math]::Sign($remaining) * [Math]::Min([Math]::Abs($remaining),40)
            $events.Add([ordered]@{type='rel';data=[ordered]@{axis=$axis.Name;value=$step}})
            $remaining -= $step
        }
    }
    return ,$events.ToArray()
}

function Move-Pointer([int]$X,[int]$Y) {
    if ($null -eq $script:cursor) { throw 'QMP pointer has not been calibrated from guest input.' }
    $dx = $X - $script:cursor.X
    $dy = $Y - $script:cursor.Y
    if ($dx -ne 0 -or $dy -ne 0) {
        Send-QmpEvents (New-RelativeMove $dx $dy) 35
        $script:cursor = [pscustomobject]@{X=$X;Y=$Y}
    }
}

function Click([int]$X,[int]$Y) {
    Move-Pointer $X $Y
    Send-QmpEvents @(
        [ordered]@{type='btn';data=[ordered]@{button='left';down=$true}},
        [ordered]@{type='btn';data=[ordered]@{button='left';down=$false}}) 180
    Start-Sleep -Milliseconds 450
}

function Send-Key([string]$Code) {
    Send-QmpEvents @(
        [ordered]@{type='key';data=[ordered]@{down=$true;key=[ordered]@{type='qcode';data=$Code}}},
        [ordered]@{type='key';data=[ordered]@{down=$false;key=[ordered]@{type='qcode';data=$Code}}}) 80
}

function Send-ControlChord([string]$Code) {
    Send-QmpEvents @(
        [ordered]@{type='key';data=[ordered]@{down=$true;key=[ordered]@{type='qcode';data='ctrl'}}},
        [ordered]@{type='key';data=[ordered]@{down=$true;key=[ordered]@{type='qcode';data=$Code}}},
        [ordered]@{type='key';data=[ordered]@{down=$false;key=[ordered]@{type='qcode';data=$Code}}},
        [ordered]@{type='key';data=[ordered]@{down=$false;key=[ordered]@{type='qcode';data='ctrl'}}}) 90
}

function Capture([string]$Name,[string]$BootPath) {
    $path = Join-Path $BootPath ($Name + '.ppm')
    [void](Invoke-Qmp 'screendump' ([ordered]@{filename=$path}))
    return $path
}

function Get-App([string]$Name) {
    $menuCount = 22
    $menuHeight = 30 + ($menuCount * 22) + 36
    $workHeight = $script:screenHeight - 40
    $menuY = [Math]::Max(0,$workHeight - $menuHeight)
    $contentY = $menuY + 31
    switch ($Name) {
        'settings' { return [pscustomobject]@{Id='com.guidexos.apps.managed.settingscenter';X=310;Y=$contentY+(6*22)+11} }
        'explorer' { return [pscustomobject]@{Id='com.guidexos.apps.managed.fileexplorer';X=115;Y=$contentY+(15*22)+11} }
        'notes' { return [pscustomobject]@{Id='com.guidexos.apps.managed.notes';X=115;Y=$contentY+(12*22)+11} }
        'calculator' { return [pscustomobject]@{Id='com.guidexos.apps.managed.calculator';X=115;Y=$contentY+(11*22)+11} }
        'taskmanager' { return [pscustomobject]@{Id='com.guidexos.apps.managed.taskmanager';X=115;Y=$contentY+(14*22)+11} }
        default { throw "Unknown managed application: $Name" }
    }
}

function Calibrate-Pointer([int]$BootNumber) {
    $text = Read-Serial
    $width = [System.Text.RegularExpressions.Regex]::Match($text,'(?m)^\[DESKTOP CAP\] framebuffer_width=0x([0-9A-Fa-f]+)\r?$')
    $height = [System.Text.RegularExpressions.Regex]::Match($text,'(?m)^\[DESKTOP CAP\] framebuffer_height=0x([0-9A-Fa-f]+)\r?$')
    if (-not $width.Success -or -not $height.Success) { throw "Guest boot $BootNumber omitted framebuffer dimensions." }
    $script:screenWidth = [Convert]::ToInt32($width.Groups[1].Value,16)
    $script:screenHeight = [Convert]::ToInt32($height.Groups[1].Value,16)
    $start = $text.Length
    Send-QmpEvents (New-RelativeMove 1 1) 50
    $marker = Wait-Pattern '^\[C138-NATIVE-INPUT\] kind=pointer-move x=(?<x>[0-9A-Fa-f]+) y=(?<y>[0-9A-Fa-f]+) result=PASS$' $start 20 "pointer calibration in boot $BootNumber"
    $clientX = [Convert]::ToInt32($marker.Groups['x'].Value,16)
    $clientY = [Convert]::ToInt32($marker.Groups['y'].Value,16)
    $windowX = [Math]::Max(0,[int](($script:screenWidth - 600) / 2))
    $windowY = [Math]::Max(0,[int](($script:screenHeight - 360) / 2))
    $script:cursor = [pscustomobject]@{X=$clientX+$windowX;Y=$clientY+$windowY+24}
    $script:startMenuAllPrograms = $false
}

function Open-StartMenu {
    $menuCount = 22
    $menuHeight = 30 + ($menuCount * 22) + 36
    $workHeight = $script:screenHeight - 40
    $menuY = [Math]::Max(0,$workHeight - $menuHeight)
    $footerY = $menuY + 30 + ($menuCount * 22)
    Click 50 ($script:screenHeight - 20)
    Start-Sleep -Milliseconds 300
    # Opening Start Menu resets the view to Recent Programs; toggle All
    # Programs on every opening, matching the established C166 runner.
    Click 70 ($footerY + 18)
    $script:startMenuAllPrograms = $true
    Start-Sleep -Milliseconds 300
}

function Open-AppMenuItem([string]$Name,[int]$BootNumber) {
    if ($Name -eq 'settings') { throw 'Managed Settings Center opens through the resident Notes Settings action.' }
    $app = Get-App $Name
    Open-StartMenu
    $bootPath = Join-Path $laneRoot ('boot' + $BootNumber)
    if ($Name -eq 'notes') {
        $script:notesMenuCapture++
        [void](Capture ("notes-menu-boot"+$BootNumber+"-"+$script:notesMenuCapture) $bootPath)
    }
    if ($Name -eq 'explorer') {
        [void](Capture 'explorer-start-menu' $bootPath)
        Move-Pointer $app.X $app.Y
        [void](Capture 'explorer-menu-target' $bootPath)
    }
    $start = (Read-Serial).Length
    Click $app.X $app.Y
    $id = [System.Text.RegularExpressions.Regex]::Escape($app.Id)
    $selector = switch ($Name) { notes {'00000004'} calculator {'00000006'} taskmanager {'00000007'} explorer {'00000008'} }
    $pattern = '^(?:\[C150-APP-LAUNCH\] id=' + $id + ' generation=[0-9A-Fa-f]+ selector=' + $selector + ' kind=normal|\[C162-MANAGED-FOCUS\] appId=' + $id + ' source=3 instance=[0-9A-Fa-f]{16} selector=' + $selector + ' existing=true result=PASS)$'
    try {
        [void](Wait-Pattern $pattern $start 12 ("$Name pointer activation in boot $BootNumber"))
    } catch {
        $script:notesMenuCapture++
        [void](Capture ("activation-timeout-boot"+$BootNumber+"-"+$script:notesMenuCapture) $bootPath)
        # C166 observed that an All Programs row can retain keyboard
        # selection when its pointer activation is not dispatched. Use the
        # same keyboard fallback for any managed row, including Notes after
        # its window has just been closed.
        Send-Key 'ret'
        try {
            [void](Wait-Pattern $pattern $start 20 ("$Name keyboard activation in boot $BootNumber"))
        } catch {
            $script:notesMenuCapture++
            [void](Capture ("keyboard-timeout-boot"+$BootNumber+"-"+$script:notesMenuCapture) $bootPath)
            throw
        }
    }
    return $app
}

function Launch-SettingsFromNotes([int]$BootNumber) {
    [void](Open-AppMenuItem 'notes' $BootNumber)
    $notesX = [Math]::Max(0,[int](($script:screenWidth - 600) / 2))
    $notesY = [Math]::Max(0,[int](($script:screenHeight - 360) / 2))
    $start = (Read-Serial).Length
    Click ($notesX + 475) ($notesY + 24 + 294)
    [void](Wait-Pattern '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS$' $start 45 ("Settings launch in boot $BootNumber"))
    $surface = Wait-Pattern '^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.settingscenter generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ result=PASS$' $start 30 ("Settings surface creation in boot $BootNumber")
    Add-Content -LiteralPath (Join-Path $laneRoot 'launches.log') -Encoding utf8 -Value ("boot="+$BootNumber+" name=settings marker="+$surface.Value)
    [void](Capture 'after-settings' (Join-Path $laneRoot ('boot' + $BootNumber)))
    # Opening Settings from Notes closes the Notes surface. Restore Notes so
    # Settings plus all four other managed applications can be resident at
    # the same time.
    $notes = Open-AppMenuItem 'notes' $BootNumber
    $text = Read-Serial
    $normal = [System.Text.RegularExpressions.Regex]::Matches($text,'(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ selector=00000004 kind=normal\r?$')
    if ($normal.Count -gt 0) {
        Add-Content -LiteralPath (Join-Path $laneRoot 'launches.log') -Encoding utf8 -Value ("boot="+$BootNumber+" name=notes-restored marker="+$normal[$normal.Count-1].Value)
    }
}

function Launch-AllFive([int]$BootNumber,[int]$SerialOffset) {
    $notes = Wait-Pattern '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ selector=00000004 kind=normal$' $SerialOffset 25 ("startup Notes launch in boot $BootNumber")
    Add-Content -LiteralPath (Join-Path $laneRoot 'launches.log') -Encoding utf8 -Value ("boot="+$BootNumber+" name=notes marker="+$notes.Value)
    [void](Capture 'startup-notes-resident' (Join-Path $laneRoot ('boot' + $BootNumber)))
    Launch-SettingsFromNotes $BootNumber
    foreach ($name in @('explorer','calculator','taskmanager')) {
        $app = Open-AppMenuItem $name $BootNumber
        $text = Read-Serial
        $id = [System.Text.RegularExpressions.Regex]::Escape($app.Id)
        $selector = switch ($name) { explorer {'00000008'} calculator {'00000006'} taskmanager {'00000007'} }
        $normal = [System.Text.RegularExpressions.Regex]::Matches($text,'(?m)^\[C150-APP-LAUNCH\] id=' + $id + ' generation=[0-9A-Fa-f]+ selector=' + $selector + ' kind=normal\r?$')
        $focus = [System.Text.RegularExpressions.Regex]::Matches($text,'(?m)^\[C162-MANAGED-FOCUS\] appId=' + $id + ' source=3 instance=[0-9A-Fa-f]{16} selector=' + $selector + ' existing=true result=PASS\r?$')
        $marker = if ($normal.Count -gt 0) { $normal[$normal.Count-1].Value } elseif ($focus.Count -gt 0) { $focus[$focus.Count-1].Value } else { throw "Missing activation identity for $name in boot $BootNumber." }
        Add-Content -LiteralPath (Join-Path $laneRoot 'launches.log') -Encoding utf8 -Value ("boot="+$BootNumber+" name="+$name+" marker="+$marker)
        [void](Capture ('after-' + $name) (Join-Path $laneRoot ('boot' + $BootNumber)))
    }
    $text = Read-Serial
    foreach ($name in @('settings','explorer','notes','calculator','taskmanager')) {
        $app = Get-App $name
        if ($text -notmatch [System.Text.RegularExpressions.Regex]::Escape($app.Id)) {
            throw "Boot $BootNumber is missing managed app launch evidence for $name."
        }
    }
}
function Refresh-TaskManager([int]$BootNumber) {
    $app = Open-AppMenuItem 'taskmanager' $BootNumber
    $start = (Read-Serial).Length
    Send-ControlChord 'r'
    $marker = Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R st=\d+ n=(?<count>\d+).*ap=true.*$' $start 20 ("Task Manager refresh in boot $BootNumber")
    $count = [int]$marker.Groups['count'].Value
    if ($count -lt 5) { throw "Task Manager reported only $count snapshot rows in boot $BootNumber." }
    [void](Capture 'task-manager-five-residents' (Join-Path $laneRoot ('boot' + $BootNumber)))
    return $count
}

function Exercise-BootTwoActions([int]$BootNumber) {
    # Settings: toggle Input Enabled, apply from the Options menu, then reset
    # through the existing confirmation dialog.
    Launch-SettingsFromNotes $BootNumber
    $settingsStart = (Read-Serial).Length
    Click 504 317
    Start-Sleep -Milliseconds 250
    Click 836 285
    Send-Key 'down'
    Send-Key 'ret'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C144-APPLY menu=PASS dirty=cleared viewport=preserved result=PASS$' $settingsStart 20 'Settings Apply')
    Click 836 285
    Send-Key 'down'
    Send-Key 'down'
    Send-Key 'ret'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS$' $settingsStart 15 'Settings Reset confirmation')
    Send-Key 'ret'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS$' $settingsStart 15 'Settings Reset')
    [void](Capture 'settings-apply-reset' (Join-Path $laneRoot 'boot2'))

    # Explorer navigates to /system, selects a row, and refreshes the listing.
    [void](Open-AppMenuItem 'explorer' $BootNumber)
    $explorerStart = (Read-Serial).Length
    Click 306 569
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=0 path=/system n=\d+ sel=none .*' $explorerStart 15 'Explorer parent navigation')
    Click 420 310
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=0 path=/system n=\d+ sel=.+ type=(Directory|File) .*' $explorerStart 15 'Explorer row selection')
    Click 524 569
    [void](Capture 'explorer-navigation-selection' (Join-Path $laneRoot 'boot2'))

    # Notes: edit the resident document, save it, then close and reopen it.
    [void](Open-AppMenuItem 'notes' $BootNumber)
    $notesStart = (Read-Serial).Length
    Click 380 338
    Send-Key 'x'
    Send-ControlChord 's'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=s command=Save .*result=PASS.*$' $notesStart 20 'Notes save shortcut')
    [void](Capture 'notes-edited-and-saved' (Join-Path $laneRoot 'boot2'))

    # Calculator: 1 + 2 = using its visible keypad.
    [void](Open-AppMenuItem 'calculator' $BootNumber)
    $calculatorStart = (Read-Serial).Length
    Click 534 477
    Click 774 477
    Click 614 477
    Click 682 513
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C158-CALC-DISPLAY .*result=PASS$' $calculatorStart 15 'Calculator arithmetic')
    [void](Capture 'calculator-arithmetic' (Join-Path $laneRoot 'boot2'))

    # Task Manager refreshes after the interaction and confirms five rows.
    [void](Refresh-TaskManager 2)
}

function Save-BootEvidence([int]$BootNumber,[int]$SerialOffset,[int]$QmpOffset) {
    $bootPath = Join-Path $laneRoot ('boot' + $BootNumber)
    New-Item -ItemType Directory -Force -Path $bootPath | Out-Null
    $text = Read-Serial
    if ($SerialOffset -gt $text.Length) { $SerialOffset = 0 }
    [System.IO.File]::WriteAllText((Join-Path $bootPath 'serial.log'),$text.Substring($SerialOffset),[System.Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $qmpLog) {
        $qmpText = [System.IO.File]::ReadAllText($qmpLog)
        if ($QmpOffset -gt $qmpText.Length) { $QmpOffset = 0 }
        [System.IO.File]::WriteAllText((Join-Path $bootPath 'qmp.log'),$qmpText.Substring($QmpOffset),[System.Text.UTF8Encoding]::new($false))
    }
    $launchPattern = '(?m)^\[C150-APP-LAUNCH\] id=([^ ]+) generation=([0-9A-Fa-f]+) selector=([0-9A-Fa-f]+) kind=normal\r?$'
    $matches = [System.Text.RegularExpressions.Regex]::Matches($text.Substring($SerialOffset),$launchPattern)
    $launches = @($matches | ForEach-Object { [pscustomobject]@{AppId=$_.Groups[1].Value;Generation=$_.Groups[2].Value;Selector=$_.Groups[3].Value} })
    $launches | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bootPath 'instance-identities.json') -Encoding utf8
}

$statusBefore = (& $qacCli status 2>&1 | Out-String)
$listBefore = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $laneRoot 'qac-before.status.txt') -Value $statusBefore -Encoding utf8
Set-Content -LiteralPath (Join-Path $laneRoot 'qac-before.list.txt') -Value $listBefore -Encoding utf8
if ($statusBefore -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
    $listBefore -notmatch 'Reservations:\s+0') {
    throw 'QAC census is not empty; no QEMU was launched.'
}

$script:qacProcess = $null
$script:qmpPort = 0
$script:cursor = $null
$script:screenWidth = 0
$script:screenHeight = 0
$script:startMenuAllPrograms = $false
$script:notesMenuCapture = 0
$stdoutTask = $null
$stderrTask = $null
$qemuArgs = @(
    '-accel','tcg','-machine','pc','-smp','1',
    '-drive',('if=pflash,format=raw,readonly=on,file='+$ovmf),
    '-drive',('file=fat:rw:'+ $esp + ',format=raw,if=ide,index=0'),
    '-m','1024M','-vga','std','-display','none',
    '-serial',('file:'+ $serial),
    '-qmp',('tcp:127.0.0.1:{0},server,nowait' -f 0),
    '-boot','order=c','-no-reboot','-no-shutdown','-rtc','base=utc,clock=host')
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$listener.Start()
$script:qmpPort = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
$listener.Stop()
$qemuArgs[19] = ('tcp:127.0.0.1:{0},server,nowait' -f $script:qmpPort)
$argsEvidence = @('run','--project','guideXOSServer','--mode','acceptance','--',$qemu) + $qemuArgs
$argsEvidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $laneRoot 'qac-run-arguments.json') -Encoding utf8

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $qacCli
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($argument in $argsEvidence) { [void]$psi.ArgumentList.Add([string]$argument) }
$script:qacProcess = [System.Diagnostics.Process]::Start($psi)
$stdoutTask = $script:qacProcess.StandardOutput.ReadToEndAsync()
$stderrTask = $script:qacProcess.StandardError.ReadToEndAsync()

$serialOffset = 0
$qmpOffset = 0
try {
    for ($boot = 1; $boot -le 3; $boot++) {
        $bootPath = Join-Path $laneRoot ('boot' + $boot)
        New-Item -ItemType Directory -Force -Path $bootPath | Out-Null
        if ($boot -gt 1) {
            $beforeReset = Read-Serial
            $serialOffset = $beforeReset.Length
            if (Test-Path -LiteralPath $qmpLog) { $qmpOffset = ([System.IO.File]::ReadAllText($qmpLog)).Length }
            [void](Invoke-Qmp 'system_reset')
            $script:cursor = $null
        }
        Wait-ReadyCount $boot 180
        Calibrate-Pointer $boot
        $text = Read-Serial
        if ($boot -eq 1) {
            $qacLive = (& $qacCli list 2>&1 | Out-String)
            Set-Content -LiteralPath (Join-Path $laneRoot 'qac-during.status.txt') -Value ((& $qacCli status 2>&1 | Out-String)) -Encoding utf8
            Set-Content -LiteralPath (Join-Path $laneRoot 'qac-during.list.txt') -Value $qacLive -Encoding utf8
            if ($qacLive -notmatch 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running') {
                throw 'QAC did not report this C181 process as a running exclusive acceptance reservation.'
            }
        }
        [void](Capture 'desktop-ready' $bootPath)
        if ($boot -eq 1) {
            Launch-AllFive $boot $serialOffset
            [void](Refresh-TaskManager $boot)
            [void](Capture 'five-apps-resident' $bootPath)
            $notes = Open-AppMenuItem 'notes' $boot
            $closeStart = (Read-Serial).Length
            Click 926 232
            Start-Sleep -Milliseconds 700
            [void](Capture 'after-notes-close' $bootPath)
            $afterClose = Read-Serial
            $normalBefore = Count-Matches $afterClose '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes .*kind=normal$'
            $notes = Open-AppMenuItem 'notes' $boot
            $deadline = (Get-Date).AddSeconds(15)
            do {
                $afterReopen = Read-Serial
                $normalAfter = Count-Matches $afterReopen '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes .*kind=normal$'
                if ($normalAfter -gt $normalBefore) { break }
                Start-Sleep -Milliseconds 100
            } while ((Get-Date) -lt $deadline)
            if ($normalAfter -le $normalBefore) { throw 'Notes did not relaunch after the close/reopen lifecycle check.' }
            [void](Refresh-TaskManager $boot)
        } elseif ($boot -eq 2) {
            Launch-AllFive $boot $serialOffset
            [void](Capture 'five-apps-resident' $bootPath)
            Exercise-BootTwoActions $boot
        } else {
            Launch-AllFive $boot $serialOffset
            [void](Capture 'five-apps-resident' $bootPath)
            [void](Refresh-TaskManager $boot)
            for ($cycle = 0; $cycle -lt 2; $cycle++) {
                [void](Open-AppMenuItem 'notes' $boot)
                Click 926 232
                Start-Sleep -Milliseconds 350
                [void](Open-AppMenuItem 'notes' $boot)
            }
            [void](Capture 'stress-recovery' $bootPath)
        }
        $current = Read-Serial
        if ((Count-Matches $current '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes .*kind=normal$') -lt $boot) {
            throw "Boot $boot did not launch Managed Notes."
        }
        Save-BootEvidence $boot $serialOffset $qmpOffset
        Write-Output ("BOOT="+$boot+" READY=PASS SERIAL_CHARS="+$current.Length)
    }
    [void](Invoke-Qmp 'quit')
    if (-not $script:qacProcess.WaitForExit(90000)) {
        throw 'QAC did not verify QEMU exit after the QMP quit request; the managed process was left untouched.'
    }
} catch {
    if ($script:qacProcess -and -not $script:qacProcess.HasExited) {
        if ($boot -gt 1) {
            $bootPath = Join-Path $laneRoot ('boot' + $boot)
            try {
                $resetStatus = Invoke-Qmp 'query-status'
                Set-Content -LiteralPath (Join-Path $bootPath 'reset-timeout-qmp-status.json') -Value $resetStatus -Encoding utf8
            } catch { }
            try { [void](Capture ("reset-timeout-boot" + $boot) $bootPath) } catch { }
        }
        try { [void](Invoke-Qmp 'quit') } catch { }
        [void]$script:qacProcess.WaitForExit(90000)
    }
    throw
} finally {
    if ($script:qacProcess) {
        if ($script:qacProcess.HasExited) {
            $stdout = $stdoutTask.GetAwaiter().GetResult()
            $stderr = $stderrTask.GetAwaiter().GetResult()
            Set-Content -LiteralPath $qacStdoutPath -Value $stdout -Encoding utf8
            Set-Content -LiteralPath $qacStderrPath -Value $stderr -Encoding utf8
            Set-Content -LiteralPath (Join-Path $laneRoot 'qac-exit-code.txt') -Value $script:qacProcess.ExitCode -Encoding ascii
        }
    }
}

$statusAfter = (& $qacCli status 2>&1 | Out-String)
$listAfter = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $laneRoot 'qac-after.status.txt') -Value $statusAfter -Encoding utf8
Set-Content -LiteralPath (Join-Path $laneRoot 'qac-after.list.txt') -Value $listAfter -Encoding utf8
if ($script:qacProcess.ExitCode -ne 0) { throw "QAC run returned QEMU exit code $($script:qacProcess.ExitCode)." }
if ($statusAfter -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
    $listAfter -notmatch 'Reservations:\s+0') {
    throw 'QAC did not confirm reservation release and an empty post-run census.'
}

$fullSerial = Read-Serial
Set-Content -LiteralPath (Join-Path $laneRoot 'serial-all-boots.log') -Value $fullSerial -Encoding utf8
$manifest = [ordered]@{
    lane=$Lane
    runLabel=$RunLabel
    evidenceRoot=$root
    boots=3
    qacMode='acceptance'
    qacProject='guideXOSServer'
    qacCli=$qacCli
    qacRunArguments=$argsEvidence
    qacExitCode=$script:qacProcess.ExitCode
    qacReleaseVerified=($statusAfter -match 'QEMU processes:\s+0 \(0 foreign\)' -and $listAfter -match 'Reservations:\s+0')
    bootReadyMarkers=(Count-Matches $fullSerial '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$')
    normalAppLaunches=(Count-Matches $fullSerial '(?m)^\[C150-APP-LAUNCH\] id=[^ ]+ generation=([0-9A-Fa-f]+) selector=([0-9A-Fa-f]+) kind=normal\r?$')
    qemuArguments=$qemuArgs
    serialLog=(Join-Path $laneRoot 'serial-all-boots.log')
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $laneRoot 'sequence-manifest.json') -Encoding utf8
Write-Output ("QAC_RELEASE=VERIFIED LANE="+$Lane+" BOOTS="+$manifest.bootReadyMarkers+" NORMAL_LAUNCHES="+$manifest.normalAppLaunches)
