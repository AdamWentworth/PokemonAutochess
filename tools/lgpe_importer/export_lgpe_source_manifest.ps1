param(
    [string]$ProfilePath = "",
    [string]$UnpackedRoot = "",
    [string]$ToolboxRoot = "",
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$script:ImporterSchemaVersion = 1
$script:ImporterVersion = "0.1.0"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrWhiteSpace($ProfilePath)) {
    $ProfilePath = Join-Path $PSScriptRoot "route1.profile.json"
}
if ([string]::IsNullOrWhiteSpace($UnpackedRoot)) {
    $UnpackedRoot = Join-Path $repoRoot (
        "..\PokemonAutochessEnvironment\" +
        "Pokemon_Lets_Go_Pikachu_v0_Environment_GFPAK_Unpacked")
}
if ([string]::IsNullOrWhiteSpace($ToolboxRoot)) {
    $ToolboxRoot = Join-Path $repoRoot (
        "..\PokemonAutochessEnvironment\Tools\Switch-Toolbox-Final")
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot (
        "debug\lgpe_importer\route1_source_manifest.json")
}

function Get-Sha256File {
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-Sha256Bytes {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace("-", "")
    } finally {
        $sha.Dispose()
    }
}

function Get-Sha256Text {
    param([Parameter(Mandatory = $true)][string]$Text)

    return Get-Sha256Bytes -Bytes ([Text.Encoding]::UTF8.GetBytes($Text))
}

function Resolve-SourcePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )

    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    $platformRelative = $RelativePath.Replace(
        "/",
        [string][IO.Path]::DirectorySeparatorChar)
    $resolved = [IO.Path]::GetFullPath((Join-Path $fullRoot $platformRelative))
    $requiredPrefix = $fullRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith(
            $requiredPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Profile path escapes the unpacked source root: $RelativePath"
    }
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Required LGPE source file is missing: $resolved"
    }
    return $resolved
}

function Convert-Vector2 {
    param($Value)

    return [ordered]@{
        x = [double]$Value.X
        y = [double]$Value.Y
    }
}

function Convert-Vector3 {
    param($Value)

    return [ordered]@{
        x = [double]$Value.X
        y = [double]$Value.Y
        z = [double]$Value.Z
    }
}

function Convert-Vector4 {
    param($Value)

    return [ordered]@{
        x = [double]$Value.X
        y = [double]$Value.Y
        z = [double]$Value.Z
        w = [double]$Value.W
    }
}

function Convert-Matrix4 {
    param($Value)

    return [ordered]@{
        row0 = Convert-Vector4 $Value.Row0
        row1 = Convert-Vector4 $Value.Row1
        row2 = Convert-Vector4 $Value.Row2
        row3 = Convert-Vector4 $Value.Row3
    }
}

function Get-AttributeSemanticHint {
    param([uint32]$VertexType)

    switch ($VertexType) {
        0 { return "POSITION" }
        1 { return "NORMAL" }
        2 { return "TANGENT" }
        3 { return "TEXCOORD_0" }
        4 { return "TEXCOORD_1" }
        5 { return "TEXCOORD_2" }
        6 { return "TEXCOORD_3" }
        7 { return "COLOR_0" }
        8 { return "JOINTS_0" }
        9 { return "WEIGHTS_0" }
        default { return "UNKNOWN_$VertexType" }
    }
}

function Get-UInt32SequenceSha256 {
    param([Parameter(Mandatory = $true)]$Values)

    $memory = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($memory)
    try {
        $writer.Write([uint32]$Values.Count)
        for ($index = 0; $index -lt $Values.Count; ++$index) {
            $writer.Write([uint32]$Values[$index])
        }
        $writer.Flush()
        return Get-Sha256Bytes -Bytes $memory.ToArray()
    } finally {
        $writer.Dispose()
        $memory.Dispose()
    }
}

function Get-TexturePayloadSha256 {
    param([Parameter(Mandatory = $true)]$Texture)

    $memory = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($memory)
    try {
        $writer.Write([int32]$Texture.TextureData.Count)
        for ($surfaceIndex = 0;
             $surfaceIndex -lt $Texture.TextureData.Count;
             ++$surfaceIndex) {
            $mips = $Texture.TextureData[$surfaceIndex]
            $writer.Write([int32]$mips.Count)
            for ($mipIndex = 0; $mipIndex -lt $mips.Count; ++$mipIndex) {
                [byte[]]$bytes = $mips[$mipIndex]
                $writer.Write([int64]$bytes.LongLength)
                $writer.Write($bytes)
            }
        }
        $writer.Flush()
        return Get-Sha256Bytes -Bytes $memory.ToArray()
    } finally {
        $writer.Dispose()
        $memory.Dispose()
    }
}

function Add-ValidationCheck {
    param(
        [Collections.ArrayList]$Checks,
        [Parameter(Mandatory = $true)][string]$Name,
        $Expected,
        $Actual
    )

    $passed = $Expected -eq $Actual
    [void]$Checks.Add([ordered]@{
        name = $Name
        passed = [bool]$passed
        expected = $Expected
        actual = $Actual
    })
}

$ProfilePath = [IO.Path]::GetFullPath($ProfilePath)
$UnpackedRoot = [IO.Path]::GetFullPath($UnpackedRoot)
$ToolboxRoot = [IO.Path]::GetFullPath($ToolboxRoot)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

foreach ($requiredDirectory in @($UnpackedRoot, $ToolboxRoot)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required directory is missing: $requiredDirectory"
    }
}
if (-not (Test-Path -LiteralPath $ProfilePath -PathType Leaf)) {
    throw "LGPE importer profile is missing: $ProfilePath"
}

$profileText = [IO.File]::ReadAllText($ProfilePath)
$profile = $profileText | ConvertFrom-Json
if ([int]$profile.schema_version -ne 1) {
    throw "Unsupported LGPE importer profile schema: $($profile.schema_version)"
}

$modelRelativePath = [string]$profile.model.relative_path
$modelPath = Resolve-SourcePath `
    -Root $UnpackedRoot `
    -RelativePath $modelRelativePath

$toolboxLibraryPath = Join-Path $ToolboxRoot "Toolbox.Library.dll"
$pluginPath = Join-Path $ToolboxRoot "FirstPlugin.Plg.dll"
$openTkPath = Join-Path $ToolboxRoot "Lib\OpenTK.dll"
$openTkControlPath = Join-Path $ToolboxRoot "Lib\OpenTK.GLControl.dll"
$decoderPaths = @(
    $toolboxLibraryPath,
    $pluginPath,
    $openTkPath,
    $openTkControlPath)
foreach ($decoderPath in $decoderPaths) {
    if (-not (Test-Path -LiteralPath $decoderPath -PathType Leaf)) {
        throw "Required Switch Toolbox decoder file is missing: $decoderPath"
    }
}

$textureInputs = @(
    foreach ($relativePathValue in $profile.texture_containers) {
        $relativePath = [string]$relativePathValue
        [ordered]@{
            relative_path = $relativePath.Replace("\", "/")
            full_path = Resolve-SourcePath `
                -Root $UnpackedRoot `
                -RelativePath $relativePath
        }
    })

Add-Type -AssemblyName System.Windows.Forms
[void][Reflection.Assembly]::LoadFrom($openTkPath)
[void][Reflection.Assembly]::LoadFrom($openTkControlPath)
$toolboxAssembly = [Reflection.Assembly]::LoadFrom($toolboxLibraryPath)
$runtimeType = $toolboxAssembly.GetType("Toolbox.Library.Runtime")
$runtimeType.GetField("ExecutableDir").SetValue($null, $ToolboxRoot)
$runtimeType.GetField("MainForm").SetValue(
    $null,
    (New-Object Windows.Forms.Form))
[void][Reflection.Assembly]::LoadFrom($pluginPath)

$model = New-Object FirstPlugin.GFBMDL
$model.FileName = [IO.Path]::GetFileName($modelPath)
$model.FilePath = $modelPath
$modelStream = [IO.File]::OpenRead($modelPath)
$decoderDiagnosticsWriter = New-Object IO.StringWriter
$originalConsoleWriter = [Console]::Out
try {
    [Console]::SetOut($decoderDiagnosticsWriter)
    $model.Load($modelStream)
} finally {
    [Console]::SetOut($originalConsoleWriter)
    $modelStream.Dispose()
}
$decoderDiagnostics = @(
    $decoderDiagnosticsWriter.ToString() -split "\r?\n" |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$decoderDiagnosticsWriter.Dispose()

$meshRecords = New-Object Collections.ArrayList
$totalVertices = [uint64]0
$totalTriangleRecords = [uint64]0
$totalUniqueMaterialIndexedTriangles = [uint64]0
$totalDuplicateMaterialIndexedTriangleRecords = [uint64]0
$totalPolygonGroups = [uint64]0
$attributePopulation = [ordered]@{}

for ($meshIndex = 0;
     $meshIndex -lt $model.Model.GenericMeshes.Count;
     ++$meshIndex) {
    $mesh = $model.Model.GenericMeshes[$meshIndex]
    $vertices = $mesh.vertices
    $vertexCount = [uint64]$vertices.Count
    $totalVertices += $vertexCount

    $boundsMin = $null
    $boundsMax = $null
    if ($vertexCount -gt 0) {
        $firstPosition = $vertices[0].pos
        $minX = [double]$firstPosition.X
        $minY = [double]$firstPosition.Y
        $minZ = [double]$firstPosition.Z
        $maxX = $minX
        $maxY = $minY
        $maxZ = $minZ
        for ($vertexIndex = 1;
             $vertexIndex -lt $vertices.Count;
             ++$vertexIndex) {
            $position = $vertices[$vertexIndex].pos
            $minX = [Math]::Min($minX, [double]$position.X)
            $minY = [Math]::Min($minY, [double]$position.Y)
            $minZ = [Math]::Min($minZ, [double]$position.Z)
            $maxX = [Math]::Max($maxX, [double]$position.X)
            $maxY = [Math]::Max($maxY, [double]$position.Y)
            $maxZ = [Math]::Max($maxZ, [double]$position.Z)
        }
        $boundsMin = [ordered]@{ x = $minX; y = $minY; z = $minZ }
        $boundsMax = [ordered]@{ x = $maxX; y = $maxY; z = $maxZ }
    }

    $attributes = @(
        foreach ($attribute in $mesh.MeshData.Attributes) {
            $semanticHint = Get-AttributeSemanticHint (
                [uint32]$attribute.VertexType)
            if (-not $attributePopulation.Contains($semanticHint)) {
                $attributePopulation[$semanticHint] = 0
            }
            $attributePopulation[$semanticHint] =
                [int]$attributePopulation[$semanticHint] + 1
            [ordered]@{
                vertex_type = [uint32]$attribute.VertexType
                semantic_hint = $semanticHint
                buffer_format = [uint32]$attribute.BufferFormat
                element_count = [uint32]$attribute.ElementCount
            }
        })

    [byte[]]$rawVertexBytes = @($mesh.MeshData.Data)
    $polygonRecords = New-Object Collections.ArrayList
    $materialIndexedTriangleKeys =
        New-Object Collections.Generic.HashSet[string] (
            [StringComparer]::Ordinal)
    $meshTriangleRecords = [uint64]0
    for ($polygonIndex = 0;
         $polygonIndex -lt $mesh.MeshData.Polygons.Count;
         ++$polygonIndex) {
        $polygon = $mesh.MeshData.Polygons[$polygonIndex]
        $indexCount = [uint64]$polygon.Faces.Count
        if (($indexCount % 3) -ne 0) {
            throw (
                "Mesh '$($mesh.Text)' polygon group $polygonIndex has " +
                "$indexCount indices, which is not a triangle list")
        }
        $triangleRecordCount = [uint64]($indexCount / 3)
        $meshTriangleRecords += $triangleRecordCount
        $totalPolygonGroups += 1

        for ($faceIndex = 0;
             $faceIndex -lt $polygon.Faces.Count;
             $faceIndex += 3) {
            $triangleKey = (
                "{0}:{1},{2},{3}" -f
                [int]$polygon.MaterialIndex,
                [uint32]$polygon.Faces[$faceIndex],
                [uint32]$polygon.Faces[$faceIndex + 1],
                [uint32]$polygon.Faces[$faceIndex + 2])
            [void]$materialIndexedTriangleKeys.Add($triangleKey)
        }

        $primitiveType = "Triangles"
        if ($polygonIndex -lt $mesh.PolygonGroups.Count) {
            $primitiveType =
                [string]$mesh.PolygonGroups[$polygonIndex].PrimativeType
        }
        [void]$polygonRecords.Add([ordered]@{
            index = $polygonIndex
            material_index = [int]$polygon.MaterialIndex
            primitive_type = $primitiveType
            index_count = $indexCount
            triangle_record_count = $triangleRecordCount
            indices_sha256 = Get-UInt32SequenceSha256 $polygon.Faces
        })
    }
    $meshUniqueMaterialIndexedTriangleCount =
        [uint64]$materialIndexedTriangleKeys.Count
    $meshDuplicateMaterialIndexedTriangleRecordCount =
        $meshTriangleRecords - $meshUniqueMaterialIndexedTriangleCount
    $totalTriangleRecords += $meshTriangleRecords
    $totalUniqueMaterialIndexedTriangles +=
        $meshUniqueMaterialIndexedTriangleCount
    $totalDuplicateMaterialIndexedTriangleRecords +=
        $meshDuplicateMaterialIndexedTriangleRecordCount

    [void]$meshRecords.Add([ordered]@{
        index = $meshIndex
        name = [string]$mesh.Text
        transform = Convert-Matrix4 $mesh.Transform
        vertex_count = $vertexCount
        bounds = [ordered]@{
            minimum = $boundsMin
            maximum = $boundsMax
        }
        raw_vertex_data = [ordered]@{
            byte_count = [uint64]$rawVertexBytes.LongLength
            sha256 = Get-Sha256Bytes $rawVertexBytes
        }
        attributes = $attributes
        polygon_groups = @($polygonRecords)
        triangle_record_count = $meshTriangleRecords
        unique_material_indexed_triangle_count =
            $meshUniqueMaterialIndexedTriangleCount
        duplicate_material_indexed_triangle_record_count =
            $meshDuplicateMaterialIndexedTriangleRecordCount
    })
}

$materialRecords = New-Object Collections.ArrayList
$shaderGroups = New-Object Collections.Generic.HashSet[string] (
    [StringComparer]::Ordinal)
for ($materialIndex = 0;
     $materialIndex -lt $model.Model.GenericMaterials.Count;
     ++$materialIndex) {
    $material = $model.Model.GenericMaterials[$materialIndex]
    $rawMetadata = [string]$material.ConvertToJson()
    $metadata = $rawMetadata | ConvertFrom-Json
    if (-not [string]::IsNullOrWhiteSpace([string]$metadata.ShaderGroup)) {
        [void]$shaderGroups.Add([string]$metadata.ShaderGroup)
    }

    $bindings = @(
        foreach ($textureMap in $material.TextureMaps) {
            [ordered]@{
                texture_name = [string]$textureMap.Name
                sampler_name = [string]$textureMap.SamplerName
                texture_type = [string]$textureMap.Type
                texture_unit = [int]$textureMap.textureUnit
                wrap_s = [string]$textureMap.WrapModeS
                wrap_t = [string]$textureMap.WrapModeT
                wrap_w = [string]$textureMap.WrapModeW
                min_filter = [string]$textureMap.MinFilter
                mag_filter = [string]$textureMap.MagFilter
                scale = Convert-Vector2 $textureMap.Transform.Scale
                translate = Convert-Vector2 $textureMap.Transform.Translate
            }
        })

    [void]$materialRecords.Add([ordered]@{
        index = $materialIndex
        name = [string]$material.Text
        shader_group = [string]$metadata.ShaderGroup
        source_metadata_sha256 = Get-Sha256Text $rawMetadata
        texture_bindings = $bindings
        source_metadata = $metadata
    })
}

$boneRecords = New-Object Collections.ArrayList
for ($boneIndex = 0;
     $boneIndex -lt $model.Model.Skeleton.bones.Count;
     ++$boneIndex) {
    $bone = $model.Model.Skeleton.bones[$boneIndex]
    [void]$boneRecords.Add([ordered]@{
        index = $boneIndex
        name = [string]$bone.Text
        parent_index = [int]$bone.parentIndex
        has_skinning = [bool]$bone.HasSkinning
        position = Convert-Vector3 $bone.Position
        rotation = Convert-Vector4 $bone.Rotation
        scale = Convert-Vector3 $bone.Scale
        billboard_index = [int]$bone.BillboardIndex
        rigid_matrix_index = [int]$bone.RigidMatrixIndex
        smooth_matrix_index = [int]$bone.SmoothMatrixIndex
        use_segment_scale_compensate = [bool]$bone.UseSegmentScaleCompensate
    })
}

$textureContainerRecords = New-Object Collections.ArrayList
$textureLocations = @{}
$decodedTextureCount = 0
foreach ($textureInput in $textureInputs) {
    $container = New-Object FirstPlugin.BNTX
    $container.FileName = [IO.Path]::GetFileName($textureInput.full_path)
    $container.FilePath = $textureInput.full_path
    $containerStream = [IO.File]::OpenRead($textureInput.full_path)
    try {
        $container.Load($containerStream)
    } finally {
        $containerStream.Dispose()
    }

    $containerTextures = New-Object Collections.ArrayList
    $textureEntries = @(
        $container.Textures.GetEnumerator() |
            Sort-Object { [string]$_.Key })
    foreach ($textureEntry in $textureEntries) {
        $textureName = [string]$textureEntry.Key
        $textureData = $textureEntry.Value
        $rawTexture = $textureData.Texture
        $decodedTextureCount += 1

        if (-not $textureLocations.ContainsKey($textureName)) {
            $textureLocations[$textureName] =
                New-Object Collections.ArrayList
        }
        [void]$textureLocations[$textureName].Add(
            [string]$textureInput.relative_path)

        [void]$containerTextures.Add([ordered]@{
            name = $textureName
            width = [uint32]$textureData.Width
            height = [uint32]$textureData.Height
            depth = [uint32]$textureData.Depth
            array_count = [uint32]$textureData.ArrayCount
            mip_count = [uint32]$textureData.MipCount
            format = [string]$textureData.Format
            surface_type = [string]$textureData.SurfaceType
            data_size_bytes = [uint64]$textureData.DataSizeInBytes
            is_swizzled = [bool]$textureData.IsSwizzled
            is_srgb = [bool]$rawTexture.UseSRGB
            tile_mode = [string]$rawTexture.TileMode
            block_height_log2 = [uint32]$rawTexture.BlockHeightLog2
            pitch = [uint32]$rawTexture.Pitch
            mip_offsets = @(
                $rawTexture.MipOffsets | ForEach-Object { [int64]$_ })
            channels = [ordered]@{
                red = [string]$rawTexture.ChannelRed
                green = [string]$rawTexture.ChannelGreen
                blue = [string]$rawTexture.ChannelBlue
                alpha = [string]$rawTexture.ChannelAlpha
            }
            payload_sha256 = Get-TexturePayloadSha256 $rawTexture
        })
    }

    [void]$textureContainerRecords.Add([ordered]@{
        relative_path = [string]$textureInput.relative_path
        size_bytes = [uint64](Get-Item -LiteralPath $textureInput.full_path).Length
        sha256 = Get-Sha256File $textureInput.full_path
        texture_count = $containerTextures.Count
        textures = @($containerTextures)
    })
}

$requiredTextureNames = @(
    $model.Model.Textures |
        ForEach-Object { [string]$_ } |
        Sort-Object -Unique)
$missingRequiredTextures = @(
    $requiredTextureNames |
        Where-Object { -not $textureLocations.ContainsKey($_) })
$ambiguousRequiredTextures = @(
    $requiredTextureNames |
        Where-Object {
            $textureLocations.ContainsKey($_) -and
            $textureLocations[$_].Count -gt 1
        })

$modelHash = Get-Sha256File $modelPath
$modelSize = [uint64](Get-Item -LiteralPath $modelPath).Length
$checks = New-Object Collections.ArrayList
Add-ValidationCheck $checks "model_sha256" `
    ([string]$profile.model.expected_sha256).ToUpperInvariant() `
    $modelHash
Add-ValidationCheck $checks "model_size_bytes" `
    ([uint64]$profile.model.expected_size_bytes) `
    $modelSize
Add-ValidationCheck $checks "mesh_count" `
    ([int]$profile.expectations.mesh_count) `
    $model.Model.GenericMeshes.Count
Add-ValidationCheck $checks "material_count" `
    ([int]$profile.expectations.material_count) `
    $model.Model.GenericMaterials.Count
Add-ValidationCheck $checks "bone_count" `
    ([int]$profile.expectations.bone_count) `
    $model.Model.Skeleton.bones.Count
Add-ValidationCheck $checks "required_texture_count" `
    ([int]$profile.expectations.required_texture_count) `
    $requiredTextureNames.Count
Add-ValidationCheck $checks "texture_container_count" `
    ([int]$profile.expectations.texture_container_count) `
    $textureContainerRecords.Count
Add-ValidationCheck $checks "triangle_record_count" `
    ([uint64]$profile.expectations.triangle_record_count) `
    $totalTriangleRecords
Add-ValidationCheck $checks "unique_material_indexed_triangle_count" `
    ([uint64]$profile.expectations.unique_material_indexed_triangle_count) `
    $totalUniqueMaterialIndexedTriangles
Add-ValidationCheck `
    $checks `
    "duplicate_material_indexed_triangle_record_count" `
    ([uint64]$profile.expectations.
        duplicate_material_indexed_triangle_record_count) `
    $totalDuplicateMaterialIndexedTriangleRecords
Add-ValidationCheck $checks "missing_required_texture_count" 0 `
    $missingRequiredTextures.Count
Add-ValidationCheck $checks "ambiguous_required_texture_count" 0 `
    $ambiguousRequiredTextures.Count

$failedChecks = @($checks | Where-Object { -not $_.passed })
$decoderRecords = @(
    foreach ($decoderPath in $decoderPaths) {
        $versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($decoderPath)
        [ordered]@{
            file_name = [IO.Path]::GetFileName($decoderPath)
            sha256 = Get-Sha256File $decoderPath
            file_version = [string]$versionInfo.FileVersion
        }
    })
$textureLocationRecords = [ordered]@{}
foreach ($textureLocationName in ($textureLocations.Keys | Sort-Object)) {
    $textureLocationName = [string]$textureLocationName
    $textureLocationRecords[$textureLocationName] = @(
        $textureLocations[$textureLocationName] | Sort-Object)
}

$manifest = [ordered]@{
    schema_version = $script:ImporterSchemaVersion
    profile_id = [string]$profile.profile_id
    ingestion = [ordered]@{
        mode = "direct_source"
        canonical_bridge = "none"
        importer_version = $script:ImporterVersion
        profile_sha256 = Get-Sha256File $ProfilePath
        decoder = "Switch Toolbox managed format readers"
        decoder_files = $decoderRecords
        decoder_diagnostics = $decoderDiagnostics
        attribute_semantic_hint_status = (
            "Switch Toolbox GFMDL vertex-type convention; " +
            "independent format verification remains required")
    }
    source = [ordered]@{
        kind = [string]$profile.source_kind
        model = [ordered]@{
            format = "GFBMDL"
            relative_path = $modelRelativePath.Replace("\", "/")
            size_bytes = $modelSize
            sha256 = $modelHash
        }
        texture_container_format = "BNTX"
    }
    scene = [ordered]@{
        mesh_count = $model.Model.GenericMeshes.Count
        material_count = $model.Model.GenericMaterials.Count
        bone_count = $model.Model.Skeleton.bones.Count
        polygon_group_count = $totalPolygonGroups
        vertex_count = $totalVertices
        triangle_record_count = $totalTriangleRecords
        unique_material_indexed_triangle_count =
            $totalUniqueMaterialIndexedTriangles
        duplicate_material_indexed_triangle_record_count =
            $totalDuplicateMaterialIndexedTriangleRecords
        triangle_uniqueness_key = (
            "mesh/material/ordered vertex-index triplet")
        required_texture_count = $requiredTextureNames.Count
        decoded_texture_count = $decodedTextureCount
        shader_groups = @($shaderGroups | Sort-Object)
        attribute_population_by_mesh = $attributePopulation
    }
    required_textures = $requiredTextureNames
    meshes = @($meshRecords)
    materials = @($materialRecords)
    skeleton = [ordered]@{
        bones = @($boneRecords)
    }
    texture_containers = @($textureContainerRecords)
    texture_coverage = [ordered]@{
        missing_required_textures = $missingRequiredTextures
        ambiguous_required_textures = $ambiguousRequiredTextures
        locations = $textureLocationRecords
    }
    validation = [ordered]@{
        passed = $failedChecks.Count -eq 0
        checks = @($checks)
    }
}

$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
$json = $manifest | ConvertTo-Json -Depth 64
[IO.File]::WriteAllText(
    $OutputPath,
    $json + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

if (-not $manifest.validation.passed) {
    $failureSummary = @(
        $failedChecks |
            ForEach-Object {
                "$($_.name): expected=$($_.expected) actual=$($_.actual)"
            }) -join "; "
    throw "LGPE source manifest validation failed: $failureSummary"
}

Write-Host (
    "[LGPEImporter] PASS " +
    "profile=$($manifest.profile_id) " +
    "meshes=$($manifest.scene.mesh_count) " +
    "materials=$($manifest.scene.material_count) " +
    "triangle_records=$($manifest.scene.triangle_record_count) " +
    "unique_triangles=" +
        "$($manifest.scene.unique_material_indexed_triangle_count) " +
    "textures=$($manifest.scene.required_texture_count)")
Write-Host "[LGPEImporter] Manifest: $OutputPath"
