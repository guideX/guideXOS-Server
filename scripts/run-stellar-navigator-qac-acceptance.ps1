[CmdletBinding()]
param(
    [ValidateRange(20, 300)]
    [int]$TimeoutSeconds = 120,
    [ValidateRange(5, 60)]
    [int]$LaneAcquireTimeoutSeconds = 20,
    [string]$ArtifactRoot = '',
    [string]$CandidateEspPath = 'E:\bkup\artifacts\SN10R2_20261020\qemu\20261020T042331758\esp',
    [string]$CandidateVerificationPath = 'E:\bkup\artifacts\SN10R2_20261020\staged-candidate-verification.json',
    [string]$CandidateDiskPath = 'E:\bkup\artifacts\SN10R6_20261020\media\stellar-navigator-sn10r6.raw',
    [string]$CandidateDiskVerificationPath = 'E:\bkup\artifacts\SN10R6_20261020\media\stellar-navigator-sn10r6-verification.json',
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

function New-DisposableQemuDiskCopy([string]$SourcePath, [string]$DestinationPath) {
    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) { throw "Verified raw disk source is missing: $SourcePath" }
    if (Test-Path -LiteralPath $DestinationPath) { throw "Refusing to replace an existing per-run disk copy: $DestinationPath" }
    [IO.File]::Copy([IO.Path]::GetFullPath($SourcePath), [IO.Path]::GetFullPath($DestinationPath), $false)
    $sourceHash = Get-Sha256 $SourcePath
    $copyHash = Get-Sha256 $DestinationPath
    if ($copyHash -cne $sourceHash) { throw 'Disposable QEMU disk copy differs from its verified source image.' }
    [ordered]@{ path = [IO.Path]::GetFullPath($DestinationPath); lengthBytes = (Get-Item -LiteralPath $DestinationPath).Length; initialSha256 = $copyHash; sourcePath = [IO.Path]::GetFullPath($SourcePath); sourceSha256 = $sourceHash; byteIdenticalAtCreation = $true; sourceModified = $false }
}

function ConvertTo-QemuOptionValue([string]$Value) {
    # QEMU keyval option values escape a filename comma by doubling it.
    # ProcessStartInfo.ArgumentList handles Windows argument quoting separately.
    $Value.Replace(',', ',,')
}

function New-QemuArguments([string]$DiskPath, [int]$SerialPort, [int]$MonitorPort, [string]$FirmwarePath = $Ovmf) {
    $firmware = ConvertTo-QemuOptionValue ([IO.Path]::GetFullPath($FirmwarePath))
    $disk = ConvertTo-QemuOptionValue ([IO.Path]::GetFullPath($DiskPath))
    @(
        '-machine', 'pc,usb=off',
        '-drive', "if=pflash,format=raw,readonly=on,file=$firmware",
        '-drive', "file=$disk,format=raw,if=ide,index=0",
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
        $ArtifactRoot = Join-Path $env:USERPROFILE ('.codex\artifacts\SN10R6_' + (Get-Date -Format 'yyyyMMdd'))
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
        '-drive', "file=C:\SN10 R3 & (quoted)'s path\candidate,, rev 2.raw,format=raw,if=ide,index=0",
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

function Get-GuestFailureMarker([string]$Text) {
    $pattern = '(?m)(\[KERNEL-PANIC\][^\r\n]*|\[KERNEL-FAULT\][^\r\n]*|\[NATIVE-ELF-FAULT\][^\r\n]*|\[Exception\] (?:Unhandled exception|Double fault)[^\r\n]*|Triple fault[^\r\n]*)'
    $match = [regex]::Match($Text, $pattern)
    if ($match.Success) { return $match.Value }
    return $null
}

function Wait-SerialPattern {
    param(
        [Parameter(Mandatory = $true)]$Capture,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][int]$Seconds,
        [string]$Description = $Pattern,
        [int]$StartIndex = 0
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        $snapshot = $Capture.Snapshot()
        if ($StartIndex -lt 0 -or $StartIndex -gt $snapshot.Length) { throw 'Serial marker start index is outside the captured stream.' }
        $text = $snapshot.Substring($StartIndex)
        $guestFailure = Get-GuestFailureMarker $text
        if ($guestFailure) { throw "GUEST_FAILURE: guest emitted a fatal kernel/native marker: $guestFailure" }
        if ($text -match $Pattern) { return $text }
        if ($Capture.ErrorMessage) { throw "SERIAL_CAPTURE_FAILURE: $($Capture.ErrorMessage)" }
        if ($Capture.Completed) {
            if (-not $Capture.Connected) { throw "SERIAL_CAPTURE_FAILURE: serial endpoint received no QEMU client before '$Description'." }
            if ($Capture.BytesReceived -eq 0) { throw "NO_GUEST_SERIAL_OUTPUT: QEMU connected but sent no serial bytes before '$Description'." }
            if ($script:Owned -and $script:Owned.Process -and $script:Owned.Process.HasExited) {
                if ($script:Owned.Process.ExitCode -ne 0) { throw "GUEST_FAILURE: QEMU exited with code $($script:Owned.Process.ExitCode) before '$Description'." }
                throw "MISSING_GUEST_MARKER: QEMU exited successfully without '$Description'."
            }
            throw "SERIAL_STREAM_DISCONNECT_FAILURE: QEMU serial TCP stream closed before '$Description' while the guest process remained available."
        }
        if ($script:Owned -and $script:Owned.Process -and $script:Owned.Process.HasExited) {
            if ($script:Owned.Process.ExitCode -ne 0) { throw "GUEST_FAILURE: QEMU exited with code $($script:Owned.Process.ExitCode) before '$Description'." }
            throw "MISSING_GUEST_MARKER: QEMU exited successfully without '$Description'."
        }
        $left = [Math]::Max(1, [int][Math]::Ceiling(($Seconds - $timer.Elapsed.TotalSeconds) * 1000))
        [void]$Capture.WaitForChange([Math]::Min(250, $left))
    }
    $lastSnapshot = $Capture.Snapshot()
    if ($StartIndex -ge 0 -and $StartIndex -le $lastSnapshot.Length) {
        $guestFailure = Get-GuestFailureMarker $lastSnapshot.Substring($StartIndex)
        if ($guestFailure) { throw "GUEST_FAILURE: guest emitted a fatal kernel/native marker: $guestFailure" }
    }
    if (-not $Capture.Connected) { throw "SERIAL_CAPTURE_FAILURE: serial endpoint received no QEMU client before '$Description'." }
    if ($Capture.BytesReceived -eq 0) { throw "NO_GUEST_SERIAL_OUTPUT: QEMU connected but sent no serial bytes before '$Description'." }
    if ($Capture.ErrorMessage) { throw "SERIAL_CAPTURE_FAILURE: $($Capture.ErrorMessage)" }
    if ($Capture.Completed) { throw "SERIAL_STREAM_DISCONNECT_FAILURE: QEMU serial TCP stream closed before '$Description'." }
    if ($script:Owned -and $script:Owned.Process -and $script:Owned.Process.HasExited) {
        if ($script:Owned.Process.ExitCode -ne 0) { throw "GUEST_FAILURE: QEMU exited with code $($script:Owned.Process.ExitCode) before '$Description'." }
        throw "MISSING_GUEST_MARKER: QEMU exited successfully without '$Description'."
    }
    throw "GUEST_TIMEOUT: bounded wait for '$Description' expired while the serial stream and guest remained active."
}

function Get-QemuTextKeyCommands([string]$Text) {
    $commands = [System.Collections.Generic.List[string]]::new()
    foreach ($character in $Text.ToCharArray()) {
        if (($character -ge 'a' -and $character -le 'z') -or ($character -ge '0' -and $character -le '9')) {
            $key = [string]$character
        } else {
            switch ([string]$character) {
                ' ' { $key = 'spc' }
                '.' { $key = 'dot' }
                default { throw "Unsupported character in bounded guest shell command: [$character]" }
            }
        }
        $commands.Add("sendkey $key")
    }
    $commands.Add('sendkey ret')
    return $commands.ToArray()
}

function Send-OwnedGuestText([string]$Text) {
    $commands = @(Get-QemuTextKeyCommands $Text)
    foreach ($command in $commands) { [void](Send-OwnedMonitorCommand $command) }
    return [ordered]@{ commandText = $Text; keyCommandCount = $commands.Count; submittedWith = 'QEMU HMP sendkey; native guest keyboard input'; }
}

function Get-QacReservationCleanupAction($Reservation, [string]$ReservationId, [int]$OwnerCliPid) {
    if (-not $Reservation -or
        [string]$Reservation.id -cne $ReservationId -or
        [string]$Reservation.project -cne 'StellarNavigator' -or
        [string]$Reservation.mode -cne 'acceptance' -or
        [string]$Reservation.owner -notmatch "\(PID $OwnerCliPid\)" -or
        $null -ne $Reservation.processId) {
        return 'DoNotTouch'
    }
    if ([string]$Reservation.state -in @('Waiting', 'Blocked')) { return 'CancelOwnPending' }
    return 'DoNotTouch'
}

function Get-VerifiedGuestEphemeris([string]$Text) {
    $pattern = 'STELLAR_NAVIGATOR_EPHEMERIS status=available body=earth target_naif=399 et_tdb_s=0\.000000 frame=J2000_SSB units=km position_km=\(([-+0-9.]+),([-+0-9.]+),([-+0-9.]+)\) sha256=' + $Expected.Dataset
    if ($Text -notmatch $pattern) { throw 'Native guest ephemeris marker is missing, malformed, or does not identify the authenticated DE440s data.' }
    $line = [regex]::Match($Text, 'STELLAR_NAVIGATOR_EPHEMERIS status=available[^\r\n]+').Value
    $position = [regex]::Match($line, 'position_km=\(([-+0-9.]+),([-+0-9.]+),([-+0-9.]+)\)')
    if (-not $position.Success) { throw 'Native guest ephemeris coordinate tuple is malformed.' }
    $observed = @(
        [double]::Parse($position.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture),
        [double]::Parse($position.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture),
        [double]::Parse($position.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
    )
    $reference = @(-27566740.483, 132361381.154, 57418653.286)
    $components = @('X', 'Y', 'Z')
    $comparison = @()
    for ($i = 0; $i -lt 3; $i++) {
        $delta = [Math]::Abs($observed[$i] - $reference[$i])
        $comparison += [pscustomobject]@{ component = $components[$i]; observedKm = $observed[$i]; trustedKm = $reference[$i]; absoluteDifferenceKm = $delta; toleranceKm = 0.0005; passed = ($delta -le 0.0005) }
    }
    if (@($comparison | Where-Object { -not $_.passed }).Count -gt 0) { throw 'Guest ephemeris coordinates exceed the established 0.0005 km displayed-value tolerance.' }
    return [pscustomobject]@{ line = $line; comparison = $comparison; frame = 'J2000_SSB'; units = 'km'; epochEtTdbSeconds = 0; evaluatorMarkerObserved = $true }
}

function Confirm-OwnedGuestStable([int]$Seconds) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        Assert-OwnedQemuIdentity $script:Owned
        [void](Get-OwnedQacReservation $script:ReservationId $script:Owned.Process.Id)
        if ($capture.ErrorMessage) { throw "SERIAL_CAPTURE_FAILURE: $($capture.ErrorMessage)" }
        if ($capture.Completed) { throw 'SERIAL_STREAM_DISCONNECT_FAILURE: guest serial stream ended during post-close stability observation.' }
        $guestFailure = Get-GuestFailureMarker $capture.Snapshot()
        if ($guestFailure) { throw "GUEST_FAILURE: guest emitted a fatal kernel/native marker after close: $guestFailure" }
        Start-Sleep -Milliseconds 500
    }
    return [ordered]@{ passed = $true; observedSeconds = [Math]::Round($timer.Elapsed.TotalSeconds, 1); qemuPid = $script:Owned.Process.Id; qacReservationVerifiedThroughout = $true; serialConnected = $capture.Connected; serialBytesReceived = $capture.BytesReceived }
}

function Get-LoopbackPort {
    $probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $probe.Start()
    try { return ([Net.IPEndPoint]$probe.LocalEndpoint).Port }
    finally { $probe.Stop() }
}

function Start-TestProducer {
    param([int]$Port, [ValidateSet('Markers', 'Partial', 'PartialHold', 'Fatal', 'Silent')][string]$Scenario, [int]$ExitCode = 23)
    $producerPath = Join-Path $script:RunRoot ("producer-$Scenario.ps1")
    $producer = @'
param([int]$Port, [string]$Scenario, [int]$ExitCode)
$client = [Net.Sockets.TcpClient]::new()
try {
    $client.Connect([Net.IPAddress]::Loopback, $Port)
    $stream = $client.GetStream()
    $lines = switch ($Scenario) {
        'Markers' { @('[TEST] boot-ready', '[TEST] app-visible', '[TEST] ephemeris-evaluated', '[TEST] second-instance-ready', 'partial-after-nonzero-exit') }
        'Partial' { @('[TEST] partial-progress') }
        'PartialHold' { @('[TEST] partial-progress') }
        'Fatal' { @('[KERNEL-PANIC] assertion=offline-fixture halted=1') }
        'Silent' { @() }
    }
    foreach ($line in $lines) {
        $bytes = [Text.Encoding]::ASCII.GetBytes($line + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        Start-Sleep -Milliseconds 100
    }
    if ($Scenario -eq 'PartialHold') { Start-Sleep -Milliseconds 1500 }
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
    $required = @($Kernel, $App, $AppManifest, $Dataset, $Ovmf, $Bootloader, $CandidateEspPath, $CandidateVerificationPath, $CandidateDiskPath, $CandidateDiskVerificationPath)
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
    $diskVerification = Get-Content -LiteralPath $CandidateDiskVerificationPath -Raw | ConvertFrom-Json
    $diskFull = [IO.Path]::GetFullPath($CandidateDiskPath)
    $recordedDisk = [IO.Path]::GetFullPath([string]$diskVerification.imagePath)
    if ($diskVerification.passed -ne $true -or $recordedDisk -cne $diskFull) { throw 'Candidate raw-disk verification does not identify this validated disk image.' }
    if ([string]$diskVerification.format -cne 'raw' -or [string]$diskVerification.partitionTable -cne 'GPT' -or [string]$diskVerification.fatType -cne 'FAT32' -or [int]$diskVerification.sectorSizeBytes -ne 512) { throw 'Candidate disk does not have the verified raw/GPT/FAT32/512-byte layout.' }
    if ([int64]$diskVerification.imageBytes -ne (Get-Item -LiteralPath $diskFull).Length -or [string]$diskVerification.imageSha256 -notmatch '^[0-9a-f]{64}$') { throw 'Candidate disk geometry or recorded SHA-256 is invalid.' }
    $diskHash = Get-Sha256 $diskFull
    if ($diskHash -cne [string]$diskVerification.imageSha256) { throw 'Candidate raw disk SHA-256 does not match its verification record.' }
    if ([IO.Path]::GetFullPath([string]$diskVerification.sourceEsp).TrimEnd('\') -cne $espFull) { throw 'Candidate disk was not built from the validated staged ESP.' }
    if ([int64]$diskVerification.partitionStartLba -lt 34 -or [int64]$diskVerification.partitionSectors -le 0 -or [int64]$diskVerification.partitionEndLba -ne ([int64]$diskVerification.partitionStartLba + [int64]$diskVerification.partitionSectors - 1)) { throw 'Candidate disk partition geometry is inconsistent.' }
    $diskFiles = @{}
    foreach ($entry in $diskVerification.files) { $diskFiles[[string]$entry.path] = [string]$entry.sha256 }
    foreach ($entry in $verification.stagedInputs) {
        $relative = ([IO.Path]::GetRelativePath($espFull, [IO.Path]::GetFullPath([string]$entry.path))).Replace('\', '/')
        if (-not $diskFiles.ContainsKey($relative) -or $diskFiles[$relative] -cne [string]$entry.sha256) { throw "Candidate disk does not contain verified staged payload $relative." }
    }
    $diskRecord = [ordered]@{ path = $diskFull; lengthBytes = (Get-Item -LiteralPath $diskFull).Length; sha256 = $diskHash; format = 'raw'; partitionTable = 'GPT'; fatType = 'FAT32'; sectorSizeBytes = 512; partitionStartLba = [int64]$diskVerification.partitionStartLba; partitionSectors = [int64]$diskVerification.partitionSectors; partitionEndLba = [int64]$diskVerification.partitionEndLba; verifiedFiles = $diskFiles.Count; verificationPath = [IO.Path]::GetFullPath($CandidateDiskVerificationPath) }
    $result = [ordered]@{ passed = $true; immutableExistingEspReused = $true; candidateDiskUntouchedByRuntime = $true; sourceArtifacts = $sourceFiles; stagedEsp = $espFull; stagedFiles = $staged; candidateDisk = $diskRecord }
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

function Get-QemuEarlyExitDetails([string]$Text) {
    $launch = [regex]::Match($Text, 'QEMU launched: reservation=([0-9a-fA-F-]{16,64})[^\r\n]*pid=(\d+)')
    $exit = [regex]::Match($Text, 'QEMU exited: reservation=([0-9a-fA-F-]{16,64})[^\r\n]*pid=(\d+)[^\r\n]*exitCode=(-?\d+)')
    if (-not $launch.Success -or -not $exit.Success) { return $null }
    if ($launch.Groups[1].Value -cne $exit.Groups[1].Value -or $launch.Groups[2].Value -cne $exit.Groups[2].Value) { return $null }
    if ($Text -match '(?i)Block node is read-only') {
        return [ordered]@{ classification = 'PREBOOT_STORAGE_FAILURE'; reservationId = $launch.Groups[1].Value; qemuPid = [int]$launch.Groups[2].Value; exitCode = [int]$exit.Groups[3].Value; rootCause = 'QEMU rejected the configured storage block node before guest startup: Block node is read-only.' }
    }
    return [ordered]@{ classification = 'PREBOOT_QEMU_STARTUP_FAILURE'; reservationId = $launch.Groups[1].Value; qemuPid = [int]$launch.Groups[2].Value; exitCode = [int]$exit.Groups[3].Value; rootCause = 'QAC launched QEMU, but QEMU exited before the runner could attach to its process or observe guest serial output.' }
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
    $capture1 = $null; $capture2 = $null; $capture3 = $null; $capture4 = $null; $capture5 = $null; $capture6 = $null
    try {
        $argCheck = Test-StructuredArgumentPassing
        $driveArgs = New-QemuArguments 'C:\SN10 R3 & (candidate), rev 2\stellar-navigator.raw' 40001 40002 'C:\SN10 R3 & (firmware), rev 2\OVMF.fd'
        $firmwareArg = $driveArgs[3]
        $diskArg = $driveArgs[5]
        $expectedFirmwareArg = 'if=pflash,format=raw,readonly=on,file=' + ('C:\SN10 R3 & (firmware), rev 2\OVMF.fd'.Replace(', ', ',, '))
        if ($firmwareArg -cne $expectedFirmwareArg -or $diskArg -notmatch '^file=(?!fat:).+\.raw,format=raw,if=ide,index=0$' -or $driveArgs[0] -ne '-machine') {
            throw 'Disposable writable raw GPT/FAT32 disk argument construction failed.'
        }
        if ($firmwareArg.Contains('"') -or $diskArg.Contains('"') -or $diskArg -notmatch ',, rev 2') { throw 'QEMU drive path escaping or structured quoting is invalid.' }
        $runnerResult.checks.commandConstruction = [ordered]@{
            passed = $true
            windowsPathsWithSpaces = $true
            qemuDriveFilenameCommasDoubled = $true
            shellQuotesEmbedded = $false
            firmwareAttachment = $firmwareArg
            diskAttachment = $diskArg
            diskInterface = 'ide index 0, raw GPT disk with FAT32 EFI System Partition, writable disposable copy'
            isolation = 'QEMU receives one per-run copy; the verified candidate disk and source ESP remain unchanged'
            argumentVector = $driveArgs
            processArgumentRoundTrip = $argCheck
        }

        $previousStorageFailure = Get-QemuEarlyExitDetails "QAC QEMU launched: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280`nQAC QEMU exited: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280 exitCode=1`nC:\Program Files\qemu\qemu-system-x86_64.exe: Block node is read-only"
        $otherStartupFailure = Get-QemuEarlyExitDetails "QAC QEMU launched: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280`nQAC QEMU exited: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280 exitCode=1`nfirmware argument could not be opened"
        if (-not $previousStorageFailure -or $previousStorageFailure.classification -cne 'PREBOOT_STORAGE_FAILURE' -or -not $otherStartupFailure -or $otherStartupFailure.classification -cne 'PREBOOT_QEMU_STARTUP_FAILURE') { throw 'Early QEMU startup failures were not classified separately from QAC coordination.' }
        $runnerResult.checks.earlyQemuExitClassification = [ordered]@{ passed = $true; reproducesRecordedStorageFailure = $true; classification = $previousStorageFailure.classification; distinguishesOtherStartupFailure = $true; otherClassification = $otherStartupFailure.classification }
        $copySource = Join-Path $script:RunRoot 'selftest-source.raw'
        $copyTarget = Join-Path $script:RunRoot 'selftest-disposable.raw'
        [IO.File]::WriteAllBytes($copySource, [Text.Encoding]::UTF8.GetBytes('immutable-candidate-test-bytes'))
        $copyEvidence = New-DisposableQemuDiskCopy $copySource $copyTarget
        if (-not $copyEvidence.byteIdenticalAtCreation -or $copyEvidence.sourceModified -or (Get-Sha256 $copySource) -cne $copyEvidence.sourceSha256) { throw 'Disposable QEMU disk-copy isolation check failed.' }
        $runnerResult.checks.disposableDiskIsolation = [ordered]@{ passed = $true; sourceRemainsByteIdentical = $true; perRunCopyInitiallyMatches = $true; qemuCanWriteOnlyToCopy = $true; evidence = $copyEvidence }

        $reopenCommand = 'desktop.launch com.guidexos.stellarnavigator'
        $reopenKeys = @(Get-QemuTextKeyCommands $reopenCommand)
        if ($reopenKeys.Count -ne ($reopenCommand.Length + 1) -or $reopenKeys[0] -cne 'sendkey d' -or $reopenKeys -notcontains 'sendkey dot' -or $reopenKeys -notcontains 'sendkey spc' -or $reopenKeys[-1] -cne 'sendkey ret') {
            throw 'Supported shell command was not converted into bounded guest keyboard input.'
        }
        $unsupportedKeyInputRejected = $false
        try { [void](Get-QemuTextKeyCommands 'desktop.launch unsupported/character') }
        catch { $unsupportedKeyInputRejected = $true }
        if (-not $unsupportedKeyInputRejected) { throw 'Guest shell input accepted a character without an explicit QEMU key mapping.' }
        $runnerResult.checks.reopenInput = [ordered]@{ passed = $true; method = 'backtick opens shell; desktop.launch com.guidexos.stellarnavigator uses desktop::launch_app'; keyCommandCount = $reopenKeys.Count; spaceAndPeriodMapped = $true; unsupportedCharactersRejected = $true; serialStartIndexSupported = $true }

        $ownedQueued = [pscustomobject]@{ id = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'; project = 'StellarNavigator'; mode = 'acceptance'; owner = 'test-user (PID 456)'; state = 'Waiting'; processId = $null }
        $foreignQueued = [pscustomobject]@{ id = $ownedQueued.id; project = $ownedQueued.project; mode = $ownedQueued.mode; owner = 'other-user (PID 789)'; state = 'Waiting'; processId = $null }
        $registeredChild = [pscustomobject]@{ id = $ownedQueued.id; project = $ownedQueued.project; mode = $ownedQueued.mode; owner = $ownedQueued.owner; state = 'Running'; processId = 1234 }
        if ((Get-QacReservationCleanupAction $ownedQueued $ownedQueued.id 456) -cne 'CancelOwnPending' -or
            (Get-QacReservationCleanupAction $foreignQueued $ownedQueued.id 456) -cne 'DoNotTouch' -or
            (Get-QacReservationCleanupAction $registeredChild $ownedQueued.id 456) -cne 'DoNotTouch') {
            throw "QAC cleanup policy did not restrict cancellation to this run's own childless pending reservation."
        }
        $runnerResult.checks.qacQueueControl = [ordered]@{ passed = $true; documentedFifoQueue = $true; requestTimeoutBoundSeconds = $LaneAcquireTimeoutSeconds; ownChildlessPendingRequestCancelled = $true; foreignOrRegisteredReservationUntouched = $true }

        $capture1Path = Join-Path $script:RunRoot 'selftest-live.serial.log'
        $capture1 = [SN10R3SerialCapture]::new($capture1Path)
        $port1 = $capture1.Port
        $producer1 = Start-TestProducer -Port $port1 -Scenario Markers -ExitCode 23
        $runnerResult.checks.captureEndpointInitialized = $true
        [void](Wait-SerialPattern -Capture $capture1 -Pattern '\[TEST\] boot-ready' -Seconds 5 -Description 'controlled boot marker')
        [void](Wait-SerialPattern -Capture $capture1 -Pattern '\[TEST\] app-visible' -Seconds 5 -Description 'controlled application marker')
        [void](Wait-SerialPattern -Capture $capture1 -Pattern '\[TEST\] ephemeris-evaluated' -Seconds 5 -Description 'controlled computation marker')
        $secondInstanceStart = $capture1.Snapshot().IndexOf('[TEST] ephemeris-evaluated', [StringComparison]::Ordinal)
        if ($secondInstanceStart -lt 0) { throw 'Serial cursor fixture marker was not captured.' }
        $secondInstanceStart += '[TEST] ephemeris-evaluated'.Length
        [void](Wait-SerialPattern -Capture $capture1 -Pattern '\[TEST\] second-instance-ready' -Seconds 5 -Description 'controlled second instance marker' -StartIndex $secondInstanceStart)
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
        $producer2 = Start-TestProducer -Port $capture2.Port -Scenario PartialHold -ExitCode 0
        [void](Wait-SerialPattern -Capture $capture2 -Pattern '\[TEST\] partial-progress' -Seconds 5 -Description 'controlled partial marker')
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $missingMarkerTimedOut = $false
        try { [void](Wait-SerialPattern -Capture $capture2 -Pattern 'MARKER-THAT-CANNOT-ARRIVE' -Seconds 1 -Description 'absent representative marker') }
        catch { $missingMarkerTimedOut = $_.Exception.Message -like 'GUEST_TIMEOUT:*' }
        $boundedMs = $timer.ElapsedMilliseconds
        if (-not $producer2.WaitForExit(5000)) { $producer2.Kill(); throw 'Partial serial producer did not exit.' }
        [void]$capture2.WaitForCompletion(3000)
        if (-not $missingMarkerTimedOut -or $boundedMs -gt 2000 -or $capture2.Snapshot() -notmatch 'partial-progress') { throw 'Missing-marker timeout or partial output preservation failed.' }
        $runnerResult.checks.boundedMissingMarker = [ordered]@{ passed = $true; classification = 'GUEST_TIMEOUT'; elapsedMilliseconds = $boundedMs; bounded = $true; partialLogPreserved = $true; logPath = $capture2Path }
        $capture2.Dispose(); $capture2 = $null

        $capture3Path = Join-Path $script:RunRoot 'selftest-zero-output.serial.log'
        $capture3 = [SN10R3SerialCapture]::new($capture3Path)
        $producer3 = Start-TestProducer -Port $capture3.Port -Scenario Silent -ExitCode 0
        if (-not $capture3.WaitForCompletion(5000) -or -not $producer3.WaitForExit(5000)) { throw 'Silent controlled producer did not complete.' }
        if (-not $capture3.Connected -or $capture3.BytesReceived -ne 0 -or $capture3.ErrorMessage) { throw 'Connected zero-byte output was not distinguished from capture failure.' }
        $runnerResult.checks.noGuestOutput = [ordered]@{ passed = $true; classification = 'NO_GUEST_SERIAL_OUTPUT'; connected = $true; bytes = 0; logPath = $capture3Path }
        $capture3.Dispose(); $capture3 = $null

        $capture5Path = Join-Path $script:RunRoot 'selftest-stream-disconnect.serial.log'
        $capture5 = [SN10R3SerialCapture]::new($capture5Path)
        $producer5 = Start-TestProducer -Port $capture5.Port -Scenario Partial -ExitCode 0
        $streamDisconnectObserved = $false
        try { [void](Wait-SerialPattern -Capture $capture5 -Pattern 'MARKER-THAT-CANNOT-ARRIVE' -Seconds 3 -Description 'controlled stream-disconnect marker') }
        catch { $streamDisconnectObserved = $_.Exception.Message -like 'SERIAL_STREAM_DISCONNECT_FAILURE:*' }
        if (-not $producer5.WaitForExit(5000)) { $producer5.Kill(); throw 'Disconnecting serial producer did not exit.' }
        [void]$capture5.WaitForCompletion(3000)
        if (-not $streamDisconnectObserved -or $capture5.Snapshot() -notmatch 'partial-progress') { throw 'Serial stream disconnect was not distinguished from a marker timeout.' }
        $runnerResult.checks.streamDisconnect = [ordered]@{ passed = $true; classification = 'SERIAL_STREAM_DISCONNECT_FAILURE'; partialLogPreserved = $true; logPath = $capture5Path }
        $capture5.Dispose(); $capture5 = $null

        $capture6Path = Join-Path $script:RunRoot 'selftest-guest-failure.serial.log'
        $capture6 = [SN10R3SerialCapture]::new($capture6Path)
        $producer6 = Start-TestProducer -Port $capture6.Port -Scenario Fatal -ExitCode 23
        $guestFailureObserved = $false
        try { [void](Wait-SerialPattern -Capture $capture6 -Pattern 'MARKER-THAT-CANNOT-ARRIVE' -Seconds 3 -Description 'controlled fatal guest marker') }
        catch { $guestFailureObserved = $_.Exception.Message -like 'GUEST_FAILURE:*' -and $_.Exception.Message -like '*KERNEL-PANIC*' }
        if (-not $producer6.WaitForExit(5000)) { $producer6.Kill(); throw 'Fatal-marker serial producer did not exit.' }
        [void]$capture6.WaitForCompletion(3000)
        if (-not $guestFailureObserved) { throw 'Fatal guest marker was reported as a timeout or transport failure.' }
        $runnerResult.checks.guestFailureMarker = [ordered]@{ passed = $true; classification = 'GUEST_FAILURE'; fatalMarkerRecognized = '[KERNEL-PANIC]'; logPath = $capture6Path }
        $capture6.Dispose(); $capture6 = $null

        $capture4Path = Join-Path $script:RunRoot 'selftest-no-client.serial.log'
        $capture4 = [SN10R3SerialCapture]::new($capture4Path)
        $captureFailureObserved = $false
        try { [void](Wait-SerialPattern -Capture $capture4 -Pattern 'unavailable-without-a-client' -Seconds 1 -Description 'no-client capture failure') }
        catch { $captureFailureObserved = $_.Exception.Message -like 'SERIAL_CAPTURE_FAILURE:*' }
        if (-not $captureFailureObserved -or $capture4.Connected -or $capture4.BytesReceived -ne 0) { throw 'No-client capture failure was not distinguished from connected zero-byte output.' }
        $capture4.Dispose(); $capture4 = $null
        $runnerResult.checks.captureFailureClassification = [ordered]@{ passed = $true; classification = 'SERIAL_CAPTURE_FAILURE'; noClientDetected = $captureFailureObserved; rule = 'No QEMU TCP client is a capture failure; a connected client with zero bytes is no guest output.' }
        $runnerResult.checks.resourcesReleased = [ordered]@{ passed = $true; listenerStopped = $true; ephemeralPortRebindSucceeded = $true }
        $runnerResult.logs = [ordered]@{ live = $capture1Path; missingMarker = $capture2Path; noOutput = $capture3Path; streamDisconnect = $capture5Path; guestFailure = $capture6Path }
        $runnerResult.finishedAtLocal = (Get-Date).ToString('o')
        $runnerResult.resultPath = Join-Path $script:RunRoot 'runner-selftest.json'
        $runnerResult | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $runnerResult.resultPath -Encoding utf8
        Write-Host "[SN10R6] offline runner self-test PASS: $($runnerResult.resultPath)"
        return 0
    } catch {
        if ($capture1) { $capture1.Dispose() }
        if ($capture2) { $capture2.Dispose() }
        if ($capture3) { $capture3.Dispose() }
        if ($capture4) { $capture4.Dispose() }
        if ($capture5) { $capture5.Dispose() }
        if ($capture6) { $capture6.Dispose() }
        $runnerResult.classification = 'FAIL'
        $runnerResult.error = $_.Exception.Message
        $runnerResult.finishedAtLocal = (Get-Date).ToString('o')
        $runnerResult.resultPath = Join-Path $script:RunRoot 'runner-selftest.json'
        $runnerResult | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $runnerResult.resultPath -Encoding utf8
        Write-Host "[SN10R6] offline runner self-test FAIL: $($runnerResult.error)"
        Write-Host "[SN10R6] evidence: $($runnerResult.resultPath)"
        return 1
    }
}

New-RunDirectory
if ($SelfTest) { exit (Invoke-SelfTest) }
if ($VerifyCandidateOnly) {
    try {
        [void](Assert-CandidateIdentity)
        Write-Host "[SN10R6] candidate, staged ESP, and raw disk verification PASS: $(Join-Path $script:RunRoot 'candidate-verification.json')"
        exit 0
    } catch {
        [ordered]@{ passed = $false; classification = 'FAIL'; error = $_.Exception.Message; candidateEsp = $CandidateEspPath; candidateDisk = $CandidateDiskPath; capturedAtLocal = (Get-Date).ToString('o') } |
            ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $script:RunRoot 'candidate-verification.json') -Encoding utf8
        Write-Host "[SN10R6] candidate verification FAIL: $($_.Exception.Message)"
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
    if ($overall.branch -cne 'STELLAR_NAVIGATOR' -or $overall.phase -cne 'SN10') {
        throw 'Repository branch or phase does not match the authorized SN10 acceptance lane.'
    }
    if (-not (Test-Path -LiteralPath $Qemu) -or -not (Test-Path -LiteralPath $QacCli)) { throw 'Installed QEMU or QAC CLI is unavailable.' }
    $candidateCheck = Assert-CandidateIdentity
    $overall.checks.artifactIntegrity = $candidateCheck

    $qacStatus = Get-QacSnapshot 'qac-status-before.log' @('status')
    $qacList = Get-QacSnapshot 'qac-list-before.log' @('list')
    $qacQueue = Get-QacSnapshot 'qac-queue-before.log' @('queue')
    if ($qacStatus -notmatch 'Coordinator: Online') {
        $overall.blocker = 'QAC_RESERVATION_FAILURE'
        $overall.checks.qacPreflight = [ordered]@{ coordinatorOnline = $false; reservationNotRequested = $true; statusPath = (Join-Path $script:RunRoot 'qac-status-before.log'); listPath = (Join-Path $script:RunRoot 'qac-list-before.log'); queuePath = (Join-Path $script:RunRoot 'qac-queue-before.log') }
        throw 'QAC coordinator is not online.'
    }
    $processCount = if ($qacStatus -match 'QEMU processes: (\d+)') { [int]$Matches[1] } else { -1 }
    $sessionCount = if ($qacStatus -match 'Managed sessions: (\d+)') { [int]$Matches[1] } else { -1 }
    $reservationCount = if ($qacList -match 'Reservations: (\d+)') { [int]$Matches[1] } else { -1 }
    $overall.checks.qacPreflight = [ordered]@{
        coordinatorOnline = $true
        processCount = $processCount
        managedSessionCount = $sessionCount
        reservationCount = $reservationCount
        queueSnapshot = $qacQueue
        acquisition = 'one QAC-managed qac run request using documented FIFO acceptance admission'
        acquisitionWaitBoundSeconds = $LaneAcquireTimeoutSeconds
        statusPath = (Join-Path $script:RunRoot 'qac-status-before.log')
        listPath = (Join-Path $script:RunRoot 'qac-list-before.log')
        queuePath = (Join-Path $script:RunRoot 'qac-queue-before.log')
    }

    $runtimeDiskPath = Join-Path $script:RunRoot 'qac-disposable-disk.raw'
    $runtimeDiskCopy = New-DisposableQemuDiskCopy $CandidateDiskPath $runtimeDiskPath
    if ($runtimeDiskCopy.initialSha256 -cne $candidateCheck.candidateDisk.sha256) { throw 'Per-run writable QEMU disk copy differs from the verified immutable candidate image.' }
    $overall.checks.runtimeDiskCopy = [ordered]@{ passed = $true; path = $runtimeDiskCopy.path; initialSha256 = $runtimeDiskCopy.initialSha256; byteLength = $runtimeDiskCopy.lengthBytes; sourceCandidate = $runtimeDiskCopy.sourcePath; sourceCandidateSha256 = $runtimeDiskCopy.sourceSha256; attachedReadWriteForIdeCompatibility = $true; sourceCandidateModified = $false }

    $monitorProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $monitorProbe.Start()
    $script:MonitorPort = ([Net.IPEndPoint]$monitorProbe.LocalEndpoint).Port
    $monitorProbe.Stop()
    $serialLog = Join-Path $script:RunRoot 'guest-serial.log'
    $capture = [SN10R3SerialCapture]::new($serialLog)
    $qemuArguments = @(New-QemuArguments $runtimeDiskPath $capture.Port $script:MonitorPort)
    $qacArguments = @('run', '--project', 'StellarNavigator', '--mode', 'acceptance', '--', $Qemu) + $qemuArguments
    [ordered]@{
        qacExecutable = $QacCli
        qacArgumentVector = $qacArguments
        qemuExecutable = $Qemu
        qemuArgumentVector = $qemuArguments
        serialEndpoint = [ordered]@{ host = $capture.Host; port = $capture.Port; backend = 'QEMU socket chardev TCP client to runner-owned loopback listener' }
        monitorEndpoint = [ordered]@{ host = '127.0.0.1'; port = $script:MonitorPort; backend = 'HMP TCP loopback, ephemeral per run' }
        candidateEspPath = [IO.Path]::GetFullPath($CandidateEspPath)
        candidateDiskPath = [IO.Path]::GetFullPath($CandidateDiskPath)
        qemuDisposableDiskPath = [IO.Path]::GetFullPath($runtimeDiskPath)
        candidateDiskSha256 = $candidateCheck.candidateDisk.sha256
        qemuDisposableDiskInitialSha256 = $runtimeDiskCopy.initialSha256
        diskLayout = [ordered]@{ format = 'raw'; partitionTable = 'GPT'; partitionStartLba = $candidateCheck.candidateDisk.partitionStartLba; partitionSectors = $candidateCheck.candidateDisk.partitionSectors; sectorSizeBytes = 512; filesystem = 'FAT32'; interface = 'IDE index 0'; qemuAccess = 'writable isolated per-run clone' }
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $script:RunRoot 'qemu-invocation.json') -Encoding utf8
    $overall.checks.launchConstruction = [ordered]@{ passed = $true; invocationPath = (Join-Path $script:RunRoot 'qemu-invocation.json'); effectiveArgumentVectorRecorded = $true; structuredProcessArguments = $true }
    $overall.checks.serialCapture = [ordered]@{ endpointInitializedBeforeQemu = $true; host = $capture.Host; port = $capture.Port; logPath = $serialLog; qemuSerialStreamEndpoint = 'QEMU socket chardev TCP client to runner-owned loopback listener' }

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
        if ($script:ManagedQac.HasExited) {
            [void]$script:ManagedQac.WaitForExit(5000)
            $earlyOutput = $script:ManagedQac.StdoutText + [Environment]::NewLine + $script:ManagedQac.StderrText
            $earlyExit = Get-QemuEarlyExitDetails $earlyOutput
            if ($earlyExit) {
                $script:ReservationId = $earlyExit.reservationId
                $overall.blocker = $earlyExit.classification
                $overall.checks.qacReservation = [ordered]@{ acquired = $true; reservationId = $earlyExit.reservationId; project = 'StellarNavigator'; mode = 'acceptance'; qemuPid = $earlyExit.qemuPid; qemuExitedBeforeRunnerAttach = $true; qemuExitCode = $earlyExit.exitCode }
                $overall.checks.qemuStartup = [ordered]@{ passed = $false; stage = 'QEMU process startup before guest serial observation'; classification = $earlyExit.classification; rootCause = $earlyExit.rootCause; qacOutputPath = (Join-Path $script:RunRoot 'qac-run.stderr.log'); guestBootObserved = $false }
                throw "$($earlyExit.classification): $($earlyExit.rootCause)"
            }
        }
        if ($script:ReservationId) {
            $queuedRaw = Get-QacSnapshot 'qac-reservation-inspect.queued.json' @('inspect', $script:ReservationId)
            $queued = $queuedRaw | ConvertFrom-Json
            $cleanupAction = Get-QacReservationCleanupAction $queued $script:ReservationId $script:ManagedQac.Process.Id
            if ($cleanupAction -ceq 'CancelOwnPending') {
                $cancelText = Get-QacSnapshot 'qac-cancel-own-unlaunched.log' @('cancel', $script:ReservationId)
                $cancelledRaw = Get-QacSnapshot 'qac-reservation-inspect.after-cancel.json' @('inspect', $script:ReservationId)
                $cancelled = $cancelledRaw | ConvertFrom-Json
                $cancelVerified = [string]$cancelled.id -ceq $script:ReservationId -and [string]$cancelled.project -ceq 'StellarNavigator' -and [string]$cancelled.mode -ceq 'acceptance' -and [string]$cancelled.owner -match "\(PID $($script:ManagedQac.Process.Id)\)" -and [string]$cancelled.state -ceq 'Cancelled' -and $null -eq $cancelled.processId
                if (-not $cancelVerified) { throw "QAC did not confirm cancellation of this run's exact childless pending reservation." }
                $overall.checks.qacReservation = [ordered]@{ acquired = $false; reservationId = $script:ReservationId; observedState = $queued.state; ownedByRunnerCliPid = $script:ManagedQac.Process.Id; pendingReservationCancelledThroughQac = $true; cancellationVerified = $true; cancelEvidence = $cancelText; terminalReservation = $cancelled }
                $overall.blocker = 'EXTERNAL_QEMU_COORDINATION_BLOCKER'
            } else {
                $overall.blocker = 'QAC_RESERVATION_FAILURE'
                $overall.checks.qacReservation = [ordered]@{ acquired = $false; reservationId = $script:ReservationId; cleanupAction = $cleanupAction; observedReservation = $queued; foreignReservationUntouched = $true }
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
    try {
        [void](Get-OwnedQacReservation $script:ReservationId $script:Owned.Process.Id)
    } catch {
        if ($script:Owned.Process.HasExited -and $script:ManagedQac) {
            [void]$script:ManagedQac.WaitForExit(5000)
            $earlyOutput = $script:ManagedQac.StdoutText + [Environment]::NewLine + $script:ManagedQac.StderrText
            $earlyExit = Get-QemuEarlyExitDetails $earlyOutput
            if ($earlyExit) {
                $overall.blocker = $earlyExit.classification
                $overall.checks.qacReservation = [ordered]@{ acquired = $true; reservationId = $earlyExit.reservationId; project = 'StellarNavigator'; mode = 'acceptance'; qemuPid = $earlyExit.qemuPid; qemuExitedBeforeRunnerAttach = $true; qemuExitCode = $earlyExit.exitCode }
                $overall.checks.qemuStartup = [ordered]@{ passed = $false; stage = 'QEMU process startup before guest serial observation'; classification = $earlyExit.classification; rootCause = $earlyExit.rootCause; qacOutputPath = (Join-Path $script:RunRoot 'qac-run.stderr.log'); guestBootObserved = $false }
                throw "$($earlyExit.classification): $($earlyExit.rootCause)"
            }
        }
        throw
    }

    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $bootText = Wait-SerialPattern -Capture $capture -Pattern '\[KERNEL\] VFS layer initialized' -Seconds $remaining -Description 'AMD64 kernel VFS boot marker'
    $overall.checks.kernelBoot = [ordered]@{ passed = $true; marker = '[KERNEL] VFS layer initialized'; desktopRequiredSeparateMarker = $false }
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $discoveryText = Wait-SerialPattern -Capture $capture -Pattern 'App Model package discovered.*Stellar Navigator|displayName=Stellar Navigator' -Seconds $remaining -Description 'Stellar Navigator AppModel discovery'
    $overall.checks.appModelDiscovery = [ordered]@{ passed = $true; marker = ($discoveryText -split "`r?`n" | Where-Object { $_ -match 'App Model package discovered.*Stellar Navigator|displayName=Stellar Navigator' } | Select-Object -Last 1) }
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $appText = Wait-SerialPattern -Capture $capture -Pattern 'STELLAR_NAVIGATOR_UI status=initial-content-drawn' -Seconds $remaining -Description 'native application initial content draw'
    if ($appText -notmatch '\[SN10-GUEST\] appmodel-launch begin id=com\.guidexos\.stellarnavigator' -or $appText -notmatch 'STELLAR_NAVIGATOR_UI status=window-created') { throw 'Guest did not report AppModel-mediated native window creation.' }
    $firstEphemeris = Get-VerifiedGuestEphemeris $appText
    $overall.checks.nativeApplication = [ordered]@{ appModelLaunchMarker = $true; nativeWindowCreated = $true; initialContentDrawn = $true; guestEphemerisEvaluatorObserved = $true; ephemerisLine = $firstEphemeris.line; coordinateFrame = $firstEphemeris.frame; units = $firstEphemeris.units; epochEtTdbSeconds = $firstEphemeris.epochEtTdbSeconds; numericalComparison = $firstEphemeris.comparison; passed = $true }
    $overall.checks.screenshotWhileOpen = Save-Screenshot 'stellar-navigator-open.ppm'

    $firstCloseStartIndex = $capture.Snapshot().Length
    [void](Send-OwnedMonitorCommand 'sendkey esc')
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    [void](Wait-SerialPattern -Capture $capture -Pattern 'STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown' -Seconds $remaining -Description 'first normal Stellar Navigator shutdown' -StartIndex $firstCloseStartIndex)
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    [void](Wait-SerialPattern -Capture $capture -Pattern '\[SN10-GUEST\] appmodel-launch result=success' -Seconds $remaining -Description 'AppModel launch return after first close' -StartIndex $firstCloseStartIndex)
    $firstCloseText = $capture.Snapshot().Substring($firstCloseStartIndex)
    $overall.checks.normalClose = [ordered]@{ passed = $true; lifecycleMarker = ($firstCloseText -match 'STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown'); loaderCleanupMarker = ($firstCloseText -match 'lifecycle PASS window/resource cleanup complete'); appModelReturnMarker = '[SN10-GUEST] appmodel-launch result=success'; applicationInstanceExited = ($firstCloseText -match 'lifecycle PASS window/resource cleanup complete') }
    if (-not $overall.checks.normalClose.loaderCleanupMarker) { throw 'Native loader did not report complete window/resource cleanup.' }
    $overall.checks.screenshotAfterClose = Save-Screenshot 'desktop-after-close.ppm'

    # Use the existing desktop backtick shortcut and guest shell command, which
    # dispatches through desktop::launch_app and the production AppModel path.
    $reopenStartIndex = $capture.Snapshot().Length
    [void](Send-OwnedMonitorCommand 'sendkey grave_accent')
    Start-Sleep -Milliseconds 500
    $overall.checks.shellBeforeReopen = Save-Screenshot 'shell-before-reopen.ppm'
    $reopenInput = Send-OwnedGuestText 'desktop.launch com.guidexos.stellarnavigator'
    $overall.checks.reopenInput = $reopenInput
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $secondAppText = Wait-SerialPattern -Capture $capture -Pattern 'STELLAR_NAVIGATOR_UI status=initial-content-drawn' -Seconds $remaining -Description 'second native application initial content draw' -StartIndex $reopenStartIndex
    if ($secondAppText -notmatch 'STELLAR_NAVIGATOR_UI status=window-created') { throw 'Second launch did not create a new native application window through the guest desktop dispatcher.' }
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    $secondAppText = Wait-SerialPattern -Capture $capture -Pattern 'STELLAR_NAVIGATOR_EPHEMERIS status=available body=earth target_naif=399 et_tdb_s=0\.000000 frame=J2000_SSB units=km position_km=' -Seconds $remaining -Description 'second native application ephemeris evaluation' -StartIndex $reopenStartIndex
    $secondEphemeris = Get-VerifiedGuestEphemeris $secondAppText
    $overall.checks.reopen = [ordered]@{
        attempted = $true
        passed = $true
        method = 'guest backtick shortcut opens shell; desktop.launch com.guidexos.stellarnavigator invokes desktop::launch_app and the production AppModel path'
        secondWindowCreated = $secondAppText -match 'STELLAR_NAVIGATOR_UI status=window-created'
        secondInitialContentDrawn = $secondAppText -match 'STELLAR_NAVIGATOR_UI status=initial-content-drawn'
        secondEphemerisEvaluatorObserved = $true
        ephemerisLine = $secondEphemeris.line
        numericalComparison = $secondEphemeris.comparison
        shellScreenshot = $overall.checks.shellBeforeReopen.path
        passedCoordinates = $true
    }
    $overall.checks.screenshotSecondInstanceOpen = Save-Screenshot 'stellar-navigator-second-instance-open.ppm'

    $secondCloseStartIndex = $capture.Snapshot().Length
    [void](Send-OwnedMonitorCommand 'sendkey esc')
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    [void](Wait-SerialPattern -Capture $capture -Pattern 'STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown' -Seconds $remaining -Description 'second normal Stellar Navigator shutdown' -StartIndex $secondCloseStartIndex)
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalSeconds))
    [void](Wait-SerialPattern -Capture $capture -Pattern 'lifecycle PASS window/resource cleanup complete' -Seconds $remaining -Description 'second native loader cleanup and instance exit' -StartIndex $secondCloseStartIndex)
    $secondCloseText = $capture.Snapshot().Substring($secondCloseStartIndex)
    $overall.checks.secondClose = [ordered]@{ passed = $true; lifecycleMarker = ($secondCloseText -match 'STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown'); loaderCleanupMarker = ($secondCloseText -match 'lifecycle PASS window/resource cleanup complete'); applicationInstanceExited = ($secondCloseText -match 'lifecycle PASS window/resource cleanup complete') }
    $overall.checks.shellAfterSecondClose = Save-Screenshot 'shell-after-second-close.ppm'
    [void](Send-OwnedMonitorCommand 'sendkey esc')
    Start-Sleep -Milliseconds 500
    $overall.checks.desktopAfterSecondClose = Save-Screenshot 'desktop-after-second-close.ppm'
    if (-not $overall.checks.desktopAfterSecondClose.captured) { throw 'Guest desktop screenshot after second close was not captured.' }
    $overall.checks.guestStability = Confirm-OwnedGuestStable 3
    $overall.checks.desktopUsable = [ordered]@{ appModelDiscoveryVerified = $true; visualDesktopAfterFirstCloseCaptured = $overall.checks.screenshotAfterClose.captured; visualDesktopAfterSecondCloseCaptured = $overall.checks.desktopAfterSecondClose.captured; guestRemainedAliveAfterSecondClose = $true }
    $overall.checks.repeatEphemerisParity = [ordered]@{ first = $firstEphemeris.comparison; second = $secondEphemeris.comparison; bothGuestEvaluationsPassed = $true; referenceToleranceKm = 0.0005 }
    $overall.classification = 'PASS'
    $overall.blocker = $null
} catch {
    if ($overall.blocker -eq 'EXTERNAL_QEMU_COORDINATION_BLOCKER') { $overall.classification = 'BLOCKED' }
    elseif ($overall.blocker -eq 'PREBOOT_STORAGE_FAILURE' -or $_.Exception.Message -like 'PREBOOT_STORAGE_FAILURE:*') { $overall.classification = 'FAIL'; $overall.blocker = 'PREBOOT_STORAGE_FAILURE' }
    elseif ($overall.blocker -eq 'PREBOOT_QEMU_STARTUP_FAILURE' -or $_.Exception.Message -like 'PREBOOT_QEMU_STARTUP_FAILURE:*') { $overall.classification = 'FAIL'; $overall.blocker = 'PREBOOT_QEMU_STARTUP_FAILURE' }
    elseif ($_.Exception.Message -like 'SERIAL_CAPTURE_FAILURE:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'SERIAL_CAPTURE_FAILURE' }
    elseif ($_.Exception.Message -like 'NO_GUEST_SERIAL_OUTPUT:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'NO_GUEST_SERIAL_OUTPUT' }
    elseif ($_.Exception.Message -like 'SERIAL_STREAM_DISCONNECT_FAILURE:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'SERIAL_STREAM_DISCONNECT_FAILURE' }
    elseif ($_.Exception.Message -like 'MISSING_GUEST_MARKER:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'MISSING_GUEST_MARKER' }
    elseif ($_.Exception.Message -like 'GUEST_TIMEOUT:*') { $overall.classification = 'BLOCKED'; $overall.blocker = 'GUEST_TIMEOUT' }
    elseif ($_.Exception.Message -like 'GUEST_FAILURE:*') { $overall.classification = 'FAIL'; $overall.blocker = 'GUEST_FAILURE' }
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
                    $ownPostRaw = Get-QacSnapshot 'qac-reservation-inspect.after-run.json' @('inspect', $script:ReservationId)
                    $ownPost = $ownPostRaw | ConvertFrom-Json
                    $overall.qacOwnReservationTerminalState = $ownPost.state
                    $overall.reservationReleased = [string]$ownPost.state -ceq 'Released'
                    $overall.reservationCancelled = [string]$ownPost.state -ceq 'Cancelled'
                    $overall.reservationTerminal = [string]$ownPost.state -in @('Released', 'Cancelled', 'Expired', 'Failed')
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
    Write-Host "[SN10R6] $($overall.classification): $($overall.resultPath)"
    if ($overall.blocker) { Write-Host "[SN10R6] blocker=$($overall.blocker)" }
}

if ($overall.classification -eq 'PASS') { exit 0 }
if ($overall.classification -eq 'BLOCKED') { exit 2 }
exit 1
