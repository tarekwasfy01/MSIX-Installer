param(
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$vsWhere = Join-Path `
    ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\vswhere.exe"

$setup = Join-Path `
    ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\setup.exe"

if (-not (Test-Path -LiteralPath $vsWhere) -or
    -not (Test-Path -LiteralPath $setup)) {
    throw "Visual Studio Installer was not found."
}

$installationPath = & $vsWhere `
    -latest `
    -products * `
    -property installationPath |
    Select-Object -First 1

if (-not $installationPath) {
    throw "No Visual Studio installation was found."
}

$arguments = @(
    "modify",
    "--installPath", $installationPath.Trim(),
    "--add", "Microsoft.VisualStudio.Workload.NativeDesktop",
    "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "--add", "Microsoft.VisualStudio.Component.Windows11SDK.26100",
    "--includeRecommended",
    "--passive",
    "--norestart"
)

Write-Host "Installing the required C++ build tools and Windows SDK..."

$process = Start-Process `
    -FilePath $setup `
    -ArgumentList $arguments `
    -Verb RunAs `
    -Wait `
    -PassThru

if ($process.ExitCode -notin @(0, 3010)) {
    throw "Visual Studio Installer exited with code $($process.ExitCode)."
}

if (-not $NoBuild) {
    & cmd.exe /d /c `
        "`"$PSScriptRoot\BUILD_ONEFILE_EXE.bat`" --after-install"

    exit $LASTEXITCODE
}

exit 0
