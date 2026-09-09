[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build',
    [string]$Recipe = 'config/environment/route1_south_entrance.authoring.json',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$OutputDirectory = 'debug/south-entrance-check',
    [switch]$NoBuild,
    [switch]$IncludeBlender,
    [switch]$Capture,
    [string]$BlendFile = '',
    [string]$Blender = "$env:ProgramFiles/Blender Foundation/Blender 4.5/blender.exe"
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot, $OutputDirectory))
$taskBuild = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot, $BuildDirectory))
$taskRecipe = Get-Content ([IO.Path]::Combine($taskRoot, $Recipe)) -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskResults = [Collections.Generic.List[object]]::new()
$taskPassed = $false
$taskFailure = ''
$taskPreviousDataRoot = $env:PHLOSION_DATA_ROOT
$taskPreviousAssetRoot = $env:PHLOSION_ASSET_ROOT

function Invoke-Check {
    param([string]$Name, [string]$Program, [string[]]$Arguments)
    Write-Host "Checking $Name..."
    $taskLog = Join-Path $taskOutput "$Name.log"
    $null = Get-Command $Program -ErrorAction Stop
    $taskSavedErrorPreference = $ErrorActionPreference
    try {
        # Windows PowerShell wraps native stderr (including unittest's normal
        # progress) as ErrorRecords. The process exit code decides success.
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments *> $taskLog
        $taskExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $taskSavedErrorPreference
    }
    $taskResults.Add(@{ name = $Name; passed = ($taskExit -eq 0); log = $taskLog })
    if ($taskExit -ne 0) {
        Get-Content -LiteralPath $taskLog -Tail 35 | Write-Host
        throw "$Name failed with exit code $taskExit. See $taskLog"
    }
}

Push-Location $taskRoot
try {
    $env:PHLOSION_DATA_ROOT = $taskRoot
    $env:PHLOSION_ASSET_ROOT = Join-Path $taskRoot 'assets'
    if (($Capture -or $IncludeBlender) -and
        ($Configuration -ne 'Release' -or $taskBuild -ne (Join-Path $taskRoot 'build'))) {
        throw 'Blender round-trip and fixed-camera checks use the standard build/Release executables.'
    }
    # These are qualification prerequisites, not skipped test cases.
    foreach ($taskRelative in @('content/phlosion/scenes/route1.phscene', 'assets/models/0001_Bulbasaur_SV.phmodel', $taskRecipe.bundle_path)) {
        if (-not (Test-Path -LiteralPath (Join-Path $taskRoot $taskRelative))) {
            throw "Missing private asset: $taskRelative. Restore the depot with tools/assets/sync_asset_depot.ps1."
        }
    }
    $taskCookedModels = @(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'content/phlosion/objects') -Recurse -File -Filter '*Bulbasaur*.phlo' |
        Select-Object -ExpandProperty Name)
    foreach ($taskVariant in @('0001_Bulbasaur_SV.phlo', '0001_Bulbasaur_SV_Shiny.phlo')) {
        if ($taskVariant -notin $taskCookedModels) { throw "Missing cooked model: $taskVariant. Restore the runtime asset depot." }
    }
    Invoke-Check 'docs' 'powershell' @('-NoProfile', '-File', 'tools/check_docs_hygiene.ps1', '-BuildDir', $taskBuild)
    Invoke-Check 'arena-data' 'python' @('tools/environment/blender/arena_map.py', '--map', $taskRecipe.gameplay_map_path,
        '--scene', $taskRecipe.scene_path, '--board', $taskRecipe.board_path, '--composition', $taskRecipe.composition_path)
    Invoke-Check 'arena-data-regressions' 'python' @('tools/environment/test_arena_map.py')
    if (-not $NoBuild) {
        Invoke-Check 'build' 'cmake' @('--build', $taskBuild, '--config', $Configuration,
            '--target', 'PAC_Tests', 'PAC_ArenaLogicTests', 'PhlosionForge', 'PokemonAutochess', '--parallel')
        Invoke-Check 'editor-pair' 'powershell' @('-NoProfile', '-File', 'tools/housekeeping/build_editor_pair.ps1',
            '-Configuration', $Configuration, '-GameBuildDirectory', $taskBuild, '-OutputDirectory', (Join-Path $taskOutput 'editor-pair'))
    }
    $taskTests = Join-Path $taskBuild "$Configuration/PAC_Tests.exe"
    $taskForge = Join-Path $taskBuild "$Configuration/PhlosionForge.exe"
    foreach ($taskBinary in @($taskTests, $taskForge)) {
        if (-not (Test-Path -LiteralPath $taskBinary)) { throw "Missing build output: $taskBinary. Run without -NoBuild." }
    }
    Invoke-Check 'arena-bundle' $taskForge @('validate-arena-bundle', $taskRecipe.bundle_path)
    Invoke-Check 'fast-arena' 'ctest' @('--test-dir', $taskBuild, '-C', $Configuration, '-L', 'fast', '--output-on-failure')
    foreach ($taskTest in @('authored_ground_surface_contract', 'authored_arena_bundle_contract', 'route1_arena_pilot_contract',
            'route1_runtime_environment_contract', 'movement_collision_regressions', 'movement_invariants',
            'battle_invariants', 'end_to_end_headless', 'shared_projected_unit_renderer_bulbasaur_vine_visibility',
            'shared_projected_unit_world_scene_multiple_rigid_batches')) {
        Invoke-Check $taskTest $taskTests @('--filter', $taskTest)
    }
    if ($IncludeBlender) {
        if (-not $BlendFile) {
            if (-not $env:PHLOSION_ASSET_DEPOT) { throw 'Set PHLOSION_ASSET_DEPOT or supply -BlendFile for Blender qualification.' }
            $BlendFile = Join-Path (Split-Path $env:PHLOSION_ASSET_DEPOT -Parent) $taskRecipe.working_source_relative
        }
        if (-not (Test-Path -LiteralPath $BlendFile)) { throw 'Working Blender source is missing. Run tools/environment/restore_arena_source.ps1.' }
        foreach ($taskCase in @('tile_editability', 'ledge_geometry', 'roundtrip')) {
            $taskCaseOutput = Join-Path $taskOutput "blender-$taskCase"
            if ($taskCase -eq 'ledge_geometry') { $taskCaseOutput += '.json' }
            $taskRecipeArguments = @()
            if ($taskCase -eq 'roundtrip') { $taskRecipeArguments = @('--recipe', $Recipe) }
            Invoke-Check "blender-$taskCase" $Blender (@('--background', '--factory-startup', '--disable-autoexec',
                $BlendFile, '--python-exit-code', '1', '--python', "tools/environment/verify_arena_$taskCase.py", '--',
                '--output', $taskCaseOutput) + $taskRecipeArguments)
        }
    }
    if ($Capture) {
        Invoke-Check 'capture' 'powershell' @('-NoProfile', '-File', 'tools/environment/capture_arena_pilot.ps1',
            '-OutputDirectory', $taskOutput)
        Invoke-Check 'editor-preview' 'powershell' @('-NoProfile', '-File', 'tools/environment/preview_route1_pilot.ps1',
            '-Phase', 'planning', '-Capture', '-OutputDirectory', (Join-Path $taskOutput 'editor-preview'))
    }
    $taskPassed = $true
} catch {
    $taskFailure = $_.Exception.Message
    throw
} finally {
    @{ passed = $taskPassed; scene_id = $taskRecipe.scene_id; configuration = $Configuration;
       built = (-not $NoBuild); blender_requested = [bool]$IncludeBlender; capture_requested = [bool]$Capture;
       failure = $taskFailure; checks = @($taskResults.ToArray()) } | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $taskOutput 'verification.json')
    Pop-Location
    $env:PHLOSION_DATA_ROOT = $taskPreviousDataRoot
    $env:PHLOSION_ASSET_ROOT = $taskPreviousAssetRoot
}
Write-Host "South entrance checks passed. Report: $(Join-Path $taskOutput 'verification.json')"
