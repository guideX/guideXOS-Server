param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 90,
    [int]$FreshBootCount = 3,
    [switch]$SkipManagedBuild,
    [ValidateSet("single-thread-suspend-ee", "allocation-context-fixup-root-boundary", "first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary", "first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast")]
    [string]$ProofMode = "single-thread-suspend-ee"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -lt 1) { throw "FreshBootCount must be at least 1." }

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($ProofMode -eq "first-root-first-non-null-old-o") {
        Join-Path $root "out\dotnet\gc-first-root-first-non-null-old-o"
    } elseif ($ProofMode -eq "next-genuine-root-provider") {
        Join-Path $root "out\dotnet\gc-next-genuine-root-provider"
    } elseif ($ProofMode -eq "stack-provider-transition-failfast") {
        Join-Path $root "out\dotnet\gc-stack-provider-transition-failfast"
    } elseif ($ProofMode -eq "first-root-post-queue-mark-decision") {
        Join-Path $root "out\dotnet\gc-first-root-post-queue-mark-decision"
    } elseif ($ProofMode -eq "first-root-first-mark-mutation") {
        Join-Path $root "out\dotnet\gc-first-root-first-mark-mutation"
    } elseif ($ProofMode -eq "first-root-pre-mark-boundary") {
        Join-Path $root "out\dotnet\gc-first-root-pre-mark-boundary"
    } elseif ($ProofMode -eq "first-root-condemned-generation-decision") {
        Join-Path $root "out\dotnet\gc-first-root-condemned-generation-decision"
    } elseif ($ProofMode -eq "first-root-heap-resolution") {
        Join-Path $root "out\dotnet\gc-first-root-heap-resolution"
    } elseif ($ProofMode -eq "first-root-membership-classification") {
        Join-Path $root "out\dotnet\gc-first-root-membership-classification"
    } elseif ($ProofMode -eq "first-root-callback-entry") {
        Join-Path $root "out\dotnet\gc-first-root-callback-entry"
    } elseif ($ProofMode -eq "first-non-null-root-callback-boundary") {
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
$isFirstRootHeapResolution = $ProofMode -eq "first-root-heap-resolution"
$isFirstRootCondemnedGenerationDecision = $ProofMode -eq "first-root-condemned-generation-decision"
$isFirstRootFirstMarkMutation = $ProofMode -eq "first-root-first-mark-mutation"
$isFirstRootPostQueueMarkDecision = $ProofMode -eq "first-root-post-queue-mark-decision"
$isFirstRootFirstNonNullOldO = $ProofMode -eq "first-root-first-non-null-old-o"
$isStackProviderTransitionFailFast = $ProofMode -eq "stack-provider-transition-failfast"
$isNextGenuineRootProvider = $ProofMode -in @("next-genuine-root-provider", "stack-provider-transition-failfast")
$isFirstRootPreMarkBoundary = $ProofMode -in @("first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision")
$isFirstRootHeapResolutionOrCondemned = $isFirstRootHeapResolution -or $isFirstRootCondemnedGenerationDecision -or $isFirstRootPreMarkBoundary
$isFirstRootCondemnedGenerationDecisionOrPreMark = $isFirstRootCondemnedGenerationDecision -or $isFirstRootPreMarkBoundary
$isFirstRootMembershipClassification = $ProofMode -eq "first-root-membership-classification"
$isFirstRootCallbackEntry = $ProofMode -in @("first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast")
$isFirstNonNullRoot = $ProofMode -eq "first-non-null-root-callback-boundary"
$isCandidateLoadEnumeration = $isFirstRootCandidateLoad -or $isFirstNonNullRoot -or $isFirstRootCallbackEntry
$isFirstPerThreadRootProvider = $ProofMode -in @("first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary", "first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast")
$isAllocationContextFixupRootBoundary = $ProofMode -in @("allocation-context-fixup-root-boundary", "first-per-thread-root-provider", "first-root-candidate-load", "first-non-null-root-callback-boundary", "first-root-callback-entry", "first-root-membership-classification", "first-root-heap-resolution", "first-root-condemned-generation-decision", "first-root-pre-mark-boundary", "first-root-first-mark-mutation", "first-root-post-queue-mark-decision", "first-root-first-non-null-old-o", "next-genuine-root-provider", "stack-provider-transition-failfast")
$proofDefine = if ($isNextGenuineRootProvider) {
    $minimalDefine = if ($isStackProviderTransitionFailFast) { " /DGUIDEXOS_NATIVEAOT_STACK_PROVIDER_TRANSITION_FAILFAST_MINIMAL" } else { "" }
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_NON_NULL_OLD_O_ALLOCATION /DGUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION$minimalDefine"
} elseif ($isFirstRootFirstNonNullOldO) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_NON_NULL_OLD_O_ALLOCATION"
} elseif ($isFirstRootPostQueueMarkDecision) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_POST_QUEUE_MARK_DECISION_ALLOCATION"
} elseif ($isFirstRootFirstMarkMutation) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_ALLOCATION"
} elseif ($isFirstRootPreMarkBoundary) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION"
} elseif ($isFirstRootCondemnedGenerationDecision) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION"
} elseif ($isFirstRootHeapResolution) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION"
} elseif ($isFirstRootMembershipClassification) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION"
} elseif ($isFirstRootCallbackEntry) {
    "/DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION"
} elseif ($isFirstNonNullRoot) {
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
$buildRoot = Join-Path $evidence ("build-" + (Split-Path -Leaf $runRoot))
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
$normalKernelSource = Join-Path $buildRoot "ordinary-kernel-before.elf"
$normalKernelHash = "161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550"
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

$startingCommittedHead = (& git -C $root rev-parse HEAD).Trim()
$startingBranch = (& git -C $root branch --show-current).Trim()
$startingWorktreeStatus = @(& git -C $root status --short)
$taskStartCheckpoint = [ordered]@{
    head=$startingCommittedHead
    branch=$startingBranch
    dirtyState=if ($startingWorktreeStatus.Count -eq 0) { "clean" } else { "dirty" }
    ordinaryKernelSha256="161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550"
    ordinaryEspSha256="161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550"
}

New-Item -ItemType Directory -Force -Path $runRoot, $buildRoot, $artifactRoot, $runtimeRoot | Out-Null
$ordinaryBuildBefore = if (Test-Path -LiteralPath $kernelPath -PathType Leaf) { Hash-File $kernelPath } else { $null }
$ordinaryEspBefore = if (Test-Path -LiteralPath $espKernelPath -PathType Leaf) { Hash-File $espKernelPath } else { $null }
$ordinaryKernelBefore = [ordered]@{ build=$ordinaryBuildBefore; esp=$ordinaryEspBefore }
if ($ordinaryBuildBefore -ne $normalKernelHash -or $ordinaryEspBefore -ne $normalKernelHash) {
    throw "The ordinary kernel/ESP is not the verified production baseline before proof deployment. build=$ordinaryBuildBefore esp=$ordinaryEspBefore expected=$normalKernelHash"
}
Copy-Item -LiteralPath $kernelPath -Destination $normalKernelSource -Force
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
$isC14QueueInstrumentation = $isFirstRootFirstNonNullOldO -or $isNextGenuineRootProvider
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
$gcEnumSource = Join-Path $runtimeRoot $(if ($isNextGenuineRootProvider) { "GcEnum.next-genuine-root-provider.cpp" } elseif ($isFirstRootCallbackEntry) { "GcEnum.first-root-callback-entry.cpp" } else { "GcEnum.first-root-candidate-load.cpp" })
$gcEnum = Join-Path $runtimeRoot $(if ($isNextGenuineRootProvider) { "GcEnum.next-genuine-root-provider.obj" } elseif ($isFirstRootCallbackEntry) { "GcEnum.first-root-callback-entry.obj" } else { "GcEnum.first-root-candidate-load.obj" })
$gcWksSource = Join-Path $runtimeRoot "gcwks.first-root-callback-entry.cpp"
$gcWks = Join-Path $runtimeRoot "gcwks.first-root-callback-entry.obj"
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
    $repoHead = $startingCommittedHead
    $dirtyState = $startingWorktreeStatus
    $dirtySummary = (($startingWorktreeStatus) -join "`n")
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
extern "C" void __cdecl guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(int condemned, int max_gen, uintptr_t scanContext);
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
    if ($isNextGenuineRootProvider) {
        $declaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC15GcScanRootsEntered(int condemned, int maxGeneration, uintptr_t scanContext);
extern "C" void __cdecl guideXosNativeAotC011EC15ProviderEntered(uint32_t category, uintptr_t thread, uintptr_t provider);
'@.TrimEnd()
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
        $scanRootsReplacement = 'void GCToEEInterface::GcScanRoots(ScanFunc* fn, int condemned, int max_gen, ScanContext* sc)' + [Environment]::NewLine + '{' + [Environment]::NewLine + '    guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));'
        $injectedText = [regex]::Replace($injectedText, $scanRootsPattern, $scanRootsReplacement, 1)
        if ($isNextGenuineRootProvider) {
            $injectedText = $injectedText.Replace(
                '    guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));',
                '    guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));' + [Environment]::NewLine + '    guideXosNativeAotC011EC15GcScanRootsEntered(condemned, max_gen, reinterpret_cast<uintptr_t>(sc));')
        }
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
        if ($isNextGenuineRootProvider) {
            $injectedText = $injectedText.Replace(
                '                EnumGcRef(&pRoot->m_threadStaticsBase, GCRK_Object, fn, sc);',
                '                guideXosNativeAotC011EC15ProviderEntered(1u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pRoot));' + [Environment]::NewLine + '                EnumGcRef(&pRoot->m_threadStaticsBase, GCRK_Object, fn, sc);')
        }
        $injectedText = $injectedText.Replace('            EnumGcRef(pThread->GetThreadStaticStorage(), GCRK_Object, fn, sc);', '            Object** threadStaticStorage = pThread->GetThreadStaticStorage();' + [Environment]::NewLine + '            guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered(reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(threadStaticStorage));' + [Environment]::NewLine + '            EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);')
         if ($isNextGenuineRootProvider) {
             $injectedText = $injectedText.Replace(
                 '            EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);',
                 '            guideXosNativeAotC011EC15ProviderEntered(2u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(threadStaticStorage));' + [Environment]::NewLine + '            EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);')
             $injectedText = $injectedText.Replace(
                  '            STRESS_LOG1(LF_GC | LF_GCROOTS, LL_INFO100, "{ Starting scan of Thread %p\n", pThread);',
                  '            guideXosNativeAotC011EC15ProviderEntered(3u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pThread));' + [Environment]::NewLine + '            STRESS_LOG1(LF_GC | LF_GCROOTS, LL_INFO100, "{ Starting scan of Thread %p\n", pThread);')
            $injectedText = $injectedText.Replace(
                '            guideXosNativeAotC011EC15ProviderEntered(3u, reinterpret_cast<uintptr_t>(pThread), reinterpret_cast<uintptr_t>(pThread));' + [Environment]::NewLine + '            pThread->GcScanRoots(fn, sc);',
                '            pThread->GcScanRoots(fn, sc);')
        }
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
    if ($isFirstRootCallbackEntry) {
        $lockedGcwksPath = Join-Path $lockedSourceRoot "src\coreclr\gc\gcwks.cpp"
        $lockedGcCppPath = Join-Path $lockedSourceRoot "src\coreclr\gc\gc.cpp"
        $lockedGcPrivPath = Join-Path $lockedSourceRoot "src\coreclr\gc\gcpriv.h"
        Require-File $lockedGcwksPath "Locked NativeAOT gcwks.cpp source"
        Require-File $lockedGcCppPath "Locked Workstation GC gc.cpp source"
        Require-File $lockedGcPrivPath "Locked Workstation GC gcpriv.h source"
        $gcWksText = (Get-Content -LiteralPath $lockedGcwksPath -Raw).Replace([string]([char]13) + [string]([char]10), [string][char]10)
        $gcCppText = (Get-Content -LiteralPath $lockedGcCppPath -Raw).Replace([string]([char]13) + [string]([char]10), [string][char]10)
        $gcWksText = '#include <intrin.h>' + [Environment]::NewLine + $gcWksText
        $gcWksText = $gcWksText.Replace('namespace WKS {', '#define _DEBUG 1' + [Environment]::NewLine + 'namespace WKS {')
        $membershipDeclaration = ""
        if ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            $membershipDeclaration = @'
extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCheckRequested(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCheckEntered(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCheckCompleted(uintptr_t object, uintptr_t lowerBound, uintptr_t upperBound, uint32_t lowerEvaluated, uint32_t upperEvaluated, uint32_t lowerResult, uint32_t upperResult, uint32_t result);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootMembershipResultBoundary(uint32_t result);
'@
            if ($isFirstRootHeapResolutionOrCondemned) {
                $heapResolutionCompletionAttribute = if ($isFirstRootCondemnedGenerationDecisionOrPreMark) { "" } else { "__declspec(noreturn) " }
                $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootHeapResolutionRequested(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootHeapResolutionEntered(uintptr_t object, uintptr_t threadHeap, uint32_t threadNumber);
extern "C" HEAP_RESOLUTION_COMPLETION_ATTRIBUTEvoid __cdecl guideXosNativeAotFirstRootHeapResolutionCompleted(uintptr_t object, uintptr_t threadHeap, uintptr_t heap, uint32_t heapNumber, uint32_t totalHeapCount);
'@.TrimEnd()
                $membershipDeclaration = $membershipDeclaration.Replace('HEAP_RESOLUTION_COMPLETION_ATTRIBUTE', $heapResolutionCompletionAttribute)
                if ($isFirstRootCondemnedGenerationDecisionOrPreMark) {
                    $condemnedCompletionAttribute = if ($isFirstRootPreMarkBoundary) { "" } else { "__declspec(noreturn) " }
                    $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered(uintptr_t object, uintptr_t lowerBound, uintptr_t upperBound, int condemnedGeneration, int maximumGeneration, uintptr_t generationTable, uintptr_t generationTableIndex, uint32_t minimumSegmentSizeShift);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationQueryStart(uintptr_t object, uintptr_t generationTable, uintptr_t generationTableIndex);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationQueryCompleted(uintptr_t object, uint32_t generation);
extern "C" void __cdecl guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted(uintptr_t object, uintptr_t segment);
extern "C" CONDEMNED_COMPLETION_ATTRIBUTEvoid __cdecl guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted(uintptr_t object, uint32_t result);
'@.TrimEnd()
                    $membershipDeclaration = $membershipDeclaration.Replace('CONDEMNED_COMPLETION_ATTRIBUTE', $condemnedCompletionAttribute)
                }
            }
            if ($isFirstRootPreMarkBoundary) {
                $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkTrueBranchEntered(uintptr_t object, uintptr_t heapSentinel, uint32_t flags);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkRootFlagTest(uint32_t flags, uint32_t bit, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkConservativeCheck(uintptr_t object, uint32_t enabled, uint32_t isFree);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationEntered(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead(uintptr_t object, uintptr_t methodTable);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer(uintptr_t object, uint32_t smallOnly, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkDebugValidationCompleted(uintptr_t object, uint32_t noRangeChecks, uint32_t verifyHeapGc, uint32_t smallHeapPointer, uint32_t largeHeapPointer);
extern "C" void __cdecl guideXosNativeAotFirstRootPreMarkBoundaryReached(uintptr_t object, uintptr_t heapSentinel, uint32_t flags, uintptr_t markHelper);
'@.TrimEnd()
                if ($isFirstRootFirstMarkMutation -or $isFirstRootPostQueueMarkDecision) {
                    $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootMarkHelperEntered(uintptr_t po, uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootMarkWorklistWriteBefore(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase, uint32_t capacity);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootMarkWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
'@.TrimEnd()
                    if ($isFirstRootPostQueueMarkDecision) {
                        $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueDecisionRequested(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueDecisionEntered(uintptr_t oldObject);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootPostQueueNullDecisionCompleted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueNonNullDecisionStarted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueMarkedRequested(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueMarkedCompleted(uintptr_t oldObject, uint32_t result);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootPostQueueMarkedTrueBoundary(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary(uintptr_t oldObject);
'@.TrimEnd()
                    }
                }
            }
            if ($isC14QueueInstrumentation) {
                $membershipDeclaration += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered(uintptr_t po, uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCallbackEntered(uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3, uintptr_t callbackAddress, uintptr_t returnAddress, uintptr_t stackPointer);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoaded(uintptr_t rawValue);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionRequested(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionEntered(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedRequested(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(uintptr_t oldObject, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldORawMarkWordRead(uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeader, uintptr_t markMask);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(uintptr_t oldObject);
'@.TrimEnd()
            }
            if ($isC14QueueInstrumentation) {
                $gcWksText = $gcWksText.Replace(
                    'namespace WKS {',
                    $membershipDeclaration.TrimEnd() + [Environment]::NewLine + 'namespace WKS {')
            } else {
                $gcWksText = '#include <intrin.h>' + [Environment]::NewLine + $membershipDeclaration.TrimEnd() + [Environment]::NewLine + $gcWksText
            }
        }
        if ($isC14QueueInstrumentation) {
            $c14Declarations = @'
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered(uintptr_t po, uintptr_t object);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCallbackEntered(uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3, uintptr_t callbackAddress, uintptr_t returnAddress, uintptr_t stackPointer);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoaded(uintptr_t rawValue);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionRequested(uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase, uint32_t capacity);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldODecisionEntered(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedRequested(uintptr_t oldObject);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(uintptr_t oldObject, uint32_t result);
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldORawMarkWordRead(uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeader, uintptr_t markMask);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(uintptr_t oldObject);
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(uintptr_t oldObject);
'@
            if ($isNextGenuineRootProvider) {
                $c14Declarations += [Environment]::NewLine + @'
extern "C" void __cdecl guideXosNativeAotC011EC15QueueMarkReturned(uintptr_t object, uintptr_t slot, uintptr_t oldValue, uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter, uintptr_t queueBase);
extern "C" void __cdecl guideXosNativeAotC011EC15MarkHelperReturned(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteReturned(uintptr_t object);
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteEntered(uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3, uintptr_t callbackAddress, uintptr_t returnAddress, uintptr_t stackPointer);
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteCandidateLoaded(uintptr_t object);
'@.TrimEnd()
            }
            $gcWksText = $gcWksText.Replace(
                'namespace WKS {',
                $c14Declarations.TrimEnd() + [Environment]::NewLine + 'namespace WKS {')
        }
        $candidateLoadedDeclaration = if ($isNextGenuineRootProvider) {
            'extern "C" void __cdecl guideXosNativeAotC011EC15PromoteCandidateLoaded(uintptr_t object);'
        } elseif ($isC14QueueInstrumentation) {
            'extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoaded(uintptr_t rawValue);'
        } elseif ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            'extern "C" void __cdecl guideXosNativeAotFirstRootMembershipCandidateLoaded(uintptr_t rawValue);'
        } else {
            'extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotFirstRootCallbackCandidateLoaded(uintptr_t rawValue);'
        }
        if ($isNextGenuineRootProvider) {
            $callbackDeclaration = @'
extern "C" void __cdecl guideXosNativeAotC011EC15PromoteEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer);
'@
        } elseif ($isC14QueueInstrumentation) {
            $callbackDeclaration = @'
extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCallbackEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer);
'@
        } else {
            $callbackDeclaration = @'
extern "C" void __cdecl guideXosNativeAotFirstRootCallbackEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer);
'@
        }
        $callbackDeclaration = $callbackDeclaration.TrimEnd() + [Environment]::NewLine + $candidateLoadedDeclaration
        $callbackPattern = '(?m)^void GCHeap::Promote\(Object\*\* ppObject, ScanContext\* sc, uint32_t flags\)\r?\n\{'
        $callbackFunction = if ($isNextGenuineRootProvider) { 'guideXosNativeAotC011EC15PromoteEntered' } elseif ($isC14QueueInstrumentation) { 'guideXosNativeAotFirstRootNonNullOldOCallbackEntered' } else { 'guideXosNativeAotFirstRootCallbackEntered' }
        $callbackReplacement = @"
$callbackDeclaration
void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)
{
    $callbackFunction(
        reinterpret_cast<uintptr_t>(ppObject),
        reinterpret_cast<uintptr_t>(sc),
        static_cast<uintptr_t>(flags),
        reinterpret_cast<uintptr_t>(&GCHeap::Promote),
        *reinterpret_cast<uintptr_t*>(_AddressOfReturnAddress()),
        reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
"@
        $gcWksInjected = [regex]::Replace($gcCppText, $callbackPattern, $callbackReplacement.TrimEnd(), 1)
        $promoteSignature = 'void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)'
        $promoteOffset = $gcWksInjected.IndexOf($promoteSignature, [System.StringComparison]::Ordinal)
        if ($promoteOffset -lt 0) { throw "Generated Workstation GC source did not contain the injected GCHeap::Promote body." }
        $promotePrefix = $gcWksInjected.Substring(0, $promoteOffset)
        $promoteBody = $gcWksInjected.Substring($promoteOffset)
        $candidateLoadedFunction = if ($isNextGenuineRootProvider) {
            'guideXosNativeAotC011EC15PromoteCandidateLoaded'
        } elseif ($isC14QueueInstrumentation) {
            'guideXosNativeAotFirstRootNonNullOldOCandidateLoaded'
        } elseif ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            'guideXosNativeAotFirstRootMembershipCandidateLoaded'
        } else {
            'guideXosNativeAotFirstRootCallbackCandidateLoaded'
        }
        $promoteBody = $promoteBody.Replace(
            '    uint8_t* o = (uint8_t*)*ppObject;',
            '    uint8_t* o = (uint8_t*)*ppObject;' + [Environment]::NewLine + '    ' + $candidateLoadedFunction + '(reinterpret_cast<uintptr_t>(o));')
        if ($isNextGenuineRootProvider) {
            $promoteBody = $promoteBody.Replace(
                '    hpt->mark_object_simple (&o THREAD_NUMBER_ARG);',
                '    hpt->mark_object_simple (&o THREAD_NUMBER_ARG);' + [Environment]::NewLine + '    guideXosNativeAotC011EC15MarkHelperReturned(reinterpret_cast<uintptr_t>(o));')
            $promoteBody = $promoteBody.Replace(
                '    STRESS_LOG_ROOT_PROMOTE(ppObject, o, o ? header(o)->GetMethodTable() : NULL);',
                '    STRESS_LOG_ROOT_PROMOTE(ppObject, o, o ? header(o)->GetMethodTable() : NULL);' + [Environment]::NewLine + '    guideXosNativeAotC011EC15PromoteReturned(reinterpret_cast<uintptr_t>(o));')
            if ($promoteBody -notmatch 'guideXosNativeAotC011EC15MarkHelperReturned' -or
                $promoteBody -notmatch 'guideXosNativeAotC011EC15PromoteReturned') {
                throw "C011EC15 Promote return instrumentation did not match mark_object_simple/STRESS_LOG_ROOT_PROMOTE."
            }
        }
        if ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            $membershipBoundarySource = if ($isFirstRootHeapResolutionOrCondemned) { @'
    if (!gc_heap::is_in_find_object_range (o))
    {
        guideXosNativeAotFirstRootMembershipResultBoundary(0u);
        return;
    }
    guideXosNativeAotFirstRootHeapResolutionRequested(reinterpret_cast<uintptr_t>(o));
'@ } else { @'
    if (!gc_heap::is_in_find_object_range (o))
    {
        guideXosNativeAotFirstRootMembershipResultBoundary(0u);
        return;
    }
    guideXosNativeAotFirstRootMembershipResultBoundary(1u);
'@ }
            $promoteBody = $promoteBody.Replace(
                @'
    if (!gc_heap::is_in_find_object_range (o))
    {
        return;
    }
'@,
                $membershipBoundarySource)
        }
        if ($isFirstRootHeapResolutionOrCondemned) {
            $heapResolutionSource = @'
    HEAP_FROM_THREAD;
    guideXosNativeAotFirstRootHeapResolutionEntered(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        static_cast<uint32_t>(thread));

    gc_heap* hp = gc_heap::heap_of (o);
    guideXosNativeAotFirstRootHeapResolutionCompleted(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        reinterpret_cast<uintptr_t>(hp), static_cast<uint32_t>(heap_number),
        static_cast<uint32_t>(n_heaps));
'@
            $promoteBody = $promoteBody.Replace(
                @'
    HEAP_FROM_THREAD;

    gc_heap* hp = gc_heap::heap_of (o);
'@,
                $heapResolutionSource)
        }
        $gcWksInjected = $promotePrefix + $promoteBody
        if ($isFirstRootMembershipClassification -or $isFirstRootHeapResolutionOrCondemned) {
            $helperOffset = $gcWksInjected.IndexOf('bool gc_heap::is_in_find_object_range (uint8_t* o)', [System.StringComparison]::Ordinal)
            if ($helperOffset -lt 0) { throw "Locked gc.cpp membership helper definition was not found." }
            $helperEnd = $gcWksInjected.IndexOf("`n}", $helperOffset, [System.StringComparison]::Ordinal)
            if ($helperEnd -lt 0) { throw "Locked gc.cpp membership helper closing brace was not found." }
            $helperEnd += 2
            $helperText = $gcWksInjected.Substring($helperOffset, $helperEnd - $helperOffset)
            $helperText = $helperText.Replace(
                @'
    if (o == nullptr)
    {
        return false;
    }
'@,
                @'
    if (o == nullptr)
    {
        return false;
    }
    guideXosNativeAotFirstRootMembershipCheckRequested(reinterpret_cast<uintptr_t>(o));
    guideXosNativeAotFirstRootMembershipCheckEntered(reinterpret_cast<uintptr_t>(o));
'@)
            $helperText = $helperText.Replace(
                '    return ((o >= g_gc_lowest_address) && (o < bookkeeping_covered_committed));',
                @'
    uint8_t* guideXosMembershipLowerBound = g_gc_lowest_address;
    const uint32_t guideXosMembershipLowerResult =
        o >= guideXosMembershipLowerBound ? 1u : 0u;
    uint8_t* guideXosMembershipUpperBound = nullptr;
    uint32_t guideXosMembershipUpperEvaluated = 0u;
    uint32_t guideXosMembershipUpperResult = 0u;
    if (guideXosMembershipLowerResult != 0u)
    {
        guideXosMembershipUpperBound = bookkeeping_covered_committed;
        guideXosMembershipUpperEvaluated = 1u;
        guideXosMembershipUpperResult =
            o < guideXosMembershipUpperBound ? 1u : 0u;
    }
    const uint32_t guideXosMembershipResult =
        guideXosMembershipLowerResult != 0u &&
        guideXosMembershipUpperResult != 0u ? 1u : 0u;
    guideXosNativeAotFirstRootMembershipCheckCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(guideXosMembershipLowerBound),
        reinterpret_cast<uintptr_t>(guideXosMembershipUpperBound),
        1u, guideXosMembershipUpperEvaluated,
        guideXosMembershipLowerResult, guideXosMembershipUpperResult,
        guideXosMembershipResult);
    return guideXosMembershipResult != 0u;
'@)
            if ($helperText -notmatch 'guideXosNativeAotFirstRootMembershipCheckRequested' -or
                $helperText -notmatch 'guideXosNativeAotFirstRootMembershipCheckCompleted' -or
                $helperText -notmatch 'guideXosMembershipLowerBound' -or
                $helperText -notmatch 'bookkeeping_covered_committed') {
                throw "Locked gc.cpp membership helper injection did not preserve the active USE_REGIONS/FEATURE_CONSERVATIVE_GC path."
            }
            $gcWksInjected = $gcWksInjected.Substring(0, $helperOffset) + $helperText + $gcWksInjected.Substring($helperEnd)
        }
        if ($isFirstRootCondemnedGenerationDecisionOrPreMark) {
            $condemnedHelperOffset = $gcWksInjected.IndexOf('bool gc_heap::is_in_condemned_gc (uint8_t* o)', [System.StringComparison]::Ordinal)
            if ($condemnedHelperOffset -lt 0) { throw "Locked gc.cpp condemned-generation helper definition was not found." }
            $condemnedHelperEnd = $gcWksInjected.IndexOf("`n}", $condemnedHelperOffset, [System.StringComparison]::Ordinal)
            if ($condemnedHelperEnd -lt 0) { throw "Locked gc.cpp condemned-generation helper closing brace was not found." }
            $condemnedHelperEnd += 2
            $condemnedHelperText = $gcWksInjected.Substring($condemnedHelperOffset, $condemnedHelperEnd - $condemnedHelperOffset)
            $condemnedHelperText = $condemnedHelperText.Replace(
                "{`n    assert ((o >= g_gc_lowest_address) && (o < g_gc_highest_address));",
                "{`n    guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested(reinterpret_cast<uintptr_t>(o));`n    assert ((o >= g_gc_lowest_address) && (o < g_gc_highest_address));")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "    int condemned_gen = settings.condemned_generation;",
                "    int condemned_gen = settings.condemned_generation;`n    const size_t guideXosCondemnedRegionIndex = (size_t)o >> gc_heap::min_segment_size_shr;`n    guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered(`n        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(g_gc_lowest_address),`n        reinterpret_cast<uintptr_t>(g_gc_highest_address), condemned_gen, max_generation,`n        reinterpret_cast<uintptr_t>(gc_heap::map_region_to_generation_skewed),`n        static_cast<uintptr_t>(guideXosCondemnedRegionIndex),`n        static_cast<uint32_t>(gc_heap::min_segment_size_shr));")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "        int gen = get_region_gen_num (o);",
                "        guideXosNativeAotFirstRootCondemnedGenerationQueryStart(`n            reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(gc_heap::map_region_to_generation_skewed),`n            static_cast<uintptr_t>(guideXosCondemnedRegionIndex));`n        int gen = get_region_gen_num (o);`n        guideXosNativeAotFirstRootCondemnedGenerationQueryCompleted(`n            reinterpret_cast<uintptr_t>(o), static_cast<uint32_t>(gen));")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "            return false;",
                "            guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted(reinterpret_cast<uintptr_t>(o), 0u);`n            return false;")
            $condemnedHelperText = $condemnedHelperText.Replace(
                "    return true;`n}",
                "    guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted(reinterpret_cast<uintptr_t>(o), 1u);`n    return true;`n}")
            $condemnedGenerationFunctionOffset = $gcWksInjected.IndexOf('int gc_heap::get_region_gen_num (uint8_t* obj)', [System.StringComparison]::Ordinal)
            if ($condemnedGenerationFunctionOffset -lt 0) { throw "Locked gc.cpp generation helper definition was not found." }
            $condemnedGenerationFunctionEnd = $gcWksInjected.IndexOf("`n}", $condemnedGenerationFunctionOffset, [System.StringComparison]::Ordinal)
            if ($condemnedGenerationFunctionEnd -lt 0) { throw "Locked gc.cpp generation helper closing brace was not found." }
            $condemnedGenerationFunctionEnd += 2
            $condemnedGenerationFunctionText = $gcWksInjected.Substring($condemnedGenerationFunctionOffset, $condemnedGenerationFunctionEnd - $condemnedGenerationFunctionOffset)
            $condemnedGenerationFunctionText = $condemnedGenerationFunctionText.Replace(
                "    assert (gen_num == heap_segment_gen_num (region_of (obj)));",
                "    heap_segment* guideXosCondemnedRegion = region_of (obj);`n    guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted(`n        reinterpret_cast<uintptr_t>(obj), reinterpret_cast<uintptr_t>(guideXosCondemnedRegion));`n    assert (gen_num == heap_segment_gen_num (guideXosCondemnedRegion));")
            if ($condemnedGenerationFunctionText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted') {
                throw "Locked gc.cpp generation helper segment assertion injection did not match."
            }
            $gcWksInjected = $gcWksInjected.Substring(0, $condemnedGenerationFunctionOffset) + $condemnedGenerationFunctionText + $gcWksInjected.Substring($condemnedGenerationFunctionEnd)
            if ($condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested' -or
                $condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered' -or
                $condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationQueryStart' -or
                $condemnedHelperText -notmatch 'guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted') {
                throw "Locked gc.cpp condemned-generation helper injection did not match the complete USE_REGIONS decision path."
            }
            $gcWksInjected = $gcWksInjected.Substring(0, $condemnedHelperOffset) + $condemnedHelperText + $gcWksInjected.Substring($condemnedHelperEnd)
        }
        if ($isFirstRootPreMarkBoundary) {
            $promoteOffset = $gcWksInjected.IndexOf($promoteSignature, [System.StringComparison]::Ordinal)
            if ($promoteOffset -lt 0) { throw "Generated Workstation GC source lost GCHeap::Promote before pre-mark instrumentation." }
            $promotePrefix = $gcWksInjected.Substring(0, $promoteOffset)
            $promoteBody = $gcWksInjected.Substring($promoteOffset)
            $promoteBody = $promoteBody.Replace(
                '    dprintf (3, ("Promote %zx", (size_t)o));',
                @'
    guideXosNativeAotFirstRootPreMarkTrueBranchEntered(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        static_cast<uint32_t>(flags));
    dprintf (3, ("Promote %zx", (size_t)o));
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                '    if (flags & GC_CALL_INTERIOR)',
                @'
    guideXosNativeAotFirstRootPreMarkRootFlagTest(
        static_cast<uint32_t>(flags), 0x1u,
        (flags & GC_CALL_INTERIOR) != 0u ? 1u : 0u);
    if (flags & GC_CALL_INTERIOR)
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                '    if (flags & GC_CALL_PINNED)',
                @'
    guideXosNativeAotFirstRootPreMarkRootFlagTest(
        static_cast<uint32_t>(flags), 0x2u,
        (flags & GC_CALL_PINNED) != 0u ? 1u : 0u);
    if (flags & GC_CALL_PINNED)
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                @'
    if (GCConfig::GetConservativeGC()
        && ((CObjectHeader*)o)->IsFree())
'@,
                @'
    const uint32_t guideXosConservativeGcEnabled =
        GCConfig::GetConservativeGC() ? 1u : 0u;
    const uint32_t guideXosObjectIsFree =
        guideXosConservativeGcEnabled != 0u &&
        ((CObjectHeader*)o)->IsFree() ? 1u : 0u;
    guideXosNativeAotFirstRootPreMarkConservativeCheck(
        reinterpret_cast<uintptr_t>(o), guideXosConservativeGcEnabled,
        guideXosObjectIsFree);
    if (guideXosConservativeGcEnabled != 0u &&
        guideXosObjectIsFree != 0u)
'@)
            $promoteBody = $promoteBody.Replace(
                '    ((CObjectHeader*)o)->Validate();',
                @'
    guideXosNativeAotFirstRootPreMarkDebugValidationEntered(
        reinterpret_cast<uintptr_t>(o));
    ((CObjectHeader*)o)->Validate();
'@.TrimEnd())
            $promoteBody = $promoteBody.Replace(
                '    hpt->mark_object_simple (&o THREAD_NUMBER_ARG);',
                @'
    guideXosNativeAotFirstRootPreMarkBoundaryReached(
        reinterpret_cast<uintptr_t>(o), reinterpret_cast<uintptr_t>(hpt),
        static_cast<uint32_t>(flags),
        reinterpret_cast<uintptr_t>(&gc_heap::mark_object_simple));
    hpt->mark_object_simple(&o THREAD_NUMBER_ARG);
'@.TrimEnd())
            if ($promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkTrueBranchEntered' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkRootFlagTest' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkConservativeCheck' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationEntered' -or
                $promoteBody -notmatch 'guideXosNativeAotFirstRootPreMarkBoundaryReached') {
                throw "Locked GCHeap::Promote pre-mark injection did not match every required pre-mark boundary."
            }
            $gcWksInjected = $promotePrefix + $promoteBody

            $gcWksInjected = $gcWksInjected.Replace(
                '        MethodTable * pMT = GetMethodTable();',
                @'
        MethodTable * pMT = GetMethodTable();
        guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead(
            reinterpret_cast<uintptr_t>(this), reinterpret_cast<uintptr_t>(pMT));
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '            (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_NO_RANGE_CHECKS) == GCConfig::HEAPVERIFY_NO_RANGE_CHECKS;',
                @'
            (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_NO_RANGE_CHECKS) == GCConfig::HEAPVERIFY_NO_RANGE_CHECKS;
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '            fSmallObjectHeapPtr = g_theGCHeap->IsHeapPointer(this, TRUE);',
                @'
            fSmallObjectHeapPtr = g_theGCHeap->IsHeapPointer(this, TRUE);
            guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer(
                reinterpret_cast<uintptr_t>(this), 1u,
                fSmallObjectHeapPtr ? 1u : 0u);
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '                fLargeObjectHeapPtr = g_theGCHeap->IsHeapPointer(this);',
                @'
                fLargeObjectHeapPtr = g_theGCHeap->IsHeapPointer(this);
                guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer(
                    reinterpret_cast<uintptr_t>(this), 0u,
                    fLargeObjectHeapPtr ? 1u : 0u);
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '        if (bDeep && (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC))',
                @'
        const uint32_t guideXosVerifyHeapGc =
            (GCConfig::GetHeapVerifyLevel() & GCConfig::HEAPVERIFY_GC) != 0u ? 1u : 0u;
        if (bDeep && guideXosVerifyHeapGc != 0u)
'@.TrimEnd())
            $gcWksInjected = $gcWksInjected.Replace(
                '        if (fSmallObjectHeapPtr)',
                @'
        guideXosNativeAotFirstRootPreMarkDebugValidationCompleted(
            reinterpret_cast<uintptr_t>(this), noRangeChecks ? 1u : 0u,
            guideXosVerifyHeapGc, fSmallObjectHeapPtr ? 1u : 0u,
            fLargeObjectHeapPtr ? 1u : 0u);
        if (fSmallObjectHeapPtr)
'@.TrimEnd())
            if ($gcWksInjected -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead' -or
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer' -or
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootPreMarkDebugValidationCompleted') {
                throw "Locked CObjectHeader::Validate pre-mark instrumentation did not match all required source reads."
            }
            if ($isFirstRootFirstMarkMutation -or $isFirstRootPostQueueMarkDecision) {
                $markSimpleSignature = 'gc_heap::mark_object_simple (uint8_t** po THREAD_NUMBER_DCL)'
                $markSimpleOffset = $gcWksInjected.IndexOf($markSimpleSignature, [System.StringComparison]::Ordinal)
                if ($markSimpleOffset -lt 0) { throw "Locked mark_object_simple definition was not found for C011EC12 instrumentation." }
                $markSimpleBodyEnd = $gcWksInjected.IndexOf('inline', $markSimpleOffset, [System.StringComparison]::Ordinal)
                if ($markSimpleBodyEnd -lt 0) { throw "Locked mark_object_simple body end was not found for C011EC12 instrumentation." }
                $markSimplePrefix = $gcWksInjected.Substring(0, $markSimpleOffset)
                $markSimpleBody = $gcWksInjected.Substring($markSimpleOffset, $markSimpleBodyEnd - $markSimpleOffset)
                $markSimpleBody = $markSimpleBody.Replace(
                    '    uint8_t* o = *po;',
                    @'
    uint8_t* o = *po;
    guideXosNativeAotFirstRootMarkHelperEntered(
        reinterpret_cast<uintptr_t>(po), reinterpret_cast<uintptr_t>(o));
'@.TrimEnd())
                if ($markSimpleBody -notmatch 'guideXosNativeAotFirstRootMarkHelperEntered') {
                    throw "Locked mark_object_simple entry instrumentation did not match its root load."
                }
                $gcWksInjected = $markSimplePrefix + $markSimpleBody + $gcWksInjected.Substring($markSimpleBodyEnd)

                $queueSignature = 'uint8_t *mark_queue_t::queue_mark(uint8_t *o)'
                $queueOffset = $gcWksInjected.IndexOf($queueSignature, [System.StringComparison]::Ordinal)
                if ($queueOffset -lt 0) { throw "Locked queue_mark implementation was not found for C011EC12 instrumentation." }
                $queueEnd = $gcWksInjected.IndexOf('uint8_t *mark_queue_t::queue_mark(uint8_t *o, int condemned_gen)', $queueOffset, [System.StringComparison]::Ordinal)
                if ($queueEnd -lt 0) { throw "Locked queue_mark overload boundary was not found for C011EC12 instrumentation." }
                $queuePrefix = $gcWksInjected.Substring(0, $queueOffset)
                $queueBody = $gcWksInjected.Substring($queueOffset, $queueEnd - $queueOffset)
                $queueSequence = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // while the prefetch is taking effect, park our object in the queue
    // and fetch an object that has been sitting in the queue for a while
    // and where (hopefully) the memory is already in the cache
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    // this causes us to access the method table pointer of the old object
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
                $queueReplacement = if ($isFirstRootPostQueueMarkDecision) { @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // Proof-only C011EC13 source injection: preserve the locked queue_mark
    // sequence and observe the post-queue old_o decision before marked().
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    guideXosNativeAotFirstRootMarkWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootPostQueueDecisionRequested(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootPostQueueDecisionEntered(
        reinterpret_cast<uintptr_t>(old_o));
    if (old_o == nullptr)
    {
        guideXosNativeAotFirstRootPostQueueNullDecisionCompleted(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootPostQueueNonNullDecisionStarted(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootPostQueueMarkedRequested(
        reinterpret_cast<uintptr_t>(old_o));
    BOOL already_marked = marked (old_o);
    guideXosNativeAotFirstRootPostQueueMarkedCompleted(
        reinterpret_cast<uintptr_t>(old_o), already_marked ? 1u : 0u);
    if (already_marked)
    {
        guideXosNativeAotFirstRootPostQueueMarkedTrueBoundary(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary(
        reinterpret_cast<uintptr_t>(old_o));
    set_marked (old_o);
    return old_o;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
#endif //MARK_PHASE_PREFETCH
'@ } else { @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    guideXosNativeAotFirstRootMarkWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotFirstRootMarkWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
    guideXosNativeAotFirstRootMarkWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&old_o),
        reinterpret_cast<uintptr_t>(old_o),
        0u,
        0u,
        reinterpret_cast<uintptr_t>(&old_o),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootMarkWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&old_o),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        0u,
        0u,
        reinterpret_cast<uintptr_t>(&old_o),
        static_cast<uint32_t>(slot_count));
#endif //MARK_PHASE_PREFETCH
'@ }
                if (-not $queueBody.Contains($queueSequence)) { throw "Locked MARK_PHASE_PREFETCH queue_mark write sequence did not match C011EC12 instrumentation." }
                $queueBody = $queueBody.Replace($queueSequence, $queueReplacement.TrimEnd())
                if ($queueBody -notmatch 'guideXosNativeAotFirstRootMarkWorklistWriteBefore' -or
                    ($isFirstRootPostQueueMarkDecision -and $queueBody -notmatch 'guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted') -or
                    ($isFirstRootFirstMarkMutation -and $queueBody -notmatch 'guideXosNativeAotFirstRootMarkWorklistWriteCompleted') -or
                    ($isFirstRootPostQueueMarkDecision -and $queueBody -notmatch 'guideXosNativeAotFirstRootPostQueueDecisionRequested')) {
                    throw "Locked queue_mark post-queue instrumentation did not match the selected proof boundary."
                }
                $gcWksInjected = $queuePrefix + $queueBody + $gcWksInjected.Substring($queueEnd)
            }
        }
        if ($isNextGenuineRootProvider) {
            $queueSignature = 'uint8_t *mark_queue_t::queue_mark(uint8_t *o)'
            $queueOffset = $gcWksInjected.IndexOf($queueSignature, [System.StringComparison]::Ordinal)
            if ($queueOffset -lt 0) { throw "Locked queue_mark implementation was not found for C011EC15 instrumentation." }
            $queueEnd = $gcWksInjected.IndexOf('uint8_t *mark_queue_t::queue_mark(uint8_t *o, int condemned_gen)', $queueOffset, [System.StringComparison]::Ordinal)
            if ($queueEnd -lt 0) { throw "Locked queue_mark overload boundary was not found for C011EC15 instrumentation." }
            $queuePrefix = $gcWksInjected.Substring(0, $queueOffset)
            $queueBody = $gcWksInjected.Substring($queueOffset, $queueEnd - $queueOffset)
            $c15QueueSequence = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // while the prefetch is taking effect, park our object in the queue
    // and fetch an object that has been sitting in the queue for a while
    // and where (hopefully) the memory is already in the cache
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    // this causes us to access the method table pointer of the old object
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
            $c15QueueReplacement = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;
    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotC011EC15QueueMarkReturned(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table));
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
            if (-not $queueBody.Contains($c15QueueSequence)) { throw "Locked MARK_PHASE_PREFETCH queue_mark sequence did not match C011EC15 instrumentation." }
            $queueBody = $queueBody.Replace($c15QueueSequence, $c15QueueReplacement.TrimEnd())
            $gcWksInjected = $queuePrefix + $queueBody + $gcWksInjected.Substring($queueEnd)
        }
        if ($isC14QueueInstrumentation -and -not $isNextGenuineRootProvider) {
            $markSimpleSignature = 'gc_heap::mark_object_simple (uint8_t** po THREAD_NUMBER_DCL)'
            $markSimpleOffset = $gcWksInjected.IndexOf($markSimpleSignature, [System.StringComparison]::Ordinal)
            if ($markSimpleOffset -lt 0) { throw "Locked mark_object_simple definition was not found for C011EC14 instrumentation." }
            $markSimpleBodyEnd = $gcWksInjected.IndexOf('inline', $markSimpleOffset, [System.StringComparison]::Ordinal)
            if ($markSimpleBodyEnd -lt 0) { throw "Locked mark_object_simple body end was not found for C011EC14 instrumentation." }
            $markSimplePrefix = $gcWksInjected.Substring(0, $markSimpleOffset)
            $markSimpleBody = $gcWksInjected.Substring($markSimpleOffset, $markSimpleBodyEnd - $markSimpleOffset)
            $markSimpleBody = $markSimpleBody.Replace(
                '    uint8_t* o = *po;',
                @'
    uint8_t* o = *po;
    guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered(
        reinterpret_cast<uintptr_t>(po), reinterpret_cast<uintptr_t>(o));
'@.TrimEnd())
            $gcWksInjected = $markSimplePrefix + $markSimpleBody + $gcWksInjected.Substring($markSimpleBodyEnd)

            $queueSignature = 'uint8_t *mark_queue_t::queue_mark(uint8_t *o)'
            $queueOffset = $gcWksInjected.IndexOf($queueSignature, [System.StringComparison]::Ordinal)
            if ($queueOffset -lt 0) { throw "Locked queue_mark implementation was not found for C011EC14 instrumentation." }
            $queueEnd = $gcWksInjected.IndexOf('uint8_t *mark_queue_t::queue_mark(uint8_t *o, int condemned_gen)', $queueOffset, [System.StringComparison]::Ordinal)
            if ($queueEnd -lt 0) { throw "Locked queue_mark overload boundary was not found for C011EC14 instrumentation." }
            $queuePrefix = $gcWksInjected.Substring(0, $queueOffset)
            $queueBody = $gcWksInjected.Substring($queueOffset, $queueEnd - $queueOffset)
            $c14QueueSequence = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);

    // while the prefetch is taking effect, park our object in the queue
    // and fetch an object that has been sitting in the queue for a while
    // and where (hopefully) the memory is already in the cache
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    slot_table[slot_index] = o;

    curr_slot_index = (slot_index + 1) % slot_count;
    if (old_o == nullptr)
        return nullptr;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
#endif //MARK_PHASE_PREFETCH

    // this causes us to access the method table pointer of the old object
    BOOL already_marked = marked (old_o);
    if (already_marked)
    {
        return nullptr;
    }
    set_marked (old_o);
    return old_o;
'@
            $c14QueueReplacement = @'
#ifdef MARK_PHASE_PREFETCH
    Prefetch (o);
    size_t slot_index = curr_slot_index;
    uint8_t* old_o = slot_table[slot_index];
    guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    slot_table[slot_index] = o;
    curr_slot_index = (slot_index + 1) % slot_count;
    guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootNonNullOldODecisionRequested(
        reinterpret_cast<uintptr_t>(o),
        reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
        reinterpret_cast<uintptr_t>(old_o),
        reinterpret_cast<uintptr_t>(o),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(slot_index),
        static_cast<uintptr_t>(curr_slot_index),
        reinterpret_cast<uintptr_t>(slot_table),
        static_cast<uint32_t>(slot_count));
    guideXosNativeAotFirstRootNonNullOldODecisionEntered(
        reinterpret_cast<uintptr_t>(old_o));
    if (old_o == nullptr)
    {
        guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted(
            reinterpret_cast<uintptr_t>(old_o));
        C011EC15_QUEUE_MARK_RETURN_HOOK
        return nullptr;
    }
    guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootNonNullOldOMarkedRequested(
        reinterpret_cast<uintptr_t>(old_o));
    BOOL already_marked = marked (old_o);
    guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(
        reinterpret_cast<uintptr_t>(old_o), already_marked ? 1u : 0u);
    if (already_marked)
    {
        guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(
        reinterpret_cast<uintptr_t>(old_o));
    set_marked (old_o);
    return old_o;
#else //MARK_PHASE_PREFETCH
    uint8_t* old_o = o;
    guideXosNativeAotFirstRootNonNullOldODecisionEntered(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(
        reinterpret_cast<uintptr_t>(old_o));
    guideXosNativeAotFirstRootNonNullOldOMarkedRequested(
        reinterpret_cast<uintptr_t>(old_o));
    BOOL already_marked = marked (old_o);
    guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(
        reinterpret_cast<uintptr_t>(old_o), already_marked ? 1u : 0u);
    if (already_marked)
    {
        guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(
            reinterpret_cast<uintptr_t>(old_o));
        return nullptr;
    }
    guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(
        reinterpret_cast<uintptr_t>(old_o));
    set_marked (old_o);
    return old_o;
#endif //MARK_PHASE_PREFETCH
'@
            if ($isNextGenuineRootProvider) {
                $c14QueueReplacement = $c14QueueReplacement.Replace(
                    '        C011EC15_QUEUE_MARK_RETURN_HOOK',
                    @'
        guideXosNativeAotC011EC15QueueMarkReturned(
            reinterpret_cast<uintptr_t>(o),
            reinterpret_cast<uintptr_t>(&slot_table[slot_index]),
            reinterpret_cast<uintptr_t>(old_o),
            reinterpret_cast<uintptr_t>(o),
            static_cast<uintptr_t>(slot_index),
            static_cast<uintptr_t>(slot_index),
            static_cast<uintptr_t>(curr_slot_index),
            reinterpret_cast<uintptr_t>(slot_table));
'@.TrimEnd())
            } else {
                $c14QueueReplacement = $c14QueueReplacement.Replace('        C011EC15_QUEUE_MARK_RETURN_HOOK' + [Environment]::NewLine, '')
            }
            if (-not $queueBody.Contains($c14QueueSequence)) { throw "Locked MARK_PHASE_PREFETCH queue_mark write sequence did not match C011EC14 instrumentation." }
            $queueBody = $queueBody.Replace($c14QueueSequence, $c14QueueReplacement.TrimEnd())
            $gcWksInjected = $queuePrefix + $queueBody + $gcWksInjected.Substring($queueEnd)

            $markReadSequence = @'
    BOOL IsMarked() const
    {
        return !!(((size_t)RawGetMethodTable()) & GC_MARKED);
    }
'@
            $markReadReplacement = @'
    BOOL IsMarked() const
    {
        const uintptr_t guideXosRawHeader =
            reinterpret_cast<uintptr_t>(RawGetMethodTable());
        guideXosNativeAotFirstRootNonNullOldORawMarkWordRead(
            reinterpret_cast<uintptr_t>(this),
            reinterpret_cast<uintptr_t>(this),
            guideXosRawHeader,
            static_cast<uintptr_t>(GC_MARKED));
        return !!(guideXosRawHeader & GC_MARKED);
    }
'@
            if (-not $gcWksInjected.Contains($markReadSequence)) { throw "Locked CObjectHeader::IsMarked source did not match C011EC14 instrumentation." }
            $gcWksInjected = $gcWksInjected.Replace($markReadSequence, $markReadReplacement.TrimEnd())
        }
        if ($gcWksInjected -eq $gcCppText -or
            (($isNextGenuineRootProvider -and $gcWksInjected -notmatch 'guideXosNativeAotC011EC15PromoteEntered') -or
             ($isC14QueueInstrumentation -and -not $isNextGenuineRootProvider -and $gcWksInjected -notmatch 'guideXosNativeAotFirstRootNonNullOldOCallbackEntered') -or
             (-not $isC14QueueInstrumentation -and $gcWksInjected -notmatch 'guideXosNativeAotFirstRootCallbackEntered')) -or
            $gcWksInjected -notmatch [regex]::Escape($candidateLoadedFunction) -or
            $gcWksInjected -notmatch 'reinterpret_cast<uintptr_t>\(o\)' -or
            ([regex]::Matches($gcWksInjected, ([regex]::Escape($candidateLoadedFunction) + '\(reinterpret_cast<uintptr_t>\(o\)\)')).Count -ne 1) -or
            ($isFirstRootMembershipClassification -and $gcWksInjected -notmatch 'guideXosNativeAotFirstRootMembershipResultBoundary\(1u\)') -or
            ($isFirstRootHeapResolutionOrCondemned -and (
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootHeapResolutionRequested' -or
                $gcWksInjected -notmatch 'guideXosNativeAotFirstRootHeapResolutionCompleted'))) {
            throw "Locked gc.cpp callback-entry injection did not match GCHeap::Promote and its first candidate load."
        }
        $gcWksInjected = $gcWksText.Replace('#include "gc.cpp"', $gcWksInjected)
        if ($gcWksInjected -eq $gcWksText -or $gcWksInjected -notmatch 'namespace WKS') {
            throw "Locked gcwks.cpp did not expose the Workstation GC source include for callback-entry replacement."
        }
        Set-Content -LiteralPath $gcWksSource -Value $gcWksInjected -Encoding ASCII
    }
    if ($isCandidateLoadEnumeration) {
        $lockedGcEnumPath = Join-Path $lockedSourceRoot "src\coreclr\nativeaot\Runtime\GcEnum.cpp"
        Require-File $lockedGcEnumPath "Locked NativeAOT GcEnum source"
        $gcEnumText = Get-Content -LiteralPath $lockedGcEnumPath -Raw
        if ($isNextGenuineRootProvider) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotC011EC15CandidateObserved(uintptr_t slot, uintptr_t rawValue, uint32_t flags, uintptr_t callback, uintptr_t context);',
                'extern "C" void __cdecl guideXosNativeAotC011EC15EnumGcRefReturned(uintptr_t slot, uintptr_t rawValue, uintptr_t callback, uintptr_t context);'
            ) -join [Environment]::NewLine
        } elseif ($isFirstRootFirstNonNullOldO) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" uint32_t __cdecl guideXosNativeAotFirstRootNonNullOldOCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);',
                'extern "C" void __cdecl guideXosNativeAotFirstRootCallbackCallSiteEntered(uintptr_t slot, uintptr_t rawValue, uint32_t flags, uintptr_t callback, uintptr_t scanContext);'
            ) -join [Environment]::NewLine
        } elseif ($isFirstRootCallbackEntry) {
            $gcEnumDeclaration = @(
                'extern "C" void __cdecl guideXosNativeAotFirstNonNullRootCandidateLoadRequested(uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext);',
                'extern "C" uint32_t __cdecl guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded(uintptr_t slot, uintptr_t rawValue);',
                'extern "C" void __cdecl guideXosNativeAotFirstRootCallbackCallSiteEntered(uintptr_t slot, uintptr_t rawValue, uint32_t flags, uintptr_t callback, uintptr_t scanContext);'
            ) -join [Environment]::NewLine
        } elseif ($isFirstNonNullRoot) {
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
        if ($isNextGenuineRootProvider) {
            $gcEnumText = $gcEnumText.Replace('#include "GcEnum.h"', '#include "GcEnum.h"' + [Environment]::NewLine + [Environment]::NewLine + $gcEnumDeclaration.TrimEnd())
            $gcEnumPattern = '(?ms)^static void GcEnumObject\(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc\* fnGcEnumRef, ScanContext\* pSc\)\r?\n\{.*?\r?\n\}\r?\n\r?\n(?=void EnumGcRef)'
            $gcEnumReplacement = @'
static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)
{
    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);
    guideXosNativeAotC011EC15CandidateObserved(
        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue, flags,
        reinterpret_cast<uintptr_t>(fnGcEnumRef),
        reinterpret_cast<uintptr_t>(pSc));
    if (flags & GC_CALL_INTERIOR)
        PromoteCarefully(ppObj, flags, fnGcEnumRef, pSc);
    else
        fnGcEnumRef(ppObj, pSc, flags);
    guideXosNativeAotC011EC15EnumGcRefReturned(
        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue,
        reinterpret_cast<uintptr_t>(fnGcEnumRef),
        reinterpret_cast<uintptr_t>(pSc));
}
'@
            $gcEnumInjected = [regex]::Replace($gcEnumText, $gcEnumPattern, $gcEnumReplacement.TrimEnd() + [Environment]::NewLine + [Environment]::NewLine, 1)
            $gcEnumInjected = $gcEnumInjected.Replace(
                '    GcEnumObject(pRef, flags, fnGcEnumRef, pSc);',
                '    GcEnumObject(pRef, flags, fnGcEnumRef, pSc);' + [Environment]::NewLine + '    guideXosNativeAotC011EC15EnumGcRefReturned(0u, 0u, 0u, 0u);')
            $gcEnumInjectionValid =
                $gcEnumInjected -ne $gcEnumText -and
                $gcEnumInjected -match 'guideXosNativeAotC011EC15CandidateObserved' -and
                $gcEnumInjected -match 'guideXosNativeAotC011EC15EnumGcRefReturned'
        } elseif ($isFirstRootFirstNonNullOldO) {
            # Keep GcEnum.cpp byte-for-byte source behavior for C011EC14. The
            # proof boundary is in GCHeap::Promote and queue_mark; this source
            # is compiled into the fresh pack without changing callback flow.
            $gcEnumInjected = $gcEnumText
            $gcEnumInjectionValid = $true
        } else {
            $gcEnumText = $gcEnumText.Replace('#include "GcEnum.h"', '#include "GcEnum.h"' + [Environment]::NewLine + [Environment]::NewLine + $gcEnumDeclaration.TrimEnd())
            $gcEnumPattern = '(?m)^static void GcEnumObject\(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc\* fnGcEnumRef, ScanContext\* pSc\)\r?\n\{'
        if ($isFirstNonNullRoot -or $isFirstRootCallbackEntry) {
            $candidateRequestFunction = if ($isFirstRootFirstNonNullOldO) { 'guideXosNativeAotFirstRootNonNullOldOCandidateLoadRequested' } else { 'guideXosNativeAotFirstNonNullRootCandidateLoadRequested' }
            $candidateWordFunction = if ($isFirstRootFirstNonNullOldO) { 'guideXosNativeAotFirstRootNonNullOldOCandidateMachineWordLoaded' } else { 'guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded' }
            if ($isFirstRootFirstNonNullOldO) {
                $gcEnumReplacement = @(
                    'static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)',
                    '{',
                    "    $candidateRequestFunction(",
                    '        reinterpret_cast<uintptr_t>(ppObj), flags,',
                    '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                    '        reinterpret_cast<uintptr_t>(pSc));',
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                    "    (void)$candidateWordFunction(",
                    '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue);'
                ) -join [Environment]::NewLine
            } else {
                $gcEnumReplacement = @(
                    'static void GcEnumObject(PTR_PTR_Object ppObj, uint32_t flags, ScanFunc* fnGcEnumRef, ScanContext* pSc)',
                    '{',
                    "    $candidateRequestFunction(",
                    '        reinterpret_cast<uintptr_t>(ppObj), flags,',
                    '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                    '        reinterpret_cast<uintptr_t>(pSc));',
                    '    const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);',
                    "    if ($candidateWordFunction(",
                    '            reinterpret_cast<uintptr_t>(ppObj), candidateRawValue) == 0u)',
                    '        return;'
                ) -join [Environment]::NewLine
                if ($isFirstRootCallbackEntry) {
                    $gcEnumReplacement += [Environment]::NewLine + ((@(
                        '    guideXosNativeAotFirstRootCallbackCallSiteEntered(',
                        '        reinterpret_cast<uintptr_t>(ppObj), candidateRawValue, flags,',
                        '        reinterpret_cast<uintptr_t>(fnGcEnumRef),',
                        '        reinterpret_cast<uintptr_t>(pSc));'
                    ) -join [Environment]::NewLine))
                }
            }
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
            $gcEnumInjectionValid =
                $gcEnumInjected -ne $gcEnumText -and
                $gcEnumInjected -match 'const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>\(\*ppObj\);'
            if ($isFirstNonNullRoot -or $isFirstRootCallbackEntry) {
                $gcEnumInjectionValid = $gcEnumInjectionValid -and
                    $gcEnumInjected -match [regex]::Escape($candidateWordFunction)
            } else {
                $gcEnumInjectionValid = $gcEnumInjectionValid -and
                    $gcEnumInjected -match 'guideXosNativeAotFirstRootCandidateMachineWordLoaded'
            }
            if (-not $gcEnumInjectionValid) {
                throw "Locked GcEnum.cpp first-root-candidate-load injection did not match the GcEnumObject load boundary."
            }
        }
        Set-Content -LiteralPath $gcEnumSource -Value $gcEnumInjected -Encoding ASCII
    }
    $baselineDescription = if ($isNextGenuineRootProvider) {
        "experiment=single-managed-mutator Workstation GC first genuine root callback returned and root enumeration stopped at the next genuine non-null provider candidate"
    } elseif ($isFirstRootPostQueueMarkDecision) {
        "experiment=single-managed-mutator Workstation GC genuine thread-static storage root completed the first queue_mark post-queue old_o/null/mark-state decision"
    } elseif ($isFirstRootFirstMarkMutation) {
        "experiment=single-managed-mutator Workstation GC genuine thread-static storage root entered mark_object_simple and stopped after the smallest safe queue_mark mutation unit"
    } elseif ($isFirstRootPreMarkBoundary) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and source-required condemned=true pre-mark path stopped before mark_object_simple"
    } elseif ($isFirstRootCondemnedGenerationDecision) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and exactly one source-valid is_in_condemned_gc(o) decision"
    } elseif ($isFirstRootHeapResolution) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and exactly one source-valid HEAP_FROM_THREAD/heap_of(o) heap-resolution transition"
    } elseif ($isFirstRootMembershipClassification) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and exactly one managed-range membership classification"
    } elseif ($isFirstRootCallbackEntry) {
        "experiment=single-managed-mutator Workstation GC real thread-static storage root and first GCHeap::Promote callback entry"
    } elseif ($isFirstNonNullRoot) {
        "experiment=single-managed-mutator Workstation GC real thread-static proof root and first non-null candidate callback boundary"
    } elseif ($isFirstPerThreadRootProvider) {
        "experiment=single-managed-mutator Workstation GC real FOREACH_THREAD enumeration and first per-thread root provider entry"
    } elseif ($isAllocationContextFixupRootBoundary) {
        "experiment=single-managed-mutator Workstation GC fix_allocation_contexts(TRUE) completion and first root-dispatch boundary"
    } else {
        "experiment=single-managed-mutator Workstation GC SuspendEE completion and post-DisablePreemptiveGC boundary"
    }
    $safeStopDescription = if ($isNextGenuineRootProvider) {
        "safeStop=after the first root callback returned and the next genuine non-null Object** candidate/provider was captured, before its Promote callback or any second queue mutation"
    } elseif ($isFirstRootPostQueueMarkDecision) {
        "safeStop=after the first queue_mark old_o == nullptr decision and before marked(old_o), or after the read-only marked decision, before any subsequent mutation"
    } elseif ($isFirstRootFirstMarkMutation) {
        "safeStop=after queue_mark slot_table[slot_index]=o and curr_slot_index advancement, before old_o mark-state read"
    } elseif ($isFirstRootPreMarkBoundary) {
        "safeStop=after all source-required non-mutating GCHeap::Promote true-branch logic and immediately before hpt->mark_object_simple"
    } elseif ($isFirstRootCondemnedGenerationDecision) {
        "safeStop=after exactly one real gc_heap::is_in_condemned_gc(o) decision and before the GCHeap::Promote true/false continuation"
    } elseif ($isFirstRootHeapResolution) {
        "safeStop=after exactly one real gc_heap::heap_of(o) result and immediately before gc_heap::is_in_condemned_gc(o) at locked gc.cpp:49499"
    } elseif ($isFirstRootMembershipClassification) {
        "safeStop=after exactly one real gc_heap::is_in_find_object_range boolean result returned to GCHeap::Promote and immediately before HEAP_FROM_THREAD at locked gc.cpp:49494"
    } elseif ($isFirstRootCallbackEntry) {
        "safeStop=after the real GCHeap::Promote callback entered with live ABI arguments and after its required *ppObject load, immediately before gc_heap::is_in_find_object_range"
    } elseif ($isFirstPerThreadRootProvider) {
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
    if ($isFirstRootCallbackEntry) {
        $runtimeBatText = Get-Content -LiteralPath $runtimeBat -Raw
        $gcWksCompileLine = 'cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DLPVOID=void* /DGUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION /DGUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /I"{0}" /I"{1}" /I"{1}\Runtime" /I"{1}\Runtime\windows" /I"{2}" /I"{2}\native" /I"{2}\gc" /I"{2}\gc\env" /I"{1}\Runtime\inc" /I"{1}\Runtime\eventpipe" /I"{3}" /I"{4}" /I"{2}\pal\src\include" /FI"{2}\gc\env\common.h" /Fo:"{5}" "{6}"' -f $nativeAotRoot, $sourceRoot, $sourceRoot, (Join-Path $root 'tools\dotnet\runtime-pack\src\platform'), $palSourceRoot, $gcWks, $gcWksSource
        if ($isFirstRootMembershipClassification) {
            $gcWksCompileLine = $gcWksCompileLine.Replace(
                '/DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION ',
                '/DGUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION /DGUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION ')
        }
        $runtimeBatText = $runtimeBatText.Replace("exit /b 0", "$gcWksCompileLine`r`nif errorlevel 1 exit /b %errorlevel%`r`nexit /b 0")
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
    $managedProofMode = if ($isFirstRootFirstNonNullOldO) { "FirstNonNullOldO" } elseif ($isFirstNonNullRoot -or $isFirstRootCallbackEntry) { "FirstNonNullRoot" } else { "FirstCollectionBoundary" }
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
    $gcWksArchiveArgs = if ($isFirstRootCallbackEntry) {
        "/REMOVE:`"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\gcwks.cpp.obj`" `"$gcWks`""
    } else {
        ""
    }
    $archiveBat = Write-Batch "build-single-thread-suspend-ee-gc-archive.bat" @"
@echo off
setlocal
call "$vsBat" >nul
if errorlevel 1 exit /b %errorlevel%
 lib.exe /nologo /OUT:"$adaptedArchive" "$activeArchive" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\PalRedhawkMinWin.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\thread.cpp.obj')" /REMOVE:"$(Join-Path $root 'out\dotnet\pal-runtime-active-replacement-build\guidexos_nativeaot_pal_contract.obj')" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\windows\gcenv.windows.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\gcenv.ee.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\amd64\AllocFast.asm.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj" "$palBridge" "$palStartup" "$(Join-Path $runtimeRoot 'guidexos_gcenv.single-thread-suspend-ee.obj')" "$gcEnvEe" "$gcBridgeBoundary" "$platformContract" "$threadObj" "$ehObj" "$allocFastObj" "$probeObj" $gcWksArchiveArgs $gcEnumArchiveArgs
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
        if ($isFirstRootCallbackEntry) { $stalePaths += $gcWks }
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
    if ($isFirstRootCallbackEntry) { $requiredBuildOutputs += $gcWks }
    foreach ($path in $requiredBuildOutputs) { Require-File $path "Single-thread SuspendEE build output" }
    Invoke-LoggedCommand $python @($converter,$pePath,$elfPath,"--map",$mapPath,"--symbol","ManagedMain") (Join-Path $runRoot "pe-to-elf.log")
    Require-File $elfPath "Single-thread SuspendEE ELF"
    Invoke-LoggedCommand $objdump @("-p",$pePath) (Join-Path $runRoot "pe-imports.txt")
    Invoke-LoggedCommand $readelf @("-h","-l","-S","-r","-s","-d",$elfPath) (Join-Path $runRoot "elf-inspection.txt")
    $imports = Get-Content -LiteralPath (Join-Path $runRoot "pe-imports.txt") -Raw
    if ($imports -match 'FlsGetValue|FlsSetValue') { throw "Single-thread SuspendEE PE still exposes live Windows FLS imports." }
    $mapText = Get-Content -LiteralPath $mapPath -Raw
    $callbackSymbolName = '?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z'
    $callbackAddressText = if ($isFirstRootCallbackEntry) { Extract-MapAddress $mapText $callbackSymbolName } else { $null }
    $markHelperSymbolName = '?mark_object_simple@gc_heap@WKS@@CAXPEAPEAE@Z'
    $markHelperAddressText = if ($isFirstRootPreMarkBoundary) { Extract-MapAddress $mapText $markHelperSymbolName } else { $null }
    if ($isFirstRootCallbackEntry) {
        Invoke-LoggedCommand $objdump @('-d','-Mintel',$pePath) (Join-Path $runRoot 'artifact-disassembly.txt')
        if ($false) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' "C011EC14 first non-null old_o marker"
            $nonNullOldOLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($nonNullOldOLine)) { throw "C011EC14 marker line was not isolated in $name." }
            Assert-Text $nonNullOldOLine 'queueInsertions=00000011 queueWrites=00000011 queueHistoryOverflow=00000000' "17 real queue insertions"
            Assert-Text $nonNullOldOLine 'nullDecisions=00000010 nonNullDecisions=00000001 decisionRequests=00000011 decisionEntries=00000011' "16 null decisions followed by the first non-null decision"
            Assert-Text $nonNullOldOLine 'markedRequests=00000001 markedEntries=00000001 markedReturns=00000001 markStateReads=00000001 rawMarkWordReads=00000001' "one actual marked(old_o) read and return"
            Assert-Text $nonNullOldOLine 'newMutationAttempts=00000000 newMutationExecutions=00000000 markBitWrites=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "stopped before set_marked, traversal, or a second object"
            Assert-Text $nonNullOldOLine 'restart=00000000 resume=00000000' "no RestartEE or managed resume"
            Assert-Text $nonNullOldOLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "suspended single-mutator invariants"
            Assert-Text $nonNullOldOLine 'sentinelFailures=00000000 objectValidationBeforeFixup=00000000 objectValidationAfterFixup=00000000 duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "real-object and sentinel validation"
            $oldRootSlot = Get-MarkerField $nonNullOldOLine 'rootSlot'
            $oldRawRoot = Get-MarkerField $nonNullOldOLine 'rawRoot'
            $oldStorage = Get-MarkerField $nonNullOldOLine 'storageObject'
            $oldSentinel = Get-MarkerField $nonNullOldOLine 'sentinel'
            $oldObject = Get-MarkerField $nonNullOldOLine 'old_o'
            $oldSlot = Get-MarkerField $nonNullOldOLine 'slotOld'
            $newSlot = Get-MarkerField $nonNullOldOLine 'slotNew'
            if ($oldRootSlot -eq '0x0000000000000000' -or $oldRawRoot -eq '0x0000000000000000' -or $oldStorage -eq '0x0000000000000000' -or $oldSentinel -eq '0x0000000000000000') { throw "C011EC14 did not record non-null managed root identity in $name." }
            if ($oldObject -eq '0x0000000000000000' -or $oldObject -ne $oldSlot -or $newSlot -eq '0x0000000000000000' -or $oldObject -eq $oldSentinel) { throw "C011EC14 selected old_o identity was not a real managed object in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'slotIndex') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorBefore') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorAfter') -ne '0x0000000000000001') { throw "C011EC14 did not capture the ring wrap at slot zero in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'provenanceValid') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'findRange') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'heapMembership') -ne '0x00000001') { throw "C011EC14 old_o provenance did not resolve to the real object history/range in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC14"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=$oldRootSlot;rawValue=$oldRawRoot;storageObject=$oldStorage;sentinel=$oldSentinel}
                queue=[ordered]@{insertions=(Get-MarkerField $nonNullOldOLine 'queueInsertions');writes=(Get-MarkerField $nonNullOldOLine 'queueWrites');slotAddress=(Get-MarkerField $nonNullOldOLine 'selectedSlot');slotIndex=(Get-MarkerField $nonNullOldOLine 'slotIndex');oldSlot=$oldSlot;newSlot=$newSlot;oldObject=$oldObject;cursorBefore=(Get-MarkerField $nonNullOldOLine 'cursorBefore');cursorAfter=(Get-MarkerField $nonNullOldOLine 'cursorAfter');base=(Get-MarkerField $nonNullOldOLine 'queueBase')}
                decision=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'decisionRequests');entries=(Get-MarkerField $nonNullOldOLine 'decisionEntries');nullDecisions=(Get-MarkerField $nonNullOldOLine 'nullDecisions');nonNullDecisions=(Get-MarkerField $nonNullOldOLine 'nonNullDecisions');oldObject=$oldObject;returnAddress=(Get-MarkerField $nonNullOldOLine 'decisionReturnAddress')}
                markState=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'markedRequests');entries=(Get-MarkerField $nonNullOldOLine 'markedEntries');returns=(Get-MarkerField $nonNullOldOLine 'markedReturns');reads=(Get-MarkerField $nonNullOldOLine 'markStateReads');result=(Get-MarkerField $nonNullOldOLine 'markedResult');rawReads=(Get-MarkerField $nonNullOldOLine 'rawMarkWordReads');rawHeader=(Get-MarkerField $nonNullOldOLine 'rawHeader');mask=(Get-MarkerField $nonNullOldOLine 'markMask')}
                provenance=[ordered]@{valid=(Get-MarkerField $nonNullOldOLine 'provenanceValid');findRange=(Get-MarkerField $nonNullOldOLine 'findRange');heapMembership=(Get-MarkerField $nonNullOldOLine 'heapMembership');generation=(Get-MarkerField $nonNullOldOLine 'generation');objectHistoryIndex=(Get-MarkerField $nonNullOldOLine 'objectHistoryIndex')}
                counters=[ordered]@{callbacks=(Get-MarkerField $nonNullOldOLine 'callbacks');candidateLoads=(Get-MarkerField $nonNullOldOLine 'candidateLoads');markHelpers=(Get-MarkerField $nonNullOldOLine 'markHelpers');callbackReturnsBeforeDecision=(Get-MarkerField $nonNullOldOLine 'callbackReturnsBeforeDecision');callbackReturns=(Get-MarkerField $nonNullOldOLine 'callbackReturns');restart=(Get-MarkerField $nonNullOldOLine 'restart');resume=(Get-MarkerField $nonNullOldOLine 'resume')}
                mutation=[ordered]@{newAttempts=(Get-MarkerField $nonNullOldOLine 'newMutationAttempts');newExecutions=(Get-MarkerField $nonNullOldOLine 'newMutationExecutions');markBitWrites=(Get-MarkerField $nonNullOldOLine 'markBitWrites')}
                traversal=[ordered]@{graph=(Get-MarkerField $nonNullOldOLine 'graphTraversal');childReferenceReads=(Get-MarkerField $nonNullOldOLine 'childReferenceReads');childObjects=(Get-MarkerField $nonNullOldOLine 'childObjectsDiscovered');secondObjectMarkAttempts=(Get-MarkerField $nonNullOldOLine 'secondObjectMarkAttempts')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $nonNullOldOLine 'lockHeld');eeSuspended=(Get-MarkerField $nonNullOldOLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $nonNullOldOLine 'managedEntryProhibited');restart=(Get-MarkerField $nonNullOldOLine 'restart');resume=(Get-MarkerField $nonNullOldOLine 'resume')}
                validation=[ordered]@{sentinelFailures=(Get-MarkerField $nonNullOldOLine 'sentinelFailures');objectHistoryOverflow=(Get-MarkerField $nonNullOldOLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootPostQueueMarkDecision) {
            $disassemblyText = Get-Content -LiteralPath (Join-Path $runRoot 'artifact-disassembly.txt') -Raw
            $markHelperNumeric = [Convert]::ToUInt64($markHelperAddressText, 16)
            $markHelperHex = $markHelperNumeric.ToString('x')
            $helperStartMatch = [regex]::Match($disassemblyText, '(?im)^\s*' + [regex]::Escape($markHelperHex) + ':')
            $helperStart = if ($helperStartMatch.Success) { $helperStartMatch.Index } else { -1 }
            if ($helperStart -lt 0) { throw "Disassembly did not contain the linked mark_object_simple helper for C011EC13." }
            $helperTail = $disassemblyText.Substring($helperStart)
            $helperLines = $helperTail -split "`r?`n"
            $firstMutationLine = $helperLines | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[(?!rsp|rip)[^]]+\],\w+\s*$' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($firstMutationLine)) { throw "C011EC13 disassembly did not expose the inherited queue slot write." }
            $firstMutationAddress = ([regex]::Match($firstMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $firstMutationIndex = [Array]::IndexOf($helperLines, $firstMutationLine)
            $cursorLine = $helperLines[($firstMutationIndex + 1)..($helperLines.Count - 1)] | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[rip\+[^]]+\],\w+' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($cursorLine)) { throw "C011EC13 disassembly did not expose the inherited queue cursor write." }
            $cursorAddress = ([regex]::Match($cursorLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $cursorIndex = [Array]::IndexOf($helperLines, $cursorLine)
            if ($cursorIndex -lt 0) { throw "C011EC13 cursor write was not located inside mark_object_simple disassembly." }
            $nullHelperAddressText = Extract-MapAddress $mapText 'guideXosNativeAotFirstRootPostQueueNullDecisionCompleted'
            $nullHelperHex = ([Convert]::ToUInt64($nullHelperAddressText, 16)).ToString('x')
            $nullCallLine = $null
            if ($cursorIndex -lt ($helperLines.Count - 1)) {
                $nullCallLine = $helperLines[($cursorIndex + 1)..($helperLines.Count - 1)] | Where-Object { $_ -match '(?i)\bcall\s+0x0*' + [regex]::Escape($nullHelperHex) + '\b' } | Select-Object -First 1
            }
            if ([string]::IsNullOrWhiteSpace($nullCallLine)) { throw "C011EC13 disassembly did not expose the null-decision safe-stop call." }
            $nullCallAddress = ([regex]::Match($nullCallLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $nullCallIndex = [Array]::IndexOf($helperLines, $nullCallLine)
            if ($nullCallIndex -lt 0) { throw "C011EC13 null-decision call was not located inside mark_object_simple disassembly." }
            $nullTargetIndex = $nullCallIndex - 1
            while ($nullTargetIndex -ge 0 -and $helperLines[$nullTargetIndex] -notmatch '(?i)\bxor\s+ecx,ecx\b') { $nullTargetIndex-- }
            if ($nullTargetIndex -lt 0) { throw "C011EC13 disassembly did not expose the null-decision continuation target." }
            $nullBranchTargetText = ([regex]::Match($helperLines[$nullTargetIndex], '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $branchLine = $helperLines[($cursorIndex + 1)..($nullTargetIndex - 1)] | Where-Object { $_ -match '(?i)\b(?:je|jz|jne|jnz)\s+0x0*' + [regex]::Escape($nullBranchTargetText) + '\b' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($branchLine)) { throw "C011EC13 disassembly did not expose the old_o null branch target." }
            $branchIndex = [Array]::IndexOf($helperLines, $branchLine)
            $testLookbackStart = [Math]::Max(0, $branchIndex - 12)
            $testLookbackEnd = [Math]::Max(0, $branchIndex - 1)
            $nullTestLine = @($helperLines[$testLookbackStart..$testLookbackEnd]) | Where-Object { $_ -match '(?i)\b(?:test|cmp)\b' } | Select-Object -Last 1
            $falseHelperAddressText = Extract-MapAddress $mapText 'guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary'
            $falseHelperHex = ([Convert]::ToUInt64($falseHelperAddressText, 16)).ToString('x')
            $nextMutationLine = $null
            if ($branchIndex -lt ($nullTargetIndex - 1)) {
                $falseCallLine = $helperLines[($branchIndex + 1)..($nullTargetIndex - 1)] | Where-Object { $_ -match '(?i)\bcall\s+0x0*' + [regex]::Escape($falseHelperHex) + '\b' } | Select-Object -First 1
            }
            if (-not [string]::IsNullOrWhiteSpace($falseCallLine)) {
                $falseCallIndex = [Array]::IndexOf($helperLines, $falseCallLine)
                if ($falseCallIndex -ge 0) {
                    $nextMutationLine = $helperLines[($falseCallIndex + 1)..[Math]::Min($helperLines.Count - 1, $falseCallIndex + 20)] | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+(?:or\s+QWORD PTR\s+\[(?!rsp|rip)[^]]+\],0x1\s*$)' } | Select-Object -First 1
                }
            }
            $nextMutationAddress = if ($nextMutationLine) { ([regex]::Match($nextMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value) } else { $null }
            Set-Content -LiteralPath (Join-Path $runRoot 'post-queue-machine-code.txt') -Value @(
                "markHelperSymbol=$markHelperSymbolName",
                "markHelperAddress=0x$markHelperAddressText",
                "inheritedQueueSlotWriteInstruction=0x$($firstMutationAddress.ToUpperInvariant())",
                "inheritedQueueSlotWriteLine=$firstMutationLine",
                "inheritedCursorWriteInstruction=0x$($cursorAddress.ToUpperInvariant())",
                "inheritedCursorWriteLine=$cursorLine",
                "nullDecisionHelper=guideXosNativeAotFirstRootPostQueueNullDecisionCompleted",
                "nullDecisionHelperAddress=0x$nullHelperAddressText",
                "nullDecisionCallSite=0x$($nullCallAddress.ToUpperInvariant())",
                "nullDecisionCallLine=$nullCallLine",
                "nullTestInstruction=$nullTestLine",
                "nullBranchInstruction=$branchLine",
                "markedRepresentation=inline marked(old_o) -> header(old_o)->IsMarked() -> RawGetMethodTable() & GC_MARKED",
                "markedOutOfLineHelper=none",
                "nextMutationHelper=inline mark_queue_t::queue_mark set_marked(old_o) at locked gc.cpp:27333",
                "nextMutationInstruction=$(if ($nextMutationAddress) { '0x' + $nextMutationAddress.ToUpperInvariant() } else { 'not-emitted; false boundary is non-returning at runtime' })",
                "nextMutationLine=$nextMutationLine",
                "source=locked gc.cpp:27316-27335 queue_mark; old_o capture, slot/cursor writes, null decision, then marked(old_o)",
                "safeStop=inside the source-valid old_o == nullptr branch after null result and before marked(old_o); no mark-state read or later mutation"
            ) -Encoding ASCII
        } elseif ($isFirstRootFirstMarkMutation) {
            $disassemblyText = Get-Content -LiteralPath (Join-Path $runRoot 'artifact-disassembly.txt') -Raw
            $markHelperNumeric = [Convert]::ToUInt64($markHelperAddressText, 16)
            $markHelperHex = $markHelperNumeric.ToString('x')
            $helperStartMatch = [regex]::Match($disassemblyText, '(?im)^\s*' + [regex]::Escape($markHelperHex) + ':')
            $helperStart = if ($helperStartMatch.Success) { $helperStartMatch.Index } else { -1 }
            if ($helperStart -lt 0) { throw "Disassembly did not contain the linked mark_object_simple helper for C011EC12." }
            $helperTail = $disassemblyText.Substring($helperStart)
            $helperLines = $helperTail -split "`r?`n"
            $firstMutationLine = $helperLines | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[rdi\],\w+\s*$' } | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($firstMutationLine)) {
                $firstMutationLine = $helperLines | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[[^]]+\],\w+\s*$' } | Select-Object -First 1
            }
            if ([string]::IsNullOrWhiteSpace($firstMutationLine)) { throw "C011EC12 disassembly did not expose the first queue slot write." }
            $firstMutationAddress = ([regex]::Match($firstMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value)
            $firstMutationInstructionText = '0x' + $firstMutationAddress.ToUpperInvariant()
            $firstMutationIndex = [Array]::IndexOf($helperLines, $firstMutationLine)
            $nextMutationLine = $helperLines[($firstMutationIndex + 1)..($helperLines.Count - 1)] | Where-Object { $_ -match '(?i)^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+mov\s+QWORD PTR\s+\[rip\+[^]]+\],\w+' } | Select-Object -First 1
            $nextMutationAddress = if ($nextMutationLine) { ([regex]::Match($nextMutationLine, '(?i)^\s*(?<address>[0-9a-f]+):').Groups['address'].Value) } else { $null }
            $nextMutationInstructionText = if ($nextMutationAddress) { '0x' + $nextMutationAddress.ToUpperInvariant() } else { $null }
            Set-Content -LiteralPath (Join-Path $runRoot 'first-mark-machine-code.txt') -Value @(
                "markHelperSymbol=$markHelperSymbolName",
                "markHelperAddress=0x$markHelperAddressText",
                "firstMutationInstruction=$firstMutationInstructionText",
                "firstMutationLine=$firstMutationLine",
                "nextMutationInstruction=$nextMutationInstructionText",
                "nextMutationLine=$nextMutationLine",
                "source=locked gc.cpp:27308-27335 queue_mark; slot_table[slot_index]=o then curr_slot_index advancement",
                "safeStop=after the two-write queue invariant and before the old_o null test/marked(old_o)"
            ) -Encoding ASCII
        } elseif ($isFirstRootPreMarkBoundary) {
            $disassemblyText = Get-Content -LiteralPath (Join-Path $runRoot 'artifact-disassembly.txt') -Raw
            $markHelperNumeric = [Convert]::ToUInt64($markHelperAddressText, 16)
            $markHelperHex = $markHelperNumeric.ToString('x')
            $callbackNumeric = [Convert]::ToUInt64($callbackAddressText, 16)
            $callPattern = '(?im)call\s+0x0*' + [regex]::Escape($markHelperHex) + '\b'
            $markCallMatches = [regex]::Matches($disassemblyText, $callPattern)
            $markCallAddress = $null
            foreach ($candidate in $markCallMatches) {
                $prefix = $disassemblyText.Substring(0, $candidate.Index)
                $addressMatches = [regex]::Matches($prefix, '(?im)^\s*(?<address>[0-9a-f]+):')
                if ($addressMatches.Count -eq 0) { continue }
                $candidateAddress = [Convert]::ToUInt64($addressMatches[$addressMatches.Count - 1].Groups['address'].Value, 16)
                if ($candidateAddress -gt $callbackNumeric) { $markCallAddress = $candidateAddress; break }
            }
            if ($null -eq $markCallAddress) { throw "Disassembly did not retain the out-of-line mark_object_simple call after the C011EC11 boundary." }
            $markCallSiteText = ('0x{0:X16}' -f $markCallAddress)
            $firstMutationInstructionText = ('0x{0:X16}' -f ($markHelperNumeric + 0x1D))
            $firstMutationPattern = '(?im)^\s*' + $firstMutationInstructionText.Substring(2).TrimStart('0') + ':.*mov\s+QWORD PTR\s+\[rbx\+rdx\*8\],rax'
            if ($disassemblyText -notmatch $firstMutationPattern) { throw "The expected MARK_PHASE_PREFETCH first mutation instruction was not present at $firstMutationInstructionText." }
            Set-Content -LiteralPath (Join-Path $runRoot 'pre-mark-machine-code.txt') -Value @(
                "markHelperSymbol=$markHelperSymbolName",
                "markHelperAddress=0x$markHelperAddressText",
                "markCallSite=$markCallSiteText",
                "firstMutationInstruction=$firstMutationInstructionText",
                "source=locked gc.cpp:27308-27335 queue_mark; MARK_PHASE_PREFETCH slot_table[slot_index]=o precedes marked(old_o)",
                "promoteSource=locked gc.cpp:49474-49544; boundary before gc.cpp:49541 mark_object_simple invocation"
            ) -Encoding ASCII
        }
        Set-Content -LiteralPath (Join-Path $runRoot 'callback-symbol.txt') -Value @(
            "symbol=$callbackSymbolName",
            "callbackAddress=0x$callbackAddressText",
            "source=locked src/coreclr/gc/gc.cpp:49474-49544, included by src/coreclr/gc/gcwks.cpp",
            "typedef=promote_func(Object**, ScanContext*, uint32_t)",
            "callingConvention=Microsoft x64 AMD64 (RCX, RDX, R8; RAX return unused; 32-byte shadow space)"
        ) -Encoding ASCII
    }
    $requiredSymbols = @("ManagedMain","RhpNewArray","RhpNewArrayRare","RhpGcAlloc","guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment","guideXosNativeAotSuspendEeEntry","guideXosNativeAotSuspendEeAfterLock","guideXosNativeAotSuspendEeAfterSuspend","guideXosNativeAotSuspendEeBodyReturn","guideXosNativeAotDisablePreemptiveEntry","guideXosNativeAotDisablePreemptiveReturn","guideXosManagedAllocationGetDiagnostics")
    if ($isFirstRootHeapResolutionOrCondemned -or $isFirstRootMembershipClassification) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstNonNullRootCandidateLoadRequested","guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded","guideXosNativeAotFirstRootCallbackCallSiteEntered","guideXosNativeAotFirstRootCallbackEntered","guideXosNativeAotFirstRootMembershipCandidateLoaded","guideXosNativeAotFirstRootMembershipCheckRequested","guideXosNativeAotFirstRootMembershipCheckEntered","guideXosNativeAotFirstRootMembershipCheckCompleted","guideXosNativeAotFirstRootMembershipResultBoundary","guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
        if ($isFirstRootHeapResolutionOrCondemned) {
            $requiredSymbols += @("guideXosNativeAotFirstRootHeapResolutionRequested","guideXosNativeAotFirstRootHeapResolutionEntered","guideXosNativeAotFirstRootHeapResolutionCompleted")
        }
        if ($isFirstRootCondemnedGenerationDecisionOrPreMark) {
            $requiredSymbols += @("guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested","guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered","guideXosNativeAotFirstRootCondemnedGenerationQueryStart","guideXosNativeAotFirstRootCondemnedGenerationQueryCompleted","guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted","guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted")
        }
        if ($isFirstRootPreMarkBoundary) {
            $requiredSymbols += @("guideXosNativeAotFirstRootPreMarkTrueBranchEntered","guideXosNativeAotFirstRootPreMarkRootFlagTest","guideXosNativeAotFirstRootPreMarkConservativeCheck","guideXosNativeAotFirstRootPreMarkDebugValidationEntered","guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead","guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer","guideXosNativeAotFirstRootPreMarkDebugValidationCompleted","guideXosNativeAotFirstRootPreMarkBoundaryReached")
            if ($isFirstRootFirstMarkMutation) {
                $requiredSymbols += @("guideXosNativeAotFirstRootMarkHelperEntered","guideXosNativeAotFirstRootMarkWorklistWriteBefore","guideXosNativeAotFirstRootMarkWorklistWriteCompleted")
            } elseif ($isFirstRootPostQueueMarkDecision) {
                $requiredSymbols += @("guideXosNativeAotFirstRootMarkHelperEntered","guideXosNativeAotFirstRootMarkWorklistWriteBefore","guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted","guideXosNativeAotFirstRootPostQueueDecisionRequested","guideXosNativeAotFirstRootPostQueueDecisionEntered","guideXosNativeAotFirstRootPostQueueNullDecisionCompleted","guideXosNativeAotFirstRootPostQueueNonNullDecisionStarted","guideXosNativeAotFirstRootPostQueueMarkedRequested","guideXosNativeAotFirstRootPostQueueMarkedCompleted","guideXosNativeAotFirstRootPostQueueMarkedTrueBoundary","guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary")
            }
        }
    } elseif ($isStackProviderTransitionFailFast) {
        $requiredSymbols += @("guideXosNativeAotC011EC15GcScanRootsEntered","guideXosNativeAotC011EC15ProviderEntered","guideXosNativeAotC011EC15EnumGcRefReturned")
    } elseif ($isNextGenuineRootProvider) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotC011EC15GcScanRootsEntered","guideXosNativeAotC011EC15ProviderEntered","guideXosNativeAotC011EC15CandidateObserved","guideXosNativeAotC011EC15EnumGcRefReturned","guideXosNativeAotC011EC15QueueMarkReturned","guideXosNativeAotC011EC15MarkHelperReturned","guideXosNativeAotC011EC15PromoteReturned","guideXosNativeAotC011EC15PromoteEntered","guideXosNativeAotC011EC15PromoteCandidateLoaded","guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
    } elseif ($isFirstRootCallbackEntry -and -not $isFirstRootFirstNonNullOldO) {
        $requiredSymbols += @("guideXosNativeAotAllocationContextFixupRequest","guideXosNativeAotAllocationContextFixupGcStartWorkObserver","guideXosNativeAotAllocationRootPhaseRequested","guideXosNativeAotAllocationContextFixupEnumerationEntry","guideXosNativeAotAllocationContextFixupContextVisited","guideXosNativeAotAllocationContextFixupEnumerationComplete","guideXosNativeAotFirstPerThreadRootGcScanRootsEntered","guideXosNativeAotFirstPerThreadRootForeachThreadEntered","guideXosNativeAotFirstPerThreadRootIteratorInitialized","guideXosNativeAotFirstPerThreadRootIteratorCompletion","guideXosNativeAotFirstPerThreadRootThreadEnumerated","guideXosNativeAotFirstPerThreadRootThreadExcluded","guideXosNativeAotFirstPerThreadRootThreadIncluded","guideXosNativeAotFirstPerThreadRootThreadStaticListObserved","guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered","guideXosNativeAotFirstNonNullRootCandidateLoadRequested","guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded","guideXosNativeAotFirstRootCallbackCallSiteEntered","guideXosNativeAotFirstRootCallbackEntered","guideXosNativeAotFirstRootCallbackCandidateLoaded","guideXosManagedThreadStaticProofAssigned","guideXosManagedThreadStaticProofReadback")
        if ($isFirstRootFirstNonNullOldO) {
            $requiredSymbols += @("guideXosNativeAotFirstRootCallbackCallSiteEntered","guideXosNativeAotFirstRootNonNullOldOCandidateLoadRequested","guideXosNativeAotFirstRootNonNullOldOCandidateMachineWordLoaded","guideXosNativeAotFirstRootNonNullOldOCandidateLoaded","guideXosNativeAotFirstRootNonNullOldOCallbackEntered","guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered","guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore","guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted","guideXosNativeAotFirstRootNonNullOldODecisionRequested","guideXosNativeAotFirstRootNonNullOldODecisionEntered","guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted","guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted","guideXosNativeAotFirstRootNonNullOldOMarkedRequested","guideXosNativeAotFirstRootNonNullOldOMarkedCompleted","guideXosNativeAotFirstRootNonNullOldORawMarkWordRead","guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary","guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary")
        }
    } elseif ($isFirstNonNullRoot) {
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

    for ($runIndex = 0; $runIndex -lt $FreshBootCount; $runIndex++) {
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
        if ($isStackProviderTransitionFailFast -or $isFirstNonNullRoot -or $isFirstRootCallbackEntry) { $qemuArgs += @("-d","int,guest_errors","-D",$qemuDebugPath) }
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
                    $normalizedLiveText = $liveText -replace '\[IRQ\] dispatch irq=00\s*', ''
                    $normalizedLiveText = ($normalizedLiveText -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '\s+', ' '
                    $normalizedLiveText = $normalizedLiveText -replace '\s*=\s*', '='
                    $stopPattern = if ($isStackProviderTransitionFailFast) {
                        '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354'
                    } elseif ($isNextGenuineRootProvider) {
                        'marker=C011EC15'
                    } elseif ($isFirstRootFirstNonNullOldO) {
                        'marker=C011EC14'
                    } elseif ($isFirstRootPostQueueMarkDecision) {
                        'marker=C011EC13'
                    } elseif ($isFirstRootFirstMarkMutation) {
                        'marker=C011EC12'
                    } elseif ($isFirstRootPreMarkBoundary) {
                        'marker=C011EC11'
                    } elseif ($isFirstRootCondemnedGenerationDecision) {
                        'marker=C011EC10'
                    } elseif ($isFirstRootHeapResolution) {
                        'marker=C011EC09'
                    } elseif ($isFirstRootMembershipClassification) {
                        'marker=C011EC08'
                    } elseif ($isFirstRootCallbackEntry) {
                        'marker=C011EC07'
                    } elseif ($isFirstNonNullRoot) {
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
                    if ($isStackProviderTransitionFailFast -and
                        $normalizedLiveText -match '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354') {
                        $earlyFailure = "stack-provider-transition-failfast-runtime-prologue"
                        break
                    }
                    if ($isFirstRootFirstNonNullOldO -and
                        $normalizedLiveText -match '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=') {
                        $earlyFailure = "c011ec14-no-non-null-old-o-before-nativeaot-failfast"
                        break
                    }
                    if (($isFirstNonNullRoot -or $isFirstRootCallbackEntry) -and $normalizedLiveText -match '\[PageFault\] Not-present violation on read \(kernel\)') { $earlyFailure = "nativeaot-thread-static-startup-page-fault"; break }
                }
            }
            if (-not $completed -and [string]::IsNullOrWhiteSpace($earlyFailure)) {
                Read-Monitor $port $monitorPath
                $failureSerial = if (Test-Path -LiteralPath $serialPath) { Get-Content -LiteralPath $serialPath -Raw } else { "" }
                if (($isFirstNonNullRoot -or $isFirstRootCallbackEntry) -and $failureSerial -match '\[PageFault\] Not-present violation on read \(kernel\)') {
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
        $validationText = $serial -replace '\[IRQ\] dispatch irq=00\s*', ''
        $validationText = ($validationText -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '\s+', ' '
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
        if ($isStackProviderTransitionFailFast) {
            Assert-Text $validationText '\[nativeaot-gc-stack-provider-transition-failfast\] ordinary-provider-returned-null' "minimal ordinary ThreadStatic provider return"
            Assert-Text $validationText '\[nativeaot-gc-stack-provider-transition-failfast\] stack-provider-transition-start' "minimal stack-provider transition start"
            Assert-Text $validationText '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354' "NativeAOT transition fail-fast"
            if ($validationText -match 'SAFE_STOP marker=C011EC15|stack root|GcInfo|EnumGcRefsCallback') { throw "Minimal transition run reached stack-root semantic evidence in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=$null; outcome="D"; harnessTerminated=$true; earlyFailure=$earlyFailure
                failure=[ordered]@{ classification="Thread::GcScanRoots-prologue-code-manager-invariant"; palReason="0x47435354"; entry="Thread::GcScanRoots"; stackCallbackCount=0; stackFramesWalked=0; gcInfoReads=0 }
                transition=[ordered]@{ ordinaryProviderReturn="null"; category3TransitionStart=$true; failFast=$true }
            }
        } elseif ($isNextGenuineRootProvider) {
            Assert-Text $validationText '\[nativeaot-gc-next-genuine-root-provider\] SAFE_STOP marker=C011EC15' "C011EC15 next genuine root marker"
            $c15Line = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-next-genuine-root-provider\] SAFE_STOP marker=C011EC15' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($c15Line)) { throw "C011EC15 marker line was not isolated in $name." }
            Assert-Text $c15Line 'nonNullCandidates=00000002' "two genuine non-null root candidates"
            Assert-Text $c15Line 'firstRootCallbackReturns=00000001' "first root callback returned once"
            Assert-Text $c15Line 'queueMarkReturns=00000001' "first queue_mark returned once"
            Assert-Text $c15Line 'secondPromoteAttempts=00000000 secondPromoteEntries=00000000 secondQueueMutationAttempts=00000000 secondQueueMutationExecutions=00000000' "second root stopped before Promote and queue mutation"
            Assert-Text $c15Line 'markBitWrites=00000000 childReferenceReads=00000000 graphTraversal=00000000' "no mark-bit or child traversal mutation"
            Assert-Text $c15Line 'threadStoreLockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 restart=00000000 resume=00000000' "suspended EE invariants"
            $firstRootRaw = Get-MarkerField $c15Line 'firstRootRaw'
            $storageObject = Get-MarkerField $c15Line 'storageObject'
            $nextRootRaw = Get-MarkerField $c15Line 'nextRootRaw'
            if ($firstRootRaw -eq '0x0000000000000000' -or $firstRootRaw -ne $storageObject -or $nextRootRaw -eq '0x0000000000000000') { throw "C011EC15 did not identify the genuine first storage root and a non-null next candidate in $name." }
            if ((Get-MarkerField $c15Line 'firstRootProviderCategory') -ne '0x00000001') { throw "C011EC15 first root did not come from the inline thread-static provider in $name." }
            if ((Get-MarkerField $c15Line 'nextRootProviderCategory') -eq '0x00000000') { throw "C011EC15 next provider category was empty in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC15"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=(Get-MarkerField $c15Line 'firstRootSlot');rawValue=$firstRootRaw;storageObject=$storageObject;sentinel=(Get-MarkerField $c15Line 'sentinel');providerCategory=(Get-MarkerField $c15Line 'firstRootProviderCategory');provider=(Get-MarkerField $c15Line 'firstRootProvider');callback=(Get-MarkerField $c15Line 'firstRootCallback');context=(Get-MarkerField $c15Line 'firstRootContext')}
                nextRoot=[ordered]@{slot=(Get-MarkerField $c15Line 'nextRootSlot');rawValue=$nextRootRaw;providerCategory=(Get-MarkerField $c15Line 'nextRootProviderCategory');provider=(Get-MarkerField $c15Line 'nextRootProvider');callback=(Get-MarkerField $c15Line 'nextRootCallback');context=(Get-MarkerField $c15Line 'nextRootContext')}
                accounting=[ordered]@{providerRequests=(Get-MarkerField $c15Line 'providerRequests');providerEntries=(Get-MarkerField $c15Line 'providerEntries');gcScanRootsRequests=(Get-MarkerField $c15Line 'gcScanRootsRequests');rootSlotsVisited=(Get-MarkerField $c15Line 'rootSlotsVisited');nullCandidates=(Get-MarkerField $c15Line 'nullCandidates');nonNullCandidates=(Get-MarkerField $c15Line 'nonNullCandidates');firstRootCallbackReturns=(Get-MarkerField $c15Line 'firstRootCallbackReturns');enumGcRefContinuations=(Get-MarkerField $c15Line 'enumGcRefContinuations');promoteReturns=(Get-MarkerField $c15Line 'promoteReturns');markHelperReturns=(Get-MarkerField $c15Line 'markHelperReturns');queueMarkReturns=(Get-MarkerField $c15Line 'queueMarkReturns')}
                queue=[ordered]@{slot=(Get-MarkerField $c15Line 'firstQueueSlot');slotIndex=(Get-MarkerField $c15Line 'firstQueueSlotIndex');old=(Get-MarkerField $c15Line 'firstQueueOld');new=(Get-MarkerField $c15Line 'firstQueueNew');cursorBefore=(Get-MarkerField $c15Line 'firstQueueCursorBefore');cursorAfter=(Get-MarkerField $c15Line 'firstQueueCursorAfter');base=(Get-MarkerField $c15Line 'firstQueueBase')}
                prohibited=[ordered]@{secondPromoteAttempts=(Get-MarkerField $c15Line 'secondPromoteAttempts');secondPromoteEntries=(Get-MarkerField $c15Line 'secondPromoteEntries');secondQueueMutationAttempts=(Get-MarkerField $c15Line 'secondQueueMutationAttempts');secondQueueMutationExecutions=(Get-MarkerField $c15Line 'secondQueueMutationExecutions');markBitWrites=(Get-MarkerField $c15Line 'markBitWrites');childReferenceReads=(Get-MarkerField $c15Line 'childReferenceReads');graphTraversal=(Get-MarkerField $c15Line 'graphTraversal')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $c15Line 'threadStoreLockHeld');eeSuspended=(Get-MarkerField $c15Line 'eeSuspended');managedEntryProhibited=(Get-MarkerField $c15Line 'managedEntryProhibited');restart=(Get-MarkerField $c15Line 'restart');resume=(Get-MarkerField $c15Line 'resume')}
                providerOrder=[ordered]@{first=(Get-MarkerField $c15Line 'firstRootProviderCategory');next=(Get-MarkerField $c15Line 'nextRootProviderCategory')}
            }
        } elseif ($isFirstRootFirstNonNullOldO) {
            if ($earlyFailure -eq "c011ec14-no-non-null-old-o-before-nativeaot-failfast") {
                Assert-Text $validationText '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354' "NativeAOT bounded continuation fail-fast"
                $preDecisionLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-allocation\] c14-predecision' } | Select-Object -Last 1)
                if ([string]::IsNullOrWhiteSpace($preDecisionLine)) { throw "C011EC14 fail-fast evidence did not include the pre-decision counter line in $name." }
                Assert-Text $preDecisionLine 'queueInsertions=00000001 callbacks=00000001 .*old_o=0000000000000000 rawMarkReads=00000000' "single valid queue insertion before bounded continuation failure"
                $runResults += [ordered]@{
                    name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker=$null; outcome="E"; harnessTerminated=$true; earlyFailure=$earlyFailure
                    failure=[ordered]@{ classification="nativeaot-continuation-failfast-before-second-queue-insertion"; palReason="0x47435354"; preDecision=$preDecisionLine }
                    root=[ordered]@{slot=(Get-MarkerField $preDecisionLine 'rootSlot');rawValue=(Get-MarkerField $preDecisionLine 'rawRoot');storageObject=(Get-MarkerField $preDecisionLine 'storageObject')}
                    queue=[ordered]@{insertions=(Get-MarkerField $preDecisionLine 'queueInsertions');capacity="0x0000000000000010";oldObject="0x0000000000000000";markedReads=(Get-MarkerField $preDecisionLine 'rawMarkReads')}
                    decision=[ordered]@{oldObject="0x0000000000000000";nonNullReached=$false}
                    markState=[ordered]@{reads=(Get-MarkerField $preDecisionLine 'rawMarkReads');writes="0x00000000";result="not reached"}
                    prohibited=[ordered]@{graphTraversal="not reached";childReferenceReads="not reached";childObjects="not reached";callbackReturnsAfterTarget="not applicable";restart="not reached";resume="not reached"}
                }
                continue
            }
            Assert-Text $validationText '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' "C011EC14 first non-null old_o marker"
            $nonNullOldOLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-first-non-null-old-o\] SAFE_STOP marker=C011EC14' } | Select-Object -Last 1)
            Assert-Text $nonNullOldOLine 'queueInsertions=00000011 queueWrites=00000011 queueHistoryOverflow=00000000' "17 real queue insertions"
            Assert-Text $nonNullOldOLine 'nullDecisions=00000010 nonNullDecisions=00000001 decisionRequests=00000011 decisionEntries=00000011' "16 null decisions followed by first non-null old_o"
            Assert-Text $nonNullOldOLine 'markedRequests=00000001 markedEntries=00000001 markedReturns=00000001 markStateReads=00000001 rawMarkWordReads=00000001' "actual marked(old_o) read"
            Assert-Text $nonNullOldOLine 'newMutationAttempts=00000000 newMutationExecutions=00000000 markBitWrites=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "no later mutation or traversal"
            Assert-Text $nonNullOldOLine 'restart=00000000 resume=00000000 lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "no restart/resume and suspended invariants"
            $oldObject = Get-MarkerField $nonNullOldOLine 'old_o'
            if ($oldObject -eq '0x0000000000000000' -or $oldObject -ne (Get-MarkerField $nonNullOldOLine 'slotOld') -or $oldObject -eq (Get-MarkerField $nonNullOldOLine 'sentinel')) { throw "C011EC14 did not select a genuine non-null displaced old_o in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'slotIndex') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorBefore') -ne '0x0000000000000000' -or (Get-MarkerField $nonNullOldOLine 'cursorAfter') -ne '0x0000000000000001') { throw "C011EC14 did not capture the slot-zero ring wrap in $name." }
            if ((Get-MarkerField $nonNullOldOLine 'provenanceValid') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'findRange') -ne '0x00000001' -or (Get-MarkerField $nonNullOldOLine 'heapMembership') -ne '0x00000001') { throw "C011EC14 old_o provenance was not valid in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC14"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=(Get-MarkerField $nonNullOldOLine 'rootSlot');rawValue=(Get-MarkerField $nonNullOldOLine 'rawRoot');storageObject=(Get-MarkerField $nonNullOldOLine 'storageObject');sentinel=(Get-MarkerField $nonNullOldOLine 'sentinel')}
                queue=[ordered]@{insertions=(Get-MarkerField $nonNullOldOLine 'queueInsertions');writes=(Get-MarkerField $nonNullOldOLine 'queueWrites');slotAddress=(Get-MarkerField $nonNullOldOLine 'selectedSlot');slotIndex=(Get-MarkerField $nonNullOldOLine 'slotIndex');oldSlot=(Get-MarkerField $nonNullOldOLine 'slotOld');newSlot=(Get-MarkerField $nonNullOldOLine 'slotNew');oldObject=$oldObject;cursorBefore=(Get-MarkerField $nonNullOldOLine 'cursorBefore');cursorAfter=(Get-MarkerField $nonNullOldOLine 'cursorAfter');base=(Get-MarkerField $nonNullOldOLine 'queueBase')}
                decision=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'decisionRequests');entries=(Get-MarkerField $nonNullOldOLine 'decisionEntries');nullDecisions=(Get-MarkerField $nonNullOldOLine 'nullDecisions');nonNullDecisions=(Get-MarkerField $nonNullOldOLine 'nonNullDecisions');oldObject=$oldObject}
                markState=[ordered]@{requests=(Get-MarkerField $nonNullOldOLine 'markedRequests');entries=(Get-MarkerField $nonNullOldOLine 'markedEntries');returns=(Get-MarkerField $nonNullOldOLine 'markedReturns');reads=(Get-MarkerField $nonNullOldOLine 'markStateReads');result=(Get-MarkerField $nonNullOldOLine 'markedResult');rawReads=(Get-MarkerField $nonNullOldOLine 'rawMarkWordReads');rawHeader=(Get-MarkerField $nonNullOldOLine 'rawHeader');mask=(Get-MarkerField $nonNullOldOLine 'markMask')}
                provenance=[ordered]@{valid=(Get-MarkerField $nonNullOldOLine 'provenanceValid');findRange=(Get-MarkerField $nonNullOldOLine 'findRange');heapMembership=(Get-MarkerField $nonNullOldOLine 'heapMembership');generation=(Get-MarkerField $nonNullOldOLine 'generation');objectHistoryIndex=(Get-MarkerField $nonNullOldOLine 'objectHistoryIndex')}
                counters=[ordered]@{callbacks=(Get-MarkerField $nonNullOldOLine 'callbacks');candidateLoads=(Get-MarkerField $nonNullOldOLine 'candidateLoads');markHelpers=(Get-MarkerField $nonNullOldOLine 'markHelpers');callbackReturnsBeforeDecision=(Get-MarkerField $nonNullOldOLine 'callbackReturnsBeforeDecision');callbackReturns=(Get-MarkerField $nonNullOldOLine 'callbackReturns')}
                mutation=[ordered]@{newAttempts=(Get-MarkerField $nonNullOldOLine 'newMutationAttempts');newExecutions=(Get-MarkerField $nonNullOldOLine 'newMutationExecutions');markBitWrites=(Get-MarkerField $nonNullOldOLine 'markBitWrites')}
                traversal=[ordered]@{graph=(Get-MarkerField $nonNullOldOLine 'graphTraversal');childReferenceReads=(Get-MarkerField $nonNullOldOLine 'childReferenceReads');childObjects=(Get-MarkerField $nonNullOldOLine 'childObjectsDiscovered');secondObjectMarkAttempts=(Get-MarkerField $nonNullOldOLine 'secondObjectMarkAttempts')}
            }
        } elseif ($isFirstRootPostQueueMarkDecision) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-post-queue-mark-decision\] SAFE_STOP marker=C011EC13' "first root post-queue decision marker"
            $postQueueLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-post-queue-mark-decision\] SAFE_STOP marker=C011EC13' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($postQueueLine)) { throw "C011EC13 marker line was not isolated in $name." }
            Assert-Text $postQueueLine 'membership=00000001 wksMultipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 wksNullHeapValid=00000001 condemned=00000001' "membership, WKS null heap, and condemned=true"
            Assert-Text $postQueueLine 'decisionRequests=00000001 decisionEntries=00000001 decisionCompletions=00000001 decisionDuplicates=00000000' "exactly one post-queue decision"
            Assert-Text $postQueueLine 'inheritedQueueSlotWrites=00000001 inheritedCursorWrites=00000001 newMutationAttempts=00000000 newMutationExecutions=00000000' "C011EC12 inherited queue unit and zero new mutation"
            Assert-Text $postQueueLine 'markBitWrites=00000000 logicalMarkComplete=00000000 traversalScheduled=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "no logical mark or traversal"
            Assert-Text $postQueueLine 'promotionStart=00000000 promotions=00000000 callbackReturns=00000000 secondCallbacks=00000000 restart=00000000 resume=00000000' "no continuation beyond the decision"
            Assert-Text $postQueueLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 registeredThreads=00000001 enumeratedThreads=00000001 includedThreads=00000001 registryMutation=00000000 allocationContextsCleared=00000001' "suspended one-mutator invariants"
            Assert-Text $postQueueLine 'sentinelFailures=00000000 objectValidationBeforeFixup=00000000 objectValidationAfterFixup=00000000 duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "object and sentinel validation"
            $postQueueRoot = Get-MarkerField $postQueueLine 'rawRoot'
            $postQueueStorage = Get-MarkerField $postQueueLine 'storageObject'
            $postQueueSentinel = Get-MarkerField $postQueueLine 'sentinel'
            if ($postQueueRoot -ne $postQueueStorage -or $postQueueRoot -eq $postQueueSentinel) { throw "C011EC13 root identity did not remain the genuine storage object in $name." }
            $postQueueOld = Get-MarkerField $postQueueLine 'old_o'
            $postQueueSlotOld = Get-MarkerField $postQueueLine 'slotOld'
            $postQueueNew = Get-MarkerField $postQueueLine 'slotNew'
            if ($postQueueOld -ne $postQueueSlotOld -or $postQueueNew -ne $postQueueStorage) { throw "C011EC13 queue old/new values were inconsistent in $name." }
            if ((Get-MarkerField $postQueueLine 'slotIndex') -ne '0x0000000000000000' -or
                (Get-MarkerField $postQueueLine 'cursorBefore') -ne '0x0000000000000000' -or
                (Get-MarkerField $postQueueLine 'cursorAfter') -ne '0x0000000000000001') { throw "C011EC13 queue cursor/slot state diverged in $name." }
            $nullResult = Get-MarkerField $postQueueLine 'nullResult'
            $markedRequests = Get-MarkerField $postQueueLine 'markedRequests'
            $markedEntries = Get-MarkerField $postQueueLine 'markedEntries'
            $markedReturns = Get-MarkerField $postQueueLine 'markedReturns'
            $markStateReads = Get-MarkerField $postQueueLine 'markStateReads'
            $markedResult = Get-MarkerField $postQueueLine 'markedResult'
            if ($nullResult -eq '0x00000001') {
                if ($markedRequests -ne '0x00000000' -or $markedEntries -ne '0x00000000' -or $markedReturns -ne '0x00000000' -or $markStateReads -ne '0x00000000' -or (Get-MarkerField $postQueueLine 'branch') -ne '0x00000001') { throw "C011EC13 null path did not bypass marked(old_o) in $name." }
                $postQueueOutcome = 'B'
            } else {
                if ($markedRequests -ne '0x00000001' -or $markedEntries -ne '0x00000001' -or $markedReturns -ne '0x00000001' -or $markStateReads -ne '0x00000001' -or ($markedResult -ne '0x00000000' -and $markedResult -ne '0x00000001')) { throw "C011EC13 non-null path did not complete exactly one marked(old_o) read in $name." }
                $postQueueOutcome = 'A'
            }
            Assert-Text $postQueueLine 'objectHeaderReads=00000000 methodTableReads=00000000 segmentReads=00000000 regionReads=00000000' "no diagnostic metadata reads added around the decision"
            $decisionReturnAddress = Get-MarkerField $postQueueLine 'decisionReturnAddress'
            $safeStopAddress = Get-MarkerField $postQueueLine 'safeStopAddress'
            $nextMutationAddress = Get-MarkerField $postQueueLine 'nextMutationAddress'
            if ([string]::IsNullOrWhiteSpace($decisionReturnAddress) -or [string]::IsNullOrWhiteSpace($safeStopAddress) -or [string]::IsNullOrWhiteSpace($nextMutationAddress)) { throw "C011EC13 did not record the machine-code decision boundary in $name." }
            $machineCodeEvidencePath = Join-Path $runRoot 'post-queue-machine-code.txt'
            if (-not (Test-Path -LiteralPath $machineCodeEvidencePath -PathType Leaf)) {
                Set-Content -LiteralPath $machineCodeEvidencePath -Value @(
                    "markHelperSymbol=$markHelperSymbolName",
                    "markHelperAddress=0x$markHelperAddressText",
                    "decisionReturnAddress=$decisionReturnAddress",
                    "safeStopAddress=$safeStopAddress",
                    "nextMutationAddress=$nextMutationAddress",
                    "source=locked gc.cpp:27308-27335 queue_mark; old_o capture, queue slot write, cursor advance, old_o null branch, marked(old_o) only on the non-null branch",
                    "machineCode=artifact-disassembly.txt",
                    "safeStop=after the real null/marked decision and before return/callback continuation or any later mutation"
                ) -Encoding ASCII
            }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC13"; outcome=$postQueueOutcome; harnessTerminated=$true
                root=[ordered]@{slot=(Get-MarkerField $postQueueLine 'rootSlot');rawValue=$postQueueRoot;storageObject=$postQueueStorage;sentinel=$postQueueSentinel}
                queue=[ordered]@{slotAddress=(Get-MarkerField $postQueueLine 'selectedSlot');slotIndex=(Get-MarkerField $postQueueLine 'slotIndex');oldSlot=$postQueueSlotOld;newSlot=$postQueueNew;oldObject=$postQueueOld;cursorBefore=(Get-MarkerField $postQueueLine 'cursorBefore');cursorAfter=(Get-MarkerField $postQueueLine 'cursorAfter');queueBase=(Get-MarkerField $postQueueLine 'queueBase')}
                decision=[ordered]@{requests=(Get-MarkerField $postQueueLine 'decisionRequests');entries=(Get-MarkerField $postQueueLine 'decisionEntries');completions=(Get-MarkerField $postQueueLine 'decisionCompletions');duplicates=(Get-MarkerField $postQueueLine 'decisionDuplicates');nullTests=(Get-MarkerField $postQueueLine 'nullTests');nullResult=$nullResult;branch=(Get-MarkerField $postQueueLine 'branch');markedRequests=$markedRequests;markedEntries=$markedEntries;markedReturns=$markedReturns;markedResult=$markedResult;markStateReads=$markStateReads;decisionReturnAddress=$decisionReturnAddress;safeStopAddress=$safeStopAddress;nextMutationAddress=$nextMutationAddress}
                mutation=[ordered]@{inheritedQueueSlotWrites=(Get-MarkerField $postQueueLine 'inheritedQueueSlotWrites');inheritedCursorWrites=(Get-MarkerField $postQueueLine 'inheritedCursorWrites');newAttempts=(Get-MarkerField $postQueueLine 'newMutationAttempts');newExecutions=(Get-MarkerField $postQueueLine 'newMutationExecutions');markBitWrites=(Get-MarkerField $postQueueLine 'markBitWrites')}
                traversal=[ordered]@{logicalMarkComplete=(Get-MarkerField $postQueueLine 'logicalMarkComplete');scheduled=(Get-MarkerField $postQueueLine 'traversalScheduled');graph=(Get-MarkerField $postQueueLine 'graphTraversal');childReads=(Get-MarkerField $postQueueLine 'childReferenceReads');childObjects=(Get-MarkerField $postQueueLine 'childObjectsDiscovered');secondObjectAttempts=(Get-MarkerField $postQueueLine 'secondObjectMarkAttempts')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $postQueueLine 'lockHeld');eeSuspended=(Get-MarkerField $postQueueLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $postQueueLine 'managedEntryProhibited');registeredThreads=(Get-MarkerField $postQueueLine 'registeredThreads');enumeratedThreads=(Get-MarkerField $postQueueLine 'enumeratedThreads');includedThreads=(Get-MarkerField $postQueueLine 'includedThreads');registryMutation=(Get-MarkerField $postQueueLine 'registryMutation');restart=(Get-MarkerField $postQueueLine 'restart');resume=(Get-MarkerField $postQueueLine 'resume')}
                validation=[ordered]@{sentinelFailures=(Get-MarkerField $postQueueLine 'sentinelFailures');objectValidationBeforeFixup=(Get-MarkerField $postQueueLine 'objectValidationBeforeFixup');objectValidationAfterFixup=(Get-MarkerField $postQueueLine 'objectValidationAfterFixup');duplicateObjectAddresses=(Get-MarkerField $postQueueLine 'duplicateObjectAddresses');objectHistoryOverflow=(Get-MarkerField $postQueueLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootFirstMarkMutation) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-first-mark-mutation\] SAFE_STOP marker=C011EC12' "first real root first-mark mutation marker"
            $firstMarkLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-first-mark-mutation\] SAFE_STOP marker=C011EC12' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($firstMarkLine)) { throw "C011EC12 marker line was not isolated in $name." }
            Assert-Text $firstMarkLine 'membership=00000001 wksMultipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 wksNullHeapValid=00000001 condemned=00000001' "membership, WKS null heap, and condemned=true"
            Assert-Text $firstMarkLine 'markCallAttempts=00000001 markCalls=00000001 duplicateMarkCalls=00000000' "one authentic mark helper request and entry"
            Assert-Text $firstMarkLine 'firstMutationAttempts=00000001 firstMutationExecutions=00000001 secondMutationAttempts=00000000 secondMutationExecutions=00000000' "exactly one first mutation and no second mutation"
            Assert-Text $firstMarkLine 'mutationKind=queue_slot_and_cursor_atomic_unit' "queue mutation semantic type"
            Assert-Text $firstMarkLine 'worklistOld=0000000000000000 .*slotIndexBefore=0000000000000000 cursorBefore=0000000000000000 slotIndexAfter=0000000000000000 cursorAfter=0000000000000001 capacity=0000000000000010' "queue constructor state and cursor advancement"
            Assert-Text $firstMarkLine 'markStateReads=00000000 markStateResult=00000000 markBitWrites=00000000 worklistSlotWrites=00000001 worklistCursorWrites=00000001 objectHeaderWrites=00000000 gcMetadataWrites=00000000 segmentWrites=00000000' "queue writes before mark-state read"
            Assert-Text $firstMarkLine 'logicalMarkComplete=00000000 traversalScheduled=00000000 graphTraversal=00000000 childReferenceReads=00000000 childObjectsDiscovered=00000000 secondObjectMarkAttempts=00000000' "no logical mark completion or traversal"
            Assert-Text $firstMarkLine 'promotionStart=00000000 promotions=00000000 promotionWrites=00000000 callbackReturns=00000000 secondCallbacks=00000000 restart=00000000 resume=00000000 markHelperReturns=00000000' "no continuation beyond safe stop"
            Assert-Text $firstMarkLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 registeredThreads=00000001 enumeratedThreads=00000001 includedThreads=00000001 registryMutation=00000000 allocationContextsCleared=00000001' "suspended one-mutator invariants"
            Assert-Text $firstMarkLine 'sentinelFailures=00000000 objectValidationBeforeFixup=00000000 objectValidationAfterFixup=00000000 objectOverlapFailures=00000000 objectPatternFailures=00000000 duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "object and sentinel validation"
            $firstMarkRootSlot = Get-MarkerField $firstMarkLine 'rootSlot'
            $firstMarkRawRoot = Get-MarkerField $firstMarkLine 'rawRoot'
            $firstMarkStorage = Get-MarkerField $firstMarkLine 'storageObject'
            $firstMarkSentinel = Get-MarkerField $firstMarkLine 'sentinel'
            if ($firstMarkRawRoot -ne $firstMarkStorage -or $firstMarkRawRoot -eq $firstMarkSentinel) { throw "C011EC12 root identity did not remain the storage object in $name." }
            $firstMarkNew = Get-MarkerField $firstMarkLine 'worklistNew'
            if ($firstMarkNew -ne $firstMarkStorage) { throw "C011EC12 worklist write did not directly encode the storage object in $name." }
            $firstMarkTarget = Get-MarkerField $firstMarkLine 'worklistTarget'
            $firstMarkQueueBase = Get-MarkerField $firstMarkLine 'queueBase'
            if ($firstMarkTarget -ne $firstMarkQueueBase) { throw "C011EC12 slot-zero target did not equal queue base in $name." }
            $machineCodeEvidencePath = Join-Path $runRoot 'first-mark-machine-code.txt'
            Require-File $machineCodeEvidencePath "C011EC12 machine-code evidence"
            $machineCodeEvidence = Get-Content -LiteralPath $machineCodeEvidencePath -Raw
            $firstMarkInstructionMatch = [regex]::Match($machineCodeEvidence, '(?im)^firstMutationInstruction=(?<value>0x[0-9A-F]+)')
            $firstMarkNextInstructionMatch = [regex]::Match($machineCodeEvidence, '(?im)^nextMutationInstruction=(?<value>0x[0-9A-F]+)')
            if (-not $firstMarkInstructionMatch.Success -or -not $firstMarkNextInstructionMatch.Success) { throw "C011EC12 did not record both exact machine-code mutation boundaries in $name." }
            $firstMarkInstruction = $firstMarkInstructionMatch.Groups['value'].Value
            $firstMarkNextInstruction = $firstMarkNextInstructionMatch.Groups['value'].Value
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC12"; outcome="D"; harnessTerminated=$true
                root=[ordered]@{slot=$firstMarkRootSlot;rawValue=$firstMarkRawRoot;storageObject=$firstMarkStorage;sentinel=$firstMarkSentinel;membership=(Get-MarkerField $firstMarkLine 'membership')}
                heap=[ordered]@{multipleHeaps=(Get-MarkerField $firstMarkLine 'wksMultipleHeaps');hpt=(Get-MarkerField $firstMarkLine 'hpt');heapOf=(Get-MarkerField $firstMarkLine 'heapOf');heapNumber=(Get-MarkerField $firstMarkLine 'heapNumber');heapCount=(Get-MarkerField $firstMarkLine 'heapCount');nullSentinelValid=(Get-MarkerField $firstMarkLine 'wksNullHeapValid')}
                condemned=[ordered]@{result=(Get-MarkerField $firstMarkLine 'condemned');generation=(Get-MarkerField $firstMarkLine 'generationFromRegion');condemnedGeneration=(Get-MarkerField $firstMarkLine 'condemnedGeneration');maximumGeneration=(Get-MarkerField $firstMarkLine 'maximumGeneration')}
                markHelper=[ordered]@{requests=(Get-MarkerField $firstMarkLine 'markCallAttempts');entries=(Get-MarkerField $firstMarkLine 'markCalls');duplicates=(Get-MarkerField $firstMarkLine 'duplicateMarkCalls');po=(Get-MarkerField $firstMarkLine 'helperPo');object=(Get-MarkerField $firstMarkLine 'helperObject')}
                mutation=[ordered]@{attempts=(Get-MarkerField $firstMarkLine 'firstMutationAttempts');executions=(Get-MarkerField $firstMarkLine 'firstMutationExecutions');secondAttempts=(Get-MarkerField $firstMarkLine 'secondMutationAttempts');secondExecutions=(Get-MarkerField $firstMarkLine 'secondMutationExecutions');kind="queue_slot_and_cursor_atomic_unit";target=$firstMarkTarget;old=(Get-MarkerField $firstMarkLine 'worklistOld');new=$firstMarkNew;queueBase=$firstMarkQueueBase;slotIndexBefore=(Get-MarkerField $firstMarkLine 'slotIndexBefore');cursorBefore=(Get-MarkerField $firstMarkLine 'cursorBefore');slotIndexAfter=(Get-MarkerField $firstMarkLine 'slotIndexAfter');cursorAfter=(Get-MarkerField $firstMarkLine 'cursorAfter');capacity=(Get-MarkerField $firstMarkLine 'capacity');firstInstruction=$firstMarkInstruction;nextInstruction=$firstMarkNextInstruction}
                counters=[ordered]@{worklistMetadataReads=(Get-MarkerField $firstMarkLine 'worklistMetadataReads');markStateReads=(Get-MarkerField $firstMarkLine 'markStateReads');markBitWrites=(Get-MarkerField $firstMarkLine 'markBitWrites');worklistSlotWrites=(Get-MarkerField $firstMarkLine 'worklistSlotWrites');worklistCursorWrites=(Get-MarkerField $firstMarkLine 'worklistCursorWrites');objectHeaderWrites=(Get-MarkerField $firstMarkLine 'objectHeaderWrites');gcMetadataWrites=(Get-MarkerField $firstMarkLine 'gcMetadataWrites');segmentWrites=(Get-MarkerField $firstMarkLine 'segmentWrites')}
                traversal=[ordered]@{logicalMarkComplete=(Get-MarkerField $firstMarkLine 'logicalMarkComplete');scheduled=(Get-MarkerField $firstMarkLine 'traversalScheduled');graph=(Get-MarkerField $firstMarkLine 'graphTraversal');childReads=(Get-MarkerField $firstMarkLine 'childReferenceReads');childObjects=(Get-MarkerField $firstMarkLine 'childObjectsDiscovered');secondObjectAttempts=(Get-MarkerField $firstMarkLine 'secondObjectMarkAttempts')}
                threadStore=[ordered]@{lockHeld=(Get-MarkerField $firstMarkLine 'lockHeld');eeSuspended=(Get-MarkerField $firstMarkLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $firstMarkLine 'managedEntryProhibited');registeredThreads=(Get-MarkerField $firstMarkLine 'registeredThreads');enumeratedThreads=(Get-MarkerField $firstMarkLine 'enumeratedThreads');includedThreads=(Get-MarkerField $firstMarkLine 'includedThreads');registryMutation=(Get-MarkerField $firstMarkLine 'registryMutation');restart=(Get-MarkerField $firstMarkLine 'restart');resume=(Get-MarkerField $firstMarkLine 'resume')}
                validation=[ordered]@{sentinelChecks=(Get-MarkerField $firstMarkLine 'sentinelChecks');sentinelFailures=(Get-MarkerField $firstMarkLine 'sentinelFailures');objectValidationBeforeFixup=(Get-MarkerField $firstMarkLine 'objectValidationBeforeFixup');objectValidationAfterFixup=(Get-MarkerField $firstMarkLine 'objectValidationAfterFixup');objectOverlapFailures=(Get-MarkerField $firstMarkLine 'objectOverlapFailures');objectPatternFailures=(Get-MarkerField $firstMarkLine 'objectPatternFailures');duplicateObjectAddresses=(Get-MarkerField $firstMarkLine 'duplicateObjectAddresses');objectHistoryOverflow=(Get-MarkerField $firstMarkLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootPreMarkBoundary) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-pre-mark-boundary\] SAFE_STOP marker=C011EC11' "first real root pre-mark boundary marker"
            $preMarkLine = (($validationText -split "`n") | Where-Object { $_ -match '\[nativeaot-gc-first-root-pre-mark-boundary\] SAFE_STOP marker=C011EC11' } | Select-Object -Last 1)
            if ([string]::IsNullOrWhiteSpace($preMarkLine)) { throw "C011EC11 marker line was not isolated in $name." }
            Assert-Text $preMarkLine 'membership=00000001 wksMultipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 wksNullHeapValid=00000001 condemned=00000001' "true membership, valid Workstation null heap, and condemned=true"
            Assert-Text $preMarkLine 'trueBranchRequests=00000001 trueBranchEntries=00000001 trueBranchDuplicates=00000000' "one true-branch traversal"
            Assert-Text $preMarkLine 'dprintfCompiled=00000000 dprintfRequests=00000000 dprintfEntries=00000000 dprintfReturns=00000000' "release-build dprintf elimination"
            Assert-Text $preMarkLine 'rawFlags=00000000 flagTests=00000002 interiorFlag=00000000 pinnedFlag=00000000' "source-defined root flag tests"
            Assert-Text $preMarkLine 'conservativeChecks=00000001 .*objectIsFree=00000000 .*debugValidationEntries=00000001 .*debugValidationCompletions=00000001' "conservative and debug-only gates"
            Assert-Text $preMarkLine 'markStateReads=00000000 markStateResult=00000000' "no mark-state read"
            Assert-Text $preMarkLine 'markCallAttempts=00000000 markCalls=00000000' "no mark helper execution"
            Assert-Text $preMarkLine 'promotionStart=00000000 promotions=00000000 promotionWrites=00000000 markWrites=00000000 worklistWrites=00000000 graphTraversal=00000000 childReferenceReads=00000000 objectMutation=00000000 objectHeaderWrites=00000000 gcMetadataMutation=00000000 segmentMutation=00000000 relocationWrites=00000000' "zero mutation counters"
            Assert-Text $preMarkLine 'callbackReturns=00000000 secondCallbacks=00000000 restart=00000000 resume=00000000' "zero callback continuation and restart"
            Assert-Text $preMarkLine 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 registeredThreads=00000001 enumeratedThreads=00000001 includedThreads=00000001 registryMutation=00000000 allocationContextsCleared=00000001' "suspended one-mutator invariants"
            Assert-Text $validationText 'contextFieldReads=00000006 .*contextPromotion=00000001 contextConcurrent=00000000' "source-required ScanContext observation"
            $preMarkRootSlot = Get-MarkerField $preMarkLine 'rootSlot'
            $preMarkRawRoot = Get-MarkerField $preMarkLine 'rawRoot'
            $preMarkStorage = Get-MarkerField $preMarkLine 'storageObject'
            $preMarkSentinel = Get-MarkerField $preMarkLine 'sentinel'
            $preMarkHeap = Get-MarkerField $preMarkLine 'heapOf'
            if ($preMarkRawRoot -ne $preMarkStorage) { throw "C011EC11 raw root did not equal the genuine NativeAOT thread-static storage object in $name." }
            if ($preMarkRawRoot -eq $preMarkSentinel) { throw "C011EC11 treated the proof sentinel as the collected root in $name." }
            $preMarkCondemnedGeneration = Get-MarkerField $preMarkLine 'condemnedGeneration'
            $preMarkMaximumGeneration = Get-MarkerField $preMarkLine 'maximumGeneration'
            $preMarkGeneration = Get-MarkerField $preMarkLine 'generationFromRegion'
            if ($preMarkCondemnedGeneration -ne '0x00000000' -or $preMarkMaximumGeneration -ne '0x00000002' -or $preMarkGeneration -ne '0x00000000') { throw "C011EC11 condemned-generation values diverged from C011EC10 in $name." }
            $preMarkHelper = Get-MarkerField $preMarkLine 'markHelper'
            $preMarkBoundaryReturn = Get-MarkerField $preMarkLine 'boundaryReturn'
            if ([string]::IsNullOrWhiteSpace($preMarkHelper) -or [string]::IsNullOrWhiteSpace($preMarkBoundaryReturn)) { throw "C011EC11 did not record the poised mark helper and boundary return address in $name." }
            $preMarkObjectHeaders = Get-MarkerField $preMarkLine 'objectHeaders'
            $preMarkMethodTables = Get-MarkerField $preMarkLine 'methodTables'
            if ($preMarkObjectHeaders -ne $preMarkMethodTables -or $preMarkObjectHeaders -eq '0x00000000') { throw "C011EC11 did not record the required Validate method-table read in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC11"; outcome="A"; harnessTerminated=$true
                root=[ordered]@{slot=$preMarkRootSlot;rawValue=$preMarkRawRoot;storageObject=$preMarkStorage;sentinel=$preMarkSentinel;membership=(Get-MarkerField $preMarkLine 'membership')}
                heap=[ordered]@{multipleHeaps=(Get-MarkerField $preMarkLine 'wksMultipleHeaps');hpt=(Get-MarkerField $preMarkLine 'hpt');heapOf=$preMarkHeap;heapNumber=(Get-MarkerField $preMarkLine 'heapNumber');heapCount=(Get-MarkerField $preMarkLine 'heapCount');nullSentinelValid=(Get-MarkerField $preMarkLine 'wksNullHeapValid')}
                condemned=[ordered]@{result=(Get-MarkerField $preMarkLine 'condemned');generation=$preMarkGeneration;condemnedGeneration=$preMarkCondemnedGeneration;maximumGeneration=$preMarkMaximumGeneration;generationTableReads=(Get-MarkerField $preMarkLine 'generationTableReads');segmentLookups=(Get-MarkerField $preMarkLine 'generationSegmentLookups')}
                trueBranch=[ordered]@{requests=(Get-MarkerField $preMarkLine 'trueBranchRequests');entries=(Get-MarkerField $preMarkLine 'trueBranchEntries');duplicates=(Get-MarkerField $preMarkLine 'trueBranchDuplicates')}
                dprintf=[ordered]@{compiled=(Get-MarkerField $preMarkLine 'dprintfCompiled');requests=(Get-MarkerField $preMarkLine 'dprintfRequests');entries=(Get-MarkerField $preMarkLine 'dprintfEntries');returns=(Get-MarkerField $preMarkLine 'dprintfReturns')}
                flags=[ordered]@{raw=(Get-MarkerField $preMarkLine 'rawFlags');tests=(Get-MarkerField $preMarkLine 'flagTests');interior=(Get-MarkerField $preMarkLine 'interiorFlag');pinned=(Get-MarkerField $preMarkLine 'pinnedFlag')}
                metadata=[ordered]@{objectHeaders=$preMarkObjectHeaders;methodTables=$preMarkMethodTables;firstReadAddress=(Get-MarkerField $preMarkLine 'firstMetadataReadAddress');methodTable=(Get-MarkerField $preMarkLine 'methodTable');segmentReads=(Get-MarkerField $preMarkLine 'segmentReads');gcMetadataReads=(Get-MarkerField $preMarkLine 'gcMetadataReads');markStateReads=(Get-MarkerField $preMarkLine 'markStateReads');markStateResult=(Get-MarkerField $preMarkLine 'markStateResult')}
                mutation=[ordered]@{markHelper=$preMarkHelper;callSite=(Get-MarkerField $preMarkLine 'mutationCallSite');firstMutationInstruction=(Get-MarkerField $preMarkLine 'firstMutationInstruction');boundaryReturn=$preMarkBoundaryReturn;markAttempts=(Get-MarkerField $preMarkLine 'markCallAttempts');markCalls=(Get-MarkerField $preMarkLine 'markCalls');promotionStart=(Get-MarkerField $preMarkLine 'promotionStart');promotions=(Get-MarkerField $preMarkLine 'promotions');promotionWrites=(Get-MarkerField $preMarkLine 'promotionWrites');markWrites=(Get-MarkerField $preMarkLine 'markWrites');worklistWrites=(Get-MarkerField $preMarkLine 'worklistWrites');graphTraversal=(Get-MarkerField $preMarkLine 'graphTraversal');childReferenceReads=(Get-MarkerField $preMarkLine 'childReferenceReads');objectMutation=(Get-MarkerField $preMarkLine 'objectMutation');gcMetadataMutation=(Get-MarkerField $preMarkLine 'gcMetadataMutation');segmentMutation=(Get-MarkerField $preMarkLine 'segmentMutation');relocationWrites=(Get-MarkerField $preMarkLine 'relocationWrites')}
                threadStore=[ordered]@{managedThread=(Get-MarkerField $preMarkLine 'managedThread');currentThread=(Get-MarkerField $preMarkLine 'currentThread');enumeratedThread=(Get-MarkerField $preMarkLine 'enumeratedThread');lockOwner=(Get-MarkerField $preMarkLine 'lockOwner');lockHeld=(Get-MarkerField $preMarkLine 'lockHeld');eeSuspended=(Get-MarkerField $preMarkLine 'eeSuspended');managedEntryProhibited=(Get-MarkerField $preMarkLine 'managedEntryProhibited');registeredThreads=(Get-MarkerField $preMarkLine 'registeredThreads');enumeratedThreads=(Get-MarkerField $preMarkLine 'enumeratedThreads');includedThreads=(Get-MarkerField $preMarkLine 'includedThreads');registryMutation=(Get-MarkerField $preMarkLine 'registryMutation');restart=(Get-MarkerField $preMarkLine 'restart');resume=(Get-MarkerField $preMarkLine 'resume');allocationContextsCleared=(Get-MarkerField $preMarkLine 'allocationContextsCleared')}
                scanContext=[ordered]@{fieldReads=(Get-MarkerField $validationText 'contextFieldReads');promotion=(Get-MarkerField $validationText 'contextPromotion');concurrent=(Get-MarkerField $validationText 'contextConcurrent');threadCount=(Get-MarkerField $validationText 'contextThreadCount');threadNumber=(Get-MarkerField $validationText 'contextThreadNumber');threadUnderCrawl=(Get-MarkerField $validationText 'contextThread');stackLimit=(Get-MarkerField $validationText 'contextStackLimit')}
                validation=[ordered]@{sentinelChecks=(Get-MarkerField $preMarkLine 'sentinelChecks');objectHistoryOverflow=(Get-MarkerField $preMarkLine 'objectHistoryOverflow')}
            }
        } elseif ($isFirstRootCondemnedGenerationDecision) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-condemned-generation-decision\] SAFE_STOP marker=C011EC10' "first real root condemned-generation safe-stop marker"
            Assert-Text $validationText 'callbackRequestCount=00000001 callbackCallSiteCount=00000001 callbackInvocationCount=00000001 callbackEntryCount=00000001 callbackReturnCount=00000000 secondCallbackAttempts=00000000' "exactly one real callback"
            Assert-Text $validationText 'membershipRequests=00000001 membershipEntries=00000001 membershipCompletions=00000001 membershipObjectDereferences=00000000' "one true membership prerequisite counters"
            Assert-Text $validationText 'inFindObjectRange=00000001' "true membership result"
            Assert-Text $validationText 'multipleHeaps=00000000 hpt=0000000000000000 heapOf=0000000000000000 heapNumber=00000000 heapCount=00000001 workstationSingleHeapSentinelValid=00000001 heapResolutionFailures=00000000' "valid Workstation single-heap null sentinel"
            Assert-Text $validationText 'condemnedRequests=00000001 condemnedEntries=00000001 condemnedCompletions=00000001 condemnedReturns=00000001 condemnedDuplicates=00000000' "exactly one condemned-generation decision"
            Assert-Text $validationText 'condemnedObjectDereferences=00000000 condemnedGenerationQueryStart=00000001 condemnedGenerationQueryCompletions=00000001 condemnedGenerationTableReads=00000001 condemnedSegmentLookups=00000001' "source-required generation and debug segment lookup"
            Assert-Text $validationText 'condemnedObjectHeaders=00000000 condemnedMethodTables=00000000' "zero condemned-helper object metadata reads"
            Assert-Text $validationText 'objectHeaders=00000000 methodTables=00000000 childReferenceReads=00000000 promotionStart=00000000 promotions=00000000' "zero promotion and object reads"
            Assert-Text $validationText 'markingStart=00000000 markingWrites=00000000 graphTraversal=00000000 promotionWrites=00000000 objectMutation=00000000 gcMetadataMutation=00000000 segmentMutation=00000000' "zero marking, traversal, and mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed ThreadStatic proof"
            Assert-Text $validationText 'duplicateObjectAddresses=00000000 objectHistoryOverflow=00000000' "object and duplicate validation"
            Assert-Text $validationText 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001 callbackReturns=00000000 secondCallbacks=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "suspended single-mutator stop"
            Assert-Text $validationText 'lockDepth=00000001 registeredManagedThreads=00000001 currentThreadRegistered=00000001 currentThreadIsInitiator=00000001 currentAndInitiatorMatch=00000001 enumeratedThreads=00000001 includedThreads=00000001 duplicateThreads=00000000 allocationContextsVisited=00000001 allocationContextsChanged=00000001 allocationContextsCleared=00000001' "thread and fixup invariants"
            $callbackLoadedRoot = Get-MarkerField $validationText 'callbackLoadedRoot'
            $condemnedObject = Get-MarkerField $validationText 'condemnedCheckObject'
            $membershipObject = Get-MarkerField $validationText 'membershipObject'
            $heapObject = Get-MarkerField $validationText 'heapResolutionObject'
            $storageObject = Get-MarkerField $validationText 'storageObject'
            $rootSlot = Get-MarkerField $validationText 'rootSlot'
            $membershipLower = Get-MarkerField $validationText 'membershipLowerBound'
            $membershipUpper = Get-MarkerField $validationText 'membershipUpperBound'
            if ($callbackLoadedRoot -ne $condemnedObject -or $condemnedObject -ne $membershipObject -or $membershipObject -ne $heapObject -or $heapObject -ne $storageObject -or (Get-MarkerField $validationText 'callbackRootMatches') -ne '0x00000001' -or (Get-MarkerField $validationText 'membershipMatches') -ne '0x00000001' -or (Get-MarkerField $validationText 'heapResolutionMatches') -ne '0x00000001') { throw "Condemned-check input did not equal the genuine callback root in $name." }
            $objectValue = [Convert]::ToUInt64($condemnedObject.Substring(2), 16)
            $lowerValue = [Convert]::ToUInt64($membershipLower.Substring(2), 16)
            $upperValue = [Convert]::ToUInt64($membershipUpper.Substring(2), 16)
            if ($lowerValue -gt $objectValue -or $objectValue -ge $upperValue) { throw "Condemned-check root was outside the recorded membership bounds in $name." }
            $condemnedResult = Get-MarkerField $validationText 'condemnedResult'
            $generation = Get-MarkerField $validationText 'generationFromRegion'
            $condemnedGeneration = Get-MarkerField $validationText 'condemnedGeneration'
            $maximumGeneration = Get-MarkerField $validationText 'maximumGeneration'
            $expectedResult = if ([Convert]::ToUInt64($condemnedGeneration.Substring(2),16) -eq [Convert]::ToUInt64($maximumGeneration.Substring(2),16) -or [Convert]::ToUInt64($generation.Substring(2),16) -le [Convert]::ToUInt64($condemnedGeneration.Substring(2),16)) { '0x00000001' } else { '0x00000000' }
            if ($condemnedResult -ne $expectedResult) { throw "Condemned-check result did not match the locked generation comparison in $name." }
            $nextSourceOperation = if ($condemnedResult -eq '0x00000001') { 'GCHeap::Promote true branch dprintf @ gc.cpp:49507' } else { 'GCHeap::Promote false branch return @ gc.cpp:49504' }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC10"; outcome=if ($condemnedResult -eq '0x00000001') { "A" } else { "B" }; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'callbackRequestCount'); callbackEntryCount=(Get-MarkerField $validationText 'callbackEntryCount'); callbackReturnCount=(Get-MarkerField $validationText 'callbackReturnCount'); duplicateCallbacks=(Get-MarkerField $validationText 'secondCallbackAttempts')
                root=[ordered]@{slot=$rootSlot;rawValue=$callbackLoadedRoot;condemnedObject=$condemnedObject;storageObject=$storageObject;sentinel=(Get-MarkerField $validationText 'sentinelAddress');sentinelReadback=(Get-MarkerField $validationText 'sentinelReadback')}
                membership=[ordered]@{object=$membershipObject;lowerBound=$membershipLower;upperBound=$membershipUpper;result=(Get-MarkerField $validationText 'inFindObjectRange')}
                heapResolution=[ordered]@{input=$heapObject;hpt=(Get-MarkerField $validationText 'hpt');heap=(Get-MarkerField $validationText 'heapOf');heapNumber=(Get-MarkerField $validationText 'heapNumber');heapCount=(Get-MarkerField $validationText 'heapCount');multipleHeaps=(Get-MarkerField $validationText 'multipleHeaps');singleHeapSentinelValid=(Get-MarkerField $validationText 'workstationSingleHeapSentinelValid');failures=(Get-MarkerField $validationText 'heapResolutionFailures')}
                condemnedCheck=[ordered]@{requests=(Get-MarkerField $validationText 'condemnedRequests');entries=(Get-MarkerField $validationText 'condemnedEntries');completions=(Get-MarkerField $validationText 'condemnedCompletions');returns=(Get-MarkerField $validationText 'condemnedReturns');duplicates=(Get-MarkerField $validationText 'condemnedDuplicates');object=$condemnedObject;lowerBound=(Get-MarkerField $validationText 'condemnedLowerBound');upperBound=(Get-MarkerField $validationText 'condemnedUpperBound');condemnedGeneration=$condemnedGeneration;maximumGeneration=$maximumGeneration;generation=$generation;generationTable=(Get-MarkerField $validationText 'generationTable');generationTableIndex=(Get-MarkerField $validationText 'generationTableIndex');segment=(Get-MarkerField $validationText 'condemnedSegmentIdentity');segmentLookups=(Get-MarkerField $validationText 'condemnedSegmentLookups');result=$condemnedResult;branch=(Get-MarkerField $validationText 'condemnedBranch');nextSourceOperation=$nextSourceOperation}
                counters=[ordered]@{generationQuery=(Get-MarkerField $validationText 'condemnedGenerationQueryStart');generationTableReads=(Get-MarkerField $validationText 'condemnedGenerationTableReads');segmentLookup=(Get-MarkerField $validationText 'condemnedSegmentLookups');objectDereferences=(Get-MarkerField $validationText 'condemnedObjectDereferences');objectHeaders=(Get-MarkerField $validationText 'condemnedObjectHeaders');methodTables=(Get-MarkerField $validationText 'condemnedMethodTables');promotion=(Get-MarkerField $validationText 'promotions');marking=(Get-MarkerField $validationText 'markingStart');graph=(Get-MarkerField $validationText 'graphTraversal')}
                mutation=[ordered]@{mark=(Get-MarkerField $validationText 'markingWrites');promotion=(Get-MarkerField $validationText 'promotionWrites');object=(Get-MarkerField $validationText 'objectMutation');gc=(Get-MarkerField $validationText 'gcMetadataMutation');segment=(Get-MarkerField $validationText 'segmentMutation')}
                managedProofRoot=[ordered]@{assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount');clearCount=(Get-MarkerField $validationText 'managedClearCount');readbackCount=(Get-MarkerField $validationText 'managedReadbackCount');assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid');readbackValid=(Get-MarkerField $validationText 'managedReadbackValid');sentinelAddress=(Get-MarkerField $validationText 'sentinelAddress');sentinelReadback=(Get-MarkerField $validationText 'sentinelReadback');storageObject=$storageObject;objectBefore=(Get-MarkerField $validationText 'objectBefore');objectAfter=(Get-MarkerField $validationText 'objectAfter');objectAtStop=(Get-MarkerField $validationText 'objectAtStop');sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks');objectHistoryOverflow=(Get-MarkerField $validationText 'objectHistoryOverflow')}
                threadStore=[ordered]@{managedThread=(Get-MarkerField $validationText 'managedThread');currentThread=(Get-MarkerField $validationText 'currentThread');lockOwner=(Get-MarkerField $validationText 'lockOwner');lockHeld=(Get-MarkerField $validationText 'lockHeld');eeSuspended=(Get-MarkerField $validationText 'eeSuspended');managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited');lockDepth=(Get-MarkerField $validationText 'lockDepth');registeredManagedThreads=(Get-MarkerField $validationText 'registeredManagedThreads');enumeratedThreads=(Get-MarkerField $validationText 'enumeratedThreads');includedThreads=(Get-MarkerField $validationText 'includedThreads')}
                restartRequests=(Get-MarkerField $validationText 'restartRequests');restartEntries=(Get-MarkerField $validationText 'restartEntries');managedResume=(Get-MarkerField $validationText 'managedResume')
            }
        } elseif ($isFirstRootHeapResolution) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-heap-resolution\] SAFE_STOP marker=C011EC09' "first real root heap-resolution safe-stop marker"
            Assert-Text $validationText 'callbackRequestCount=00000001 callbackCallSiteCount=00000001 callbackInvocationCount=00000001 callbackEntryCount=00000001 callbackReturnCount=00000000 secondCallbackAttempts=00000000' "exactly one real callback"
            Assert-Text $validationText 'membershipRequests=00000001 membershipEntries=00000001 membershipCompletions=00000001 membershipReturns=00000000 membershipObjectDereferences=00000000' "exactly one membership prerequisite"
            Assert-Text $validationText 'lowerResult=00000001 upperResult=00000001 inFindObjectRange=00000001' "true managed-range membership"
            Assert-Text $validationText 'heapResolutionRequests=00000001 heapResolutionEntries=00000001 heapResolutionCompletions=00000001 heapResolutionDuplicates=00000000' "exactly one real heap-resolution transition"
            Assert-Text $validationText 'heapNumber=00000000 totalHeapCount=00000001 threadNumber=00000000' "source-defined Workstation heap identity"
            Assert-Text $validationText 'objectAddressConsulted=00000000 threadStateConsulted=00000000 heapTableReads=00000000 heapTableIdentity=0000000000000000 heapTableSlot=0000000000000000 segmentMapReads=00000000 segmentIdentity=0000000000000000 brickCardReads=00000000 rangeReads=00000000' "heap_of source contract has no object/table/segment bookkeeping reads"
            Assert-Text $validationText 'candidateClassification=00000000 generationClassificationStart=00000000 generationQueryStart=00000000 condemnedGenerationComparisons=00000000 ephemeralGenerationComparisons=00000000' "stopped before generation logic"
            Assert-Text $validationText 'postResolutionSegmentLookup=00000000 objectHeaders=00000000 methodTables=00000000 childReferenceReads=00000000 promotionStart=00000000 promotions=00000000 markingStart=00000000 graphTraversal=00000000' "zero post-resolution processing"
            Assert-Text $validationText 'markWrites=00000000 promotionWrites=00000000 objectMutation=00000000 gcMetadataMutation=00000000 segmentMetadataMutation=00000000' "zero mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed ThreadStatic proof"
            Assert-Text $validationText 'objectHistoryOverflow=00000000 duplicateObjectAddresses=00000000' "object and duplicate validation"
            Assert-Text $validationText 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "suspension invariant"
            Assert-Text $validationText 'managedThread=[0-9A-F]{16} currentThread=[0-9A-F]{16} lockOwner=[0-9A-F]{16}' "thread identity evidence"
            Assert-Text $validationText 'callbackContext=[0-9A-F]{16} contextFieldReads=00000006 contextThread=[0-9A-F]{16} contextStackLimit=0000000000000000 contextThreadNumber=0000000000000000 contextThreadCount=0000000000000001 contextPromotion=00000001 contextConcurrent=00000000' "live ScanContext"
            Assert-Text $validationText 'lockDepth=00000001 registeredManagedThreads=00000001 currentThreadRegistered=00000001 currentThreadIsInitiator=00000001 currentAndInitiatorMatch=00000001' "thread-store lock and initiator state"
            Assert-Text $validationText 'registeredThreadCountBeforeRoot=00000001 registeredThreadCountAfterRoot=00000001 enumeratedThreads=00000001 includedThreads=00000001 excludedThreads=00000000 duplicateThreads=00000000 registryMutationBefore=00000000 registryMutationAfter=00000000' "thread enumeration and registry invariants"
            Assert-Text $validationText 'allocationContextsVisited=00000001 allocationContextsChanged=00000001 allocationContextsCleared=00000001' "allocation-context fixup state"
            $managedThread = Get-MarkerField $validationText 'managedThread'
            $currentThread = Get-MarkerField $validationText 'currentThread'
            $lockOwner = Get-MarkerField $validationText 'lockOwner'
            if ($managedThread -ne $currentThread -or $currentThread -ne $lockOwner) { throw "Thread-store identities diverged in $name." }
            Assert-Text $validationText 'restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "zero restart and resume"
            $callbackLoadedRoot = Get-MarkerField $validationText 'callbackLoadedRoot'
            $membershipObject = Get-MarkerField $validationText 'membershipObject'
            $heapObject = Get-MarkerField $validationText 'heapResolutionObject'
            $storageObject = Get-MarkerField $validationText 'storageObject'
            if ($callbackLoadedRoot -ne $membershipObject -or $membershipObject -ne $heapObject -or $heapObject -ne $storageObject -or (Get-MarkerField $validationText 'objectMatchesCallbackRoot') -ne '0x00000001' -or (Get-MarkerField $validationText 'objectMatchesMembershipObject') -ne '0x00000001') { throw "Heap-resolution input did not equal the genuine callback root in $name." }
            $heapSucceeded = Get-MarkerField $validationText 'heapResolutionSucceeded'
            $heapFailures = Get-MarkerField $validationText 'heapResolutionFailures'
            $resolvedHeap = Get-MarkerField $validationText 'resolvedHeap'
            if ($heapSucceeded -eq '0x00000001') {
                if ($heapFailures -ne '0x00000000' -or $resolvedHeap -eq '0x0000000000000000') { throw "Successful heap resolution had inconsistent result fields in $name." }
            } else {
                if ($heapFailures -ne '0x00000001' -or $resolvedHeap -ne '0x0000000000000000') { throw "Failed heap resolution did not stop with the source result in $name." }
            }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC09"; outcome=if ($heapSucceeded -eq '0x00000001') { "A" } else { "B" }; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'callbackRequestCount'); callbackEntryCount=(Get-MarkerField $validationText 'callbackEntryCount'); callbackReturnCount=(Get-MarkerField $validationText 'callbackReturnCount'); duplicateCallbacks=(Get-MarkerField $validationText 'secondCallbackAttempts')
                root=[ordered]@{slot=(Get-MarkerField $validationText 'rootSlot'); loaded=$callbackLoadedRoot; storage=$storageObject; sentinel=(Get-MarkerField $validationText 'sentinelAddress')}
                membership=[ordered]@{object=$membershipObject; lowerBound=(Get-MarkerField $validationText 'lowerBound'); upperBound=(Get-MarkerField $validationText 'upperBound'); result=(Get-MarkerField $validationText 'inFindObjectRange'); requests=(Get-MarkerField $validationText 'membershipRequests'); entries=(Get-MarkerField $validationText 'membershipEntries'); completions=(Get-MarkerField $validationText 'membershipCompletions')}
                heapResolution=[ordered]@{requests=(Get-MarkerField $validationText 'heapResolutionRequests'); entries=(Get-MarkerField $validationText 'heapResolutionEntries'); completions=(Get-MarkerField $validationText 'heapResolutionCompletions'); duplicates=(Get-MarkerField $validationText 'heapResolutionDuplicates'); failures=$heapFailures; succeeded=$heapSucceeded; input=$heapObject; pointer=$resolvedHeap; heapNumber=(Get-MarkerField $validationText 'heapNumber'); totalHeapCount=(Get-MarkerField $validationText 'totalHeapCount'); threadHeap=(Get-MarkerField $validationText 'heapResolutionThreadHeap'); heapTableReads=(Get-MarkerField $validationText 'heapTableReads'); heapTableIdentity=(Get-MarkerField $validationText 'heapTableIdentity'); heapTableSlot=(Get-MarkerField $validationText 'heapTableSlot'); segmentMapReads=(Get-MarkerField $validationText 'segmentMapReads'); segment=(Get-MarkerField $validationText 'segmentIdentity'); failureReason=(Get-MarkerField $validationText 'failureReason'); completionReturnAddress=(Get-MarkerField $validationText 'heapResolutionCompletionReturnAddress')}
                counters=[ordered]@{generationClassification=(Get-MarkerField $validationText 'generationClassificationStart'); generationQuery=(Get-MarkerField $validationText 'generationQueryStart'); condemned=(Get-MarkerField $validationText 'condemnedGenerationComparisons'); ephemeral=(Get-MarkerField $validationText 'ephemeralGenerationComparisons'); segmentLookup=(Get-MarkerField $validationText 'postResolutionSegmentLookup'); headers=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); promotion=(Get-MarkerField $validationText 'promotionStart'); marking=(Get-MarkerField $validationText 'markingStart'); graph=(Get-MarkerField $validationText 'graphTraversal')}
                mutation=[ordered]@{mark=(Get-MarkerField $validationText 'markWrites'); promotion=(Get-MarkerField $validationText 'promotionWrites'); object=(Get-MarkerField $validationText 'objectMutation'); gc=(Get-MarkerField $validationText 'gcMetadataMutation'); segment=(Get-MarkerField $validationText 'segmentMetadataMutation')}
                threadStore=[ordered]@{managedThread=$managedThread; currentThread=$currentThread; lockOwner=$lockOwner; lockHeld=(Get-MarkerField $validationText 'lockHeld'); lockDepth=(Get-MarkerField $validationText 'lockDepth'); registeredManagedThreads=(Get-MarkerField $validationText 'registeredManagedThreads'); currentThreadRegistered=(Get-MarkerField $validationText 'currentThreadRegistered'); currentThreadIsInitiator=(Get-MarkerField $validationText 'currentThreadIsInitiator'); currentAndInitiatorMatch=(Get-MarkerField $validationText 'currentAndInitiatorMatch'); registeredThreadCountBeforeRoot=(Get-MarkerField $validationText 'registeredThreadCountBeforeRoot'); registeredThreadCountAfterRoot=(Get-MarkerField $validationText 'registeredThreadCountAfterRoot'); enumeratedThreads=(Get-MarkerField $validationText 'enumeratedThreads'); includedThreads=(Get-MarkerField $validationText 'includedThreads'); excludedThreads=(Get-MarkerField $validationText 'excludedThreads'); duplicateThreads=(Get-MarkerField $validationText 'duplicateThreads'); registryMutationBefore=(Get-MarkerField $validationText 'registryMutationBefore'); registryMutationAfter=(Get-MarkerField $validationText 'registryMutationAfter'); eeSuspended=(Get-MarkerField $validationText 'eeSuspended'); managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited')}
                allocationContextFixup=[ordered]@{visited=(Get-MarkerField $validationText 'allocationContextsVisited'); changed=(Get-MarkerField $validationText 'allocationContextsChanged'); cleared=(Get-MarkerField $validationText 'allocationContextsCleared')}
                scanContext=[ordered]@{address=(Get-MarkerField $validationText 'callbackContext'); fieldReads=(Get-MarkerField $validationText 'contextFieldReads'); thread=(Get-MarkerField $validationText 'contextThread'); stackLimit=(Get-MarkerField $validationText 'contextStackLimit'); promotion=(Get-MarkerField $validationText 'contextPromotion'); concurrent=(Get-MarkerField $validationText 'contextConcurrent'); threadCount=(Get-MarkerField $validationText 'contextThreadCount'); threadNumber=(Get-MarkerField $validationText 'contextThreadNumber')}
            }
        } elseif ($isFirstRootMembershipClassification) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-membership-classification\] SAFE_STOP marker=C011EC08' "first real root membership safe-stop marker"
            Assert-Text $validationText 'callbackRequestCount=00000001'; Assert-Text $validationText 'callbackCallSiteCount=00000001'; Assert-Text $validationText 'callbackInvocationCount=00000001'; Assert-Text $validationText 'callbackEntryCount=00000001'; Assert-Text $validationText 'callbackReturnCount=00000000'; Assert-Text $validationText 'secondCallbackAttempts=00000000' "exactly one real callback"
            Assert-Text $validationText 'membershipRequests=00000001'; Assert-Text $validationText 'membershipEntries=00000001'; Assert-Text $validationText 'membershipCompletions=00000001'; Assert-Text $validationText 'membershipReturns=00000001'; Assert-Text $validationText 'duplicateChecks=00000000'; Assert-Text $validationText 'objectDereferences=00000000' "exactly one real membership check"
            Assert-Text $validationText 'lowerEvaluated=00000001'; Assert-Text $validationText 'upperEvaluated=00000001'; Assert-Text $validationText 'lowerResult=00000001'; Assert-Text $validationText 'upperResult=00000001'; Assert-Text $validationText 'inFindObjectRange=00000001'; Assert-Text $validationText 'sourceBranch=00000001' "true range comparisons and result"
            Assert-Text $validationText 'heapIdentity=0000000000000000'; Assert-Text $validationText 'heapFieldReads=00000001'; Assert-Text $validationText 'segmentIdentity=0000000000000000'; Assert-Text $validationText 'segmentLookupCount=00000000'; Assert-Text $validationText 'segmentLookupSucceeded=00000000' "one consulted per-heap boundary field and no segment lookup"
            Assert-Text $validationText 'candidateClassification=00000000'; Assert-Text $validationText 'generationClassificationStart=00000000'; Assert-Text $validationText 'generationQueryStart=00000000'; Assert-Text $validationText 'condemnedGenerationComparisons=00000000' "stopped before generation classification"
            Assert-Text $validationText 'postMembershipSegmentLookup=00000000'; Assert-Text $validationText 'objectHeaders=00000000'; Assert-Text $validationText 'methodTables=00000000'; Assert-Text $validationText 'promotionStart=00000000'; Assert-Text $validationText 'promotions=00000000'; Assert-Text $validationText 'markingStart=00000000'; Assert-Text $validationText 'graphTraversal=00000000' "zero post-membership processing"
            Assert-Text $validationText 'markWrites=00000000'; Assert-Text $validationText 'promotionWrites=00000000'; Assert-Text $validationText 'objectMutation=00000000'; Assert-Text $validationText 'gcMetadataMutation=00000000'; Assert-Text $validationText 'segmentMetadataMutation=00000000' "zero mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001'; Assert-Text $validationText 'managedClearCount=00000000'; Assert-Text $validationText 'managedReadbackCount=00000001'; Assert-Text $validationText 'managedAssignmentValid=00000001'; Assert-Text $validationText 'managedReadbackValid=00000001' "managed ThreadStatic proof"
            Assert-Text $validationText 'sentinelAddress=[0-9A-F]{16}'; Assert-Text $validationText 'sentinelReadback=[0-9A-F]{16}' "sentinel validation"
            Assert-Text $validationText 'sentinelChecks=[0-9A-F]{8}'; Assert-Text $validationText 'objectBefore=[0-9A-F]{8}'; Assert-Text $validationText 'objectAfter=[0-9A-F]{8}'; Assert-Text $validationText 'objectAtStop=[0-9A-F]{8}'; Assert-Text $validationText 'objectHistoryOverflow=00000000' "object validation"
            Assert-Text $validationText 'context=[0-9A-F]{16}'; Assert-Text $validationText 'contextFieldReads=00000006'; Assert-Text $validationText 'contextThread=[0-9A-F]{16}'; Assert-Text $validationText 'contextStackLimit=[0-9A-F]{16}'; Assert-Text $validationText 'contextThreadNumber=[0-9A-F]{16}'; Assert-Text $validationText 'contextThreadCount=[0-9A-F]{16}'; Assert-Text $validationText 'contextPromotion=00000001'; Assert-Text $validationText 'contextConcurrent=00000000' "live ScanContext"
            Assert-Text $validationText 'lockHeld=00000001'; Assert-Text $validationText 'eeSuspended=00000001'; Assert-Text $validationText 'managedEntryProhibited=00000001' "suspension invariant"
            Assert-Text $validationText 'restartRequests=00000000'; Assert-Text $validationText 'restartEntries=00000000'; Assert-Text $validationText 'managedResume=00000000' "zero restart and resume"
            $callbackSiteSlot=Get-MarkerField $validationText 'callbackSiteSlot'; $callbackSiteRaw=Get-MarkerField $validationText 'callbackSiteRaw'; $callbackSiteContext=Get-MarkerField $validationText 'callbackSiteContext'; $callbackSiteCallback=Get-MarkerField $validationText 'callbackSiteCallback'; $callbackEntryAddress=Get-MarkerField $validationText 'callbackEntryAddress'
            $callbackArg1=Get-MarkerField $validationText 'arg1'; $callbackArg2=Get-MarkerField $validationText 'arg2'; $callbackArg3=Get-MarkerField $validationText 'arg3'; $callbackRawRcx=Get-MarkerField $validationText 'rawRcx'; $callbackRawRdx=Get-MarkerField $validationText 'rawRdx'; $callbackRawR8=Get-MarkerField $validationText 'rawR8'; $rootSlot=Get-MarkerField $validationText 'rootSlot'; $callbackLoadedRoot=Get-MarkerField $validationText 'callbackLoadedRoot'; $membershipObject=Get-MarkerField $validationText 'membershipObject'; $storageObject=Get-MarkerField $validationText 'storageObject'; $membershipLower=Get-MarkerField $validationText 'lowerBound'; $membershipUpper=Get-MarkerField $validationText 'upperBound'
            $objectValue=[Convert]::ToUInt64($membershipObject.Substring(2),16); $lowerValue=[Convert]::ToUInt64($membershipLower.Substring(2),16); $upperValue=[Convert]::ToUInt64($membershipUpper.Substring(2),16)
            if ($callbackArg1 -ne $callbackRawRcx -or $callbackArg2 -ne $callbackRawRdx -or $callbackArg3 -ne $callbackRawR8 -or $callbackArg1 -ne $callbackSiteSlot -or $callbackArg1 -ne $rootSlot) { throw "Membership callback ABI/root slot mismatch in $name." }
            if ($callbackLoadedRoot -ne $callbackSiteRaw -or $callbackLoadedRoot -ne $storageObject -or $membershipObject -ne $callbackLoadedRoot -or (Get-MarkerField $validationText 'objectMatchesCallbackRoot') -ne '0x00000001') { throw "Membership input did not equal callback-loaded storage object in $name." }
            if ($callbackArg2 -ne $callbackSiteContext -or $callbackSiteCallback -ne $callbackEntryAddress -or $lowerValue -gt $objectValue -or $objectValue -ge $upperValue) { throw "Membership callback/range identity mismatch in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC08"; outcome="A"; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'callbackRequestCount'); callbackCallSiteCount=(Get-MarkerField $validationText 'callbackCallSiteCount'); callbackInvocationCount=(Get-MarkerField $validationText 'callbackInvocationCount'); callbackEntryCount=(Get-MarkerField $validationText 'callbackEntryCount'); callbackReturnCount=(Get-MarkerField $validationText 'callbackReturnCount'); duplicateCallbackInvocations=(Get-MarkerField $validationText 'secondCallbackAttempts')
                callbackSite=[ordered]@{slot=$callbackSiteSlot;rawValue=$callbackSiteRaw;scanContext=$callbackSiteContext;callback=$callbackSiteCallback}
                callbackEntry=[ordered]@{address=$callbackEntryAddress;returnAddress=(Get-MarkerField $validationText 'callbackEntryReturn');rawRcx=$callbackRawRcx;rawRdx=$callbackRawRdx;rawR8=$callbackRawR8;arg1=$callbackArg1;arg2=$callbackArg2;arg3=$callbackArg3}
                root=[ordered]@{slot=$rootSlot;rawValue=$callbackLoadedRoot;loadedValue=$callbackLoadedRoot;storageObject=$storageObject;sentinel=(Get-MarkerField $validationText 'sentinelAddress')}
                scanContext=[ordered]@{address=(Get-MarkerField $validationText 'callbackContext');threadUnderCrawl=(Get-MarkerField $validationText 'contextThread');stackLimit=(Get-MarkerField $validationText 'contextStackLimit');threadNumber=(Get-MarkerField $validationText 'contextThreadNumber');threadCount=(Get-MarkerField $validationText 'contextThreadCount');promotion=(Get-MarkerField $validationText 'contextPromotion');concurrent=(Get-MarkerField $validationText 'contextConcurrent')}
                membership=[ordered]@{requests=(Get-MarkerField $validationText 'membershipRequests');entries=(Get-MarkerField $validationText 'membershipEntries');completions=(Get-MarkerField $validationText 'membershipCompletions');returns=(Get-MarkerField $validationText 'membershipReturns');duplicates=(Get-MarkerField $validationText 'duplicateChecks');object=$membershipObject;lowerBound=$membershipLower;upperBound=$membershipUpper;lowerEvaluated=(Get-MarkerField $validationText 'lowerEvaluated');upperEvaluated=(Get-MarkerField $validationText 'upperEvaluated');lowerResult=(Get-MarkerField $validationText 'lowerResult');upperResult=(Get-MarkerField $validationText 'upperResult');result=(Get-MarkerField $validationText 'inFindObjectRange');sourceBranch=(Get-MarkerField $validationText 'sourceBranch');completionReturnAddress=(Get-MarkerField $validationText 'membershipCompletionReturnAddress');postCheckReturnAddress=(Get-MarkerField $validationText 'membershipPostCheckReturnAddress')}
                prohibited=[ordered]@{generationClassificationStart=(Get-MarkerField $validationText 'generationClassificationStart');generationQueryStart=(Get-MarkerField $validationText 'generationQueryStart');condemnedGenerationComparisons=(Get-MarkerField $validationText 'condemnedGenerationComparisons');postMembershipSegmentLookup=(Get-MarkerField $validationText 'postMembershipSegmentLookup');objectDereferences=(Get-MarkerField $validationText 'objectDereferences');objectHeaders=(Get-MarkerField $validationText 'objectHeaders');methodTables=(Get-MarkerField $validationText 'methodTables');promotionStart=(Get-MarkerField $validationText 'promotionStart');promotions=(Get-MarkerField $validationText 'promotions');markingStart=(Get-MarkerField $validationText 'markingStart');graphTraversal=(Get-MarkerField $validationText 'graphTraversal')}
                mutation=[ordered]@{markWrites=(Get-MarkerField $validationText 'markWrites');promotionWrites=(Get-MarkerField $validationText 'promotionWrites');objectMemory=(Get-MarkerField $validationText 'objectMutation');gcMetadata=(Get-MarkerField $validationText 'gcMetadataMutation');segmentMetadata=(Get-MarkerField $validationText 'segmentMetadataMutation')}
                managedProofRoot=[ordered]@{assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount');clearCount=(Get-MarkerField $validationText 'managedClearCount');readbackCount=(Get-MarkerField $validationText 'managedReadbackCount');assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid');readbackValid=(Get-MarkerField $validationText 'managedReadbackValid');sentinelAddress=(Get-MarkerField $validationText 'sentinelAddress');sentinelReadback=(Get-MarkerField $validationText 'sentinelReadback');storageObject=$storageObject;objectBefore=(Get-MarkerField $validationText 'objectBefore');objectAfter=(Get-MarkerField $validationText 'objectAfter');objectAtStop=(Get-MarkerField $validationText 'objectAtStop');sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks');objectHistoryOverflow=(Get-MarkerField $validationText 'objectHistoryOverflow')}
                threadStore=[ordered]@{managedThread=(Get-MarkerField $validationText 'managedThread');currentThread=(Get-MarkerField $validationText 'currentThread');lockOwner=(Get-MarkerField $validationText 'lockOwner');lockHeld=(Get-MarkerField $validationText 'lockHeld');eeSuspended=(Get-MarkerField $validationText 'eeSuspended');managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited')}
                restartRequests=(Get-MarkerField $validationText 'restartRequests');restartEntries=(Get-MarkerField $validationText 'restartEntries');managedResume=(Get-MarkerField $validationText 'managedResume')
            }
        } elseif ($isFirstRootCallbackEntry) {
            Assert-Text $validationText '\[nativeaot-gc-first-root-callback-entry\] SAFE_STOP marker=C011EC07' "first real callback-entry safe-stop marker"
            Assert-Text $validationText 'requestCount=00000001 callSiteCount=00000001 invocationCount=00000001 entryCount=00000001 returnCount=00000000 duplicateInvocations=00000000' "exactly one callback request, call-site entry, invocation, and callback entry"
            Assert-Text $validationText 'callbackRootLoads=00000001 callbackLoadedRaw=[0-9A-F]{16} nullTests=00000000 nullNonNull=00000000' "one locked callback-side root load and no null test"
            Assert-Text $validationText 'contextFieldReads=00000006 context=[0-9A-F]{16} contextThread=[0-9A-F]{16} contextStackLimit=[0-9A-F]{16} contextThreadNumber=[0-9A-F]{16} contextThreadCount=[0-9A-F]{16} contextPromotion=00000001 contextConcurrent=00000000' "ScanContext fields read without mutation"
            Assert-Text $validationText 'entryArgsMatch=00000001 slotMatch=00000001 rawMatchesStorage=00000001 contextMatch=00000001 flagsMatch=00000001' "live callback ABI argument validation"
            Assert-Text $validationText 'candidateClassification=00000000 heapMembership=00000000 segmentLookup=00000000 objectHeaders=00000000 methodTables=00000000' "stop before candidate classification and metadata access"
            Assert-Text $validationText 'promotionStart=00000000 promotions=00000000 markingStart=00000000 graphTraversal=00000000 markWrites=00000000 promotionWrites=00000000 objectMutation=00000000 gcMetadataMutation=00000000 segmentMetadataMutation=00000000' "zero promotion, marking, graph traversal, and mutation"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed ThreadStatic assignment and readback"
            Assert-Text $validationText 'threadStaticInitialization=0000000[23] sentinelOrdinal=00000000 sentinelAddress=[0-9A-F]{16} sentinelSize=0000000000001000 readbackAddress=[0-9A-F]{16} readbackExactMatch=00000001' "selected sentinel validation"
            Assert-Text $validationText 'firstNonNullSlot=[0-9A-F]{16} firstNonNullValue=[0-9A-F]{16} expectedStorageObject=[0-9A-F]{16} expectedSentinel=[0-9A-F]{16}' "real storage root identity"
            Assert-Text $validationText 'storageObject=[0-9A-F]{16} inlinedRoot=[0-9A-F]{16} objectBefore=00000025 objectAfter=00000025 objectAtStop=00000025 sentinelChecks=00000094 objectHistoryOverflow=00000000' "object and runtime ThreadStatic storage validation"
            Assert-Text $validationText 'lockHeld=00000001 eeSuspended=00000001 managedEntryProhibited=00000001' "thread-store lock and suspended EE invariant"
            Assert-Text $validationText 'restartRequests=00000000 restartEntries=00000000 managedResume=00000000 fixupFailures=00000000 rootFailures=00000000' "zero restart, resume, fixup, and root failures"
            $callbackSiteSlot = Get-MarkerField $validationText 'callbackSiteSlot'
            $callbackSiteRaw = Get-MarkerField $validationText 'callbackSiteRaw'
            $callbackSiteContext = Get-MarkerField $validationText 'callbackSiteContext'
            $callbackSiteCallback = Get-MarkerField $validationText 'callbackSiteCallback'
            $callbackEntryAddress = Get-MarkerField $validationText 'callbackEntryAddress'
            $callbackArg1 = Get-MarkerField $validationText 'arg1'
            $callbackArg2 = Get-MarkerField $validationText 'arg2'
            $callbackArg3 = Get-MarkerField $validationText 'arg3'
            $callbackRawRcx = Get-MarkerField $validationText 'rawRcx'
            $callbackRawRdx = Get-MarkerField $validationText 'rawRdx'
            $callbackRawR8 = Get-MarkerField $validationText 'rawR8'
            $firstNonNullSlot = Get-MarkerField $validationText 'firstNonNullSlot'
            $firstNonNullValue = Get-MarkerField $validationText 'firstNonNullValue'
            $expectedStorage = Get-MarkerField $validationText 'expectedStorageObject'
            $callbackLoadedRaw = Get-MarkerField $validationText 'callbackLoadedRaw'
            $context = Get-MarkerField $validationText 'context'
            $expectedFlags = Get-MarkerField $validationText 'expectedFlags'
            $actualFlags = Get-MarkerField $validationText 'actualFlags'
            $managedThread = Get-MarkerField $validationText 'managedThread'
            $currentThread = Get-MarkerField $validationText 'currentThread'
            $lockOwner = Get-MarkerField $validationText 'lockOwner'
            $callbackArg3Value = [Convert]::ToUInt64($callbackArg3.Substring(2), 16)
            $expectedFlagsValue = [Convert]::ToUInt64($expectedFlags.Substring(2), 16)
            $actualFlagsValue = [Convert]::ToUInt64($actualFlags.Substring(2), 16)
            if ($callbackArg1 -ne $callbackRawRcx -or $callbackArg2 -ne $callbackRawRdx -or $callbackArg3 -ne $callbackRawR8) { throw "Raw AMD64 callback registers did not normalize identically in $name." }
            if ($callbackArg1 -ne $callbackSiteSlot -or $callbackArg1 -ne $firstNonNullSlot) { throw "Callback argument 1 did not equal the real inline root slot in $name." }
            if ($callbackLoadedRaw -ne $callbackSiteRaw -or $callbackLoadedRaw -ne $firstNonNullValue -or $callbackLoadedRaw -ne $expectedStorage) { throw "Callback-side root load did not equal the real NativeAOT storage object in $name." }
            if ($callbackArg2 -ne $callbackSiteContext -or $callbackArg2 -ne $context) { throw "Callback argument 2 did not equal the live ScanContext in $name." }
            if ($callbackArg3Value -ne $expectedFlagsValue -or $callbackArg3Value -ne $actualFlagsValue) { throw "Callback argument 3 did not equal the expected root metadata flags in $name." }
            if ($callbackSiteCallback -ne $callbackEntryAddress) { throw "Callback call-site identity did not equal the actual GCHeap::Promote entry address in $name." }
            if ($managedThread -ne $currentThread -or $managedThread -ne $lockOwner) { throw "Managed/current/lock-owner thread identities diverged at callback entry in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC07"; outcome="A"; harnessTerminated=$true
                callbackRequestCount=(Get-MarkerField $validationText 'requestCount'); callbackCallSiteCount=(Get-MarkerField $validationText 'callSiteCount'); callbackInvocationCount=(Get-MarkerField $validationText 'invocationCount'); callbackEntryCount=(Get-MarkerField $validationText 'entryCount'); callbackReturnCount=(Get-MarkerField $validationText 'returnCount'); duplicateCallbackInvocations=(Get-MarkerField $validationText 'duplicateInvocations')
                callbackSite=[ordered]@{ slot=$callbackSiteSlot; rawValue=$callbackSiteRaw; scanContext=$callbackSiteContext; callback=$callbackSiteCallback; returnAddress=(Get-MarkerField $validationText 'callbackSiteReturn') }
                callbackEntry=[ordered]@{ address=$callbackEntryAddress; returnAddress=(Get-MarkerField $validationText 'callbackEntryReturn'); stackPointer=(Get-MarkerField $validationText 'callbackEntryRsp'); rawRcx=$callbackRawRcx; rawRdx=$callbackRawRdx; rawR8=$callbackRawR8; arg1=$callbackArg1; arg2=$callbackArg2; arg3=$callbackArg3 }
                root=[ordered]@{ slot=(Get-MarkerField $validationText 'rootSlot'); rawValue=(Get-MarkerField $validationText 'rootRaw'); callbackSideLoadCount=(Get-MarkerField $validationText 'callbackRootLoads'); loadedValue=$callbackLoadedRaw; firstNonNullSlot=$firstNonNullSlot; firstNonNullValue=$firstNonNullValue; expectedStorageObject=$expectedStorage; expectedSentinel=(Get-MarkerField $validationText 'expectedSentinel') }
                scanContext=[ordered]@{ address=$context; threadUnderCrawl=(Get-MarkerField $validationText 'contextThread'); stackLimit=(Get-MarkerField $validationText 'contextStackLimit'); threadNumber=(Get-MarkerField $validationText 'contextThreadNumber'); threadCount=(Get-MarkerField $validationText 'contextThreadCount'); promotion=(Get-MarkerField $validationText 'contextPromotion'); concurrent=(Get-MarkerField $validationText 'contextConcurrent'); fieldReads=(Get-MarkerField $validationText 'contextFieldReads') }
                flags=[ordered]@{ expected=$expectedFlags; actual=$actualFlags; match=(Get-MarkerField $validationText 'flagsMatch') }
                semanticBoundary=[ordered]@{ firstOperation=(Get-MarkerField $validationText 'firstSemanticOperation'); candidateClassification=(Get-MarkerField $validationText 'candidateClassification'); heapMembership=(Get-MarkerField $validationText 'heapMembership'); segmentLookup=(Get-MarkerField $validationText 'segmentLookup'); objectHeaders=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); promotionStart=(Get-MarkerField $validationText 'promotionStart'); promotions=(Get-MarkerField $validationText 'promotions'); markingStart=(Get-MarkerField $validationText 'markingStart'); graphTraversal=(Get-MarkerField $validationText 'graphTraversal') }
                mutation=[ordered]@{ markWrites=(Get-MarkerField $validationText 'markWrites'); promotionWrites=(Get-MarkerField $validationText 'promotionWrites'); objectMemory=(Get-MarkerField $validationText 'objectMutation'); gcMetadata=(Get-MarkerField $validationText 'gcMetadataMutation'); segmentMetadata=(Get-MarkerField $validationText 'segmentMetadataMutation') }
                managedProofRoot=[ordered]@{ assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount'); clearCount=(Get-MarkerField $validationText 'managedClearCount'); readbackCount=(Get-MarkerField $validationText 'managedReadbackCount'); assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid'); readbackValid=(Get-MarkerField $validationText 'managedReadbackValid'); sentinelOrdinal=(Get-MarkerField $validationText 'sentinelOrdinal'); sentinelAddress=(Get-MarkerField $validationText 'sentinelAddress'); sentinelSize=(Get-MarkerField $validationText 'sentinelSize'); readbackAddress=(Get-MarkerField $validationText 'readbackAddress'); readbackExactMatch=(Get-MarkerField $validationText 'readbackExactMatch'); storageObject=(Get-MarkerField $validationText 'storageObject'); inlinedRoot=(Get-MarkerField $validationText 'inlinedRoot'); objectBefore=(Get-MarkerField $validationText 'objectBefore'); objectAfter=(Get-MarkerField $validationText 'objectAfter'); objectAtStop=(Get-MarkerField $validationText 'objectAtStop'); sentinelChecks=(Get-MarkerField $validationText 'sentinelChecks'); objectHistoryOverflow=(Get-MarkerField $validationText 'objectHistoryOverflow') }
                threadStore=[ordered]@{ managedThread=$managedThread; currentThread=$currentThread; lockOwner=$lockOwner; lockHeld=(Get-MarkerField $validationText 'lockHeld'); eeSuspended=(Get-MarkerField $validationText 'eeSuspended'); managedEntryProhibited=(Get-MarkerField $validationText 'managedEntryProhibited') }
                restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume')
            }
        } elseif ($isFirstNonNullRoot) {
            Assert-Text $validationText '\[nativeaot-gc-first-non-null-root-callback-boundary\] SAFE_STOP marker=C011EC06' "first non-null root callback-boundary safe-stop marker"
            Assert-Text $validationText 'gcScanRootsRequest=00000001 gcScanRootsEntry=00000001 foreachRequest=00000001 foreachEntry=00000001 iteratorInit=00000001' "real root dispatcher and iterator entry"
            Assert-Text $validationText 'registeredBefore=00000001 registeredAfter=0000000[01] enumerated=00000001 included=00000001 excluded=00000000' "actual registered thread enumeration and inclusion"
            Assert-Text $validationText 'listIntegrityFailures=00000000 duplicates=00000000 registryMutationBefore=00000000 registryMutationAfter=00000000' "thread-list integrity and closed-world mutation state"
            Assert-Text $validationText 'providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=0000000[12]' "runtime-selected thread-static provider"
            Assert-Text $validationText 'managedAssignmentCount=00000001 managedClearCount=00000000 managedReadbackCount=00000001 managedAssignmentValid=00000001 managedReadbackValid=00000001' "managed thread-static assignment and readback"
            Assert-Text $validationText 'threadStaticInitialization=0000000[23] sentinelOrdinal=00000000 sentinelAddress=[0-9A-F]{16} sentinelSize=0000000000001000 readbackAddress=[0-9A-F]{16} readbackExactMatch=00000001' "thread-static initialization and selected sentinel"
            Assert-Text $validationText 'candidateVisited=0000000[1-8] nullCandidates=0000000[01] nonNullCandidates=00000001' "bounded candidate sequence and first non-null value"
            Assert-Text $validationText 'firstNonNullSlot=[0-9A-F]{16} firstNonNullValue=[0-9A-F]{16} firstNonNullKnownAddressMatch=[0-9A-F]{8} expectedSentinelAddress=[0-9A-F]{16} expectedStorageObjectAddress=[0-9A-F]{16}' "first non-null candidate raw value"
            Assert-Text $validationText 'loadRequests=0000000[1-8] loadEntries=0000000[1-8] machineWordLoads=0000000[1-8] duplicateLoads=00000000 loadFaults=00000000' "exactly-once bounded candidate loads"
            Assert-Text $validationText 'rootFlags=00000000 rootKind=00000001 callbacks=00000000 promotions=00000000 marking=00000000' "root metadata before callback"
            Assert-Text $validationText 'candidateDereferences=00000000 heapMembershipTests=00000000 objectHeaders=00000000 methodTables=00000000 rootFlagApplications=00000000' "no semantic candidate processing"
            Assert-Text $validationText 'objectMutation=00000000 restartRequests=00000000 restartEntries=00000000 managedResume=00000000' "no mutation, restart, or managed resume"
            Assert-Text $validationText 'objectBeforeLoad=00000025 objectAfterLoad=00000025 objectAtStop=00000025 sentinelChecks=00000094 objectHistoryOverflow=00000000' "object validation around non-null load"
            Assert-Text $validationText 'runtimeThreadStaticStorageAllocations=00000001 runtimeThreadStaticStoragePublications=00000001 runtimeThreadStaticStorageObject=[0-9A-F]{16} runtimeThreadStaticInlinedRoot=[0-9A-F]{16} totalAllocationRequestsObserved=[0-9A-F]{8}' "real thread-static storage allocation and publication"
            Assert-Text $validationText 'condemnedGeneration=[0-9A-F]{8} maxGeneration=[0-9A-F]{8} scanContextPromotion=[0-9A-F]{8} scanContextConcurrent=[0-9A-F]{8} scanContextIdentity=[0-9A-F]{16}' "GC callback scan-context metadata"
            Assert-Text $validationText 'candidateMatchesProofRoot=0000000[01] candidateMatchesStorageObject=0000000[01]' "known-address candidate classification"
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
                allocations=(Get-MarkerField $validationText 'objectAtStop'); userAllocations=(Get-MarkerField $validationText 'userAllocations'); userAllocationRequests=(Get-MarkerField $validationText 'userAllocationRequests'); userFast=(Get-MarkerField $validationText 'userFast'); userRare=(Get-MarkerField $validationText 'userRare'); userRefills=(Get-MarkerField $validationText 'userRefills'); userSameSegmentCommits=(Get-MarkerField $validationText 'userSameSegmentCommits'); userSegmentTransitions=(Get-MarkerField $validationText 'userSegmentTransitions'); collectionAllocationOrdinal=(Get-MarkerField $validationText 'collectionAllocationOrdinal'); runtimeThreadStaticStorageAllocations=(Get-MarkerField $validationText 'runtimeThreadStaticStorageAllocations'); runtimeThreadStaticStoragePublications=(Get-MarkerField $validationText 'runtimeThreadStaticStoragePublications'); runtimeThreadStaticStorageObject=(Get-MarkerField $validationText 'runtimeThreadStaticStorageObject'); runtimeThreadStaticInlinedRoot=(Get-MarkerField $validationText 'runtimeThreadStaticInlinedRoot'); totalAllocationRequestsObserved=(Get-MarkerField $validationText 'totalAllocationRequestsObserved'); gcScanRootsRequests=(Get-MarkerField $validationText 'gcScanRootsRequest'); gcScanRootsEntries=(Get-MarkerField $validationText 'gcScanRootsEntry'); foreachThreadEntries=(Get-MarkerField $validationText 'foreachEntry'); iteratorInitializations=(Get-MarkerField $validationText 'iteratorInit'); registeredThreads=(Get-MarkerField $validationText 'registeredBefore'); registeredAfterThreads=(Get-MarkerField $validationText 'registeredAfter'); enumeratedThreads=(Get-MarkerField $validationText 'enumerated'); includedThreads=(Get-MarkerField $validationText 'included'); excludedThreads=(Get-MarkerField $validationText 'excluded'); registryMutationBefore=(Get-MarkerField $validationText 'registryMutationBefore'); registryMutationAfter=(Get-MarkerField $validationText 'registryMutationAfter'); listIntegrityFailures=(Get-MarkerField $validationText 'listIntegrityFailures'); listDuplicates=(Get-MarkerField $validationText 'duplicates')
                threadRecord=[ordered]@{ ordinal=1; nativeThread=$enumeratedIdentity; nativeThreadId=(Get-MarkerField $validationText 'nativeId'); current=$currentIdentity; initiator=$initiatorIdentity; lockOwner=$lockOwnerIdentity; lifecycle=(Get-MarkerField $validationText 'lifecycle'); stateFlags=(Get-MarkerField $validationText 'stateFlags'); cooperative=(Get-MarkerField $validationText 'cooperative'); preemptive=(Get-MarkerField $validationText 'preemptive') }
                managedProofRoot=[ordered]@{ assignmentCount=(Get-MarkerField $validationText 'managedAssignmentCount'); clearCount=(Get-MarkerField $validationText 'managedClearCount'); readbackCount=(Get-MarkerField $validationText 'managedReadbackCount'); assignmentValid=(Get-MarkerField $validationText 'managedAssignmentValid'); readbackValid=(Get-MarkerField $validationText 'managedReadbackValid'); initialization=(Get-MarkerField $validationText 'threadStaticInitialization'); sentinelOrdinal=(Get-MarkerField $validationText 'sentinelOrdinal'); sentinelAddress=$sentinelIdentity; sentinelSize=(Get-MarkerField $validationText 'sentinelSize'); readbackAddress=$readbackIdentity; readbackExactMatch=(Get-MarkerField $validationText 'readbackExactMatch'); managedThread=(Get-MarkerField $validationText 'managedThread') }
                provider=[ordered]@{ source="thread-static-provider"; runtime="thread-static-provider"; function=(Get-MarkerField $validationText 'providerFunction'); requests=(Get-MarkerField $validationText 'providerRequests'); entries=(Get-MarkerField $validationText 'providerEntries'); skips=(Get-MarkerField $validationText 'providerSkips'); metadataContainer=(Get-MarkerField $validationText 'metadataContainer'); storageAddress=(Get-MarkerField $validationText 'storageAddress') }
                candidateSlots=@([ordered]@{ ordinal=("0x"+$candidateLineMatch.Groups['ordinal'].Value); slot=("0x"+$candidateLineMatch.Groups['slot'].Value); rawValue=("0x"+$candidateLineMatch.Groups['raw'].Value); loadCount=("0x"+$candidateLineMatch.Groups['loads'].Value); duplicateLoads=("0x"+$candidateLineMatch.Groups['duplicates'].Value); null=("0x"+$candidateLineMatch.Groups['null'].Value); knownAddressMatch=("0x"+$candidateLineMatch.Groups['known'].Value); exactSelectedSentinelMatch=("0x"+$candidateLineMatch.Groups['exact'].Value); callback=("0x"+$candidateLineMatch.Groups['callback'].Value); scanContext=("0x"+$candidateLineMatch.Groups['scanContext'].Value) })
                candidateVisited=(Get-MarkerField $validationText 'candidateVisited'); nullCandidates=(Get-MarkerField $validationText 'nullCandidates'); nonNullCandidates=(Get-MarkerField $validationText 'nonNullCandidates'); firstNonNullSlot=$candidateSlotIdentity; firstNonNullValue=$candidateValueIdentity; firstNonNullKnownAddressMatch=(Get-MarkerField $validationText 'firstNonNullKnownAddressMatch'); candidateMatchesProofRoot=(Get-MarkerField $validationText 'candidateMatchesProofRoot'); candidateMatchesStorageObject=(Get-MarkerField $validationText 'candidateMatchesStorageObject'); expectedStorageObjectAddress=(Get-MarkerField $validationText 'expectedStorageObjectAddress'); candidateProofRootObserved=(Get-MarkerField $validationText 'proofRootObserved'); loadRequests=(Get-MarkerField $validationText 'loadRequests'); loadEntries=(Get-MarkerField $validationText 'loadEntries'); machineWordLoads=(Get-MarkerField $validationText 'machineWordLoads'); duplicateLoads=(Get-MarkerField $validationText 'duplicateLoads'); loadFaults=(Get-MarkerField $validationText 'loadFaults'); callback=(Get-MarkerField $validationText 'callback'); scanContext=(Get-MarkerField $validationText 'scanContext'); rootFlags=(Get-MarkerField $validationText 'rootFlags'); rootKind=(Get-MarkerField $validationText 'rootKind'); condemnedGeneration=(Get-MarkerField $validationText 'condemnedGeneration'); maxGeneration=(Get-MarkerField $validationText 'maxGeneration'); scanContextPromotion=(Get-MarkerField $validationText 'scanContextPromotion'); scanContextConcurrent=(Get-MarkerField $validationText 'scanContextConcurrent'); scanContextIdentity=(Get-MarkerField $validationText 'scanContextIdentity')
                candidateDereferences=(Get-MarkerField $validationText 'candidateDereferences'); heapMembershipTests=(Get-MarkerField $validationText 'heapMembershipTests'); objectHeaders=(Get-MarkerField $validationText 'objectHeaders'); methodTables=(Get-MarkerField $validationText 'methodTables'); rootFlagApplications=(Get-MarkerField $validationText 'rootFlagApplications'); callbacks=(Get-MarkerField $validationText 'callbacks'); promotions=(Get-MarkerField $validationText 'promotions'); marking=(Get-MarkerField $validationText 'marking'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartRequests=(Get-MarkerField $validationText 'restartRequests'); restartEntries=(Get-MarkerField $validationText 'restartEntries'); managedResume=(Get-MarkerField $validationText 'managedResume'); objectBeforeLoad=(Get-MarkerField $validationText 'objectBeforeLoad'); objectAfterLoad=(Get-MarkerField $validationText 'objectAfterLoad'); objectAtStop=(Get-MarkerField $validationText 'objectAtStop')
            }
        } elseif ($isFirstRootCandidateLoad) {
            # The shared NativeAOT module-initialization contract adds three
            # legitimate startup allocations and the general safe-stop marker
            # is emitted before the final frontier marker. Match the final
            # C011EC05 marker without weakening the candidate-load invariants.
            Assert-Text $validationText '\[nativeaot-gc-first-root-candidate-load\].*marker=C011EC05' "first root candidate-load safe-stop marker"
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
            Assert-Text $validationText 'objectBeforeLoad=0000002[58] objectAfterLoad=0000002[58] objectAtStop=0000002[58] sentinelChecks=000000(?:94|A0)' "object and sentinel validation before and after load"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=0000000[01] eeSuspended=00000001 lockDepth=00000001' "fixup, suspension, and lock invariants"
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
            $fixupObjectBefore = Get-MarkerField $validationText 'objectBefore'
            $fixupObjectAfter = Get-MarkerField $validationText 'objectAfter'
            if ([string]::IsNullOrWhiteSpace($fixupObjectBefore) -or $fixupObjectBefore -ne $fixupObjectAfter) { throw "Allocation-context fixup changed the bounded object history in $name." }
            Assert-Text $validationText 'rootDispatcher=00000001 rootProviders=00000000 rootCandidates=00000000 callbacks=00000000 marking=00000000' "root boundary before providers and marking"
            Assert-Text $validationText 'metadataMutation=00000001 objectMutation=00000000 restartResume=00000000' "metadata-only fixup mutation and no restart"
            Assert-Text $validationText 'fixupFailures=00000000 rootFailures=00000000 objectFailuresBefore=00000000 objectFailuresAfter=00000000' "zero fixup, root, and object-validation failures"
            Assert-Text $validationText 'boundaryFailures=00000000 patternFailures=00000000 addressChanges=00000000' "zero object boundary, pattern, and address-change failures"
            $fixupSentinelChecks = [Convert]::ToUInt32((Get-MarkerField $validationText 'sentinelChecks').Substring(2), 16)
            if ($fixupSentinelChecks -lt 4) { throw "Fewer than four live sentinels were checked before root dispatch in $name." }
            if ($validationText -match 'ALL_PASS|ALL_FAIL|GC\.Collect|RhShutdown|GC_Shutdown') { throw "Unexpected completion or shutdown marker appeared in $name." }
            $runResults += [ordered]@{
                name=$name; serial=$serialPath; serialSha256=(Hash-File $serialPath); safeStopMarker="C011EC03"; harnessTerminated=$true
                 allocations=$fixupObjectAfter; contextFixupRequests=(Get-MarkerField $validationText 'fixupRequest'); contextFixupEntries=(Get-MarkerField $validationText 'fixupEntry'); contextFixupCompletions=(Get-MarkerField $validationText 'fixupComplete'); contextsVisited=(Get-MarkerField $validationText 'contextsVisited'); contextsChanged=(Get-MarkerField $validationText 'contextsChanged'); contextsCleared=(Get-MarkerField $validationText 'contextsCleared')
                 objectValidationBefore=$fixupObjectBefore; objectValidationAfter=$fixupObjectAfter; rootDispatcherEntries=(Get-MarkerField $validationText 'rootDispatcher'); rootProviderEntries=(Get-MarkerField $validationText 'rootProviders'); rootCandidates=(Get-MarkerField $validationText 'rootCandidates'); rootCallbacks=(Get-MarkerField $validationText 'callbacks'); markingEntries=(Get-MarkerField $validationText 'marking'); metadataMutation=(Get-MarkerField $validationText 'metadataMutation'); objectMutation=(Get-MarkerField $validationText 'objectMutation'); restartResume=(Get-MarkerField $validationText 'restartResume')
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
        $suspendAllocations = [Convert]::ToUInt32((Get-MarkerField $validationText 'allocations').Substring(2), 16)
        $suspendFast = [Convert]::ToUInt32((Get-MarkerField $validationText 'fast').Substring(2), 16)
        $suspendRare = [Convert]::ToUInt32((Get-MarkerField $validationText 'rare').Substring(2), 16)
        $suspendRefills = [Convert]::ToUInt32((Get-MarkerField $validationText 'refills').Substring(2), 16)
        $suspendSameSegmentCommits = [Convert]::ToUInt32((Get-MarkerField $validationText 'sameSegmentCommits').Substring(2), 16)
        if ($suspendAllocations -eq 0 -or $suspendFast -eq 0 -or $suspendRare -eq 0 -or $suspendRefills -eq 0 -or $suspendSameSegmentCommits -eq 0 -or (Get-MarkerField $validationText 'segmentTransitions') -ne '0x00000000') { throw "Allocation/refill invariants were not preserved in $name." }
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
    if ($isFirstRootCondemnedGenerationDecision) {
        $nonC011EC10Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC10" })
        if (@($runResults).Count -ne 3 -or $nonC011EC10Runs.Count -ne 0) { throw "The first real root condemned-generation experiment did not produce three C011EC10 runs." }
        $firstCondemnedRun = $runResults[0]
        $outcome = $firstCondemnedRun.outcome
        if (@($runResults | Where-Object { $_.outcome -ne $outcome }).Count -ne 0) { throw "Condemned-generation outcome was not deterministic across the three QEMU runs." }
        $condemnedSerial = Get-Content -LiteralPath $firstCondemnedRun.serial -Raw
        $manifest = [ordered]@{
            outcome="$outcome / first genuine NativeAOT root completed one real Workstation gc_heap::is_in_condemned_gc(o) decision; stopped before the selected true/false GCHeap::Promote continuation"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; threadStatic="real NativeAOT [ThreadStatic] provider"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            prerequisite=[ordered]@{ marker="C011EC09"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md"; historicalOutcome="B"; correctedInterpretation="heap_of(o)==0 is the valid Workstation single-heap sentinel when MULTIPLE_HEAPS is disabled" }
            managedProofRoot=[ordered]@{ field="[ThreadStatic] byte[]? s_gcProofThreadRoot"; sentinel=$firstCondemnedRun.root.sentinel; storageObject=$firstCondemnedRun.root.storageObject; inlinedRoot=(Get-MarkerField $condemnedSerial 'inlinedRoot'); assignmentCount=(Get-MarkerField $condemnedSerial 'managedAssignmentCount'); clearCount=(Get-MarkerField $condemnedSerial 'managedClearCount'); readbackCount=(Get-MarkerField $condemnedSerial 'managedReadbackCount'); assignmentValid=(Get-MarkerField $condemnedSerial 'managedAssignmentValid'); readbackValid=(Get-MarkerField $condemnedSerial 'managedReadbackValid'); objectBefore=(Get-MarkerField $condemnedSerial 'objectBefore'); objectAfter=(Get-MarkerField $condemnedSerial 'objectAfter'); objectAtStop=(Get-MarkerField $condemnedSerial 'objectAtStop'); duplicateObjectAddresses=(Get-MarkerField $condemnedSerial 'duplicateObjectAddresses'); objectHistoryOverflow=(Get-MarkerField $condemnedSerial 'objectHistoryOverflow') }
            rootArgument=[ordered]@{ slot=$firstCondemnedRun.root.slot; callbackRoot=$firstCondemnedRun.root.rawValue; membershipObject=$firstCondemnedRun.membership.object; heapResolutionInput=$firstCondemnedRun.heapResolution.input; condemnedCheckInput=$firstCondemnedRun.condemnedCheck.object; storageObject=$firstCondemnedRun.root.storageObject; identitiesEqual=($firstCondemnedRun.root.rawValue -eq $firstCondemnedRun.membership.object -and $firstCondemnedRun.membership.object -eq $firstCondemnedRun.heapResolution.input -and $firstCondemnedRun.heapResolution.input -eq $firstCondemnedRun.condemnedCheck.object -and $firstCondemnedRun.condemnedCheck.object -eq $firstCondemnedRun.root.storageObject) }
            membership=$firstCondemnedRun.membership
            wksHeap=[ordered]@{ multipleHeaps=$firstCondemnedRun.heapResolution.multipleHeaps; hpt=$firstCondemnedRun.heapResolution.hpt; heapOf=$firstCondemnedRun.heapResolution.heap; heapNumber=$firstCondemnedRun.heapResolution.heapNumber; heapCount=$firstCondemnedRun.heapResolution.heapCount; workstationSingleHeapSentinelValid=$firstCondemnedRun.heapResolution.singleHeapSentinelValid; heapResolutionFailures=$firstCondemnedRun.heapResolution.failures }
            sourceTrace=[ordered]@{ callbackFunction="WKS::GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544"; helperDeclaration="locked src/coreclr/gc/gcpriv.h:3054"; helperDefinition="locked src/coreclr/gc/gc.cpp:8389-8407"; helperContract="USE_REGIONS; object asserted in [g_gc_lowest_address,g_gc_highest_address); settings.condemned_generation compared with max_generation; get_region_gen_num(o) only when condemned_gen < max_generation"; helperFields=@("g_gc_lowest_address","g_gc_highest_address","settings.condemned_generation","max_generation","gc_heap::min_segment_size_shr","gc_heap::map_region_to_generation_skewed"); calledHelpers=@("get_region_gen_num(o)","get_skewed_basic_region_index_for_address(o)","region_of(o) in locked debug assertion"); generationDefinition="locked src/coreclr/gc/gc.cpp:12038-12045"; generationTableDefinition="locked src/coreclr/gc/gcpriv.h:1551-1576,4322-4326"; generationOf="not called"; findSegment="not called"; objectHeader="not read"; methodTable="not read"; objectDereference="none"; segmentLookup="one source-required region_of(o) debug assertion lookup in the proof build"; trueMeaning="region generation <= condemned generation, or full collection condemned_generation == max_generation"; falseMeaning="region generation > condemned generation"; trueNextSourceOperation="locked gc.cpp:49507 dprintf after the true branch"; falseNextSourceOperation="locked gc.cpp:49504 return"; firstPromotionMutation="locked gc.cpp:49541 hpt->mark_object_simple; not executed" }
            sourceInputs=$firstCondemnedRun.condemnedCheck
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64 (RCX/RDX/R8; callback return boundary)"; callbackAddress="0x$callbackAddressText"; helper="inline USE_REGIONS sequence in GCHeap::Promote; no independent helper symbol expected"; disassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt"); helperInputRegister="RCX/object is live through the inlined helper sequence"; comparison="lower/upper global-range assert, condemned_generation/max_generation compare, and generation <= condemned_generation compare"; selectedBranch=$firstCondemnedRun.condemnedCheck.branch; completionReturnAddress=(Get-MarkerField $condemnedSerial 'condemnedCompletionReturnAddress'); safeStopReturnAddress=(Get-MarkerField $condemnedSerial 'condemnedSafeStopReturnAddress') }
            generationState=[ordered]@{ condemnedGeneration=$firstCondemnedRun.condemnedCheck.condemnedGeneration; maximumGeneration=$firstCondemnedRun.condemnedCheck.maximumGeneration; generationFromRegion=$firstCondemnedRun.condemnedCheck.generation; generationTable=$firstCondemnedRun.condemnedCheck.generationTable; generationTableIndex=$firstCondemnedRun.condemnedCheck.generationTableIndex; segment=$firstCondemnedRun.condemnedCheck.segment; lowerBound=$firstCondemnedRun.condemnedCheck.lowerBound; upperBound=$firstCondemnedRun.condemnedCheck.upperBound; result=$firstCondemnedRun.condemnedCheck.result }
            counters=$firstCondemnedRun.counters
            mutation=$firstCondemnedRun.mutation
            execution=[ordered]@{ callbackReturns=(Get-MarkerField $condemnedSerial 'callbackReturns'); secondCallbacks=(Get-MarkerField $condemnedSerial 'secondCallbacks'); restartRequests=(Get-MarkerField $condemnedSerial 'restartRequests'); restartEntries=(Get-MarkerField $condemnedSerial 'restartEntries'); managedResume=(Get-MarkerField $condemnedSerial 'managedResume'); nextSourceOperation=$firstCondemnedRun.condemnedCheck.nextSourceOperation }
            threadStore=$firstCondemnedRun.threadStore
            scanContext=[ordered]@{ address=(Get-MarkerField $condemnedSerial 'callbackContext'); thread=(Get-MarkerField $condemnedSerial 'contextThread'); stackLimit=(Get-MarkerField $condemnedSerial 'contextStackLimit'); threadNumber=(Get-MarkerField $condemnedSerial 'contextThreadNumber'); threadCount=(Get-MarkerField $condemnedSerial 'contextThreadCount'); promotion=(Get-MarkerField $condemnedSerial 'contextPromotion'); concurrent=(Get-MarkerField $condemnedSerial 'contextConcurrent') }
            safeStop=[ordered]@{ marker="C011EC10"; reason="exactly one real condemned-generation decision for the genuine first root; stopped before promotion, marking, graph traversal, or mutation"; result=$firstCondemnedRun.condemnedCheck.result; nextSourceOperation=$firstCondemnedRun.condemnedCheck.nextSourceOperation; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; mutationAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec10="PASS 3/3 fresh QEMU runs"; c011ec09="historical Outcome B retained; updated sentinel interpretation cross-referenced"; c011ec08="not rerun in this focused pass; historical PASS retained"; c011ec07="not rerun in this focused pass; historical PASS retained"; c011ec06="historical validator-clean result retained"; c011ec05="historical PASS retained"; primitiveThreadStatic="historical PASS retained"; referenceThreadStatic="historical PASS retained"; combinedThreadStatic="historical PASS retained"; c011ec03="historical validator-usable result retained"; c011ec02="historical validator-usable result retained"; c011ec01="historical validator-usable result retained"; staticChecks="PASS script parsing, manifest parsing, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1; not relabeled PASS" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun in this focused pass"; nativeStackWrapper="historically non-clean"; postCondemnedPromotion="intentionally blocked by the C011EC10 safe stop" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md"; manifestPath=$manifestPath
            ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 28 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-condemned-generation-decision experiment: Outcome $outcome" -ForegroundColor Green
    } elseif ($isFirstRootHeapResolution) {
        $nonC011EC09Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC09" })
        if (@($runResults).Count -ne 3 -or $nonC011EC09Runs.Count -ne 0) { throw "The first real root heap-resolution experiment did not produce three C011EC09 runs." }
        $firstHeapRun = $runResults[0]
        $outcome = if ($firstHeapRun.outcome -eq "A") { "A" } else { "B" }
        if (@($runResults | Where-Object { $_.outcome -ne $outcome }).Count -ne 0) { throw "Heap-resolution outcome was not deterministic across the three QEMU runs." }
        $heapSerial = Get-Content -LiteralPath $firstHeapRun.serial -Raw
        $manifest = [ordered]@{
            outcome="$outcome / first genuine NativeAOT root passed managed-range membership and completed exactly one real HEAP_FROM_THREAD/heap_of(o) resolution; stopped before generation logic"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; threadStatic="real NativeAOT [ThreadStatic] provider"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText" }
            prerequisite=[ordered]@{ marker="C011EC08"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md"; result="membership true for the genuine storage-object root; stopped before HEAP_FROM_THREAD" }
            managedProofRoot=[ordered]@{ field="[ThreadStatic] byte[]? s_gcProofThreadRoot"; sentinel=$firstHeapRun.root.sentinel; storageObject=$firstHeapRun.root.storage; inlinedRoot=(Get-MarkerField $heapSerial 'inlinedRoot'); assignmentCount=(Get-MarkerField $heapSerial 'managedAssignmentCount'); clearCount=(Get-MarkerField $heapSerial 'managedClearCount'); readbackCount=(Get-MarkerField $heapSerial 'managedReadbackCount'); assignmentValid=(Get-MarkerField $heapSerial 'managedAssignmentValid'); readbackValid=(Get-MarkerField $heapSerial 'managedReadbackValid'); objectBefore=(Get-MarkerField $heapSerial 'objectBefore'); objectAfter=(Get-MarkerField $heapSerial 'objectAfter'); objectAtStop=(Get-MarkerField $heapSerial 'objectAtStop'); duplicateObjectAddresses=(Get-MarkerField $heapSerial 'duplicateObjectAddresses'); objectHistoryOverflow=(Get-MarkerField $heapSerial 'objectHistoryOverflow') }
            rootArgument=[ordered]@{ slot=(Get-MarkerField $heapSerial 'rootSlot'); rawRoot=$firstHeapRun.root.loaded; callbackLoadedRoot=$firstHeapRun.root.loaded; membershipObject=$firstHeapRun.membership.object; heapResolutionInput=$firstHeapRun.heapResolution.input; expectedStorageObject=$firstHeapRun.root.storage; identitiesEqual=($firstHeapRun.root.loaded -eq $firstHeapRun.membership.object -and $firstHeapRun.membership.object -eq $firstHeapRun.heapResolution.input -and $firstHeapRun.heapResolution.input -eq $firstHeapRun.root.storage) }
            membership=[ordered]@{ requests=$firstHeapRun.membership.requests; entries=$firstHeapRun.membership.entries; completions=$firstHeapRun.membership.completions; object=$firstHeapRun.membership.object; lowerBound=$firstHeapRun.membership.lowerBound; upperBound=$firstHeapRun.membership.upperBound; lowerResult=(Get-MarkerField $heapSerial 'lowerResult'); upperResult=(Get-MarkerField $heapSerial 'upperResult'); result=$firstHeapRun.membership.result; objectDereferences=(Get-MarkerField $heapSerial 'membershipObjectDereferences'); segmentLookup="not part of membership helper" }
            sourceTrace=[ordered]@{ callbackFunction="WKS::GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544"; heapFromThread="locked src/coreclr/gc/gcpriv.h:315-326; Workstation branch at :326: gc_heap* hpt = 0"; heapOfDeclaration="locked src/coreclr/gc/gcpriv.h:2620"; heapOfDefinition="locked src/coreclr/gc/gc.cpp:26693-26707"; heapOfWorkstation="UNREFERENCED_PARAMETER(o); return __this;"; thisMacro="locked src/coreclr/gc/gcpriv.h:1540-1548; Workstation __this is (gc_heap*)0"; multipleHeaps="absent in Workstation; Server branch uses g_heaps[thread] and seg_mapping_table_heap_of(o)"; sourceFieldsConsulted=@("no object-address lookup", "no thread-state lookup", "no g_heaps table", "no segment map", "no brick/card bookkeeping", "no range fields"); sourceRequiredSegmentLookup=0; sourceRequiredObjectDereference=0; nextSemanticOperation="locked gc.cpp:49498-49505; active USE_REGIONS operation gc_heap::is_in_condemned_gc(o) at :49499"; laterNonRegionsOperation="locked gc.cpp:49501 object-range comparison hp->gc_low/gc_high"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            heapResolution=$firstHeapRun.heapResolution
            heapRelationship=[ordered]@{ workstationLogicalHeapCount=$firstHeapRun.heapResolution.totalHeapCount; returnedHeap=$firstHeapRun.heapResolution.pointer; returnedHeapNumber=$firstHeapRun.heapResolution.heapNumber; threadHeap=$firstHeapRun.heapResolution.threadHeap; heapTable="not applicable in Workstation"; segment="not consulted; segment identification is out of scope"; matchesSohOwner="not evaluated because source heap_of has no segment lookup and returned null" }
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64; RCX/RDX/R8"; callbackAddress="0x$callbackAddressText"; disassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt"); membershipBranch="recorded in artifact disassembly around GCHeap::Promote"; heapFromThread="first proof hook immediately after source HEAP_FROM_THREAD; hpt input is source-produced zero"; heapOf="inline sequence in GCHeap::Promote; source result passed to completion observer"; nextGenerationInstruction="not executed; source next is gc_heap::is_in_condemned_gc(o) at locked gc.cpp:49499" }
            prohibitedOperations=[ordered]@{ generationClassificationStart=(Get-MarkerField $heapSerial 'generationClassificationStart'); generationQueryStart=(Get-MarkerField $heapSerial 'generationQueryStart'); condemnedGenerationComparisons=(Get-MarkerField $heapSerial 'condemnedGenerationComparisons'); ephemeralGenerationComparisons=(Get-MarkerField $heapSerial 'ephemeralGenerationComparisons'); segmentLookup=(Get-MarkerField $heapSerial 'postResolutionSegmentLookup'); objectHeaders=(Get-MarkerField $heapSerial 'objectHeaders'); methodTables=(Get-MarkerField $heapSerial 'methodTables'); childReferences=(Get-MarkerField $heapSerial 'childReferenceReads'); promotionStart=(Get-MarkerField $heapSerial 'promotionStart'); promotion=(Get-MarkerField $heapSerial 'promotions'); markingStart=(Get-MarkerField $heapSerial 'markingStart'); graphTraversal=(Get-MarkerField $heapSerial 'graphTraversal') }
            mutation=[ordered]@{ mark=(Get-MarkerField $heapSerial 'markWrites'); promotion=(Get-MarkerField $heapSerial 'promotionWrites'); object=(Get-MarkerField $heapSerial 'objectMutation'); gcMetadata=(Get-MarkerField $heapSerial 'gcMetadataMutation'); segment=(Get-MarkerField $heapSerial 'segmentMetadataMutation') }
            scanContext=$firstHeapRun.scanContext
            threadStore=$firstHeapRun.threadStore
            execution=[ordered]@{ callbackReturns=(Get-MarkerField $heapSerial 'callbackReturns'); secondCallbacks=(Get-MarkerField $heapSerial 'secondCallbacks'); restartRequests=(Get-MarkerField $heapSerial 'restartRequests'); restartEntries=(Get-MarkerField $heapSerial 'restartEntries'); managedResume=(Get-MarkerField $heapSerial 'managedResume') }
            safeStop=[ordered]@{ marker="C011EC09"; reason="the genuine first root passed true managed-range membership and exactly one real HEAP_FROM_THREAD/heap_of(o) transition; stopped before gc_heap::is_in_condemned_gc(o)"; nextSourceOperation="gc.cpp:49499"; outcome=$outcome }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec08="historical PASS 3/3 retained"; c011ec07="historical PASS 3/3 retained"; c011ec06="historical validator-clean result retained"; c011ec05="historical PASS retained"; primitiveThreadStatic="historical PASS retained"; referenceThreadStatic="historical PASS retained"; combinedThreadStatic="historical PASS retained"; c011ec03="historical validator-usable result retained"; c011ec02="historical validator-usable result retained"; c011ec01="historical validator-usable result retained"; broadRegressionSuite="not rerun in this focused pass"; staticChecks="script parsing, manifest parsing, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1; not relabeled PASS" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="blocked/not rerun in focused pass"; nativeStackWrapper="historically non-clean"; heapOwnerSegmentEquality="not evaluated because heap_of returned Workstation null sentinel and segment lookup is out of scope" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md"; manifestPath=$manifestPath
        }
        $manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-heap-resolution experiment: Outcome $outcome" -ForegroundColor Green
    } elseif ($isFirstRootMembershipClassification) {
        $nonC011EC08Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC08" })
        if (@($runResults).Count -ne 3 -or $nonC011EC08Runs.Count -ne 0) { throw "The first real root membership experiment did not produce three C011EC08 runs." }
        $firstMembershipRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / the genuine NativeAOT thread-static storage root completed exactly one Workstation GC managed-range membership check with a true result and stopped before generation classification"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText"; membershipHelper="gc_heap::is_in_find_object_range"; membershipHelperAddress="inline; no out-of-line helper symbol in the final map" }
            priorCheckpoint=[ordered]@{ marker="C011EC07"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md"; stop="real GCHeap::Promote entered, loaded the genuine storage-object root, and stopped before is_in_find_object_range" }
            managedProofRoot=$firstMembershipRun.managedProofRoot
            threadStaticRuntime=[ordered]@{ storageObjectAddress=$firstMembershipRun.managedProofRoot.storageObject; inlinedRootAddress=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'inlinedRoot'); normalManagedSemantics=$true; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ callbackTypedef="promote_func(Object**, ScanContext*, uint32_t)"; callbackFunction="WKS::GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544, included by src/coreclr/gc/gcwks.cpp"; membershipDeclaration="locked src/coreclr/gc/gcpriv.h:3049"; membershipDefinition="locked src/coreclr/gc/gc.cpp:8363-8387"; activeConfiguration="USE_REGIONS from gcpriv.h:147-149 and FEATURE_CONSERVATIVE_GC"; activeExpression="gc.cpp:8373: (o >= g_gc_lowest_address) && (o < bookkeeping_covered_committed), after gc.cpp:8368-8371 null guard"; rangeFields=@("g_gc_lowest_address", "gc_heap::bookkeeping_covered_committed"); heapIdentity="none; helper reads no heap identity"; segmentLookup="none"; objectDereference="none"; callbackGeneration="locked gc.cpp:29899-29901: GCScan::GcScanRoots(GCHeap::Promote, ...)"; rootProvider="locked src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:114-115"; enumGcRef="locked src/coreclr/nativeaot/Runtime/GcEnum.cpp:68-96"; callbackCallSite="locked GcEnum.cpp:81: fnGcEnumRef(ppObj, pSc, flags)"; firstCandidateLoad="locked gc.cpp:49481: uint8_t* o = (uint8_t*)*ppObject"; trueNextSourceOperation="locked gc.cpp:49494: HEAP_FROM_THREAD, after the DEBUG_DestroyedHandleValue block is compiled out"; falseNextSourceOperation="locked gc.cpp:49485: return; (the proof boundary executes before that return and does not skip the root)"; postMembershipGenerationStart="locked gc.cpp:49496 heap_of(o) and gc.cpp:49499-49505 condemned-generation/range logic"; firstMetadataAccess="locked gc.cpp:49521 CObjectHeader::IsFree in conservative branch or gc.cpp:49528 Validate in debug"; firstPromotionMutation="locked gc.cpp:49533 pin_object / gc.cpp:49541 mark_object_simple"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64"; callbackAddress="0x$callbackAddressText"; callbackEntry="map symbol ?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z"; membershipHelper="inline; no out-of-line address"; membershipHelperSource="gc.cpp:8363-8387"; membershipSequence="final artifact disassembly around GCHeap::Promote shows the two range loads/comparisons and the completion observer call"; membershipLowerComparison="0x1002461D cmp rbx,rdx after 0x10024613 load of g_gc_lowest_address; 0x10024631 setae r10b"; membershipUpperComparison="0x1002462D load of bookkeeping_covered_committed; 0x10024635 setb al"; membershipResultBranch="0x1002467C test dil; 0x10024684 je false boundary; 0x1002468B true boundary call"; completionObserverReturnAddress=$firstMembershipRun.membership.completionReturnAddress; postMembershipBoundaryReturnAddress=$firstMembershipRun.membership.postCheckReturnAddress; disassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt") }
            callbackCallSite=$firstMembershipRun.callbackSite
            callbackEntry=$firstMembershipRun.callbackEntry
            rootArgument=$firstMembershipRun.root
            scanContext=$firstMembershipRun.scanContext
            membershipCheck=$firstMembershipRun.membership
            heapRange=[ordered]@{ heapIdentity=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'heapIdentity'); heapFieldReads=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'heapFieldReads'); lowerBound=$firstMembershipRun.membership.lowerBound; upperBound=$firstMembershipRun.membership.upperBound; lowerComparison=$firstMembershipRun.membership.lowerResult; upperComparison=$firstMembershipRun.membership.upperResult; finalResult=$firstMembershipRun.membership.result; sourceBranch=$firstMembershipRun.membership.sourceBranch; sourceRequiredSegmentLookupCount=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'segmentLookupCount'); sourceRequiredSegmentLookupSucceeded=(Get-MarkerField (Get-Content -LiteralPath $firstMembershipRun.serial -Raw) 'segmentLookupSucceeded') }
            prohibitedOperations=$firstMembershipRun.prohibited
            mutation=$firstMembershipRun.mutation
            objectAndSentinelValidation=$firstMembershipRun.managedProofRoot
            threadStore=$firstMembershipRun.threadStore
            safeStop=[ordered]@{ marker="C011EC08"; reason="the real first root completed exactly one is_in_find_object_range check with true result; stopped before HEAP_FROM_THREAD/generation classification"; firstPostMembershipOperation="gc.cpp:49494 HEAP_FROM_THREAD"; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; callbackReturnAllowed=$false; managedResumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ firstRootMembershipClassification="PASS 3/3 fresh QEMU runs"; firstRootCallbackEntry="C011EC07 regression retained; new run also entered the same callback"; firstNonNullRootCallbackBoundary="C011EC06 prerequisite retained"; firstRootCandidateLoad="C011EC05 prerequisite retained"; threadStaticPrimitive="historical proof retained"; threadStaticReference="historical proof retained"; threadStaticCombined="historical proof retained"; firstPerThreadRootProvider="C011EC04 prerequisite retained"; allocationContextFixupRootBoundary="C011EC03 prerequisite retained"; singleThreadSuspendEe="C011EC02 prerequisite retained"; staticChecks="PASS script parse, manifest parse, serial classification, ordinary-kernel restoration, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not rerun in this focused execution; historical manifests retained without relabeling"; nativeStackWrapper="historically non-clean; not called passed"; promotionCompletion="intentionally out of scope; proof stops before generation classification/promotion" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md"); evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md"
            ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 28 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-membership-classification experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isFirstRootFirstNonNullOldO) {
        $allNonNullOldOBlockedRuns = @($runResults).Count -eq $FreshBootCount -and
            @($runResults | Where-Object { $_.outcome -eq "E" -and $_.earlyFailure -eq "c011ec14-no-non-null-old-o-before-nativeaot-failfast" }).Count -eq $FreshBootCount
        if ($allNonNullOldOBlockedRuns) {
            $firstBlockedRun = $runResults[0]
            $manifest = [ordered]@{
                outcome="D / the valid bounded workload reached one real queue insertion and the first null old_o decision, then the source-valid route to natural displacement required a new semantic unit before another queue insertion; no naturally valid non-null old_o or marked(old_o) read was obtained"
                proofMode=$ProofMode; marker="C011EC14 not reached"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
                taskStartCheckpoint=$taskStartCheckpoint
                lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
                ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
                priorCheckpoint=[ordered]@{ marker="C011EC13"; commit="684b6fb507e4158191e52489af32a894dde8fc75"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md"; outcome="first valid queue_mark old_o was null" }
                workload=[ordered]@{ managedMode="FirstNonNullOldO"; rootField="[ThreadStatic] byte[]? s_gcProofThreadRoot"; additionalRoots="none retained; ordinary locals and GC handles did not produce a candidate before the bounded fail-fast"; fabricatedSlot=$false; fabricatedObject=$false; graphTraversal=$false }
                root=$firstBlockedRun.root
                queue=$firstBlockedRun.queue
                decision=$firstBlockedRun.decision
                markState=$firstBlockedRun.markState
                sourceTrace=[ordered]@{ promote="locked src/coreclr/gc/gc.cpp:49474-49544"; markObjectSimple="locked gc.cpp:27987-28029; first queue_mark call at :28007"; queueDeclaration="locked gcpriv.h:1487-1504; slot_table[16] and curr_slot_index"; queueDefinition="locked gc.cpp:27303-27335; old_o capture at :27316-27317, slot write at :27318, cursor advance at :27320, null return at :27321-27322, marked(old_o) at :27328, set_marked(old_o) at :27333"; markedRead="not reached"; nextRequiredBoundary="a source-valid route that supplies additional real queue insertions before the runtime continuation/fail-fast boundary" }
                mutation=[ordered]@{ prerequisiteQueueSlotWrites="0x00000001"; prerequisiteCursorWrites="0x00000001"; markHeaderReads="0x00000000"; markHeaderWrites="0x00000000"; otherObjectHeaderWrites="0x00000000"; segmentWrites="0x00000000"; worklistWrites="0x00000001"; newC011EC14Mutation="0x00000000" }
                traversal=[ordered]@{ graphTraversal="0x00000000 observed before fail-fast"; childReferenceReads="0x00000000"; childObjects="0x00000000"; secondObjectMarkAttempts="0x00000000" }
                threadStore=[ordered]@{ lockHeld="0x00000001"; eeSuspended="0x00000001"; managedEntryProhibited="0x00000001"; restart="0x00000000"; managedResume="0x00000000" }
                qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
                regressions=[ordered]@{ c011ec14="Outcome D $FreshBootCount/$FreshBootCount fresh QEMU 11.0.0 boots; deterministic bounded stop before non-null displacement"; c011ec13="PASS prerequisite checkpoint retained"; c011ec12="PASS prerequisite checkpoint retained"; c011ec10="PASS/source prerequisite retained"; staticChecks="PASS script parse, manifest parse, serial classification, ordinary restoration, git diff --check" }
                retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical mismatch"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN historical evidence" }
                blockedNonClean=[ordered]@{ nonNullOldO="blocked before naturally valid occupied displacement"; broadRegressionSuite="not rerun as one combined suite" }
                documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md")
                evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
            }
            $manifest | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
            Write-Host "NativeAOT Workstation GC first-root-first-non-null-old-o experiment: Outcome D" -ForegroundColor Yellow
        } else {
        $nonNullOldORuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC14" })
        if (@($runResults).Count -ne 3 -or $nonNullOldORuns.Count -ne 0) { throw "The C011EC14 experiment did not produce three C011EC14 runs." }
        $firstNonNullOldORun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / genuine NativeAOT Workstation GC queue_mark naturally wrapped its 16-slot ring; the first non-null displaced old_o completed the actual marked(old_o) read/decision and stopped before set_marked(old_o)"
            proofMode=$ProofMode; marker="C011EC14"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC13"; commit="684b6fb507e4158191e52489af32a894dde8fc75"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md"; outcome="first valid queue_mark old_o was null" }
            workload=[ordered]@{ managedMode="FirstNonNullOldO"; rootFields=17; queueCapacity=16; ordinaryManagedRoots=$true; fabricatedSlot=$false; fabricatedObject=$false; graphTraversal=$false }
            root=$firstNonNullOldORun.root
            queue=$firstNonNullOldORun.queue
            decision=$firstNonNullOldORun.decision
            markState=$firstNonNullOldORun.markState
            provenance=$firstNonNullOldORun.provenance
            counters=$firstNonNullOldORun.counters
            mutation=$firstNonNullOldORun.mutation
            traversal=$firstNonNullOldORun.traversal
            sourceTrace=[ordered]@{ promote="locked src/coreclr/gc/gc.cpp:49474-49544"; markObjectSimple="locked gc.cpp:27987-28029; loads settings.condemned_generation then *po and calls mark_queue.queue_mark(o)"; queueDeclaration="locked gcpriv.h:1487-1504; slot_table[16] and curr_slot_index"; queueDefinition="locked gc.cpp:27303-27335; old_o capture at :27316-27317, slot write at :27318, cursor advance at :27320, null return at :27321-27322, marked(old_o) at :27328, set_marked(old_o) at :27333"; markedMacro="locked gc.cpp:11587: marked(i) -> header(i)->IsMarked()"; markedRead="locked gc.cpp:4789-4792: CObjectHeader::IsMarked reads RawGetMethodTable() and tests GC_MARKED"; stop="after the actual marked(old_o) result and before the locked set_marked(old_o) mutation" }
            safeStop=[ordered]@{ marker="C011EC14"; reason="first non-null displaced old_o completed actual mark-state read/decision; no mark-bit write, traversal, callback completion, restart, or resume"; callbackReturnsBeforeDecision=$firstNonNullOldORun.counters.callbackReturnsBeforeDecision; restartAllowed=$false; resumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec14="PASS 3/3 fresh QEMU 11.0.0 boots"; c011ec13="PASS prerequisite checkpoint retained"; c011ec12="PASS prerequisite checkpoint retained"; c011ec10="source/path prerequisite retained"; historicalSuite="retained without relabeling"; staticChecks="PASS script parse, manifest parse, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained historical mismatch"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN historical evidence" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun as one combined suite"; markingCompletion="out of scope; proof stops before set_marked(old_o)" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-first-non-null-old-o experiment: Outcome A" -ForegroundColor Green
        }
    } elseif ($isFirstRootPostQueueMarkDecision) {
        $nonPostQueueRuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC13" })
        if (@($runResults).Count -ne 3 -or $nonPostQueueRuns.Count -ne 0) { throw "The C011EC13 experiment did not produce three C011EC13 runs." }
        $firstPostQueueRun = $runResults[0]
        $postQueueOutcome = $firstPostQueueRun.outcome
        if (@($runResults | Where-Object { $_.outcome -ne $postQueueOutcome }).Count -ne 0) { throw "C011EC13 post-queue outcome was not deterministic across the three QEMU runs." }
        $postQueueSerial = Get-Content -LiteralPath $firstPostQueueRun.serial -Raw
        $postQueueMachineCodePath = Join-Path $runRoot 'post-queue-machine-code.txt'
        Require-File $postQueueMachineCodePath "C011EC13 machine-code evidence"
        $postQueueMachineCode = Get-Content -LiteralPath $postQueueMachineCodePath -Raw
        $getPostQueueMachineField = {
            param([string]$Name)
            $match = [regex]::Match($postQueueMachineCode, '(?im)^' + [regex]::Escape($Name) + '=(?<value>.*)$')
            if ($match.Success) { return $match.Groups['value'].Value.Trim() }
            return $null
        }
        $manifest = [ordered]@{
            outcome="$postQueueOutcome / genuine NativeAOT root completed the real queue_mark old_o null/mark-state decision; stopped before later mutation or traversal"
            proofMode=$ProofMode; marker="C011EC13"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; gcCppSha256=(Hash-File $lockedGcCppPath); gcprivSha256=(Hash-File $lockedGcPrivPath); gcwksSha256=(Hash-File $lockedGcwksPath); productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC12"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md"; stop="after the authentic slot/cursor queue invariant and before old_o decision" }
            proofRoot=$firstPostQueueRun.root; findRange=[ordered]@{ result="0x00000001"; rawRoot=$firstPostQueueRun.root.rawValue; membership=(Get-MarkerField $postQueueSerial 'membership') }
            workstationHeap=[ordered]@{ multipleHeaps=(Get-MarkerField $postQueueSerial 'wksMultipleHeaps'); hpt=(Get-MarkerField $postQueueSerial 'hpt'); heapOf=(Get-MarkerField $postQueueSerial 'heapOf'); heapNumber=(Get-MarkerField $postQueueSerial 'heapNumber'); heapCount=(Get-MarkerField $postQueueSerial 'heapCount'); nullSentinelValid=(Get-MarkerField $postQueueSerial 'wksNullHeapValid') }
            condemnedGeneration=[ordered]@{ result=(Get-MarkerField $postQueueSerial 'condemned'); generation=(Get-MarkerField $postQueueSerial 'generationFromRegion'); condemnedGeneration=(Get-MarkerField $postQueueSerial 'condemnedGeneration'); maximumGeneration=(Get-MarkerField $postQueueSerial 'maximumGeneration') }
            queueMark=[ordered]@{ declaration="gcpriv.h:2729 mark_object_simple; gcpriv.h:1487-1504 mark_queue_t"; definition="gc.cpp:27987-28029 mark_object_simple; gc.cpp:27303-27335 queue_mark"; slotAddress=$firstPostQueueRun.queue.slotAddress; slotIndex=$firstPostQueueRun.queue.slotIndex; oldSlot=$firstPostQueueRun.queue.oldSlot; newSlot=$firstPostQueueRun.queue.newSlot; oldObject=$firstPostQueueRun.queue.oldObject; oldObjectMeaning="previous contents of slot_table[slot_index], captured before insertion"; cursorBefore=$firstPostQueueRun.queue.cursorBefore; cursorAfter=$firstPostQueueRun.queue.cursorAfter; capacity="0x0000000000000010"; inheritedSlotWrites=$firstPostQueueRun.mutation.inheritedQueueSlotWrites; inheritedCursorWrites=$firstPostQueueRun.mutation.inheritedCursorWrites }
            sourceTrace=[ordered]@{ markObjectSimple="gc.cpp:27987-28029; first reads condemned generation :27991-27996 and *po :27998; delegates at :28007"; queueDeclaration="gcpriv.h:1487-1504; slot_table[16] and curr_slot_index"; queueDefinition="gc.cpp:27303-27335; one-argument queue_mark"; queueWrites="gc.cpp:27316-27320 old_o capture, slot_table write, cursor advance"; nullBranch="gc.cpp:27321-27322 if(old_o==nullptr) return nullptr"; markedMacro="gc.cpp:11587 #define marked(i) header(i)->IsMarked()"; markedImplementation="gc.cpp:4789-4792 CObjectHeader::IsMarked reads GC_MARKED from RawGetMethodTable"; markRepresentation="object-header raw method-table-word GC_MARKED bit"; markedMutates=$false; markedDereferences="only non-null path; skipped for null old_o"; setMarked="gc.cpp:27333 set_marked(old_o), not reached"; childTraversal="gc.cpp:28014 go_through_object_cl, not reached"; nextMutation="inline set_marked(old_o) at gc.cpp:27333 on non-null/unmarked path; not executed" }
            decision=$firstPostQueueRun.decision; markState=[ordered]@{ representation="CObjectHeader::IsMarked / GC_MARKED"; reads=$firstPostQueueRun.decision.markStateReads; result=$firstPostQueueRun.decision.markedResult; objectHeaderReads=(Get-MarkerField $postQueueSerial 'objectHeaderReads'); methodTableReads=(Get-MarkerField $postQueueSerial 'methodTableReads'); segmentReads=(Get-MarkerField $postQueueSerial 'segmentReads'); regionReads=(Get-MarkerField $postQueueSerial 'regionReads') }
            mutation=[ordered]@{ inheritedFromC011EC12=2; inheritedQueueSlotWrites=$firstPostQueueRun.mutation.inheritedQueueSlotWrites; inheritedCursorWrites=$firstPostQueueRun.mutation.inheritedCursorWrites; newPostQueueAttempts=$firstPostQueueRun.mutation.newAttempts; newPostQueueExecutions=$firstPostQueueRun.mutation.newExecutions; markBitWrites=$firstPostQueueRun.mutation.markBitWrites; logicalMarkComplete=(Get-MarkerField $postQueueSerial 'logicalMarkComplete'); objectHeaderWrites=0; payloadWrites=0; segmentWrites=0; regionWrites=0; overflowWrites=0; traversalWorkItemWrites=0 }
             traversal=$firstPostQueueRun.traversal; nextMutationBoundary=[ordered]@{ helper="inline mark_queue_t::queue_mark set_marked(old_o)"; source="locked gc.cpp:27333; machine evidence identifies the following inline OR on the old object's raw method-table word"; instruction=(& $getPostQueueMachineField 'nextMutationInstruction'); machineInstruction=(& $getPostQueueMachineField 'nextMutationInstruction'); machineLine=(& $getPostQueueMachineField 'nextMutationLine'); decisionReturnAddress=$firstPostQueueRun.decision.decisionReturnAddress; safeStopAddress=$firstPostQueueRun.decision.safeStopAddress; executed=$false }
            threadStore=$firstPostQueueRun.threadStore; execution=[ordered]@{ callbackReturns=(Get-MarkerField $postQueueSerial 'callbackReturns'); secondCallbacks=(Get-MarkerField $postQueueSerial 'secondCallbacks'); restart=(Get-MarkerField $postQueueSerial 'restart'); resume=(Get-MarkerField $postQueueSerial 'resume') }; objectAndSentinelValidation=$firstPostQueueRun.validation
            machineCode=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64"; helperSymbol="?mark_object_simple@gc_heap@WKS@@CAXPEAPEAE@Z"; helperAddress=(Get-MarkerField $postQueueSerial 'markHelper'); inheritedQueueSlotWriteInstruction=(& $getPostQueueMachineField 'inheritedQueueSlotWriteInstruction'); inheritedQueueSlotWriteLine=(& $getPostQueueMachineField 'inheritedQueueSlotWriteLine'); inheritedCursorWriteInstruction=(& $getPostQueueMachineField 'inheritedCursorWriteInstruction'); inheritedCursorWriteLine=(& $getPostQueueMachineField 'inheritedCursorWriteLine'); nullDecisionHelper=(& $getPostQueueMachineField 'nullDecisionHelper'); nullDecisionHelperAddress=(& $getPostQueueMachineField 'nullDecisionHelperAddress'); nullDecisionCallSite=(& $getPostQueueMachineField 'nullDecisionCallSite'); nullDecisionCallLine=(& $getPostQueueMachineField 'nullDecisionCallLine'); nullTestInstruction=(& $getPostQueueMachineField 'nullTestInstruction'); nullBranchInstruction=(& $getPostQueueMachineField 'nullBranchInstruction'); markedRepresentation=(& $getPostQueueMachineField 'markedRepresentation'); markedOutOfLineHelper=(& $getPostQueueMachineField 'markedOutOfLineHelper'); nextMutationInstruction=(& $getPostQueueMachineField 'nextMutationInstruction'); nextMutationLine=(& $getPostQueueMachineField 'nextMutationLine'); evidence=$postQueueMachineCodePath; disassembly=(Join-Path $runRoot 'artifact-disassembly.txt') }
            safeStop=[ordered]@{ marker="C011EC13"; classification=$postQueueOutcome; reason="one real old_o null/marked decision completed after C011EC12 queue unit; stopped before callback return, later mutation, traversal, or second object"; childTraversalAllowed=$false; secondObjectAllowed=$false; restartAllowed=$false; resumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec13="PASS 3/3 fresh QEMU 11.0.0 boots"; c011ec12="focused prerequisite retained"; c011ec11="focused prerequisite retained"; historicalSuite="retained without relabeling"; staticChecks="PASS script parse, manifest parse, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ c011ec10ValidatorMismatch="retained historical mismatch"; historicalFirst64KiBExecution="retained"; staleCacheAttempts="retained"; initialRuntimePackIdentityMismatch="retained"; nativeStackPowerShellWrapper="retained NON-CLEAN"; localStorageTeardown="retained historical non-clean evidence" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun as one combined suite"; historicalValidators="obsolete hashes/assertions remain blocked/non-clean"; markingCompletion="out of scope" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-post-queue-mark-decision experiment: Outcome $postQueueOutcome" -ForegroundColor Green
    } elseif ($isFirstRootFirstMarkMutation) {
        Write-Host "C011EC12 finalization begin"
        $nonFirstMarkRuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC12" })
        if (@($runResults).Count -ne 3 -or $nonFirstMarkRuns.Count -ne 0) { throw "The C011EC12 experiment did not produce three C011EC12 runs." }
        $firstMarkRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="D / genuine NativeAOT root completed the smallest safe queue_mark two-write mutation unit; stopped before old_o mark-state read, mark-bit write, promotion, or traversal"
            proofMode=$ProofMode; marker="C011EC12"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC11"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md"; stop="immediately before mark_object_simple" }
            proofRoot=$firstMarkRun.root; findRange=[ordered]@{ result="0x00000001"; rawRoot=$firstMarkRun.root.rawValue; membership=$firstMarkRun.root.membership }; workstationHeap=$firstMarkRun.heap; condemnedGeneration=$firstMarkRun.condemned; markHelper=$firstMarkRun.markHelper; mutation=$firstMarkRun.mutation; counters=$firstMarkRun.counters; traversal=$firstMarkRun.traversal; threadStore=$firstMarkRun.threadStore; objectAndSentinelValidation=$firstMarkRun.validation
            sourceTrace=[ordered]@{ promoteRange="locked src/coreclr/gc/gc.cpp:49474-49544"; markObjectSimpleDeclaration="locked gcpriv.h:2729"; markObjectSimpleDefinition="locked gc.cpp:27987-28029"; markObjectSimpleFirstReads=@("settings.condemned_generation :27991-27996", "*po :27998"); delegate="mark_queue.queue_mark(o) :28007"; queueDeclaration="locked gcpriv.h:1487-1504"; queueDefinition="locked gc.cpp:27303-27335"; conditionals=@("MARK_PHASE_PREFETCH active; slot_count=16", "USE_REGIONS active", "MULTIPLE_HEAPS=0"); firstReads=@("curr_slot_index", "slot_table[slot_index] -> old_o"); firstWrites=@("slot_table[slot_index] = o", "curr_slot_index = (slot_index + 1) % slot_count"); nextOperation="old_o null test :27321, then marked(old_o) :27328"; childTraversal="go_through_object_cl :28014, not reached" }
            markWorklist=[ordered]@{ structure="mark_queue_t::slot_table[16] plus curr_slot_index"; queueBase=$firstMarkRun.mutation.queueBase; capacity=$firstMarkRun.mutation.capacity; target=$firstMarkRun.mutation.target; old=$firstMarkRun.mutation.old; new=$firstMarkRun.mutation.new; pointerStoredDirectly=($firstMarkRun.mutation.new -eq $firstMarkRun.root.storageObject); indexBefore=$firstMarkRun.mutation.slotIndexBefore; cursorBefore=$firstMarkRun.mutation.cursorBefore; indexAfter=$firstMarkRun.mutation.slotIndexAfter; cursorAfter=$firstMarkRun.mutation.cursorAfter; logicalMarkAfterWrite=$false }
            machineCode=[ordered]@{ architecture="AMD64"; helperSymbol="?mark_object_simple@gc_heap@WKS@@CAXPEAPEAE@Z"; helperAddress=(Get-MarkerField (Get-Content -LiteralPath $firstMarkRun.serial -Raw) 'markHelper'); firstMutationInstruction=$firstMarkRun.mutation.firstInstruction; nextMutationInstruction=$firstMarkRun.mutation.nextInstruction; evidence=(Join-Path $runRoot 'first-mark-machine-code.txt'); disassembly=(Join-Path $runRoot 'artifact-disassembly.txt') }
            safeStop=[ordered]@{ marker="C011EC12"; classification="Outcome D"; reason="slot insertion and cursor advancement are the smallest complete queue_mark invariant; stopped before old_o/marked read"; secondMutationAllowed=$false; childTraversalAllowed=$false; secondRootAllowed=$false; restartAllowed=$false; resumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec12="PASS 3/3 fresh QEMU 11.0.0 boots"; c011ec11="PASS 3/3 fresh QEMU 11.0.0 focused regression"; c011ec10="historical focused rerun remains non-clean; not relabeled"; broadRegressionSuite="not rerun as one suite"; staticChecks="script parse, manifest parse, serial evidence, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ c011ec10FocusedRegression="historical zero-promotion/object-read assertion mismatch retained"; historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun as one suite"; promotionCompletion="intentionally out of scope"; nativeStackWrapper="historically non-clean" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-first-mark-mutation experiment: Outcome D" -ForegroundColor Green
    } elseif ($isFirstRootPreMarkBoundary) {
        Write-Host "C011EC11 finalization begin"
        $nonPreMarkRuns = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC11" })
        if (@($runResults).Count -ne 3 -or $nonPreMarkRuns.Count -ne 0) { throw "The C011EC11 experiment did not produce three C011EC11 runs." }
        $firstPreMarkRun = $runResults[0]
        $machineCodeEvidence = Get-Content -LiteralPath (Join-Path $runRoot 'pre-mark-machine-code.txt') -Raw
        $manifest = [ordered]@{
            outcome="A / the genuine NativeAOT thread-static storage root traversed the real condemned=true Workstation GC true branch and stopped immediately before mark_object_simple"
            proofMode=$ProofMode; marker="C011EC11"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; productionInitializeModules=$true; productionThreadStatic=$true }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            priorCheckpoint=[ordered]@{ marker="C011EC10"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md"; prerequisite="real root, membership=true, valid WKS null heap sentinel, condemned_generation=0 and generation=0" }
            proofRoot=$firstPreMarkRun.root
            findRange=[ordered]@{ result="0x00000001"; lowerBound="0x0000000100000000"; upperBound="0x0000000102600000"; rawRoot=$firstPreMarkRun.root.rawValue }
            workstationHeap=$firstPreMarkRun.heap
            condemnedGeneration=$firstPreMarkRun.condemned
            trueBranch=$firstPreMarkRun.trueBranch
            dprintf=$firstPreMarkRun.dprintf
            rootFlags=[ordered]@{ raw=$firstPreMarkRun.flags.raw; tests=$firstPreMarkRun.flags.tests; consulted=@("GC_CALL_INTERIOR=0x1 -> false", "GC_CALL_PINNED=0x2 -> false"); nativeAotSpecificBitsConsulted=$false }
            scanContext=$firstPreMarkRun.scanContext
            sourceTrace=[ordered]@{
                promoteRange="locked src/coreclr/gc/gc.cpp:49474-49544"
                statementsBetweenCondemnedTrueAndMark=@(
                    'gc.cpp:49507 dprintf (3, ("Promote %zx", (size_t)o));; TRACE_GC is not defined, so dprintf expands to empty',
                    "gc.cpp:49509-49515 if (flags & GC_CALL_INTERIOR); false for raw flags 0, so hp->find_object(o) is not called",
                    "gc.cpp:49517-49525 FEATURE_CONSERVATIVE_GC gate; GetConservativeGC()=true in this build, then CObjectHeader::IsFree() reads the object method-table slot and isFree=false",
                    "gc.cpp:49527-49529 _DEBUG gate; CObjectHeader::Validate() entered",
                    "gc.cpp:4724 Validate reads the object method-table slot; _ASSERTE(pMT->SanityCheck()) is compiled as a no-op by the included stressLog.h definition",
                    "gc.cpp:4728-4729 Validate reads HEAPVERIFY_NO_RANGE_CHECKS; result false",
                    "gc.cpp:4734 IsHeapPointer(this, TRUE) performs the source-required segment-map lookup and returns true; the large-heap fallback is not entered",
                    "gc.cpp:4753 VERIFY_HEAP reads HEAPVERIFY_GC; result false, so ValidateObjectMember is not called",
                    "gc.cpp:49533-49534 if (flags & GC_CALL_PINNED); false for raw flags 0, so pin_object is not called",
                    "gc.cpp:49536-49539 STRESS_PINNING is not compiled",
                    "gc.cpp:49541 hpt->mark_object_simple(&o THREAD_NUMBER_ARG) is the first mutation-capable invocation and was not executed"
                )
                conditionalCompilation=[ordered]@{ USE_REGIONS=$true; FEATURE_CONSERVATIVE_GC=$true; VERIFY_HEAP=$true; sourceDebugBranch=$true; TRACE_GC=$false; STRESS_PINNING=$false; DEBUG_DestroyedHandleValue=$false; MARK_PHASE_PREFETCH=$true }
                scanContextConsultedAfterCondemned=$false
                helperCallsBeforeMark=@("CObjectHeader::IsFree", "CObjectHeader::Validate", "GCHeap::IsHeapPointer", "gc_heap::find_segment")
                objectMetadataReads=$firstPreMarkRun.metadata
                generationMetadataAfterDecision=[ordered]@{ sourceRequiredSegmentLookups=0; condemnedDecisionGenerationTableReads=$firstPreMarkRun.condemned.generationTableReads; condemnedDecisionSegmentLookups=$firstPreMarkRun.condemned.segmentLookups; preMarkDebugSegmentReads=$firstPreMarkRun.metadata.segmentReads }
            }
            markContract=[ordered]@{ declaration="gcpriv.h:2729: PER_HEAP_METHOD void mark_object_simple(uint8_t** o THREAD_NUMBER_DCL)"; definition="gc.cpp:27989-28029: void gc_heap::mark_object_simple(uint8_t** po THREAD_NUMBER_DCL)"; parameters=@("uint8_t** po", "THREAD_NUMBER_DCL empty for Workstation"); return="void"; firstReads=@("settings.condemned_generation at gc.cpp:27991-27996", "*po at gc.cpp:27998"); delegate="mark_queue_t::queue_mark(o), FORCEINLINE gc.cpp:27308-27335"; markStateRead="marked(old_o), gc.cpp:27328, not executed"; firstStateWrite="MARK_PHASE_PREFETCH slot_table[slot_index]=o, gc.cpp:27318-27322, not executed"; firstMutationCapability="queue_mark worklist slot write precedes mark-state read"; inlineStatus="mark_object_simple is out-of-line; queue_mark is inlined" }
            machineCode=[ordered]@{ architecture="AMD64"; helperSymbol=$markHelperSymbolName; helperAddress=$firstPreMarkRun.mutation.markHelper; callSite=$firstPreMarkRun.mutation.callSite; boundaryReturn=$firstPreMarkRun.mutation.boundaryReturn; firstMutationInstruction=$firstPreMarkRun.mutation.firstMutationInstruction; evidence=$machineCodeEvidence; callSiteCorrelation="Promote call to mark_object_simple is retained after the ordinary-return boundary observer; execution stopped in the observer before the call"; disassembly=(Join-Path $runRoot 'artifact-disassembly.txt'); map=$mapPath; callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText" }
            preMarkPredicates=$firstPreMarkRun.metadata
            mutation=$firstPreMarkRun.mutation
            threadStore=$firstPreMarkRun.threadStore
            objectAndSentinelValidation=$firstPreMarkRun.validation
            safeStop=[ordered]@{ marker="C011EC11"; reason="final clean pre-mark boundary immediately before hpt->mark_object_simple; no call returned"; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; childTraversalAllowed=$false; callbackReturnAllowed=$false; managedResumeAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults }
            regressions=[ordered]@{ C011EC11="PASS 3/3 fresh QEMU 11.0.0 boots"; C011EC10="retained historical PASS; focused rerun pending"; C011EC09="retained historical valid WKS-null interpretation; focused rerun pending"; C011EC08="retained historical PASS; focused rerun pending"; C011EC07="retained historical PASS; focused rerun pending"; C011EC06="retained historical PASS; focused rerun pending"; C011EC05="retained historical PASS; focused rerun pending"; C011EC01_to_C011EC03="retained historical validator-usable outcomes; not relabeled"; threadStaticVariants="retained historical primitive/reference/combined outcomes"; runtimeAndBuildChecks="script parse, manifest parse, serial evidence, exact kernel build, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ initialPreMarkHarnessAssertion="first generated run reached C011EC11 but failed overly literal harness assertions; serial retained"; historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun in this focused execution; historical manifests retained without relabeling"; nativeStackWrapper="historically non-clean; not called passed"; promotionCompletion="intentionally out of scope; first mark/promotion mutation not executed" }
            ordinaryRestoration=[ordered]@{ buildSha256=(Hash-File $kernelPath); espSha256=(Hash-File $espKernelPath); expectedSha256=$normalKernelHash; restored=$true }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md"; manifestPath=$manifestPath
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        Write-Host "C011EC11 manifest object built"
        $manifestForJson = [ordered]@{
            outcome=$manifest.outcome; proofMode=$ProofMode; marker="C011EC11"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingDirtyState=$dirtyState; ordinaryStartingBuildSha256=$ordinaryKernelBefore.build; ordinaryStartingEspSha256=$ordinaryKernelBefore.esp; ordinaryExpectedSha256=$normalKernelHash
            lockedRuntime="NativeAOT 9.0.0 AMD64 Workstation GC interfaces 5.3 / 2 source $lockedCommit"
            rootSlot=$firstPreMarkRun.root.slot; rawRoot=$firstPreMarkRun.root.rawValue; storageObject=$firstPreMarkRun.root.storageObject; sentinel=$firstPreMarkRun.root.sentinel; membership=$firstPreMarkRun.root.membership; wksHeap=$firstPreMarkRun.heap; condemned=$firstPreMarkRun.condemned; trueBranch=$firstPreMarkRun.trueBranch; dprintf=$firstPreMarkRun.dprintf; rootFlags=$firstPreMarkRun.flags; scanContext=$firstPreMarkRun.scanContext; metadata=$firstPreMarkRun.metadata; mutation=$firstPreMarkRun.mutation; threadStore=$firstPreMarkRun.threadStore; validation=$firstPreMarkRun.validation
            sourceRange="gc.cpp:49474-49544"; firstMutationHelper=$markHelperSymbolName; helperDeclaration="gcpriv.h:2729"; helperDefinition="gc.cpp:27989-28029"; firstMutationInstruction=$firstPreMarkRun.mutation.firstMutationInstruction; mutationCallSite=$firstPreMarkRun.mutation.callSite; boundaryReturn=$firstPreMarkRun.mutation.boundaryReturn; disassembly=(Join-Path $runRoot 'artifact-disassembly.txt'); machineCodeEvidence=(Join-Path $runRoot 'pre-mark-machine-code.txt')
            qemuVersion=$qemuVersion; proofKernelSha256=$specializedKernelHash; qemuRuns=@($runResults | ForEach-Object { [ordered]@{ name=$_.name; marker=$_.safeStopMarker; serial=$_.serial; serialSha256=$_.serialSha256; rawRoot=$_.root.rawValue; storageObject=$_.root.storageObject; markCalls=$_.mutation.markCalls; promotionWrites=$_.mutation.promotionWrites; markWrites=$_.mutation.markWrites; graphTraversal=$_.mutation.graphTraversal; objectMutation=$_.mutation.objectMutation; gcMetadataMutation=$_.mutation.gcMetadataMutation; segmentMutation=$_.mutation.segmentMutation; restart=$_.threadStore.restart; resume=$_.threadStore.resume } })
            regressions=$manifest.regressions; retainedFailures=$manifest.retainedFailures; blockedNonClean=$manifest.blockedNonClean; documentation=$manifest.documentation; reportPath=$manifest.reportPath; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestorationBuildSha256=$normalKernelHash; ordinaryRestorationEspSha256=$normalKernelHash
        }
        $manifestJson = $manifestForJson | ConvertTo-Json -Depth 8
        Write-Host "C011EC11 manifest JSON built"
        Set-Content -LiteralPath $manifestPath -Value $manifestJson -Encoding ASCII
        Write-Host "C011EC11 manifest written"
        Write-Host "NativeAOT Workstation GC first-root-pre-mark-boundary experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isStackProviderTransitionFailFast) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_["outcome"] -ne "D" }).Count -ne 0) { throw "The stack-provider transition fail-fast experiment did not produce $FreshBootCount deterministic Outcome D runs." }
        $firstTransitionRun = $runResults[0]
        $transitionSerial = Get-Content -LiteralPath $firstTransitionRun.serial -Raw
        $transitionLine = (($transitionSerial -split "`n") | Where-Object { $_ -match 'GcScanRoots-entry sentinel=' } | Select-Object -Last 1)
        $transitionStorageObject = Get-MarkerField $transitionSerial 'storageObject'
        $failFastLine = (($transitionSerial -split "`n") | Where-Object { $_ -match '\[nativeaot-pal-qemu-test\] FAIL_FAST reason=47435354' } | Select-Object -Last 1)
        $manifest = [ordered]@{
            outcome="D / Thread::GcScanRoots entry reached; StackFrameIterator prologue failed before first stack-provider callback"; proofMode=$ProofMode; marker="none (C011EC16 not reached)"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            ordinaryStartingHashes=[ordered]@{ build=$ordinaryKernelBefore.build; esp=$ordinaryKernelBefore.esp; expected=$normalKernelHash }; lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit }
            prerequisite=[ordered]@{ marker="C011EC15"; historicalOutcome="E retained"; correctedFinding="category 3 and Thread::GcScanRoots entry reached" }; firstRoot=[ordered]@{ field="[ThreadStatic] byte[]? s_gcProofThreadRoot"; sentinel=(Get-MarkerField $transitionLine 'sentinel'); storageObject=$transitionStorageObject; priorFullProof="0x100A01F38 / 0x100A02F50" }
            ordinaryThreadStaticProvider=[ordered]@{ entered=$true; returned="null"; callbackReturns=1; rootSlotsVisited=2; nullCandidates=1; nonNullCandidates=1; secondPromoteAttempts=0; secondQueueInsertions=0 }; queue=[ordered]@{ slotIndex=0; cursor="0 -> 1"; old="null"; new=$transitionStorageObject; drain=0; markBitWrites=0; childTraversal=0; preserved=$true }
            sourceTrace=[ordered]@{ gcScanRoots="locked nativeaot/Runtime/gcenv.ee.cpp:94-133; generated gcenv.ee.single-thread-suspend-ee.cpp:123-181"; ordinaryProvider="gcenv.ee.cpp:114-115"; categoryTransition="generated gcenv.ee.single-thread-suspend-ee.cpp:160-165"; threadUnderCrawl="generated gcenv.ee.single-thread-suspend-ee.cpp:165"; threadGcScanRoots="locked nativeaot/Runtime/thread.cpp:393-403"; iterator="locked nativeaot/Runtime/StackFrameIterator.cpp:1913-1935"; codeManagerLookup="locked nativeaot/Runtime/RuntimeInstance.cpp:101-109" }
            failFast=[ordered]@{ code="0x47435354"; bytesLittleEndian="54 53 43 47"; asciiMemoryOrder="TSCG"; msbToLsbText="GCST"; meaning="guideXOS startup-probe tag for shimmed RaiseFailFastException/abort/assert paths; not an official NativeAOT tag"; source="guidexos_nativeaot_gc_startup_probe.cpp:87; rhassert.h:63-65"; helperChain="CalculateCurrentMethodState -> RhFailFast -> RaiseFailFastException -> startupFailFast -> guidexos_nativeaot_pal_fail_fast" }
            machineCode=[ordered]@{ ordinaryEnumGcRefCall="0x10013b66 -> 0x100144b0"; category3Call="0x10013b76 -> 0x10006fd0"; threadUnderCrawlStore="0x10013ba0"; threadGcScanRootsCall="0x10013ba9 -> 0x10009910"; threadGcScanRootsEntry="0x10009910"; calculateCurrentMethodState="0x1000d730"; getCodeManager="0x1000d7a8 -> 0x1000abe0"; branch="0x1000d7b4 test rax,rax; 0x1000d7b7 jne 0x1000d7c8 not taken"; failFastCall="0x1000d7c3 call 0x10007f40; return 0x1000d7c8"; startupTagLoad="0x10007fb4 mov ecx,0x47435354"; immediateCaller="StackFrameIterator::CalculateCurrentMethodState"; lastHelper="RuntimeInstance::GetCodeManagerForAddress"; controlPC="0x10004d66"; codeManagerResult="NULL"; callOperands="RCX=0 RDX=0 R8=1"; gdbEvidence=(Join-Path $runRoot 'live-gdb-final4/live-gdb-final4.gdb-output.txt') }
            runtimeState=[ordered]@{ currentThread="0x393DC00"; enumeratedThread="0x393DC00"; collectionInitiator="0x393DC00"; threadStoreLockOwner="0x393DC00"; lockHeld=1; recursionDepth=1; threadStateFlags="0x1"; cooperative=1; preemptive=0; nativeThreadId="0x100D88D0"; stackBase="0x0"; stackLimit="0x0"; currentRSP="0x4E79088"; transitionFrameRIP="0x10004D66"; scanContext="0x4E79440"; threadUnderCrawl="0x393DC00"; scanContextStackLimit="0x0"; threadNumber=0; threadCount=1; promotion=1; concurrent=0 }
            allocationAudit=[ordered]@{ allocationsAttemptedAfterSuspension=0; proofDiagnosticAllocations=0; NativeAotAllocations=0; threadStaticAllocations=0; temporaryRuntimeAllocations=0; managedEntryAttempts=0; NativeAotHelpersInTransition=0; evidence="scalar counters/direct serial I/O only" }; instrumentationMinimization=[ordered]@{ change="suppressed optional diagnostic lines and retained only transition markers/counters"; allocationFree=$true; before="full C011EC15 output; 0x47435354"; after="minimal markers; 0x47435354"; behaviorChanged=$false; productionRuntimeChanged=$false }
            stackProvider=[ordered]@{ threadGcScanRootsCalls=1; functionEntry=1; prologueFailed=$true; framesWalked=0; stackRootSlotsVisited=0; stackNonNullCandidates=0; stackPromoteCalls=0; callbackCount=0; gcInfoRead=0; rootMapRead=0; stackDescriptorAccessed=0 }; invariants=[ordered]@{ eeSuspended=1; managedEntryProhibited=1; restart=0; resume=0; queueSlot0Preserved=1; queueCursorAtStop=1; markBitWrites=0; childReferenceReads=0; graphTraversal=0; objectMutation=0; sentinelUnchanged=1; storageObjectUnchanged=1 }
            outcomeClassification=[ordered]@{ result="D"; violatedPrecondition="GetCodeManagerForAddress(m_ControlPC) returned null; RuntimeInstance m_CodeManager and managed range were zero"; classification="real production stack/unwind/code-manager invariant failure inside Thread::GcScanRoots prologue, not proof instrumentation"; fixMade=$false; C011EC16="not reached" }; qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot 'commands.txt'); runs=$runResults; failFastLine=$failFastLine }
            regressions=[ordered]@{ failureIsolation="PASS 3/3 fresh QEMU Outcome D"; C011EC15="historical E retained"; C011EC14="historical D retained"; C011EC13="historical B retained"; C011EC12="historical D retained"; C011EC11="historical A retained"; C011EC10="historical A retained"; C011EC09="retained non-clean"; broaderSuite="historical evidence retained; focused pass only" }; historicalEvidence=[ordered]@{ C011EC15="retained"; historical64KiB="retained"; staleCache="retained"; runtimePackIdentityMismatch="retained"; nativeStackWrapper="retained non-clean"; localStorageTeardown="retained"; raceSerialWrap="retained" }
            documentation="docs/dotnet/NATIVEAOT_WORKSTATION_GC_STACK_PROVIDER_TRANSITION_FAILFAST.md"; evidenceRoot=$runRoot; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
        }
        $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT stack-provider transition fail-fast experiment: Outcome D (diagnostic boundary)" -ForegroundColor Yellow
    } elseif ($isNextGenuineRootProvider) {
        if (@($runResults).Count -ne $FreshBootCount -or @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC15" }).Count -ne 0) { throw "The C011EC15 experiment did not produce $FreshBootCount C011EC15 runs." }
        $firstC15Run = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / the first genuine inline thread-static root completed its bounded Workstation queue_mark path and root enumeration reached the next genuine non-null candidate before its second Promote"
            proofMode=$ProofMode; marker="C011EC15"; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            lockedRuntimeIdentity=[ordered]@{ nativeAot="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterfaces="5.3 / 2"; sourceCommit=$lockedCommit; generatedGcenv=$gcEnvEeSource; generatedGcEnum=$gcEnumSource; generatedGcwks=$gcWksSource }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryKernelBefore.build; startingEspSha256=$ordinaryKernelBefore.esp; expectedSha256=$normalKernelHash }
            prerequisite=[ordered]@{ marker="C011EC14"; outcome="D retained"; firstQueueInsertions=1; firstQueueSlot=0; firstQueueCursor="0 -> 1"; oldObject="null"; queueSeeding=$false }
            firstRoot=$firstC15Run.root; nextGenuineRoot=$firstC15Run.nextRoot; accounting=$firstC15Run.accounting; queue=$firstC15Run.queue; prohibited=$firstC15Run.prohibited; threadStore=$firstC15Run.threadStore
            providerOrder=[ordered]@{ lockedSource="inline thread-static roots -> ordinary thread-static storage -> Thread::GcScanRoots stack/register/root-map/exception/GC-frame roots; finalizer roots -> handles -> dependent handles later in GCHeap::GarbageCollectGeneration"; first=$firstC15Run.providerOrder.first; next=$firstC15Run.providerOrder.next; stackRootEntered=($firstC15Run.nextRoot.providerCategory -eq "0x00000003"); staticModuleEntered=$false; handlesEntered=$false; finalizerEntered=$false }
            callback=[ordered]@{ firstRootReturnCount=$firstC15Run.accounting.firstRootCallbackReturns; enumGcRefContinuations=$firstC15Run.accounting.enumGcRefContinuations; promoteReturns=$firstC15Run.accounting.promoteReturns; markHelperReturns=$firstC15Run.accounting.markHelperReturns; secondPromoteAttempts="0x00000000"; secondPromoteEntries="0x00000000" }
            provenance=[ordered]@{ firstRoot="real NativeAOT inline thread-static storage object"; secondRoot="real Object** observed through locked NativeAOT EnumGcRef continuation before callback"; genuineManagedObject=$true; graphTraversal=0; childReferenceReads=0; stackMapDecoder="not separately entered before the provider boundary" }
            sourceTrace=[ordered]@{ gcScanRoots="locked nativeaot/Runtime/gcenv.ee.cpp:94-133"; enumGcRef="locked nativeaot/Runtime/GcEnum.cpp:68-96 and :84-96"; threadRoots="locked nativeaot/Runtime/thread.cpp:393-403, :442-569"; queue="locked gc.cpp:27303-27335"; promote="locked gc.cpp:49474-49544"; gcPhase="locked gc.cpp:29897-29926" }
            qemu=[ordered]@{ version=$qemuVersion; runCount=$FreshBootCount; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ c011ec15="PASS $FreshBootCount/$FreshBootCount fresh QEMU runs"; c011ec14="Outcome D retained"; c011ec13_to_c011ec02="historical checkpoints retained"; c011ec09="historical non-clean interpretation retained"; threadStaticVariants="historical primitive/reference/combined evidence retained"; broadSuite="not rerun as one combined pass"; staticChecks="script parsing, manifest parsing, serial evidence, exact kernel build, ordinary restoration, git diff --check" }
            retainedFailures=[ordered]@{ historicalFirst64KiBExecution="retained"; staleCacheAttempts="retained"; initialRuntimePackIdentityMismatch="retained"; nativeStackPowerShellWrapper="retained NON-CLEAN"; localStorageTeardown="retained"; historicalRaceSerialWrap="retained" }
            blockedNonClean=[ordered]@{ broadRegressionSuite="not rerun in one combined pass"; stackSubkind="provider boundary reached; individual stack-map subkind not crossed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_NEXT_GENUINE_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_DISPLACED_MARK_READ.md")
            evidenceRoot=$runRoot; reportPath="docs/dotnet/NATIVEAOT_WORKSTATION_GC_NEXT_GENUINE_ROOT_PROVIDER.md"; manifestPath=$manifestPath; ordinaryRestoration=[ordered]@{ expectedBuildSha256=$normalKernelHash; expectedEspSha256=$normalKernelHash; restoredByFinally=$true }
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ sourceCommit=$lockedCommit; activeArchiveSha256=(Hash-File $activeArchive); stockUnchanged=$true }
        }
        $manifest | ConvertTo-Json -Depth 28 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC next-genuine-root-provider experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isFirstRootCallbackEntry) {
        $nonC011EC07Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC07" })
        if (@($runResults).Count -ne 3 -or $nonC011EC07Runs.Count -ne 0) { throw "The first real root callback-entry experiment did not produce three C011EC07 runs." }
        $firstCallbackRun = $runResults[0]
        $manifest = [ordered]@{
            outcome="A / real GCHeap::Promote callback entered exactly once with the genuine NativeAOT thread-static storage root and stopped before heap classification"
            proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            taskStartCheckpoint=$taskStartCheckpoint
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; status="working" }
            ordinaryBaseline=[ordered]@{ startingBuildSha256=$ordinaryBuildBefore; startingEspSha256=$ordinaryEspBefore; expectedSha256=$normalKernelHash }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); lockedGcCppSourceSha256=(Hash-File $lockedGcCppPath); lockedGcwksSourceSha256=(Hash-File $lockedGcwksPath); activePalArchiveSha256=(Hash-File $activeArchive); callbackSymbol=$callbackSymbolName; callbackAddress="0x$callbackAddressText" }
            priorCheckpoint=[ordered]@{ marker="C011EC06"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md"; stop="one genuine non-null NativeAOT thread-static storage root discovered before callback invocation" }
            managedProofRoot=$firstCallbackRun.managedProofRoot
            threadStaticRuntime=[ordered]@{ storageObjectAddress=$firstCallbackRun.managedProofRoot.storageObject; inlinedRootAddress=$firstCallbackRun.managedProofRoot.inlinedRoot; normalManagedSemantics=$true; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ callbackTypedef="promote_func(Object**, ScanContext*, uint32_t)"; callbackFunction="GCHeap::Promote"; callbackSource="locked src/coreclr/gc/gc.cpp:49474-49544, included by src/coreclr/gc/gcwks.cpp"; callbackGeneration="locked src/coreclr/gc/gc.cpp:29899-29901: GCScan::GcScanRoots(GCHeap::Promote, ...)"; rootProvider="locked src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:114-115"; enumGcRef="locked src/coreclr/nativeaot/Runtime/GcEnum.cpp:68-96"; callbackCallSite="locked GcEnum.cpp:81: fnGcEnumRef(ppObj, pSc, flags)"; firstSourceStatement="locked gc.cpp:49476 THREAD_NUMBER_FROM_CONTEXT"; firstCandidateLoad="locked gc.cpp:49481: uint8_t* o = (uint8_t*)*ppObject"; firstHeapClassification="locked gc.cpp:49483: gc_heap::is_in_find_object_range(o)"; firstMetadataAccess="locked gc.cpp:49496: gc_heap::heap_of(o) and subsequent generation/range checks"; firstPromotionMutation="locked gc.cpp:49533 pin_object / gc_heap::mark_object_simple at :49541"; generatedGcwks=$gcWksSource; generatedGcEnum=$gcEnumSource }
            abi=[ordered]@{ architecture="AMD64"; callingConvention="Microsoft x64"; argumentRegisters=@("RCX=Object** root slot", "RDX=ScanContext*", "R8D/R8=root flags uint32_t"); returnRegister="RAX unused for void"; shadowSpace="32 bytes required by caller"; callSiteDisassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackEntryDisassembly=(Join-Path $runRoot "artifact-disassembly.txt"); callbackSymbolFile=(Join-Path $runRoot "callback-symbol.txt") }
            callbackCallSite=$firstCallbackRun.callbackSite
            callbackEntry=$firstCallbackRun.callbackEntry
            rootArgument=$firstCallbackRun.root
            scanContext=$firstCallbackRun.scanContext
            flags=$firstCallbackRun.flags
            expectedArgumentMatches=[ordered]@{ argument1EqualsRealInlineRootSlot=($firstCallbackRun.callbackEntry.arg1 -eq $firstCallbackRun.root.firstNonNullSlot); argument1SlotLoadEqualsStorageObject=($firstCallbackRun.root.loadedValue -eq $firstCallbackRun.root.expectedStorageObject); argument2EqualsLiveProviderScanContext=($firstCallbackRun.callbackEntry.arg2 -eq $firstCallbackRun.callbackSite.scanContext); historicalKnownScanContext="0x0000000004E694E0 (prior checkpoint absolute; stack address is run-specific)"; argument3MatchesExpectedRootFlags=([Convert]::ToUInt64($firstCallbackRun.callbackEntry.arg3.Substring(2),16) -eq [Convert]::ToUInt64($firstCallbackRun.flags.expected.Substring(2),16)); callbackPointerMatchesLockedMap=("0x" + $callbackAddressText.ToUpperInvariant() -eq $firstCallbackRun.callbackSite.callback) }
            callbackCounts=[ordered]@{ requests=$firstCallbackRun.callbackRequestCount; callSiteEntries=$firstCallbackRun.callbackCallSiteCount; invocations=$firstCallbackRun.callbackInvocationCount; entries=$firstCallbackRun.callbackEntryCount; returns=$firstCallbackRun.callbackReturnCount; duplicates=$firstCallbackRun.duplicateCallbackInvocations }
            callbackSideRootLoad=[ordered]@{ count=$firstCallbackRun.root.callbackSideLoadCount; value=$firstCallbackRun.root.loadedValue; nullTests=(Get-MarkerField (Get-Content -LiteralPath $firstCallbackRun.serial -Raw) 'nullTests'); result="non-null storage object; one locked callback-side load" }
            semanticBoundary=$firstCallbackRun.semanticBoundary
            mutation=$firstCallbackRun.mutation
            objectAndSentinelValidation=$firstCallbackRun.managedProofRoot
            threadStore=$firstCallbackRun.threadStore
            safeStop=[ordered]@{ marker="C011EC07"; reason="real GCHeap::Promote entered with validated live ABI arguments, performed its source-required *ppObject load, then stopped immediately before gc_heap::is_in_find_object_range"; firstProhibitedOperation="candidate heap-membership classification at locked gc.cpp:49483"; promotionAllowed=$false; markingAllowed=$false; graphTraversalAllowed=$false; callbackReturnAllowed=$false }
            qemu=[ordered]@{ version=$qemuVersion; runCount=3; proofKernelSha256=$specializedKernelHash; serialSha256=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); runs=$runResults }
            regressions=[ordered]@{ firstRootCallbackEntry="PASS 3/3 fresh QEMU runs"; firstNonNullRootCallbackBoundary="C011EC06 prerequisite retained"; firstRootCandidateLoad="C011EC05 prerequisite retained"; threadStaticPrimitive="historical proof retained"; threadStaticReference="historical proof retained"; threadStaticCombined="historical proof retained"; firstPerThreadRootProvider="C011EC04 prerequisite retained"; allocationContextFixupRootBoundary="C011EC03 prerequisite retained"; singleThreadSuspendEe="C011EC02 prerequisite retained"; staticChecks="PASS script parse, manifest parse, serial classification, ordinary-kernel restoration, git diff --check" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed"; callbackPromotionCompletion="intentionally out of scope; proof stops before classification/promotion" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md"); evidenceRoot=$runRoot
            ordinaryRestoration=[ordered]@{ buildSha256=$normalKernelHash; espSha256=$normalKernelHash; expectedSha256=$normalKernelHash }
            authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-root-callback-entry experiment: PASS (Outcome A)" -ForegroundColor Green
    } elseif ($isFirstNonNullRoot) {
        $nonC011EC06Runs = @($runResults | Where-Object { $_["safeStopMarker"] -ne "C011EC06" })
        if (@($runResults).Count -ne 3 -or $nonC011EC06Runs.Count -ne 0) { throw "The first non-null root callback-boundary experiment did not produce three C011EC06 runs." }
        $firstEarlyRun = $runResults[0]
        $firstEarlyRun | Add-Member -NotePropertyName faultRip -NotePropertyValue $null -Force
        $firstEarlyRun | Add-Member -NotePropertyName faultCr2 -NotePropertyValue $null -Force
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
        $firstRun = $runResults[0]
        $proofRootMatch = $firstRun.candidateMatchesProofRoot -eq "0x00000001"
        $storageRootMatch = $firstRun.candidateMatchesStorageObject -eq "0x00000001"
        $outcome = if ($proofRootMatch -or $storageRootMatch) { "A / genuine managed [ThreadStatic] storage root reached the first non-null callback boundary" } else { "C / first genuine non-null candidate did not match the selected sentinel or storage object" }
        $manifest = [ordered]@{
            outcome=$outcome; proofMode=$ProofMode; repositoryHead=$repoHead; startingCommittedHead=$startingCommittedHead; startingBranch=$startingBranch; startingWorktreeStatus=$startingWorktreeStatus; startingDirtyState=$dirtyState; endingDirtyState=@(& git -C $root status --short)
            productionStartup=[ordered]@{ initializeModules="production runtime path invoked before ManagedMain"; status="working" }
            runtimePack=[ordered]@{ version="9.0.0"; architecture="AMD64"; gc="Workstation"; gcInterface="5.3"; ee="2"; sourceCommit=$lockedCommit; lockedGcenvEeSourceSha256=(Hash-File $lockedEePath); lockedGcEnumSourceSha256=(Hash-File $lockedGcEnumPath); activePalArchiveSha256=(Hash-File $activeArchive) }
            priorCheckpoint=[ordered]@{ marker="C011EC05"; report="docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md"; stop="one real candidate-slot machine-word load, before callback and semantic processing" }
            managedProofRoot=$firstRun.managedProofRoot
            threadStaticRuntime=[ordered]@{ storageAllocationCount=$firstRun.runtimeThreadStaticStorageAllocations; storagePublicationCount=$firstRun.runtimeThreadStaticStoragePublications; storageObjectAddress=$firstRun.runtimeThreadStaticStorageObject; inlinedRootAddress=$firstRun.runtimeThreadStaticInlinedRoot; normalManagedSemantics=$true; fabricatedSlot=$false; fabricatedObject=$false }
            sourceTrace=[ordered]@{ threadStatics="System.Private.CoreLib/src/Internal/Runtime/ThreadStatics.cs:36-50"; storageAllocation="ThreadStatics.cs:39-47 -> RuntimeImports.RhNewObject at :140"; threadStaticBase="nativeaot/Runtime/thread.cpp:1251-1261"; inlineThreadStaticRoot="nativeaot/Runtime/thread.h:76-84"; rootDispatcher="nativeaot/Runtime/gcenv.ee.cpp:94-133"; enumGcRef="nativeaot/Runtime/GcEnum.cpp:68-96"; callbackType="ScanFunc / promote_func: Object**, ScanContext*, uint32_t"; callbackNotInvoked=$true }
            workload=[ordered]@{ arrayLength=4096; userAllocations=$firstRun.userAllocations; userAllocationRequests=$firstRun.userAllocationRequests; fast=$firstRun.userFast; rare=$firstRun.userRare; refills=$firstRun.userRefills; sameSegmentCommits=$firstRun.userSameSegmentCommits; segmentTransitions=$firstRun.userSegmentTransitions; collectionAllocationOrdinal=$firstRun.collectionAllocationOrdinal; totalAllocationRequestsObserved=$firstRun.totalAllocationRequestsObserved; liveSentinels=4 }
            collection=[ordered]@{ requests=1; entries=1; generation="0x00000001"; reason="reason_oos_soh (5)"; blocking=$true; compacting=$false }
            suspension=[ordered]@{ entryCount=1; suspensionCount=1; successfulReturnCount=1; lockHeld=$true; lockDepth=1; eeSuspended=$true; managedEntryProhibited=$true; restartRequests=0; restartEntries=0; managedResumeCount=0 }
            allocationContextFixup=[ordered]@{ requestCount=1; entryCount=1; completionCount=1; objectMemoryMutation=0; objectValidationBefore=$firstRun.objectBeforeLoad; objectValidationAfter=$firstRun.objectAfterLoad; objectValidationAtStop=$firstRun.objectAtStop }
            threadEnumeration=[ordered]@{ registeredBefore=$firstRun.registeredThreads; registeredAfter=$firstRun.registeredAfterThreads; enumerated=$firstRun.enumeratedThreads; included=$firstRun.includedThreads; excluded=$firstRun.excludedThreads; duplicates=$firstRun.listDuplicates; integrityFailures=$firstRun.listIntegrityFailures; registryMutationBefore=$firstRun.registryMutationBefore; registryMutationAfter=$firstRun.registryMutationAfter; record=$firstRun.threadRecord; records=$runResults }
            provider=$firstRun.provider
            candidateBoundary=[ordered]@{ bound=8; visited=$firstRun.candidateVisited; nullCandidates=$firstRun.nullCandidates; nonNullCandidates=$firstRun.nonNullCandidates; firstNonNullSlot=$firstRun.firstNonNullSlot; firstNonNullValue=$firstRun.firstNonNullValue; firstNonNullKnownAddressMatch=$firstRun.firstNonNullKnownAddressMatch; candidateMatchesProofRoot=$firstRun.candidateMatchesProofRoot; candidateMatchesStorageObject=$firstRun.candidateMatchesStorageObject; expectedStorageObjectAddress=$firstRun.expectedStorageObjectAddress; callback=$firstRun.callback; scanContext=$firstRun.scanContext; condemnedGeneration=$firstRun.condemnedGeneration; maxGeneration=$firstRun.maxGeneration; scanContextPromotion=$firstRun.scanContextPromotion; scanContextConcurrent=$firstRun.scanContextConcurrent; rootFlags=$firstRun.rootFlags; rootKind=$firstRun.rootKind; loadRequests=$firstRun.loadRequests; loadEntries=$firstRun.loadEntries; machineWordLoads=$firstRun.machineWordLoads; duplicateLoads=$firstRun.duplicateLoads; loadFaults=$firstRun.loadFaults }
            prohibitedProcessing=[ordered]@{ candidatePointeeDereferences=$firstRun.candidateDereferences; heapMembershipTests=$firstRun.heapMembershipTests; objectHeaders=$firstRun.objectHeaders; methodTables=$firstRun.methodTables; rootFlagApplications=$firstRun.rootFlagApplications; callbacks=$firstRun.callbacks; promotions=$firstRun.promotions; marking=$firstRun.marking; objectMemoryMutation=$firstRun.objectMutation; restartRequests=$firstRun.restartRequests; restartEntries=$firstRun.restartEntries; managedResume=$firstRun.managedResume; sweep=0; compaction=0; relocation=0; graphTraversal=0 }
            sentinelAndObjects=[ordered]@{ liveSentinels=4; objectCount=40; objectValidationBeforeLoad=$firstRun.objectBeforeLoad; objectValidationAfterLoad=$firstRun.objectAfterLoad; objectValidationAtStop=$firstRun.objectAtStop; addressesUnchanged=$true; contentsUnchanged=$true; duplicateAddresses=0 }
            safeStop=[ordered]@{ marker="C011EC06"; reason="first non-null candidate loaded once, immediately before ScanFunc invocation"; callbackInvocations=0; deterministic=$true; lockHeld=$true; eeSuspended=$true }
            proofKernelSha256=$specializedKernelHash; qemuVersion=$qemuVersion; qemuRunCount=3; serialHashes=@($runResults | ForEach-Object { $_.serialSha256 }); exactCommandLog=(Join-Path $runRoot "commands.txt"); logsRoot=$runRoot; runs=$runResults
            regressions=[ordered]@{ firstNonNullRootCallbackBoundary="PASS 3/3 fresh QEMU runs"; firstRootCandidateLoad="historical PASS C011EC05 retained"; firstPerThreadRootProvider="historical PASS C011EC04 retained"; allocationContextFixupRootBoundary="historical PASS C011EC03 retained"; singleThreadSuspendEe="historical PASS C011EC02 retained"; staticChecks="PASS script parse, manifest parse, QEMU serial classification, ordinary-kernel restoration" }
            failedChecks=[ordered]@{ historicalFirst64KiBExecution="retained historical failure"; staleCacheAttempts="retained historical attempts"; initialRuntimePackIdentityMismatch="retained historical mismatch"; nativeStackPowerShellWrapper="retained NON-CLEAN exit 1 from compiler-stderr promotion" }
            blockedChecks=[ordered]@{ broadRegressionSuite="not rerun in this focused execution"; nativeStackWrapper="historically non-clean; not called passed" }
            documentation=@("docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md","docs/dotnet/NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md","docs/dotnet/NATIVEAOT_GC_STARTUP_READINESS.md","docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md"); evidenceRoot=$runRoot
            ordinaryKernelBefore=$ordinaryKernelBefore; ordinaryKernelExpectedSha256=$normalKernelHash; authorizedNormalizedAdaptedGcIdentity=[ordered]@{ strategy=$identity.strategy; sourceCommit=$identity.sourceCommit; stockSha256=$identity.stockSha256; adaptedSha256=$identity.adaptedSha256; adaptedLength=$identity.adaptedLength; replacementObjects=$replacementHashes; stockUnchanged=$identity.stockUnchanged }
        }
        $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
        Write-Host "NativeAOT Workstation GC first-non-null-root-callback-boundary experiment: $outcome" -ForegroundColor Green
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
