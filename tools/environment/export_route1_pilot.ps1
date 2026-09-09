[CmdletBinding()]
param(
    [string]$BlendFile = '',
    [string]$ResearchRoot = '',
    [string]$Blender = "$env:ProgramFiles/Blender Foundation/Blender 4.5/blender.exe",
    [switch]$OpenBlender,
    [switch]$UseExistingExport,
    [switch]$Capture
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ResearchRoot) {
    $ResearchRoot = Join-Path ([IO.Path]::GetFullPath((Join-Path $taskGameRoot '../..'))) 'Research/PokemonSwitchAssetResearch'
}
if (-not $BlendFile) {
    if (-not $env:PHLOSION_ASSET_DEPOT) { throw 'Set PHLOSION_ASSET_DEPOT or supply -BlendFile.' }
    $BlendFile = Join-Path (Split-Path $env:PHLOSION_ASSET_DEPOT -Parent) 'EnvironmentResearch/Route1/authoring/arena-pilot/Route1_GardenClearing.blend'
}
$BlendFile = [IO.Path]::GetFullPath($BlendFile)
$taskBridge = Join-Path $ResearchRoot 'tools/lgpe_blender_bridge/arena_pilot.py'
$taskForge = Join-Path $taskGameRoot 'build/Release/PhlosionForge.exe'
foreach ($taskRequired in @($Blender, $BlendFile, $taskBridge, $taskForge)) {
    if (-not (Test-Path -LiteralPath $taskRequired)) { throw "Required file missing: $taskRequired" }
}
if ($OpenBlender) {
    # This is the explicitly requested visible authoring window.
    Start-Process -FilePath $Blender -WorkingDirectory $taskGameRoot -ArgumentList @(
        '--disable-autoexec', "`"$BlendFile`"", '--python', "`"$taskBridge`"", '--', 'ui', '--game-root', "`"$taskGameRoot`"")
    return
}
$taskExport = Join-Path (Split-Path $BlendFile -Parent) 'export'
if (-not $UseExistingExport) {
    & $Blender --background --factory-startup --disable-autoexec $BlendFile --python-exit-code 1 --python $taskBridge -- export --output $taskExport
    if ($LASTEXITCODE -ne 0) { throw 'Blender export failed; game files were not changed.' }
}
$taskReport = Get-Content (Join-Path $taskExport 'export-report.json') -Raw | ConvertFrom-Json
if ([IO.Path]::GetFullPath($taskReport.source_blend) -ne $BlendFile) { throw 'Export belongs to a different Blender file.' }
$taskCompiled = Join-Path $taskExport 'terrain.phpatch'
Push-Location $taskGameRoot
try {
    & $taskForge compile-environment-patch (Join-Path $taskExport 'terrain.patch.json') $taskCompiled
    if ($LASTEXITCODE -ne 0) { throw 'Patch compilation failed; game files were not changed.' }
    $taskDestination = Join-Path $taskGameRoot 'content/phlosion/environment/arena-pilot/terrain.phpatch'
    $taskScene = Join-Path $taskGameRoot 'scenes/route1_pilot.scene.json'
    $taskPrevious = Join-Path $taskExport 'previous-install'
    New-Item -ItemType Directory -Path (Split-Path $taskDestination -Parent), $taskPrevious -Force | Out-Null
    $taskHadPatch = Test-Path -LiteralPath $taskDestination
    $taskHadScene = Test-Path -LiteralPath $taskScene
    if ($taskHadPatch) { Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskPrevious 'terrain.phpatch') }
    if ($taskHadScene) { Copy-Item -LiteralPath $taskScene -Destination (Join-Path $taskPrevious 'route1_pilot.scene.json') }
    try {
        Copy-Item -LiteralPath $taskCompiled -Destination $taskDestination
        Copy-Item -LiteralPath (Join-Path $taskExport 'route1_pilot.scene.json') -Destination $taskScene
        & $taskForge validate-authored-environment 'scenes/route1_pilot.scene.json'
        if ($LASTEXITCODE -ne 0) { throw 'Installed scene failed validation.' }
    } catch {
        if ($taskHadPatch) { Copy-Item -LiteralPath (Join-Path $taskPrevious 'terrain.phpatch') -Destination $taskDestination }
        else { Remove-Item -LiteralPath $taskDestination -ErrorAction SilentlyContinue }
        if ($taskHadScene) { Copy-Item -LiteralPath (Join-Path $taskPrevious 'route1_pilot.scene.json') -Destination $taskScene }
        else { Remove-Item -LiteralPath $taskScene -ErrorAction SilentlyContinue }
        throw
    }
    if ($env:PHLOSION_ASSET_DEPOT) {
        $taskDepotSource = Join-Path $env:PHLOSION_ASSET_DEPOT 'pokemon-autochess/source/project-authored/route1-arena-pilot'
        $taskDepotRuntime = Join-Path $env:PHLOSION_ASSET_DEPOT 'pokemon-autochess/runtime/content/phlosion/environment/arena-pilot'
        New-Item -ItemType Directory -Path $taskDepotSource, $taskDepotRuntime -Force | Out-Null
        Copy-Item -LiteralPath $BlendFile -Destination (Join-Path $taskDepotSource 'Route1_GardenClearing.blend')
        Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskDepotRuntime 'terrain.phpatch')
        Copy-Item -LiteralPath $taskScene -Destination (Join-Path $taskDepotSource 'route1_pilot.scene.json')
    }
    Write-Output "Installed Route 1 Arena Pilot from $BlendFile"
    if ($Capture) { & (Join-Path $PSScriptRoot 'capture_arena_pilot.ps1') }
} finally { Pop-Location }
