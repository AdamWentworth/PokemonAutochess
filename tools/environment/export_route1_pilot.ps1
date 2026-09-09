[CmdletBinding()]
param(
    [string]$BlendFile = '',
    [string]$Blender = "$env:ProgramFiles/Blender Foundation/Blender 4.5/blender.exe",
    [switch]$OpenBlender,
    [switch]$UseExistingExport,
    [switch]$Capture
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskRecipe = Get-Content (Join-Path $taskGameRoot 'config/environment/route1_south_entrance.authoring.json') -Raw | ConvertFrom-Json
if (-not $BlendFile) {
    if (-not $env:PHLOSION_ASSET_DEPOT) { throw 'Set PHLOSION_ASSET_DEPOT or supply -BlendFile.' }
    $BlendFile = Join-Path (Split-Path $env:PHLOSION_ASSET_DEPOT -Parent) $taskRecipe.working_source_relative
}
$BlendFile = [IO.Path]::GetFullPath($BlendFile)
$taskBridge = Join-Path $PSScriptRoot 'blender/arena_pilot.py'
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
if ($taskReport.scene_id -ne $taskRecipe.scene_id) { throw 'Export belongs to a different arena scene.' }
$taskCompiled = Join-Path $taskExport 'terrain.phpatch'
Push-Location $taskGameRoot
try {
    & $taskForge compile-environment-patch (Join-Path $taskExport 'terrain.patch.json') $taskCompiled
    if ($LASTEXITCODE -ne 0) { throw 'Patch compilation failed; game files were not changed.' }
    $taskDestination = Join-Path $taskGameRoot $taskRecipe.terrain_path
    $taskScene = Join-Path $taskGameRoot $taskRecipe.scene_path
    $taskMap = Join-Path $taskGameRoot $taskRecipe.gameplay_map_path
    & python (Join-Path $PSScriptRoot 'blender/arena_map.py') --map (Join-Path $taskExport 'arena-map.json') `
        --scene (Join-Path $taskExport 'route1_pilot.scene.json') --board $taskRecipe.board_path --composition $taskRecipe.composition_path
    if ($LASTEXITCODE -ne 0) { throw 'Arena gameplay data failed validation; game files were not changed.' }
    $taskPrevious = Join-Path $taskExport 'previous-install'
    New-Item -ItemType Directory -Path (Split-Path $taskDestination -Parent), $taskPrevious -Force | Out-Null
    $taskHadPatch = Test-Path -LiteralPath $taskDestination
    $taskHadScene = Test-Path -LiteralPath $taskScene
    $taskHadMap = Test-Path -LiteralPath $taskMap
    if ($taskHadPatch) { Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskPrevious 'terrain.phpatch') }
    if ($taskHadScene) { Copy-Item -LiteralPath $taskScene -Destination (Join-Path $taskPrevious 'route1_pilot.scene.json') }
    if ($taskHadMap) { Copy-Item -LiteralPath $taskMap -Destination (Join-Path $taskPrevious 'arena-map.json') }
    try {
        Copy-Item -LiteralPath $taskCompiled -Destination $taskDestination
        Copy-Item -LiteralPath (Join-Path $taskExport 'route1_pilot.scene.json') -Destination $taskScene
        Copy-Item -LiteralPath (Join-Path $taskExport 'arena-map.json') -Destination $taskMap
        & $taskForge validate-authored-environment $taskRecipe.scene_path
        if ($LASTEXITCODE -ne 0) { throw 'Installed scene failed validation.' }
    } catch {
        if ($taskHadPatch) { Copy-Item -LiteralPath (Join-Path $taskPrevious 'terrain.phpatch') -Destination $taskDestination }
        else { Remove-Item -LiteralPath $taskDestination -ErrorAction SilentlyContinue }
        if ($taskHadScene) { Copy-Item -LiteralPath (Join-Path $taskPrevious 'route1_pilot.scene.json') -Destination $taskScene }
        else { Remove-Item -LiteralPath $taskScene -ErrorAction SilentlyContinue }
        if ($taskHadMap) { Copy-Item -LiteralPath (Join-Path $taskPrevious 'arena-map.json') -Destination $taskMap }
        else { Remove-Item -LiteralPath $taskMap -ErrorAction SilentlyContinue }
        throw
    }
    if ($env:PHLOSION_ASSET_DEPOT) {
        $taskDepotSource = Join-Path $env:PHLOSION_ASSET_DEPOT 'pokemon-autochess/source/project-authored/route1-arena-pilot'
        $taskDepotRuntime = Join-Path $env:PHLOSION_ASSET_DEPOT 'pokemon-autochess/runtime/content/phlosion/environment/arena-pilot'
        New-Item -ItemType Directory -Path $taskDepotSource, $taskDepotRuntime -Force | Out-Null
        Copy-Item -LiteralPath $BlendFile -Destination (Join-Path $taskDepotSource 'Route1_GardenClearing.blend')
        Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskDepotRuntime 'terrain.phpatch')
        Copy-Item -LiteralPath $taskScene -Destination (Join-Path $taskDepotSource 'route1_pilot.scene.json')
        Copy-Item -LiteralPath $taskMap -Destination (Join-Path $taskDepotSource 'arena-map.json')
    }
    Write-Output "Installed Route 1 Arena Pilot from $BlendFile"
    if ($Capture) { & (Join-Path $PSScriptRoot 'capture_arena_pilot.ps1') }
} finally { Pop-Location }
