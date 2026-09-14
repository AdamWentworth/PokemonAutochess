[CmdletBinding()]
param(
    [ValidateSet('Art','Portrait','Source')][string]$View='Portrait',
    [string]$OutputDirectory='debug/tcg-art/gallery'
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskRows=(Get-Content (Join-Path $taskRoot 'config/ui/pokemon_card_art.json') -Raw | ConvertFrom-Json).cards
$taskOutput=[IO.Path]::GetFullPath((Join-Path $taskRoot (Join-Path $OutputDirectory $View.ToLowerInvariant())))
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
Add-Type -AssemblyName System.Drawing
$taskFont=[Drawing.Font]::new('Segoe UI',10)
try {
    for($taskStart=0;$taskStart -lt $taskRows.Count;$taskStart+=25) {
        $taskCellH=if($View -eq 'Source'){315}else{182}
        $taskSheet=[Drawing.Bitmap]::new(1150,($taskCellH*5))
        $taskGraphics=[Drawing.Graphics]::FromImage($taskSheet)
        try {
            $taskGraphics.Clear([Drawing.Color]::FromArgb(23,36,32))
            $taskGraphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            for($taskIndex=0;$taskIndex -lt 25 -and $taskStart+$taskIndex -lt $taskRows.Count;$taskIndex++) {
                $taskRow=$taskRows[$taskStart+$taskIndex]
                $taskImage=[Drawing.Image]::FromFile((Join-Path $taskRoot $taskRow.path))
                try {
                    $taskX=($taskIndex%5)*230+5; $taskY=[Math]::Floor($taskIndex/5)*$taskCellH+4
                    $taskRect=if($View -eq 'Source'){@(0,0,$taskImage.Width,$taskImage.Height)}elseif($View -eq 'Art'){$taskRow.art}else{@($taskRow.portrait[0],$taskRow.portrait[1],$taskRow.portrait[2],$taskRow.portrait[2])}
                    $taskWidth=if($View -eq 'Portrait'){150}else{220};$taskHeight=if($View -eq 'Source'){285}else{150}
                    # Diagnostic contact sheets only. Runtime scans stay byte-for-byte intact.
                    $taskGraphics.DrawImage($taskImage,[Drawing.RectangleF]::new($taskX,$taskY,$taskWidth,$taskHeight),[Drawing.RectangleF]::new($taskRect[0],$taskRect[1],$taskRect[2],$taskRect[3]),[Drawing.GraphicsUnit]::Pixel)
                    $taskGraphics.DrawString("$($taskRow.dex) $($taskRow.species) | $($taskRow.number)",$taskFont,[Drawing.Brushes]::White,[single]$taskX,[single]($taskY+$taskHeight+4))
                } finally {$taskImage.Dispose()}
            }
            $taskSheet.Save((Join-Path $taskOutput ('{0:D3}-{1:D3}.png' -f ($taskStart+1),[Math]::Min($taskStart+25,$taskRows.Count))),[Drawing.Imaging.ImageFormat]::Png)
        } finally {$taskGraphics.Dispose();$taskSheet.Dispose()}
    }
} finally {$taskFont.Dispose()}
Write-Output "Artwork review sheets: $taskOutput"
