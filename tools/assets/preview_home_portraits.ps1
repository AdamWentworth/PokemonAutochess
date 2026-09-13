[CmdletBinding()]
param([switch]$FullImages, [string]$OutputDirectory = 'debug/home-portraits/gallery')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
$manifest = Get-Content (Join-Path $root 'config/ui/pokemon_home_portraits.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $output -Force | Out-Null
Add-Type -AssemblyName System.Drawing
$font = [Drawing.Font]::new('Segoe UI', 11)
try {
    for ($start = 0; $start -lt $manifest.portraits.Count; $start += 30) {
        # A review contact sheet only; runtime source PNGs remain unmodified.
        $sheet = [Drawing.Bitmap]::new(1080, 1000)
        $g = [Drawing.Graphics]::FromImage($sheet)
        try {
            $g.Clear([Drawing.Color]::FromArgb(23, 36, 32))
            $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            for ($j = 0; $j -lt 30 -and $start + $j -lt $manifest.portraits.Count; $j++) {
                $entry = $manifest.portraits[$start + $j]
                $image = [Drawing.Bitmap]::FromFile((Join-Path $root $entry.path))
                try {
                    $x = ($j % 6) * 180 + 10
                    $y = [Math]::Floor($j / 6) * 200 + 8
                    $src = if ($FullImages) { [Drawing.RectangleF]::new(0, 0, 512, 512) } else { [Drawing.RectangleF]::new($entry.crop[0], $entry.crop[1], $entry.crop[2], $entry.crop[2]) }
                    $g.DrawImage($image, [Drawing.RectangleF]::new($x, $y, 160, 160), $src, [Drawing.GraphicsUnit]::Pixel)
                    $g.DrawString("$($entry.dex) $($entry.species)", $font, [Drawing.Brushes]::White, [single]$x, [single]($y + 169))
                } finally { $image.Dispose() }
            }
            $sheet.Save((Join-Path $output ('{0:D3}-{1:D3}.png' -f ($start+1), [Math]::Min($start+30,151))), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $g.Dispose(); $sheet.Dispose() }
    }
} finally { $font.Dispose() }
Write-Host "Portrait review sheets: $output"
