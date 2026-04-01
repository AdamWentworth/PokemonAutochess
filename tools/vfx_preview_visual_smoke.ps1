param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$OutputDir = "debug/vfx_preview_smoke",
    [int]$LabScreenshotFrame = 2,
    [int]$LabAutoQuitSeconds = 20,
    [int]$PreviewerScreenshotFrame = 10,
    [int]$PreviewerAutoQuitSeconds = 15,
    [switch]$SkipVfxLab
)

$ErrorActionPreference = "Stop"

function Resolve-PreviewExePath {
    param(
        [string]$BuildDir,
        [string]$Config,
        [string]$ExeName
    )

    $candidateA = Join-Path $BuildDir "$Config/$ExeName"
    if (Test-Path $candidateA) { return (Resolve-Path $candidateA).Path }

    $candidateB = Join-Path $BuildDir $ExeName
    if (Test-Path $candidateB) { return (Resolve-Path $candidateB).Path }

    throw "$ExeName not found under '$BuildDir' (config '$Config')."
}

function Set-PreviewEnvVar {
    param(
        [string]$Name,
        [string]$Value,
        [hashtable]$Backup
    )

    if (-not $Backup.ContainsKey($Name)) {
        $Backup[$Name] = [Environment]::GetEnvironmentVariable($Name, "Process")
    }

    if ($null -eq $Value) {
        Remove-Item "Env:$Name" -ErrorAction SilentlyContinue
    } else {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

function Restore-PreviewEnv {
    param([hashtable]$Backup)

    foreach ($entry in $Backup.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
}

function Invoke-PreviewCapture {
    param(
        [string]$ExePath,
        [string]$CaseName,
        [string]$OutputDir,
        [string]$InitialEffect,
        [string]$InitialRig,
        [int]$ScreenshotFrame,
        [int]$AutoQuitSeconds
    )

    $backup = @{}
    $shotPath = Join-Path $OutputDir "$CaseName.png"
    $stdoutPath = Join-Path $OutputDir "$CaseName.stdout.log"
    $stderrPath = Join-Path $OutputDir "$CaseName.stderr.log"

    try {
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_INITIAL_EFFECT" -Value $InitialEffect -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_INITIAL_RIG" -Value $InitialRig -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_SCREENSHOT_PATH" -Value $shotPath -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_SCREENSHOT_FRAME" -Value "$ScreenshotFrame" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_EXIT_AFTER_SCREENSHOT" -Value "1" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_AUTO_QUIT_SECONDS" -Value "$AutoQuitSeconds" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_HIDE_HELP" -Value "1" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_HIDE_GUIDES" -Value "1" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_PREVIEW_CHARACTER_INKING" -Value "0" -Backup $backup

        if (Test-Path $shotPath) { Remove-Item $shotPath -Force }
        if (Test-Path $stdoutPath) { Remove-Item $stdoutPath -Force }
        if (Test-Path $stderrPath) { Remove-Item $stderrPath -Force }

        $process = Start-Process -FilePath $ExePath `
            -WorkingDirectory (Resolve-Path ".").Path `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -PassThru

        if (-not $process.WaitForExit(($AutoQuitSeconds + 15) * 1000)) {
            Stop-Process -Id $process.Id -Force
            throw "$CaseName timed out without exiting after screenshot capture."
        }

        $process.Refresh()
        if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
            throw "$CaseName exited with code $($process.ExitCode)."
        }

        if (-not (Test-Path $shotPath)) {
            $stdoutTail = @()
            $stderrTail = @()
            if (Test-Path $stdoutPath) {
                $stdoutTail = @(Get-Content $stdoutPath -ErrorAction SilentlyContinue | Select-Object -Last 20)
            }
            if (Test-Path $stderrPath) {
                $stderrTail = @(Get-Content $stderrPath -ErrorAction SilentlyContinue | Select-Object -Last 20)
            }
            $detail = @()
            if ($stdoutTail.Count -gt 0) {
                $detail += "stdout_tail:`n" + ($stdoutTail -join "`n")
            }
            if ($stderrTail.Count -gt 0) {
                $detail += "stderr_tail:`n" + ($stderrTail -join "`n")
            }
            $detailText = if ($detail.Count -gt 0) { "`n" + ($detail -join "`n") } else { "" }
            throw "$CaseName did not produce screenshot '$shotPath'.$detailText"
        }

        $stdout = @(Get-Content $stdoutPath -ErrorAction SilentlyContinue)
        $shotLine = $stdout | Where-Object { $_ -match "^\[PreviewScreenshot\] WROTE " } | Select-Object -Last 1
        if (-not $shotLine) {
            throw "$CaseName did not report a successful screenshot write."
        }

        Write-Host "[PreviewSmoke][$CaseName] $shotLine"
        return @{
            ScreenshotPath = $shotPath
            StdoutPath = $stdoutPath
            StderrPath = $stderrPath
        }
    } finally {
        Restore-PreviewEnv -Backup $backup
    }
}

function Get-PreviewImageMetrics {
    param(
        [string]$Path,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height
    )

    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        if ($Width -le 0 -or $Height -le 0) {
            throw "Invalid region size for '$Path'."
        }
        if ($X -lt 0 -or $Y -lt 0 -or ($X + $Width) -gt $bmp.Width -or ($Y + $Height) -gt $bmp.Height) {
            throw "Region $X,$Y ${Width}x$Height is outside '$Path' (${($bmp.Width)}x${($bmp.Height)})."
        }

        $pixelCount = [double]($Width * $Height)
        $nonBg = 0
        $warm = 0
        $magenta = 0
        $bright = 0

        for ($py = $Y; $py -lt ($Y + $Height); ++$py) {
            for ($px = $X; $px -lt ($X + $Width); ++$px) {
                $pixel = $bmp.GetPixel($px, $py)
                $bgDiff =
                    [math]::Abs($pixel.R - 13) +
                    [math]::Abs($pixel.G - 15) +
                    [math]::Abs($pixel.B - 23)
                if ($bgDiff -gt 30) { ++$nonBg }
                if ($pixel.R -gt 75 -and $pixel.R -gt ($pixel.G + 10) -and $pixel.G -gt $pixel.B) {
                    ++$warm
                }
                if ($pixel.R -gt 85 -and $pixel.B -gt 70 -and $pixel.R -gt ($pixel.G + 12)) {
                    ++$magenta
                }
                if (($pixel.R + $pixel.G + $pixel.B) -gt 420) {
                    ++$bright
                }
            }
        }

        return [pscustomobject]@{
            NonBackgroundRatio = $nonBg / $pixelCount
            WarmRatio = $warm / $pixelCount
            MagentaRatio = $magenta / $pixelCount
            BrightRatio = $bright / $pixelCount
        }
    } finally {
        $bmp.Dispose()
    }
}

function Assert-PreviewImageMetrics {
    param(
        [string]$CaseName,
        [string]$Path,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height,
        [double]$MinNonBackgroundRatio,
        [double]$MinWarmPlusMagentaRatio,
        [double]$MinBrightRatio
    )

    $metrics = Get-PreviewImageMetrics -Path $Path -X $X -Y $Y -Width $Width -Height $Height
    $colorRatio = $metrics.WarmRatio + $metrics.MagentaRatio

    Write-Host (
        "[PreviewSmoke][$CaseName] region=$X,$Y ${Width}x$Height " +
        ("non_bg={0:N4} warm={1:N4} magenta={2:N4} bright={3:N4}" -f `
            $metrics.NonBackgroundRatio, $metrics.WarmRatio, $metrics.MagentaRatio, $metrics.BrightRatio))

    if ($metrics.NonBackgroundRatio -lt $MinNonBackgroundRatio) {
        throw "$CaseName non-background ratio too low: $($metrics.NonBackgroundRatio) < $MinNonBackgroundRatio"
    }
    if ($colorRatio -lt $MinWarmPlusMagentaRatio) {
        throw "$CaseName warm+magenta ratio too low: $colorRatio < $MinWarmPlusMagentaRatio"
    }
    if ($metrics.BrightRatio -lt $MinBrightRatio) {
        throw "$CaseName bright ratio too low: $($metrics.BrightRatio) < $MinBrightRatio"
    }
}

$repoRoot = (Resolve-Path ".").Path
$outputDirAbs = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Path $outputDirAbs -Force | Out-Null

$previewerExe = Resolve-PreviewExePath -BuildDir $BuildDir -Config $Config -ExeName "PAC_VfxPreviewer.exe"
$vfxLabExe = Resolve-PreviewExePath -BuildDir $BuildDir -Config $Config -ExeName "VfxLab.exe"

Write-Host "[PreviewSmoke] PAC_VfxPreviewer: $previewerExe"
Write-Host "[PreviewSmoke] VfxLab: $vfxLabExe"
Write-Host "[PreviewSmoke] Output dir: $outputDirAbs"

if (-not $SkipVfxLab) {
    $labCapture = Invoke-PreviewCapture `
        -ExePath $vfxLabExe `
        -CaseName "growl_lab" `
        -OutputDir $outputDirAbs `
        -InitialEffect "Growl" `
        -InitialRig $null `
        -ScreenshotFrame $LabScreenshotFrame `
        -AutoQuitSeconds $LabAutoQuitSeconds

    Assert-PreviewImageMetrics `
        -CaseName "growl_lab" `
        -Path $labCapture.ScreenshotPath `
        -X 660 -Y 190 -Width 140 -Height 110 `
        -MinNonBackgroundRatio 0.02 `
        -MinWarmPlusMagentaRatio 0.01 `
        -MinBrightRatio 0.002
}

$previewCapture = Invoke-PreviewCapture `
    -ExePath $previewerExe `
    -CaseName "growl_models" `
    -OutputDir $outputDirAbs `
    -InitialEffect "Growl" `
    -InitialRig "3D Models" `
    -ScreenshotFrame $PreviewerScreenshotFrame `
    -AutoQuitSeconds $PreviewerAutoQuitSeconds

Assert-PreviewImageMetrics `
    -CaseName "growl_models" `
    -Path $previewCapture.ScreenshotPath `
    -X 660 -Y 120 -Width 100 -Height 130 `
    -MinNonBackgroundRatio 0.03 `
    -MinWarmPlusMagentaRatio 0.02 `
    -MinBrightRatio 0.02

Write-Host "[PreviewSmoke] PASS"
