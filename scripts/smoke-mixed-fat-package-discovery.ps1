param(
    [int]$TimeoutSeconds = 90,
    [string]$EspPath,
    [string]$QemuPath,
    [string]$OvmfPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($EspPath)) { $EspPath = Join-Path $Root 'ESP' }
if ([string]::IsNullOrWhiteSpace($QemuPath)) {
    $qemuCommand = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    if ($qemuCommand) {
        $QemuPath = $qemuCommand.Source
    } else {
        $QemuPath = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
    }
}
if ([string]::IsNullOrWhiteSpace($OvmfPath)) {
    $OvmfPath = Join-Path $Root 'OVMF.fd'
}

if (-not (Test-Path -LiteralPath $EspPath -PathType Container)) { throw "ESP directory not found: $EspPath" }
if (-not (Test-Path -LiteralPath $QemuPath -PathType Leaf)) { throw "QEMU executable not found: $QemuPath" }
if (-not (Test-Path -LiteralPath $OvmfPath -PathType Leaf)) { throw "OVMF image not found: $OvmfPath" }

foreach ($required in @(
    'EFI\BOOT\BOOTX64.EFI',
    'kernel.elf',
    'ramdisk.img',
    'Apps\DeveloperStudio\app.json',
    'Apps\DeveloperStudio\bin\amd64\developerstudio.elf',
    'Apps\PacMan\app.json',
    'Apps\PacMan\bin\amd64\pacman.elf'
)) {
    $requiredPath = Join-Path $EspPath $required
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required mixed-package release file is missing: $requiredPath"
    }
}

$runRoot = Join-Path $Root 'out\validation'
$runDirectory = Join-Path $runRoot ('mixed-fat-package-discovery-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
$serialLog = Join-Path $runDirectory 'qemu-serial.log'
$stdoutLog = Join-Path $runDirectory 'qemu.stdout.log'
$stderrLog = Join-Path $runDirectory 'qemu.stderr.log'

$arguments = @(
    '-machine', 'pc,usb=off',
    '-drive', "if=pflash,format=raw,readonly=on,file=$OvmfPath",
    '-drive', "file=fat:rw:$EspPath,format=raw,if=ide,index=0",
    '-m', '1024M',
    '-vga', 'std',
    '-display', 'none',
    '-serial', "file:$serialLog",
    '-no-reboot',
    '-rtc', 'base=utc,clock=host'
)

Write-Host "[mixed-fat-package-discovery] ESP: $EspPath"
Write-Host "[mixed-fat-package-discovery] serial log: $serialLog"
$process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $Root `
    -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog -PassThru -WindowStyle Hidden

try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serialLog -PathType Leaf) {
            $serial = Get-Content -LiteralPath $serialLog -Raw
            if ($serial -match '\[NATIVE-ELF\] discovery result packages=0x00000002') {
                $ready = $true
                break
            }
        }
        $process.Refresh()
        if ($process.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
} finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}

$serial = if (Test-Path -LiteralPath $serialLog -PathType Leaf) {
    Get-Content -LiteralPath $serialLog -Raw
} else {
    ''
}

$developerDiscoveries = [regex]::Matches($serial, '\[NATIVE-ELF\] App Model package discovered package=DeveloperStudio').Count
$pacmanDiscoveries = [regex]::Matches($serial, '\[NATIVE-ELF\] App Model package discovered package=PacMan').Count
$countMarkers = [regex]::Matches($serial, '\[NATIVE-ELF\] discovery result packages=0x00000002').Count
$developerManifest = $serial.Contains('[VFS] Opened: /Apps/DeveloperStudio/app.json')
$pacmanManifest = $serial.Contains('[VFS] Opened: /Apps/PacMan/app.json')
$developerExecutable = $serial.Contains('executable=/Apps/DeveloperStudio/bin/amd64/developerstudio.elf')
$pacmanExecutable = $serial.Contains('executable=/Apps/PacMan/bin/amd64/pacman.elf')
$panic = $serial -match '(?im)(panic|fatal exception|triple fault)'

Write-Host "[mixed-fat-package-discovery] packages=0x00000002 marker count: $countMarkers"
Write-Host "[mixed-fat-package-discovery] DeveloperStudio discovery count: $developerDiscoveries"
Write-Host "[mixed-fat-package-discovery] PacMan discovery count: $pacmanDiscoveries"
Write-Host "[mixed-fat-package-discovery] DeveloperStudio manifest: $developerManifest"
Write-Host "[mixed-fat-package-discovery] PacMan manifest: $pacmanManifest"
Write-Host "[mixed-fat-package-discovery] DeveloperStudio executable path: $developerExecutable"
Write-Host "[mixed-fat-package-discovery] PacMan executable path: $pacmanExecutable"
Write-Host "[mixed-fat-package-discovery] panic marker: $panic"

$pass = $ready -and $countMarkers -eq 1 -and
    $developerDiscoveries -eq 1 -and $pacmanDiscoveries -eq 1 -and
    $developerManifest -and $pacmanManifest -and
    $developerExecutable -and $pacmanExecutable -and -not $panic
if (-not $pass) {
    throw "Mixed FAT package discovery regression failed. Review serial log: $serialLog"
}

Write-Host "Mixed FAT package discovery smoke PASS. Serial log: $serialLog" -ForegroundColor Green
