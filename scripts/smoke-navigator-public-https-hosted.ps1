param(
    [string[]]$TargetUrl = @(
        "https://example.com/",
        "https://www.iana.org/domains/example",
        "https://news.ycombinator.com/",
        "https://www.wikipedia.org/"
    ),
    [int]$TimeoutSeconds = 30,
    [switch]$RequireAnySuccess
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Executable = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Missing hosted Server binary: $Executable. Run build.bat first."
}

function Invoke-NavigatorGoto {
    param([Parameter(Mandatory = $true)][string]$Url)

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    # The hosted server's established smoke path is a cmd pipeline.  Use the
    # same bounded command stream here so stdin reaches the child consistently
    # even when PowerShell launches the process without an attached console.
    $startInfo.FileName = "cmd.exe"
    $startInfo.Arguments = "/c (echo navigator.goto $Url&& echo exit) | `"$Executable`""
    $startInfo.WorkingDirectory = $Root
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $process.StandardInput.Close()
    # Drain both redirected pipes while the process runs.  Waiting before reading
    # can deadlock when a real page produces more diagnostics than the pipe buffer.
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        return [pscustomobject]@{ Url = $Url; Result = "TIMEOUT"; HttpStatus = 0; FinalUrl = ""; Redirects = 0; Blocks = 0; Framing = ""; Encoding = ""; TlsValidated = "no"; TlsHostname = ""; TlsProtocol = ""; RemoteResources = 0; ResourceFailures = 0; Error = "bounded hosted process timeout"; OutputBytes = 0 }
    }
    $output = $stdoutTask.GetAwaiter().GetResult() + "`n" + $stderrTask.GetAwaiter().GetResult()
    $result = if ($output -match "NAVIGATOR_GOTO_RESULT: (PASS|FAIL)") { $Matches[1] } else { "FAIL" }
    $status = if ($output -match "(?m)^http_status=(\d+)") { [int]$Matches[1] } else { 0 }
    $finalUrl = if ($output -match "(?m)^final_url=(.*)$") { $Matches[1].Trim() } else { "" }
    $redirects = if ($output -match "(?m)^redirect_count=(\d+)") { [int]$Matches[1] } else { 0 }
    $blocks = if ($output -match "(?m)^document_block_count=(\d+)") { [int]$Matches[1] } else { 0 }
    $framing = if ($output -match "(?m)^response_framing=(.*)$") { $Matches[1].Trim() } else { "" }
    $encoding = if ($output -match "(?m)^content_encoding=(.*)$") { $Matches[1].Trim() } else { "" }
    $tlsValidated = if ($output -match "(?m)^tls_validated=(.*)$") { $Matches[1].Trim() } else { "no" }
    $tlsHostname = if ($output -match "(?m)^tls_hostname=(.*)$") { $Matches[1].Trim() } else { "" }
    $tlsProtocol = if ($output -match "(?m)^tls_protocol=(.*)$") { $Matches[1].Trim() } else { "" }
    $remoteResources = if ($output -match "(?m)^remote_resource_count=(\d+)") { [int]$Matches[1] } else { 0 }
    $resourceFailures = if ($output -match "(?m)^resource_failures=(\d+)") { [int]$Matches[1] } else { 0 }
    $error = if ($output -match "(?m)^error_status=(.*)$") { $Matches[1].Trim() } else { "" }
    [pscustomobject]@{ Url = $Url; Result = $result; HttpStatus = $status; FinalUrl = $finalUrl; Redirects = $redirects; Blocks = $blocks; Framing = $framing; Encoding = $encoding; TlsValidated = $tlsValidated; TlsHostname = $tlsHostname; TlsProtocol = $tlsProtocol; RemoteResources = $remoteResources; ResourceFailures = $resourceFailures; Error = $error; OutputBytes = $output.Length }
}

$results = foreach ($url in $TargetUrl) {
    if ([string]::IsNullOrWhiteSpace($url)) { continue }
    Invoke-NavigatorGoto -Url $url.Trim()
}

Write-Host "Navigator hosted public HTTPS compatibility corpus (external-network rail)"
$results | Select-Object Url, Result, HttpStatus, Redirects, Blocks, Framing, Encoding, TlsValidated, TlsHostname, TlsProtocol, RemoteResources, ResourceFailures, Error | Format-Table -AutoSize
foreach ($result in $results) {
    Write-Host ("diag url={0} final={1} status={2} redirects={3} framing={4} encoding={5} tls_validated={6} tls_hostname={7} tls_protocol={8} blocks={9} remote_resources={10} resource_failures={11} error={12}" -f
        $result.Url, $result.FinalUrl, $result.HttpStatus, $result.Redirects, $result.Framing, $result.Encoding,
        $result.TlsValidated, $result.TlsHostname, $result.TlsProtocol, $result.Blocks,
        $result.RemoteResources, $result.ResourceFailures, ($result.Error -replace "`r|`n", " "))
}
$successes = @($results | Where-Object { $_.Result -eq "PASS" -and $_.HttpStatus -ge 200 -and $_.HttpStatus -lt 400 -and $_.Blocks -gt 0 -and [string]::IsNullOrWhiteSpace($_.Error) })
Write-Host ("valid_http_html_reaches={0}/{1}" -f $successes.Count, @($results).Count)
if ($RequireAnySuccess -and $successes.Count -eq 0) { exit 2 }
exit 0
