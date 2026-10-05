function Find-Phase29LOwnerMismatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$SerialText
    )

    if ($null -eq $SerialText) {
        $text = ''
    } elseif ($SerialText -is [array]) {
        $text = [string]::Join([Environment]::NewLine, [string[]]$SerialText)
    } else {
        $text = [string]$SerialText
    }

    # Ownership output is written in fragments by the guest serial path. Only
    # treat a newline-terminated record as an event; a poll that ends at
    # result=CUR must not become a synthetic ownership failure.
    $pattern = '(?m)^.*DEVELOPER_STUDIO_PHASE29L_OWNER checkpoint=([^ \r\n]+) state=([^ \r\n]+) result=(?!CURRENT(?=[ \t\r\n])|TRANSACTION_NOT_ACTIVE(?=[ \t\r\n]))([^ \t\r\n]+)(?=[ \t])[^\r\n]*\r?\n'
    $match = [regex]::Match($text, $pattern)
    if ($match.Success) { return $match }
    return $null
}

Export-ModuleMember -Function Find-Phase29LOwnerMismatch
