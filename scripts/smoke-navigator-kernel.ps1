param(
    [switch]$Build,
    [int]$TimeoutSeconds = 300,
    [ValidateSet("Deterministic", "PublicPilot", "All")]
    [string]$ScenarioGroup = "Deterministic",
    [switch]$IncludePublicPilot,
    [string]$CandidateBundlePath,
    [string]$CandidateRotationId,
    [switch]$CandidateReviewed,
    [string[]]$ScenarioFilter
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
$wallpaperPackCaBundleState = Save-NavigatorSmokeFileState -LiteralPath (Join-Path $Root "out\\wallpaper-pack\\certs\\ca-bundle.pem")

function Invoke-KernelBuildForSmoke {
    param([string]$ExtraCFlags)
    $oldExtra = $env:EXTRA_CFLAGS
    if ($ExtraCFlags -and $oldExtra) {
        $env:EXTRA_CFLAGS = "$oldExtra $ExtraCFlags"
    } elseif ($ExtraCFlags) {
        $env:EXTRA_CFLAGS = $ExtraCFlags
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }
    # The Navigator text-metrics pass hit a stale desktop object once; keep the
    # smoke build deterministic by forcing that object to rebuild here too.
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "desktop.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
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
        Wait-NavigatorSmokeFileUnlock -LiteralPath (Join-Path $Root "ESP\\ramdisk.img")
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
$oldTlsDiagnostics = [Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_TLS_DIAGNOSTICS", "Process")
$env:GXOS_NAVIGATOR_TLS_DIAGNOSTICS = "1"
Invoke-KernelBuildForSmoke "-DGXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE -DGXOS_NAVIGATOR_TLS_CAPABILITY_CONTRACT_NEGATIVE_TEST_ACTIVE"
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
$emptyCaFixture = Join-Path $Root "scripts\fixtures\navigator-empty-ca-bundle.pem"
if (-not (Test-Path $emptyCaFixture)) {
    throw "Navigator empty CA fixture not found: $emptyCaFixture"
}
$malformedCaFixture = Join-Path $Root "scripts\fixtures\navigator-malformed-ca-bundle.pem"
if (-not (Test-Path $malformedCaFixture)) {
    throw "Navigator malformed CA fixture not found: $malformedCaFixture"
}
$defaultHttpsCert = Join-Path $Root "scripts\fixtures\navigator-smoke-guidexos.test.crt"
$defaultHttpsKey = Join-Path $Root "scripts\fixtures\navigator-smoke-guidexos.test.key"
$untrustedHttpsCert = Join-Path $Root "scripts\fixtures\navigator-fault-untrusted-guidexos.test.crt"
$untrustedHttpsKey = Join-Path $Root "scripts\fixtures\navigator-fault-untrusted-guidexos.test.key"
$expiredHttpsCert = Join-Path $Root "scripts\fixtures\navigator-fault-expired-guidexos.test.crt"
$expiredHttpsKey = Join-Path $Root "scripts\fixtures\navigator-fault-expired-guidexos.test.key"
$policyDevHttpsCert = Join-Path $Root "scripts\fixtures\navigator-policy-dev.guidexos.test.crt"
$policyDevHttpsKey = Join-Path $Root "scripts\fixtures\navigator-policy-dev.guidexos.test.key"
$policyProdHttpsCert = Join-Path $Root "scripts\fixtures\navigator-policy-prod.guidexos.test.crt"
$policyProdHttpsKey = Join-Path $Root "scripts\fixtures\navigator-policy-prod.guidexos.test.key"
$publicPilotHttpsCert = Join-Path $Root "scripts\fixtures\navigator-public-pilot.guidexos.test.crt"
$publicPilotHttpsKey = Join-Path $Root "scripts\fixtures\navigator-public-pilot.guidexos.test.key"
$untrustedPolicyDevHttpsCert = Join-Path $Root "scripts\fixtures\navigator-fault-untrusted-dev.guidexos.test.crt"
$untrustedPolicyDevHttpsKey = Join-Path $Root "scripts\fixtures\navigator-fault-untrusted-dev.guidexos.test.key"
$untrustedPolicyProdHttpsCert = Join-Path $Root "scripts\fixtures\navigator-fault-untrusted-prod.guidexos.test.crt"
$untrustedPolicyProdHttpsKey = Join-Path $Root "scripts\fixtures\navigator-fault-untrusted-prod.guidexos.test.key"
$expiredPolicyProdHttpsCert = Join-Path $Root "scripts\fixtures\navigator-fault-expired-prod.guidexos.test.crt"
$expiredPolicyProdHttpsKey = Join-Path $Root "scripts\fixtures\navigator-fault-expired-prod.guidexos.test.key"
foreach ($path in @(
    $defaultHttpsCert, $defaultHttpsKey, $untrustedHttpsCert, $untrustedHttpsKey, $expiredHttpsCert, $expiredHttpsKey,
    $policyDevHttpsCert, $policyDevHttpsKey, $policyProdHttpsCert, $policyProdHttpsKey,
    $publicPilotHttpsCert, $publicPilotHttpsKey,
    $untrustedPolicyDevHttpsCert, $untrustedPolicyDevHttpsKey, $untrustedPolicyProdHttpsCert, $untrustedPolicyProdHttpsKey,
    $expiredPolicyProdHttpsCert, $expiredPolicyProdHttpsKey
)) {
    if (-not (Test-Path $path)) {
        throw "Navigator HTTPS smoke fixture not found: $path"
    }
}
Write-Host "Kernel smoke fixtures available:"
Write-Host "  smoke-only CA bundle: $smokeCaFixture"
Write-Host "  validated policy CA bundle: $validatedCaFixture"
Write-Host "  malformed CA bundle: $malformedCaFixture"
Write-Host "  empty CA bundle: $emptyCaFixture"

$startup = Join-Path $esp "startup.nsh"
$createdStartup = $false
if (-not (Test-Path $startup)) {
    "FS0:\EFI\BOOT\BOOTX64.EFI" | Set-Content -Path $startup -Encoding ASCII
    $createdStartup = $true
}

$httpServer = Join-Path $Root "scripts\navigator_kernel_http_server.py"

function Start-NavigatorKernelSmokeServers {
    param(
        [Parameter(Mandatory = $true)][string]$ScenarioName,
        [Parameter(Mandatory = $true)][string]$LocalTlsCert,
        [Parameter(Mandatory = $true)][string]$LocalTlsKey,
        [Parameter(Mandatory = $true)][string]$PolicyHost,
        [Parameter(Mandatory = $true)][string]$PolicyTlsCert,
        [Parameter(Mandatory = $true)][string]$PolicyTlsKey,
        [Parameter(Mandatory = $true)][string]$PublicPilotHost,
        [Parameter(Mandatory = $true)][string]$PublicPilotTlsCert,
        [Parameter(Mandatory = $true)][string]$PublicPilotTlsKey
    )

    Clear-NavigatorKernelSmokePortConflicts -Ports @(8080, 8443)

    $httpLog = Join-Path $LogDir "navigator-kernel-http-$stamp-$ScenarioName.log"
    $httpErrLog = Join-Path $LogDir "navigator-kernel-http-$stamp-$ScenarioName.err.log"
    $httpsLog = Join-Path $LogDir "navigator-kernel-https-$stamp-$ScenarioName.log"
    $httpsErrLog = Join-Path $LogDir "navigator-kernel-https-$stamp-$ScenarioName.err.log"

    $httpArgs = @(
        "`"$httpServer`"", "--port", "8080", "--host", "0.0.0.0", "--root", "`"$Root`"",
        "--http-port", "8080", "--https-port", "8443",
        "--policy-host", $PolicyHost, "--policy-wrong-host", "wrong.guidexos.test",
        "--public-pilot-host", $PublicPilotHost
    )
    $httpProc = Start-Process -FilePath $python -ArgumentList $httpArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpLog -RedirectStandardError $httpErrLog
    $httpsArgs = @(
        "`"$httpServer`"", "--port", "8443", "--host", "0.0.0.0", "--root", "`"$Root`"",
        "--http-port", "8080", "--https-port", "8443",
        "--policy-host", $PolicyHost, "--policy-wrong-host", "wrong.guidexos.test",
        "--local-tls-cert", "`"$LocalTlsCert`"", "--local-tls-key", "`"$LocalTlsKey`"",
        "--policy-tls-cert", "`"$PolicyTlsCert`"", "--policy-tls-key", "`"$PolicyTlsKey`"",
        "--public-pilot-host", $PublicPilotHost,
        "--public-pilot-tls-cert", "`"$PublicPilotTlsCert`"", "--public-pilot-tls-key", "`"$PublicPilotTlsKey`""
    )
    $httpsProc = Start-Process -FilePath $python -ArgumentList $httpsArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpsLog -RedirectStandardError $httpsErrLog
    Start-Sleep -Milliseconds 800
    if ($httpProc.HasExited) {
        throw "local HTTP smoke server exited early; see $httpLog"
    }
    if ($httpsProc.HasExited) {
        throw "local HTTPS smoke server exited early; see $httpsLog"
    }

    return [pscustomobject]@{
        HttpProcess = $httpProc
        HttpsProcess = $httpsProc
        HttpLog = $httpLog
        HttpsLog = $httpsLog
    }
}

function Stop-NavigatorKernelSmokeServers {
    param($Servers)

    if ($Servers -and $Servers.HttpProcess -and -not $Servers.HttpProcess.HasExited) {
        Stop-Process -Id $Servers.HttpProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($Servers -and $Servers.HttpsProcess -and -not $Servers.HttpsProcess.HasExited) {
        Stop-Process -Id $Servers.HttpsProcess.Id -Force -ErrorAction SilentlyContinue
    }
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

function Test-NavigatorSmokeEnvFlag {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) { return $false }
    switch ($Value.Trim().ToLowerInvariant()) {
        "1" { return $true }
        "true" { return $true }
        "yes" { return $true }
        "on" { return $true }
        "enabled" { return $true }
        default { return $false }
    }
}

$navigatorSmokeEnvNames = @(
    "GXOS_NAVIGATOR_SMOKE_CA_FIXTURE",
    "GXOS_NAVIGATOR_HTTPS_POLICY",
    "GXOS_NAVIGATOR_HTTPS_FAULT_MODE",
    "GXOS_NAVIGATOR_USER_CA_BUNDLE_SOURCE",
    "GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE",
    "GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_CA_BUNDLE_SOURCE",
    "GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_ROTATION_ID",
    "GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_PRODUCTION_READY",
    "GXOS_NAVIGATOR_USER_CA_MANIFEST_MODE",
    "GXOS_NAVIGATOR_PRODUCTION_CA_MANIFEST_MODE",
    "GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS",
    "GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE",
    "GXOS_NAVIGATOR_TLS_DIAGNOSTICS"
)
$navigatorSmokeEnvOriginal = @{}
foreach ($envName in $navigatorSmokeEnvNames) {
    $navigatorSmokeEnvOriginal[$envName] = [Environment]::GetEnvironmentVariable($envName, "Process")
}
$navigatorSmokeEnvOriginal["GXOS_NAVIGATOR_TLS_DIAGNOSTICS"] = $oldTlsDiagnostics

function Restore-NavigatorKernelSmokeEnvironment {
    foreach ($envName in $navigatorSmokeEnvNames) {
        Set-ProcessEnvValue -Name $envName -Value $navigatorSmokeEnvOriginal[$envName]
    }
}

function Resolve-NavigatorKernelSmokeOptionalPath {
    param([AllowNull()][string]$PathValue)

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $null
    }

    $candidate = $PathValue.Trim()
    if (-not [System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $Root $candidate
    }
    return [System.IO.Path]::GetFullPath($candidate)
}

function Invoke-NavigatorKernelSmokeRamdiskStage {
    param(
        [AllowNull()][string]$HttpsPolicy,
        [AllowNull()][string]$HttpsFaultMode,
        [AllowNull()][string]$UserCaSource,
        [AllowNull()][string]$ProductionCaSource,
        [AllowNull()][string]$CandidateCaSource,
        [AllowNull()][string]$CandidateRotationId,
        [bool]$CandidateProductionReady = $false,
        [string]$UserManifestMode = "normal",
        [string]$ProductionManifestMode = "normal",
        [bool]$UseSmokeFixture,
        [bool]$EnableRealPublicProbe = $false,
        [bool]$RequireRealPublicProbe = $false,
        [AllowNull()][string]$RealPublicProbeTarget = $null
    )

    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_CA_FIXTURE" -Value ($(if ($UseSmokeFixture) { "1" } else { $null }))
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_HTTPS_POLICY" -Value $HttpsPolicy
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_HTTPS_FAULT_MODE" -Value $HttpsFaultMode
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_USER_CA_BUNDLE_SOURCE" -Value $UserCaSource
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE" -Value $ProductionCaSource
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_CA_BUNDLE_SOURCE" -Value $CandidateCaSource
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_ROTATION_ID" -Value $CandidateRotationId
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_PRODUCTION_READY" -Value ($(if ($CandidateProductionReady) { "1" } else { $null }))
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_USER_CA_MANIFEST_MODE" -Value $UserManifestMode
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_PRODUCTION_CA_MANIFEST_MODE" -Value $ProductionManifestMode
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS" -Value ($(if ($EnableRealPublicProbe) { "1" } else { $null }))
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS" -Value ($(if ($RequireRealPublicProbe) { "1" } else { $null }))
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL" -Value ($(if ($EnableRealPublicProbe) { $RealPublicProbeTarget } else { $null }))
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET" -Value ($(if ($EnableRealPublicProbe) { $RealPublicProbeTarget } else { $null }))

    Wait-NavigatorSmokeFileUnlock -LiteralPath (Join-Path $Root "ESP\\ramdisk.img")
    $packScript = Join-Path $Root "scripts\generate-wallpaper-pack.ps1"
    & $packScript -InputDir (Join-Path $Root "assets\Backgrounds") `
        -OutputDir (Join-Path $Root "out\wallpaper-pack") `
        -OutputImage (Join-Path $Root "ESP\ramdisk.img")
    if ($LASTEXITCODE -ne 0) {
        throw "generate-wallpaper-pack.ps1 failed for the current smoke scenario."
    }

    $productionManifestPath = Join-Path $Root "out\wallpaper-pack\certs\ca-bundle.manifest"
    $userManifestPath = Join-Path $Root "out\wallpaper-pack\config\certs\ca-bundle.manifest"

    return [pscustomobject]@{
        ProductionManifestPath = $productionManifestPath
        ProductionManifest = Get-NavigatorKernelSmokeCaManifest -LiteralPath $productionManifestPath
        UserManifestPath = $userManifestPath
        UserManifest = Get-NavigatorKernelSmokeCaManifest -LiteralPath $userManifestPath
    }
}

function Invoke-NavigatorKernelSmokeQemuPass {
    param([Parameter(Mandatory = $true)][string]$ScenarioName)

    $serialLog = Join-Path $LogDir "navigator-kernel-smoke-$stamp-$ScenarioName.serial.log"
    $output = ""
    for ($attempt = 1; $attempt -le 2; $attempt++) {
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
            Wait-NavigatorSmokeFileUnlock -LiteralPath (Join-Path $Root "ESP\\ramdisk.img")
            Start-Sleep -Milliseconds 300
        }

        $output = if (Test-Path $serialLog) { Get-Content $serialLog -Raw } else { "" }
        if ($output.Contains("[NAVIGATOR-SMOKE] result=PASS") -or
            $output.Contains("[NAVIGATOR-SMOKE] result=FAIL")) {
            break
        }

        if ($attempt -lt 2) {
            Write-Host "Kernel Navigator smoke scenario '$ScenarioName' produced no terminal smoke marker; retrying once..."
        }
    }

    return [pscustomobject]@{
        Name = $ScenarioName
        SerialLog = $serialLog
        Output = $output
    }
}

function New-NavigatorOversizedCaBundleFixture {
    $fixturePath = Join-Path $LogDir "navigator-oversized-ca-bundle-$stamp.pem"
    $seed = Get-Content -Raw $validatedCaFixture
    $builder = New-Object System.Text.StringBuilder
    while ($builder.Length -le (540KB)) {
        [void]$builder.Append($seed)
    }
    [System.IO.File]::WriteAllText($fixturePath, $builder.ToString(), [System.Text.Encoding]::ASCII)
    return $fixturePath
}

function Get-NavigatorKernelSmokeCaManifest {
    param([AllowNull()][string]$LiteralPath)

    if ([string]::IsNullOrWhiteSpace($LiteralPath)) {
        return $null
    }
    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        return $null
    }

    return (Get-Content -LiteralPath $LiteralPath -Raw | ConvertFrom-Json)
}

function Add-NavigatorKernelSmokeRealPublicProbeManifestLines {
    param(
        [Parameter(Mandatory = $true)][string]$SerialLogPath,
        [Parameter(Mandatory = $true)][string]$Output,
        [AllowNull()]$StageInfo
    )

    $manifest = if ($StageInfo) { $StageInfo.ProductionManifest } else { $null }
    $lines = @()
    if ($null -eq $manifest) {
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_manifest_present=no"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_sha256=(not-available)"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_type=(not-available)"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_root_count=(not-available)"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_production_ready=no"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_test_only=(not-available)"
    } else {
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_manifest_present=yes"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_sha256=$($manifest.sha256)"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_type=$($manifest.bundle_type)"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_root_count=$($manifest.root_count)"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_production_ready=$($manifest.production_ready)"
        $lines += "[NAVIGATOR-SMOKE] https.case.real_public_probe.trust_bundle_test_only=$($manifest.test_only)"
    }

    Add-Content -LiteralPath $SerialLogPath -Value $lines
    $suffix = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
    return $Output + $suffix
}

function Wait-NavigatorSmokeFileUnlock {
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [int]$TimeoutMilliseconds = 10000
    )

    if (-not (Test-Path -LiteralPath $LiteralPath)) {
        return
    }

    $deadline = (Get-Date).AddMilliseconds($TimeoutMilliseconds)
    do {
        try {
            $stream = [System.IO.File]::Open($LiteralPath,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
            $stream.Dispose()
            return
        } catch {
            Start-Sleep -Milliseconds 200
        }
    } while ((Get-Date) -lt $deadline)

    throw "Timed out waiting for file unlock: $LiteralPath"
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

function Test-NavigatorKernelSmokeTlsClientHelloEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$HttpsLogPath
    )

    if (-not (Test-Path -LiteralPath $HttpsLogPath -PathType Leaf)) {
        return @("TLS clienthello evidence log is missing: $HttpsLogPath")
    }
    $output = Get-Content -LiteralPath $HttpsLogPath -Raw
    $missing = @()
    if (-not [regex]::IsMatch($output, 'TLS clienthello metadata .*real_suite_count=[1-9][0-9]* .*scsv_only=no .*canonical_offer=yes')) {
        $missing += "TLS clienthello must contain a real canonical suite offer and must not be SCSV-only."
    }
    if (-not [regex]::IsMatch($output, 'TLS handshake ok .*protocol=TLSv1\.2 .*fixture_suite_contract=yes')) {
        $missing += "TLS fixture handshake must negotiate TLS 1.2 with a canonical contract suite."
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

function Test-NavigatorKernelSmokeProductionPolicy {
    param($Scenario)

    $policyText = [string]$Scenario.HttpsPolicy
    return $policyText -match '(?im)^\s*production-validated\s*$'
}

function Test-NavigatorKernelSmokeRealPublicProbeOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Output,
        [Parameter(Mandatory = $true)][string]$Target,
        [bool]$RequireSuccess
    )

    $missing = @()
    $targetHost = $null
    try {
        $targetHost = ([Uri]$Target).Host
    } catch {
        $targetHost = $null
    }
    foreach ($check in @(
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.enabled=yes",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.required=$(if ($RequireSuccess) { 'yes' } else { 'no' })",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.target=$Target",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_bundle_source=",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_bytes=",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_parsed_certs=",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_present=",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_hash_match=",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_bundle_type=",
        "[NAVIGATOR-SMOKE] https.case.real_public_probe.plaintext_fallback=no"
    )) {
        if (-not $Output.Contains($check)) {
            $missing += $check
        }
    }

    $resultMatch = [regex]::Match($Output, '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.result=(PASS|SKIP|FAIL)')
    if (-not $resultMatch.Success) {
        $probeStarted =
            $Output.Contains("[NAVIGATOR-SMOKE] https.case.real_public_probe.attempted=yes") -and
            (($targetHost -and $Output.Contains("[DNS] Cached: $targetHost ->")) -or
             $Output.Contains("[VFS] Opened: /config/navigator/RPUBURL.TXT"))
        if ($probeStarted -and -not $RequireSuccess) {
            return [pscustomobject]@{
                Missing = @()
                Result = "SKIP"
            }
        }
        $missing += "[NAVIGATOR-SMOKE] https.case.real_public_probe.result=<PASS|SKIP|FAIL>"
        return [pscustomobject]@{
            Missing = $missing
            Result = $null
        }
    }

    $result = $resultMatch.Groups[1].Value
    switch ($result) {
        "PASS" {
            $passPatterns = @(
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.attempted=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.public_trust_ready=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.trust_bundle_manifest_present=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.trust_bundle_sha256=[0-9a-f]{64}',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.trust_bundle_type=production-public-probe-merged',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.trust_bundle_root_count=[1-9][0-9]*',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.trust_bundle_production_ready=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.trust_bundle_test_only=no',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.runtime_manifest_present=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.runtime_manifest_hash_match=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.runtime_manifest_bundle_type=production-public-probe-merged',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.runtime_manifest_production_ready=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.runtime_manifest_test_only=no',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.runtime_manifest_root_count=[1-9][0-9]*',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.runtime_manifest_sha256=[0-9a-f]{64}',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.transport_selection=PolicyValidatedTlsHttps',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.tls_status=Success',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.dns_result=PASS',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.tcp_result=PASS',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.tls_result=PASS',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.tls_validated=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.certificate_validation_result=PASS',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.hostname_validated=yes',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.hostname_validation_result=PASS',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.verify_flags=0',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.tls_tcp_connect_attempts=[1-9][0-9]*',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.source_type=https',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.header_cap_hit=(yes|no)',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.body_cap_hit=(yes|no)',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.downgrade_blocked=(yes|no)',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.tls_succeeded_before_content_failure=(yes|no)',
                '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.unsupported_reason=.*'
            )
            foreach ($pattern in $passPatterns) {
                if (-not [regex]::IsMatch($Output, $pattern)) {
                    $missing += $pattern
                }
            }
        }
        "SKIP" {
            if ($RequireSuccess) {
                $missing += "Real public HTTPS probe was required but reported SKIP."
            }
            if (-not [regex]::IsMatch($Output, '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.attempted=(yes|no)')) {
                $missing += "[NAVIGATOR-SMOKE] https.case.real_public_probe.attempted=<yes|no>"
            }
            if (-not [regex]::IsMatch($Output, '\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\.skip_reason=(?!\(none\)).+')) {
                $missing += "[NAVIGATOR-SMOKE] https.case.real_public_probe.skip_reason=<probe blocker>"
            }
        }
        "FAIL" {
            $missing += "Real public HTTPS probe reported FAIL."
        }
    }

    return [pscustomobject]@{
        Missing = $missing
        Result = $result
    }
}

function Get-NavigatorKernelSmokePolicyHost {
    param($Scenario)

    if ($Scenario.PolicyHost) {
        return [string]$Scenario.PolicyHost
    }
    if (Test-NavigatorKernelSmokeProductionPolicy -Scenario $Scenario) {
        return "prod.guidexos.test"
    }
    return "dev.guidexos.test"
}

function Get-NavigatorKernelSmokePolicyCertPair {
    param($Scenario)

    $policyHost = Get-NavigatorKernelSmokePolicyHost -Scenario $Scenario
    if ($policyHost -eq "prod.guidexos.test") {
        if ($Scenario.HttpsFaultMode -eq "untrusted-root") {
            return @($untrustedPolicyProdHttpsCert, $untrustedPolicyProdHttpsKey)
        }
        if ($Scenario.HttpsFaultMode -eq "expired-cert") {
            return @($expiredPolicyProdHttpsCert, $expiredPolicyProdHttpsKey)
        }
        return @($policyProdHttpsCert, $policyProdHttpsKey)
    }

    if ($Scenario.HttpsFaultMode -eq "untrusted-root") {
        return @($untrustedPolicyDevHttpsCert, $untrustedPolicyDevHttpsKey)
    }
    return @($policyDevHttpsCert, $policyDevHttpsKey)
}

function Get-NavigatorKernelSmokePublicPilotHost {
    param($Scenario)

    if ($Scenario.PublicPilotHost) {
        return [string]$Scenario.PublicPilotHost
    }
    return "public-pilot.guidexos.test"
}

function Get-NavigatorKernelSmokePublicPilotCertPair {
    param($Scenario)

    return @($publicPilotHttpsCert, $publicPilotHttpsKey)
}

$realPublicProbeEnabled = Test-NavigatorSmokeEnvFlag ([Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS", "Process"))
$realPublicProbeRequired = Test-NavigatorSmokeEnvFlag ([Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS", "Process"))
if ($realPublicProbeRequired) {
    $realPublicProbeEnabled = $true
}
$realPublicProbeTarget = [Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL", "Process")
if ([string]::IsNullOrWhiteSpace($realPublicProbeTarget)) {
    $realPublicProbeTarget = [Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET", "Process")
}
if ([string]::IsNullOrWhiteSpace($realPublicProbeTarget)) {
    $realPublicProbeTarget = "https://sha256.badssl.com/"
} else {
    $realPublicProbeTarget = $realPublicProbeTarget.Trim()
}
$realPublicProbeCaBundleSource = [Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE", "Process")
if (-not [string]::IsNullOrWhiteSpace($realPublicProbeCaBundleSource)) {
    $realPublicProbeCaBundleSource = $realPublicProbeCaBundleSource.Trim()
}

$commonChecks = @(
    "[NAVIGATOR-SMOKE] BEGIN",
    "[NAVIGATOR] throbber_animation=passive_elapsed_time",
    "[DESKTOP] cooperative_ui_pump=input+render",
    "[NAVIGATOR-SMOKE] registered=true",
    "[NAVIGATOR-SMOKE] runtime.mode=bare-metal/kernel",
    "[NAVIGATOR-SMOKE] launch.path=AppManager::registerApp -> NavigatorApp::create",
    "[NAVIGATOR-SMOKE] capability.http=enabled numeric IPv4 and hostname HTTP/1.0 GET/POST with redirects/chunked",
    "[NAVIGATOR-SMOKE] capability.http_transport=shared HttpByteStream policy layer (PlainTcpHttp + LocalAllowlistedTlsHttps + PolicyValidatedTlsHttps)",
    "[NAVIGATOR-SMOKE] capability.tls_policy_layer=shared HttpByteStream transport policy layer selects plain TCP HTTP, local allowlisted Mbed TLS, or policy-validated Mbed TLS; validated fixture hosts stay policy-gated, public HTTPS stays pilot-gated, plaintext fallback stays disabled, and policy stays fail-closed by default",
    "[NAVIGATOR-SMOKE] tls_prereq.rng_quality=Secure",
    "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_status=Plausible",
    "[NAVIGATOR-SMOKE] tls_prereq.hostname_validation_available=yes",
    "[NAVIGATOR-SMOKE] tls_smoke.result=PASS",
    "[NAVIGATOR-SMOKE] tls_smoke.tls_backend=mbedtls",
    "[NAVIGATOR-SMOKE] tls_smoke.evidence_lane=kernel_local_fixture",
    "[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract=explicit_bounded",
    "[NAVIGATOR-SMOKE] tls_smoke.contract_negative.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.result=PASS",
    "[NAVIGATOR-SMOKE] tls_smoke.failure.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.local_scope_block.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.direct_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.redirect_public_unsupported.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_decision.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_html_200.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_text_200.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_404.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_500.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_download.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_gzip.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_br.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_deflate.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_redirect_relative.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_redirect_absolute.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_redirect_loop.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_large_body.result=PASS",
    "[NAVIGATOR-SMOKE] https.case.compat_large_headers.result=PASS",
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
    "[NAVIGATOR-SMOKE] result=PASS",
    "[NAVIGATOR-SMOKE] END"
)

$manifestBlockedChecks = @(
    $commonChecks | Where-Object {
        $_ -ne "[NAVIGATOR-SMOKE] tls_smoke.result=PASS" -and
        $_ -ne "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.result=PASS" -and
        $_ -ne "[NAVIGATOR-SMOKE] result=PASS"
    }
) + @(
    "[NAVIGATOR-SMOKE] tls_smoke.result=FAIL",
    "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.result=FAIL",
    "[NAVIGATOR-SMOKE] result=FAIL"
)

$commonRegexChecks = @{
    '\[NAVIGATOR-SMOKE\] tls_prereq\.wall_clock_epoch=[1-9][0-9]+' = "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_epoch=<positive Unix seconds>"
    '\[NAVIGATOR-SMOKE\] tls_prereq\.wall_clock_utc=20[2-9][0-9]-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z' = "[NAVIGATOR-SMOKE] tls_prereq.wall_clock_utc=<plausible UTC date>"
    '\[NAVIGATOR-SMOKE\] tls_prereq\.tls_backend_status=(ReadyForLocalHandshake|CaMissing|CaParseFailed)' = "[NAVIGATOR-SMOKE] tls_prereq.tls_backend_status=<expected backend status>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_html_200\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_html_200.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_text_200\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_text_200.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_404\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_404.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_500\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_500.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_download\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_download.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_gzip\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_gzip.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_br\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_br.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_deflate\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_deflate.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_redirect_relative\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_redirect_relative.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_redirect_absolute\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_redirect_absolute.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_redirect_loop\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_redirect_loop.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_large_body\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_large_body.enabled=<yes|no>"
    '\[NAVIGATOR-SMOKE\] https\.case\.compat_large_headers\.enabled=(yes|no)' = "[NAVIGATOR-SMOKE] https.case.compat_large_headers.enabled=<yes|no>"
}

$localTlsSuccessRegexChecks = @{
    '\[NAVIGATOR-SMOKE\] tls_smoke\.protocol=TLSv1\.[23]' = "[NAVIGATOR-SMOKE] tls_smoke.protocol=<TLSv1.2 or TLSv1.3>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.cipher_suite=.+' = "[NAVIGATOR-SMOKE] tls_smoke.cipher_suite=<non-empty cipher suite>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.tls_suite_contract_count=4' = "[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract_count=4"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.tls_suite_contract_real_count=4' = "[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract_real_count=4"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.tls_suite_contract_installed=yes' = "[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract_installed=yes"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.tls_clienthello_real_suite_count=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_smoke.tls_clienthello_real_suite_count=<positive>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.tls_clienthello_scsv_only=no' = "[NAVIGATOR-SMOKE] tls_smoke.tls_clienthello_scsv_only=no"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.tls_clienthello_contract_match=yes' = "[NAVIGATOR-SMOKE] tls_smoke.tls_clienthello_contract_match=yes"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.tls_negotiated_suite=.+' = "[NAVIGATOR-SMOKE] tls_smoke.tls_negotiated_suite=<non-empty suite>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.verify_flags=0' = "[NAVIGATOR-SMOKE] tls_smoke.verify_flags=0"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.failure\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_smoke.failure.verify_flags=<positive mismatch flags>"
}

$localTlsSelectedBlockedRegexChecks = @{
    '\[NAVIGATOR-SMOKE\] tls_smoke\.verify_flags=0' = "[NAVIGATOR-SMOKE] tls_smoke.verify_flags=0"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.failure\.verify_flags=0' = "[NAVIGATOR-SMOKE] tls_smoke.failure.verify_flags=0"
}

$localTlsExplicitPolicyTrustMismatchRegexChecks = @{
    '\[NAVIGATOR-SMOKE\] tls_smoke\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_smoke.verify_flags=<positive failure flags>"
    '\[NAVIGATOR-SMOKE\] tls_smoke\.failure\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_smoke.failure.verify_flags=<positive mismatch flags>"
    '\[NAVIGATOR-SMOKE\] https\.case\.redirect_allowlisted\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.verify_flags=<positive failure flags>"
}

$publicPilotDisabledChecks = @(
    "[NAVIGATOR-SMOKE] https.case.public_pilot_decision.enabled=no",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.enabled=no",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.enabled=no",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.enabled=no"
)

$publicPilotEnabledChecks = @(
    "[NAVIGATOR-SMOKE] https.case.public_pilot_decision.enabled=yes",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_decision.transport_selection=PolicyValidatedTlsHttps",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_decision.reason=ProductionValidated public HTTPS pilot matched.",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.enabled=yes",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.http_status=200",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.tls_tcp_connect_attempts=1",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.verify_flags=0",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.sni_host=public-pilot.guidexos.test",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.transport_selection=PolicyValidatedTlsHttps",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.tls_status=Success",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.enabled=yes",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.http_status=200",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.redirect_count=1",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.plain_tcp_connect_attempts=1",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.tls_tcp_connect_attempts=1",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.transport_selection=PolicyValidatedTlsHttps",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.tls_status=Success",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.enabled=yes",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.redirect_count=1",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.plain_tcp_connect_attempts=0",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.tls_tcp_connect_attempts=1",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.transport_selection=BlockedPolicy",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.tls_status=PolicyBlocked",
    "[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.error=HTTPS downgrade redirect blocked"
)

$realPublicProbeDisabledChecks = @(
    "[NAVIGATOR-SMOKE] https.case.real_public_probe.enabled=no",
    "[NAVIGATOR-SMOKE] https.case.real_public_probe.required=no",
    "[NAVIGATOR-SMOKE] https.case.real_public_probe.target=https://sha256.badssl.com/",
    "[NAVIGATOR-SMOKE] https.case.real_public_probe.attempted=no",
    "[NAVIGATOR-SMOKE] https.case.real_public_probe.skip_reason=Opt-in real public HTTPS probe is disabled.",
    "[NAVIGATOR-SMOKE] https.case.real_public_probe.plaintext_fallback=no",
    "[NAVIGATOR-SMOKE] https.case.real_public_probe.result=SKIP"
)

$oversizedCaFixture = New-NavigatorOversizedCaBundleFixture

$scenarioDefinitions = @(
    [pscustomobject]@{
        Name = "no_policy"
        HttpsPolicy = $null
        HttpsFaultMode = $null
        UseSmokeFixture = $true
        UserCaSource = $null
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=smoke-only",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=SmokeFixtureTrust",
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=LocalSmokeOnly",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=default-safe policy (no /config/navigator/https-policy.txt)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=(none)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_reason=Public HTTPS pilot is disabled while the smoke-only trust fixture is active.",
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
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra (Merge-CheckMaps -Base $localTlsSuccessRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_bytes=<positive bytes>"
            '\[NAVIGATOR-SMOKE\] vfs\.system_block_device=[0-9]+' = "[NAVIGATOR-SMOKE] vfs.system_block_device=<numeric block device>"
            '\[NAVIGATOR-SMOKE\] vfs\.certs_block_device=[0-9]+' = "[NAVIGATOR-SMOKE] vfs.certs_block_device=<numeric block device>"
            '\[NAVIGATOR-SMOKE\] vfs\.certs_file_read_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] vfs.certs_file_read_bytes=<positive bytes>"
        })
    },
    [pscustomobject]@{
        Name = "invalid_policy"
        HttpsPolicy = "definitely-invalid"
        HttpsFaultMode = $null
        UseSmokeFixture = $true
        UserCaSource = $null
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=LocalSmokeOnly",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=default-safe policy (no /config/navigator/https-policy.txt)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=HTTPS policy config is invalid; falling back to the default-safe policy.",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
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
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsSuccessRegexChecks
    },
    [pscustomobject]@{
        Name = "user_dev_missing_ca"
        HttpsPolicy = "user-trust-dev-mode"
        HttpsFaultMode = $null
        UseSmokeFixture = $true
        UserCaSource = $null
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_status=Missing",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=VFS config file /config/navigator/https-policy.txt",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=User/dev HTTPS policy requires /config/certs/ca-bundle.pem.",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsSelectedBlockedRegexChecks
    },
    [pscustomobject]@{
        Name = "user_dev_malformed_ca"
        HttpsPolicy = "user-trust-dev-mode"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $malformedCaFixture
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_status=Invalid",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_parse_status=NotAttempted",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_detail=Root CA bundle does not look like a PEM certificate bundle.",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=UserProvidedTrustStore",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=Root CA bundle does not look like a PEM certificate bundle.",
            "[NAVIGATOR-SMOKE] tls_readiness=no"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsSelectedBlockedRegexChecks
    },
    [pscustomobject]@{
        Name = "user_dev_empty_ca"
        HttpsPolicy = "user-trust-dev-mode"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $emptyCaFixture
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_status=Invalid",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_detail=Root CA bundle is empty.",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=UserProvidedTrustStore",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=Root CA bundle is empty.",
            "[NAVIGATOR-SMOKE] tls_readiness=no"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsSelectedBlockedRegexChecks
    },
    [pscustomobject]@{
        Name = "user_dev_oversized_ca"
        HttpsPolicy = "user-trust-dev-mode"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $oversizedCaFixture
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_status=TooLarge",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_detail=Root CA bundle exceeds the 512 KiB safety cap.",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=UserProvidedTrustStore",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=Root CA bundle exceeds the 512 KiB safety cap.",
            "[NAVIGATOR-SMOKE] tls_readiness=no"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsSelectedBlockedRegexChecks
    },
    [pscustomobject]@{
        Name = "user_dev_policy"
        HttpsPolicy = "user-trust-dev-mode"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $validatedCaFixture
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=normal",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=UserProvidedTrustStore",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source_detail=User-provided trust store loaded from /config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_path=/config/certs/ca-bundle.manifest",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_status=Loaded",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_present=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_bundle_type=user-dev",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_hash_match=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_production_ready=no",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_test_only=no",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_public_ready=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=VFS config file /config/navigator/https-policy.txt",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=(none)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_reason=Public HTTPS pilot is unavailable in UserTrustStoreDevMode.",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_production_ready=no",
            "[NAVIGATOR-SMOKE] tls_readiness=yes",
            "[NAVIGATOR-SMOKE] tls_readiness_blocker=(none)",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.fault_mode=None",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_backend=mbedtls",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.evidence_lane=kernel_local_fixture",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract=explicit_bounded",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract_count=4",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract_real_count=4",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract_installed=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_clienthello_scsv_only=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_clienthello_contract_match=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.certificate_validated=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.hostname_validated=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.fault_mode=None",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.fault_mode=None",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra (Merge-CheckMaps -Base $localTlsExplicitPolicyTrustMismatchRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_bytes=<positive bytes>"
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_manifest_sha256=[0-9a-f]{64}' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_sha256=<64 hex>"
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_computed_sha256=[0-9a-f]{64}' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_computed_sha256=<64 hex>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.protocol=TLSv1\.[23]' = "[NAVIGATOR-SMOKE] https.case.policy_validated.protocol=<TLSv1.2 or TLSv1.3>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.cipher_suite=.+' = "[NAVIGATOR-SMOKE] https.case.policy_validated.cipher_suite=<non-empty cipher suite>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.http_status=200' = "[NAVIGATOR-SMOKE] https.case.policy_validated.http_status=200"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.content_type=(text/html|text/plain)' = "[NAVIGATOR-SMOKE] https.case.policy_validated.content_type=<HTML or text>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.body_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated.body_bytes=<positive>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.parsed_blocks=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated.parsed_blocks=<positive>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.tls_clienthello_real_suite_count=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_clienthello_real_suite_count=<positive>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.tls_negotiated_suite=.+' = "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_negotiated_suite=<non-empty suite>"
        })
    },
    [pscustomobject]@{
        Name = "user_dev_manifest_hash_mismatch"
        HttpsPolicy = "user-trust-dev-mode"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $validatedCaFixture
        UserManifestMode = "hash-mismatch"
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $manifestBlockedChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/config/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=UserProvidedTrustStore",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_status=Loaded",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_present=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_hash_match=no",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_error=CA bundle manifest sha256 does not match the loaded PEM bytes.",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_readiness_blocker=CA bundle manifest sha256 does not match the loaded PEM bytes.",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=CA bundle manifest sha256 does not match the loaded PEM bytes.",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsExplicitPolicyTrustMismatchRegexChecks
    },
    [pscustomobject]@{
        Name = "user_dev_untrusted_root"
        HttpsPolicy = "user-trust-dev-mode"
        HttpsFaultMode = "untrusted-root"
        UseSmokeFixture = $false
        UserCaSource = $validatedCaFixture
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=UserTrustStoreDevMode",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_readiness=yes",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra (Merge-CheckMaps -Base $localTlsExplicitPolicyTrustMismatchRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated.verify_flags=<positive failure flags>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated_redirect\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.verify_flags=<positive failure flags>"
        })
    },
    [pscustomobject]@{
        Name = "production_missing_ca_user_only"
        HttpsPolicy = "production-validated"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $validatedCaFixture
        ProductionCaSource = $null
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_status=Missing",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=Production HTTPS policy requires a non-smoke bundle at /certs/ca-bundle.pem.",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=no",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsSelectedBlockedRegexChecks
    },
    [pscustomobject]@{
        Name = "production_validated"
        HttpsPolicy = "production-validated"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=normal",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=ProductionRootStore",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source_detail=Production root store loaded from /certs/ca-bundle.pem.",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_path=/certs/ca-bundle.manifest",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_status=Loaded",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_present=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_bundle_type=production-source",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_hash_match=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_production_ready=no",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_test_only=no",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_public_ready=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=VFS config file /config/navigator/https-policy.txt",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=(none)",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_reason=Public HTTPS pilot requires public-https-pilot=enabled under ProductionValidated.",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_production_ready=yes",
            "[NAVIGATOR-SMOKE] tls_readiness=yes",
            "[NAVIGATOR-SMOKE] tls_readiness_blocker=(none)",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.fault_mode=None",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.fault_mode=None",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.transport_selection=PolicyValidatedTlsHttps",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.fault_mode=None",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra (Merge-CheckMaps -Base $localTlsExplicitPolicyTrustMismatchRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_bytes=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_bytes=<positive bytes>"
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_manifest_sha256=[0-9a-f]{64}' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_sha256=<64 hex>"
            '\[NAVIGATOR-SMOKE\] tls_prereq\.root_ca_computed_sha256=[0-9a-f]{64}' = "[NAVIGATOR-SMOKE] tls_prereq.root_ca_computed_sha256=<64 hex>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.protocol=TLSv1\.[23]' = "[NAVIGATOR-SMOKE] https.case.policy_validated.protocol=<TLSv1.2 or TLSv1.3>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.cipher_suite=.+' = "[NAVIGATOR-SMOKE] https.case.policy_validated.cipher_suite=<non-empty cipher suite>"
        })
    },
    [pscustomobject]@{
        Name = "production_missing_manifest"
        HttpsPolicy = "production-validated"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        ProductionManifestMode = "missing"
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $manifestBlockedChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=ProductionRootStore",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_status=Missing",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_present=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=CA bundle manifest not found at /certs/ca-bundle.manifest.",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_readiness_blocker=CA bundle manifest not found at /certs/ca-bundle.manifest.",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsExplicitPolicyTrustMismatchRegexChecks
    },
    [pscustomobject]@{
        Name = "production_manifest_test_only"
        HttpsPolicy = "production-validated"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        ProductionManifestMode = "test-only"
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $manifestBlockedChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=ProductionRootStore",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_status=Loaded",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_present=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_test_only=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=CA bundle manifest is marked test_only=yes; broader validated HTTPS remains fail-closed.",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_readiness_blocker=CA bundle manifest is marked test_only=yes; broader validated HTTPS remains fail-closed.",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsExplicitPolicyTrustMismatchRegexChecks
    },
    [pscustomobject]@{
        Name = "production_manifest_hash_mismatch"
        HttpsPolicy = "production-validated"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        ProductionManifestMode = "hash-mismatch"
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        Checks = $manifestBlockedChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=ProductionRootStore",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_status=Loaded",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_present=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_hash_match=no",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_error=CA bundle manifest sha256 does not match the loaded PEM bytes.",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=Disabled",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=CA bundle manifest sha256 does not match the loaded PEM bytes.",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_readiness_blocker=CA bundle manifest sha256 does not match the loaded PEM bytes.",
            "[NAVIGATOR-SMOKE] tls_readiness=no",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsExplicitPolicyTrustMismatchRegexChecks
    },
    [pscustomobject]@{
        Name = "production_public_pilot_enabled"
        HttpsPolicy = "production-validated`npublic-https-pilot=enabled"
        HttpsFaultMode = $null
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        TlsCert = $defaultHttpsCert
        TlsKey = $defaultHttpsKey
        PublicPilotEnabled = $true
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=/certs/ca-bundle.pem",
            "[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=normal",
            "[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=ProductionPublicProbeTrust",
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=None",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_reason=Public HTTPS pilot is enabled for hostname-only HTTPS targets under ProductionValidated.",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_production_ready=yes",
            "[NAVIGATOR-SMOKE] tls_readiness=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra $localTlsExplicitPolicyTrustMismatchRegexChecks
    },
    [pscustomobject]@{
        Name = "production_untrusted_root"
        HttpsPolicy = "production-validated"
        HttpsFaultMode = "untrusted-root"
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        TlsCert = $untrustedHttpsCert
        TlsKey = $untrustedHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_readiness=yes",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.fault_mode=UntrustedRoot",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra (Merge-CheckMaps -Base $localTlsExplicitPolicyTrustMismatchRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated.verify_flags=<positive failure flags>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated_redirect\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.verify_flags=<positive failure flags>"
        })
    },
    [pscustomobject]@{
        Name = "production_expired_cert"
        HttpsPolicy = "production-validated"
        HttpsFaultMode = "expired-cert"
        UseSmokeFixture = $false
        UserCaSource = $null
        ProductionCaSource = $validatedCaFixture
        TlsCert = $expiredHttpsCert
        TlsKey = $expiredHttpsKey
        Checks = $commonChecks + @(
            "[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=ExpiredCertificate",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=ProductionValidated",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=yes",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=no",
            "[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=no",
            "[NAVIGATOR-SMOKE] tls_readiness=yes",
            "[NAVIGATOR-SMOKE] tls_smoke.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=yes",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.fault_mode=ExpiredCertificate",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.fault_mode=ExpiredCertificate",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=PASS",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.fault_mode=ExpiredCertificate",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.tls_status=CertificateVerifyFailed",
            "[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=PASS"
        )
        RegexChecks = Merge-CheckMaps -Base $commonRegexChecks -Extra (Merge-CheckMaps -Base $localTlsExplicitPolicyTrustMismatchRegexChecks -Extra @{
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated.verify_flags=<positive failure flags>"
            '\[NAVIGATOR-SMOKE\] https\.case\.policy_validated_redirect\.verify_flags=[1-9][0-9]*' = "[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.verify_flags=<positive failure flags>"
        })
    }
)

$publicPilotScenarioNames = @(
    "production_public_pilot_enabled"
)

function Get-NavigatorKernelSmokeScenarioLane {
    param([Parameter(Mandatory = $true)]$Scenario)

    if ($publicPilotScenarioNames -contains [string]$Scenario.Name) {
        return "PublicPilot"
    }
    return "Deterministic"
}

function Test-NavigatorKernelSmokeScenarioMatchesGroup {
    param(
        [Parameter(Mandatory = $true)]$Scenario,
        [Parameter(Mandatory = $true)][string]$EffectiveScenarioGroup
    )

    $lane = Get-NavigatorKernelSmokeScenarioLane -Scenario $Scenario
    switch ($EffectiveScenarioGroup) {
        "All" { return $true }
        "PublicPilot" { return $lane -eq "PublicPilot" }
        default { return $lane -eq "Deterministic" }
    }
}

$effectiveScenarioGroup = if ($IncludePublicPilot) { "All" } else { $ScenarioGroup }
$selectedScenarios = @($scenarioDefinitions | Where-Object {
    Test-NavigatorKernelSmokeScenarioMatchesGroup -Scenario $_ -EffectiveScenarioGroup $effectiveScenarioGroup
})
if ($ScenarioFilter -and $ScenarioFilter.Count -gt 0) {
    $requestedScenarioNames = @()
    foreach ($scenarioName in $ScenarioFilter) {
        if (-not [string]::IsNullOrWhiteSpace($scenarioName)) {
            $requestedScenarioNames += $scenarioName.Trim()
        }
    }
    if ($requestedScenarioNames.Count -le 0) {
        throw "ScenarioFilter was provided but did not include any scenario names."
    }

    $selectedScenarios = @()
    $missingScenarioNames = @()
    foreach ($requestedScenarioName in $requestedScenarioNames) {
        $match = $scenarioDefinitions | Where-Object {
            [string]::Equals([string]$_.Name, $requestedScenarioName, [System.StringComparison]::OrdinalIgnoreCase)
        } | Select-Object -First 1
        if ($null -eq $match) {
            $missingScenarioNames += $requestedScenarioName
            continue
        }
        $selectedScenarios += $match
    }

    if ($missingScenarioNames.Count -gt 0) {
        throw "Unknown kernel smoke scenario filter(s): $($missingScenarioNames -join ', ')"
    }
} elseif ($selectedScenarios.Count -le 0) {
    throw "ScenarioGroup '$effectiveScenarioGroup' did not select any kernel smoke scenarios."
}

if ((-not [string]::IsNullOrWhiteSpace($CandidateRotationId)) -and [string]::IsNullOrWhiteSpace($CandidateBundlePath)) {
    throw "CandidateRotationId requires CandidateBundlePath."
}
if ($CandidateReviewed -and [string]::IsNullOrWhiteSpace($CandidateBundlePath)) {
    throw "CandidateReviewed requires CandidateBundlePath."
}

$resolvedCandidateBundlePath = Resolve-NavigatorKernelSmokeOptionalPath -PathValue $CandidateBundlePath
$effectiveCandidateRotationId = $(if ([string]::IsNullOrWhiteSpace($CandidateRotationId)) { $null } else { $CandidateRotationId.Trim() })
if ($resolvedCandidateBundlePath) {
    $nonPublicPilotScenarios = @($selectedScenarios | Where-Object {
        (Get-NavigatorKernelSmokeScenarioLane -Scenario $_) -ne "PublicPilot"
    })
    if ($nonPublicPilotScenarios.Count -gt 0) {
        throw "CandidateBundlePath is only supported for the PublicPilot lane. Use -ScenarioGroup PublicPilot, -IncludePublicPilot with ScenarioFilter, or filter directly to public-pilot scenarios."
    }
}

Write-Host "Kernel smoke scenario selection: group=$effectiveScenarioGroup count=$($selectedScenarios.Count)"

foreach ($scenario in $selectedScenarios) {
    if ($scenario.PSObject.Properties.Match("PublicPilotEnabled").Count -gt 0 -and $scenario.PublicPilotEnabled) {
        $scenario.Checks = @($scenario.Checks + $publicPilotEnabledChecks)
        if (-not $realPublicProbeEnabled) {
            $scenario.Checks = @($scenario.Checks + $realPublicProbeDisabledChecks)
        }
        continue
    }
    $scenario.Checks = @($scenario.Checks + $publicPilotDisabledChecks + $realPublicProbeDisabledChecks)
}

$scenarioFailures = @()
$activeServers = $null

try {
    foreach ($scenario in $selectedScenarios) {
        Write-Host "Running kernel smoke scenario '$($scenario.Name)'..."
        $enableRealPublicProbeForScenario =
            $realPublicProbeEnabled -and
            $scenario.PSObject.Properties.Match("PublicPilotEnabled").Count -gt 0 -and
            $scenario.PublicPilotEnabled
        $scenarioCandidateCaSource = $(if ($scenario.PSObject.Properties.Match("CandidateCaSource").Count -gt 0) { $scenario.CandidateCaSource } else { $null })
        $scenarioCandidateRotationId = $(if ($scenario.PSObject.Properties.Match("CandidateRotationId").Count -gt 0) { $scenario.CandidateRotationId } else { $null })
        $scenarioCandidateProductionReady = $(if ($scenario.PSObject.Properties.Match("CandidateProductionReady").Count -gt 0) { [bool]$scenario.CandidateProductionReady } else { $false })
        $effectiveCandidateCaSource = $(if ($resolvedCandidateBundlePath) { $resolvedCandidateBundlePath } else { $scenarioCandidateCaSource })
        $effectiveCandidateRotationIdForScenario = $(if ($effectiveCandidateRotationId) { $effectiveCandidateRotationId } else { $scenarioCandidateRotationId })
        $effectiveCandidateProductionReadyForScenario = $(if ($resolvedCandidateBundlePath) { [bool]$CandidateReviewed } else { $scenarioCandidateProductionReady })
        $effectiveProductionCaSource = $(if ($effectiveCandidateCaSource) { $null } else { $scenario.ProductionCaSource })
        $stageInfo = Invoke-NavigatorKernelSmokeRamdiskStage -HttpsPolicy $scenario.HttpsPolicy `
            -HttpsFaultMode $scenario.HttpsFaultMode `
            -UserCaSource $scenario.UserCaSource `
            -ProductionCaSource $effectiveProductionCaSource `
            -CandidateCaSource $effectiveCandidateCaSource `
            -CandidateRotationId $effectiveCandidateRotationIdForScenario `
            -CandidateProductionReady:$effectiveCandidateProductionReadyForScenario `
            -UserManifestMode $(if ($scenario.UserManifestMode) { [string]$scenario.UserManifestMode } else { "normal" }) `
            -ProductionManifestMode $(if ($scenario.ProductionManifestMode) { [string]$scenario.ProductionManifestMode } else { "normal" }) `
            -UseSmokeFixture $scenario.UseSmokeFixture `
            -EnableRealPublicProbe:$enableRealPublicProbeForScenario `
            -RequireRealPublicProbe:($enableRealPublicProbeForScenario -and $realPublicProbeRequired) `
            -RealPublicProbeTarget $(if ($enableRealPublicProbeForScenario) { $realPublicProbeTarget } else { $null })

        try {
            $policyHost = Get-NavigatorKernelSmokePolicyHost -Scenario $scenario
            $policyCertPair = Get-NavigatorKernelSmokePolicyCertPair -Scenario $scenario
            $publicPilotHost = Get-NavigatorKernelSmokePublicPilotHost -Scenario $scenario
            $publicPilotCertPair = Get-NavigatorKernelSmokePublicPilotCertPair -Scenario $scenario
            $activeServers = Start-NavigatorKernelSmokeServers `
                -ScenarioName $scenario.Name `
                -LocalTlsCert $scenario.TlsCert `
                -LocalTlsKey $scenario.TlsKey `
                -PolicyHost $policyHost `
                -PolicyTlsCert $policyCertPair[0] `
                -PolicyTlsKey $policyCertPair[1] `
                -PublicPilotHost $publicPilotHost `
                -PublicPilotTlsCert $publicPilotCertPair[0] `
                -PublicPilotTlsKey $publicPilotCertPair[1]
            $run = Invoke-NavigatorKernelSmokeQemuPass -ScenarioName $scenario.Name
            $runOutput = $run.Output
            if ($enableRealPublicProbeForScenario) {
                $runOutput = Add-NavigatorKernelSmokeRealPublicProbeManifestLines `
                    -SerialLogPath $run.SerialLog `
                    -Output $runOutput `
                    -StageInfo $stageInfo
            }

            Write-Host $runOutput
            $scenarioChecks = @($scenario.Checks)
            if ($enableRealPublicProbeForScenario) {
                $scenarioChecks = @($scenarioChecks | Where-Object { $_ -ne "[NAVIGATOR-SMOKE] result=PASS" })
            }
            $missing = Test-NavigatorKernelSmokeOutput -Output $runOutput -Contains $scenarioChecks -RegexChecks $scenario.RegexChecks
            if ($scenario.Name -eq "production_validated" -and
                $runOutput.Contains("[NAVIGATOR-SMOKE] tls_smoke.local_ready=yes")) {
                $missing += Test-NavigatorKernelSmokeTlsClientHelloEvidence -HttpsLogPath $activeServers.HttpsLog
            }
            if ($enableRealPublicProbeForScenario) {
                $probeCheck = Test-NavigatorKernelSmokeRealPublicProbeOutput `
                    -Output $runOutput `
                    -Target $realPublicProbeTarget `
                    -RequireSuccess:$realPublicProbeRequired
                $missing += $probeCheck.Missing
                if ($probeCheck.Result) {
                    Write-Host "Real public HTTPS probe result for '$($scenario.Name)': $($probeCheck.Result)"
                }
            }
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
        } finally {
            Stop-NavigatorKernelSmokeServers -Servers $activeServers
            $activeServers = $null
        }
    }
} finally {
    Stop-NavigatorKernelSmokeServers -Servers $activeServers
    if ($createdStartup) {
        Remove-Item $startup -ErrorAction SilentlyContinue
    }
    Restore-NavigatorKernelSmokeEnvironment
    Restore-NormalKernelBuild
    Restore-NavigatorSmokeDirectoryState -State $downloadsState
    Restore-NavigatorSmokeFileState -State $ramdiskState
    Restore-NavigatorSmokeDirectoryState -State $wallpaperPackState
    Restore-NavigatorSmokeFileState -State $wallpaperPackCaBundleState
    if (Test-Path $oversizedCaFixture) {
        Remove-Item -LiteralPath $oversizedCaFixture -Force -ErrorAction SilentlyContinue
    }
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

