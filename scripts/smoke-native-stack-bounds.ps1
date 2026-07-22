$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $root 'out\runtime\native-stack-bounds'
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

function HasPass([string[]]$Lines, [string]$Name) {
    return $Lines -contains "${Name}: PASS"
}

$compilerCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    Status 'Hosted stack-bound tests' 'FAIL'
    Status 'Bare-metal build' 'FAIL'
    Status 'Inactive ThreadStore adapter probe' 'FAIL'
    exit 1
}
$compiler = $compilerCommand.Source
$quoteRoot = @('-iquote', $root)

$hostedExe = Join-Path $outputRoot 'guidexos_native_stack_bounds_tests.exe'
$hostedBuild = Invoke-Compiler $compiler (@(
    '-std=c++17','-O2','-Wall','-Wextra','-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\thread\guidexos_native_stack_bounds.cpp'),
    (Join-Path $root 'runtime\local_storage\guidexos_local_storage.cpp'),
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_native_stack_bounds_tests.cpp'),
    '-pthread','-o',$hostedExe
))
$hostedOutput = @()
$hostedRun = $false
if ($hostedBuild.ExitCode -eq 0) {
    $hostedOutput = @(& $hostedExe 2>&1 | ForEach-Object { $_.ToString() })
    $hostedRun = $LASTEXITCODE -eq 0
}
foreach ($label in @(
    'Initial-thread query','Initial RSP inside bounds','Low/high ordering',
    'Initial stack page alignment','Invalid output pointer',
    'Corrupt bounds rejected','Current pointer outside bounds rejected',
    'Worker-thread query','Worker RSP inside bounds',
    'Minimum expected worker size','Bounds valid during detach callback',
    'Distinct worker stacks','Repeated TCB/host-slot reuse bounds',
    'Query after worker state unavailable','Local-storage detach and teardown')) {
    Status $label $(if ($hostedRun -and (HasPass $hostedOutput $label)) { 'PASS' } else { 'FAIL' })
}
Status 'Hosted stack-bound tests' $(if ($hostedRun) { 'PASS' } else { 'FAIL' })

$baremetalFlags = @(
    '-std=c++14','-ffreestanding','-fno-exceptions','-fno-rtti',
    '-nostdlib','-nostdinc++','-fno-builtin','-Wall','-Wextra','-Wpedantic',
    '-DGXOS_BARE_METAL','-DARCH_AMD64','-iquote',$root,
    '-Ikernel/core/include','-Ikernel/arch/amd64/include'
)
$stackObject = Join-Path $outputRoot 'guidexos_native_stack_bounds_baremetal.o'
$processObject = Join-Path $outputRoot 'guidexos_process_stack_bounds_baremetal.o'
$stackBuild = Invoke-Compiler $compiler ($baremetalFlags + @(
    '-c',(Join-Path $root 'runtime\thread\guidexos_native_stack_bounds.cpp'),
    '-o',$stackObject
))
$processBuild = Invoke-Compiler $compiler ($baremetalFlags + @(
    '-c',(Join-Path $root 'kernel\core\process.cpp'),'-o',$processObject
))
$make = Get-Command mingw32-make -ErrorAction SilentlyContinue
$kernelBuild = $false
if ($null -ne $make) {
    Push-Location $root
    try {
        & $make.Source -C kernel ARCH=amd64 2>&1 | Out-Null
        $kernelBuild = $LASTEXITCODE -eq 0
    }
    finally { Pop-Location }
}
Status 'Bare-metal stack-bound compile' $(if ($stackBuild.ExitCode -eq 0 -and $processBuild.ExitCode -eq 0) { 'PASS' } else { 'FAIL' })
Status 'Bare-metal build' $(if ($stackBuild.ExitCode -eq 0 -and $processBuild.ExitCode -eq 0 -and $kernelBuild) { 'PASS' } else { 'FAIL' })

$adapterExe = Join-Path $outputRoot 'guidexos_nativeaot_threadstore_adapter_probe.exe'
$adapterBuild = Invoke-Compiler $compiler (@(
    '-std=c++17','-O2','-Wall','-Wextra','-Wpedantic'
) + $quoteRoot + @(
    (Join-Path $root 'runtime\thread\guidexos_native_stack_bounds.cpp'),
    (Join-Path $root 'runtime\local_storage\guidexos_local_storage.cpp'),
    (Join-Path $root 'runtime\synchronization\guidexos_event.cpp'),
    (Join-Path $root 'runtime\thread\guidexos_native_thread.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_fls_adapter.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_stack_bounds_adapter.cpp'),
    (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_threadstore_adapter.cpp'),
    (Join-Path $root 'runtime\tests\guidexos_nativeaot_threadstore_adapter_probe.cpp'),
    '-pthread','-o',$adapterExe
))
$adapterOutput = @()
$adapterRun = $false
if ($adapterBuild.ExitCode -eq 0) {
    $adapterOutput = @(& $adapterExe 2>&1 | ForEach-Object { $_.ToString() })
    $adapterRun = $LASTEXITCODE -eq 0
}
foreach ($label in @(
    'ThreadStore global initialization','Initial-thread exact stack bounds',
    'Initial-thread attachment','Worker exact stack bounds',
    'Worker-thread attachment','Worker lookup isolation',
    'Current-thread lookup','Transition-frame readiness',
    'ThreadStore detach','FLS callback detach','Bounds valid through detach',
    'Runtime-record generation reuse','TCB/runtime bounds clearing',
    'ThreadStore shutdown','FLS/ThreadStore detach ordering')) {
    Status $label $(if ($adapterRun -and (HasPass $adapterOutput $label)) { 'PASS' } else { 'FAIL' })
}
Status 'Inactive ThreadStore adapter probe' $(if ($adapterRun -and
    (HasPass $adapterOutput 'Inactive ThreadStore adapter probe')) { 'PASS' } else { 'FAIL' })

if (-not ($hostedRun -and $stackBuild.ExitCode -eq 0 -and
          $processBuild.ExitCode -eq 0 -and $kernelBuild -and $adapterRun)) {
    exit 1
}
exit 0
