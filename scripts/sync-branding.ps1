$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repoRoot = Split-Path -Parent $PSScriptRoot
$masterPath = Join-Path $repoRoot 'resources/icons/icon.png'
$source = [System.Drawing.Image]::FromFile($masterPath)

function Get-LogoPng([int]$size) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $stream = [System.IO.MemoryStream]::new()
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.DrawImage($source, 0, 0, $size, $size)
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    } finally {
        $stream.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Write-BigEndian32($writer, [uint32]$value) {
    $bytes = [BitConverter]::GetBytes($value)
    [Array]::Reverse($bytes)
    $writer.Write($bytes)
}

try {
    if ($source.Width -ne $source.Height -or $source.Width -lt 256) {
        throw 'The master logo must be square and at least 256 x 256.'
    }
    foreach ($name in @('logo.png', 'logo-mac.png')) {
        [System.IO.File]::Copy($masterPath, (Join-Path $repoRoot "resources/assets/$name"), $true)
    }
    $tray = Get-LogoPng 256
    foreach ($name in @('tray.png', 'tray-mac.png')) {
        [System.IO.File]::WriteAllBytes((Join-Path $repoRoot "resources/assets/$name"), $tray)
    }
    [System.IO.File]::WriteAllBytes((Join-Path $repoRoot 'resources/icons/128x128.png'), (Get-LogoPng 128))
    [System.IO.File]::WriteAllBytes((Join-Path $repoRoot 'resources/icons/512x512.png'), (Get-LogoPng 512))

    $sizes = @(16, 24, 32, 48, 64, 128, 256)
    $frames = @($sizes | ForEach-Object { ,(Get-LogoPng $_) })
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
            $writer.Write([byte]$dimension)
            $writer.Write([byte]$dimension)
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$i].Length)
            $writer.Write([uint32]$offset)
            $offset += $frames[$i].Length
        }
        foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
        [System.IO.File]::WriteAllBytes((Join-Path $repoRoot 'resources/icons/icon.ico'), $stream.ToArray())
    } finally { $writer.Dispose(); $stream.Dispose() }

    $types = @('icp4', 'icp5', 'icp6', 'ic07', 'ic08', 'ic09', 'ic11', 'ic12', 'ic13', 'ic14')
    $sizes = @(16, 32, 64, 128, 256, 512, 32, 64, 256, 512)
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('icns'))
        Write-BigEndian32 $writer 0
        for ($i = 0; $i -lt $types.Count; $i++) {
            $frame = Get-LogoPng $sizes[$i]
            $writer.Write([System.Text.Encoding]::ASCII.GetBytes($types[$i]))
            Write-BigEndian32 $writer ($frame.Length + 8)
            $writer.Write([byte[]]$frame)
        }
        $length = $stream.Length
        $stream.Position = 4
        Write-BigEndian32 $writer $length
        [System.IO.File]::WriteAllBytes((Join-Path $repoRoot 'resources/icons/icon.icns'), $stream.ToArray())
    } finally { $writer.Dispose(); $stream.Dispose() }
} finally { $source.Dispose() }
Write-Host 'Updated app, tray, Windows, macOS and Linux logo assets.'
