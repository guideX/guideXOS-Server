[CmdletBinding()]
param(
    [string]$Workspace = "",
    [string]$EvidenceRoot = "",
    [string]$KernelPath = "",
    [string]$ArtifactPath = "",
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = if ([string]::IsNullOrWhiteSpace($Workspace)) {
    (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
    [IO.Path]::GetFullPath($Workspace)
}
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\gc-process-teardown-policy"
}
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$runRoot = Join-Path $EvidenceRoot ("run-" + (Get-Date -Format "yyyyMMdd-HHmmssfff"))
$espRoot = Join-Path $runRoot "esp"
New-Item -ItemType Directory -Force -Path (Join-Path $espRoot "EFI\BOOT") | Out-Null

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: $Path"
    }
    return [IO.Path]::GetFullPath($Path)
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

$qemu = Require-File "C:\Program Files\qemu\qemu-system-x86_64.exe" "QEMU"
$ovmf = Require-File "C:\Program Files\qemu\share\edk2-x86_64-code.fd" "OVMF"
$bootloader = Require-File (Join-Path $root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe") "bootloader"
if ([string]::IsNullOrWhiteSpace($KernelPath)) {
    $KernelPath = Join-Path $root "out\dotnet\gc-initialization-dry-run\qemu\matrix\first\esp\kernel.elf"
}
if ([string]::IsNullOrWhiteSpace($ArtifactPath)) {
    $ArtifactPath = Join-Path $root "out\dotnet\gc-initialization-dry-run\artifact\NativeAotGcStartupMinimal.exe"
}
$kernel = Require-File $KernelPath "startup kernel"
$artifact = Require-File $ArtifactPath "startup artifact"

# The startup artifact is the only path allowed to call RhInitialize here.
# This guard prevents an accidental runtime-level shutdown or managed entry
# from being smuggled into this process-lifetime policy harness.
$startupProbeSource = Join-Path $root "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_startup_probe.cpp"
$startupText = Get-Content -LiteralPath (Require-File $startupProbeSource "startup probe source") -Raw
if ($startupText -notmatch "RhInitialize\(false\)") { throw "Startup-only RhInitialize path is not present." }
if ($startupText -match "RhShutdown|GC_Shutdown|RhpShutdown") { throw "Runtime shutdown was added to the startup probe." }

Copy-Item -LiteralPath $bootloader -Destination (Join-Path $espRoot "EFI\BOOT\BOOTX64.EFI") -Force
Copy-Item -LiteralPath $kernel -Destination (Join-Path $espRoot "kernel.elf") -Force
Copy-Item -LiteralPath $artifact -Destination (Join-Path $runRoot "startup-artifact.exe") -Force
$serial = Join-Path $runRoot "serial.log"
$stdout = Join-Path $runRoot "qemu.stdout.log"
$stderr = Join-Path $runRoot "qemu.stderr.log"
$qemuArgs = @(
    "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
    "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $ovmf)),
    "-drive", ("file=fat:rw:" + (Quote-QemuValue $espRoot) + ",format=raw,if=ide,index=0"),
    "-m", "1024M", "-vga", "std", "-display", "none",
    "-serial", ("file:" + (Quote-QemuValue $serial)), "-no-reboot", "-no-shutdown",
    "-rtc", "base=utc,clock=host"
)

$baseline = [ordered]@{
    addressSpaceMappings = "unavailable (guest process counters are not exposed)"
    physicalFrames = "unavailable (guest frame counters are not exposed)"
    nativeWorkerStack = "unavailable (guest stack counter is not exposed)"
    tcbSlot = "unavailable (guest TCB counter is not exposed)"
    waitNode = "unavailable (guest wait-node counter is not exposed)"
    localStorageContext = "unavailable (guest local-storage counter is not exposed)"
    threadStoreAdapterRecord = "unavailable (guest ThreadStore counter is not exposed)"
    palWorkerHandle = "unavailable (guest PAL handle counter is not exposed)"
    hookInstallationDomain = "unavailable (guest hook-domain counter is not exposed)"
    activeCallbackState = "unavailable (guest callback counter is not exposed)"
}
$baseline | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot "baseline-counters.json") -Encoding UTF8

$process = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -WorkingDirectory $root `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
$startupObserved = $false
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
    if (Test-Path -LiteralPath $serial) {
        $text = Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
        if ($text -match "\[nativeaot-gc-startup-qemu-test\] ALL_(PASS|FAIL)") {
            $startupObserved = $true
            break
        }
    }
    if ($process.HasExited) { break }
}

$serialText = if (Test-Path -LiteralPath $serial) { Get-Content -LiteralPath $serial -Raw } else { "" }
$helperParked = $serialText -match "Finalizer helper parked: PASS"
$noFinalizer = $serialText -match "No managed finalizer entry: PASS"
$startupPass = $startupObserved -and $serialText -match "\[nativeaot-gc-startup-qemu-test\] ALL_PASS" -and
    $serialText -match "RhInitialize return=00000000" -and $helperParked -and $noFinalizer -and
    $serialText -match "legacyAllocCalls=00000000" -and $serialText -notmatch "RhpNewArray|RhpGcAlloc|GC\.Collect"

# This is the documented disposable-process boundary.  No runtime-level
# shutdown is attempted; QEMU is terminated through the host OS process path.
if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force -ErrorAction Stop
}
$process.WaitForExit(5000) | Out-Null
$process.Refresh()
$osTeardownPass = $process.HasExited
$after = [ordered]@{}
foreach ($name in $baseline.Keys) { $after[$name] = "unavailable (guest process exited before counter publication)" }
$after | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot "after-counters.json") -Encoding UTF8

@(
    "Startup-only RhInitialize: $(if ($startupPass) { 'PASS' } else { 'FAIL' })",
    "Helper worker parked: $(if ($helperParked) { 'PASS' } else { 'FAIL' })",
    "No managed allocation/finalizer: $(if ($startupPass -and $noFinalizer) { 'PASS' } else { 'FAIL' })",
    "Runtime-level shutdown: NOT SUPPORTED",
    "OS process teardown: $(if ($osTeardownPass) { 'PASS' } else { 'FAIL' })",
    "Address-space mappings: $($after.addressSpaceMappings)",
    "Physical frames: $($after.physicalFrames)",
    "Native worker stack: $($after.nativeWorkerStack)",
    "TCB slot: $($after.tcbSlot)",
    "Wait node: $($after.waitNode)",
    "Local-storage context: $($after.localStorageContext)",
    "ThreadStore adapter record: $($after.threadStoreAdapterRecord)",
    "PAL worker handle: $($after.palWorkerHandle)",
    "Hook installation domain: $($after.hookInstallationDomain)",
    "Active callback state: $($after.activeCallbackState)",
    "Evidence: $runRoot"
) | Tee-Object -FilePath (Join-Path $runRoot "policy-result.txt")

if (-not ($startupPass -and $osTeardownPass)) { exit 1 }
exit 0
