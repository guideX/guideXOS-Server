[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$IncludeDir = Join-Path $Root 'sdk\include'
$SourceDir = $PSScriptRoot
$OutputDir = Join-Path $Root 'Apps\StellarNavigator\bin\amd64'
$ElfPath = Join-Path $OutputDir 'stellarnavigator.elf'
$ManifestPath = Join-Path $Root 'Apps\StellarNavigator\app.json'
$Clang = 'C:\Program Files\LLVM\bin\clang++.exe'
$Lld = 'C:\Program Files\LLVM\bin\ld.lld.exe'
$ReadElf = 'C:\mingw64\bin\readelf.exe'

foreach ($tool in @($Clang, $Lld, $ReadElf)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required Native ELF tool is missing: $tool" }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$BuildDir = Join-Path $env:TEMP ('stellar-navigator-sn10-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Common = @(
    '--target=x86_64-unknown-elf', '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
    '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-builtin',
    '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-fno-threadsafe-statics',
    '-fno-use-cxa-atexit', '-ffp-contract=off', "-I$IncludeDir", "-I$SourceDir"
)

$CoreObject = Join-Path $BuildDir 'ephemeris_core.o'
$MainObject = Join-Path $BuildDir 'main.o'
& $Clang @Common '-c' (Join-Path $SourceDir 'ephemeris_core.cpp') '-o' $CoreObject
if ($LASTEXITCODE -ne 0) { throw "Native ephemeris core compilation failed with exit code $LASTEXITCODE." }
& $Clang @Common '-c' (Join-Path $SourceDir 'main.cpp') '-o' $MainObject
if ($LASTEXITCODE -ne 0) { throw "Native application compilation failed with exit code $LASTEXITCODE." }
& $Lld '-m' 'elf_x86_64' '-static' '-e' 'gx_main' $MainObject $CoreObject '-o' $ElfPath
if ($LASTEXITCODE -ne 0) { throw "Native application link failed with exit code $LASTEXITCODE." }

$Header = (& $ReadElf '-h' $ElfPath 2>&1) -join "`n"
$ProgramHeaders = (& $ReadElf '-l' $ElfPath 2>&1) -join "`n"
$Relocations = (& $ReadElf '-r' $ElfPath 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $Header -notmatch 'Class:\s+ELF64' -or
    $Header -notmatch 'Type:\s+EXEC' -or $Header -notmatch 'Advanced Micro Devices X86-64' -or
    $Header -notmatch 'Entry point address:\s+0x[1-9a-fA-F][0-9a-fA-F]*' -or
    $ProgramHeaders -match 'INTERP' -or $Relocations -notmatch 'There are no relocations') {
    throw "Built artifact does not meet the Native ELF static ET_EXEC contract.`n$Header`n$ProgramHeaders`n$Relocations"
}

Copy-Item -LiteralPath (Join-Path $SourceDir 'app.json') -Destination $ManifestPath -Force
Write-Host "Native Stellar Navigator ELF: $ElfPath"
Write-Host 'Target: AMD64 static ET_EXEC, entry point gx_main, no interpreter or relocations.'
Write-Host "Manifest: $ManifestPath"
