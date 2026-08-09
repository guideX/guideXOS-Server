param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 90,
    [switch]$SkipManagedBuild,
    [ValidateSet("single-thread-suspend-ee", "allocation-context-fixup-root-boundary", "first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary")]
    [string]$ProofMode = "single-thread-suspend-ee"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($ProofMode -eq "first-non-null-root-callback-boundary") {
        Join-Path $root "out\dotnet\gc-first-non-null-root-callback-boundary"
    } elseif ($ProofMode -eq "first-root-candidate-load") {
        Join-Path $root "out\dotnet\gc-first-root-candidate-load"
    } elseif ($ProofMode -eq "first-per-thread-root-provider") {
        Join-Path $root "out\dotnet\gc-first-per-thread-root-provider"
    } elseif ($ProofMode -eq "allocation-context-fixup-root-boundary") {
        Join-Path $root "out\dotnet\gc-allocation-context-fixup-root-boundary"
    } else {
        Join-Path $root "out\dotnet\gc-single-thread-suspend-ee"
    }
}
$isFirstRootCandidateLoad = $ProofMode -eq "first-root-candidate-load"
$isFirstNonNullRoot = $ProofMode -eq "first-non-null-root-callback-boundary"
$isCandidateLoadEnumeration = $isFirstRootCandidateLoad -or $isFirstNonNullRoot
$isFirstPerThreadRootProvider = $ProofMode -in @("first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary")
$isAllocationContextFixupRootBoundary = $ProofMode -in @("allocation-context-fixup-root-boundary", "first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary")
$proofDefine = if ($isFirstNonNullRoot) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION"
} elseif ($isFirstRootCandidateLoad) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_ALLOCATION"
} elseif ($isFirstPerThreadRootProvider) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION"
} elseif ($isAllocationContextFixupRootBoundary) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION"
} else {
    ""
}
$evidence = [System.IO.Path]::GetFullPath($EvidenceRoot)
$runRoot = Join-Path $evidence (Get-Date -Format "run-yyyyMMdd-HHmmssfff")
$buildRoot = Join-Path $evidence "build"
$artifactRoot = Join-Path $buildRoot "artifact"
$runtimeRoot = Join-Path $buildRoot "runtime-pack"
$oldRoot = Join-Path $root "out\dotnet\gc-first-real-allocation"
$oldArtifact = Join-Path $oldRoot "managed-artifact"
$gcStartupRoot = Join-Path $root "out\dotnet\gc-initialization-dry-run\artifact"
$managedPublishRoot = Join-Path $buildRoot "managed"

$pePath = Join-Path $artifactRoot "NativeAotGcSingleThreadSuspendEe.exe"
$elfPath = Join-Path $artifactRoot "NativeAotGcSingleThreadSuspendEe.elf"
$mapPath = Join-Path $artifactRoot "NativeAotGcSingleThreadSuspendEe.map"
$kernelPath = Join-Path $root "kernel\build\amd64\bin\kernel.elf"
$espKernelPath = Join-Path $root "ESP\kernel.elf"
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
    if ($Text -notmatch $Pattern) { throw "Missing NativeAOT GC evidence for ${Label}: ${Pattern}" }
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

function Get-MarkerField([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, '\b' + [regex]::Escape($Name) + '=(?<value>[0-9A-Fa-f]+)')
    if (-not $match.Success) { return $null }
    return "0x" + $match.Groups['value'].Value.ToUpperInvariant()
}

New-Item -ItemType Directory -Force -Path $runRoot, $buildRoot, $artifactRoot, $runtimeRoot | Out-Null
$ordinaryBuildBefore = if (Test-Path -LiteralPath $kernelPath -PathType Leaf) { Hash-File $kernelPath } else { $null }
$ordinaryEspBefore = if (Test-Path -LiteralPath $espKernelPath -PathType Leaf) { Hash-File $espKernelPath } else { $null }
$ordinaryKernelBefore = [ordered]@{ build=$ordinaryBuildBefore; esp=$ordinaryEspBefore }
foreach ($path in @($normalKernelSource,$bootloader,$qemu,$ovmf,$objcopy,$objdump,$readelf,$python,$converter,$vsBat,$dotnet,$activeArchive)) {
    Require-File $path "Required experiment input"
}
if ([string]::IsNullOrWhiteSpace($make)) { throw "mingw32-make.exe/make.exe was not found." }
if ((Hash-File $activeArchive) -ne $activeArchiveHash) { throw "Active PAL archive hash changed." }
$qemuVersion = (& $qemu --version | Select-Object -First 1)
Set-Content -LiteralPath (Join-Path $runRoot "qemu-version.txt") -Value $qemuVersion -Encoding ASCII

$sourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source\src\coreclr"
$nativeAotRoot = Join-Path $sourceRoot "nativeaot"
$palSourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source\src\native"
$lockedSourceRoot = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\locked-source"
$platformSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"
$probeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_allocation_probe.cpp"
$hostShimSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_managed_host_shims.cpp"
$startupProbeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_startup_probe.cpp"
$runtimeSupportSource = Join-Path $root "samples\managed\HostLogProof\runtime_support.c"
$replacementRoot = Join-Path $root "out\dotnet\gc-first-real-allocation\identity\build1\rebuilt"
$identityManifestPath = Join-Path $root "out\dotnet\gc-first-real-allocation\identity\build1\stock\identity.json"
$palBridge = Join-Path $root "out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.bridge.obj"
$gcEnv = Join-Path $replacementRoot "guidexos_gcenv.obj"
$gcEnvEeSource = Join-Path $runtimeRoot "gcenv.ee.single-thread-suspend-ee.cpp"
$gcEnvEe = Join-Path $runtimeRoot "gcenv.ee.single-thread-suspend-ee.obj"
$gcEnumSource = Join-Path $runtimeRoot "GcEnum.first-root-candidate-load.cpp"
$gcEnum = Join-Path $runtimeRoot "GcEnum.first-root-candidate-load.obj"
$platformObj = Join-Path $runtimeRoot "guidexos_nativeaot_platform.single-thread-suspend-ee.obj"
$probeObj = Join-Path $runtimeRoot "guidexos_nativeaot_gc_allocation_probe.single-thread-suspend-ee.obj"
$gcBridgeBoundary = Join-Path $runtimeRoot "guidexos_nativeaot_gcenv_startup_bridge.single-thread-suspend-ee.obj"
$gcBridgeSource = Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcenv_startup_bridge.cpp"
$platformContract = Join-Path $gcStartupRoot "guidexos_nativeaot_gc_startup_platform_contract.obj"
$palStartup = Join-Path $gcStartupRoot "PalRedhawkMinWin.gc-startup.obj"
$startupDiagnostic = Join-Path $gcStartupRoot "startup-diagnostic.obj"
$gcHelpersDiagnostic = Join-Path $gcStartupRoot "gc-helpers-diagnostic.obj"
$gcHelpersAlign = Join-Path $gcStartupRoot "gc-helpers-align-up.obj"
$threadObj = Join-Path $oldArtifact "thread.renamed.obj"
$ehObj = Join-Path $oldArtifact "EHHelpers.renamed.obj"
$allocFastObj = Join-Path $oldArtifact "AllocFast.renamed.obj"
$runtimeSupportObj = Join-Path $artifactRoot "runtime_support.obj"
$hostShimObj = Join-Path $artifactRoot "guidexos_nativeaot_managed_host_shims.obj"
$startupProbeObj = Join-Path $artifactRoot "guidexos_nativeaot_gc_startup_probe.managed.obj"
$adaptedArchive = Join-Path $artifactRoot "Runtime.WorkstationGC.guidexos-nativeaot-single-thread-suspend-ee.lib"
$manifestPath = Join-Path $runRoot "manifest.json"
$runResults = @()

foreach ($path in @($palBridge,$gcEnv,$gcBridgeSource,$platformContract,$palStartup,$startupDiagnostic,$gcHelpersDiagnostic,$gcHelpersAlign,$threadObj,$ehObj,$allocFastObj)) {
    Require-File $path "Authorized replacement input"
}
Require-File $identityManifestPath "Authorized normalized adapted-GC identity manifest"

try {
    $repoHead = (& git -C $root rev-parse HEAD).Trim()
    $dirtyState = @(& git -C $root status --short)
    $dirtySummary = ((& git -C $root diff --stat) -join "`n")
    $lockedCommit = "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
    $lockedEePath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\gcenv.ee.cpp"
    Require-File $lockedEePath "Locked NativeAOT EE source"
    $lockedEeText = Get-Content -LiteralPath $lockedEePath -Raw
    if ($isFirstPerThreadRootProvider) {
        $storageObserverAttribute = if ($isCandidateLoadEnumeration) { "" } else { "__declspec(noreturn) " }
        $declaration = @'
extern "C" void __cdecl guideXosNativeAotSuspendEeEntry(uint32_t reason);
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterLock();
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterSuspend();
extern "C" void __cdecl guideXosNativeAotSuspendEeBodyReturn();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveEntry();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveReturn();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupGcStartWorkObserver();
extern "C" void __cdecl guideXosNativeAotAllocationRootPhaseRequested();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationEntry();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupContextVisited(uintptr_t context);
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationComplete();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootGcScanRootsEntered();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootForeachThreadEntered();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootIteratorInitialized();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootIteratorCompletion();
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadEnumerated(uintptr_t thread);
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadExcluded(uintptr_t thread, uint32_t reason);
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadIncluded(uintptr_t thread);
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootThreadStaticListObserved(uintptr_t thread, uintptr_t list);
extern "C" STORAGE_OBSERVER_ATTRIBUTEvoid __cdecl guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered(uintptr_t thread, uintptr_t storage);
'@
        $declaration = $declaration.Replace('STORAGE_OBSERVER_ATTRIBUTE', $storageObserverAttribute)
    } elseif ($isAllocationContextFixupRootBoundary) {
        $declaration = @'
extern "C" void __cdecl guideXosNativeAotSuspendEeEntry(uint32_t reason);
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterLock();
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterSuspend();
extern "C" void __cdecl guideXosNativeAotSuspendEeBodyReturn();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveEntry();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveReturn();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupGcStartWorkObserver();
extern "C" void __cdecl guideXosNativeAotAllocationRootPhaseRequested();
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotAllocationContextFixupRootBoundary();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationEntry();
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupContextVisited(uintptr_t context);
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationComplete();
'@
    } else {
        $declaration = @'
extern "C" void __cdecl guideXosNativeAotSuspendEeEntry(uint32_t reason);
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterLock();
extern "C" void __cdecl guideXosNativeAotSuspendEeAfterSuspend();
extern "C" void __cdecl guideXosNativeAotSuspendEeBodyReturn();
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveEntry();
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotDisablePreemptiveReturn();
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotSuspendEeGcStartWorkBoundary();
'@
    }
    $lockedEeText = $lockedEeText.Replace('#include "volatile.h"', '#include "volatile.h"' + [Environment]::NewLine + [Environment]::NewLine + $declaration.TrimEnd())
    $suspendPattern = '(?m)^void GCToEEInterface::SuspendEE\(SUSPEND_REASON reason\)\r?\n\{'
    $suspendReplacement = 'void GCToEEInterface::SuspendEE(SUSPEND_REASON reason)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeEntry((uint32_t)reason);'
    $injectedText = [regex]::Replace($lockedEeText, $suspendPattern, $suspendReplacement, 1)
    $injectedText = $injectedText.Replace('    GetThreadStore()->LockThreadStore();', '    GetThreadStore()->LockThreadStore();' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeAfterLock();')
    $injectedText = $injectedText.Replace('    GetThreadStore()->SuspendAllThreads(true);', '    GetThreadStore()->SuspendAllThreads(true);' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeAfterSuspend();')
    $injectedText = $injectedText.Replace('    FireEtwGCSuspendEEEnd_V1(GetClrInstanceId());', '    FireEtwGCSuspendEEEnd_V1(GetClrInstanceId());' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeBodyReturn();')
    $disablePattern = '(?m)^void GCToEEInterface::DisablePreemptiveGC\(\)\r?\n\{'
    $disableReplacement = 'void GCToEEInterface::DisablePreemptiveGC()' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotDisablePreemptiveEntry();'
    $injectedText = [regex]::Replace($injectedText, $disablePattern, $disableReplacement, 1)
    $injectedText = $injectedText.Replace('    ThreadStore::GetCurrentThread()->DisablePreemptiveMode();', '    ThreadStore::GetCurrentThread()->DisablePreemptiveMode();' + [Environment]::NewLine + '    guideXosNativeAotDisablePreemptiveReturn();')
    if ($isFirstPerThreadRootProvider) {
        $gcStartPattern = '(?m)^void GCToEEInterface::GcStartWork\(int condemned, int /\*max_gen\*/\)\r?\n\{'
        $gcStartReplacement = 'void GCToEEInterface::GcStartWork(int condemned, int /*max_gen*/)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupGcStartWorkObserver();'
        $injectedText = [regex]::Replace($injectedText, $gcStartPattern, $gcStartReplacement, 1)
        $beforeRootsPattern = '(?m)^void GCToEEInterface::BeforeGcScanRoots\(int condemned, bool is_bgc, bool is_concurrent\)\r?\n\{'
        $beforeRootsReplacement = 'void GCToEEInterface::BeforeGcScanRoots(int condemned, bool is_bgc, bool is_concurrent)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationRootPhaseRequested();'
        $injectedText = [regex]::Replace($injectedText, $beforeRootsPattern, $beforeRootsReplacement, 1)
        $scanRootsPattern = '(?m)^void GCToEEInterface::GcScanRoots\(ScanFunc\* fn, int condemned, int max_gen, ScanContext\* sc\)\r?\n\{'
        $scanRootsReplacement = 'void GCToEEInterface::GcScanRoots(ScanFunc* fn, int condemned, int max_gen, ScanContext* sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotFirstPerThreadRootGcScanRootsEntered();'
        $injectedText = [regex]::Replace($injectedText, $scanRootsPattern, $scanRootsReplacement, 1)
        $enumPattern = '(?m)^void GCToEEInterface::GcEnumAllocContexts\(enum_alloc_context_func\* fn, void\* param\)\r?\n\{'
        $enumReplacement = 'void GCToEEInterface::GcEnumAllocContexts(enum_alloc_context_func* fn, void* param)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationEntry();'
        $injectedText = [regex]::Replace($injectedText, $enumPattern, $enumReplacement, 1)
        $injectedText = $injectedText.Replace('        (*fn) (thread->GetAllocContext(), param);', '        guideXosNativeAotAllocationContextFixupContextVisited(reinterpret_cast<uintptr_t>(thread->GetAllocContext()));' + [Environment]::NewLine + '        (*fn) (thread->GetAllocContext(), param);')
        $enumEnd = '    END_FOREACH_THREAD' + [Environment]::NewLine + '}'
        $injectedText = $injectedText.Replace($enumEnd, '    END_FOREACH_THREAD' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationComplete();' + [Environment]::NewLine + '}')

        $foreachOpen = '    FOREACH_THREAD(pThread)' + [Environment]::NewLine + '    {'
        $foreachReplacement = @"
    guideXosNativeAotFirstPerThreadRootForeachThreadEntered();
    {
        ThreadStore::Iterator __threads;
        guideXosNativeAotFirstPerThreadRootIteratorInitialized();
        Thread* pThread;
        while ((pThread = __threads.GetNext()) != NULL)
        {
            guideXosNativeAotFirstPerThreadRootThreadEnumerated(reinterpret_cast<uintptr_t>(pThread));
"@
        if (-not $injectedText.Contains($foreachOpen)) { throw "Locked GcScanRoots FOREACH_THREAD opening was not found." }
        $injectedText = $injectedText.Replace($foreachOpen, $foreachReplacement.TrimEnd())
        $injectedText = $injectedText.Replace('        if (pThread->IsGCSpecial())' + [Environment]::NewLine + '            continue;', '        if (pThread->IsGCSpecial())' + [Environment]::NewLine + '        {' + [Environment]::NewLine + '            guideXosNativeAotFirstPerThreadRootThreadExcluded(reinterpret_cast<uintptr_t>(pThread), 1u);' + [Environment]::NewLine + '            continue;' + [Environment]::NewLine + '        }')
        $injectedText = $injectedText.Replace('            InlinedThreadStaticRoot* pRoot = pThread->GetInlinedThreadStaticList();', '            guideXosNativeAotFirstPerThreadRootThreadIncluded(reinterpret_cast<uintptr_t>(pThread));' + [Environment]::NewLine + '            InlinedThreadStaticRoot* pRoot = pThread->GetInlinedThreadStaticList();' + [Environment]::NewLine + '            guideXosNativeAotFirstPerThreadRootThreadStaticListObserved(reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pRoot));')
        $injectedText = $injectedText.Replace('            EnumGcRef(pThread->GetThreadStaticStorage(), GCRK_Object, fn, sc);', '            Object** threadStaticStorage = pThread->GetThreadStaticStorage();' + [Environment]::NewLine + '            guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered(reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(threadStaticStorage));' + [Environment]::NewLine + '            EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);')
        $injectedText = $injectedText.Replace('    END_FOREACH_THREAD' + [Environment]::NewLine + [Environment]::NewLine + '    sc->thread_under_crawl = NULL;', '        guideXosNativeAotFirstPerThreadRootIteratorCompletion();' + [Environment]::NewLine + '    }' + [Environment]::NewLine + [Environment]::NewLine + '    sc->thread_under_crawl = NULL;')
        if ($injectedText -eq $lockedEeText -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootGcScanRootsEntered' -or
            $injectedText -notmatch 'ThreadStore::Iterator __threads' -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootThreadEnumerated' -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootThreadStaticListObserved' -or
            $injectedText -notmatch 'guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered') {
            throw "Locked gcenv.ee.cpp first-per-thread-root-provider injection did not match all required boundaries."
        }
    } elseif ($isAllocationContextFixupRootBoundary) {
        $gcStartPattern = '(?m)^void GCToEEInterface::GcStartWork\(int condemned, int /\*max_gen\*/\)\r?\n\{'
        $gcStartReplacement = 'void GCToEEInterface::GcStartWork(int condemned, int /*max_gen*/)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupGcStartWorkObserver();'
        $injectedText = [regex]::Replace($injectedText, $gcStartPattern, $gcStartReplacement, 1)
        $beforeRootsPattern = '(?m)^void GCToEEInterface::BeforeGcScanRoots\(int condemned, bool is_bgc, bool is_concurrent\)\r?\n\{'
        $beforeRootsReplacement = 'void GCToEEInterface::BeforeGcScanRoots(int condemned, bool is_bgc, bool is_concurrent)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationRootPhaseRequested();'
        $injectedText = [regex]::Replace($injectedText, $beforeRootsPattern, $beforeRootsReplacement, 1)
        $scanRootsPattern = '(?m)^void GCToEEInterface::GcScanRoots\(ScanFunc\* fn, int condemned, int max_gen, ScanContext\* sc\)\r?\n\{'
        $scanRootsReplacement = 'void GCToEEInterface::GcScanRoots(ScanFunc* fn, int condemned, int max_gen, ScanContext* sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupRootBoundary();'
        $injectedText = [regex]::Replace($injectedText, $scanRootsPattern, $scanRootsReplacement, 1)
        $enumPattern = '(?m)^void GCToEEInterface::GcEnumAllocContexts\(enum_alloc_context_func\* fn, void\* param\)\r?\n\{'
        $enumReplacement = 'void GCToEEInterface::GcEnumAllocContexts(enum_alloc_context_func* fn, void* param)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationEntry();'
        $injectedText = [regex]::Replace($injectedText, $enumPattern, $enumReplacement, 1)
        $injectedText = $injectedText.Replace('        (*fn) (thread->GetAllocContext(), param);', '        guideXosNativeAotAllocationContextFixupContextVisited(reinterpret_cast<uintptr_t>(thread->GetAllocContext()));' + [Environment]::NewLine + '        (*fn) (thread->GetAllocContext(), param);')
        $enumEnd = '    END_FOREACH_THREAD' + [Environment]::NewLine + '}'
        $injectedText = $injectedText.Replace($enumEnd, '    END_FOREACH_THREAD' + [Environment]::NewLine + '    guideXosNativeAotAllocationContextFixupEnumerationComplete();' + [Environment]::NewLine + '}')
        if ($injectedText -eq $lockedEeText -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupEnumerationEntry' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupContextVisited' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupEnumerationComplete' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupGcStartWorkObserver' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationRootPhaseRequested' -or
            $injectedText -notmatch 'guideXosNativeAotAllocationContextFixupRootBoundary') {
            throw "Locked gcenv.ee.cpp allocation-context/root-boundary injection did not match all required boundaries."
        }
    } else {
        $gcStartPattern = '(?m)^void GCToEEInterface::GcStartWork\(int condemned, int /\*max_gen\*/\)\r?\n\{'
        $gcStartReplacement = 'void GCToEEInterface::GcStartWork(int condemned, int /*max_gen*/)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotSuspendEeGcStartWorkBoundary();'
        $injectedText = [regex]::Replace($injectedText, $gcStartPattern, $gcStartReplacement, 1)
        if ($injectedText -eq $lockedEeText -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeEntry' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeAfterLock' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeAfterSuspend' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeBodyReturn' -or
            $injectedText -notmatch 'guideXosNativeAotDisablePreemptiveEntry' -or
            $injectedText -notmatch 'guideXosNativeAotDisablePreemptiveReturn' -or
            $injectedText -notmatch 'guideXosNativeAotSuspendEeGcStartWorkBoundary') {
            throw "Locked gcenv.ee.cpp injection did not match all required boundaries."
        }
    }
    Set-Content -LiteralPath $gcEnvEeSource -Value $injectedText -Encoding ASCII
    if ($isCandidateLoadEnumeration) {
        $lockedGcEnumPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\GcEnum.cpp"
        Require-File $lockedGcEnumPath "Locked NativeAOT GcEnum source"
        $gcEnumText = Get-Content -LiteralPath $lockedGcEnumPath -Raw
        if ($isFirstNonNullRoot) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstNonNullRootCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" uint32_t __cdecl guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);'
            ) -join [Environment]::NewLine
        } else {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstRootCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);'
            ) -join [Environment]::NewLine
        }
        $gcEnumText = $gcEnumText.Replace('#include "GcEnum.h"', '#include "GcEnum.h"' + [Environment]::NewLine + [Environment]::NewLine + $gcEnumDeclaration.TrimEnd())
        $gcEnumPattern = '(?m)^static void GcEnumObject\(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc\* fnGcEnumRef, ScanContext\* pSc\)\r?\n\{'
        if ($isFirstNonNullRoot) {
            $gcEnumReplacement = @(
                'static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)',
                '{',
                '    guideXosNativeAotFirstNonNullRootCandidateLoadRequested(',
                '        reinterpret_cast<uintptr_t>(ppObj), flags,',
                '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                '        reinterpret_cast<uintptr_t>(pSc));',
                '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                '    if (guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded(',
                '            reinterpret_cast<uintptr_t>(ppObj), candidateRawValue) == 0u)',
                '        return;'
            ) -join [Environment]::NewLine
        } else {
            $gcEnumReplacement = @(
                'static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)',
                '{',
                '    guideXosNativeAotFirstRootCandidateLoadRequested(',
                '        reinterpret_cast<uintptr_t>(ppObj), flags,',
                '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                '        reinterpret_cast<uintptr_t>(pSc));',
                '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                '    guideXosNativeAotFirstRootCandidateMachineWordLoaded(',
                '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue);'
            ) -join [Environment]::NewLine
        }
        $gcEnumInjected = [regex]::Replace($gcEnumText, $gcEnumPattern, $gcEnumReplacement.TrimEnd(), 1)
        if ($gcEnumInjected -eq $gcEnumText -or
            $gcEnumInjected -notmatch 'const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>\(\*ppObj\);' -or
            (($isFirstNonNullRoot -and $gcEnumInjected -notmatch 'guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded') -or
             (-not $isFirstNonNullRoot -and $gcEnumInjected -notmatch 'guideXosNativeAotFirstRootCandidateMachineWordLoaded'))) {
            throw "Locked GcEnum.cpp first-root-candidate-load injection did not match the GcEnumObject load boundary."
        }
        Set-Content -LiteralPath $gcEnumSource -Value $gcEnumInjected -Encoding ASCII
    }
    $baselineDescription = if ($isFirstNonNullRoot) {
        "experiment=single-managed-mutator Workstation GC real thread-static proof root and first non-null candidate callback boundary"
    } elseif ($isFirstPerThreadRootProvider) {
        "experiment=single-managed-mutator Workstation GC real FOREACH_THREAD enumeration and first per-thread root provider entry"
    } elseif ($isAllocationContextFixupRootBoundary) {
        "experiment=single-managed-mutator Workstation GC fix_allocation_contexts(TRUE) completion and first root-dispatch boundary"
    } else {
        "experiment=single-managed-mutator Workstation GC SuspendEE completion and post-DisablePreemptiveGC boundary"
    }
    $safeStopDescription = if ($isFirstPerThreadRootProvider) {
        if ($isFirstNonNullRoot) {
            "safeStop=after a genuine non-null managed thread-static candidate load and before the first root callback"
        } elseif ($isFirstRootCandidateLoad) {
            "safeStop=after the real EnumGcRef path's one explicit slot load and before callback/semantic processing"
        } else {
            "safeStop=after real ThreadStore::Iterator enumeration and Thread::GetThreadStaticStorage entry before EnumGcRef candidate access"
        }
    } elseif ($isAllocationContextFixupRootBoundary) {
        "safeStop=after real allocation-context enumeration and GcStartWork; at GcScanRoots entry before FOREACH_THREAD"
    } else {
        "safeStop=after real ThreadStore::LockThreadStore and SuspendAllThreads return, at GcStartWork entry"
    }
    Set-Content -LiteralPath (Join-Path $runRoot "baseline.txt") -Value @(
        $baselineDescription,
        "arrayLength=4096",
        "hardAllocationLimit=256",
        "heapReserveCommit=locked adapted Workstation GC configuration",
        "nativeAotSourceCommit=$lockedCommit",
        "runtimePack=9.0.0 AMD64 Workstation GC interface=5.3 EE=2",
        "collectionPath=soh_try_fit->a_fit_segment_end_p->a_state_trigger_ephemeral_gc->trigger_ephemeral_gc->GCHeap::GarbageCollectGeneration->GCToEEInterface::SuspendEE->GCHeap::GarbageCollect before fix_allocation_contexts",
        $safeStopDescription,
        "activePalArchiveSha256=$(Hash-File $activeArchive)",
        "normalKernelSha256=$normalKernelHash"
    ) -Encoding ASCII

    $runtimeBat = Write-Batch "build-single-thread-suspend-ee-runtime-pack.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DGUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION /DGUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ARRAY_LENGTH=4096 /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_HARD_LIMIT=256 /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$sourceRoot\pal\src\include" /Fo:"$platformObj" "$platformSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGXOS_BARE_METAL /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$nativeAotRoot\Runtime" /I"$sourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$sourceRoot\pal\src\include" /Fo:"$gcBridgeBoundary" "$gcBridgeSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime" /I"$sourceRoot\native" /Fo:"$(Join-Path $runtimeRoot 'guidexos_gcenv.single-thread-suspend-ee.obj')" "$(Join-Path $root 'tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp')"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\eventpipe" /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$sourceRoot\pal\src\include" /FI"$sourceRoot\gc\env\common.h" /Fo:"$gcEnvEe" "$gcEnvEeSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DHOST_64BIT /DTARGET_64BIT /DHOST_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /I"$nativeAotRoot\Runtime" /I"$nativeAotRoot\Runtime\windows" /I"$sourceRoot" /I"$palSourceRoot" /I"$sourceRoot\native" /I"$sourceRoot\gc" /I"$sourceRoot\gc\env" /I"$nativeAotRoot\Runtime\inc" /I"$nativeAotRoot\Runtime\eventpipe" /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$sourceRoot\pal\src\include" /FI"$sourceRoot\gc\env\common.h" /Fo:"$probeObj" "$probeSource"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    if ($isCandidateLoadEnumeration) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $gcEnumCompileLine = 'cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /I"{0}\Runtime" /I"{0}\Runtime\windows" /I"{1}" /I"{1}\native" /I"{1}\gc" /I"{1}\gc\env" /I"{0}\Runtime\inc" /I"{0}\Runtime\eventpipe" /I"{2}" /I"{5}" /I"{1}\pal\src\include" /FI"{1}\gc\env\common.h" /Fo:"{3}" "{4}"' -f $nativeAotRoot, $sourceRoot, (Join-Path $root 'tools\dotnet\runtime-pack\src\platform'), $gcEnum, $gcEnumSource, $palSourceRoot
        $runtimeBatText = $runtimeBatText.Replace("exit /b 0", "$gcEnumCompileLine`r`nif errorlevel 1 exit /b %errorlevel%`r`nexit /b 0")
        Set-Content -LiteralPath $runtimeBat -Value $runtimeBatText -Encoding ASCII
    }
    if ($isAllocationContextFixupRootBoundary) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $runtimeBatText = $runtimeBatText.Replace(
            "/DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION ",
            "/DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION $proofDefine ")
        Set-Content -LiteralPath $runtimeBat -Value $runtimeBatText -Encoding ASCII
    }
    $probeObjectPropertyArgument = if ($isAllocationContextFixupRootBoundary) {
        "-p:HostLogProofRuntimePackProbeObj=$probeObj"
    } else {
        ""
    }
    $managedProofMode = if ($isFirstNonNullRoot) { "FirstNonNullRoot" } else { "FirstCollectionBoundary" }
    $artifactBat = Write-Batch "build-single-thread-suspend-ee-artifact.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /c /GS- /Zl /Fo:"$runtimeSupportObj" "$runtimeSupportSource"
if errorlevel 1 exit /b %errorlevel%
"$dotnet" publish "$(Join-Path $root 'samples\managed\HostLogProof\HostLogProof.csproj')" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true -p:HostLogProofRuntimeSupportObj=$runtimeSupportObj -p:HostLogProofMapPath=$mapPath -p:HostLogProofMode=$managedProofMode -p:BaseOutputPath=$managedPublishRoot\bin\ -p:BaseIntermediateOutputPath=$managedPublishRoot\obj\ -p:HostLogProofRuntimePackObj=$platformObj $probeObjectPropertyArgument -p:IlcSdkPath=$oldArtifact\sdk\
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /Fo:"$hostShimObj" "$hostShimSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DGUIDEXOS_NATIVEAOT_MANAGED_IMAGE /I"$(Join-Path $root 'tools\dotnet\runtime-pack\src\platform')" /I"$nativeAotRoot\Runtime" /Fo:"$startupProbeObj" "$startupProbeSource"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    $gcEnumArchiveArgs = if ($isCandidateLoadEnumeration) {
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\GcEnum.cpp.obj`" `"$gcEnum`""
    } else {
        ""
    }
    $archiveBat = Write-Batch "build-single-thread-suspend-ee-gc-archive.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
 lib.exe /nologo /OUT:"$adaptedArchive" "$activeArchive" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\PalRedhawkMinWin.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\thread.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.obj')" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\windows\gcenv.windows.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\gcenv.ee.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\amd64\AllocFast.asm.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj" "$palBridge" "$palStartup" "$(Join-Path $runtimeRoot 'guidexos_gcenv.single-thread-suspend-ee.obj')" "$gcEnvEe" "$gcBridgeBoundary" "$platformContract" "$threadObj" "$ehObj" "$allocFastObj" "$probeObj" $gcEnumArchiveArgs
if errorlevel 1 exit /b %errorlevel%
exit /b 0
"@
    $linkBat = Write-Batch "link-single-thread-suspend-ee.bat" @"
@echo off
call "$vsBat" >nul
link.exe /nologo /MANIFEST:NO /INCREMENTAL:NO /fixed /base:0x10000000 /SUBSYSTEM:NATIVE /ENTRY:GuideXosNativeAotGcStartupMain /OUT:"$pePath" /MAP:"$mapPath" /INCLUDE:RhInitialize /EXPORT:GuideXosNativeAotGcStartupMain /EXPORT:GuideXosNativeAotGcStartupInstallPalHooks /EXPORT:GuideXosNativeAotGcStartupInstallHookTable /EXPORT:GuideXosNativeAotGcStartupInstallPlatformHooks /EXPORT:GuideXosNativeAotGcStartupGetState /EXPORT:GuideXosNativeAotGcStartupGetPreGcState /EXPORT:GuideXosNativeAotGcStartupGetAllocationCount /EXPORT:GuideXosNativeAotGcStartupGetLastAllocationSize /EXPORT:GuideXosNativeAotGcStartupGetDiagnosticStage /EXPORT:ManagedMain /EXPORT:guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment /EXPORT:guideXosManagedAllocationFinalize /EXPORT:guideXosManagedAllocationGetDiagnostics /EXPORT:guideXosManagedAllocationValidateObject /EXPORT:guideXosManagedAllocationRecordSentinelValidation /EXPORT:guideXosManagedAllocationGetLoopStatus /EXPORT:guideXosManagedAllocationGetHardLimit /NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib /IGNORE:4104 "$managedPublishRoot\obj\x64\Release\net9.0\win-x64\native\HostLogProof.obj" "$runtimeSupportObj" "$platformObj" "$oldArtifact\sdk\bootstrapper.obj" "$adaptedArchive" "$startupProbeObj" "$hostShimObj" "$startupDiagnostic" "$gcHelpersDiagnostic" "$gcHelpersAlign" "$oldArtifact\sdk\eventpipe-disabled.lib" "$oldArtifact\sdk\Runtime.VxsortEnabled.lib" "$oldArtifact\sdk\standalonegc-disabled.lib" "$oldArtifact\sdk\zlibstatic.lib" "$oldArtifact\sdk\System.Globalization.Native.Aot.lib" "$oldArtifact\sdk\System.IO.Compression.Native.Aot.lib"
exit /b %errorlevel%
"@

    if (-not $SkipManagedBuild) {
        $stalePaths = @($platformObj,$gcEnvEe,$probeObj,$gcBridgeBoundary,$runtimeSupportObj,$hostShimObj,$startupProbeObj,$adaptedArchive,$pePath,$elfPath,$mapPath)
        if ($isCandidateLoadEnumeration) { $stalePaths += $gcEnum }
        foreach ($stale in $stalePaths) {
            if (Test-Path -LiteralPath $stale -PathType Leaf) { Remove-Item -LiteralPath $stale -Force }
        }
        Invoke-Batch $runtimeBat "runtime-pack-build.log"
        Invoke-Batch $artifactBat "managed-artifact-build.log"
        Invoke-Batch $archiveBat "gc-archive-build.log"
        Invoke-Batch $linkBat "managed-link.log"
    }
    $requiredBuildOutputs = @($platformObj,$gcEnvEe,$probeObj,$runtimeSupportObj,$hostShimObj,$startupProbeObj,$adaptedArchive,$pePath,$mapPath)
    if ($isCandidateLoadEnumeration) { $requiredBuildOutputs += $gcEnum }
    foreach ($path in $requiredBuildOutputs) { Require-File $path "Single-thread SuspendEE build output" }
    Invoke-LoggedCommand $python @($converter,$pePath,$elfPath,"--map",$mapPath,"--symbol","ManagedMain") (Join-Path $runRoot "pe-to-elf.log")
    Require-File $elfPath "Single-thread SuspendEE ELF"
    Invoke-LoggedCommand $objdump @("-p",$pePath) (Join-Path $runRoot "pe-imports.txt")
    Invoke-LoggedCommand $readelf @("-h","-l","-S","-r","-s","-d",$elfPath) (Join-Path $runRoot "elf-inspection.txt")
    $imports = Get-Content -LiteralPath (Join-Path $runRoot "pe-imports.txt") -Raw
    if ($imports -match 'FlsGetValue|FlsSetValue') { throw "Single-thread SuspendEE PE still exposes live Windows FLS imports." }
    $mapText = Get-Content -LiteralPath $mapPath -Raw
    $requiredSymbols = @("ManagedMain","RhpNewArray","RhpNewArrayRare","RhpGcAlloc","guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment","guideXosNativeAotSuspendEeEntry","guideXosNativeAotSuspendEeAfterLock","guideXosNativeAotSuspendEeAfterSuspend","guideXosNativeAotSuspendEeBodyReturn","guideXosNativeAotDisablePreemptiveEntry","guideXosNativeAotDisablePreemptiveReturn","guideXosManagedAllocationGetDiagnostics")
    if ($isFirstNonNullRoot) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstNonNullRootCandidateLoadRequested","guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded","guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
    } elseif ($isFirstRootCandidateLoad) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstRootCandidateLoadRequested","guideXosNativeAotFirstRootCandidateMachineWordLoaded")
    } elseif ($isFirstPerThreadRootProvider) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered")
    } elseif ($isAllocationContextFixupRootBoundary) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupRootBoundary","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete")
    } else {
        $requiredSymbols += "guideXosNativeAotSuspendEeGcStartWorkBoundary"
    }
    foreach ($symbol in $requiredSymbols) { [void](Extract-MapAddress $mapText $symbol) }
    Assert-Text $mapText 'GcAllocInternal' "decorated GcAllocInternal map symbol"

    $exports = [ordered]@{
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_PAL_ADDRESS = "GuideXosNativeAotGcStartupInstallPalHooks"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_TABLE_ADDRESS = "GuideXosNativeAotGcStartupInstallHookTable"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_PLATFORM_ADDRESS = "GuideXosNativeAotGcStartupInstallPlatformHooks"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_STARTUP_MAIN_ADDRESS = "GuideXosNativeAotGcStartupMain"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetState"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_PRE_GC_STATE_ADDRESS = "GuideXosNativeAotGcStartupGetPreGcState"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_ALLOCATION_COUNT_ADDRESS = "GuideXosNativeAotGcStartupGetAllocationCount"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_LAST_ALLOCATION_SIZE_ADDRESS = "GuideXosNativeAotGcStartupGetLastAllocationSize"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_DIAGNOSTIC_STAGE_ADDRESS = "GuideXosNativeAotGcStartupGetDiagnosticStage"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_MANAGED_MAIN_ADDRESS = "ManagedMain"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_BEGIN_EXPERIMENT_ADDRESS = "guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_FINALIZE_ADDRESS = "guideXosManagedAllocationFinalize"
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_DIAGNOSTICS_ADDRESS = "guideXosManagedAllocationGetDiagnostics"
    }
    $headerLines = @("#pragma once", "", "#include <stdint.h>", "")
    foreach ($define in $exports.Keys) { $headerLines += "#define $define ((uintptr_t)0x$(Extract-MapAddress $mapText $exports[$define])u)" }
    $exportHeader = Join-Path $artifactRoot "guidexos_nativeaot_gc_single_thread_suspend_ee_exports.h"
    Set-Content -LiteralPath $exportHeader -Value $headerLines -Encoding ASCII
    Copy-Item -LiteralPath $exportHeader -Destination (Join-Path $runRoot "guidexos_nativeaot_gc_single_thread_suspend_ee_exports.h") -Force
    $rawObj = Join-Path $buildRoot "gc-single-thread-suspend-ee-artifact.raw.o"
    $embeddedObj = Join-Path $buildRoot "gc-single-thread-suspend-ee-artifact.o"
    Invoke-LoggedCommand $objcopy @("-I","binary","-O","pe-x86-64","-B","i386:x86-64",$elfPath,$rawObj) (Join-Path $runRoot "embed-raw.log")
    $symbols = & $objdump -t $rawObj
    $startSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_start)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $endSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_end)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    $sizeSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_size)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($startSymbol) -or [string]::IsNullOrWhiteSpace($endSymbol) -or [string]::IsNullOrWhiteSpace($sizeSymbol)) { throw "Embedded symbol extraction failed." }
    Copy-Item -LiteralPath $rawObj -Destination $embeddedObj -Force
    Invoke-LoggedCommand $objcopy @("--redefine-sym","${startSymbol}=guidexos_nativeaot_gc_startup_artifact_start","--redefine-sym","${endSymbol}=guidexos_nativeaot_gc_startup_artifact_end","--redefine-sym","${sizeSymbol}=guidexos_nativeaot_gc_startup_artifact_size","--set-section-alignment",".data=4096","--rename-section",".data=.data,alloc,load,readonly,data,contents",$embeddedObj) (Join-Path $runRoot "embed-final.log")

    $extraCflags = "-DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST -DGXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST -I$artifactRoot"
    Set-Content -LiteralPath (Join-Path $runRoot "selectors.txt") -Value @("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1","NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj") -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $runRoot "extra-cflags.txt") -Value $extraCflags -Encoding ASCII
    Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","clean") (Join-Path $runRoot "kernel-preclean.log")
    Invoke-LoggedCommand $make @("-C","kernel","ARCH=amd64","GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1","NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObj","EXTRA_CFLAGS=$extraCflags") (Join-Path $runRoot "kernel-build.log")
    Require-File $kernelPath "Specialized single-thread SuspendEE kernel"
    $specializedKernelHash = Hash-File $kernelPath
    Set-Content -LiteralPath (Join-Path $runRoot "kernel-symbols.txt") -Value (& $objdump -t $kernelPath) -Encoding ASCII

    for ($runIndex = 0; $runIndex -lt 3; $runIndex++) {
        $name = if ($runIndex -eq 0) { "first-run" } else { "repeat-$runIndex" }
        $oneRoot = Join-Path $runRoot $name
        $espRoot = Join-Path $oneRoot "esp"
        $bootPath = Join-Path $espRoot "EFI\BOOT\BOOTX64.EFI"
        $serialPath = Join-Path $oneRoot "serial.log"
        New-Item -ItemType Directory -Force -Path (Split-Path $bootPath) | Out-Null
        Copy-Item -LiteralPath $bootloader -Destination $bootPath -Force
        Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $espRoot "kernel.elf") -Force
        $port = 44800 + $runIndex
        $monitorPath = Join-Path $oneRoot "watchdog-monitor.txt"
        $qemuDebugPath = Join-Path $oneRoot "qemu-debug.log"
        $qemuArgs = @("-accel","tcg,thread=single","-machine","pc","-smp","1","-drive",('if=pflash,format=raw,readonly=on,file="' + $ovmf + '"'),"-drive",('file=fat:rw:"' + $espRoot + '",format=raw,if=ide,index=0'),"-m","1024M","-vga","std","-display","none","-serial",('file:"' + $serialPath + '"'),"-monitor",("tcp:127.0.0.1:$port,server,nowait"),"-no-reboot","-no-shutdown","-rtc","base=utc,clock=host")
        if ($isFirstNonNullRoot) { $qemuArgs += @("-d","int,guest_errors","-D",$qemuDebugPath) }
        Log-Command ('"' + $qemu + '" ' + ($qemuArgs -join ' '))
        $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -WindowStyle Hidden -PassThru
        $completed = $false
        $earlyFailure = $null
        try {
            $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
            while ((Get-Date) -lt $deadline -and -not $qemuProcess.HasExited) {
                Start-Sleep -Milliseconds 250
                if (Test-Path -LiteralPath $serialPath) {
                    $liveText = Get-Content -LiteralPath $serialPath -Raw
                    $normalizedLiveText = (($liveText -replace '\[IRQ\] dispatch irq=00\s*', '') -replace '\s+', ' ') -replace '\s*=\s*', '='
                    $stopPattern = if ($isFirstNonNullRoot) {
                        'lockDepth=00000001 marker=C011EC06'
                    } elseif ($isFirstRootCandidateLoad) {
                        'lockDepth=00000001 marker=C011EC05'
                    } elseif ($isFirstPerThreadRootProvider) {
                        'lockDepth=00000001 marker=C011EC04'
                    } elseif ($isAllocationContextFixupRootBoundary) {
                        'segmentReservedAfter=0000000100B00000'
                    } else {
                        'liveSentinels=00000004'
                    }
                    if ($normalizedLiveText -match $stopPattern) { $completed = $true; break }
                    if ($isFirstNonNullRoot -and $normalizedLiveText -match '\[PageFault\] Not-present violation on read \(kernel\)') { $earlyFailure = "nativeaot-thread-static-startup-page-fault"; break }
                }
            }
            if (-not $completed -and [string]::IsNullOrWhiteSpace($earlyFailure)) {
                Read-Monitor $port $monitorPath
                $failureSerial = if (Test-Path -LiteralPath $serialPath) { Get-Content -LiteralPath $serialPath -Raw } else { "" }
                if ($isFirstNonNullRoot -and $failureSerial -match '\[PageFault\] Not-present violation on read \(kernel\)') {
                    $earlyFailure = "nativeaot-thread-static-startup-page-fault"
                } elseif ($qemuProcess.HasExited) {
                    throw "QEMU $name exited before the NativeAOT GC safe-stop marker."
                } else {
                    throw "QEMU $name timed out after $TimeoutSeconds seconds."
                }
            }
        } finally {
            if (-not $qemuProcess.HasExited) { Stop-Process -Id $qemuProcess.Id -Force }
            try { $qemuProcess.WaitForExit() } catch { }
            [ordered]@{ run=$name; timeoutSeconds=$TimeoutSeconds; safeStopObserved=$completed; earlyFailure=$earlyFailure; harnessTerminated=$true; processId=$qemuProcess.Id; serialPath=$serialPath; monitorPath=$monitorPath; qemuDebugPath=$qemuDebugPath } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $oneRoot "watchdog.json") -Encoding ASCII
        }
        Require-File $serialPath "Fresh QEMU serial log"
        $serial = Get-Content -LiteralPath $serialPath -Raw
        Set-Content -LiteralPath (Join-Path $oneRoot "serial.sha256") -Value (Hash-File $serialPath) -Encoding ASCII
        $validationText = ($serial -replace '\[IRQ\] dispatch irq=00\s*', '') -replace '\s+', ' '
        $validationText = $validationText -replace '\s*=\s*', '='
        $validationText = $validationText -replace '\s*-\s*', '-'
        if ($isFirstNonNullRoot -and -not [string]::IsNullOrWhiteSpace($earlyFailure)) {
            Assert-Text $validationText '\[nativeaot-gc-single-thread-suspend-ee\] entering ManagedMain once' "managed entry before early failure"
            Assert-Text $validationText '\[PageFault\] Not-present violation on read \(kernel\)' "NativeAOT thread-static startup page fault"
            if ($validationText -match 'C011EC06|nativeaot-gc-first-non-null-root-callback-boundary|candidateVisited=|managedAssignmentCount=') { throw "The early-failure run reached proof-root or candidate-boundary evidence unexpectedly in $name." }
            $debugText = if (Test-Path -LiteralPath $qemuDebugPath -PathType Leaf) { Get-Content -LiteralPath $qemuDebugPath -Raw } else { "" }
            $faultRipMatches = [regex]::Matches($debugText, '(?im)\bRIP=(?<value>[0-9A-Fa-f]{16})')
            $faultCr2Matches = [regex]::Matches($debugText, '(?im)\bCR2=(?<value>[0-9A-Fa-f]{16})')
            $faultRip = if ($faultRipMatches.Count -gt 0) { "0x" + $faultRipMatches[$faultRipMatches.Count - 1].Groups['value'].Value.ToUpperInvariant() } else { $null }
            $faultCr2 = if ($faultCr2Matches.Count -gt 0) { "0x" + $faultCr2Matches[$faultCr2Matches.Count - 1].Groups['value'].Value.ToUpperInvariant() } else { $null }
            $qemuDebugSha256 = if (Test-Path -LiteralPath $qemuDebugPath -PathType Leaf) { Hash-File $qemuDebugPath } else { $null }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=$null; outcome="E"; harnessTerminated=$true; earlyFailure=$earlyFailure
                managedMainEntered=$true; managedAssignmentCount="0x00000000"; managedReadbackCount="0x00000000"; candidateScanStarted=$false; callbackCount="0x00000000"; qemuDebugLog=$qemuDebugPath; qemuDebugSha256=$qemuDebugSha256; faultRip=$faultRip; faultCr2=$faultCr2
            }
            continue
        }
        if ($isFirstNonNullRoot) {
            Assert-Text $validationText '\[nativeaot-gc-first-non-null-root-callback-boundary\] SAFE_STOP marker=C011EC06' "first non-null root callback-boundary safe-stop marker"
            Assert-Text $validationText 'gcScanRootsRequest=00000001 gcScanRootsEntry=00000001 foreachRequest=00000001 foreachEntry=00000001 iteratorInit=00000001' "real root dispatcher and iterator entry"
            Assert-Text $validationText 'registeredBefore=00000001 registeredAfter=00000001 enumerated=00000001 included=00000001 excluded=00000000' "actual registered thread enumeration and inclusion"
            Assert-Text $validationText 'providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=0000000[12]' "runtime-selected thread-static provider"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed thread-static assignment and readback"
            Assert-Text $validationText 'threadStaticInitialization=0000000[23] sentinelOrdinal=00000000 sentinelAddress=[0-9A-F]{16} sentinelSize=0000000000001018 readbackAddress=[0-9A-F]{16} readbackExactMatch=00000001' "thread-static initialization and selected sentinel"
            Assert-Text $validationText 'candidateVisited=0000000[1-8] nullCandidates=0000000[01] nonNullCandidates=00000001' "bounded candidate sequence and first non-null value"
            Assert-Text $validationText 'firstNonNullSlot=[0-9A-F]{16} firstNonNullValue=[0-9A-F]{16} firstNonNullKnownAddressMatch=[0-9A-F]{8} expectedSentinelAddress=[0-9A-F]{16}' "first non-null candidate raw value"
            Assert-Text $validationText 'loadRequests=0000000[1-8] loadEntries=0000000[1-8] machineWordLoads=0000000[1-8] duplicateLoads=00000000 loadFaults=00000000' "exactly-once bounded candidate loads"
            Assert-Text $validationText 'rootFlags=00000000 rootKind=00000001 callbacks=00000000 promotions=00000000 marking=00000000' "root metadata before callback"
            Assert-Text $validationText 'candidateDereferences=00000000 heapMembershipTests=00000000 objectHeaders=00000000 methodTables=00000000 rootFlagApplications=00000000' "no semantic candidate processing"
            Assert-Text $validationText 'objectMutation=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "no mutation, restart, or managed resume"
            Assert-Text $validationText 'objectBeforeLoad=00000028 objectAfterLoad=00000028 objectAtStop=00000028' "object validation around non-null load"
            Assert-Text $validationText 'providerRequests=0000000[12] providerEntries=0000000[12] providerSkips=0000000[01]' "provider request, entry, and null-inline-root state"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 eeSuspended=00000001 lockDepth=00000001 marker=C011EC06' "fixup, suspension, and lock invariants"
            $currentIdentity = Get-MarkerField $validationText 'current'
            $enumeratedIdentity = Get-MarkerField $validationText 'enumeratedThread'
            $initiatorIdentity = Get-MarkerField $validationText 'initiator'
            $lockOwnerIdentity = Get-MarkerField $validationText 'lockOwner'
            $candidateSlotIdentity = Get-MarkerField $validationText 'firstNonNullSlot'
            $candidateValueIdentity = Get-MarkerField $validationText 'firstNonNullValue'
            $sentinelIdentity = Get-MarkerField $validationText 'sentinelAddress'
            $readbackIdentity = Get-MarkerField $validationText 'readbackAddress'
            $expectedSentinelIdentity = Get-MarkerField $validationText 'expectedSentinelAddress'
            if ([string]::IsNullOrWhiteSpace($currentIdentity) -or $currentIdentity -ne $enumeratedIdentity -or $currentIdentity -ne $initiatorIdentity -or $currentIdentity -ne $lockOwnerIdentity) { throw "Thread identity mismatch in $name." }
            if ([string]::IsNullOrWhiteSpace($sentinelIdentity) -or $sentinelIdentity -ne $readbackIdentity -or $sentinelIdentity -ne $expectedSentinelIdentity) { throw "Managed thread-static readback did not match the selected sentinel in $name." }
            if ([string]::IsNullOrWhiteSpace($candidateSlotIdentity) -or [string]::IsNullOrWhiteSpace($candidateValueIdentity) -or $candidateValueIdentity -eq '0x0000000000000000') { throw "Non-null candidate identity was not captured in $name." }
            $candidateLineMatch = [regex]::Match($validationText, 'CANDIDATE ordinal=(?<ordinal>[0-9A-F]+) slot=(?<slot>[0-9A-F]{16}) rawValue=(?<raw>[0-9A-F]{16}) loadCount=(?<loads>[0-9A-F]+) duplicateLoads=(?<duplicates>[0-9A-F]+) null=(?<null>[0-9A-F]+) knownAddressMatch=(?<known>[0-9A-F]+) exactSelectedSentinelMatch=(?<exact>[0-9A-F]+) callback=(?<callback>[0-9A-F]{16}) scanContext=(?<scanContext>[0-9A-F]{16})')
            if (-not $candidateLineMatch.Success) { throw "Candidate record was not captured in $name." }
            if ($candidateLineMatch.Groups['slot'].Value -ne $candidateSlotIdentity.Substring(2) -or $candidateLineMatch.Groups['raw'].Value -ne $candidateValueIdentity.Substring(2)) { throw "Candidate record does not match the safe-stop candidate in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC06"; harnessTerminated=$true
                allocations=(Get-MarkerField $validationText 'objectAtStop'); gcScanRootsRequests=(Get-MarkerField $validationText 'gcScanRootsRequest'); gcScanRootsEntries=(Get-MarkerField $validationText 'gcScanRootsEntry'); foreachThreadEntries=(Get-MarkerField $validationText 'foreachEntry'); iteratorInitializations=(Get-MarkerField $validationText 'iteratorInit'); registeredThreads=(Get-MarkerField $validationText 'registeredBefore'); enumeratedThreads=(Get-MarkerField $validationText 'enumerated'); includedThreads=(Get-MarkerField $validationText 'included'); excludedThreads=(Get-MarkerField $validationText 'excluded')
                threadRecord=[ordered]@{ ordinal=1; nativeThread=$enumeratedIdentity; nativeThreadId=(Get-MarkerField $validationText 'nativeId'); current=$currentIdentity; initiator=$initiatorIdentity; lockOwner=$lockOwnerIdentity; lifecycle=(Get-MarkerField $validationText 'lifecycle'); stateFlags=(Get-MarkerField $validationText 'stateFlags'); cooperative=(Get-MarkerField $validationText 'cooperative'); preemptive=(Get-MarkerField $validationText 'preemptive') }
                managedProofRoot=[ordered]@{ assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount'); clearCount=(Get-MarkerField $validationText 'managedClearCount'); readbackCount=(Get-MarkerField $validationText 'managedReadbackCount'); assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid'); readbackValid=(Get-MarkerField $validationText 'managedReadbackValid'); initialization=(Get-MarkerField $validationText 'threadStaticInitialization'); sentinelOrdinal=(Get-MarkerField $validationText 'sentinelOrdinal'); sentinelAddress=$sentinelIdentity; sentinelSize=(Get-MarkerField $validationText 'sentinelSize'); readbackAddress=$readbackIdentity; readbackExactMatch=(Get-MarkerField $validationText 'readbackExactMatch'); managedThread=(Get-MarkerField $validationText 'managedThread') }
                provider=[ordered]@{ source="thread-static-provider"; runtime="thread-static-provider"; function=(Get-MarkerField $validationText 'providerFunction'); requests=(Get-MarkerField $validationText 'providerRequests'); entries=(Get-MarkerField $validationText 'providerEntries'); skips=(Get-MarkerField $validationText 'providerSkips'); metadataContainer=(Get-MarkerField $validationText 'metadataContainer'); storageAddress=(Get-MarkerField $validationText 'storageAddress') }
                candidateSlots=@([ordered]@{ ordinal=("0x"+$candidateLineMatch.Groups['ordinal'].Value); slot=("0x"+$candidateLineMatch.Groups['slot'].Value); rawValue=("0x"+$candidateLineMatch.Groups['raw'].Value); loadCount=("0x"+$candidateLineMatch.Groups['loads'].Value); duplicateLoads=("0x"+$candidateLineMatch.Groups['duplicates'].Value); null=("0x"+$candidateLineMatch.Groups['null'].Value); knownAddressMatch=("0x"+$candidateLineMatch.Groups['known'].Value); exactSelectedSentinelMatch=("0x"+$candidateLineMatch.Groups['exact'].Value); callback=("0x"+$candidateLineMatch.Groups['callback'].Value); scanContext=("0x"+$candidateLineMatch.Groups['scanContext'].Value) })
                candidateVisited=(Get-MarkerField $validationText 'candidateVisited'); nullCandidates=(Get-MarkerField $validationText 'nullCandidates'); nonNullCandidates=(Get-MarkerField $validationText 'nonNullCandidates'); firstNonNullSlot=$candidateSlotIdentity; firstNonNullValue=$candidateValueIdentity; firstNonNullKnownAddressMatch=(Get-MarkerField $validationText 'firstNonNullKnownAddressMatch'); candidateMatchesProofRoot=(Get-MarkerField $validationText 'candidateMatchesProofRoot'); candidateProofRootObserved=(Get-MarkerField $validationText 'proofRootObserved'); loadRequests=(Get-MarkerField $validationText 'loadRequests'); loadEntries=(Get-MarkerField $validationText 'loadEntries'); machineWordLoads=(Get-MarkerField $validationText 'machineWordLoads'); duplicateLoads=(Get-MarkerField $validationText 'duplicateLoads'); loadFaults=(Get-MarkerField $validationText 'loadFaults'); callback=(Get-MarkerField $validationText 'callback'); scanContext=(Get-MarkerField $validationText 'scanContext'); rootFlags=(Get-MarkerField $validationText 'rootFlags'); rootKind=(Get-MarkerField $validationText 'rootKind')
                candidateDereferences=(Get-MarkerField $validationText 'candidateDereferences'); heapMembershipTests=(Get-MarkerField $validationText 'heapMembershipTests'); objectHeaders=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); rootFlagApplications=(Get-MarkerField $validationText 'rootFlagApplications'); callbacks=(Get-MarkerField $validationText 'callbacks'); promotions=(Get-MarkerField $validationText 'promotions'); marking=(Get-MarkerField $validationText 'marking'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume'); objectBeforeLoad=(Get-MarkerField $validationText 'objectBeforeLoad'); objectAfterLoad=(Get-MarkerField $validationText 'objectAfterLoad'); objectAtStop=(Get-MarkerField $validationText 'objectAtStop')
            }
        } elseif ($isFirstRootCandidateLoad) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-candidate-load\] SAFE_STOP marker=C011EC05' "first root candidate-load safe-stop marker"
            Assert-Text $validationText 'gcScanRootsRequest=00000001 gcScanRootsEntry=00000001 foreachRequest=00000001 foreachEntry=00000001 iteratorInit=00000001' "real root dispatcher and iterator entry"
            Assert-Text $validationText 'registeredBefore=00000001 registeredAfter=00000001 enumerated=00000001 included=00000001 excluded=00000000' "actual registered thread enumeration and inclusion"
            Assert-Text $validationText 'providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=00000002' "runtime-selected thread-static provider"
            Assert-Text $validationText 'providerRequests=00000002 providerEntries=00000001 providerSkips=00000001 metadataContainers=00000001' "provider request, entry, skip, and metadata counts"
            Assert-Text $validationText 'slotWidth=00000008 slotAlignment=00000000 slotMapped=00000001 slotCommitted=00000001 slotWritableContract=00000001 slotStable=00000001 slotExpectedThreadStorage=00000001' "real aligned mapped candidate slot"
            Assert-Text $validationText 'slotOverlapHeap=00000000 slotOverlapThread=00000001 slotOverlapAllocContext=00000000 slotOverlapStack=00000000 slotOverlapOther=00000000' "candidate slot region separation"
            Assert-Text $validationText 'loadRequests=00000001 loadEntries=00000001 machineWordLoads=00000001 duplicateLoads=00000000 loadFaults=00000000' "exactly one successful machine-word load"
            Assert-Text $validationText 'loadAddress=[0-9A-F]{16} rawValue=[0-9A-F]{16} valueIsNull=(?:00000000|00000001) knownAddressMatch=[0-9A-F]{8}' "saved opaque raw candidate value"
            Assert-Text $validationText 'candidateDereferences=00000000 heapMembershipTests=00000000 objectHeaders=00000000 methodTables=00000000 rootFlagApplications=00000000' "no semantic candidate processing"
            Assert-Text $validationText 'candidates=00000000 callbacks=00000000 promotions=00000000 marking=00000000 objectMutation=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "no callbacks, marking, mutation, restart, or resume"
            Assert-Text $validationText 'objectBeforeLoad=00000028 objectAfterLoad=00000028 objectAtStop=00000028 sentinelChecks=000000A0' "object and sentinel validation before and after load"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 eeSuspended=00000001 lockDepth=00000001' "fixup, suspension, and lock invariants"
            $currentIdentity = Get-MarkerField $validationText 'current'
            $enumeratedIdentity = Get-MarkerField $validationText 'enumeratedThread'
            $initiatorIdentity = Get-MarkerField $validationText 'initiator'
            $lockOwnerIdentity = Get-MarkerField $validationText 'lockOwner'
            $candidateSlotIdentity = Get-MarkerField $validationText 'candidateSlot'
            $loadAddressIdentity = Get-MarkerField $validationText 'loadAddress'
            if ([string]::IsNullOrWhiteSpace($currentIdentity) -or $currentIdentity -ne $enumeratedIdentity -or $currentIdentity -ne $initiatorIdentity -or $currentIdentity -ne $lockOwnerIdentity) { throw "Thread identity mismatch in $name." }
            if ([string]::IsNullOrWhiteSpace($candidateSlotIdentity) -or $candidateSlotIdentity -ne $loadAddressIdentity) { throw "Candidate slot/load-address mismatch in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC05"; harnessTerminated=$true
                allocations=(Get-MarkerField $validationText 'objectAtStop'); gcScanRootsRequests=(Get-MarkerField $validationText 'gcScanRootsRequest'); gcScanRootsEntries=(Get-MarkerField $validationText 'gcScanRootsEntry'); foreachThreadEntries=(Get-MarkerField $validationText 'foreachEntry'); iteratorInitializations=(Get-MarkerField $validationText 'iteratorInit'); registeredThreads=(Get-MarkerField $validationText 'registeredBefore'); enumeratedThreads=(Get-MarkerField $validationText 'enumerated'); includedThreads=(Get-MarkerField $validationText 'included'); excludedThreads=(Get-MarkerField $validationText 'excluded')
                threadRecord=[ordered]@{ ordinal=1; nativeThread=$enumeratedIdentity; nativeThreadId=(Get-MarkerField $validationText 'nativeId'); current=$currentIdentity; initiator=$initiatorIdentity; lockOwner=$lockOwnerIdentity; lifecycle=(Get-MarkerField $validationText 'lifecycle'); stateFlags=(Get-MarkerField $validationText 'stateFlags'); cooperative=(Get-MarkerField $validationText 'cooperative'); preemptive=(Get-MarkerField $validationText 'preemptive'); allocationContext=(Get-MarkerField $validationText 'allocContext'); stackLow=(Get-MarkerField $validationText 'stackLow'); stackHigh=(Get-MarkerField $validationText 'stackHigh') }
                provider=[ordered]@{ source="thread-static-provider"; runtime="thread-static-provider"; function="Thread::GetThreadStaticStorage"; requests=(Get-MarkerField $validationText 'providerRequests'); entries=(Get-MarkerField $validationText 'providerEntries'); skips=(Get-MarkerField $validationText 'providerSkips'); metadataContainers=(Get-MarkerField $validationText 'metadataContainers'); metadataContainer=(Get-MarkerField $validationText 'metadataContainer') }
                candidateSlot=$candidateSlotIdentity; slotOffset=(Get-MarkerField $validationText 'slotOffset'); slotWidth=(Get-MarkerField $validationText 'slotWidth'); slotAlignment=(Get-MarkerField $validationText 'slotAlignment'); slotMapped=(Get-MarkerField $validationText 'slotMapped'); slotCommitted=(Get-MarkerField $validationText 'slotCommitted'); slotWritableContract=(Get-MarkerField $validationText 'slotWritableContract'); slotStable=(Get-MarkerField $validationText 'slotStable'); slotExpectedThreadStorage=(Get-MarkerField $validationText 'slotExpectedThreadStorage')
                rootFlags=(Get-MarkerField $validationText 'rootFlags'); rootKind=(Get-MarkerField $validationText 'rootKind'); callback=(Get-MarkerField $validationText 'callback'); scanContext=(Get-MarkerField $validationText 'scanContext'); loadRequests=(Get-MarkerField $validationText 'loadRequests'); loadEntries=(Get-MarkerField $validationText 'loadEntries'); machineWordLoads=(Get-MarkerField $validationText 'machineWordLoads'); duplicateLoads=(Get-MarkerField $validationText 'duplicateLoads'); loadFaults=(Get-MarkerField $validationText 'loadFaults'); loadAddress=$loadAddressIdentity; rawValue=(Get-MarkerField $validationText 'rawValue'); valueIsNull=(Get-MarkerField $validationText 'valueIsNull'); knownAddressMatch=(Get-MarkerField $validationText 'knownAddressMatch')
                candidateDereferences=(Get-MarkerField $validationText 'candidateDereferences'); heapMembershipTests=(Get-MarkerField $validationText 'heapMembershipTests'); objectHeaders=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); rootFlagApplications=(Get-MarkerField $validationText 'rootFlagApplications'); candidates=(Get-MarkerField $validationText 'candidates'); callbacks=(Get-MarkerField $validationText 'callbacks'); promotions=(Get-MarkerField $validationText 'promotions'); marking=(Get-MarkerField $validationText 'marking'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume'); objectBeforeLoad=(Get-MarkerField $validationText 'objectBeforeLoad'); objectAfterLoad=(Get-MarkerField $validationText 'objectAfterLoad'); objectAtStop=(Get-MarkerField $validationText 'objectAtStop'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks')
            }
        } elseif ($isFirstPerThreadRootProvider) {
            Assert-Text $validationText '\[nativeaot-gc-first-per-thread-root-provider\] SAFE_STOP marker=C011EC04' "first per-thread provider safe-stop marker"
            Assert-Text $validationText 'gcScanRootsRequest=00000001 gcScanRootsEntry=00000001 foreachRequest=00000001 foreachEntry=00000001 iteratorInit=00000001' "real root dispatcher and iterator entry"
            Assert-Text $validationText 'registeredBefore=00000001 registeredAfter=00000001 enumerated=00000001 included=00000001 excluded=00000000' "actual registered thread enumeration and inclusion"
            Assert-Text $validationText 'listIntegrityFailures=00000000 duplicates=00000000 registryMutationBefore=00000000 registryMutationAfter=00000000' "thread-list integrity and closed-world mutation state"
            Assert-Text $validationText 'providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=00000002' "runtime-selected thread-static provider"
            Assert-Text $validationText 'providerRequests=00000002 providerEntries=00000001 providerSkips=00000001 metadataContainers=00000001' "provider request, entry, skip, and metadata counts"
            Assert-Text $validationText 'candidateMetadata=00000001 candidateReads=00000000 candidates=00000000 callbacks=00000000 promotions=00000000 marking=00000000' "candidate boundary before value access"
            Assert-Text $validationText 'objectMutation=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "no mutation, restart, or managed resume"
            Assert-Text $validationText 'stackBoundsRequested=00000000 stackScanning=00000000 threadStaticRequested=00000001 threadStaticScanning=00000000' "provider boundary before stack or static candidate scanning"
            Assert-Text $validationText 'sentinelChecks=000000A0 objectBefore=00000028 objectAfter=00000028' "sentinel and object validation at provider boundary"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 eeSuspended=00000001 lockDepth=00000001' "fixup, suspension, and lock invariants"
            $currentIdentity = Get-MarkerField $validationText 'current'
            $enumeratedIdentity = Get-MarkerField $validationText 'enumeratedThread'
            $initiatorIdentity = Get-MarkerField $validationText 'initiator'
            $lockOwnerIdentity = Get-MarkerField $validationText 'lockOwner'
            if ([string]::IsNullOrWhiteSpace($currentIdentity) -or $currentIdentity -ne $enumeratedIdentity -or $currentIdentity -ne $initiatorIdentity -or $currentIdentity -ne $lockOwnerIdentity) { throw "Thread identity mismatch in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC04"; harnessTerminated=$true
                allocations=(Get-MarkerField $validationText 'objectAfter'); gcScanRootsRequests=(Get-MarkerField $validationText 'gcScanRootsRequest'); gcScanRootsEntries=(Get-MarkerField $validationText 'gcScanRootsEntry'); foreachThreadEntries=(Get-MarkerField $validationText 'foreachEntry'); iteratorInitializations=(Get-MarkerField $validationText 'iteratorInit'); registeredThreads=(Get-MarkerField $validationText 'registeredBefore'); enumeratedThreads=(Get-MarkerField $validationText 'enumerated'); includedThreads=(Get-MarkerField $validationText 'included'); excludedThreads=(Get-MarkerField $validationText 'excluded')
                threadRecord=[ordered]@{ ordinal=1; nativeThread=$enumeratedIdentity; nativeThreadId=(Get-MarkerField $validationText 'nativeId'); current=$currentIdentity; initiator=$initiatorIdentity; lockOwner=$lockOwnerIdentity; lifecycle=(Get-MarkerField $validationText 'lifecycle'); stateFlags=(Get-MarkerField $validationText 'stateFlags'); cooperative=(Get-MarkerField $validationText 'cooperative'); preemptive=(Get-MarkerField $validationText 'preemptive'); allocationContext=(Get-MarkerField $validationText 'allocContext'); stackLow=(Get-MarkerField $validationText 'stackLow'); stackHigh=(Get-MarkerField $validationText 'stackHigh'); listHeadBefore=(Get-MarkerField $validationText 'listHeadBefore'); listTailBefore=(Get-MarkerField $validationText 'listTailBefore'); listHeadAfter=(Get-MarkerField $validationText 'listHeadAfter'); listTailAfter=(Get-MarkerField $validationText 'listTailAfter'); listIntegrityFailures=(Get-MarkerField $validationText 'listIntegrityFailures'); duplicates=(Get-MarkerField $validationText 'duplicates') }
                providerSource="thread-static-provider"; providerRuntime="thread-static-provider"; providerFunction="Thread::GetThreadStaticStorage"; providerRequests=(Get-MarkerField $validationText 'providerRequests'); providerEntries=(Get-MarkerField $validationText 'providerEntries'); providerSkips=(Get-MarkerField $validationText 'providerSkips'); metadataContainers=(Get-MarkerField $validationText 'metadataContainers'); firstMetadata=(Get-MarkerField $validationText 'firstMetadata'); candidateMetadata=(Get-MarkerField $validationText 'candidateMetadata'); candidateReads=(Get-MarkerField $validationText 'candidateReads'); candidates=(Get-MarkerField $validationText 'candidates'); callbacks=(Get-MarkerField $validationText 'callbacks'); promotions=(Get-MarkerField $validationText 'promotions'); marking=(Get-MarkerField $validationText 'marking'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume'); stackBoundsRequested=(Get-MarkerField $validationText 'stackBoundsRequested'); stackScanning=(Get-MarkerField $validationText 'stackScanning'); threadStaticRequested=(Get-MarkerField $validationText 'threadStaticRequested'); threadStaticScanning=(Get-MarkerField $validationText 'threadStaticScanning'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks'); objectBefore=(Get-MarkerField $validationText 'objectBefore'); objectAfter=(Get-MarkerField $validationText 'objectAfter')
            }
        } elseif ($isAllocationContextFixupRootBoundary) {
            Assert-Text $validationText '\[nativeaot-gc-allocation-context-fixup-root-boundary\] SAFE_STOP marker=C011EC03' "unique root-boundary safe-stop marker"
            Assert-Text $validationText 'callback=GCToEEInterface::GcScanRoots entry before FOREACH_THREAD' "root dispatcher entry before thread iteration"
            Assert-Text $validationText 'fixupRequest=00000001 fixupEntry=00000001 fixupComplete=00000001' "real fix_allocation_contexts request, enumeration, and completion"
            Assert-Text $validationText 'contextsVisited=00000001 contextsChanged=00000001 contextsCleared=00000001' "one real context changed and was cleared"
            Assert-Text $validationText 'objectBefore=00000028 objectAfter=00000028' "bounded object history validation"
            Assert-Text $validationText 'rootDispatcher=00000001 rootProviders=00000000 rootCandidates=00000000 callbacks=00000000 marking=00000000' "root boundary before providers and marking"
            Assert-Text $validationText 'metadataMutation=00000001 objectMutation=00000000 restartResume=00000000' "metadata-only fixup mutation and no restart"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 objectFailuresBefore=00000000 objectFailuresAfter=00000000' "zero fixup, root, and object-validation failures"
            Assert-Text $validationText 'boundaryFailures=00000000 patternFailures=00000000 addressChanges=00000000' "zero object boundary, pattern, and address-change failures"
            Assert-Text $validationText 'sentinelChecks=000000A0' "four live sentinels checked before root dispatch"
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC03"; harnessTerminated=$true
                allocations=(Get-MarkerField $validationText 'objectAfter'); contextFixupRequests=(Get-MarkerField $validationText 'fixupRequest'); contextFixupEntries=(Get-MarkerField $validationText 'fixupEntry'); contextFixupCompletions=(Get-MarkerField $validationText 'fixupComplete'); contextsVisited=(Get-MarkerField $validationText 'contextsVisited'); contextsChanged=(Get-MarkerField $validationText 'contextsChanged'); contextsCleared=(Get-MarkerField $validationText 'contextsCleared')
                objectValidationBefore=(Get-MarkerField $validationText 'objectBefore'); objectValidationAfter=(Get-MarkerField $validationText 'objectAfter'); rootDispatcherEntries=(Get-MarkerField $validationText 'rootDispatcher'); rootProviderEntries=(Get-MarkerField $validationText 'rootProviders'); rootCandidates=(Get-MarkerField $validationText 'rootCandidates'); rootCallbacks=(Get-MarkerField $validationText 'callbacks'); markingEntries=(Get-MarkerField $validationText 'marking'); metadataMutation=(Get-MarkerField $validationText 'metadataMutation'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartResume=(Get-MarkerField $validationText 'restartResume')
                fixupMode=(Get-MarkerField $validationText 'fixupMode'); enumerationComplete=(Get-MarkerField $validationText 'enumerationComplete'); activeBefore=(Get-MarkerField $validationText 'activeBefore'); activeAfter=(Get-MarkerField $validationText 'activeAfter'); retired=(Get-MarkerField $validationText 'retired'); metadataComplete=(Get-MarkerField $validationText 'metadataComplete'); segmentBookkeeping=(Get-MarkerField $validationText 'segmentBookkeeping'); fixupFailures=(Get-MarkerField $validationText 'fixupFailures'); rootFailures=(Get-MarkerField $validationText 'rootFailures'); objectFailuresBefore=(Get-MarkerField $validationText 'objectFailuresBefore'); objectFailuresAfter=(Get-MarkerField $validationText 'objectFailuresAfter'); boundaryFailures=(Get-MarkerField $validationText 'boundaryFailures'); patternFailures=(Get-MarkerField $validationText 'patternFailures'); addressChanges=(Get-MarkerField $validationText 'addressChanges'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks')
                allocationPointerBefore=(Get-MarkerField $validationText 'allocPtrBefore'); allocationLimitBefore=(Get-MarkerField $validationText 'allocLimitBefore'); allocationPointerAfter=(Get-MarkerField $validationText 'allocPtrAfter'); allocationLimitAfter=(Get-MarkerField $validationText 'allocLimitAfter'); validExtentBefore=(Get-MarkerField $validationText 'validExtentBefore'); validExtentAfter=(Get-MarkerField $validationText 'validExtentAfter'); unusedTailBefore=(Get-MarkerField $validationText 'unusedTailBefore'); unusedTailAfter=(Get-MarkerField $validationText 'unusedTailAfter'); heapCounterBefore=(Get-MarkerField $validationText 'heapCounterBefore'); heapCounterAfter=(Get-MarkerField $validationText 'heapCounterAfter'); segmentAllocatedBefore=(Get-MarkerField $validationText 'segmentAllocatedBefore'); segmentAllocatedAfter=(Get-MarkerField $validationText 'segmentAllocatedAfter'); segmentCommittedBefore=(Get-MarkerField $validationText 'segmentCommittedBefore'); segmentCommittedAfter=(Get-MarkerField $validationText 'segmentCommittedAfter'); segmentReservedBefore=(Get-MarkerField $validationText 'segmentReservedBefore'); segmentReservedAfter=(Get-MarkerField $validationText 'segmentReservedAfter')
            }
        } else {
            Assert-Text $validationText '\[nativeaot-gc-single-thread-suspend-ee\] SAFE_STOP marker=C011EC02' "unique safe-stop marker"
        Assert-Text $validationText 'callback=GCHeap::GarbageCollect before fix_allocation_contexts' "safe boundary after EE mode restoration"
        Assert-Text $validationText 'requestCount=00000001 entryCount=00000001' "collection request and entry"
        Assert-Text $validationText 'requestedGeneration=00000001 reason=00000005' "generation and reason"
        Assert-Text $validationText 'suspendReason=00000001' "SuspendEE reason"
        Assert-Text $validationText 'suspendEeEntryCount=00000001 suspendEeReturnCount=00000001 suspendEeSuspensionCount=00000001' "SuspendEE entry, suspension, and return"
        Assert-Text $validationText 'lockRequests=00000001 lockAcquisitions=00000001 lockFailures=00000000 unlocks=00000000' "real thread-store lock state"
        Assert-Text $validationText 'lockDepth=00000001 registeredThreads=00000001' "single registered mutator"
        Assert-Text $validationText 'identitiesMatch=00000001 expectedOtherMutators=00000000 stoppedOtherMutators=00000000' "initiator and peer state"
        Assert-Text $validationText 'currentThreadExempt=00000001 managedEntryProhibited=00000001 eeSuspended=00000001' "suspended EE state"
        Assert-Text $validationText 'nextBoundary=00000002 rootRequests=00000000 rootEntries=00000000 stackWalkRequests=00000000 stackWalkEntries=00000000 handleScanRequests=00000000 handleScanEntries=00000000' "pre-root boundary"
        Assert-Text $validationText 'heapMutationStarted=00000000 restartRequests=00000000 restartEntries=00000000 managedResumeCount=00000000' "no heap mutation or restart"
        Assert-Text $validationText 'registryMutationAttemptsWhileLocked=00000000 adapterRegistrations=00000001' "closed-world registry invariant and adapter"
        Assert-Text $validationText 'allocations=00000028 fast=00000013 rare=00000016 refills=00000015 sameSegmentCommits=00000002 segmentTransitions=00000000' "preserved allocation counts"
        Assert-Text $validationText 'sentinelFailures=00000000 liveSentinels=00000004' "live sentinel validation"
        if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
        $runResults += [ordered]@{
            name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC02"; harnessTerminated=$true
            allocations=(Get-MarkerField $validationText 'allocations'); fast=(Get-MarkerField $validationText 'fast'); rare=(Get-MarkerField $validationText 'rare')
            refills=(Get-MarkerField $validationText 'refills'); sameSegmentCommits=(Get-MarkerField $validationText 'sameSegmentCommits'); segmentTransitions=(Get-MarkerField $validationText 'segmentTransitions')
            collectionRequests=(Get-MarkerField $validationText 'requestCount'); collectionEntries=(Get-MarkerField $validationText 'entryCount'); suspendEeEntries=(Get-MarkerField $validationText 'suspendEeEntryCount'); suspendEeReturns=(Get-MarkerField $validationText 'suspendEeReturnCount')
            lockRequests=(Get-MarkerField $validationText 'lockRequests'); lockAcquisitions=(Get-MarkerField $validationText 'lockAcquisitions'); registeredThreads=(Get-MarkerField $validationText 'registeredThreads'); adapterRegistrations=(Get-MarkerField $validationText 'adapterRegistrations'); expectedOtherMutators=(Get-MarkerField $validationText 'expectedOtherMutators'); stoppedOtherMutators=(Get-MarkerField $validationText 'stoppedOtherMutators')
            rootEntries=(Get-MarkerField $validationText 'rootEntries'); heapMutationStarted=(Get-MarkerField $validationText 'heapMutationStarted'); restartEntries=(Get-MarkerField $validationText 'restartEntries')
        }
        }
    }

    $identity = Get-Content -LiteralPath $identityManifestPath -Raw | ConvertFrom-Json
    $replacementHashes = @($identity.replacementObjects | ForEach-Object { [ordered]@{ path=$_.path; sha256=$_.sha256 } })
    if ($isFirstNonNullRoot) {
        if (@($runResults | Where-Object { $_.outcome -eq "E" }).Count -ne 3) { throw "The first non-null root callback-boundary experiment did not produce three classified early-failure runs." }
        $firstEarlyRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="E / NativeAOT real [ThreadStatic] initialization faulted before managed assignment completed; root-provider candidate scan not reached"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$repoHead; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); generatedInjectedGcenvEeSource=$gcEnvEeSource; generatedInjectedGcEnumSource=$gcEnumSource; activePalArchiveSha256=(Hash-File $activeArchive) }
            priorCheckpoint=[ordered]@{ marker="C011EC05"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md"; stop="one real candidate-slot machine-word load, before callback and semantic processing" }
            managedProofRoot=[ordered]@{ field="HostLogProof.Program.s_gcProofThreadRoot"; attribute="[ThreadStatic]"; assignmentCount="0x00000000"; readbackCount="0x00000000"; assignmentValid="0x00000000"; readbackValid="0x00000000"; initializationIndicator="0x00000000"; selectedSentinel="not assigned before fault"; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ managedSemantics="NativeAOT normal ThreadStatic slow path"; threadStaticBase="nativeaot/Runtime/thread.cpp:1251-1261"; inlineThreadStaticRoot="nativeaot/Runtime/thread.h:76-84"; rootDispatcher="nativeaot/Runtime/gcenv.ee.cpp:94-133"; enumGcRef="nativeaot/Runtime/GcEnum.cpp:68-96"; injectedCandidateLoad=$gcEnumSource; faultFunction="S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow" }
            failure=[ordered]@{ classification="nativeaot-thread-static-startup-page-fault"; firstManagedEntry=$true; serialEvidence="[PageFault] Not-present violation on read (kernel) at 0xFFFB5FF9"; faultRip=$firstEarlyRun.faultRip; faultCr2=$firstEarlyRun.faultCr2; faultFunction="S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow"; safeStopMarker="not reached"; rootProviderEntered=$false; candidateLoadRequested=0; candidateMachineWordLoaded=0; callbacks=0; promotions=0; marking=0; objectMutation=0; restartRequests=0; restartEntries=0; managedResume=0; interpretation="normal NativeAOT thread-static initialization failed before the proof field could be assigned; this is not a fabricated runtime root or candidate result" }
            candidateBoundary=[ordered]@{ providerSource="not reached"; candidateVisited=0; nullCandidates=0; nonNullCandidates=0; firstNonNullValue="not observed"; exactSelectedSentinelMatch="not observed"; callback=0; scanContext="not observed"; candidateDereferences=0; heapMembershipTests=0; objectHeaders=0; methodTables=0; rootFlagApplications=0; safeStopMarker="C011EC06 not reached" }
            sentinelAndObjects=[ordered]@{ workloadSentinels=4; managedSentinelAssignment=$false; objectValidationAtManagedEntry="not applicable before assignment"; fabricatedSentinel=$false; fabricatedCandidate=$false }
            proofKernelSha256=$specializedKernelHash; qemuVersion=$qemuVersion; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot; runs=$runResults
            regressions=[ordered]@{ firstNonNullRootCallbackBoundary="OUTCOME E 3/3 fresh QEMU runs; same early NativeAOT thread-static page-fault class"; firstRootCandidateLoad="historical PASS C011EC05 retained; not overwritten"; firstPerThreadRootProvider="historical PASS C011EC04 retained"; allocationContextFixupRootBoundary="historical PASS C011EC03 retained"; singleThreadSuspendEe="historical PASS C011EC02 retained"; staticChecks="PASS script parse, manifest parse, QEMU serial classification, ordinary-kernel restoration" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ firstNonNullRootCallbackBoundary="blocked at legitimate managed ThreadStatic initialization before root-provider callback boundary"; candidateAbiCharacterization="not reached because no real candidate was loaded"; broadRegressionSuite="not rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md"); evidenceRoot=$runRoot
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash; authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-non-null-root-callback-boundary experiment: OUTCOME E (thread-static initialization failed before root scan)" -ForegroundColor Yellow
    } elseif ($isFirstRootCandidateLoad) {
        $firstCandidateRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / first candidate value loaded exactly once; stopped before semantic processing"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$repoHead; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); generatedInjectedGcenvEeSource=$gcEnvEeSource; generatedInjectedGcEnumSource=$gcEnumSource; activePalArchiveSha256=(Hash-File $activeArchive) }
            priorCheckpoint=[ordered]@{ marker="C011EC04"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md"; stop="Thread::GetThreadStaticStorage returned the real slot address before EnumGcRef" }
            sourceTrace=[ordered]@{ enumGcRefHeader="nativeaot/Runtime/GcEnum.h:13"; enumGcRefDefinition="nativeaot/Runtime/GcEnum.cpp:84-96"; gcEnumObject="nativeaot/Runtime/GcEnum.cpp:68-82"; firstProvider="nativeaot/Runtime/gcenv.ee.cpp:114-115"; threadStaticStorage="nativeaot/Runtime/thread.cpp:1251-1254"; scanFunc="nativeaot/Runtime/forward_declarations.h:43-45"; proofLoadStatement="const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);"; proofLoadSource=$gcEnumSource }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            collection=[ordered]@{ requests=1; entries=1; generation=1; reason="reason_oos_soh (5)"; blocking=$true; compacting=$false }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; lockHeld=$true; lockDepth=1; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            allocationContextFixup=[ordered]@{ requestCount=1; entryCount=1; completionCount=1; contextsVisited=1; contextsChanged=1; contextsCleared=1; objectMemoryMutation=0; objectValidationBefore=40; objectValidationAfter=40 }
            rootDispatcher=[ordered]@{ gcScanRootsRequests=1; gcScanRootsEntries=1; foreachThreadRequests=1; foreachThreadEntries=1; iteratorInitializations=1; sourceOrderCategory="thread-static-provider"; runtimeSelectedCategory="thread-static-provider" }
            threadEnumeration=[ordered]@{ registeredBefore=1; registeredAfter=1; enumerated=1; included=1; excluded=0; duplicates=0; integrityFailures=0; registryMutationBefore=0; registryMutationAfter=0; record=$firstCandidateRun.threadRecord; records=$runResults }
            provider=$firstCandidateRun.provider
            candidateSlot=[ordered]@{ address=$firstCandidateRun.candidateSlot; metadataContainer=$firstCandidateRun.provider.metadataContainer; ownerThread=$firstCandidateRun.threadRecord.nativeThread; providerFunction="Thread::GetThreadStaticStorage"; width=$firstCandidateRun.slotWidth; alignment=$firstCandidateRun.slotAlignment; offsetFromThread=$firstCandidateRun.slotOffset; mapped=$firstCandidateRun.slotMapped; committed=$firstCandidateRun.slotCommitted; writableContract=$firstCandidateRun.slotWritableContract; stable=$firstCandidateRun.slotStable; expectedThreadStorage=$firstCandidateRun.slotExpectedThreadStorage; overlapManagedHeap="0x00000000"; overlapRuntimeThread="0x00000001"; overlapAllocationContext="0x00000000"; overlapNativeStack="0x00000000"; overlapOtherKnownRegion="0x00000000" }
            candidateLoad=[ordered]@{ requests=$firstCandidateRun.loadRequests; entries=$firstCandidateRun.loadEntries; machineWordLoads=$firstCandidateRun.machineWordLoads; duplicateLoads=$firstCandidateRun.duplicateLoads; loadFaults=$firstCandidateRun.loadFaults; address=$firstCandidateRun.loadAddress; width=$firstCandidateRun.slotWidth; rawValue=$firstCandidateRun.rawValue; valueIsNull=$firstCandidateRun.valueIsNull; knownAddressMatch=$firstCandidateRun.knownAddressMatch; rootFlags=$firstCandidateRun.rootFlags; rootKind=$firstCandidateRun.rootKind; callback=$firstCandidateRun.callback; scanContext=$firstCandidateRun.scanContext }
            prohibitedProcessing=[ordered]@{ candidatePointeeDereferences=$firstCandidateRun.candidateDereferences; heapMembershipTests=$firstCandidateRun.heapMembershipTests; objectHeaderInspections=$firstCandidateRun.objectHeaders; methodTableInspections=$firstCandidateRun.methodTables; rootFlagApplications=$firstCandidateRun.rootFlagApplications; rootCandidates=$firstCandidateRun.candidates; rootCallbacks=$firstCandidateRun.callbacks; promotionCallbacks=$firstCandidateRun.promotions; marking=$firstCandidateRun.marking; objectMemoryMutation=$firstCandidateRun.objectMutation; restartRequests=$firstCandidateRun.restartRequests; restartEntries=$firstCandidateRun.restartEntries; managedResume=$firstCandidateRun.managedResume }
            sentinelAndObjects=[ordered]@{ sentinelChecks=$firstCandidateRun.sentinelChecks; liveSentinels="0x00000004"; objectCount="0x00000028"; objectValidationBeforeLoad=$firstCandidateRun.objectBeforeLoad; objectValidationAfterLoad=$firstCandidateRun.objectAfterLoad; objectValidationAtStop=$firstCandidateRun.objectAtStop; addressesUnchanged=$true; contentsUnchanged=$true; layoutsUnchanged=$true; duplicateAddresses=0 }
            safeStop=[ordered]@{ marker="C011EC05"; reason="one explicit pointer-width load from the real EnumGcRef slot, before callback and semantic processing"; rawValue=$firstCandidateRun.rawValue; nullOrNonNull=$firstCandidateRun.valueIsNull; deterministic=$true; lockHeld=$true; eeSuspended=$true }
            proofKernelSha256=$specializedKernelHash; qemuVersion=$qemuVersion; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot; runs=$runResults
            regressions=[ordered]@{ firstRootCandidateLoad="PASS 3/3 fresh QEMU runs"; firstPerThreadRootProvider="PASS preserved by candidate run's earlier milestones"; allocationContextFixupRootBoundary="PENDING separate C011EC03 regression"; singleThreadSuspendEe="PENDING separate C011EC02 regression"; staticChecks="PASS script parse, manifest parse, serial checks, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not yet rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed"; runtimePackStaticNonallocating="blocked by observed runtime-pack identity mismatch where still unresolved" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md"); evidenceRoot=$runRoot
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash; authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-candidate-load experiment: PASS (safe bounded stop)" -ForegroundColor Green
    } elseif ($isFirstPerThreadRootProvider) {
        $manifest = [ordered]@{
            outcome="A / first real per-thread provider entered; safe stop before EnumGcRef candidate access"
            proofMode=$ProofMode; repositoryHead=$repoHead; dirtyState=$dirtyState; dirtyDiffStat=$dirtySummary
            startingCommittedHead=$repoHead; startingDirtyState=$dirtyState
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); generatedInjectedGcenvEeSource=$gcEnvEeSource }
            priorCheckpoint=[ordered]@{ marker="C011EC03"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md"; stop="GcScanRoots entry before FOREACH_THREAD" }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            collection=[ordered]@{ requests=1; entries=1; generation=1; reason="reason_oos_soh (5)"; blocking=$true; compacting=$false }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; lockHeld=$true; lockDepth=1; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            allocationContextFixup=[ordered]@{ requestCount=1; entryCount=1; completionCount=1; contextsVisited=1; contextsChanged=1; contextsCleared=1; objectMemoryMutation=0; objectValidationBefore=40; objectValidationAfter=40 }
            rootDispatcher=[ordered]@{ gcScanRootsRequests=1; gcScanRootsEntries=1; foreachThreadRequests=1; foreachThreadEntries=1; iteratorInitializations=1; iteratorCompletions=0; sourceOrderCategory="thread-static-provider"; runtimeSelectedCategory="thread-static-provider" }
            threadEnumeration=[ordered]@{ registeredBefore=1; registeredAfter=1; enumerated=1; included=1; excluded=0; duplicates=0; integrityFailures=0; registryMutationBefore=0; registryMutationAfter=0; identities="current=initiator=enumerated=lock-owner"; record=$runResults[0].threadRecord; records=$runResults }
            provider=[ordered]@{ sourceOrderCategory="thread-static-provider"; runtimeSelectedCategory="thread-static-provider"; exactFunction="Thread::GetThreadStaticStorage"; functionCode=2; requests=2; entries=1; skips=1; skipReason="no inline thread-static roots"; metadataContainers=1; candidateMetadataLocations=1; stackBoundsRequested=0; stackScanningStarted=0; threadStaticStorageRequested=1; threadStaticScanningStarted=0 }
            candidateBoundary=[ordered]@{ candidateValuesRead=0; candidatesDiscovered=0; rootCallbacks=0; promotionCallbacks=0; marking=0; objectMemoryMutation=0; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            sentinelAndObjects=[ordered]@{ sentinelChecks=160; liveSentinels=4; objectCount=40; objectValidationBefore=40; objectValidationAfter=40; addressesUnchanged=$true; contentsUnchanged=$true; layoutsUnchanged=$true; duplicateAddresses=0 }
            safeStop=[ordered]@{ marker="C011EC04"; reason="Thread::GetThreadStaticStorage returned the real static-root slot address; stopped before EnumGcRef"; lockHeld=$true; eeSuspended=$true }
            proofKernelSha256=$specializedKernelHash; activePalArchiveSha256=(Hash-File $activeArchive); qemuVersion=$qemuVersion; runs=$runResults; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot
            regressions=[ordered]@{ firstPerThreadRootProvider="PASS 3/3 fresh QEMU runs"; allocationContextFixupRootBoundary="PENDING separate C011EC03 regression"; singleThreadSuspendEe="PENDING separate C011EC02 regression"; staticChecks="PASS script parse, manifest parse, serial checks, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not yet rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md")
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-per-thread-root-provider experiment: PASS (safe bounded stop)" -ForegroundColor Green
    } elseif ($isAllocationContextFixupRootBoundary) {
        $manifest = [ordered]@{
            outcome="A / single-mutator Workstation GC allocation-context fixup(TRUE) completed; safe stop at GcScanRoots entry before FOREACH_THREAD"
            proofMode=$ProofMode; repositoryHead=$repoHead; dirtyState=$dirtyState; dirtyDiffStat=$dirtySummary
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); generatedInjectedGcenvEeSource=$gcEnvEeSource }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; heapReserveCommit="locked adapted Workstation GC configuration"; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            fixup=[ordered]@{ requestCount=1; enumerationEntryCount=1; completionCount=1; mode="fix_allocation_contexts(TRUE)"; contextsVisited=1; contextsChanged=1; contextsCleared=1; contextsRetired=0; metadataMutationStarted=1; metadataMutationCompleted=1; objectMemoryMutationStarted=0; objectValidationBefore=40; objectValidationAfter=40; objectValidationFailuresBefore=0; objectValidationFailuresAfter=0; overlapFailures=0; duplicateAddressFailures=0; boundaryFailures=0; patternFailures=0; addressChanges=0; segmentBookkeepingMutation=1; allocationPointerBefore="0x0000000100A285C8"; allocationLimitBefore="0x0000000100A29040"; allocationPointerAfter="0x0000000000000000"; allocationLimitAfter="0x0000000000000000"; validExtentBefore="0x0000000100A285C8"; validExtentAfter="0x0000000100A285C8"; unusedTailBefore="0x0000000000000A78"; unusedTailAfter="0x0000000000000000"; heapCounterBefore="0x0000000000028E38"; heapCounterAfter="0x00000000000283C0"; segmentAllocatedBefore="0x0000000100A00028"; segmentAllocatedAfter="0x0000000100A285C8"; segmentCommittedBefore="0x0000000100A31000"; segmentCommittedAfter="0x0000000100A31000"; segmentReservedBefore="0x0000000100B00000"; segmentReservedAfter="0x0000000100B00000" }
            rootBoundary=[ordered]@{ rootPhaseRequestCount=1; dispatcherEntryCount=1; category="thread statics then stack roots, selected at dispatcher entry"; providerRequestCount=0; providerEntryCount=0; firstRootCandidateCount=0; callbacksDelivered=0; promotionCallbacksDelivered=0; markingEntryCount=0; stackScanEntryCount=0; staticRootRequestCount=0; staticRootEntryCount=0; handleRootRequestCount=0; handleRootEntryCount=0; finalizerRootRequestCount=0; finalizerRootEntryCount=0; rootScanningStarted=0; restartResumeCount=0; marker="C011EC03" }
            mutations=[ordered]@{ allocationContextMetadataMutationStarted=1; allocationContextMetadataMutationCompleted=1; objectMemoryMutationStarted=0; rootScanningStarted=0; markingStarted=0; sweepingStarted=0; compactionStarted=0; relocationStarted=0 }
            proofKernelSha256=$specializedKernelHash; activePalArchiveSha256=(Hash-File $activeArchive); qemuVersion=$qemuVersion
            selectors=@("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1")
            collectionPath="soh_try_fit -> a_fit_segment_end_p -> a_state_trigger_ephemeral_gc -> trigger_ephemeral_gc -> GCHeap::GarbageCollectGeneration -> GCToEEInterface::SuspendEE -> GCHeap::GarbageCollect -> fix_allocation_contexts(TRUE) -> GcStartWork -> BeforeGcScanRoots -> GcScanRoots entry"
            sourceBoundaries=[ordered]@{ fixup="GCHeap::fix_allocation_contexts(TRUE) -> GCToEEInterface::GcEnumAllocContexts -> GCHeap::FixAllocContext"; postFixup="GCToEEInterface::GcStartWork entry"; rootPhase="GCToEEInterface::BeforeGcScanRoots entry"; rootDispatcher="GCToEEInterface::GcScanRoots entry before FOREACH_THREAD" }
            firstUnsupportedContract="root provider enumeration, callbacks, promotion, marking, sweeping, compaction, relocation, restart, and resume intentionally not attempted"
            threadStore=[ordered]@{ lockRequests=1; lockAcquisitions=1; lockFailures=0; unlocks=0; recursionDepth=1; registryMutationAttemptsWhileLocked=0; adapterRegistrations=1; registeredManagedThreads=1; expectedOtherMutators=0; stoppedOtherMutators=0; lockHeldAtSafeStop=$true }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; heapMutationStarted=$false; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            identities="recorded in each serial log and diagnostics; current runtime thread equals collection initiator and lock owner"
            runs=$runResults; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot
            regressions=[ordered]@{ allocationContextFixupRootBoundary="PASS 3/3 fresh QEMU runs"; singleThreadSuspendEe="PASS 3/3 fresh QEMU runs, C011EC02 preserved"; staticChecks="PASS script parse, manifest parse, serial checks, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md")
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash
        }
        $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC allocation-context fixup/root-boundary experiment: PASS (safe bounded stop)" -ForegroundColor Green
    } else {
        $manifest = [ordered]@{
            outcome="A / single-mutator SuspendEE completed and returned; safe stop after DisablePreemptiveGC and before GCHeap::GarbageCollect"
            repositoryHead=$repoHead; dirtyState=$dirtyState; dirtyDiffStat=$dirtySummary
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); generatedInjectedGcenvEeSource=$gcEnvEeSource }
            workload=[ordered]@{ arrayLength=4096; actualAlignedSize="0x1018"; hardAllocationLimit=256; heapReserveCommit="locked adapted Workstation GC configuration"; allocationCount="0x28"; fast="0x13"; rare="0x16"; refills="0x15"; sameSegmentCommits="0x2"; segmentTransitions="0x0"; liveSentinels="0x4" }
            proofKernelSha256=$specializedKernelHash; activePalArchiveSha256=(Hash-File $activeArchive); qemuVersion=$qemuVersion
            selectors=@("GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1","GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST=1")
            collectionPath="soh_try_fit -> a_fit_segment_end_p -> a_state_trigger_ephemeral_gc -> trigger_ephemeral_gc -> GCHeap::GarbageCollectGeneration -> GCToEEInterface::SuspendEE -> GCHeap::GarbageCollect before fix_allocation_contexts"
            requestedGeneration="0x00000001"; collectionReason="reason_oos_soh (5 / 0x00000005)"; suspendReason="SUSPEND_FOR_GC (1 / 0x00000001)"; collectionBlockingMode="blocking"; collectionCompactingMode="not selected before safe stop"
            firstUnsupportedContract="none for single-mutator SuspendEE; root enumeration is the intentionally unsupported next work"
            nextBoundary="GCHeap::GarbageCollect before fix_allocation_contexts"; rootEnumerationRequestCount=0; rootEnumerationEntryCount=0; stackWalkRequestCount=0; stackWalkEntryCount=0; handleScanRequestCount=0; handleScanEntryCount=0
            threadStore=[ordered]@{ lockRequests=1; lockAcquisitions=1; lockFailures=0; unlocks=0; recursionDepth=1; registryMutationAttemptsWhileLocked=0; adapterRegistrations=1; registeredManagedThreads=1; expectedOtherMutators=0; stoppedOtherMutators=0; lockHeldAtSafeStop=$true }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; currentThreadExempt=$true; managedEntryProhibited=$true; eeSuspended=$true; heapMutationStarted=$false; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            identities="recorded in each serial log and diagnostics; current runtime thread equals collection initiator and lock owner"
            runs=$runResults; exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash
        }
        $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "Single-thread SuspendEE QEMU experiment: PASS (safe bounded stop)" -ForegroundColor Green
    }
}
finally {
    if (-not [string]::IsNullOrWhiteSpace($make)) {
        try { & $make -C kernel ARCH=amd64 clean *> (Join-Path $runRoot "kernel-clean.log") } catch { }
    }
    if (Test-Path -LiteralPath $normalKernelSource -PathType Leaf) {
        New-Item -ItemType Directory -Force -Path (Split-Path $kernelPath), (Split-Path $espKernelPath) | Out-Null
        Copy-Item -LiteralPath $normalKernelSource -Destination $kernelPath -Force
        Copy-Item -LiteralPath $normalKernelSource -Destination $espKernelPath -Force
        $restoredBuildHash = Hash-File $kernelPath
        $restoredEspHash = Hash-File $espKernelPath
        Set-Content -LiteralPath (Join-Path $runRoot "restored-normal-kernel.sha256") -Value @("build=$restoredBuildHash","esp=$restoredEspHash") -Encoding ASCII
        if ($restoredBuildHash -ne $normalKernelHash -or $restoredEspHash -ne $normalKernelHash) { throw "Normal kernel restoration hash mismatch." }
        if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
            $completedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            $completedManifest | Add-Member -NotePropertyName ordinaryKernelAfter -NotePropertyValue ([ordered]@{ build=$restoredBuildHash; esp=$restoredEspHash }) -Force
            $completedManifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        }
    }
}
