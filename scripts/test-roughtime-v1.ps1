param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root

& $Python -c "import cryptography" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "Roughtime hosted tests need the pinned dependency. Install it with: python -m pip install -r requirements-roughtime.txt"
}

& $Python -B -m unittest discover -s tests -p "test_roughtime_v1.py" -v
if ($LASTEXITCODE -ne 0) {
    throw "Roughtime v1 hosted tests failed."
}

Write-Output "ROUGHTIME_V1_HOSTED_RESULT: PASS (23 tests)"
