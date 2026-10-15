[CmdletBinding()]
param(
    [string]$OutputDirectory = "out\validation\settings-s10-freestanding-20261011"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $Compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $Compiler) { throw "The AMD64 kernel C++ compiler was not found." }

$Output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $Output) {
    throw "Freestanding validation directory already exists; preserving it: $Output"
}
New-Item -ItemType Directory -Path $Output -Force | Out-Null

$CompilerDirectory = Split-Path -Parent $Compiler.Source
$Linker = Join-Path $CompilerDirectory "ld.exe"
$Nm = Join-Path $CompilerDirectory "nm.exe"
if (-not (Test-Path -LiteralPath $Linker)) { throw "GNU ld was not found beside $($Compiler.Source)." }
if (-not (Test-Path -LiteralPath $Nm)) { throw "GNU nm was not found beside $($Compiler.Source)." }

# These match kernel/Makefile and kernel/arch/amd64/Makefile.arch on Windows.
$ProductionFlags = @(
    "-std=c++14", "-ffreestanding", "-O2", "-Wall", "-Wextra",
    "-fno-exceptions", "-fno-rtti", "-nostdlib", "-nostdinc++", "-fno-builtin",
    "-DGXOS_BARE_METAL", "-I.", "-Ikernel/core/include", "-Ikernel/arch/amd64/include",
    "-m64", "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2",
    "-fno-pic", "-fno-pie", "-fno-PIC", "-fno-PIE", "-mno-stack-arg-probe",
    "-fno-stack-check", "-fno-stack-protector", "-D__x86_64__"
)

$CoreObject = Join-Path $Output "settings_presentation_core.o"
$RefreshObject = Join-Path $Output "settings_display_refresh.o"
$HarnessObject = Join-Path $Output "settings_presentation_freestanding_harness.o"
$LinkedObject = Join-Path $Output "settings_presentation_freestanding_linked.o"

Write-Host "Compiling shared core with production AMD64 freestanding flags."
& $Compiler.Source @ProductionFlags -c settings_presentation_core.cpp -o $CoreObject
if ($LASTEXITCODE -ne 0) { throw "Freestanding core compilation failed with exit code $LASTEXITCODE." }

Write-Host "Compiling the bounded Display refresh helper with production flags."
& $Compiler.Source @ProductionFlags -c kernel/core/settings_display_refresh.cpp -o $RefreshObject
if ($LASTEXITCODE -ne 0) { throw "Freestanding Display refresh compilation failed with exit code $LASTEXITCODE." }

Write-Host "Compiling bounded contract and link harness with production flags."
& $Compiler.Source @ProductionFlags -c tests/settings_presentation_freestanding_harness.cpp -o $HarnessObject
if ($LASTEXITCODE -ne 0) { throw "Freestanding harness compilation failed with exit code $LASTEXITCODE." }

Write-Host "Linking core and bounded harness as a freestanding relocatable object."
& $Linker -r $CoreObject $HarnessObject -o $LinkedObject
if ($LASTEXITCODE -ne 0) { throw "Freestanding relocatable link failed with exit code $LASTEXITCODE." }

foreach ($Object in @($CoreObject, $LinkedObject)) {
    $Undefined = @(& $Nm -u $Object)
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect symbols in $Object." }
    $Unresolved = @($Undefined | Where-Object { $_ -match "\S" })
    if ($Unresolved.Count -ne 0) {
        throw "Unresolved symbols remain in $Object`: $($Unresolved -join '; ')"
    }
}

$CoreSymbols = @(& $Nm $CoreObject)
if ($LASTEXITCODE -ne 0) { throw "Could not inspect the core symbol table." }
if ($CoreSymbols -match "ProcessTable|Compositor|ipc::|std::|operator new|__cxa|malloc") {
    throw "A hosted or heap-runtime symbol was found in the freestanding core object."
}
$RefreshSymbols = @(& $Nm $RefreshObject)
if ($LASTEXITCODE -ne 0) { throw "Could not inspect the Display refresh symbol table." }
if ($RefreshSymbols -match "std::|operator new|__cxa|malloc|ProcessTable|ipc::") {
    throw "A hosted or heap-runtime symbol was found in the freestanding Display refresh object."
}

Get-Item $CoreObject, $RefreshObject, $HarnessObject, $LinkedObject |
    Select-Object Name, Length |
    Format-Table -AutoSize
Write-Host "Freestanding compile, bounded link, and unresolved-symbol checks PASS."
