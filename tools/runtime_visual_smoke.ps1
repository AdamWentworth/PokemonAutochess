param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$OutputDir = "debug/runtime_visual_smoke",
    [string]$SnapshotPath = "config/debug/debug_state_snapshot_tail_fire_starter_line.json",
    [string[]]$Backends = @("opengl", "vulkan", "d3d12"),
    [string[]]$SupportedResolutions = @("960x540", "1280x720"),
    [int]$ScreenshotFrame = 120,
    [double]$FixedFrameDtSeconds = (1.0 / 60.0),
    [int]$AutoQuitSeconds = 8,
    [int]$WaitTimeoutSeconds = 75
)

$ErrorActionPreference = "Stop"

function Resolve-GameExePath {
    param(
        [string]$BuildDir,
        [string]$Config
    )

    $candidateA = Join-Path $BuildDir "$Config/PokemonAutochess.exe"
    if (Test-Path $candidateA) { return (Resolve-Path $candidateA).Path }

    $candidateB = Join-Path $BuildDir "PokemonAutochess.exe"
    if (Test-Path $candidateB) { return (Resolve-Path $candidateB).Path }

    throw "PokemonAutochess.exe not found under '$BuildDir' (config '$Config')."
}

function Parse-ResolutionToken {
    param([string]$Token)

    if ($Token -notmatch '^\s*(\d+)\s*[xX]\s*(\d+)\s*$') {
        throw "Invalid resolution token '$Token'. Expected format WIDTHxHEIGHT."
    }

    [PSCustomObject]@{
        Width = [int]$Matches[1]
        Height = [int]$Matches[2]
        Label = ("{0}x{1}" -f [int]$Matches[1], [int]$Matches[2])
        PixelCount = ([int]$Matches[1] * [int]$Matches[2])
    }
}

function Get-PrimaryDisplayWorkingArea {
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop | Out-Null
        $area = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
        if ($null -ne $area -and $area.Width -gt 0 -and $area.Height -gt 0) {
            return [PSCustomObject]@{
                Width = [int]$area.Width
                Height = [int]$area.Height
            }
        }
    } catch {
    }
    return $null
}

function Select-SmokeResolution {
    param([string[]]$SupportedResolutions)

    $parsed = @($SupportedResolutions | ForEach-Object { Parse-ResolutionToken ([string]$_) })
    if ($parsed.Count -eq 0) {
        throw "No runtime visual smoke resolutions were configured."
    }

    $display = Get-PrimaryDisplayWorkingArea
    if ($null -eq $display) {
        return ($parsed | Sort-Object PixelCount -Descending | Select-Object -First 1)
    }

    $fit = @(
        $parsed | Where-Object {
            $_.Width -le $display.Width -and $_.Height -le $display.Height
        } | Sort-Object PixelCount -Descending
    )

    if ($fit.Count -eq 0) {
        $configured = ($parsed | ForEach-Object { $_.Label }) -join ", "
        throw "None of the configured runtime-visual-smoke resolutions fit the current primary-display working area $($display.Width)x$($display.Height). Configured resolutions: $configured"
    }

    return $fit[0]
}

function Set-SmokeEnvVar {
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

function Restore-SmokeEnv {
    param([hashtable]$Backup)

    foreach ($entry in $Backup.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
}

function Invoke-RuntimeCapture {
    param(
        [string]$ExePath,
        [string]$Backend,
        [string]$OutputDir,
        [string]$SnapshotPath,
        [int]$Width,
        [int]$Height,
        [int]$ScreenshotFrame,
        [double]$FixedFrameDtSeconds,
        [int]$AutoQuitSeconds,
        [int]$WaitTimeoutSeconds
    )

    $backup = @{}
    $shotPath = Join-Path $OutputDir "$Backend.png"
    $stdoutPath = Join-Path $OutputDir "$Backend.stdout.log"
    $stderrPath = Join-Path $OutputDir "$Backend.stderr.log"

    try {
        Set-SmokeEnvVar -Name "PAC_RENDER_BACKEND" -Value $Backend -Backup $backup
        Set-SmokeEnvVar -Name "PAC_RANDOM_SEED" -Value "12345" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_VIDEO_WIDTH" -Value "$Width" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_VIDEO_HEIGHT" -Value "$Height" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_VIDEO_FULLSCREEN" -Value "0" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_VIDEO_VSYNC" -Value "0" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_VIDEO_FPS_CAP" -Value "0" -Backup $backup
        Set-SmokeEnvVar `
            -Name "PAC_FIXED_FRAME_DT_SECONDS" `
            -Value $FixedFrameDtSeconds.ToString("R", [Globalization.CultureInfo]::InvariantCulture) `
            -Backup $backup
        Set-SmokeEnvVar -Name "PAC_DEBUG_STATE_PATH" -Value $SnapshotPath -Backup $backup
        Set-SmokeEnvVar -Name "PAC_AUTO_LOAD_DEBUG_SNAPSHOT" -Value "1" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_PIN_DEBUG_SNAPSHOT_STATE" -Value "1" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_AUTO_QUIT_SECONDS" -Value "$AutoQuitSeconds" -Backup $backup
        Set-SmokeEnvVar -Name "PAC_BACKEND_SCREENSHOT_PATH" -Value $shotPath -Backup $backup
        Set-SmokeEnvVar -Name "PAC_BACKEND_SCREENSHOT_FRAME" -Value "$ScreenshotFrame" -Backup $backup

        if (Test-Path $shotPath) { Remove-Item $shotPath -Force }
        if (Test-Path $stdoutPath) { Remove-Item $stdoutPath -Force }
        if (Test-Path $stderrPath) { Remove-Item $stderrPath -Force }

        $process = Start-Process -FilePath $ExePath `
            -WorkingDirectory (Resolve-Path ".").Path `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -PassThru

        if (-not $process.WaitForExit($WaitTimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force
            throw "Runtime visual smoke for backend '$Backend' timed out before screenshot capture."
        }

        if (-not (Test-Path $shotPath)) {
            throw "Runtime visual smoke for backend '$Backend' did not produce screenshot '$shotPath'."
        }

        $stdout = @(Get-Content $stdoutPath -ErrorAction SilentlyContinue)
        $shotLine = $stdout | Where-Object { $_ -match "^\[Screenshot\]\[$($Backend.ToUpperInvariant())\] WROTE " } | Select-Object -Last 1
        if (-not $shotLine) {
            $shotLine = $stdout | Where-Object { $_ -match "^\[Screenshot\]" } | Select-Object -Last 1
        }
        if (-not $shotLine) {
            throw "Runtime visual smoke for backend '$Backend' did not report a successful screenshot write."
        }

        Write-Host "[RuntimeSmoke][$Backend] $shotLine"
        return @{
            ScreenshotPath = $shotPath
            StdoutPath = $stdoutPath
            StderrPath = $stderrPath
        }
    } finally {
        Restore-SmokeEnv -Backup $backup
    }
}

function Get-ScaledRegion {
    param(
        [int]$ImageWidth,
        [int]$ImageHeight,
        [double]$NormX,
        [double]$NormY,
        [double]$NormW,
        [double]$NormH
    )

    $x = [int][Math]::Floor($ImageWidth * $NormX)
    $y = [int][Math]::Floor($ImageHeight * $NormY)
    $w = [int][Math]::Floor($ImageWidth * $NormW)
    $h = [int][Math]::Floor($ImageHeight * $NormH)
    $w = [Math]::Max(1, $w)
    $h = [Math]::Max(1, $h)
    if (($x + $w) -gt $ImageWidth) { $w = $ImageWidth - $x }
    if (($y + $h) -gt $ImageHeight) { $h = $ImageHeight - $y }

    return @{
        X = $x
        Y = $y
        Width = $w
        Height = $h
    }
}

function Get-RuntimeImageMetrics {
    param(
        [string]$Path,
        [double]$NormX,
        [double]$NormY,
        [double]$NormW,
        [double]$NormH
    )

    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $region = Get-ScaledRegion `
            -ImageWidth $bmp.Width `
            -ImageHeight $bmp.Height `
            -NormX $NormX -NormY $NormY -NormW $NormW -NormH $NormH

        $bright = 0
        $warm = 0
        $blue = 0
        $sumL = 0.0
        $samples = 0

        for ($py = $region.Y; $py -lt ($region.Y + $region.Height); $py += 2) {
            for ($px = $region.X; $px -lt ($region.X + $region.Width); $px += 2) {
                $pixel = $bmp.GetPixel($px, $py)
                if (($pixel.R + $pixel.G + $pixel.B) -gt 420) { ++$bright }
                if ($pixel.R -gt 75 -and $pixel.R -gt ($pixel.G + 10) -and $pixel.G -gt $pixel.B) { ++$warm }
                if ($pixel.B -gt 70 -and $pixel.B -gt ($pixel.R + 5) -and $pixel.B -gt ($pixel.G + 5)) { ++$blue }
                $sumL += (($pixel.R + $pixel.G + $pixel.B) / 3.0) / 255.0
                ++$samples
            }
        }

        return [pscustomobject]@{
            X = $region.X
            Y = $region.Y
            Width = $region.Width
            Height = $region.Height
            BrightRatio = $bright / [Math]::Max(1.0, [double]$samples)
            WarmRatio = $warm / [Math]::Max(1.0, [double]$samples)
            BlueRatio = $blue / [Math]::Max(1.0, [double]$samples)
            AvgLuma = $sumL / [Math]::Max(1.0, [double]$samples)
        }
    } finally {
        $bmp.Dispose()
    }
}

function Assert-RuntimeImageRegion {
    param(
        [string]$CaseName,
        [string]$Path,
        [double]$NormX,
        [double]$NormY,
        [double]$NormW,
        [double]$NormH,
        [double]$MinBrightRatio,
        [double]$MinWarmRatio,
        [double]$MinBluePlusWarmRatio,
        [double]$MinAvgLuma
    )

    $metrics = Get-RuntimeImageMetrics `
        -Path $Path `
        -NormX $NormX -NormY $NormY -NormW $NormW -NormH $NormH

    $colorRatio = $metrics.BlueRatio + $metrics.WarmRatio
    Write-Host (
        "[RuntimeSmoke][$CaseName] region=$($metrics.X),$($metrics.Y) $($metrics.Width)x$($metrics.Height) " +
        ("bright={0:N4} warm={1:N4} blue={2:N4} avgL={3:N4}" -f `
            $metrics.BrightRatio, $metrics.WarmRatio, $metrics.BlueRatio, $metrics.AvgLuma))

    if ($metrics.BrightRatio -lt $MinBrightRatio) {
        throw "$CaseName bright ratio too low: $($metrics.BrightRatio) < $MinBrightRatio"
    }
    if ($metrics.WarmRatio -lt $MinWarmRatio) {
        throw "$CaseName warm ratio too low: $($metrics.WarmRatio) < $MinWarmRatio"
    }
    if ($colorRatio -lt $MinBluePlusWarmRatio) {
        throw "$CaseName blue+warm ratio too low: $colorRatio < $MinBluePlusWarmRatio"
    }
    if ($metrics.AvgLuma -lt $MinAvgLuma) {
        throw "$CaseName average luma too low: $($metrics.AvgLuma) < $MinAvgLuma"
    }
}

$exePath = Resolve-GameExePath -BuildDir $BuildDir -Config $Config
$repoRoot = (Resolve-Path ".").Path
$outputDirAbs = Join-Path $repoRoot $OutputDir
$snapshotAbs = (Resolve-Path $SnapshotPath).Path
New-Item -ItemType Directory -Path $outputDirAbs -Force | Out-Null

$selectedResolution = Select-SmokeResolution -SupportedResolutions $SupportedResolutions
$display = Get-PrimaryDisplayWorkingArea

Write-Host "[RuntimeSmoke] EXE: $exePath"
Write-Host "[RuntimeSmoke] Snapshot: $snapshotAbs"
Write-Host "[RuntimeSmoke] Output dir: $outputDirAbs"
if ($null -ne $display) {
    Write-Host "[RuntimeSmoke] Display working area: $($display.Width)x$($display.Height)"
}
Write-Host "[RuntimeSmoke] Selected resolution: $($selectedResolution.Label)"

foreach ($backend in $Backends) {
    $capture = Invoke-RuntimeCapture `
        -ExePath $exePath `
        -Backend $backend `
        -OutputDir $outputDirAbs `
        -SnapshotPath $snapshotAbs `
        -Width $selectedResolution.Width `
        -Height $selectedResolution.Height `
        -ScreenshotFrame $ScreenshotFrame `
        -FixedFrameDtSeconds $FixedFrameDtSeconds `
        -AutoQuitSeconds $AutoQuitSeconds `
        -WaitTimeoutSeconds $WaitTimeoutSeconds

    Assert-RuntimeImageRegion `
        -CaseName "$backend hud_left" `
        -Path $capture.ScreenshotPath `
        -NormX 0.0 -NormY 0.0 -NormW 0.28125 -NormH 0.3056 `
        -MinBrightRatio 0.01 `
        -MinWarmRatio 0.001 `
        -MinBluePlusWarmRatio 0.008 `
        -MinAvgLuma 0.012

    Assert-RuntimeImageRegion `
        -CaseName "$backend center_board" `
        -Path $capture.ScreenshotPath `
        -NormX 0.25 -NormY 0.1944 -NormW 0.5 -NormH 0.5 `
        -MinBrightRatio 0.60 `
        -MinWarmRatio 0.05 `
        -MinBluePlusWarmRatio 0.05 `
        -MinAvgLuma 0.45

    Assert-RuntimeImageRegion `
        -CaseName "$backend lower_center" `
        -Path $capture.ScreenshotPath `
        -NormX 0.2344 -NormY 0.5278 -NormW 0.5313 -NormH 0.3056 `
        -MinBrightRatio 0.40 `
        -MinWarmRatio 0.04 `
        -MinBluePlusWarmRatio 0.04 `
        -MinAvgLuma 0.34
}

Write-Host "[RuntimeSmoke] PASS"
