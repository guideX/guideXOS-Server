Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-QacS22R10FullPath {
    param([Parameter(Mandatory)][string]$Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Test-QacS22R10PathWithin {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Root)
    $fullPath = Get-QacS22R10FullPath $Path
    $fullRoot = (Get-QacS22R10FullPath $Root) + [IO.Path]::DirectorySeparatorChar
    return $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)
}

function Get-QacS22R10ArtifactIdentity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][ValidateSet('file','directory')][string]$Kind
    )

    $fullPath = Get-QacS22R10FullPath $Path
    if (-not (Test-Path -LiteralPath $fullPath)) { throw "Artifact does not exist: $fullPath" }
    $rootItem = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
    if (($Kind -eq 'file' -and $rootItem.PSIsContainer) -or ($Kind -eq 'directory' -and -not $rootItem.PSIsContainer)) {
        throw "Artifact kind mismatch for '$fullPath'. Expected $Kind."
    }

    if ($Kind -eq 'file') { $fileItems = @($rootItem) }
    else { $fileItems = @(Get-ChildItem -LiteralPath $fullPath -File -Recurse -Force -ErrorAction Stop) }
    if ($fileItems.Count -eq 0) { throw "Artifact contains no files: $fullPath" }
    foreach ($item in $fileItems) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Artifact contains a reparse point: $($item.FullName)" }
    }

    $records = New-Object 'System.Collections.Generic.List[object]'
    [int64]$totalBytes = 0
    foreach ($item in $fileItems) {
        $relative = if ($Kind -eq 'file') { [IO.Path]::GetFileName($fullPath) } else { $item.FullName.Substring($fullPath.Length).TrimStart([char[]]@('\','/')) }
        $records.Add([PSCustomObject]@{
            Path = [string]$relative.Replace('\','/')
            SizeBytes = [int64]$item.Length
            Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
        })
        $totalBytes += [int64]$item.Length
    }
    $recordArray = $records.ToArray()
    [Array]::Sort($recordArray, [System.Comparison[object]]{
        param($left, $right)
        [StringComparer]::OrdinalIgnoreCase.Compare([string]$left.Path, [string]$right.Path)
    })
    $identityText = ($recordArray | ForEach-Object { "{0}`t{1}`t{2}" -f $_.Path, $_.SizeBytes, $_.Sha256 }) -join "`n"
    $identitySha = if ($Kind -eq 'file') { [string]$recordArray[0].Sha256 } else {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($identityText))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    }
    return [PSCustomObject]@{ Path=$fullPath; Kind=$Kind; FileCount=$recordArray.Count; TotalBytes=$totalBytes; Sha256=$identitySha; Files=@($recordArray) }
}

function Get-QacS22R10ValidatedManifest {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ManifestPath)

    $manifest = Test-QacUefiArtifactManifest -ManifestPath $ManifestPath
    if ($manifest.project -ne 'guideXOSServer' -or $manifest.selectedWorkflow -ne 's22r10-pacman-uefi-qac') {
        throw 'Manifest project or workflow does not identify the S22R10 PacMan UEFI acceptance run.'
    }
    if ($manifest.completionStatus -ne 'complete' -or $manifest.schemaVersion -ne 1) { throw 'Manifest is incomplete or has an unsupported schema.' }

    $esp = @($manifest.artifacts | Where-Object { $_.name -eq 'esp' })
    $iso = @($manifest.artifacts | Where-Object { $_.name -eq 'release-iso' })
    $ovmf = @($manifest.artifacts | Where-Object { $_.name -eq 'ovmf-firmware' })
    if ($esp.Count -ne 1 -or $iso.Count -ne 1 -or $ovmf.Count -ne 1 -or @($manifest.artifacts).Count -ne 3) {
        throw 'Manifest must contain exactly one private ESP, one shared ISO, and one shared OVMF image.'
    }
    if ($esp[0].kind -ne 'directory' -or $esp[0].access -ne 'writable' -or $esp[0].disposition -ne 'copy') {
        throw 'The QEMU writable FAT input must be a private copied ESP directory.'
    }
    foreach ($readOnlyArtifact in @($iso[0],$ovmf[0])) {
        if ($readOnlyArtifact.kind -ne 'file' -or $readOnlyArtifact.access -ne 'read-only' -or $readOnlyArtifact.disposition -ne 'share') {
            throw "Artifact '$($readOnlyArtifact.name)' must remain a verified shared read-only file."
        }
        if ([string]$readOnlyArtifact.privatePath -cne [string]$readOnlyArtifact.sourcePath) {
            throw "Shared read-only artifact '$($readOnlyArtifact.name)' path differs from its verified source."
        }
    }

    $workspace = Get-QacS22R10FullPath ([string]$manifest.workspacePath)
    $outputs = Get-QacS22R10FullPath (Join-Path $workspace ([string]$manifest.outputs.directory))
    if (-not (Test-QacS22R10PathWithin -Path $outputs -Root $workspace)) { throw 'Output directory escapes the private workspace.' }
    foreach ($relative in @($manifest.outputs.serialLog,$manifest.outputs.qemuDebugLog,$manifest.outputs.qmpTranscript,$manifest.outputs.screenshots)) {
        if ([IO.Path]::IsPathRooted([string]$relative) -or [string]$relative -match '(^|[\\/])\.\.([\\/]|$)') { throw "Output path escapes the private workspace: $relative" }
        $resolved = Get-QacS22R10FullPath (Join-Path $workspace ([string]$relative).Replace('/','\'))
        if (-not (Test-QacS22R10PathWithin -Path $resolved -Root $workspace)) { throw "Output path escapes the private workspace: $relative" }
        if (-not ([string]$relative -match '\.[A-Za-z0-9]+$')) {
            if (-not (Test-Path -LiteralPath $resolved -PathType Container)) { [void][IO.Directory]::CreateDirectory($resolved) }
            if (-not (Test-Path -LiteralPath $resolved -PathType Container)) { throw "Required per-run output directory is missing: $resolved" }
        } elseif (Test-Path -LiteralPath $resolved -PathType Container) {
            throw "Manifest output file path resolves to a directory: $resolved"
        }
    }

    $espSource = Get-QacS22R10ArtifactIdentity -Path ([string]$esp[0].sourcePath) -Kind directory
    $espPrivate = Get-QacS22R10ArtifactIdentity -Path ([string]$esp[0].privatePath) -Kind directory
    if ($espSource.Sha256 -ne [string]$esp[0].sourceSha256 -or $espPrivate.Sha256 -ne [string]$esp[0].privateSha256 -or $espPrivate.Sha256 -ne $espSource.Sha256) {
        throw 'Canonical and private ESP identities do not match the completed manifest.'
    }
    foreach ($artifact in @($iso[0],$ovmf[0])) {
        $identity = Get-QacS22R10ArtifactIdentity -Path ([string]$artifact.privatePath) -Kind file
        if ($identity.Sha256 -ne [string]$artifact.sourceSha256 -or $identity.Sha256 -ne [string]$artifact.privateSha256) {
            throw "Shared artifact '$($artifact.name)' no longer matches the staged identity."
        }
    }

    $bootFile = Join-Path ([string]$esp[0].privatePath) 'EFI\BOOT\BOOTX64.EFI'
    if (-not (Test-Path -LiteralPath $bootFile -PathType Leaf)) { throw 'Private workspace is missing EFI/BOOT/BOOTX64.EFI.' }
    return [PSCustomObject]@{ Manifest=$manifest; Esp=$esp[0]; Iso=$iso[0]; Ovmf=$ovmf[0]; Workspace=$workspace; Outputs=$outputs }
}

function Get-QacS22R10QmpPortCandidate {
    [CmdletBinding()]
    param([int[]]$ExcludePort = @())
    for ($attempt = 0; $attempt -lt 128; $attempt++) {
        $port = [Security.Cryptography.RandomNumberGenerator]::GetInt32(49152, 65536)
        if ($port -ne 65253 -and $port -notin $ExcludePort) { return $port }
    }
    throw 'Could not allocate a unique randomized loopback QMP candidate.'
}

function New-QacS22R10QacArguments {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$QemuPath, [Parameter(Mandatory)][string[]]$QemuArguments)
    $arguments = [System.Collections.Generic.List[string]]::new()
    foreach ($argument in @('run','--project','guideXOSServer','--mode','acceptance','--',[IO.Path]::GetFullPath($QemuPath))) { $arguments.Add([string]$argument) }
    foreach ($argument in $QemuArguments) { $arguments.Add([string]$argument) }
    return $arguments.ToArray()
}

function Test-QacS22R10CoordinatorOnline {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$StatusOutput, [Parameter(Mandatory)][int]$ExitCode)
    return $ExitCode -eq 0 -and $StatusOutput -match '(?m)^Coordinator: Online\s*$' -and $StatusOutput -notmatch '(?i)Recovery:\s*Required'
}

function Test-QacS22R10QueuedCancellationNeeded {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ReservationState, [Parameter(Mandatory)][bool]$CancellationRequested)
    return $CancellationRequested -and $ReservationState -in @('Waiting','Blocked')
}

function New-QacS22R10QemuArguments {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$ValidatedManifest,
        [Parameter(Mandatory)][string]$QemuPath,
        [Parameter(Mandatory)][ValidateRange(49152,65535)][int]$QmpPort
    )

    $manifest = $ValidatedManifest.Manifest
    $arguments = [System.Collections.Generic.List[string]]::new()
    $replace = [ordered]@{
        '{ovmfFirmware}' = [string]$ValidatedManifest.Ovmf.privatePath
        '{esp}' = [string]$ValidatedManifest.Esp.privatePath
        '{iso}' = [string]$ValidatedManifest.Iso.privatePath
        '{serialLog}' = (Get-QacS22R10FullPath (Join-Path $ValidatedManifest.Workspace ([string]$manifest.outputs.serialLog).Replace('/','\')))
        '{qemuDebugLog}' = (Get-QacS22R10FullPath (Join-Path $ValidatedManifest.Workspace ([string]$manifest.outputs.qemuDebugLog).Replace('/','\')))
        '{QAC3C3-allocated-port}' = [string]$QmpPort
        '{runId}' = [string]$manifest.runId
    }
    foreach ($templateArgument in @($manifest.launchContract.arguments)) {
        $argument = [string]$templateArgument
        foreach ($placeholder in $replace.Keys) { $argument = $argument.Replace([string]$placeholder,[string]$replace[$placeholder]) }
        if ($argument -match '\{[^}]+\}') { throw "Unresolved QEMU argument placeholder: $argument" }
        $arguments.Add($argument)
    }
    if ($arguments -notcontains '-S') { $arguments.Add('-S') }
    $qmpArguments = @($arguments | Where-Object { $_ -match '^tcp:127\.0\.0\.1:' })
    $expectedQmpArgument = 'tcp:127.0.0.1:{0},server=on,wait=off' -f $QmpPort
    if ($qmpArguments.Count -ne 1 -or $qmpArguments[0] -ne $expectedQmpArgument) { throw 'QEMU QMP argument is not bound to this run loopback candidate.' }
    if ($QmpPort -eq 65253 -or $qmpArguments[0] -match '65253') { throw 'The historical fixed QMP endpoint is forbidden.' }

    $writableFatArguments = @($arguments | Where-Object { $_ -match '(?i)fat:rw:' })
    if ($writableFatArguments.Count -ne 1 -or $writableFatArguments[0] -notlike "*fat:rw:$($ValidatedManifest.Esp.privatePath)*") {
        throw 'QEMU writable FAT argument does not select the manifest private ESP.'
    }
    if ($writableFatArguments[0].IndexOf([string]$ValidatedManifest.Esp.sourcePath,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw 'Canonical ESP path appears in the writable FAT argument.'
    }
    $joined = $arguments -join "`n"
    if ($joined.IndexOf([string]$ValidatedManifest.Iso.privatePath,[StringComparison]::OrdinalIgnoreCase) -lt 0 -or $joined -notmatch '(?i)media=cdrom,readonly=on') {
        throw 'Expected read-only ISO selection is missing from QEMU arguments.'
    }
    if ($joined.IndexOf([string]$ValidatedManifest.Ovmf.privatePath,[StringComparison]::OrdinalIgnoreCase) -lt 0 -or $joined -notmatch '(?i)if=pflash,format=raw,readonly=on,file=') {
        throw 'Expected read-only OVMF selection is missing from QEMU arguments.'
    }
    foreach ($requiredPair in @(@('-machine','pc,accel=whpx'),@('-smp','2'),@('-m','2048M'),@('-vga','std'),@('-display','none'),@('-net','none'),@('-no-reboot'),@('-no-shutdown'),@('-monitor','none'))) {
        if (-not $arguments.Contains($requiredPair[0]) -or ($requiredPair.Count -gt 1 -and -not $arguments.Contains($requiredPair[1]))) {
            throw "Historical S22R10 QEMU configuration token is missing: $($requiredPair -join ' ')"
        }
    }
    return $arguments.ToArray()
}

function Test-QacS22R10DirectChildIdentity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$ProcessRecord,
        [Parameter(Mandatory)][int]$ExpectedPid,
        [Parameter(Mandatory)][int]$ExpectedParentPid,
        [Parameter(Mandatory)][string]$ExpectedExecutable,
        [Parameter(Mandatory)][DateTimeOffset]$ExpectedStartUtc,
        [int]$StartToleranceSeconds = 3
    )
    if ([int]$ProcessRecord.ProcessId -ne $ExpectedPid -or [int]$ProcessRecord.ParentProcessId -ne $ExpectedParentPid) { return $false }
    if ([string]$ProcessRecord.ExecutablePath -ine [IO.Path]::GetFullPath($ExpectedExecutable)) { return $false }
    try {
        $creationDate = $ProcessRecord.CreationDate
        if ($creationDate -is [DateTimeOffset]) { $actualStart = $creationDate.UtcDateTime }
        elseif ($creationDate -is [DateTime]) { $actualStart = $creationDate.ToUniversalTime() }
        else { $actualStart = [Management.ManagementDateTimeConverter]::ToDateTime([string]$creationDate).ToUniversalTime() }
        $difference = [Math]::Abs(($actualStart - $ExpectedStartUtc.UtcDateTime).TotalSeconds)
        return $difference -le $StartToleranceSeconds
    } catch { return $false }
}

function Test-QacS22R10ReservationIdentity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Reservation,
        [Parameter(Mandatory)][string]$ExpectedProject,
        [Parameter(Mandatory)][string]$ExpectedMode,
        [Parameter(Mandatory)][string]$ExpectedState,
        [Parameter(Mandatory)][int]$ExpectedChildProcessId
    )
    $reportedChildPid = if ($null -ne $Reservation.processId) { [int]$Reservation.processId } else { [int]$Reservation.childProcessId }
    return ([string]$Reservation.project -ceq $ExpectedProject) -and
        ([string]$Reservation.mode -ceq $ExpectedMode) -and
        ([string]$Reservation.state -ceq $ExpectedState) -and
        ($reportedChildPid -eq $ExpectedChildProcessId)
}

function New-QacS22R10ScreendumpCommand {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)
    $fullPath = Get-QacS22R10FullPath $Path
    return 'screendump "' + $fullPath.Replace('\','/') + '"'
}

function Test-QacS22R10QmpListenerOwner {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object[]]$Connections,
        [Parameter(Mandatory)][int]$Port,
        [Parameter(Mandatory)][int]$ExpectedProcessId
    )
    $listeners = @($Connections | Where-Object { $_.State -eq 'Listen' -and [int]$_.LocalPort -eq $Port -and [string]$_.LocalAddress -eq '127.0.0.1' })
    return $listeners.Count -eq 1 -and [int]$listeners[0].OwningProcess -eq $ExpectedProcessId
}

function Test-QacS22R10RetryablePortCollision {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Output,
        [Parameter(Mandatory)][bool]$ChildExitVerified,
        [Parameter(Mandatory)][bool]$ReservationReleased,
        [Parameter(Mandatory)][bool]$GuestProgressObserved
    )
    $collision = $Output -match '(?i)(address already in use|only one usage of each socket address|WSAEADDRINUSE|error\s*10048)'
    return $collision -and $ChildExitVerified -and $ReservationReleased -and -not $GuestProgressObserved
}

function Test-QacS22R10MarkerWithinDeadline {
    [CmdletBinding()]
    param([Parameter(Mandatory)][DateTimeOffset]$ObservedAtUtc, [Parameter(Mandatory)][DateTimeOffset]$DeadlineUtc)
    return $ObservedAtUtc -le $DeadlineUtc
}

function Get-QacS22R10GuestResult {
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyCollection()][string[]]$ObservedSerialLines)
    $lines = $ObservedSerialLines -join "`n"
    if ($lines -match '(?i)discovery result=NO_APPS_DIRECTORY') { return [PSCustomObject]@{ Result='FAIL'; Reason='Guest App Model rediscovery reported NO_APPS_DIRECTORY.' } }
    if ($lines -match '(?i)\[PACMAN-SMOKE\]\s+result=PASS') { return [PSCustomObject]@{ Result='PASS'; Reason='Existing PacMan guest PASS marker observed live.' } }
    if ($lines -match '(?i)\[PACMAN-SMOKE\]\s+result=FAIL') { return [PSCustomObject]@{ Result='FAIL'; Reason='Existing PacMan guest FAIL marker observed live.' } }
    return [PSCustomObject]@{ Result='INCOMPLETE'; Reason='No existing PacMan gameplay PASS/FAIL marker was observed.' }
}

function Test-QacS22R10CleanupEligible {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ReservationState,
        [Parameter(Mandatory)][bool]$ChildWasLaunched,
        [Parameter(Mandatory)][bool]$ChildExitVerified,
        [Parameter(Mandatory)][bool]$ReservationReleaseVerified
    )
    if ($ChildWasLaunched) { return $ChildExitVerified -and $ReservationReleaseVerified -and $ReservationState -eq 'Released' }
    return $ReservationState -in @('Cancelled','Expired','Failed','Released') -and $ReservationReleaseVerified
}

function Get-QacS22R10QmpResponse {
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Response, [Parameter(Mandatory)][int]$ExpectedId)
    $properties = @($Response.PSObject.Properties.Name)
    if ($properties -notcontains 'id') {
        if ($properties -contains 'error') { throw "Uncorrelated QMP error: $($Response.error | ConvertTo-Json -Compress -Depth 8)" }
        throw 'QMP response did not contain a command ID.'
    }
    if ([int]$Response.id -ne $ExpectedId) { throw "QMP response ID mismatch: expected $ExpectedId." }
    if ($Response.PSObject.Properties.Name -contains 'error') { throw "QMP command failed: $($Response.error | ConvertTo-Json -Compress -Depth 8)" }
    if (-not ($Response.PSObject.Properties.Name -contains 'return')) { throw 'QMP response did not contain return or error.' }
    return $Response.return
}

function Write-QacS22R10QmpRecord {
    param([Parameter(Mandatory)][string]$TranscriptPath, [Parameter(Mandatory)][string]$Direction, [Parameter(Mandatory)][string]$Line)
    $entry = [ordered]@{ timestampUtc=[DateTimeOffset]::UtcNow.ToString('O'); direction=$Direction; line=$Line }
    [IO.File]::AppendAllText($TranscriptPath, (ConvertTo-Json -InputObject $entry -Compress -Depth 8) + "`n", (New-Object Text.UTF8Encoding($false)))
}

function Read-QacS22R10QmpMessage {
    param([Parameter(Mandatory)]$Client)
    try { $line = $Client.Reader.ReadLine() } catch [IO.IOException] { throw 'QMP read timed out or failed.' }
    if ([string]::IsNullOrWhiteSpace($line)) { throw 'QMP connection closed before a response.' }
    Write-QacS22R10QmpRecord -TranscriptPath $Client.TranscriptPath -Direction 'RX' -Line $line
    try { return ($line | ConvertFrom-Json -ErrorAction Stop) } catch { throw "Malformed QMP JSON: $line" }
}

function Open-QacS22R10QmpClient {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][ValidateRange(49152,65535)][int]$Port,
        [Parameter(Mandatory)][string]$TranscriptPath,
        [int]$ConnectTimeoutMilliseconds = 5000,
        [int]$ReadTimeoutMilliseconds = 5000
    )
    if (-not (Test-Path -LiteralPath (Split-Path -Parent $TranscriptPath) -PathType Container)) { throw 'QMP transcript directory does not exist.' }
    $tcp = [Net.Sockets.TcpClient]::new()
    try {
        $connect = $tcp.ConnectAsync([Net.IPAddress]::Loopback, $Port)
        if (-not $connect.Wait($ConnectTimeoutMilliseconds)) { throw 'QMP connection timed out.' }
        if ($connect.IsFaulted) { throw $connect.Exception.GetBaseException() }
        $stream = $tcp.GetStream()
        $stream.ReadTimeout = $ReadTimeoutMilliseconds
        $stream.WriteTimeout = $ReadTimeoutMilliseconds
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $false, 4096, $true)
        $writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false), 4096, $true)
        $writer.NewLine = "`n"
        $writer.AutoFlush = $true
        $client = [PSCustomObject]@{ Tcp=$tcp; Stream=$stream; Reader=$reader; Writer=$writer; TranscriptPath=$TranscriptPath; NextId=1 }
        $greetingLine = $reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($greetingLine)) { throw 'QMP server closed before sending its greeting.' }
        Write-QacS22R10QmpRecord -TranscriptPath $TranscriptPath -Direction 'RX' -Line $greetingLine
        try { $greeting = $greetingLine | ConvertFrom-Json -ErrorAction Stop } catch { throw 'QMP greeting is malformed.' }
        if (-not ($greeting.PSObject.Properties.Name -contains 'QMP')) { throw 'Endpoint did not send a QMP greeting.' }
        $client | Add-Member -NotePropertyName Greeting -NotePropertyValue $greeting
        return $client
    } catch {
        $tcp.Dispose()
        throw
    }
}

function Invoke-QacS22R10Qmp {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Client,
        [Parameter(Mandatory)][string]$Execute,
        [hashtable]$Arguments,
        [int]$TimeoutMilliseconds = 5000
    )
    $id = [int]$Client.NextId
    $Client.NextId = $id + 1
    $request = [ordered]@{ execute=$Execute }
    if ($null -ne $Arguments) { $request.arguments = $Arguments }
    $request.id = $id
    $json = ConvertTo-Json -InputObject $request -Compress -Depth 12
    Write-QacS22R10QmpRecord -TranscriptPath $Client.TranscriptPath -Direction 'TX' -Line $json
    $Client.Stream.ReadTimeout = $TimeoutMilliseconds
    $Client.Stream.WriteTimeout = $TimeoutMilliseconds
    $Client.Writer.WriteLine($json)
    while ($true) {
        $response = Read-QacS22R10QmpMessage -Client $Client
        if (-not ($response.PSObject.Properties.Name -contains 'id')) {
            if ($response.PSObject.Properties.Name -contains 'error') { throw "Uncorrelated QMP error: $($response.error | ConvertTo-Json -Compress -Depth 8)" }
            continue
        }
        return Get-QacS22R10QmpResponse -Response $response -ExpectedId $id
    }
}

function Initialize-QacS22R10QmpClient {
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Client, [Parameter(Mandatory)][string]$ExpectedName)
    [void](Invoke-QacS22R10Qmp -Client $Client -Execute 'qmp_capabilities')
    $name = Invoke-QacS22R10Qmp -Client $Client -Execute 'query-name'
    if ([string]$name.name -cne $ExpectedName) { throw "QMP instance name mismatch: expected '$ExpectedName', received '$($name.name)'." }
    $status = Invoke-QacS22R10Qmp -Client $Client -Execute 'query-status'
    return [PSCustomObject]@{ Name=[string]$name.name; Status=[string]$status.status; Running=[bool]$status.running }
}

function Close-QacS22R10QmpClient {
    param($Client)
    if ($null -eq $Client) { return }
    try { $Client.Reader.Dispose() } catch { }
    try { $Client.Writer.Dispose() } catch { }
    try { $Client.Tcp.Dispose() } catch { }
}

Export-ModuleMember -Function Get-QacS22R10FullPath, Get-QacS22R10ArtifactIdentity, Get-QacS22R10ValidatedManifest, Get-QacS22R10QmpPortCandidate, New-QacS22R10QacArguments, Test-QacS22R10CoordinatorOnline, Test-QacS22R10QueuedCancellationNeeded, New-QacS22R10QemuArguments, Test-QacS22R10DirectChildIdentity, Test-QacS22R10ReservationIdentity, New-QacS22R10ScreendumpCommand, Test-QacS22R10QmpListenerOwner, Test-QacS22R10RetryablePortCollision, Test-QacS22R10MarkerWithinDeadline, Get-QacS22R10GuestResult, Test-QacS22R10CleanupEligible, Open-QacS22R10QmpClient, Invoke-QacS22R10Qmp, Initialize-QacS22R10QmpClient, Close-QacS22R10QmpClient
