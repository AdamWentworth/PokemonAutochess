param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$OutputDir = "debug/parity",
    [int]$AutoQuitSeconds = 3,
    [int]$ScreenshotFrame = 120,
    [double]$MeanDiffThreshold = 0.035
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

function Invoke-BackendScreenshot {
    param(
        [string]$ExePath,
        [string]$Backend,
        [string]$ScreenshotPath,
        [int]$AutoQuitSeconds,
        [int]$ScreenshotFrame
    )

    $oldBackend = $env:PAC_RENDER_BACKEND
    $oldAutoQuit = $env:PAC_AUTO_QUIT_SECONDS
    $oldShotPath = $env:PAC_BACKEND_SCREENSHOT_PATH
    $oldShotFrame = $env:PAC_BACKEND_SCREENSHOT_FRAME
    $oldFatal = $env:PAC_PARITY_CONTRACT_FATAL

    try {
        $env:PAC_RENDER_BACKEND = $Backend
        $env:PAC_AUTO_QUIT_SECONDS = "$AutoQuitSeconds"
        $env:PAC_BACKEND_SCREENSHOT_PATH = $ScreenshotPath
        $env:PAC_BACKEND_SCREENSHOT_FRAME = "$ScreenshotFrame"
        $env:PAC_PARITY_CONTRACT_FATAL = "1"

        if (Test-Path $ScreenshotPath) {
            Remove-Item $ScreenshotPath -Force
        }

        $quotedExe = '"' + $ExePath + '"'
        $log = @(cmd /c "$quotedExe 2>&1")
        $logLines = @($log | ForEach-Object { [string]$_ } | Where-Object { $_ -ne "" })
        $shotLine = $logLines | Where-Object { $_ -match "^\[Screenshot\]" } | Select-Object -Last 1
        if (-not $shotLine) {
            throw "No [Screenshot] line observed for backend '$Backend'."
        }
        if (-not (Test-Path $ScreenshotPath)) {
            throw "Screenshot file not produced for backend '$Backend': $ScreenshotPath"
        }
        Write-Host "[Shot][$Backend] $shotLine"
    } finally {
        if ($null -ne $oldBackend) { $env:PAC_RENDER_BACKEND = $oldBackend } else { Remove-Item Env:PAC_RENDER_BACKEND -ErrorAction SilentlyContinue }
        if ($null -ne $oldAutoQuit) { $env:PAC_AUTO_QUIT_SECONDS = $oldAutoQuit } else { Remove-Item Env:PAC_AUTO_QUIT_SECONDS -ErrorAction SilentlyContinue }
        if ($null -ne $oldShotPath) { $env:PAC_BACKEND_SCREENSHOT_PATH = $oldShotPath } else { Remove-Item Env:PAC_BACKEND_SCREENSHOT_PATH -ErrorAction SilentlyContinue }
        if ($null -ne $oldShotFrame) { $env:PAC_BACKEND_SCREENSHOT_FRAME = $oldShotFrame } else { Remove-Item Env:PAC_BACKEND_SCREENSHOT_FRAME -ErrorAction SilentlyContinue }
        if ($null -ne $oldFatal) { $env:PAC_PARITY_CONTRACT_FATAL = $oldFatal } else { Remove-Item Env:PAC_PARITY_CONTRACT_FATAL -ErrorAction SilentlyContinue }
    }
}

function Get-MeanRgbDiff {
    param(
        [string]$PathA,
        [string]$PathB
    )

    Add-Type -AssemblyName System.Drawing
    $bmpA = [System.Drawing.Bitmap]::FromFile($PathA)
    $bmpB = [System.Drawing.Bitmap]::FromFile($PathB)
    try {
        if ($bmpA.Width -ne $bmpB.Width -or $bmpA.Height -ne $bmpB.Height) {
            throw "Image size mismatch: A=$($bmpA.Width)x$($bmpA.Height) B=$($bmpB.Width)x$($bmpB.Height)"
        }

        $width = $bmpA.Width
        $height = $bmpA.Height
        $sum = 0.0
        $count = [double]($width * $height * 3)

        for ($y = 0; $y -lt $height; ++$y) {
            for ($x = 0; $x -lt $width; ++$x) {
                $a = $bmpA.GetPixel($x, $y)
                $b = $bmpB.GetPixel($x, $y)
                $sum += [math]::Abs($a.R - $b.R) / 255.0
                $sum += [math]::Abs($a.G - $b.G) / 255.0
                $sum += [math]::Abs($a.B - $b.B) / 255.0
            }
        }

        return $sum / [math]::Max(1.0, $count)
    } finally {
        $bmpA.Dispose()
        $bmpB.Dispose()
    }
}

$exePath = Resolve-GameExePath -BuildDir $BuildDir -Config $Config
$outDirAbs = (Resolve-Path -Path .).Path
$outDirAbs = Join-Path $outDirAbs $OutputDir
New-Item -ItemType Directory -Path $outDirAbs -Force | Out-Null

$openglShot = Join-Path $outDirAbs "opengl.png"
$d3d12Shot = Join-Path $outDirAbs "d3d12.png"

Write-Host "[ShotDiff] EXE: $exePath"
Write-Host "[ShotDiff] Output dir: $outDirAbs"

Invoke-BackendScreenshot -ExePath $exePath -Backend "opengl" -ScreenshotPath $openglShot -AutoQuitSeconds $AutoQuitSeconds -ScreenshotFrame $ScreenshotFrame
Invoke-BackendScreenshot -ExePath $exePath -Backend "d3d12" -ScreenshotPath $d3d12Shot -AutoQuitSeconds $AutoQuitSeconds -ScreenshotFrame $ScreenshotFrame

$meanDiff = Get-MeanRgbDiff -PathA $openglShot -PathB $d3d12Shot
Write-Host ("[ShotDiff] mean_rgb_abs_diff={0:N6} threshold={1:N6}" -f $meanDiff, $MeanDiffThreshold)

if ($meanDiff -gt $MeanDiffThreshold) {
    throw ("Screenshot parity diff too high: mean={0:N6} > threshold={1:N6}" -f $meanDiff, $MeanDiffThreshold)
}

Write-Host "[ShotDiff] PASS"
