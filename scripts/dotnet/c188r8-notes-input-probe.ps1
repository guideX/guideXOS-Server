function Get-C188R8NotesInputProbeResult {
    param([Parameter(Mandatory=$true)][string]$SerialText)

    return [pscustomobject]@{
        PointerAccepted = [bool]($SerialText -match
            '(?m)^C117-TEXT-AREA focus=PASS source=pointer\r?$')
        KeyboardAccepted = [bool]($SerialText -match
            '(?m)^C117-TEXT-AREA edit=changed result=PASS\r?$')
    }
}
