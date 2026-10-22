param(
    [Parameter(Mandatory=$true)][string]$BuildRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188 candidate kernel builds require PowerShell Core.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$artifactRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repo 'out\dotnet\c188-external-app-identity'))
$buildRootFull = [System.IO.Path]::GetFullPath($BuildRoot)
$relativeBuildRoot = [System.IO.Path]::GetRelativePath($artifactRoot, $buildRootFull)
if ($relativeBuildRoot -eq '.' -or $relativeBuildRoot.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relativeBuildRoot)) {
    throw "BuildRoot must be a unique directory below $artifactRoot."
}
if (Test-Path -LiteralPath $buildRootFull) {
    throw "Refusing to overwrite an existing C188 kernel build: $buildRootFull"
}

$make = (Get-Command 'mingw32-make' -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $make) { $make = 'C:\mingw64\bin\mingw32-make.exe' }
if (-not (Test-Path -LiteralPath $make -PathType Leaf)) {
    throw "MinGW make is unavailable: $make"
}
$kernelSource = Join-Path $repo 'kernel'
$buildFromKernel = [System.IO.Path]::GetRelativePath($kernelSource, $buildRootFull).Replace('\','/')

# This is the C181 managed application feature set with the candidate-only
# CounterApp identity and bounded launcher-count proof enabled.
$nativeFeatures = @(
    'GXOS_NATIVEAOT_PRODUCTION_APPLICATION',
    'GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH',
    'GXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION',
    'GXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES',
    'GXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES',
    'GXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER',
    'GXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT',
    'GXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA',
    'GXOS_NATIVEAOT_C118_MANAGED_LIST_BOX',
    'GXOS_NATIVEAOT_C119_MANAGED_BUTTON',
    'GXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST',
    'GXOS_NATIVEAOT_C121_MANAGED_CHECKBOX',
    'GXOS_NATIVEAOT_C122_MANAGED_LABEL',
    'GXOS_NATIVEAOT_C123_MANAGED_SEPARATOR',
    'GXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON',
    'GXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR',
    'GXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX',
    'GXOS_NATIVEAOT_C127_MANAGED_PANEL',
    'GXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE',
    'GXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT',
    'GXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX',
    'GXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON',
    'GXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX',
    'GXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING',
    'GXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU',
    'GXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU',
    'GXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING',
    'GXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR',
    'GXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT',
    'GXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW',
    'GXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK',
    'GXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK',
    'GXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX',
    'GXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER',
    'GXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG',
    'GXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE',
    'GXOS_NATIVEAOT_C147_RUNTIME_SETTINGS',
    'GXOS_NATIVEAOT_C148_SETTINGS_V2',
    'GXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING',
    'GXOS_NATIVEAOT_C150_MANAGED_APP_RETURN',
    'GXOS_NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG',
    'GXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW',
    'GXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION',
    'GXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS',
    'GXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT',
    'GXOS_NATIVEAOT_C158_MANAGED_CALCULATOR',
    'GXOS_NATIVEAOT_C161_MANAGED_TASK_MANAGER',
    'GXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
    'GXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE',
    'GXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER',
    'GUIDEXOS_PROOF_CONTROL',
    'GXOS_NATIVEAOT_C188_EXTERNAL_APP_IDENTITY'
)
$extraCFlags = ($nativeFeatures | ForEach-Object { '-D' + $_ }) -join ' '
New-Item -ItemType Directory -Path $buildRootFull | Out-Null

$logPath = Join-Path $buildRootFull 'kernel-build.log'
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $make
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($argument in @(
    '-C', $kernelSource, 'ARCH=amd64', "BUILD_DIR=$buildFromKernel",
    "EXTRA_CFLAGS=$extraCFlags", '-B')) {
    [void]$psi.ArgumentList.Add($argument)
}
$process = [System.Diagnostics.Process]::Start($psi)
if (-not $process) { throw 'Failed to start the isolated C188 kernel build.' }
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
$process.WaitForExit()
$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
[System.IO.File]::WriteAllText($logPath, $stdout + $stderr, [System.Text.Encoding]::UTF8)
if ($process.ExitCode -ne 0) {
    throw "Isolated C188 kernel build failed with exit code $($process.ExitCode). See $logPath"
}

$kernelElf = Join-Path $buildRootFull 'bin\kernel.elf'
if (-not (Test-Path -LiteralPath $kernelElf -PathType Leaf)) {
    throw "C188 kernel build produced no ELF: $kernelElf"
}
$bytes = [System.IO.File]::ReadAllBytes($kernelElf)
$kernelText = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($marker in @('C171-WINDOW-CREATE','C172-COMPOSITOR-TRACE','C175-NATIVE-GEOMETRY','C188-IDENTITY-REGISTRY','C188-LAUNCHER-COUNTS','C188-STALE-GENERATION')) {
    if (-not $kernelText.Contains($marker)) { throw "C188 kernel ELF is missing required marker $marker." }
}
foreach ($requiredFlag in @('-fno-exceptions','-fno-rtti','-nostdinc++','-std=c++14')) {
    if (-not $stdout.Contains($requiredFlag) -and -not $stderr.Contains($requiredFlag)) {
        throw "Freestanding kernel command omitted required flag $requiredFlag."
    }
}
$readelf = (Get-Command 'readelf' -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $readelf) { throw 'readelf is required to validate the actual C188 kernel ELF.' }
$elfHeader = (& $readelf -h $kernelElf 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $elfHeader -notmatch 'ELF64' -or $elfHeader -notmatch 'Advanced Micro Devices X86-64') {
    throw "C188 kernel is not a valid ELF64 x86-64 image. See $logPath"
}
$hash = (Get-FileHash -LiteralPath $kernelElf -Algorithm SHA256).Hash
$manifest = [ordered]@{
    result = 'PASS'
    architecture = 'amd64'
    buildDirectory = $buildRootFull
    kernelElf = $kernelElf
    kernelSha256 = $hash
    elfHeader = $elfHeader.Trim()
    freestandingFlags = @('-std=c++14','-ffreestanding','-fno-exceptions','-fno-rtti','-nostdlib','-nostdinc++','-fno-builtin')
    c181ProofMarkers = @('C171-WINDOW-CREATE','C172-COMPOSITOR-TRACE','C175-NATIVE-GEOMETRY')
    c188CandidateMarkers = @('C188-IDENTITY-REGISTRY','C188-LAUNCHER-COUNTS','C188-STALE-GENERATION')
    candidateOnlyDefine = 'GXOS_NATIVEAOT_C188_EXTERNAL_APP_IDENTITY'
    standardKernelOutputTouched = $false
}
$manifestPath = Join-Path $buildRootFull 'kernel-qualification.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output ("C188_KERNEL_ELF={0} SHA256={1}" -f $kernelElf,$hash)
Write-Output "C188_KERNEL_QUALIFICATION=$manifestPath"
