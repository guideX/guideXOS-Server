param(
    [string]$EvidenceDirectory
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$Compiler = "C:\mingw64\bin\x86_64-w64-mingw32-g++.exe"
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "JS126 AMD64 compiler was not found: $Compiler"
}
$Version = (& $Compiler -dumpfullversion).Trim()
$Triple = (& $Compiler -dumpmachine).Trim()
if ($Version -ne "15.2.0" -or $Triple -ne "x86_64-w64-mingw32") {
    throw "Unexpected JS126 compiler: GCC $Version, target $Triple"
}

$CompilerRoot = Split-Path -Parent (Split-Path -Parent $Compiler)
$CppInclude = Join-Path $CompilerRoot "include\c++\$Version"
$TargetCppInclude = Join-Path $CppInclude $Triple
$ToolRoot = Join-Path $CompilerRoot "$Triple\bin"
$Ld = Join-Path $ToolRoot "ld.exe"
$Ar = Join-Path $ToolRoot "ar.exe"
$Nm = Join-Path $ToolRoot "nm.exe"
$Objdump = Join-Path $ToolRoot "objdump.exe"
foreach ($tool in @($Ld, $Ar, $Nm, $Objdump)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required JS126 target binutil was not found: $tool"
    }
}

if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $runId = [Guid]::NewGuid().ToString("N")
    $EvidenceDirectory = Join-Path `
        "E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS126" `
        ("run-$stamp-$runId\freestanding")
}
$Evidence = [System.IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $Evidence) {
    throw "Refusing to overwrite JS126 evidence: $Evidence"
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
    "compiler=$Compiler", "version=$Version", "target=$Triple",
    "cpp_include=$CppInclude", "target_cpp_include=$TargetCppInclude",
    "linker=$Ld", "archiver=$Ar", "nm=$Nm", "objdump=$Objdump",
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
    if ($code -ne 0) { throw "JS126 $Label failed with exit code $code" }
    return ,$output
}

$Sources = @(
    "navigator_javascript\lexer.cpp",
    "navigator_javascript\ast.cpp",
    "navigator_javascript\parser.cpp",
    "navigator_javascript\value.cpp",
    "navigator_javascript\environment.cpp",
    "navigator_javascript\object_storage.cpp",
    "tests\navigator_javascript_js126_callable_identity_stack_anchor.cpp"
)
$Objects = @()
foreach ($source in $Sources) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $object = Join-Path $ObjectDir ($baseName + ".o")
    $compileArgs = @($Flags) + @("-c", $source, "-o", $object)
    Invoke-Logged ($baseName + "-compile") { & $Compiler @compileArgs } | Out-Null
    $Objects += $object
}

$Component = Join-Path $Evidence "js126-portable-callable-identity-stack.o"
Invoke-Logged "relocatable-link" {
    & $Ld "-r" "--no-undefined" @Objects "-o" $Component
} | Out-Null
$Archive = Join-Path $Evidence "js126-portable-callable-identity-stack.a"
Invoke-Logged "static-archive" { & $Ar "rcs" $Archive $Component } | Out-Null

$format = Invoke-Logged "object-format" { & $Objdump "-f" $Component }
$sections = Invoke-Logged "sections" { & $Objdump "-h" $Component }
$symbols = Invoke-Logged "defined-symbols" {
    & $Nm "-g" "-C" "--defined-only" $Component
}
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
    throw "JS126 component has an undefined dependency; inspect nm logs."
}
if (($format -join "`n") -notmatch "pe-x86-64|pei-x86-64") {
    throw "JS126 component is not AMD64 PE/COFF."
}
$sectionText = $sections -join "`n"
if ($sectionText -match "\.tls|\.CRT|\.ctors|\.init_array") {
    throw "JS126 component contains TLS or constructor sections."
}
$symbolText = $symbols -join "`n"
foreach ($requiredSymbol in @(
        "gxos_js126_portable_callback_probe",
        "PortableObjectStore::snapshotOwnEnumerableStringProperties",
        "PortableObjectStore::getProperty",
        "PortableObjectStore::setOwnProperty",
        "PortableObjectStore::deleteOwnProperty")) {
    if ($symbolText -notmatch [regex]::Escape($requiredSymbol)) {
        throw "JS126 component omitted a qualified shared symbol: $requiredSymbol"
    }
}
if ($symbolText -match "std::|__cxa|__gxx|operator new|operator delete|_tls|__imp_|malloc|free") {
    throw "JS126 component contains a runtime or platform dependency."
}

$stackLines = @()
Get-ChildItem -LiteralPath $ObjectDir -File -Filter "*.su" |
    ForEach-Object { $stackLines += Get-Content -LiteralPath $_.FullName }
$stackLines | Set-Content -LiteralPath (Join-Path $Evidence "stack-usage.txt")
$metadata = [pscustomobject]@{
    compiler = $Compiler
    compilerVersion = $Version
    target = $Triple
    languageStandard = "C++17"
    flags = $Flags
    objectFormat = $format
    undefinedSymbolCount = $undefinedLines.Count
    archiveUndefinedSymbolCount = $archiveUndefinedLines.Count
    objectFiles = $Objects
    relocatableComponent = $Component
    staticArchive = $Archive
    componentSha256 = (Get-FileHash -LiteralPath $Component -Algorithm SHA256).Hash
    structureSizes = [pscustomobject]@{
        value = 16
        runtimeFunctionId = 8
        functionRecord = 48
        environmentRecord = 24
        binding = 40
        portableCallFrame = 72
        propertySnapshotEntry = 8
    }
    stackUsageFile = (Join-Path $Evidence "stack-usage.txt")
    tlsOrConstructorSectionFound = ($sectionText -match "\.tls|\.CRT|\.ctors|\.init_array")
    guestExecution = $false
    tailDispatch = "Direct returned-call invocations use an iterative loop while retaining logical depth and caller-backed frames"
    status = "Actual shared callable lookup and invocation core compile/link/dependency audit PASS; no guest execution"
}
$metadata | ConvertTo-Json -Depth 7 |
    Set-Content -LiteralPath (Join-Path $Evidence "qualification.json") -Encoding UTF8
Write-Output "JS126 AMD64 freestanding callable identity and bounded stack: compile, link, and dependency audit PASS"
Write-Output "Evidence: $Evidence"
