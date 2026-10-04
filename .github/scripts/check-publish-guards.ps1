[CmdletBinding()]
param([switch]$SelfTest)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$upstreamGuards = @(
    "github.repository == 'vladelaina/BongoCat'"
)
$workflowDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'workflows'
$sensitiveMarkers = @(
    'build-store-package.ps1',
    'CUBISM_SDK_ARCHIVE_URL',
    'VIRUSTOTAL_API_KEY',
    'actions/upload-artifact',
    'softprops/action-gh-release'
)
$runtimeArtifactMarkers = @(
    'build-store-package.ps1',
    'Upload desktop release',
    'Upload Microsoft Store MSIX',
    'Upload Windows desktop release',
    'Upload Windows portable release',
    'name: bongocat-release-',
    'softprops/action-gh-release'
)

function Get-PublishingGuardFailures {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $jobsStart = $Text.IndexOf("jobs:", [StringComparison]::Ordinal)
    if ($jobsStart -lt 0) { return @() }
    $jobsText = $Text.Substring($jobsStart)
    $jobPattern = '(?ms)^  ([A-Za-z0-9_-]+):\s*\r?\n' +
        '(.*?)(?=^  [A-Za-z0-9_-]+:\s*\r?\n|\z)'
    $failures = @()
    $jobMatches = [regex]::Matches($jobsText, $jobPattern)
    $buildJob = @($jobMatches | Where-Object {
        $_.Groups[1].Value -eq 'build'
    } | Select-Object -First 1)
    $cubismRequirementMarkers = @(
        '-DBONGO_CAT_REQUIRE_CUBISM=ON',
        '-RequireCubism'
    )
    $protectedBuild = $buildJob.Count -eq 1 -and @(
        $cubismRequirementMarkers | Where-Object {
            $buildJob[0].Value.Contains($_)
        }
    ).Count -gt 0

    foreach ($job in $jobMatches) {
        $jobName = $job.Groups[1].Value
        $jobText = $job.Value
        $matched = @($sensitiveMarkers | Where-Object { $jobText.Contains($_) })
        if ($matched.Count -eq 0) { continue }
        $guarded = @($upstreamGuards | Where-Object {
            $jobText.Contains($_)
        }).Count -gt 0
        if (-not $guarded) {
            $failures += "$Label`: job '$jobName' lacks upstream guard; " +
                "matched $($matched -join ', ')"
        }
        $publishesRuntimeArtifacts = @($runtimeArtifactMarkers | Where-Object {
            $jobText.Contains($_)
        }).Count -gt 0
        $usesProtectedBuild = $protectedBuild -and $jobText -match
            '(?m)^\s+needs:\s*(?:build|\[[^\]]*\bbuild\b[^\]]*\])\s*$'
        $hasCubismRequirement = @(
            $cubismRequirementMarkers | Where-Object {
                $jobText.Contains($_)
            }
        ).Count -gt 0
        if ($publishesRuntimeArtifacts -and
            -not $hasCubismRequirement -and
            -not $usesProtectedBuild) {
            $failures += "$Label`: job '$jobName' publishes artifacts without " +
                'requiring a Cubism SDK build'
        }
    }
    return $failures
}

$workflowPaths = @(
    Get-ChildItem -LiteralPath $workflowDirectory -File |
        Where-Object { $_.Extension -in @('.yml', '.yaml') } |
        Sort-Object FullName
)
$failures = @()
$checkedJobs = 0
$selfTestText = $null

foreach ($path in $workflowPaths) {
    $text = Get-Content -LiteralPath $path.FullName -Raw
    $pathFailures = @(Get-PublishingGuardFailures -Text $text -Label $path.Name)
    $failures += $pathFailures
    $jobsStart = $text.IndexOf("jobs:", [StringComparison]::Ordinal)
    if ($jobsStart -ge 0) {
        $jobsText = $text.Substring($jobsStart)
        $checkedJobs += @([regex]::Matches($jobsText,
            '(?ms)^  ([A-Za-z0-9_-]+):\s*\r?\n(.*?)(?=^  [A-Za-z0-9_-]+:\s*\r?\n|\z)') |
            Where-Object {
                $jobText = $_.Value
                @($sensitiveMarkers | Where-Object {
                    $jobText.Contains($_)
                }).Count -gt 0
            }).Count
    }
    if (-not $selfTestText -and
        ($sensitiveMarkers | Where-Object { $text.Contains($_) })) {
        $selfTestText = $text
    }
}

if ($SelfTest) {
    if (-not $selfTestText) {
        $failures += 'Self-test could not find a sensitive workflow.'
    }
    else {
        $mutated = $selfTestText
        foreach ($guard in $upstreamGuards) {
            $mutated = $mutated.Replace($guard, 'true')
        }
        $caught = @(Get-PublishingGuardFailures -Text $mutated -Label 'self-test')
        if ($caught.Count -eq 0) {
            $failures += 'Self-test did not reject a removed upstream guard.'
        }
        $withoutCubism = $selfTestText.Replace(
            '-DBONGO_CAT_REQUIRE_CUBISM=ON',
            '-DBONGO_CAT_REQUIRE_CUBISM=OFF').Replace(
            '-RequireCubism', '')
        $cubismFailures = @(Get-PublishingGuardFailures `
            -Text $withoutCubism -Label 'self-test')
        if (-not ($cubismFailures -match 'requiring a Cubism SDK build')) {
            $failures += 'Self-test did not reject an SDK-free artifact job.'
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Error ("Publishing guard policy failed:`n- " +
        ($failures -join "`n- "))
    exit 1
}

Write-Host "Publishing guard policy passed for $checkedJobs sensitive job(s)."
if ($SelfTest) { Write-Host 'Publishing guard negative self-tests passed.' }
