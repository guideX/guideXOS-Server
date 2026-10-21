param(
    [ValidateSet('RX12R1','RX13','RX14','RX15','RX16','RX17','RX18','RX19','RX20','RX21')]
    [string]$Phase = 'RX12R1',
    [string]$MakeExecutable = '',
    [string]$OutputEvidenceRoot = ''
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ((git -C $rootDir branch --show-current).Trim() -ne 'REXX_SUPPORT') {
    throw "$Phase native qualification requires branch REXX_SUPPORT."
}
$kernelDir = Join-Path $rootDir 'kernel'
$phaseRoot = Join-Path $rootDir "build\rexx\$($Phase.ToLowerInvariant())"
$guardManifest = if ($Phase -eq 'RX21') {
    Join-Path $rootDir 'docs\rexx\evidence\rx21\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX19') {
    Join-Path $rootDir 'docs\rexx\evidence\rx19\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX20') {
    Join-Path $rootDir 'docs\rexx\evidence\rx20\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX18') {
    Join-Path $rootDir 'docs\rexx\evidence\rx18\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX17') {
    Join-Path $rootDir 'docs\rexx\evidence\rx17\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX16') {
    Join-Path $rootDir 'docs\rexx\evidence\rx16\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX15') {
    Join-Path $rootDir 'docs\rexx\evidence\rx15\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX14') {
    Join-Path $rootDir 'docs\rexx\evidence\rx14\protected-artifacts-start.json'
} else {
    Join-Path $rootDir 'docs\rexx\evidence\rx12r1\protected-artifacts-baseline.json'
}
Import-Module -Name (Join-Path $PSScriptRoot 'rexx_artifact_guard.psm1') -Force

if ([string]::IsNullOrWhiteSpace($MakeExecutable)) {
    $makeCommand = Get-Command mingw32-make -ErrorAction SilentlyContinue
    if ($null -eq $makeCommand) { $makeCommand = Get-Command make -ErrorAction SilentlyContinue }
    if ($null -eq $makeCommand) { throw 'GNU Make is required for the AMD64 kernel build.' }
    $makeExecutable = $makeCommand.Source
} else {
    $makeExecutable = [System.IO.Path]::GetFullPath($MakeExecutable)
    if (-not (Test-Path -LiteralPath $makeExecutable -PathType Leaf)) { throw "GNU Make executable not found: $makeExecutable" }
}

$requiredTools = @('g++','ld','nasm','objcopy','nm','python','powershell')
$toolMap = @{}
foreach ($toolName in $requiredTools) {
    $tool = Get-Command $toolName -ErrorAction SilentlyContinue
    if ($null -eq $tool) { throw "Required AMD64 build tool is unavailable: $toolName" }
    $toolMap[$toolName] = $tool.Source
}
$versionResult = & $makeExecutable --version 2>&1
if ($LASTEXITCODE -ne 0) { throw "GNU Make version probe failed: $versionResult" }
$makeVersion = ($versionResult | Select-Object -First 1).ToString()

$runId = [Guid]::NewGuid().ToString('N')
$runName = "native-amd64-$runId"
$runRoot = Join-Path $phaseRoot $runName
$workDir = Join-Path $runRoot 'temp'
$buildRootArgument = "../build/rexx/$($Phase.ToLowerInvariant())/$runName"
$buildDir = Join-Path $runRoot 'amd64'
$plannedOutputs = [System.Collections.Generic.List[string]]::new()
$runPrefix = $runRoot.TrimEnd([char[]]@('\','/')) + [System.IO.Path]::DirectorySeparatorChar

function Test-NativeOutputOwned([string]$Path) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    return $fullPath.StartsWith($script:runPrefix, [System.StringComparison]::OrdinalIgnoreCase)
}

# Resolve the full possible source-to-output map from the reviewed AMD64 Makefile
# rules. Every compiler, assembler, linker, and objcopy target is rooted below
# BUILD_ROOT; compiler -MMD sidecars are adjacent to their explicit -o .o files.
$sourceMap = [System.Collections.Generic.List[object]]::new()
function Add-NativeSourceOutputs([string]$Source, [string]$ObjectRelativePath, [bool]$DependencyFile) {
    $objectPath = Join-Path $buildDir $ObjectRelativePath
    if (-not (Test-NativeOutputOwned $objectPath)) { throw "Native output map escaped its run root: $objectPath" }
    $null = $plannedOutputs.Add($objectPath)
    if ($DependencyFile) { $null = $plannedOutputs.Add([System.IO.Path]::ChangeExtension($objectPath, '.d')) }
    $sourceMap.Add([PSCustomObject]@{ source = $Source; object = $objectPath; dependency = $DependencyFile })
}
foreach ($source in Get-ChildItem -LiteralPath (Join-Path $kernelDir 'core') -Filter '*.cpp' -File | Sort-Object FullName) {
    Add-NativeSourceOutputs $source.FullName (Join-Path 'obj\core' ($source.BaseName + '.o')) $true
}
foreach ($source in Get-ChildItem -LiteralPath (Join-Path $kernelDir 'shell') -Filter '*.cpp' -File | Sort-Object FullName) {
    Add-NativeSourceOutputs $source.FullName (Join-Path 'obj\shell' ($source.BaseName + '.o')) $true
}
foreach ($source in Get-ChildItem -LiteralPath (Join-Path $kernelDir 'arch\amd64') -File | Where-Object { $_.Extension -in '.cpp','.asm','.s' } | Sort-Object FullName) {
    Add-NativeSourceOutputs $source.FullName (Join-Path 'obj\arch' ($source.BaseName + '.o')) ($source.Extension -eq '.cpp')
}
$thirdPartyRoot = [System.IO.Path]::GetFullPath((Join-Path $rootDir 'third_party')).TrimEnd([char[]]@('\','/')) + [System.IO.Path]::DirectorySeparatorChar
foreach ($source in Get-ChildItem -LiteralPath (Join-Path $rootDir 'third_party\mbedtls') -Filter '*.c' -File -Recurse | Sort-Object FullName) {
    $relative = [System.IO.Path]::GetFullPath($source.FullName).Substring($thirdPartyRoot.Length)
    Add-NativeSourceOutputs $source.FullName (Join-Path 'obj\third_party' ([System.IO.Path]::ChangeExtension($relative, '.o'))) $true
}
foreach ($sourceName in @('rexx_lexer.c','rexx_parser.c','rexx_compiler.c','rexx_vm.c','rexx_builtin.c','rexx_numeric.c','rexx_output.c','rexx_os_io.c','rexx_cli.c')) {
    Add-NativeSourceOutputs (Join-Path $rootDir $sourceName) (Join-Path 'obj\rexx' ([System.IO.Path]::ChangeExtension($sourceName, '.o'))) $true
}
$null = $plannedOutputs.Add((Join-Path (Join-Path $buildDir 'bin') 'kernel.pe'))
$null = $plannedOutputs.Add((Join-Path (Join-Path $buildDir 'bin') 'kernel.elf'))
$plannedOutputs = @($plannedOutputs | Sort-Object -Unique)

$null = Start-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot `
    -WorkingDirectory $workDir -PlannedOutputs $plannedOutputs -Phase $Phase
$env:TEMP = $workDir
$env:TMP = $workDir
$env:TMPDIR = $workDir
Set-Location -LiteralPath $workDir

$evidenceRoot = if ([string]::IsNullOrWhiteSpace($OutputEvidenceRoot)) {
    Join-Path $rootDir "docs\rexx\evidence\$($Phase.ToLowerInvariant())\native-build"
} else {
    [System.IO.Path]::GetFullPath($OutputEvidenceRoot)
}
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$auditPath = Join-Path $evidenceRoot ("output-audit-$runId.json")
[PSCustomObject]@{
    schema = 'guidexos.rexx.native-output-audit.v1'
    audited_at_utc = [DateTime]::UtcNow.ToString('o')
    repository = $rootDir
    branch = (git -C $rootDir branch --show-current).Trim()
    head = (git -C $rootDir rev-parse HEAD).Trim()
    make = $makeExecutable
    make_version = $makeVersion
    build_root_argument = $buildRootArgument
    run_root = $runRoot
    working_directory = $workDir
    required_tools = $toolMap
    output_paths = $plannedOutputs
    source_output_map = @($sourceMap)
    audited_makefile_rules = @(Select-String -LiteralPath (Join-Path $kernelDir 'Makefile') -Pattern 'BUILD_ROOT|BUILD_DIR|OBJ_DIR|BIN_DIR|DEPFLAGS| -o \$@|\$< \$@|kernel\.elf|kernel\.pe' | ForEach-Object { "line $($_.LineNumber): $($_.Line.Trim())" })
    no_clean_target_invoked = $true
    production_esp_or_disk_paths_in_build_plan = $false
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $auditPath -Encoding utf8

$stdoutPath = Join-Path $evidenceRoot ("native-build-$runId.stdout.log")
$stderrPath = Join-Path $evidenceRoot ("native-build-$runId.stderr.log")
$statusPath = Join-Path $evidenceRoot ("native-build-$runId.status.json")
$makeArguments = @('-C', $kernelDir, '-B', 'ARCH=amd64', "BUILD_ROOT=$buildRootArgument", 'all')
$process = Start-Process -FilePath $makeExecutable -ArgumentList $makeArguments `
    -WorkingDirectory $workDir -NoNewWindow -PassThru -Wait `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$status = [PSCustomObject]@{
    exit_code = $process.ExitCode
    pid = $process.Id
    started_at_utc = $process.StartTime.ToUniversalTime().ToString('o')
    finished_at_utc = $process.ExitTime.ToUniversalTime().ToString('o')
    arguments = $makeArguments
}
$status | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statusPath -Encoding utf8
if ($process.ExitCode -ne 0) { throw "Isolated AMD64 make failed with exit code $($process.ExitCode). Raw logs: $stdoutPath and $stderrPath" }

Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot -Stage 'native-amd64-build' -Phase $Phase | Out-Null
$kernelElf = Join-Path (Join-Path $buildDir 'bin') 'kernel.elf'
$kernelPe = Join-Path (Join-Path $buildDir 'bin') 'kernel.pe'
if (-not (Test-Path -LiteralPath $kernelElf -PathType Leaf) -or -not (Test-Path -LiteralPath $kernelPe -PathType Leaf)) {
    throw "Native link outputs are missing from the isolated run: $buildDir"
}
$elfHash = (Get-FileHash -LiteralPath $kernelElf -Algorithm SHA256).Hash.ToLowerInvariant()
$elfHashSecond = (Get-FileHash -LiteralPath $kernelElf -Algorithm SHA256).Hash.ToLowerInvariant()
if ($elfHash -ne $elfHashSecond) { throw 'kernel.elf changed during its recorded SHA-256 verification.' }
$rexxObjectDirectory = Join-Path (Join-Path $buildDir 'obj') 'rexx'
$rexxObjectCount = @(Get-ChildItem -LiteralPath $rexxObjectDirectory -Filter '*.o' -File).Count
if ($rexxObjectCount -ne 9) { throw "Expected nine REXX C object files; found $rexxObjectCount in $rexxObjectDirectory." }
$previousPhaseReference = $null
if ($Phase -eq 'RX14') {
    $previousEvidenceDirectory = Join-Path $rootDir 'docs\rexx\evidence\rx13\native-build'
    $previousEvidenceFile = Get-ChildItem -LiteralPath $previousEvidenceDirectory `
        -Filter 'native-link-evidence-*.json' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $previousEvidenceFile) {
        throw "RX13 native-link evidence is required to record the RX14 successor comparison: $previousEvidenceDirectory"
    }
    $previousEvidence = Get-Content -Raw -LiteralPath $previousEvidenceFile.FullName | ConvertFrom-Json
    $previousPhaseReference = [PSCustomObject]@{
        phase = 'RX13'
        evidence = $previousEvidenceFile.FullName
        kernel_elf_sha256 = [string]$previousEvidence.kernel_elf_sha256
        matches_previous_phase = $elfHash -eq [string]$previousEvidence.kernel_elf_sha256
    }
}
$linkEvidence = [PSCustomObject]@{
    schema = 'guidexos.rexx.native-link-evidence.v1'
    built_at_utc = [DateTime]::UtcNow.ToString('o')
    kernel_elf = $kernelElf
    kernel_elf_sha256 = $elfHash
    kernel_elf_sha256_reverified = $elfHashSecond
    kernel_pe = $kernelPe
    kernel_pe_sha256 = (Get-FileHash -LiteralPath $kernelPe -Algorithm SHA256).Hash.ToLowerInvariant()
    rexx_c_object_count = $rexxObjectCount
    rexx_c_objects = @(Get-ChildItem -LiteralPath $rexxObjectDirectory -Filter '*.o' -File | Sort-Object Name | ForEach-Object { $_.Name })
    freestanding_flags = @('-ffreestanding','-nostdlib','-nostdinc++','-fno-builtin','-fno-exceptions','-fno-rtti')
    historical_rx12_elf_sha256 = 'ce835ef3b77aa066db46f9cb9964b6bb0d7c589b4202a2b1965129854fd80dee'
    matches_historical_rx12 = $elfHash -eq 'ce835ef3b77aa066db46f9cb9964b6bb0d7c589b4202a2b1965129854fd80dee'
    previous_phase_reference = $previousPhaseReference
    build_output_root = $runRoot
    build_succeeded = $true
}
$linkEvidencePath = Join-Path $evidenceRoot ("native-link-evidence-$runId.json")
$linkEvidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $linkEvidencePath -Encoding utf8
Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot -Stage 'native-amd64-final' -Phase $Phase | Out-Null
$preservationEvidence = Join-Path $evidenceRoot ("preservation-$runId")
New-Item -ItemType Directory -Path $preservationEvidence | Out-Null
Copy-Item -LiteralPath (Join-Path $runRoot 'preservation') -Destination (Join-Path $preservationEvidence 'reports') -Recurse

Write-Output "AMD64 native build passed."
Write-Output "ELF SHA-256: $elfHash"
Write-Output "REXX C object files: $rexxObjectCount"
Write-Output "Output audit: $auditPath"
Write-Output "Link evidence: $linkEvidencePath"
Write-Output "Preservation evidence: $preservationEvidence"
