[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$PackageName
)
$ErrorActionPreference = 'Stop'
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$log = Join-Path $BuildDir 'installer.log'
Set-Content -LiteralPath $log -Value 'BongoCat installer packaging' -Encoding utf8

function Invoke-PackagingCommand {
    param([string]$Executable, [string[]]$Arguments)
    # Windows PowerShell wraps native stderr as ErrorRecord objects. Keep
    # collecting output until the process exits, then check its actual status.
    $ErrorActionPreference = 'Continue'
    & $Executable @Arguments 2>&1 | ForEach-Object {
        $line = $_.ToString()
        Add-Content -LiteralPath $log -Value $line -Encoding utf8 -ErrorAction Stop
        Write-Host $line
    }
    $status = $LASTEXITCODE
    if ($status -ne 0) {
        throw "$(Split-Path $Executable -Leaf) failed (exit $status). Log: $log"
    }
}

try {
    $iscc = & "$PSScriptRoot/find-inno.ps1"
    Write-Host "Inno Setup compiler: $iscc"
    # A fresh staging directory avoids shipping files left by an older build.
    $stage = Join-Path $BuildDir ('inno-stage-' + [Guid]::NewGuid().ToString('N'))
    try {
        Invoke-PackagingCommand -Executable 'cmake' -Arguments @(
            '--install', $BuildDir, '--config', $Configuration,
            '--component', 'Runtime', '--prefix', $stage)
        Invoke-PackagingCommand -Executable $iscc -Arguments @(
            "/DPayloadDir=$stage", (Join-Path $BuildDir 'BongoCat.iss'))
        $output = Join-Path $BuildDir "dist/$PackageName-setup.exe"
        # MSBuild launches Windows PowerShell with the runner's inherited module
        # paths; Get-FileHash may be unavailable there. Use the .NET stream API.
        $sha256 = [Security.Cryptography.SHA256]::Create()
        $stream = $null
        try {
            $stream = [IO.File]::OpenRead($output)
            $hash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
        } finally {
            if ($null -ne $stream) { $stream.Dispose() }
            $sha256.Dispose()
        }
        "$hash  $PackageName-setup.exe" | Set-Content -LiteralPath "$output.sha256" -Encoding ascii
    } finally {
        # Only remove the unique stage directory directly below the build directory.
        $stageFull = [IO.Path]::GetFullPath($stage)
        $buildFull = [IO.Path]::GetFullPath($BuildDir).TrimEnd('\', '/')
        if ((Split-Path $stageFull -Parent) -eq $buildFull -and
            (Split-Path $stageFull -Leaf) -match '^inno-stage-[0-9a-f]{32}$' -and
            (Test-Path -LiteralPath $stageFull)) {
            Remove-Item -LiteralPath $stageFull -Recurse -Force
        }
    }
} catch {
    $summary = $_.Exception.Message
    Add-Content -LiteralPath $log -Value $summary -Encoding utf8
    Write-Host 'Installer failure details:'
    Get-Content -LiteralPath $log -Tail 30 | ForEach-Object { Write-Host $_ }
    if ($env:GITHUB_ACTIONS -eq 'true') {
        $detail = (Get-Content -LiteralPath $log -Tail 12) -join "`n"
        $detail = $detail.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Host "::error title=Inno Setup packaging failed::$detail"
    }
    throw
}
