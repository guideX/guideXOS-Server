param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [ValidateSet("ThreadStaticPrimitive", "ThreadStaticReference", "ThreadStaticCombined")]
    [string]$ProofMode = "ThreadStaticCombined",
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\thread-static-runtime-support"
}
$evidence = [System.IO.Path]::GetFullPath($EvidenceRoot)
$runRoot = Join-Path $evidence (Get-Date -Format "run-yyyyMMdd-HHmmssfff")
$buildRoot = Join-Path $evidence "build"
$artifactRoot = Join-Path $buildRoot "artifact"
$runtimeRoot = Join-Path $buildRoot "runtime-pack"
$managedRoot = Join-Path $buildRoot "managed"
$runResults = @()
$manifest = [ordered]@{}

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is missing: $Path" }
}
function Hash-File([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant() }
function Invoke-LoggedCommand {
    param([string]$FilePath, [string[]]$Arguments, [string]$LogPath, [string]$WorkingDirectory = $root)
    $display = '"' + $FilePath + '" ' + ($Arguments -join ' ')
    Add-Content -LiteralPath (Join-Path $runRoot "commands.txt") -Value $display
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Push-Location -LiteralPath $WorkingDirectory
    try { & $FilePath @Arguments *> $LogPath; $code = $LASTEXITCODE }
    finally { Pop-Location; $ErrorActionPreference = $oldPreference }
    if ($code -ne 0) { throw "Command failed with exit code ${code}: $display" }
}
function Write-Batch([string]$Name, [string]$Text) {
    $path = Join-Path $buildRoot $Name
    Set-Content -LiteralPath $path -Value $Text -Encoding ASCII
    return $path
}
function Invoke-Batch([string]$Path, [string]$LogName) {
    Invoke-LoggedCommand "cmd.exe" @("/d", "/c", "call `"$Path`"") (Join-Path $runRoot $LogName)
}
function Extract-MapAddress([string]$MapText, [string]$Symbol) {
    $match = [regex]::Match($MapText, '(?m)^\s+\S+\s+' + [regex]::Escape($Symbol) + '\s+([0-9A-Fa-f]{16})(?:\s+f)?\s')
    if (-not $match.Success) { throw "Map is missing export $Symbol" }
    return $match.Groups[1].Value
}
function Read-Monitor([int]$Port, [string]$Path) {
    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $client.Connect("127.0.0.1", $Port)
        $stream = $client.GetStream()
        $bytes = [Text.Encoding]::ASCII.GetBytes("info registers`r`ninfo cpus`r`ninfo mtree`r`n")
        $stream.Write($bytes, 0, $bytes.Length)
        Start-Sleep -Milliseconds 400
        $buffer = New-Object byte[] 65536
        $builder = [Text.StringBuilder]::new()
        while ($stream.DataAvailable) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            [void]$builder.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $count))
        }
        Set-Content -LiteralPath $Path -Value $builder.ToString() -Encoding ASCII
        $client.Close()
    } catch { Set-Content -LiteralPath $Path -Value ("monitor capture failed: " + $_.Exception.Message) -Encoding ASCII }
}
function Get-Field([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, '\b' + [regex]::Escape($Name) + '=(?<value>[0-9A-Fa-f]+)')
    if (-not $match.Success) { return $null }
    return "0x" + $match.Groups['value'].Value.ToUpperInvariant()
}
function Assert-Field([string]$Text, [string]$Name, [string]$Expected, [string]$Label) {
    $actual = Get-Field $Text $Name
    if ($actual -ne $Expected) { throw "$Label expected $Name=$Expected, got $actual" }
}

New-Item -ItemType Directory -Force -Path $runRoot, $buildRoot, $artifactRoot, $runtimeRoot, $managedRoot | Out-Null
$repoHead = (& git -C $root rev-parse HEAD).Trim()
$startingDirty = @(& git -C $root status --short)
$startingDiffStat = (& git -C $root diff --stat) -join "`n"
$manifest.startingHead = $repoHead
$manifest.startingDirtyState = $startingDirty
$manifest.proofMode = $ProofMode

$normalKernelSource = Join-Path $root "out\dotnet\gc-first-allocation-hang\baseline\kernel-build-current.elf"
$kernelPath = Join-Path $root "kernel\build\amd64\bin\kernel.elf"
$espKernelPath = Join-Path $root "ESP\kernel.elf"
$normalHash = "D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C"
$bootloader = Join-Path $root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$objcopy = "C:\mingw64\bin\objcopy.exe"
$objdump = "C:\mingw64\bin\objdump.exe"
$readelf = "C:\mingw64\bin\readelf.exe"
$python = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$converter = Join-Path $root "tools\dotnet\pe_to_elf_v2_fixed_base.py"
$dotnet = "C:\Program Files\dotnet\dotnet.exe"
$vsBat = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$makeCommand = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $makeCommand) { $makeCommand = Get-Command make.exe -ErrorAction SilentlyContinue | Select-Object -First 1 }
$make = if ($null -eq $makeCommand) { $null } else { $makeCommand.Source }

$oldArtifact = Join-Path $root "out\dotnet\gc-first-real-allocation\managed-artifact"
$gcStartupRoot = Join-Path $root "out\dotnet\gc-initialization-dry-run\artifact"
$adaptedArchive = Join-Path $root "out\dotnet\thread-static-compile-check-2\build\artifact\Runtime.WorkstationGC.guidexos-nativeaot-single-thread-suspend-ee.lib"
$palBridge = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.bridge.obj"
$startupDiagnostic = Join-Path $gcStartupRoot "startup-diagnostic.obj"
$gcHelpersDiagnostic = Join-Path $gcStartupRoot "gc-helpers-diagnostic.obj"
$gcHelpersAlign = Join-Path $gcStartupRoot "gc-helpers-align-up.obj"
$platformContract = Join-Path $gcStartupRoot "guidexos_nativeaot_gc_startup_platform_contract.obj"
$palStartup = Join-Path $gcStartupRoot "PalRedhawkMinWin.gc-startup.obj"
$threadObj = Join-Path $oldArtifact "thread.renamed.obj"
$ehObj = Join-Path $oldArtifact "EHHelpers.renamed.obj"
$allocFastObj = Join-Path $oldArtifact "AllocFast.renamed.obj"
$sourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source\src\coreclr"
$nativeAotRoot = Join-Path $sourceRoot "nativeaot"
$palSourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source\src\native"
$platformSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"
$runtimeSupportSource = Join-Path $root "samples\managed\HostLogProof\runtime_support.c"
$hostShimSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_managed_host_shims.cpp"
$startupProbeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_startup_probe.cpp"
$platformObj = Join-Path $runtimeRoot "guidexos_nativeaot_platform.thread-static.obj"
$runtimeSupportObj = Join-Path $artifactRoot "runtime_support.obj"
$hostShimObj = Join-Path $artifactRoot "guidexos_nativeaot_managed_host_shims.obj"
$startupProbeObj = Join-Path $artifactRoot "guidexos_nativeaot_gc_startup_probe.managed.obj"
$managedObj = Join-Path $managedRoot "obj\x64\Release\net9.0\win-x64\native\HostLogProof.obj"
$managedMap = Join-Path $artifactRoot "HostLogProof.map"
$pePath = Join-Path $artifactRoot "NativeAotThreadStatic.exe"
$elfPath = Join-Path $artifactRoot "NativeAotThreadStatic.elf"
$linkMap = Join-Path $artifactRoot "NativeAotThreadStatic.map"
$embeddedRaw = Join-Path $buildRoot "thread-static.raw.o"
$embeddedObj = Join-Path $buildRoot "thread-static.o"
$exportHeader = Join-Path $artifactRoot "guidexos_nativeaot_thread_static_exports.h"
$manifestPath = Join-Path $runRoot "manifest.json"

foreach ($path in @($normalKernelSource,$bootloader,$qemu,$ovmf,$objcopy,$objdump,$readelf,$python,$converter,$dotnet,$vsBat,$adaptedArchive,$palBridge,$startupDiagnostic,$gcHelpersDiagnostic,$gcHelpersAlign,$platformContract,$palStartup,$threadObj,$ehObj,$allocFastObj)) {
    Require-File $path "Required thread-static proof input"
}
if ([string]::IsNullOrWhiteSpace($make)) { throw "make.exe/mingw32-make.exe was not found." }
if ((Hash-File $converter) -ne "55994B674326D21A8FADE6FDDBA10D6A602E5605F67709C87F9AA57C9212F678") { throw "PE-to-ELF converter hash changed." }
$qemuVersion = (& $qemu --version | Select-Object -First 1)
Set-Content -LiteralPath (Join-Path $runRoot "qemu-version.txt") -Value $qemuVersion -Encoding ASCII

$compileBatch = Write-Batch "build-thread-static-runtime.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /c /GS- /Zl /Fo:"$runtimeSupportObj" "$runtimeSupportSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF /Fo:"$hostShimObj" "$hostShimSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DGUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION /DGUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /DGUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$sourceRoot\pal\src\include" /Fo:"$platformObj" "$platformSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGUIDEXOS_NATIVEAOT_MANAGED_IMAGE /I"$root\tools\dotnet\runtime-pack\src\platform" /I"$nativeAotRoot\Runtime" /I"$sourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$sourceRoot\pal\src\include" /Fo:"$startupProbeObj" "$startupProbeSource"
if errorlevel 1 exit /b %errorlevel%
"$dotnet" publish "$root\samples\managed\HostLogProof\HostLogProof.csproj" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true -p:HostLogProofRuntimeSupportObj=$runtimeSupportObj -p:HostLogProofRuntimePackObj=$platformObj -p:HostLogProofMapPath=$managedMap -p:HostLogProofMode=$ProofMode -p:BaseOutputPath=$managedRoot\bin\ -p:BaseIntermediateOutputPath=$managedRoot\obj\ -p:IlcSdkPath=$oldArtifact\sdk\
if errorlevel 1 exit /b %errorlevel%
link.exe /nologo /MANIFEST:NO /INCREMENTAL:NO /fixed /base:0x10000000 /SUBSYSTEM:NATIVE /ENTRY:GuideXosNativeAotGcStartupMain /OUT:"$pePath" /MAP:"$linkMap" /INCLUDE:RhInitialize /EXPORT:GuideXosNativeAotGcStartupMain /EXPORT:GuideXosNativeAotGcStartupInstallPalHooks /EXPORT:GuideXosNativeAotGcStartupInstallHookTable /EXPORT:GuideXosNativeAotGcStartupInstallPlatformHooks /EXPORT:GuideXosNativeAotGcStartupGetState /EXPORT:GuideXosNativeAotGcStartupGetPreGcState /EXPORT:GuideXosNativeAotGcStartupGetAllocationCount /EXPORT:GuideXosNativeAotGcStartupGetLastAllocationSize /EXPORT:GuideXosNativeAotGcStartupGetDiagnosticStage /EXPORT:ManagedMain /EXPORT:guideXosManagedThreadStaticProofGetDiagnostics /EXPORT:guideXosManagedThreadStaticProofRecord /NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib /IGNORE:4104 "$managedObj" "$runtimeSupportObj" "$platformObj" "$oldArtifact\sdk\bootstrapper.obj" "$adaptedArchive" "$startupProbeObj" "$hostShimObj" "$startupDiagnostic" "$gcHelpersDiagnostic" "$gcHelpersAlign" "$oldArtifact\sdk\eventpipe-disabled.lib" "$oldArtifact\sdk\Runtime.VxsortEnabled.lib" "$oldArtifact\sdk\standalonegc-disabled.lib" "$oldArtifact\sdk\zlibstatic.lib" "$oldArtifact\sdk\System.Globalization.Native.Aot.lib" "$oldArtifact\sdk\System.IO.Compression.Native.Aot.lib"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@

try {
    Invoke-Batch $compileBatch "build-thread-static-runtime.log"
    Require-File $platformObj "NativeAOT platform object"
    Require-File $hostShimObj "managed import-cell shim object"
    Require-File $managedObj "NativeAOT managed object"
    Require-File $pePath "linked NativeAOT PE"
    Invoke-LoggedCommand $python @($converter,$pePath,$elfPath,"--map",$linkMap,"--symbol","ManagedMain") (Join-Path $runRoot "pe-to-elf.log")
    Require-File $elfPath "converted NativeAOT ELF"
    $mapText = Get-Content -LiteralPath $linkMap -Raw
    $symbols = [ordered]@{}
    foreach ($name in @("ManagedMain","InitializeModules","S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow","RhpGetModuleSection","RhGetThreadStaticStorage","RhRegisterInlinedThreadStaticRoot","RhpCheckedAssignRef","guideXosManagedThreadStaticProofGetDiagnostics","guideXosManagedThreadStaticProofRecord")) {
        $symbols[$name] = Extract-MapAddress $mapText $name
    }
    $headerLines = @("#pragma once", "", "#include <stdint.h>", "")
    $headerNames = [ordered]@{
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_INSTALL_PAL_ADDRESS = "GuideXosNativeAotGcStartupInstallPalHooks"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_INSTALL_TABLE_ADDRESS = "GuideXosNativeAotGcStartupInstallHookTable"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_INSTALL_PLATFORM_ADDRESS = "GuideXosNativeAotGcStartupInstallPlatformHooks"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_STARTUP_MAIN_ADDRESS = "GuideXosNativeAotGcStartupMain"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetState"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_PRE_GC_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetPreGcState"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_ALLOCATION_COUNT_ADDRESS = "GuideXosNativeAotGcStartupGetAllocationCount"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_LAST_ALLOCATION_SIZE_ADDRESS = "GuideXosNativeAotGcStartupGetLastAllocationSize"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_DIAGNOSTIC_STAGE_ADDRESS = "GuideXosNativeAotGcStartupGetDiagnosticStage"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_MANAGED_MAIN_ADDRESS = "ManagedMain"
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_DIAGNOSTICS_ADDRESS = "guideXosManagedThreadStaticProofGetDiagnostics"
    }
    foreach ($name in $headerNames.Keys) { $headerLines += "#define $name ((uintptr_t)0x$(Extract-MapAddress $mapText $headerNames[$name])u)" }
    Set-Content -LiteralPath $exportHeader -Value $headerLines -Encoding ASCII
    Invoke-LoggedCommand $objdump @("-d","-M","intel","--start-address=0x1008e250","--stop-address=0x1008e320",$pePath) (Join-Path $runRoot "inherited-fault-disassembly.txt")
    Invoke-LoggedCommand $objdump @("-t",$pePath) (Join-Path $runRoot "thread-static-pe-symbols.txt")
    Invoke-LoggedCommand $objcopy @("-I","binary","-O","pe-x86-64","-B","i386:x86-64",$elfPath,$embeddedRaw) (Join-Path $runRoot "embed-raw.log")
    $rawSymbols = & $objdump -t $embeddedRaw
    $rawStart = ($rawSymbols | Where-Object { $_ -match '(_binary_\S+_start)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $rawEnd = ($rawSymbols | Where-Object { $_ -match '(_binary_\S+_end)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $rawSize = ($rawSymbols | Where-Object { $_ -match '(_binary_\S+_size)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($rawStart) -or [string]::IsNullOrWhiteSpace($rawEnd) -or [string]::IsNullOrWhiteSpace($rawSize)) { throw "Embedded symbol extraction failed." }
    Copy-Item -LiteralPath $embeddedRaw -Destination $embeddedObj -Force
    Invoke-LoggedCommand $objcopy @("--redefine-sym","${rawStart}=guidexos_nativeaot_gc_startup_artifact_start","--redefine-sym","${rawEnd}=guidexos_nativeaot_gc_startup_artifact_end","--redefine-sym","${rawSize}=guidexos_nativeaot_gc_startup_artifact_size","--set-section-alignment",".data=4096","--rename-section",".data=.data,alloc,load,readonly,data,contents",$embeddedObj) (Join-Path $runRoot "embed-final.log")
    $extraCflags = "-DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST -DGXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST -I$artifactRoot"
    Set-Content -LiteralPath (Join-Path $runRoot "selectors.txt") -Value @("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1", "GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST=1", "NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj", "EXTRA_CFLAGS=$extraCflags") -Encoding ASCII
    Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","clean") (Join-Path $runRoot "kernel-preclean.log")
    Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST=1","NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj","EXTRA_CFLAGS=$extraCflags") (Join-Path $runRoot "kernel-build.log")
    Require-File $kernelPath "thread-static proof kernel"
    $proofKernelHash = Hash-File $kernelPath
    Set-Content -LiteralPath (Join-Path $runRoot "proof-kernel.sha256") -Value $proofKernelHash -Encoding ASCII
    Invoke-LoggedCommand $objdump @("-t",$kernelPath) (Join-Path $runRoot "kernel-symbols.txt")

    for ($runIndex = 0; $runIndex -lt 3; $runIndex++) {
        $name = if ($runIndex -eq 0) { "first-run" } else { "repeat-$runIndex" }
        $oneRoot = Join-Path $runRoot $name
        $espRoot = Join-Path $oneRoot "esp"
        $bootPath = Join-Path $espRoot "EFI\BOOT\BOOTX64.EFI"
        $serialPath = Join-Path $oneRoot "serial.log"
        $monitorPath = Join-Path $oneRoot "watchdog-monitor.txt"
        New-Item -ItemType Directory -Force -Path (Split-Path $bootPath) | Out-Null
        Copy-Item -LiteralPath $bootloader -Destination $bootPath -Force
        Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $espRoot "kernel.elf") -Force
        $port = 44900 + $runIndex
        $qemuArgs = @("-accel","tcg,thread=single","-machine","pc","-smp","1","-drive",('if=pflash,format=raw,readonly=on,file="' + $ovmf + '"'),"-drive",('file=fat:rw:"' + $espRoot + '",format=raw,if=ide,index=0'),"-m","1024M","-vga","std","-display","none","-serial",('file:"' + $serialPath + '"'),"-monitor",("tcp:127.0.0.1:$port,server,nowait"),"-no-reboot","-no-shutdown","-rtc","base=utc,clock=host")
        $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -WindowStyle Hidden -PassThru
        $completed = $false
        try {
            $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
            while ((Get-Date) -lt $deadline -and -not $qemuProcess.HasExited) {
                Start-Sleep -Milliseconds 250
                if (Test-Path -LiteralPath $serialPath) {
                    $liveText = Get-Content -LiteralPath $serialPath -Raw
                    if ($liveText -match '\[nativeaot-thread-static\] ALL_(PASS|FAIL)') { $completed = $true; break }
                }
            }
            if (-not $completed) { Read-Monitor $port $monitorPath; throw "QEMU $name did not reach the thread-static result marker." }
        } finally {
            if (-not $qemuProcess.HasExited) {
                try { $qemuProcess.Kill() } catch { Stop-Process -Id $qemuProcess.Id -Force -ErrorAction SilentlyContinue }
            }
            try { [void]$qemuProcess.WaitForExit(5000) } catch { }
        }
        Require-File $serialPath "QEMU serial log"
        $serial = Get-Content -LiteralPath $serialPath -Raw
        $normalized = (($serial -replace '\[IRQ\] dispatch irq=00\s*', '') -replace '\s+', ' ') -replace '\s*=\s*', '='
        if ($normalized -notmatch '\[nativeaot-thread-static\] ALL_PASS') { throw "Thread-static proof failed in $name." }
        $wantPrimitive = $ProofMode -in @("ThreadStaticPrimitive","ThreadStaticCombined")
        $wantReference = $ProofMode -in @("ThreadStaticReference","ThreadStaticCombined")
        Assert-Field $normalized "primitiveStart" $(if ($wantPrimitive) { "0x00000001" } else { "0x00000000" }) "primitive start count"
        Assert-Field $normalized "primitiveSuccess" $(if ($wantPrimitive) { "0x00000001" } else { "0x00000000" }) "primitive success count"
        Assert-Field $normalized "referenceStart" $(if ($wantReference) { "0x00000001" } else { "0x00000000" }) "reference start count"
        Assert-Field $normalized "referenceSuccess" $(if ($wantReference) { "0x00000001" } else { "0x00000000" }) "reference success count"
        Assert-Field $normalized "unexpectedGcRequests" "0x00000000" "unexpected collection requests"
        Assert-Field $normalized "collectionEntries" "0x00000000" "collection entries"
        Assert-Field $normalized "suspensionRequests" "0x00000000" "suspension requests"
        Assert-Field $normalized "registeredThreads" "0x00000001" "registered managed threads"
        if ($wantReference) {
            Assert-Field $normalized "identityMatch" "0x00000001" "reference identity"
            Assert-Field $normalized "objectValid" "0x00000001" "reference object validity"
            $assigned = Get-Field $normalized "referenceAssigned"
            $readback = Get-Field $normalized "referenceReadback"
            if ([string]::IsNullOrWhiteSpace($assigned) -or $assigned -eq "0x0000000000000000" -or $assigned -ne $readback) { throw "Reference address identity mismatch in $name." }
        }
        $serialHash = Hash-File $serialPath
        Set-Content -LiteralPath (Join-Path $oneRoot "serial.sha256") -Value $serialHash -Encoding ASCII
        $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=$serialHash; result="PASS"; primitiveExpected=$wantPrimitive; referenceExpected=$wantReference; fields=[ordered]@{ primitiveInitial=(Get-Field $normalized "primitiveInitial"); primitiveAssigned=(Get-Field $normalized "primitiveAssigned"); primitiveReadback=(Get-Field $normalized "primitiveReadback"); referenceAssigned=(Get-Field $normalized "referenceAssigned"); referenceReadback=(Get-Field $normalized "referenceReadback"); identityMatch=(Get-Field $normalized "identityMatch"); objectValid=(Get-Field $normalized "objectValid"); runtimeThread=(Get-Field $normalized "runtimeThread"); tlsBlock=(Get-Field $normalized "tlsBlock"); flsIdentity=(Get-Field $normalized "flsIdentity"); threadStaticStorage=(Get-Field $normalized "threadStaticStorage"); storageBase=(Get-Field $normalized "storageBase"); storageInitRequests=(Get-Field $normalized "storageInitRequests"); storageInitEntries=(Get-Field $normalized "storageInitEntries"); storageInitCompletions=(Get-Field $normalized "storageInitCompletions"); storageAllocations=(Get-Field $normalized "storageAllocations"); repeatedLookups=(Get-Field $normalized "repeatedLookups"); registeredThreads=(Get-Field $normalized "registeredThreads"); finalMarker=(Get-Field $normalized "finalMarker") } }
    }
    $manifest.outcome = "A"
    $manifest.results = $runResults
    $manifest.proofKernelSha256 = $proofKernelHash
    $manifest.qemuVersion = $qemuVersion
    $manifest.lockedNativeAot = [ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit="9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3" }
    $manifest.previousFault = [ordered]@{ rip="0x1008E2BE"; cr2="0xFFFB5FF9"; bytes="48 8B 52 10"; instruction="mov rdx,QWORD PTR [rdx+0x10]"; effectiveAddress="0x00000000FFFB5FE9 + 0x10 = 0x00000000FFFB5FF9" }
    $manifest.sourceMapping = [ordered]@{ generatedMethod="S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow"; mapAddress=$symbols["S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow"]; source="Common/src/Internal/Runtime/ThreadStatics.cs:GetInlinedThreadStaticBaseSlow"; helpers=@("RhpGetModuleSection (managed RhGetModuleSection import)","RhGetThreadStaticStorage","RhRegisterInlinedThreadStaticRoot","RhpCheckedAssignRef","RhpAssignRef"); storage="Thread::GetThreadStaticStorage / RuntimeThreadLocals::m_pThreadLocalStatics plus InlinedThreadStaticRoot" }
    $manifest.startingDiffStat = $startingDiffStat
    $manifest.proofMarkers = @("7A510001","7A510002","7A510003","7A510004")
    $manifest.artifacts = [ordered]@{ pe=$pePath; elf=$elfPath; map=$linkMap; exportHeader=$exportHeader; kernel=$kernelPath; runRoot=$runRoot }
    $manifest.documentation = "docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md"
    $manifest.reportedRegressions = "See report; this focused script records the three fresh no-GC QEMU runs."
    Write-Host "NativeAOT thread-static ${ProofMode}: PASS (3 fresh QEMU runs)" -ForegroundColor Green
}
catch {
    $manifest.outcome = "FAILURE_DURING_HARNESS"
    $manifest.error = $_.Exception.Message
    throw
}
finally {
    if (Test-Path -LiteralPath $kernelPath -PathType Leaf) {
        $manifest.proofKernelSha256 = Hash-File $kernelPath
    }
    if (Test-Path -LiteralPath $normalKernelSource -PathType Leaf) {
        if ($null -ne $make) {
            try { Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","clean") (Join-Path $runRoot "kernel-final-clean.log") } catch { $manifest.restoreCleanError = $_.Exception.Message }
        }
        New-Item -ItemType Directory -Force -Path (Split-Path $kernelPath), (Split-Path $espKernelPath) | Out-Null
        Copy-Item -LiteralPath $normalKernelSource -Destination $kernelPath -Force
        Copy-Item -LiteralPath $normalKernelSource -Destination $espKernelPath -Force
        $manifest.ordinaryKernelSha256 = Hash-File $kernelPath
        $manifest.ordinaryEspSha256 = Hash-File $espKernelPath
        $manifest.ordinaryExpectedSha256 = $normalHash
        $manifest.ordinaryRestored = ($manifest.ordinaryKernelSha256 -eq $normalHash -and $manifest.ordinaryEspSha256 -eq $normalHash)
    }
    $manifest.endingHead = (& git -C $root rev-parse HEAD).Trim()
    $manifest.endingDirtyState = @(& git -C $root status --short)
    $manifest.reportPaths = @("docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md", $manifestPath)
    $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
}
