param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$Backend = "",
    [switch]$NoAutoLoad,
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
$exePath = Join-Path $repoRoot "$BuildDir/$Config/PokemonAutochess.exe"
$snapshotPath = Join-Path $repoRoot "config/debug/debug_state_snapshot_tail_fire_starter_line.json"

if (-not (Test-Path $exePath)) {
    throw "PokemonAutochess executable not found at $exePath. Build the target first or pass -BuildDir/-Config."
}

if (-not (Test-Path $snapshotPath)) {
    throw "Tail Fire starter-line snapshot not found at $snapshotPath."
}

$envKeys = @(
    "PAC_DEBUG_STATE_PATH",
    "PAC_AUTO_LOAD_DEBUG_SNAPSHOT",
    "PAC_RENDER_BACKEND"
)
$envBackup = @{}
foreach ($key in $envKeys) {
    $envBackup[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
}

try {
    $env:PAC_DEBUG_STATE_PATH = $snapshotPath
    if ($NoAutoLoad) {
        Remove-Item "Env:PAC_AUTO_LOAD_DEBUG_SNAPSHOT" -ErrorAction SilentlyContinue
    } else {
        $env:PAC_AUTO_LOAD_DEBUG_SNAPSHOT = "1"
    }
    if ([string]::IsNullOrWhiteSpace($Backend)) {
        Remove-Item "Env:PAC_RENDER_BACKEND" -ErrorAction SilentlyContinue
    } else {
        $env:PAC_RENDER_BACKEND = $Backend.Trim().ToLowerInvariant()
    }

    Write-Host "Native Charmander-line snapshot:"
    Write-Host "  $snapshotPath"
    if ($NoAutoLoad) {
        Write-Host "Auto-load is disabled for this launch. Press F9 in-game to restore the snapshot."
    } else {
    Write-Host "Auto-load is enabled for this launch."
    }
    Write-Host "Current expected behavior on mainline:"
    Write-Host "  Charmander, Charmeleon, and Charizard should all load from the snapshot."
    Write-Host "  The whole Charmander line should render its native layered-Unlit fire meshes with source animation."

    if ($NoLaunch) {
        return
    }

    Push-Location $repoRoot
    try {
        & $exePath
    } finally {
        Pop-Location
    }
} finally {
    foreach ($key in $envKeys) {
        $previous = $envBackup[$key]
        if ([string]::IsNullOrEmpty($previous)) {
            Remove-Item "Env:$key" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($key, $previous, "Process")
        }
    }
}
