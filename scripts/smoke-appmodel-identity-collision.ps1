[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\appmodel-identity-collision.log"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

function Assert-IdentityCheck {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "App Model identity check failed: $Message" }
    Write-Host "PASS: $Message"
}

$exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "guideXOSServer.exe not found: $exe" }

$output = Join-Path $Root $OutputPath
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null

$psi = [Diagnostics.ProcessStartInfo]::new()
$psi.FileName = (Resolve-Path -LiteralPath $exe).Path
$psi.WorkingDirectory = $Root
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$process = [Diagnostics.Process]::new()
$process.StartInfo = $psi
[void]$process.Start()
$commands = @(
    "gui.start",
    "desktop.apps.verbose",
    "desktop.launch.resolve Hello World",
    "desktop.launch.resolve Resource Viewer",
    "desktop.launch.resolve com.guidexos.helloworld",
    "desktop.launch.resolve com.guidexos.resourceviewer",
    "desktop.launch.resolve Notepad",
    "exit"
)
$process.StandardInput.WriteLine(($commands -join "`n"))
$process.StandardInput.Close()
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()
[IO.File]::WriteAllText($output, $stdout + "`n[STDERR]`n" + $stderr)

Assert-IdentityCheck ($process.ExitCode -eq 0) "Server exited cleanly"
Assert-IdentityCheck ($stdout.Contains("originalLabel: Hello World") -and
    $stdout.Contains("appId: com.guidexos.helloworld") -and
    $stdout.Contains("reason: Matched hosted registered desktop app by deterministic display-name compatibility policy")) `
    "Hello World display-name lookup resolves the packaged canonical ID"
Assert-IdentityCheck ($stdout.Contains("originalLabel: Resource Viewer") -and
    $stdout.Contains("appId: com.guidexos.resourceviewer") -and
    $stdout.Contains("reason: Matched hosted registered desktop app by deterministic display-name compatibility policy")) `
    "Resource Viewer display-name lookup resolves the packaged canonical ID"
Assert-IdentityCheck ($stdout.Contains("originalLabel: com.guidexos.helloworld") -and
    $stdout.Contains("reason: Matched hosted registered desktop app by exact canonical application id") -and
    $stdout.Contains("originalLabel: com.guidexos.resourceviewer")) `
    "canonical-ID lookups remain exact and stable"
Assert-IdentityCheck ($stdout.Contains("Compositor start menu skipped shadowed app: Hello World id=com.guidexos.samples.helloworld") -and
    $stdout.Contains("Compositor start menu skipped shadowed app: Resource Viewer id=com.guidexos.samples.resourceviewer") -and
    $stdout.Contains("Compositor start menu include: Hello World targetAppId=com.guidexos.helloworld") -and
    $stdout.Contains("Compositor start menu include: Resource Viewer targetAppId=com.guidexos.resourceviewer")) `
    "Start Menu removes stale sample labels and retains canonical target IDs"
Assert-IdentityCheck (-not $stdout.Contains("sample binary not built: sdk/samples")) `
    "display-name launches do not select missing SDK sample entries"
Assert-IdentityCheck ($stdout.Contains("originalLabel: Notepad") -and $stdout.Contains("appId: gxos.builtin.notepad")) `
    "built-in Notepad remains canonically resolvable"

$sampleHello = Get-Content -LiteralPath (Join-Path $Root "sdk\samples\helloworld\app.json") -Raw | ConvertFrom-Json
$sampleResource = Get-Content -LiteralPath (Join-Path $Root "sdk\samples\resourceviewer\app.json") -Raw | ConvertFrom-Json
$packagedHello = Get-Content -LiteralPath (Join-Path $Root "Apps\HelloWorld\app.json") -Raw | ConvertFrom-Json
$packagedResource = Get-Content -LiteralPath (Join-Path $Root "Apps\ResourceViewer\app.json") -Raw | ConvertFrom-Json
Assert-IdentityCheck ($sampleHello.id -ne $packagedHello.id -and $sampleResource.id -ne $packagedResource.id) `
    "sample and packaged records have distinct canonical IDs"
Assert-IdentityCheck ((-not (Test-Path (Join-Path $Root "sdk\samples\helloworld\bin\amd64\helloworld.elf"))) -and
    (Test-Path (Join-Path $Root "Apps\HelloWorld\bin\amd64\helloworld.elf")) -and
    (-not (Test-Path (Join-Path $Root "sdk\samples\resourceviewer\bin\amd64\resourceviewer.elf"))) -and
    (Test-Path (Join-Path $Root "Apps\ResourceViewer\bin\amd64\resourceviewer.elf"))) `
    "missing sample entries are ineligible while packaged entries exist"

$registrySource = Get-Content -LiteralPath (Join-Path $Root "app_registry.cpp") -Raw
$desktopSource = Get-Content -LiteralPath (Join-Path $Root "desktop_service.cpp") -Raw
$compositorSource = Get-Content -LiteralPath (Join-Path $Root "compositor.cpp") -Raw
Assert-IdentityCheck ($registrySource.Contains("DisplayNameResolutionStatus::Ambiguous") -and
    $registrySource.Contains("DisplayNameSourcePriority") -and
    $registrySource.Contains("temporary development registrations require an explicit development route")) `
    "duplicate display names have explicit priority, ambiguity, and temporary-record policy"
Assert-IdentityCheck ($compositorSource.Contains("launchAction(item.targetAppId)") -and
    $compositorSource.Contains("g_startMenuAllProgsTargetIds")) `
    "desktop shortcuts and Start Menu activation carry canonical IDs"
Assert-IdentityCheck ($desktopSource.Contains("temporary development registrations require an explicit development route") -or
    $registrySource.Contains("temporaryDevelopment")) `
    "temporary development records remain separated from compatibility lookup"
Assert-IdentityCheck ((-not (Get-Content -LiteralPath (Join-Path $Root "native_app_runtime.cpp") -Raw).Contains("pointerText(")) -and
    (-not $stdout.Contains("destination=0x") -and -not $stdout.Contains("outBytesRead=0x"))) `
    "normal output contains no raw-pointer file-read diagnostics"

Write-Host "App Model identity collision smoke PASS. Output: $output"
