param(
    [Parameter()][ValidatePattern('^[a-z0-9-]+$')][string]$RunLabel = 'r1-ordinary',
    [Parameter()][string]$EvidenceRoot = 'E:\guideXOS_C181R1_evidence'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'The C181 production orchestrator requires PowerShell Core (pwsh).'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$root = [System.IO.Path]::GetFullPath($EvidenceRoot)
$runner = Join-Path $PSScriptRoot 'run-c181-managed-application-residency.ps1'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$summaryRoot = Join-Path $root ('production-coldboots-' + $RunLabel)
$summaryPath = Join-Path $summaryRoot 'production-ordinary-manifest.json'
$candidateEsp = Join-Path $repo 'out/dotnet/c181-managed-application-residency/media/esp-production-c181-qac'
$proofOnlyMarkers = @(
    'GUIDEXOS_PROOF_CONTROL','PROOF-SCENARIO-PASS','PROOF-SCENARIO-FAIL',
    'PROOF C166','proof1.bin','GXPF','C172-COMPOSITOR-TRACE',
    'C172-NATIVE-GEOMETRY','C172-MANAGED-GEOMETRY',
    'C173-FILE-EXPLORER-TESTS','C173-MANAGED-GEOMETRY',
    'C175-NATIVE-GEOMETRY','C175-MANAGED-GEOMETRY',
    'C176-NOTES-NAVIGATION','C177-NOTES-NEWLINE','C177-SAVE-PROOF',
    'C177-STRESS','C177-PROOF-FIXTURE-COLLECTION',
    'C178-NATIVE-GEOMETRY','C178-CALCULATOR-LAUNCH','C178-CALC-GEOMETRY',
    'C178-CALC-STATE','C178-CALCULATOR-RESIZE',
    'C179-NATIVE-GEOMETRY','C179-TM-LAUNCH','C179-TM-GEOMETRY',
    'C180-MANAGED-GEOMETRY')
if (Test-Path -LiteralPath $summaryRoot) { throw "Refusing to overwrite production evidence: $summaryRoot" }
if (-not (Test-Path -LiteralPath $qacCli -PathType Leaf)) { throw "QAC CLI is unavailable: $qacCli" }
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing C181 runner: $runner" }
New-Item -ItemType Directory -Force -Path $summaryRoot | Out-Null

function Get-ArtifactHash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Find-ProofOnlyMarkers([string]$Path) {
    $text = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($Path))
    return @($proofOnlyMarkers | Where-Object {
        $text.IndexOf($_,[System.StringComparison]::OrdinalIgnoreCase) -ge 0
    })
}

function Get-BootArtifacts([string]$BootPath) {
    $files = [System.Collections.Generic.List[object]]::new()
    foreach ($name in @('serial.log','qmp.log','managed-identities.json','resize-evidence.json','lifecycle-evidence.json')) {
        $path = Join-Path $BootPath $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $files.Add([ordered]@{path=$path;sha256=(Get-ArtifactHash $path)})
        }
    }
    foreach ($shot in (Get-ChildItem -LiteralPath $BootPath -Filter '*.ppm' -File | Sort-Object Name)) {
        $files.Add([ordered]@{path=$shot.FullName;sha256=(Get-ArtifactHash $shot.FullName)})
    }
    return ,$files.ToArray()
}

$statusBefore = (& $qacCli status 2>&1 | Out-String)
$listBefore = (& $qacCli list 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $summaryRoot 'qac-before.status.txt') -Value $statusBefore -Encoding utf8
Set-Content -LiteralPath (Join-Path $summaryRoot 'qac-before.list.txt') -Value $listBefore -Encoding utf8
if ($statusBefore -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or $listBefore -notmatch 'Reservations:\s+0') {
    throw 'QAC census is not empty; no production boot was launched.'
}

$candidateProofScans = [System.Collections.Generic.List[object]]::new()
foreach ($relativePath in @('kernel.elf','ramdisk.img','EFI/BOOT/BOOTX64.EFI')) {
    $candidatePath = Join-Path $candidateEsp $relativePath
    if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        throw "Production candidate is missing $relativePath."
    }
    $markerMatches = @(Find-ProofOnlyMarkers $candidatePath)
    $candidateProofScans.Add([ordered]@{
        path=$candidatePath;sha256=(Get-ArtifactHash $candidatePath)
        proofOnlyMarkersAbsent=($markerMatches.Count -eq 0);matchedMarkers=$markerMatches
    })
    if ($markerMatches.Count -gt 0) {
        throw "Production candidate $relativePath contains proof-only markers: $($markerMatches -join ', ')."
    }
}

$powershell = (Get-Command pwsh.exe -ErrorAction Stop).Source
$bootResults = [System.Collections.Generic.List[object]]::new()
$candidateHashes = $null
$failure = $null
try {
    foreach ($bootNumber in 1..3) {
        $childLabel = $RunLabel + '-boot-' + ('0{0}' -f $bootNumber)
        $childArgs = @(
            '-NoProfile','-ExecutionPolicy','Bypass','-File',$runner,
            '-Lane','production','-RunLabel',$childLabel,
            '-EvidenceRoot',$root,'-SingleBootNumber',[string]$bootNumber)
        $childOutput = & $powershell @childArgs 2>&1
        $childExit = $LASTEXITCODE
        $childLaneRoot = Join-Path $root ('guest-runs/production-' + $childLabel + '-qac-sequence')
        Set-Content -LiteralPath (Join-Path $childLaneRoot 'orchestrator.stdout-stderr.log') -Value ($childOutput | Out-String) -Encoding utf8
        if ($childExit -ne 0) { throw "Production cold boot $bootNumber failed in its QAC lane; exit=$childExit evidence=$childLaneRoot." }

        $childManifestPath = Join-Path $childLaneRoot 'sequence-manifest.json'
        $childManifest = Get-Content -LiteralPath $childManifestPath -Raw | ConvertFrom-Json
        if (-not $childManifest.qacReleaseVerified -or $childManifest.bootReadyMarkers -ne 1 -or $childManifest.scenarioBootNumber -ne $bootNumber) {
            throw "Production cold boot $bootNumber did not pass its single-boot QAC manifest checks."
        }
        if ($childManifest.qmpResetEvents -ne 0) { throw "Production cold boot $bootNumber unexpectedly used a warm reset." }
        if ($null -eq $candidateHashes) {
            $candidateHashes = $childManifest.candidateMediaHashes
        } else {
            foreach ($relativePath in $candidateHashes.PSObject.Properties.Name) {
                $expectedHash = $candidateHashes.PSObject.Properties[$relativePath].Value
                $actualHash = $childManifest.candidateMediaHashes.PSObject.Properties[$relativePath].Value
                if ($expectedHash -ne $actualHash) {
                    throw "Production cold boots did not use a consistent candidate image: $relativePath."
                }
            }
        }

        $bootPath = Join-Path $childLaneRoot ('boot' + $bootNumber)
        $serialPath = Join-Path $bootPath 'serial.log'
        $serialText = Get-Content -LiteralPath $serialPath -Raw
        $serialProofMarkers = @(Find-ProofOnlyMarkers $serialPath)
        $proofOnlyDiagnosticsAbsent = $serialProofMarkers.Count -eq 0
        if (-not $proofOnlyDiagnosticsAbsent) { throw "Production cold boot $bootNumber emitted proof-only diagnostics: $($serialProofMarkers -join ', ')." }

        $bootResults.Add([ordered]@{
            bootNumber=$bootNumber
            mode='independent-QAC-cold-boot'
            laneRoot=$childLaneRoot
            sequenceManifest=$childManifestPath
            qacReservationId=$childManifest.qacReservationId
            qemuPid=$childManifest.qemuPid
            readyMarkers=$childManifest.bootReadyMarkers
            normalAppLaunches=$childManifest.normalAppLaunches
            qacReleaseVerified=$childManifest.qacReleaseVerified
            qmpResetEvents=$childManifest.qmpResetEvents
            proofOnlyDiagnosticsAbsent=$proofOnlyDiagnosticsAbsent
            matchedProofOnlySerialMarkers=$serialProofMarkers
            candidateProofScans=$candidateProofScans.ToArray()
            candidateMediaHashes=$childManifest.candidateMediaHashes
            isolatedBootedMediaHashes=$childManifest.isolatedBootedMediaHashes
            artifactHashes=(Get-BootArtifacts $bootPath)
        })
        Write-Output ("PRODUCTION_COLD_BOOT=" + $bootNumber + " QAC_RELEASE=VERIFIED READY=PASS")
    }
} catch {
    $failure = $_.Exception.Message
} finally {
    $manifest = [ordered]@{
        lane='production'
        runLabel=$RunLabel
        bootPolicy='three independent QAC-launched QEMU cold boots'
        expectedBoots=3
        completedBoots=$bootResults.Count
        outcome=if ($bootResults.Count -eq 3 -and -not $failure) {'PASS'} else {'INCOMPLETE'}
        failure=$failure
        qacPreflightStatus=$statusBefore
        qacPreflightList=$listBefore
        candidateProofScans=$candidateProofScans.ToArray()
        candidateMediaHashes=$candidateHashes
        boots=$bootResults
    }
    $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding utf8
}

if ($failure) { throw $failure }
Write-Output ("PRODUCTION_COLD_BOOT_SEQUENCE=PASS BOOTS=" + $bootResults.Count + " MANIFEST=" + $summaryPath)
