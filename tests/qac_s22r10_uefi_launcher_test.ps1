[CmdletBinding()]
param([switch]$KeepTestArtifacts)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$isolationPath = Join-Path $repoRoot 'scripts\qac-uefi-artifacts.psm1'
$launchPath = Join-Path $repoRoot 'scripts\qac-s22r10-uefi.psm1'
$launchModuleSource = Get-Content -LiteralPath $launchPath -Raw
Import-Module $isolationPath -Force -ErrorAction Stop
Import-Module $launchPath -Force -ErrorAction Stop
$script:checkCount = 0
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('qac3c3 fixture with spaces ' + [Guid]::NewGuid().ToString('N'))
$sourcesRoot = Join-Path $testRoot 'sources'
$outputRoot = Join-Path $testRoot 'workspaces'
$manifestBytes = $null

function Assert-True {
    param([string]$Name, [bool]$Condition)
    $script:checkCount++
    if (-not $Condition) { throw "FAIL [$($script:checkCount)] $Name" }
    Write-Host "PASS [$($script:checkCount)] $Name"
}

function Assert-Throws {
    param([string]$Name,[scriptblock]$Action,[string]$MessagePattern='')
    $thrown=$false; $message=''
    try { & $Action } catch { $thrown=$true; $message=$_.Exception.Message }
    Assert-True $Name ($thrown -and (-not $MessagePattern -or $message -match $MessagePattern))
    return $message
}

function Write-TestJson {
    param([string]$Path,$Value)
    [IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 20) + "`n",(New-Object Text.UTF8Encoding($false)))
}

function New-TestSources {
    param([string]$Root)
    $esp=Join-Path $Root 'canonical-esp'
    [void][IO.Directory]::CreateDirectory((Join-Path $esp 'EFI\BOOT'))
    [void][IO.Directory]::CreateDirectory((Join-Path $esp 'Apps\PacMan'))
    [IO.File]::WriteAllBytes((Join-Path $esp 'EFI\BOOT\BOOTX64.EFI'),[Text.Encoding]::ASCII.GetBytes('UEFI boot fixture'))
    [IO.File]::WriteAllBytes((Join-Path $esp 'kernel.elf'),[Text.Encoding]::ASCII.GetBytes('kernel fixture'))
    [IO.File]::WriteAllBytes((Join-Path $esp 'ramdisk.img'),[Text.Encoding]::ASCII.GetBytes('ramdisk fixture'))
    [IO.File]::WriteAllText((Join-Path $esp 'Apps\PacMan\app.json'),'{}')
    $isoBytes=New-Object byte[] 32774
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('CD001'),0,$isoBytes,32769,5)
    $iso=Join-Path $Root 'candidate.iso'; [IO.File]::WriteAllBytes($iso,$isoBytes)
    $ovmf=Join-Path $Root 'ovmf.fd'; [IO.File]::WriteAllBytes($ovmf,(New-Object byte[] 4096))
    return [PSCustomObject]@{Esp=$esp;Iso=$iso;Ovmf=$ovmf}
}

function New-TestLaunchContract {
    return [ordered]@{
        executableFamily='qemu-system-x86_64.exe'
        arguments=@(
            '-machine','pc,accel=whpx','-smp','2','-m','2048M','-vga','std','-display','none',
            '-drive','if=pflash,format=raw,readonly=on,file={ovmfFirmware}',
            '-drive','if=ide,format=raw,file=fat:rw:{esp},index=0',
            '-drive','if=none,id=releasecdrom,file={iso},media=cdrom,readonly=on,format=raw',
            '-device','ide-cd,drive=releasecdrom,bootindex=1','-boot','order=d','-net','none',
            '-serial','file:{serialLog}','-D','{qemuDebugLog}','-d','guest_errors,cpu_reset',
            '-rtc','base=utc,clock=host','-no-reboot','-no-shutdown','-monitor','none',
            '-qmp','tcp:127.0.0.1:{QAC3C3-allocated-port},server=on,wait=off','-name','guideXOS-{runId}'
        )
        qmp=[ordered]@{bindAddress='127.0.0.1';port=$null;allocation='unbound until QAC launch';fixedPortReusable=$false}
        firmwareVariableStore=[ordered]@{separatePflashStore=$false;path=$null;guestStateLocation='private ESP directory';guestStateFilename='NvVars'}
        storage=[ordered]@{writableEspArtifact='esp';readOnlyIsoArtifact='release-iso';separateWritableGuestDisk=$false}
    }
}

function New-TestWorkspace {
    param($Sources,[string]$Label)
    $artifacts=@(
        @{Name='esp';Role='writable-uefi-esp';Kind='directory';Access='writable';Disposition='copy';Path=$Sources.Esp;Validation='uefi-esp-directory'},
        @{Name='ovmf-firmware';Role='read-only-uefi-firmware';Kind='file';Access='read-only';Disposition='share';Path=$Sources.Ovmf;Validation='raw-firmware'},
        @{Name='release-iso';Role='read-only-uefi-cdrom';Kind='file';Access='read-only';Disposition='share';Path=$Sources.Iso;Validation='iso9660'}
    )
    return New-QacUefiArtifactWorkspace -Project 'guideXOSServer' -Workflow 's22r10-pacman-uefi-qac' -RunId $Label -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $artifacts -LaunchContract (New-TestLaunchContract)
}

if (-not ('Qac3c3QmpFixtureServer' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;

public sealed class Qac3c3QmpFixtureServer : IDisposable {
    private readonly TcpListener listener;
    private readonly Thread thread;
    private readonly string instanceName;
    private readonly bool silent;
    private TcpClient active;
    public int Port { get; private set; }
    public Qac3c3QmpFixtureServer(string name, bool noGreeting) {
        instanceName = name; silent = noGreeting;
        listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        Port = ((IPEndPoint)listener.LocalEndpoint).Port;
        thread = new Thread(Serve) { IsBackground = true, Name = "QAC3C3 QMP fixture" }; thread.Start();
    }
    private void Serve() {
        try {
            active = listener.AcceptTcpClient();
            using (var stream = active.GetStream())
            using (var reader = new StreamReader(stream, Encoding.UTF8, false, 4096, true))
            using (var writer = new StreamWriter(stream, new UTF8Encoding(false), 4096, true) { NewLine = "\n", AutoFlush = true }) {
                if (silent) { Thread.Sleep(5000); return; }
                writer.WriteLine("{\"QMP\":{\"version\":{\"qemu\":{\"major\":11,\"minor\":0,\"micro\":0},\"package\":\"fixture\"},\"capabilities\":[]}}");
                string line;
                while ((line = reader.ReadLine()) != null) {
                    if (line.Length > 0 && line[0] == '\uFEFF') { writer.WriteLine("{\"error\":{\"class\":\"GenericError\",\"desc\":\"unexpected UTF-8 BOM\"}}"); continue; }
                    using (var document = JsonDocument.Parse(line)) {
                        var root = document.RootElement;
                        var id = root.GetProperty("id").GetRawText();
                        var execute = root.GetProperty("execute").GetString();
                        string value;
                        switch (execute) {
                            case "query-name": value = "{\"name\":\"" + instanceName + "\",\"id\":null}"; break;
                            case "query-status": value = "{\"status\":\"prelaunch\",\"running\":false}"; break;
                            default: value = "{}"; break;
                        }
                        writer.WriteLine("{\"return\":" + value + ",\"id\":" + id + "}");
                    }
                }
            }
        } catch { }
    }
    public void Dispose() {
        try { active?.Close(); } catch { }
        try { listener.Stop(); } catch { }
        try { thread.Join(500); } catch { }
    }
}
'@
}

[void][IO.Directory]::CreateDirectory($sourcesRoot)
[void][IO.Directory]::CreateDirectory($outputRoot)
$sources=New-TestSources -Root $sourcesRoot
$sourceEsp=Get-QacS22R10ArtifactIdentity -Path $sources.Esp -Kind directory
$sourceIso=Get-QacS22R10ArtifactIdentity -Path $sources.Iso -Kind file
$sourceOvmf=Get-QacS22R10ArtifactIdentity -Path $sources.Ovmf -Kind file
$first=$null; $second=$null; $qmpClient=$null; $goodServer=$null; $wrongServer=$null; $silentServer=$null

try {
    $first=New-TestWorkspace -Sources $sources -Label 'manifest'
    $second=New-TestWorkspace -Sources $sources -Label 'manifest'
    $validated=Get-QacS22R10ValidatedManifest -ManifestPath $first.ManifestPath
    Assert-True 'valid completed QAC3C2 manifest is accepted by the launch consumer' ($validated.Manifest.schemaVersion -eq 1 -and $validated.Manifest.completionStatus -eq 'complete' -and $validated.Manifest.runId -eq $first.RunId)
    Assert-True 'run workspaces and output directories are unique' ($first.WorkspacePath -ne $second.WorkspacePath -and (Join-Path $first.WorkspacePath 'output') -ne (Join-Path $second.WorkspacePath 'output'))
    Assert-True 'per-run screenshots directory is created within the validated workspace' (Test-Path -LiteralPath (Join-Path $first.WorkspacePath 'output\screenshots') -PathType Container)
    Assert-True 'workspace ownership marker and completion marker validate' ($validated.Manifest.ownerToken -and (Test-Path -LiteralPath (Join-Path $first.WorkspacePath '.complete')))

    $savedManifest=[IO.File]::ReadAllBytes($first.ManifestPath)
    $tampered=$validated.Manifest
    $tampered.completionStatus='staging'; Write-TestJson $first.ManifestPath $tampered
    [void](Assert-Throws 'incomplete manifest is rejected before any launch argument is produced' { Get-QacS22R10ValidatedManifest -ManifestPath $first.ManifestPath } 'complete')
    [IO.File]::WriteAllBytes($first.ManifestPath,$savedManifest)
    $validated=Get-QacS22R10ValidatedManifest -ManifestPath $first.ManifestPath

    $tampered=$validated.Manifest
    ($tampered.artifacts | Where-Object name -eq 'esp').privatePath=$sources.Esp
    Write-TestJson $first.ManifestPath $tampered
    [void](Assert-Throws 'tampered private ESP path escaping the workspace is rejected' { Get-QacS22R10ValidatedManifest -ManifestPath $first.ManifestPath } 'escape|workspace')
    [IO.File]::WriteAllBytes($first.ManifestPath,$savedManifest)
    $validated=Get-QacS22R10ValidatedManifest -ManifestPath $first.ManifestPath

    $port=Get-QacS22R10QmpPortCandidate
    $args=New-QacS22R10QemuArguments -ValidatedManifest $validated -QemuPath 'C:\Program Files\qemu\qemu-system-x86_64.exe' -QmpPort $port
    $writable=@($args | Where-Object {$_ -match '(?i)fat:rw:'})
    Assert-True 'only the run-private ESP is selected in the actual writable FAT argument' ($writable.Count -eq 1 -and $writable[0] -like "*fat:rw:$($validated.Esp.privatePath)*" -and $writable[0] -notlike "*fat:rw:$($sources.Esp)*")
    $writableJoined = $writable -join ' '
    Assert-True 'canonical ESP is never selected as a writable launch input' ($writableJoined.IndexOf([string]$sources.Esp,[StringComparison]::OrdinalIgnoreCase) -lt 0)
    Assert-True 'read-only ISO and firmware identities remain in the manifest' (($validated.Iso.privateSha256 -eq $sourceIso.Sha256) -and ($validated.Ovmf.privateSha256 -eq $sourceOvmf.Sha256) -and $args -contains "if=pflash,format=raw,readonly=on,file=$($sources.Ovmf)")
    Assert-True 'QEMU retains historical machine, CPU, memory, display, and network configuration' ($args -contains 'pc,accel=whpx' -and $args -contains '2' -and $args -contains '2048M' -and $args -contains '-vga' -and $args -contains '-net' -and $args -contains 'none')
    Assert-True 'QEMU argument vector uses a unique loopback endpoint and excludes fixed port 65253' ($args -contains "tcp:127.0.0.1:$port,server=on,wait=off" -and $port -ne 65253 -and ($args -join ' ') -notmatch '65253')
    Assert-True 'QEMU is paused until QMP ownership is verified' ($args -contains '-S')
    Assert-True 'QMP argument remains unbound in the staging manifest' ($validated.Manifest.launchContract.qmp.port -eq $null)
    $screendumpCommand=New-QacS22R10ScreendumpCommand -Path 'C:\qac3c3 fixture with spaces\initial.ppm'
    Assert-True 'QMP screendump command normalizes Windows separators and quotes paths with spaces' ($screendumpCommand -eq 'screendump "C:/qac3c3 fixture with spaces/initial.ppm"')

    $tampered=$validated.Manifest
    $tampered.outputs.screenshots='..\..\shared-screenshots'
    Write-TestJson $first.ManifestPath $tampered
    [void](Assert-Throws 'screenshot output traversal is rejected before a QEMU request can be built' { Get-QacS22R10ValidatedManifest -ManifestPath $first.ManifestPath } 'output path')
    [IO.File]::WriteAllBytes($first.ManifestPath,$savedManifest)
    $validated=Get-QacS22R10ValidatedManifest -ManifestPath $first.ManifestPath

    $port2=Get-QacS22R10QmpPortCandidate -ExcludePort @($port)
    Assert-True 'endpoint candidates vary per run and avoid the historical fixed endpoint' ($port2 -ne $port -and $port2 -ne 65253)
    Assert-True 'randomized endpoint candidates stay in the dynamic loopback port range' ($port -ge 49152 -and $port -le 65535)

    $qacArgs=New-QacS22R10QacArguments -QemuPath 'C:\Program Files\qemu\qemu-system-x86_64.exe' -QemuArguments $args
    Assert-True 'QAC launch uses the required project and acceptance mode' (($qacArgs[0..5] -join ' ') -eq 'run --project guideXOSServer --mode acceptance --')
    Assert-True 'QEMU executable remains a single argument after the QAC separator' ($qacArgs[6] -eq 'C:\Program Files\qemu\qemu-system-x86_64.exe')
    Assert-True 'argument boundaries preserve a path containing spaces as one QEMU token' (@($qacArgs | Where-Object {$_ -like '*fat:rw:*'}).Count -eq 1 -and $qacArgs -contains $writable[0])
    Assert-True 'QAC does not fall back to an unmanaged QEMU argument vector' ($qacArgs[0] -eq 'run' -and $qacArgs -contains '--' -and $qacArgs[6] -like '*qemu-system-x86_64.exe')
    $runnerSource=Get-Content -LiteralPath (Join-Path $repoRoot 'scripts\run-qac-s22r10-uefi.ps1') -Raw
    Assert-True 'outer runner launches the QAC CLI process rather than starting QEMU directly' ($runnerSource -match '\$start\.FileName\s*=\s*\$script:QacCli' -and $runnerSource -notmatch 'Start-Process\s+\$qemu|ProcessStartInfo\s*\(\s*\$qemu')
    Assert-True 'existing guest Start-menu hotkey is available through the verified QMP operator path' ($runnerSource -match '\(meta_l\|meta_r\|ctrl-esc')

    $runnerTokens=$null; $runnerParseErrors=$null
    $runnerAst=[System.Management.Automation.Language.Parser]::ParseInput($runnerSource,[ref]$runnerTokens,[ref]$runnerParseErrors)
    $operatorAst=$runnerAst.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-GuestOperatorCommand'},$true)
    Assert-True 'the interactive screenshot operator is available for isolated failure-path verification' ($null -ne $operatorAst -and $runnerParseErrors.Count -eq 0)
    Invoke-Expression $operatorAst.Extent.Text
    $script:ScreenshotPath=Join-Path $first.OutputDirectory 'screenshots'
    $script:QmpClient=[PSCustomObject]@{fixture=$true}
    $script:Execution=[ordered]@{guestResult='INCOMPLETE';events=[System.Collections.Generic.List[object]]::new()}
    $script:QmpCommandCalls=0
    $script:LastQmpCommand=$null
    function Invoke-QacS22R10Qmp {
        param($Client,[string]$Execute,[hashtable]$Arguments,[int]$TimeoutMilliseconds)
        $script:QmpCommandCalls++
        $script:LastQmpCommand=[PSCustomObject]@{Execute=$Execute;Arguments=$Arguments}
        return 'fixture screendump failure'
    }
    [void](Assert-Throws 'unsafe screenshot labels that could traverse directories are rejected' { Invoke-GuestOperatorCommand 'screenshot ..\outside' } 'safe alphanumeric label')
    Assert-True 'unsafe screenshot labels are rejected before contacting QMP' ($script:QmpCommandCalls -eq 0)
    [void](Assert-Throws 'QMP screenshot failures remain explicit operator errors' { Invoke-GuestOperatorCommand 'screenshot initial' } 'screendump command failed')
    $expectedScreenshot=Join-Path $script:ScreenshotPath 'initial.ppm'
    $expectedScreenshotCommand=New-QacS22R10ScreendumpCommand -Path $expectedScreenshot
    Assert-True 'screenshot requests target the private per-run output directory with a quoted Windows path' ($script:LastQmpCommand.Execute -eq 'human-monitor-command' -and $script:LastQmpCommand.Arguments['command-line'] -eq $expectedScreenshotCommand -and $expectedScreenshot.StartsWith($first.WorkspacePath,[StringComparison]::OrdinalIgnoreCase))
    Assert-True 'failed screenshot request leaves guest acceptance incomplete and records no guest result event' ($script:Execution.guestResult -eq 'INCOMPLETE' -and $script:Execution.events.Count -eq 0)
    Remove-Item Function:\Invoke-QacS22R10Qmp -Force -ErrorAction SilentlyContinue
    Import-Module $launchPath -Force -ErrorAction Stop
    Remove-Item Function:\Invoke-GuestOperatorCommand -Force -ErrorAction SilentlyContinue
    $script:ScreenshotPath=$null; $script:QmpClient=$null; $script:Execution=$null; $script:QmpCommandCalls=$null; $script:LastQmpCommand=$null

    Assert-True 'coordinator online status is required for launch' (Test-QacS22R10CoordinatorOnline -StatusOutput "Coordinator: Online`nManaged sessions: 0" -ExitCode 0)
    Assert-True 'coordinator unavailability fails closed' (-not (Test-QacS22R10CoordinatorOnline -StatusOutput 'Coordinator: Unavailable' -ExitCode 2))
    Assert-True 'coordinator recovery state fails closed' (-not (Test-QacS22R10CoordinatorOnline -StatusOutput "Coordinator: Online`nRecovery: Required" -ExitCode 0))
    Assert-True 'queued QAC request can be cancelled without launching QEMU' (Test-QacS22R10QueuedCancellationNeeded -ReservationState 'Waiting' -CancellationRequested $true)
    Assert-True 'blocked acceptance request can be cancelled while queued' (Test-QacS22R10QueuedCancellationNeeded -ReservationState 'Blocked' -CancellationRequested $true)
    Assert-True 'running QAC reservation cannot be cancelled through queued path' (-not (Test-QacS22R10QueuedCancellationNeeded -ReservationState 'Running' -CancellationRequested $true))
    Assert-True 'no cancellation request leaves a queued reservation intact' (-not (Test-QacS22R10QueuedCancellationNeeded -ReservationState 'Waiting' -CancellationRequested $false))

    $qacSource=Get-Content -LiteralPath 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\Program.cs' -Raw
    $grantCheck=$qacSource.IndexOf('reservation.State != "Granted"',[StringComparison]::Ordinal)
    $startChild=$qacSource.IndexOf('Process.Start(start)',[StringComparison]::Ordinal)
    Assert-True 'existing QAC CLI refuses to create a QEMU child before a Granted reservation' ($grantCheck -ge 0 -and $startChild -gt $grantCheck)

    $collision='QEMU: Failed to bind socket: Only one usage of each socket address is normally permitted (10048)'
    Assert-True 'verified QMP port collision is retryable before guest progress and after QAC release' (Test-QacS22R10RetryablePortCollision -Output $collision -ChildExitVerified $true -ReservationReleased $true -GuestProgressObserved $false)
    Assert-True 'port is not retried while child exit remains uncertain' (-not (Test-QacS22R10RetryablePortCollision -Output $collision -ChildExitVerified $false -ReservationReleased $true -GuestProgressObserved $false))
    Assert-True 'port is not retried before reservation release' (-not (Test-QacS22R10RetryablePortCollision -Output $collision -ChildExitVerified $true -ReservationReleased $false -GuestProgressObserved $false))
    Assert-True 'port is not retried after guest progress' (-not (Test-QacS22R10RetryablePortCollision -Output $collision -ChildExitVerified $true -ReservationReleased $true -GuestProgressObserved $true))
    Assert-True 'generic QEMU startup failures are not misclassified as QMP port collisions' (-not (Test-QacS22R10RetryablePortCollision -Output 'Could not load firmware image' -ChildExitVerified $true -ReservationReleased $true -GuestProgressObserved $false))

    $processFixture=[PSCustomObject]@{ProcessId=42001;ParentProcessId=42000;ExecutablePath='C:\Program Files\qemu\qemu-system-x86_64.exe';CreationDate=[Management.ManagementDateTimeConverter]::ToDmtfDateTime([DateTime]::UtcNow)}
    $startFixture=[DateTimeOffset]::UtcNow
    Assert-True 'direct-child identity includes the expected PID, parent, executable, and start time' (Test-QacS22R10DirectChildIdentity -ProcessRecord $processFixture -ExpectedPid 42001 -ExpectedParentPid 42000 -ExpectedExecutable 'C:\Program Files\qemu\qemu-system-x86_64.exe' -ExpectedStartUtc $startFixture -StartToleranceSeconds 2)
    $typedDateFixture=[PSCustomObject]@{ProcessId=42001;ParentProcessId=42000;ExecutablePath='C:\Program Files\qemu\qemu-system-x86_64.exe';CreationDate=[DateTime]::UtcNow}
    Assert-True 'direct-child identity accepts the typed DateTime returned by Get-CimInstance' (Test-QacS22R10DirectChildIdentity -ProcessRecord $typedDateFixture -ExpectedPid 42001 -ExpectedParentPid 42000 -ExpectedExecutable 'C:\Program Files\qemu\qemu-system-x86_64.exe' -ExpectedStartUtc ([DateTimeOffset]::UtcNow) -StartToleranceSeconds 2)
    Assert-True 'wrong QAC parent identity is rejected' (-not (Test-QacS22R10DirectChildIdentity -ProcessRecord $processFixture -ExpectedPid 42001 -ExpectedParentPid 9 -ExpectedExecutable 'C:\Program Files\qemu\qemu-system-x86_64.exe' -ExpectedStartUtc $startFixture))
    $inspectFixture=[PSCustomObject]@{project='guideXOSServer';mode='acceptance';state='Running';processId=42001;processStartTimeUtc=$startFixture.ToString('O')}
    Assert-True 'actual qac inspect processId field confirms the expected reservation and child identity' (Test-QacS22R10ReservationIdentity -Reservation $inspectFixture -ExpectedProject 'guideXOSServer' -ExpectedMode 'acceptance' -ExpectedState 'Running' -ExpectedChildProcessId 42001)
    Assert-True 'qac inspect reservation for another process is rejected' (-not (Test-QacS22R10ReservationIdentity -Reservation $inspectFixture -ExpectedProject 'guideXOSServer' -ExpectedMode 'acceptance' -ExpectedState 'Running' -ExpectedChildProcessId 42002))
    $connection=[PSCustomObject]@{State='Listen';LocalAddress='127.0.0.1';LocalPort=50001;OwningProcess=42001}
    Assert-True 'QMP listener ownership requires the exact QEMU child PID' (Test-QacS22R10QmpListenerOwner -Connections @($connection) -Port 50001 -ExpectedProcessId 42001)
    Assert-True 'QMP listener owned by another PID is rejected' (-not (Test-QacS22R10QmpListenerOwner -Connections @($connection) -Port 50001 -ExpectedProcessId 42002))
    Assert-True 'QMP listener on a non-loopback address is rejected' (-not (Test-QacS22R10QmpListenerOwner -Connections @([PSCustomObject]@{State='Listen';LocalAddress='0.0.0.0';LocalPort=50001;OwningProcess=42001}) -Port 50001 -ExpectedProcessId 42001))

    Assert-True 'marker observed before its deadline is accepted' (Test-QacS22R10MarkerWithinDeadline -ObservedAtUtc ([DateTimeOffset]::UtcNow.AddSeconds(-2)) -DeadlineUtc ([DateTimeOffset]::UtcNow.AddSeconds(2)))
    Assert-True 'marker first observed after its deadline is rejected' (-not (Test-QacS22R10MarkerWithinDeadline -ObservedAtUtc ([DateTimeOffset]::UtcNow.AddSeconds(2)) -DeadlineUtc ([DateTimeOffset]::UtcNow.AddSeconds(-2))))
    Assert-True 'existing PacMan guest FAIL evidence is preserved as FAIL' ((Get-QacS22R10GuestResult -ObservedSerialLines @('[NATIVE-ELF] discovery result=NO_APPS_DIRECTORY')).Result -eq 'FAIL')
    Assert-True 'missing PacMan gameplay marker remains incomplete' ((Get-QacS22R10GuestResult -ObservedSerialLines @('[NAVIGATOR-SMOKE] result=PASS')).Result -eq 'INCOMPLETE')
    Assert-True 'an existing PacMan guest PASS marker is classified separately' ((Get-QacS22R10GuestResult -ObservedSerialLines @('[PACMAN-SMOKE] result=PASS')).Result -eq 'PASS')
    Assert-True 'child launch failure can be classified without claiming guest acceptance' ((Get-QacS22R10GuestResult -ObservedSerialLines @()).Result -eq 'INCOMPLETE')

    $goodServer=[Qac3c3QmpFixtureServer]::new('guideXOS-fixture-run',$false)
    $qmpLog=Join-Path $first.OutputDirectory 'qmp-fixture-transcript.jsonl'
    Assert-True 'QMP client emits JSON as UTF-8 without a byte-order mark' ($launchModuleSource -match 'StreamWriter\]::new\(\$stream,\s*\[Text\.UTF8Encoding\]::new\(\$false\)')
    $qmpClient=Open-QacS22R10QmpClient -Port $goodServer.Port -TranscriptPath $qmpLog -ConnectTimeoutMilliseconds 2000 -ReadTimeoutMilliseconds 1000
    $qmpIdentity=Initialize-QacS22R10QmpClient -Client $qmpClient -ExpectedName 'guideXOS-fixture-run'
    Assert-True 'valid QMP greeting and capabilities negotiation are accepted' ($qmpClient.Greeting.QMP.version.qemu.major -eq 11 -and $qmpIdentity.Name -eq 'guideXOS-fixture-run')
    Assert-True 'QMP identity remains paused before guest execution' ($qmpIdentity.Status -eq 'prelaunch' -and -not $qmpIdentity.Running)
    $quitResponse=Invoke-QacS22R10Qmp -Client $qmpClient -Execute 'quit'
    Assert-True 'safe QMP quit requires and receives a matching command response' ($null -ne $quitResponse)
    Close-QacS22R10QmpClient -Client $qmpClient; $qmpClient=$null; $goodServer.Dispose(); $goodServer=$null

    $wrongServer=[Qac3c3QmpFixtureServer]::new('other-run',$false)
    $qmpClient=Open-QacS22R10QmpClient -Port $wrongServer.Port -TranscriptPath $qmpLog -ConnectTimeoutMilliseconds 2000 -ReadTimeoutMilliseconds 1000
    [void](Assert-Throws 'wrong QMP instance identity is rejected after capability negotiation' { Initialize-QacS22R10QmpClient -Client $qmpClient -ExpectedName 'guideXOS-our-run' } 'mismatch')
    Close-QacS22R10QmpClient -Client $qmpClient; $qmpClient=$null; $wrongServer.Dispose(); $wrongServer=$null

    $silentServer=[Qac3c3QmpFixtureServer]::new('silent',$true)
    [void](Assert-Throws 'silent or unresponsive QMP endpoint is rejected within the read timeout' { Open-QacS22R10QmpClient -Port $silentServer.Port -TranscriptPath $qmpLog -ConnectTimeoutMilliseconds 2000 -ReadTimeoutMilliseconds 150 } 'timed out|closed|failed')
    $silentServer.Dispose(); $silentServer=$null

    Assert-True 'uncertain active execution is never cleanup eligible' (-not (Test-QacS22R10CleanupEligible -ReservationState 'Recovery Required' -ChildWasLaunched $true -ChildExitVerified $false -ReservationReleaseVerified $false))
    Assert-True 'QAC active state is not cleanup eligible' (-not (Test-QacS22R10CleanupEligible -ReservationState 'Running' -ChildWasLaunched $true -ChildExitVerified $false -ReservationReleaseVerified $false))
    Assert-True 'verified QAC release and exact child exit permit guarded cleanup' (Test-QacS22R10CleanupEligible -ReservationState 'Released' -ChildWasLaunched $true -ChildExitVerified $true -ReservationReleaseVerified $true)

    $activeManifest=$validated.Manifest; $activeManifest.qacExecution.status='unknown'; Write-TestJson $first.ManifestPath $activeManifest
    [void](Assert-Throws 'Recovery Required workspace state blocks the existing QAC cleanup guard' { Remove-QacUefiArtifactWorkspace -ManifestPath $first.ManifestPath -RunId $first.RunId -Confirm:$false } 'active|uncertain')
    $activeManifest.qacExecution.status='active'; Write-TestJson $first.ManifestPath $activeManifest
    [void](Assert-Throws 'active workspace is refused by guarded cleanup' { Remove-QacUefiArtifactWorkspace -ManifestPath $first.ManifestPath -RunId $first.RunId -Confirm:$false } 'active|uncertain')
    $releasedManifest=(Get-QacS22R10ValidatedManifest -ManifestPath $second.ManifestPath).Manifest; $releasedManifest.qacExecution.status='released'; $releasedManifest.qacExecution.childExitVerified=$true; Write-TestJson $second.ManifestPath $releasedManifest
    $secondValidated=Get-QacS22R10ValidatedManifest -ManifestPath $second.ManifestPath
    Assert-True 'verified released manifest passes backward-compatible validation' ($secondValidated.Manifest.qacExecution.status -eq 'released' -and $secondValidated.Manifest.qacExecution.childExitVerified)

    Assert-True 'canonical ESP identity is unchanged after isolated staging' ((Get-QacS22R10ArtifactIdentity -Path $sources.Esp -Kind directory).Sha256 -eq $sourceEsp.Sha256)
    Assert-True 'read-only ISO identity is unchanged after isolated staging' ((Get-QacS22R10ArtifactIdentity -Path $sources.Iso -Kind file).Sha256 -eq $sourceIso.Sha256)
    Assert-True 'read-only OVMF identity is unchanged after isolated staging' ((Get-QacS22R10ArtifactIdentity -Path $sources.Ovmf -Kind file).Sha256 -eq $sourceOvmf.Sha256)

    $cliSource=$qacSource
    Assert-True 'QAC direct-child ownership implementation has not been replaced by an intermediate QEMU wrapper' ($cliSource -match 'new ProcessStartInfo\(executable\)' -and $cliSource -match 'ArgumentList\.Add\(argument\)')
    Assert-True 'S22R10 integration has no references to the existing x86 or Developer Studio launchers' ($runnerSource -notmatch 'run-qemu-x86|DeveloperStudio|smoke-compiler-bootstrap')

    $released=Remove-QacUefiArtifactWorkspace -ManifestPath $second.ManifestPath -RunId $second.RunId -Confirm:$false
    Assert-True 'guarded cleanup removes only the exact verified released fixture workspace' (-not (Test-Path -LiteralPath $second.WorkspacePath))
    Write-Host "QAC3C3 launcher contract checks passed: $script:checkCount/$script:checkCount"
} finally {
    Close-QacS22R10QmpClient -Client $qmpClient
    if ($goodServer) { $goodServer.Dispose() }
    if ($wrongServer) { $wrongServer.Dispose() }
    if ($silentServer) { $silentServer.Dispose() }
    if (-not $KeepTestArtifacts -and (Test-Path -LiteralPath $testRoot)) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    } elseif (Test-Path -LiteralPath $testRoot) {
        Write-Host "Fixture artifacts retained: $testRoot"
    }
}
