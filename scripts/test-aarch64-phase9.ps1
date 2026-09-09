[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = '',
    [int]$Boots = 3,
    [int]$StartingMonitorPort = 4910,
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase9' }
else { $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory) }
$powershell = (Get-Command powershell.exe -ErrorAction Stop).Source

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $FilePath" }
}

function Wait-LogMatch {
    param([string]$Path, [string]$Pattern, [datetime]$Deadline)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (Test-Path -LiteralPath $Path) {
            $text = Get-Content -Raw -LiteralPath $Path
            if ($text -match $Pattern) { return $true }
            if ($text -match 'AARCH64_PHASE9_ERROR') { return $false }
        }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Receive-Qmp {
    param([Net.Sockets.NetworkStream]$Stream, [byte[]]$Buffer)
    $text = ''
    Start-Sleep -Milliseconds 100
    while ($Stream.DataAvailable) {
        $count = $Stream.Read($Buffer, 0, $Buffer.Length)
        if ($count -gt 0) { $text += [Text.Encoding]::UTF8.GetString($Buffer, 0, $count) }
    }
    return $text
}

function Send-Qmp {
    param([Net.Sockets.NetworkStream]$Stream, [byte[]]$Buffer, [string]$Json)
    $bytes = [Text.Encoding]::UTF8.GetBytes($Json + [char]10)
    $Stream.Write($bytes, 0, $bytes.Length)
    $response = Receive-Qmp $Stream $Buffer
    if ($response -match '"error"') { throw "QMP rejected event: $response" }
}

function Open-Qmp {
    param([int]$Port, [datetime]$Deadline)
    while ([DateTime]::UtcNow -lt $Deadline) {
        try {
            $client = [Net.Sockets.TcpClient]::new()
            $client.Connect('127.0.0.1', $Port)
            $stream = $client.GetStream()
            $stream.ReadTimeout = 3000
            $buffer = New-Object byte[] 8192
            $null = Receive-Qmp $stream $buffer
            return [pscustomobject]@{ Client = $client; Stream = $stream; Buffer = $buffer }
        } catch {
            if ($client) { $client.Dispose() }
            Start-Sleep -Milliseconds 250
        }
    }
    throw "QMP port $Port did not become available"
}

function Send-Click {
    param($Qmp, [int]$X, [int]$Y)
    $json = '{"execute":"input-send-event","arguments":{"events":[' +
        '{"type":"abs","data":{"axis":"x","value":' + $X + '}},' +
        '{"type":"abs","data":{"axis":"y","value":' + $Y + '}},' +
        '{"type":"btn","data":{"button":"left","down":true}},' +
        '{"type":"btn","data":{"button":"left","down":false}}]}}'
    Send-Qmp $Qmp.Stream $Qmp.Buffer $json
}

function Send-Key {
    param($Qmp, [string]$Key)
    $down = '{"execute":"input-send-event","arguments":{"events":[' +
        '{"type":"key","data":{"down":true,"key":{"type":"qcode","data":"' + $Key + '"}}}]}}'
    $up = '{"execute":"input-send-event","arguments":{"events":[' +
        '{"type":"key","data":{"down":false,"key":{"type":"qcode","data":"' + $Key + '"}}}]}}'
    Send-Qmp $Qmp.Stream $Qmp.Buffer $down
    Start-Sleep -Milliseconds 60
    Send-Qmp $Qmp.Stream $Qmp.Buffer $up
}

if ($Boots -lt 1) { throw 'Boots must be positive' }
$buildScript = Join-Path $PSScriptRoot 'build-aarch64-phase9.ps1'
Write-Host '[phase9] building artifacts and host controls...' -ForegroundColor Yellow
Invoke-Checked $powershell @('-NoProfile','-ExecutionPolicy','Bypass','-File',$buildScript,'-LlvmRoot',$LlvmRoot,'-OutputDirectory',$OutputDirectory)

$runScript = Join-Path $PSScriptRoot 'run-aarch64-phase9.ps1'
for ($boot = 1; $boot -le $Boots; ++$boot) {
    $port = $StartingMonitorPort + $boot - 1
    $logPath = Join-Path $OutputDirectory ('qemu-aarch64-phase9-run{0}.log' -f $boot)
    if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force }
    $arguments = @(
        '-NoProfile','-ExecutionPolicy','Bypass','-File',$runScript,
        '-ArtifactDirectory',$OutputDirectory,'-MonitorPort',$port,
        '-DisplayBackend','gtk,gl=off','-TimeoutSeconds',$TimeoutSeconds,
        '-LogPath',$logPath)
    Write-Host "[phase9] fresh QEMU boot $boot/$Boots (QMP $port)..." -ForegroundColor Yellow
    $runner = Start-Process -FilePath $powershell -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $ready = Wait-LogMatch $logPath '\[phase9-app-a\] multi-window: PASS' $deadline
    if (!$ready) {
        if (!$runner.HasExited) { $runner.Kill() }
        throw "Boot $boot did not reach the App A QMP gate"
    }
    if (!(Wait-LogMatch $logPath 'application wait/wake: PASS' $deadline)) {
        if (!$runner.HasExited) { $runner.Kill() }
        throw "Boot $boot did not reach the concurrent App B wait/wake gate"
    }

    $qmp = Open-Qmp $port $deadline
    try {
        Send-Qmp $qmp.Stream $qmp.Buffer '{"execute":"qmp_capabilities"}'

        # App A main textbox, then its button; App B is lower on the desktop.
        Send-Click $qmp 7100 10600
        foreach ($key in @('a','p','p','a')) { Send-Key $qmp $key }
        if (!(Wait-LogMatch $logPath 'focus/input isolation: PASS value=appa' $deadline)) {
            throw "Boot $boot did not deliver App A textbox value"
        }
        Send-Click $qmp 9200 13500
        if (!(Wait-LogMatch $logPath 'phase9-app-a\] button: PASS count=1' $deadline)) {
            throw "Boot $boot did not deliver App A button click"
        }

        # Exercise App B while both runtimes are concurrently live.  Its
        # window is below App A, so this also verifies independent hit testing
        # before App A's durability relaunches begin.
        Start-Sleep -Milliseconds 300
        Send-Click $qmp 8500 24500
        foreach ($key in @('a','p','p','b')) { Send-Key $qmp $key }
        if (!(Wait-LogMatch $logPath 'focus/input isolation: PASS value=appb' $deadline)) {
            throw "Boot $boot did not deliver App B textbox value"
        }
        Start-Sleep -Milliseconds 1000
        Send-Click $qmp 10500 27500
        if (!(Wait-LogMatch $logPath 'cross-app control routing: PASS' $deadline)) {
            throw "Boot $boot did not deliver App B button click"
        }
        # Focus the secondary window, close it, then close App A's main window.
        Send-Click $qmp 18800 7900
        Start-Sleep -Milliseconds 300
        Send-Click $qmp 28500 8000
        if (!(Wait-LogMatch $logPath 'secondary window close: PASS' $deadline)) {
            throw "Boot $boot did not close App A's secondary window"
        }
        Start-Sleep -Milliseconds 1000
        Send-Click $qmp 15560 6000
    } finally {
        $qmp.Client.Dispose()
    }

    # Let App A finish its first teardown before injecting App B input. This
    # keeps two independent lifecycle transitions from sharing one QMP burst.
    if (!(Wait-LogMatch $logPath 'cross-app cleanup isolation: PASS' $deadline)) {
        throw "Boot $boot did not complete App A teardown"
    }
    while ([DateTime]::UtcNow -lt $deadline -and !$runner.HasExited) {
        if (Test-Path -LiteralPath $logPath) {
            $text = Get-Content -Raw -LiteralPath $logPath
            if ($text -match 'AARCH64_PHASE9_PASS|AARCH64_PHASE9_ERROR') { break }
        }
        Start-Sleep -Milliseconds 500
    }
    if (!$runner.HasExited) { $runner.Kill() }
    $runner.WaitForExit()
    $output = if (Test-Path -LiteralPath $logPath) { Get-Content -Raw -LiteralPath $logPath } else { '' }
    if ($output -notmatch 'AARCH64_PHASE9_PASS') {
        $tail = (($output -split "`r?`n") | Select-Object -Last 25) -join "`n"
        throw "Boot $boot failed Phase 9 acceptance:`n$tail"
    }
    if ($output -notmatch 'runtime lifecycle durability: PASS launches=51' -or
        $output -notmatch 'scheduler/VFS integration: PASS' -or
        $output -notmatch 'phase9-app-b\] close requested') {
        throw "Boot $boot lacked durability or scheduler/VFS proof"
    }
    Write-Host "[phase9] boot $boot PASS" -ForegroundColor Green
}

Write-Host "AARCH64_PHASE9_QMP_HARNESS_PASS boots=$Boots" -ForegroundColor Green
