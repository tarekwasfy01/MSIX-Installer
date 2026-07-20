$ErrorActionPreference = "Stop"

$signTool = Get-ChildItem `
    "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" `
    -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $signTool) {
    throw "signtool.exe was not found. Install the Windows SDK signing tools."
}

$targetDirectory = Join-Path `
    $PSScriptRoot `
    "tools\signtool"

New-Item `
    -ItemType Directory `
    -Path $targetDirectory `
    -Force | Out-Null

Copy-Item `
    -LiteralPath $signTool `
    -Destination (Join-Path $targetDirectory "signtool.exe") `
    -Force

Write-Host "SignTool prepared for resource embedding."
