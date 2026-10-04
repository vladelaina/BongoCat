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
    $lastPreparationStep = ''
    & $Executable @Arguments 2>&1 | ForEach-Object {
        $line = $_.ToString()
        if ($line -match '^\s*(Updating .+|Parsing .+|Compressing .+)') {
            $lastPreparationStep = $line.Trim()
        }
        Add-Content -LiteralPath $log -Value $line -Encoding utf8 -ErrorAction Stop
        Write-Host $line
    }
    $status = $LASTEXITCODE
    if ($status -ne 0) {
        $failure = [Exception]::new("$(Split-Path $Executable -Leaf) failed (exit $status). Log: $log")
        $failure.Data['ResourcePreparationFailed'] = (
            (Split-Path $Executable -Leaf) -ieq 'ISCC.exe' -and $status -eq 2 -and
            $lastPreparationStep -match '^Updating (icons|version info) \(Setup(?:CustomStyle)?\.e32\)$')
        throw $failure
    }
}

try {
    $iscc = & "$PSScriptRoot/find-inno.ps1"
    Write-Host "Inno Setup compiler: $iscc"
    # A fresh staging directory avoids shipping files left by an older build.
    $stage = Join-Path $BuildDir ('inno-stage-' + [Guid]::NewGuid().ToString('N'))
    try {
        $payload = Join-Path $stage 'payload'
        Invoke-PackagingCommand -Executable 'cmake' -Arguments @(
            '--install', $BuildDir, '--config', $Configuration,
            '--component', 'Runtime', '--prefix', $payload)
        # ISCC rewrites a temporary Setup.e32 beside its output. Keep those
        # files away from existing release artifacts and use a fresh path on
        # retry if Windows resource preparation fails. Never retry script or
        # payload errors, and never rebuild the application here.
        for ($attempt = 1; $attempt -le 2; $attempt++) {
            $installerOutput = Join-Path $stage "output-$attempt"
            New-Item -ItemType Directory -Path $installerOutput -Force | Out-Null
            try {
                Invoke-PackagingCommand -Executable $iscc -Arguments @(
                    "/DPayloadDir=$payload", "/O$installerOutput",
                    (Join-Path $BuildDir 'BongoCat.iss'))
                break
            } catch {
                if ($attempt -eq 2 -or -not $_.Exception.Data['ResourcePreparationFailed']) {
                    throw
                }
                $notice = 'Inno Setup resource preparation failed; retrying once in a fresh output directory.'
                Add-Content -LiteralPath $log -Value $notice -Encoding utf8
                Write-Host $notice
            }
        }
        $output = Join-Path $BuildDir "dist/$PackageName-setup.exe"
        New-Item -ItemType Directory -Path (Split-Path $output -Parent) -Force | Out-Null
        Move-Item -LiteralPath (Join-Path $installerOutput "$PackageName-setup.exe") -Destination $output -Force
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
