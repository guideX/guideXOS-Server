param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 90,
    [switch]$SkipManagedBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\gc-multiple-refills-first-segment-boundary"
}
$evidence = [System.IO.Path]::GetFullPath($EvidenceRoot)
$runRoot = Join-Path $evidence (Get-Date -Format "run-yyyyMMdd-HHmmssfff")
$buildRoot = Join-Path $evidence "build"
$artifactRoot = Join-Path $buildRoot "artifact"
$runtimeRoot = Join-Path $buildRoot "runtime-pack"
$oldRoot = Join-Path $root "out\dotnet\gc-first-real-allocation"
$oldArtifact = Join-Path $oldRoot "managed-artifact"
$pePath = Join-Path $artifactRoot "NativeAotGcMultipleRefillsBoundary.exe"
$elfPath = Join-Path $artifactRoot "NativeAotGcMultipleRefillsBoundary.elf"
$mapPath = Join-Path $artifactRoot "NativeAotGcMultipleRefillsBoundary.map"
$kernelPath = Join-Path $root "kernel\build\amd64\bin\kernel.elf"
$normalKernelSource = Join-Path $root "out\dotnet\gc-first-allocation-hang\baseline\kernel-build-current.elf"
$normalKernelHash = "D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C"
$activeArchive = Join-Path $root "out\dotnet\pal-runtime-active-replacement\archives\Runtime.WorkstationGC.guidexos-nativeaot-pal.lib"
$activeArchiveHash = "C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F"
$bootloader = Join-Path $root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$objcopy = "C:\mingw64\bin\objcopy.exe"
$objdump = "C:\mingw64\bin\objdump.exe"
$readelf = "C:\mingw64\bin\readelf.exe"
$python = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$converter = Join-Path $root "tools\dotnet\pe_to_elf_v2_fixed_base.py"
$vsBat = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$dotnet = "C:\Program Files\dotnet\dotnet.exe"
$make = (Get-Command mingw32-make.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if ([string]::IsNullOrWhiteSpace($make)) { $make = (Get-Command make.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source }

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is missing: $Path" }
}

function Hash-File([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Log-Command([string]$Text) {
    Add-Content -LiteralPath (Join-Path $runRoot "commands.txt") -Value $Text
}

function Invoke-LoggedCommand {
    param([string]$FilePath, [string[]]$Arguments, [string]$LogPath, [string]$WorkingDirectory = $root)
    $commandText = '"' + $FilePath + '" ' + (($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    }) -join ' ')
    Log-Command $commandText
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        Push-Location -LiteralPath $WorkingDirectory
        try { & $FilePath @Arguments *> $LogPath; $exitCode = $LASTEXITCODE } finally { Pop-Location }
    } finally { $ErrorActionPreference = $oldPreference }
    if ($exitCode -ne 0) { throw "Command failed with exit code ${exitCode}: $commandText" }
}

function Write-Batch([string]$Name, [string]$Text) {
    $path = Join-Path $buildRoot $Name
    Set-Content -LiteralPath $path -Value $Text -Encoding ASCII
    return $path
}

function Invoke-Batch([string]$Path, [string]$LogName) {
    Invoke-LoggedCommand "cmd.exe" @("/d", "/c", "call `"$Path`"") (Join-Path $runRoot $LogName)
}

function Assert-Text([string]$Text, [string]$Pattern, [string]$Label) {
    if ($Text -notmatch $Pattern) { throw "Missing segment-boundary evidence for ${Label}: ${Pattern}" }
}

function Extract-MapAddress([string]$MapText, [string]$Symbol) {
    $pattern = '(?m)^\s+\S+\s+' + [regex]::Escape($Symbol) + '\s+([0-9A-Fa-f]{16})(?:\s+f)?\s'
    $match = [regex]::Match($MapText, $pattern)
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
        Start-Sleep -Milliseconds 500
        $buffer = New-Object byte[] 65536
        $builder = [Text.StringBuilder]::new()
        while ($stream.DataAvailable) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            [void]$builder.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $count))
        }
        Set-Content -LiteralPath $Path -Value $builder.ToString() -Encoding ASCII
        $client.Close()
    } catch {
        Set-Content -LiteralPath $Path -Value ("monitor capture failed: " + $_.Exception.Message) -Encoding ASCII
    }
}

New-Item -ItemType Directory -Force -Path $runRoot, $buildRoot, $artifactRoot, $runtimeRoot | Out-Null
foreach ($path in @($normalKernelSource,$bootloader,$qemu,$ovmf,$objcopy,$objdump,$readelf,$python,$converter,$vsBat,$dotnet,$activeArchive)) {
    Require-File $path "Required experiment input"
}
if ([string]::IsNullOrWhiteSpace($make)) { throw "mingw32-make.exe/make.exe was not found." }
if ((Hash-File $activeArchive) -ne $activeArchiveHash) { throw "Active PAL archive hash changed." }

$sourceRoot = Join-Path $root "out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr"
$nativeAotRoot = Join-Path $sourceRoot "nativeaot"
$palSourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source\src\native"
$platformSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"
$probeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_allocation_probe.cpp"
$hostShimSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_managed_host_shims.cpp"
$startupProbeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_startup_probe.cpp"
$runtimeSupportSource = Join-Path $root "samples\managed\HostLogProof\runtime_support.c"
$replacementRoot = Join-Path $root "out\dotnet\gc-first-real-allocation\identity\build1\rebuilt"
$identityManifestPath = Join-Path $root "out\dotnet\gc-first-real-allocation\identity\build1\stock\identity.json"
$gcStartupRoot = Join-Path $root "out\dotnet\gc-initialization-dry-run\artifact"
$palBridge = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.bridge.obj"
$gcEnv = Join-Path $replacementRoot "guidexos_gcenv.obj"
$gcEnvBoundary = Join-Path $runtimeRoot "guidexos_gcenv.segment-boundary.obj"
$vmAdapter = Join-Path $replacementRoot "guidexos_nativeaot_virtual_memory_adapter.obj"
$vmRegion = Join-Path $replacementRoot "guidexos_virtual_memory_region.obj"
$gcPlatformServices = Join-Path $replacementRoot "guidexos_gc_platform_services.obj"
$gcEvent = Join-Path $replacementRoot "guidexos_event.obj"
$gcMutex = Join-Path $replacementRoot "guidexos_mutex.obj"
$gcCriticalSection = Join-Path $replacementRoot "guidexos_nativeaot_critical_section_adapter.obj"
$gcEventAdapter = Join-Path $replacementRoot "guidexos_nativeaot_event_adapter.obj"
$gcFlsAdapter = Join-Path $replacementRoot "guidexos_nativeaot_fls_adapter.obj"
$gcThreadAdapter = Join-Path $replacementRoot "guidexos_nativeaot_thread_adapter.obj"
$gcNativeThread = Join-Path $replacementRoot "guidexos_native_thread.obj"
$gcLocalStorage = Join-Path $replacementRoot "guidexos_local_storage.obj"
$gcBridge = Join-Path $gcStartupRoot "guidexos_nativeaot_gcenv_startup_bridge.obj"
$gcBridgeSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcenv_startup_bridge.cpp"
$platformContract = Join-Path $gcStartupRoot "guidexos_nativeaot_gc_startup_platform_contract.obj"
$palStartup = Join-Path $gcStartupRoot "PalRedhawkMinWin.gc-startup.obj"
$startupDiagnostic = Join-Path $gcStartupRoot "startup-diagnostic.obj"
$gcHelpersDiagnostic = Join-Path $gcStartupRoot "gc-helpers-diagnostic.obj"
$gcHelpersAlign = Join-Path $gcStartupRoot "gc-helpers-align-up.obj"
$threadObj = Join-Path $oldArtifact "thread.renamed.obj"
$ehObj = Join-Path $oldArtifact "EHHelpers.renamed.obj"
$allocFastObj = Join-Path $oldArtifact "AllocFast.renamed.obj"
$platformObj = Join-Path $runtimeRoot "guidexos_nativeaot_platform.segment-boundary.obj"
$probeObj = Join-Path $runtimeRoot "guidexos_nativeaot_gc_allocation_probe.segment-boundary.obj"
$gcBridgeBoundary = Join-Path $runtimeRoot "guidexos_nativeaot_gcenv_startup_bridge.segment-boundary.obj"
$hostShimObj = Join-Path $artifactRoot "guidexos_nativeaot_managed_host_shims.obj"
$startupProbeObj = Join-Path $artifactRoot "guidexos_nativeaot_gc_startup_probe.managed.obj"
$runtimeSupportObj = Join-Path $artifactRoot "runtime_support.obj"
$adaptedArchive = Join-Path $artifactRoot "Runtime.WorkstationGC.guidexos-nativeaot-gc-segment-boundary.lib"
$managedPublishRoot = Join-Path $buildRoot "managed"

foreach ($path in @($palBridge,$gcEnv,$gcBridge,$platformContract,$palStartup,$startupDiagnostic,$gcHelpersDiagnostic,$gcHelpersAlign,$threadObj,$ehObj,$allocFastObj)) {
    Require-File $path "Authorized replacement input"
}
Require-File $identityManifestPath "Authorized normalized adapted-GC identity manifest"

try {
    Set-Content -LiteralPath (Join-Path $runRoot "baseline.txt") -Value @(
        "experiment=bounded byte[4096] allocations through multiple context refills to first GC heap commit or segment transition",
        "arrayLength=4096",
        "hardAllocationLimit=384",
        "nativeAotSourceCommit=9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3",
        "runtimePack=9.0.0 AMD64 Workstation GC interface=5.3 EE=2",
        "activePalArchiveSha256=$(Hash-File $activeArchive)",
        "normalKernelSha256=$normalKernelHash",
        "segmentIdentity=source heap_segment pointer from gc_heap::find_segment",
        "commitSource=GCToOSInterface::VirtualCommit -> gcVirtualCommit -> adapter trace",
        "lifetime=process teardown only; same-process reinitialization unsupported"
    ) -Encoding ASCII

    $runtimeBat = Write-Batch "build-segment-boundary-runtime-pack.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION /DGUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ARRAY_LENGTH=4096 /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_HARD_LIMIT=384 /Fo:"$platformObj" "$platformSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGXOS_BARE_METAL /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$nativeAotRoot\Runtime" /I"$sourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$sourceRoot\pal\src\include" /Fo:"$gcBridgeBoundary" "$gcBridgeSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime" /I"$sourceRoot\native" /Fo:"$gcEnvBoundary" "$(Join-Path $root 'tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp')"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\eventpipe" /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$sourceRoot\pal\src\include" /FI"$sourceRoot\gc\env\common.h" /Fo:"$probeObj" "$probeSource"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    $artifactBat = Write-Batch "build-segment-boundary-artifact.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /c /GS- /Zl /Fo:"$runtimeSupportObj" "$runtimeSupportSource"
if errorlevel 1 exit /b %errorlevel%
"$dotnet" publish "$(Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj')" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true -p:HostLogProofRuntimeSupportObj=$runtimeSupportObj -p:HostLogProofMapPath=$mapPath -p:HostLogProofMode=SegmentBoundary -p:BaseOutputPath=$managedPublishRoot\bin\ -p:BaseIntermediateOutputPath=$managedPublishRoot\obj\ -p:HostLogProofRuntimePackObj=$platformObj -p:IlcSdkPath=$oldArtifact\sdk\
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /Fo:"$hostShimObj" "$hostShimSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGUIDEXOS_NATIVEAOT_MANAGED_IMAGE /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$nativeAotRoot\Runtime" /Fo:"$startupProbeObj" "$startupProbeSource"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    $archiveBat = Write-Batch "build-segment-boundary-gc-archive.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
lib.exe /nologo /OUT:"$adaptedArchive" "$activeArchive" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\PalRedhawkMinWin.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\thread.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.obj')" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\windows\gcenv.windows.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\amd64\AllocFast.asm.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj" "$palBridge" "$palStartup" "$gcEnvBoundary" "$gcBridgeBoundary" "$platformContract" "$threadObj" "$ehObj" "$allocFastObj" "$probeObj"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    $linkBat = Write-Batch "link-segment-boundary.bat" @"
@echo off
call "$vsBat" >nul
link.exe /nologo /MANIFEST:NO /INCREMENTAL:NO /fixed /base:0x10000000 /SUBSYSTEM:NATIVE /ENTRY:GuideXosNativeAotGcStartupMain /OUT:"$pePath" /MAP:"$mapPath" /INCLUDE:RhInitialize /EXPORT:GuideXosNativeAotGcStartupMain /EXPORT:GuideXosNativeAotGcStartupInstallPalHooks /EXPORT:GuideXosNativeAotGcStartupInstallHookTable /EXPORT:GuideXosNativeAotGcStartupInstallPlatformHooks /EXPORT:GuideXosNativeAotGcStartupGetState /EXPORT:GuideXosNativeAotGcStartupGetPreGcState /EXPORT:GuideXosNativeAotGcStartupGetAllocationCount /EXPORT:GuideXosNativeAotGcStartupGetLastAllocationSize /EXPORT:GuideXosNativeAotGcStartupGetDiagnosticStage /EXPORT:ManagedMain /EXPORT:guideXosManagedAllocationBeginSegmentBoundaryExperiment /EXPORT:guideXosManagedAllocationFinalize /EXPORT:guideXosManagedAllocationGetDiagnostics /EXPORT:guideXosManagedAllocationValidateObject /EXPORT:guideXosManagedAllocationGetLoopStatus /EXPORT:guideXosManagedAllocationGetHardLimit /NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib /IGNORE:4104 "$managedPublishRoot\obj\x64\Release\net9.0\win-x64\native\HostLogProof.obj" "$runtimeSupportObj" "$platformObj" "$oldArtifact\sdk\bootstrapper.obj" "$adaptedArchive" "$startupProbeObj" "$hostShimObj" "$startupDiagnostic" "$gcHelpersDiagnostic" "$gcHelpersAlign" "$oldArtifact\sdk\eventpipe-disabled.lib" "$oldArtifact\sdk\Runtime.VxsortEnabled.lib" "$oldArtifact\sdk\standalonegc-disabled.lib" "$oldArtifact\sdk\zlibstatic.lib" "$oldArtifact\sdk\System.Globalization.Native.Aot.lib" "$oldArtifact\sdk\System.IO.Compression.Native.Aot.lib"
exit /b %errorlevel%
"@

    if (-not $SkipManagedBuild) {
        # Remove only this experiment's generated objects before rebuilding;
        # the ordinary runtime-pack and prior milestone artifacts are inputs.
        foreach ($stale in @($platformObj, $gcEnvBoundary, $probeObj,
                             $gcBridgeBoundary, $runtimeSupportObj,
                             $hostShimObj, $startupProbeObj, $adaptedArchive,
                             $pePath, $elfPath, $mapPath)) {
            if (Test-Path -LiteralPath $stale -PathType Leaf) {
                Remove-Item -LiteralPath $stale -Force
            }
        }
        Invoke-Batch $runtimeBat "runtime-pack-build.log"
        Invoke-Batch $artifactBat "managed-artifact-build.log"
        Invoke-Batch $archiveBat "gc-archive-build.log"
        Invoke-Batch $linkBat "managed-link.log"
    }
    foreach ($path in @($platformObj,$gcEnvBoundary,$probeObj,$runtimeSupportObj,$hostShimObj,$startupProbeObj,$adaptedArchive,$pePath,$mapPath)) { Require-File $path "Segment-boundary build output" }
    Invoke-LoggedCommand $python @($converter,$pePath,$elfPath,"--map",$mapPath,"--symbol","ManagedMain") (Join-Path $runRoot "pe-to-elf.log")
    Require-File $elfPath "Segment-boundary ELF"
    Invoke-LoggedCommand $objdump @("-p",$pePath) (Join-Path $runRoot "pe-imports.txt")
    Invoke-LoggedCommand $readelf @("-h","-l","-S","-r","-s","-d",$elfPath) (Join-Path $runRoot "elf-inspection.txt")
    $imports = Get-Content -LiteralPath (Join-Path $runRoot "pe-imports.txt") -Raw
    if ($imports -match 'FlsGetValue|FlsSetValue') { throw "Segment-boundary PE still exposes live Windows FLS imports." }
    $mapText = Get-Content -LiteralPath $mapPath -Raw
    foreach ($symbol in @("ManagedMain","RhpNewArray","guideXosStockRhpNewArray","RhpNewArrayRare","RhpGcAlloc","guideXosManagedAllocationBeginSegmentBoundaryExperiment","guideXosManagedAllocationFinalize","guideXosManagedAllocationGetDiagnostics","guideXosManagedAllocationValidateObject","guideXosManagedAllocationGetLoopStatus","guideXosManagedAllocationGetHardLimit")) { [void](Extract-MapAddress $mapText $symbol) }
    Assert-Text $mapText 'GcAllocInternal' "decorated GcAllocInternal map symbol"
    $exports = [ordered]@{
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_INSTALL_PAL_ADDRESS = "GuideXosNativeAotGcStartupInstallPalHooks"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_INSTALL_TABLE_ADDRESS = "GuideXosNativeAotGcStartupInstallHookTable"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_INSTALL_PLATFORM_ADDRESS = "GuideXosNativeAotGcStartupInstallPlatformHooks"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_STARTUP_MAIN_ADDRESS = "GuideXosNativeAotGcStartupMain"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetState"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_PRE_GC_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetPreGcState"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_ALLOCATION_COUNT_ADDRESS = "GuideXosNativeAotGcStartupGetAllocationCount"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_LAST_ALLOCATION_SIZE_ADDRESS = "GuideXosNativeAotGcStartupGetLastAllocationSize"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_DIAGNOSTIC_STAGE_ADDRESS = "GuideXosNativeAotGcStartupGetDiagnosticStage"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_MANAGED_MAIN_ADDRESS = "ManagedMain"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_BEGIN_EXPERIMENT_ADDRESS = "guideXosManagedAllocationBeginSegmentBoundaryExperiment"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_FINALIZE_ADDRESS = "guideXosManagedAllocationFinalize"
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_DIAGNOSTICS_ADDRESS = "guideXosManagedAllocationGetDiagnostics"
    }
    $headerLines = @("#pragma once", "", "#include <stdint.h>", "")
    foreach ($define in $exports.Keys) { $headerLines += "#define $define ((uintptr_t)0x$(Extract-MapAddress $mapText $exports[$define])u)" }
    $exportHeader = Join-Path $artifactRoot "guidexos_nativeaot_gc_segment_boundary_exports.h"
    Set-Content -LiteralPath $exportHeader -Value $headerLines -Encoding ASCII
    Copy-Item -LiteralPath $exportHeader -Destination (Join-Path $runRoot "guidexos_nativeaot_gc_segment_boundary_exports.h") -Force
    $rawObj = Join-Path $buildRoot "gc-segment-boundary-artifact.raw.o"
    $embeddedObj = Join-Path $buildRoot "gc-segment-boundary-artifact.o"
    Invoke-LoggedCommand $objcopy @("-I","binary","-O","pe-x86-64","-B","i386:x86-64",$elfPath,$rawObj) (Join-Path $runRoot "embed-raw.log")
    $symbols = & $objdump -t $rawObj
    $startSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_start)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $endSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_end)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $sizeSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_size)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($startSymbol) -or [string]::IsNullOrWhiteSpace($endSymbol) -or [string]::IsNullOrWhiteSpace($sizeSymbol)) { throw "Embedded symbol extraction failed." }
    Copy-Item -LiteralPath $rawObj -Destination $embeddedObj -Force
    Invoke-LoggedCommand $objcopy @("--redefine-sym","${startSymbol}=guidexos_nativeaot_gc_startup_artifact_start","--redefine-sym","${endSymbol}=guidexos_nativeaot_gc_startup_artifact_end","--redefine-sym","${sizeSymbol}=guidexos_nativeaot_gc_startup_artifact_size","--set-section-alignment",".data=4096","--rename-section",".data=.data,alloc,load,readonly,data,contents",$embeddedObj) (Join-Path $runRoot "embed-final.log")

    $extraCflags = "-DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST -DGXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST -I$artifactRoot"
    Set-Content -LiteralPath (Join-Path $runRoot "selectors.txt") -Value @("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST=1","NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj") -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $runRoot "extra-cflags.txt") -Value $extraCflags -Encoding ASCII
    if (Test-Path -LiteralPath $kernelPath) { Remove-Item -LiteralPath $kernelPath -Force }
    Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST=1","NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj","EXTRA_CFLAGS=$extraCflags") (Join-Path $runRoot "kernel-build.log")
    Require-File $kernelPath "Specialized segment-boundary kernel"
    $specializedKernelHash = Hash-File $kernelPath
    Copy-Item -LiteralPath (Join-Path $runRoot "kernel-build.log") -Destination (Join-Path $runRoot "kernel-build-current.log") -Force
    Set-Content -LiteralPath (Join-Path $runRoot "kernel-symbols.txt") -Value (& $objdump -t $kernelPath) -Encoding ASCII

    $runResults = @()
    for ($runIndex = 0; $runIndex -lt 3; $runIndex++) {
        $name = if ($runIndex -eq 0) { "first-run" } else { "repeat-$runIndex" }
        $oneRoot = Join-Path $runRoot $name
        $espRoot = Join-Path $oneRoot "esp"
        $bootPath = Join-Path $espRoot "EFI\BOOT\BOOTX64.EFI"
        $serialPath = Join-Path $oneRoot "serial.log"
        New-Item -ItemType Directory -Force -Path (Split-Path $bootPath) | Out-Null
        Copy-Item -LiteralPath $bootloader -Destination $bootPath -Force
        Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $espRoot "kernel.elf") -Force
        $port = 44600 + $runIndex
        $monitorPath = Join-Path $oneRoot "watchdog-monitor.txt"
        $qemuArgs = @("-accel","tcg,thread=single","-machine","pc","-smp","1","-drive",('if=pflash,format=raw,readonly=on,file="' + $ovmf + '"'),"-drive",('file=fat:rw:"' + $espRoot + '",format=raw,if=ide,index=0'),"-m","1024M","-vga","std","-display","none","-serial",('file:"' + $serialPath + '"'),"-monitor",("tcp:127.0.0.1:$port,server,nowait"),"-no-reboot","-no-shutdown","-rtc","base=utc,clock=host")
        Log-Command ('"' + $qemu + '" ' + ($qemuArgs -join ' '))
        $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -WindowStyle Hidden -PassThru
        $completed = $false
        $timedOut = $false
        try {
            $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
            while ((Get-Date) -lt $deadline -and -not $qemuProcess.HasExited) {
                Start-Sleep -Milliseconds 250
                if (Test-Path -LiteralPath $serialPath) {
                    $liveText = Get-Content -LiteralPath $serialPath -Raw
                    $liveValidation = ($liveText -replace '\[IRQ\] dispatch irq=00\s*', '') -replace '\s+', ' '
                    if ($liveValidation -match '\[nativeaot-gc-segment-boundary\] ALL_(PASS|FAIL)') { $completed = $true; break }
                }
            }
            if (-not $completed) { $timedOut = $true; Read-Monitor $port $monitorPath; throw "QEMU $name timed out after $TimeoutSeconds seconds." }
        } finally {
            if (-not $qemuProcess.HasExited) { Stop-Process -Id $qemuProcess.Id -Force }
            try { $qemuProcess.WaitForExit() } catch { }
            [ordered]@{ run=$name; timeoutSeconds=$TimeoutSeconds; triggered=$timedOut; processId=$qemuProcess.Id; serialPath=$serialPath; monitorPath=$monitorPath } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $oneRoot "watchdog.json") -Encoding ASCII
        }
        Require-File $serialPath "Fresh QEMU serial log"
        $serial = Get-Content -LiteralPath $serialPath -Raw
        Set-Content -LiteralPath (Join-Path $oneRoot "serial.sha256") -Value (Hash-File $serialPath) -Encoding ASCII
        if ($serial -match 'nativeaot-gc-startup-qemu-test\] ALL_PASS|DesktopStateReady|AppRegistry initialized|Welcome to guideXOS|GC\.Collect|RhShutdown|RhpShutdown|GC_Shutdown') { throw "Forbidden or startup-only marker appeared in $name." }
        # QEMU can interleave the timer IRQ diagnostic between two serial
        # characters.  Remove that fixed diagnostic token, rather than
        # replacing it with whitespace, so a split marker such as
        # `P[IRQ] dispatch irq=00` + `rocess` remains matchable.
        $validationText = ($serial -replace '\[IRQ\] dispatch irq=00\s*', ' ') -replace '\s+', ' '
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] Managed entry once: PASS' "managed entry"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] Managed byte\[4096\] bounded loop return: PASS' "managed loop"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] Exact multi-refill allocation counters: PASS' "exact counters"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] First GC heap commit or segment transition boundary: PASS' "boundary"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] Source-backed segment identity and size: PASS' "segment metadata"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] No collection or finalization: PASS' "no collection"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] Primitive-array object and ownership validation: PASS' "object validation"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] Process teardown: PASS' "teardown"
        Assert-Text $validationText '\[nativeaot-gc-segment-boundary\] ALL_PASS' "completion"
        $experimentalEspHash = Hash-File (Join-Path $espRoot "kernel.elf")
        $runResults += [ordered]@{ name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); experimentalKernelSha256=$experimentalEspHash; experimentalEspSha256=$experimentalEspHash }
    }

    $identity = Get-Content -LiteralPath $identityManifestPath -Raw | ConvertFrom-Json
    $replacementHashes = @($identity.replacementObjects | ForEach-Object { [ordered]@{ path=$_.path; sha256=$_.sha256 } })
    $manifest = [ordered]@{
        outcome="A/Bounded primitive-array allocations through multiple refills to first GC heap commit or segment transition"
        arrayLength=4096; hardAllocationLimit=384
        artifactPe=$pePath; artifactPeSha256=(Hash-File $pePath)
        artifactElf=$elfPath; artifactElfSha256=(Hash-File $elfPath)
        adaptedGcArchive=$adaptedArchive; adaptedGcArchiveSha256=(Hash-File $adaptedArchive)
        activePalArchive=$activeArchive; activePalArchiveSha256=(Hash-File $activeArchive)
        embeddedObject=$embeddedObj; embeddedObjectSha256=(Hash-File $embeddedObj)
        specializedKernelSha256=$specializedKernelHash
        authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; prohibitedInventoryCount=$identity.prohibitedInventoryCount; stockUnchanged=$identity.stockUnchanged }
        palReplacementHashes=$replacementHashes
        selectors=@("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST=1")
        runs=$runResults; processTeardown="PASS"; runtimeLevelShutdown="NOT SUPPORTED"; normalKernelSha256=$normalKernelHash
    }
    $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $runRoot "manifest.json") -Encoding ASCII
    Write-Host "Segment-boundary QEMU experiment: PASS" -ForegroundColor Green
    Write-Host "PE SHA256: $($manifest.artifactPeSha256)" -ForegroundColor Cyan
    Write-Host "ELF SHA256: $($manifest.artifactElfSha256)" -ForegroundColor Cyan
    Write-Host "Specialized kernel SHA256: $specializedKernelHash" -ForegroundColor Cyan
}
finally {
    if ([string]::IsNullOrWhiteSpace($make) -eq $false) {
        try { & $make -C kernel ARCH=amd64 clean *> (Join-Path $runRoot "kernel-clean.log") } catch { }
    }
    if (Test-Path -LiteralPath $normalKernelSource -PathType Leaf) {
        New-Item -ItemType Directory -Force -Path (Split-Path $kernelPath), (Join-Path $root "ESP") | Out-Null
        Copy-Item -LiteralPath $normalKernelSource -Destination $kernelPath -Force
        Copy-Item -LiteralPath $normalKernelSource -Destination (Join-Path $root "ESP\kernel.elf") -Force
        $restoredHash = Hash-File (Join-Path $root "ESP\kernel.elf")
        Set-Content -LiteralPath (Join-Path $runRoot "restored-normal-kernel.sha256") -Value $restoredHash -Encoding ASCII
        if ($restoredHash -ne $normalKernelHash) { throw "Normal kernel restoration hash mismatch." }
        $manifestPath = Join-Path $runRoot "manifest.json"
        if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
            $completedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            $completedManifest | Add-Member -NotePropertyName restoredNormalKernelSha256 -NotePropertyValue $restoredHash -Force
            $completedManifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        }
    }
}
