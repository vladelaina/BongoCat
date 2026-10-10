param(
    [Parameter(Mandatory = $true)][string]$SignedDirectory,
    [Parameter(Mandatory = $true)][string]$UnsignedDirectory,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-fA-F0-9]{40}$')][string]$ExpectedThumbprint
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$unsigned = @(Get-ChildItem -LiteralPath $UnsignedDirectory -Filter *.zip -File)
$signed = @(Get-ChildItem -LiteralPath $SignedDirectory -Filter *.zip -File)
if ($unsigned.Count -ne 1 -or $signed.Count -ne 1) {
    throw 'Expected exactly one unsigned and one signed release ZIP.'
}
$name = $unsigned[0].BaseName
if ($name -notmatch '^BongoCat-\d+\.\d+\.\d+-windows-x64$' -or
    $signed[0].Name -ne $unsigned[0].Name) {
    throw 'The signed package name does not match the unsigned Windows package.'
}

# Compare archive entries so signing cannot silently discard or replace payloads.
$before = [IO.Compression.ZipFile]::OpenRead($unsigned[0].FullName)
$after = [IO.Compression.ZipFile]::OpenRead($signed[0].FullName)
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('bongo-signpath-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $exePath = "$name/BongoCat.exe"
    $originalEntries = @($before.Entries | Where-Object { $_.Name })
    $signedEntries = @($after.Entries | Where-Object { $_.Name })
    if ($originalEntries.Count -ne $signedEntries.Count) {
        throw 'Signed ZIP entry count changed.'
    }
    foreach ($entry in $originalEntries) {
        $entryPath = $entry.FullName.Replace([char]92, [char]47)
        $matches = @($signedEntries | Where-Object {
            $_.FullName.Replace([char]92, [char]47) -ceq $entryPath
        })
        if ($matches.Count -ne 1) { throw "Missing or duplicate ZIP entry: $($entry.FullName)" }
        if ($entryPath -ceq $exePath) { continue }
        $hash = [Security.Cryptography.SHA256]::Create()
        $sourceStream = $entry.Open()
        $signedStream = $matches[0].Open()
        try {
            $sourceHash = [Convert]::ToBase64String($hash.ComputeHash($sourceStream))
            $signedHash = [Convert]::ToBase64String($hash.ComputeHash($signedStream))
            if ($sourceHash -ne $signedHash) { throw "Unsigned payload changed: $($entry.FullName)" }
        } finally {
            $sourceStream.Dispose()
            $signedStream.Dispose()
            $hash.Dispose()
        }
    }
    $executables = @($signedEntries | Where-Object {
        $_.FullName.Replace([char]92, [char]47) -ceq $exePath
    })
    if ($executables.Count -ne 1) { throw 'Expected exactly one packaged BongoCat.exe.' }
    $executable = Join-Path $temporary 'BongoCat.exe'
    [IO.Compression.ZipFileExtensions]::ExtractToFile($executables[0], $executable)
    $signature = Get-AuthenticodeSignature -LiteralPath $executable
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $ExpectedThumbprint) {
        throw 'The signing certificate does not match the configured test certificate.'
    }
    # Self-signed test certificates may be untrusted on a fresh hosted runner.
    if ($signature.Status -notin @('Valid', 'NotTrusted')) {
        throw "Invalid Authenticode signature: $($signature.Status): $($signature.StatusMessage)"
    }
    Write-Output "Test certificate: $($signature.SignerCertificate.Subject); status: $($signature.Status)"
} finally {
    $before.Dispose()
    $after.Dispose()
    # This directory was created with a unique name under the OS temporary directory.
    Remove-Item -LiteralPath $temporary -Recurse -Force
}

$digest = (Get-FileHash -LiteralPath $signed[0].FullName -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$($signed[0].FullName).sha256", "$digest`n", [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $UnsignedDirectory 'DIAGNOSTIC-NO-CUBISM.txt') -Destination $SignedDirectory
'Signed with a self-signed TEST certificate. For integration testing only. Not for end-user distribution.' |
    Set-Content -LiteralPath (Join-Path $SignedDirectory 'TEST-CERTIFICATE.txt') -Encoding ascii
