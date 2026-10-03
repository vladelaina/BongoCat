[CmdletBinding()]
param(
    [string]$ArchiveUrl = $(if ($env:CUBISM_SDK_ARCHIVE_URL) {
            $env:CUBISM_SDK_ARCHIVE_URL
        } else {
            'https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-5-r.5.zip'
        }),
    [string]$ExpectedSha256 = $(if ($env:CUBISM_SDK_ARCHIVE_SHA256) {
            $env:CUBISM_SDK_ARCHIVE_SHA256
        } else {
            '7FF3A4BBC19C0A8728965AA522AB77EB11B252916453E68A8A78D3B71188BB12'
        }),
    [string]$GlewArchiveUrl = $(if ($env:CUBISM_GLEW_ARCHIVE_URL) {
            $env:CUBISM_GLEW_ARCHIVE_URL
        } else {
            'https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip'
        }),
    [string]$ExpectedGlewSha256 = $(if ($env:CUBISM_GLEW_ARCHIVE_SHA256) {
            $env:CUBISM_GLEW_ARCHIVE_SHA256
        } else {
            'A9046A913774395A095EDCC0B0AC2D81C3AACCA61787B39839B941E9BE14E0D4'
        }),
    [string]$Destination
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Destination) {
    $Destination = Join-Path $repositoryRoot 'vendor/CubismSdkForNative'
}

if ([string]::IsNullOrWhiteSpace($ArchiveUrl) -or
    [string]::IsNullOrWhiteSpace($ExpectedSha256) -or
    [string]::IsNullOrWhiteSpace($GlewArchiveUrl) -or
    [string]::IsNullOrWhiteSpace($ExpectedGlewSha256)) {
    throw 'Cubism SDK and GLEW archive URLs and SHA-256 digests are required.'
}

$ExpectedSha256 = $ExpectedSha256.Trim().ToUpperInvariant()
if ($ExpectedSha256 -notmatch '^[0-9A-F]{64}$') {
    throw "CUBISM_SDK_ARCHIVE_SHA256 must be a 64-character SHA-256 digest."
}
$ExpectedGlewSha256 = $ExpectedGlewSha256.Trim().ToUpperInvariant()
if ($ExpectedGlewSha256 -notmatch '^[0-9A-F]{64}$') {
    throw "CUBISM_GLEW_ARCHIVE_SHA256 must be a 64-character SHA-256 digest."
}

$Destination = [System.IO.Path]::GetFullPath($Destination)
$repositoryPrefix = $repositoryRoot.TrimEnd('\', '/') +
    [IO.Path]::DirectorySeparatorChar
if (-not $Destination.StartsWith($repositoryPrefix,
        [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path $Destination -Leaf) -ne 'CubismSdkForNative') {
    throw "Refusing to replace an unexpected Cubism SDK destination: $Destination"
}
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("BongoCatCubism_" + [guid]::NewGuid())
$archivePath = Join-Path $temporaryRoot "cubism-sdk.zip"
$glewArchivePath = Join-Path $temporaryRoot "glew.zip"
$extractPath = Join-Path $temporaryRoot "extracted"

try {
    New-Item -ItemType Directory -Force -Path $temporaryRoot, $extractPath | Out-Null
    Write-Host "Downloading the pinned Cubism SDK archive..."
    Invoke-WebRequest -Uri $ArchiveUrl -OutFile $archivePath -UseBasicParsing

    $actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualSha256 -ne $ExpectedSha256) {
        throw "Cubism SDK archive hash mismatch: expected $ExpectedSha256, got $actualSha256."
    }
    Write-Host "Cubism SDK archive SHA-256: $actualSha256"

    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractPath -Force
    $rootCandidate = Get-Item -LiteralPath $extractPath
    if (-not (Test-Path (Join-Path $rootCandidate.FullName "Core/include/Live2DCubismCore.h"))) {
        $rootCandidate = Get-ChildItem -LiteralPath $extractPath -Directory -Recurse |
            Where-Object { Test-Path (Join-Path $_.FullName "Core/include/Live2DCubismCore.h") } |
            Select-Object -First 1
    }
    if (-not $rootCandidate) {
        throw "The archive does not contain a CubismSdkForNative Core/include tree."
    }

    $required = @(
        (Join-Path $rootCandidate.FullName "Core/include/Live2DCubismCore.h"),
        (Join-Path $rootCandidate.FullName "Core/lib/windows/x86/143/Live2DCubismCore_MT.lib"),
        (Join-Path $rootCandidate.FullName "Core/lib/windows/x86_64/143/Live2DCubismCore_MT.lib"),
        (Join-Path $rootCandidate.FullName "Core/lib/linux/x86_64/libLive2DCubismCore.a"),
        (Join-Path $rootCandidate.FullName "Core/lib/macos/x86_64/libLive2DCubismCore.a"),
        (Join-Path $rootCandidate.FullName "Core/lib/macos/arm64/libLive2DCubismCore.a"),
        (Join-Path $rootCandidate.FullName "Framework/CMakeLists.txt")
    )
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "The Cubism SDK archive is incomplete; missing $path."
        }
    }

    Write-Host "Downloading the pinned GLEW 2.2.0 archive..."
    Invoke-WebRequest -Uri $GlewArchiveUrl -OutFile $glewArchivePath -UseBasicParsing
    $actualGlewSha256 = (Get-FileHash -LiteralPath $glewArchivePath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualGlewSha256 -ne $ExpectedGlewSha256) {
        throw "GLEW archive hash mismatch: expected $ExpectedGlewSha256, got $actualGlewSha256."
    }
    Write-Host "GLEW archive SHA-256: $actualGlewSha256"
    Expand-Archive -LiteralPath $glewArchivePath -DestinationPath $extractPath -Force
    $glewRoot = Get-ChildItem -LiteralPath $extractPath -Directory -Recurse |
        Where-Object {
            (Test-Path (Join-Path $_.FullName 'build/cmake/CMakeLists.txt')) -and
            (Test-Path (Join-Path $_.FullName 'include/GL/glew.h')) -and
            (Test-Path (Join-Path $_.FullName 'src/glew.c'))
        } |
        Select-Object -First 1
    if (-not $glewRoot) {
        throw 'The GLEW archive does not contain the expected CMake, header, and source trees.'
    }

    Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $rootCandidate.FullName -Force | Copy-Item -Destination $Destination -Recurse -Force
    $glewDestination = Join-Path $Destination 'Samples/OpenGL/thirdParty/glew'
    New-Item -ItemType Directory -Force -Path $glewDestination | Out-Null
    Get-ChildItem -LiteralPath $glewRoot.FullName -Force |
        Copy-Item -Destination $glewDestination -Recurse -Force
    $requiredGlew = @(
        (Join-Path $glewDestination 'build/cmake/CMakeLists.txt'),
        (Join-Path $glewDestination 'include/GL/glew.h'),
        (Join-Path $glewDestination 'src/glew.c')
    )
    foreach ($path in $requiredGlew) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "The restored GLEW tree is incomplete; missing $path."
        }
    }
    Write-Host "Cubism SDK restored to $Destination"
}
finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}
