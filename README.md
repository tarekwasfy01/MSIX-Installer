# MSIX Installer — native C++ one-file build

This project creates a compact native Windows application with:

- one large MSIX drag-and-drop area
- a **Choose MSIX** button inside the drop area
- one blue **Install** button
- a blue application background and blue frame
- an entirely English interface
- no visible Publisher or PFX password fields
- embedded Publisher and PFX password settings
- embedded PowerShell signing workflow
- embedded `signtool.exe`
- statically linked MSVC runtime
- no companion files beside the final EXE

## Build the standalone EXE

Run:

`BUILD_ONEFILE_EXE.bat`

The final distributable is:

`output\MSIXInstaller.exe`

Only that EXE needs to be distributed. At runtime it temporarily extracts the
embedded signing script and SignTool, runs the signing workflow, and removes
the temporary copies again.

## Install all required build tools automatically

Run:

`INSTALL_EVERYTHING_AND_BUILD.bat`

This opens the Visual Studio Installer with UAC, installs the native C++ build
tools and Windows SDK, and then builds the one-file EXE.

## Hidden settings

The following values are compiled into `src\main.cpp`:

- Publisher: `CN=85434AF5-74BD-4E8F-90F0-13F9EA1270DE`
- PFX password: `SupremeMapsDownloader2026!`

They are hidden from the interface but are not cryptographically secret.
Anyone who reverse-engineers the executable may still recover embedded values.

## Certificate trust

Signing runs without starting the application as administrator. A self-signed
MSIX certificate must still be trusted by the computer once. When required,
the application opens the standard Windows certificate wizard.

Choose:

1. Install Certificate
2. Local Machine
3. Place all certificates in the following store
4. Trusted People
5. Finish

Then click **Install** again.

## Store package

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\BUILD_STORE_MSIX.ps1 `
  -PackageName "TarekWasfy.MSIXInstaller" `
  -Publisher "CN=85434AF5-74BD-4E8F-90F0-13F9EA1270DE" `
  -Version "1.0.0.0"
```

The Store manifest contains `runFullTrust`, but does not contain
`allowElevation` or `packageManagement`.


## V2 fixes

- Before installing, the app now removes an already installed package with the same package identity for the current user.
- The UI is now dark.
- Only the **Install** button is blue.
- The **Choose MSIX** button and the drop area use dark gray styling.
