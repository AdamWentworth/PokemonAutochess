param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$BaselinePath = "config/perf/release_perf_smoke_starter_line.json",
    [string]$OutDir = "benchmark_smoke",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return (Resolve-Path $Path).Path
    }
    return (Resolve-Path (Join-Path (Resolve-Path ".").Path $Path)).Path
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

function Get-PerfRowKey {
    param($Row)
    return ($Row.backend.ToString().ToLowerInvariant() + "|" + $Row.resolution.ToString().ToLowerInvariant())
}

function Round-PerfValue {
    param([double]$Value)
    return [Math]::Round($Value, 3)
}

$baselineAbs = Resolve-RepoPath $BaselinePath
$baseline = Get-Content $baselineAbs | ConvertFrom-Json
$repoRoot = (Resolve-Path ".").Path
$perfScript = Join-Path $PSScriptRoot "benchmark_render_matrix.ps1"
$tag = $baseline.name
$outDirAbs = Join-Path $repoRoot $OutDir
New-Item -ItemType Directory -Path $outDirAbs -Force | Out-Null

$existingJson = @{}
Get-ChildItem -Path $outDirAbs -Filter "render_matrix_${tag}_*.json" -ErrorAction SilentlyContinue |
    ForEach-Object { $existingJson[$_.FullName] = $true }

$snapshotPath = $null
if ($baseline.benchmark.snapshot_path) {
    $snapshotPath = Resolve-RepoPath $baseline.benchmark.snapshot_path
}

$configuredResolutions = @($baseline.benchmark.resolutions | ForEach-Object { Parse-ResolutionToken ([string]$_) })
$displayWorkingArea = Get-PrimaryDisplayWorkingArea
$selectedResolutionLabels = @()
if ($null -ne $displayWorkingArea) {
    $fittingResolutions = @(
        $configuredResolutions | Where-Object {
            $_.Width -le $displayWorkingArea.Width -and $_.Height -le $displayWorkingArea.Height
        } | Sort-Object PixelCount -Descending
    )
    if ($fittingResolutions.Count -eq 0) {
        $available = ($configuredResolutions | ForEach-Object { $_.Label }) -join ", "
        throw "None of the configured perf-smoke resolutions fit the current primary-display working area $($displayWorkingArea.Width)x$($displayWorkingArea.Height). Configured resolutions: $available. Add a smaller protected baseline row or run the lower-level benchmark_render_matrix.ps1 probe at a fitting local resolution."
    }
    $selectedResolutionLabels = @($fittingResolutions[0].Label)
} else {
    $selectedResolutionLabels = @($configuredResolutions | ForEach-Object { $_.Label })
}

$benchParams = @{
    BuildDir = $BuildDir
    Config = $Config
    Backends = @($baseline.benchmark.backends | ForEach-Object { [string]$_ })
    Resolutions = $selectedResolutionLabels
    DurationSeconds = [int]$baseline.benchmark.duration_seconds
    WarmupSamples = [int]$baseline.benchmark.warmup_samples
    MinScoredSamples = [int]$baseline.benchmark.min_scored_samples
    OutDir = $OutDir
    Tag = $tag
    VideoVsync = "0"
    VideoFpsCap = "0"
}
if ($snapshotPath) {
    $benchParams.SnapshotPath = $snapshotPath
}
if ([bool]$baseline.benchmark.auto_load_snapshot) {
    $benchParams.AutoLoadSnapshot = $true
}
if ([bool]$baseline.benchmark.pin_snapshot_state) {
    $benchParams.PinSnapshotState = $true
}
if ($NoBuild.IsPresent) {
    $benchParams.NoBuild = $true
}

Write-Host "[PerfSmoke] Baseline: $baselineAbs"
Write-Host "[PerfSmoke] Output dir: $outDirAbs"
if ($null -ne $displayWorkingArea) {
    Write-Host "[PerfSmoke] Display working area: $($displayWorkingArea.Width)x$($displayWorkingArea.Height)"
}
Write-Host "[PerfSmoke] Selected resolution(s): $($selectedResolutionLabels -join ', ')"

& $perfScript @benchParams

$newJson = Get-ChildItem -Path $outDirAbs -Filter "render_matrix_${tag}_*.json" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending |
    Where-Object { -not $existingJson.ContainsKey($_.FullName) } |
    Select-Object -First 1

if ($null -eq $newJson) {
    throw "Perf smoke did not produce a new benchmark JSON under '$outDirAbs'."
}

$run = Get-Content $newJson.FullName | ConvertFrom-Json
$thresholds = $baseline.thresholds
$actualRows = @{}
foreach ($row in $run.rows) {
    $actualRows[(Get-PerfRowKey $row)] = $row
}

$failures = @()
foreach ($baselineRow in @($baseline.rows | Where-Object {
    $selectedResolutionLabels -contains $_.resolution.ToString()
})) {
    $key = Get-PerfRowKey $baselineRow
    if (-not $actualRows.ContainsKey($key)) {
        $failures += "Missing benchmark row for $key"
        continue
    }

    $actual = $actualRows[$key]
    $minFps = [double]$baselineRow.avg_fps * [double]$thresholds.min_avg_fps_ratio
    $maxCpu = [double]$baselineRow.avg_frame_cpu_ms * [double]$thresholds.max_avg_frame_cpu_ms_ratio
    $maxBuild = [double]$baselineRow.avg_render_build_ms * [double]$thresholds.max_avg_render_build_ms_ratio

    Write-Host (
        "[PerfSmoke][$($baselineRow.backend) $($baselineRow.resolution)] " +
        ("fps={0:N3} min={1:N3} | cpu_ms={2:N3} max={3:N3} | build_ms={4:N3} max={5:N3}" -f `
            [double]$actual.avg_fps, $minFps,
            [double]$actual.avg_frame_cpu_ms, $maxCpu,
            [double]$actual.avg_render_build_ms, $maxBuild))

    if ([double]$actual.avg_fps -lt $minFps) {
        $failures += "$($baselineRow.backend) $($baselineRow.resolution) avg_fps regressed: $([double]$actual.avg_fps) < $minFps"
    }
    if ([double]$actual.avg_frame_cpu_ms -gt $maxCpu) {
        $failures += "$($baselineRow.backend) $($baselineRow.resolution) avg_frame_cpu_ms regressed: $([double]$actual.avg_frame_cpu_ms) > $maxCpu"
    }
    if ([double]$actual.avg_render_build_ms -gt $maxBuild) {
        $failures += "$($baselineRow.backend) $($baselineRow.resolution) avg_render_build_ms regressed: $([double]$actual.avg_render_build_ms) > $maxBuild"
    }
}

if ($failures.Count -gt 0) {
    $message = "[PerfSmoke] FAIL`n - " + ($failures -join "`n - ")
    throw $message
}

Write-Host "[PerfSmoke] PASS"
