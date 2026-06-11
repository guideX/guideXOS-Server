param(
    [string]$TargetUrl,
    [string]$Memory = "1024M",
    [switch]$StageOnly
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir
$BuildScript = Join-Path $Root "build.ps1"
$EspDir = Join-Path $Root "ESP"
$PublicBundlePath = Join-Path $Root "scripts\fixtures\public-roots\ca-bundle.pem.local"
$ValidatedFixturePath = Join-Path $Root "scripts\fixtures\navigator-validated-root-ca-bundle.pem"
$PackDir = Join-Path $Root "out\wallpaper-pack"

. (Join-Path $ScriptDir "process_environment.ps1")
Normalize-ProcessEnvironment
. (Join-Path $ScriptDir "navigator-public-https-reviewed-targets.ps1")
# The reviewed-target helper enables strict mode in its caller. build.ps1 is a
# normal interactive build path and currently contains non-strict display code.
Set-StrictMode -Off

function Find-NavigatorScreenshotQemu {
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($qemu) {
        return $qemu.Source
    }

    foreach ($candidate in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe",
        "$env:USERPROFILE\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return $null
}

function Find-NavigatorScreenshotOvmf {
    foreach ($candidate in @(
        (Join-Path $Root "OVMF.fd"),
        (Join-Path $Root "ovmf.fd"),
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return $null
}

function Set-NavigatorScreenshotEnv {
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

function Restore-NavigatorScreenshotHostStage {
    foreach ($path in $script:HostStageFileSnapshots.Keys) {
        $snapshot = $script:HostStageFileSnapshots[$path]
        if ($null -eq $snapshot) {
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        } else {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
            [System.IO.File]::WriteAllBytes($path, $snapshot)
        }
    }

    if (Test-Path -LiteralPath $script:PackConfigPath) {
        Remove-Item -LiteralPath $script:PackConfigPath -Recurse -Force
    }
    if ($script:PackConfigSnapshotPath) {
        Copy-Item -LiteralPath $script:PackConfigSnapshotPath -Destination $script:PackConfigPath -Recurse
        Remove-Item -LiteralPath $script:PackConfigSnapshotPath -Recurse -Force
    }
}

function Clear-NavigatorScreenshotMainObject {
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "main.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

$requestedTarget = if ([string]::IsNullOrWhiteSpace($TargetUrl)) {
    Get-NavigatorPublicHttpsDefaultTarget
} else {
    $TargetUrl.Trim()
}
$reviewed = Test-NavigatorPublicHttpsReviewedTarget -TargetUrl $requestedTarget
if (-not $reviewed.Approved) {
    $approvedTargets = [string]::Join(", ", $reviewed.ApprovedTargets)
    throw "TargetUrl '$requestedTarget' is not in the reviewed public HTTPS allowlist ($approvedTargets)."
}
$canonicalTarget = [string]$reviewed.Match.Url

foreach ($requiredFile in @($BuildScript, $PublicBundlePath, $ValidatedFixturePath)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required screenshot-launch input not found: $requiredFile"
    }
}

$stagingEnvironment = [ordered]@{
    GXOS_NAVIGATOR_SMOKE_CA_FIXTURE = $null
    GXOS_NAVIGATOR_HTTPS_POLICY = "production-validated`npublic-https-pilot=enabled"
    GXOS_NAVIGATOR_HTTPS_FAULT_MODE = $null
    GXOS_NAVIGATOR_USER_CA_BUNDLE_SOURCE = $null
    GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE = $ValidatedFixturePath
    GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_CA_BUNDLE_SOURCE = $null
    GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_ROTATION_ID = $null
    GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_PRODUCTION_READY = $null
    GXOS_NAVIGATOR_USER_CA_MANIFEST_MODE = $null
    GXOS_NAVIGATOR_PRODUCTION_CA_MANIFEST_MODE = $null
    GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS = "1"
    GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS = "1"
    GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL = $canonicalTarget
    GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET = $canonicalTarget
    GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE = $PublicBundlePath
    GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_REVIEWED_OVERRIDE = $null
    EXTRA_CFLAGS = "-DGXOS_NAVIGATOR_BOOT_STAGED_CONFIG_ACTIVE"
}
$originalEnvironment = @{}
foreach ($name in $stagingEnvironment.Keys) {
    $originalEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

$hostStageFiles = @(
    (Join-Path $PackDir "certs\ca-bundle.pem"),
    (Join-Path $PackDir "certs\ca-bundle.manifest"),
    (Join-Path $PackDir "certs\CABUNDLE.MAN")
)
$script:HostStageFileSnapshots = @{}
foreach ($path in $hostStageFiles) {
    $script:HostStageFileSnapshots[$path] = if (Test-Path -LiteralPath $path -PathType Leaf) {
        [System.IO.File]::ReadAllBytes($path)
    } else {
        $null
    }
}
$script:PackConfigPath = Join-Path $PackDir "config"
$script:PackConfigSnapshotPath = $null
if (Test-Path -LiteralPath $script:PackConfigPath -PathType Container) {
    $script:PackConfigSnapshotPath = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-navigator-screenshot-" + [guid]::NewGuid().ToString("N"))
    Copy-Item -LiteralPath $script:PackConfigPath -Destination $script:PackConfigSnapshotPath -Recurse
}

Write-Host "Preparing interactive Navigator public HTTPS screenshot launch..." -ForegroundColor Cyan
Write-Host "  reviewed allowlist: $(Get-NavigatorPublicHttpsReviewedAllowlistName)"
Write-Host "  target: $canonicalTarget"
Write-Host "  public root source: scripts/fixtures/public-roots/ca-bundle.pem.local"

try {
    foreach ($name in $stagingEnvironment.Keys) {
        Set-NavigatorScreenshotEnv -Name $name -Value $stagingEnvironment[$name]
    }

    Clear-NavigatorScreenshotMainObject
    & $BuildScript
    if ($LASTEXITCODE -ne 0) {
        throw "build.ps1 failed while preparing the interactive screenshot launch."
    }

    $policyPath = Join-Path $PackDir "config\navigator\https-policy.txt"
    $targetPath = Join-Path $PackDir "config\navigator\real-public-https-probe-url.txt"
    $bundlePath = Join-Path $PackDir "certs\ca-bundle.pem"
    $manifestPath = Join-Path $PackDir "certs\ca-bundle.manifest"
    foreach ($stagedFile in @($policyPath, $targetPath, $bundlePath, $manifestPath)) {
        if (-not (Test-Path -LiteralPath $stagedFile -PathType Leaf)) {
            throw "Expected screenshot-launch staging output not found: $stagedFile"
        }
    }

    $stagedPolicy = (Get-Content -LiteralPath $policyPath -Raw).Trim()
    if ($stagedPolicy -ne "production-validated`npublic-https-pilot=enabled" -and
        $stagedPolicy -ne "production-validated`r`npublic-https-pilot=enabled") {
        throw "Staged Navigator HTTPS policy does not match the reviewed public-proof policy."
    }
    if ((Get-Content -LiteralPath $targetPath -Raw).Trim() -ne $canonicalTarget) {
        throw "Staged real public HTTPS target does not match the reviewed target."
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.bundle_type -ne "production-public-probe-merged" -or
        $manifest.production_ready -ne "yes" -or
        $manifest.test_only -ne "no" -or
        [int]$manifest.root_count -le 0) {
        throw "Staged public HTTPS CA manifest is not production-ready reviewed-proof material."
    }

    Write-Host "Interactive screenshot staging verified:" -ForegroundColor Green
    Write-Host "  /config/navigator/https-policy.txt: production-validated + public-https-pilot=enabled"
    Write-Host "  /certs/ca-bundle.pem: staged"
    Write-Host "  /certs/ca-bundle.manifest: type=$($manifest.bundle_type), roots=$($manifest.root_count), production_ready=$($manifest.production_ready), test_only=$($manifest.test_only)"
    Write-Host "  reviewed target metadata: $canonicalTarget"
}
finally {
    foreach ($name in $stagingEnvironment.Keys) {
        Set-NavigatorScreenshotEnv -Name $name -Value $originalEnvironment[$name]
    }
    Restore-NavigatorScreenshotHostStage
}

if ($StageOnly) {
    Clear-NavigatorScreenshotMainObject
    Write-Host "StageOnly complete; QEMU was not launched." -ForegroundColor Green
    exit 0
}

$qemu = Find-NavigatorScreenshotQemu
$ovmf = Find-NavigatorScreenshotOvmf
if (-not $qemu) {
    throw "qemu-system-x86_64 was not found."
}
if (-not $ovmf) {
    throw "OVMF firmware was not found."
}

Write-Host ""
Write-Host "Launching visible QEMU for the reviewed public HTTPS screenshot..." -ForegroundColor Cyan
Write-Host "Open Navigator and enter: $canonicalTarget" -ForegroundColor Yellow
Write-Host "Normal boot remains default-safe; this ramdisk was explicitly staged by this launcher." -ForegroundColor DarkGray

$qemuArgs = @(
    "-machine", "pc",
    "-drive", "if=pflash,format=raw,readonly=on,file=$ovmf",
    "-drive", "file=fat:rw:$EspDir,format=raw,if=ide,index=0",
    "-m", $Memory,
    "-vga", "std",
    "-serial", "stdio",
    "-no-reboot",
    "-rtc", "base=utc,clock=host",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0",
    "-object", "rng-builtin,id=rng0",
    "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000"
)

Push-Location $Root
try {
    & $qemu $qemuArgs
}
finally {
    Pop-Location
    Clear-NavigatorScreenshotMainObject
}
