param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Write-Utf8([string]$PathValue, [string]$Value) {
    [IO.File]::WriteAllText(
        $PathValue, $Value, (New-Object Text.UTF8Encoding($false)))
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$planner = Join-Path $PSScriptRoot 'plan_za_kanto_option_differentials.py'
$analyzer = Join-Path $PSScriptRoot 'analyze_za_kanto_option_differentials.py'
$extractor = Join-Path $PSScriptRoot (
    'extract_sv_kanto_differential_programs.ps1')
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-za-kanto-shaders-' + [Guid]::NewGuid().ToString('N'))
$studyRoot = Join-Path $temporaryRoot 'study'
$inventoryPath = Join-Path $temporaryRoot 'inventory.json'
$metadataPath = Join-Path $studyRoot 'fixture.trsha.json'
$planPath = Join-Path $temporaryRoot 'option-plan.json'
$selectedRoot = Join-Path $temporaryRoot 'selected-programs'
$comparisonRoot = Join-Path $temporaryRoot 'option-differential-programs'
$reportPath = Join-Path $temporaryRoot 'option-evidence.json'
$promotedInventoryPath = Join-Path $gameRoot (
    'docs\kanto\evidence\za_kanto_shader_inventory.json')
$promotedCensusPath = Join-Path $gameRoot (
    'docs\kanto\evidence\za_kanto_material_census.json')
$promotedAbiPath = Join-Path $gameRoot (
    'docs\kanto\evidence\za_kanto_selected_program_abi.json')
$promotedTextureDifferentialsPath = Join-Path $gameRoot (
    'docs\kanto\evidence\za_kanto_program_differentials.json')
$promotedOptionDifferentialsPath = Join-Path $gameRoot (
    'docs\kanto\evidence\za_kanto_option_differentials.json')

Assert-Condition (Test-Path -LiteralPath $planner -PathType Leaf) (
    'Z-A option-differential planner is missing.')
Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A option-differential analyzer is missing.')
$plannerSource = Get-Content -LiteralPath $planner -Raw
foreach ($token in @(
        'change exactly one effective',
        'no_exact_archived_single_option_counterpart',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($plannerSource.Contains($token)) (
        "Z-A option planner lost contract token: $token")
}
$analyzerSource = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'compiled_single_option_program_structural_differential',
        'changed_buffer_uses', 'stable compiled identities',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($analyzerSource.Contains($token)) (
        "Z-A option analyzer lost contract token: $token")
}
$extractorSource = Get-Content -LiteralPath $extractor -Raw
foreach ($token in @(
        "'TextureRole', 'ShaderOption', 'OptionGraph'",
        'pokemon-autochess-private-za-option-differential-programs-v1',
        'option-differential-programs')) {
    Assert-Condition ($extractorSource.Contains($token)) (
        "Trinity differential extractor lost Z-A option support: $token")
}

try {
    New-Item -ItemType Directory -Path $studyRoot -Force | Out-Null
    $slotValues = @(
        [ordered]@{ string_value = 'False'; u_int_value = 0 },
        [ordered]@{ string_value = 'True'; u_int_value = 1 })
    $metadata = [ordered]@{
        shader_param = @(
            [ordered]@{
                slot_name = 'EnableFeatureA'; slot_values = $slotValues
                bool1 = 0; slot_index = 0; offset = 1
            },
            [ordered]@{
                slot_name = 'EnableFeatureB'; slot_values = $slotValues
                bool1 = 0; slot_index = 1; offset = 2
            })
        global_param = @(
            [ordered]@{
                slot_name = 'EnableGlobalFeature'; slot_values = $slotValues
                bool1 = 0; slot_index = 0; offset = 1
            })
        param_buffer = @(
            0, 0,
            1, 0,
            2, 0,
            3, 0,
            0, 1,
            1, 1,
            2, 1,
            3, 1)
    }
    Write-Utf8 $metadataPath ($metadata | ConvertTo-Json -Depth 7)
    $resolvedOptions = @(
        [ordered]@{ name = 'EnableFeatureA'; choice = 'False' },
        [ordered]@{ name = 'EnableFeatureB'; choice = 'False' })
    $resolvedGlobalOptions = @(
        [ordered]@{ name = 'EnableGlobalFeature'; choice = 'False' })
    $inventory = [ordered]@{
        schema = 'pokemon-autochess-za-kanto-shader-inventory-v1'
        scope = [ordered]@{ source_profile = 'pokemon-legends-za-v2.0.0' }
        summary = [ordered]@{
            unresolved_permutations = 0
            selected_materials = 1
            unique_selected_programs = 1
        }
        families = @([ordered]@{
            shader_family = 'Fixture'
            decoded_metadata = [ordered]@{ identity = 'fixture.trsha.json' }
            resolved_permutations = @([ordered]@{
                variation_index = 0
                material_count = 1
                permutation_sha256 = ('a' * 64)
                shader_options = $resolvedOptions
                global_options = $resolvedGlobalOptions
            })
        })
    }
    Write-Utf8 $inventoryPath ($inventory | ConvertTo-Json -Depth 9)

    & python $planner `
        --inventory $inventoryPath `
        --shader-study $studyRoot `
        --output $planPath
    Assert-Condition ($LASTEXITCODE -eq 0) (
        "Z-A option planner failed with exit code $LASTEXITCODE")
    $plan = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
    Assert-Condition (
        [int]$plan.summary.differential_count -eq 3 -and
        [int]$plan.summary.unique_comparison_programs -eq 3 -and
        [int]$plan.summary.covered_options -eq 3 -and
        [int]$plan.summary.unresolved_option_choices -eq 0) (
        'Synthetic Z-A options did not resolve to exact one-choice counterparts.')

    $selectedFragment = @'
#version 450 core
layout (binding = 0) uniform sampler2D fp_t_tcb_8;
layout (binding = 8, std140) uniform _fp_c7 { vec4 data[8]; } fp_c7;
layout (location = 0) in vec4 in_attr0;
layout (location = 0) out vec4 out_attr0;
void main() { out_attr0 = texture(fp_t_tcb_8, in_attr0.xy) + fp_c7.data[0]; }
'@
    $selectedVertex = @'
#version 450 core
layout (location = 0) in vec4 in_attr0;
layout (location = 0) out vec4 out_attr0;
void main() { out_attr0 = in_attr0; }
'@
    $comparisonSources = @{
        1 = [ordered]@{
            fragment = @'
#version 450 core
layout (binding = 0) uniform sampler2D fp_t_tcb_8;
layout (binding = 1) uniform sampler2D fp_t_tcb_A;
layout (binding = 8, std140) uniform _fp_c7 { vec4 data[8]; } fp_c7;
layout (location = 0) in vec4 in_attr0;
layout (location = 0) out vec4 out_attr0;
void main() { out_attr0 = texture(fp_t_tcb_8, in_attr0.xy) + texture(fp_t_tcb_A, in_attr0.xy) + fp_c7.data[0]; }
'@
            vertex = $selectedVertex
        }
        2 = [ordered]@{
            fragment = @'
#version 450 core
layout (binding = 0) uniform sampler2D fp_t_tcb_8;
layout (binding = 8, std140) uniform _fp_c7 { vec4 data[8]; } fp_c7;
layout (location = 0) in vec4 in_attr0;
layout (location = 0) out vec4 out_attr0;
void main() { out_attr0 = texture(fp_t_tcb_8, in_attr0.xy) + fp_c7.data[1]; }
'@
            vertex = $selectedVertex
        }
        4 = [ordered]@{
            fragment = $selectedFragment
            vertex = @'
#version 450 core
layout (binding = 0) uniform sampler2D vp_t_tcb_24;
layout (location = 0) in vec4 in_attr0;
layout (location = 0) out vec4 out_attr0;
void main() { out_attr0 = in_attr0 + texture(vp_t_tcb_24, in_attr0.xy); }
'@
        }
    }

    $selectedDirectory = Join-Path $selectedRoot 'fixture\v0000'
    New-Item -ItemType Directory -Path $selectedDirectory -Force | Out-Null
    $selectedFragmentPath = Join-Path $selectedDirectory (
        'v0000.fsh.maxwell.glsl')
    $selectedVertexPath = Join-Path $selectedDirectory (
        'v0000.vsh.maxwell.glsl')
    Write-Utf8 $selectedFragmentPath $selectedFragment
    Write-Utf8 $selectedVertexPath $selectedVertex
    $selectedManifest = [ordered]@{
        schema = 'pokemon-autochess-private-za-selected-programs-v1'
        source_profile = 'pokemon-legends-za-v2.0.0'
        runtime_execution = $false
        emulator_used = $false
        program_count = 1
        programs = @([ordered]@{
            shader_family = 'Fixture'; variation_index = 0
            directory = 'fixture/v0000'
            fragment_glsl_sha256 = (
                Get-FileHash $selectedFragmentPath -Algorithm SHA256).Hash.ToLowerInvariant()
            vertex_glsl_sha256 = (
                Get-FileHash $selectedVertexPath -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    Write-Utf8 (Join-Path $selectedRoot 'selected_programs_manifest.json') (
        $selectedManifest | ConvertTo-Json -Depth 7)

    $comparisonRecords = [Collections.Generic.List[object]]::new()
    foreach ($variation in @(1, 2, 4)) {
        $stem = 'v{0:D4}' -f $variation
        $directory = Join-Path $comparisonRoot "fixture\$stem"
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $fragmentPath = Join-Path $directory "$stem.fsh.maxwell.glsl"
        $vertexPath = Join-Path $directory "$stem.vsh.maxwell.glsl"
        Write-Utf8 $fragmentPath ([string]$comparisonSources[$variation].fragment)
        Write-Utf8 $vertexPath ([string]$comparisonSources[$variation].vertex)
        $comparisonRecords.Add([ordered]@{
            shader_family = 'Fixture'; variation_index = $variation
            directory = "fixture/$stem"
            fragment_glsl_sha256 = (
                Get-FileHash $fragmentPath -Algorithm SHA256).Hash.ToLowerInvariant()
            vertex_glsl_sha256 = (
                Get-FileHash $vertexPath -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    $comparisonManifest = [ordered]@{
        schema = (
            'pokemon-autochess-private-za-option-differential-programs-v1')
        source_profile = 'pokemon-legends-za-v2.0.0'
        runtime_execution = $false
        emulator_used = $false
        plan_sha256 = (
            Get-FileHash $planPath -Algorithm SHA256).Hash.ToLowerInvariant()
        program_count = 3
        programs = @($comparisonRecords)
    }
    Write-Utf8 (
        Join-Path $comparisonRoot 'differential_programs_manifest.json') (
        $comparisonManifest | ConvertTo-Json -Depth 7)

    & python $analyzer `
        --plan $planPath `
        --selected-program-root $selectedRoot `
        --comparison-program-root $comparisonRoot `
        --output $reportPath
    Assert-Condition ($LASTEXITCODE -eq 0) (
        "Z-A option analyzer failed with exit code $LASTEXITCODE")
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Assert-Condition (
        [int]$report.summary.differential_count -eq 3 -and
        [int]$report.summary.covered_options -eq 3 -and
        [int]$report.summary.fragment_changed_differentials -eq 2 -and
        [int]$report.summary.vertex_changed_differentials -eq 1 -and
        [int]$report.summary.resource_changing_differentials -eq 2) (
        'Synthetic Z-A option structural differentials were misclassified.')
    $featureA = @($report.differentials | Where-Object changed_option -eq (
        'EnableFeatureA'))[0]
    $featureB = @($report.differentials | Where-Object changed_option -eq (
        'EnableFeatureB'))[0]
    $globalFeature = @($report.differentials | Where-Object changed_option -eq (
        'EnableGlobalFeature'))[0]
    Assert-Condition (
        [string]$featureA.fragment.removed_samplers[0].name -eq 'fp_t_tcb_A' -and
        [string]$featureB.fragment.classification -eq (
            'data_flow_change_with_stable_resource_abi') -and
        [string]$globalFeature.vertex.removed_samplers[0].name -eq 'vp_t_tcb_24') (
        'Z-A option analyzer lost fragment data-flow or vertex-resource evidence.')

    $promotedInventory = Get-Content -LiteralPath $promotedInventoryPath -Raw |
        ConvertFrom-Json
    $promotedCensus = Get-Content -LiteralPath $promotedCensusPath -Raw |
        ConvertFrom-Json
    $promotedAbi = Get-Content -LiteralPath $promotedAbiPath -Raw |
        ConvertFrom-Json
    $promotedTexture = Get-Content `
        -LiteralPath $promotedTextureDifferentialsPath -Raw | ConvertFrom-Json
    $promotedOptions = Get-Content `
        -LiteralPath $promotedOptionDifferentialsPath -Raw | ConvertFrom-Json
    Assert-Condition (
        [int]$promotedInventory.summary.selected_materials -eq 234 -and
        [int]$promotedInventory.summary.material_permutations -eq 11 -and
        [int]$promotedInventory.summary.unique_selected_programs -eq 6 -and
        [int]$promotedInventory.summary.unresolved_permutations -eq 0 -and
        [int]$promotedCensus.summary.unresolved_materials -eq 0) (
        'Promoted Z-A source inventory or census is incomplete.')
    Assert-Condition (
        [int]$promotedAbi.summary.program_count -eq 6 -and
        [int]$promotedAbi.summary.unique_vertex_sampler_symbols -eq 1 -and
        [int]$promotedTexture.summary.proven_texture_mappings -eq 1 -and
        [string]$promotedTexture.proven_texture_mappings[0].shader_stage -eq (
            'vertex') -and
        [string]$promotedTexture.proven_texture_mappings[0].texture_role -eq (
            'DisplacementMap')) (
        'Promoted Z-A selected ABI or displacement proof is incomplete.')
    Assert-Condition (
        [int]$promotedOptions.summary.differential_count -eq 42 -and
        [int]$promotedOptions.summary.covered_options -eq 22 -and
        [int]$promotedOptions.summary.fragment_changed_differentials -eq 32 -and
        [int]$promotedOptions.summary.vertex_changed_differentials -eq 10 -and
        [int]$promotedOptions.summary.resource_changing_differentials -eq 25) (
        'Promoted Z-A option-differential evidence is stale.')
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force `
        -ErrorAction SilentlyContinue
}

Write-Host '[ZaKantoShaderResearchWorkflowTest] PASS'
