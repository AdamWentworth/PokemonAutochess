[CmdletBinding()]
param(
    [string]$GameFilesRoot = "\\TNAS-98B9\pokemon\Game Files\Switch\Pokemon_Legends_ZA_v2.0.0_Merged_GameFiles",
    [string]$HashListPath = "D:\ProjectData\Games\PokemonAutochess\Assets\pokemon-autochess\source\gamefreak\pokemon-legends-za\v2.0.0\hashes_inside_fd.txt",
    [string]$ShaderStudyRoot = "D:\ProjectData\Games\PokemonAutochess\Assets\pokemon-autochess\derived\shader-study\za-v2.0.0",
    [string]$ExporterDll = "D:\DevTools\ThirdParty\PokemonScarlet\gftool\TrinityBatchExporter\bin\Release\net8.0-windows7.0\TrinityBatchExporter.dll",
    [string]$RegistryPath = "",
    [string]$InventoryOutput = "",
    [string]$EvidenceOutput = "",
    [string]$CensusOutput = "",
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-RequiredFile([string]$Value, [string]$Label) {
    $path = [IO.Path]::GetFullPath($Value)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "$Label is missing: $path"
    }
    return $path
}

function Resolve-RequiredDirectory([string]$Value, [string]$Label) {
    $path = [IO.Path]::GetFullPath($Value).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "$Label is missing: $path"
    }
    return $path
}

function Get-Sha256([string]$PathValue) {
    return (Get-FileHash -LiteralPath $PathValue -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Publish-File([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        if ((Get-Sha256 $Source) -eq (Get-Sha256 $Destination)) {
            return $false
        }
        if (-not $Force) {
            throw "Private shader source differs; pass -Force to replace it: $Destination"
        }
    }
    $partial = $Destination + '.partial-' + [Guid]::NewGuid().ToString('N')
    try {
        Copy-Item -LiteralPath $Source -Destination $partial -Force
        Move-Item -LiteralPath $partial -Destination $Destination -Force
    } finally {
        Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
    }
    return $true
}

function Invoke-Exporter([string[]]$Arguments, [string]$Description) {
    & dotnet $ExporterDll @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Decode-Metadata([string]$InputPath, [string]$OutputPath, [string]$Description) {
    $partial = $OutputPath + '.partial-' + [Guid]::NewGuid().ToString('N')
    try {
        Invoke-Exporter @('--trsha', $InputPath, '--output', $partial) $Description
        if (-not (Test-Path -LiteralPath $partial -PathType Leaf) -or
            (Get-Item -LiteralPath $partial).Length -le 0) {
            throw "$Description produced no output: $partial"
        }
        Move-Item -LiteralPath $partial -Destination $OutputPath -Force
    } finally {
        Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
    }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$GameFilesRoot = Resolve-RequiredDirectory $GameFilesRoot 'Z-A extracted game-files directory'
$packageRoot = Resolve-RequiredDirectory (Join-Path $GameFilesRoot 'arc') 'Z-A TRPAK package directory'
$HashListPath = Resolve-RequiredFile $HashListPath 'Z-A filesystem hash list'
$ExporterDll = Resolve-RequiredFile $ExporterDll 'Trinity exporter'
if ([string]::IsNullOrWhiteSpace($RegistryPath)) {
    $RegistryPath = Join-Path $PSScriptRoot 'za_kanto_shader_families.json'
}
$RegistryPath = Resolve-RequiredFile $RegistryPath 'Z-A shader source registry'
$analyzer = Resolve-RequiredFile (
    Join-Path $PSScriptRoot 'analyze_za_kanto_shader_permutations.py') (
    'Z-A Kanto shader analyzer')
$ShaderStudyRoot = [IO.Path]::GetFullPath($ShaderStudyRoot).TrimEnd('\', '/')
New-Item -ItemType Directory -Path $ShaderStudyRoot -Force | Out-Null
if ([string]::IsNullOrWhiteSpace($InventoryOutput)) {
    $InventoryOutput = Join-Path $ShaderStudyRoot 'za_kanto_shader_inventory.json'
}
if ([string]::IsNullOrWhiteSpace($EvidenceOutput)) {
    $EvidenceOutput = Join-Path $gameRoot 'docs\kanto\evidence\za_kanto_shader_inventory.json'
}
if ([string]::IsNullOrWhiteSpace($CensusOutput)) {
    $CensusOutput = Join-Path $gameRoot 'docs\kanto\evidence\za_kanto_material_census.json'
}

$registry = Get-Content -LiteralPath $RegistryPath -Raw | ConvertFrom-Json
if ([string]$registry.schema -ne 'pokemon-autochess-za-shader-source-registry-v1' -or
    [string]$registry.source_profile -ne 'pokemon-legends-za-v2.0.0') {
    throw 'Unsupported Z-A shader source registry.'
}
$expectedHashList = [string]$registry.source_index.sha256
$actualHashList = (Get-FileHash -LiteralPath $HashListPath -Algorithm SHA256).Hash
if ($actualHashList -ne $expectedHashList) {
    throw "Z-A hash-list identity mismatch: expected $expectedHashList, got $actualHashList"
}

$hashNames = @{}
foreach ($line in [IO.File]::ReadLines($HashListPath)) {
    if ($line -match '^0x([0-9A-Fa-f]{16})\s+(.+)$') {
        $hashNames[$matches[1].ToUpperInvariant()] = $matches[2].Replace('\', '/')
    }
}

$env:DOTNET_ROLL_FORWARD = 'Major'
$sourceRecords = [Collections.Generic.List[object]]::new()
foreach ($family in @($registry.families | Sort-Object shader_family)) {
    $familyName = [string]$family.shader_family
    foreach ($kind in @('archive', 'metadata')) {
        $source = $family.$kind
        $hash = ([string]$source.romfs_hash).Substring(2).ToUpperInvariant()
        if (-not $hashNames.ContainsKey($hash) -or
            [string]$hashNames[$hash] -ne [string]$source.romfs_path) {
            throw "$familyName $kind hash/path identity is absent from the registered hash list."
        }
        $packagePath = Resolve-RequiredDirectory (
            Join-Path $packageRoot ([string]$source.source_package)) (
            "$familyName $kind source package")
        $matches = @(Get-ChildItem -LiteralPath $packagePath -File | Where-Object {
            $_.Name -match ("- " + [regex]::Escape($hash) + "\.")
        })
        if ($matches.Count -ne 1) {
            throw "$familyName $kind resolved $($matches.Count) source files for hash 0x$hash."
        }
        $destination = Join-Path $ShaderStudyRoot ([string]$source.file)
        $copied = Publish-File $matches[0].FullName $destination
        $sourceRecords.Add([pscustomobject][ordered]@{
            shader_family = $familyName
            kind = $kind
            romfs_path = [string]$source.romfs_path
            romfs_hash = [string]$source.romfs_hash
            source_package = [string]$source.source_package
            source_file = $matches[0].Name
            output_file = [string]$source.file
            bytes = (Get-Item -LiteralPath $destination).Length
            sha256 = Get-Sha256 $destination
            copied_this_run = [bool]$copied
        })
    }

    $metadataPath = Join-Path $ShaderStudyRoot ([string]$family.metadata.file)
    $decodedPath = Join-Path $ShaderStudyRoot ([string]$family.metadata.decoded_file)
    if ($Force -or -not (Test-Path -LiteralPath $decodedPath -PathType Leaf)) {
        Decode-Metadata $metadataPath $decodedPath "$familyName shader metadata decode"
    }
    $decoded = Get-Content -LiteralPath $decodedPath -Raw | ConvertFrom-Json
    if ([string]$decoded.file_name -ne [string]$family.archive.file) {
        throw "$familyName decoded metadata names $($decoded.file_name), expected $($family.archive.file)."
    }
}

& python $analyzer `
    --game-root $gameRoot `
    --registry $RegistryPath `
    --shader-study $ShaderStudyRoot `
    --output $InventoryOutput `
    --evidence-output $EvidenceOutput `
    --census-output $CensusOutput `
    --require-complete-source `
    --require-exact-resolution
if ($LASTEXITCODE -ne 0) {
    throw "Z-A Kanto shader inventory validation failed with exit code $LASTEXITCODE."
}

$manifest = [ordered]@{
    schema = 'pokemon-autochess-private-za-shader-study-v1'
    source_profile = 'pokemon-legends-za-v2.0.0'
    runtime_execution = $false
    emulator_used = $false
    hash_list_sha256 = $actualHashList.ToLowerInvariant()
    registry_sha256 = Get-Sha256 $RegistryPath
    inventory_file = [IO.Path]::GetFileName($InventoryOutput)
    sources = @($sourceRecords)
}
$manifestPath = Join-Path $ShaderStudyRoot 'za_kanto_shader_source_manifest.json'
[IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 8),
    (New-Object Text.UTF8Encoding($false)))

Write-Host (
    "Z-A Kanto shader sources staged and exactly resolved offline: " +
    "$(@($registry.families).Count) families -> $InventoryOutput")
