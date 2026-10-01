[CmdletBinding()]
param([switch]$PassThru)
$ErrorActionPreference = 'Stop'
$candidates = @()
$registeredVersions = @{}
if ($env:BONGO_CAT_ISCC) { $candidates += $env:BONGO_CAT_ISCC.Trim().Trim('"') }
$command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($command) { $candidates += $command.Source }
foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA\Programs")) {
    if ($base) { $candidates += Join-Path $base 'Inno Setup 6\ISCC.exe' }
}
foreach ($key in @(
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
    'HKCU:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1')) {
    $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
    if ($entry -and $entry.InstallLocation) {
        $path = [IO.Path]::GetFullPath((Join-Path $entry.InstallLocation.Trim().Trim('"') 'ISCC.exe'))
        $candidates += $path
        $registeredVersions[$path] = [string]$entry.DisplayVersion
    }
}
$rejected = @()
foreach ($candidate in ($candidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    $path = (Resolve-Path -LiteralPath $candidate).Path
    $fileVersion = (Get-Item -LiteralPath $path).VersionInfo
    $version = [version]::new($fileVersion.FileMajorPart, $fileVersion.FileMinorPart,
        $fileVersion.FileBuildPart, $fileVersion.FilePrivatePart)
    $versionSource = 'FileVersion'
    # Some official builds report 0.0.0.0 in ISCC.exe (and ISCmplr.dll).
    # Use DisplayVersion only from a registration for this exact compiler path.
    if ($version.Major -eq 0 -and $registeredVersions.ContainsKey($path)) {
        $parsed = $null
        if ([version]::TryParse($registeredVersions[$path], [ref]$parsed)) {
            $version = $parsed
            $versionSource = 'Registry DisplayVersion'
        }
    }
    if ($version.Major -eq 6 -and $version.Minor -ge 3) {
        if ($PassThru) {
            return [PSCustomObject]@{
                Path = $path
                Version = $version
                VersionSource = $versionSource
            }
        }
        return $path
    }
    $rejected += "$path (version $version, $versionSource)"
}
$details = if ($rejected.Count) { " Found but rejected: $($rejected -join '; ')." } else { '' }
throw "Inno Setup 6.3 or newer (6.x) is required for packaging. Install from https://jrsoftware.org/isdl.php or set BONGO_CAT_ISCC to ISCC.exe. Normal application builds do not require Inno Setup.$details"
