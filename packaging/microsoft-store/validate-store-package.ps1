[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,

    [ValidateSet('x86', 'x64')]
    [string]$ExpectedArchitecture = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = (Resolve-Path (Join-Path $scriptDirectory '..\..')).Path
$projectVersion = & (Join-Path $repositoryRoot 'packaging\get-project-version.ps1')
$ExpectedAppVersion = $projectVersion.AppVersion
$ExpectedPackageVersion = $projectVersion.StorePackageVersion
$PackagePath = (Resolve-Path $PackagePath).Path
if ((Split-Path $PackagePath -Leaf) -notmatch
    "^bongocat_$([regex]::Escape($ExpectedAppVersion))_") {
    throw "Package filename does not match app version $ExpectedAppVersion."
}

function Get-WindowsSdkTool {
    param([Parameter(Mandatory = $true)][string]$Name)

    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $sdkVersions = Get-ChildItem $sdkRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+' } |
        Sort-Object { [version]$_.Name } -Descending
    foreach ($sdkVersion in $sdkVersions) {
        $candidate = Join-Path $sdkVersion.FullName "x64\$Name"
        if (Test-Path $candidate) { return $candidate }
    }
    throw "$Name was not found. Install the Windows 10/11 SDK."
}

function Get-PeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw 'The packaged executable does not have an MZ header.'
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0x40 -or $peOffset -gt $stream.Length - 6) {
            throw 'The packaged executable has an invalid PE offset.'
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw 'The packaged executable does not have a PE signature.'
        }
        return $reader.ReadUInt16()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

if (-not ('BongoCatPackageIdentityNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using System.Text;
public static class BongoCatPackageIdentityNative {
    [DllImport("kernelbase.dll", CharSet = CharSet.Unicode)]
    public static extern int PackagePublisherIdFromPublisher(
        string publisher, ref uint length, StringBuilder publisherId);
}
'@
}

$signature = Get-AuthenticodeSignature -FilePath $PackagePath
if ($signature.Status -ne [Management.Automation.SignatureStatus]::NotSigned) {
    throw "Store submission package must be unsigned; status is $($signature.Status)."
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'BongoCatStoreValidation_' + [guid]::NewGuid())
try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    $makeAppx = Get-WindowsSdkTool 'makeappx.exe'
    & $makeAppx unpack /o /p $PackagePath /d $temporaryRoot | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "MakeAppx unpack failed with exit code $LASTEXITCODE."
    }

    $manifestPath = Join-Path $temporaryRoot 'AppxManifest.xml'
    [xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
    $identity = $manifest.Package.Identity
    $expectedIdentity = @{
        Name = 'vladelaina.bongocat'
        Publisher = 'CN=5503A135-7FA4-466B-815C-DBE627F4065F'
        Version = $ExpectedPackageVersion
        ProcessorArchitecture = $ExpectedArchitecture
    }
    foreach ($field in $expectedIdentity.Keys) {
        if ($identity.$field -ne $expectedIdentity[$field]) {
            throw "Manifest Identity $field is '$($identity.$field)'; expected '$($expectedIdentity[$field])'."
        }
    }

    $application = $manifest.Package.Applications.Application
    if ($application.Id -ne 'BongoCat' -or
        $application.Executable -ne 'BongoCat.exe' -or
        $application.EntryPoint -ne 'Windows.FullTrustApplication') {
        throw 'Manifest application entry does not describe the BongoCat full-trust executable.'
    }

    $namespaceManager = [Xml.XmlNamespaceManager]::new($manifest.NameTable)
    $namespaceManager.AddNamespace(
        'foundation',
        'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $namespaceManager.AddNamespace(
        'rescap',
        'http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities')
    foreach ($capability in @('runFullTrust', 'allowElevation')) {
        $declaration = $manifest.SelectSingleNode(
            "/foundation:Package/foundation:Capabilities/rescap:Capability[@Name='$capability']",
            $namespaceManager)
        if (-not $declaration) {
            throw "Manifest is missing required restricted capability: $capability."
        }
    }
    $namespaceManager.AddNamespace(
        'desktop7',
        'http://schemas.microsoft.com/appx/manifest/desktop/windows10/7')
    $shortcutExtension = $manifest.SelectSingleNode(
        "//*[local-name()='Application']/*[local-name()='Extensions']/" +
        "desktop7:Extension[@Category='windows.shortcut']",
        $namespaceManager)
    $shortcut = if ($shortcutExtension) {
        $shortcutExtension.SelectSingleNode('desktop7:Shortcut', $namespaceManager)
    }
    else {
        $null
    }
    if (-not $shortcutExtension -or
        $shortcutExtension.Executable -ne 'BongoCat.exe' -or
        $shortcutExtension.EntryPoint -ne 'Windows.FullTrustApplication' -or
        -not $shortcut -or
        $shortcut.File -ne '$(Desktop)\BongoCat.lnk' -or
        $shortcut.Icon -ne 'BongoCat.exe') {
        throw 'Manifest does not declare the expected system-managed desktop shortcut.'
    }

    $requiredFiles = @(
        'BongoCat.exe',
        'Assets\StoreLogo.png',
        'Assets\Square44x44Logo.png',
        'Assets\Square150x150Logo.png'
    )
    foreach ($relativePath in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $temporaryRoot $relativePath))) {
            throw "Required package payload is missing: $relativePath"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $temporaryRoot 'AppxSignature.p7x')) {
        throw 'Unsigned Store submission package unexpectedly contains AppxSignature.p7x.'
    }

    $executablePath = Join-Path $temporaryRoot 'BongoCat.exe'
    $versionInfo = (Get-Item -LiteralPath $executablePath).VersionInfo
    $packagedAppVersion = '{0}.{1}.{2}' -f $versionInfo.FileMajorPart,
        $versionInfo.FileMinorPart, $versionInfo.FileBuildPart
    if ($packagedAppVersion -ne $ExpectedAppVersion) {
        throw "Packaged executable version is $packagedAppVersion; expected $ExpectedAppVersion."
    }

    $machine = Get-PeMachine $executablePath
    $expectedMachine = if ($ExpectedArchitecture -eq 'x64') { 0x8664 } else { 0x014C }
    if ($machine -ne $expectedMachine) {
        throw ('Packaged executable machine is 0x{0:X4}; expected {1}.' -f
            $machine, $ExpectedArchitecture)
    }

    $publisherLength = 0
    $publisherResult = [BongoCatPackageIdentityNative]::PackagePublisherIdFromPublisher(
        $identity.Publisher, [ref]$publisherLength, $null)
    if ($publisherResult -ne 122) {
        throw "Publisher ID sizing failed with Windows error $publisherResult."
    }
    $publisherId = [Text.StringBuilder]::new([int]$publisherLength)
    $publisherResult = [BongoCatPackageIdentityNative]::PackagePublisherIdFromPublisher(
        $identity.Publisher, [ref]$publisherLength, $publisherId)
    if ($publisherResult -ne 0) {
        throw "Publisher ID calculation failed with Windows error $publisherResult."
    }
    $packageFamilyName = "$($identity.Name)_$publisherId"
    if ($packageFamilyName -ne 'vladelaina.bongocat_hnew8t3b8e0t6') {
        throw "Derived package family name is unexpected: $packageFamilyName"
    }

    Write-Host 'Microsoft Store package validation passed.'
    Write-Host "Identity: $($identity.Name)"
    Write-Host "Version: $($identity.Version)"
    Write-Host "Package family name: $packageFamilyName"
    Write-Host 'Signature: unsigned (Partner Center submission)'
}
finally {
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $resolvedTemporaryRoot = [IO.Path]::GetFullPath($temporaryRoot)
    if ($resolvedTemporaryRoot.StartsWith($tempPrefix,
            [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path $resolvedTemporaryRoot -Leaf) -like
            'BongoCatStoreValidation_*') {
        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
