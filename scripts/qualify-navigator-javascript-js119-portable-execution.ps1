param(
    [string]$EvidenceDirectory
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$Compiler = "C:\mingw64\bin\x86_64-w64-mingw32-g++.exe"
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "JS119 AMD64 compiler was not found: $Compiler"
}
$Version = (& $Compiler -dumpfullversion).Trim()
$Triple = (& $Compiler -dumpmachine).Trim()
if ($Version -ne "15.2.0" -or $Triple -ne "x86_64-w64-mingw32") {
    throw "Unexpected JS119 target compiler: GCC $Version, target $Triple"
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
        throw "Required JS119 target binutil was not found: $tool"
    }
}

$EvidenceRoot = "E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS119"
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    $runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $EvidenceDirectory = Join-Path $EvidenceRoot "run-$runStamp\freestanding"
}
$Evidence = [System.IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $Evidence) {
    throw "Refusing to overwrite JS119 evidence: $Evidence"
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
        throw "JS119 $Label failed with exit code $code"
    }
    return ,$output
}

$Sources = @(
    "navigator_javascript\lexer.cpp",
    "navigator_javascript\ast.cpp",
    "navigator_javascript\parser.cpp",
    "navigator_javascript\value.cpp",
    "navigator_javascript\environment.cpp",
    "tests\navigator_javascript_js119_execution_anchor.cpp"
)
$Objects = @()
foreach ($source in $Sources) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $object = Join-Path $ObjectDir ($baseName + ".o")
    $compileArgs = @($Flags) + @("-c", $source, "-o", $object)
    Invoke-Logged ($baseName + "-compile") { & $Compiler @compileArgs } | Out-Null
    $Objects += $object
}

$Component = Join-Path $Evidence "js119-portable-execution.o"
$linkArgs = @("-r", "--no-undefined") + $Objects + @("-o", $Component)
Invoke-Logged "relocatable-link" { & $Ld @linkArgs } | Out-Null
$Archive = Join-Path $Evidence "js119-portable-execution.a"
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
    throw "JS119 component has an undefined dependency; inspect nm logs."
}
if (($format -join "`n") -notmatch "pe-x86-64|pei-x86-64") {
    throw "JS119 component is not AMD64 PE/COFF."
}
$sectionText = $sections -join "`n"
if ($sectionText -match "\.tls|\.CRT|\.ctors") {
    throw "JS119 component contains TLS or constructor sections."
}
if (($symbols -join "`n") -notmatch "gxos_js119_portable_execution_probe") {
    throw "JS119 real portable parser/execution probe was not retained."
}
if (($symbols -join "`n") -match "std::|__cxa|__gxx|operator new|operator delete|_tls|__imp_|malloc|free") {
    throw "JS119 component defines an unexpected runtime or platform dependency."
}
$bssMatch = [regex]::Match($sectionText, '(?m)^\s*\d+\s+\.bss\s+([0-9a-fA-F]+)')
$bssBytes = if ($bssMatch.Success) {
    [Convert]::ToInt64($bssMatch.Groups[1].Value, 16)
} else { 0 }
$dataMatch = [regex]::Match($sectionText, '(?m)^\s*\d+\s+\.data\s+([0-9a-fA-F]+)')
$dataBytes = if ($dataMatch.Success) {
    [Convert]::ToInt64($dataMatch.Groups[1].Value, 16)
} else { 0 }

$stackLines = @()
Get-ChildItem -LiteralPath $ObjectDir -File -Filter "*.su" | ForEach-Object {
    $stackLines += Get-Content -LiteralPath $_.FullName
}
$stackLines | Set-Content -LiteralPath (Join-Path $Evidence "stack-usage.txt")
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
    recordSizes = [pscustomobject]@{
        value = 16
        functionRecord = 48
        environmentRecord = 24
        binding = 40
        portableCallFrame = 72
        portableExecutionCore = 296
    }
    recordAlignments = [pscustomobject]@{
        value = 8
        functionRecord = 8
        environmentRecord = 4
        binding = 8
        portableCallFrame = 8
        portableExecutionCore = 8
    }
    stackUsageFile = (Join-Path $Evidence "stack-usage.txt")
    tlsSectionFound = ($sectionText -match "\.tls")
    dataSectionBytes = $dataBytes
    bssSectionBytes = $bssBytes
    status = "AMD64 COFF compile/link and dependency audit; no target execution"
}
$metadata | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $Evidence "qualification.json") -Encoding UTF8
Write-Output "JS119 AMD64 freestanding execution component: compile, link, and dependency audit PASS"
Write-Output "Evidence: $Evidence"
