$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$outputRoot = Join-Path $root 'out\dotnet\native-event'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

function Write-Status {
    param(
        [string]$Name,
        [bool]$Passed
    )

    $result = if ($Passed) { 'PASS' } else { 'FAIL' }
    Write-Host "${Name}: $result"
}

function Invoke-Compiler {
    param(
        [string]$Compiler,
        [string[]]$Arguments
    )

    $output = @(& $Compiler @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $output | ForEach-Object { Write-Host $_ }
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { $_.ToString() })
    }
}

$compilerCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    Write-Status 'Hosted build' $false
    Write-Status 'Bare-metal build' $false
    Write-Status 'NativeAOT adapter compile/link probe' $false
    exit 1
}

$compiler = $compilerCommand.Source
$quoteRoot = @('-iquote', $root)

$hostedExe = Join-Path $outputRoot 'guidexos_event_tests.exe'
$hostedArgs = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_event_tests.cpp'),
    '-pthread', '-o', $hostedExe
)
$hostedBuild = Invoke-Compiler $compiler $hostedArgs
Write-Status 'Hosted build' ($hostedBuild.ExitCode -eq 0)

$testOutput = @()
$hostedRunPassed = $false
if ($hostedBuild.ExitCode -eq 0) {
    $testOutput = @(& $hostedExe 2>&1 | ForEach-Object { $_.ToString() })
    $hostedRunPassed = ($LASTEXITCODE -eq 0)
}

$categories = @(
    'Manual initial-state tests',
    'Manual signal/reset/reuse',
    'Manual multi-waiter release',
    'Auto initial-state tests',
    'Auto one-waiter release',
    'Auto pending-signal behavior',
    'Auto reuse',
    'Zero-timeout polling',
    'Finite timeout',
    'Signal-timeout race',
    'Cleanup/leak check'
)
foreach ($category in $categories) {
    $line = "${category}: PASS"
    Write-Status $category ($hostedRunPassed -and ($testOutput -contains $line))
}

$baremetalSourceObject = Join-Path $outputRoot 'guidexos_event_baremetal.o'
$baremetalProbeObject = Join-Path $outputRoot 'guidexos_event_baremetal_compile_probe.o'
$baremetalFlags = @(
    '-std=c++14', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
    '-nostdlib', '-nostdinc++', '-fno-builtin', '-Wall', '-Wextra', '-Wpedantic',
    '-DGXOS_BARE_METAL'
) + $quoteRoot
$baremetalSourceBuild = Invoke-Compiler $compiler ($baremetalFlags + @(
    '-c', (Join-Path $root 'runtime\synchronization\guidexos_event_baremetal.cpp'),
    '-o', $baremetalSourceObject
))
$baremetalProbeBuild = Invoke-Compiler $compiler ($baremetalFlags + @(
    '-c', (Join-Path $root 'runtime\tests\guidexos_event_baremetal_compile_probe.cpp'),
    '-o', $baremetalProbeObject
))
$baremetalBuildPassed = ($baremetalSourceBuild.ExitCode -eq 0 -and $baremetalProbeBuild.ExitCode -eq 0)
Write-Status 'Bare-metal build' $baremetalBuildPassed
Write-Host 'Bare-metal runtime execution: BLOCKED (generic scheduler wait queue and timer wake hooks are not available)'

$adapterExe = Join-Path $outputRoot 'guidexos_event_adapter_probe.exe'
$adapterArgs = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_event_adapter.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_event_adapter_probe.cpp'),
    '-pthread', '-o', $adapterExe
)
$adapterBuild = Invoke-Compiler $compiler $adapterArgs
$adapterRunPassed = $false
if ($adapterBuild.ExitCode -eq 0) {
    & $adapterExe
    $adapterRunPassed = ($LASTEXITCODE -eq 0)
}
Write-Status 'NativeAOT adapter compile/link probe' ($adapterBuild.ExitCode -eq 0 -and $adapterRunPassed)

$genericFiles = @(
    (Join-Path $root 'runtime\synchronization\guidexos_event.h'),
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\synchronization\guidexos_event_baremetal.h'),
    (Join-Path $root 'runtime\synchronization\guidexos_event_baremetal.cpp')
)
$forbiddenGenericPattern = '(?i)\.NET|NativeAOT|\bGC\b|Workstation|Win32|HostLogProof|\bFLS\b|Finalizer'
$genericCouplingPassed = $true
foreach ($file in $genericFiles) {
    if (Select-String -Path $file -Pattern $forbiddenGenericPattern -Quiet) {
        Write-Host "Generic coupling check: forbidden name in $file"
        $genericCouplingPassed = $false
    }
}
Write-Status 'Generic event coupling check' $genericCouplingPassed

$adapterFiles = @(
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_event_adapter.h'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_event_adapter.cpp')
)
$adapterOperationalPattern = '(?i)GC_Initialize|RhInitialize|PalStartFinalizerThread|GarbageCollect|RhpCollect'
$adapterIsolationPassed = $true
foreach ($file in $adapterFiles) {
    if (Select-String -Path $file -Pattern $adapterOperationalPattern -Quiet) {
        Write-Host "Adapter isolation check: operational runtime entry in $file"
        $adapterIsolationPassed = $false
    }
}
Write-Status 'Inactive adapter isolation check' $adapterIsolationPassed

if (-not ($hostedBuild.ExitCode -eq 0 -and $hostedRunPassed -and $baremetalBuildPassed -and
        $adapterBuild.ExitCode -eq 0 -and $adapterRunPassed -and $genericCouplingPassed -and $adapterIsolationPassed)) {
    exit 1
}
