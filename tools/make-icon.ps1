$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$outFile = Join-Path $PSScriptRoot '..\res\app.ico'
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngData = @()

foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $pad = [Math]::Max(1.0, $size * 0.035)
    $r = $size * 0.235
    $rect = New-Object System.Drawing.RectangleF($pad, $pad, ($size - 2 * $pad), ($size - 2 * $pad))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        $rect,
        [System.Drawing.Color]::FromArgb(255, 102, 210, 255),
        [System.Drawing.Color]::FromArgb(255, 42, 158, 232),
        90.0)
    $g.FillPath($brush, $path)

    $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $cx = $size / 2.0
    $stemW = $size * 0.135
    $stemTop = $size * 0.245
    $stemH = $size * 0.245
    $g.FillRectangle($white, [single]($cx - $stemW / 2), [single]$stemTop, [single]$stemW, [single]$stemH)

    $points = @(
        (New-Object System.Drawing.PointF([single]($size * 0.285), [single]($size * 0.485))),
        (New-Object System.Drawing.PointF([single]($size * 0.715), [single]($size * 0.485))),
        (New-Object System.Drawing.PointF([single]($size * 0.5), [single]($size * 0.715)))
    )
    $g.FillPolygon($white, $points)
    $g.FillRectangle($white, [single]($size * 0.285), [single]($size * 0.775),
                     [single]($size * 0.43), [single]([Math]::Max(1.0, $size * 0.045)))

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngData += , $ms.ToArray()
    $ms.Dispose()
    $bmp.Dispose()
    $brush.Dispose()
    $white.Dispose()
    $path.Dispose()
}

$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($stream)
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $data = $pngData[$i]
    $dim = if ($s -ge 256) { 0 } else { $s }
    $writer.Write([Byte]$dim)
    $writer.Write([Byte]$dim)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$data.Length)
    $writer.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($data in $pngData) { $writer.Write($data) }
$writer.Flush()
[System.IO.File]::WriteAllBytes($outFile, $stream.ToArray())
$writer.Dispose()
$stream.Dispose()
Write-Output "Wrote $outFile"
