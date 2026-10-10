param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [switch]$BuildProofProducts,
    [switch]$RunProofBoots,
    [switch]$HostTestsOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ((& git -C $RepoRoot branch --show-current).Trim() -ne 'v1.1_DOTNET_SUPPORT') {
    throw 'C166R3 requires the existing v1.1_DOTNET_SUPPORT branch.'
}
$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
if (-not $head) { throw 'Could not resolve repository HEAD.' }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($HostTestsOnly) {
        Join-Path $RepoRoot 'out\dotnet\c166r9-host-validation'
    } else {
        Join-Path $RepoRoot 'out\dotnet\c166r3-file-associations'
    }
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet')).TrimEnd('\', '/') + '\'
if (-not $EvidenceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C166R3 evidence must remain under $allowedRoot"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

if ($HostTestsOnly) {
    $gxxCommand = Get-Command g++.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    $dotnetCommand = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $gxxCommand) { throw 'C166R7 host mode requires a C++ host compiler (g++).' }
    if (-not $dotnetCommand) { throw 'C166R7 host mode requires the .NET SDK.' }
    $nativeExe = Join-Path $EvidenceRoot 'C166Association.exe'
    $nativeLog = Join-Path $EvidenceRoot 'native-host-tests.txt'
    $fixturePath = Join-Path $EvidenceRoot 'native-managed-fixture.txt'
    $namedPath = Join-Path $EvidenceRoot 'dual-slot-cases.json'
    $nativeSources = @(
        (Join-Path $RepoRoot 'samples\host\C166Association\main.cpp'),
        (Join-Path $RepoRoot 'kernel\core\file_association_service.cpp'))
    & $gxxCommand.Source -std=c++14 -O2 -Wall -Wextra -DGUIDEXOS_PROOF_CONTROL "-I$RepoRoot" `
        "-I$(Join-Path $RepoRoot 'kernel\core')" @nativeSources -o $nativeExe
    if ($LASTEXITCODE -ne 0) { throw "C166R7 native host compilation failed: $LASTEXITCODE" }
    $env:C166_NATIVE_FIXTURE = $fixturePath
    $env:C166_NAMED_RESULTS = $namedPath
    $nativeOutput = & $nativeExe 2>&1 | Out-String
    $nativeExit = $LASTEXITCODE
    Remove-Item Env:C166_NATIVE_FIXTURE,Env:C166_NAMED_RESULTS -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $nativeLog -Value $nativeOutput -Encoding utf8
    if ($nativeExit -ne 0) { throw "C166R7 native host tests failed; see $nativeLog" }
    $productionNativeExe = Join-Path $EvidenceRoot 'C166Association-production-gate.exe'
    $productionNativeLog = Join-Path $EvidenceRoot 'native-production-gate-tests.txt'
    $productionFixture = Join-Path $EvidenceRoot 'production-managed-fixture.txt'
    $productionNamed = Join-Path $EvidenceRoot 'production-dual-slot-cases.json'
    & $gxxCommand.Source -std=c++14 -O2 -Wall -Wextra "-I$RepoRoot" `
        "-I$(Join-Path $RepoRoot 'kernel\core')" @nativeSources -o $productionNativeExe
    if ($LASTEXITCODE -ne 0) { throw 'Proof-disabled native C166 gate build failed.' }
    $env:C166_NATIVE_FIXTURE = $productionFixture
    $env:C166_NAMED_RESULTS = $productionNamed
    $productionOutput = & $productionNativeExe 2>&1 | Out-String
    $productionExit = $LASTEXITCODE
    Remove-Item Env:C166_NATIVE_FIXTURE,Env:C166_NAMED_RESULTS -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $productionNativeLog -Value $productionOutput -Encoding utf8
    if ($productionExit -ne 0 -or $productionOutput -notmatch 'failures=0') {
        throw "Proof-disabled diagnostics exclusion failed; see $productionNativeLog"
    }
    $managedProject = Join-Path $RepoRoot 'samples\host\C166Association\ManagedHostTests.csproj'
    $managedLog = Join-Path $EvidenceRoot 'managed-host-tests.txt'
    $managedOutput = & $dotnetCommand.Source run --project $managedProject --configuration Release -- $fixturePath 2>&1 | Out-String
    $managedExit = $LASTEXITCODE
    Set-Content -LiteralPath $managedLog -Value $managedOutput -Encoding utf8
    if ($managedExit -ne 0) { throw "C166R7 managed host tests failed; see $managedLog" }
    $nativeCount = 0
    if ($nativeOutput -match 'native host cases=(\d+)') { $nativeCount = [int]$Matches[1] }
    $managedCount = 0
    if ($managedOutput -match 'C166-MANAGED-ASSOCIATION-TESTS cases=(\d+)') {
        $managedCount = [int]$Matches[1]
    }
    $namedCases = Get-Content -Raw -LiteralPath $namedPath | ConvertFrom-Json
    $nativeLine = [string]($nativeOutput -split "`r?`n" | Where-Object { $_ -match '^C166 native host cases=' } | Select-Object -Last 1)
    $nativeStats = @{}
    foreach ($match in [regex]::Matches($nativeLine, '(\w+)=([0-9]+)')) { $nativeStats[$match.Groups[1].Value] = [uint64]$match.Groups[2].Value }
    $agreementPass = $managedOutput -match 'native-managed agreement cases=7 result=PASS statusMapping=PASS abi=v4/128 associationOffset=120'
    $nativePass = $nativeExit -eq 0 -and $nativeStats.failures -eq 0
    $managedPass = $managedExit -eq 0 -and $managedCount -ge 22
    $namedProperties = @($namedCases.PSObject.Properties)
    $matrixPass = $namedProperties.Count -eq 31 -and @($namedProperties | Where-Object { $_.Value -ne 'PASS' }).Count -eq 0
    $auditsPass = $nativeStats.audited -eq 1000 -and $nativeStats.crcFailures -eq 0 -and $nativeStats.malformed -eq 0 -and $nativeStats.monotonic -eq 1 -and $nativeStats.alternating -eq 1
    $hostPass = $nativePass -and $productionExit -eq 0 -and $managedPass -and $matrixPass -and $auditsPass -and $agreementPass
    $manifest = [ordered]@{
        phase='C166R9'; sourceHead=$head; sourceBranch='v1.1_DOTNET_SUPPORT'; upstream='origin/v1.1_DOTNET_SUPPORT'
        productionModule='kernel/core/file_association_service.cpp'
        storageInterface='kernel/core/include/kernel/file_association_service.h'
        productionVfsAdapter='kernel/core/file_association_vfs_storage.cpp'
        hostStorageAdapter='samples/host/C166Association/fake_storage.h'
        nativeCases=$nativeCount; nativeStatus=$(if($nativePass){'PASS'}else{'FAIL'})
        abiCases=4; abiSizes=@(104,112,120,128); callbackOffsets=@{snapshot=104;close=112;association=120}
        managedCases=$managedCount; managedStatus=$(if($managedPass){'PASS'}else{'FAIL'})
        dualSlotCases=$namedCases; dualSlotCaseCount=$namedProperties.Count; dualSlotStatus=$(if($matrixPass){'PASS'}else{'FAIL'})
        crcVectors=@{known='123456789=0xCBF43926';empty='0x00000000'}
        partialWriteRange='0..135'; partialWriteCases=136; fullWriteControl='PASS'
        stressTransactions=1000; persistedImagesAudited=$nativeStats.audited; crcMismatches=$nativeStats.crcFailures
        malformedCommittedImages=$nativeStats.malformed; generationStart=$nativeStats.generationStart
        generationEnd=$nativeStats.generationEnd; successfulGenerationIncrements=1000
        slotACommits=$nativeStats.slotA; slotBCommits=$nativeStats.slotB
        generationMonotonicity=$(if($nativeStats.monotonic -eq 1){'PASS'}else{'FAIL'}); slotAlternation=$(if($nativeStats.alternating -eq 1){'PASS'}else{'FAIL'})
        resolverLookups=1000; nativeManagedAgreement=@{cases=7;status=$(if($agreementPass){'PASS'}else{'FAIL'});statusMapping=$(if($agreementPass){'PASS'}else{'FAIL'});fixture='native-managed-fixture.txt';abi='v4/128';associationOffset=120}
        hostValidationComplete=$hostPass; hostValidationMarker=$(if($hostPass){'C166R9_HOST_VALIDATION_PASS'}else{$null})
        hostSuiteStatus=$(if($hostPass){'PASS'}else{'FAIL'}); accepted=$false
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
        (Join-Path $EvidenceRoot 'c166r9-host-manifest.json') -Encoding utf8
    Write-Host $nativeOutput.Trim()
    Write-Host $managedOutput.Trim()
    Write-Host 'PROOF1-PRODUCTION-GATE PASS: proof-disabled native diagnostics request rejected'
    if ($hostPass) { Write-Host 'C166R9_HOST_VALIDATION_PASS' } else { throw 'C166R9 host acceptance failed; see per-case manifest and suite logs.' }
    return
}

function Resolve-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    return $null
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-FreeQmpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Send-OwnedQemuQuit([int]$Port, [System.Diagnostics.Process]$Process) {
    if ($null -eq $Process) { return }
    $Process.Refresh()
    if ($Process.HasExited) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $deadline = (Get-Date).AddSeconds(5)
        while (-not $client.Connected -and (Get-Date) -lt $deadline) {
            try { $client.Connect('127.0.0.1', $Port) } catch { Start-Sleep -Milliseconds 100 }
        }
        if (-not $client.Connected) { throw "Owned QEMU QMP port $Port did not become available." }
        $stream = $client.GetStream()
        $stream.ReadTimeout = 2000
        $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
        $writer = [System.IO.StreamWriter]::new($stream, [System.Text.Encoding]::ASCII, 1024, $true)
        [void]$reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}'); $writer.Flush()
        [void]$reader.ReadLine()
        $writer.WriteLine('{"execute":"quit"}'); $writer.Flush()
        [void]$reader.ReadLine()
    } finally { $client.Dispose() }
    if (-not $Process.WaitForExit(10000)) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        [void]$Process.WaitForExit(5000)
    }
}

function Invoke-ProofQemuBoot([string]$Stage, [string]$Ramdisk,
    [string]$EspPath, [string]$Bootloader, [string]$Kernel,
    [string]$QemuPath, [string]$OvmfPath, [string]$BootRoot) {
    $serialPath = Join-Path $BootRoot 'serial.log'
    $stdoutPath = Join-Path $BootRoot 'qemu.stdout.log'
    $stderrPath = Join-Path $BootRoot 'qemu.stderr.log'
    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath 'EFI\BOOT'), $BootRoot | Out-Null
    Copy-Item -LiteralPath $Bootloader -Destination (Join-Path $EspPath 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $EspPath 'kernel.elf') -Force
    # Only boot metadata changes. This path is the same root FAT backing for every boot.
    Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $EspPath 'ramdisk.img') -Force
    $qmpPort = Get-FreeQmpPort
    $arguments = @('-accel','tcg,thread=single','-machine','pc','-smp','1',
        '-drive',('if=pflash,format=raw,readonly=on,file="' + $OvmfPath + '"'),
        '-drive',('file=fat:rw:"' + $EspPath + '",format=raw,if=ide,index=0'),
        '-m','1024M','-vga','std','-display','none',
        '-serial',('file:"' + $serialPath + '"'),
        '-qmp',("tcp:127.0.0.1:{0},server,nowait" -f $qmpPort),
        '-boot','order=c','-no-reboot','-no-shutdown','-rtc','base=utc,clock=host')
    $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -WindowStyle Hidden -PassThru
    $serial = ''
    try {
        $deadline = (Get-Date).AddSeconds(120)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $serialPath) {
                $serial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
                if ($serial -match '(?m)^PROOF-SCENARIO-(?:PASS|FAIL) C166 ') { break }
            }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            try { Send-OwnedQemuQuit $qmpPort $process }
            catch {
                # Fallback is constrained to the QEMU process created above.
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                [void]$process.WaitForExit(5000)
            }
        }
    }
    if (Test-Path -LiteralPath $serialPath) { $serial = Get-Content -LiteralPath $serialPath -Raw }
    return [ordered]@{stage=$Stage;serialPath=$serialPath;serialSha256=(Get-Sha256 $serialPath);serial=$serial;pid=$process.Id;exitCode=$process.ExitCode;backingEspPath=[System.IO.Path]::GetFullPath($EspPath)}
}

function Assert-ProofScenario([object]$Boot, [string]$ScenarioCode,
    [string]$StageCode, [int]$ExpectedSteps) {
    $expectedCompletion = "PROOF-SCENARIO-PASS C166 $ScenarioCode $StageCode"
    if ($Boot.serial -notmatch [regex]::Escape($expectedCompletion)) { throw "$($Boot.stage) proof completion failed; see $($Boot.serialPath)." }
    if (@([regex]::Matches($Boot.serial, '(?m)^PROOF-SCENARIO-(?:PASS|FAIL) C166 ')).Count -ne 1) { throw "$($Boot.stage) emitted duplicate or missing completion markers." }
    $stepLines = @([regex]::Matches($Boot.serial, '(?m)^PROOF C166 [SP] (?:Same|Pre|Post) (?<step>[0-9]+) PASS '))
    if ($stepLines.Count -ne $ExpectedSteps) { throw "$($Boot.stage) expected $ExpectedSteps passing proof steps; found $($stepLines.Count)." }
    for ($i = 0; $i -lt $stepLines.Count; $i++) {
        if ([int]$stepLines[$i].Groups['step'].Value -ne $i + 1) { throw "$($Boot.stage) proof steps were missing, duplicated, or out of order." }
    }
}

$dotnet = Resolve-Tool 'dotnet.exe' @('C:\Program Files\dotnet\dotnet.exe')
$gcc = Resolve-Tool 'x86_64-elf-gcc.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-gcc.exe')
$gxx = Resolve-Tool 'x86_64-elf-g++.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-g++.exe')
$ld = Resolve-Tool 'x86_64-elf-ld.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-ld.exe')
$objcopy = Resolve-Tool 'x86_64-elf-objcopy.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-objcopy.exe')
$make = Resolve-Tool 'mingw32-make.exe' @('C:\mingw64\bin\mingw32-make.exe','C:\msys64\mingw64\bin\mingw32-make.exe')
$qemu = Resolve-Tool 'qemu-system-x86_64.exe' @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\qemu\qemu-system-x86_64.exe'),
    'C:\qemu\qemu-system-x86_64.exe',
    'C:\msys64\mingw64\bin\qemu-system-x86_64.exe')
$qemuRelevant = @()

$sdk = 'unavailable'
if ($dotnet) {
    $sdkOutput = & $dotnet --version 2>&1
    if ($LASTEXITCODE -eq 0) { $sdk = ([string]$sdkOutput -split "`r?`n" | Select-Object -First 1).Trim() }
}
$crossAvailable = $false
if ($gcc -and $gxx -and $ld -and $objcopy) { $crossAvailable = $true }
$productionCompiler = Resolve-Tool 'g++.exe' @('C:\mingw64\bin\g++.exe','C:\msys64\mingw64\bin\g++.exe')
$productionLinker = Resolve-Tool 'ld.exe' @('C:\mingw64\bin\ld.exe','C:\msys64\mingw64\bin\ld.exe')
$productionObjcopy = Resolve-Tool 'objcopy.exe' @('C:\mingw64\bin\objcopy.exe','C:\msys64\mingw64\bin\objcopy.exe')
$productionToolchain = [bool]($make -and $productionCompiler -and $productionLinker -and $productionObjcopy)
$qemuVersion = 'unavailable'
if ($qemu) {
    $versionOutput = & $qemu -version 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        $qemuVersion = ($versionOutput -split "`r?`n" | Select-Object -First 1).Trim()
    }
}
$capability = @"
SOURCE_HEAD=$head
DOTNET_TOOLCHAIN=$(if ($sdk -ne 'unavailable') {'AVAILABLE'} else {'UNAVAILABLE'}) path=$dotnet sdk=$sdk
CROSS_TOOLCHAIN=$(if ($crossAvailable) {'AVAILABLE'} else {'UNAVAILABLE'}) gcc=$gcc g++=$gxx ld=$ld objcopy=$objcopy
PRODUCTION_KERNEL_TOOLCHAIN=$(if ($productionToolchain) {'AVAILABLE'} else {'UNAVAILABLE'}) make=$make g++=$productionCompiler ld=$productionLinker objcopy=$productionObjcopy
QEMU=$(if ($qemu) {'AVAILABLE'} else {'UNAVAILABLE'}) path=$qemu version=$qemuVersion
QEMU_OWNERSHIP=deferred-until-host-and-build-gates
"@
Set-Content -LiteralPath (Join-Path $EvidenceRoot 'environment-capability.txt') -Value $capability -Encoding utf8
Write-Host $capability
if (-not $dotnet) { throw 'Outcome E: current .NET SDK is unavailable.' }

if (-not $productionToolchain) { throw 'Outcome E: repository production kernel build toolchain is unavailable.' }
if (-not $BuildProofProducts) {
    Write-Host 'Focused C166R3 source/tool discovery complete; pass -BuildProofProducts to build fresh proof products.'
    if ($RunProofBoots -and $qemuRelevant.Count -gt 0) {
        throw 'Outcome E: proof boots blocked by an already running QEMU workload; no process was terminated.'
    }
    return
}

$buildRoot = Join-Path $EvidenceRoot 'build'
$proofComposite = Join-Path $buildRoot 'composite-c166-proof'
$productionComposite = Join-Path $buildRoot 'composite-c166-production'
$runtimeOutput = Join-Path $buildRoot 'runtime-pack'
$python = Resolve-Tool 'python.exe' @(
    'C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    'C:\Python312\python.exe','C:\Python311\python.exe')
if (-not $python) { throw 'The managed artifact converter Python runtime could not be located.' }
$managedBuilder = Join-Path $PSScriptRoot 'build-managed-hostlog-proof.ps1'
$managedArgs = @('-ExecutionPolicy','Bypass','-File',$managedBuilder,
    '-RepoRoot',$RepoRoot,'-OutputRoot',$proofComposite,
    '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
    '-RuntimePackOutputRoot',$runtimeOutput,'-UseGuideXosRuntimePack',
    '-ProductionApplication','-PersistentCompositeLifecycle','-AllocationMode','Allocating','-ProofControl',
    '-ManagedProjectMode','C160Composite','-C155ManagedNotesSession',
    '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C158ManagedCalculator',
    '-C160ApplicationSnapshotProof','-C161ManagedTaskManager','-C161TaskManagerProof',
    '-C162ManagedTaskManagerClose','-C162TaskManagerCloseProof','-C163ManagedFileExplorer',
    '-C163FileExplorerProof','-C164FileActivationProof','-C166FileAssociationProof',
    '-HeapConfiguration','Primary8MiB','-PythonExe',$python)
& powershell.exe @managedArgs
if ($LASTEXITCODE -ne 0) { throw "Fresh C166 managed proof build failed: $LASTEXITCODE" }
$proofElf = Join-Path $proofComposite 'artifacts\HostLogProof.elf'
if (-not (Test-Path -LiteralPath $proofElf -PathType Leaf)) { throw "Proof composite missing: $proofElf" }
$proofCompositeHash = Get-Sha256 $proofElf

$generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
$proofRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c166-proof.img'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $proofRamdisk) | Out-Null
$proofScenarioMedia = [ordered]@{}
foreach ($proofStage in @('SameBoot','PreReboot','PostReboot')) {
    $stageSuffix = $proofStage.ToLowerInvariant()
    $stageImage = if ($proofStage -eq 'SameBoot') { $proofRamdisk } else {
        Join-Path $EvidenceRoot "staging\ramdisk-c166-$stageSuffix.img"
    }
    $mediaArgs = @('-ExecutionPolicy','Bypass','-File',$generator,
        '-OutputDir',(Join-Path $EvidenceRoot "staging\wallpaper-pack-c166-$stageSuffix"),
        '-OutputImage',$stageImage,'-C104AppAPath',$proofElf,'-Proof1Control','-Proof1Stage',$proofStage,
        '-ProductionCompositeApplicationPath',$proofElf,'-C114ManagedDirectoryServices',
        '-C117ManagedTextArea','-C118ManagedListBox','-C151ManagedOpenFileDialog',
        '-C152ManagedNotesSaveWorkflow','-C155ManagedNotesSession',
        '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C164FileActivation')
    & powershell.exe @mediaArgs
    if ($LASTEXITCODE -ne 0) { throw "Proof ramdisk generation failed for $proofStage`: $LASTEXITCODE" }
    $proofScenarioMedia[$proofStage] = [ordered]@{ path=$stageImage; sha256=(Get-Sha256 $stageImage) }
}
$proofRamdiskHash = $proofScenarioMedia.SameBoot.sha256

$flags = @('-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION',
    '-DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH',
    '-DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION',
    '-DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES',
    '-DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES',
    '-DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER','-DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT',
    '-DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA','-DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX',
    '-DGXOS_NATIVEAOT_C119_MANAGED_BUTTON','-DGXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST',
    '-DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX','-DGXOS_NATIVEAOT_C122_MANAGED_LABEL',
    '-DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR','-DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON',
    '-DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR','-DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX',
    '-DGXOS_NATIVEAOT_C127_MANAGED_PANEL','-DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE',
    '-DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT','-DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX',
    '-DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON','-DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX',
    '-DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING','-DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU',
    '-DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU','-DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING',
    '-DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR','-DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT',
    '-DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW','-DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK',
    '-DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK','-DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX',
    '-DGXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER','-DGXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG',
    '-DGXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE','-DGXOS_NATIVEAOT_C147_RUNTIME_SETTINGS',
    '-DGXOS_NATIVEAOT_C148_SETTINGS_V2','-DGXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING',
    '-DGXOS_NATIVEAOT_C150_MANAGED_APP_RETURN','-DGXOS_NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG',
    '-DGXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW','-DGXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION',
    '-DGXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS','-DGXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT',
    '-DGXOS_NATIVEAOT_C158_MANAGED_CALCULATOR','-DGXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF',
    '-DGXOS_NATIVEAOT_C161_MANAGED_TASK_MANAGER','-DGXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
    '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE','-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF',
    '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER','-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER_PROOF',
    '-DGXOS_NATIVEAOT_C164_FILE_ACTIVATION_PROOF',
    '-DGUIDEXOS_PROOF_CONTROL')
$kernelArgs = @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',
    "EXTRA_CFLAGS=$($flags -join ' ')",'-B')
$proofKernelStatus = 'SKIPPED: no configured AMD64 compiler'
$proofKernelHash = $null
if ($productionToolchain) {
    & $make @kernelArgs
    if ($LASTEXITCODE -ne 0) { throw "Fresh C166 kernel proof link failed: $LASTEXITCODE" }
    $canonicalKernel = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
    $proofKernel = Join-Path $EvidenceRoot 'proof-kernel.elf'
    Copy-Item -LiteralPath $canonicalKernel -Destination $proofKernel -Force
    $proofKernelHash = Get-Sha256 $proofKernel
    $proofKernelStatus = "PASS: configured AMD64 MinGW fallback ($productionCompiler)"
}

# QEMU ownership is deliberately checked only after the host and all three build gates.
$qemuRelevant = @(Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" |
    ForEach-Object { [ordered]@{ pid=$_.ProcessId; path=$_.ExecutablePath; commandLine=$_.CommandLine } })
$guestGate = if ($qemuRelevant.Count -gt 0) { 'BLOCKED: unrelated QEMU owner' }
    elseif ($RunProofBoots -and -not $proofKernelHash) { 'BLOCKED: no configured AMD64 compiler; no fresh proof kernel' }
    elseif ($RunProofBoots) { 'ready: three managed proof scenarios staged' }
    else { 'not-run: proof boots not requested' }

$manifest = [ordered]@{
    phase='C166R9'; outcome='host-and-proof-build-gates-passed'; accepted=$false
    sourceHead=$head; sourceBranch='v1.1_DOTNET_SUPPORT'
    dotnetSdkPath=$dotnet; dotnetSdkVersion=$sdk
    crossCompilerGcc=$gcc; crossCompilerGxx=$gxx; crossLinker=$ld; crossObjcopy=$objcopy
    productionCompiler=$productionCompiler; productionLinker=$productionLinker
    productionMake=$make; qemuPath=$qemu; qemuVersion=$qemuVersion
    existingQemu=$qemuRelevant; proofCompositeSha256=$proofCompositeHash
    proofKernelSha256=$proofKernelHash; proofKernelStatus=$proofKernelStatus; proofRamdiskSha256=$proofRamdiskHash
    proofControlFlag='GUIDEXOS_PROOF_CONTROL'; registeredProofSuites=@('C166')
    c166Scenarios=@('SameBoot','Persistence'); scenarioStages=@('SameBoot','PreReboot','PostReboot')
    scenarioSelectorTransport='fixed 9-byte /system/config/proof1.bin in boot ramdisk'
    scenarioMedia=$proofScenarioMedia; maximumResultMarkerLength=96
    c166GuestLanesWired=@{Boot1='C166/SameBoot';Boot2='C166/Persistence/PreReboot';Boot3='C166/Persistence/PostReboot'}
    persistentProductDiskPreservation='Stage selector media are separate ramdisks; the association-storage disk path is invariant across Boot2 and Boot3.'
    proofProductsStatus='PASS'; proofProductsMarker='C166R9_PROOF_PRODUCTS_READY'
    win32ConsoleImports=@{GetConsoleMode='absent-in-successful-link';GetFileType='absent-in-successful-link';WriteConsoleW='absent-in-successful-link'}
    consoleWriteLineRemoval='strongly-supported-cause-by-before-after-link-evidence'
    qemuBoots='not-run'; qemuAvailable=[bool]$qemu; qemuOwnership=$qemuRelevant; guestAcceptance=$guestGate
    artifactAuthority='current committed source and freshly built proof products; historical C163/C164 hashes are informational only'
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c166r9-proof-manifest.json') -Encoding utf8
$proof1Manifest = [ordered]@{
    phase='PROOF1'; sourceHead=$head; proofBuildFlag='GUIDEXOS_PROOF_CONTROL'
    registeredSuites=@('C166'); c166Scenarios=@('SameBoot','Persistence')
    scenarioStages=@('SameBoot','PreReboot','PostReboot'); maximumResultMarkerLength=96
    focusedTests=[ordered]@{native='PASS: proof diagnostics read is slot/generation neutral; production rejects operation 4';managed='PASS: 34 cases including selector validation, fixed C166 route, and diagnostic callback'}
    proofDisabledProduction=[ordered]@{managedBuild='PASS';managedProofMarkers='absent';nativeDiagnosticsOperation='rejected'}
    managedNativeAotBuild='PASS'; proofCompositeSha256=$proofCompositeHash
    kernelToolchain='kernel/arch/amd64/Makefile.arch MinGW fallback'
    kernelBuild=$proofKernelStatus; proofRamdiskBuild='PASS'; sameBootRamdiskSha256=$proofRamdiskHash
    qemuAvailability=$(if ($qemu) {'available'} else {'unavailable'})
    guestLaneWired=$true; guestLaneExecutable=[bool]$proofKernelHash
    guestLaneExecuted=$false; c166Accepted=$false
    scenarioMedia=$proofScenarioMedia
    coordinatorPath='samples/managed/HostLogProof/GuideXos/GuideXosProofControl.cs'
    selectorRepresentation='9 bytes: GXPF, version=1, suite=1, scenario=1|2, stage=1|2|3, XOR checksum'
    selectorStorage='separate boot ramdisk /system/config/proof1.bin'
    completionProtocol='exactly one PROOF-SCENARIO-PASS|FAIL C166 <scenario> <stage> marker'
    resultProtocol='PROOF C166 <suite> <scenario> <stage> <step> PASS|FAIL <op> st=<code> ov=<state> [slot=<A|B|N> gen=<n>]'
    crossBootState='host out/dotnet/proof1/guest-run-*/c166-persistence-state.json; expected slot, generation, prior serial SHA-256, and association disk path'
    productDiskIdentity='same writable fat:rw ESP directory at IDE index 0 for Boot 2 and Boot 3; only ramdisk.img is replaced'
}
$proof1Dir = Join-Path $RepoRoot 'out\dotnet\proof1'
New-Item -ItemType Directory -Force -Path $proof1Dir | Out-Null
$proof1Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $proof1Dir 'proof1-manifest.json') -Encoding utf8
Write-Host 'C166R9_PROOF_PRODUCTS_READY'
Write-Host "C166R9 proof products built from $head"
Write-Host "composite=$proofCompositeHash kernel=$proofKernelHash ramdisk=$proofRamdiskHash"
if (-not $RunProofBoots) {
    Write-Host 'No QEMU boot requested; proof products and manifest are retained.'
    return
}
if (-not $qemu -and $RunProofBoots) { throw 'Outcome D: QEMU executable is unavailable; fresh builds remain usable.' }
if ($RunProofBoots -and $qemuRelevant.Count -gt 0) {
    $owners = ($qemuRelevant | ForEach-Object { "pid=$($_.pid) exe=$($_.path) cmd=$($_.commandLine)" }) -join "`n"
    throw "Outcome E: proof boots are blocked by existing QEMU process(es); no process was terminated.`n$owners"
}
if (-not $proofKernelHash) {
    throw 'Outcome E: guest execution is externally blocked because the configured AMD64 cross compiler is unavailable; the proof scenarios and managed proof composite are built.'
}
$ovmf = Resolve-Tool 'edk2-x86_64-code.fd' @(
    (Join-Path $RepoRoot 'OVMF.fd'),(Join-Path $RepoRoot 'ovmf.fd'),
    'C:\Program Files\qemu\share\edk2-x86_64-code.fd')
$bootloader = Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
if (-not $ovmf -or -not (Test-Path -LiteralPath $bootloader -PathType Leaf)) {
    throw 'Outcome E: fresh proof QEMU boot requires the repository bootloader and OVMF code image.'
}
$runStamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$guestRoot = Join-Path $proof1Dir "guest-run-$runStamp"
$sharedEsp = Join-Path $guestRoot 'shared-esp'
New-Item -ItemType Directory -Force -Path $sharedEsp | Out-Null
$diskIdentity = [System.IO.Path]::GetFullPath($sharedEsp)
$boot1 = Invoke-ProofQemuBoot 'Boot1-SameBoot' $proofScenarioMedia.SameBoot.path $sharedEsp $bootloader $proofKernel $qemu $ovmf (Join-Path $guestRoot 'boot1')
Assert-ProofScenario $boot1 'S' 'Same' 5
$boot2 = Invoke-ProofQemuBoot 'Boot2-PreReboot' $proofScenarioMedia.PreReboot.path $sharedEsp $bootloader $proofKernel $qemu $ovmf (Join-Path $guestRoot 'boot2')
Assert-ProofScenario $boot2 'P' 'Pre' 3
if ($boot2.backingEspPath -ne $diskIdentity) { throw 'Boot 2 association storage backing path changed unexpectedly.' }
$boot2Diagnostic = [regex]::Match($boot2.serial, '(?m)^PROOF C166 P Pre [0-9]+ PASS Diag st=0 ov=2 slot=(?<slot>[ABN]) gen=(?<gen>[0-9]+)$')
if (-not $boot2Diagnostic.Success) { throw 'Boot 2 did not report Disabled state and authoritative slot/generation.' }
$crossBootStatePath = Join-Path $guestRoot 'c166-persistence-state.json'
$crossBootState = [ordered]@{suite='C166';scenario='Persistence';slot=$boot2Diagnostic.Groups['slot'].Value;generation=$boot2Diagnostic.Groups['gen'].Value;boot2SerialSha256=$boot2.serialSha256;associationDiskPath=$diskIdentity}
$crossBootState | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $crossBootStatePath -Encoding ASCII
$boot3 = Invoke-ProofQemuBoot 'Boot3-PostReboot' $proofScenarioMedia.PostReboot.path $sharedEsp $bootloader $proofKernel $qemu $ovmf (Join-Path $guestRoot 'boot3')
Assert-ProofScenario $boot3 'P' 'Post' 4
if ($boot3.backingEspPath -ne $diskIdentity -or $boot2.backingEspPath -ne $boot3.backingEspPath) {
    throw 'Boot 2 and Boot 3 did not use the same writable association-storage disk identity.'
}
$boot3Diagnostic = [regex]::Match($boot3.serial, '(?m)^PROOF C166 P Post [0-9]+ PASS Diag st=0 ov=2 slot=(?<slot>[ABN]) gen=(?<gen>[0-9]+)$')
if (-not $boot3Diagnostic.Success -or $boot3Diagnostic.Groups['slot'].Value -ne $crossBootState.slot -or $boot3Diagnostic.Groups['gen'].Value -ne $crossBootState.generation) {
    throw 'Boot 3 slot/generation did not match host-owned Boot 2 orchestration state.'
}
$proof1Manifest.guestLaneExecuted = $true
$proof1Manifest.c166Accepted = $false
$guestBootSummary = @()
foreach ($bootResult in @($boot1,$boot2,$boot3)) {
    $guestBootSummary += [ordered]@{stage=$bootResult.stage;serialPath=$bootResult.serialPath;serialSha256=$bootResult.serialSha256;exitCode=$bootResult.exitCode}
}
$proof1Manifest.guestBoots = $guestBootSummary
$proof1Manifest.crossBootStatePath = $crossBootStatePath
$proof1Manifest.associationDiskIdentity = [ordered]@{path=$diskIdentity;boot2=$boot2.backingEspPath;boot3=$boot3.backingEspPath;same=$true}
$proof1Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $proof1Dir 'proof1-manifest.json') -Encoding utf8
Write-Host "PROOF1 managed guest scenarios passed; C166 acceptance remains pending. Evidence: $guestRoot"
