param(
    [string]$PackageName = "TarekWasfy.MSIXInstaller",
    [string]$Publisher = "CN=85434AF5-74BD-4E8F-90F0-13F9EA1270DE",
    [string]$Version = "1.0.0.0"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "Building the standalone C++ executable..."
& cmd.exe /d /c "`"$PSScriptRoot\BUILD_ONEFILE_EXE.bat`" --no-open"

if ($LASTEXITCODE -ne 0) {
    throw "The native C++ build failed."
}

$makeAppx = Get-ChildItem `
    "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\makeappx.exe" `
    -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $makeAppx) {
    throw "makeappx.exe was not found."
}

$stage = Join-Path $PSScriptRoot "build\package-stage"
$outputDirectory = Join-Path $PSScriptRoot "output"

Remove-Item `
    -LiteralPath $stage `
    -Recurse `
    -Force `
    -ErrorAction SilentlyContinue

New-Item `
    -ItemType Directory `
    -Path (Join-Path $stage "Assets") `
    -Force | Out-Null

Copy-Item `
    "output\MSIXInstaller.exe" `
    $stage `
    -Force

Copy-Item `
    "package\Assets\*" `
    (Join-Path $stage "Assets") `
    -Force

$manifest = Get-Content `
    "package\AppxManifest.template.xml" `
    -Raw

$manifest = $manifest.Replace(
    "__PACKAGE_NAME__",
    $PackageName)

$manifest = $manifest.Replace(
    "__PUBLISHER__",
    $Publisher)

$manifest = $manifest.Replace(
    "__VERSION__",
    $Version)

[System.IO.File]::WriteAllText(
    (Join-Path $stage "AppxManifest.xml"),
    $manifest,
    [System.Text.UTF8Encoding]::new($false)
)

$msixPath = Join-Path `
    $outputDirectory `
    "${PackageName}_${Version}_x64_STORE_UNSIGNED.msix"

Remove-Item `
    -LiteralPath $msixPath `
    -Force `
    -ErrorAction SilentlyContinue

& $makeAppx pack `
    /d $stage `
    /p $msixPath `
    /o

if ($LASTEXITCODE -ne 0) {
    throw "MakeAppx failed."
}

Write-Host ""
Write-Host "Store package created:" -ForegroundColor Green
Write-Host $msixPath
Write-Host ""
Write-Host "Declared capabilities:"
Write-Host "  runFullTrust:      yes"
Write-Host "  allowElevation:    no"
Write-Host "  packageManagement: no"
