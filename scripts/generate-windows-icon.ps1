[CmdletBinding()]
param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\resources\assets\bongocat.png'),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\resources\icons\icon.ico')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

# ICO directory dimensions are one byte: zero means 256, never 512.
# Small frames use 32-bit DIBs with an AND mask; the 256px frame uses PNG.
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$frames = [Collections.Generic.List[byte[]]]::new()
$source = [Drawing.Image]::FromFile([IO.Path]::GetFullPath($SourcePath))
try {
    if ($source.Width -ne $source.Height) { throw 'The source logo must be square.' }
    foreach ($size in $sizes) {
        $bitmap = [Drawing.Bitmap]::new($size, $size,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stream = [IO.MemoryStream]::new()
        $writer = [IO.BinaryWriter]::new($stream)
        try {
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([Drawing.Color]::Transparent)
                $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($source, [Drawing.Rectangle]::new(0, 0, $size, $size))
            }
            finally { $graphics.Dispose() }

            if ($size -eq 256) {
                $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            }
            else {
                $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
                $writer.Write([uint32]40) # BITMAPINFOHEADER
                $writer.Write([int32]$size)
                $writer.Write([int32]($size * 2)) # XOR bitmap plus AND mask
                $writer.Write([uint16]1)
                $writer.Write([uint16]32)
                $writer.Write([uint32]0) # BI_RGB
                $writer.Write([uint32]($size * $size * 4 + $maskStride * $size))
                1..4 | ForEach-Object { $writer.Write([uint32]0) }
                $mask = [byte[]]::new($maskStride * $size)
                for ($row = 0; $row -lt $size; $row++) {
                    for ($x = 0; $x -lt $size; $x++) {
                        $pixel = $bitmap.GetPixel($x, $size - 1 - $row)
                        $writer.Write([byte]$pixel.B)
                        $writer.Write([byte]$pixel.G)
                        $writer.Write([byte]$pixel.R)
                        $writer.Write([byte]$pixel.A)
                        if ($pixel.A -eq 0) {
                            $index = $row * $maskStride + [int][Math]::Floor($x / 8.0)
                            $mask[$index] = $mask[$index] -bor (0x80 -shr ($x % 8))
                        }
                    }
                }
                $writer.Write($mask)
            }
            $frames.Add($stream.ToArray())
        }
        finally { $writer.Dispose(); $stream.Dispose(); $bitmap.Dispose() }
    }
}
finally { $source.Dispose() }

$output = [IO.MemoryStream]::new()
$directory = [IO.BinaryWriter]::new($output)
try {
    $directory.Write([uint16]0)
    $directory.Write([uint16]1)
    $directory.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $dimension = [byte]($sizes[$i] % 256)
        $directory.Write($dimension)
        $directory.Write($dimension)
        $directory.Write([uint16]0) # color count and reserved byte
        $directory.Write([uint16]1)
        $directory.Write([uint16]32)
        $directory.Write([uint32]$frames[$i].Length)
        $directory.Write([uint32]$offset)
        $offset += $frames[$i].Length
    }
    foreach ($frame in $frames) { $directory.Write($frame) }
    [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath), $output.ToArray())
}
finally { $directory.Dispose(); $output.Dispose() }
Write-Host "Generated $OutputPath ($($sizes -join ', ') pixels)."
