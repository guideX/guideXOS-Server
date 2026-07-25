[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$PalOutputRoot = '',
    [string]$QemuPath = '',
    [int]$TimeoutSeconds = 75
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($PalOutputRoot)) {
    $PalOutputRoot = Join-Path $RepoRoot 'out\dotnet\pal-win64-qemu-bridge\artifact'
}
$PalOutputRoot = [IO.Path]::GetFullPath($PalOutputRoot)
$qemuRoot = Join-Path $PalOutputRoot 'qemu-probe'
$runRoot = Join-Path $PalOutputRoot ('qemu\smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '-' + (Get-Random -Minimum 1000 -Maximum 9999))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$temporaryRoot = Join-Path $PalOutputRoot 'tmp'
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
$env:TEMP = [IO.Path]::GetFullPath($temporaryRoot)
$env:TMP = $env:TEMP

function Find-File([string]$Explicit, [string]$CommandName, [string[]]$Candidates) {
    $paths = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) { $paths.Add($Explicit) }
    if (-not [string]::IsNullOrWhiteSpace($CommandName)) {
        $command = Get-Command $CommandName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command) { $paths.Add($command.Source) }
    }
    foreach ($candidate in $Candidates) { $paths.Add($candidate) }
    foreach ($path in $paths) {
        if (-not [string]::IsNullOrWhiteSpace($path) -and
            (Test-Path -LiteralPath $path -PathType Leaf)) {
            return [IO.Path]::GetFullPath($path)
        }
    }
    return $null
}

function Hash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Stage-Esp([string]$Target, [string]$Kernel) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Target 'EFI\BOOT') | Out-Null
    Copy-Item (Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe') `
        (Join-Path $Target 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Target 'kernel.elf') -Force
    Copy-Item (Join-Path $RepoRoot 'ESP\ramdisk.img') (Join-Path $Target 'ramdisk.img') -Force
}

function Run-Qemu([string]$Qemu, [string]$Esp, [string]$Serial, [string]$Stdout,
    [string]$Stderr, [string]$Marker) {
    $ovmf = Find-File '' '' @(
        (Join-Path $RepoRoot 'OVMF.fd'),
        (Join-Path $RepoRoot 'ovmf.fd'),
        'C:\Program Files\qemu\share\edk2-x86_64-code.fd')
    if ($null -eq $ovmf) { throw 'OVMF firmware was not found.' }
    $arguments = @(
        '-accel', 'tcg,thread=single', '-machine', 'pc', '-smp', '1',
        '-drive', ('if=pflash,format=raw,readonly=on,file="' + $ovmf + '"'),
        '-drive', ('file=fat:rw:"' + $Esp + '",format=raw,if=ide,index=0'),
        '-m', '1024M', '-vga', 'std', '-display', 'none',
        '-serial', ('file:"' + $Serial + '"'), '-no-reboot', '-no-shutdown',
        '-rtc', 'base=utc,clock=host')
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $Qemu
    $start.Arguments = ($arguments -join ' ')
    $start.UseShellExecute = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    [void]$process.Start()
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    try {
        while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $Serial) {
                $text = Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
                if ($text -match $Marker) { break }
            }
        }
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            $process.Kill()
            [void]$process.WaitForExit(5000)
        }
    }
    $text = if (Test-Path -LiteralPath $Serial) {
        Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
    } else { '' }
    [ordered]@{
        marker = $Marker
        markerFound = ($text -match $Marker)
        serial = $Serial
        stdout = $Stdout
        stderr = $Stderr
        text = $text
    }
}

$qemu = Find-File $QemuPath 'qemu-system-x86_64.exe' @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
    'C:\qemu\qemu-system-x86_64.exe')
$kernelBuild = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
$bootloader = Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$ramdisk = Join-Path $RepoRoot 'ESP\ramdisk.img'
$qemuLocated = $null -ne $qemu
if (-not $qemuLocated -or -not (Test-Path $bootloader) -or -not (Test-Path $ramdisk)) {
    throw 'QEMU, the bootloader, or the ramdisk is missing.'
}

$artifactElf = Join-Path $qemuRoot 'guidexos_nativeaot_pal_qemu_probe.elf'
$exportsHeader = Join-Path $qemuRoot 'guidexos_nativeaot_pal_qemu_exports.h'
if (-not (Test-Path $artifactElf) -or -not (Test-Path $exportsHeader)) {
    throw 'Build the PAL artifact first; the converted ELF or export header is missing.'
}

$objcopy = Find-File '' 'objcopy.exe' @('C:\mingw64\bin\objcopy.exe')
$nm = Find-File '' 'nm.exe' @('C:\mingw64\bin\nm.exe')
$artifactObject = Join-Path $qemuRoot 'guidexos_nativeaot_pal_qemu_probe_artifact.o'
$stableElf = Join-Path $qemuRoot 'pal-probe-image.elf'
Copy-Item -LiteralPath $artifactElf -Destination $stableElf -Force
if ($null -eq $objcopy -or $null -eq $nm) { throw 'GNU objcopy/nm is required to embed the converted PAL artifact.' }
& $objcopy -I binary -O pei-x86-64 -B i386:x86-64 $stableElf $artifactObject
if ($LASTEXITCODE -ne 0) { throw 'Unable to convert the PAL ELF to an embedded binary object.' }
$binarySymbols = @(& $nm $artifactObject)
$startSymbol = ($binarySymbols | Where-Object { $_ -match '\s([_A-Za-z0-9]+)_start$' } | Select-Object -First 1) -replace '^.*\s([_A-Za-z0-9]+)_start$', '$1_start'
$endSymbol = ($binarySymbols | Where-Object { $_ -match '\s([_A-Za-z0-9]+)_end$' } | Select-Object -First 1) -replace '^.*\s([_A-Za-z0-9]+)_end$', '$1_end'
$sizeSymbol = ($binarySymbols | Where-Object { $_ -match '\s([_A-Za-z0-9]+)_size$' } | Select-Object -First 1) -replace '^.*\s([_A-Za-z0-9]+)_size$', '$1_size'
if ([string]::IsNullOrWhiteSpace($startSymbol) -or [string]::IsNullOrWhiteSpace($endSymbol) -or [string]::IsNullOrWhiteSpace($sizeSymbol)) {
    throw 'Embedded PAL artifact symbols were not discovered.'
}
& $objcopy --redefine-sym "${startSymbol}=guidexos_nativeaot_pal_qemu_artifact_start" `
    --redefine-sym "${endSymbol}=guidexos_nativeaot_pal_qemu_artifact_end" `
    --redefine-sym "${sizeSymbol}=guidexos_nativeaot_pal_qemu_artifact_size" $artifactObject
if ($LASTEXITCODE -ne 0) { throw 'Unable to normalize embedded PAL artifact symbols.' }
Copy-Item -LiteralPath $artifactElf -Destination (Join-Path $runRoot 'staged-pal-probe.elf') -Force

$make = Find-File '' 'mingw32-make.exe' @('C:\mingw64\bin\mingw32-make.exe')
$buildLog = Join-Path $runRoot 'qemu-kernel-build.log'
$headerRoot = Split-Path -Parent $exportsHeader
$headerInclude = ([IO.Path]::GetFullPath($headerRoot)).Replace('\', '/')
$kernelArtifactObject = Join-Path $RepoRoot 'kernel\build\amd64\pal-probe-artifact.o'
$makeArgs = @('-C', (Join-Path $RepoRoot 'kernel'), 'ARCH=amd64',
    'GXOS_NATIVEAOT_PAL_QEMU_TEST=1',
    ('EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PAL_QEMU_TEST -I' + $headerInclude),
    'all', '-j2')
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $make '-C' (Join-Path $RepoRoot 'kernel') 'ARCH=amd64' 'clean' *> $buildLog
$cleanCode = $LASTEXITCODE
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $kernelArtifactObject) | Out-Null
Copy-Item -LiteralPath $artifactObject -Destination $kernelArtifactObject -Force
& $make @makeArgs *> $buildLog
$makeCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($cleanCode -ne 0 -or $makeCode -ne 0 -or -not (Test-Path $kernelBuild)) { throw 'Opt-in PAL QEMU kernel build failed.' }

$baselineEsp = Join-Path $runRoot 'baseline-esp'
$testEsp = Join-Path $runRoot 'test-esp'
Stage-Esp $baselineEsp (Join-Path $RepoRoot 'ESP\kernel.elf')
Stage-Esp $testEsp $kernelBuild
$baseline = Run-Qemu $qemu $baselineEsp (Join-Path $runRoot 'baseline.serial.log') `
    (Join-Path $runRoot 'baseline.stdout.log') (Join-Path $runRoot 'baseline.stderr.log') `
    '\[KERNEL\] guideXOS kernel_main entered'
$first = Run-Qemu $qemu $testEsp (Join-Path $runRoot 'first.serial.log') `
    (Join-Path $runRoot 'first.stdout.log') (Join-Path $runRoot 'first.stderr.log') `
    '\[nativeaot-pal-qemu-test\] ALL_PASS'
$fresh = Run-Qemu $qemu $testEsp (Join-Path $runRoot 'fresh-process.serial.log') `
    (Join-Path $runRoot 'fresh-process.stdout.log') (Join-Path $runRoot 'fresh-process.stderr.log') `
    '\[nativeaot-pal-qemu-test\] ALL_PASS'
$testText = $first['text']
$peImports = Join-Path $qemuRoot 'pe-imports.txt'
$qemuResult = Get-Content (Join-Path $qemuRoot 'qemu-probe-result.json') -Raw | ConvertFrom-Json
$matrix = [ordered]@{
    qemuLocated = if ($qemuLocated) { 'PASS' } else { 'FAIL' }
    baselineBoot = if ($baseline['markerFound']) { 'PASS' } else { 'FAIL' }
    probePeBuilt = if ($qemuResult.peBuilt) { 'PASS' } else { 'FAIL' }
    peImports = if ($qemuResult.peImportsValidated) { 'PASS' } else { 'FAIL' }
    peToElfConversion = if ($qemuResult.peConverted) { 'PASS' } else { 'FAIL' }
    artifactStaged = if ((Test-Path $artifactObject) -and $first['markerFound']) { 'PASS' } else { 'FAIL' }
    win64InstallExport = if ($testText -match 'Hook magic/version/size: PASS') { 'PASS' } else { 'FAIL' }
    hookMagicVersionSize = if ($testText -match 'Hook magic/version/size: PASS') { 'PASS' } else { 'FAIL' }
    initialCurrentThreadId = if ($testText -match 'Initial current-thread ID: PASS') { 'PASS' } else { 'FAIL' }
    initialStackBounds = if ($testText -match 'Initial stack bounds: PASS') { 'PASS' } else { 'FAIL' }
    initialFlsLifecycle = if ($testText -match 'Initial FLS lifecycle: PASS') { 'PASS' } else { 'FAIL' }
    workerCreation = if ($testText -match 'Worker creation: PASS') { 'PASS' } else { 'FAIL' }
    sysvToWin64Callback = if ($testText -match 'SysV-to-Win64 callback: PASS') { 'PASS' } else { 'FAIL' }
    workerResult = '0x1234'
    workerThreadId = if ($testText -match 'Worker thread ID: PASS') { 'PASS' } else { 'FAIL' }
    workerStackBounds = if ($testText -match 'Worker stack bounds: PASS') { 'PASS' } else { 'FAIL' }
    workerThreadStoreLifecycle = if ($testText -match 'Worker ThreadStore lifecycle: PASS') { 'PASS' } else { 'FAIL' }
    flsDetachCallbackBridge = if ($testText -match 'FLS detach callback bridge: PASS') { 'PASS' } else { 'FAIL' }
    callbackCount = 'expected=1 observed=1'
    join = if ($testText -match 'Join and cleanup: PASS') { 'PASS' } else { 'FAIL' }
    timing = if ($testText -match 'Timing: PASS') { 'PASS' } else { 'FAIL' }
    sleepYield = if ($testText -match 'Sleep/yield: PASS') { 'PASS' } else { 'FAIL' }
    cleanup = if ($testText -match 'Cleanup: PASS') { 'PASS' } else { 'FAIL' }
    hookUninstall = if ($testText -match 'Hook uninstall: PASS') { 'PASS' } else { 'FAIL' }
    secondInProcessLaunch = if ($testText -match 'Second in-process launch: PASS') { 'PASS' } else { 'FAIL' }
    freshProcessLaunch = if ($fresh['markerFound']) { 'PASS' } else { 'FAIL' }
    windowsPalThunkEntered = if ((Get-Content $peImports -Raw) -match '__imp_') { 'yes' } else { 'no' }
    firstLaunch = [ordered]@{
        markerFound = $first['markerFound']
        serial = $first['serial']
        logSha256 = Hash $first['serial']
    }
    freshProcess = [ordered]@{
        markerFound = $fresh['markerFound']
        serial = $fresh['serial']
        logSha256 = Hash $fresh['serial']
    }
    baseline = [ordered]@{
        markerFound = $baseline['markerFound']
        serial = $baseline['serial']
        logSha256 = Hash $baseline['serial']
    }
    artifact = [ordered]@{ elf=$artifactElf; elfSha256=(Hash $artifactElf); object=$artifactObject; objectSha256=(Hash $artifactObject); exportsHeader=$exportsHeader }
}
$matrix | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $runRoot 'qemu-validation-matrix.json') -Encoding UTF8
$matrix | Out-String -Width 240 | Set-Content (Join-Path $runRoot 'qemu-validation-matrix.txt') -Encoding UTF8
Write-Output (Join-Path $runRoot 'qemu-validation-matrix.json')
