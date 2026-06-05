param(
    [switch]$Build,
    [int]$TimeoutSeconds = 40
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
. (Join-Path $Root "scripts\process_environment.ps1")
Normalize-ProcessEnvironment
. (Join-Path $Root "scripts\navigator_smoke_repo_hygiene.ps1")

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$downloadsState = Save-NavigatorSmokeDirectoryState -LiteralPath (Join-Path $Root "downloads")
$ramdiskState = Save-NavigatorSmokeFileState -LiteralPath (Join-Path $Root "ESP\\ramdisk.img")
$wallpaperPackState = Save-NavigatorSmokeDirectoryState -LiteralPath (Join-Path $Root "out\\wallpaper-pack")

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
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "gxos_tls_foundation.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "main.o" -ErrorAction SilentlyContinue |
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

function Get-NavigatorKernelSmokePortOwners {
    param([int[]]$Ports)

    $connections = Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
        Where-Object { $_.LocalPort -in $Ports }
    if (-not $connections) { return @() }

    $owners = @()
    foreach ($connection in $connections) {
        $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($connection.OwningProcess)" -ErrorAction SilentlyContinue
        $owners += [pscustomobject]@{
            LocalAddress = $connection.LocalAddress
            LocalPort = $connection.LocalPort
            OwningProcess = $connection.OwningProcess
            Name = if ($process) { $process.Name } else { $null }
            CommandLine = if ($process) { $process.CommandLine } else { $null }
        }
    }
    return $owners
}

function Clear-NavigatorKernelSmokePortConflicts {
    param([int[]]$Ports)

    $owners = Get-NavigatorKernelSmokePortOwners -Ports $Ports
    foreach ($owner in $owners) {
        $commandLine = $owner.CommandLine
        if ($commandLine -and $commandLine -match 'navigator_kernel_http_server\.py') {
            Stop-Process -Id $owner.OwningProcess -Force -ErrorAction SilentlyContinue
            continue
        }
        throw "Navigator kernel smoke port conflict on $($owner.LocalAddress):$($owner.LocalPort) owned by PID $($owner.OwningProcess) ($($owner.Name))."
    }

    Start-Sleep -Milliseconds 300
    $remaining = Get-NavigatorKernelSmokePortOwners -Ports $Ports
    if ($remaining) {
        $detail = ($remaining | ForEach-Object {
            "$($_.LocalAddress):$($_.LocalPort) pid=$($_.OwningProcess)"
        }) -join ", "
        throw "Navigator kernel smoke could not clear stale listeners: $detail"
    }
}

$python = Find-Python
if (-not $python) { throw "python not found; required for local Navigator HTTP smoke server." }

Write-Host "Building kernel with active Navigator HTTP/PNG smoke diagnostics..."
$oldSmokeCaFixture = $env:GXOS_NAVIGATOR_SMOKE_CA_FIXTURE
$env:GXOS_NAVIGATOR_SMOKE_CA_FIXTURE = "1"
Invoke-KernelBuildForSmoke "-DGXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE"
$env:GXOS_NAVIGATOR_SMOKE_CA_FIXTURE = $oldSmokeCaFixture
$activeSmokeBuild = $true

$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
if (-not (Test-Path $ovmf)) { throw "OVMF image not found: $ovmf" }

$esp = Join-Path $Root "ESP"
$bootloader = Join-Path $esp "EFI\BOOT\BOOTX64.EFI"
if (-not (Test-Path $bootloader)) {
    throw "ESP/EFI/BOOT/BOOTX64.EFI not found. Run build-kernel.bat first or pass -Build."
}

$smokeCaFixture = Join-Path $Root "scripts\fixtures\navigator-smoke-root-ca-bundle.pem"
if (-not (Test-Path $smokeCaFixture)) {
    throw "Navigator smoke CA fixture not found: $smokeCaFixture"
}
$validatedCaFixture = Join-Path $Root "scripts\fixtures\navigator-validated-root-ca-bundle.pem"
if (-not (Test-Path $validatedCaFixture)) {
    throw "Navigator validated CA fixture not found: $validatedCaFixture"
}
Write-Host "Kernel smoke fixtures available:"
Write-Host "  smoke-only CA bundle: $smokeCaFixture"
Write-Host "  validated policy CA bundle: $validatedCaFixture"

$startup = Join-Path $esp "startup.nsh"
$createdStartup = $false
if (-not (Test-Path $startup)) {
    "FS0:\EFI\BOOT\BOOTX64.EFI" | Set-Content -Path $startup -Encoding ASCII
    $createdStartup = $true
}

$httpLog = Join-Path $LogDir "navigator-kernel-http-$stamp.log"
$httpErrLog = Join-Path $LogDir "navigator-kernel-http-$stamp.err.log"
$httpsLog = Join-Path $LogDir "navigator-kernel-https-$stamp.log"
$httpsErrLog = Join-Path $LogDir "navigator-kernel-https-$stamp.err.log"
$httpServer = Join-Path $Root "scripts\navigator_kernel_http_server.py"
$httpsCert = Join-Path $Root "scripts\fixtures\navigator-smoke-guidexos.test.crt"
$httpsKey = Join-Path $Root "scripts\fixtures\navigator-smoke-guidexos.test.key"
if (-not (Test-Path $httpsCert)) { throw "Navigator TLS smoke certificate not found: $httpsCert" }
if (-not (Test-Path $httpsKey)) { throw "Navigator TLS smoke private key not found: $httpsKey" }
Clear-NavigatorKernelSmokePortConflicts -Ports @(8080, 8443)
$httpArgs = @("`"$httpServer`"", "--port", "8080", "--host", "0.0.0.0", "--root", "`"$Root`"", "--http-port", "8080", "--https-port", "8443")
$httpProc = Start-Process -FilePath $python -ArgumentList $httpArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpLog -RedirectStandardError $httpErrLog
$httpsArgs = @("`"$httpServer`"", "--port", "8443", "--host", "0.0.0.0", "--root", "`"$Root`"", "--http-port", "8080", "--https-port", "8443", "--tls-cert", "`"$httpsCert`"", "--tls-key", "`"$httpsKey`"")
$httpsProc = Start-Process -FilePath $python -ArgumentList $httpsArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpsLog -RedirectStandardError $httpsErrLog
Start-Sleep -Milliseconds 800
if ($httpProc.HasExited) {
    throw "local HTTP smoke server exited early; see $httpLog"
}
if ($httpsProc.HasExited) {
    throw "local HTTPS smoke server exited early; see $httpsLog"
}

function Set-ProcessEnvValue {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowNull()][string]$Value
    )

    if ([string]::IsNullOrEmpty($Value)) {
        Remove-Item "Env:$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item "Env:$Name" $Value
    }
}

$navigatorSmokeEnvNames = @(
    "GXOS_NAVIGATOR_SMOKE_CA_FIXTURE",
    "GXOS_NAVIGATOR_HTTPS_POLICY",
    "GXOS_NAVIGATOR_USER_CA_BUNDLE_SOURCE",
    "GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE"
)
$navigatorSmokeEnvOriginal = @{}
foreach ($envName in $navigatorSmokeEnvNames) {
    $navigatorSmokeEnvOriginal[$envName] = [Environment]::GetEnvironmentVariable($envName, "Process")
}

function Restore-NavigatorKernelSmokeEnvironment {
    foreach ($envName in $navigatorSmokeEnvNames) {
        Set-ProcessEnvValue -Name $envName -Value $navigatorSmokeEnvOriginal[$envName]
    }
}

function Invoke-NavigatorKernelSmokeRamdiskStage {
    param(
        [AllowNull()][string]$HttpsPolicy,
        [AllowNull()][string]$UserCaSource,
        [AllowNull()][string]$ProductionCaSource,
        [bool]$UseSmokeFixture
    )

    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_CA_FIXTURE" -Value ($(if ($UseSmokeFixture) { "1" } else { $null }))
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_HTTPS_POLICY" -Value $HttpsPolicy
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_USER_CA_BUNDLE_SOURCE" -Value $UserCaSource
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE" -Value $ProductionCaSource

    $packScript = Join-Path $Root "scripts\generate-wallpaper-pack.ps1"
    & $packScript -InputDir (Join-Path $Root "assets\Backgrounds") `
        -OutputDir (Join-Path $Root "out\wallpaper-pack") `
        -OutputImage (Join-Path $Root "ESP\ramdisk.img")
    if ($LASTEXITCODE -ne 0) {
        throw "generate-wallpaper-pack.ps1 failed for the current smoke scenario."
    }
}

function Invoke-NavigatorKernelSmokeQemuPass {
    param([Parameter(Mandatory = $true)][string]$ScenarioName)

    $serialLog = Join-Path $LogDir "navigator-kernel-smoke-$stamp-$ScenarioName.serial.log"
    if (Test-Path $serialLog) {
        Remove-Item -LiteralPath $serialLog -Force -ErrorAction SilentlyContinue
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
                if ($partial.Contains("[NAVIGATOR-SMOKE] result=PASS") -or
                    $partial.Contains("[NAVIGATOR-SMOKE] result=FAIL")) {
                    break
                }
            }
        }
        if (-not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force
            Wait-Process -Id $proc.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    } finally {
        Start-Sleep -Milliseconds 300
    }

    $output = if (Test-Path $serialLog) { Get-Content $serialLog -Raw } else { "" }
    return [pscustomobject]@{
        Name = $ScenarioName
        SerialLog = $serialLog
        Output = $output
    }
}

function Test-NavigatorKernelSmokeOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Output,
        [Parameter(Mandatory = $true)][string[]]$Contains,
        [AllowNull()][hashtable]$RegexChecks
    )

    $missing = @()
    foreach ($check in $Contains) {
        if (-not $Output.Contains($check)) {
            $missing += $check
        }
    }
    if ($RegexChecks) {
        foreach ($pattern in $RegexChecks.Keys) {
            if (-not [regex]::IsMatch($Output, $pattern)) {
                $missing += $RegexChecks[$pattern]
            }
        }
    }
    return $missing
}

function Merge-CheckMaps {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Base,
        [AllowNull()][hashtable]$Extra
    )

    $merged = @{}
    foreach ($key in $Base.Keys) {
        $merged[$key] = $Base[$key]
    }
    if ($Extra) {
        foreach ($key in $Extra.Keys) {
            $merged[$key] = $Extra[$key]
        }
    }
    return $merged
}

$commonChecks = @(
    "[NAVIGATOR-SMOKE] BEGIN",
    "[NAVIGATOR-SMOKE] registered=true",
    "[NAVIGATOR-SMOKE] runtime.mode=bare-metal/kernel",
    "[NAVIGATOR-SMOKE] launch.path=AppManager::registerApp -> NavigatorApp::create",
    "[NAVIGATOR-SMOKE] capability.http=enabled numeric IPv4 and hostname HTTP/1.0 GET/POST with redirects/chunked",
    "[NAVIGATOR-SMOKE] capability.http_transport=shared HttpByteStream policy layer (PlainTcpHttp + LocalAllowlistedTlsHttps + PolicyValidatedTlsHttps)",
    "[NAVIGATOR-SMOKE] capability.tls_policy_layer=shared HttpByteStream transport policy layer selects plain TCP HTTP, local allowlisted Mbed TLS, or policy-validated Mbed TLS; plaintext fallback stays disabled and unrestricted public https:// remains blocked",
    "[NAVIGATOR-SMOKE] tls_prereq.rng_quality=Secure",
    "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_status=Plausible",
    "[NAVIGATOR-SMOKE] tls_prereq.tls_backend_status=ReadyForLocalHandshake",
    "[NAVIGATOR-SMOKE] tls_prereq.hostname_validation_available=yes",
    "[NAVIGATOR-SMOKE] tls_smoke.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.result=PASS",
    "[NAVIGATOR-SMOKE] tls_smoke.failure.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.direct_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.redirect_public_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.basic.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.relative_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.absolute_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.hostname_basic.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.hostname_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.redirect_loop.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.chunked.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.missing_404.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.gzip_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_relative.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_absolute.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_chunked.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.image_nonpng.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.hostname_image_relative.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_post.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_get.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_303.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_307.result=PASS",
    "[NAVIGATOR-SMOKE] http.case.forms_post_redirect_hostname.result=PASS",
    "[NAVIGATOR-SMOKE] result=PASS"
)

$commonRegexChecks = @{
    '\[NAVIGATOR-SMOKE\] tls_prereq\.wall_clock_epoch=[1-9][0-9]+' = "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_epoch=<positive Unix seconds>"
    '\[NAVIGATOR-SMOKE\] tls_prereq\.wall_clock_utc=20[2-9][0-9]-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z' = "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_utc=<plausible UTC date>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.protocol=TLSv1\.[23]' = "[NAVIGATOR-SMOKE] tls_smoke.protocol=<TLSv1.2 or TLSv1.3>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.cipher_suite=.+' = "[NAVIGATOR-SMOKE] tls_smoke.cipher_suite=<non-empty cipher suite>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.verify_flags=0' = "[NAVIGATOR-SMOKE] tls_smoke.verify_flags=0"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.failure\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_smoke.failure.verify_flags=<positive mismatch flags>"
}

$scenarioDefinitions = @(
    [pscustomobject]@{
        Name = "no_policy"
        HttpsPolicy = $null
        UseSmokeFixture = $true
        UserCaSource = $null
        ProductionCaSource = $null
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=smoke-only",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=SmokeFixtureTrust",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=LocalSmokeOnly",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=default-safe policy (no /config/navigator/https-policy.txt)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=(none)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_production_ready=no",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_bytes=<positive bytes>"
            '\[NAVIGATOR-SMOKE\] vfs\.system_block_device=[0-9]+' = "[NAVIGATOR-SMOKE] vfs.system_block_device=<numeric block device>"
            '\[NAVIGATOR-SMOKE\] vfs\.certs_block_device=[0-9]+' = "[NAVIGATOR-SMOKE] vfs.certs_block_device=<numeric block device>"
            '\[NAVIGATOR-SMOKE\] vfs\.certs_file_read_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] vfs.certs_file_read_bytes=<positive bytes>"
        }
    },
    [pscustomobject]@{
        Name = "invalid_policy"
        HttpsPolicy = "definitely-invalid"
        UseSmokeFixture = $true
        UserCaSource = $null
        ProductionCaSource = $null
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=LocalSmokeOnly",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=default-safe policy (no /config/navigator/https-policy.txt)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=HTTPS policy config is invalid; falling back to the default-safe policy.",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = $commonRegexChecks
    },
    [pscustomobject]@{
        Name = "user_dev_policy"
        HttpsPolicy = "user-trust-dev-mode"
        UseSmokeFixture = $false
        UserCaSource = $validatedCaFixture
        ProductionCaSource = $null
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=normal",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=UserProvidedTrustStore",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source_detail=User-provided trust store loaded from /config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=VFS config file /config/navigator/https-policy.txt",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=(none)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_production_ready=no",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] tls_readiness_blocker=Validated HTTPS is enabled only for explicit dev/test policy scope; production TLS readiness remains disabled.",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_bytes=<positive bytes>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.protocol=TLSv1\.[23]' = "[NAVIGATOR-SMOKE] https.case.policy_validated.protocol=<TLSv1.2 or TLSv1.3>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.cipher_suite=.+' = "[NAVIGATOR-SMOKE] https.case.policy_validated.cipher_suite=<non-empty cipher suite>"
        }
    },
    [pscustomobject]@{
        Name = "production_validated"
        HttpsPolicy = "production-validated"
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=normal",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=ProductionBundle",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source_detail=Production trust store loaded from /certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=VFS config file /config/navigator/https-policy.txt",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=(none)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_production_ready=yes",
            "[NAVIGATOR-SMOKE] tls_readiness=yes",
            "[NAVIGATOR-SMOKE] tls_readiness_blocker=(none)",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_bytes=<positive bytes>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.protocol=TLSv1\.[23]' = "[NAVIGATOR-SMOKE] https.case.policy_validated.protocol=<TLSv1.2 or TLSv1.3>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.cipher_suite=.+' = "[NAVIGATOR-SMOKE] https.case.policy_validated.cipher_suite=<non-empty cipher suite>"
        }
    }
)

$scenarioFailures = @()

try {
    foreach ($scenario in $scenarioDefinitions) {
        Write-Host "Running kernel smoke scenario '$($scenario.Name)'..."
        Invoke-NavigatorKernelSmokeRamdiskStage -HttpsPolicy $scenario.HttpsPolicy `
            -UserCaSource $scenario.UserCaSource `
            -ProductionCaSource $scenario.ProductionCaSource `
            -UseSmokeFixture $scenario.UseSmokeFixture

        $run = Invoke-NavigatorKernelSmokeQemuPass -ScenarioName $scenario.Name
        Write-Host $run.Output
        $missing = Test-NavigatorKernelSmokeOutput -Output $run.Output -Contains $scenario.Checks -RegexChecks $scenario.RegexChecks
        if ($missing.Count -eq 0) {
            Write-Host "Kernel Navigator smoke scenario '$($scenario.Name)' PASS. Serial log: $($run.SerialLog)"
        } else {
            Write-Host "Kernel Navigator smoke scenario '$($scenario.Name)' FAIL. Serial log: $($run.SerialLog)" -ForegroundColor Red
            foreach ($item in $missing) {
                Write-Host "Missing [$($scenario.Name)]: $item" -ForegroundColor Red
            }
            $scenarioFailures += [pscustomobject]@{
                Name = $scenario.Name
                SerialLog = $run.SerialLog
                Missing = $missing
            }
        }
    }
} finally {
    if ($httpProc -and -not $httpProc.HasExited) {
        Stop-Process -Id $httpProc.Id -Force
    }
    if ($httpsProc -and -not $httpsProc.HasExited) {
        Stop-Process -Id $httpsProc.Id -Force
    }
    if ($createdStartup) {
        Remove-Item $startup -ErrorAction SilentlyContinue
    }
    Restore-NormalKernelBuild
    Restore-NavigatorKernelSmokeEnvironment
    Restore-NavigatorSmokeDirectoryState -State $downloadsState
    Restore-NavigatorSmokeFileState -State $ramdiskState
    Restore-NavigatorSmokeDirectoryState -State $wallpaperPackState
}

if ($scenarioFailures.Count -eq 0) {
    Write-Host "Kernel Navigator smoke PASS across all scenarios."
    exit 0
}

Write-Host "Kernel Navigator smoke FAIL." -ForegroundColor Red
foreach ($failure in $scenarioFailures) {
    Write-Host "Scenario '$($failure.Name)' failed. Serial log: $($failure.SerialLog)" -ForegroundColor Red
}
exit 1

