param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path,
    [string]$AdaptedLibrary = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Find-Tool([string]$Name) {
    $candidate = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\$Name"
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    throw "MSVC tool not found: $Name"
}

function Find-VcVars64 {
    $candidate = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    throw "Visual C++ vcvars64.bat was not found."
}

function Invoke-VcBatch([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllLines($Path, $Lines, [Text.ASCIIEncoding]::new())
    & $Path
    if ($LASTEXITCODE -ne 0) { throw "Build command failed ($LASTEXITCODE): $Path" }
}

$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$runtimePackRoot = Join-Path $RepoRoot "tools\dotnet\runtime-pack"
$headerRoot = Join-Path $RepoRoot "out\dotnet\gc-feasibility-baseline\nativeaot-runtime"
$outputRoot = Join-Path $RepoRoot "out\dotnet\gc-platform-object-replacement\rebuilt\exact-symbol-probe"
if ([string]::IsNullOrWhiteSpace($AdaptedLibrary)) {
    $AdaptedLibrary = Join-Path $RepoRoot "out\dotnet\gc-platform-object-replacement\rebuilt\Runtime.WorkstationGC.lib"
}
$AdaptedLibrary = [IO.Path]::GetFullPath($AdaptedLibrary)
if (-not (Test-Path -LiteralPath $AdaptedLibrary)) { throw "Adapted archive not found: $AdaptedLibrary" }
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$vcvars = Find-VcVars64
$cl = Find-Tool "cl.exe"
$probeSource = Join-Path $runtimePackRoot "src\gcenv\guidexos_gcenv_exact_symbol_probe.cpp"
$gcIncludes = "/I`"$(Join-Path $headerRoot 'src\coreclr\gc')`" /I`"$(Join-Path $headerRoot 'src\coreclr\gc\env')`" /I`"$(Join-Path $headerRoot 'src\coreclr\nativeaot\Runtime')`" /I`"$(Join-Path $headerRoot 'src\native')`""
$common = "/nologo /std:c++17 /TP /c /EHsc /GR- /GS- /O2 /Brepro"

$sources = [ordered]@{
    probe = $probeSource
    gc_platform_services = Join-Path $runtimePackRoot "src\gcenv\guidexos_gc_platform_services.cpp"
    virtual_memory_region = Join-Path $RepoRoot "runtime\memory\guidexos_virtual_memory_region.cpp"
    nativeaot_virtual_memory_adapter = Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_virtual_memory_adapter.cpp"
    event = Join-Path $RepoRoot "runtime\synchronization\guidexos_event.cpp"
    nativeaot_event_adapter = Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_event_adapter.cpp"
    mutex = Join-Path $RepoRoot "runtime\synchronization\guidexos_mutex.cpp"
    nativeaot_critical_section_adapter = Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_critical_section_adapter.cpp"
    native_thread = Join-Path $RepoRoot "runtime\thread\guidexos_native_thread.cpp"
    local_storage = Join-Path $RepoRoot "runtime\local_storage\guidexos_local_storage.cpp"
    nativeaot_fls_adapter = Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_fls_adapter.cpp"
    nativeaot_thread_adapter = Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_thread_adapter.cpp"
}
$objects = [ordered]@{}
$batch = Join-Path $outputRoot "build-exact-symbol-probe.bat"
$lines = @("@echo off", "setlocal", "call `"$vcvars`" >nul", "if errorlevel 1 exit /b %errorlevel%")
foreach ($entry in $sources.GetEnumerator()) {
    $object = Join-Path $outputRoot ($entry.Key + ".obj")
    $objects[$entry.Key] = $object
    $flags = if ($entry.Key -eq "probe") { $common + " " + $gcIncludes } else { $common }
    $lines += "cl.exe $flags /Fo:`"$object`" `"$($entry.Value)`""
    $lines += "if errorlevel 1 exit /b %errorlevel%"
}
$exe = Join-Path $outputRoot "guidexos_gcenv_exact_symbol_probe.exe"
$map = Join-Path $outputRoot "guidexos_gcenv_exact_symbol_probe.map"
$linkObjects = ($objects.Values | ForEach-Object { "`"$_`"" }) -join " "
$lines += "link.exe /nologo /OUT:`"$exe`" /MAP:`"$map`" $linkObjects `"$AdaptedLibrary`""
$lines += "if errorlevel 1 exit /b %errorlevel%"
$lines += "exit /b 0"
Invoke-VcBatch $batch $lines

$output = @(& $exe 2>&1 | ForEach-Object { $_.ToString() })
$exitCode = $LASTEXITCODE
$output | Set-Content -LiteralPath (Join-Path $outputRoot "probe-output.txt") -Encoding ASCII
if ($exitCode -ne 0) {
    throw "Exact-symbol hosted probe failed with exit code $exitCode."
}
if (-not ($output -contains "Exact-symbol hosted probe: PASS")) {
    throw "Exact-symbol hosted probe did not emit its PASS marker."
}

$identity = [ordered]@{
    adaptedLibrary = $AdaptedLibrary
    adaptedLibrarySha256 = (Get-FileHash -LiteralPath $AdaptedLibrary -Algorithm SHA256).Hash.ToUpperInvariant()
    executable = $exe
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToUpperInvariant()
    map = $map
    result = "PASS"
    rhInitializeCalled = $false
    collectorHeapConstructed = $false
    collectionEntered = $false
}
$identity | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot "probe-result.json") -Encoding UTF8
Write-Output "Exact-symbol hosted probe: PASS"
Write-Output ("Probe SHA-256: {0}" -f $identity.executableSha256)
