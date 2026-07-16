$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $root 'out\dotnet\native-thread-smoke'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

function Status([string]$Name, [string]$Value) {
    Write-Host "${Name}: $Value"
}

function Invoke-Compiler([string]$Compiler, [string[]]$Arguments) {
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = @(& $Compiler @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorAction
    if ($exitCode -ne 0) { $output | ForEach-Object { Write-Host $_ } }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function ContainsPass([string[]]$Lines, [string]$Label) {
    return $Lines -contains "${Label}: PASS"
}

$compilerCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    Status 'Hosted build' 'FAIL'
    Status 'Bare-metal build' 'FAIL'
    Status 'NativeAOT adapter-probe' 'FAIL'
    exit 1
}
$compiler = $compilerCommand.Source
$quoteRoot = @('-iquote', $root)

$hostedExe = Join-Path $outputRoot 'guidexos_native_thread_tests.exe'
$hostedBuild = Invoke-Compiler $compiler (@('-std=c++17','-O2','-Wall','-Wextra','-Wpedantic') + $quoteRoot + @(
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_native_thread_tests.cpp'),
    '-pthread', '-o', $hostedExe
))
$hostedOutput = @()
$hostedRun = $false
if ($hostedBuild.ExitCode -eq 0) {
    $hostedOutput = @(& $hostedExe 2>&1 | ForEach-Object { $_.ToString() })
    $hostedRun = ($LASTEXITCODE -eq 0)
}
Status 'Hosted build' $(if ($hostedBuild.ExitCode -eq 0) { 'PASS' } else { 'FAIL' })

$requiredHosted = @(
    'Single thread creation', 'Context delivery', 'Join before exit',
    'Join after exit', 'Zero-timeout join', 'Finite-timeout join',
    'Join after timeout', 'Request/done Event coordination',
    'Multiple bounded threads', 'TCB reuse', 'Stale-handle rejection',
    'Self-join rejection', 'Double-join rejection', 'Detach semantics',
    'Cleanup/leak checks'
)
foreach ($label in $requiredHosted) {
    Status $label $(if ($hostedRun -and (ContainsPass $hostedOutput $label)) { 'PASS' } else { 'FAIL' })
}
$resultLine = @($hostedOutput | Where-Object { $_ -like 'Exit result: value=*' } | Select-Object -First 1)
if ($resultLine.Count -eq 0) { Write-Host 'Exit result: unavailable' } else { Write-Host $resultLine[0] }

$baremetalFlags = @(
    '-std=c++14','-ffreestanding','-fno-exceptions','-fno-rtti',
    '-nostdlib','-nostdinc++','-fno-builtin','-Wall','-Wextra','-Wpedantic',
    '-DGXOS_BARE_METAL','-DARCH_AMD64','-iquote',$root,
    '-Ikernel/core/include','-Ikernel/arch/amd64/include'
)
$baremetalObjects = @(
    @{ Source = 'runtime\synchronization\guidexos_scheduler_wait.cpp'; Name = 'scheduler_wait.o' },
    @{ Source = 'runtime\synchronization\guidexos_event_baremetal.cpp'; Name = 'event_baremetal.o' },
    @{ Source = 'runtime\thread\guidexos_native_thread_baremetal.cpp'; Name = 'native_thread_baremetal.o' },
    @{ Source = 'kernel\core\process.cpp'; Name = 'process.o' }
)
$baremetalCompilePassed = $true
foreach ($item in $baremetalObjects) {
    $object = Join-Path $outputRoot $item.Name
    $compile = Invoke-Compiler $compiler ($baremetalFlags + @('-c',(Join-Path $root $item.Source),'-o',$object))
    if ($compile.ExitCode -ne 0) { $baremetalCompilePassed = $false }
}
$makeCommand = Get-Command mingw32-make -ErrorAction SilentlyContinue
$baremetalKernelPassed = $false
if ($null -ne $makeCommand) {
    $makeOutput = @(& $makeCommand.Source '-C' (Join-Path $root 'kernel') 'ARCH=amd64' '-j2' 2>&1 | ForEach-Object { $_.ToString() })
    $baremetalKernelPassed = ($LASTEXITCODE -eq 0)
    if (-not $baremetalKernelPassed) { $makeOutput | ForEach-Object { Write-Host $_ } }
}
Status 'Bare-metal build' $(if ($baremetalCompilePassed -and $baremetalKernelPassed) { 'PASS' } else { 'FAIL' })

$qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if ($null -eq $qemu) {
    Status 'Bare-metal runtime' 'BLOCKED (qemu-system-x86_64 unavailable)'
    Status 'Process teardown policy' 'BLOCKED'
} else {
    # The repository has no deterministic concurrent-thread QEMU harness yet.
    Status 'Bare-metal runtime' 'BLOCKED (no concurrent thread harness)'
    Status 'Process teardown policy' 'BLOCKED'
}

$adapterExe = Join-Path $outputRoot 'guidexos_native_thread_adapter_probe.exe'
$adapterBuild = Invoke-Compiler $compiler (@('-std=c++17','-O2','-Wall','-Wextra','-Wpedantic') + $quoteRoot + @(
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_thread_adapter.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_native_thread_adapter_probe.cpp'),
    '-pthread', '-o', $adapterExe
))
$adapterRun = $false
if ($adapterBuild.ExitCode -eq 0) {
    & $adapterExe
    $adapterRun = ($LASTEXITCODE -eq 0)
}
Status 'NativeAOT adapter-probe' $(if ($adapterBuild.ExitCode -eq 0 -and $adapterRun) { 'PASS' } else { 'FAIL' })

$eventSmoke = Join-Path $root 'scripts\dotnet\smoke-native-event-primitive.ps1'
$eventOutput = @(& powershell -ExecutionPolicy Bypass -File $eventSmoke 2>&1 | ForEach-Object { $_.ToString() })
$eventPassed = ($LASTEXITCODE -eq 0)
Status 'Scheduler/Event regressions' $(if ($eventPassed) { 'PASS' } else { 'FAIL' })

$genericFiles = @(
    (Join-Path $root 'runtime\thread\guidexos_native_thread.h'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread.cpp'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread_baremetal.cpp')
)
$forbidden = '\.NET|NativeAOT|\bGC\b|Finalizer|Win32|\bCreateThread\b|\bWaitForSingleObject\b'
$couplingPassed = $true
foreach ($file in $genericFiles) {
    if (Select-String -LiteralPath $file -Pattern $forbidden -CaseSensitive -Quiet) { $couplingPassed = $false }
}
Status 'Generic thread coupling check' $(if ($couplingPassed) { 'PASS' } else { 'FAIL' })

$adapterFiles = @(
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_thread_adapter.h'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_thread_adapter.cpp')
)
$adapterForbidden = '(?i)GC_Initialize|RhInitialize|PalStartFinalizerThread|GarbageCollect|RhpCollect|ThreadStore'
$adapterIsolationPassed = $true
foreach ($file in $adapterFiles) {
    if (Select-String -LiteralPath $file -Pattern $adapterForbidden -Quiet) { $adapterIsolationPassed = $false }
}
Status 'Inactive adapter isolation check' $(if ($adapterIsolationPassed) { 'PASS' } else { 'FAIL' })

if (-not ($hostedBuild.ExitCode -eq 0 -and $hostedRun -and
          $baremetalCompilePassed -and $baremetalKernelPassed -and
          $adapterBuild.ExitCode -eq 0 -and $adapterRun -and $eventPassed -and
          $couplingPassed -and $adapterIsolationPassed)) {
    exit 1
}
