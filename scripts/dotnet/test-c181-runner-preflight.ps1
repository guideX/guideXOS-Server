$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$artifactRoot = Join-Path $repo 'out/dotnet/c181-managed-application-residency'
$runnerPath = Join-Path $PSScriptRoot 'run-c181-managed-application-residency.ps1'
$runnerText = [System.IO.File]::ReadAllText($runnerPath)
$expectedProofKernelHash = 'B360905ED40EEDDA32E14851E5E980C7204D427F21ABE200C8D4FBBE61832C63'
$proofKernelPath = Join-Path $artifactRoot 'kernel-proof-c181/bin/kernel.elf'
$focusedRamdiskPath = Join-Path $artifactRoot 'media/proof-app-c181-r1/ramdisk.img'
$expectedFocusedRamdiskHash = 'F61BF2A3012D298762A94DA64A48AAC2382632C39068D8C8860E3255D4E50E28'

$tokens = $null
$parseErrors = $null
$runnerAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $runnerPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
    throw ('C181 runner parse failed: ' + (($parseErrors | ForEach-Object Message) -join '; '))
}

$expectedProofPathPattern = '(?s)\$proofKernelPath\s*=\s*if\s*\(\$Lane\s*-eq\s*''focused''\)\s*\{\s*Join-Path\s+\$artifactRoot\s+''kernel-proof-c181/bin/kernel\.elf'''
if ($runnerText -notmatch $expectedProofPathPattern) {
    throw 'Focused lane no longer selects the recorded kernel-proof-c181 build output.'
}
if ($runnerText.Contains('media/proof-esp/kernel.elf')) {
    throw 'Focused lane still references the non-C181 proof-ESP kernel.'
}
if (-not $runnerText.Contains($expectedProofKernelHash)) {
    throw 'Focused lane no longer pins the recorded C181 proof-kernel hash.'
}

$actualProofKernelHash = (Get-FileHash -LiteralPath $proofKernelPath -Algorithm SHA256).Hash
if ($actualProofKernelHash -ne $expectedProofKernelHash) {
    throw "C181 proof-kernel artifact drift: expected $expectedProofKernelHash; found $actualProofKernelHash."
}
$proofKernelText = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($proofKernelPath))
foreach ($marker in @('C171-WINDOW-CREATE','C172-COMPOSITOR-TRACE','C175-NATIVE-GEOMETRY')) {
    if (-not $proofKernelText.Contains($marker)) {
        throw "Recorded C181 proof kernel is missing required marker $marker."
    }
}

if (-not $runnerText.Contains('media/esp-focused-c181-qac')) {
    throw 'Focused lane no longer selects the isolated focused ESP base and loader.'
}
if (-not $runnerText.Contains("Join-Path `$artifactRoot 'media/proof-app-c181-r1/ramdisk.img'")) {
    throw 'Focused lane no longer selects the C181-specific proof-enabled app ramdisk.'
}
$actualFocusedRamdiskHash = (Get-FileHash -LiteralPath $focusedRamdiskPath -Algorithm SHA256).Hash
if ($actualFocusedRamdiskHash -ne $expectedFocusedRamdiskHash) {
    throw "Focused proof app ramdisk artifact drift: expected $expectedFocusedRamdiskHash; found $actualFocusedRamdiskHash."
}
$proofRamdiskText = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($focusedRamdiskPath))
foreach ($marker in @(
    'C172-MANAGED-GEOMETRY','C173-MANAGED-GEOMETRY','C175-MANAGED-GEOMETRY',
    'C178-CALC-GEOMETRY','C179-TM-GEOMETRY')) {
    if (-not $proofRamdiskText.Contains($marker)) {
        throw "Focused proof app ramdisk is missing required marker $marker."
    }
}
if (-not $runnerText.Contains('Copy-Item -LiteralPath $proofAppRamdiskPath -Destination (Join-Path $esp ''ramdisk.img'') -Force')) {
    throw 'Focused lane no longer stages the proof-enabled app ramdisk into isolated media.'
}
if ($proofRamdiskText.Contains('C162-CLOSE-NATIVE-BOUNDARY')) {
    throw 'Focused C181 proof ramdisk unexpectedly includes the unrelated C162 close-boundary proof.'
}
if ($runnerText.Contains('SingleBootNumber is reserved for independent ordinary production boots.')) {
    throw 'C181 single-cold-boot diagnostic mode is still restricted to production.'
}
if (-not $runnerText.Contains("'single-cold-boot-diagnostic'")) {
    throw 'C181 manifest no longer distinguishes single-boot diagnostics from three-boot acceptance.'
}
if (-not $runnerText.Contains('disp=56 phase=3 op=0 len=2 focus=18 result=PASS')) {
    throw 'C181 Calculator resize assertion does not match the completed 7*8 state, whose pending operator is None (0).'
}
if (-not $runnerText.Contains('boots=@($bootNumbers).Count')) {
    throw 'C181 runner does not safely count a one-boot diagnostic when building its manifest.'
}

$widthResolverAst = $runnerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Resolve-ResidentWidth'
}, $true)
if ($null -eq $widthResolverAst) {
    throw 'C181 runner no longer defines the measured-width resolver used by Notes close/reopen.'
}
Invoke-Expression $widthResolverAst.Extent.Text
foreach ($case in @(
    @{Measured=562; Expected=560},
    @{Measured=602; Expected=600}
)) {
    $resolvedWidth = Resolve-ResidentWidth $case.Measured @($case.Expected)
    if ($resolvedWidth -ne $case.Expected) {
        throw "C181 Notes width resolver returned $resolvedWidth for measured $($case.Measured); expected $($case.Expected)."
    }
}
$invalidWidthRejected = $false
try { [void](Resolve-ResidentWidth 548 @(560,600)) } catch { $invalidWidthRejected = $true }
if (-not $invalidWidthRejected) {
    throw 'C181 Notes width resolver accepted geometry outside the target and default widths.'
}
if (-not $runnerText.Contains('$currentWidth = $reopenedWidth')) {
    throw 'C181 Boot 3 does not use the measured Notes geometry for the next close cycle.'
}
if (-not $runnerText.Contains('-ExpectedWidth $notesSpec.Width')) {
    throw 'C181 Boot 3 does not validate fresh Notes generations at the documented default width.'
}
if (-not $runnerText.Contains('-ExpectedWidth $notesSpec.TargetWidth')) {
    throw 'C181 Boot 3 does not validate the post-resize Notes refocus against its measured target width.'
}
$settingsLaunchAst = $runnerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Launch-SettingsFromNotes'
}, $true)
if ($null -eq $settingsLaunchAst -or
    -not $settingsLaunchAst.Extent.Text.Contains("Open-AppMenuItem 'notes' `$BootNumber")) {
    throw 'C181 Settings setup no longer focuses the startup Notes surface at its normal initial size.'
}

$qemuArgsAssignment = $runnerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left.Extent.Text -eq '$qemuArgs'
}, $true)
if ($null -eq $qemuArgsAssignment) { throw 'C181 runner QEMU argument array was not found.' }
$qemuLiteralArguments = @($qemuArgsAssignment.Right.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.StringConstantExpressionAst]
}, $true) | ForEach-Object Value)
if ($qemuLiteralArguments -contains '-no-reboot') {
    throw 'C181 runner reintroduced the QEMU -no-reboot option.'
}
if ($qemuLiteralArguments -notcontains '-no-shutdown') {
    throw 'C181 runner no longer keeps QEMU available for QMP cleanup.'
}
foreach ($guard in @(
    "Invoke-Qmp 'system_reset'",
    'reset-event-observation=RESET_SEEN',
    "Invoke-Qmp 'query-status'",
    'Wait-ReadyCount $boot 180 $serialOffset',
    "Invoke-Qmp 'quit'",
    'WaitForExit(90000)',
    'Refusing to overwrite sequence evidence',
    'Copy-Item -LiteralPath $baseEsp -Destination $esp -Recurse'
)) {
    if (-not $runnerText.Contains($guard)) {
        throw "C181 runner lost a required reset, timeout, cleanup, or evidence-isolation guard: $guard"
    }
}

Write-Output 'C181 runner media/reset preflight: PASS (C181 proof kernel and app payload, reset, timeout, cleanup, and isolated evidence guards).'
