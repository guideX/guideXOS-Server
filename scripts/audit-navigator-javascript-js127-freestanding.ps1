param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactDirectory
)

$ErrorActionPreference = "Stop"
$Evidence = [System.IO.Path]::GetFullPath($ArtifactDirectory)
$qualificationPath = Join-Path $Evidence "qualification.json"
$symbolPath = Join-Path $Evidence "defined-symbols.log"
$undefinedPath = Join-Path $Evidence "undefined-symbols.log"
$archiveUndefinedPath = Join-Path $Evidence "archive-undefined-symbols.log"
$formatPath = Join-Path $Evidence "object-format.log"
$sectionPath = Join-Path $Evidence "sections.log"
$stackPath = Join-Path $Evidence "stack-usage.txt"
foreach ($path in @($qualificationPath, $symbolPath, $undefinedPath,
        $archiveUndefinedPath, $formatPath, $sectionPath, $stackPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing JS127 freestanding qualification evidence: $path"
    }
}

$qualification = Get-Content -Raw -LiteralPath $qualificationPath |
    ConvertFrom-Json
$symbols = Get-Content -Raw -LiteralPath $symbolPath
$undefined = Get-Content -Raw -LiteralPath $undefinedPath
$archiveUndefined = Get-Content -Raw -LiteralPath $archiveUndefinedPath
$format = Get-Content -Raw -LiteralPath $formatPath
$sections = Get-Content -Raw -LiteralPath $sectionPath
$stack = Get-Content -Raw -LiteralPath $stackPath
$loopMatch = [regex]::Match($stack,
    'invokeSelfArithmeticContinuation[^\r\n]*?[\t ]+(\d+)[\t ]+static')
$pushMatch = [regex]::Match($stack,
    'pushFunctionActivation[^\r\n]*?[\t ]+(\d+)[\t ]+static')
$invokeMatch = [regex]::Match($stack,
    'invokeFunction\([^\r\n]*?[\t ]+(\d+)[\t ]+static')
$loopFrame = if ($loopMatch.Success) { [int]$loopMatch.Groups[1].Value } else { $null }
$pushFrame = if ($pushMatch.Success) { [int]$pushMatch.Groups[1].Value } else { $null }
$invokeFrame = if ($invokeMatch.Success) { [int]$invokeMatch.Groups[1].Value } else { $null }
$missing = [System.Collections.Generic.List[string]]::new()
if ($qualification.compilerVersion -ne "15.2.0") { $missing.Add("compiler-version") }
if ($qualification.target -ne "x86_64-w64-mingw32") { $missing.Add("target") }
if ($qualification.structureSizes.portableCallFrame -ne 80) {
    $missing.Add("call-frame-size")
}
if ($qualification.undefinedSymbolCount -ne 0 -or
    $qualification.archiveUndefinedSymbolCount -ne 0 -or
    $undefined -match '\n\s*U\s+\S' -or
    $archiveUndefined -match '\n\s*U\s+\S') {
    $missing.Add("undefined-symbols")
}
if ($format -notmatch 'pe-x86-64|pei-x86-64') { $missing.Add("object-format") }
if ($sections -match '\.tls|\.CRT|\.ctors|\.init_array') {
    $missing.Add("tls-or-constructor-section")
}
if ($symbols -notmatch 'invokeSelfArithmeticContinuation' -or
    $symbols -notmatch 'pushFunctionActivation') {
    $missing.Add("continuation-symbols")
}
if (-not $loopMatch.Success -or -not $pushMatch.Success -or
    -not $invokeMatch.Success) { $missing.Add("stack-usage-records") }

$audit = [pscustomobject]@{
    status = if ($missing.Count -eq 0) { "Passed" } else { "Failed" }
    sourceEvidence = $qualificationPath
    compiler = $qualification.compiler
    compilerVersion = $qualification.compilerVersion
    target = $qualification.target
    objectFormat = $format.Trim()
    undefinedSymbolCount = $qualification.undefinedSymbolCount
    archiveUndefinedSymbolCount = $qualification.archiveUndefinedSymbolCount
    tlsOrConstructorSectionFound = [bool]($sections -match
        '\.tls|\.CRT|\.ctors|\.init_array')
    portableCallFrameBytes = $qualification.structureSizes.portableCallFrame
    invokeFunctionStackBytes = $invokeFrame
    continuationLoopStackBytes = $loopFrame
    activationSetupStackBytes = $pushFrame
    missingChecks = @($missing)
    guestExecution = $false
}
$auditPath = Join-Path $Evidence "js127-freestanding-audit.json"
$audit | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $auditPath
if ($missing.Count -ne 0) {
    throw "JS127 freestanding audit failed: $($missing -join ', ')"
}
Write-Output "JS127 freestanding continuation audit PASS"
Write-Output "Continuation loop frame: $loopFrame bytes; call frame: $($qualification.structureSizes.portableCallFrame) bytes"
Write-Output "Audit: $auditPath"
