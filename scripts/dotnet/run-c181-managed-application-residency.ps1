param(
    [Parameter(Mandatory=$true)][ValidateSet('focused','production')][string]$Lane,
    [Parameter()][ValidatePattern('^[a-z0-9-]+$')][string]$RunLabel = 'attempt-01',
    [Parameter()][string]$EvidenceRoot,
    [Parameter()][ValidateRange(0,3)][int]$SingleBootNumber = 0
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'The C181 runner requires PowerShell Core (pwsh); Windows PowerShell 5.1 does not support ProcessStartInfo.ArgumentList.'
}

if ($SingleBootNumber -ne 0 -and $Lane -ne 'production') {
    throw 'SingleBootNumber is reserved for independent ordinary production boots.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$artifactRoot = Join-Path $repo 'out/dotnet/c181-managed-application-residency'
$root = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $artifactRoot
} else {
    [System.IO.Path]::GetFullPath($EvidenceRoot)
}
$laneRoot = Join-Path $root ('guest-runs/' + $Lane + '-' + $RunLabel + '-qac-sequence')
$baseEsp = if ($Lane -eq 'focused') {
    Join-Path $artifactRoot 'media/esp-focused-c181-qac'
} else {
    Join-Path $artifactRoot 'media/esp-production-c181-qac'
}
$proofKernelPath = if ($Lane -eq 'focused') {
    Join-Path $artifactRoot 'media/proof-esp/kernel.elf'
} else { $null }
$esp = Join-Path $laneRoot 'esp'
$serial = Join-Path $laneRoot 'serial.log'
$qmpLog = Join-Path $laneRoot 'qmp.log'
$qacStdoutPath = Join-Path $laneRoot 'qac.stdout.log'
$qacStderrPath = Join-Path $laneRoot 'qac.stderr.log'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = Join-Path $repo 'OVMF.fd'
Import-Module (Join-Path $PSScriptRoot 'c181-qmp.psm1') -Force -ErrorAction Stop

function Get-MediaHashes([string]$EspPath) {
    $relativePaths = @('kernel.elf','ramdisk.img','EFI/BOOT/BOOTX64.EFI')
    $hashes = [ordered]@{}
    foreach ($relativePath in $relativePaths) {
        $path = Join-Path $EspPath $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidate media is missing $relativePath in $EspPath." }
        $hashes[$relativePath] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
    return $hashes
}

if (-not (Test-Path -LiteralPath $baseEsp -PathType Container)) { throw "Missing staged ESP: $baseEsp" }
if ($Lane -eq 'focused' -and -not (Test-Path -LiteralPath $proofKernelPath -PathType Leaf)) {
    throw "Focused lane requires the staged C181 proof kernel: $proofKernelPath."
}
if (-not (Test-Path -LiteralPath $qacCli -PathType Leaf)) { throw "QAC CLI is unavailable: $qacCli" }
if (-not (Test-Path -LiteralPath $qemu -PathType Leaf)) { throw "QEMU is unavailable: $qemu" }
if (-not (Test-Path -LiteralPath $ovmf -PathType Leaf)) { throw "OVMF is unavailable: $ovmf" }
if (Test-Path -LiteralPath $laneRoot) { throw "Refusing to overwrite sequence evidence: $laneRoot" }
$proofMarkers = @('C171-WINDOW-CREATE','C172-COMPOSITOR-TRACE','C175-NATIVE-GEOMETRY')
$sourceKernelPath = if ($Lane -eq 'focused') { $proofKernelPath } else { Join-Path $baseEsp 'kernel.elf' }
$sourceKernel = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($sourceKernelPath))
$presentProofMarkers = @($proofMarkers | Where-Object { $sourceKernel.Contains($_) })
if ($Lane -eq 'focused' -and $presentProofMarkers.Count -ne $proofMarkers.Count) {
    $missingProofMarkers = @($proofMarkers | Where-Object { -not $sourceKernel.Contains($_) }) -join ', '
    throw "Focused lane requires the proof-controlled C181 kernel; staged kernel is missing: $missingProofMarkers ($baseEsp)."
}
if ($Lane -eq 'production' -and $presentProofMarkers.Count -ne 0) {
    throw "Production lane must use a proof-disabled kernel; proof-only marker found in $baseEsp."
}
$statusBefore = (& $qacCli status 2>&1 | Out-String)
$listBefore = (& $qacCli list 2>&1 | Out-String)
if ($statusBefore -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
    $listBefore -notmatch 'Reservations:\s+0') {
    throw 'QAC census is not empty; no QEMU was launched.'
}
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $root | Out-Null
}
New-Item -ItemType Directory -Force -Path $laneRoot | Out-Null
Set-Content -LiteralPath (Join-Path $laneRoot 'qac-before.status.txt') -Value $statusBefore -Encoding utf8
Set-Content -LiteralPath (Join-Path $laneRoot 'qac-before.list.txt') -Value $listBefore -Encoding utf8
$sourceMediaHashes = Get-MediaHashes $baseEsp
Copy-Item -LiteralPath $baseEsp -Destination $esp -Recurse
if ($Lane -eq 'focused') {
    # Use the C181 proof kernel for compositor diagnostics, while keeping the
    # regular focused app payload out of the separate C166 proof-selector path.
    Copy-Item -LiteralPath $proofKernelPath -Destination (Join-Path $esp 'kernel.elf') -Force
}
$candidateMediaHashes = Get-MediaHashes $esp
foreach ($relativePath in $candidateMediaHashes.Keys) {
    $expectedHash = if ($Lane -eq 'focused' -and $relativePath -eq 'kernel.elf') {
        (Get-FileHash -LiteralPath $proofKernelPath -Algorithm SHA256).Hash
    } else { $sourceMediaHashes[$relativePath] }
    if ($expectedHash -ne $candidateMediaHashes[$relativePath]) {
        throw "Staged media hash mismatch for $relativePath."
    }
}

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

function Wait-ReadyCount([int]$Count,[int]$TimeoutSeconds,[int]$After=0) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $readyPattern = '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$'
    $inputPattern = '(?m)^\[KERNEL\] Input manager initialized'
    do {
        $text = Read-Serial
        $tail = if ($After -le $text.Length) { $text.Substring($After) } else { '' }
        if ((Count-Matches $tail $readyPattern) -ge 1 -and
            (Count-Matches $tail $inputPattern) -ge 1) { return }
        if ($script:qacProcess -and $script:qacProcess.HasExited) {
            throw "QAC/QEMU exited before guest boot $Count became ready; QAC exit=$($script:qacProcess.ExitCode)."
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    throw "Guest boot $Count did not reach the input loop."
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
        $capabilities = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command 'qmp_capabilities'
        if ($capabilities -match '"error"') { throw "QMP capability negotiation failed: $capabilities" }
        $payload = [ordered]@{execute=$Command}
        if ($null -ne $Arguments) { $payload.arguments = $Arguments }
        $request = $payload | ConvertTo-Json -Compress -Depth 10
        $writer.WriteLine($request)
        $response = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command $Command
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
        $capabilities = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command 'qmp_capabilities'
        if ($capabilities -match '"error"') { throw "QMP capability negotiation failed: $capabilities" }
        foreach ($event in $Events) {
            $request = [ordered]@{execute='input-send-event';arguments=[ordered]@{events=@($event)}} | ConvertTo-Json -Compress -Depth 10
            $writer.WriteLine($request)
            $response = Read-C181QmpResponse -Reader $reader -LogPath $qmpLog -Command 'input-send-event'
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

function Send-ShiftKey([string]$Code) {
    Send-QmpEvents @(
        [ordered]@{type='key';data=[ordered]@{down=$true;key=[ordered]@{type='qcode';data='shift'}}},
        [ordered]@{type='key';data=[ordered]@{down=$true;key=[ordered]@{type='qcode';data=$Code}}},
        [ordered]@{type='key';data=[ordered]@{down=$false;key=[ordered]@{type='qcode';data=$Code}}},
        [ordered]@{type='key';data=[ordered]@{down=$false;key=[ordered]@{type='qcode';data='shift'}}}) 90
}

function Drag-Pointer([int]$StartX,[int]$StartY,[int]$EndX,[int]$EndY) {
    Move-Pointer $StartX $StartY
    $events = [System.Collections.Generic.List[object]]::new()
    $events.Add([ordered]@{type='btn';data=[ordered]@{button='left';down=$true}})
    foreach ($event in (New-RelativeMove ($EndX-$StartX) ($EndY-$StartY))) {
        $events.Add($event)
    }
    $events.Add([ordered]@{type='btn';data=[ordered]@{button='left';down=$false}})
    # Keep button-down, pointer movement, and release on one QMP connection.
    Send-QmpEvents $events.ToArray() 90
    $script:cursor = [pscustomobject]@{X=$EndX;Y=$EndY}
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
    if ($script:screenWidth -lt 2 -or $script:screenHeight -lt 2) { throw "Guest boot $BootNumber reported invalid framebuffer dimensions." }
    $start = $text.Length
    Send-QmpEvents (New-RelativeMove 1 1) 50
    # QEMU's VGA relative pointer starts at the framebuffer center. C138 is
    # app-local and is absent when startup has no scrollable app under the
    # pointer, so it cannot be a required global-position calibration signal.
    $script:cursor = [pscustomobject]@{
        X=[Math]::Min($script:screenWidth-1,[int]($script:screenWidth/2)+1)
        Y=[Math]::Min($script:screenHeight-1,[int]($script:screenHeight/2)+1)
    }
    Start-Sleep -Milliseconds 250
    $afterProbe = Read-Serial
    $probeTail = if ($start -le $afterProbe.Length) { $afterProbe.Substring($start) } else { '' }
    $localMove = [System.Text.RegularExpressions.Regex]::Match(
        $probeTail,'(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move x=(?<x>[0-9A-Fa-f]+) y=(?<y>[0-9A-Fa-f]+) result=PASS\r?$')
    $script:pointerCalibration = [ordered]@{
        method='qemu-vga-default-center-plus-relative-probe'
        framebuffer=[ordered]@{width=$script:screenWidth;height=$script:screenHeight}
        inferredDesktop=[ordered]@{x=$script:cursor.X;y=$script:cursor.Y}
        appLocalProbeObserved=$localMove.Success
        appLocalProbe=if ($localMove.Success) {
            [ordered]@{x=[Convert]::ToInt32($localMove.Groups['x'].Value,16);y=[Convert]::ToInt32($localMove.Groups['y'].Value,16)}
        } else { $null }
    }
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

function Open-AppMenuItem([string]$Name,[int]$BootNumber,[switch]$NoFocusFallback) {
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
    if ($Lane -eq 'production') {
        Start-Sleep -Milliseconds 500
        $productionText = Read-Serial
        if (-not $NoFocusFallback -and -not [System.Text.RegularExpressions.Regex]::IsMatch($productionText.Substring($start),$pattern,[System.Text.RegularExpressions.RegexOptions]::Multiline)) {
            # The proof-disabled image may omit internal focus logs. Use the
            # ordinary taskbar path and a visible titlebar capture instead.
            [void](Focus-ResidentApp $Name $BootNumber)
        }
        return $app
    }
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
    $surfacePattern = '^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.settingscenter generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ result=PASS\r?$'
    $currentText = Read-Serial
    $currentTail = if ($start -le $currentText.Length) { $currentText.Substring($start) } else { '' }
    $surface = [System.Text.RegularExpressions.Regex]::Match($currentTail,$surfacePattern,[System.Text.RegularExpressions.RegexOptions]::Multiline)
    if (-not $surface.Success) {
        $bootTail = if ($script:serialOffset -le $currentText.Length) { $currentText.Substring($script:serialOffset) } else { '' }
        $bootSurfaces = [System.Text.RegularExpressions.Regex]::Matches($bootTail,$surfacePattern,[System.Text.RegularExpressions.RegexOptions]::Multiline)
        if ($bootSurfaces.Count -gt 0) {
            $focusPattern = '^\[C162-MANAGED-FOCUS\] appId=com\.guidexos\.apps\.managed\.settingscenter source=\d+ instance=[0-9A-Fa-f]+ selector=[0-9A-Fa-f]+ existing=true result=PASS\r?$'
            $surface = Wait-Pattern $focusPattern $start 15 ("Settings existing-surface focus in boot $BootNumber")
        } else {
            $surface = Wait-Pattern $surfacePattern $start 30 ("Settings surface creation in boot $BootNumber")
        }
    }
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
    foreach ($name in @('settings','explorer','notes','calculator','taskmanager')) {
        [void](Focus-ResidentApp $name $BootNumber)
    }
}
function Refresh-TaskManager([int]$BootNumber,[string]$CaptureName='task-manager-five-residents',[int]$ExpectedCount=5) {
    $app = Open-AppMenuItem 'taskmanager' $BootNumber
    $start = (Read-Serial).Length
    Send-ControlChord 'r'
    $marker = Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R st=\d+ n=(?<count>\d+).*ap=true.*$' $start 20 ("Task Manager refresh in boot $BootNumber")
    $count = [int]$marker.Groups['count'].Value
    if ($count -ne $ExpectedCount) { throw "Task Manager reported $count snapshot rows in boot $BootNumber; expected $ExpectedCount." }
    [void](Capture $CaptureName (Join-Path $laneRoot ('boot' + $BootNumber)))
    return $count
}

function Get-ResidentResizeSpec([string]$Name) {
    switch ($Name) {
        'settings' { return [pscustomobject]@{Name=$Name;Id='com.guidexos.apps.managed.settingscenter';Selector='00000005';Width=560;Height=340;TargetWidth=720;TargetHeight=420;NativeLabel='C172-NATIVE-GEOMETRY';NativeOwner='app=ManagedSettings';ManagedPattern='^\[C102-MANAGED-OUTPUT\] C172-MANAGED-GEOMETRY layout=recomputed popup=dismissed capture=none focus=retained result=PASS\r?$';TitlebarRgb='43,80,111'} }
        'explorer' { return [pscustomobject]@{Name=$Name;Id='com.guidexos.apps.managed.fileexplorer';Selector='00000008';TaskbarX=351;Width=800;Height=390;TargetWidth=600;TargetHeight=360;NativeLabel='C173-NATIVE-GEOMETRY';NativeOwner='appId=com.guidexos.apps.managed.fileexplorer';ManagedPattern='^\[C102-MANAGED-OUTPUT\] C173-MANAGED-GEOMETRY size=600x360 path=1 selection=1 scroll=\d+ focus=1 popup=0 result=PASS\r?$';TitlebarRgb='43,80,111'} }
        'notes' { return [pscustomobject]@{Name=$Name;Id='com.guidexos.apps.managed.notes';Selector='00000004';TaskbarX=617;Width=600;Height=360;TargetWidth=560;TargetHeight=344;NativeLabel='C175-NATIVE-GEOMETRY';NativeOwner='appId=com.guidexos.apps.managed.notes';ManagedPattern='^\[C102-MANAGED-OUTPUT\] C175-MANAGED-GEOMETRY size=560x344 client=560x320 editor=\d+x\d+ result=PASS\r?$';TitlebarRgb='85,136,170'} }
        'calculator' { return [pscustomobject]@{Name=$Name;Id='com.guidexos.apps.managed.calculator';Selector='00000006';TaskbarX=497;Width=292;Height=270;TargetWidth=512;TargetHeight=420;NativeLabel='C178-NATIVE-GEOMETRY';NativeOwner='appId=com.guidexos.apps.managed.calculator';ManagedPattern='^\[C102-MANAGED-OUTPUT\] C178-CALC-GEOMETRY outer=512x420 client=512x396 disp=56 phase=3 op=3 len=2 focus=18 result=PASS\r?$';TitlebarRgb='43,80,111'} }
        'taskmanager' { return [pscustomobject]@{Name=$Name;Id='com.guidexos.apps.managed.taskmanager';Selector='00000007';TaskbarX=741;Width=800;Height=370;TargetWidth=800;TargetHeight=344;NativeLabel='C179-NATIVE-GEOMETRY';NativeOwner='appId=com.guidexos.apps.managed.taskmanager';ManagedPattern='^\[C102-MANAGED-OUTPUT\] C179-TM-GEOMETRY outer=800x344 client=800x320 list=45x10 rows=\d+ sel=\S+ scroll=\d+ focus=\d+ snap=1 id=1 result=PASS\r?$';TitlebarRgb='43,80,111'} }
        default { throw "Unknown resident app resize target: $Name" }
    }
}

function Get-PpmTitlebarWidth([string]$Path,[string]$Rgb) {
    $channels = @($Rgb.Split(',') | ForEach-Object { [int]$_ })
    if ($channels.Count -ne 3) { throw "Invalid titlebar RGB triplet: $Rgb" }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $cursor = 0
    $tokens = [System.Collections.Generic.List[string]]::new()
    while ($tokens.Count -lt 4) {
        while ($cursor -lt $bytes.Length) {
            $value = $bytes[$cursor]
            if ($value -eq 35) {
                while ($cursor -lt $bytes.Length -and $bytes[$cursor] -ne 10) { $cursor++ }
                continue
            }
            if ($value -in @(9,10,13,32)) { $cursor++; continue }
            break
        }
        $start = $cursor
        while ($cursor -lt $bytes.Length -and $bytes[$cursor] -notin @(9,10,13,32)) { $cursor++ }
        if ($start -eq $cursor) { throw "Malformed PPM screenshot: $Path" }
        $tokens.Add([System.Text.Encoding]::ASCII.GetString($bytes,$start,$cursor-$start))
    }
    if ($tokens[0] -ne 'P6' -or [int]$tokens[3] -ne 255) { throw "Unsupported PPM screenshot: $Path" }
    $width = [int]$tokens[1]
    $height = [int]$tokens[2]
    if ($cursor -ge $bytes.Length -or $bytes[$cursor] -notin @(9,10,13,32)) { throw "PPM screenshot has no pixel separator: $Path" }
    if ($bytes[$cursor] -eq 13 -and $cursor+1 -lt $bytes.Length -and $bytes[$cursor+1] -eq 10) { $cursor += 2 }
    else { $cursor++ }
    $pixelStart = $cursor
    if ($bytes.Length -lt $pixelStart + ($width*$height*3)) { throw "Truncated PPM screenshot: $Path" }
    $longest = 0
    for ($y=0; $y -lt $height; $y++) {
        $run = 0
        for ($x=0; $x -lt $width; $x++) {
            $pixel = $pixelStart + (($y*$width+$x)*3)
            if ($bytes[$pixel] -eq $channels[0] -and $bytes[$pixel+1] -eq $channels[1] -and $bytes[$pixel+2] -eq $channels[2]) {
                $run++
                if ($run -gt $longest) { $longest = $run }
            } else { $run = 0 }
        }
    }
    if ($longest -lt 250) { throw "Focused resident window titlebar was not measurable in $Path (longest run $longest)." }
    return $longest + 2
}

function Focus-ResidentApp([string]$Name,[int]$BootNumber,[int]$ExpectedWidth=0) {
    $spec = Get-ResidentResizeSpec $Name
    if ($ExpectedWidth -le 0) { $ExpectedWidth = $spec.Width }
    $before = Read-Serial
    $surfacePattern = '(?m)^\[C150-SURFACE\] action=create appId=' + [regex]::Escape($spec.Id) + ' generation=(?<generation>[0-9A-Fa-f]{8}) window=(?<window>[0-9A-Fa-f]{8}) result=PASS\r?$'
    $surfaces = [System.Text.RegularExpressions.Regex]::Matches($before,$surfacePattern)
    if ($surfaces.Count -eq 0) { throw "No resident surface identity exists for $Name." }
    $surface = $surfaces[$surfaces.Count-1]
    $focusPattern = '(?m)^\[C162-MANAGED-FOCUS\] appId=' + [regex]::Escape($spec.Id) + ' source=3 instance=(?<instance>[0-9A-Fa-f]{16}) selector=' + $spec.Selector + ' existing=true result=PASS\r?$'
    $bootPath = Join-Path $laneRoot ('boot'+$BootNumber)
    $focusRoute = if ($Lane -eq 'focused') {'start-menu-existing-instance'} else {'start-menu-visible-window'}
    $focusScreenshot = $null
    $measuredWidth = 0
    if ($Name -eq 'settings') {
        $focusStart = $before.Length
        $focus = [System.Text.RegularExpressions.Regex]::Match('', $focusPattern)
        $taskbarProbes = [System.Collections.Generic.List[object]]::new()
        for ($x=120; $x -le ($script:screenWidth-100); $x+=80) {
            Click $x ($script:screenHeight-13)
            $candidatePath = Capture ("focus-settings-taskbar-x"+$x+"-"+([guid]::NewGuid().ToString('N').Substring(0,6))) $bootPath
            $candidateWidth = 0
            try { $candidateWidth = Get-PpmTitlebarWidth $candidatePath $spec.TitlebarRgb } catch { }
            $taskbarProbes.Add([ordered]@{x=$x;screenshot=$candidatePath;measuredTitlebarWidth=$candidateWidth})
            if ([Math]::Abs($candidateWidth-$ExpectedWidth) -le 2) {
                $focusScreenshot = $candidatePath
                $measuredWidth = $candidateWidth
                $focusRoute = 'taskbar-titlebar-width-scan-x' + $x
                break
            }
        }
        @($taskbarProbes) | ConvertTo-Json -Depth 5 |
            Set-Content -LiteralPath (Join-Path $bootPath 'settings-taskbar-focus-probes.json') -Encoding utf8
        if (-not $focusScreenshot) {
            throw "Settings taskbar focus scan did not find a $ExpectedWidth-pixel titlebar; probed $($taskbarProbes.Count) positions."
        }
    } else {
        $focusStart = (Read-Serial).Length
        [void](Open-AppMenuItem $Name $BootNumber -NoFocusFallback)
        $focus = [System.Text.RegularExpressions.Regex]::Match((Read-Serial).Substring($focusStart),$focusPattern)
        if ($Lane -eq 'focused' -and -not $focus.Success) {
            $focus = Wait-Pattern $focusPattern $focusStart 12 "$Name managed instance focus identity"
        }
    }
    if (-not $focusScreenshot) {
        $focusScreenshot = Capture ("focus-"+$Name+"-"+([guid]::NewGuid().ToString('N').Substring(0,8))) $bootPath
        $measuredWidth = Get-PpmTitlebarWidth $focusScreenshot $spec.TitlebarRgb
    }
    if ([Math]::Abs($measuredWidth-$ExpectedWidth) -gt 2) { throw "$Name focus screenshot measured width $measuredWidth; expected $ExpectedWidth." }
    return [pscustomobject]@{
        identity=[ordered]@{
            instance=if ($focus.Success) {$focus.Groups['instance'].Value} else {$null}
            instanceSource=if ($focus.Success) {'C162-MANAGED-FOCUS'} else {'surface-generation-window'}
            generation=$surface.Groups['generation'].Value.ToUpperInvariant()
            window=$surface.Groups['window'].Value.ToUpperInvariant()
        }
        createCount=$surfaces.Count
        screenshot=$focusScreenshot;screenshotSha256=(Get-FileHash -LiteralPath $focusScreenshot -Algorithm SHA256).Hash
        measuredTitlebarWidth=$measuredWidth;focusRoute=$focusRoute;focusMarker=if ($focus.Success) {$focus.Value} else {$null}
    }
}

function Resize-ResidentApp([string]$Name,[int]$BootNumber) {
    $spec = Get-ResidentResizeSpec $Name
    $residentCount = Refresh-TaskManager $BootNumber ("five-residents-before-resize-"+$Name)
    if ($residentCount -ne 5) { throw "Task Manager reported $residentCount residents before resizing $Name; expected exactly five." }
    $identity = Focus-ResidentApp $Name $BootNumber $spec.Width
    $originX = [Math]::Max(0,[int](($script:screenWidth-$spec.Width)/2))
    $originY = [Math]::Max(0,[int](($script:screenHeight-$spec.Height)/2))
    $startX = $originX + $spec.Width - 4
    $startY = $originY + $spec.Height - 4
    $targetX = $startX + ($spec.TargetWidth-$spec.Width)
    $targetY = $startY + ($spec.TargetHeight-$spec.Height)
    $hoverStart = (Read-Serial).Length
    Move-Pointer $startX $startY
    $hoverPattern = '(?m)^\[C172-COMPOSITOR-TRACE\] action=resize-hover x=' + ('{0:X8}' -f $startX) + ' y=' + ('{0:X8}' -f $startY) + ' window=' + [regex]::Escape($identity.identity.window) + ' flags=[0-9A-Fa-f]{8}\r?$'
    if ($Lane -eq 'focused') { [void](Wait-Pattern $hoverPattern $hoverStart 8 "$Name resize-corner hit test") }
    Start-Sleep -Milliseconds 250
    $markerStart = (Read-Serial).Length
    Drag-Pointer $startX $startY $targetX $targetY
    $nativePattern = '(?m)^\[' + $spec.NativeLabel + '\] ' + $spec.NativeOwner + ' instance=(?<instance>[0-9A-Fa-f]{16}) generation=(?<generation>[0-9A-Fa-f]{8}) window=(?<window>[0-9A-Fa-f]{8}) oldOuter=[0-9A-Fa-f]{8}x[0-9A-Fa-f]{8} newOuter=' + ('{0:X8}' -f $spec.TargetWidth) + 'x' + ('{0:X8}' -f $spec.TargetHeight) + ' client=[0-9A-Fa-f]{8}x[0-9A-Fa-f]{8} result=PASS\r?$'
    $native = $null
    if ($Lane -eq 'focused') {
        $native = Wait-Pattern $nativePattern $markerStart 20 "$Name native resize callback"
        if ($native.Groups['generation'].Value.ToUpperInvariant() -ne $identity.identity.generation -or
            $native.Groups['window'].Value.ToUpperInvariant() -ne $identity.identity.window) {
            throw "$Name resize changed its surface generation or native window identity."
        }
        if ($identity.identity.instance) {
            if ($native.Groups['instance'].Value -ne $identity.identity.instance) {
                throw "$Name resize changed its native application instance."
            }
        } else {
            $identity.identity.instance = $native.Groups['instance'].Value
            $identity.identity.instanceSource = 'native-resize-callback'
        }
        [void](Wait-Pattern $spec.ManagedPattern $markerStart 15 "$Name managed resize layout callback")
        if ($Name -eq 'notes') {
            [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C175-MANAGED-STATE caret=\d+ anchor=\d+ view=\d+ h=\d+ i=\d+ undo=[01] redo=[01] dirty=0 doc=1 focus=1 result=PASS\r?$' $markerStart 10 'Notes document state preservation after resize')
        } elseif ($Name -eq 'calculator') {
            [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C178-CALC-STATE controls=18 commands=\d+ state=preserved capture=none result=PASS\r?$' $markerStart 10 'Calculator state preservation after resize')
        }
    } else {
        Start-Sleep -Milliseconds 500
    }
    $screenshot = Capture ("resize-"+$Name+"-"+$spec.TargetWidth+"x"+$spec.TargetHeight) (Join-Path $laneRoot ('boot'+$BootNumber))
    $measuredWidth = Get-PpmTitlebarWidth $screenshot $spec.TitlebarRgb
    if ([Math]::Abs($measuredWidth-$spec.TargetWidth) -gt 2) {
        throw "$Name screenshot measured width $measuredWidth after resize; expected $($spec.TargetWidth)."
    }
    $script:resizeEvidence.Add([ordered]@{
        app=$Name;residentCountBefore=$residentCount;identity=$identity.identity
        resize=[ordered]@{from=('{0}x{1}' -f $spec.Width,$spec.Height);to=('{0}x{1}' -f $spec.TargetWidth,$spec.TargetHeight);nativeMarker=if ($native) {$native.Value} else {'not-visible-in-production'};managedLayout=if ($Lane -eq 'focused') {'PASS'} else {'visible-screenshot-only'}}
        resizeCornerHitTest=if ($Lane -eq 'focused') {'PASS'} else {'not-visible-in-production'}
        taskbarFocusReturn='PASS'
        screenshot=$screenshot;screenshotSha256=(Get-FileHash -LiteralPath $screenshot -Algorithm SHA256).Hash
        measuredTitlebarWidth=$measuredWidth
    })
}

function Reject-NotesGeometry([int]$BootNumber) {
    $spec = Get-ResidentResizeSpec 'notes'
    $residentCount = Refresh-TaskManager $BootNumber 'five-residents-before-rejected-notes-geometry' 5
    $identity = Focus-ResidentApp 'notes' $BootNumber $spec.TargetWidth
    $currentWidth = $spec.TargetWidth
    $currentHeight = $spec.TargetHeight
    $startX = [Math]::Max(0,[int](($script:screenWidth-$spec.Width)/2)) + $currentWidth - 4
    $startY = [Math]::Max(0,[int](($script:screenHeight-$spec.Height)/2)) + $currentHeight - 4
    $hoverStart = (Read-Serial).Length
    Move-Pointer $startX $startY
    if ($Lane -eq 'focused') {
        $hoverPattern = '(?m)^\[C172-COMPOSITOR-TRACE\] action=resize-hover x=' + ('{0:X8}' -f $startX) + ' y=' + ('{0:X8}' -f $startY) + ' window=' + [regex]::Escape($identity.identity.window) + ' flags=[0-9A-Fa-f]{8}\r?$'
        [void](Wait-Pattern $hoverPattern $hoverStart 8 'Notes rejected-geometry resize-corner hit test')
    }
    $markerStart = (Read-Serial).Length
    Drag-Pointer $startX $startY ($startX-1) ($startY-1)
    $rejected = $null
    if ($Lane -eq 'focused') {
        $nativePattern = '(?m)^\[C175-NATIVE-GEOMETRY\] appId=com\.guidexos\.apps\.managed\.notes instance=' + [regex]::Escape($identity.identity.instance) + ' generation=' + [regex]::Escape($identity.identity.generation) + ' window=' + [regex]::Escape($identity.identity.window) + ' oldOuter=[0-9A-Fa-f]{8}x[0-9A-Fa-f]{8} newOuter=0000022Fx00000157 client=[0-9A-Fa-f]{8}x[0-9A-Fa-f]{8} result=REJECT\r?$'
        $rejected = Wait-Pattern $nativePattern $markerStart 15 'Notes rejected minimum geometry request'
        [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C180-MANAGED-GEOMETRY result=REJECT layout=unchanged\r?$' $markerStart 10 'Notes rejected geometry rollback')
    } else {
        Start-Sleep -Milliseconds 500
    }
    $screenshot = Capture 'notes-rejected-geometry-rollback' (Join-Path $laneRoot ('boot'+$BootNumber))
    $measuredWidth = Get-PpmTitlebarWidth $screenshot $spec.TitlebarRgb
    if ([Math]::Abs($measuredWidth-$currentWidth) -gt 2) { throw "Notes rejected geometry changed screenshot width to $measuredWidth; expected rollback to $currentWidth." }
    $script:resizeEvidence.Add([ordered]@{
        app='notes';identity=$identity.identity;residentCountBefore=$residentCount;requested='559x343';result=if ($Lane -eq 'focused') {'REJECT'} else {'screenshot-rollback-pass'}
        rollbackGeometry=('{0}x{1}' -f $currentWidth,$currentHeight);layout='unchanged'
        nativeMarker=if ($rejected) {$rejected.Value} else {'not-visible-in-production'};screenshot=$screenshot
        screenshotSha256=(Get-FileHash -LiteralPath $screenshot -Algorithm SHA256).Hash
        measuredTitlebarWidth=$measuredWidth
    })
}

function Exercise-BootThreeLifecycle([int]$BootNumber) {
    Launch-AllFive $BootNumber $serialOffset
    [void](Capture 'five-apps-resident' (Join-Path $laneRoot ('boot'+$BootNumber)))
    Resize-ResidentApp 'notes' $BootNumber

    # Interleave an edit/save with focus transitions and two close/reopen
    # cycles. Each close frees one resident slot; reopening must create a new
    # Notes generation and return the actual resident count to five.
    [void](Open-AppMenuItem 'notes' $BootNumber)
    $editStart = (Read-Serial).Length
    Click 380 338
    Send-Key 'z'
    Send-ControlChord 's'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=s command=Save .*result=PASS.*$' $editStart 20 'Boot 3 Notes edit/save')

    $originX = [Math]::Max(0,[int](($script:screenWidth-600)/2))
    $originY = [Math]::Max(0,[int](($script:screenHeight-360)/2))
    $currentWidth = 560
    for ($cycle=1; $cycle -le 2; $cycle++) {
        $before = Focus-ResidentApp 'notes' $BootNumber $currentWidth
        $closeStart = (Read-Serial).Length
        Click ($originX+$currentWidth-14) ($originY+12)
        $destroyPattern = '(?m)^\[C150-SURFACE\] action=destroy appId=com\.guidexos\.apps\.managed\.notes generation=' + [regex]::Escape($before.identity.generation) + ' window=' + [regex]::Escape($before.identity.window) + ' reason=close-or-replace\r?$'
        [void](Wait-Pattern $destroyPattern $closeStart 15 "Boot 3 Notes close cycle $cycle")
        $closedCount = Refresh-TaskManager $BootNumber ("boot3-notes-closed-"+$cycle) 4

        $launchStart = (Read-Serial).Length
        [void](Open-AppMenuItem 'notes' $BootNumber)
        $afterClose = Read-Serial
        $surfacePattern = '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=(?<generation>[0-9A-Fa-f]{8}) window=(?<window>[0-9A-Fa-f]{8}) result=PASS\r?$'
        $surfaces = [System.Text.RegularExpressions.Regex]::Matches($afterClose,$surfacePattern)
        if ($surfaces.Count -lt ($before.createCount+1)) { throw "Boot 3 Notes reopen cycle $cycle did not create a fresh surface." }
        $created = $surfaces[$surfaces.Count-1]
        $newGeneration = $created.Groups['generation'].Value.ToUpperInvariant()
        $newWindow = $created.Groups['window'].Value.ToUpperInvariant()
        if ($newGeneration -eq $before.identity.generation) {
            throw "Boot 3 Notes reopen cycle $cycle reused its closed generation."
        }
        $after = Focus-ResidentApp 'notes' $BootNumber 600
        if ($Lane -eq 'focused' -and $after.identity.instance -eq $before.identity.instance) {
            throw "Boot 3 Notes reopen cycle $cycle reused its closed native instance."
        }
        # Dispatch a real edit/save through the newly opened Notes generation
        # before accepting recovery. This exercises the slot owner after each
        # close, rather than relying on surface creation alone.
        $reopenedNotesStart = (Read-Serial).Length
        Click 380 338
        $reopenKey = if ($cycle -eq 1) {'y'} else {'x'}
        Send-Key $reopenKey
        Send-ControlChord 's'
        [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=s command=Save .*result=PASS.*$' $reopenedNotesStart 20 "Boot 3 Notes generation $cycle edit/save")
        $restoredCount = Refresh-TaskManager $BootNumber ("boot3-notes-restored-"+$cycle) 5
        $script:lifecycleEvidence.Add([ordered]@{
            app='notes';cycle=$cycle;closed=[ordered]@{instance=$before.identity.instance;generation=$before.identity.generation;window=$before.identity.window}
            fresh=[ordered]@{instance=$after.identity.instance;generation=$after.identity.generation;window=$after.identity.window}
            concurrentCountAfterClose=$closedCount;concurrentCountAfterReopen=$restoredCount
            generationChanged=($after.identity.generation -ne $before.identity.generation)
            nativeInstanceChanged=if ($Lane -eq 'focused') {$after.identity.instance -ne $before.identity.instance} else {'not-visible-in-production'}
        })
        $currentWidth = 600
    }

    # The resident Explorer remains active through the Notes lifecycle and
    # continues enforcing its managed root after Notes is recreated twice.
    [void](Open-AppMenuItem 'explorer' $BootNumber)
    $explorerStart = (Read-Serial).Length
    Click 306 569
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=1 path=/system/apps n=\d+ sel=none .*' $explorerStart 15 'Boot 3 Explorer managed-root boundary')
    [void](Refresh-TaskManager $BootNumber 'boot3-final-five-residents' 5)
    $bootText = Read-Serial
    $bootTail = if ($serialOffset -le $bootText.Length) { $bootText.Substring($serialOffset) } else { $bootText }
    if ($bootTail -match 'C150-DISPATCH rejected=stale-application result=FAIL|C150-LIFETIME-CLOSE result=REJECT stale-generation=true') {
        throw 'Boot 3 emitted a stale-generation dispatch or close rejection.'
    }
}

function Exercise-BootTwoActions([int]$BootNumber) {
    # Settings: toggle Input Enabled, apply from the Options menu, then reset
    # through the existing confirmation dialog.
    Launch-SettingsFromNotes $BootNumber
    [void](Focus-ResidentApp 'settings' $BootNumber)
    $settingsStart = (Read-Serial).Length
    $toggleStart = (Read-Serial).Length
    Click 532 317
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C144-GROUP input=(?:enabled own-member-state=preserved|disabled pointer-tab-popup=gated) result=PASS\r?$' $toggleStart 12 'Settings Input Enabled toggle')
    Start-Sleep -Milliseconds 250
    $settingsSpec = Get-ResidentResizeSpec 'settings'
    $settingsClientX = [Math]::Max(0,[int](($script:screenWidth-$settingsSpec.Width)/2)) + 24
    $settingsClientY = [Math]::Max(0,[int](($script:screenHeight-$settingsSpec.Height)/2)) + 24
    Click 836 285
    Click ($settingsClientX+440) ($settingsClientY+52)
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C144-APPLY menu=PASS dirty=cleared viewport=preserved result=PASS$' $settingsStart 20 'Settings Apply')
    Click 836 285
    Click ($settingsClientX+440) ($settingsClientY+72)
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS$' $settingsStart 15 'Settings Reset confirmation')
    Send-Key 'ret'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS$' $settingsStart 15 'Settings Reset')
    [void](Capture 'settings-apply-reset' (Join-Path $laneRoot 'boot2'))

    # Explorer enforces its /system/apps VFS root, then selects a row and
    # refreshes the listing without losing the selection.
    [void](Open-AppMenuItem 'explorer' $BootNumber)
    $explorerStart = (Read-Serial).Length
    Click 306 569
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=1 path=/system/apps n=\d+ sel=none .*' $explorerStart 15 'Explorer managed-root boundary')
    Click 420 400
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=1 path=/system/apps n=\d+ sel=.+ type=(Directory|File) .*' $explorerStart 15 'Explorer row selection')
    $refreshStart = (Read-Serial).Length
    Click 524 569
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=1 path=/system/apps n=\d+ sel=.+ type=File .*' $refreshStart 15 'Explorer refresh preserves selected file')
    [void](Capture 'explorer-root-selection-refresh' (Join-Path $laneRoot 'boot2'))

    # Notes: edit the resident document and save the updated document.
    [void](Open-AppMenuItem 'notes' $BootNumber)
    $notesStart = (Read-Serial).Length
    Click 380 338
    Send-Key 'x'
    Send-ControlChord 's'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT key=s command=Save .*result=PASS.*$' $notesStart 20 'Notes save shortcut')
    [void](Capture 'notes-edited-and-saved' (Join-Path $laneRoot 'boot2'))

    # Calculator: use the keyboard path to calculate 7 * 8 = 56.
    [void](Open-AppMenuItem 'calculator' $BootNumber)
    $calculatorStart = (Read-Serial).Length
    Send-Key '7'
    Send-ShiftKey '8'
    Send-Key '8'
    Send-Key 'ret'
    [void](Wait-Pattern '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=56 phase=3 commands=4 result=PASS\r?$' $calculatorStart 15 'Calculator 7*8=56')
    [void](Capture 'calculator-7x8-56' (Join-Path $laneRoot 'boot2'))

    # Task Manager refreshes after the interaction and confirms five rows.
    [void](Refresh-TaskManager 2)

    # Keep all five surfaces resident while exercising each application's
    # existing bottom-right resize path. Refreshing Task Manager immediately
    # before every resize records the actual simultaneous resident count.
    foreach ($name in @('settings','explorer','notes','calculator','taskmanager')) {
        Resize-ResidentApp $name $BootNumber
    }
    Reject-NotesGeometry $BootNumber
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
    $bootText = $text.Substring($SerialOffset)
    $surfacePattern = '(?m)^\[C150-SURFACE\] action=(create|destroy) appId=([^ ]+) generation=([0-9A-Fa-f]{8}) window=([0-9A-Fa-f]{8})(?: result=PASS| reason=close-or-replace)\r?$'
    $surfaces = @([System.Text.RegularExpressions.Regex]::Matches($bootText,$surfacePattern) | ForEach-Object {
        [pscustomobject]@{Action=$_.Groups[1].Value;AppId=$_.Groups[2].Value;Generation=$_.Groups[3].Value;Window=$_.Groups[4].Value;Marker=$_.Value}
    })
    $focusPattern = '(?m)^\[C162-MANAGED-FOCUS\] appId=([^ ]+) source=3 instance=([0-9A-Fa-f]{16}) selector=([0-9A-Fa-f]{8}) existing=true result=PASS\r?$'
    $focuses = @([System.Text.RegularExpressions.Regex]::Matches($bootText,$focusPattern) | ForEach-Object {
        [pscustomobject]@{AppId=$_.Groups[1].Value;Instance=$_.Groups[2].Value;Selector=$_.Groups[3].Value;Marker=$_.Value}
    })
    [ordered]@{Surfaces=$surfaces;ExistingInstanceFocuses=$focuses} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootPath 'managed-identities.json') -Encoding utf8
    @($script:resizeEvidence) | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $bootPath 'resize-evidence.json') -Encoding utf8
    @($script:lifecycleEvidence) | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $bootPath 'lifecycle-evidence.json') -Encoding utf8
    $script:pointerCalibration | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $bootPath 'pointer-calibration.json') -Encoding utf8
}

$script:qacProcess = $null
$script:qmpPort = 0
$script:cursor = $null
$script:pointerCalibration = $null
$script:screenWidth = 0
$script:screenHeight = 0
$script:startMenuAllPrograms = $false
$script:notesMenuCapture = 0
$script:focusCaptureSequence = 0
$script:resizeEvidence = [System.Collections.Generic.List[object]]::new()
$script:lifecycleEvidence = [System.Collections.Generic.List[object]]::new()
$stdoutTask = $null
$stderrTask = $null
$qemuArgs = @(
    '-accel','tcg','-machine','pc','-smp','1',
    '-drive',('if=pflash,format=raw,readonly=on,file='+$ovmf),
    '-drive',('file=fat:rw:'+ $esp + ',format=raw,if=ide,index=0'),
    '-m','1024M','-vga','std','-display','none',
    '-serial',('file:'+ $serial),
    '-qmp',('tcp:127.0.0.1:{0},server,nowait' -f 0),
    # Keep QEMU available after guest shutdown for QMP cleanup. Omitting
    # -no-reboot lets QMP system_reset perform a guest reset instead of
    # turning it into the SHUTDOWN/STOP state recorded during C181 attempt 13.
    '-boot','order=c','-no-shutdown','-rtc','base=utc,clock=host')
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
    $bootNumbers = if ($SingleBootNumber -ne 0) { @($SingleBootNumber) } else { @(1,2,3) }
    foreach ($boot in $bootNumbers) {
        $script:resizeEvidence = [System.Collections.Generic.List[object]]::new()
        $script:lifecycleEvidence = [System.Collections.Generic.List[object]]::new()
        $bootPath = Join-Path $laneRoot ('boot' + $boot)
        New-Item -ItemType Directory -Force -Path $bootPath | Out-Null
        if ($boot -gt 1 -and $SingleBootNumber -eq 0) {
            $beforeReset = Read-Serial
            $serialOffset = $beforeReset.Length
            if (Test-Path -LiteralPath $qmpLog) { $qmpOffset = ([System.IO.File]::ReadAllText($qmpLog)).Length }
            $resetResponse = Invoke-Qmp 'system_reset'
            $resetQmp = [System.IO.File]::ReadAllText($qmpLog).Substring($qmpOffset)
            if ($resetQmp -notmatch '(?m)^reset-event-observation=RESET_SEEN\r?$') {
                throw "QMP did not emit a RESET event for focused guest boot $boot."
            }
            $resetRequestStatus = Invoke-Qmp 'query-status'
            Set-Content -LiteralPath (Join-Path (Join-Path $laneRoot ('boot' + $boot)) 'reset-request-qmp-status.json') -Value $resetRequestStatus -Encoding utf8
            if ($resetRequestStatus -notmatch '"status"\s*:\s*"running"') {
                throw "QEMU was not running after system_reset for guest boot ${boot}: $resetRequestStatus"
            }
            [ordered]@{
                systemResetResponse=$resetResponse
                resetEventObserved=$true
                queryStatus=$resetRequestStatus
                serialOffsetBeforeReset=$serialOffset
            } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path (Join-Path $laneRoot ('boot' + $boot)) 'reset-evidence.json') -Encoding utf8
            $script:cursor = $null
        }
        Wait-ReadyCount $boot 180 $serialOffset
        Calibrate-Pointer $boot
        $text = Read-Serial
        if ($boot -eq 1 -or $SingleBootNumber -ne 0) {
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
            $notesBefore = Focus-ResidentApp 'notes' $boot
            $closeStart = (Read-Serial).Length
            Click 926 232
            Start-Sleep -Milliseconds 700
            [void](Capture 'after-notes-close' $bootPath)
            $afterClose = Read-Serial
            $destroyPattern = '(?m)^\[C150-SURFACE\] action=destroy appId=com\.guidexos\.apps\.managed\.notes generation=' + [regex]::Escape($notesBefore.identity.generation) + ' window=' + [regex]::Escape($notesBefore.identity.window) + ' reason=close-or-replace\r?$'
            [void](Wait-Pattern $destroyPattern $closeStart 15 'Boot 1 Notes surface close')
            $closedCount = Refresh-TaskManager $boot 'boot1-notes-closed' 4
            $normalBefore = Count-Matches $afterClose '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes .*kind=normal$'
            [void](Open-AppMenuItem 'notes' $boot)
            $deadline = (Get-Date).AddSeconds(15)
            do {
                $afterReopen = Read-Serial
                $normalAfter = Count-Matches $afterReopen '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes .*kind=normal$'
                if ($normalAfter -gt $normalBefore) { break }
                Start-Sleep -Milliseconds 100
            } while ((Get-Date) -lt $deadline)
            if ($normalAfter -le $normalBefore) { throw 'Notes did not relaunch after the close/reopen lifecycle check.' }
            $notesAfter = Focus-ResidentApp 'notes' $boot
            if ($notesAfter.identity.generation -eq $notesBefore.identity.generation) { throw 'Boot 1 Notes reopen reused the closed surface generation.' }
            $restoredCount = Refresh-TaskManager $boot 'boot1-notes-restored' 5
            $script:lifecycleEvidence.Add([ordered]@{
                app='notes';cycle=1;closed=[ordered]@{instance=$notesBefore.identity.instance;generation=$notesBefore.identity.generation;window=$notesBefore.identity.window}
                fresh=[ordered]@{instance=$notesAfter.identity.instance;generation=$notesAfter.identity.generation;window=$notesAfter.identity.window}
                concurrentCountAfterClose=$closedCount;concurrentCountAfterReopen=$restoredCount
                generationChanged=($notesAfter.identity.generation -ne $notesBefore.identity.generation)
            })
        } elseif ($boot -eq 2) {
            Launch-AllFive $boot $serialOffset
            [void](Capture 'five-apps-resident' $bootPath)
            Exercise-BootTwoActions $boot
        } else {
            Exercise-BootThreeLifecycle $boot
            [void](Capture 'stress-recovery' $bootPath)
        }
        $current = Read-Serial
        $minimumNotesLaunches = if ($SingleBootNumber -ne 0) { 1 } else { $boot }
        if ((Count-Matches $current '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes .*kind=normal$') -lt $minimumNotesLaunches) {
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
        if ($boot -gt 1 -and $SingleBootNumber -eq 0) {
            $bootPath = Join-Path $laneRoot ('boot' + $boot)
            try {
                $resetStatus = Invoke-Qmp 'query-status'
                Set-Content -LiteralPath (Join-Path $bootPath 'reset-timeout-qmp-status.json') -Value $resetStatus -Encoding utf8
            } catch { }
        }
        if ($boot -ge 1 -and $boot -le 3) {
            $bootPath = Join-Path $laneRoot ('boot' + $boot)
            try { [void](Capture ("failure-boot" + $boot) $bootPath) } catch { }
            try { Save-BootEvidence $boot $serialOffset $qmpOffset } catch { }
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
$qacStderr = if (Test-Path -LiteralPath $qacStderrPath) { [System.IO.File]::ReadAllText($qacStderrPath) } else { '' }
$reservationMatch = [System.Text.RegularExpressions.Regex]::Match($qacStderr,'QAC reservation requested: id=([0-9a-f]+)')
$releasedMatch = [System.Text.RegularExpressions.Regex]::Match($qacStderr,'QAC reservation released: id=([0-9a-f]+)')
$qemuExitMatch = [System.Text.RegularExpressions.Regex]::Match($qacStderr,'QAC QEMU exited: reservation=([0-9a-f]+) pid=(\d+) exitCode=(\d+)')
$qemuPidMatch = [System.Text.RegularExpressions.Regex]::Match($qacStderr,'QAC QEMU launched: reservation=[0-9a-f]+ pid=(\d+)')
$qacReleaseVerified = $reservationMatch.Success -and
    $releasedMatch.Success -and
    $qemuExitMatch.Success -and
    $reservationMatch.Groups[1].Value -eq $releasedMatch.Groups[1].Value -and
    $reservationMatch.Groups[1].Value -eq $qemuExitMatch.Groups[1].Value -and
    [int]$qemuExitMatch.Groups[3].Value -eq $script:qacProcess.ExitCode -and
    $listAfter -notmatch [System.Text.RegularExpressions.Regex]::Escape($reservationMatch.Groups[1].Value)
if ($script:qacProcess.ExitCode -ne 0) { throw "QAC run returned QEMU exit code $($script:qacProcess.ExitCode)." }
if ($statusAfter -notmatch 'QEMU processes:\s+\d+ \(0 foreign\)') {
    throw 'QAC post-run census found a foreign QEMU process.'
}
if (-not $qacReleaseVerified) {
    throw 'QAC did not confirm the run reservation and child QEMU release.'
}

$fullSerial = Read-Serial
Set-Content -LiteralPath (Join-Path $laneRoot 'serial-all-boots.log') -Value $fullSerial -Encoding utf8
$fullQmp = if (Test-Path -LiteralPath $qmpLog) { [System.IO.File]::ReadAllText($qmpLog) } else { '' }
$bootedMediaHashes = Get-MediaHashes $esp
foreach ($relativePath in $candidateMediaHashes.Keys) {
    if ($candidateMediaHashes[$relativePath] -ne $bootedMediaHashes[$relativePath]) {
        throw "The isolated ESP changed $relativePath during the QAC run."
    }
}
$manifest = [ordered]@{
    lane=$Lane
    runLabel=$RunLabel
    evidenceRoot=$root
    sourceMediaPath=$baseEsp
    proofKernelPath=$proofKernelPath
    kernelBuildClass=if ($Lane -eq 'focused') {'proof-controlled'} else {'proof-disabled-production'}
    proofKernelMarkers=$presentProofMarkers
    boots=$bootNumbers.Count
    scenarioBootNumber=if ($SingleBootNumber -ne 0) {$SingleBootNumber} else {$null}
    qacMode='acceptance'
    qacProject='guideXOSServer'
    qacCli=$qacCli
    qacRunArguments=$argsEvidence
    qacReservationId=if ($reservationMatch.Success) {$reservationMatch.Groups[1].Value} else {$null}
    qemuPid=if ($qemuPidMatch.Success) {[int]$qemuPidMatch.Groups[1].Value} else {$null}
    qacExitCode=$script:qacProcess.ExitCode
    qacReleaseVerified=$qacReleaseVerified
    bootReadyMarkers=(Count-Matches $fullSerial '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$')
    normalAppLaunches=(Count-Matches $fullSerial '(?m)^\[C150-APP-LAUNCH\] id=[^ ]+ generation=([0-9A-Fa-f]+) selector=([0-9A-Fa-f]+) kind=normal\r?$')
    qmpResetEvents=(Count-Matches $fullQmp '(?m)^qmp-event=.*"event"\s*:\s*"RESET"')
    qmpResetEventsMissing=(Count-Matches $fullQmp '(?m)^reset-event-observation=RESET_NOT_OBSERVED\r?$')
    candidateMediaHashes=$candidateMediaHashes
    isolatedBootedMediaHashes=$bootedMediaHashes
    qemuArguments=$qemuArgs
    serialLog=(Join-Path $laneRoot 'serial-all-boots.log')
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $laneRoot 'sequence-manifest.json') -Encoding utf8
Write-Output ("QAC_RELEASE=VERIFIED LANE="+$Lane+" BOOTS="+$manifest.bootReadyMarkers+" NORMAL_LAUNCHES="+$manifest.normalAppLaunches)
