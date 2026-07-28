param(
    [string]$RootPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($RootPath)) {
    $RootPath = Join-Path $repoRoot "cache\lgpe\route1"
}
$RootPath = [IO.Path]::GetFullPath($RootPath)

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Resolve-PayloadPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$FileName
    )

    Assert-Condition (-not [IO.Path]::IsPathRooted($FileName)) (
        "Canonical payload path must be relative: $FileName")
    Assert-Condition (
        $FileName.IndexOfAny(
            [IO.Path]::GetInvalidFileNameChars()) -lt 0) (
        "Canonical payload must be a file name: $FileName")
    $resolved = [IO.Path]::GetFullPath((Join-Path $Root $FileName))
    $requiredPrefix =
        $Root.TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar
    Assert-Condition (
        $resolved.StartsWith(
            $requiredPrefix,
            [StringComparison]::OrdinalIgnoreCase)) (
        "Canonical payload escapes its root: $FileName")
    Assert-Condition (
        (Test-Path -LiteralPath $resolved -PathType Leaf)) (
        "Canonical payload is missing: $resolved")
    return $resolved
}

function Assert-Range {
    param(
        [Parameter(Mandatory = $true)][uint64]$Offset,
        [Parameter(Mandatory = $true)][uint64]$Size,
        [Parameter(Mandatory = $true)][uint64]$FileSize,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Assert-Condition ($Offset -le $FileSize) (
        "$Label starts beyond its payload")
    Assert-Condition ($Size -le ($FileSize - $Offset)) (
        "$Label extends beyond its payload")
}

function Get-SegmentSha256 {
    param(
        [Parameter(Mandatory = $true)][IO.FileStream]$Stream,
        [Parameter(Mandatory = $true)][uint64]$Offset,
        [Parameter(Mandatory = $true)][uint64]$Size
    )

    Assert-Condition ($Size -le [int]::MaxValue) (
        "Validation segment is too large")
    [void]$Stream.Seek([int64]$Offset, [IO.SeekOrigin]::Begin)
    [byte[]]$bytes = New-Object byte[] ([int]$Size)
    $read = $Stream.Read($bytes, 0, $bytes.Length)
    Assert-Condition ($read -eq $bytes.Length) (
        "Unable to read canonical payload segment")
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return (
            [BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "")
    } finally {
        $sha.Dispose()
    }
}

$scenePath = Join-Path $RootPath "scene.json"
Assert-Condition (
    (Test-Path -LiteralPath $scenePath -PathType Leaf)) (
    "Canonical scene descriptor is missing: $scenePath")
$rawScene = [IO.File]::ReadAllText($scenePath)
$scene = $rawScene | ConvertFrom-Json

Assert-Condition ($scene.schema_version -eq 1) (
    "Unexpected canonical scene schema")
Assert-Condition ($scene.kind -eq "lgpe_canonical_scene_directory") (
    "Unexpected canonical scene kind")
Assert-Condition (
    $scene.stability -eq "provisional_not_a_frozen_runtime_cache") (
    "Canonical scene stability contract changed")
Assert-Condition ($scene.source_manifest.ingestion.mode -eq "direct_source") (
    "Canonical scene was not cooked directly from source")
Assert-Condition (
    $scene.source_manifest.ingestion.canonical_bridge -eq "none") (
    "A conversion bridge entered the canonical scene")
Assert-Condition ([bool]$scene.source_manifest.validation.passed) (
    "Embedded source-manifest validation failed")
Assert-Condition ($rawScene -notmatch '(?i)\.(dae|glb|pacmdl)"') (
    "A converted model path entered the canonical scene")

$geometryDescriptor = $scene.payloads.geometry
$geometryPath = Resolve-PayloadPath `
    -Root $RootPath `
    -FileName ([string]$geometryDescriptor.file_name)
$geometryFile = Get-Item -LiteralPath $geometryPath
Assert-Condition (
    [uint64]$geometryFile.Length -eq
        [uint64]$geometryDescriptor.size_bytes) (
    "Geometry payload size changed")
Assert-Condition (
    (Get-FileHash -LiteralPath $geometryPath -Algorithm SHA256).Hash -eq
        [string]$geometryDescriptor.sha256) (
    "Geometry payload hash changed")

$geometryStream = [IO.File]::OpenRead($geometryPath)
$geometryReader = New-Object IO.BinaryReader($geometryStream)
try {
    $magic =
        [Text.Encoding]::ASCII.GetString($geometryReader.ReadBytes(8))
    Assert-Condition ($magic -eq "LGPEGEOM") (
        "Geometry payload magic changed")
    Assert-Condition ($geometryReader.ReadUInt32() -eq 1) (
        "Geometry payload schema changed")
    $vertexStride = $geometryReader.ReadUInt32()
    Assert-Condition ($vertexStride -eq 188) (
        "Canonical vertex layout changed")
    Assert-Condition (
        $geometryReader.ReadUInt32() -eq
            [uint32]$scene.source_manifest.scene.mesh_count) (
        "Geometry header mesh count changed")
    [void]$geometryReader.ReadUInt32()
    Assert-Condition (
        $geometryReader.ReadUInt64() -eq
            [uint64]$scene.source_manifest.scene.vertex_count) (
        "Geometry header vertex count changed")
    Assert-Condition (
        $geometryReader.ReadUInt64() -eq
            ([uint64]$scene.source_manifest.scene.
                triangle_record_count * 3)) (
        "Geometry header index count changed")

    $triangleRecords = [uint64]0
    $uniqueTriangleRecords = [uint64]0
    foreach ($mesh in $geometryDescriptor.meshes) {
        $vertexCount = [uint64]$mesh.vertex_count
        Assert-Range `
            -Offset ([uint64]$mesh.decoded_vertices_offset_bytes) `
            -Size ([uint64]$mesh.decoded_vertices_size_bytes) `
            -FileSize ([uint64]$geometryFile.Length) `
            -Label "Mesh '$($mesh.name)' decoded vertices"
        Assert-Condition (
            [uint64]$mesh.decoded_vertices_size_bytes -eq
                ($vertexCount * $vertexStride)) (
            "Mesh '$($mesh.name)' decoded vertex size changed")
        Assert-Range `
            -Offset ([uint64]$mesh.source_raw_vertices_offset_bytes) `
            -Size ([uint64]$mesh.source_raw_vertices_size_bytes) `
            -FileSize ([uint64]$geometryFile.Length) `
            -Label "Mesh '$($mesh.name)' raw vertices"
        Assert-Condition (
            (Get-SegmentSha256 `
                -Stream $geometryStream `
                -Offset ([uint64]$mesh.source_raw_vertices_offset_bytes) `
                -Size ([uint64]$mesh.source_raw_vertices_size_bytes)) -eq
                    [string]$mesh.source_raw_vertices_sha256) (
            "Mesh '$($mesh.name)' raw source bytes changed")

        $triangleKeys =
            New-Object Collections.Generic.HashSet[string] (
                [StringComparer]::Ordinal)
        foreach ($group in $mesh.polygon_groups) {
            $indexCount = [uint64]$group.index_count
            Assert-Condition (($indexCount % 3) -eq 0) (
                "Mesh '$($mesh.name)' has a non-triangle index group")
            Assert-Range `
                -Offset ([uint64]$group.indices_offset_bytes) `
                -Size ([uint64]$group.indices_size_bytes) `
                -FileSize ([uint64]$geometryFile.Length) `
                -Label "Mesh '$($mesh.name)' indices"
            Assert-Condition (
                [uint64]$group.indices_size_bytes -eq ($indexCount * 2)) (
                "Mesh '$($mesh.name)' index size changed")
            [void]$geometryStream.Seek(
                [int64]$group.indices_offset_bytes,
                [IO.SeekOrigin]::Begin)
            for ($index = 0; $index -lt $indexCount; $index += 3) {
                $a = [uint16]$geometryReader.ReadUInt16()
                $b = [uint16]$geometryReader.ReadUInt16()
                $c = [uint16]$geometryReader.ReadUInt16()
                Assert-Condition (
                    $a -lt $vertexCount -and
                    $b -lt $vertexCount -and
                    $c -lt $vertexCount) (
                    "Mesh '$($mesh.name)' contains an out-of-range index")
                $key =
                    "$([uint32]$group.material_index):$a,$b,$c"
                [void]$triangleKeys.Add($key)
                $triangleRecords += 1
            }
        }
        $uniqueTriangleRecords += [uint64]$triangleKeys.Count
    }
    Assert-Condition (
        $triangleRecords -eq
            [uint64]$scene.source_manifest.scene.triangle_record_count) (
        "Canonical triangle-record count changed")
    Assert-Condition (
        $uniqueTriangleRecords -eq
            [uint64]$scene.source_manifest.scene.
                unique_material_indexed_triangle_count) (
        "Canonical unique-triangle count changed")
} finally {
    $geometryReader.Dispose()
    $geometryStream.Dispose()
}

$textureDescriptor = $scene.payloads.textures
$texturePath = Resolve-PayloadPath `
    -Root $RootPath `
    -FileName ([string]$textureDescriptor.file_name)
$textureFile = Get-Item -LiteralPath $texturePath
Assert-Condition (
    [uint64]$textureFile.Length -eq [uint64]$textureDescriptor.size_bytes) (
    "Texture payload size changed")
Assert-Condition (
    (Get-FileHash -LiteralPath $texturePath -Algorithm SHA256).Hash -eq
        [string]$textureDescriptor.sha256) (
    "Texture payload hash changed")

$textureStream = [IO.File]::OpenRead($texturePath)
$textureReader = New-Object IO.BinaryReader($textureStream)
try {
    $magic = [Text.Encoding]::ASCII.GetString($textureReader.ReadBytes(8))
    Assert-Condition ($magic -eq "LGPETEXS") (
        "Texture payload magic changed")
    Assert-Condition ($textureReader.ReadUInt32() -eq 1) (
        "Texture payload schema changed")
    Assert-Condition (
        $textureReader.ReadUInt32() -eq
            [uint32]$scene.source_manifest.scene.required_texture_count) (
        "Texture header count changed")

    $subresourceCount = 0
    foreach ($texture in $textureDescriptor.textures) {
        foreach ($subresource in $texture.subresources) {
            $expectedSize =
                [uint64]$subresource.width *
                [uint64]$subresource.height *
                4
            Assert-Condition (
                [uint64]$subresource.size_bytes -eq $expectedSize) (
                "Texture '$($texture.name)' RGBA8 size changed")
            Assert-Range `
                -Offset ([uint64]$subresource.offset_bytes) `
                -Size ([uint64]$subresource.size_bytes) `
                -FileSize ([uint64]$textureFile.Length) `
                -Label "Texture '$($texture.name)' subresource"
            Assert-Condition (
                (Get-SegmentSha256 `
                    -Stream $textureStream `
                    -Offset ([uint64]$subresource.offset_bytes) `
                    -Size ([uint64]$subresource.size_bytes)) -eq
                        [string]$subresource.sha256) (
                "Texture '$($texture.name)' decoded bytes changed")
            $subresourceCount += 1
        }
    }
    Assert-Condition ($subresourceCount -gt 0) (
        "Canonical scene contains no decoded texture subresources")
} finally {
    $textureReader.Dispose()
    $textureStream.Dispose()
}

Write-Host (
    "[LGPECanonicalValidation] PASS " +
    "meshes=$($scene.source_manifest.scene.mesh_count) " +
    "vertices=$($scene.source_manifest.scene.vertex_count) " +
    "triangle_records=" +
        "$($scene.source_manifest.scene.triangle_record_count) " +
    "textures=$($scene.source_manifest.scene.required_texture_count) " +
    "subresources=$subresourceCount")
