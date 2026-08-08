param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'NativeModelPayloadStore.psm1') -Force

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Write-Utf8Text {
    param([string]$PathValue, [string]$Text)
    New-Item -ItemType Directory -Path (Split-Path -Parent $PathValue) -Force | Out-Null
    [IO.File]::WriteAllText($PathValue, $Text, (New-Object Text.UTF8Encoding($false)))
}

function Write-NativeFixture {
    param(
        [string]$Root,
        [string]$Stem,
        [string]$Variant,
        [byte[]]$Payload,
        [string]$MaterialName
    )
    $payloadPath = Join-Path $Root ($Stem + '.bin')
    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    [IO.File]::WriteAllBytes($payloadPath, $Payload)
    $hash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $document = [ordered]@{
        schema = 'phlosion-native-model-ir-v1'
        schema_version = 1
        source = [ordered]@{ material_variant = $Variant; material_source = "$Variant.material" }
        coordinate_system = [ordered]@{ texcoords_0 = 'gamefreak_native' }
        payload = [ordered]@{
            file = "$Stem.bin"
            byte_length = $Payload.Length
            sha256 = $hash
            byte_order = 'little_endian'
        }
        model = [ordered]@{ name = $Stem; vertex_count = 3; index_count = 3; submesh_count = 1 }
        materials = @([ordered]@{ name = $MaterialName; base_color = $Variant })
        skeleton = [ordered]@{ bones = @([ordered]@{ name = 'Root' }) }
        animations = @([ordered]@{ name = 'idle' })
    }
    $manifestPath = Join-Path $Root ($Stem + '.phmodel')
    Write-Utf8Text $manifestPath ($document | ConvertTo-Json -Depth 8)
    return $manifestPath
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-native-payload-test-' + [Guid]::NewGuid().ToString('N'))
$sourceRoot = Join-Path $tempRoot 'source'
$publishedRoot = Join-Path $tempRoot 'published'
$migrationRoot = Join-Path $tempRoot 'migration'

try {
    $sharedBytes = [byte[]](0, 1, 2, 3, 5, 8, 13, 21)
    $regularSource = Write-NativeFixture $sourceRoot 'Testmon' 'regular' $sharedBytes 'body_regular'
    $shinySource = Write-NativeFixture $sourceRoot 'Testmon_Shiny' 'shiny' $sharedBytes 'body_shiny'

    $regularPublish = Publish-NativeModelSharedPayload `
        -SourceManifestPath $regularSource `
        -SourceModelsRoot $sourceRoot `
        -DestinationModelsRoot $publishedRoot `
        -DestinationManifestName 'Testmon.phmodel'
    $shinyPublish = Publish-NativeModelSharedPayload `
        -SourceManifestPath $shinySource `
        -SourceModelsRoot $sourceRoot `
        -DestinationModelsRoot $publishedRoot `
        -DestinationManifestName 'Testmon_Shiny.phmodel'

    Assert-Condition $regularPublish.created_payload 'First publication should create the shared payload.'
    Assert-Condition (-not $shinyPublish.created_payload) 'Second publication should reuse the shared payload.'
    Assert-Condition (@(Get-ChildItem $publishedRoot -Recurse -File -Filter '*.bin').Count -eq 1) 'Publishing identical variants should store one physical payload.'
    Assert-Condition (-not (Test-Path (Join-Path $publishedRoot 'Testmon.bin'))) 'Publication left a regular stem payload behind.'
    Assert-Condition (-not (Test-Path (Join-Path $publishedRoot 'Testmon_Shiny.bin'))) 'Publication left a shiny stem payload behind.'

    $regular = Get-Content (Join-Path $publishedRoot 'Testmon.phmodel') -Raw | ConvertFrom-Json
    $shiny = Get-Content (Join-Path $publishedRoot 'Testmon_Shiny.phmodel') -Raw | ConvertFrom-Json
    Assert-Condition ($regular.payload.file -eq $shiny.payload.file) 'Variant manifests do not share one content-addressed identity.'
    Assert-Condition ($regular.source.material_variant -eq 'regular' -and $shiny.source.material_variant -eq 'shiny') 'Publication changed logical variant identity.'
    Assert-Condition ($regular.materials[0].name -eq 'body_regular' -and $shiny.materials[0].name -eq 'body_shiny') 'Publication changed variant material semantics.'

    $regularMigration = Write-NativeFixture $migrationRoot 'Migratemon' 'regular' $sharedBytes 'regular_material'
    $shinyMigration = Write-NativeFixture $migrationRoot 'Migratemon_Shiny' 'shiny' $sharedBytes 'shiny_material'
    $dryPlan = New-NativeModelPayloadMigrationPlan -ModelsRoot $migrationRoot
    Assert-Condition ($dryPlan.manifest_count -eq 2 -and $dryPlan.physical_payload_count -eq 2) 'Migration plan did not classify both legacy payloads.'
    Assert-Condition ($dryPlan.unique_payload_count -eq 1 -and $dryPlan.reclaimable_bytes -eq $sharedBytes.Length) 'Migration plan computed the wrong duplicate-byte recovery.'
    Assert-Condition ((Get-Content $regularMigration -Raw | ConvertFrom-Json).payload.file -eq 'Migratemon.bin') 'Dry-run migration changed a manifest.'

    $migrationResult = Invoke-NativeModelPayloadMigration -Plan $dryPlan -ConfirmMigration
    Assert-Condition ($migrationResult.rewritten_manifest_count -eq 2) 'Migration did not rewrite both manifests.'
    Assert-Condition ($migrationResult.final_payload_count -eq 1 -and $migrationResult.final_reclaimable_bytes -eq 0) 'Migration did not converge on one physical payload.'
    Assert-Condition (-not (Test-Path (Join-Path $migrationRoot 'Migratemon.bin'))) 'Migration left the regular legacy payload behind.'
    Assert-Condition (-not (Test-Path (Join-Path $migrationRoot 'Migratemon_Shiny.bin'))) 'Migration left the shiny legacy payload behind.'
    $postPlan = New-NativeModelPayloadMigrationPlan -ModelsRoot $migrationRoot
    Assert-Condition ($postPlan.legacy_manifest_count -eq 0 -and $postPlan.orphan_payload_count -eq 0) 'Migrated store failed its zero-legacy/zero-orphan guard.'
    Assert-Condition ($postPlan.duplicate_group_count -eq 0 -and $postPlan.shared_reference_group_count -eq 1) 'Migrated store must distinguish physical duplication from intentional shared references.'

    $idempotentResult = Invoke-NativeModelPayloadMigration -Plan $postPlan -ConfirmMigration
    Assert-Condition ($idempotentResult.created_payload_count -eq 0 -and $idempotentResult.rewritten_manifest_count -eq 0) 'Migration is not idempotent.'

    $corruptRoot = Join-Path $tempRoot 'corrupt'
    New-Item -ItemType Directory -Path $corruptRoot -Force | Out-Null
    $sourceRecord = Get-NativeModelPayloadRecord -ManifestPath $regularSource -ModelsRoot $sourceRoot
    $corruptPayload = Join-Path $corruptRoot $sourceRecord.canonical_relative_path
    New-Item -ItemType Directory -Path (Split-Path -Parent $corruptPayload) -Force | Out-Null
    [IO.File]::WriteAllBytes($corruptPayload, [byte[]](99, 98, 97))
    $corruptionRejected = $false
    try {
        Publish-NativeModelSharedPayload `
            -SourceManifestPath $regularSource `
            -SourceModelsRoot $sourceRoot `
            -DestinationModelsRoot $corruptRoot `
            -DestinationManifestName 'Testmon.phmodel' | Out-Null
    } catch {
        $corruptionRejected = $_.Exception.Message -like '*corrupt*'
    }
    Assert-Condition $corruptionRejected 'Publication should reject a corrupt immutable payload identity.'
    Assert-Condition (-not (Test-Path (Join-Path $corruptRoot 'Testmon.phmodel'))) 'Rejected publication should not commit its manifest.'

    Write-Host '[NativeModelPayloadStoreTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith($resolvedSystemTemp, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
