param(
    [string]$ArtifactDir = 'E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS114\run-20261108',
    [switch]$SkipHostedExecution
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$artifactDir = [System.IO.Path]::GetFullPath($ArtifactDir)
$null = New-Item -ItemType Directory -Force -Path $artifactDir
foreach ($name in @('target', 'hosted', 'logs', 'metadata')) {
    $null = New-Item -ItemType Directory -Force -Path (Join-Path $artifactDir $name)
}

$cxx = (Get-Command g++ -ErrorAction Stop).Source
$cc = (Get-Command gcc -ErrorAction Stop).Source
$ld = (Get-Command ld -ErrorAction Stop).Source
$objcopy = (Get-Command objcopy -ErrorAction Stop).Source
$objdump = (Get-Command objdump -ErrorAction Stop).Source
$nm = (Get-Command nm -ErrorAction Stop).Source
$readelf = (Get-Command readelf -ErrorAction Stop).Source
$libgcc = (& $cxx -print-libgcc-file-name | Out-String).Trim()

$version = (& $cxx -dumpversion | Out-String).Trim()
$target = (& $cxx -dumpmachine | Out-String).Trim()
$compilerBin = Split-Path -Parent $cxx
$toolchainRoot = Split-Path -Parent $compilerBin
$standardRoot = Join-Path $toolchainRoot "include\c++\$version"
$targetStandardRoot = Join-Path $standardRoot $target
if (-not (Test-Path (Join-Path $standardRoot 'new')) -or
    -not (Test-Path (Join-Path $targetStandardRoot 'bits\c++config.h'))) {
    throw "Could not locate the target-matched libstdc++ headers under $toolchainRoot."
}

$targetDir = Join-Path $artifactDir 'target'
$hostedDir = Join-Path $artifactDir 'hosted'
$logDir = Join-Path $artifactDir 'logs'
$metadataDir = Join-Path $artifactDir 'metadata'
$probeCpp = Join-Path $repoRoot 'tests\js114_cpp_probe.cpp'
$probeC = Join-Path $repoRoot 'tests\js114_cpp_probe_c_abi.c'
$hostCpp = Join-Path $repoRoot 'tests\js114_cpp_probe_host.cpp'
$headerProbeCpp = Join-Path $repoRoot 'tests\js114_std_header_probe.cpp'
$charconvProbeCpp = Join-Path $repoRoot 'tests\js114_charconv_probe.cpp'
$cmathProbeCpp = Join-Path $repoRoot 'tests\js114_cmath_probe.cpp'
$stringProbe = Join-Path $repoRoot 'tests\js114_std_string_probe.cpp'
$vectorProbe = Join-Path $repoRoot 'tests\js114_std_vector_probe.cpp'
$linkerScript = Join-Path $repoRoot 'kernel\arch\amd64\linker.ld'

$commonTargetFlags = @(
    '-ffreestanding', '-O2', '-Wall', '-Wextra', '-fno-exceptions', '-fno-rtti',
    '-nostdlib', '-nostdinc++', '-fno-builtin', '-DGXOS_BARE_METAL',
    '-isystem', $standardRoot, '-isystem', $targetStandardRoot,
    '-I', (Join-Path $repoRoot 'kernel'), '-I', $repoRoot,
    '-I', (Join-Path $repoRoot 'kernel\core\include'),
    '-I', (Join-Path $repoRoot 'kernel\arch\amd64\include'),
    '-I', (Join-Path $repoRoot 'tests'),
    '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-mmx', '-mno-sse', '-mno-sse2',
    '-fno-pic', '-fno-pie', '-fno-PIC', '-fno-PIE', '-mno-stack-arg-probe',
    '-fno-stack-check', '-fno-stack-protector', '-D__x86_64__'
)
$commands = [System.Collections.Generic.List[object]]::new()

function Invoke-LoggedCommand {
    param(
        [string]$Name,
        [string]$Executable,
        [string[]]$Arguments,
        [int]$ExpectedExit = 0
    )
    $safeName = $Name -replace '[^A-Za-z0-9_.-]', '_'
    $logPath = Join-Path $logDir "$safeName.log"
    $rendered = @($Executable) + $Arguments
    $output = & $Executable @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    @(
        ('COMMAND: ' + ($rendered -join ' ')),
        ('EXIT: ' + $exitCode),
        ($output | ForEach-Object { [string]$_ })
    ) | Out-File -FilePath $logPath -Encoding utf8
    $commands.Add([pscustomobject]@{
        name = $Name
        command = ($rendered -join ' ')
        exitCode = $exitCode
        expectedExit = $ExpectedExit
        status = if ($exitCode -eq $ExpectedExit) { 'PASS' } else { 'FAIL' }
        log = $logPath
    })
    if ($exitCode -ne $ExpectedExit) {
        throw "Command '$Name' returned $exitCode; expected $ExpectedExit. See $logPath"
    }
    return ,$output
}

function Invoke-ExpectedBlockedCompile {
    param([string]$Name, [string]$Source)
    $outputObject = Join-Path $targetDir "$Name.o"
    $arguments = @('-std=c++17') + $commonTargetFlags + @('-c', $Source, '-o', $outputObject)
    $rendered = @($cxx) + $arguments
    $output = & $cxx @arguments 2>&1
    $exitCode = $LASTEXITCODE
    $logPath = Join-Path $logDir "$Name-expected-block.log"
    @(('COMMAND: ' + ($rendered -join ' ')), ('EXIT: ' + $exitCode),
      ($output | ForEach-Object { [string]$_ })) | Out-File -FilePath $logPath -Encoding utf8
    $commands.Add([pscustomobject]@{
        name = $Name
        command = ($rendered -join ' ')
        exitCode = $exitCode
        expected = 'compile rejected in current freestanding profile'
        status = if ($exitCode -ne 0) { 'EXPECTED_BLOCK' } else { 'UNEXPECTED_PASS' }
        log = $logPath
    })
    if ($exitCode -eq 0) { throw "$Name unexpectedly compiled in the current freestanding profile." }
}

$null = Invoke-LoggedCommand -Name 'compiler-version' -Executable $cxx -Arguments @('--version')
$null = Invoke-LoggedCommand -Name 'compiler-target' -Executable $cxx -Arguments @('-dumpmachine')
$null = Invoke-LoggedCommand -Name 'linker-version' -Executable $ld -Arguments @('--version')
$null = Invoke-LoggedCommand -Name 'objcopy-version' -Executable $objcopy -Arguments @('--version')
$null = Invoke-LoggedCommand -Name 'objdump-version' -Executable $objdump -Arguments @('--version')
$null = Invoke-LoggedCommand -Name 'readelf-version' -Executable $readelf -Arguments @('--version')

$cppObject = Join-Path $targetDir 'js114_cpp_probe.o'
$cObject = Join-Path $targetDir 'js114_cpp_probe_c_abi.o'
$headerProbeObject = Join-Path $targetDir 'js114_std_header_probe.o'
$probeArgs = @('-std=c++17') + $commonTargetFlags + @(
    '-fstack-usage', '-MMD', '-MP', '-c', $probeCpp, '-o', $cppObject
)
$null = Invoke-LoggedCommand -Name 'target-cpp17-compile' -Executable $cxx -Arguments $probeArgs
$headerProbeArgs = @('-std=c++17') + $commonTargetFlags + @(
    '-fstack-usage', '-MMD', '-MP', '-c', $headerProbeCpp, '-o', $headerProbeObject
)
$null = Invoke-LoggedCommand -Name 'target-standard-header-compile' -Executable $cxx -Arguments $headerProbeArgs
$cArgs = @('-std=c11') + $commonTargetFlags + @(
    '-fstack-usage', '-MMD', '-MP', '-c', $probeC, '-o', $cObject
)
$null = Invoke-LoggedCommand -Name 'target-c-abi-compile' -Executable $cc -Arguments $cArgs

$stringObject = Join-Path $targetDir 'js114_std_string_probe.o'
$stringArgs = @('-std=c++17') + $commonTargetFlags + @('-c', $stringProbe, '-o', $stringObject)
$stringCommand = @($cxx) + $stringArgs
$stringOutput = & $cxx @stringArgs 2>&1
$stringExit = $LASTEXITCODE
$stringLog = Join-Path $logDir 'target-std-string-expected-block.log'
@(('COMMAND: ' + ($stringCommand -join ' ')), ('EXIT: ' + $stringExit),
  ($stringOutput | ForEach-Object { [string]$_ })) | Out-File -FilePath $stringLog -Encoding utf8
$commands.Add([pscustomobject]@{
    name = 'target-std-string-probe'
    command = ($stringCommand -join ' ')
    exitCode = $stringExit
    expected = 'compile rejected in freestanding mode'
    status = if ($stringExit -ne 0) { 'EXPECTED_BLOCK' } else { 'UNEXPECTED_PASS' }
    log = $stringLog
})
if ($stringExit -eq 0) { throw 'std::string unexpectedly compiled in the freestanding profile.' }

$vectorObject = Join-Path $targetDir 'js114_std_vector_probe.o'
$vectorArgs = @('-std=c++17') + $commonTargetFlags + @('-c', $vectorProbe, '-o', $vectorObject)
$vectorCommand = @($cxx) + $vectorArgs
$vectorOutput = & $cxx @vectorArgs 2>&1
$vectorExit = $LASTEXITCODE
$vectorLog = Join-Path $logDir 'target-std-vector-expected-block.log'
@(('COMMAND: ' + ($vectorCommand -join ' ')), ('EXIT: ' + $vectorExit),
  ($vectorOutput | ForEach-Object { [string]$_ })) | Out-File -FilePath $vectorLog -Encoding utf8
$commands.Add([pscustomobject]@{
    name = 'target-std-vector-probe'
    command = ($vectorCommand -join ' ')
    exitCode = $vectorExit
    expected = 'compile rejected in freestanding mode'
    status = if ($vectorExit -ne 0) { 'EXPECTED_BLOCK' } else { 'UNEXPECTED_PASS' }
    log = $vectorLog
})
if ($vectorExit -eq 0) { throw 'std::vector unexpectedly compiled in the freestanding profile.' }
Invoke-ExpectedBlockedCompile -Name 'target-std-charconv-probe' -Source $charconvProbeCpp
Invoke-ExpectedBlockedCompile -Name 'target-std-cmath-probe' -Source $cmathProbeCpp

$undefinedCpp = Join-Path $metadataDir 'target-cpp-undefined-symbols.txt'
$undefinedHeaderProbe = Join-Path $metadataDir 'target-standard-header-undefined-symbols.txt'
$undefinedC = Join-Path $metadataDir 'target-c-undefined-symbols.txt'
$cppSymbols = Invoke-LoggedCommand -Name 'target-cpp-undefined-symbols' -Executable $nm -Arguments @('-u', $cppObject)
$headerProbeSymbols = Invoke-LoggedCommand -Name 'target-standard-header-undefined-symbols' -Executable $nm -Arguments @('-u', $headerProbeObject)
$cSymbols = Invoke-LoggedCommand -Name 'target-c-undefined-symbols' -Executable $nm -Arguments @('-u', $cObject)
$cppSymbols | ForEach-Object { [string]$_ } | Out-File -FilePath $undefinedCpp -Encoding utf8
$headerProbeSymbols | ForEach-Object { [string]$_ } | Out-File -FilePath $undefinedHeaderProbe -Encoding utf8
$cSymbols | ForEach-Object { [string]$_ } | Out-File -FilePath $undefinedC -Encoding utf8

$objectFormat = Invoke-LoggedCommand -Name 'target-object-format' -Executable $objdump -Arguments @('-f', $cppObject)
$objectFormat | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'target-object-format.txt') -Encoding utf8
$objectSections = Invoke-LoggedCommand -Name 'target-object-sections' -Executable $objdump -Arguments @('-h', $cppObject)
$objectSections | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'target-object-sections.txt') -Encoding utf8
$objectSymbols = Invoke-LoggedCommand -Name 'target-object-symbols' -Executable $nm -Arguments @('-C', $cppObject)
$objectSymbols | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'target-object-symbols.txt') -Encoding utf8

$peImage = Join-Path $targetDir 'js114_cpp_probe.pe'
$mapFile = Join-Path $targetDir 'js114_cpp_probe.map'
$elfImage = Join-Path $targetDir 'js114_cpp_probe.elf'
$linkArgs = @(
    '-T', $linkerScript, '-no-pie', '--image-base=0x100000',
    '--entry=guidex_cpp_probe_run', '--no-insert-timestamp',
    '--no-undefined', '--fatal-warnings', "--Map=$mapFile", '-o', $peImage,
    $cppObject, $cObject, $libgcc
)
$null = Invoke-LoggedCommand -Name 'target-static-link' -Executable $ld -Arguments $linkArgs
$null = Invoke-LoggedCommand -Name 'target-pe-to-elf-format-conversion' -Executable $objcopy -Arguments @(
    '-I', 'pei-x86-64', '-O', 'elf64-x86-64', $peImage, $elfImage
)
$linkedFormat = Invoke-LoggedCommand -Name 'linked-pe-format' -Executable $objdump -Arguments @('-f', $peImage)
$linkedFormat | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'linked-pe-format.txt') -Encoding utf8
$linkedImports = Invoke-LoggedCommand -Name 'linked-pe-import-audit' -Executable $objdump -Arguments @('-p', $peImage)
$linkedImports | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'linked-pe-import-audit.txt') -Encoding utf8
$linkedSymbols = Invoke-LoggedCommand -Name 'linked-elf-symbols' -Executable $nm -Arguments @('-C', $elfImage)
$linkedSymbols | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'linked-elf-symbols.txt') -Encoding utf8
$elfHeader = Invoke-LoggedCommand -Name 'linked-elf-header' -Executable $readelf -Arguments @('-h', $elfImage)
$elfHeader | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'linked-elf-header.txt') -Encoding utf8
$elfProgramHeaders = Invoke-LoggedCommand -Name 'linked-elf-program-headers' -Executable $readelf -Arguments @('-l', $elfImage)
$elfProgramHeaders | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'linked-elf-program-headers.txt') -Encoding utf8
$elfSections = Invoke-LoggedCommand -Name 'linked-elf-sections' -Executable $readelf -Arguments @('-S', $elfImage)
$elfSections | ForEach-Object { [string]$_ } | Out-File -FilePath (Join-Path $metadataDir 'linked-elf-sections.txt') -Encoding utf8

$hostedExe = Join-Path $hostedDir 'js114_cpp_probe_host.exe'
$hostedArgs = @('-std=c++17', '-O2', '-Wall', '-Wextra',
    (Join-Path $repoRoot 'tests\js114_cpp_probe_host.cpp'), $probeCpp, $probeC,
    '-o', $hostedExe)
$null = Invoke-LoggedCommand -Name 'hosted-probe-build' -Executable $cxx -Arguments $hostedArgs
if (-not $SkipHostedExecution) {
    $null = Invoke-LoggedCommand -Name 'hosted-probe-execution' -Executable $hostedExe -Arguments @()
}

$artifactFiles = @(
    $cppObject, $cObject, $headerProbeObject, $peImage, $mapFile, $elfImage, $hostedExe,
    (Join-Path $targetDir 'js114_cpp_probe.su'),
    (Join-Path $targetDir 'js114_cpp_probe_c_abi.su'),
    (Join-Path $targetDir 'js114_std_header_probe.su')
)
$hashes = foreach ($path in $artifactFiles) {
    if (Test-Path $path) {
        $item = Get-Item -LiteralPath $path
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $path
        [pscustomobject]@{ path = $item.FullName; bytes = $item.Length; sha256 = $hash.Hash }
    }
}

$meta = [ordered]@{
    repository = $repoRoot
    compiler = $cxx
    compilerTarget = $target
    compilerVersion = $version
    linker = $ld
    objcopy = $objcopy
    linkerScript = $linkerScript
    cxxFlags = (@('-std=c++17') + $commonTargetFlags)
    cFlags = (@('-std=c11') + $commonTargetFlags)
    standardHeaderDirectories = @($standardRoot, $targetStandardRoot)
    noExceptionOrRtti = $true
    noExternalRuntimeLibraries = $true
    commands = $commands
    artifacts = @($hashes)
}
$meta | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $metadataDir 'js114-probe-results.json') -Encoding utf8

Write-Output "JS114 probe artifacts: $artifactDir"
Write-Output "Target C++17/C ABI compile, standard-header compile, static link, ELF format conversion: PASS"
Write-Output "Freestanding std::string/std::vector/charconv/cmath probes: expected rejection"
if (-not $SkipHostedExecution) { Write-Output 'Hosted allocation/lifetime/error checks: PASS' }
