param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [ValidateSet("VfxLab", "PAC_VfxPreviewer")]
    [string]$App = "VfxLab",
    [string]$Effect = "Tackle",
    [string]$InitialRig = "",
    [int]$StartFrame = 1,
    [int]$EndFrame = 18,
    [string]$OutputDir = "debug/vfx_focus_frames",
    [double]$FocusTightness = 0.85,
    [double]$FixedDtSeconds = (1.0 / 30.0),
    [int]$AutoQuitSeconds = 20
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

    if ([string]::IsNullOrWhiteSpace($Value)) {
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

function Invoke-FocusedFrameCapture {
    param(
        [string]$ExePath,
        [string]$Effect,
        [string]$InitialRig,
        [int]$Frame,
        [string]$ShotPath,
        [double]$FocusTightness,
        [double]$FixedDtSeconds,
        [int]$AutoQuitSeconds
    )

    $backup = @{}
    $stdoutPath = "$ShotPath.stdout.log"
    $stderrPath = "$ShotPath.stderr.log"

    try {
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_INITIAL_EFFECT" -Value $Effect -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_INITIAL_RIG" -Value $InitialRig -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_AUTO_FOCUS_EFFECT" -Value "1" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_FOCUS_TIGHTNESS" -Value "$FocusTightness" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_SCREENSHOT_PATH" -Value $ShotPath -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_SCREENSHOT_FRAME" -Value "$Frame" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_EXIT_AFTER_SCREENSHOT" -Value "1" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_AUTO_QUIT_SECONDS" -Value "$AutoQuitSeconds" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_HIDE_HELP" -Value "1" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_HIDE_GUIDES" -Value "1" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_PREVIEW_CHARACTER_INKING" -Value "0" -Backup $backup
        Set-PreviewEnvVar -Name "PAC_VFX_PREVIEW_FIXED_DT_SECONDS" -Value "$FixedDtSeconds" -Backup $backup

        if (Test-Path $ShotPath) { Remove-Item $ShotPath -Force }
        if (Test-Path $stdoutPath) { Remove-Item $stdoutPath -Force }
        if (Test-Path $stderrPath) { Remove-Item $stderrPath -Force }

        $process = Start-Process -FilePath $ExePath `
            -WorkingDirectory (Resolve-Path ".").Path `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -PassThru

        if (-not $process.WaitForExit(($AutoQuitSeconds + 15) * 1000)) {
            Stop-Process -Id $process.Id -Force
            throw "Frame $Frame timed out without exiting after screenshot capture."
        }

        $process.Refresh()
        if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
            throw "Frame $Frame exited with code $($process.ExitCode)."
        }

        if (-not (Test-Path $ShotPath)) {
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
            throw "Frame $Frame did not produce screenshot '$ShotPath'.$detailText"
        }

        Write-Host ("[VfxFocusCapture] frame={0} path={1}" -f $Frame, $ShotPath)
    } finally {
        Restore-PreviewEnv -Backup $backup
    }
}

if ($StartFrame -lt 0) {
    throw "StartFrame must be >= 0."
}
if ($EndFrame -lt $StartFrame) {
    throw "EndFrame must be >= StartFrame."
}

$exeName = "$App.exe"
$exePath = Resolve-PreviewExePath -BuildDir $BuildDir -Config $Config -ExeName $exeName
$resolvedOutputDir = Resolve-Path "." | ForEach-Object { Join-Path $_.Path $OutputDir }
New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

$safeEffect = ($Effect -replace "[^A-Za-z0-9_-]", "_")
$caseDir = Join-Path $resolvedOutputDir ("{0}_{1}" -f $App, $safeEffect)
New-Item -ItemType Directory -Force -Path $caseDir | Out-Null

Write-Host "[VfxFocusCapture] exe=$exePath"
Write-Host "[VfxFocusCapture] effect=$Effect app=$App frames=$StartFrame..$EndFrame"
Write-Host "[VfxFocusCapture] output=$caseDir"

for ($frame = $StartFrame; $frame -le $EndFrame; ++$frame) {
    $shotPath = Join-Path $caseDir ("frame_{0:D3}.png" -f $frame)
    Invoke-FocusedFrameCapture `
        -ExePath $exePath `
        -Effect $Effect `
        -InitialRig $InitialRig `
        -Frame $frame `
        -ShotPath $shotPath `
        -FocusTightness $FocusTightness `
        -FixedDtSeconds $FixedDtSeconds `
        -AutoQuitSeconds $AutoQuitSeconds
}

Write-Host "[VfxFocusCapture] PASS"
