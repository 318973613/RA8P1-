param(
    [Parameter(Mandatory = $true)]
    [string]$InputC,
    [Parameter(Mandatory = $true)]
    [string]$OutputBin
)

if (-not (Test-Path $InputC)) {
    throw "Input file not found: $InputC"
}

$content = Get-Content $InputC -Raw
$matches = [regex]::Matches($content, '0x([0-9A-Fa-f]{2})')
if ($matches.Count -eq 0) {
    throw "No hex byte tokens found in: $InputC"
}

$bytes = New-Object byte[] $matches.Count
for ($i = 0; $i -lt $matches.Count; $i++) {
    $bytes[$i] = [Convert]::ToByte($matches[$i].Groups[1].Value, 16)
}

[System.IO.File]::WriteAllBytes($OutputBin, $bytes)
Write-Host "Wrote $($bytes.Length) bytes to $OutputBin"

