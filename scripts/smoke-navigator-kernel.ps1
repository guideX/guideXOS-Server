param(
    [switch]$Build,
    [int]$TimeoutSeconds = 25
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "navigator-kernel-smoke-$stamp.serial.log"

if ($Build) {
    & (Join-Path $Root "build-kernel.bat")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Find-Qemu {
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($qemu) { return $qemu.Source }
    foreach ($candidate in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }

$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
if (-not (Test-Path $ovmf)) { throw "OVMF image not found: $ovmf" }

$esp = Join-Path $Root "ESP"
$bootloader = Join-Path $esp "EFI\BOOT\BOOTX64.EFI"
if (-not (Test-Path $bootloader)) {
    throw "ESP/EFI/BOOT/BOOTX64.EFI not found. Run build-kernel.bat first or pass -Build."
}

$startup = Join-Path $esp "startup.nsh"
$createdStartup = $false
if (-not (Test-Path $startup)) {
    "FS0:\EFI\BOOT\BOOTX64.EFI" | Set-Content -Path $startup -Encoding ASCII
    $createdStartup = $true
}

$args = @(
    "-machine", "pc",
    "-drive", "if=pflash,format=raw,readonly=on,file=`"$ovmf`"",
    "-drive", "file=fat:rw:`"$esp`",format=raw,if=ide,index=0",
    "-m", "512M",
    "-vga", "std",
    "-display", "none",
    "-serial", "file:`"$serialLog`"",
    "-no-reboot",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0"
)

$proc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-Path $serialLog) {
            $partial = Get-Content $serialLog -Raw
            if ($null -eq $partial) { $partial = "" }
            if ($partial.Contains("[NAVIGATOR-SMOKE] result=PASS")) { break }
        }
    }
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
} finally {
    Start-Sleep -Milliseconds 300
    if ($createdStartup) {
        Remove-Item $startup -ErrorAction SilentlyContinue
    }
}

$output = if (Test-Path $serialLog) { Get-Content $serialLog -Raw } else { "" }
Write-Host $output

$checks = @(
    "[NAVIGATOR-SMOKE] BEGIN",
    "[NAVIGATOR-SMOKE] registered=true",
    "[NAVIGATOR-SMOKE] runtime.mode=bare-metal/kernel",
    "[NAVIGATOR-SMOKE] launch.path=AppManager::registerApp -> NavigatorApp::create",
    "[NAVIGATOR-SMOKE] current.url=about:navigator-runtime",
    "[NAVIGATOR-SMOKE] stale.placeholder=not active",
    "[NAVIGATOR-SMOKE] capability.file_read=enabled through VFS",
    "[NAVIGATOR-SMOKE] capability.http=unsupported/network unavailable",
    "[NAVIGATOR-SMOKE] capability.remote_png=unsupported without bare-metal HTTP transport",
    "[NAVIGATOR-SMOKE] capability.downloads=unavailable until HTTP/VFS write path is connected",
    "[NAVIGATOR-SMOKE] capability.css_lite=enabled for embedded style blocks",
    "[NAVIGATOR-SMOKE] capability.external_stylesheets=unsupported",
    "[NAVIGATOR-SMOKE] capability.bookmark_persistence=unavailable; in-memory defaults only",
    "[NAVIGATOR-SMOKE] result=PASS"
)

$failed = @()
foreach ($check in $checks) {
    if (-not $output.Contains($check)) { $failed += $check }
}

if ($failed.Count -eq 0) {
    Write-Host "Kernel Navigator smoke PASS. Serial log: $serialLog"
    exit 0
}

Write-Host "Kernel Navigator smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
foreach ($item in $failed) { Write-Host "Missing: $item" -ForegroundColor Red }
exit 1
