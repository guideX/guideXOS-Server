param(
    [string]$EvidenceDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$Compiler = "C:\mingw64\bin\x86_64-w64-mingw32-g++.exe"
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "JS123 AMD64 compiler was not found: $Compiler"
}
$Version = (& $Compiler -dumpfullversion).Trim()
$Triple = (& $Compiler -dumpmachine).Trim()
if ($Version -ne "15.2.0" -or $Triple -ne "x86_64-w64-mingw32") {
    throw "Unexpected JS123 compiler: GCC $Version, target $Triple"
}

$CompilerRoot = Split-Path -Parent (Split-Path -Parent $Compiler)
$CppInclude = Join-Path $CompilerRoot "include\c++\$Version"
$TargetCppInclude = Join-Path $CppInclude $Triple
$ToolRoot = Join-Path $CompilerRoot "$Triple\bin"
$Ld = Join-Path $ToolRoot "ld.exe"
$Nm = Join-Path $ToolRoot "nm.exe"
$Objdump = Join-Path $ToolRoot "objdump.exe"
foreach ($tool in @($Ld, $Nm, $Objdump)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required JS123 target binutil was not found: $tool"
    }
}

if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    $runName = "run-" + (Get-Date -Format "yyyyMMdd-HHmmss") + "-" +
        [Guid]::NewGuid().ToString("N")
    $EvidenceDirectory = Join-Path `
        "E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS123" `
        (Join-Path $runName "freestanding")
}
$Evidence = [System.IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $Evidence) {
    throw "Refusing to overwrite JS123 evidence: $Evidence"
}
$ObjectDir = Join-Path $Evidence "objects"
New-Item -ItemType Directory -Path $ObjectDir -Force | Out-Null

$Flags = @(
    "-std=c++17", "-ffreestanding", "-O2", "-Wall", "-Wextra", "-Werror",
    "-pedantic", "-fno-exceptions", "-fno-rtti", "-nostdlib",
    "-nostdinc++", "-fno-builtin", "-DGXOS_BARE_METAL",
    "-m64", "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse",
    "-mno-sse2", "-fno-pic", "-fno-pie", "-fno-PIC", "-fno-PIE",
    "-mno-stack-arg-probe", "-fno-stack-check", "-fno-stack-protector",
    "-fno-use-cxa-atexit", "-fstack-usage", "-D__x86_64__",
    "-isystem", $CppInclude, "-isystem", $TargetCppInclude, "-iquote", "."
)

@(
    "compiler=$Compiler",
    "version=$Version",
    "target=$Triple",
    "cpp_include=$CppInclude",
    "target_cpp_include=$TargetCppInclude",
    "linker=$Ld",
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
        throw "JS123 $Label failed with exit code $code"
    }
    return ,$output
}

$Sources = @(
    "navigator_javascript\value.cpp",
    "navigator_javascript\object_storage.cpp",
    "tests\navigator_javascript_js122_json_anchor.cpp",
    "tests\navigator_javascript_js123_json_anchor.cpp"
)
$Objects = @()
foreach ($source in $Sources) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $object = Join-Path $ObjectDir ($baseName + ".o")
    $compileArgs = @($Flags) + @("-c", $source, "-o", $object)
    Invoke-Logged ($baseName + "-compile") {
        & $Compiler @compileArgs
    } | Out-Null
    $Objects += $object
}

$Component = Join-Path $Evidence "js123-portable-json.o"
Invoke-Logged "relocatable-link" {
    & $Ld "-r" "--no-undefined" @Objects "-o" $Component
} | Out-Null
$format = Invoke-Logged "object-format" { & $Objdump "-f" $Component }
$sections = Invoke-Logged "sections" { & $Objdump "-h" $Component }
$symbols = Invoke-Logged "defined-symbols" {
    & $Nm "-g" "-C" "--defined-only" $Component
}
$allSymbols = Invoke-Logged "all-symbols" { & $Nm "-C" $Component }
$undefined = Invoke-Logged "undefined-symbols" { & $Nm "-u" "-C" $Component }
$undefinedLines = @($undefined | Where-Object {
    ([string]$_) -match '^\s+U\s+\S'
})
if ($undefinedLines.Count -ne 0) {
    throw "JS123 freestanding component has undefined dependencies; inspect nm logs."
}
if (($format -join "`n") -notmatch "pe-x86-64|pei-x86-64") {
    throw "JS123 component is not AMD64 PE/COFF."
}
$sectionText = $sections -join "`n"
if ($sectionText -match "\.tls|\.CRT|\.ctors|\.init_array") {
    throw "JS123 component contains TLS or constructor sections."
}
$symbolText = $symbols -join "`n"
foreach ($requiredSymbol in @(
        "gxos_js122_portable_json_probe",
        "gxos_js123_json_writer_probe",
        "PortableObjectStore::createObject",
        "PortableObjectStore::setOwnProperty",
        "PortableObjectStore::setArrayElement",
        "RuntimeStringStore::create")) {
    if ($symbolText -notmatch [regex]::Escape($requiredSymbol)) {
        throw "JS123 component omitted required shared symbol: $requiredSymbol"
    }
}
if (($allSymbols -join "`n") -match "std::|__cxa|__gxx|operator new|operator delete|_tls|__imp_|malloc|free|_Unwind") {
    throw "JS123 component defines a prohibited C++ runtime or platform dependency."
}

$stackLines = @()
Get-ChildItem -LiteralPath $ObjectDir -File -Filter "*.su" |
    ForEach-Object { $stackLines += Get-Content -LiteralPath $_.FullName }
$stackLines | Set-Content -LiteralPath (Join-Path $Evidence "stack-usage.txt")
$sectionSizes = [ordered]@{}
[regex]::Matches($sectionText,
    '(?m)^\s*\d+\s+(\.\S+)\s+([0-9a-fA-F]+)') | ForEach-Object {
    $sectionSizes[$_.Groups[1].Value] =
        [Convert]::ToInt64($_.Groups[2].Value, 16)
}
$metadata = [pscustomobject]@{
    compiler = $Compiler
    compilerVersion = $Version
    target = $Triple
    languageStandard = "C++17"
    flags = $Flags
    objectFormat = $format
    undefinedSymbols = $undefinedLines
    translationUnits = $Sources
    relocatableComponent = $Component
    componentSha256 = (Get-FileHash -LiteralPath $Component -Algorithm SHA256).Hash
    componentBytes = (Get-Item -LiteralPath $Component).Length
    sectionBytes = $sectionSizes
    dynamicAllocation = $false
    exceptionSupport = $false
    rttiSupport = $false
    undefinedSymbolCount = $undefinedLines.Count
    stackUsageFile = (Join-Path $Evidence "stack-usage.txt")
    unwindMetadataSections = @(".pdata", ".xdata")
    tlsOrConstructorSectionFound = ($sectionText -match "\.tls|\.CRT|\.ctors|\.init_array")
    guestExecution = $false
    status = "AMD64 freestanding numeric parser and bounded writer component compile/link and dependency audit PASS"
}
$metadata | ConvertTo-Json -Depth 7 |
    Set-Content -LiteralPath (Join-Path $Evidence "qualification.json") -Encoding UTF8
Write-Output "JS123 AMD64 freestanding portable JSON compile/link and dependency audit PASS"
Write-Output "Evidence: $Evidence"
