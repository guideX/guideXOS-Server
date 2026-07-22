$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $root 'out\runtime\native-local-storage'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

function Status([string]$Name, [string]$Value) {
    Write-Host "${Name}: $Value"
}

function Invoke-Compiler([string]$Compiler, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = @(& $Compiler @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($exitCode -ne 0) { $output | ForEach-Object { Write-Host $_ } }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function ContainsPass([string[]]$Lines, [string]$Label) {
    return $Lines -contains "${Label}: PASS"
}

$compilerCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    Status 'Hosted local-storage tests' 'FAIL'
    Status 'Bare-metal local-storage build' 'FAIL'
    Status 'NativeAOT FLS adapter probe' 'FAIL'
    exit 1
}
$compiler = $compilerCommand.Source
$quoteRoot = @('-iquote', $root)

$hostedExe = Join-Path $outputRoot 'guidexos_local_storage_tests.exe'
$hostedBuild = Invoke-Compiler $compiler (@(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\local_storage\guidexos_local_storage.cpp'),
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_local_storage_tests.cpp'),
    '-pthread', '-o', $hostedExe
))
$hostedOutput = @()
$hostedRun = $false
if ($hostedBuild.ExitCode -eq 0) {
    $hostedOutput = @(& $hostedExe 2>&1 | ForEach-Object { $_.ToString() })
    $hostedRun = $LASTEXITCODE -eq 0
}
foreach ($label in @(
    'Manager initialization', 'Dynamic index allocation', 'Index release',
    'Stale-index rejection', 'Multiple-index isolation', 'Initial-thread values',
    'Worker-thread values', 'Per-thread isolation', 'Thread attach', 'Thread detach',
    'Detach callback value', 'Exactly-once detach callback', 'Index exhaustion',
    'Index generation/reuse', 'TCB reuse clearing', 'Detach callback order',
    'Callback iteration semantics', 'Callback repopulation policy',
    'Callback failure reporting', 'Index release callback',
    'Runtime shutdown cleanup')) {
    Status $label $(if ($hostedRun -and (ContainsPass $hostedOutput $label)) { 'PASS' } else { 'FAIL' })
}
Status 'Hosted local-storage tests' $(if ($hostedRun) { 'PASS' } else { 'FAIL' })

$adapterExe = Join-Path $outputRoot 'guidexos_nativeaot_fls_adapter_probe.exe'
$adapterBuild = Invoke-Compiler $compiler (@(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\local_storage\guidexos_local_storage.cpp'),
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_fls_adapter.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_nativeaot_fls_adapter_probe.cpp'),
    '-pthread', '-o', $adapterExe
))
$adapterOutput = @()
$adapterRun = $false
if ($adapterBuild.ExitCode -eq 0) {
    $adapterOutput = @(& $adapterExe 2>&1 | ForEach-Object { $_.ToString() })
    $adapterRun = $LASTEXITCODE -eq 0
}
foreach ($label in @(
    'Initial-thread values', 'Adapter dynamic allocation', 'Worker-thread isolation',
    'Detach callback value', 'Initial-thread detach', 'Index release',
    'Index release callback',
    'Slot-generation reuse', 'Stale-index rejection')) {
    Status "Adapter $label" $(if ($adapterRun -and (ContainsPass $adapterOutput $label)) { 'PASS' } else { 'FAIL' })
}
Status 'NativeAOT FLS adapter probe' $(if ($adapterRun -and
    (ContainsPass $adapterOutput 'NativeAOT adapter probe')) { 'PASS' } else { 'FAIL' })

$baremetalFlags = @(
    '-std=c++14', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
    '-nostdlib', '-nostdinc++', '-fno-builtin', '-Wall', '-Wextra', '-Wpedantic',
    '-DGXOS_BARE_METAL', '-DARCH_AMD64', '-iquote', $root,
    '-Ikernel/core/include', '-Ikernel/arch/amd64/include'
)
$baremetalObjects = @(
    @{ Source = 'runtime\local_storage\guidexos_local_storage.cpp'; Name = 'local_storage.o' },
    @{ Source = 'kernel\core\process.cpp'; Name = 'process.o' }
)
$baremetalCompilePassed = $true
foreach ($item in $baremetalObjects) {
    $compile = Invoke-Compiler $compiler ($baremetalFlags + @(
        '-c', (Join-Path $root $item.Source), '-o', (Join-Path $outputRoot $item.Name)))
    if ($compile.ExitCode -ne 0) { $baremetalCompilePassed = $false }
}
$makeCommand = Get-Command mingw32-make -ErrorAction SilentlyContinue
$kernelBuildPassed = $false
if ($null -ne $makeCommand) {
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $makeCommand.Source '-C' (Join-Path $root 'kernel') 'ARCH=amd64' 'clean' 2>&1 |
        Tee-Object -FilePath (Join-Path $outputRoot 'bare-metal-build.log')
    $cleanCode = $LASTEXITCODE
    & $makeCommand.Source '-C' (Join-Path $root 'kernel') 'ARCH=amd64' '-j2' 2>&1 |
        Tee-Object -FilePath (Join-Path $outputRoot 'bare-metal-build.log')
    $kernelBuildPassed = $cleanCode -eq 0 -and $LASTEXITCODE -eq 0
    $ErrorActionPreference = $previousErrorAction
}
Status 'Bare-metal local-storage build' $(if ($baremetalCompilePassed -and $kernelBuildPassed) { 'PASS' } else { 'FAIL' })

$genericCoupling = $true
foreach ($file in @(
    (Join-Path $root 'runtime\local_storage\guidexos_local_storage.h'),
    (Join-Path $root 'runtime\local_storage\guidexos_local_storage.cpp'))) {
    if (Select-String -LiteralPath $file -Pattern '(?i)\.NET|NativeAOT|Workstation|Win32|FlsAlloc|FlsFree|HostLogProof' -Quiet) {
        $genericCoupling = $false
    }
}
Status 'Generic local-storage coupling check' $(if ($genericCoupling) { 'PASS' } else { 'FAIL' })

if (-not ($hostedRun -and $adapterRun -and $baremetalCompilePassed -and
          $kernelBuildPassed -and $genericCoupling)) { exit 1 }
exit 0
