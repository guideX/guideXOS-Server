param(
	[Parameter(Mandatory = $true)]
	[string]$EvidenceDirectory,
	[string]$Memory = "1024M",
	[int]$TimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
$QemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
if (-not [System.IO.Path]::IsPathRooted($EvidenceDirectory)) {
	$EvidenceDirectory = [System.IO.Path]::GetFullPath((Join-Path $RootDir $EvidenceDirectory))
} else {
	$EvidenceDirectory = [System.IO.Path]::GetFullPath($EvidenceDirectory)
}
$EspDir = Join-Path $EvidenceDirectory "ESP"
$Firmware = Join-Path $EvidenceDirectory "OVMF.fd"
$Kernel = Join-Path $EspDir "kernel.elf"
$Bootloader = Join-Path $EspDir "EFI\BOOT\BOOTX64.EFI"
$Ramdisk = Join-Path $EspDir "ramdisk.img"
$DemoManifest = Join-Path $EspDir "Apps\Font5GlyphCacheDemo\app.json"
$DemoElf = Join-Path $EspDir "Apps\Font5GlyphCacheDemo\bin\amd64\font5demo.elf"
$DemoFont = Join-Path $EspDir "Apps\Font5GlyphCacheDemo\resources\Roboto-Regular.ttf"
$SerialLog = Join-Path $EvidenceDirectory "guest-com1.serial.log"
$Screenshot = Join-Path $EvidenceDirectory "font5-desktop.ppm"
$LaunchRecord = Join-Path $EvidenceDirectory "qemu-launch.json"
$CensusPath = Join-Path $EvidenceDirectory "qemu-process-census.json"

foreach ($requiredFile in @($QemuExe, $Firmware, $Kernel, $Bootloader, $Ramdisk,
	$DemoManifest, $DemoElf, $DemoFont)) {
	if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
		throw "Required private FONT5 guest input is missing: $requiredFile"
	}
}
if (-not (Test-Path -LiteralPath $EspDir -PathType Container)) {
	throw "Private ESP directory is missing: $EspDir"
}
if ($TimeoutSeconds -lt 15 -or $TimeoutSeconds -gt 600) {
	throw "TimeoutSeconds must be between 15 and 600."
}

function Get-SerialText {
	if (-not (Test-Path -LiteralPath $SerialLog -PathType Leaf)) { return "" }
	# QEMU keeps COM1 open without sharing the handle with File.ReadAllText.
	# Get-Content opens the active log with read/write sharing so live markers
	# remain visible while the guest is running.
	try {
		$text = Get-Content -LiteralPath $SerialLog -Raw -ErrorAction Stop
		if ($null -eq $text) { return "" }
		return [string]$text
	}
	catch { return "" }
}

function Wait-SerialMarker([string]$Marker, [int]$Seconds) {
	$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
	while ([DateTime]::UtcNow -lt $deadline) {
		$text = Get-SerialText
		if ($text.Contains($Marker)) { return $text }
		if ($script:qemuProcess -and $script:qemuProcess.HasExited) {
			throw "QEMU exited before guest serial marker: $Marker"
		}
		if ($text.Contains("[FONT5] proof launch=FAIL")) {
			throw "Guest could not launch the FONT5 demo package."
		}
		if ($text.Contains("[FONT5] validated font load failed") -or
			$text.Contains("[FONT5] scalable draw failed")) {
			throw "Guest reported a FONT5 load/draw failure or bitmap fallback."
		}
		Start-Sleep -Milliseconds 250
	}
	throw "Timed out waiting for guest serial marker: $Marker"
}

function Invoke-QmpCommand($Client, $Reader, $Writer, [string]$Id, [string]$Json) {
	$Writer.WriteLine($Json)
	for ($i = 0; $i -lt 32; ++$i) {
		$line = $Reader.ReadLine()
		if ($null -eq $line) { throw "QMP connection closed before response to $Id." }
		if ($line.Contains('"id"') -and $line.Contains(('"' + $Id + '"'))) {
			if ($line.Contains('"error"')) { throw "QMP command $Id failed: $line" }
			return $line
		}
	}
	throw "QMP response was not received for $Id."
}

function Send-QmpHumanCommand($Client, $Reader, $Writer, [string]$Id, [string]$Command) {
	$payload = @{
		execute = "human-monitor-command"
		arguments = @{ "command-line" = $Command }
		id = $Id
	} | ConvertTo-Json -Compress -Depth 4
	return Invoke-QmpCommand $Client $Reader $Writer $Id $payload
}

function Stop-OwnedQemu($Process, [string]$PrivateRoot) {
	if (-not $Process) { return }
	$Process.Refresh()
	if ($Process.HasExited) { return }
	$current = Get-CimInstance Win32_Process -Filter ("ProcessId=" + $Process.Id) -ErrorAction SilentlyContinue
	if (-not $current) { return }
	if ([string]::IsNullOrWhiteSpace($current.ExecutablePath) -or
		[string]::IsNullOrWhiteSpace($current.CommandLine)) {
		$Process.Refresh()
		if ($Process.HasExited) { return }
		throw "Refusing to stop PID $($Process.Id): its QEMU ownership cannot be revalidated."
	}
	$resolvedExe = [System.IO.Path]::GetFullPath($current.ExecutablePath)
	$expectedExe = [System.IO.Path]::GetFullPath($QemuExe)
	if ($current.Name -ne "qemu-system-x86_64.exe" -or
		-not $resolvedExe.Equals($expectedExe, [System.StringComparison]::OrdinalIgnoreCase) -or
		-not $current.CommandLine.Contains($PrivateRoot)) {
		throw "Refusing to stop PID $($Process.Id): it no longer matches this FONT5 guest command line."
	}
	Stop-Process -Id $Process.Id -Force
	$Process.WaitForExit(10000) | Out-Null
}

$requestedMiB = 0
if ($Memory -match '^(?<amount>[0-9]+)M$') {
	$requestedMiB = [int]$Matches.amount
} elseif ($Memory -match '^(?<amount>[0-9]+)G$') {
	$requestedMiB = [int]$Matches.amount * 1024
} else {
	throw "Memory must be specified as an integer MiB or GiB value, for example 1024M."
}
if ($requestedMiB -lt 512 -or $requestedMiB -gt 4096) {
	throw "Memory must be between 512M and 4096M."
}

# Fresh process census immediately precedes resource selection and launch.
$foreignQemu = @(
	Get-CimInstance Win32_Process |
		Where-Object { $_.Name -like "qemu-system-*" -or $_.Name -like "qemu*" } |
		Select-Object ProcessId, Name, ExecutablePath, CommandLine
)
$os = Get-CimInstance Win32_OperatingSystem
$freeMiB = [int][Math]::Floor($os.FreePhysicalMemory / 1024)
$totalMiB = [int][Math]::Floor($os.TotalVisibleMemorySize / 1024)
$census = [ordered]@{
	capturedUtc = [DateTime]::UtcNow.ToString("o")
	totalPhysicalMiB = $totalMiB
	freePhysicalMiB = $freeMiB
	requestedGuestMemoryMiB = $requestedMiB
	foreignQemuProcesses = $foreignQemu
}
$census | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $CensusPath -Encoding utf8

# Each guest input and endpoint is private. Coexistence is allowed only when
# the private evidence path is not mentioned by an existing QEMU command and
# available host RAM leaves at least 512 MiB beyond this guest's allocation.
foreach ($processInfo in $foreignQemu) {
	if ($processInfo.CommandLine.Contains($EvidenceDirectory)) {
		throw "An existing QEMU process already references the FONT5 evidence path: PID $($processInfo.ProcessId)"
	}
}
if ($freeMiB -lt ($requestedMiB + 512)) {
	throw "Insufficient free physical RAM for isolated QEMU coexistence: free=${freeMiB}MiB guest=${requestedMiB}MiB margin=512MiB."
}

$portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$QmpPort = ([System.Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()

$arguments = @(
	"-machine", "pc,accel=whpx,usb=off",
	"-smp", "2",
	"-m", $Memory,
	"-vga", "std",
	"-display", "none",
	"-drive", "if=pflash,format=raw,readonly=on,file=$Firmware",
	"-drive", "file=fat:rw:$EspDir,format=raw,if=ide,index=0",
	"-serial", "file:$SerialLog",
	"-qmp", "tcp:127.0.0.1:$QmpPort,server=on,wait=off",
	"-monitor", "none",
	"-no-reboot",
	"-rtc", "base=utc,clock=host",
	"-net", "none"
)
$argumentLine = ($arguments | ForEach-Object {
	if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ }
}) -join ' '

Remove-Item -LiteralPath $SerialLog,$Screenshot,$LaunchRecord -Force -ErrorAction SilentlyContinue
$privateRootForCommand = $EvidenceDirectory
$startedUtc = [DateTime]::UtcNow.ToString("o")
$qemuProcess = $null
$qmpClient = $null
$proofPassed = $false
try {
	$qemuProcess = Start-Process -FilePath $QemuExe -ArgumentList $argumentLine -WindowStyle Hidden -PassThru
	$launch = [ordered]@{
		startedUtc = $startedUtc
		processId = $qemuProcess.Id
		executable = $QemuExe
		arguments = $arguments
		qmpPort = $QmpPort
		evidenceDirectory = $EvidenceDirectory
		firmware = $Firmware
		esp = $EspDir
		serialLog = $SerialLog
		screenshot = $Screenshot
		memory = $Memory
		machine = "pc,accel=whpx,usb=off"
		network = "disabled"
		foreignQemuProcessesObserved = $foreignQemu
		kernelSha256 = (Get-FileHash -LiteralPath $Kernel -Algorithm SHA256).Hash
		bootloaderSha256 = (Get-FileHash -LiteralPath $Bootloader -Algorithm SHA256).Hash
		firmwareSha256 = (Get-FileHash -LiteralPath $Firmware -Algorithm SHA256).Hash
	}
	$launch | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $LaunchRecord -Encoding utf8

	$null = Wait-SerialMarker "[KERNEL] Desktop drawn" $TimeoutSeconds
	$null = Wait-SerialMarker "[FONT5] redraw-pass" $TimeoutSeconds
	$visibleLog = Get-SerialText
	if (-not $visibleLog.Contains("[FONT5] validated font loaded family=")) {
		throw "Guest did not report loading the validated Roboto font."
	}
	if ($visibleLog.Contains("[FONT5] validated font load failed") -or
		$visibleLog.Contains("[FONT5] scalable draw failed")) {
		throw "Guest reported a FONT5 load/draw failure or bitmap fallback."
	}
	if ($visibleLog -notmatch '\[FONT5\] advance-i[\s\S]{0,256}?=0x([0-9A-Fa-f]+)\s+advance-W=0x([0-9A-Fa-f]+)') {
		throw "Guest did not report i/W advance metrics."
	}
	$narrowAdvance = [Convert]::ToUInt32($Matches[1], 16)
	$wideAdvance = [Convert]::ToUInt32($Matches[2], 16)
	if ($narrowAdvance -ge $wideAdvance) {
		throw "TrueType advance metrics did not distinguish narrow i from wide W."
	}
	if ($visibleLog -notmatch '\[FONT5\] redraw-pass hits=0x([0-9A-Fa-f]+) misses=0x([0-9A-Fa-f]+) rasterizations=0x([0-9A-Fa-f]+)') {
		throw "Guest did not report warm redraw cache counters."
	}
	$warmHits = [Convert]::ToUInt64($Matches[1], 16)
	if ($warmHits -eq 0) { throw "Guest warm redraw did not produce cache hits." }

	$deadline = [DateTime]::UtcNow.AddSeconds(10)
	while (-not $qmpClient -and [DateTime]::UtcNow -lt $deadline) {
		if ($qemuProcess.HasExited) { throw "QEMU exited before the desktop proof completed." }
		try {
			$client = [System.Net.Sockets.TcpClient]::new()
			$client.ReceiveTimeout = 5000
			$client.SendTimeout = 5000
			$client.Connect([System.Net.IPAddress]::Loopback, $QmpPort)
			$stream = $client.GetStream()
			$reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
			$writer = [System.IO.StreamWriter]::new($stream, [System.Text.Encoding]::ASCII, 1024, $true)
			$writer.AutoFlush = $true
			$greeting = $reader.ReadLine()
			if (-not $greeting.Contains('"QMP"')) { throw "Unexpected QMP greeting: $greeting" }
			$null = Invoke-QmpCommand $client $reader $writer "capabilities" '{"execute":"qmp_capabilities","id":"capabilities"}'
			$qmpClient = @{ client = $client; reader = $reader; writer = $writer }
		} catch {
			if ($client) { $client.Dispose() }
			Start-Sleep -Milliseconds 250
		}
	}
	if (-not $qmpClient) { throw "QEMU QMP endpoint did not become ready on 127.0.0.1:$QmpPort." }

	$screenshotPathForHmp = $Screenshot.Replace('\', '/')
	$null = Send-QmpHumanCommand $qmpClient.client $qmpClient.reader $qmpClient.writer `
		"screendump" "screendump $screenshotPathForHmp"
Start-Sleep -Milliseconds 250
	if (-not (Test-Path -LiteralPath $Screenshot -PathType Leaf) -or
		(Get-Item -LiteralPath $Screenshot).Length -le 0) {
		throw "QEMU did not produce the requested desktop screenshot."
	}
	$null = Send-QmpHumanCommand $qmpClient.client $qmpClient.reader $qmpClient.writer "escape" "sendkey esc"
	$proofLog = Wait-SerialMarker "[FONT5] proof launch=PASS" $TimeoutSeconds
	if ($proofLog.Contains("[NATIVE-ELF-FAULT]") -or $proofLog.Contains("[KERNEL-FAULT]")) {
		throw "Guest reported a kernel or Native ELF fault during the FONT5 proof."
	}
	$proofPassed = $true
	$launch.proofResult = "PASS"
	$launch.completedUtc = [DateTime]::UtcNow.ToString("o")
	$launch.narrowAdvancePx = $narrowAdvance
	$launch.wideAdvancePx = $wideAdvance
	$launch.warmCacheHits = $warmHits
	$launch.screenshotSha256 = (Get-FileHash -LiteralPath $Screenshot -Algorithm SHA256).Hash
	$launch | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $LaunchRecord -Encoding utf8
	$proofLog | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "guest-proof-serial.log") -Encoding utf8
	Write-Host "FONT5 QEMU guest proof PASS"
	Write-Host "  serial: $SerialLog"
	Write-Host "  screenshot: $Screenshot"
	Write-Host "  launch record: $LaunchRecord"
} finally {
	if ($qmpClient) {
		try {
			$null = Send-QmpHumanCommand $qmpClient.client $qmpClient.reader $qmpClient.writer "quit" "quit"
		} catch { }
		$qmpClient.client.Dispose()
		if ($qemuProcess) { $qemuProcess.WaitForExit(10000) | Out-Null }
	}
	if ($qemuProcess -and -not $qemuProcess.HasExited) {
		Stop-OwnedQemu $qemuProcess $privateRootForCommand
	}
	if (-not $proofPassed) {
		(Get-SerialText) | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "guest-proof-serial.log") -Encoding utf8
	}
}
