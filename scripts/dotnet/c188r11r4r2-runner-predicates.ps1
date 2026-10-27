function Get-C188R11R4R2StageSnapshot([string]$SerialText,
    [string]$Stage) {
    $escapedStage = [regex]::Escape($Stage)
    $statePattern = '\[C102-MANAGED-OUTPUT\] C188R11R4R2-NOTES stage=' +
        $escapedStage +
        ' kind=(?<kind>[a-z-]+) x=(?<x>\d+) y=(?<y>\d+) value=(?<value>\d+) focused=(?<focused>[01]) len=(?<length>\d+) caret=(?<caret>\d+) edit=(?<edit>none|\d+) render=(?<render>[01])'
    $state = [regex]::Match($SerialText, '(?m)^' + $statePattern + '\r?$')
    if (-not $state.Success) { return $null }

    $length = [int]$state.Groups['length'].Value
    if ($length -gt 256) { return $null }
    $content = [char[]]::new($length)
    $written = [bool[]]::new($length)
    $contentPattern = '\[C102-MANAGED-OUTPUT\] C188R11R4R2-TEXT stage=' +
        $escapedStage +
        ' offset=(?<offset>\d+) count=(?<count>\d+) hex=(?<hex>[A-Fa-f0-9]*)'
    $chunks = [regex]::Matches($SerialText, '(?m)^' + $contentPattern + '\r?$')
    if ($chunks.Count -eq 0) { return $null }
    $sawEmptyChunk = $false
    foreach ($chunk in $chunks) {
        $offset = [int]$chunk.Groups['offset'].Value
        $count = [int]$chunk.Groups['count'].Value
        $hex = $chunk.Groups['hex'].Value
        if ($count -lt 0 -or $count -gt 32 -or $hex.Length -ne 2 * $count -or
            $offset -lt 0 -or $offset + $count -gt $length) { return $null }
        if ($count -eq 0) {
            if ($length -ne 0 -or $offset -ne 0 -or $sawEmptyChunk) { return $null }
            $sawEmptyChunk = $true
            continue
        }
        for ($index = 0; $index -lt $count; $index++) {
            $target = $offset + $index
            if ($written[$target]) { return $null }
            $byte = [Convert]::ToByte($hex.Substring(2 * $index, 2), 16)
            if ($byte -gt 0x7f) { return $null }
            $content[$target] = [char]$byte
            $written[$target] = $true
        }
    }
    if ($length -eq 0) {
        if (-not $sawEmptyChunk) { return $null }
    } else {
        foreach ($present in $written) { if (-not $present) { return $null } }
    }

    return [pscustomobject]@{
        stage=$Stage
        kind=$state.Groups['kind'].Value
        x=[int]$state.Groups['x'].Value
        y=[int]$state.Groups['y'].Value
        value=[int]$state.Groups['value'].Value
        focused=($state.Groups['focused'].Value -eq '1')
        length=$length
        caret=[int]$state.Groups['caret'].Value
        edit=$state.Groups['edit'].Value
        render=($state.Groups['render'].Value -eq '1')
        text=[string]::new($content)
    }
}

function Get-C188R11R4R2NotesTextEntryEvidence([string]$SerialText,
    [bool]$CallbackCompleted) {
    $beforePointer = Get-C188R11R4R2StageSnapshot $SerialText 'before-pointer'
    $afterPointer = Get-C188R11R4R2StageSnapshot $SerialText 'after-pointer'
    $beforeKey = Get-C188R11R4R2StageSnapshot $SerialText 'before-key'
    $afterKey = Get-C188R11R4R2StageSnapshot $SerialText 'after-key'
    $nativePointer = [regex]::IsMatch($SerialText,
        '(?m)^\[C116-NATIVE-INPUT\] kind=pointer-down x=00000018 y=00000063 result=PASS\r?$')
    $singleA = ([regex]::Matches($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11R4R2-NOTES stage=before-key kind=key-char .*? value=97 ')).Count -eq 1 -and
        ([regex]::Matches($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11R4R2-NOTES stage=after-key kind=key-char .*? value=97 ')).Count -eq 1

    $validClick = $null -ne $afterPointer -and $nativePointer -and
        $afterPointer.kind -eq 'pointer-down' -and $afterPointer.x -eq 24 -and
        $afterPointer.y -eq 99 -and $afterPointer.focused -and
        $afterPointer.caret -ge 0 -and
        $afterPointer.caret -le $afterPointer.length -and
        $afterPointer.edit -eq '2' -and
        $afterPointer.render -and $null -ne $beforePointer -and
        $beforePointer.kind -eq 'pointer-down' -and
        $beforePointer.length -eq $afterPointer.length -and
        $beforePointer.text -ceq $afterPointer.text
    $validBeforeKey = $null -ne $beforeKey -and $null -ne $afterPointer -and
        $beforeKey.kind -eq 'key-char' -and $beforeKey.value -eq 97 -and
        $beforeKey.focused -and $beforeKey.length -eq $afterPointer.length -and
        $beforeKey.caret -eq $afterPointer.caret -and
        $beforeKey.text -ceq $afterPointer.text
    $validInsertion = $null -ne $afterKey -and $null -ne $beforeKey -and
        $afterKey.kind -eq 'key-char' -and $afterKey.value -eq 97 -and
        $afterKey.focused -and $afterKey.edit -eq '1' -and
        $afterKey.length -eq $beforeKey.length + 1 -and
        $afterKey.caret -eq $beforeKey.caret + 1 -and
        $beforeKey.caret -ge 0 -and $beforeKey.caret -le $beforeKey.length -and
        $afterKey.text -ceq $beforeKey.text.Insert($beforeKey.caret, 'a')
    $redrawCompleted = $null -ne $afterKey -and $afterKey.render
    $accepted = $validClick -and $validBeforeKey -and $validInsertion -and
        $redrawCompleted -and $singleA -and $CallbackCompleted

    return [ordered]@{
        accepted=[bool]$accepted
        editorFocusEstablished=[bool]$validClick
        preInputStateQualified=[bool]$validBeforeKey
        oneCharacterInserted=[bool]$validInsertion
        callbackCompleted=$CallbackCompleted
        pointerLocalX=$(if ($null -ne $afterPointer) { $afterPointer.x } else { $null })
        pointerLocalY=$(if ($null -ne $afterPointer) { $afterPointer.y } else { $null })
        beforeLength=$(if ($null -ne $beforeKey) { $beforeKey.length } else { $null })
        afterLength=$(if ($null -ne $afterKey) { $afterKey.length } else { $null })
        beforeCaret=$(if ($null -ne $beforeKey) { $beforeKey.caret } else { $null })
        afterCaret=$(if ($null -ne $afterKey) { $afterKey.caret } else { $null })
        beforeText=$(if ($null -ne $beforeKey) { $beforeKey.text } else { $null })
        afterText=$(if ($null -ne $afterKey) { $afterKey.text } else { $null })
        insertedCharacter=$(if ($validInsertion) { 'a' } else { $null })
        redrawCompleted=[bool]($validInsertion -and $redrawCompleted)
    }
}
