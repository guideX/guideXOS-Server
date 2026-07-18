$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$outputRoot = Join-Path $root 'out\dotnet\native-mutex'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

function Status([string]$Name, [string]$Value) { Write-Host "${Name}: $Value" }

function Invoke-Compiler([string]$Compiler, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = @(& $Compiler @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($exitCode -ne 0) { $output | ForEach-Object { Write-Host $_ } }
    [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function HasPass([string[]]$Lines, [string]$Label) {
    return $Lines -contains "[mutex-test] ${Label}: PASS"
}

$compilerCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    Status 'Hosted mutex build' 'FAIL'
    Status 'Bare-metal mutex build' 'FAIL'
    Status 'Inactive adapter probe' 'FAIL'
    exit 1
}
$compiler = $compilerCommand.Source
$quoteRoot = @('-iquote', $root)

$hostedExe = Join-Path $outputRoot 'guidexos_mutex_tests.exe'
$hostedBuild = Invoke-Compiler $compiler (@(
    '-std=c++17','-O2','-Wall','-Wextra','-Wpedantic') + $quoteRoot + @(
    (Join-Path $root 'runtime\synchronization\guidexos_mutex.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_mutex_tests.cpp'),
    '-pthread','-o',$hostedExe))
$hostedOutput = @()
$hostedRun = $false
if ($hostedBuild.ExitCode -eq 0) {
    $hostedOutput = @(& $hostedExe 2>&1 | ForEach-Object { $_.ToString() })
    $hostedRun = ($LASTEXITCODE -eq 0)
}
Status 'Hosted mutex build' $(if ($hostedBuild.ExitCode -eq 0) { 'PASS' } else { 'FAIL' })
foreach ($label in @(
    'Uninitialized state','Nonrecursive initialize','Basic acquire',
    'Nonrecursive self-lock rejected','Basic release','Double release rejected',
    'Quiescent destroy','Recursive initialize','Recursion overflow bounded',
    'Recursive releases','Try-lock contended without parking','FIFO waiter order',
    'Destroy with waiters rejected','Non-owner release rejected',
    'Owner-exit violation detected','Orphaned owner is not silently released',
    'Orphaned mutex remains busy')) {
    Status $label $(if ($hostedRun -and (HasPass $hostedOutput $label)) { 'PASS' } else { 'FAIL' })
}
Status 'Hosted mutex ALL_PASS' $(if ($hostedRun -and ($hostedOutput -contains '[mutex-test] ALL_PASS: PASS')) { 'PASS' } else { 'FAIL' })

$bareFlags = @(
    '-std=c++14','-ffreestanding','-fno-exceptions','-fno-rtti','-nostdlib',
    '-nostdinc++','-fno-builtin','-Wall','-Wextra','-Wpedantic',
    '-DGXOS_BARE_METAL','-DARCH_AMD64','-iquote',$root,
    '-Ikernel/core/include','-Ikernel/arch/amd64/include')
$bareSources = @(
    @{ Source='runtime\synchronization\guidexos_scheduler_wait.cpp'; Name='scheduler_wait.o' },
    @{ Source='runtime\synchronization\guidexos_mutex_baremetal.cpp'; Name='mutex_baremetal.o' },
    @{ Source='kernel\core\process.cpp'; Name='process_mutex.o' })
$barePassed = $true
foreach ($item in $bareSources) {
    $object = Join-Path $outputRoot $item.Name
    $compile = Invoke-Compiler $compiler ($bareFlags + @('-c',(Join-Path $root $item.Source),'-o',$object))
    if ($compile.ExitCode -ne 0) { $barePassed = $false }
}
Status 'Bare-metal mutex build' $(if ($barePassed) { 'PASS' } else { 'FAIL' })

$adapterExe = Join-Path $outputRoot 'guidexos_critical_section_adapter_probe.exe'
$adapterBuild = Invoke-Compiler $compiler (@(
    '-std=c++17','-O2','-Wall','-Wextra','-Wpedantic') + $quoteRoot + @(
    (Join-Path $root 'runtime\synchronization\guidexos_mutex.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_critical_section_adapter.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_critical_section_adapter_probe.cpp'),
    '-pthread','-o',$adapterExe))
$adapterRun = $false
if ($adapterBuild.ExitCode -eq 0) {
    & $adapterExe
    $adapterRun = ($LASTEXITCODE -eq 0)
}
Status 'Inactive adapter compile/link probe' $(if ($adapterBuild.ExitCode -eq 0 -and $adapterRun) { 'PASS' } else { 'FAIL' })

$genericFiles = @(
    (Join-Path $root 'runtime\synchronization\guidexos_mutex.h'),
    (Join-Path $root 'runtime\synchronization\guidexos_mutex.cpp'),
    (Join-Path $root 'runtime\synchronization\guidexos_mutex_baremetal.cpp'),
    (Join-Path $root 'runtime\synchronization\guidexos_scheduler_wait.h'),
    (Join-Path $root 'runtime\synchronization\guidexos_scheduler_wait.cpp'))
$genericPattern = '(?i)\.NET|NativeAOT|\bGC\b|Workstation|Win32|HostLogProof|Finalizer|CRITICAL_SECTION'
$genericCoupling = $true
foreach ($file in $genericFiles) {
    if (Select-String -LiteralPath $file -Pattern $genericPattern -Quiet) { $genericCoupling = $false }
}
Status 'Generic mutex coupling check' $(if ($genericCoupling) { 'PASS' } else { 'FAIL' })

$adapterFiles = @(
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_critical_section_adapter.h'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_critical_section_adapter.cpp'))
$adapterPattern = '(?i)GC_Initialize|RhInitialize|PalStartFinalizerThread|GarbageCollect|RhpCollect|Finalizer'
$adapterIsolation = $true
foreach ($file in $adapterFiles) {
    if (Select-String -LiteralPath $file -Pattern $adapterPattern -Quiet) { $adapterIsolation = $false }
}
Status 'Inactive adapter isolation check' $(if ($adapterIsolation) { 'PASS' } else { 'FAIL' })

if (-not ($hostedBuild.ExitCode -eq 0 -and $hostedRun -and $barePassed -and
          $adapterBuild.ExitCode -eq 0 -and $adapterRun -and $genericCoupling -and
          $adapterIsolation)) { exit 1 }
