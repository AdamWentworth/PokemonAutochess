[CmdletBinding()]
param(
    [string]$OutputDirectory = 'debug/native-character-materials',
    [string]$Profile = 'config/render/world_materials.json',
    [string]$Configuration = 'Release',
    [string[]]$Backends = @('opengl', 'vulkan', 'd3d12'),
    [switch]$SkipCapture
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskOutput = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot, $OutputDirectory))
if (-not $SkipCapture) {
    python (Join-Path $PSScriptRoot 'prepare_character_material_cases.py') --output $taskOutput --profile $Profile
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare native character fixtures.' }
    foreach ($taskName in @('assets', 'content', 'scripts')) {
        $taskLink = Join-Path $taskOutput "game-data/$taskName"
        if (-not (Test-Path -LiteralPath $taskLink)) {
            New-Item -ItemType Junction -Path $taskLink -Target (Join-Path $taskRoot $taskName) | Out-Null
        }
    }
}
$taskPriorRoot = $env:PHLOSION_DATA_ROOT
$taskPriorInking = $env:PAC_VIDEO_CHARACTER_INKING
try {
    $env:PHLOSION_DATA_ROOT = Join-Path $taskOutput 'game-data'
    $env:PAC_VIDEO_CHARACTER_INKING = '1'
    & (Join-Path $PSScriptRoot 'render_parity_matrix.ps1') -Config $Configuration `
        -ManifestPath (Join-Path $taskOutput 'native-matrix.json') `
        -OutputDir (Join-Path $taskOutput 'captures') -Backends $Backends -SkipCapture:$SkipCapture
} finally {
    $env:PHLOSION_DATA_ROOT = $taskPriorRoot
    $env:PAC_VIDEO_CHARACTER_INKING = $taskPriorInking
}
