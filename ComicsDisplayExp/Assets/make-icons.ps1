<#
.SYNOPSIS
    Draws the ComicsDisplayExp icons and writes them next to this script.

.DESCRIPTION
    AppIcon.ico   - the app (exe, window, taskbar, Start menu, installer)
    ComicFile.ico - comic book files (.cbz/.cbr/.cb7/.cbt) when the app is their default
    AppIcon.png   - 256 px version of the app icon, used inside the app's UI
    ..\..\installer\WizardSmall-*.bmp - the app icon for the installer wizard, per display scale

    Every size is drawn directly (not downscaled from 256 px); the smallest sizes use a
    simplified drawing so they stay crisp. Re-run after changing the design:
        powershell -ExecutionPolicy Bypass -File ComicsDisplayExp\Assets\make-icons.ps1
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256

# Palette: violet -> pink accent, white page, yellow speech bubble, dark ink.
$violet = [Drawing.Color]::FromArgb(255, 124, 77, 255)
$pink = [Drawing.Color]::FromArgb(255, 236, 72, 153)
$panel = [Drawing.Color]::FromArgb(255, 221, 214, 254)
$bubble = [Drawing.Color]::FromArgb(255, 250, 204, 21)
$ink = [Drawing.Color]::FromArgb(255, 30, 27, 46)

function New-RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $d = [Math]::Min($r * 2, [Math]::Min($w, $h))
    if ($d -le 0) { $path.AddRectangle((New-Object Drawing.RectangleF $x, $y, $w, $h)); return $path }
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

# Speech bubble: ellipse with a tail towards the lower left, in a 256-unit design space.
function Add-SpeechBubble($g, [float]$cx, [float]$cy, [float]$rx, [float]$ry, [float]$outline, [bool]$dots) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddEllipse($cx - $rx, $cy - $ry, 2 * $rx, 2 * $ry)
    $tail = New-Object Drawing.Drawing2D.GraphicsPath
    $tail.AddPolygon([Drawing.PointF[]]@(
        (New-Object Drawing.PointF ($cx - $rx * 0.55), ($cy + $ry * 0.55)),
        (New-Object Drawing.PointF ($cx - $rx * 0.05), ($cy + $ry * 0.85)),
        (New-Object Drawing.PointF ($cx - $rx * 1.05), ($cy + $ry * 1.55))))
    $region = New-Object Drawing.Region $path
    $region.Union($tail)
    if ($outline -gt 0) {
        # Outline by drawing the shape slightly larger in ink first.
        $pen = New-Object Drawing.Pen $ink, ($outline * 2)
        $pen.LineJoin = 'Round'
        $g.DrawPath($pen, $path); $g.DrawPath($pen, $tail)
        $pen.Dispose()
    }
    $g.FillRegion((New-Object Drawing.SolidBrush $bubble), $region)
    if ($dots) {
        $dot = $rx * 0.16
        foreach ($k in -1, 0, 1) { $g.FillEllipse((New-Object Drawing.SolidBrush $ink), $cx + $k * $rx * 0.42 - $dot, $cy - $dot, 2 * $dot, 2 * $dot) }
    }
}

function New-AppIconBitmap([int]$size) {
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.CompositingQuality = 'HighQuality'
    $g.ScaleTransform($size / 256.0, $size / 256.0)
    $small = $size -le 24

    # Tile
    $tileInset = if ($small) { 4 } else { 12 }
    $tile = New-RoundedRect $tileInset $tileInset (256 - 2 * $tileInset) (256 - 2 * $tileInset) 56
    $grad = New-Object Drawing.Drawing2D.LinearGradientBrush (New-Object Drawing.PointF 0, 0), (New-Object Drawing.PointF 256, 256), $violet, $pink
    $g.FillPath($grad, $tile)

    # Page with panels
    $page = if ($small) { New-RoundedRect 44 40 120 176 12 } else { New-RoundedRect 52 50 112 156 10 }
    if (-not $small) { $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(60, 0, 0, 0))), (New-RoundedRect 58 58 112 156 10)) }
    $g.FillPath([Drawing.Brushes]::White, $page)
    $pb = New-Object Drawing.SolidBrush $panel
    if ($small) {
        $g.FillPath($pb, (New-RoundedRect 60 58 88 50 6))
        $g.FillPath($pb, (New-RoundedRect 60 124 88 74 6))
    } else {
        $g.FillPath($pb, (New-RoundedRect 64 64 88 46 5))
        $g.FillPath($pb, (New-RoundedRect 64 120 40 72 5))
        $g.FillPath($pb, (New-RoundedRect 112 120 40 72 5))
    }

    # Speech bubble over the top right of the page
    if ($small) { Add-SpeechBubble $g 178 92 58 46 0 $false }
    else { Add-SpeechBubble $g 172 90 52 40 5 ($size -ge 32) }

    $g.Dispose()
    return $bmp
}

function New-FileIconBitmap([int]$size) {
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.CompositingQuality = 'HighQuality'
    $g.ScaleTransform($size / 256.0, $size / 256.0)
    $small = $size -le 24

    # Document with a folded corner (like Windows' own file icons)
    $left = if ($small) { 36 } else { 44 }; $right = 256 - $left; $top = 12; $bottom = 244; $fold = 56
    $doc = New-Object Drawing.Drawing2D.GraphicsPath
    $doc.AddPolygon([Drawing.PointF[]]@(
        (New-Object Drawing.PointF $left, $top), (New-Object Drawing.PointF ($right - $fold), $top),
        (New-Object Drawing.PointF $right, ($top + $fold)), (New-Object Drawing.PointF $right, $bottom),
        (New-Object Drawing.PointF $left, $bottom)))
    $g.FillPath([Drawing.Brushes]::White, $doc)
    $edge = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(255, 140, 140, 160)), $(if ($small) { 14 } else { 6 })
    $edge.LineJoin = 'Round'
    $g.DrawPath($edge, $doc)
    $corner = New-Object Drawing.Drawing2D.GraphicsPath
    $corner.AddPolygon([Drawing.PointF[]]@(
        (New-Object Drawing.PointF ($right - $fold), $top), (New-Object Drawing.PointF ($right - $fold), ($top + $fold)),
        (New-Object Drawing.PointF $right, ($top + $fold))))
    $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 226, 226, 236))), $corner)
    $g.DrawPath($edge, $corner)

    # Panels in the app's gradient
    $grad = New-Object Drawing.Drawing2D.LinearGradientBrush (New-Object Drawing.PointF 0, 60), (New-Object Drawing.PointF 256, 220), $violet, $pink
    if ($small) {
        $g.FillPath($grad, (New-RoundedRect ($left + 24) 84 (196 - 2 * $left) 52 6))
        $g.FillPath($grad, (New-RoundedRect ($left + 24) 152 (196 - 2 * $left) 64 6))
    } else {
        $inner = $left + 22; $w = $right - 22 - $inner
        $g.FillPath($grad, (New-RoundedRect $inner 84 $w 52 6))
        $g.FillPath($grad, (New-RoundedRect $inner 146 ($w / 2 - 5) 76 6))
        $g.FillPath($grad, (New-RoundedRect ($inner + $w / 2 + 5) 146 ($w / 2 - 5) 76 6))
    }

    # Speech bubble
    if ($small) { Add-SpeechBubble $g 92 64 50 34 0 $false }
    else { Add-SpeechBubble $g 100 62 48 32 5 ($size -ge 40) }

    $g.Dispose()
    return $bmp
}

# Classic icon image: 32-bit BGRA DIB (bottom-up, alpha channel) plus an empty AND mask.
function ConvertTo-IconDib([Drawing.Bitmap]$bmp) {
    $s = $bmp.Width
    $rect = New-Object Drawing.Rectangle 0, 0, $s, $s
    $bits = $bmp.LockBits($rect, 'ReadOnly', [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object byte[] ($s * $s * 4)
    [Runtime.InteropServices.Marshal]::Copy($bits.Scan0, $pixels, 0, $pixels.Length) # top-down BGRA
    $bmp.UnlockBits($bits)

    $maskStride = [int]([Math]::Ceiling($s / 32.0) * 4)
    $ms = New-Object IO.MemoryStream
    $w = New-Object IO.BinaryWriter $ms
    # BITMAPINFOHEADER; height counts colour rows + mask rows.
    $w.Write([UInt32]40); $w.Write([Int32]$s); $w.Write([Int32]($s * 2))
    $w.Write([UInt16]1); $w.Write([UInt16]32); $w.Write([UInt32]0)
    $w.Write([UInt32]($s * $s * 4 + $maskStride * $s)); $w.Write([Int32]0); $w.Write([Int32]0)
    $w.Write([UInt32]0); $w.Write([UInt32]0)
    for ($y = $s - 1; $y -ge 0; $y--) { $w.Write($pixels, $y * $s * 4, $s * 4) } # bottom-up
    $w.Write((New-Object byte[] ($maskStride * $s)))                           # AND mask: alpha decides
    $w.Flush()
    return $ms.ToArray()
}

# ICO: PNG-compressed 256 px image (like Windows' own icons), classic DIBs for the rest.
function Write-Ico([string]$path, [scriptblock]$draw) {
    $images = foreach ($s in $sizes) {
        $bmp = & $draw $s
        if ($s -ge 256) {
            $ms = New-Object IO.MemoryStream
            $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png)
            $data = $ms.ToArray()
        } else {
            $data = ConvertTo-IconDib $bmp
        }
        $bmp.Dispose()
        , @($s, $data)
    }
    $out = New-Object IO.MemoryStream
    $w = New-Object IO.BinaryWriter $out
    $w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($img in $images) {
        $s = $img[0]; $data = $img[1]
        $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256))   # 256 is stored as 0
        $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([UInt16]1); $w.Write([UInt16]32)
        $w.Write([UInt32]$data.Length); $w.Write([UInt32]$offset)
        $offset += $data.Length
    }
    foreach ($img in $images) { $w.Write([byte[]]$img[1]) }
    $w.Flush()
    [IO.File]::WriteAllBytes($path, $out.ToArray())
}

Write-Ico (Join-Path $PSScriptRoot 'AppIcon.ico') { param($s) New-AppIconBitmap $s }
Write-Ico (Join-Path $PSScriptRoot 'ComicFile.ico') { param($s) New-FileIconBitmap $s }
$png = New-AppIconBitmap 256
$png.Save((Join-Path $PSScriptRoot 'AppIcon.png'), [Drawing.Imaging.ImageFormat]::Png)
$png.Dispose()

# Installer wizard image (top right), one bitmap per display scale 100%..250% on white.
$installer = Join-Path $PSScriptRoot '..\..\installer'
foreach ($size in 55, 64, 69, 83, 92, 110, 119, 138) {
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.Clear([Drawing.Color]::White)
    $g.InterpolationMode = 'HighQualityBicubic'
    $icon = New-AppIconBitmap $size
    $g.DrawImage($icon, 0, 0, $size, $size)
    $icon.Dispose(); $g.Dispose()
    $bmp.Save((Join-Path $installer "WizardSmall-$size.bmp"), [Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
}
Write-Host "Icons written to $PSScriptRoot and installer images to $installer"
