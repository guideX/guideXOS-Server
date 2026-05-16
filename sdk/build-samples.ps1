param(
    [switch]$Clean,
    [switch]$Verbose,
    [switch]$SkipBuild,
    [switch]$SkipReadElf
)

$ErrorActionPreference = 'Stop'

$script:VerboseOutput = [bool]$Verbose
$SdkRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $SdkRoot
$AppsRoot = Join-Path $RepoRoot 'Apps'
$IncludeDir = Join-Path $SdkRoot 'include'

$Samples = @(
    [ordered]@{
        Name = 'HelloWorld'
        SourceDir = Join-Path $SdkRoot 'samples/helloworld'
        Source = 'main.cpp'
        OutputDir = Join-Path $AppsRoot 'HelloWorld'
        ElfRelativePath = 'bin/amd64/helloworld.elf'
        Manifest = 'app.json'
        Resources = @('resources/message.txt')
    },
    [ordered]@{
        Name = 'ResourceViewer'
        SourceDir = Join-Path $SdkRoot 'samples/resourceviewer'
        Source = 'main.cpp'
        OutputDir = Join-Path $AppsRoot 'ResourceViewer'
        ElfRelativePath = 'bin/amd64/resourceviewer.elf'
        Manifest = 'app.json'
        Resources = @('resources/about.txt')
    }
)

function Write-Detail($Message) {
    if ($script:VerboseOutput) { Write-Host $Message }
}

function Find-Tool($Names) {
    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }
    return $null
}

function Find-ClangTool($Names) {
    $tool = Find-Tool $Names
    if ($tool) { return $tool }

    $knownRoots = @(
        'C:\Program Files\LLVM\bin',
        'C:\mingw64\bin',
        'C:\Program Files (x86)\Android\AndroidNDK\android-ndk-r27c\toolchains\llvm\prebuilt\windows-x86_64\bin'
    )

    foreach ($root in $knownRoots) {
        foreach ($name in $Names) {
            $candidate = Join-Path $root $name
            if (Test-Path $candidate) { return $candidate }
        }
    }

    return $null
}

function Copy-RequiredFile($Source, $Destination) {
    $destinationDir = Split-Path -Parent $Destination
    if (-not (Test-Path $destinationDir)) { New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null }
    Copy-Item -Path $Source -Destination $Destination -Force
}

function Invoke-Checked($FilePath, $Arguments) {
    Write-Detail ("Running: {0} {1}" -f $FilePath, ($Arguments -join ' '))
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE`: $FilePath" }
}

function Test-RequiredFiles($Sample) {
    $required = @(
        Join-Path $Sample.OutputDir 'app.json'
        Join-Path $Sample.OutputDir $Sample.ElfRelativePath
    )
    foreach ($resource in $Sample.Resources) { $required += Join-Path $Sample.OutputDir $resource }

    foreach ($path in $required) {
        if (-not (Test-Path $path)) { throw "Required staged file missing: $path" }
    }
}

function Inspect-Elf($ReadElf, $ElfPath) {
    if (-not $ReadElf) { return 'ELF validation tool not found; skipping external ELF inspection.' }

    $output = & $ReadElf -h $ElfPath 2>&1
    if ($LASTEXITCODE -ne 0) { return "readelf failed: $($output -join ' ')" }

    $text = $output -join "`n"
    $classOk = $text -match 'Class:\s+ELF64'
    $machineOk = $text -match 'Machine:\s+Advanced Micro Devices X86-64'
    $entryLine = ($output | Where-Object { $_ -match 'Entry point address:' } | Select-Object -First 1)
    $entryOk = $entryLine -and $entryLine -notmatch '0x0\s*$'

    if ($classOk -and $machineOk -and $entryOk) {
        return "OK ($($entryLine.Trim()))"
    }

    return "Unexpected ELF header (ELF64=$classOk, X86-64=$machineOk, Entry=$entryOk)"
}

if ($Clean -and (Test-Path $AppsRoot)) {
    foreach ($sample in $Samples) {
        if (Test-Path $sample.OutputDir) {
            Write-Host "Cleaning $($sample.OutputDir)"
            Remove-Item -Recurse -Force $sample.OutputDir
        }
    }
}

$clang = Find-ClangTool @('clang++.exe', 'clang++', 'clang.exe', 'clang')
$readElf = $null
if (-not $SkipReadElf) { $readElf = Find-ClangTool @('llvm-readelf.exe', 'llvm-readelf', 'readelf.exe', 'readelf') }

if (-not $SkipBuild -and -not $clang) {
    Write-Error 'Could not find clang/clang++. Install LLVM or add it to PATH.'
    exit 1
}

if ($SkipReadElf) {
    Write-Host 'Skipping external ELF inspection.'
} elseif (-not $readElf) {
    Write-Host 'ELF validation tool not found; skipping external ELF inspection.'
}

$results = @()

foreach ($sample in $Samples) {
    $buildStatus = 'Skipped'
    $readElfStatus = 'Skipped'
    $resourceStatus = 'No resources'
    $elfPath = Join-Path $sample.OutputDir $sample.ElfRelativePath

    try {
        if (-not (Test-Path $sample.OutputDir)) { New-Item -ItemType Directory -Path $sample.OutputDir -Force | Out-Null }
        $elfDir = Split-Path -Parent $elfPath
        if (-not (Test-Path $elfDir)) { New-Item -ItemType Directory -Path $elfDir -Force | Out-Null }

        Copy-RequiredFile (Join-Path $sample.SourceDir $sample.Manifest) (Join-Path $sample.OutputDir 'app.json')
        foreach ($resource in $sample.Resources) {
            Copy-RequiredFile (Join-Path $sample.SourceDir $resource) (Join-Path $sample.OutputDir $resource)
        }
        if ($sample.Resources.Count -gt 0) { $resourceStatus = ($sample.Resources -join ', ') }

        if (-not $SkipBuild) {
            $sourcePath = Join-Path $sample.SourceDir $sample.Source
            $objectPath = Join-Path $elfDir ([IO.Path]::GetFileNameWithoutExtension($sample.Source) + '.o')
            $compileArgs = @('--target=x86_64-unknown-elf', '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', "-I$IncludeDir", '-c', $sourcePath, '-o', $objectPath)
            Invoke-Checked $clang $compileArgs

            $lld = Find-ClangTool @('ld.lld.exe', 'ld.lld')
            if (-not $lld) { throw 'Could not find ld.lld. Install LLVM or add it to PATH.' }

            $linkArgs = @('-m', 'elf_x86_64', '-static', '-e', 'gx_main', $objectPath, '-o', $elfPath)
            Invoke-Checked $lld $linkArgs
            Remove-Item -Force $objectPath -ErrorAction SilentlyContinue
            $buildStatus = 'Success'
        }

        Test-RequiredFiles $sample

        if ($SkipReadElf) {
            $readElfStatus = 'Skipped'
        } else {
            $readElfStatus = Inspect-Elf $readElf $elfPath
        }
    } catch {
        $buildStatus = if ($SkipBuild) { 'Skipped' } else { 'Failure' }
        $readElfStatus = "Error: $($_.Exception.Message)"
    }

    $results += [pscustomobject]@{
        Sample = $sample.Name
        Build = $buildStatus
        StagedPath = $sample.OutputDir
        ElfPath = $elfPath
        Resources = $resourceStatus
        ReadElf = $readElfStatus
    }
}

Write-Host ''
Write-Host 'guideXOS Native ELF SDK sample build summary'
$results | Format-Table -AutoSize

if ($results | Where-Object { $_.Build -eq 'Failure' -or $_.ReadElf -like 'Error:*' -or $_.ReadElf -like 'Unexpected*' -or $_.ReadElf -like 'readelf failed*' }) {
    exit 1
}
