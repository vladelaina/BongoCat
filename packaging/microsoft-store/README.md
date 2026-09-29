# Microsoft Store packaging

The protected `store-package` job in `.github/workflows/ci.yml` creates a raw
`bongocat_<app-version>_x64.msix` artifact on pushes to the upstream `main`
branch of `vladelaina/BongoCat`. The artifact is intentionally unsigned:
Partner Center signs accepted MSIX packages with the Microsoft Store
certificate during submission.

## Store identity

- Package name: `vladelaina.bongocat`
- Publisher: `CN=5503A135-7FA4-466B-815C-DBE627F4065F`
- Publisher display name: `vladelaina`
- Package family name: `vladelaina.bongocat_hnew8t3b8e0t6`
- Package SID: `S-1-15-2-1569586416-2650217304-2351454042-2692954497-3079119466-4141654193-1648471479`
- Store ID: `9P41MLSX72XW`

The package name and publisher are stored in `AppxManifest.xml.in`. The PFN
and package SID are derived by Windows and must not be used as signing
secrets. The Store package is x64; the separate Windows x86 workflow target
only publishes its desktop installer and portable executable.

The manifest declares a system-managed `BongoCat.lnk` on the current user's
desktop through the `desktop7:windows.shortcut` extension. Windows creates the
link during package registration and removes it during package removal. The
shortcut extension is supported starting with Windows build 19645; older
supported Windows releases ignore that extension while continuing to install
and run the app.

On the first normal launch after installation/update, the packaged app converts
the existing desktop file shortcut into an AppsFolder shell-item shortcut using
its application user model ID. This makes Explorer's **Run as administrator**
use the same packaged activation route as Start. Direct `runas` on the executable
under `WindowsApps` can fail with Access denied even with `allowElevation`.
The filename remains manifest-owned for removal by Windows. Missing shortcuts,
links to the standalone edition, and links with custom arguments are left alone.
An update may recreate the file shortcut; launch normally once to convert it again.

To verify, install the new package, launch it normally once, exit it, and use the
desktop shortcut's **Run as administrator** menu. Verify elevation in Task Manager
and also check ordinary launch, a second launch, and uninstall cleanup. Use the
Explorer context menu for this check: PowerShell `Start-Process -Verb RunAs` on
an AppsFolder shortcut does not necessarily invoke that same shell-item verb.

## App data isolation

Ordinary EXE, installer, and portable launches store data under
`%LOCALAPPDATA%\BongoCat`. When the process has an MSIX package identity,
BongoCat instead resolves the package-private virtualization location as
`%LOCALAPPDATA%\Packages\<PFN>\LocalCache\Local\BongoCat`. Configuration,
session state, imported models, caches, and logs therefore remain separate
between the desktop and Microsoft Store versions. An explicit
`--storage-root` argument remains isolated at the requested location.
In particular, the desktop settings file is
`%LOCALAPPDATA%\BongoCat\config\settings.json`, while the Store settings file
is `%LOCALAPPDATA%\Packages\<PFN>\LocalCache\Local\BongoCat\config\settings.json`.

Package identity is detected at runtime with `GetCurrentPackageFullName`, and
the selected mode and resolved storage path are written to `BongoCat.log`.

The app version is declared only in the top-level `CMakeLists.txt` and read by
all Store scripts through `packaging/get-project-version.ps1`. MSIX versions
must use four numeric components, have a non-zero major component, and use a
zero revision. The helper therefore converts `major.minor.patch` to the same
version with a `.0` suffix, for example `1.1.0` to `1.1.0.0`. The app version
remains in the filename so GitHub artifacts are easy to identify.

## Actions SDK setup

Live2D's licensed Cubism SDK is ignored by Git and cannot be checked into this
public repository. The protected job downloads the pinned Cubism 5-r.5 archive
and GLEW 2.2.0 archive from their official upstream URLs and verifies both
SHA-256 hashes before building. A repository owner may override either URL and
hash with these secrets when an approved mirror is required:

- `CUBISM_SDK_ARCHIVE_URL` and `CUBISM_SDK_ARCHIVE_SHA256`
- `CUBISM_GLEW_ARCHIVE_URL` and `CUBISM_GLEW_ARCHIVE_SHA256`

Running the protected workflow is subject to the Live2D Cubism SDK licenses;
the repository owner is responsible for satisfying their release terms.

The job verifies the digests and required files before configuring CMake with
`BONGO_CAT_REQUIRE_CUBISM=ON`; it never uploads the diagnostic backend.

## Local build and install check

`build.bat Release -Package` builds the portable executable, Inno Setup
installer, and unsigned MSIX in `build-cubism\dist`. It requires Inno Setup
6.3+ and the Windows 10/11 SDK, and runs the same MSIX validation as CI.
The MSIX filename is recorded in `build-cubism\bongocat-msix-name.txt` for
local wrapper scripts. The desktop `run_bongocat_inno.bat` wrapper also
copies the MSIX to the desktop alongside the portable and installer files.

On Windows with the Windows 10/11 SDK and the local Cubism SDK installed:

```powershell
cmake -S . -B build-cubism -G "Visual Studio 17 2022" -A x64 `
  -DBONGO_CAT_REQUIRE_CUBISM=ON -DBONGO_CAT_WARNINGS_AS_ERRORS=ON
cmake --build build-cubism --config Release --target bongo_cat --parallel 2

.\packaging\microsoft-store\build-store-package.ps1 `
  -ExecutablePath .\build-cubism\Release\BongoCat.exe
```

The script writes the unsigned Store submission package to
`output\microsoft-store\bongocat_<app-version>_x64.msix`. Upload that file
directly to Partner Center. It cannot be installed locally until Microsoft
Store signs it. Validate the exact Identity, PFN, manifest and executable
versions, architecture, payload, and unsigned submission state with:

```powershell
$projectVersion = .\packaging\get-project-version.ps1
$msix = ".\output\microsoft-store\bongocat_$($projectVersion.AppVersion)_x64.msix"
.\packaging\microsoft-store\validate-store-package.ps1 `
  -PackagePath $msix
```

The protected Actions job runs this validation before uploading its artifact.

To test local deployment before submission, explicitly build a separate
self-signed package:

```powershell
$projectVersion = .\packaging\get-project-version.ps1
$localBase = ".\output\microsoft-store\bongocat_$($projectVersion.AppVersion)_x64-local-test"
.\packaging\microsoft-store\build-store-package.ps1 `
  -ExecutablePath .\build-cubism\Release\BongoCat.exe `
  -SignForLocalTesting

# Run these certificate/deployment commands from an elevated PowerShell.
Import-Certificate `
  "$localBase.cer" `
  -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
Add-AppxPackage "$localBase.msix"
Get-AppxPackage -Name vladelaina.bongocat
```

The `-local-test` certificate and package are only for sideload testing. Do not
upload either one to Partner Center. The GitHub Actions job never passes
`-SignForLocalTesting` and always emits the unsigned Store submission package.
