$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\scripts\DeveloperStudio.QacAcceptance.psm1') -Force

function Assert-QacTest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    Write-Host "PASS: $Message"
}

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
try {
    Assert-QacTest (Send-QacQmpQuit -Port $port -TimeoutSeconds 3) 'Graceful shutdown sends QMP capabilities and quit over loopback.'
    Assert-QacTest ([QacQmpProtocolFixture]::WaitForQuit(3000)) 'QMP fixture observes both protocol commands.'
    $received = [QacQmpProtocolFixture]::ReceivedCommands()
    Assert-QacTest ($received.Count -eq 2 -and $received[0] -eq '{"execute":"qmp_capabilities"}' -and $received[1] -eq '{"execute":"quit"}') 'QMP shutdown uses the expected commands without process termination.'
} finally {
    [QacQmpProtocolFixture]::Stop()
}

Assert-QacTest (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true) 'Acceptance success requires a successful QEMU exit and verified reservation release.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $false)) 'No acceptance success is reported when reservation release is unverified.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 2 -QemuExitCode 0 -ReservationReleased $false)) 'Coordinator-unavailable exit cannot become an acceptance pass.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -RunTimedOut $true)) 'A runtime timeout cannot become an acceptance pass even if the guest later emits success markers.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -CancellationRequested $true)) 'A cancelled QEMU run cannot become an acceptance pass.'
Assert-QacTest (-not (Test-QacManagedAcceptanceSuccess -CliExitCode 0 -QemuExitCode 0 -ReservationReleased $true -RecoveryRequired $true)) 'An unresolved QAC recovery state cannot become an acceptance pass.'
Write-Host 'developer_studio_qac_acceptance_helpers=PASS'
