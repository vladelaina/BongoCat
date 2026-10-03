[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Tag
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$projectVersion = & (Join-Path $root 'packaging/get-project-version.ps1')
$expected = "v$($projectVersion.AppVersion)"
if ($Tag -cne $expected) {
    throw "Tag '$Tag' does not match CMakeLists.txt version. Expected '$expected'. Build and release are blocked."
}
Write-Host "Tag version verified: $Tag"
