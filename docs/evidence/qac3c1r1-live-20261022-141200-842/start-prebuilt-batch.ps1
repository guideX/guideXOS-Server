[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BatchPath,
    [Parameter(Mandatory=$true)][string]$QacCliPath,
    [Parameter(Mandatory=$true)][ValidateRange(1024,65535)][int]$QualificationQmpPort
)
$ErrorActionPreference='Stop'
& $BatchPath -QacCliPath $QacCliPath -QualificationQmpPort $QualificationQmpPort
exit $LASTEXITCODE