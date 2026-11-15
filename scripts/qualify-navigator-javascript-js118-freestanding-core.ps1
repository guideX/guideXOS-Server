param(
    [string]$EvidenceDirectory
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$Compiler = "C:\mingw64\bin\x86_64-w64-mingw32-g++.exe"
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "JS118 AMD64 compiler was not found: $Compiler"
}
$Version = (& $Compiler -dumpfullversion).Trim()
$Triple = (& $Compiler -dumpmachine).Trim()
if ($Version -ne "15.2.0" -or $Triple -ne "x86_64-w64-mingw32") {
    throw "Unexpected JS118 target compiler: GCC $Version, target $Triple"
}

$CompilerRoot = Split-Path -Parent (Split-Path -Parent $Compiler)
$CppInclude = Join-Path $CompilerRoot "include\c++\$Version"
$TargetCppInclude = Join-Path $CppInclude $Triple
if (-not (Test-Path -LiteralPath (Join-Path $CppInclude "cstdint")) -or
    -not (Test-Path -LiteralPath (Join-Path $TargetCppInclude "bits\c++config.h"))) {
    throw "Explicit target C++17 headers were not found under $CppInclude"
}

$ToolRoot = Join-Path $CompilerRoot "$Triple\bin"
$Ld = Join-Path $ToolRoot "ld.exe"
$Ar = Join-Path $ToolRoot "ar.exe"
$Nm = Join-Path $ToolRoot "nm.exe"
$Objdump = Join-Path $ToolRoot "objdump.exe"
foreach ($tool in @($Ld, $Ar, $Nm, $Objdump)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required target binutil was not found: $tool"
    }
}

$Evidence = $EvidenceDirectory
if ([string]::IsNullOrWhiteSpace($Evidence)) {
    $Evidence = "E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS118\freestanding-run-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
$Evidence = [System.IO.Path]::GetFullPath($Evidence)
if (Test-Path -LiteralPath $Evidence) {
    throw "Refusing to overwrite existing JS118 evidence: $Evidence"
}
$ObjectDir = Join-Path $Evidence "objects"
New-Item -ItemType Directory -Path $ObjectDir -Force | Out-Null

$Flags = @(
    "-std=c++17", "-ffreestanding", "-O2", "-Wall", "-Wextra", "-Werror",
    "-pedantic", "-fno-exceptions", "-fno-rtti", "-nostdlib",
    "-nostdinc++", "-fno-builtin", "-DGXOS_BARE_METAL",
    "-DGXOS_NAVIGATOR_FRONTEND_FREESTANDING", "-m64", "-mcmodel=large",
    "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2", "-fno-pic",
    "-fno-pie", "-fno-PIC", "-fno-PIE", "-mno-stack-arg-probe",
    "-fno-stack-check", "-fno-stack-protector", "-fstack-usage",
    "-D__x86_64__", "-isystem", $CppInclude, "-isystem", $TargetCppInclude,
    "-iquote", "."
)

@(
    "compiler=$Compiler",
    "version=$Version",
    "target=$Triple",
    "cpp_include=$CppInclude",
    "target_cpp_include=$TargetCppInclude",
    "linker=$Ld",
    "archiver=$Ar",
    "nm=$Nm",
    "objdump=$Objdump",
    "flags=$($Flags -join ' ')"
) | Set-Content -LiteralPath (Join-Path $Evidence "toolchain-and-flags.txt")

function Invoke-Logged([string]$Label, [scriptblock]$Action) {
    $output = & $Action 2>&1
    $code = $LASTEXITCODE
    $log = Join-Path $Evidence "$Label.log"
    if ($null -eq $output -or @($output).Count -eq 0) {
        Set-Content -LiteralPath $log -Value ""
    } else {
        $output | Set-Content -LiteralPath $log
    }
    if ($code -ne 0) {
        throw "JS118 $Label failed with exit code $code"
    }
    return ,$output
}

$Sources = @(
    "navigator_javascript\value.cpp",
    "navigator_javascript\environment.cpp",
    "tests\navigator_javascript_js118_runtime_core_anchor.cpp"
)
$Objects = @()
foreach ($source in $Sources) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $object = Join-Path $ObjectDir ($baseName + ".o")
    $compileArgs = @($Flags) + @("-c", $source, "-o", $object)
    $label = $baseName + "-compile"
    Invoke-Logged $label { & $Compiler @compileArgs } | Out-Null
    $Objects += $object
}

$Component = Join-Path $Evidence "js118-runtime-core.o"
$linkArgs = @("-r", "--no-undefined") + $Objects + @("-o", $Component)
Invoke-Logged "relocatable-link" { & $Ld @linkArgs } | Out-Null

$Archive = Join-Path $Evidence "js118-runtime-core.a"
Invoke-Logged "static-archive" { & $Ar "rcs" $Archive $Component } | Out-Null

$format = Invoke-Logged "object-format" { & $Objdump "-f" $Component }
$sections = Invoke-Logged "sections" { & $Objdump "-h" $Component }
$symbols = Invoke-Logged "defined-symbols" { & $Nm "-g" "-C" "--defined-only" $Component }
$undefined = Invoke-Logged "undefined-symbols" { & $Nm "-u" "-C" $Component }
$archiveUndefined = Invoke-Logged "archive-undefined-symbols" {
    & $Nm "-u" "-C" $Archive
}

$undefinedLines = @($undefined | Where-Object {
    ([string]$_) -match '^\s+U\s+\S'
})
$archiveUndefinedLines = @($archiveUndefined | Where-Object {
    ([string]$_) -match '^\s+U\s+\S'
})
if ($undefinedLines.Count -ne 0 -or $archiveUndefinedLines.Count -ne 0) {
    throw "JS118 component has an undefined dependency; inspect nm logs."
}
if (($format -join "`n") -notmatch "pe-x86-64|pei-x86-64") {
    throw "JS118 component is not AMD64 PE/COFF."
}
$sectionText = $sections -join "`n"
if ($sectionText -match "\.tls|\.CRT|\.ctors") {
    throw "JS118 component contains a TLS or constructor section."
}
$bssMatch = [regex]::Match($sectionText, '(?m)^\s*\d+\s+\.bss\s+([0-9a-fA-F]+)')
$bssBytes = if ($bssMatch.Success) {
    [Convert]::ToInt64($bssMatch.Groups[1].Value, 16)
} else { 0 }
if ($bssBytes -ne 0) {
    throw "JS118 component contains $bssBytes bytes of BSS storage."
}
if (($symbols -join "`n") -notmatch "gxos_js118_runtime_core_probe" -or
    ($symbols -join "`n") -notmatch "RuntimeStringStore::create" -or
    ($symbols -join "`n") -notmatch "EnvironmentStore::declareImpl") {
    throw "JS118 shared operations were not retained in the closed component."
}
if (($symbols -join "`n") -match "std::|__cxa|__gxx|operator new|operator delete|_tls|__imp_|malloc|free") {
    throw "JS118 component defines an unexpected runtime or platform dependency."
}

$StackFiles = Get-ChildItem -LiteralPath $ObjectDir -File -Filter "*.su"
$stackLines = @()
foreach ($stackFile in $StackFiles) {
    $stackLines += Get-Content -LiteralPath $stackFile.FullName
}
$stackLines | Set-Content -LiteralPath (Join-Path $Evidence "stack-usage.txt")

$recordSizes = [pscustomobject]@{
    value = 16
    runtimeStringRecord = 16
    environmentRecord = 24
    binding = 40
    environmentStoreMetadata = 88
    runtimeStringStoreMetadata = 64
    environmentAdapter = 32
    valueAlignment = 8
    stringRecordAlignment = 8
    environmentRecordAlignment = 4
    bindingAlignment = 8
}
$metadata = [pscustomobject]@{
    compiler = $Compiler
    compilerVersion = $Version
    target = $Triple
    languageStandard = "C++17"
    flags = $Flags
    objectFormat = $format
    undefinedSymbols = $undefinedLines
    archiveUndefinedSymbols = $archiveUndefinedLines
    objectFiles = $Objects
    relocatableComponent = $Component
    staticArchive = $Archive
    recordSizes = $recordSizes
    stackUsageFile = (Join-Path $Evidence "stack-usage.txt")
    tlsSectionFound = ($sectionText -match "\.tls")
    bssSectionBytes = $bssBytes
    status = "COFF dependency closure passed; no target execution performed"
}
$metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Evidence "qualification.json") -Encoding UTF8
Write-Output "JS118 AMD64 freestanding runtime core: COFF compile, relocatable link, and undefined-symbol audit PASS"
Write-Output "Evidence: $Evidence"
