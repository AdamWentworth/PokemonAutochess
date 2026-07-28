param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ManifestPath = [IO.Path]::GetFullPath($ManifestPath)
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "LGPE source manifest is missing: $ManifestPath"
}

$raw = [IO.File]::ReadAllText($ManifestPath)
$manifest = $raw | ConvertFrom-Json

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

Assert-Condition ($manifest.schema_version -eq 1) (
    "Unexpected source-manifest schema: $($manifest.schema_version)")
Assert-Condition ($manifest.profile_id -eq "lgpe_route1_road001_00") (
    "Unexpected source-manifest profile: $($manifest.profile_id)")
Assert-Condition ($manifest.ingestion.mode -eq "direct_source") (
    "Manifest was not produced by direct source ingestion")
Assert-Condition ($manifest.ingestion.canonical_bridge -eq "none") (
    "A conversion bridge entered the canonical ingestion path")
Assert-Condition ([bool]$manifest.validation.passed) (
    "Importer validation did not pass")

Assert-Condition ($manifest.source.model.format -eq "GFBMDL") (
    "Source model format changed")
Assert-Condition (
    $manifest.source.model.sha256 -eq
        "941EBF95032362D08D516D7502F15865FB1BA47CA78DFA0334DC665D3BDC2A86") (
    "Route 1 source GFBMDL hash changed")
Assert-Condition ($manifest.scene.mesh_count -eq 38) (
    "Expected 38 direct-source meshes")
Assert-Condition ($manifest.scene.material_count -eq 21) (
    "Expected 21 direct-source materials")
Assert-Condition ($manifest.scene.bone_count -eq 63) (
    "Expected 63 direct-source bones")
Assert-Condition ($manifest.scene.triangle_record_count -eq 88618) (
    "Expected 88,618 direct-source triangle records")
Assert-Condition (
    $manifest.scene.unique_material_indexed_triangle_count -eq 88480) (
    "Expected 88,480 unique material-indexed triangles")
Assert-Condition (
    $manifest.scene.
        duplicate_material_indexed_triangle_record_count -eq 138) (
    "Expected 138 exact duplicate triangle records")
Assert-Condition (
    $manifest.scene.triangle_record_count -eq
        ($manifest.scene.unique_material_indexed_triangle_count +
         $manifest.scene.
            duplicate_material_indexed_triangle_record_count)) (
    "Triangle accounting does not balance")
Assert-Condition ($manifest.scene.required_texture_count -eq 39) (
    "Expected 39 required source textures")
Assert-Condition ($manifest.texture_containers.Count -eq 39) (
    "Expected 39 BNTX dependency containers")
Assert-Condition ($manifest.texture_coverage.missing_required_textures.Count -eq 0) (
    "Required BNTX textures are missing")
Assert-Condition (
    $manifest.texture_coverage.ambiguous_required_textures.Count -eq 0) (
    "Required texture ownership is ambiguous")

Assert-Condition ($manifest.meshes.Count -eq $manifest.scene.mesh_count) (
    "Mesh records do not match the scene count")
foreach ($mesh in $manifest.meshes) {
    Assert-Condition ($mesh.vertex_count -gt 0) (
        "Mesh '$($mesh.name)' has no decoded vertices")
    Assert-Condition (
        $mesh.raw_vertex_data.sha256 -match "^[A-F0-9]{64}$") (
        "Mesh '$($mesh.name)' has no raw vertex-buffer digest")
    Assert-Condition ($mesh.polygon_groups.Count -gt 0) (
        "Mesh '$($mesh.name)' has no polygon groups")
    Assert-Condition (
        $mesh.triangle_record_count -eq
            ($mesh.unique_material_indexed_triangle_count +
             $mesh.duplicate_material_indexed_triangle_record_count)) (
        "Mesh '$($mesh.name)' triangle accounting does not balance")
    foreach ($group in $mesh.polygon_groups) {
        Assert-Condition ($group.indices_sha256 -match "^[A-F0-9]{64}$") (
            "Mesh '$($mesh.name)' has an invalid index digest")
    }
}

Assert-Condition (
    $manifest.scene.attribute_population_by_mesh.POSITION -eq 38) (
    "POSITION was not preserved on every mesh")
Assert-Condition (
    $manifest.scene.attribute_population_by_mesh.NORMAL -eq 38) (
    "NORMAL was not preserved on every mesh")
Assert-Condition (
    $manifest.scene.attribute_population_by_mesh.TANGENT -eq 38) (
    "TANGENT was not preserved on every mesh")
Assert-Condition (
    $manifest.scene.attribute_population_by_mesh.TEXCOORD_0 -eq 38) (
    "TEXCOORD_0 was not preserved on every mesh")
Assert-Condition (
    $manifest.scene.attribute_population_by_mesh.TEXCOORD_1 -eq 27) (
    "Expected TEXCOORD_1 on 27 meshes")
Assert-Condition (
    $manifest.scene.attribute_population_by_mesh.TEXCOORD_2 -eq 12) (
    "Expected TEXCOORD_2 on 12 meshes")
Assert-Condition (
    $manifest.scene.attribute_population_by_mesh.COLOR_0 -eq 38) (
    "COLOR_0 was not preserved on every mesh")

foreach ($material in $manifest.materials) {
    Assert-Condition (
        $material.source_metadata_sha256 -match "^[A-F0-9]{64}$") (
        "Material '$($material.name)' has no source metadata digest")
    Assert-Condition (
        -not [string]::IsNullOrWhiteSpace([string]$material.shader_group)) (
        "Material '$($material.name)' has no source shader group")
}
$skipMainRenderingMaterials = @(
    $manifest.materials |
        Where-Object {
            @(
                $_.source_metadata.Switches |
                    Where-Object {
                        $_.Name -eq "SkipMainRendering" -and
                        [bool]$_.Value
                    }).Count -gt 0
        })
Assert-Condition ($skipMainRenderingMaterials.Count -eq 2) (
    "Expected two source materials marked SkipMainRendering")
Assert-Condition (
    @(
        $skipMainRenderingMaterials |
            Where-Object { $_.shader_group -ne "FieldShadowOnlyShader" }
    ).Count -eq 0) (
    "SkipMainRendering materials must retain FieldShadowOnlyShader semantics")

$textureRecordCount = 0
foreach ($container in $manifest.texture_containers) {
    Assert-Condition (
        -not [IO.Path]::IsPathRooted([string]$container.relative_path)) (
        "Manifest leaked an absolute source-container path")
    Assert-Condition ($container.sha256 -match "^[A-F0-9]{64}$") (
        "BNTX container '$($container.relative_path)' has no digest")
    foreach ($texture in $container.textures) {
        $textureRecordCount += 1
        Assert-Condition ($texture.width -gt 0 -and $texture.height -gt 0) (
            "Texture '$($texture.name)' has invalid dimensions")
        Assert-Condition ($texture.mip_count -gt 0) (
            "Texture '$($texture.name)' has no mip chain")
        Assert-Condition ($texture.payload_sha256 -match "^[A-F0-9]{64}$") (
            "Texture '$($texture.name)' has no payload digest")
    }
}
Assert-Condition (
    $textureRecordCount -eq $manifest.scene.decoded_texture_count) (
    "Texture records do not match the decoded texture count")

Assert-Condition (
    -not [IO.Path]::IsPathRooted([string]$manifest.source.model.relative_path)) (
    "Manifest leaked an absolute source-model path")
Assert-Condition ($raw -notmatch '(?i)\.(dae|glb)"') (
    "A DAE or GLB path entered the direct-source manifest")

Write-Host (
    "[LGPEImporterValidation] PASS " +
    "meshes=$($manifest.scene.mesh_count) " +
    "materials=$($manifest.scene.material_count) " +
    "triangle_records=$($manifest.scene.triangle_record_count) " +
    "unique_triangles=" +
        "$($manifest.scene.unique_material_indexed_triangle_count) " +
    "textures=$($manifest.scene.required_texture_count)")
