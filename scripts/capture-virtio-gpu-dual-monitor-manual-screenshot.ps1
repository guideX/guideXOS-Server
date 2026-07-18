param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 65535)]
    [int]$QmpPort,

    [Parameter(Mandatory = $true)]
    [string]$Label,

    [string]$EvidenceRoot = '',
    [int[]]$Heads = @(0, 1),
    [switch]$Shutdown
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $Root ("logs\manual-validation-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

function Read-QmpMessage([System.IO.StreamReader]$Reader) {
    while ($true) {
        try {
            $line = $Reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try { return ($line | ConvertFrom-Json -ErrorAction Stop) } catch { continue }
        } catch [System.IO.IOException] {
            Start-Sleep -Milliseconds 50
        }
    }
}

function Invoke-Qmp([System.IO.StreamWriter]$Writer, [System.IO.StreamReader]$Reader, [string]$Command, [hashtable]$Arguments = $null) {
    $payload = [ordered]@{ execute = $Command }
    if ($Arguments) { $payload.arguments = $Arguments }
    $Writer.WriteLine(($payload | ConvertTo-Json -Compress -Depth 8))
    while ($true) {
        $message = Read-QmpMessage $Reader
        if ($message.PSObject.Properties.Name -contains 'event') { continue }
        if ($message.PSObject.Properties.Name -contains 'return') { return $message.return }
        if ($message.PSObject.Properties.Name -contains 'error') { throw "QMP '$Command' failed: $($message.error.desc)" }
    }
}

$client = [System.Net.Sockets.TcpClient]::new()
$client.Connect('127.0.0.1', $QmpPort)
$stream = $client.GetStream()
$stream.ReadTimeout = 1000
$stream.WriteTimeout = 1000
$encoding = New-Object System.Text.UTF8Encoding $false
$reader = [System.IO.StreamReader]::new($stream, $encoding, $false, 1024, $true)
$writer = [System.IO.StreamWriter]::new($stream, $encoding, 1024, $true)
$writer.NewLine = "`n"
$writer.AutoFlush = $true

try {
    $greeting = Read-QmpMessage $reader
    if (-not ($greeting.PSObject.Properties.Name -contains 'QMP')) { throw 'QMP greeting missing.' }
    [void](Invoke-Qmp $writer $reader 'qmp_capabilities')
    foreach ($head in $Heads) {
        $path = Join-Path $EvidenceRoot ("{0}-head{1}.png" -f $Label, $head)
        [void](Invoke-Qmp $writer $reader 'screendump' @{ filename = $path; device = 'gpu0'; head = $head; format = 'png' })
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline -and (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -le 0)) {
            Start-Sleep -Milliseconds 100
        }
        if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -le 0) { throw "capture missing: $path" }
        Write-Output $path
    }
    if ($Shutdown) { [void](Invoke-Qmp $writer $reader 'quit') }
} finally {
    $writer.Dispose()
    $reader.Dispose()
    $stream.Dispose()
    $client.Dispose()
}
