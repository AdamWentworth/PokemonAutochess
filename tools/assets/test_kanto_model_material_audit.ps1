param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Write-Utf8Json {
    param([string]$PathValue, $Value)
    $parent = Split-Path -Parent $PathValue
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    [IO.File]::WriteAllText(
        $PathValue,
        ($Value | ConvertTo-Json -Depth 14),
        (New-Object Text.UTF8Encoding($false)))
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-material-audit-test-' + [Guid]::NewGuid().ToString('N'))
$auditScript = Join-Path $PSScriptRoot 'audit_kanto_model_materials.ps1'

try {
    New-Item -ItemType Directory -Path (
        Join-Path $tempRoot 'assets\models') -Force | Out-Null

    $recipe = [ordered]@{
        schema = 'phlosion-gamefreak-import-recipe-v1'
        sourceGame = 'test-switch-game-v1'
        imports = @([ordered]@{
            speciesId = 25
            speciesName = 'Pikachu'
            genderLabel = 'unisex'
            outputs = @([ordered]@{
                appearance = 'regular'
                stem = '0025_Pikachu_Test'
            })
        })
    }
    Write-Utf8Json (
        Join-Path $tempRoot 'tools\assets\test_recipe.json') $recipe

    $catalog = [ordered]@{
        schema_version = 1
        kind = 'pokemon_autochess_asset_catalog'
        native_import_sets = @([ordered]@{
            recipe = 'tools/assets/test_recipe.json'
            selection = 'all_outputs'
        })
    }
    Write-Utf8Json (
        Join-Path $tempRoot 'config\assets\asset_catalog.json') $catalog

    $policy = [ordered]@{
        schema = 'pokemon-autochess-kanto-model-confidence-policy-v1'
        sourceProfiles = @([ordered]@{
            tag = 'Test'
            sourceGame = 'test-switch-game-v1'
            currentOverall = 50
            targetOverall = 90
            ratings = [ordered]@{ source_data_capture = 80 }
            knownUnknowns = @([ordered]@{
                id = 'test_unknown'
                severity = 'high'
                evidence = 'source_payload'
                summary = 'Fixture unknown.'
            })
        })
        captureTargets = @([ordered]@{
            id = 'test-capture'
            priority = 1
            sourceTag = 'Test'
            status = 'not_started'
            features = @('light_table')
            models = @([ordered]@{
                stem = '0025_Pikachu_Test'
                role = 'fixture canary'
                requiredLocal = $true
            })
        })
        requiredCaptureEvidence = @('shader_program')
        comparisonStates = @('neutral_front_light')
    }
    Write-Utf8Json (
        Join-Path $tempRoot 'tools\assets\confidence.json') $policy

    $manifest = [ordered]@{
        schema = 'phlosion-native-model-ir-v1'
        schema_version = 1
        source = [ordered]@{
            profile = 'test-switch-game-v1'
            native_formats = @('gfbmdl', 'bntx')
        }
        model = [ordered]@{
            vertex_count = 3
            index_count = 3
            submesh_count = 1
        }
        materials = @([ordered]@{
            name = 'Body'
            shader_family = 'PokeDefaultShader'
            is_transparent = $false
            shader_options = [ordered]@{ Layer1Enable = 'True' }
            float_parameters = [ordered]@{ NormalHeight = 1 }
            vec2_parameters = [ordered]@{}
            vec3_parameters = [ordered]@{}
            vec4_parameters = [ordered]@{}
            textures = @(
                [ordered]@{ role = 'Col0Tex'; slot = 0 },
                [ordered]@{ role = 'LightTblTex'; slot = 6 })
            runtime_translation = [ordered]@{
                base_color_texture = 'body.png'
            }
            native_material = [ordered]@{ Name = 'Body' }
        })
        skeleton = [ordered]@{ bones = @([ordered]@{ name = 'Root' }) }
        animations = @([ordered]@{
            name = 'idle'
            mesh_visibility = @()
            material_parameters = @()
        })
    }
    $manifestPath = Join-Path $tempRoot (
        'assets\models\0025_Pikachu_Test.phmodel')
    Write-Utf8Json $manifestPath $manifest

    $output = Join-Path $tempRoot 'output'
    & $auditScript `
        -GameRoot $tempRoot `
        -CatalogPath 'config/assets/asset_catalog.json' `
        -PolicyPath 'tools/assets/confidence.json' `
        -OutputDirectory $output `
        -RequireAllSelectedModels | Out-Null

    $report = Get-Content -LiteralPath (
        Join-Path $output 'kanto_model_material_inventory.json') -Raw |
        ConvertFrom-Json
    Assert-Condition ($report.summary.selected_species -eq 1) (
        'Audit did not count one selected species.')
    Assert-Condition ($report.summary.materials -eq 1 -and
        $report.summary.shader_permutations -eq 1) (
        'Audit did not count the fixture material and permutation.')
    Assert-Condition ($report.materials[0].lossy_or_unknown -contains
        'light_table_not_exactly_evaluated') (
        'Audit did not classify the retained light table as unresolved.')
    Assert-Condition ($report.materials[0].preserved_evidence -contains
        'native_material_block') (
        'Audit did not classify the native material block as preserved.')
    Assert-Condition ($report.capture_targets[0].locally_available) (
        'Audit did not resolve the local capture canary.')

    Remove-Item -LiteralPath $manifestPath -Force
    $missingRejected = $false
    try {
        & $auditScript `
            -GameRoot $tempRoot `
            -CatalogPath 'config/assets/asset_catalog.json' `
            -PolicyPath 'tools/assets/confidence.json' `
            -OutputDirectory (Join-Path $tempRoot 'missing-output') `
            -RequireAllSelectedModels 2>$null | Out-Null
    } catch {
        $missingRejected = $_.Exception.Message -like
            '*Selected native model manifests are missing*'
    }
    Assert-Condition $missingRejected (
        'Strict audit did not reject a missing selected manifest.')

    Write-Host '[KantoModelMaterialAuditTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
