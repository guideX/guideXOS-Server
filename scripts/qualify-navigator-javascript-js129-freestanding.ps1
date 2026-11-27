param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root
$Evidence = [System.IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $Evidence) {
    throw "Refusing to overwrite JS129 freestanding evidence: $Evidence"
}
New-Item -ItemType Directory -Path $Evidence | Out-Null
$ObjectDir = Join-Path $Evidence "objects"
New-Item -ItemType Directory -Path $ObjectDir | Out-Null

$Compiler = "C:\mingw64\bin\x86_64-w64-mingw32-g++.exe"
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "JS129 AMD64 compiler was not found: $Compiler"
}
$Version = (& $Compiler -dumpfullversion).Trim()
$Triple = (& $Compiler -dumpmachine).Trim()
if ($Version -ne "15.2.0" -or $Triple -ne "x86_64-w64-mingw32") {
    throw "Unexpected JS129 compiler: GCC $Version, target $Triple"
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
        throw "Required JS129 target tool was not found: $tool"
    }
}

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
    "language=C++17", "flags=$($Flags -join ' ')",
    "base_revision=$(& git rev-parse HEAD)",
    "branch=$(& git branch --show-current)",
    "working_tree=$(& git status --short)"
) | Set-Content -LiteralPath (Join-Path $Evidence "toolchain-and-source.txt")

function Invoke-Logged([string]$Label, [scriptblock]$Action) {
    $output = & $Action 2>&1
    $code = $LASTEXITCODE
    $log = Join-Path $Evidence "$Label.log"
    if ($null -eq $output -or @($output).Count -eq 0) {
        Set-Content -LiteralPath $log -Value ""
    } else {
        $output | Set-Content -LiteralPath $log
    }
    if ($null -eq $code -or $code -ne 0) {
        throw "JS129 $Label failed with exit code $code"
    }
    return ,$output
}

$Sources = @(
    "navigator_javascript\lexer.cpp",
    "navigator_javascript\ast.cpp",
    "navigator_javascript\parser.cpp",
    "navigator_javascript\value.cpp",
    "navigator_javascript\environment.cpp",
    "navigator_javascript\object_storage.cpp",
    "tests\navigator_javascript_js129_unified_call_entry_anchor.cpp"
)
$Objects = @()
foreach ($source in $Sources) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $object = Join-Path $ObjectDir ($base + ".o")
    $compileArgs = @($Flags) + @("-c", $source, "-o", $object)
    Invoke-Logged ($base + "-compile") { & $Compiler @compileArgs } | Out-Null
    $Objects += $object
}

$Component = Join-Path $Evidence "js129-unified-call-entry.o"
Invoke-Logged "relocatable-link" {
    & $Ld "-r" "--no-undefined" @Objects "-o" $Component
} | Out-Null
$Archive = Join-Path $Evidence "js129-unified-call-entry.a"
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
$undefinedCount = @($undefined | Where-Object {
    ([string]$_) -match '^\s+U\s+\S'
}).Count
$archiveUndefinedCount = @($archiveUndefined | Where-Object {
    ([string]$_) -match '^\s+U\s+\S'
}).Count
if ($undefinedCount -ne 0 -or $archiveUndefinedCount -ne 0) {
    throw "JS129 component has an unresolved dependency; inspect nm logs."
}
if (($format -join "`n") -notmatch "pe-x86-64|pei-x86-64") {
    throw "JS129 component is not AMD64 PE/COFF."
}
$sectionText = $sections -join "`n"
if ($sectionText -match "\.tls|\.CRT|\.ctors|\.init_array") {
    throw "JS129 component contains TLS or constructor sections."
}
$symbolText = $symbols -join "`n"
if ($symbolText -notmatch "gxos_js129_unified_call_entry_probe") {
    throw "JS129 anchor symbol is missing from the component."
}
if ($symbolText -notmatch "gxos_js129_invoke_callable_entry") {
    throw "JS129 component is missing the unified external call entry."
}
if ($symbolText -match "std::|__cxa|__gxx|operator new|operator delete|_tls|__imp_|malloc|free") {
    throw "JS129 component contains a prohibited runtime or platform symbol."
}

$stack = @()
Get-ChildItem -LiteralPath $ObjectDir -File -Filter "*.su" |
    ForEach-Object { $stack += Get-Content -LiteralPath $_.FullName }
$stack | Set-Content -LiteralPath (Join-Path $Evidence "stack-usage.txt")
$stackText = $stack -join "`n"
foreach ($entry in @("invoke_callable_entry", "runExecutionMachine",
        "preflightAst", "machineRunExpression")) {
    if ($stackText -notmatch [regex]::Escape($entry)) {
        throw "Target stack report does not include $entry."
    }
}
$targetStack = @($stack | Where-Object {
    ([string]$_) -match "invoke_callable_entry|runExecutionMachine|preflightAst|machineRunExpression"
})
$targetStack | Set-Content -LiteralPath (Join-Path $Evidence "target-stack-frames.txt")

$metadata = [pscustomobject]@{
    status = "Passed"
    sourceLineage = "Reconstructed from JS128; not byte-identical to the unavailable JS129 source."
    compiler = $Compiler
    compilerVersion = $Version
    target = $Triple
    languageStandard = "C++17"
    flags = $Flags
    sourceFiles = $Sources
    objectFiles = $Objects
    relocatableComponent = $Component
    staticArchive = $Archive
    objectFormat = ($format -join "`n").Trim()
    undefinedSymbolCount = $undefinedCount
    archiveUndefinedSymbolCount = $archiveUndefinedCount
    prohibitedSymbolsFound = $false
    tlsOrConstructorSectionFound = $false
    portableCallFrameBytes = 80
    portableExecutionFrameBytes = 112
    preflightWorkRecordBytes = 112
    maximumAstTraversalDepth = 256
    maximumPreflightWorkUnits = 65536
    maximumPreflightFramesAtConfiguredDepth = 257
    preflightFrameBackingAtConfiguredDepthBytes = 28784
    defaultExecutionFrameCapacity = 4096
    executionFrameBackingBytes = 458752
    logicalCallDepth = 64
    configuredArgumentValueCapacity = 512
    configuredInvocationBackingBytes = 472064
    stackUsageFile = (Join-Path $Evidence "stack-usage.txt")
    targetStackFramesFile = (Join-Path $Evidence "target-stack-frames.txt")
    componentSha256 = (Get-FileHash -LiteralPath $Component -Algorithm SHA256).Hash
    hostedExecutionUsesSameCore = $true
    guestExecution = $false
}
$metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
    (Join-Path $Evidence "qualification.json") -Encoding UTF8
Write-Output "JS129 AMD64 freestanding unified call entry: compile, link, and dependency audit PASS"
Write-Output "Evidence: $Evidence"
