[CmdletBinding()]
param(
    [ValidateRange(20, 300)]
    [int]$TimeoutSeconds = 120,
    [ValidateRange(5, 60)]
    [int]$LaneAcquireTimeoutSeconds = 20,
    [string]$ArtifactRoot = '',
    [string]$CandidateEspPath = 'E:\bkup\artifacts\SN10R2_20261020\qemu\20261020T042331758\esp',
    [string]$CandidateVerificationPath = 'E:\bkup\artifacts\SN10R2_20261020\staged-candidate-verification.json',
    [switch]$VerifyCandidateOnly,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$QacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$Kernel = Join-Path $Root 'kernel\build\amd64\bin\kernel.elf'
$App = Join-Path $Root 'Apps\StellarNavigator\bin\amd64\stellarnavigator.elf'
$AppManifest = Join-Path $Root 'Apps\StellarNavigator\app.json'
$Dataset = Join-Path $Root 'Apps\StellarNavigator\Astronomy.Ephemeris\Data\ephemeris_de440s_chebyshev.sn4bin'
$Ovmf = Join-Path $Root 'OVMF.fd'
$Bootloader = Join-Path $Root 'out\mc7-proof\esp\EFI\BOOT\BOOTX64.EFI'

$Expected = @{
    Kernel = '3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a'
    NativeElf = '6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb'
    Dataset = 'd52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a'
    Manifest = '08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c'
    Bootloader = '07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783'
    Ovmf = '2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792'
}

$TypeDefinition = @'
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

public sealed class SN10R3SerialCapture : IDisposable
{
    private readonly TcpListener _listener;
    private readonly string _logPath;
    private readonly object _gate = new object();
    private readonly StringBuilder _text = new StringBuilder();
    private readonly ManualResetEventSlim _changed = new ManualResetEventSlim(false);
    private readonly CancellationTokenSource _stop = new CancellationTokenSource();
    private Task _worker;
    private TcpClient _client;
    private Exception _error;
    private bool _connected;
    private bool _completed;
    private long _bytes;

    public int Port { get; private set; }
    public string Host { get { return "127.0.0.1"; } }
    public bool Connected { get { lock (_gate) return _connected; } }
    public bool Completed { get { lock (_gate) return _completed; } }
    public long BytesReceived { get { lock (_gate) return _bytes; } }
    public string ErrorMessage { get { lock (_gate) return _error == null ? null : _error.ToString(); } }

    public SN10R3SerialCapture(string logPath)
    {
        _logPath = Path.GetFullPath(logPath);
        Directory.CreateDirectory(Path.GetDirectoryName(_logPath));
        File.WriteAllBytes(_logPath, Array.Empty<byte>());
        _listener = new TcpListener(IPAddress.Loopback, 0);
        _listener.Server.ExclusiveAddressUse = true;
        _listener.Start(1);
        Port = ((IPEndPoint)_listener.LocalEndpoint).Port;
        _worker = Task.Run((Func<Task>)PumpAsync);
    }

    private async Task PumpAsync()
    {
        try
        {
            _client = await _listener.AcceptTcpClientAsync(_stop.Token).ConfigureAwait(false);
            _client.NoDelay = true;
            lock (_gate) _connected = true;
            _changed.Set();
            using (var output = new FileStream(_logPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite, 8192, true))
            using (var stream = _client.GetStream())
            {
                var buffer = new byte[8192];
                while (!_stop.IsCancellationRequested)
                {
                    int count = await stream.ReadAsync(buffer.AsMemory(0, buffer.Length), _stop.Token).ConfigureAwait(false);
                    if (count == 0) break;
                    await output.WriteAsync(buffer.AsMemory(0, count), _stop.Token).ConfigureAwait(false);
                    await output.FlushAsync(_stop.Token).ConfigureAwait(false);
                    string chunk = Encoding.ASCII.GetString(buffer, 0, count);
                    lock (_gate)
                    {
                        _text.Append(chunk);
                        _bytes += count;
                    }
                    _changed.Set();
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (ObjectDisposedException) when (_stop.IsCancellationRequested) { }
        catch (SocketException) when (_stop.IsCancellationRequested) { }
        catch (Exception ex)
        {
            lock (_gate) _error = ex;
            _changed.Set();
        }
        finally
        {
            try { _client?.Dispose(); } catch { }
            lock (_gate) _completed = true;
            _changed.Set();
        }
    }

    public string Snapshot()
    {
        lock (_gate) return _text.ToString();
    }

    public bool WaitForChange(int milliseconds)
    {
        bool signaled = _changed.Wait(milliseconds);
        if (signaled) _changed.Reset();
        return signaled;
    }

    public bool WaitForCompletion(int milliseconds)
    {
        return _worker.Wait(milliseconds);
    }

    public void Stop()
    {
        _stop.Cancel();
        try { _listener.Stop(); } catch { }
        try { _client?.Close(); } catch { }
    }

    public void Dispose()
    {
        Stop();
        try { _worker.Wait(3000); } catch { }
        _changed.Dispose();
        _stop.Dispose();
    }
}

public sealed class SN10R3ManagedProcess : IDisposable
{
    private readonly object _gate = new object();
    private readonly StringBuilder _stdout = new StringBuilder();
    private readonly StringBuilder _stderr = new StringBuilder();
    private readonly ConcurrentQueue<string> _stderrLines = new ConcurrentQueue<string>();
    public Process Process { get; private set; }

    public SN10R3ManagedProcess(string fileName, string workingDirectory, string[] arguments,
                                string stdoutPath, string stderrPath)
    {
        var info = new ProcessStartInfo
        {
            FileName = fileName,
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (string argument in arguments) info.ArgumentList.Add(argument);
        Process = new Process { StartInfo = info, EnableRaisingEvents = true };
        Process.OutputDataReceived += (sender, eventArgs) =>
        {
            if (eventArgs.Data == null) return;
            lock (_gate) _stdout.AppendLine(eventArgs.Data);
            File.AppendAllText(stdoutPath, eventArgs.Data + Environment.NewLine, Encoding.UTF8);
        };
        Process.ErrorDataReceived += (sender, eventArgs) =>
        {
            if (eventArgs.Data == null) return;
            lock (_gate) _stderr.AppendLine(eventArgs.Data);
            _stderrLines.Enqueue(eventArgs.Data);
            File.AppendAllText(stderrPath, eventArgs.Data + Environment.NewLine, Encoding.UTF8);
        };
        File.WriteAllText(stdoutPath, string.Empty, Encoding.UTF8);
        File.WriteAllText(stderrPath, string.Empty, Encoding.UTF8);
        if (!Process.Start()) throw new InvalidOperationException("Could not start managed child process.");
        Process.BeginOutputReadLine();
        Process.BeginErrorReadLine();
    }

    public string[] DrainStderrLines()
    {
        var lines = new List<string>();
        string line;
        while (_stderrLines.TryDequeue(out line)) lines.Add(line);
        return lines.ToArray();
    }

    public string StdoutText { get { lock (_gate) return _stdout.ToString(); } }
    public string StderrText { get { lock (_gate) return _stderr.ToString(); } }
    public bool WaitForExit(int milliseconds) { return Process.WaitForExit(milliseconds); }
    public bool HasExited { get { return Process.HasExited; } }
    public int ExitCode { get { return Process.ExitCode; } }
    public void Dispose() { Process?.Dispose(); }
}
'@

if (-not ('SN10R3SerialCapture' -as [type])) {
    Add-Type -TypeDefinition $TypeDefinition -Language CSharp
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-QemuOptionValue([string]$Value) {
    # QEMU keyval option values escape a filename comma by doubling it.
    # ProcessStartInfo.ArgumentList handles Windows argument quoting separately.
    $Value.Replace(',', ',,')
}

function New-QemuArguments([string]$EspPath, [int]$SerialPort, [int]$MonitorPort, [string]$FirmwarePath = $Ovmf) {
    $firmware = ConvertTo-QemuOptionValue ([IO.Path]::GetFullPath($FirmwarePath))
    $esp = ConvertTo-QemuOptionValue ([IO.Path]::GetFullPath($EspPath))
    @(
        '-machine', 'pc,usb=off',
        '-drive', "if=pflash,format=raw,readonly=on,file=$firmware",
        '-drive', "file=fat:$esp,format=raw,if=ide,index=0,readonly=on",
        '-m', '1024M',
        '-vga', 'std',
        '-display', 'none',
        '-chardev', "socket,id=sn10serial,host=127.0.0.1,port=$SerialPort,server=off,ipv4=on,reconnect-ms=0",
        '-serial', 'chardev:sn10serial',
        '-monitor', "tcp:127.0.0.1:$MonitorPort,server,nowait",
        '-D', (Join-Path $script:RunRoot 'qemu.log'),
        '-rtc', 'base=utc,clock=host',
        '-no-reboot',
        '-no-shutdown'
    )
}

function New-RunDirectory {
    if (-not $ArtifactRoot) {
        $ArtifactRoot = Join-Path $env:USERPROFILE ('.codex\artifacts\SN10R3_' + (Get-Date -Format 'yyyyMMdd'))
    }
    $full = [IO.Path]::GetFullPath($ArtifactRoot)
    $id = Get-Date -Format 'yyyyMMddTHHmmssfff'
    $path = Join-Path $full (Join-Path 'qemu' $id)
    if (Test-Path -LiteralPath $path) { $path = Join-Path $full (Join-Path 'qemu' ($id + '_' + [guid]::NewGuid().ToString('N').Substring(0, 8))) }
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    $script:RunRoot = $path
}

function Test-StructuredArgumentPassing {
    $powershell = (Get-Command pwsh.exe -ErrorAction Stop).Source
    $scriptPath = Join-Path $script:RunRoot 'argv-echo.ps1'
    $scriptLines = @(
        '$ErrorActionPreference = ''Stop''',
        '$args | ConvertTo-Json -Compress | Set-Content -LiteralPath $env:SN10R3_ARGV_PATH -Encoding utf8'
    )
    Set-Content -LiteralPath $scriptPath -Value $scriptLines -Encoding utf8
    $expected = @(
        '-drive', "if=pflash,format=raw,readonly=on,file=C:\SN10 R3 & (quoted)'s path\firmware,, rev 2.fd",
        '-drive', "file=fat:C:\SN10 R3 & (quoted)'s path\candidate,, rev 2,format=raw,if=ide,index=0,readonly=on",
        'single quoted path with spaces.txt',
        "ampersand & parentheses (kept literally) and an apostrophe '"
    )
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $powershell
    $info.WorkingDirectory = $script:RunRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.ArgumentList.Add('-NoProfile')
    $info.ArgumentList.Add('-File')
    $info.ArgumentList.Add($scriptPath)
    foreach ($arg in $expected) { $info.ArgumentList.Add($arg) }
    $dumpPath = Join-Path $script:RunRoot 'argv-roundtrip.json'
    $info.Environment['SN10R3_ARGV_PATH'] = $dumpPath
    $process = [Diagnostics.Process]::Start($info)
    if (-not $process.WaitForExit(10000)) { $process.Kill(); throw 'Structured argument round-trip timed out.' }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $actual = if (Test-Path -LiteralPath $dumpPath) { @(Get-Content -LiteralPath $dumpPath -Raw | ConvertFrom-Json) } else { @() }
    if ($process.ExitCode -ne 0 -or $actual.Count -ne $expected.Count) { throw "Structured argument round-trip failed: exit=$($process.ExitCode) stdout=$stdout stderr=$stderr" }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        if ([string]$actual[$i] -cne $expected[$i]) { throw "Structured argument boundary mismatch at index $($i): expected=[$($expected[$i])] actual=[$($actual[$i])]" }
    }
    [pscustomobject]@{ passed = $true; argumentCount = $actual.Count; exactArgumentBoundaries = $true; shellQuotesAdded = $false; qemuSpecialCharacters = @('spaces', 'comma doubled for QEMU keyval', 'ampersand', 'parentheses', 'literal apostrophe') }
}

function Wait-SerialPattern {
    param(
        [Parameter(Mandatory = $true)]$Capture,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][int]$Seconds,
        [string]$Description = $Pattern
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        $text = $Capture.Snapshot()
        if ($text -match $Pattern) { return $text }
        if ($Capture.ErrorMessage) { throw "SERIAL_CAPTURE_FAILURE: $($Capture.ErrorMessage)" }
        if ($Capture.Completed) { break }
        if ($script:Owned -and $script:Owned.Process -and $script:Owned.Process.HasExited) { break }
        $left = [Math]::Max(1, [int][Math]::Ceiling(($Seconds - $timer.Elapsed.TotalSeconds) * 1000))
        [void]$Capture.WaitForChange([Math]::Min(250, $left))
    }
    if (-not $Capture.Connected) { throw "SERIAL_CAPTURE_FAILURE: serial endpoint received no QEMU client before '$Description'." }
    if ($Capture.BytesReceived -eq 0) { throw "NO_GUEST_SERIAL_OUTPUT: QEMU connected but sent no serial bytes before '$Description'." }
    throw "GUEST_PROGRESS_INCOMPLETE: bounded wait for '$Description' expired; serial bytes were captured."
}

function Get-LoopbackPort {
    $probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $probe.Start()
    try { return ([Net.IPEndPoint]$probe.LocalEndpoint).Port }
    finally { $probe.Stop() }
}

function Start-TestProducer {
    param([int]$Port, [ValidateSet('Markers', 'Partial', 'Silent')][string]$Scenario, [int]$ExitCode = 23)
    $producerPath = Join-Path $script:RunRoot ("producer-$Scenario.ps1")
    $producer = @'
param([int]$Port, [string]$Scenario, [int]$ExitCode)
$client = [Net.Sockets.TcpClient]::new()
try {
    $client.Connect([Net.IPAddress]::Loopback, $Port)
    $stream = $client.GetStream()
    $lines = switch ($Scenario) {
        'Markers' { @('[TEST] boot-ready', '[TEST] app-visible', '[TEST] ephemeris-evaluated', 'partial-after-nonzero-exit') }
        'Partial' { @('[TEST] partial-progress') }
        'Silent' { @() }
    }
    foreach ($line in $lines) {
        $bytes = [Text.Encoding]::ASCII.GetBytes($line + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        Start-Sleep -Milliseconds 100
    }
    $stream.Dispose()
} finally { $client.Dispose() }
exit $ExitCode
'@
    Set-Content -LiteralPath $producerPath -Value $producer -Encoding utf8
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = (Get-Command pwsh.exe -ErrorAction Stop).Source
    $info.WorkingDirectory = $script:RunRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile', '-File', $producerPath, [string]$Port, $Scenario, [string]$ExitCode)) { $info.ArgumentList.Add($argument) }
    [Diagnostics.Process]::Start($info)
}

function Assert-CandidateIdentity {
    $required = @($Kernel, $App, $AppManifest, $Dataset, $Ovmf, $Bootloader, $CandidateEspPath, $CandidateVerificationPath)
    foreach ($path in $required) { if (-not (Test-Path -LiteralPath $path)) { throw "SN10 immutable candidate input is missing: $path" } }
    $verification = Get-Content -LiteralPath $CandidateVerificationPath -Raw | ConvertFrom-Json
    $espFull = [IO.Path]::GetFullPath($CandidateEspPath).TrimEnd('\')
    $recordedEsp = [IO.Path]::GetFullPath([string]$verification.stagedEsp).TrimEnd('\')
    if ($recordedEsp -cne $espFull) { throw "Candidate manifest names a different ESP: $recordedEsp" }
    $sourceExpected = @{
        'proof-kernel' = $Expected.Kernel
        'native-package' = $Expected.NativeElf
        'manifest' = $Expected.Manifest
        'DE440s-data' = $Expected.Dataset
        'UEFI-bootloader' = $Expected.Bootloader
    }
    $staged = [ordered]@{}
    foreach ($entry in $verification.stagedInputs) {
        $path = [IO.Path]::GetFullPath([string]$entry.path)
        if (-not $path.StartsWith($espFull + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Staged manifest entry escaped the candidate ESP: $path" }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Staged candidate file is missing: $path" }
        $actual = Get-Sha256 $path
        if ($actual -cne [string]$entry.sha256) { throw "Staged file differs from its verification manifest: $path" }
        if ($sourceExpected.ContainsKey([string]$entry.name) -and $actual -cne $sourceExpected[[string]$entry.name]) { throw "Staged candidate hash does not match the accepted identity: $($entry.name)" }
        $staged[[string]$entry.name] = [ordered]@{ path = $path; lengthBytes = (Get-Item -LiteralPath $path).Length; sha256 = $actual; expectedSha256 = $sourceExpected[[string]$entry.name]; matchesExpected = $actual -ceq $sourceExpected[[string]$entry.name] }
    }
    foreach ($name in $sourceExpected.Keys) { if (-not $staged.Contains($name) -or -not $staged[$name].matchesExpected) { throw "Candidate verification manifest did not validate required input: $name" } }
    $sourceFiles = [ordered]@{}
    foreach ($pair in @(@('kernel', $Kernel, $Expected.Kernel), @('nativeElf', $App, $Expected.NativeElf), @('dataset', $Dataset, $Expected.Dataset), @('manifest', $AppManifest, $Expected.Manifest), @('ovmf', $Ovmf, $Expected.Ovmf), @('bootloader', $Bootloader, $Expected.Bootloader))) {
        $actual = Get-Sha256 $pair[1]
        if ($actual -cne $pair[2]) { throw "Current source artifact identity mismatch: $($pair[0]) ($actual)" }
        $sourceFiles[$pair[0]] = [ordered]@{ path = $pair[1]; lengthBytes = (Get-Item -LiteralPath $pair[1]).Length; sha256 = $actual; expectedSha256 = $pair[2]; matchesExpected = $true }
    }
    $result = [ordered]@{ passed = $true; immutableExistingEspReused = $true; qemuEspReadOnly = $true; sourceArtifacts = $sourceFiles; stagedEsp = $espFull; stagedFiles = $staged }
    $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $script:RunRoot 'candidate-verification.json') -Encoding utf8
    return $result
}

function Get-QacSnapshot([string]$Operation, [string[]]$Arguments) {
    $output = @(& $QacCli @Arguments 2>&1)
    $exit = $LASTEXITCODE
    $text = ($output | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
    $path = Join-Path $script:RunRoot $Operation
    [IO.File]::WriteAllText($path, $text + [Environment]::NewLine, [Text.Encoding]::UTF8)
    if ($exit -ne 0) { throw "QAC $($Arguments[0]) failed with exit code $($exit): $text" }
    return $text
}

function Get-OwnedQacReservation([string]$ReservationId, [int]$QemuPid) {
    if (-not $ReservationId) { throw 'QAC reservation ID is not yet known.' }
    $raw = Get-QacSnapshot 'qac-reservation-inspect.latest.json' @('inspect', $ReservationId)
    $reservation = $raw | ConvertFrom-Json
    $same = ([string]$reservation.id -ceq $ReservationId) -and
        ([string]$reservation.project -ceq 'StellarNavigator') -and
        ([string]$reservation.mode -ceq 'acceptance') -and
        ([string]$reservation.state -ceq 'Running') -and
        ([int]$reservation.processId -eq $QemuPid) -and
        ([string]$reservation.processIdentity -ieq [IO.Path]::GetFullPath($Qemu))
    if (-not $same) { throw "QAC did not verify this run's exact registered QEMU child; reservation=$raw" }
    $reservation | ConvertTo-Json -Depth 8 | Add-Content -LiteralPath (Join-Path $script:RunRoot 'qac-reservation-snapshots.jsonl') -Encoding utf8
    return $reservation
}

function Get-QacReservationId([SN10R3ManagedProcess]$Managed) {
    foreach ($line in $Managed.DrainStderrLines()) {
        if ($line -match 'QAC reservation requested: id=([0-9a-fA-F-]{16,64}) project=StellarNavigator mode=acceptance') { return $Matches[1] }
    }
    return $null
}

function Get-DirectQemuChild([int]$ParentPid) {
    $child = Get-CimInstance Win32_Process -Filter "ParentProcessId = $ParentPid" | Where-Object { $_.Name -match '^qemu-system-x86_64\.exe$' } | Select-Object -First 1
    if (-not $child) { return $null }
    $process = Get-Process -Id $child.ProcessId -ErrorAction Stop
    [pscustomobject]@{ Process = $process; Cim = $child }
}

function Assert-OwnedQemuIdentity($Owned) {
    if (-not $Owned -or $Owned.Process.HasExited) { throw 'Owned QEMU is no longer running.' }
    $current = Get-CimInstance Win32_Process -Filter "ProcessId = $($Owned.Process.Id)"
    if (-not $current -or $current.ParentProcessId -ne $script:ManagedQac.Process.Id -or
        $current.Name -cne 'qemu-system-x86_64.exe' -or $current.ExecutablePath -ine [IO.Path]::GetFullPath($Qemu)) {
        throw 'QEMU process identity or direct QAC parent changed; refusing to operate on it.'
    }
    $startDelta = [Math]::Abs(($Owned.Process.StartTime.ToUniversalTime() - ([datetime]$current.CreationDate).ToUniversalTime()).TotalSeconds)
    if ($startDelta -gt 2) {
        throw 'QEMU process start identity changed; refusing to operate on it.'
    }
}

function Send-OwnedMonitorCommand([string]$Command) {
    Assert-OwnedQemuIdentity $script:Owned
    [void](Get-OwnedQacReservation $script:ReservationId $script:Owned.Process.Id)
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $client.SendTimeout = 1000
        $client.ReceiveTimeout = 1000
        $connect = $client.ConnectAsync([Net.IPAddress]::Loopback, $script:MonitorPort)
        if (-not $connect.Wait(1500)) { throw 'Timed out connecting to the QEMU monitor.' }
        $stream = $client.GetStream()
        $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 1024, $true)
        $writer.NewLine = "`n"
        $writer.AutoFlush = $true
        $writer.WriteLine($Command)
        Start-Sleep -Milliseconds 200
        $memory = [IO.MemoryStream]::new()
        $buffer = New-Object byte[] 4096
        $readDeadline = [Diagnostics.Stopwatch]::StartNew()
        while ($readDeadline.ElapsedMilliseconds -lt 1000 -and $stream.DataAvailable) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            $memory.Write($buffer, 0, $count)
        }
        $response = [Text.Encoding]::ASCII.GetString($memory.ToArray())
        [IO.File]::AppendAllText((Join-Path $script:RunRoot 'qemu-monitor-commands.log'), ("{0}`t{1}`t{2}`r`n" -f (Get-Date -Format o), $Command, ($response -replace "[`r`n]+", ' ')), [Text.Encoding]::UTF8)
        return $response
    } finally { $client.Dispose() }
}

function Save-Screenshot([string]$Name) {
    $path = Join-Path $script:RunRoot $Name
    $escaped = $path.Replace('"', '\"')
    $response = Send-OwnedMonitorCommand ("screendump `"$escaped`"")
    $until = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path -LiteralPath $path -PathType Leaf) -and $until.ElapsedMilliseconds -lt 3000) { Start-Sleep -Milliseconds 100 }
    $exists = Test-Path -LiteralPath $path -PathType Leaf
    $length = if ($exists) { (Get-Item -LiteralPath $path).Length } else { 0 }
    $item = [ordered]@{ path = $path; captured = ($exists -and $length -gt 1024); lengthBytes = $length; monitorResponse = $response }
    $item | ConvertTo-Json -Depth 5 | Add-Content -LiteralPath (Join-Path $script:RunRoot 'screenshots.jsonl') -Encoding utf8
    return $item
}

function Invoke-SelfTest {
    $runnerResult = [ordered]@{
        schemaVersion = 1
        mode = 'offline-runner-self-test'
        startedAtLocal = (Get-Date).ToString('o')
        classification = 'PASS'
        qemuLaunched = $false
        qacReservationRequested = $false
        checks = [ordered]@{}
        logs = [ordered]@{}
    }
    $capture1 = $null; $capture2 = $null; $capture3 = $null; $capture4 = $null
    try {
        $argCheck = Test-StructuredArgumentPassing
        $driveArgs = New-QemuArguments 'C:\SN10 R3 & (candidate), rev 2\esp' 40001 40002 'C:\SN10 R3 & (firmware), rev 2\OVMF.fd'
        $firmwareArg = $driveArgs[3]
        $espArg = $driveArgs[5]
        $expectedFirmwareArg = 'if=pflash,format=raw,readonly=on,file=' + ('C:\SN10 R3 & (firmware), rev 2\OVMF.fd'.Replace(', ', ',, '))
        if ($firmwareArg -cne $expectedFirmwareArg -or $espArg -notmatch 'file=fat:.*format=raw,if=ide,index=0,readonly=on' -or $driveArgs[0] -ne '-machine') {
            throw 'Safe QEMU drive-construction assertions failed.'
        }
        if ($firmwareArg.Contains('"') -or $espArg.Contains('"') -or $espArg -notmatch ',, rev 2') { throw 'QEMU drive path escaping or structured quoting is invalid.' }
        $runnerResult.checks.commandConstruction = [ordered]@{
            passed = $true
            windowsPathsWithSpaces = $true
            qemuDriveFilenameCommasDoubled = $true
            shellQuotesEmbedded = $false
            firmwareAttachment = $firmwareArg
            espAttachment = $espArg
            espInterface = 'ide index 0, read-only FAT directory'
            isolation = 'no image writes; existing FAT directory is attached read-only'
            argumentVector = $driveArgs
            processArgumentRoundTrip = $argCheck
        }

        $capture1Path = Join-Path $script:RunRoot 'selftest-live.serial.log'
        $capture1 = [SN10R3SerialCapture]::new($capture1Path)
        $port1 = $capture1.Port
        $producer1 = Start-TestProducer -Port $port1 -Scenario Markers -ExitCode 23
        $runnerResult.checks.captureEndpointInitialized = $true
        [void](Wait-SerialPattern -Capture $capture1 -Pattern '\[TEST\] boot-ready' -Seconds 5 -Description 'controlled boot marker')
        [void](Wait-SerialPattern -Capture $capture1 -Pattern '\[TEST\] app-visible' -Seconds 5 -Description 'controlled application marker')
        [void](Wait-SerialPattern -Capture $capture1 -Pattern '\[TEST\] ephemeris-evaluated' -Seconds 5 -Description 'controlled computation marker')
        if (-not $producer1.WaitForExit(5000)) { $producer1.Kill(); throw 'Controlled serial producer did not exit.' }
        [void]$capture1.WaitForCompletion(3000)
        $liveText = $capture1.Snapshot()
        if ($producer1.ExitCode -ne 23 -or $capture1.BytesReceived -le 0 -or $liveText -notmatch 'partial-after-nonzero-exit') { throw 'Abnormal producer exit did not leave a complete partial log.' }
        $runnerResult.checks.liveSerialStream = [ordered]@{ passed = $true; connected = $capture1.Connected; bytes = $capture1.BytesReceived; markersRecognizedIncrementally = $true; producerExitCode = $producer1.ExitCode; partialLogPreservedAfterAbnormalTermination = $true; logPath = $capture1Path }
        $capture1.Dispose(); $capture1 = $null
        $portReleased = $false
        try { $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $port1); $listener.Start(); $listener.Stop(); $portReleased = $true } catch { }
        if (-not $portReleased) { throw 'Serial capture port was not released after completion.' }

        $capture2Path = Join-Path $script:RunRoot 'selftest-missing-marker.serial.log'
        $capture2 = [SN10R3SerialCapture]::new($capture2Path)
        $producer2 = Start-TestProducer -Port $capture2.Port -Scenario Partial -ExitCode 0
        [void](Wait-SerialPattern -Capture $capture2 -Pattern '\[TEST\] partial-progress' -Seconds 5 -Description 'controlled partial marker')
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $missingMarkerTimedOut = $false
        try { [void](Wait-SerialPattern -Capture $capture2 -Pattern 'MARKER-THAT-CANNOT-ARRIVE' -Seconds 1 -Description 'absent representative marker') }
        catch { $missingMarkerTimedOut = $_.Exception.Message -like 'GUEST_PROGRESS_INCOMPLETE:*' }
        $boundedMs = $timer.ElapsedMilliseconds
        if (-not $producer2.WaitForExit(5000)) { $producer2.Kill(); throw 'Partial serial producer did not exit.' }
        [void]$capture2.WaitForCompletion(3000)
        if (-not $missingMarkerTimedOut -or $boundedMs -gt 2000 -or $capture2.Snapshot() -notmatch 'partial-progress') { throw 'Missing-marker timeout or partial output preservation failed.' }
        $runnerResult.checks.boundedMissingMarker = [ordered]@{ passed = $true; classification = 'GUEST_PROGRESS_INCOMPLETE'; elapsedMilliseconds = $boundedMs; bounded = $true; partialLogPreserved = $true; logPath = $capture2Path }
        $capture2.Dispose(); $capture2 = $null

        $capture3Path = Join-Path $script:RunRoot 'selftest-zero-output.serial.log'
        $capture3 = [SN10R3SerialCapture]::new($capture3Path)
        $producer3 = Start-TestProducer -Port $capture3.Port -Scenario Silent -ExitCode 0
        if (-not $capture3.WaitForCompletion(5000) -or -not $producer3.WaitForExit(5000)) { throw 'Silent controlled producer did not complete.' }
        if (-not $capture3.Connected -or $capture3.BytesReceived -ne 0 -or $capture3.ErrorMessage) { throw 'Connected zero-byte output was not distinguished from capture failure.' }
        $runnerResult.checks.noGuestOutput = [ordered]@{ passed = $true; classification = 'NO_GUEST_SERIAL_OUTPUT'; connected = $true; bytes = 0; logPath = $capture3Path }
        $capture3.Dispose(); $capture3 = $null

        $capture4Path = Join-Path $script:RunRoot 'selftest-no-client.serial.log'
        $capture4 = [SN10R3SerialCapture]::new($capture4Path)
        $captureFailureObserved = $false
        try { [void](Wait-SerialPattern -Capture $capture4 -Pattern 'unavailable-without-a-client' -Seconds 1 -Description 'no-client capture failure') }
        catch { $captureFailureObserved = $_.Exception.Message -like 'SERIAL_CAPTURE_FAILURE:*' }
        if (-not $captureFailureObserved -or $capture4.Connected -or $capture4.BytesReceived -ne 0) { throw 'No-client capture failure was not distinguished from connected zero-byte output.' }
        $capture4.Dispose(); $capture4 = $null
        $runnerResult.checks.captureFailureClassification = [ordered]@{ passed = $true; classification = 'SERIAL_CAPTURE_FAILURE'; noClientDetected = $captureFailureObserved; rule = 'No QEMU TCP client is a capture failure; a connected client with zero bytes is no guest output.' }
        $runnerResult.checks.resourcesReleased = [ordered]@{ passed = $true; listenerStopped = $true; ephemeralPortRebindSucceeded = $true }
        $runnerResult.logs = [ordered]@{ live = $capture1Path; missingMarker = $capture2Path; noOutput = $capture3Path }
        $runnerResult.finishedAtLocal = (Get-Date).ToString('o')
        $runnerResult.resultPath = Join-Path $script:RunRoot 'runner-selftest.json'
        $runnerResult | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $runnerResult.resultPath -Encoding utf8
        Write-Host "[SN10R3] offline runner self-test PASS: $($runnerResult.resultPath)"
        return 0
    } catch {
        if ($capture1) { $capture1.Dispose() }
        if ($capture2) { $capture2.Dispose() }
        if ($capture3) { $capture3.Dispose() }
        if ($capture4) { $capture4.Dispose() }
        $runnerResult.classification = 'FAIL'
        $runnerResult.error = $_.Exception.Message
        $runnerResult.finishedAtLocal = (Get-Date).ToString('o')
        $runnerResult.resultPath = Join-Path $script:RunRoot 'runner-selftest.json'
        $runnerResult | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $runnerResult.resultPath -Encoding utf8
        Write-Host "[SN10R3] offline runner self-test FAIL: $($runnerResult.error)"
        Write-Host "[SN10R3] evidence: $($runnerResult.resultPath)"
        return 1
    }
}

New-RunDirectory
if ($SelfTest) { exit (Invoke-SelfTest) }
if ($VerifyCandidateOnly) {
    try {
        [void](Assert-CandidateIdentity)
        Write-Host "[SN10R3] candidate and staged manifest verification PASS: $(Join-Path $script:RunRoot 'candidate-verification.json')"
        exit 0
    } catch {
        [ordered]@{ passed = $false; classification = 'FAIL'; error = $_.Exception.Message; candidateEsp = $CandidateEspPath; capturedAtLocal = (Get-Date).ToString('o') } |
            ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $script:RunRoot 'candidate-verification.json') -Encoding utf8
        Write-Host "[SN10R3] candidate verification FAIL: $($_.Exception.Message)"
        exit 1
    }
}

$script:Owned = $null
$script:ManagedQac = $null
$script:ReservationId = $null
$script:MonitorPort = 0
$capture = $null
$overall = [ordered]@{
    schemaVersion = 1
    mode = 'native-guest-acceptance'
    startedAtLocal = (Get-Date).ToString('o')
    branch = (git -C $Root branch --show-current).Trim()
    head = (git -C $Root rev-parse HEAD).Trim()
    phase = (Get-Content -LiteralPath (Join-Path $Root '.phase') -Raw).Trim()
    classification = 'BLOCKED'
    blocker = $null
    checks = [ordered]@{}
    evidenceDirectory = $script:RunRoot
}
$script:RunDeadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)

try {
    if ($overall.branch -cne 'STELLAR_NAVIGATOR' -or $overall.head -cne 'd5d944b69c55b0cd74dd8b5ba9a6d5ea31305467' -or $overall.phase -cne 'SN10') {
        throw 'Repository branch, HEAD, or phase does not match the authorized SN10 starting state.'
    }
    if (-not (Test-Path -LiteralPath $Qemu) -or -not (Test-Path -LiteralPath $QacCli)) { throw 'Installed QEMU or QAC CLI is unavailable.' }
    $candidateCheck = Assert-CandidateIdentity
    $overall.checks.artifactIntegrity = $candidateCheck

    $qacStatus = Get-QacSnapshot 'qac-status-before.log' @('status')
    $qacList = Get-QacSnapshot 'qac-list-before.log' @('list')
    if ($qacStatus -notmatch 'Coordinator: Online') {
        $overall.blocker = 'QAC_RESERVATION_FAILURE'
        $overall.checks.qacPreflight = [ordered]@{ coordinatorOnline = $false; statusPath = (Join-Path $script:RunRoot 'qac-status-before.log'); listPath = (Join-Path $script:RunRoot 'qac-list-before.log'); reservationNotRequested = $true }
        throw 'QAC coordinator is not online.'
    }
    $processCount = if ($qacStatus -match 'QEMU processes: (\d+)') { [int]$Matches[1] } else { -1 }
    $sessionCount = if ($qacStatus -match 'Managed sessions: (\d+)') { [int]$Matches[1] } else { -1 }
    $reservationCount = if ($qacList -match 'Reservations: (\d+)') { [int]$Matches[1] } else { -1 }
    if ($processCount -ne 0 -or $sessionCount -ne 0 -or $reservationCount -ne 0) {
        $overall.blocker = 'EXTERNAL_QEMU_COORDINATION_BLOCKER'
        $overall.checks.qacPreflight = [ordered]@{
            coordinatorOnline = $true
            processCount = $processCount
            managedSessionCount = $sessionCount
            reservationCount = $reservationCount
            reservationNotRequested = $true
            statusPath = (Join-Path $script:RunRoot 'qac-status-before.log')
            listPath = (Join-Path $script:RunRoot 'qac-list-before.log')
        }
        throw 'EXTERNAL_QEMU_COORDINATION_BLOCKER: QAC is occupied or its census is incomplete; no reservation was requested.'
    }
    $overall.checks.qacPreflight = [ordered]@{ coordinatorOnline = $true; processCount = 0; managedSessionCount = 0; reservationCount = 0; statusPath = (Join-Path $script:RunRoot 'qac-status-before.log'); listPath = (Join-Path $script:RunRoot 'qac-list-before.log') }

    $monitorProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $monitorProbe.Start()
    $script:MonitorPort = ([Net.IPEndPoint]$monitorProbe.LocalEndpoint).Port
    $monitorProbe.Stop()
    $serialLog = Join-Path $script:RunRoot 'guest-serial.log'
    $capture = [SN10R3SerialCapture]::new($serialLog)
    $qemuArguments = @(New-QemuArguments $CandidateEspPath $capture.Port $script:MonitorPort)
    $qacArguments = @('run', '--project', 'StellarNavigator', '--mode', 'acceptance', '--', $Qemu) + $qemuArguments
    [ordered]@{
        qacExecutable = $QacCli
        qacArgumentVector = $qacArguments
        qemuExecutable = $Qemu
        qemuArgumentVector = $qemuArguments
        serialEndpoint = [ordered]@{ host = $capture.Host; port = $capture.Port; backend = 'QEMU socket chardev TCP client to runner-owned loopback listener' }
        monitorEndpoint = [ordered]@{ host = '127.0.0.1'; port = $script:MonitorPort; backend = 'HMP TCP loopback, ephemeral per run' }
        espPath = [IO.Path]::GetFullPath($CandidateEspPath)
        espAccess = 'read-only FAT directory backend and read-only drive'
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $script:RunRoot 'qemu-invocation.json') -Encoding utf8
    $overall.checks.launchConstruction = [ordered]@{ passed = $true; invocationPath = (Join-Path $script:RunRoot 'qemu-invocation.json'); effectiveArgumentVectorRecorded = $true; structuredProcessArguments = $true }
    $overall.checks.serialCapture = [ordered]@{ endpointInitializedBeforeQemu = $true; host = $capture.Host; port = $capture.Port; logPath = $serialLog }

    $script:ManagedQac = [SN10R3ManagedProcess]::new($QacCli, $Root, $qacArguments,
        (Join-Path $script:RunRoot 'qac-run.stdout.log'), (Join-Path $script:RunRoot 'qac-run.stderr.log'))
    $launchDeadline = [DateTimeOffset]::UtcNow.AddSeconds($LaneAcquireTimeoutSeconds)
    $ownedChild = $null
    while ([DateTimeOffset]::UtcNow -lt $launchDeadline) {
        if (-not $script:ReservationId) { $script:ReservationId = Get-QacReservationId $script:ManagedQac }
        $ownedChild = Get-DirectQemuChild $script:ManagedQac.Process.Id
        if ($ownedChild) { $script:Owned = $ownedChild; break }
        if ($script:ManagedQac.HasExited) { break }
        Start-Sleep -Milliseconds 200
    }

    if (-not $script:Owned) {
        if (-not $script:ReservationId) { $script:ReservationId = Get-QacReservationId $script:ManagedQac }
        if ($script:ReservationId) {
            $queuedRaw = Get-QacSnapshot 'qac-reservation-inspect.queued.json' @('inspect', $script:ReservationId)
            $queued = $queuedRaw | ConvertFrom-Json
            if ([string]$queued.project -ceq 'StellarNavigator' -and [string]$queued.mode -ceq 'acceptance' -and [string]$queued.state -in @('Waiting', 'Blocked', 'Granted') -and $null -eq $queued.processId) {
                $cancelText = Get-QacSnapshot 'qac-cancel-own-unlaunched.log' @('cancel', $script:ReservationId)
                $overall.checks.qacReservation = [ordered]@{ acquired = $false; reservationId = $script:ReservationId; observedState = $queued.state; onlyOwnUnlaunchedReservationCancelled = $true; cancelEvidence = $cancelText }
                $overall.blocker = 'EXTERNAL_QEMU_COORDINATION_BLOCKER'
            } else {
                $overall.blocker = 'QAC_RESERVATION_FAILURE'
                $overall.checks.qacReservation = [ordered]@{ acquired = $false; reservationId = $script:ReservationId; observedReservation = $queued }
            }
        } else {
            $overall.blocker = 'QAC_RESERVATION_FAILURE'
        }
        if ($script:ManagedQac -and -not $script:ManagedQac.HasExited) { [void]$script:ManagedQac.WaitForExit(10000) }
        throw "QAC did not launch an owned direct QEMU child within $LaneAcquireTimeoutSeconds seconds."
    }

    if (-not $script:ReservationId) { $script:ReservationId = Get-QacReservationId $script:ManagedQac }
    if (-not $script:ReservationId) { throw 'QAC launched QEMU but its reservation identity could not be read.' }
    $cim = $script:Owned.Cim
    $overall.checks.qacReservation = [ordered]@{
        acquired = $true
        reservationId = $script:ReservationId
        project = 'StellarNavigator'
        mode = 'acceptance'
        ownerCliPid = $script:ManagedQac.Process.Id
        qemuPid = $script:Owned.Process.Id
        qemuParentPid = $cim.ParentProcessId
        qemuPath = $cim.ExecutablePath
        qemuCommandLine = $cim.CommandLine
        qemuCreationDate = ([datetime]$cim.CreationDate).ToUniversalTime().ToString('o')
    }
    [void](Get-OwnedQacReservation $script:ReservationId $script:Owned.Process.Id)

    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $bootText = Wait-SerialPattern -Capture $capture -Pattern '\[KERNEL\] VFS layer initialized' -Seconds $remaining -Description 'AMD64 kernel VFS boot marker'
    $overall.checks.kernelBoot = [ordered]@{ passed = $true; marker = '[KERNEL] VFS layer initialized'; desktopRequiredSeparateMarker = $false }
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $discoveryText = Wait-SerialPattern -Capture $capture -Pattern 'App Model package discovered.*Stellar Navigator|displayName=Stellar Navigator' -Seconds $remaining -Description 'Stellar Navigator AppModel discovery'
    $overall.checks.appModelDiscovery = [ordered]@{ passed = $true; marker = ($discoveryText -split "`r?`n" | Where-Object { $_ -match 'App Model package discovered.*Stellar Navigator|displayName=Stellar Navigator' } | Select-Object -Last 1) }
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $appText = Wait-SerialPattern -Capture $capture -Pattern 'STELLAR_NAVIGATOR_UI status=initial-content-drawn' -Seconds $remaining -Description 'native application initial content draw'
    if ($appText -notmatch '\[SN10-GUEST\] appmodel-launch begin id=com\.guidexos\.stellarnavigator' -or $appText -notmatch 'STELLAR_NAVIGATOR_UI status=window-created') { throw 'Guest did not report AppModel-mediated native window creation.' }
    if ($appText -notmatch 'STELLAR_NAVIGATOR_EPHEMERIS status=available body=earth target_naif=399 et_tdb_s=0\.000000 frame=J2000_SSB units=km position_km=\(([-+0-9.]+),([-+0-9.]+),([-+0-9.]+)\) sha256=' + $Expected.Dataset) { throw 'Native ephemeris guest marker is missing, malformed, or does not identify the authenticated DE440s data.' }
    $ephemerisLine = [regex]::Match($appText, 'STELLAR_NAVIGATOR_EPHEMERIS status=available[^\r\n]+').Value
    $positionMatch = [regex]::Match($ephemerisLine, 'position_km=\(([-+0-9.]+),([-+0-9.]+),([-+0-9.]+)\)')
    $observed = @([double]::Parse($positionMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture), [double]::Parse($positionMatch.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture), [double]::Parse($positionMatch.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture))
    $reference = @(-27566740.483, 132361381.154, 57418653.286)
    $components = @('X', 'Y', 'Z')
    $comparison = @()
    for ($i = 0; $i -lt 3; $i++) {
        $delta = [Math]::Abs($observed[$i] - $reference[$i])
        $comparison += [pscustomobject]@{ component = $components[$i]; observedKm = $observed[$i]; trustedKm = $reference[$i]; absoluteDifferenceKm = $delta; toleranceKm = 0.0005; passed = ($delta -le 0.0005) }
    }
    if (@($comparison | Where-Object { -not $_.passed }).Count -gt 0) { throw 'Guest ephemeris coordinates exceed the established 0.0005 km displayed-value tolerance.' }
    $overall.checks.nativeApplication = [ordered]@{ appModelLaunchMarker = $true; nativeWindowCreated = $true; initialContentDrawn = $true; guestEphemerisEvaluatorObserved = $true; ephemerisLine = $ephemerisLine; coordinateFrame = 'J2000_SSB'; units = 'km'; epochEtTdbSeconds = 0; numericalComparison = $comparison; passed = $true }
    $overall.checks.screenshotWhileOpen = Save-Screenshot 'stellar-navigator-open.ppm'

    [void](Send-OwnedMonitorCommand 'sendkey esc')
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $afterClose = Wait-SerialPattern -Capture $capture -Pattern 'STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown' -Seconds $remaining -Description 'normal Stellar Navigator shutdown'
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    [void](Wait-SerialPattern -Capture $capture -Pattern '\[SN10-GUEST\] appmodel-launch result=success' -Seconds $remaining -Description 'AppModel launch return after close')
    $overall.checks.normalClose = [ordered]@{ passed = $true; lifecycleMarker = 'STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown'; loaderCleanupMarker = ($afterClose -match 'lifecycle PASS window/resource cleanup complete'); appModelReturnMarker = '[SN10-GUEST] appmodel-launch result=success' }
    if (-not $overall.checks.normalClose.loaderCleanupMarker) { throw 'Native loader did not report complete window/resource cleanup.' }
    $overall.checks.screenshotAfterClose = Save-Screenshot 'desktop-after-close.ppm'

    # Reopen through the guest desktop's Start Menu, using the discovered package row.
    [void](Send-OwnedMonitorCommand 'sendkey super')
    Start-Sleep -Milliseconds 500
    $overall.checks.startMenuScreenshot = Save-Screenshot 'start-menu-for-reopen.ppm'
    $qacInfo = Send-OwnedMonitorCommand 'info mice'
    $overall.checks.inputDevices = $qacInfo
    $overall.checks.reopen = [ordered]@{ attempted = $false; passed = $false; method = 'Start Menu / AppModel UI'; blocker = 'Interactive menu selection requires a verified desktop hit target.' }
    $overall.classification = 'BLOCKED'
    $overall.blocker = 'GUEST_REOPEN_INTERACTION_UNVERIFIED'
    $overall.checks.desktopUsable = [ordered]@{ serialBootAndAppModelPathVerified = $true; visualDesktopAfterCloseCaptured = $overall.checks.screenshotAfterClose.captured; screenshotPath = $overall.checks.screenshotAfterClose.path }
    throw 'First launch, computation, UI markers, screenshot, normal close, and loader cleanup passed. Reopen was not claimed because no menu selection has yet been verified.'
} catch {
    if ($overall.blocker -eq 'EXTERNAL_QEMU_COORDINATION_BLOCKER') { $overall.classification = 'BLOCKED' }
    elseif ($_.Exception.Message -like 'SERIAL_CAPTURE_FAILURE:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'SERIAL_CAPTURE_FAILURE' }
    elseif ($_.Exception.Message -like 'NO_GUEST_SERIAL_OUTPUT:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'NO_GUEST_SERIAL_OUTPUT' }
    elseif ($_.Exception.Message -like 'GUEST_PROGRESS_INCOMPLETE:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'GUEST_PROGRESS_INCOMPLETE' }
    elseif ($overall.blocker) { $overall.classification = 'BLOCKED' }
    else { $overall.classification = 'FAIL'; $overall.blocker = $null }
    $overall.error = $_.Exception.Message
} finally {
    if ($script:Owned -and -not $script:Owned.Process.HasExited) {
        try {
            Assert-OwnedQemuIdentity $script:Owned
            [void](Get-OwnedQacReservation $script:ReservationId $script:Owned.Process.Id)
            [void](Send-OwnedMonitorCommand 'quit')
            if (-not $script:Owned.Process.WaitForExit(8000) -and -not $script:Owned.Process.HasExited) {
                Assert-OwnedQemuIdentity $script:Owned
                [void](Get-OwnedQacReservation $script:ReservationId $script:Owned.Process.Id)
                Stop-Process -Id $script:Owned.Process.Id -Force -ErrorAction Stop
                [void]$script:Owned.Process.WaitForExit(5000)
            }
        } catch { $overall.cleanupWarning = $_.Exception.Message }
    }
    if ($capture) {
        [void]$capture.WaitForCompletion(3000)
        $overall.checks.serialCaptureFinal = [ordered]@{ connected = $capture.Connected; bytesReceived = $capture.BytesReceived; completed = $capture.Completed; error = $capture.ErrorMessage; logPath = $serialLog }
        $capture.Dispose()
    }
    if ($script:ManagedQac) {
        if (-not $script:ManagedQac.HasExited) { [void]$script:ManagedQac.WaitForExit(30000) }
        if ($script:ManagedQac.HasExited) {
            $overall.qacWrapperExitCode = $script:ManagedQac.ExitCode
            if ($script:ReservationId) {
                try {
                    $post = Get-QacSnapshot 'qac-list-after-run.log' @('list')
                    $overall.qacPostRunList = $post
                    $overall.reservationReleased = $post -match 'Reservations: 0' -and $post -match 'Managed sessions: 0'
                } catch { $overall.reservationReleaseError = $_.Exception.Message }
            }
        } else {
            $overall.reservationReleased = $false
            $overall.cleanupWarning = 'QAC wrapper remained alive after the bounded cleanup wait; its reservation was not manually released.'
        }
        $script:ManagedQac.Dispose()
    }
    $overall.finishedAtLocal = (Get-Date).ToString('o')
    $overall.serialLog = if ($serialLog) { $serialLog } else { $null }
    $overall.resultPath = Join-Path $script:RunRoot 'acceptance-result.json'
    $overall | ConvertTo-Json -Depth 18 | Set-Content -LiteralPath $overall.resultPath -Encoding utf8
    Write-Host "[SN10R3] $($overall.classification): $($overall.resultPath)"
    if ($overall.blocker) { Write-Host "[SN10R3] blocker=$($overall.blocker)" }
}

if ($overall.classification -eq 'PASS') { exit 0 }
if ($overall.classification -eq 'BLOCKED') { exit 2 }
exit 1
