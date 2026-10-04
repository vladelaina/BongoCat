[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$PackageVersion = (Get-Content -LiteralPath "$PSScriptRoot/../../packaging/windows/inno-version.txt" -Raw).Trim()
choco upgrade innosetup --version $PackageVersion `
    --source https://community.chocolatey.org/api/v2/ `
    --force --allow-downgrade --fail-on-unfound --no-progress --yes
if ($LASTEXITCODE -ne 0) { throw "Pinned Inno Setup installation failed (exit $LASTEXITCODE)" }
$info = & "$PSScriptRoot/../../packaging/windows/find-inno.ps1" -PassThru
$compiler = $info.Path
if ($info.Version.ToString(3) -ne $PackageVersion) {
    throw "Expected Inno Setup $PackageVersion, found $($info.Version) at $compiler"
}
$compilerDir = Split-Path $compiler -Parent
$env:Path = "$compilerDir;$env:Path"
$compilerDir | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8
Write-Host "Inno Setup compiler: $compiler"
