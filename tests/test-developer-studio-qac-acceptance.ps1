$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\scripts\DeveloperStudio.QacAcceptance.psm1') -Force

function Assert-QacTest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    Write-Host "PASS: $Message"
}

Assert-QacTest ((Get-QacAcceptancePassMarker -Phase '28Q') -ceq 'DEVELOPER_STUDIO_PHASE28Q_PASS') 'Phase 28Q waits only for its guest acceptance PASS marker.'
Assert-QacTest ((Get-QacAcceptancePassMarker -Phase '28O') -ceq 'DEVELOPER_STUDIO_PHASE28O_PASS') 'Phase 28O waits only for its guest acceptance PASS marker.'
Assert-QacTest ((Get-QacAcceptancePassMarker -Phase '27E') -ceq 'phase27e=PASS') 'Phase 27E waits only for its guest acceptance PASS marker.'
Assert-QacTest ((Get-QacAcceptancePassMarker -Phase '27F') -ceq 'phase27f=PASS') 'Phase 27F waits only for its guest acceptance PASS marker.'
Assert-QacTest ((Get-QacAcceptancePassMarker -Phase 'COMP4D') -ceq 'DEVELOPER_STUDIO_COMP4D_GUEST_PASS') 'COMP4D waits only for its genuine guest Build/Run PASS marker.'
Assert-QacTest ((Get-QacAcceptanceCompletionMarker -Phase 'COMP4D') -ceq 'DEVELOPER_STUDIO_COMP4D_GUEST_COMPLETE') 'COMP4D exposes a diagnostic stop marker that cannot satisfy acceptance.'
Assert-QacTest ((Get-QacAcceptancePassMarker -Phase 'COMP4F') -ceq 'DEVELOPER_STUDIO_COMP4F_GUEST_PASS') 'COMP4F waits only for its genuine guest character/byte Build/Run PASS marker.'
Assert-QacTest ((Get-QacAcceptanceCompletionMarker -Phase 'COMP4F') -ceq 'DEVELOPER_STUDIO_COMP4F_GUEST_COMPLETE') 'COMP4F exposes a diagnostic stop marker that cannot satisfy acceptance.'
Assert-QacTest ((Get-QacAcceptancePassMarker -Phase 'COMP4K') -ceq 'DEVELOPER_STUDIO_COMP4K_GUEST_PASS') 'COMP4K waits only for consolidated native compiler guest acceptance.'
Assert-QacTest ((Get-QacAcceptanceCompletionMarker -Phase 'COMP4K') -ceq 'DEVELOPER_STUDIO_COMP4K_GUEST_COMPLETE') 'COMP4K exposes a diagnostic stop marker that cannot satisfy acceptance.'
$smokeScript = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\scripts\smoke-compiler-bootstrap.ps1') -Raw
Assert-QacTest ($smokeScript.Contains('if ($Phase27W -and -not $Comp4DNativeBuildRun -and -not $Comp4FNativeBuildRun -and -not $Comp4KNativeBuildRun)')) 'COMP4D, COMP4F, and COMP4K use their focused proof gates instead of requiring the full Phase 27W matrix.'
Assert-QacTest ($smokeScript.Contains("'comp4d_cooperative_progress=PASS'") -and
    $smokeScript.Contains("'comp4d_compile_failure_preserved=PASS'") -and
    $smokeScript.Contains("'comp4d_failed_build_blocks_run=PASS'")) 'COMP4D requires guest Build progress, failed-Build preservation, and Run rejection evidence.'
Assert-QacTest ($smokeScript.Contains('[switch]$Comp4FNativeBuildRunOnly') -and
    $smokeScript.Contains("'comp4f_character_literal=PASS'") -and
    $smokeScript.Contains("'comp4f_byte_array=PASS'") -and
    $smokeScript.Contains("'comp4f_queued_cancel_preserved=PASS'") -and
    $smokeScript.Contains("'comp4f_partial_elf_rejected=PASS'") -and
    $smokeScript.Contains("'comp4f_compile_failure_preserved=PASS'") -and
    $smokeScript.Contains("'comp4f_repeated_build_identity=PASS'")) 'COMP4F admission requires the bounded character, array, failure-safety, and generation checks.'
Assert-QacTest ($smokeScript.Contains('[switch]$Comp4KNativeBuildRunOnly') -and
    $smokeScript.Contains("'comp4k_object_abi16=PASS'") -and
    $smokeScript.Contains("'comp4k_character_bytes=PASS'") -and
    $smokeScript.Contains("'comp4k_queued_cancel_preserved=PASS'") -and
    $smokeScript.Contains("'comp4k_function_abi=PASS'") -and
    $smokeScript.Contains("'comp4k_struct_fields=PASS'") -and
    $smokeScript.Contains("'comp4k_unsigned_byte_pointers=PASS'") -and
    $smokeScript.Contains("'comp4k_stale_artifact_rejected=PASS'") -and
    $smokeScript.Contains("'DEVELOPER_STUDIO_COMP4K_GUEST_PASS'")) 'COMP4K admission requires consolidated guest Build/Run markers for bytes, functions, structures, and typed pointers.'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

public static class QacWindowsCommandLineTest {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CommandLineToArgvW(string commandLine, out int argumentCount);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    public static string[] Parse(string commandLine) {
        int count;
        IntPtr values = CommandLineToArgvW(commandLine, out count);
        if (values == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try {
            string[] result = new string[count];
            for (int index = 0; index < count; ++index) {
                IntPtr item = Marshal.ReadIntPtr(values, index * IntPtr.Size);
                result[index] = Marshal.PtrToStringUni(item);
            }
            return result;
        } finally { LocalFree(values); }
    }
}

public static class QacQmpProtocolFixture {
    private static readonly ConcurrentQueue<string> Commands = new ConcurrentQueue<string>();
    private static readonly TaskCompletionSource<bool> QuitSeen = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
    private static TcpListener listener;

    public static int Start() {
        listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task.Run(() => {
            using (TcpClient client = listener.AcceptTcpClient())
            using (NetworkStream stream = client.GetStream())
            using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
            using (StreamWriter writer = new StreamWriter(stream, Encoding.UTF8) { AutoFlush = true, NewLine = "\n" }) {
                writer.WriteLine("{\"QMP\":{\"version\":{\"qemu\":{\"major\":0}},\"capabilities\":[]}}");
                string capabilities = reader.ReadLine();
                Commands.Enqueue(capabilities ?? "<eof>");
                writer.WriteLine("{\"return\":{}}");
                string quit = reader.ReadLine();
                Commands.Enqueue(quit ?? "<eof>");
                QuitSeen.TrySetResult(true);
            }
        });
        return port;
    }

    public static bool WaitForQuit(int timeoutMilliseconds) { return QuitSeen.Task.Wait(timeoutMilliseconds); }
    public static string[] ReceivedCommands() { return Commands.ToArray(); }
    public static void Stop() { try { if (listener != null) listener.Stop(); } catch (SocketException) { } }
}
'@

$qemuPath = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$qemuArguments = @(
    '-machine', 'pc,usb=off',
    '-drive', 'file=fat:rw:C:\Dev Studio\ESP\,format=raw,if=ide,index=0',
    '-name', 'Developer Studio "QAC" guest',
    'trailing-backslash=C:\Dev Studio\',
    ''
)
$argumentString = New-QacManagedRunArgumentString -Project 'DeveloperStudio' -Mode acceptance `
    -QemuExecutable $qemuPath -QemuArguments $qemuArguments
$parsed = [QacWindowsCommandLineTest]::Parse(('qac.exe ' + $argumentString))
$expected = @('qac.exe', 'run', '--project', 'DeveloperStudio', '--mode', 'acceptance', '--', $qemuPath) + $qemuArguments
Assert-QacTest (($parsed -join "`0") -ceq ($expected -join "`0")) 'QAC CLI command keeps QEMU executable and every argument boundary, including spaces, quotes, trailing slashes, and empty values.'

$missingRejected = $false
try { Resolve-QacAcceptanceCliPath -Path 'C:\Program Files\guideXOS\QAC\qac.exe' | Out-Null }
catch { $missingRejected = $_.Exception.Message -like '*Acceptance did not launch QEMU*' }
Assert-QacTest $missingRejected 'Missing QAC CLI is rejected before acceptance launch.'

$port = [QacQmpProtocolFixture]::Start()
$activeCancellation = @{ CancelRequested = $false }
Request-QacCancellation -State $activeCancellation
Assert-QacTest $activeCancellation.CancelRequested 'Active cancellation is recorded for the execution owner without terminating a process.'
$timedOutBeforeQmp = $true
try {
    Assert-QacTest (Send-QacQmpQuit -Port $port -TimeoutSeconds 3) 'Graceful shutdown sends QMP capabilities and quit over loopback.'
    Assert-QacTest ([QacQmpProtocolFixture]::WaitForQuit(3000)) 'QMP fixture observes both protocol commands.'
    $received = [QacQmpProtocolFixture]::ReceivedCommands()
    Assert-QacTest ($received.Count -eq 2 -and $received[0] -eq '{"execute":"qmp_capabilities"}' -and $received[1] -eq '{"execute":"quit"}') 'QMP shutdown uses the expected commands without process termination.'
    Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true `
        -MarkerObservedWithinDeadline $true -RunTimedOut $timedOutBeforeQmp)) 'A graceful QMP quit after timeout preserves the timeout failure.'
} finally {
    [QacQmpProtocolFixture]::Stop()
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('qac-serial-fixture-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
try {
    $marker = 'DEVELOPER_STUDIO_PHASE28Q_PASS'
    $serialPath = Join-Path $fixtureRoot 'serial.log'
    $readerState = @{ Offset = [long]0; Carry = '' }
    [IO.File]::WriteAllText($serialPath, 'DEVELOPER_STUDIO_PHASE28', [Text.Encoding]::ASCII)
    $firstRead = Read-QacSerialMarker -Path $serialPath -Marker $marker -State $readerState
    [IO.File]::AppendAllText($serialPath, 'Q_PASS', [Text.Encoding]::ASCII)
    $secondRead = Read-QacSerialMarker -Path $serialPath -Marker $marker -State $readerState
    Assert-QacTest (-not $firstRead.Found -and $secondRead.Found) 'Serial marker matching survives a marker split across separate reads.'

    $runtimeStart = [DateTime]::Parse('2026-10-19T20:00:00Z').ToUniversalTime()
    $runtimeDeadline = $runtimeStart.AddSeconds(360)
    $queuedAt = $runtimeStart.AddSeconds(-90)
    $withinObservation = $runtimeStart.AddSeconds(359.9)
    Assert-QacTest (Test-QacMarkerObservedWithinDeadline -ObservedAtUtc $withinObservation -DeadlineUtc $runtimeDeadline) 'A marker observed before the guest runtime deadline is eligible.'
    Assert-QacTest (($withinObservation - $queuedAt).TotalSeconds -gt 360 -and
        (Test-QacMarkerObservedWithinDeadline -ObservedAtUtc $withinObservation -DeadlineUtc $runtimeDeadline)) 'Nonzero queue wait is excluded from the guest runtime budget.'

    $delayedPath = Join-Path $fixtureRoot 'delayed.serial.log'
    $delayedState = @{ Offset = [long]0; Carry = '' }
    $writer = [IO.File]::Open($delayedPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    try {
        $bufferedPrefix = [Text.Encoding]::ASCII.GetBytes('guest is still running')
        $writer.Write($bufferedPrefix, 0, $bufferedPrefix.Length)
        $writer.Flush()
        $legacyReadBlocked = $false
        try {
            [void][IO.File]::ReadAllText($delayedPath)
        } catch [IO.IOException] {
            $legacyReadBlocked = $true
        }
        $emptyRead = Read-QacSerialMarker -Path $delayedPath -Marker $marker -State $delayedState
        $delayedBytes = [Text.Encoding]::ASCII.GetBytes($marker)
        $writer.Write($delayedBytes, 0, $delayedBytes.Length)
        $writer.Flush()
        $delayedRead = Read-QacSerialMarker -Path $delayedPath -Marker $marker -State $delayedState
    } finally {
        $writer.Dispose()
    }
    $afterDeadline = $runtimeDeadline.AddMilliseconds(1)
    Assert-QacTest ($legacyReadBlocked -and -not $emptyRead.Found -and $delayedRead.Found -and
        -not (Test-QacMarkerObservedWithinDeadline -ObservedAtUtc $afterDeadline -DeadlineUtc $runtimeDeadline)) 'The legacy whole-file reader is blocked by the active serial writer; shared incremental reads detect later output without moving the deadline.'

    $childExit = $runtimeStart.AddSeconds(10)
    $markerAfterExit = $childExit.AddMilliseconds(1)
    Assert-QacTest (-not (Test-QacMarkerObservedBeforeChildExit -ObservedAtUtc $markerAfterExit -ChildExitAtUtc $childExit)) 'A marker first observed after QEMU exits cannot satisfy acceptance.'
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
}

Assert-QacTest (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -MarkerObservedWithinDeadline $true) 'Acceptance success requires the marker within runtime, zero child exit, and verified reservation release.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $false -MarkerObservedWithinDeadline $true)) 'No acceptance success is reported when reservation release is unverified.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 2 -QemuExitCode 0 -ReservationReleased $false -MarkerObservedWithinDeadline $true -CoordinatorUnavailable $true)) 'Coordinator-unavailable admission cannot become an acceptance pass.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -MarkerObservedWithinDeadline $true -AdmissionTimedOut $true)) 'Admission failure or queue timeout cannot become an acceptance pass.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -MarkerObservedWithinDeadline $false -RunTimedOut $true)) 'A timeout followed by a late flushed marker cannot become an acceptance pass.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -MarkerObservedWithinDeadline $true -CancellationRequested $true)) 'A QMP-shutdown cancellation cannot become an acceptance pass.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -MarkerObservedWithinDeadline $true -RecoveryRequired $true)) 'An unresolved QAC recovery state cannot become an acceptance pass.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 7 -QemuExitCode 7 -ReservationReleased $true -MarkerObservedWithinDeadline $true)) 'A propagated nonzero child exit remains a failed acceptance.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 7 -ReservationReleased $true -MarkerObservedWithinDeadline $true)) 'A mismatch between QAC and QEMU exit codes cannot become an acceptance pass.'
Write-Host 'developer_studio_qac_acceptance_helpers=PASS'
