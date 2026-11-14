[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$KernelElf,
    [string]$OvmfCodePath = (Join-Path $env:ProgramFiles 'qemu/share/edk2-x86_64-code.fd'),
    [string]$OvmfVarsTemplatePath = (Join-Path $env:ProgramFiles 'qemu/share/edk2-i386-vars.fd'),
    [string]$TemplateEsp = (Join-Path (Split-Path -Parent $PSScriptRoot) 'tmp/navhttps13-guest-run/ESP'),
    [int]$TimeoutSeconds = 90,
    [switch]$SkipNavhttps7TrustScenario
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $repo "logs/navhttps15-guest-$stamp"
$stagedEsp = Join-Path $runDir 'ESP'
$serialPath = Join-Path $runDir 'guest-serial.log'
$pcapPath = Join-Path $runDir 'guest-network.pcap'
$qemuStdout = Join-Path $runDir 'qemu.stdout.log'
$qemuStderr = Join-Path $runDir 'qemu.stderr.log'
$ovmfVarsPath = Join-Path $runDir 'OVMF_VARS.fd'
$fixtureLog = Join-Path $runDir 'fixture-server.log'
$fixtureError = Join-Path $runDir 'fixture-server.err.log'
$navhttps7Log = Join-Path $runDir 'navhttps7-trust-scenarios.log'
$qemuCandidates = @(
    (Join-Path $env:ProgramFiles 'qemu/qemu-system-x86_64.exe'),
    (Join-Path $env:ProgramFiles 'QEMU/qemu-system-x86_64.exe')
)
$qemu = $qemuCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $qemu) {
    $command = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    if ($command) { $qemu = $command.Source }
}
if (-not $qemu) { throw 'QEMU was not found; no guest or fixture was launched.' }

$resolvedKernel = (Resolve-Path -LiteralPath $KernelElf).Path
$resolvedOvmfCode = (Resolve-Path -LiteralPath $OvmfCodePath).Path
$resolvedOvmfVarsTemplate = (Resolve-Path -LiteralPath $OvmfVarsTemplatePath).Path
$resolvedTemplateEsp = (Resolve-Path -LiteralPath $TemplateEsp).Path
if ($TimeoutSeconds -lt 10 -or $TimeoutSeconds -gt 300) {
    throw 'TimeoutSeconds must be between 10 and 300.'
}

New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$mutex = [System.Threading.Mutex]::new($false, 'Global\guideXOS_QAC_QEMU_EXCLUSIVE')
$ownsQac = $false
$fixtureProcess = $null
$guestProcess = $null
$fixtureExit = $null
$guestPassed = $false

try {
    try {
        $ownsQac = $mutex.WaitOne(0)
    } catch [System.Threading.AbandonedMutexException] {
        $ownsQac = $true
    }
    if (-not $ownsQac) {
        throw 'QAC is reserved by another task; no guest or fixture was launched.'
    }

    $activeQemu = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -match '^qemu-system-.*\.exe$'
    })
    if ($activeQemu.Count -ne 0) {
        $owners = ($activeQemu | ForEach-Object { "PID=$($_.ProcessId) $($_.CommandLine)" }) -join ' | '
        throw "An existing QEMU process owns the guest lane; no process was touched. $owners"
    }

    foreach ($port in @(5319, 5320, 5321)) {
        $probe = [System.Net.Sockets.UdpClient]::new($port)
        $probe.Dispose()
    }

    New-Item -ItemType Directory -Path $stagedEsp | Out-Null
    Copy-Item -LiteralPath $resolvedOvmfVarsTemplate -Destination $ovmfVarsPath -Force
    Copy-Item -Path (Join-Path $resolvedTemplateEsp '*') -Destination $stagedEsp -Recurse -Force
    Copy-Item -LiteralPath $resolvedKernel -Destination (Join-Path $stagedEsp 'kernel.elf') -Force
    foreach ($required in @('BOOTX64.EFI', 'kernel.elf', 'ramdisk.img')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stagedEsp $required) -PathType Leaf)) {
            throw "Staged ESP is missing $required."
        }
    }

    $python = (Get-Command python.exe -ErrorAction Stop).Source
    $fixtureProcess = Start-Process -FilePath $python `
        -ArgumentList @('-B', 'tests/roughtime_v1_multisource_guest_fixture.py', '--timeout', "$TimeoutSeconds") `
        -WorkingDirectory $repo -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $fixtureLog -RedirectStandardError $fixtureError
    Start-Sleep -Milliseconds 700
    if ($fixtureProcess.HasExited) {
        throw "Roughtime fixture exited before QEMU launch (exit $($fixtureProcess.ExitCode))."
    }

    # Recheck after fixture startup and immediately before QEMU admission.
    $activeQemu = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -match '^qemu-system-.*\.exe$'
    })
    if ($activeQemu.Count -ne 0) {
        throw 'A QEMU process appeared during staging; this runner will not interfere with it.'
    }

    $qemuArguments = @(
        '-machine', 'pc,accel=whpx',
        '-smp', '2',
        '-m', '1024M',
        '-vga', 'std',
        '-display', 'none',
        '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=`"$resolvedOvmfCode`"",
        '-drive', "if=pflash,format=raw,unit=1,file=`"$ovmfVarsPath`"",
        '-drive', "file=fat:rw:`"$stagedEsp`",format=raw,if=ide,index=0",
        '-serial', "file:`"$serialPath`"",
        '-no-reboot',
        '-no-shutdown',
        '-rtc', 'base=utc,clock=host',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-object', 'rng-builtin,id=rng0',
        '-device', 'virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000',
        '-object', "filter-dump,id=navhttps15_dump,netdev=net0,file=`"$pcapPath`"",
        '-boot', 'order=c',
        '-name', 'guideXOS-NAVHTTPS15-QAC'
    )
    $guestProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArguments `
        -WorkingDirectory $repo -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $qemuStdout -RedirectStandardError $qemuStderr

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($guestProcess.HasExited) {
            throw "QEMU exited before qualification completed (exit $($guestProcess.ExitCode))."
        }
        if (Test-Path -LiteralPath $serialPath) {
            $serialText = Get-Content -LiteralPath $serialPath -Raw
            if ($null -eq $serialText) { $serialText = [string]::Empty }
            $guestPassed = $serialText.Contains('[NAVHTTPS13-GUEST-A] hook_result=PASS') -and
                $serialText.Contains('[NAVHTTPS13R-GUEST] unsigned_timestamp_formatter=PASS') -and
                $serialText.Contains('[NAVHTTPS13-GUEST-B] result=success') -and
                $serialText.Contains('[NAVHTTPS15-GUEST] result=PASS')
            if ($guestPassed) { break }
            if ($serialText.Contains('[NAVHTTPS13-GUEST-A] hook_result=FAIL') -or
                $serialText.Contains('[NAVHTTPS13R-GUEST] unsigned_timestamp_formatter=FAIL') -or
                $serialText.Contains('[NAVHTTPS15-GUEST] result=FAIL')) {
                throw "Guest emitted a qualification failure marker. Evidence: $runDir"
            }
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $guestPassed) {
        throw "Guest qualification markers were incomplete before timeout. Evidence: $runDir"
    }

    if (-not $guestProcess.HasExited) {
        Stop-Process -Id $guestProcess.Id -Force
        Wait-Process -Id $guestProcess.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    if ($fixtureProcess -and -not $fixtureProcess.HasExited) {
        Wait-Process -Id $fixtureProcess.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    if ($fixtureProcess -and $fixtureProcess.HasExited) {
        $fixtureExit = $fixtureProcess.ExitCode
    }
    if ($fixtureExit -ne 0) {
        throw "Fixture did not complete all four signed UDP exchanges (exit $fixtureExit). Evidence: $runDir"
    }

    if (-not $SkipNavhttps7TrustScenario) {
        $navhttps7Script = Join-Path $repo 'scripts/smoke-navigator-kernel.ps1'
        try {
            $navhttps7Output = & $navhttps7Script `
                -TimeoutSeconds 300 `
                -ScenarioFilter @(
                    'production_trust_profile_missing',
                    'production_trust_profile_unsupported_version',
                    'production_critical_name_constraints'
                ) *>&1
            $navhttps7Text = $navhttps7Output | Out-String
            $navhttps7Text | Set-Content -LiteralPath $navhttps7Log -Encoding UTF8
            if (-not $navhttps7Text.Contains('Kernel Navigator smoke PASS across all scenarios.')) {
                throw 'Filtered NAVHTTPS7 guest scenarios did not report a complete pass.'
            }
        } catch {
            ($_ | Out-String) | Add-Content -LiteralPath $navhttps7Log -Encoding UTF8
            throw "NAVHTTPS15 guest A/B passed, but filtered NAVHTTPS7 guest qualification failed. Evidence: $navhttps7Log"
        }
        Write-Output "NAVHTTPS7 guest trust result=PASS evidence=$navhttps7Log"
    }

    Write-Output "NAVHTTPS15 guest result=PASS evidence=$runDir"
} finally {
    if ($guestProcess -and -not $guestProcess.HasExited) {
        Stop-Process -Id $guestProcess.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $guestProcess.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    if ($fixtureProcess -and -not $fixtureProcess.HasExited) {
        Stop-Process -Id $fixtureProcess.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $fixtureProcess.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    if ($ownsQac) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
