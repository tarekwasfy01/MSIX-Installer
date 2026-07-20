param(
    [Parameter(Mandatory = $true)]
    [string]$Msix,

    [Parameter(Mandatory = $true)]
    [string]$Publisher,

    [Parameter(Mandatory = $true)]
    [string]$Password,

    [Parameter(Mandatory = $true)]
    [string]$SignTool
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

function Write-Step {
    param([string]$Text)
    Write-Output $Text
}

function Get-SafeName {
    param([string]$Value)

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
    $sha = [System.Security.Cryptography.SHA256]::Create()

    try {
        return ([System.BitConverter]::ToString(
            $sha.ComputeHash($bytes)
        )).Replace("-", "").Substring(0, 16)
    }
    finally {
        $sha.Dispose()
    }
}

function Get-PackageIdentityName {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackagePath
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $archive = [System.IO.Compression.ZipFile]::OpenRead(
        $PackagePath
    )

    try {
        $manifestEntry = $archive.Entries |
            Where-Object {
                $_.FullName -ieq "AppxManifest.xml"
            } |
            Select-Object -First 1

        if (-not $manifestEntry) {
            $manifestEntry = $archive.Entries |
                Where-Object {
                    $_.FullName -ieq `
                        "AppxMetadata/AppxBundleManifest.xml"
                } |
                Select-Object -First 1
        }

        if (-not $manifestEntry) {
            return $null
        }

        $stream = $manifestEntry.Open()
        $reader = [System.IO.StreamReader]::new($stream)

        try {
            [xml]$manifestXml = $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
            $stream.Dispose()
        }

        $identityNode = $manifestXml.SelectSingleNode(
            "/*[local-name()='Package' or " +
            "local-name()='Bundle']/" +
            "*[local-name()='Identity']"
        )

        if (-not $identityNode) {
            return $null
        }

        return $identityNode.GetAttribute("Name")
    }
    finally {
        $archive.Dispose()
    }
}

try {
    if (-not (Test-Path -LiteralPath $Msix -PathType Leaf)) {
        throw "The selected package does not exist: $Msix"
    }

    if (-not (Test-Path -LiteralPath $SignTool -PathType Leaf)) {
        throw "The embedded signing tool could not be extracted."
    }

    $extension = [System.IO.Path]::GetExtension(
        $Msix
    ).ToLowerInvariant()

    if ($extension -notin @(
        ".msix",
        ".msixbundle",
        ".appx",
        ".appxbundle"
    )) {
        throw "Unsupported package extension: $extension"
    }

    Write-Step "[1/6] Preparing the signing certificate..."

    $safeName = Get-SafeName $Publisher
    $certificateFolder = Join-Path `
        $env:LOCALAPPDATA `
        "MSIXOneFileInstaller\Certificates\$safeName"

    $pfxPath = Join-Path `
        $certificateFolder `
        "MSIX_Test_Certificate.pfx"

    $cerPath = Join-Path `
        $certificateFolder `
        "MSIX_Test_Certificate.cer"

    New-Item `
        -ItemType Directory `
        -Path $certificateFolder `
        -Force | Out-Null

    $securePassword = ConvertTo-SecureString `
        $Password `
        -AsPlainText `
        -Force

    $certificate = Get-ChildItem "Cert:\CurrentUser\My" |
        Where-Object {
            $_.Subject -eq $Publisher -and
            $_.HasPrivateKey -and
            $_.NotAfter -gt (Get-Date).AddDays(30)
        } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1

    if (-not $certificate) {
        $certificate = New-SelfSignedCertificate `
            -Type Custom `
            -Subject $Publisher `
            -FriendlyName "MSIX Installer Test Certificate" `
            -KeyUsage DigitalSignature `
            -KeyAlgorithm RSA `
            -KeyLength 2048 `
            -HashAlgorithm SHA256 `
            -CertStoreLocation "Cert:\CurrentUser\My" `
            -NotAfter (Get-Date).AddYears(5) `
            -TextExtension @(
                "2.5.29.37={text}1.3.6.1.5.5.7.3.3",
                "2.5.29.19={text}"
            )
    }

    Export-PfxCertificate `
        -Cert $certificate `
        -FilePath $pfxPath `
        -Password $securePassword `
        -Force | Out-Null

    Export-Certificate `
        -Cert $certificate `
        -FilePath $cerPath `
        -Force | Out-Null

    Write-Step "[2/6] Signing the package..."

    & $SignTool sign `
        /fd SHA256 `
        /f $pfxPath `
        /p $Password `
        $Msix

    if ($LASTEXITCODE -ne 0) {
        throw "Package signing failed. SignTool exit code: $LASTEXITCODE"
    }

    Write-Step "[3/6] Inspecting the package signature..."

    $signature = Get-AuthenticodeSignature `
        -LiteralPath $Msix

    if (-not $signature.SignerCertificate) {
        throw "The package does not contain a readable signer certificate."
    }

    if ($signature.SignerCertificate.Thumbprint -ne
        $certificate.Thumbprint) {
        throw "The signer certificate does not match the generated certificate."
    }

    Write-Step "[4/6] Checking certificate trust..."

    $trustedCertificate = @(
        Get-ChildItem `
            "Cert:\LocalMachine\TrustedPeople" `
            -ErrorAction SilentlyContinue

        Get-ChildItem `
            "Cert:\LocalMachine\Root" `
            -ErrorAction SilentlyContinue
    ) |
        Where-Object {
            $_.Thumbprint -eq $certificate.Thumbprint
        } |
        Select-Object -First 1

    if (-not $trustedCertificate) {
        Write-Output "CER=$cerPath"
        Write-Output "RESULT=NEEDS_TRUST"
        exit 10
    }

    Write-Step "[5/6] Verifying the trusted signature..."

    & $SignTool verify `
        /pa `
        /v `
        $Msix

    if ($LASTEXITCODE -ne 0) {
        throw "Signature verification failed. SignTool exit code: $LASTEXITCODE"
    }

    Write-Step "[6/6] Installing the package..."

    $packageIdentityName = Get-PackageIdentityName `
        -PackagePath $Msix

    if ($packageIdentityName) {
        $existingPackages = @(
            Get-AppxPackage `
                -Name $packageIdentityName `
                -ErrorAction SilentlyContinue
        )

        foreach ($existingPackage in $existingPackages) {
            Write-Step (
                "Removing the existing current-user package: " +
                $existingPackage.PackageFullName
            )

            Remove-AppxPackage `
                -Package $existingPackage.PackageFullName `
                -ErrorAction Stop
        }

        if ($existingPackages.Count -gt 0) {
            Start-Sleep -Milliseconds 700
        }
    }

    try {
        Add-AppxPackage `
            -Path $Msix `
            -ForceApplicationShutdown `
            -ForceUpdateFromAnyVersion
    }
    catch {
        $deploymentText = $_ | Out-String

        if ($deploymentText -match "0x80073CFB") {
            throw (
                "Windows still reports package identity conflict 0x80073CFB. " +
                "The conflicting package is probably installed for another " +
                "Windows account or provisioned system-wide. Increase the " +
                "package version or remove that other installation."
            )
        }

        throw
    }

    Write-Output "RESULT=INSTALLED"
    exit 0
}
catch {
    Write-Error $_
    Write-Output "RESULT=FAILED"
    exit 1
}
