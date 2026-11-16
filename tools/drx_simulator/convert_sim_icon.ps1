Add-Type -AssemblyName System.Drawing

$srcPath = Join-Path $PSScriptRoot "..\..\resources\images\neta_sim_app.png"
$icoPath = Join-Path $PSScriptRoot "..\..\resources\images\neta_sim_app.ico"
$simIcoPath = Join-Path $PSScriptRoot "neta_sim_app.ico"

if (-not (Test-Path $srcPath)) {
    Write-Error "Source PNG not found at $srcPath"
    exit 1
}

$src = [System.Drawing.Bitmap]::FromFile($srcPath)
Write-Host "Source PNG Loaded: $($src.Width)x$($src.Height), PixelFormat: $($src.PixelFormat)"

$sizes = @(256, 128, 64, 48, 32, 16)
$pngStreams = @()

foreach ($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $sz, $sz
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($src, 0, 0, $sz, $sz)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngStreams += $ms
}

function Write-IcoFile($targetPath, $sizes, $pngStreams) {
    $fs = [System.IO.File]::Create($targetPath)
    $bw = New-Object System.IO.BinaryWriter $fs

    # Header: 6 bytes
    $bw.Write([UInt16]0)            # Reserved
    $bw.Write([UInt16]1)            # Type 1 = Icon
    $bw.Write([UInt16]$sizes.Count) # Image count

    $offset = 6 + (16 * $sizes.Count)

    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $sz = $sizes[$i]
        $dataLen = [UInt32]$pngStreams[$i].Length

        $w = if ($sz -eq 256) { 0 } else { $sz }
        $h = if ($sz -eq 256) { 0 } else { $sz }

        $bw.Write([Byte]$w)            # Width
        $bw.Write([Byte]$h)            # Height
        $bw.Write([Byte]0)             # ColorCount
        $bw.Write([Byte]0)             # Reserved
        $bw.Write([UInt16]1)           # Planes
        $bw.Write([UInt16]32)          # BitCount
        $bw.Write([UInt32]$dataLen)    # Size
        $bw.Write([UInt32]$offset)     # Offset

        $offset += $dataLen
    }

    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $bytes = $pngStreams[$i].ToArray()
        $bw.Write($bytes)
    }

    $bw.Close()
    $fs.Close()
}

Write-IcoFile $icoPath $sizes $pngStreams
Write-IcoFile $simIcoPath $sizes $pngStreams

foreach ($ms in $pngStreams) {
    $ms.Dispose()
}
$src.Dispose()

Write-Host "Generated ICO successfully:"
Get-Item $icoPath, $simIcoPath | Select-Object FullName, Length
