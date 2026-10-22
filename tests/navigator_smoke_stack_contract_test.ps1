param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\kernel\core\kernel_apps.cpp')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedSource = (Resolve-Path -LiteralPath $SourcePath).Path
$source = [System.IO.File]::ReadAllText($resolvedSource)

$automaticNavigatorApps = [regex]::Matches(
    $source,
    '(?m)^\s*NavigatorApp\s+[A-Za-z_][A-Za-z0-9_]*\s*;'
)
if ($automaticNavigatorApps.Count -gt 0) {
    $lineNumbers = foreach ($match in $automaticNavigatorApps) {
        1 + ([regex]::Matches($source.Substring(0, $match.Index), "`n")).Count
    }
    throw "NavigatorApp must not be an automatic kernel-stack object; declarations found at line(s): $($lineNumbers -join ', ')."
}

if ($source -notmatch 'NavigatorSmokeAppScope\(\)\s*:\s*app_\s*\(\s*new NavigatorApp\(\)\s*\)') {
    throw 'Navigator smoke fixtures must acquire NavigatorApp through the heap-backed scope.'
}
if ($source -notmatch '~NavigatorSmokeAppScope\(\)\s*\{\s*delete app_;\s*\}') {
    throw 'Navigator smoke fixture ownership must release its heap allocation at scope exit.'
}

$scopedFixtures = [regex]::Matches($source, '(?m)^\s*NavigatorSmokeAppScope\s+appScope\s*;')
if ($scopedFixtures.Count -lt 7) {
    throw "Expected all seven existing Navigator smoke fixtures to use the heap-backed scope; found $($scopedFixtures.Count)."
}

Write-Output "Navigator smoke stack contract PASS ($($scopedFixtures.Count) heap-scoped fixtures; no automatic NavigatorApp objects)."
