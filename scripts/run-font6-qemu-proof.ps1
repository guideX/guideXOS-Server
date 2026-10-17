param(
	[string]$EvidenceDirectory = "out\font6-qemu-20261016-fontsupport-run",
	[string]$Memory = "1024M",
	[int]$TimeoutSeconds = 240
)

$ErrorActionPreference = "Stop"
$RootDir = Split-Path -Parent $PSScriptRoot
$QemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$BaseGuest = Join-Path $RootDir "out\font5-qemu-20261016-fontsupport"
$ProofKernel = Join-Path $RootDir "kernel\build\font6-amd64-proof\bin\kernel.elf"
$FontSource = Join-Path $RootDir "assets\Fonts\roboto\Roboto-Regular.ttf"
if (-not [System.IO.Path]::IsPathRooted($EvidenceDirectory)) {
	$EvidenceDirectory = [System.IO.Path]::GetFullPath((Join-Path $RootDir $EvidenceDirectory))
} else {
	$EvidenceDirectory = [System.IO.Path]::GetFullPath($EvidenceDirectory)
}
$EspDir = Join-Path $EvidenceDirectory "ESP"
$Firmware = Join-Path $EvidenceDirectory "OVMF.fd"
$CensusPath = Join-Path $EvidenceDirectory "qemu-process-census.json"

foreach ($requiredFile in @($QemuExe, (Join-Path $BaseGuest "OVMF.fd"), $ProofKernel, $FontSource)) {
	if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
		throw "Required isolated FONT6 guest input is missing: $requiredFile"
	}
}
if (-not (Test-Path -LiteralPath (Join-Path $BaseGuest "ESP\EFI\BOOT\BOOTX64.EFI") -PathType Leaf)) {
	throw "FONT5 guest bootloader source is missing."
}
if ($TimeoutSeconds -lt 20 -or $TimeoutSeconds -gt 600) { throw "TimeoutSeconds must be between 20 and 600." }

if (Test-Path -LiteralPath $EvidenceDirectory -PathType Container) {
	if ((Test-Path -LiteralPath (Join-Path $EvidenceDirectory "qemu-process-census.json")) -or
		(Test-Path -LiteralPath (Join-Path $EvidenceDirectory "ESP\fontsel0.cfg")) -or
		(Test-Path -LiteralPath (Join-Path $EvidenceDirectory "ESP\fontsel1.cfg"))) {
		throw "This evidence directory already contains FONT6 run state. Choose a new EvidenceDirectory; the script will not reset or overwrite persistent guest storage."
	}
} else {
	New-Item -ItemType Directory -Path $EvidenceDirectory | Out-Null
	Copy-Item -LiteralPath (Join-Path $BaseGuest "ESP") -Destination $EspDir -Recurse
	Copy-Item -LiteralPath (Join-Path $BaseGuest "OVMF.fd") -Destination $Firmware
}
if (-not (Test-Path -LiteralPath $EspDir -PathType Container) -or
	-not (Test-Path -LiteralPath $Firmware -PathType Leaf)) {
	throw "The isolated FONT6 ESP/firmware pair is incomplete: $EvidenceDirectory"
}
$fontDirectory = Join-Path $EspDir "system\fonts"
New-Item -ItemType Directory -Path $fontDirectory -Force | Out-Null
$guestFont = Join-Path $fontDirectory "Roboto-Regular.ttf"
if (-not (Test-Path -LiteralPath $guestFont -PathType Leaf)) {
	Copy-Item -LiteralPath $FontSource -Destination $guestFont
}
Copy-Item -LiteralPath $ProofKernel -Destination (Join-Path $EspDir "kernel.elf") -Force

$requestedMiB = 0
if ($Memory -match '^(?<amount>[0-9]+)M$') { $requestedMiB = [int]$Matches.amount }
elseif ($Memory -match '^(?<amount>[0-9]+)G$') { $requestedMiB = [int]$Matches.amount * 1024 }
else { throw "Memory must be an integer MiB or GiB value, for example 1024M." }
if ($requestedMiB -lt 512 -or $requestedMiB -gt 4096) { throw "Memory must be between 512M and 4096M." }

$script:qemuProcess = $null
$script:qmpClient = $null
$script:currentSerial = ""
$script:censuses = @()

function Get-SerialText {
	if (-not (Test-Path -LiteralPath $script:currentSerial -PathType Leaf)) { return "" }
	try { return [string](Get-Content -LiteralPath $script:currentSerial -Raw -ErrorAction Stop) }
	catch { return "" }
}

function Wait-SerialMarker([string]$Marker, [int]$Seconds) {
	$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
	while ([DateTime]::UtcNow -lt $deadline) {
		$text = Get-SerialText
		if ($null -eq $text) { $text = [string]::Empty }
		if ($text.Contains($Marker)) { return $text }
		if ($script:qemuProcess -and $script:qemuProcess.HasExited) {
			throw "QEMU exited before guest serial marker: $Marker"
		}
		if ($text.Contains("[FONT6] live-apply=FAIL") -or $text.Contains("[FONT6] live-size-change=FAIL") -or
			$text.Contains("[NATIVE-ELF-FAULT]") -or $text.Contains("[KERNEL-FAULT]")) {
			throw "Guest reported a FONT6 or kernel failure before marker: $Marker"
		}
		Start-Sleep -Milliseconds 250
	}
	throw "Timed out waiting for guest serial marker: $Marker"
}

function Invoke-QmpCommand($Reader, $Writer, [string]$Id, [string]$Json) {
	$Writer.WriteLine($Json)
	for ($i = 0; $i -lt 64; ++$i) {
		$line = $Reader.ReadLine()
		if ($null -eq $line) { throw "QMP closed before response to $Id." }
		if ($line.Contains(('"id":"' + $Id + '"')) -or $line.Contains(('"id": "' + $Id + '"'))) {
			if ($line.Contains('"error"')) { throw "QMP command $Id failed: $line" }
			return $line
		}
	}
	throw "QMP response was not received for $Id."
}

function Connect-Qmp([int]$Port) {
	$deadline = [DateTime]::UtcNow.AddSeconds(15)
	while ([DateTime]::UtcNow -lt $deadline) {
		if ($script:qemuProcess.HasExited) { throw "QEMU exited before QMP became ready." }
		$client = $null
		try {
			$client = [System.Net.Sockets.TcpClient]::new()
			$client.ReceiveTimeout = 5000
			$client.SendTimeout = 5000
			$client.Connect([System.Net.IPAddress]::Loopback, $Port)
			$stream = $client.GetStream()
			$reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
			$writer = [System.IO.StreamWriter]::new($stream, [System.Text.Encoding]::ASCII, 1024, $true)
			$writer.AutoFlush = $true
			$greeting = $reader.ReadLine()
			if (-not $greeting.Contains('"QMP"')) { throw "Unexpected QMP greeting: $greeting" }
			$null = Invoke-QmpCommand $reader $writer "capabilities" '{"execute":"qmp_capabilities","id":"capabilities"}'
			return @{ client = $client; reader = $reader; writer = $writer }
		} catch {
			if ($client) { $client.Dispose() }
			Start-Sleep -Milliseconds 250
		}
	}
	throw "QEMU QMP endpoint did not become ready."
}

function Stop-OwnedQemu {
	if (-not $script:qemuProcess) { return }
	$script:qemuProcess.Refresh()
	if ($script:qemuProcess.HasExited) { return }
	$current = Get-CimInstance Win32_Process -Filter ("ProcessId=" + $script:qemuProcess.Id) -ErrorAction SilentlyContinue
	if (-not $current -or [string]::IsNullOrWhiteSpace($current.CommandLine) -or
		[string]::IsNullOrWhiteSpace($current.ExecutablePath)) {
		$script:qemuProcess.Refresh()
		if ($script:qemuProcess.HasExited) { return }
		throw "Refusing to stop FONT6 QEMU PID $($script:qemuProcess.Id): ownership cannot be revalidated."
	}
	$resolvedExe = [System.IO.Path]::GetFullPath($current.ExecutablePath)
	if ($current.Name -ne "qemu-system-x86_64.exe" -or
		-not $resolvedExe.Equals([System.IO.Path]::GetFullPath($QemuExe), [System.StringComparison]::OrdinalIgnoreCase) -or
		-not $current.CommandLine.Contains($EvidenceDirectory)) {
		throw "Refusing to stop PID $($script:qemuProcess.Id): it no longer matches the private FONT6 guest."
	}
	Stop-Process -Id $script:qemuProcess.Id -Force
	$script:qemuProcess.WaitForExit(10000) | Out-Null
}

function Invoke-Font6Boot([string]$Name, [string[]]$Markers, [string]$ScreenshotName, [bool]$RequireFont5) {
	# A fresh census is recorded immediately before each isolated guest launch.
	$foreignQemu = @(
		Get-CimInstance Win32_Process |
			Where-Object { $_.Name -like "qemu-system-*" -or $_.Name -like "qemu*" } |
			Select-Object ProcessId, Name, ExecutablePath, CommandLine
	)
	foreach ($processInfo in $foreignQemu) {
		if ($processInfo.CommandLine -and $processInfo.CommandLine.Contains($EvidenceDirectory)) {
			throw "Another QEMU already references the private FONT6 evidence path: PID $($processInfo.ProcessId)"
		}
	}
	$os = Get-CimInstance Win32_OperatingSystem
	$freeMiB = [int][Math]::Floor($os.FreePhysicalMemory / 1024)
	$totalMiB = [int][Math]::Floor($os.TotalVisibleMemorySize / 1024)
	if ($freeMiB -lt ($requestedMiB + 512)) {
		throw "Insufficient free RAM for isolated guest coexistence: free=${freeMiB}MiB requested=${requestedMiB}MiB margin=512MiB."
	}
	$census = [ordered]@{
		boot = $Name
		capturedUtc = [DateTime]::UtcNow.ToString("o")
		totalPhysicalMiB = $totalMiB
		freePhysicalMiB = $freeMiB
		requestedGuestMemoryMiB = $requestedMiB
		foreignQemuProcesses = $foreignQemu
	}
	$script:censuses += $census
	$script:censuses | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $CensusPath -Encoding utf8

	$serial = Join-Path $EvidenceDirectory "$Name.serial.log"
	$screenshot = Join-Path $EvidenceDirectory $ScreenshotName
	Remove-Item -LiteralPath $serial,$screenshot -Force -ErrorAction SilentlyContinue
	$script:currentSerial = $serial
	$portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
	$portProbe.Start()
	$port = ([System.Net.IPEndPoint]$portProbe.LocalEndpoint).Port
	$portProbe.Stop()
	$arguments = @(
		"-machine", "pc,accel=whpx,usb=off", "-smp", "2", "-m", $Memory,
		"-vga", "std", "-display", "none",
		"-drive", "if=pflash,format=raw,readonly=on,file=$Firmware",
		"-drive", "file=fat:rw:$EspDir,format=raw,if=ide,index=0",
		"-serial", "file:$serial", "-qmp", "tcp:127.0.0.1:$port,server=on,wait=off",
		"-monitor", "none", "-no-reboot", "-rtc", "base=utc,clock=host", "-net", "none"
	)
	$argumentLine = ($arguments | ForEach-Object {
		if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ }
	}) -join ' '
	$script:qemuProcess = $null
	$script:qmpClient = $null
	$completed = $false
	try {
		$script:qemuProcess = Start-Process -FilePath $QemuExe -ArgumentList $argumentLine -WindowStyle Hidden -PassThru
		$census.processId = $script:qemuProcess.Id
		$census.privateEsp = $EspDir
		$census.qmpPort = $port
		$census.network = "disabled"
		$census.kernelSha256 = (Get-FileHash -LiteralPath (Join-Path $EspDir "kernel.elf") -Algorithm SHA256).Hash
		$script:censuses | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $CensusPath -Encoding utf8
		foreach ($marker in $Markers) { $null = Wait-SerialMarker $marker $TimeoutSeconds }
		$script:qmpClient = Connect-Qmp $port
		$ppmPath = $screenshot.Replace('\', '/')
		$payload = @{ execute = "human-monitor-command"; arguments = @{ "command-line" = "screendump $ppmPath" }; id = "screendump" } |
			ConvertTo-Json -Compress -Depth 4
		$null = Invoke-QmpCommand $script:qmpClient.reader $script:qmpClient.writer "screendump" $payload
		Start-Sleep -Milliseconds 250
		if (-not (Test-Path -LiteralPath $screenshot -PathType Leaf) -or (Get-Item -LiteralPath $screenshot).Length -le 0) {
			throw "QEMU did not produce a FONT6 $Name screenshot."
		}
		if ($RequireFont5) { $null = Wait-SerialMarker "[FONT5] proof launch=PASS" $TimeoutSeconds }
		$text = Get-SerialText
		if ($text.Contains("[KERNEL-FAULT]") -or $text.Contains("[NATIVE-ELF-FAULT]")) {
			throw "Guest reported a kernel or Native ELF fault during $Name."
		}
		$text | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$Name.full-serial.log") -Encoding utf8
		$census.screenshotSha256 = (Get-FileHash -LiteralPath $screenshot -Algorithm SHA256).Hash
		$census.completedUtc = [DateTime]::UtcNow.ToString("o")
		$census.result = "PASS"
		$script:censuses | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $CensusPath -Encoding utf8
		$completed = $true
		Write-Host "$Name PASS — serial=$serial screenshot=$screenshot"
	} finally {
		if ($script:qmpClient) {
			try {
				$payload = @{ execute = "human-monitor-command"; arguments = @{ "command-line" = "quit" }; id = "quit" } |
					ConvertTo-Json -Compress -Depth 4
				$null = Invoke-QmpCommand $script:qmpClient.reader $script:qmpClient.writer "quit" $payload
			} catch { }
			$script:qmpClient.client.Dispose()
			if ($script:qemuProcess) { $script:qemuProcess.WaitForExit(10000) | Out-Null }
		}
		if ($script:qemuProcess -and -not $script:qemuProcess.HasExited) { Stop-OwnedQemu }
		if (-not $completed) { (Get-SerialText) | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$Name.full-serial.log") -Encoding utf8 }
	}
}

Invoke-Font6Boot "boot1-live-selection" @("[FONT6] live-apply=PASS", "[FONT6] live-size-change=PASS") `
	"font6-live-selection.ppm" $true
Invoke-Font6Boot "boot2-restored" @("[FONT6] reboot-restore=PASS", "[FONT6] control-panel-system-settings-draw=PASS") `
	"font6-reboot-restored.ppm" $false

# This deletion is confined to the newly staged private guest ESP. The active
# source in the prior boot has already been closed with its QEMU process.
Remove-Item -LiteralPath $guestFont -Force
Invoke-Font6Boot "boot3-missing-font-fallback" @("[FONT6] missing-source-fallback=PASS", "[FONT6] control-panel-system-settings-draw=PASS") `
	"font6-bitmap-fallback.ppm" $false

Write-Host "FONT6 QEMU persistence, restore, missing-source fallback, and FONT5 guest proof PASS."
Write-Host "  private guest: $EvidenceDirectory"
Write-Host "  process census: $CensusPath"
