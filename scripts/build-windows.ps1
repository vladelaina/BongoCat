param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [string]$BuildDir = '',
    [ValidateSet('x64', 'Win32')]
    [string]$Architecture = 'x64',
    [ValidateRange(1, 64)]
    [int]$Jobs = 2,
    [switch]$SkipConfigure,
    [string[]]$Target = @('bongo_cat'),
    [switch]$RequireCubism,
    [switch]$Package,
    [switch]$Clean,
    # Reapply defaults to existing caches; -SkipConfigure reuses cached values.
    [bool]$OptimizeReleaseSize = $true,
    [bool]$OptimizeReleaseIpo = $true
)

$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$canonicalPath = $env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $canonicalPath
$env:VSLANG = '1033'
$env:MSBUILDDISABLENODEREUSE = '1'
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build-cubism' }
if (-not [IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$Target = @($Target | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($Target.Count -eq 0) {
    Write-Host 'At least one CMake target is required.'
    exit 1
}
if ($Clean -and $SkipConfigure) {
    Write-Host 'The -Clean and -SkipConfigure options cannot be used together.'
    exit 1
}
$esc = [char]27
$pink = '38;2;247;125;170'
$muted = '38;2;80;80;80'
$barWidth = 40

function Write-BuildProgress {
    param([int]$Percent, [string]$Message, [switch]$NewLine)
    $Percent = [Math]::Max(0, [Math]::Min(100, $Percent))
    $filled = [int][Math]::Floor($Percent * $script:barWidth / 100)
    $empty = $script:barWidth - $filled
    $fillText = '#' * $filled
    $emptyText = '.' * $empty
    $line = "`r${script:esc}[1m${script:esc}[$script:pink" +
        "m[$($Percent.ToString().PadLeft(3))%]${script:esc}[0m " +
        "${script:esc}[$script:pink" + "m$fillText" +
        "${script:esc}[$script:muted" + "m$emptyText${script:esc}[0m $Message"
    if ($NewLine) { Write-Host $line } else { Write-Host -NoNewline $line }
}

function Show-FailureLog {
    param([string[]]$Paths)
    Write-Host ''
    foreach ($path in $Paths) {
        if (-not (Test-Path -LiteralPath $path)) { continue }
        Get-Content -LiteralPath $path -Tail 30 | ForEach-Object { Write-Host $_ }
    }
}

function Write-GitHubBuildAnnotations {
    param([string]$Path)
    if ($env:GITHUB_ACTIONS -ne 'true' -or
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $pattern = 'FAILED:|fatal error|(?:warning|error) [A-Z]+\d+:|LNK\d+|MSB\d+: error|unresolved external|cannot open file|ninja: build stopped'
    Get-Content -LiteralPath $Path |
        Where-Object { $_ -match $pattern } |
        Select-Object -Last 30 |
        ForEach-Object {
            $message = $_.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
            Write-Output "::error title=Windows build failure::$message"
        }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host 'Error: CMake was not found in PATH.'
    exit 1
}

if ($Package -or $Target -contains 'package-installer') {
    try {
        $isccPath = & (Join-Path $root 'packaging/windows/find-inno.ps1')
        $env:Path = "$(Split-Path $isccPath -Parent);$env:Path"
        Write-Host "Inno Setup compiler: $isccPath"
    } catch {
        Write-Host $_.Exception.Message
        exit 1
    }
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    $rootPrefix = $root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $insideRoot = $BuildDir.StartsWith($rootPrefix,
        [StringComparison]::OrdinalIgnoreCase)
    if (-not $insideRoot -or (Split-Path $BuildDir -Leaf) -notlike 'build*') {
        Write-Host "Refusing to clean unexpected directory: $BuildDir"
        exit 1
    }
    Write-BuildProgress 2 'Cleaning previous build artifacts...'
    $depsPath = Join-Path $BuildDir '_deps'
    $preservedDeps = Join-Path (Split-Path $BuildDir -Parent) (
        '.bongocat-deps-' + [Guid]::NewGuid().ToString('N'))
    $hasDeps = Test-Path -LiteralPath $depsPath
    try {
        if ($hasDeps) {
            Move-Item -LiteralPath $depsPath -Destination $preservedDeps `
                -Force -ErrorAction Stop
        }
        Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction Stop
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
        if ($hasDeps) {
            Move-Item -LiteralPath $preservedDeps `
                -Destination (Join-Path $BuildDir '_deps') -Force `
                -ErrorAction Stop
        }
    } catch {
        if ($hasDeps -and (Test-Path -LiteralPath $preservedDeps) -and
            -not (Test-Path -LiteralPath $depsPath)) {
            New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
            Move-Item -LiteralPath $preservedDeps -Destination $depsPath `
                -Force -ErrorAction SilentlyContinue
        }
        Write-Host "Cleaning failed: $($_.Exception.Message)"
        exit 1
    }
}

if ($SkipConfigure -and -not (Test-Path -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt'))) {
    Write-Host "Cannot skip configuration because no CMake cache exists in: $BuildDir"
    exit 1
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$configureLog = Join-Path $BuildDir 'cmake_config.log'
$buildLog = Join-Path $BuildDir 'build.log'
$start = [DateTime]::UtcNow

Write-Host ''
Write-Host "BongoCat $Configuration build"
if ($SkipConfigure) {
    Write-BuildProgress 20 'Using existing CMake configuration.' -NewLine
} else {
    Write-BuildProgress 5 'Configuring project...'
    $configureArgs = @(
        '-S', $root, '-B', $BuildDir,
        '-G', 'Visual Studio 17 2022', '-A', $Architecture,
        '-DBONGO_CAT_WARNINGS_AS_ERRORS=ON',
        "-DBONGO_CAT_OPTIMIZE_RELEASE_SIZE=$($OptimizeReleaseSize.ToString().ToUpperInvariant())",
        "-DBONGO_CAT_OPTIMIZE_RELEASE_IPO=$($OptimizeReleaseIpo.ToString().ToUpperInvariant())"
    )
    if ($RequireCubism) { $configureArgs += '-DBONGO_CAT_REQUIRE_CUBISM=ON' }
    $configureWriter = New-Object IO.StreamWriter(
        $configureLog, $false, (New-Object Text.UTF8Encoding($false)))
    $configureActivity = 0
    $configurePercent = 5
    try {
        & cmake @configureArgs 2>&1 | ForEach-Object {
            $configureWriter.WriteLine($_.ToString())
            $configureActivity++
            $nextPercent = [Math]::Min(19,
                5 + [int][Math]::Floor([Math]::Sqrt($configureActivity)))
            if ($nextPercent -ne $configurePercent) {
                $configurePercent = $nextPercent
                Write-BuildProgress $configurePercent 'Configuring project...'
            }
        }
        $configureStatus = $LASTEXITCODE
    } finally {
        $configureWriter.Dispose()
    }
    if ($configureStatus -ne 0) {
        Write-BuildProgress 5 'Configuration failed.' -NewLine
        Write-GitHubBuildAnnotations $configureLog
        Show-FailureLog @($configureLog)
        Write-Host "Full log: $configureLog"
        exit 1
    }
    Write-BuildProgress 20 'Configuration complete.' -NewLine
}

Remove-Item -LiteralPath $buildLog -Force -ErrorAction SilentlyContinue
$projects = @(Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter '*.vcxproj' `
    -ErrorAction SilentlyContinue)
$compileItems = 0
foreach ($project in $projects) {
    $compileItems += @(Select-String -LiteralPath $project.FullName `
        -SimpleMatch '<ClCompile Include=' -ErrorAction SilentlyContinue).Count
}
$compileItems = [Math]::Max(1, $compileItems)
$buildArgs = @('--build', $BuildDir, '--config', $Configuration,
    '--target') + $Target + @('--parallel', $Jobs)
$lastPercent = 20
$compiled = 0
$activity = 0
Write-BuildProgress $lastPercent ("Building target(s): {0}..." -f ($Target -join ', '))
$buildWriter = New-Object IO.StreamWriter(
    $buildLog, $false, (New-Object Text.UTF8Encoding($false)))
try {
    & cmake @buildArgs 2>&1 | ForEach-Object {
        $line = $_.ToString()
        $buildWriter.WriteLine($line)
        $activity++
        if ($line -match '\.(c|cc|cpp|cxx)(\s|$)') { $compiled++ }
        $compilePercent = 20 + [int][Math]::Floor(
            [Math]::Min(1.0, $compiled / [double]$compileItems) * 75)
        $activityPercent = 20 + [int][Math]::Min(72,
            [Math]::Floor([Math]::Sqrt($activity) * 4))
        $percent = [Math]::Min(95,
            [Math]::Max($lastPercent,
                [Math]::Max($compilePercent, $activityPercent)))
        if ($percent -ne $lastPercent) {
            $lastPercent = $percent
            $message = if ($compiled -gt 0) {
                "Compiling ($compiled files)..."
            } else { "Building target(s): $($Target -join ', ')..." }
            Write-BuildProgress $lastPercent $message
        }
    }
    $buildStatus = $LASTEXITCODE
} finally {
    $buildWriter.Dispose()
}

if ($buildStatus -ne 0) {
    Write-BuildProgress $lastPercent 'Build failed.' -NewLine
    Write-GitHubBuildAnnotations $buildLog
    Show-FailureLog @($buildLog)
    Write-Host "Full log: $buildLog"
    exit $buildStatus
}

if ($Target -contains 'bongo_cat') {
    $output = Join-Path (Join-Path $BuildDir $Configuration) 'BongoCat.exe'
    if (-not (Test-Path -LiteralPath $output)) {
        Write-BuildProgress 95 'BongoCat.exe was not produced.' -NewLine
        exit 1
    }
}
$elapsedTotal = [DateTime]::UtcNow - $start
Write-BuildProgress 100 'Build complete.' -NewLine
Write-Host ("Build time: {0:mm\:ss}" -f $elapsedTotal)
if ($output) { Write-Host "Output: $output" }
Write-Host "Logs: $buildLog"

if ($Package) {
    Write-Host ''
    Write-Host 'Building versioned portable and installer packages...'
    $packageArgs = @('--build', $BuildDir, '--config', $Configuration,
        '--target', 'package-portable', 'package-installer', '--parallel', $Jobs)
    & cmake @packageArgs
    $packageStatus = $LASTEXITCODE
    if ($packageStatus -ne 0) {
        Write-Host 'Package generation failed.'
        Write-Host 'Ensure Inno Setup 6.3 or newer is installed (ISCC.exe).'
        Write-Host "Packaging build directory: $BuildDir"
        exit $packageStatus
    }
    $packageNameFile = Join-Path $BuildDir 'bongocat-package-name.txt'
    if (-not (Test-Path -LiteralPath $packageNameFile)) {
        Write-Host "Package name file was not produced: $packageNameFile"
        exit 1
    }
    $packageName = (Get-Content -LiteralPath $packageNameFile -Raw).Trim()
    $packageDist = Join-Path $BuildDir 'dist'
    $portable = Join-Path $packageDist "$packageName-portable.exe"
    $installer = Join-Path $packageDist "$packageName-setup.exe"
    if (-not (Test-Path -LiteralPath $portable) -or
        -not (Test-Path -LiteralPath $installer)) {
        Write-Host 'Package generation completed without both expected files.'
        Write-Host "Expected portable: $portable"
        Write-Host "Expected installer: $installer"
        exit 1
    }
    Write-Host "Portable package: $portable"
    Write-Host "Installer package: $installer"
}
exit 0
