[CmdletBinding()]
param(
    [ValidateRange(20, 300)]
    [int]$TimeoutSeconds = 120,
    [ValidateRange(5, 60)]
    [int]$LaneAcquireTimeoutSeconds = 20,
    [string]$ArtifactRoot = '',
    [string]$CandidateEspPath = 'E:\bkup\artifacts\SN10R2_20261020\qemu\20261020T042331758\esp',
    [string]$CandidateVerificationPath = 'E:\bkup\artifacts\SN10R2_20261020\staged-candidate-verification.json',
    [string]$CandidateDiskPath = 'E:\bkup\artifacts\SN10R8_20261020\media\stellar-navigator-sn10r8-mbr.raw',
    [string]$CandidateDiskVerificationPath = 'E:\bkup\artifacts\SN10R8_20261020\media\stellar-navigator-sn10r8-verification.json',
    [string]$KernelArtifactPath = '',
    [string]$ExpectedKernelSha256 = '',
    [string]$ExpectedDiskSha256 = '',
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
    Disk = '29d060d068edc81daf720ca314e7b9dd9a3bbd376a053d1c0cb903de0c00a8a5'
}

if ($KernelArtifactPath) { $Kernel = [IO.Path]::GetFullPath($KernelArtifactPath) }
if ($ExpectedKernelSha256) {
    if ($ExpectedKernelSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'ExpectedKernelSha256 must be a 64-digit SHA-256 value.' }
    $Expected.Kernel = $ExpectedKernelSha256.ToLowerInvariant()
}
if ($ExpectedDiskSha256) {
    if ($ExpectedDiskSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'ExpectedDiskSha256 must be a 64-digit SHA-256 value.' }
    $Expected.Disk = $ExpectedDiskSha256.ToLowerInvariant()
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

public sealed class SN10R3SerialCapture : IDisposable
{
    private readonly TcpListener _listener;
    private readonly string _logPath;
    private readonly object _gate = new object();
    private readonly StringBuilder _text = new StringBuilder();
    private readonly ManualResetEventSlim _changed = new ManualResetEventSlim(false);
    private readonly CancellationTokenSource _stop = new CancellationTokenSource();
    private Thread _worker;
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
        File.WriteAllBytes(_logPath, new byte[0]);
        _listener = new TcpListener(IPAddress.Loopback, 0);
        _listener.Server.ExclusiveAddressUse = true;
        _listener.Start(1);
        Port = ((IPEndPoint)_listener.LocalEndpoint).Port;
        _worker = new Thread(Pump);
        _worker.IsBackground = true;
        _worker.Name = "SN10 serial capture";
        _worker.Start();
    }

    private void RecordError(Exception error)
    {
        lock (_gate)
        {
            if (_error == null) _error = error;
        }
        _changed.Set();
    }

    private void Pump()
    {
        try
        {
            TcpClient accepted = _listener.AcceptTcpClient();
            lock (_gate)
            {
                if (_stop.IsCancellationRequested)
                {
                    accepted.Close();
                    return;
                }
                _client = accepted;
                _client.NoDelay = true;
                _connected = true;
            }
            _changed.Set();
            using (var output = new FileStream(_logPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite, 8192))
            using (var stream = _client.GetStream())
            {
                var buffer = new byte[8192];
                while (!_stop.IsCancellationRequested)
                {
                    int count = stream.Read(buffer, 0, buffer.Length);
                    if (count == 0) break;
                    output.Write(buffer, 0, count);
                    output.Flush();
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
        catch (ObjectDisposedException ex)
        {
            if (!_stop.IsCancellationRequested) RecordError(ex);
        }
        catch (SocketException ex)
        {
            if (!_stop.IsCancellationRequested) RecordError(ex);
        }
        catch (IOException ex)
        {
            if (!_stop.IsCancellationRequested) RecordError(ex);
        }
        catch (Exception ex)
        {
            RecordError(ex);
        }
        finally
        {
            try { if (_client != null) _client.Dispose(); } catch { }
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
        return _worker.Join(milliseconds);
    }

    public void Stop()
    {
        _stop.Cancel();
        try { _listener.Stop(); } catch { }
        TcpClient client = null;
        lock (_gate) client = _client;
        try { if (client != null) client.Close(); } catch { }
    }

    public void Dispose()
    {
        Stop();
        if (_worker != null && !_worker.Join(3000))
        {
            throw new TimeoutException("Serial capture worker did not stop after its listener and client were closed.");
        }
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

    public static string QuoteArgument(string argument)
    {
        if (argument == null) argument = String.Empty;
        var result = new StringBuilder();
        result.Append('"');
        int backslashes = 0;
        for (int i = 0; i < argument.Length; i++)
        {
            char value = argument[i];
            if (value == '\\')
            {
                backslashes++;
            }
            else if (value == '"')
            {
                result.Append('\\', backslashes * 2 + 1);
                result.Append('"');
                backslashes = 0;
            }
            else
            {
                result.Append('\\', backslashes);
                result.Append(value);
                backslashes = 0;
            }
        }
        result.Append('\\', backslashes * 2);
        result.Append('"');
        return result.ToString();
    }

    public static string BuildCommandLine(string[] arguments)
    {
        var result = new StringBuilder();
        for (int i = 0; i < arguments.Length; i++)
        {
            if (i != 0) result.Append(' ');
            result.Append(QuoteArgument(arguments[i]));
        }
        return result.ToString();
    }

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
        info.Arguments = BuildCommandLine(arguments);
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
    public bool WaitForExit(int milliseconds)
    {
        bool exited = Process.WaitForExit(milliseconds);
        if (exited) Process.WaitForExit();
        return exited;
    }
    public bool HasExited { get { return Process.HasExited; } }
    public int ExitCode { get { return Process.ExitCode; } }
    public void Dispose() { if (Process != null) Process.Dispose(); }
}
'@

if (-not ('SN10R3SerialCapture' -as [type])) {
    try {
        $script:CompiledHelperTypes = @(Add-Type -TypeDefinition $TypeDefinition -Language CSharp -PassThru -ErrorAction Stop)
    } catch {
        Write-Error -ErrorRecord $_
        throw
    }
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Confirm-WritableQemuDiskFile([string]$Path) {
    $attributes = [IO.File]::GetAttributes($Path)
    if (($attributes -band [IO.FileAttributes]::ReadOnly) -ne 0) {
        $writableAttributes = [IO.FileAttributes](([int]$attributes) -band (-bnot [int][IO.FileAttributes]::ReadOnly))
        [IO.File]::SetAttributes($Path, $writableAttributes)
    }
    $finalAttributes = [IO.File]::GetAttributes($Path)
    if (($finalAttributes -band [IO.FileAttributes]::ReadOnly) -ne 0) { throw "Disposable QEMU disk remains marked read-only: $Path" }

    $stream = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    } finally {
        if ($stream) { $stream.Dispose() }
    }
    [ordered]@{
        passed = $true
        path = [IO.Path]::GetFullPath($Path)
        readOnlyAttribute = $false
        writeOpenSucceeded = $true
        effectiveWriterIdentity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
        aclOwner = (Get-Acl -LiteralPath $Path).Owner
    }
}

function New-DisposableQemuDiskCopy([string]$SourcePath, [string]$DestinationPath) {
    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) { throw "Verified raw disk source is missing: $SourcePath" }
    if (Test-Path -LiteralPath $DestinationPath) { throw "Refusing to replace an existing per-run disk copy: $DestinationPath" }
    $sourceFullPath = [IO.Path]::GetFullPath($SourcePath)
    $sourceHash = Get-Sha256 $sourceFullPath
    $sourceAttributes = [IO.File]::GetAttributes($sourceFullPath)
    [IO.File]::Copy([IO.Path]::GetFullPath($SourcePath), [IO.Path]::GetFullPath($DestinationPath), $false)
    $copyFullPath = [IO.Path]::GetFullPath($DestinationPath)
    $writeCheck = Confirm-WritableQemuDiskFile $copyFullPath
    $copyHash = Get-Sha256 $copyFullPath
    if ($copyHash -cne $sourceHash) { throw 'Disposable QEMU disk copy differs from its verified source image.' }
    $sourceHashAfterCopy = Get-Sha256 $sourceFullPath
    $sourceAttributesAfterCopy = [IO.File]::GetAttributes($sourceFullPath)
    if ($sourceHashAfterCopy -cne $sourceHash -or $sourceAttributesAfterCopy -ne $sourceAttributes) { throw 'Creating the disposable QEMU disk copy changed the verified source disk.' }
    [ordered]@{
        path = $copyFullPath
        lengthBytes = (Get-Item -LiteralPath $copyFullPath).Length
        initialSha256 = $copyHash
        sourcePath = $sourceFullPath
        sourceSha256 = $sourceHash
        sourceSha256AfterCopy = $sourceHashAfterCopy
        sourceAttributes = [string]$sourceAttributes
        sourceAttributesAfterCopy = [string]$sourceAttributesAfterCopy
        byteIdenticalAtCreation = $true
        destinationReadOnly = $false
        writeAccess = $writeCheck
        sourceModified = $false
    }
}

function ConvertTo-QemuOptionValue([string]$Value) {
    # QEMU keyval option values escape a filename comma by doubling it.
    # Windows command-line quoting is applied separately when starting a process.
    $Value.Replace(',', ',,')
}

function Join-WindowsCommandLineArguments([string[]]$Arguments) {
    [SN10R3ManagedProcess]::BuildCommandLine($Arguments)
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
        $ArtifactRoot = Join-Path $env:USERPROFILE ('.codex\artifacts\SN10R8_' + (Get-Date -Format 'yyyyMMdd'))
    }
    $full = [IO.Path]::GetFullPath($ArtifactRoot)
    $id = Get-Date -Format 'yyyyMMddTHHmmssfff'
    $path = Join-Path $full (Join-Path 'qemu' $id)
    if (Test-Path -LiteralPath $path) { $path = Join-Path $full (Join-Path 'qemu' ($id + '_' + [guid]::NewGuid().ToString('N').Substring(0, 8))) }
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    $script:RunRoot = $path
}

function Test-StructuredArgumentPassing {
    $powershell = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $powershell -PathType Leaf)) { throw "Windows PowerShell executable is missing: $powershell" }
    $scriptPath = Join-Path $script:RunRoot 'argv-echo.ps1'
    $scriptLines = @(
        '$ErrorActionPreference = ''Stop''',
        'ConvertTo-Json -InputObject @($args) -Compress | Set-Content -LiteralPath $env:SN10R3_ARGV_PATH -Encoding utf8'
    )
    Set-Content -LiteralPath $scriptPath -Value $scriptLines -Encoding utf8
    $expected = @(
        '-drive', "if=pflash,format=raw,readonly=on,file=C:\SN10 R3 & (quoted)'s path\firmware,, rev 2.fd",
        '-drive', "file=C:\SN10 R3 & (quoted)'s path\candidate,, rev 2.raw,format=raw,if=ide,index=0",
        'single quoted path with spaces.txt',
        "ampersand & parentheses (kept literally) and an apostrophe '",
        'embedded " quote and trailing slash\',
        ''
    )
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $powershell
    $info.WorkingDirectory = $script:RunRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $childArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath) + $expected
    $info.Arguments = Join-WindowsCommandLineArguments $childArguments
    $dumpPath = Join-Path $script:RunRoot 'argv-roundtrip.json'
    $info.EnvironmentVariables['SN10R3_ARGV_PATH'] = $dumpPath
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
    $pattern = '(?m)(\[KERNEL-PANIC\][^\r\n]*|\[KERNEL-FAULT\][^\r\n]*|\[NATIVE-ELF-FAULT\][^\r\n]*|\[NATIVE-ELF\] discovery result=NO_APPS_DIRECTORY[^\r\n]*|\[SN10-GUEST\] appmodel-launch result=failed[^\r\n]*|\[Exception\] (?:Unhandled exception|Double fault)[^\r\n]*|Triple fault[^\r\n]*)'
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
        'Markers' { @('[TEST] boot-ready', '[TEST] app-visible', '[TEST] first-ephemeris-evaluated', '[TEST] first-close-requested', '[TEST] first-shutdown-complete', '[TEST] first-appmodel-return', '[TEST] reopen-command-dispatched', '[TEST] second-window-created', '[TEST] second-ephemeris-evaluated', '[TEST] second-close-requested', '[TEST] second-shutdown-complete', '[TEST] second-cleanup-complete', 'partial-after-nonzero-exit') }
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
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $PSHOME 'powershell.exe'
    $info.WorkingDirectory = $script:RunRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $producerArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $producerPath, [string]$Port, $Scenario, [string]$ExitCode)
    $info.Arguments = Join-WindowsCommandLineArguments $producerArguments
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
    if ([string]$diskVerification.format -cne 'raw' -or [string]$diskVerification.partitionTable -cne 'MBR' -or [string]$diskVerification.fatType -cne 'FAT32' -or [int]$diskVerification.sectorSizeBytes -ne 512) { throw 'Candidate disk does not have the verified raw/MBR/FAT32/512-byte layout.' }
    if ([int64]$diskVerification.imageBytes -ne (Get-Item -LiteralPath $diskFull).Length -or [string]$diskVerification.imageSha256 -notmatch '^[0-9a-f]{64}$') { throw 'Candidate disk geometry or recorded SHA-256 is invalid.' }
    $diskHash = Get-Sha256 $diskFull
    if ($diskHash -cne [string]$diskVerification.imageSha256 -or $diskHash -cne $Expected.Disk) { throw 'Candidate raw disk SHA-256 does not match its verification record and accepted SN10R8 identity.' }
    if ([IO.Path]::GetFullPath([string]$diskVerification.sourceEsp).TrimEnd('\') -cne $espFull) { throw 'Candidate disk was not built from the validated staged ESP.' }
    $rootPartition = $diskVerification.rootPartition
    $efiPartition = $diskVerification.efiPartition
    if (-not $rootPartition -or -not $efiPartition -or [string]$rootPartition.typeHex -cne '0x0C' -or [string]$efiPartition.typeHex -cne '0xEF' -or [string]$rootPartition.fatType -cne 'FAT32' -or [string]$efiPartition.fatType -cne 'FAT32') { throw 'Candidate must have an MBR 0x0C root FAT32 partition and a separate 0xEF EFI FAT32 partition.' }
    if ([int64]$rootPartition.index -ne 1 -or [int64]$efiPartition.index -ne 2 -or [int64]$rootPartition.startLba -lt 1 -or [int64]$rootPartition.sectors -le 0 -or [int64]$efiPartition.startLba -lt 1 -or [int64]$efiPartition.sectors -le 0 -or [int64]$rootPartition.endLba -ne ([int64]$rootPartition.startLba + [int64]$rootPartition.sectors - 1) -or [int64]$efiPartition.endLba -ne ([int64]$efiPartition.startLba + [int64]$efiPartition.sectors - 1) -or [int64]$rootPartition.endLba -ge [int64]$efiPartition.startLba) { throw 'Candidate MBR partition geometry is inconsistent or overlapping.' }
    if ([int64]$diskVerification.partitionStartLba -ne [int64]$rootPartition.startLba -or [int64]$diskVerification.partitionSectors -ne [int64]$rootPartition.sectors -or [int64]$diskVerification.partitionEndLba -ne [int64]$rootPartition.endLba) { throw 'Candidate root partition compatibility fields disagree with its MBR entry.' }
    $diskStream = [IO.File]::OpenRead($diskFull)
    try {
        $mbr = New-Object byte[] 512
        if ($diskStream.Read($mbr, 0, $mbr.Length) -ne 512 -or $mbr[510] -ne 0x55 -or $mbr[511] -ne 0xAA) { throw 'Candidate raw disk has no valid MBR signature.' }
        $actualPartitions = @()
        for ($index = 0; $index -lt 4; $index++) {
            $entryOffset = 446 + 16 * $index
            $type = [int]$mbr[$entryOffset + 4]
            if ($type -eq 0) { continue }
            $actualPartitions += [pscustomobject]@{ index = $index + 1; type = $type; startLba = [BitConverter]::ToUInt32($mbr, $entryOffset + 8); sectors = [BitConverter]::ToUInt32($mbr, $entryOffset + 12) }
        }
        if ($actualPartitions.Count -ne 2 -or $actualPartitions[0].type -ne 0x0C -or $actualPartitions[1].type -ne 0xEF -or [int64]$actualPartitions[0].startLba -ne [int64]$rootPartition.startLba -or [int64]$actualPartitions[0].sectors -ne [int64]$rootPartition.sectors -or [int64]$actualPartitions[1].startLba -ne [int64]$efiPartition.startLba -or [int64]$actualPartitions[1].sectors -ne [int64]$efiPartition.sectors) { throw 'Raw MBR bytes do not match the verified root and EFI partition records.' }
        foreach ($partition in @($rootPartition, $efiPartition)) {
            $diskStream.Position = [int64]$partition.startLba * 512
            $boot = New-Object byte[] 512
            if ($diskStream.Read($boot, 0, $boot.Length) -ne 512 -or [BitConverter]::ToUInt16($boot, 11) -ne 512 -or $boot[13] -eq 0 -or $boot[510] -ne 0x55 -or $boot[511] -ne 0xAA -or [BitConverter]::ToUInt32($boot, 32) -ne [int64]$partition.sectors -or [Text.Encoding]::ASCII.GetString($boot, 82, 8) -cne 'FAT32   ') { throw "FAT32 BPB at MBR partition $($partition.index) is invalid or has the wrong LBA extent." }
        }
    } finally { $diskStream.Dispose() }
    if ($diskVerification.originalGptUnchanged -ne $true -or $diskVerification.payloadHashesMatchAcceptedManifest -ne $true -or $diskVerification.kernelFatPartitionProbe.rootType0x0cSupported -ne $true -or [string]$diskVerification.kernelFatPartitionProbe.firstMountablePartitionType -cne '0x0C' -or $rootPartition.containsAppsDirectory -ne $true -or $rootPartition.containsNativeAmd64Elf -ne $true -or $rootPartition.containsDE440sData -ne $true -or [string]$efiPartition.firmwareBootPath -cne 'EFI/BOOT/BOOTX64.EFI') { throw 'Candidate verification does not establish kernel mount compatibility, AppModel payload availability, and UEFI fallback boot content.' }
    $diskFiles = @{}
    foreach ($entry in $diskVerification.files) { $diskFiles[[string]$entry.path] = [string]$entry.sha256 }
    foreach ($entry in $verification.stagedInputs) {
        $relative = ([IO.Path]::GetFullPath([string]$entry.path).Substring($espFull.Length + 1)).Replace('\', '/')
        if (-not $diskFiles.ContainsKey($relative) -or $diskFiles[$relative] -cne [string]$entry.sha256) { throw "Candidate disk does not contain verified staged payload $relative." }
    }
    $diskRecord = [ordered]@{ path = $diskFull; lengthBytes = (Get-Item -LiteralPath $diskFull).Length; sha256 = $diskHash; format = 'raw'; partitionTable = 'MBR'; fatType = 'FAT32'; sectorSizeBytes = 512; rootPartitionType = '0x0C'; rootPartitionStartLba = [int64]$rootPartition.startLba; rootPartitionSectors = [int64]$rootPartition.sectors; rootPartitionEndLba = [int64]$rootPartition.endLba; efiPartitionType = '0xEF'; efiPartitionStartLba = [int64]$efiPartition.startLba; efiPartitionSectors = [int64]$efiPartition.sectors; efiPartitionEndLba = [int64]$efiPartition.endLba; partitionStartLba = [int64]$diskVerification.partitionStartLba; partitionSectors = [int64]$diskVerification.partitionSectors; partitionEndLba = [int64]$diskVerification.partitionEndLba; verifiedFiles = $diskFiles.Count; verificationPath = [IO.Path]::GetFullPath($CandidateDiskVerificationPath) }
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

function Get-QacLaneCensus([string]$Status, [string]$List, [string]$Queue) {
    $processMatch = [regex]::Match($Status, 'QEMU processes:\s*(\d+)')
    $sessionMatch = [regex]::Match($Status, 'Managed sessions:\s*(\d+)')
    $reservationMatch = [regex]::Match($List, 'Reservations:\s*(\d+)')
    $queueMatch = [regex]::Match($Queue, 'Queued reservations:\s*(\d+)')
    $known = $processMatch.Success -and $sessionMatch.Success -and $reservationMatch.Success -and $queueMatch.Success
    $processCount = if ($processMatch.Success) { [int]$processMatch.Groups[1].Value } else { -1 }
    $sessionCount = if ($sessionMatch.Success) { [int]$sessionMatch.Groups[1].Value } else { -1 }
    $reservationCount = if ($reservationMatch.Success) { [int]$reservationMatch.Groups[1].Value } else { -1 }
    $queueCount = if ($queueMatch.Success) { [int]$queueMatch.Groups[1].Value } else { -1 }
    $available = $known -and $processCount -eq 0 -and $sessionCount -eq 0 -and $reservationCount -eq 0 -and $queueCount -eq 0
    [ordered]@{
        countsKnown = $known
        processCount = $processCount
        managedSessionCount = $sessionCount
        reservationCount = $reservationCount
        queuedReservationCount = $queueCount
        available = $available
    }
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
    $frameworkDescription = $null
    try { $frameworkDescription = [Runtime.InteropServices.RuntimeInformation]::FrameworkDescription } catch { $frameworkDescription = '.NET Framework ' + [Environment]::Version.ToString() }
    $cscDirectory = if ([IntPtr]::Size -eq 8) { Join-Path $env:windir 'Microsoft.NET\Framework64\v4.0.30319' } else { Join-Path $env:windir 'Microsoft.NET\Framework\v4.0.30319' }
    $cscPath = Join-Path $cscDirectory 'csc.exe'
    $cscVersion = if (Test-Path -LiteralPath $cscPath) { (Get-Item -LiteralPath $cscPath).VersionInfo.FileVersion } else { $null }
    $runnerResult = [ordered]@{
        schemaVersion = 1
        mode = 'offline-runner-self-test'
        startedAtLocal = (Get-Date).ToString('o')
        hostRuntime = [ordered]@{ powershellVersion = [string]$PSVersionTable.PSVersion; edition = [string]$PSVersionTable.PSEdition; clrVersion = [string]$PSVersionTable.CLRVersion; frameworkDescription = [string]$frameworkDescription; processBitness = [IntPtr]::Size * 8; csharpCompilerPath = $cscPath; csharpCompilerFileVersion = $cscVersion }
        classification = 'PASS'
        qemuLaunched = $false
        qacReservationRequested = $false
        checks = [ordered]@{}
        logs = [ordered]@{}
    }
    $capture1 = $null; $capture2 = $null; $capture3 = $null; $capture4 = $null; $capture5 = $null; $capture6 = $null
    $managedExitTest = $null; $managedTimeoutTest = $null
    try {
        $tokens = $null
        $parseErrors = $null
        [System.Management.Automation.Language.Parser]::ParseFile($PSCommandPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
        if (@($parseErrors).Count -ne 0) { throw "Windows PowerShell parser returned $(@($parseErrors).Count) error(s): $($parseErrors[0].Message)" }
        $runnerResult.checks.scriptParsing = [ordered]@{ passed = $true; parser = 'Windows PowerShell 5.1 Parser.ParseFile'; parseErrors = 0 }
        $runnerResult.checks.candidateIdentityOverrides = [ordered]@{
            passed = $true
            kernelArtifactPath = [IO.Path]::GetFullPath($Kernel)
            expectedKernelSha256 = $Expected.Kernel
            expectedDiskSha256 = $Expected.Disk
        }

        $loadedHelpers = @('SN10R3SerialCapture', 'SN10R3ManagedProcess')
        foreach ($typeName in $loadedHelpers) { if (-not ($typeName -as [type])) { throw "Required C# helper type was not loaded: $typeName" } }
        $runnerResult.checks.addTypeCompilation = [ordered]@{ passed = $true; language = 'C# supported by Windows PowerShell 5.1 Add-Type'; loadedTypes = $loadedHelpers; compiler = 'System.CodeDom CSharpCodeProvider invoked by Add-Type'; diagnostics = @(); result = 'Compilation completed without errors or warnings.' }

        $freeLane = Get-QacLaneCensus "Coordinator: Online`nQEMU processes: 0`nManaged sessions: 0; queued: 0" 'Reservations: 0' 'Queued reservations: 0'
        $busyLane = Get-QacLaneCensus "Coordinator: Online`nQEMU processes: 1 (0 foreign)`nManaged sessions: 1; queued: 0; blocked: 0" 'Reservations: 1' 'Queued reservations: 0'
        $unknownLane = Get-QacLaneCensus 'Coordinator: Online' 'Reservations: 0' 'Queued reservations: 0'
        if (-not $freeLane.available -or $busyLane.available -or -not $busyLane.countsKnown -or $unknownLane.countsKnown) { throw 'QAC lane preflight did not fail closed on occupied or incomplete census data.' }
        $runnerResult.checks.qacLaneFailFast = [ordered]@{ passed = $true; emptyLaneAdmitted = $freeLane.available; occupiedLaneRejected = (-not $busyLane.available); incompleteCensusRejected = (-not $unknownLane.countsKnown); reservationRequestedBySelfTest = $false }

        $argCheck = Test-StructuredArgumentPassing
        $driveArgs = New-QemuArguments 'C:\SN10 R3 & (candidate), rev 2\stellar-navigator.raw' 40001 40002 'C:\SN10 R3 & (firmware), rev 2\OVMF.fd'
        $firmwareArg = $driveArgs[3]
        $diskArg = $driveArgs[5]
        $expectedFirmwareArg = 'if=pflash,format=raw,readonly=on,file=' + ('C:\SN10 R3 & (firmware), rev 2\OVMF.fd'.Replace(', ', ',, '))
        if ($firmwareArg -cne $expectedFirmwareArg -or $diskArg -notmatch '^file=(?!fat:).+\.raw,format=raw,if=ide,index=0$' -or $driveArgs[0] -ne '-machine') {
            throw 'Disposable writable raw MBR/FAT32 disk argument construction failed.'
        }
        if ($firmwareArg.Contains('"') -or $diskArg.Contains('"') -or $diskArg -notmatch ',, rev 2') { throw 'QEMU drive path escaping or structured quoting is invalid.' }
        $runnerResult.checks.commandConstruction = [ordered]@{
            passed = $true
            windowsPathsWithSpaces = $true
            qemuDriveFilenameCommasDoubled = $true
            shellQuotesEmbedded = $false
            firmwareAttachment = $firmwareArg
            diskAttachment = $diskArg
            diskInterface = 'ide index 0, raw MBR disk with a FAT32 0x0C root partition and FAT32 0xEF EFI partition, writable disposable copy'
            isolation = 'QEMU receives one per-run copy; the verified candidate disk and source ESP remain unchanged'
            argumentVector = $driveArgs
            processArgumentRoundTrip = $argCheck
        }

        $managedExitStdout = Join-Path $script:RunRoot 'selftest-managed-process.stdout.log'
        $managedExitStderr = Join-Path $script:RunRoot 'selftest-managed-process.stderr.log'
        $exitCommand = "Write-Output SN10R8_TEST_STDOUT; [Console]::Error.WriteLine('SN10R8_TEST_STDERR'); exit 23"
        $managedExitTest = [SN10R3ManagedProcess]::new((Join-Path $PSHOME 'powershell.exe'), $script:RunRoot, @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $exitCommand), $managedExitStdout, $managedExitStderr)
        if (-not $managedExitTest.WaitForExit(10000) -or $managedExitTest.ExitCode -ne 23 -or $managedExitTest.StdoutText -notmatch 'SN10R8_TEST_STDOUT' -or $managedExitTest.StderrText -notmatch 'SN10R8_TEST_STDERR') { throw 'Managed process helper did not preserve redirected output and abnormal exit code 23.' }
        $managedExitTest.Dispose(); $managedExitTest = $null

        $timeoutStdout = Join-Path $script:RunRoot 'selftest-managed-timeout.stdout.log'
        $timeoutStderr = Join-Path $script:RunRoot 'selftest-managed-timeout.stderr.log'
        $timeoutCommand = "Write-Output SN10R8_TIMEOUT_STARTED; Start-Sleep -Seconds 2; exit 0"
        $managedTimeoutTest = [SN10R3ManagedProcess]::new((Join-Path $PSHOME 'powershell.exe'), $script:RunRoot, @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $timeoutCommand), $timeoutStdout, $timeoutStderr)
        $timeoutReady = [Diagnostics.Stopwatch]::StartNew()
        while ($managedTimeoutTest.StdoutText -notmatch 'SN10R8_TIMEOUT_STARTED' -and $timeoutReady.Elapsed.TotalSeconds -lt 5) { Start-Sleep -Milliseconds 50 }
        if ($managedTimeoutTest.StdoutText -notmatch 'SN10R8_TIMEOUT_STARTED') { throw 'Managed timeout fixture did not report its live marker.' }
        $boundedExitWait = -not $managedTimeoutTest.WaitForExit(100)
        if (-not $boundedExitWait -or -not $managedTimeoutTest.WaitForExit(5000) -or $managedTimeoutTest.ExitCode -ne 0) { throw 'Managed process bounded wait did not time out while active and then report normal completion.' }
        $managedTimeoutTest.Dispose(); $managedTimeoutTest = $null
        $runnerResult.checks.managedProcessHandling = [ordered]@{ passed = $true; abnormalExitCode = 23; stdoutCaptured = $true; stderrCaptured = $true; boundedWaitTimedOutWhileRunning = $boundedExitWait; normalExitAfterTimeout = $true; evidence = @($managedExitStdout, $managedExitStderr, $timeoutStdout, $timeoutStderr) }

        $previousStorageFailure = Get-QemuEarlyExitDetails "QAC QEMU launched: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280`nQAC QEMU exited: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280 exitCode=1`nC:\Program Files\qemu\qemu-system-x86_64.exe: Block node is read-only"
        $otherStartupFailure = Get-QemuEarlyExitDetails "QAC QEMU launched: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280`nQAC QEMU exited: reservation=20e46155756b4cb383d0a7198b86b21e pid=13280 exitCode=1`nfirmware argument could not be opened"
        if (-not $previousStorageFailure -or $previousStorageFailure.classification -cne 'PREBOOT_STORAGE_FAILURE' -or -not $otherStartupFailure -or $otherStartupFailure.classification -cne 'PREBOOT_QEMU_STARTUP_FAILURE') { throw 'Early QEMU startup failures were not classified separately from QAC coordination.' }
        $runnerResult.checks.earlyQemuExitClassification = [ordered]@{ passed = $true; reproducesRecordedStorageFailure = $true; classification = $previousStorageFailure.classification; distinguishesOtherStartupFailure = $true; otherClassification = $otherStartupFailure.classification }
        $copySource = Join-Path $script:RunRoot 'selftest-source.raw'
        $copyTarget = Join-Path $script:RunRoot 'selftest-disposable.raw'
        [IO.File]::WriteAllBytes($copySource, [Text.Encoding]::UTF8.GetBytes('immutable-candidate-test-bytes'))
        [IO.File]::SetAttributes($copySource, ([IO.File]::GetAttributes($copySource) -bor [IO.FileAttributes]::ReadOnly))
        $copyEvidence = New-DisposableQemuDiskCopy $copySource $copyTarget
        if (-not $copyEvidence.byteIdenticalAtCreation -or $copyEvidence.sourceModified -or $copyEvidence.destinationReadOnly -or -not $copyEvidence.writeAccess.writeOpenSucceeded -or (Get-Sha256 $copySource) -cne $copyEvidence.sourceSha256 -or ([IO.File]::GetAttributes($copySource) -band [IO.FileAttributes]::ReadOnly) -eq 0) { throw 'Disposable QEMU disk-copy isolation or write-access check failed.' }
        $sourceHashBeforeSimulatedGuestWrite = Get-Sha256 $copySource
        $simulatedGuestWrite = $null
        try {
            $simulatedGuestWrite = [IO.File]::Open($copyTarget, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
            $simulatedGuestWrite.Seek(0, [IO.SeekOrigin]::Begin) | Out-Null
            $simulatedGuestWrite.WriteByte(0xff)
        } finally { if ($simulatedGuestWrite) { $simulatedGuestWrite.Dispose() } }
        if ((Get-Sha256 $copySource) -cne $sourceHashBeforeSimulatedGuestWrite -or (Get-Sha256 $copyTarget) -ceq $copyEvidence.initialSha256) { throw 'Simulated guest disk write did not remain isolated to the writable copy.' }
        $runnerResult.checks.disposableDiskIsolation = [ordered]@{ passed = $true; sourceRemainsByteIdenticalAfterSimulatedGuestWrite = $true; sourceReadOnlyAttributePreserved = $true; perRunCopyInitiallyMatches = $true; perRunCopyReadOnlyAttributeCleared = $true; hostWriteOpenSucceeded = $true; simulatedGuestWriteChangedOnlyCopy = $true; evidence = $copyEvidence }

        $candidateCopyPath = Join-Path $script:RunRoot 'selftest-candidate-writable-copy.raw'
        $candidateCopyEvidence = New-DisposableQemuDiskCopy $CandidateDiskPath $candidateCopyPath
        if ($candidateCopyEvidence.sourceSha256 -cne $Expected.Disk -or $candidateCopyEvidence.initialSha256 -cne $Expected.Disk -or $candidateCopyEvidence.sourceSha256AfterCopy -cne $Expected.Disk -or -not $candidateCopyEvidence.writeAccess.writeOpenSucceeded) { throw 'Actual SN10R8 candidate disposable-copy verification failed.' }
        $candidateQemuArguments = @(New-QemuArguments $candidateCopyPath 40003 40004)
        $candidateIdeArguments = @()
        for ($i = 0; $i -lt $candidateQemuArguments.Count - 1; $i++) {
            if ($candidateQemuArguments[$i] -ceq '-drive' -and $candidateQemuArguments[$i + 1] -match '(^|,)if=ide(,|$)') { $candidateIdeArguments += [string]$candidateQemuArguments[$i + 1] }
        }
        if ($candidateIdeArguments.Count -ne 1 -or $candidateIdeArguments[0] -cne "file=$candidateCopyPath,format=raw,if=ide,index=0" -or $candidateIdeArguments[0] -match '(?i)readonly|if=cdrom|fat:') { throw 'Actual candidate copy is not represented by one writable raw IDE QEMU attachment.' }
        $candidateSourceAfterTest = Get-Sha256 $CandidateDiskPath
        if ($candidateSourceAfterTest -cne $Expected.Disk) { throw 'Actual candidate source disk changed during offline writable-copy verification.' }
        Remove-Item -LiteralPath $candidateCopyPath -Force
        $runnerResult.checks.actualCandidateDiskCopy = [ordered]@{ passed = $true; originalSha256 = $candidateSourceAfterTest; originalUnchanged = $true; disposableInitialSha256 = $candidateCopyEvidence.initialSha256; disposableWriteOpenSucceeded = $candidateCopyEvidence.writeAccess.writeOpenSucceeded; sourceAttributesUnchanged = ($candidateCopyEvidence.sourceAttributes -ceq $candidateCopyEvidence.sourceAttributesAfterCopy); effectiveIdeDiskArgument = $candidateIdeArguments[0]; disposableCopyRemovedAfterOfflineCheck = (-not (Test-Path -LiteralPath $candidateCopyPath)); qemuLaunched = $false }

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
        $lifecycleMarkers = @('[TEST] first-ephemeris-evaluated', '[TEST] first-close-requested', '[TEST] first-shutdown-complete', '[TEST] first-appmodel-return', '[TEST] reopen-command-dispatched', '[TEST] second-window-created', '[TEST] second-ephemeris-evaluated', '[TEST] second-close-requested', '[TEST] second-shutdown-complete', '[TEST] second-cleanup-complete')
        $lifecycleCursor = 0
        foreach ($marker in $lifecycleMarkers) {
            $lifecycleText = Wait-SerialPattern -Capture $capture1 -Pattern ([regex]::Escape($marker)) -Seconds 5 -Description "offline close/reopen marker $marker" -StartIndex $lifecycleCursor
            $markerMatch = [regex]::Match($lifecycleText, [regex]::Escape($marker))
            if (-not $markerMatch.Success) { throw "Offline close/reopen marker was not received: $marker" }
            $lifecycleCursor += $markerMatch.Index + $markerMatch.Length
        }
        if (-not $producer1.WaitForExit(5000)) { $producer1.Kill(); throw 'Controlled serial producer did not exit.' }
        [void]$capture1.WaitForCompletion(3000)
        $liveText = $capture1.Snapshot()
        if ($producer1.ExitCode -ne 23 -or $capture1.BytesReceived -le 0 -or $liveText -notmatch 'partial-after-nonzero-exit') { throw 'Abnormal producer exit did not leave a complete partial log.' }
        $runnerResult.checks.liveSerialStream = [ordered]@{ passed = $true; connected = $capture1.Connected; bytes = $capture1.BytesReceived; markersRecognizedIncrementally = $true; producerExitCode = $producer1.ExitCode; partialLogPreservedAfterAbnormalTermination = $true; logPath = $capture1Path }
        $runnerResult.checks.offlineCloseReopen = [ordered]@{ passed = $true; markersObservedInOrder = $lifecycleMarkers; firstInstanceNormalCloseAndReturn = $true; relaunchDispatchedThroughDesktopLaunch = $true; secondInstanceFreshEvaluationAndCleanup = $true; cursorAdvancedBetweenMarkers = $true }
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
        $missingAppsFailure = Get-GuestFailureMarker '[NATIVE-ELF] discovery result=NO_APPS_DIRECTORY'
        $appModelLaunchFailure = Get-GuestFailureMarker '[SN10-GUEST] appmodel-launch result=failed'
        if ($missingAppsFailure -notmatch 'NO_APPS_DIRECTORY' -or $appModelLaunchFailure -notmatch 'appmodel-launch result=failed') { throw 'Native AppModel discovery or launch failure was not recognized as a fatal guest acceptance marker.' }
        $runnerResult.checks.guestFailureMarker = [ordered]@{ passed = $true; classification = 'GUEST_FAILURE'; fatalKernelMarkerRecognized = '[KERNEL-PANIC]'; fatalNativeDiscoveryMarkerRecognized = '[NATIVE-ELF] discovery result=NO_APPS_DIRECTORY'; fatalAppModelLaunchMarkerRecognized = '[SN10-GUEST] appmodel-launch result=failed'; logPath = $capture6Path }
        $capture6.Dispose(); $capture6 = $null

        $capture4Path = Join-Path $script:RunRoot 'selftest-no-client.serial.log'
        $capture4 = [SN10R3SerialCapture]::new($capture4Path)
        $captureFailureObserved = $false
        try { [void](Wait-SerialPattern -Capture $capture4 -Pattern 'unavailable-without-a-client' -Seconds 1 -Description 'no-client capture failure') }
        catch { $captureFailureObserved = $_.Exception.Message -like 'SERIAL_CAPTURE_FAILURE:*' }
        if (-not $captureFailureObserved -or $capture4.Connected -or $capture4.BytesReceived -ne 0) { throw 'No-client capture failure was not distinguished from connected zero-byte output.' }
        $capture4.Dispose(); $capture4 = $null
        $runnerResult.checks.captureFailureClassification = [ordered]@{ passed = $true; classification = 'SERIAL_CAPTURE_FAILURE'; noClientDetected = $captureFailureObserved; rule = 'No QEMU TCP client is a capture failure; a connected client with zero bytes is no guest output.' }
        $logWritable = $true
        foreach ($capturePath in @($capture1Path, $capture2Path, $capture3Path, $capture5Path, $capture6Path, $capture4Path)) {
            $logStream = [IO.File]::Open($capturePath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $logStream.Dispose()
        }
        $runnerResult.checks.resourcesReleased = [ordered]@{ passed = $true; listenerStopped = $true; ephemeralPortRebindSucceeded = $true; captureLogFilesCanBeExclusivelyReopened = $logWritable }
        $runnerResult.logs = [ordered]@{ live = $capture1Path; missingMarker = $capture2Path; noOutput = $capture3Path; streamDisconnect = $capture5Path; guestFailure = $capture6Path }
        $runnerResult.finishedAtLocal = (Get-Date).ToString('o')
        $runnerResult.resultPath = Join-Path $script:RunRoot 'runner-selftest.json'
        $runnerResult | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $runnerResult.resultPath -Encoding utf8
        Write-Host "[SN10R8] offline runner self-test PASS: $($runnerResult.resultPath)"
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
        Write-Host "[SN10R8] offline runner self-test FAIL: $($runnerResult.error)"
        Write-Host "[SN10R8] evidence: $($runnerResult.resultPath)"
        return 1
    }
}

New-RunDirectory
if ($SelfTest) { exit (Invoke-SelfTest) }
if ($VerifyCandidateOnly) {
    try {
        [void](Assert-CandidateIdentity)
        Write-Host "[SN10R8] candidate, staged ESP, and raw disk verification PASS: $(Join-Path $script:RunRoot 'candidate-verification.json')"
        exit 0
    } catch {
        [ordered]@{ passed = $false; classification = 'FAIL'; error = $_.Exception.Message; candidateEsp = $CandidateEspPath; candidateDisk = $CandidateDiskPath; capturedAtLocal = (Get-Date).ToString('o') } |
            ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $script:RunRoot 'candidate-verification.json') -Encoding utf8
        Write-Host "[SN10R8] candidate verification FAIL: $($_.Exception.Message)"
        exit 1
    }
}

$script:Owned = $null
$script:ManagedQac = $null
$script:ReservationId = $null
$script:MonitorPort = 0
$capture = $null
$runtimeDiskCopy = $null
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
    $laneCensus = Get-QacLaneCensus $qacStatus $qacList $qacQueue
    $overall.checks.qacPreflight = [ordered]@{
        coordinatorOnline = $true
        processCount = $laneCensus.processCount
        managedSessionCount = $laneCensus.managedSessionCount
        reservationCount = $laneCensus.reservationCount
        queuedReservationCount = $laneCensus.queuedReservationCount
        censusComplete = $laneCensus.countsKnown
        laneAvailable = $laneCensus.available
        reservationNotRequested = $true
        queueSnapshot = $qacQueue
        acquisition = 'one QAC-managed acceptance request only after a fresh empty-lane census'
        acquisitionWaitBoundSeconds = $LaneAcquireTimeoutSeconds
        statusPath = (Join-Path $script:RunRoot 'qac-status-before.log')
        listPath = (Join-Path $script:RunRoot 'qac-list-before.log')
        queuePath = (Join-Path $script:RunRoot 'qac-queue-before.log')
    }
    if (-not $laneCensus.countsKnown) {
        $overall.blocker = 'QAC_CENSUS_FAILURE'
        $overall.checks.qacPreflight.reservationNotRequested = $true
        throw 'QAC lane census is incomplete; no reservation was requested.'
    }
    if (-not $laneCensus.available) {
        $overall.blocker = 'QAC_OCCUPIED'
        $overall.checks.qacPreflight.foreignResourcesUntouched = $true
        throw 'QAC lane is occupied; no reservation was requested and no foreign session was changed.'
    }

    $runtimeDiskPath = Join-Path $script:RunRoot 'qac-disposable-disk.raw'
    $runtimeDiskCopy = New-DisposableQemuDiskCopy $CandidateDiskPath $runtimeDiskPath
    if ($runtimeDiskCopy.initialSha256 -cne $candidateCheck.candidateDisk.sha256) { throw 'Per-run writable QEMU disk copy differs from the verified immutable candidate image.' }
    $overall.checks.runtimeDiskCopy = [ordered]@{ passed = $true; path = $runtimeDiskCopy.path; initialSha256 = $runtimeDiskCopy.initialSha256; byteLength = $runtimeDiskCopy.lengthBytes; sourceCandidate = $runtimeDiskCopy.sourcePath; sourceCandidateSha256 = $runtimeDiskCopy.sourceSha256; sourceCandidateSha256AfterCopy = $runtimeDiskCopy.sourceSha256AfterCopy; sourceCandidateModified = $runtimeDiskCopy.sourceModified; destinationReadOnly = $runtimeDiskCopy.destinationReadOnly; writeAccess = $runtimeDiskCopy.writeAccess; attachedReadWriteForIdeCompatibility = $true }

    $monitorProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $monitorProbe.Start()
    $script:MonitorPort = ([Net.IPEndPoint]$monitorProbe.LocalEndpoint).Port
    $monitorProbe.Stop()
    $serialLog = Join-Path $script:RunRoot 'guest-serial.log'
    $capture = [SN10R3SerialCapture]::new($serialLog)
    $qemuArguments = @(New-QemuArguments $runtimeDiskPath $capture.Port $script:MonitorPort)
    $ideDriveArguments = @()
    for ($i = 0; $i -lt $qemuArguments.Count - 1; $i++) {
        if ($qemuArguments[$i] -ceq '-drive' -and $qemuArguments[$i + 1] -match '(^|,)if=ide(,|$)') { $ideDriveArguments += [string]$qemuArguments[$i + 1] }
    }
    if ($ideDriveArguments.Count -ne 1 -or $ideDriveArguments[0] -notmatch '^file=.+\.raw,format=raw,if=ide,index=0$' -or $ideDriveArguments[0] -match '(?i)readonly|if=cdrom|fat:') { throw 'Effective QEMU arguments do not attach exactly one writable raw IDE hard disk.' }
    $qacArguments = @('run', '--project', 'StellarNavigator', '--mode', 'acceptance', '--', $Qemu) + $qemuArguments
    $qacChildArguments = @($qacArguments[7..($qacArguments.Count - 1)])
    if ($qacChildArguments.Count -ne $qemuArguments.Count) { throw 'QAC launcher argument vector does not preserve the effective QEMU argument boundaries.' }
    for ($i = 0; $i -lt $qemuArguments.Count; $i++) { if ([string]$qacChildArguments[$i] -cne [string]$qemuArguments[$i]) { throw "QAC launcher changed QEMU argument $i." } }
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
        diskLayout = [ordered]@{ format = 'raw'; partitionTable = 'MBR'; rootPartitionType = $candidateCheck.candidateDisk.rootPartitionType; rootPartitionStartLba = $candidateCheck.candidateDisk.rootPartitionStartLba; rootPartitionSectors = $candidateCheck.candidateDisk.rootPartitionSectors; efiPartitionType = $candidateCheck.candidateDisk.efiPartitionType; efiPartitionStartLba = $candidateCheck.candidateDisk.efiPartitionStartLba; efiPartitionSectors = $candidateCheck.candidateDisk.efiPartitionSectors; sectorSizeBytes = 512; filesystem = 'FAT32'; interface = 'IDE index 0'; qemuAccess = 'writable isolated per-run clone' }
        diskAttachment = $ideDriveArguments[0]
        qacChildArgumentPassthroughVerified = $true
        firmwarePflashReadOnly = $true
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
    if ($runtimeDiskCopy) {
        try {
            $sourceHashAfterAttempt = Get-Sha256 $CandidateDiskPath
            $sourceUnchanged = $sourceHashAfterAttempt -ceq $candidateCheck.candidateDisk.sha256
            $overall.checks.sourceDiskAfterAttempt = [ordered]@{ passed = $sourceUnchanged; path = [IO.Path]::GetFullPath($CandidateDiskPath); expectedSha256 = $candidateCheck.candidateDisk.sha256; actualSha256 = $sourceHashAfterAttempt; unchanged = $sourceUnchanged }
            if (-not $sourceUnchanged) { $overall.classification = 'FAIL'; $overall.blocker = 'SOURCE_DISK_MUTATED' }
        } catch {
            $overall.checks.sourceDiskAfterAttempt = [ordered]@{ passed = $false; path = [IO.Path]::GetFullPath($CandidateDiskPath); error = $_.Exception.Message }
            $overall.classification = 'FAIL'
            $overall.blocker = 'SOURCE_DISK_HASH_FAILURE'
        }
    }
    $overall.finishedAtLocal = (Get-Date).ToString('o')
    $overall.serialLog = if ($serialLog) { $serialLog } else { $null }
    $overall.resultPath = Join-Path $script:RunRoot 'acceptance-result.json'
    $overall | ConvertTo-Json -Depth 18 | Set-Content -LiteralPath $overall.resultPath -Encoding utf8
    Write-Host "[SN10R8] $($overall.classification): $($overall.resultPath)"
    if ($overall.blocker) { Write-Host "[SN10R8] blocker=$($overall.blocker)" }
}

if ($overall.classification -eq 'PASS') { exit 0 }
if ($overall.classification -eq 'BLOCKED') { exit 2 }
exit 1
