[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)]
    [ValidateSet('linux-x64', 'macos-x64', 'macos-arm64')][string]$Platform,
    [switch]$SkipSmoke
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$archivePath = (Resolve-Path -LiteralPath $Archive).Path
$testRunner = Join-Path $PSScriptRoot 'test-unix.sh'
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("BongoCatPackage_" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
try {
    Push-Location $temporaryRoot
    try {
        cmake -E tar xf $archivePath
        if ($LASTEXITCODE -ne 0) { throw 'Package extraction failed' }
        $roots = @(Get-ChildItem -LiteralPath $temporaryRoot -Directory)
        if ($roots.Count -ne 1 -or $roots[0].Name -notmatch "^BongoCat-[0-9].*-$Platform$") {
            throw 'Expected one production BongoCat package directory'
        }
        $root = $roots[0].FullName
        if ($Platform.StartsWith('macos-')) {
            $executable = Join-Path $root 'BongoCat.app/Contents/MacOS/BongoCat'
            $assets = Join-Path $root 'BongoCat.app/Contents/Resources/assets'
        } else {
            $executable = Join-Path $root 'BongoCat'
            $assets = Join-Path $root 'assets'
        }
        $required = @($executable) + @(
            'bongocat.png', 'locales/en-US.json',
            'models/standard/cat.model3.json',
            'models/standard/demomodel.moc3',
            'models/standard/demomodel.1024/texture_00.png',
            'FrameworkShaders/VertShaderSrc.vert',
            'FrameworkShaders/FragShaderSrc.frag',
            'FrameworkShaders/VertShaderSrcBlend.vert',
            'FrameworkShaders/FragShaderSrcBlend.frag'
        ) | ForEach-Object {
            if ([IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $assets $_ }
        }
        foreach ($path in $required) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-Item -LiteralPath $path).Length -eq 0) {
                throw "Package resource missing or empty: $path"
            }
        }
        $licenses = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter LICENSE)
        if ($licenses.Count) { throw "Unexpected loose LICENSE files: $($licenses.FullName -join ', ')" }
        Write-Host "Package layout verified: $Platform"
        if (-not $SkipSmoke) {
            $storage = Join-Path $temporaryRoot 'smoke-data'
            & bash $testRunner env BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN=1 `
                $executable --ci-smoke --ci-ignore-global-input `
                --ci-live2d-scenario=visual-consistency "--storage-root=$storage"
            if ($LASTEXITCODE -ne 0) { throw 'Packaged application smoke test failed' }
            $audits = @(Get-ChildItem -LiteralPath $storage -Recurse -File -Filter live2d-audit.txt)
            if ($audits.Count -ne 1) { throw 'Packaged application produced no unique Live2D audit' }
            $audit = Get-Content -LiteralPath $audits[0].FullName -Raw
            if ($audit -notmatch '(?m)^renderer=cubism-native\r?$' -or
                $audit -notmatch '(?m)^assertions=passed\r?$') {
                throw "Packaged Live2D verification failed: $audit"
            }
            Write-Host "Packaged native Live2D smoke test passed: $Platform"
        }
    } finally {
        Pop-Location
    }
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
}
