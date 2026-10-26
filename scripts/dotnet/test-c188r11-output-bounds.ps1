$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$managedHost = Get-Content -LiteralPath (Join-Path $repo 'samples/managed/HostLogProof/GuideXos/GuideXosHost.cs') -Raw
$managedRegistry = Get-Content -LiteralPath (Join-Path $repo 'samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs') -Raw
$managedLifetime = Get-Content -LiteralPath (Join-Path $repo 'samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs') -Raw
$kernel = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/nativeaot_application.cpp') -Raw
$fixture = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'fixtures/c188r11/state-provenance.serial.txt') -Raw
$checks = 0

$managedLimit = '(?s)#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE\s+const int maximumMessageLength = 255;\s+#else\s+const int maximumMessageLength = 127;\s+#endif\s+if \(text.Length == 0 \|\| text.Length > maximumMessageLength\)'
if (-not [regex]::IsMatch($managedHost, $managedLimit)) {
    throw 'Managed C188R11 logging must reject messages over 255 bytes while preserving the 127-byte limit in other builds.'
}
$checks++

if (-not [regex]::IsMatch($managedRegistry,
        '(?s)C188R11WriteEvents.*?C188R11LastWriteSite.*?GC\.CollectionCount\(0\)') -or
    -not [regex]::IsMatch($managedLifetime,
        '(?s)C188R11WriteEvents.*?C188R11LastWriteSite.*?RecordC188R11Write\(1u\).*?RecordC188R11Write\(2u\).*?RecordC188R11Write\(3u\)')) {
    throw 'C188R11 snapshots must include read-only GC count and bounded pool-writer provenance for all three mutation sites.'
}
$checks++

$nativeLimit = '(?s)#if defined\(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE\)\s+constexpr uint32_t managedLogMessageLimit = 256u;\s+#else\s+constexpr uint32_t managedLogMessageLimit = 128u;\s+#endif\s+for \(uint32_t index = 0;\s+index < managedLogMessageLimit && message\[index\] != 0; \+\+index\)'
if (-not [regex]::IsMatch($kernel, $nativeLimit)) {
    throw 'Native managedLog must scan at most 256 bytes for C188R11 and retain the 128-byte limit in other builds.'
}
$checks++

if (-not [regex]::IsMatch($managedRegistry,
        '(?s)C188R11SequenceCounter s_counter = new\(128u\).*?s_c188r11RegistryTraceRecords >= 48u')) {
    throw 'Managed C188R11 sequence and state-trace record counts must remain bounded.'
}
$checks++

$prefix = '[C102-MANAGED-OUTPUT] '
$fixtureRows = @($fixture -split "`r?`n" | Where-Object { $_.StartsWith($prefix + 'C188R11') })
if ($fixtureRows.Count -eq 0) { throw 'C188R11 output-bound fixture contains no managed diagnostic records.' }
$fixtureBodyLengths = @($fixtureRows | ForEach-Object {
    [System.Text.Encoding]::UTF8.GetByteCount($_.Substring($prefix.Length))
})
$maxFixtureBodyLength = ($fixtureBodyLengths | Measure-Object -Maximum).Maximum
if (@($fixtureBodyLengths | Where-Object { $_ -gt 255 }).Count -ne 0) {
    throw "C188R11 fixture includes a managed message above the 255-byte cap (maximum $maxFixtureBodyLength)."
}
$checks++

Write-Output "C188R11-OUTPUT-BOUNDS cases=$checks result=PASS maxFixtureBodyBytes=$maxFixtureBodyLength"
