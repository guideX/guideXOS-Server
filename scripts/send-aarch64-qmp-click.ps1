[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][int]$X,
    [Parameter(Mandatory = $true)][int]$Y
)

$ErrorActionPreference = 'Stop'
$client = [Net.Sockets.TcpClient]::new()
try {
    $client.Connect('127.0.0.1', $Port)
    $stream = $client.GetStream()
    $stream.ReadTimeout = 3000
    $stream.WriteTimeout = 3000
    $buffer = New-Object byte[] 8192

    function Receive-Qmp([Net.Sockets.NetworkStream]$InputStream, [byte[]]$InputBuffer) {
        $text = ''
        Start-Sleep -Milliseconds 150
        while ($InputStream.DataAvailable) {
            $count = $InputStream.Read($InputBuffer, 0, $InputBuffer.Length)
            if ($count -gt 0) { $text += [Text.Encoding]::UTF8.GetString($InputBuffer, 0, $count) }
        }
        return $text
    }
    function Send-Qmp([Net.Sockets.NetworkStream]$OutputStream, [byte[]]$OutputBuffer, [string]$Json) {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Json + [char]10)
        $OutputStream.Write($bytes, 0, $bytes.Length)
        $OutputStream.Flush()
        $response = Receive-Qmp $OutputStream $OutputBuffer
        if ($response -match '"error"') { throw "QMP rejected command: $response" }
    }

    $null = Receive-Qmp $stream $buffer
    Send-Qmp $stream $buffer '{"execute":"qmp_capabilities"}'
    Start-Sleep -Milliseconds 300
    $json = '{"execute":"input-send-event","arguments":{"events":[' +
        '{"type":"abs","data":{"axis":"x","value":' + $X + '}},' +
        '{"type":"abs","data":{"axis":"y","value":' + $Y + '}},' +
        '{"type":"btn","data":{"button":"left","down":true}},' +
        '{"type":"btn","data":{"button":"left","down":false}}]}}'
    Send-Qmp $stream $buffer $json
    Start-Sleep -Milliseconds 500
} finally {
    $client.Dispose()
}
