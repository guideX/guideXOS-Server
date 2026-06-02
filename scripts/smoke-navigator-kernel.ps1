param(
    [switch]$Build,
    [int]$TimeoutSeconds = 40
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "navigator-kernel-smoke-$stamp.serial.log"

function Invoke-KernelBuildForSmoke {
    param([string]$ExtraCFlags)
    $oldExtra = $env:EXTRA_CFLAGS
    if ($ExtraCFlags) {
        $env:EXTRA_CFLAGS = $ExtraCFlags
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "kernel_apps.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    & (Join-Path $Root "build-kernel.bat")
    $buildCode = $LASTEXITCODE
    if ($null -ne $oldExtra) {
        $env:EXTRA_CFLAGS = $oldExtra
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }
    if ($buildCode -ne 0) { exit $buildCode }
}

$activeSmokeBuild = $false
function Restore-NormalKernelBuild {
    if ($script:activeSmokeBuild) {
        Write-Host "Restoring normal kernel build without active Navigator HTTP smoke diagnostics..."
        Invoke-KernelBuildForSmoke ""
        $script:activeSmokeBuild = $false
    }
}

if ($Build) {
    Invoke-KernelBuildForSmoke ""
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

$qemuObjectHelp = (& $qemu -object help 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or ($qemuObjectHelp -notmatch 'rng-builtin')) {
    throw "QEMU environment blocker: rng-builtin object is unavailable."
}
$qemuDeviceHelp = (& $qemu -device help | Out-String)
if ($LASTEXITCODE -ne 0 -or ($qemuDeviceHelp -notmatch 'virtio-rng-pci')) {
    throw "QEMU environment blocker: virtio-rng-pci device is unavailable."
}

function Find-Python {
    foreach ($candidate in @(
        "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
        "$Root\.venv\Scripts\python.exe"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    $python = Get-Command "python" -ErrorAction SilentlyContinue
    if ($python) { return $python.Source }
    $py = Get-Command "py" -ErrorAction SilentlyContinue
    if ($py) { return $py.Source }
    return $null
}

$python = Find-Python
if (-not $python) { throw "python not found; required for local Navigator HTTP smoke server." }

Write-Host "Building kernel with active Navigator HTTP/PNG smoke diagnostics..."
Invoke-KernelBuildForSmoke "-DGXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE"
$activeSmokeBuild = $true

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

$httpLog = Join-Path $LogDir "navigator-kernel-http-$stamp.log"
$httpErrLog = Join-Path $LogDir "navigator-kernel-http-$stamp.err.log"
$httpServer = Join-Path $Root "scripts\navigator_kernel_http_server.py"
$httpArgs = @("`"$httpServer`"", "--port", "8080", "--host", "0.0.0.0", "--root", "`"$Root`"")
$httpProc = Start-Process -FilePath $python -ArgumentList $httpArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpLog -RedirectStandardError $httpErrLog
Start-Sleep -Milliseconds 800
if ($httpProc.HasExited) {
    throw "local HTTP smoke server exited early; see $httpLog"
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
    "-rtc", "base=utc,clock=host",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0",
    "-object", "rng-builtin,id=rng0",
    "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000"
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
        Wait-Process -Id $proc.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
} finally {
    Start-Sleep -Milliseconds 300
    if ($httpProc -and -not $httpProc.HasExited) {
        Stop-Process -Id $httpProc.Id -Force
    }
    if ($createdStartup) {
        Remove-Item $startup -ErrorAction SilentlyContinue
    }
    Restore-NormalKernelBuild
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
    "[NAVIGATOR-SMOKE] capability.http=enabled numeric IPv4 and hostname HTTP/1.0 GET/POST with redirects/chunked",
    "[NAVIGATOR-SMOKE] capability.http_dns=enabled-basic A records",
    "[NAVIGATOR-SMOKE] capability.http_redirects=enabled limit 5",
    "[NAVIGATOR-SMOKE] capability.http_chunked=enabled",
    "[NAVIGATOR-SMOKE] capability.https_tls=unsupported",
    "[NAVIGATOR-SMOKE] capability.tls_backend=none",
    "[NAVIGATOR-SMOKE] capability.http_transport=plain TCP byte-stream",
    "[NAVIGATOR-SMOKE] capability.tls_insertion_seam=prepared",
    "[NAVIGATOR-SMOKE] coverage.direct_https_unsupported=covered",
    "[NAVIGATOR-SMOKE] coverage.http_to_https_redirect_unsupported=covered",
    "[NAVIGATOR-SMOKE] capability.remote_png=enabled-basic numeric IPv4 and hostname http:// PNG images",
    "[NAVIGATOR-SMOKE] capability.downloads=unavailable for bare-metal HTTP v0.1",
    "[NAVIGATOR-SMOKE] capability.css_lite=enabled for embedded style blocks",
    "[NAVIGATOR-SMOKE] capability.forms_lite=enabled interactive GET/POST document controls",
    "[NAVIGATOR-SMOKE] capability.forms_post_hosted=enabled in authoritative hosted Navigator path",
    "[NAVIGATOR-SMOKE] capability.forms_post_bare_metal=enabled-basic application/x-www-form-urlencoded",
    "[NAVIGATOR-SMOKE] capability.forms_post_interactive=enabled through document controls",
    "[NAVIGATOR-SMOKE] capability.forms_post_redirect_policy=303 becomes GET; 301/302/307/308 preserve POST",
    "[NAVIGATOR-SMOKE] page_info.forms_post_bare_metal=enabled-basic",
    "[NAVIGATOR-SMOKE] capability.forms_controls=text, checkbox, radio, textarea, select, submit",
    "[NAVIGATOR-SMOKE] capability.forms_focus_navigation=Tab, Enter, Space in bare-metal document controls",
    "[NAVIGATOR-SMOKE] capability.find_in_page=unsupported in bare-metal adapter",
    "[NAVIGATOR-SMOKE] capability.external_stylesheets=unsupported",
    "[NAVIGATOR-SMOKE] capability.bookmark_persistence=unavailable; in-memory defaults only",
    "[NAVIGATOR-SMOKE] tls_prereq.rng_quality=Secure",
    "[NAVIGATOR-SMOKE] tls_prereq.rng_backend=virtio-rng legacy PCI transitional",
    "[NAVIGATOR-SMOKE] tls_prereq.virtio_rng_detected=yes",
    "[NAVIGATOR-SMOKE] tls_prereq.virtio_rng_status=success",
    "[NAVIGATOR-SMOKE] tls_prereq.random_read_1=PASS",
    "[NAVIGATOR-SMOKE] tls_prereq.random_read_2=PASS",
    "[NAVIGATOR-SMOKE] tls_prereq.random_reads_identical=false",
    "[NAVIGATOR-SMOKE] tls_prereq.rng_fail_closed=false",
    "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_status=Plausible",
    "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_backend=CMOS RTC (interpreted as UTC)",
    "[NAVIGATOR-SMOKE] tls_prereq.tls_backend=none",
    "[NAVIGATOR-SMOKE] tls_prereq.root_ca_store=missing",
    "[NAVIGATOR-SMOKE] tls_readiness=HTTPS bare-metal unsupported, waiting on RNG/clock/TLS backend/root store",
    "[NAVIGATOR-SMOKE] https.case.direct_unsupported.requested_url=https://example.com/",
    "[NAVIGATOR-SMOKE] https.case.direct_unsupported.final_url=https://example.com/",
    "[NAVIGATOR-SMOKE] https.case.direct_unsupported.error=HTTPS/TLS unsupported",
    "[NAVIGATOR-SMOKE] https.case.direct_unsupported.tcp_connect_attempts=0",
    "[NAVIGATOR-SMOKE] https.case.direct_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.redirect_unsupported.requested_url=http://10.0.2.2:8080/navigator-smoke/redirect-to-https",
    "[NAVIGATOR-SMOKE] https.case.redirect_unsupported.final_url=https://example.com/secure",
    "[NAVIGATOR-SMOKE] https.case.redirect_unsupported.error=HTTPS/TLS unsupported redirect",
    "[NAVIGATOR-SMOKE] https.case.redirect_unsupported.tcp_connect_attempts=1",
    "[NAVIGATOR-SMOKE] https.case.redirect_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.basic.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.relative_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.absolute_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.hostname_basic.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.hostname_basic.dns_resolved_ip=10.0.2.2",
    "[NAVIGATOR-SMOKE] http.case.hostname_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.hostname_redirect.final_url=http://guidexos.test:8080/navigator-smoke/final.html",
    "[NAVIGATOR-SMOKE] http.case.hostname_redirect.dns_resolved_ip=10.0.2.2",
    "[NAVIGATOR-SMOKE] http.case.redirect_loop.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.chunked.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.missing_404.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.gzip_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_relative.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_relative.loaded_images=1",
    "[NAVIGATOR-SMOKE] http.case.image_absolute.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_absolute.loaded_images=1",
    "[NAVIGATOR-SMOKE] http.case.image_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_redirect.loaded_images=1",
    "[NAVIGATOR-SMOKE] http.case.image_chunked.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_chunked.loaded_images=1",
    "[NAVIGATOR-SMOKE] http.case.image_nonpng.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_nonpng.failed_images=1",
    "[NAVIGATOR-SMOKE] http.case.hostname_image_relative.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.hostname_image_relative.loaded_images=1",
    "[NAVIGATOR-SMOKE] http.case.hostname_image_relative.dns_resolved_ip=10.0.2.2",
    "[NAVIGATOR-SMOKE] http.case.forms_post.path=interactive-document-controls",
    "[NAVIGATOR-SMOKE] http.case.forms_post.method=POST",
    "[NAVIGATOR-SMOKE] http.case.forms_post.status=200",
    "[NAVIGATOR-SMOKE] http.case.forms_post.content_type=text/html",
    "[NAVIGATOR-SMOKE] http.case.forms_post.submitted_body_bytes=67",
    "[NAVIGATOR-SMOKE] http.case.forms_post.parsed_block_count=",
    "[NAVIGATOR-SMOKE] http.case.forms_post.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_get.path=interactive-document-controls",
    "[NAVIGATOR-SMOKE] http.case.forms_get.method=GET",
    "[NAVIGATOR-SMOKE] http.case.forms_get.final_url=http://10.0.2.2:8080/forms/get-echo?q=posted+value&agree=yes&kind=alpha&note=hello%0Asecond+line&size=m",
    "[NAVIGATOR-SMOKE] http.case.forms_get.parsed_block_count=",
    "[NAVIGATOR-SMOKE] http.case.forms_get.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_303.final_url=http://10.0.2.2:8080/navigator-smoke/final.html",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_303.redirect_count=1",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_303.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_307.final_url=http://10.0.2.2:8080/forms/post-echo",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_307.redirect_count=1",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_307.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_hostname.final_url=http://guidexos.test:8080/forms/post-echo",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_hostname.dns_resolved_ip=10.0.2.2",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_hostname.result=PASS",
    "[NAVIGATOR-SMOKE] result=PASS"
)

$failed = @()
foreach ($check in $checks) {
    if (-not $output.Contains($check)) { $failed += $check }
}
if (-not [regex]::IsMatch($output, '\[NAVIGATOR-SMOKE\] tls_prereq\.wall_clock_epoch=[1-9][0-9]+')) {
    $failed += "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_epoch=<positive Unix seconds>"
}
if (-not [regex]::IsMatch($output, '\[NAVIGATOR-SMOKE\] tls_prereq\.wall_clock_utc=20[2-9][0-9]-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z')) {
    $failed += "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_utc=<plausible UTC date>"
}

if ($failed.Count -eq 0) {
    Write-Host "Kernel Navigator smoke PASS. Serial log: $serialLog"
    exit 0
}

Write-Host "Kernel Navigator smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
foreach ($item in $failed) { Write-Host "Missing: $item" -ForegroundColor Red }
exit 1

