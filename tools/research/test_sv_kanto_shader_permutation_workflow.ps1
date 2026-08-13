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
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-sv-kanto-shaders-' + [Guid]::NewGuid().ToString('N'))
$studyRoot = Join-Path $temporaryRoot 'study'
$reportPath = Join-Path $temporaryRoot 'report.json'
$evidencePath = Join-Path $temporaryRoot 'evidence.json'
$promotedPath = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_shader_inventory.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'SV Kanto shader analyzer is missing.')
Assert-Condition (Test-Path -LiteralPath $extractor -PathType Leaf) (
    'SV Kanto headless shader source extractor is missing.')
$extractorSource = Get-Content -LiteralPath $extractor -Raw
foreach ($token in @(
        '--romfs', '--file-hash', '--trsha',
        '--require-complete-source', '--require-exact-resolution',
        'runtime_execution = $false', 'emulator_used = $false')) {
    Assert-Condition ($extractorSource.Contains($token)) (
        "SV Kanto source extractor lost contract token: $token")
}
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'runtime_execution": False', 'emulator_used": False',
        'shader_metadata_default', 'material_document', 'param_buffer',
        'to exactly one param_buffer variation')) {
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
    Assert-Condition ([int]$summary.selected_species -eq 77 -and
        [int]$summary.selected_models -eq 174 -and
        [int]$summary.selected_materials -eq 726) (
        'SV selected-corpus totals changed; review the canonical selection.')
    Assert-Condition ([int]$summary.material_permutations -eq 38 -and
        [int]$summary.shader_families -eq 8) (
        'SV material permutation totals changed; review the inventory.')
    Assert-Condition ([int]$summary.exactly_resolved_permutations -eq 38 -and
        [int]$summary.exactly_resolved_materials -eq 726 -and
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
        EyeClearCoat = @(368, 17)
        NonDirectional = @(2, 1)
        SSS = @(308, 6)
        SSSEffect = @(8, 1)
        Standard = @(12, 5)
        Transparent = @(4, 2)
        Unlit = @(10, 3)
    }
    foreach ($family in @($report.families)) {
        $expected = $expectedCounts[[string]$family.shader_family]
        Assert-Condition ($null -ne $expected -and
            [int]$family.material_count -eq $expected[0] -and
            [int]$family.permutation_count -eq $expected[1]) (
            "Unexpected corpus count for $($family.shader_family).")
    }

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
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force `
        -ErrorAction SilentlyContinue
}

Write-Host '[SvKantoShaderPermutationWorkflowTest] PASS'
