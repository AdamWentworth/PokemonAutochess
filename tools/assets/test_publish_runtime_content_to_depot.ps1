param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Write-FixtureFile([string]$PathValue, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $PathValue) -Force | Out-Null
    [IO.File]::WriteAllText($PathValue, $Text, (New-Object Text.UTF8Encoding($false)))
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-runtime-depot-test-' + [Guid]::NewGuid().ToString('N'))
$gameRoot = Join-Path $tempRoot 'game'
$depotRoot = Join-Path $tempRoot 'depot'
$runtimeDepot = Join-Path $depotRoot 'pokemon-autochess\runtime\content\phlosion'
$publisher = Join-Path $PSScriptRoot 'publish_runtime_content_to_depot.ps1'

try {
    $modelObject = Join-Path $gameRoot 'content\phlosion\objects\fixture-model\fixture.phlo'
    $modelDependency = Join-Path $gameRoot 'content\phlosion\objects\fixture-model\model.phmesh'
    $environmentObject = Join-Path $gameRoot 'content\phlosion\objects\environment\route1\tree\tree.phlo'
    $scene = Join-Path $gameRoot 'content\phlosion\scenes\route1.phscene'
    $sharedDependency = Join-Path $gameRoot 'content\phlosion\dependencies\ktx2\0123456789abcdef-0123456789abcdef.ktx2'
    $manifestPath = Join-Path $gameRoot 'content\phlosion\cook_manifest.json'
    Write-FixtureFile $modelObject 'model-current'
    Write-FixtureFile $modelDependency 'mesh-current'
    Write-FixtureFile $environmentObject 'environment-current'
    Write-FixtureFile $scene 'scene-current'
    Write-FixtureFile $sharedDependency 'shared-current'
    $manifest = [ordered]@{
        schema_version = 2
        kind = 'phlosion_cook_manifest'
        pokemon = @([ordered]@{
            source = 'assets/models/fixture.phmodel'
            object = 'content/phlosion/objects/fixture-model/fixture.phlo'
            texture_dependencies = @('dependencies/ktx2/0123456789abcdef-0123456789abcdef.ktx2')
        })
        staged_imports = @()
        runtime_auxiliary_objects = @()
        shared_dependencies = @([ordered]@{
            asset_id = 'dependencies/ktx2/0123456789abcdef-0123456789abcdef.ktx2'
            path = 'content/phlosion/dependencies/ktx2/0123456789abcdef-0123456789abcdef.ktx2'
            fnv1a64 = '0123456789abcdef'
            bytes = 14
        })
        environment = [ordered]@{ scene = 'content/phlosion/scenes/route1.phscene' }
    }
    Write-FixtureFile $manifestPath ($manifest | ConvertTo-Json -Depth 6)

    $depotModel = Join-Path $runtimeDepot 'objects\fixture-model\fixture.phlo'
    $depotEnvironment = Join-Path $runtimeDepot 'objects\environment\route1\tree\tree.phlo'
    $unrelated = Join-Path $runtimeDepot 'objects\unrelated\keep.bin'
    $staleModelDependency = Join-Path $runtimeDepot 'objects\fixture-model\textures\stale.ktx2'
    $staleSharedDependency = Join-Path $runtimeDepot 'dependencies\ktx2\ffffffffffffffff-ffffffffffffffff.ktx2'
    Write-FixtureFile $depotModel 'model-current'
    Write-FixtureFile $depotEnvironment 'environment-old'
    Write-FixtureFile $unrelated 'keep-me'
    Write-FixtureFile $staleModelDependency 'stale-model'
    Write-FixtureFile $staleSharedDependency 'stale-shared'

    $plan = & $publisher -GameRoot $gameRoot -DepotRoot $depotRoot
    Assert-Condition ($plan.changed_file_count -gt 0) 'Publisher plan did not find differing fixture files.'
    Assert-Condition ((Get-Content $depotEnvironment -Raw) -eq 'environment-old') 'Publisher dry-run changed depot content.'

    $result = & $publisher -GameRoot $gameRoot -DepotRoot $depotRoot -Apply
    Assert-Condition ($result.published_file_count -eq $plan.changed_file_count) 'Publisher applied a different file set than it planned.'
    Assert-Condition ((Get-Content $depotEnvironment -Raw) -eq 'environment-current') 'Publisher did not update differing environment content.'
    Assert-Condition ((Get-Content (Join-Path $runtimeDepot 'objects\fixture-model\model.phmesh') -Raw) -eq 'mesh-current') 'Publisher omitted a manifest-owned object dependency.'
    Assert-Condition ((Get-Content (Join-Path $runtimeDepot 'dependencies\ktx2\0123456789abcdef-0123456789abcdef.ktx2') -Raw) -eq 'shared-current') 'Publisher omitted a manifest-owned shared dependency.'
    Assert-Condition (-not (Test-Path -LiteralPath $staleModelDependency)) 'Publisher retained a stale file inside a manifest-owned object.'
    Assert-Condition (-not (Test-Path -LiteralPath $staleSharedDependency)) 'Publisher retained an orphan shared dependency.'
    Assert-Condition ((Get-Content $unrelated -Raw) -eq 'keep-me') 'Publisher changed unrelated depot content.'

    $idempotent = & $publisher -GameRoot $gameRoot -DepotRoot $depotRoot
    Assert-Condition ($idempotent.changed_file_count -eq 0 -and $idempotent.changed_bytes -eq 0 -and $idempotent.stale_file_count -eq 0) 'Publisher is not idempotent.'
    Write-Host '[RuntimeContentDepotPublisherTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith($resolvedSystemTemp, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
