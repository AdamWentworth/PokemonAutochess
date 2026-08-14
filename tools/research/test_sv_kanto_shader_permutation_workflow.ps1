param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_sv_kanto_shader_permutations.py'
$registryPath = Join-Path $PSScriptRoot 'sv_kanto_shader_families.json'
$extractor = Join-Path $PSScriptRoot 'extract_sv_kanto_shader_sources.ps1'
$programExtractor = Join-Path $PSScriptRoot (
    'extract_sv_kanto_selected_programs.ps1')
$abiAnalyzer = Join-Path $PSScriptRoot (
    'analyze_sv_kanto_selected_program_abi.py')
$differentialPlanner = Join-Path $PSScriptRoot (
    'plan_sv_kanto_program_differentials.py')
$differentialExtractor = Join-Path $PSScriptRoot (
    'extract_sv_kanto_differential_programs.ps1')
$differentialAnalyzer = Join-Path $PSScriptRoot (
    'analyze_sv_kanto_program_differentials.py')
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-sv-kanto-shaders-' + [Guid]::NewGuid().ToString('N'))
$studyRoot = Join-Path $temporaryRoot 'study'
$reportPath = Join-Path $temporaryRoot 'report.json'
$evidencePath = Join-Path $temporaryRoot 'evidence.json'
$syntheticPrograms = Join-Path $temporaryRoot 'programs'
$syntheticAbiReport = Join-Path $temporaryRoot 'program-abi.json'
$syntheticRegistryPath = Join-Path $temporaryRoot 'registry.json'
$promotedPath = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_shader_inventory.json')
$promotedAbiPath = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_selected_program_abi.json')
$promotedDifferentialsPath = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_program_differentials.json')
$promotedCensusPath = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_material_census.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'SV Kanto shader analyzer is missing.')
Assert-Condition (Test-Path -LiteralPath $extractor -PathType Leaf) (
    'SV Kanto headless shader source extractor is missing.')
Assert-Condition (Test-Path -LiteralPath $programExtractor -PathType Leaf) (
    'SV Kanto selected-program extractor is missing.')
Assert-Condition (Test-Path -LiteralPath $abiAnalyzer -PathType Leaf) (
    'SV Kanto selected-program ABI analyzer is missing.')
Assert-Condition (Test-Path -LiteralPath $differentialPlanner -PathType Leaf) (
    'SV Kanto differential planner is missing.')
Assert-Condition (Test-Path -LiteralPath $differentialExtractor -PathType Leaf) (
    'SV Kanto differential program extractor is missing.')
Assert-Condition (Test-Path -LiteralPath $differentialAnalyzer -PathType Leaf) (
    'SV Kanto differential analyzer is missing.')
$extractorSource = Get-Content -LiteralPath $extractor -Raw
foreach ($token in @(
        '--romfs', '--file-hash', '--trsha',
        '--require-complete-source', '--require-exact-resolution',
        'runtime_execution = $false', 'emulator_used = $false')) {
    Assert-Condition ($extractorSource.Contains($token)) (
        "SV Kanto source extractor lost contract token: $token")
}
$differentialAnalyzerSource = Get-Content -LiteralPath (
    $differentialAnalyzer) -Raw
foreach ($token in @(
        'compiled_single_option_program_differential',
        'Exactly one sampled fragment sampler appears',
        'no semantic binding', 'runtime_execution": False',
        'emulator_used": False')) {
    Assert-Condition ($differentialAnalyzerSource.Contains($token)) (
        "SV differential analyzer lost contract token: $token")
}
$differentialExtractorSource = Get-Content -LiteralPath (
    $differentialExtractor) -Raw
foreach ($token in @(
        '--bnsh', '--variation', 'fsh.maxwell.glsl', 'vsh.maxwell.glsl',
        'unique_comparison_programs', 'runtime_execution = $false',
        'emulator_used = $false')) {
    Assert-Condition ($differentialExtractorSource.Contains($token)) (
        "SV differential extractor lost contract token: $token")
}
$differentialSource = Get-Content -LiteralPath $differentialPlanner -Raw
foreach ($token in @(
        'no_exact_archived_single_option_counterpart',
        'ambiguous_archived_single_option_counterpart',
        'every other material and metadata-default option remains identical',
        'compiled resource by exclusion',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($differentialSource.Contains($token)) (
        "SV differential planner lost contract token: $token")
}
$abiSource = Get-Content -LiteralPath $abiAnalyzer -Raw
foreach ($token in @(
        'compiled_program_static_abi', 'runtime_execution": False',
        'emulator_used": False', 'static_texture_call_count',
        'constant_indices', 'source semantic names')) {
    Assert-Condition ($abiSource.Contains($token)) (
        "SV selected-program ABI analyzer lost contract token: $token")
}
$programExtractorSource = Get-Content -LiteralPath $programExtractor -Raw
foreach ($token in @(
        '--bnsh', '--variation', 'fsh.maxwell.glsl', 'vsh.maxwell.glsl',
        'runtime_execution = $false', 'emulator_used = $false',
        'unique_selected_programs')) {
    Assert-Condition ($programExtractorSource.Contains($token)) (
        "SV selected-program extractor lost contract token: $token")
}
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'runtime_execution": False', 'emulator_used": False',
        'shader_metadata_default', 'material_document', 'param_buffer',
        'to exactly one param_buffer variation',
        'unregistered_shader_family', 'build_material_census')) {
    Assert-Condition ($source.Contains($token)) (
        "SV Kanto shader analyzer lost contract token: $token")
}

$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
Assert-Condition ([string]$registry.schema -eq
    'pokemon-autochess-sv-shader-source-registry-v1') (
    'SV shader registry has the wrong schema.')
$expectedFamilies = @(
    'Eye', 'EyeClearCoat', 'NonDirectional', 'SSS', 'SSSEffect',
    'Standard', 'Transparent', 'Unlit')
$actualFamilies = @($registry.families | ForEach-Object {
    [string]$_.shader_family
} | Sort-Object)
Assert-Condition ($actualFamilies.Count -eq $expectedFamilies.Count -and
    (@(Compare-Object $expectedFamilies $actualFamilies).Count -eq 0)) (
    'SV shader registry does not cover the selected Kanto family set.')
$allHashes = @(
    foreach ($family in @($registry.families)) {
        [string]$family.archive.romfs_hash
        [string]$family.metadata.romfs_hash
    })
Assert-Condition ($allHashes.Count -eq 16 -and
    @($allHashes | Select-Object -Unique).Count -eq 16) (
    'SV shader registry source hashes must be complete and unique.')

try {
    # The current selected corpus newly exposes Scarlet/Violet FresnelEffect
    # on four Tentacool-family materials. Its private source identity is not
    # registered yet, so add a synthetic-only family to exercise complete
    # resolution without inventing a promoted RomFS hash.
    New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null
    $registry.families += [pscustomobject][ordered]@{
        shader_family = 'FresnelEffect'
        file_stem = 'fresnel_effect'
        archive = [pscustomobject][ordered]@{
            file = 'fresnel_effect.bnsh'
            romfs_path = 'synthetic/fresnel_effect.bnsh'
            romfs_hash = '0x0000000000000001'
        }
        metadata = [pscustomobject][ordered]@{
            file = 'fresnel_effect.trsha'
            decoded_file = 'fresnel_effect.trsha.json'
            romfs_path = 'synthetic/fresnel_effect.trsha'
            romfs_hash = '0x0000000000000002'
        }
    }
    [IO.File]::WriteAllText(
        $syntheticRegistryPath,
        ($registry | ConvertTo-Json -Depth 8),
        (New-Object Text.UTF8Encoding($false)))
    New-Item -ItemType Directory -Path $studyRoot -Force | Out-Null
    foreach ($family in @($registry.families)) {
        # Synthetic source identities exercise corpus-wide exact resolution
        # without requiring private game payloads in CI. Empty option tables
        # select the unique (0, 0) fixture variation for every permutation.
        $syntheticArchive = [byte[]]::new(0x100)
        [Text.Encoding]::ASCII.GetBytes('grsc').CopyTo($syntheticArchive, 0x20)
        [BitConverter]::GetBytes([uint32]1).CopyTo($syntheticArchive, 0x3c)
        [IO.File]::WriteAllBytes(
            (Join-Path $studyRoot ([string]$family.archive.file)),
            $syntheticArchive)
        [IO.File]::WriteAllBytes(
            (Join-Path $studyRoot ([string]$family.metadata.file)),
            [byte[]]@(0x54))
        $metadata = [ordered]@{
            name = [string]$family.shader_family
            file_name = [string]$family.archive.file
            shader_param = @()
            global_param = @()
            param_buffer = @(0, 0)
            has_shader_param = $true
            has_global_param = $true
        }
        if ([string]$family.shader_family -eq 'Standard') {
            $choice = @([ordered]@{ string_value = 'False'; u_int_value = 0 })
            $metadata.shader_param = @(
                [ordered]@{
                    slot_name = 'SyntheticHighSlot'
                    slot_values = $choice
                    bool1 = 0
                    bool2 = 0
                    bool3 = 0
                    slot_index = 31
                    offset = 0x80000000
                },
                [ordered]@{
                    slot_name = 'SyntheticWrappedSlot'
                    slot_values = $choice
                    bool1 = 0
                    bool2 = 0
                    bool3 = 0
                    slot_index = 0
                    offset = 1
                })
            $metadata.param_buffer = @(0, 0, 0)
        }
        [IO.File]::WriteAllText(
            (Join-Path $studyRoot ([string]$family.metadata.decoded_file)),
            ($metadata | ConvertTo-Json -Depth 5),
            (New-Object Text.UTF8Encoding($false)))
    }

    & python $analyzer `
        --game-root $gameRoot `
        --registry $syntheticRegistryPath `
        --shader-study $studyRoot `
        --output $reportPath `
        --evidence-output $evidencePath `
        --require-complete-source `
        --require-exact-resolution
    Assert-Condition ($LASTEXITCODE -eq 0) (
        "SV Kanto shader analyzer failed with exit code $LASTEXITCODE")

    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Assert-Condition ([string]$report.schema -eq
        'pokemon-autochess-sv-kanto-shader-inventory-v1') (
        'SV Kanto shader report has the wrong schema.')
    Assert-Condition (-not [bool]$report.method.runtime_execution -and
        -not [bool]$report.method.emulator_used) (
        'SV Kanto shader inventory must remain emulator-free.')
    $summary = $report.summary
    Assert-Condition ([int]$summary.selected_species -eq 99 -and
        [int]$summary.selected_models -eq 226 -and
        [int]$summary.selected_materials -eq 946) (
        'SV selected-corpus totals changed; review the canonical selection.')
    Assert-Condition ([int]$summary.material_permutations -eq 44 -and
        [int]$summary.shader_families -eq 9) (
        'SV material permutation totals changed; review the inventory.')
    Assert-Condition ([int]$summary.exactly_resolved_permutations -eq 44 -and
        [int]$summary.exactly_resolved_materials -eq 946 -and
        [int]$summary.unresolved_permutations -eq 0) (
        'Synthetic complete source did not resolve the entire SV corpus.')
    Assert-Condition (@($report.extraction_queue).Count -eq 0) (
        'Complete synthetic source unexpectedly produced an extraction queue.')
    $syntheticStandard = @($report.families | Where-Object shader_family -eq 'Standard')
    Assert-Condition ($syntheticStandard.Count -eq 1 -and
        [int]$syntheticStandard[0].archive_variation_count -eq 1 -and
        [int]$syntheticStandard[0].parameter_words_per_variation -eq 3) (
        'SV resolver lost support for multi-word Standard shader options.')
    $syntheticEvidence = Get-Content -LiteralPath $evidencePath -Raw |
        ConvertFrom-Json
    Assert-Condition ([string]$syntheticEvidence.schema -eq
        'pokemon-autochess-sv-kanto-shader-evidence-v1') (
        'SV analyzer did not produce compact promoted evidence.')

    $expectedCounts = @{
        Eye = @(14, 3)
        EyeClearCoat = @(486, 18)
        FresnelEffect = @(4, 1)
        NonDirectional = @(4, 2)
        SSS = @(392, 6)
        SSSEffect = @(8, 1)
        Standard = @(18, 6)
        Transparent = @(4, 2)
        Unlit = @(16, 5)
    }
    foreach ($family in @($report.families)) {
        $expected = $expectedCounts[[string]$family.shader_family]
        Assert-Condition ($null -ne $expected -and
            [int]$family.material_count -eq $expected[0] -and
            [int]$family.permutation_count -eq $expected[1]) (
            "Unexpected corpus count for $($family.shader_family).")
    }

    $syntheticProgramDirectory = Join-Path $syntheticPrograms 'fixture\v0000'
    New-Item -ItemType Directory -Path $syntheticProgramDirectory -Force |
        Out-Null
    $fragmentPath = Join-Path $syntheticProgramDirectory (
        'v0000.fsh.maxwell.glsl')
    $vertexPath = Join-Path $syntheticProgramDirectory (
        'v0000.vsh.maxwell.glsl')
    [IO.File]::WriteAllText($fragmentPath, @'
#version 450 core
layout (binding = 2) uniform sampler2D fp_t_tcb_8;
layout (binding = 8, std140) uniform _fp_c7
{
    vec4 data[16];
} fp_c7;
layout (location = 0) in vec4 in_attr0;
layout (location = 0) out vec4 out_attr0;
void main() {
    out_attr0 = texture(fp_t_tcb_8, in_attr0.xy) + fp_c7.data[4];
}
'@, (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($vertexPath, @'
#version 450 core
layout (location = 0) in vec4 in_attr0;
layout (location = 0) out vec4 out_attr0;
void main() { out_attr0 = in_attr0; }
'@, (New-Object Text.UTF8Encoding($false)))
    $programManifest = [ordered]@{
        schema = 'pokemon-autochess-private-sv-selected-programs-v1'
        source_profile = 'pokemon-scarlet-v3.0.1'
        runtime_execution = $false
        emulator_used = $false
        evidence_sha256 = ('0' * 64)
        program_count = 1
        programs = @([ordered]@{
            shader_family = 'Fixture'
            variation_index = 0
            material_count = 1
            permutation_count = 1
            fragment_glsl_sha256 = (
                Get-FileHash -LiteralPath $fragmentPath -Algorithm SHA256).Hash.ToLowerInvariant()
            vertex_glsl_sha256 = (
                Get-FileHash -LiteralPath $vertexPath -Algorithm SHA256).Hash.ToLowerInvariant()
            directory = 'fixture/v0000'
        })
    }
    [IO.File]::WriteAllText(
        (Join-Path $syntheticPrograms 'selected_programs_manifest.json'),
        ($programManifest | ConvertTo-Json -Depth 7),
        (New-Object Text.UTF8Encoding($false)))
    & python $abiAnalyzer `
        --program-root $syntheticPrograms `
        --output $syntheticAbiReport
    Assert-Condition ($LASTEXITCODE -eq 0) (
        "SV selected-program ABI analyzer failed with exit code $LASTEXITCODE")
    $abiReport = Get-Content -LiteralPath $syntheticAbiReport -Raw |
        ConvertFrom-Json
    $fixtureProgram = @($abiReport.programs)[0]
    Assert-Condition ([int]$abiReport.summary.program_count -eq 1 -and
        [int]$fixtureProgram.fragment.static_sampler_count -eq 1 -and
        [int]$fixtureProgram.fragment.sampled_sampler_count -eq 1 -and
        [int]$fixtureProgram.fragment.referenced_buffer_count -eq 1 -and
        [string]$fixtureProgram.fragment.buffers[0].name -eq 'fp_c7' -and
        [int]$fixtureProgram.fragment.buffers[0].constant_indices[0] -eq 4) (
        'SV ABI analyzer lost sampler or constant-buffer use-site parsing.')

    Assert-Condition (Test-Path -LiteralPath $promotedPath -PathType Leaf) (
        'Promoted SV Kanto shader evidence is missing.')
    $promoted = Get-Content -LiteralPath $promotedPath -Raw | ConvertFrom-Json
    Assert-Condition ([string]$promoted.schema -eq
        'pokemon-autochess-sv-kanto-shader-evidence-v1') (
        'Promoted SV Kanto shader evidence has the wrong schema.')
    Assert-Condition (-not [bool]$promoted.method.runtime_execution -and
        -not [bool]$promoted.method.emulator_used -and
        [int]$promoted.summary.source_families_staged -eq 8 -and
        [int]$promoted.summary.exactly_resolved_permutations -eq 38 -and
        [int]$promoted.summary.exactly_resolved_materials -eq 726 -and
        [int]$promoted.summary.unique_selected_programs -eq 19) (
        'Promoted SV Kanto shader evidence is incomplete.')
    $promotedStandard = @($promoted.families |
        Where-Object shader_family -eq 'Standard')
    Assert-Condition ($promotedStandard.Count -eq 1 -and
        [int]$promotedStandard[0].archive_variation_count -eq 6074 -and
        [int]$promotedStandard[0].metadata_variation_count -eq 6074 -and
        [int]$promotedStandard[0].parameter_words_per_variation -eq 3) (
        'Promoted evidence lost the Standard three-word ABI boundary.')
    Assert-Condition (Test-Path -LiteralPath $promotedAbiPath -PathType Leaf) (
        'Promoted SV selected-program ABI evidence is missing.')
    $promotedAbi = Get-Content -LiteralPath $promotedAbiPath -Raw |
        ConvertFrom-Json
    Assert-Condition ([string]$promotedAbi.schema -eq
        'pokemon-autochess-sv-selected-program-abi-v1' -and
        -not [bool]$promotedAbi.method.runtime_execution -and
        -not [bool]$promotedAbi.method.emulator_used -and
        [int]$promotedAbi.summary.program_count -eq 19 -and
        [int]$promotedAbi.summary.shader_families -eq 8 -and
        [int]$promotedAbi.summary.unique_fragment_sampler_symbols -eq 18 -and
        [int]$promotedAbi.summary.unique_fragment_buffer_symbols -eq 8 -and
        [int]$promotedAbi.summary.unique_vertex_buffer_symbols -eq 7) (
        'Promoted SV selected-program ABI evidence is incomplete.')
    Assert-Condition (Test-Path -LiteralPath $promotedDifferentialsPath `
        -PathType Leaf) (
        'Promoted SV program-differential evidence is missing.')
    $promotedDifferentials = Get-Content `
        -LiteralPath $promotedDifferentialsPath -Raw | ConvertFrom-Json
    Assert-Condition ([string]$promotedDifferentials.schema -eq
        'pokemon-autochess-sv-kanto-program-differential-evidence-v1' -and
        -not [bool]$promotedDifferentials.method.runtime_execution -and
        -not [bool]$promotedDifferentials.method.emulator_used -and
        [int]$promotedDifferentials.summary.differential_count -eq 9 -and
        [int]$promotedDifferentials.summary.proven_texture_mappings -eq 6 -and
        [int]$promotedDifferentials.summary.mapped_shader_families -eq 3 -and
        [int]$promotedDifferentials.summary.mapped_texture_roles -eq 4 -and
        [int]$promotedDifferentials.summary.unresolved_role_checks -eq 79) (
        'Promoted SV program-differential evidence is incomplete.')
    $expectedMappings = @{
        'SSS/RoughnessMap' = 'fp_t_tcb_10'
        'Standard/EmissionColorMap' = 'fp_t_tcb_12'
        'Standard/MetallicMap' = 'fp_t_tcb_A'
        'Standard/NormalMap' = 'fp_t_tcb_C'
        'Standard/RoughnessMap' = 'fp_t_tcb_10'
        'Transparent/NormalMap' = 'fp_t_tcb_C'
    }
    foreach ($mapping in @($promotedDifferentials.proven_texture_mappings)) {
        $mappingKey = [string]$mapping.shader_family + '/' +
            [string]$mapping.texture_role
        Assert-Condition ($expectedMappings.ContainsKey($mappingKey) -and
            [string]$mapping.sampler_name -eq $expectedMappings[$mappingKey] -and
            [string]$mapping.sampler_type -eq 'sampler2D') (
            "Unexpected promoted SV texture mapping: $mappingKey")
        $expectedMappings.Remove($mappingKey)
    }
    Assert-Condition ($expectedMappings.Count -eq 0) (
        'Promoted SV differential evidence lost a required texture mapping.')
    Assert-Condition (Test-Path -LiteralPath $promotedCensusPath -PathType Leaf) (
        'Current promoted SV material census is missing.')
    $promotedCensus = Get-Content -LiteralPath $promotedCensusPath -Raw |
        ConvertFrom-Json
    Assert-Condition (
        [string]$promotedCensus.schema -eq
            'pokemon-autochess-sv-kanto-material-census-v1' -and
        [int]$promotedCensus.summary.selected_species -eq 99 -and
        [int]$promotedCensus.summary.selected_models -eq 226 -and
        [int]$promotedCensus.summary.selected_materials -eq 946 -and
        [int]$promotedCensus.summary.unresolved_permutations -eq 1 -and
        [int]$promotedCensus.summary.unresolved_materials -eq 4) (
        'Current promoted SV material census is stale.')
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force `
        -ErrorAction SilentlyContinue
}

Write-Host '[SvKantoShaderPermutationWorkflowTest] PASS'
