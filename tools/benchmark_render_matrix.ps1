param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string[]]$Backends = @("opengl", "d3d12"),
    [string[]]$Resolutions = @("1280x720", "1600x900", "1920x1080"),
    [int]$DurationSeconds = 35,
    [int]$Seed = 12345,
    [int]$WarmupSamples = 5,
    [int]$MinScoredSamples = 10,
    [string]$OutDir = "benchmark",
    [string]$Tag = "",
    [string]$SnapshotPath = "",
    [switch]$AutoLoadSnapshot,
    [switch]$NoBuild,
    [switch]$AllowEmptySamples,
    [string]$BackendVertexDeform = "",
    [string]$BackendClipSkinning = "",
    [string]$BackendClipSkinningAdaptive = "",
    [string]$BackendClipSkinningMaxUnits = ""
)

$ErrorActionPreference = "Stop"

function Resolve-GameExecutablePath {
    param(
        [string]$BuildDir,
        [string]$Config
    )

    $candidates = @(
        (Join-Path $BuildDir "$Config\PokemonAutochess.exe"),
        (Join-Path $BuildDir "PokemonAutochess.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw "PokemonAutochess.exe not found in '$BuildDir' for config '$Config'."
}

function Parse-ResolutionToken {
    param([string]$Token)

    if ($Token -notmatch '^\s*(\d+)\s*[xX]\s*(\d+)\s*$') {
        throw "Invalid resolution token '$Token'. Expected format WIDTHxHEIGHT (example: 1600x900)."
    }

    $w = [int]$Matches[1]
    $h = [int]$Matches[2]
    if ($w -le 0 -or $h -le 0) {
        throw "Invalid resolution token '$Token'. Width/height must be > 0."
    }

    return @{
        Width = $w
        Height = $h
        Label = "${w}x${h}"
    }
}

function Extract-PerfSamples {
    param([string[]]$Lines)

    $samples = @()
    foreach ($line in $Lines) {
        if ($line -match '^\[PerfJSON\]\s*(\{.*\})\s*$') {
            try {
                $samples += ($Matches[1] | ConvertFrom-Json)
            } catch {
                Write-Warning "Failed to parse PerfJSON line: $line"
            }
        }
    }
    return $samples
}

function Get-AverageOrNull {
    param([double[]]$Values)

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return $null
    }
    return ($Values | Measure-Object -Average).Average
}

function Get-OnePercentLowOrNull {
    param([double[]]$Values)

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return $null
    }
    $sorted = $Values | Sort-Object
    $idx = [int][Math]::Floor(($sorted.Count - 1) * 0.01)
    return $sorted[$idx]
}

function Round-OrNull {
    param(
        [object]$Value,
        [int]$Digits = 3
    )

    if ($null -eq $Value) {
        return $null
    }
    return [Math]::Round([double]$Value, $Digits)
}

if (-not [string]::IsNullOrWhiteSpace($SnapshotPath)) {
    if (-not (Test-Path -Path $SnapshotPath -PathType Leaf)) {
        throw "Snapshot file not found: $SnapshotPath"
    }
    $SnapshotPath = (Resolve-Path -Path $SnapshotPath).Path
}

if (-not $NoBuild) {
    cmake --build $BuildDir --config $Config --target PokemonAutochess
}

$exePath = Resolve-GameExecutablePath -BuildDir $BuildDir -Config $Config
$exeInfo = Get-Item -Path $exePath
Write-Host "Using executable: $($exeInfo.FullName)"
Write-Host "Executable last write time: $($exeInfo.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"))"
Write-Host "Benchmark config: $Config"
Write-Host "Warmup samples: $WarmupSamples"
Write-Host "Minimum scored samples: $MinScoredSamples"
if (-not [string]::IsNullOrWhiteSpace($SnapshotPath)) {
    Write-Host "Snapshot path: $SnapshotPath"
}
if ($AutoLoadSnapshot) {
    if ([string]::IsNullOrWhiteSpace($SnapshotPath)) {
        Write-Host "Snapshot auto-load: enabled (runtime default snapshot path)"
    } else {
        Write-Host "Snapshot auto-load: enabled"
    }
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runName = if ([string]::IsNullOrWhiteSpace($Tag)) {
    "render_matrix_$timestamp"
} else {
    "render_matrix_${Tag}_$timestamp"
}
$rawDir = Join-Path $OutDir "${runName}_raw"
New-Item -ItemType Directory -Path $rawDir -Force | Out-Null

$csvPath = Join-Path $OutDir "$runName.csv"
$jsonPath = Join-Path $OutDir "$runName.json"

$envKeys = @(
    "PAC_RENDER_BACKEND",
    "PAC_RANDOM_SEED",
    "PAC_AUTO_QUIT_SECONDS",
    "PAC_VIDEO_WIDTH",
    "PAC_VIDEO_HEIGHT",
    "PAC_VIDEO_FULLSCREEN",
    "PAC_DEBUG_STATE_PATH",
    "PAC_AUTO_LOAD_DEBUG_SNAPSHOT",
    "PAC_BACKEND_VERTEX_DEFORM",
    "PAC_BACKEND_CLIP_SKINNING",
    "PAC_BACKEND_CLIP_SKINNING_ADAPTIVE",
    "PAC_BACKEND_CLIP_SKINNING_MAX_UNITS"
)
$envBackup = @{}
foreach ($key in $envKeys) {
    $envBackup[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
}

$rows = @()

try {
    foreach ($backendRaw in $Backends) {
        $backend = $backendRaw.Trim().ToLowerInvariant()
        if ([string]::IsNullOrWhiteSpace($backend)) {
            continue
        }

        foreach ($resolutionToken in $Resolutions) {
            $res = Parse-ResolutionToken -Token $resolutionToken
            Write-Host "Running benchmark row: backend=$backend resolution=$($res.Label) duration=${DurationSeconds}s"

            $env:PAC_RENDER_BACKEND = $backend
            $env:PAC_RANDOM_SEED = "$Seed"
            $env:PAC_AUTO_QUIT_SECONDS = "$DurationSeconds"
            $env:PAC_VIDEO_WIDTH = "$($res.Width)"
            $env:PAC_VIDEO_HEIGHT = "$($res.Height)"
            $env:PAC_VIDEO_FULLSCREEN = "0"
            if ([string]::IsNullOrWhiteSpace($SnapshotPath)) {
                Remove-Item "Env:PAC_DEBUG_STATE_PATH" -ErrorAction SilentlyContinue
            } else {
                $env:PAC_DEBUG_STATE_PATH = $SnapshotPath
            }
            if ($AutoLoadSnapshot) {
                $env:PAC_AUTO_LOAD_DEBUG_SNAPSHOT = "1"
            } else {
                Remove-Item "Env:PAC_AUTO_LOAD_DEBUG_SNAPSHOT" -ErrorAction SilentlyContinue
            }
            if ([string]::IsNullOrWhiteSpace($BackendVertexDeform)) {
                Remove-Item "Env:PAC_BACKEND_VERTEX_DEFORM" -ErrorAction SilentlyContinue
            } else {
                $env:PAC_BACKEND_VERTEX_DEFORM = $BackendVertexDeform
            }
            if ([string]::IsNullOrWhiteSpace($BackendClipSkinning)) {
                Remove-Item "Env:PAC_BACKEND_CLIP_SKINNING" -ErrorAction SilentlyContinue
            } else {
                $env:PAC_BACKEND_CLIP_SKINNING = $BackendClipSkinning
            }
            if ([string]::IsNullOrWhiteSpace($BackendClipSkinningAdaptive)) {
                Remove-Item "Env:PAC_BACKEND_CLIP_SKINNING_ADAPTIVE" -ErrorAction SilentlyContinue
            } else {
                $env:PAC_BACKEND_CLIP_SKINNING_ADAPTIVE = $BackendClipSkinningAdaptive
            }
            if ([string]::IsNullOrWhiteSpace($BackendClipSkinningMaxUnits)) {
                Remove-Item "Env:PAC_BACKEND_CLIP_SKINNING_MAX_UNITS" -ErrorAction SilentlyContinue
            } else {
                $env:PAC_BACKEND_CLIP_SKINNING_MAX_UNITS = $BackendClipSkinningMaxUnits
            }

            $stdoutPath = Join-Path $rawDir ("{0}_{1}.stdout.tmp.log" -f $backend, $res.Label)
            $stderrPath = Join-Path $rawDir ("{0}_{1}.stderr.tmp.log" -f $backend, $res.Label)
            try {
                $proc = Start-Process `
                    -FilePath $exePath `
                    -WorkingDirectory (Get-Location).Path `
                    -NoNewWindow `
                    -Wait `
                    -PassThru `
                    -RedirectStandardOutput $stdoutPath `
                    -RedirectStandardError $stderrPath

                $exitCode = $proc.ExitCode
                $runLines = @()
                if (Test-Path $stdoutPath) {
                    $runLines += @(Get-Content -Path $stdoutPath)
                }
                if (Test-Path $stderrPath) {
                    $runLines += @(Get-Content -Path $stderrPath)
                }
            } finally {
                Remove-Item -Path $stdoutPath -ErrorAction SilentlyContinue
                Remove-Item -Path $stderrPath -ErrorAction SilentlyContinue
            }

            $rawPath = Join-Path $rawDir ("{0}_{1}.log" -f $backend, $res.Label)
            Set-Content -Path $rawPath -Value $runLines -Encoding UTF8

            $samples = Extract-PerfSamples -Lines $runLines
            if ($samples.Count -eq 0) {
                $message = "No [PerfJSON] samples found for backend=$backend resolution=$($res.Label). " +
                           "This usually means the wrong/stale executable is being run or instrumentation is missing. " +
                           "Raw log: $rawPath"
                if ($AllowEmptySamples) {
                    Write-Warning $message
                } else {
                    throw $message
                }
            }
            $scoredSamples = @($samples | Select-Object -Skip $WarmupSamples)
            if ($samples.Count -gt 0 -and $scoredSamples.Count -lt $MinScoredSamples) {
                throw "Too few scored samples for backend=$backend resolution=$($res.Label): " +
                      "total=$($samples.Count), warmup=$WarmupSamples, scored=$($scoredSamples.Count), " +
                      "required=$MinScoredSamples. Raw log: $rawPath"
            }

            $fpsVals = @($scoredSamples | ForEach-Object { [double]$_.fps })
            $frameCpuVals = @($scoredSamples | ForEach-Object { [double]$_.frame_cpu_ms })
            $buildVals = @($scoredSamples | ForEach-Object { [double]$_.render_build_ms })
            $submitVals = @($scoredSamples | ForEach-Object { [double]$_.render_submit_ms })
            $presentVals = @($scoredSamples | ForEach-Object { [double]$_.present_wait_ms })
            $drawVals = @($scoredSamples | ForEach-Object { [double]$_.draw_calls })
            $triVals = @($scoredSamples | ForEach-Object { [double]$_.triangles })
            $visibleUnitVals = @($scoredSamples | ForEach-Object { [double]$_.visible_animated_units })
            $particleVals = @($scoredSamples | ForEach-Object { [double]$_.particle_count })
            $projectedUnitsVals = @($scoredSamples | ForEach-Object { [double]$_.projected_units_ms })
            $projectedPoseVals = @($scoredSamples | ForEach-Object { [double]$_.projected_pose_eval_ms })
            $projectedModelVals = @($scoredSamples | ForEach-Object { [double]$_.projected_model_ms })
            $projectedOverlayVals = @($scoredSamples | ForEach-Object { [double]$_.projected_overlay_ms })
            $projectedUnitsProcessedVals = @($scoredSamples | ForEach-Object { [double]$_.projected_units_processed })
            $projectedModelUnitsVals = @($scoredSamples | ForEach-Object { [double]$_.projected_model_units })
            $projectedClipSkinnedUnitVals = @($scoredSamples | ForEach-Object { [double]$_.projected_clip_skinned_units })

            $gpuValidSamples = @(
                $scoredSamples | Where-Object {
                    [int]$_.gpu_frame_valid -eq 1 -and [double]$_.gpu_frame_ms -ge 0.0
                }
            )
            $gpuVals = @($gpuValidSamples | ForEach-Object { [double]$_.gpu_frame_ms })
            $gpuValidRate = if ($scoredSamples.Count -gt 0) {
                [double]$gpuValidSamples.Count / [double]$scoredSamples.Count
            } else {
                $null
            }

            $row = [PSCustomObject][ordered]@{
                backend = $backend
                resolution = $res.Label
                width = $res.Width
                height = $res.Height
                sample_count_total = $samples.Count
                sample_count_scored = $scoredSamples.Count
                warmup_samples_skipped = $WarmupSamples
                process_exit_code = $exitCode
                avg_fps = Round-OrNull (Get-AverageOrNull $fpsVals)
                low_1pct_fps = Round-OrNull (Get-OnePercentLowOrNull $fpsVals)
                avg_frame_cpu_ms = Round-OrNull (Get-AverageOrNull $frameCpuVals)
                avg_render_build_ms = Round-OrNull (Get-AverageOrNull $buildVals)
                avg_render_submit_ms = Round-OrNull (Get-AverageOrNull $submitVals)
                avg_gpu_frame_ms = Round-OrNull (Get-AverageOrNull $gpuVals)
                gpu_frame_valid_rate = Round-OrNull $gpuValidRate
                avg_present_wait_ms = Round-OrNull (Get-AverageOrNull $presentVals)
                avg_draw_calls = Round-OrNull (Get-AverageOrNull $drawVals)
                avg_triangles = Round-OrNull (Get-AverageOrNull $triVals)
                avg_visible_animated_units = Round-OrNull (Get-AverageOrNull $visibleUnitVals)
                avg_particle_count = Round-OrNull (Get-AverageOrNull $particleVals)
                avg_projected_units_ms = Round-OrNull (Get-AverageOrNull $projectedUnitsVals)
                avg_projected_pose_eval_ms = Round-OrNull (Get-AverageOrNull $projectedPoseVals)
                avg_projected_model_ms = Round-OrNull (Get-AverageOrNull $projectedModelVals)
                avg_projected_overlay_ms = Round-OrNull (Get-AverageOrNull $projectedOverlayVals)
                avg_projected_units_processed = Round-OrNull (Get-AverageOrNull $projectedUnitsProcessedVals)
                avg_projected_model_units = Round-OrNull (Get-AverageOrNull $projectedModelUnitsVals)
                avg_projected_clip_skinned_units = Round-OrNull (Get-AverageOrNull $projectedClipSkinnedUnitVals)
                backend_vertex_deform = if ([string]::IsNullOrWhiteSpace($BackendVertexDeform)) { "default" } else { $BackendVertexDeform }
                backend_clip_skinning = if ([string]::IsNullOrWhiteSpace($BackendClipSkinning)) { "default" } else { $BackendClipSkinning }
                backend_clip_skinning_adaptive = if ([string]::IsNullOrWhiteSpace($BackendClipSkinningAdaptive)) { "default" } else { $BackendClipSkinningAdaptive }
                backend_clip_skinning_max_units = if ([string]::IsNullOrWhiteSpace($BackendClipSkinningMaxUnits)) { "default" } else { $BackendClipSkinningMaxUnits }
                snapshot_path = if ([string]::IsNullOrWhiteSpace($SnapshotPath)) { "" } else { $SnapshotPath }
                auto_load_snapshot = [bool]$AutoLoadSnapshot
                seed = $Seed
                duration_seconds = $DurationSeconds
                raw_log_path = $rawPath
            }
            $rows += $row
        }
    }
} finally {
    foreach ($key in $envKeys) {
        $previous = $envBackup[$key]
        if ($null -eq $previous) {
            Remove-Item "Env:$key" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($key, $previous, "Process")
        }
    }
}

$rows | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8

$payload = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    executable = $exePath
    build_dir = $BuildDir
    config = $Config
    seed = $Seed
    duration_seconds = $DurationSeconds
    warmup_samples = $WarmupSamples
    min_scored_samples = $MinScoredSamples
    backends = $Backends
    resolutions = $Resolutions
    snapshot_path = if ([string]::IsNullOrWhiteSpace($SnapshotPath)) { "" } else { $SnapshotPath }
    auto_load_snapshot = [bool]$AutoLoadSnapshot
    rows = $rows
}
$payload | ConvertTo-Json -Depth 6 | Set-Content -Path $jsonPath -Encoding UTF8

Write-Host ""
Write-Host "Benchmark matrix complete."
Write-Host "CSV : $csvPath"
Write-Host "JSON: $jsonPath"
Write-Host "Raw : $rawDir"
Write-Host ""
$rows | Format-Table backend, resolution, avg_fps, low_1pct_fps, avg_frame_cpu_ms, avg_render_build_ms, avg_gpu_frame_ms, avg_projected_units_ms, avg_projected_model_ms, avg_projected_clip_skinned_units, avg_present_wait_ms, avg_draw_calls, avg_triangles, avg_visible_animated_units, avg_particle_count, sample_count_scored -AutoSize
