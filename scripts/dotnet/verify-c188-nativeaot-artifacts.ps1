param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [Parameter(Mandatory=$true)][string]$EvidenceRoot,
    [Parameter()][switch]$NotesStartupDiagnostics,
    [Parameter()][switch]$NotesStartupOperationIsolation,
    [Parameter()][string]$ReadElfPath = 'C:\mingw64\bin\readelf.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$isStartupDiagnostic = $NotesStartupDiagnostics -or $NotesStartupOperationIsolation
if ($NotesStartupDiagnostics -and $NotesStartupOperationIsolation) { throw 'Choose one C188 Notes startup diagnostic phase.' }
$phaseRootName = if ($NotesStartupOperationIsolation) { 'out/dotnet/c188r4-notes-startup-operation' } elseif ($NotesStartupDiagnostics) { 'out/dotnet/c188r3-notes-startup-diagnostics' } else { 'out/dotnet/c188-external-app-identity' }
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo $phaseRootName))
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$evidence = [System.IO.Path]::GetFullPath($EvidenceRoot)
foreach ($path in @($candidate,$evidence)) {
    if (-not $path.StartsWith($phaseRoot,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw "C188 paths must remain under $phaseRoot : $path"
    }
}
if (-not (Test-Path -LiteralPath $candidate -PathType Container)) { throw "Missing candidate root: $candidate" }
if (-not (Test-Path -LiteralPath $ReadElfPath -PathType Leaf)) { throw "Missing ELF validator: $ReadElfPath" }
$null = New-Item -ItemType Directory -Force -Path $evidence

$artifacts = [ordered]@{
    elf=Join-Path $candidate 'artifacts/HostLogProof.elf'
    pe=Join-Path $candidate 'artifacts/HostLogProof.exe'
    map=Join-Path $candidate 'artifacts/HostLogProof.map'
    counterApp=Join-Path $candidate 'bin/release/GuideXos.C185.CounterApp.dll'
    contracts=Join-Path $candidate 'bin/release/GuideXos.ManagedUi.Contracts.dll'
}
foreach ($entry in $artifacts.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) { throw "Missing NativeAOT artifact $($entry.Key): $($entry.Value)" }
}
$elfBytes = [System.IO.File]::ReadAllBytes($artifacts.elf)
$peBytes = [System.IO.File]::ReadAllBytes($artifacts.pe)
if ($elfBytes.Length -lt 64 -or $elfBytes[0] -ne 0x7f -or $elfBytes[1] -ne 0x45 -or $elfBytes[2] -ne 0x4c -or $elfBytes[3] -ne 0x46) {
    throw 'Candidate ELF has an invalid ELF identification header.'
}
if ($elfBytes[4] -ne 2 -or $elfBytes[5] -ne 1 -or $peBytes[0] -ne 0x4d -or $peBytes[1] -ne 0x5a) {
    throw 'Expected an ELF64 little-endian ELF and a PE image.'
}

$mapText = [System.IO.File]::ReadAllText($artifacts.map)
$requiredSymbols = @(
    'CounterApplicationFactory__Create',
    'CounterApplication___ctor',
    'CounterApplication__HandleAction',
    'CounterApplication__HandleInput',
    'CounterApplication__HandleResize',
    'CounterApplication__OnTearingDown',
    'DispatchC188ExternalAppIdentity',
    'GuideXosNativeAotApplicationEntry'
)
$symbolEvidence = [ordered]@{}
foreach ($symbol in $requiredSymbols) {
    $symbolEvidence[$symbol] = $mapText.Contains($symbol)
    if (-not $symbolEvidence[$symbol]) { throw "NativeAOT link map omitted required retained symbol $symbol" }
}
$elfText = [System.Text.Encoding]::ASCII.GetString($elfBytes)
foreach ($marker in @('C188-FACTORY selector=9 identity=com.guidexos.apps.managed.counterapp','C188-WINDOW selector=9 identity=com.guidexos.apps.managed.counterapp','C188-INPUT selector=9 identity=com.guidexos.apps.managed.counterapp')) {
    if (-not $elfText.Contains($marker)) { throw "C188 ELF omitted required retained marker $marker" }
}
$notesSources = @(
    (Join-Path $repo 'samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs'),
    (Join-Path $repo 'samples/managed/HostLogProof/Applications/ManagedNotes.cs')
)
$allNotesMarkers = [System.Collections.Generic.List[string]]::new()
foreach ($sourcePath in $notesSources) {
    $sourceText = [System.IO.File]::ReadAllText($sourcePath)
    foreach ($match in [regex]::Matches($sourceText,'C188R[34]-\d{3} [A-Z][A-Z0-9 =-]+')) {
        if (-not $allNotesMarkers.Contains($match.Value)) { $allNotesMarkers.Add($match.Value) }
    }
}
if ($isStartupDiagnostic) {
    foreach ($marker in $allNotesMarkers) {
        if (-not $elfText.Contains($marker)) { throw "$phaseRootName diagnostic ELF omitted required retained marker $marker" }
    }
} else {
    foreach ($marker in $allNotesMarkers) {
        if ($elfText.Contains($marker)) { throw "Ordinary C188 ELF contains proof-only startup instrumentation $marker" }
    }
}

$headerPath = Join-Path $evidence 'HostLogProof.readelf-header.txt'
$programPath = Join-Path $evidence 'HostLogProof.readelf-program-headers.txt'
$relocationPath = Join-Path $evidence 'HostLogProof.readelf-relocations.txt'
& $ReadElfPath -h $artifacts.elf 2>&1 | Set-Content -LiteralPath $headerPath -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "readelf -h failed with exit code $LASTEXITCODE" }
& $ReadElfPath -l $artifacts.elf 2>&1 | Set-Content -LiteralPath $programPath -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "readelf -l failed with exit code $LASTEXITCODE" }
& $ReadElfPath -r $artifacts.elf 2>&1 | Set-Content -LiteralPath $relocationPath -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "readelf -r failed with exit code $LASTEXITCODE" }
$header = [System.IO.File]::ReadAllText($headerPath)
$programs = [System.IO.File]::ReadAllText($programPath)
$relocations = [System.IO.File]::ReadAllText($relocationPath)
if ($header -notmatch 'Class:\s+ELF64' -or $header -notmatch 'Machine:\s+Advanced Micro Devices X86-64') {
    throw 'readelf does not identify the candidate as ELF64 x86-64.'
}
$loadCount = [regex]::Matches($programs,'(?m)^\s+LOAD\s').Count
if ($loadCount -lt 1) { throw 'readelf reported no ELF LOAD program headers.' }
if ($relocations -notmatch 'There are no relocations') { throw 'readelf reported relocations in the candidate ELF.' }

$counterProject = [System.IO.File]::ReadAllText((Join-Path $repo 'samples/managed/C185CounterApp/GuideXos.C185.CounterApp.csproj'))
$hostProject = [System.IO.File]::ReadAllText((Join-Path $repo 'samples/managed/HostLogProof/HostLogProof.csproj'))
if ($counterProject -notmatch 'ProjectReference Include="\.\.\\ManagedUiContracts\\GuideXos\.ManagedUi\.Contracts\.csproj"') {
    throw 'CounterApp project does not independently reference ManagedUiContracts.'
}
if ($hostProject -notmatch 'GuideXos\.C185\.CounterApp\.csproj' -or $hostProject -notmatch 'GuideXos\.ManagedUi\.Contracts\.csproj') {
    throw 'C188 host project is missing explicit external application/contracts references.'
}
$hostProjectXml = [System.Xml.XmlDocument]::new()
$hostProjectXml.LoadXml($hostProject)
$directCounterSources = @($hostProjectXml.SelectNodes('//Compile[@Include]') | Where-Object {
    $_.GetAttribute('Include') -match '(^|[\\/])C185CounterApp([\\/]|$)'
})
if ($directCounterSources.Count -gt 0) { throw 'CounterApp source is included directly in HostLogProof.' }

$fileEvidence = [ordered]@{}
foreach ($entry in $artifacts.GetEnumerator()) {
    $item = Get-Item -LiteralPath $entry.Value
    $fileEvidence[$entry.Key] = [ordered]@{path=$item.FullName;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash}
}
$nativeAotMode = if ($NotesStartupOperationIsolation) {
    'C160Composite+C185Boundary+C188ExternalAppIdentity+C188R4OperationIsolation'
} elseif ($NotesStartupDiagnostics) {
    'C160Composite+C185Boundary+C188ExternalAppIdentity'
} else {
    'C167ProofComposite+C185Boundary+C188ExternalAppIdentity'
}
$manifest = [ordered]@{
    result='PASS';candidateRoot=$candidate;nativeAotMode=$nativeAotMode
    elfClass='ELF64';elfMachine='x86-64';loadSegmentCount=$loadCount;relocations=0
    nativeApplicationId='com.guidexos.apps.managed.counterapp';nativeSelector=9
    productionSelectors=@{notes=4;settings=5;calculator=6;taskManager=7;fileExplorer=8}
    requiredRetainedSymbols=$symbolEvidence;notesStartupDiagnostics=$isStartupDiagnostic;notesStartupOperationIsolation=[bool]$NotesStartupOperationIsolation;retainedNotesMarkers=$(if ($isStartupDiagnostic) { @($allNotesMarkers) } else { @() });artifacts=$fileEvidence
    readelfEvidence=[ordered]@{header=$headerPath;programHeaders=$programPath;relocations=$relocationPath}
}
$manifestPath = Join-Path $evidence 'nativeaot-qualification.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188_NATIVEAOT_VALIDATION=PASS ELF64 LOADS=$loadCount RELOCATIONS=0 RETAINED_SYMBOLS=$($requiredSymbols.Count)"
Write-Output "C188_NATIVEAOT_MANIFEST=$manifestPath"
