<#!
.SYNOPSIS
    Runs the controlled DM18 NVMe sparse/dense/preallocated cache matrix.

.DESCRIPTION
    Each case uses a fresh 600 MiB all-zero raw namespace and the same DM16
    nine-sector final-LBA write/Flush/read/restore proof. The target is either
    a sparse hole, part of a dense file, or preallocated in an otherwise sparse
    file. Results, QEMU arguments, allocation maps, raw-region inspections, and
    serial output are kept under out/dm18-matrix.
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDirectory = "out\dm18-matrix",
    [string]$DiskDirectory = "E:\guidexos-dm18-proof-disks",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$QemuImgExecutable = "C:\Program Files\qemu\qemu-img.exe",
    [string[]]$CaseName = @(),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = [IO.Path]::GetFullPath($Root)
Set-Location -LiteralPath $Root
$outRoot = [IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $outRoot.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must be below the repository out directory."
}
if (Test-Path -LiteralPath $outRoot) {
    if ((Get-ChildItem -LiteralPath $outRoot -Force | Measure-Object).Count -ne 0) {
        throw "Refusing to overwrite existing DM18 matrix evidence: $outRoot"
    }
} else {
    New-Item -ItemType Directory -Path $outRoot -Force | Out-Null
}
if (-not (Test-Path -LiteralPath $DiskDirectory)) {
    New-Item -ItemType Directory -Path $DiskDirectory -Force | Out-Null
}
$runner = Join-Path $PSScriptRoot "run-dm16-qemu-proof.ps1"
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop).Source

$cases = [System.Collections.Generic.List[object]]::new()
foreach ($allocation in @("sparse-hole", "dense", "target-preallocated")) {
    foreach ($cache in @("writeback", "writethrough", "none", "directsync")) {
        $cases.Add([pscustomobject]@{
            Name = "$allocation-$cache"
            Allocation = $allocation
            Cache = $cache
            Discard = "ignore"
            DetectZeroes = "off"
            Aio = ""
            Trace = ($cache -eq "writeback" -or $cache -eq "directsync")
        })
    }
}
$cases.Add([pscustomobject]@{
    Name = "sparse-hole-writeback-detect-zeroes-unmap"
    Allocation = "sparse-hole"
    Cache = "writeback"
    Discard = "unmap"
    DetectZeroes = "unmap"
    Aio = ""
    Trace = $true
})
$cases.Add([pscustomobject]@{
    Name = "target-preallocated-writeback-detect-zeroes-unmap"
    Allocation = "target-preallocated"
    Cache = "writeback"
    Discard = "unmap"
    DetectZeroes = "unmap"
    Aio = ""
    Trace = $true
})
$cases.Add([pscustomobject]@{
    Name = "dense-writeback-aio-threads"
    Allocation = "dense"
    Cache = "writeback"
    Discard = "ignore"
    DetectZeroes = "off"
    Aio = "threads"
    Trace = $true
})

if ($CaseName.Count -gt 0) {
    $CaseName = @($CaseName | ForEach-Object { $_ -split "," } |
        ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $unknownCases = @($CaseName | Where-Object { $_ -notin $cases.Name })
    if ($unknownCases.Count -gt 0) {
        throw "Unknown matrix case(s): $($unknownCases -join ', ')"
    }
    $selectedCases = @($cases | Where-Object { $_.Name -in $CaseName })
    $cases.Clear()
    foreach ($selectedCase in $selectedCases) { $cases.Add($selectedCase) }
}

$results = [System.Collections.Generic.List[object]]::new()
$attempt = 200
foreach ($case in $cases) {
    $workDir = Join-Path $OutputDirectory $case.Name
    $runnerLog = Join-Path $outRoot "$($case.Name).runner.log"
    $arguments = @(
        "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $runner,
        "-Stage", "PrivateWrite",
        "-WorkDir", $workDir,
        "-DiskDirectory", $DiskDirectory,
        "-QemuExecutable", $QemuExecutable,
        "-QemuImgExecutable", $QemuImgExecutable,
        "-AttemptNumber", $attempt,
        "-CacheMode", $case.Cache,
        "-DiscardMode", $case.Discard,
        "-DetectZeroesMode", $case.DetectZeroes,
        "-SkipBuild")
    if ($case.Aio) { $arguments += @("-AioMode", $case.Aio) }
    if ($case.Allocation -eq "dense") { $arguments += "-DenseImage" }
    else { $arguments += "-SparseImage" }
    if ($case.Allocation -eq "target-preallocated") { $arguments += "-PreallocateTarget" }
    if ($case.Trace) { $arguments += "-QemuTrace" }
    Write-Host "DM18 matrix: $($case.Name)"
    & $pwsh @arguments 2>&1 | Tee-Object -FilePath $runnerLog
    $exitCode = $LASTEXITCODE
    $manifestPath = Join-Path $outRoot (Join-Path $case.Name "dm16-manifest.txt")
    $serialPath = Join-Path $outRoot (Join-Path $case.Name "private-write-boot.serial.log")
    $manifest = if (Test-Path -LiteralPath $manifestPath) {
        Get-Content -LiteralPath $manifestPath
    } else { @() }
    $resultLine = $manifest | Where-Object { $_ -like "result=*" } | Select-Object -Last 1
    $failureLine = $manifest | Where-Object { $_ -like "failure=*" } | Select-Object -Last 1
    $qemuStderr = Join-Path $outRoot (Join-Path $case.Name "private-write-boot.stderr.log")
    $stderText = if (Test-Path -LiteralPath $qemuStderr) { Get-Content -LiteralPath $qemuStderr -Raw } else { "" }
    $status = if ($resultLine -like "result=PASS*") { "PASS" }
        elseif ($exitCode -ne 0 -and $stderText -match "(not supported|unsupported|invalid parameter|not available)") { "UNSUPPORTED" }
        elseif ($exitCode -ne 0) { "FAIL" }
        else { "INCOMPLETE" }
    $serialResult = if (Test-Path -LiteralPath $serialPath) {
        Get-Content -LiteralPath $serialPath -Tail 30 | Where-Object {
            $_ -match "private-cycle=|private-write-flush-stress=|private-proof=|KERNEL-FAULT"
        } | Select-Object -Last 3
    } else { @() }
    $results.Add([pscustomobject]@{
        Name = $case.Name
        Allocation = $case.Allocation
        Cache = $case.Cache
        Discard = $case.Discard
        DetectZeroes = $case.DetectZeroes
        Aio = if ($case.Aio) { $case.Aio } else { "QEMU-default" }
        QemuTrace = $case.Trace
        Status = $status
        ExitCode = $exitCode
        ResultLine = [string]$resultLine
        Failure = [string]$failureLine
        SerialSummary = ($serialResult -join " | ")
    })
    $attempt++
}

$summaryPath = Join-Path $outRoot "matrix-summary.csv"
$results | Export-Csv -LiteralPath $summaryPath -NoTypeInformation -Encoding utf8
$summaryText = $results | ForEach-Object {
    "$($_.Name) status=$($_.Status) cache=$($_.Cache) allocation=$($_.Allocation) result=$($_.ResultLine) failure=$($_.Failure)"
}
Set-Content -LiteralPath (Join-Path $outRoot "matrix-summary.txt") `
    -Value $summaryText -Encoding utf8
$results | Format-Table Name,Status,Cache,Allocation -AutoSize
Write-Host "DM18 matrix summary: $summaryPath"
