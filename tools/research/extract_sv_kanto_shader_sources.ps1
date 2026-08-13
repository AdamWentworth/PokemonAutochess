[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RomfsRoot,
    [Parameter(Mandatory = $true)]
    [string]$ShaderStudyRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExporterDll,
    [string]$OodleDecoder = '',
    [string]$RegistryPath = '',
    [string]$InventoryOutput = '',
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

function Invoke-Exporter([string[]]$Arguments, [string]$Description) {
    & dotnet $ExporterDll @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Invoke-AtomicExport(
    [string]$OutputPath,
    [string[]]$Arguments,
    [string]$Description) {
    $temporaryPath = $OutputPath + '.partial-' + [Guid]::NewGuid().ToString('N')
    try {
        Invoke-Exporter `
            -Arguments @($Arguments + @('--output', $temporaryPath)) `
            -Description $Description
        if (-not (Test-Path -LiteralPath $temporaryPath -PathType Leaf) -or
            (Get-Item -LiteralPath $temporaryPath).Length -le 0) {
            throw "$Description produced no output: $temporaryPath"
        }
        Move-Item -LiteralPath $temporaryPath -Destination $OutputPath -Force
    } finally {
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
    }
}

function Get-OptionWordCount($Slots) {
    $wordCount = 1
    $previousSlotIndex = -1
    foreach ($slot in @($Slots)) {
        $slotIndex = [int]$slot.slot_index
        if ($previousSlotIndex -ge 0 -and $slotIndex -le $previousSlotIndex) {
            $wordCount++
        }
        $previousSlotIndex = $slotIndex
    }
    return $wordCount
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$RomfsRoot = Resolve-RequiredDirectory $RomfsRoot 'Scarlet RomFS directory'
$ExporterDll = Resolve-RequiredFile $ExporterDll 'Trinity exporter'
if (-not [string]::IsNullOrWhiteSpace($OodleDecoder)) {
    $OodleDecoder = Resolve-RequiredFile $OodleDecoder 'Oodle decoder'
}
$descriptorPath = Resolve-RequiredFile (
    Join-Path $RomfsRoot 'arc\data.trpfd') 'Scarlet RomFS descriptor'
$fileSystemPath = Resolve-RequiredFile (
    Join-Path $RomfsRoot 'arc\data.trpfs') 'Scarlet RomFS filesystem'
if ([string]::IsNullOrWhiteSpace($RegistryPath)) {
    $RegistryPath = Join-Path $PSScriptRoot 'sv_kanto_shader_families.json'
}
$RegistryPath = Resolve-RequiredFile $RegistryPath 'SV shader source registry'
$analyzer = Resolve-RequiredFile (
    Join-Path $PSScriptRoot 'analyze_sv_kanto_shader_permutations.py') (
    'SV Kanto shader analyzer')

$ShaderStudyRoot = [IO.Path]::GetFullPath($ShaderStudyRoot).TrimEnd('\', '/')
New-Item -ItemType Directory -Path $ShaderStudyRoot -Force | Out-Null
if ([string]::IsNullOrWhiteSpace($InventoryOutput)) {
    $InventoryOutput = Join-Path $ShaderStudyRoot 'sv_kanto_shader_inventory.json'
} else {
    $InventoryOutput = [IO.Path]::GetFullPath($InventoryOutput)
}

$registry = Get-Content -LiteralPath $RegistryPath -Raw | ConvertFrom-Json
if ([string]$registry.schema -ne
    'pokemon-autochess-sv-shader-source-registry-v1' -or
    [string]$registry.source_profile -ne 'pokemon-scarlet-v3.0.1') {
    throw 'Unsupported SV shader source registry.'
}

# The exporter targets .NET 8 while this workstation may carry a newer runtime
# only. Host roll-forward changes no source input or decoded output.
$env:DOTNET_ROLL_FORWARD = 'Major'

$sourceRecords = [Collections.Generic.List[object]]::new()
foreach ($family in @($registry.families | Sort-Object shader_family)) {
    $familyName = [string]$family.shader_family
    $archivePath = Join-Path $ShaderStudyRoot ([string]$family.archive.file)
    $metadataPath = Join-Path $ShaderStudyRoot ([string]$family.metadata.file)
    $decodedPath = Join-Path $ShaderStudyRoot ([string]$family.metadata.decoded_file)
    $archiveExtracted = $false
    $metadataExtracted = $false

    if ($Force -or -not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        $archiveArguments = @(
            '--romfs', $RomfsRoot,
            '--file-hash', [string]$family.archive.romfs_hash)
        if (-not [string]::IsNullOrWhiteSpace($OodleDecoder)) {
            $archiveArguments += @('--oodle-decoder', $OodleDecoder)
        }
        Invoke-AtomicExport `
            -OutputPath $archivePath `
            -Arguments $archiveArguments `
            -Description "$familyName shader archive extraction"
        $archiveExtracted = $true
    }
    if ($Force -or -not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
        $metadataArguments = @(
            '--romfs', $RomfsRoot,
            '--file-hash', [string]$family.metadata.romfs_hash)
        if (-not [string]::IsNullOrWhiteSpace($OodleDecoder)) {
            $metadataArguments += @('--oodle-decoder', $OodleDecoder)
        }
        Invoke-AtomicExport `
            -OutputPath $metadataPath `
            -Arguments $metadataArguments `
            -Description "$familyName shader metadata extraction"
        $metadataExtracted = $true
    }
    if ($Force -or $metadataExtracted -or
        -not (Test-Path -LiteralPath $decodedPath -PathType Leaf)) {
        Invoke-AtomicExport `
            -OutputPath $decodedPath `
            -Arguments @('--trsha', $metadataPath) `
            -Description "$familyName shader metadata decode"
    }

    $decoded = Get-Content -LiteralPath $decodedPath -Raw | ConvertFrom-Json
    if ([string]$decoded.name -ne $familyName -or
        [string]$decoded.file_name -ne [string]$family.archive.file) {
        throw "$familyName decoded metadata identity does not match the registry."
    }
    $wordsPerVariation =
        (Get-OptionWordCount $decoded.shader_param) +
        (Get-OptionWordCount $decoded.global_param)
    if (@($decoded.param_buffer).Count -eq 0 -or
        @($decoded.param_buffer).Count % $wordsPerVariation -ne 0) {
        throw "$familyName decoded metadata has an invalid variation table."
    }

    $sourceRecords.Add([pscustomobject][ordered]@{
        shader_family = $familyName
        archive_file = [string]$family.archive.file
        archive_sha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        metadata_file = [string]$family.metadata.file
        metadata_sha256 = (Get-FileHash -LiteralPath $metadataPath -Algorithm SHA256).Hash.ToLowerInvariant()
        decoded_file = [string]$family.metadata.decoded_file
        variation_count = [int](@($decoded.param_buffer).Count / $wordsPerVariation)
        parameter_words_per_variation = $wordsPerVariation
        archive_extracted_this_run = $archiveExtracted
        metadata_extracted_this_run = $metadataExtracted
    })
}

& python $analyzer `
    --game-root $gameRoot `
    --registry $RegistryPath `
    --shader-study $ShaderStudyRoot `
    --output $InventoryOutput `
    --require-complete-source `
    --require-exact-resolution
if ($LASTEXITCODE -ne 0) {
    throw "SV Kanto shader inventory validation failed with exit code $LASTEXITCODE."
}

$manifestPath = Join-Path $ShaderStudyRoot 'sv_kanto_shader_source_manifest.json'
$manifest = [ordered]@{
    schema = 'pokemon-autochess-private-sv-shader-study-v1'
    source_profile = 'pokemon-scarlet-v3.0.1'
    runtime_execution = $false
    emulator_used = $false
    romfs_descriptor_sha256 = (
        Get-FileHash -LiteralPath $descriptorPath -Algorithm SHA256).Hash.ToLowerInvariant()
    romfs_filesystem_bytes = (Get-Item -LiteralPath $fileSystemPath).Length
    registry_sha256 = (
        Get-FileHash -LiteralPath $RegistryPath -Algorithm SHA256).Hash.ToLowerInvariant()
    inventory_file = [IO.Path]::GetFileName($InventoryOutput)
    sources = @($sourceRecords)
}
[IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 8),
    (New-Object Text.UTF8Encoding($false)))

Write-Host (
    "SV Kanto shader sources staged and exactly resolved offline: " +
    "$($sourceRecords.Count) families -> $InventoryOutput")
