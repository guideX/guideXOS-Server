param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 75,
    [switch]$SkipManagedBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\gc-first-allocation-closure"
}
$evidence = [System.IO.Path]::GetFullPath($EvidenceRoot)
$runRoot = Join-Path $evidence (Get-Date -Format "run-yyyyMMdd-HHmmssfff")
$artifactWorkspace = Join-Path $root "out\dotnet\gc-first-real-allocation\managed-artifact"
$artifactRoot = Join-Path $artifactWorkspace "artifact"
$runtimeWorkspace = Join-Path $root "out\dotnet\gc-first-real-allocation\managed-runtime-pack"
$pePath = Join-Path $artifactRoot "NativeAotGcFirstAllocation.exe"
$elfPath = Join-Path $artifactRoot "NativeAotGcFirstAllocation.elf"
$mapPath = Join-Path $artifactRoot "NativeAotGcFirstAllocation.map"
$embeddedRaw = Join-Path $runRoot "gc-first-allocation-artifact.raw.o"
$embeddedObj = Join-Path $runRoot "gc-first-allocation-artifact.o"
$kernelPath = Join-Path $root "kernel\build\amd64\bin\kernel.elf"
$normalKernelSource = Join-Path $root "out\dotnet\gc-first-allocation-hang\baseline\kernel-build-current.elf"
$normalKernelHash = "D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C"
$bootloader = Join-Path $root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$objcopy = "C:\mingw64\bin\objcopy.exe"
$objdump = "C:\mingw64\bin\objdump.exe"
$readelf = "C:\mingw64\bin\readelf.exe"
$python = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$converter = Join-Path $root "tools\dotnet\pe_to_elf_v2_fixed_base.py"
$make = (Get-Command mingw32-make.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if ([string]::IsNullOrWhiteSpace($make)) {
    $make = (Get-Command make.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
}

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: $Path"
    }
}

function Hash-File([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Invoke-LoggedCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$LogPath,
        [string]$WorkingDirectory = $root
    )

    $commandText = '"' + $FilePath + '" ' + (($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    }) -join ' ')
    Add-Content -LiteralPath (Join-Path $runRoot "commands.txt") -Value $commandText
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        Push-Location -LiteralPath $WorkingDirectory
        try {
            & $FilePath @Arguments *> $LogPath
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    if ($exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $commandText"
    }
}

function Invoke-Batch([string]$Path, [string]$LogName) {
    Require-File $Path "Generated build command"
    Invoke-LoggedCommand "cmd.exe" @("/d", "/c", "`"$Path`"") (Join-Path $runRoot $LogName) $root
}

function Assert-Regex([string]$Text, [string]$Pattern, [string]$Reason) {
    if ($Text -notmatch $Pattern) { throw "Missing allocation evidence for ${Reason}: $Pattern" }
}

function Assert-RegexCount([string]$Text, [string]$Pattern, [int]$Expected, [string]$Reason) {
    $count = [regex]::Matches(($Text -replace "`r", ""), $Pattern).Count
    if ($count -ne $Expected) { throw "Expected $Expected matches for ${Reason}, saw ${count}: $Pattern" }
}

New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
if (Test-Path -LiteralPath (Join-Path $runRoot "serial.log")) {
    throw "Fresh run directory already contains a serial log."
}
Require-File $normalKernelSource "Restoration kernel"
Require-File $bootloader "Bootloader"
Require-File $qemu "QEMU"
Require-File $ovmf "OVMF"
Require-File $objcopy "objcopy"
Require-File $objdump "objdump"
Require-File $readelf "readelf"
Require-File $python "Bundled Python"
Require-File $converter "PE-to-ELF converter"
if ([string]::IsNullOrWhiteSpace($make)) { throw "mingw32-make.exe/make.exe was not found." }

$makefile = Get-Content -LiteralPath (Join-Path $root "kernel\Makefile") -Raw
foreach ($required in @(
    'GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST',
    'GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST',
    'NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ'
)) {
    if ($makefile -notmatch $required) { throw "Kernel Makefile is missing required experiment selector: $required" }
}
if (-not $makefile.Contains('CFLAGS += $(EXTRA_CFLAGS)')) {
    throw 'Kernel Makefile is missing required EXTRA_CFLAGS propagation.'
}
$sourceTest = Get-Content -LiteralPath (Join-Path $root "kernel\core\nativeaot_pal_qemu_test.cpp") -Raw
if ($sourceTest -notmatch '\[nativeaot-gc-first-allocation\] ALL_PASS') {
    throw "First-allocation source does not publish its allocation-specific completion marker."
}
if ($sourceTest -match 'wrapperCallCount=1[\s\S]{0,300}\[nativeaot-gc-startup-qemu-test\] ALL_PASS') {
    throw "First-allocation source still aliases success to the startup-only marker."
}

try {
    Set-Content -LiteralPath (Join-Path $runRoot "selectors.txt") -Value @(
        "GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1",
        "GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST=1",
        "NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj"
    ) -Encoding ASCII
    $extraCflags = "-DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST -DGXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST -I$artifactRoot"
    Set-Content -LiteralPath (Join-Path $runRoot "extra-cflags.txt") -Value $extraCflags -Encoding ASCII
    $selectorDefines = @("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST", "GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST")
    $extraDefines = @([regex]::Matches($extraCflags, '-D([A-Za-z0-9_]+)') | ForEach-Object { $_.Groups[1].Value })
    $selectorDefineText = (@($selectorDefines | Sort-Object) -join '|')
    $extraDefineText = (@($extraDefines | Sort-Object) -join '|')
    if ($selectorDefineText -ne $extraDefineText) {
        throw "Experiment selector and EXTRA_CFLAGS define groups are inconsistent."
    }

    if (-not $SkipManagedBuild) {
        Invoke-Batch (Join-Path $runtimeWorkspace "build-real-allocation-objects.bat") "managed-runtime-pack-build.log"
        Invoke-Batch (Join-Path $artifactWorkspace "build-real-gc-archive.bat") "managed-gc-archive-build.log"
        Invoke-Batch (Join-Path $artifactWorkspace "build-first-real-managed.bat") "managed-pe-build.log"
        Invoke-Batch (Join-Path $artifactWorkspace "compile-managed-host-shims.bat") "managed-host-shims-build.log"
        Invoke-Batch (Join-Path $artifactWorkspace "compile-managed-startup-probe.bat") "managed-startup-probe-build.log"
        Invoke-Batch (Join-Path $artifactWorkspace "link-first-real-allocation.bat") "managed-pe-link.log"
    }
    Require-File $pePath "First-allocation PE"
    Require-File $mapPath "First-allocation map"

    $peHash = Hash-File $pePath
    $pythonLog = Join-Path $runRoot "pe-to-elf.log"
    Invoke-LoggedCommand $python @($converter, $pePath, $elfPath, "--map", $mapPath, "--symbol", "ManagedMain") $pythonLog $root
    Require-File $elfPath "Converted first-allocation ELF"
    $elfHash = Hash-File $elfPath
    & $objdump -p $pePath *> (Join-Path $runRoot "pe-imports.txt")
    if ($LASTEXITCODE -ne 0) { throw "PE import inspection failed." }
    & $readelf -h -l -S -r -s -d $elfPath *> (Join-Path $runRoot "elf-inspection.txt")
    if ($LASTEXITCODE -ne 0) { throw "ELF inspection failed." }
    $mapText = Get-Content -LiteralPath $mapPath -Raw
    foreach ($symbol in @("ManagedMain", "RhpNewArray", "guideXosStockRhpNewArray", "RhpNewArrayRare", "RhpGcAlloc", "GcAllocInternal", "guideXosManagedAllocationFinalize", "guideXosManagedAllocationGetDiagnostics")) {
        Assert-Regex $mapText ([regex]::Escape($symbol)) "export/map symbol $symbol"
    }
    $exportNames = [ordered]@{
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PAL_ADDRESS = "GuideXosNativeAotGcStartupInstallPalHooks"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_TABLE_ADDRESS = "GuideXosNativeAotGcStartupInstallHookTable"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PLATFORM_ADDRESS = "GuideXosNativeAotGcStartupInstallPlatformHooks"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_STARTUP_MAIN_ADDRESS = "GuideXosNativeAotGcStartupMain"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetState"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_PRE_GC_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetPreGcState"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_ALLOCATION_COUNT_ADDRESS = "GuideXosNativeAotGcStartupGetAllocationCount"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_LAST_ALLOCATION_SIZE_ADDRESS = "GuideXosNativeAotGcStartupGetLastAllocationSize"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_DIAGNOSTIC_STAGE_ADDRESS = "GuideXosNativeAotGcStartupGetDiagnosticStage"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_MANAGED_MAIN_ADDRESS = "ManagedMain"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_FINALIZE_ADDRESS = "guideXosManagedAllocationFinalize"
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_DIAGNOSTICS_ADDRESS = "guideXosManagedAllocationGetDiagnostics"
    }
    $headerLines = @("#pragma once", "", "#include <stdint.h>", "")
    foreach ($define in $exportNames.Keys) {
        $pattern = '(?m)^\s+\S+\s+' + [regex]::Escape([string]$exportNames[$define]) + '\s+([0-9A-Fa-f]{16})(?:\s+f)?\s'
        $match = [regex]::Match($mapText, $pattern)
        if (-not $match.Success) { throw "Current PE map is missing generated export $($exportNames[$define])." }
        $headerLines += "#define $define ((uintptr_t)0x$($match.Groups[1].Value)u)"
    }
    $exportHeader = Join-Path $artifactRoot "guidexos_nativeaot_gc_first_allocation_exports.h"
    Set-Content -LiteralPath $exportHeader -Value $headerLines -Encoding ASCII
    foreach ($define in $exportNames.Keys) {
        if ((Get-Content -LiteralPath $exportHeader -Raw) -notmatch ('#define\s+' + [regex]::Escape([string]$define) + '\s+\(\(uintptr_t\)0x[0-9A-Fa-f]{16}u\)')) {
            throw "Generated export header validation failed for $define."
        }
    }
    Copy-Item -LiteralPath $exportHeader -Destination (Join-Path $runRoot "guidexos_nativeaot_gc_first_allocation_exports.h") -Force
    if ((Get-Content -LiteralPath (Join-Path $runRoot "pe-imports.txt") -Raw) -match 'FlsGetValue|FlsSetValue') {
        throw "First-allocation PE still exposes live Windows FLS imports."
    }
    if ($sourceTest -notmatch 'GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST') {
        throw "First-allocation kernel source is not gated by the required experiment define."
    }
    $platformSource = Get-Content -LiteralPath (Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp") -Raw
    if ($platformSource -notmatch 'sourceDerivedArrayObjectSize' -or
        $platformSource -notmatch 'unalignedObjectSize' -or
        $platformSource -notmatch '\(unalignedObjectSize \+ 7u\)') {
        throw "Source-derived allocation-size formula is missing from the platform source."
    }

    Invoke-LoggedCommand $objcopy @("-I", "binary", "-O", "pe-x86-64", "-B", "i386:x86-64", $elfPath, $embeddedRaw) (Join-Path $runRoot "embed-raw.log") $root
    $symbols = & $objdump -t $embeddedRaw
    $startSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_start)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $endSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_end)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $sizeSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_size)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($startSymbol) -or [string]::IsNullOrWhiteSpace($endSymbol) -or [string]::IsNullOrWhiteSpace($sizeSymbol)) {
        throw "Embedded artifact symbol extraction failed."
    }
    Copy-Item -LiteralPath $embeddedRaw -Destination $embeddedObj -Force
    Invoke-LoggedCommand $objcopy @(
        "--redefine-sym", "${startSymbol}=guidexos_nativeaot_gc_startup_artifact_start",
        "--redefine-sym", "${endSymbol}=guidexos_nativeaot_gc_startup_artifact_end",
        "--redefine-sym", "${sizeSymbol}=guidexos_nativeaot_gc_startup_artifact_size",
        "--set-section-alignment", ".data=4096",
        "--rename-section", ".data=.data,alloc,load,readonly,data,contents",
        $embeddedObj
    ) (Join-Path $runRoot "embed-final.log") $root
    Require-File $embeddedObj "Embedded first-allocation object"

    if (Test-Path -LiteralPath $kernelPath) { Remove-Item -LiteralPath $kernelPath -Force }
    Invoke-LoggedCommand $make @(
        "-C", "kernel", "ARCH=amd64",
        "GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1",
        "GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST=1",
        "NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj",
        "EXTRA_CFLAGS=$extraCflags"
    ) (Join-Path $runRoot "kernel-build.log") $root
    Require-File $kernelPath "Specialized kernel"
    $kernelHash = Hash-File $kernelPath
    $kernelBuildText = Get-Content -LiteralPath (Join-Path $runRoot "kernel-build.log") -Raw
    Assert-Regex $kernelBuildText '-DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST' "kernel compile definition"
    Assert-Regex $kernelBuildText '-DGXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST' "kernel compile definition"
    Assert-Regex $kernelBuildText 'gc-startup-artifact\.o' "embedded experiment artifact"
    Assert-Regex $kernelBuildText 'gc-first-allocation-artifact\.o' "current embedded artifact"

    $espRoot = Join-Path $runRoot "esp"
    $bootPath = Join-Path $espRoot "EFI\BOOT\BOOTX64.EFI"
    $serialPath = Join-Path $runRoot "serial.log"
    New-Item -ItemType Directory -Force -Path (Split-Path $bootPath) | Out-Null
    Copy-Item -LiteralPath $bootloader -Destination $bootPath -Force
    Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $espRoot "kernel.elf") -Force
    if (Test-Path -LiteralPath $serialPath) { throw "Stale serial log appeared before QEMU launch." }
    $qemuArgs = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ('if=pflash,format=raw,readonly=on,file="' + $ovmf + '"'),
        "-drive", ('file=fat:rw:"' + $espRoot + '",format=raw,if=ide,index=0'),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ('file:"' + $serialPath + '"'), "-no-reboot", "-no-shutdown",
        "-rtc", "base=utc,clock=host"
    )
    Add-Content -LiteralPath (Join-Path $runRoot "commands.txt") -Value ('"' + $qemu + '" ' + ($qemuArgs -join ' '))
    $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -WindowStyle Hidden -PassThru
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $sawCompletion = $false
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $serialPath) {
                $liveText = Get-Content -LiteralPath $serialPath -Raw
                if ($liveText -match '\[nativeaot-gc-first-allocation\] ALL_(PASS|FAIL)') {
                    $sawCompletion = $true
                    break
                }
            }
        }
        if (-not $sawCompletion) { throw "First-allocation QEMU did not publish a completion marker within $TimeoutSeconds seconds." }
    } finally {
        if (-not $qemuProcess.HasExited) { Stop-Process -Id $qemuProcess.Id -Force }
        try { $qemuProcess.WaitForExit() } catch { }
    }
    Require-File $serialPath "Fresh first-allocation serial log"
    $serial = Get-Content -LiteralPath $serialPath -Raw
    Set-Content -LiteralPath (Join-Path $runRoot "serial.sha256") -Value (Hash-File $serialPath) -Encoding ASCII
    if ($serial -match '\[nativeaot-gc-startup-qemu-test\] ALL_PASS') { throw "Startup-only QEMU success was accepted in the allocation log." }
    if ($serial -match 'DesktopStateReady|AppRegistry initialized|desktop\.apps|Welcome to guideXOS') { throw "Ordinary desktop boot marker appeared in the allocation run." }
    if ($serial -match 'GC\.Collect|RhShutdown|RhpShutdown|GC_Shutdown') { throw "Forbidden collection or runtime-shutdown marker appeared." }
    Assert-RegexCount $serial '\[nativeaot-gc-first-allocation\] Managed entry once: PASS' 1 "managed entry"
    Assert-RegexCount $serial 'rhpNewArrayEntries=00000001' 1 "RhpNewArray entry count"
    Assert-RegexCount $serial 'realGcAllocationEntries=00000001' 1 "real GC allocation count"
    Assert-Regex $serial 'wrapperCallCount=1 managedEntryCallCount=1 shutdownCalls=0' "wrapper and managed entry counters"
    Assert-Regex $serial 'returnedObject=([0-9A-Fa-f]{16})' "returned object"
    $returnedObject = ([regex]::Match($serial, 'returnedObject=([0-9A-Fa-f]{16})')).Groups[1].Value
    if ($returnedObject -eq '0000000000000000') { throw "Returned object was null." }
    Assert-Regex $serial 'length=00000018 requestedLength=00000018 size=00000030 zeroBytes=00000018 pattern=00000001 alignment=00000001 layout=00000001 range=00000001 ownership=00000001' "object geometry and ownership"
    Assert-Regex $serial 'gcCountBefore=00000000 gcCountAfter=00000000 collectionsEntered=00000000 collectionTriggeringEntries=00000000 gcInProgressBefore=00000000 gcInProgressAfter=00000000 finalizableBefore=00000000 finalizableAfter=00000000 finalizationScans=00000000 finalizersExecuted=00000000' "collection/finalizer counters"
    Assert-Regex $serial '\[nativeaot-gc-first-allocation\] One real Workstation GC allocation: PASS' "real allocation"
    Assert-Regex $serial '\[nativeaot-gc-first-allocation\] Finalizer worker parked: PASS' "parked finalizer worker"
    Assert-Regex $serial '\[nativeaot-gc-first-allocation\] ALL_PASS' "allocation completion"

    $manifest = [ordered]@{
        allocationPe = $pePath
        allocationPeSha256 = $peHash
        allocationElf = $elfPath
        allocationElfSha256 = $elfHash
        embeddedObject = $embeddedObj
        embeddedObjectSha256 = Hash-File $embeddedObj
        specializedKernel = $kernelPath
        specializedKernelSha256 = $kernelHash
        selectors = @{
            GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST = 1
            GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST = 1
            NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ = $embeddedObj
        }
        extraCflags = $extraCflags
        serial = $serialPath
        serialSha256 = Hash-File $serialPath
        processTeardown = "PASS"
        runtimeLevelShutdown = "NOT SUPPORTED"
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot "manifest.json") -Encoding ASCII
    Write-Host "Allocation-specific smoke runner: PASS" -ForegroundColor Green
    Write-Host "Managed PE SHA256: $peHash" -ForegroundColor Cyan
    Write-Host "Managed ELF SHA256: $elfHash" -ForegroundColor Cyan
    Write-Host "Specialized kernel SHA256: $kernelHash" -ForegroundColor Cyan
    Write-Host "Returned object: $returnedObject" -ForegroundColor Cyan
    Write-Host "Process teardown: PASS; runtime-level shutdown: NOT SUPPORTED" -ForegroundColor Cyan
}
finally {
    if (Test-Path -LiteralPath $normalKernelSource) {
        $cleanLog = Join-Path $runRoot "kernel-clean.log"
        if (-not [string]::IsNullOrWhiteSpace($make)) {
            $oldPreference = $ErrorActionPreference
            try {
                $ErrorActionPreference = "Continue"
                & $make -C kernel ARCH=amd64 clean *> $cleanLog
            } finally {
                $ErrorActionPreference = $oldPreference
            }
        }
        New-Item -ItemType Directory -Force -Path (Split-Path $kernelPath) | Out-Null
        Copy-Item -LiteralPath $normalKernelSource -Destination $kernelPath -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $root "ESP") | Out-Null
        Copy-Item -LiteralPath $normalKernelSource -Destination (Join-Path $root "ESP\kernel.elf") -Force
        $restoredHash = Hash-File (Join-Path $root "ESP\kernel.elf")
        Set-Content -LiteralPath (Join-Path $runRoot "restored-normal-kernel.sha256") -Value $restoredHash -Encoding ASCII
        if ($restoredHash -ne $normalKernelHash) {
            throw "Normal kernel restoration hash mismatch. Expected $normalKernelHash, got $restoredHash"
        }
        Write-Host "Normal kernel restored: PASS ($restoredHash)" -ForegroundColor Green
    }
}
