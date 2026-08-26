$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
. (Join-Path $Root "scripts\build-native-command.ps1")

$tempRoot = Join-Path $env:TEMP ("guidexos-build-wrapper-selftest-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

function Assert-BuildWrapperSelfTest {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "Build wrapper self-test failed: $Message" }
}

try {
    $successOutput = Join-Path $tempRoot "success.out"
    $successCommand = @(
        "-NoProfile", "-Command",
        "[Console]::Error.WriteLine('warning-on-stderr'); [IO.File]::WriteAllText('$successOutput', 'ok'); exit 0"
    )
    $success = Invoke-GxosNativeBuildCommand -FilePath "powershell.exe" -ArgumentList $successCommand -ExpectedOutputPaths @($successOutput)
    Assert-BuildWrapperSelfTest ($success.Succeeded -and $success.ExitCode -eq 0) "exit 0 with stderr and output must succeed"

    $failureCommand = @(
        "-NoProfile", "-Command",
        "[Console]::Error.WriteLine('error-on-stderr'); exit 7"
    )
    $failure = Invoke-GxosNativeBuildCommand -FilePath "powershell.exe" -ArgumentList $failureCommand
    Assert-BuildWrapperSelfTest (-not $failure.Succeeded -and $failure.ExitCode -eq 7) "nonzero exit with stderr must fail"

    $missingOutput = Join-Path $tempRoot "missing.out"
    $missingCommand = @(
        "-NoProfile", "-Command",
        "[Console]::Error.WriteLine('no-output-on-stderr'); exit 0"
    )
    $missing = Invoke-GxosNativeBuildCommand -FilePath "powershell.exe" -ArgumentList $missingCommand -ExpectedOutputPaths @($missingOutput)
    Assert-BuildWrapperSelfTest (-not $missing.Succeeded -and $missing.ExitCode -eq 0 -and -not $missing.ExpectedOutputsPresent) "exit 0 with missing output must fail"

    $callerFlags = "-DCALLER_FLAG=1 -DSECOND_CALLER_FLAG=1"
    $combinedFlags = @($callerFlags, "-DGXOS_DESKTOP_CLEANUP_RUNTIME_PASS") -join " "
    Assert-BuildWrapperSelfTest ($combinedFlags.Contains("-DCALLER_FLAG=1") -and
        $combinedFlags.Contains("-DSECOND_CALLER_FLAG=1") -and
        $combinedFlags.Contains("-DGXOS_DESKTOP_CLEANUP_RUNTIME_PASS")) "caller EXTRA_CFLAGS must survive append behavior"

    $buildScript = Get-Content -LiteralPath (Join-Path $Root "build.ps1") -Raw
    Assert-BuildWrapperSelfTest ($buildScript.Contains('build\$Arch\obj\core\kernel_apps.o')) `
        "feature-gated Navigator kernel_apps.o must be invalidated before scripted builds"
    Assert-BuildWrapperSelfTest ($buildScript.Contains('build\$Arch\obj\core\gxos_tls_foundation.o')) `
        "feature-gated TLS foundation object must be invalidated before scripted builds"
    Assert-BuildWrapperSelfTest ($buildScript.Contains('-ArgumentList @("ARCH=$Arch", "EXTRA_CFLAGS=' ) -and
        $buildScript.Contains('$KernelExtraCFlags -join')) `
        "caller EXTRA_CFLAGS must be forwarded to the kernel make invocation"
    Assert-BuildWrapperSelfTest ($buildScript.Contains('Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $KernelBinDir "kernel.elf")')) `
        "stale kernel.elf must be removed before a scripted build"

    Write-Host "Build wrapper self-test PASS"
    exit 0
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
