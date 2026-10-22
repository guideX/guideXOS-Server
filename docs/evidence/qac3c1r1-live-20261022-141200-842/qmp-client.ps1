[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateRange(1024,65535)][int]$Port,
    [Parameter(Mandatory=$true)][ValidateSet('status','screendump','quit')][string]$Operation,
    [Parameter(Mandatory=$true)][string]$TranscriptPath,
    [string]$ScreenshotPath
)
$ErrorActionPreference='Stop'
if ($Operation -eq 'screendump' -and [string]::IsNullOrWhiteSpace($ScreenshotPath)) { throw 'ScreenshotPath is required for screendump.' }
$client=[Net.Sockets.TcpClient]::new()
$client.ReceiveTimeout=5000
$client.SendTimeout=5000
$client.Connect([Net.IPAddress]::Loopback,$Port)
$stream=$client.GetStream()
$stream.ReadTimeout=5000
$utf8=[Text.UTF8Encoding]::new($false)
$reader=[IO.StreamReader]::new($stream,$utf8,$false,1024,$true)
$writer=[IO.StreamWriter]::new($stream,$utf8,1024,$true)
$writer.AutoFlush=$true
$log=[System.Collections.Generic.List[string]]::new()
$log.Add("client_local=$((Get-Date).ToString('o')) operation=$Operation port=$Port")
function Read-QmpResult {
    param([IO.StreamReader]$Reader,[System.Collections.Generic.List[string]]$Log)
    for ($i=0; $i -lt 20; $i++) {
        $line=$Reader.ReadLine()
        if ($null -eq $line) { throw 'QMP closed before returning a response.' }
        $Log.Add("recv=$line")
        $obj=$line | ConvertFrom-Json -ErrorAction Stop
        if ($obj.PSObject.Properties.Name -contains 'return' -or $obj.PSObject.Properties.Name -contains 'error') { return $obj }
    }
    throw 'QMP did not return a result within the bounded response count.'
}
try {
    $greeting=$reader.ReadLine()
    if ([string]::IsNullOrWhiteSpace($greeting)) { throw 'QMP did not provide its protocol greeting.' }
    $log.Add("recv=$greeting")
    $writer.WriteLine('{"execute":"qmp_capabilities"}')
    $log.Add('send={"execute":"qmp_capabilities"}')
    $null=Read-QmpResult -Reader $reader -Log $log
    if ($Operation -eq 'status') {
        $request=@{execute='query-status'} | ConvertTo-Json -Compress
    } elseif ($Operation -eq 'screendump') {
        $request=@{execute='human-monitor-command';arguments=@{'command-line'="screendump $ScreenshotPath"}} | ConvertTo-Json -Compress
    } else {
        $request=@{execute='quit'} | ConvertTo-Json -Compress
    }
    $writer.WriteLine($request)
    $log.Add("send=$request")
    $result=Read-QmpResult -Reader $reader -Log $log
    $log.Add("result=$($result | ConvertTo-Json -Compress -Depth 12)")
    [IO.File]::AppendAllText($TranscriptPath,($log -join "`r`n")+"`r`n",$utf8)
    Write-Output ($result | ConvertTo-Json -Compress -Depth 12)
} finally {
    $writer.Dispose()
    $reader.Dispose()
    $stream.Dispose()
    $client.Dispose()
}