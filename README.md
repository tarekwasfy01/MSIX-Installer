# MSIX Installer

A lightweight native Windows application for signing and installing `.msix` packages through a simple drag-and-drop interface.

The application is designed for distributing MSIX packages outside the Microsoft Store, such as internal builds, test versions, GitHub releases, and direct-download applications.

## Download:

https://github.com/tarekwasfy01/MSIX-Installer/releases/download/MSIXInstaller/MSIXInstaller.exe

<img width="1601" height="983" alt="ChatGPT Image 20  Juli 2026, 13_45_37" src="https://github.com/user-attachments/assets/bf2104a6-12f4-41e1-952f-ede614f53a9f" />


## Features

* Drag and drop an `.msix` package into the application
* Select an MSIX package using the centered **Choose File** button
* Sign unsigned MSIX packages automatically
* Install the signing certificate when required
* Install the signed MSIX package
* Dark native Windows interface
* Clear installation status and error messages
* No PowerShell window displayed during normal use
* Distributed as a single executable
* No separate installer required


## Usage

1. Download `MSIXInstaller.exe` from the GitHub Releases page.
2. Start the application.
3. Drag an `.msix` file into the large drop area.

Alternatively, click **Choose File** and select the MSIX package manually.

4. Click **Install**.
5. Accept the Windows administrator prompt when it appears.
6. Wait until the signing and installation process is complete.

## How It Works

The application performs the following operations:

1. Validates the selected MSIX package.
2. Locates the required Windows SDK signing tools.
3. Creates or loads the configured signing certificate.
4. Signs the selected package.
5. Adds the certificate to the required Windows certificate store.
6. Installs the signed MSIX package.

The configured publisher information and certificate password are not displayed in the user interface.

## Requirements

* Windows 10 or Windows 11
* 64-bit Windows
* Administrator permissions
* Windows App Installer
* Windows SDK Signing Tools, when they are not bundled with the application
* A valid MSIX package

The package identity publisher must match the certificate subject used for signing.

Example:

```xml
<Identity
    Name="ExampleCompany.ExampleApp"
    Publisher="CN=YOUR-PUBLISHER-ID"
    Version="1.0.0.0"
    ProcessorArchitecture="x64" />
```

## Supported Files

The application currently supports:

```text
.msix
```

Support for `.msixbundle`, `.appx`, and `.appxbundle` may be added in a future release.

## Common Errors

### The MSIX package cannot be signed

The selected file may be damaged, incomplete, or not be a valid MSIX package.

Verify the package with:

```powershell
MakeAppx.exe unpack /p "Application.msix" /d "UnpackedPackage"
```

### SignTool was not found

Install the Windows SDK and enable the following component:

```text
Windows SDK Signing Tools for Desktop Apps
```

### Publisher does not match

The `Publisher` value in `AppxManifest.xml` must exactly match the subject of the signing certificate.

### Certificate is not trusted

The certificate must be installed in the appropriate certificate store before Windows can install the signed package.

### The package is already installed

Remove the existing version or increase the package version in `AppxManifest.xml`.

Example:

```xml
Version="1.0.1.0"
```

### A newer version is already installed

Windows normally prevents installation of a package with a lower version number. Build the package with a version higher than the installed version.

## Security Notice

Installing MSIX packages from outside the Microsoft Store requires trust in the package publisher.

Only install packages obtained from a source you trust.

The application may install a signing certificate on the local computer. Review the certificate and package source before continuing.

Do not distribute private production certificates or expose certificate passwords in a public repository.

## Building

Open the project in a supported Visual Studio installation and compile it for Windows x64.

For a native MSVC build, initialize the Visual Studio developer environment:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
```

Then run the included build script:

```bat
BUILD.bat
```

The generated executable will be placed in the configured output directory.

Example:

```text
output\MSIXInstaller.exe
```

## One-File Distribution

The release build is designed to produce a single executable containing the required application resources.

Users only need:

```text
MSIXInstaller.exe
```

Certificate files, passwords, temporary signed packages, and other sensitive build resources should not be committed to the public repository.

## Disclaimer

This project is provided without warranty.

The developer is not responsible for damaged packages, installation failures, certificate problems, data loss, or packages installed from untrusted sources.

Always keep backups of the original MSIX package.



