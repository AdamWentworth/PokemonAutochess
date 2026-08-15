[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ShaderStudyRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExporterDll,
    [Parameter(Mandatory = $true)]
    [string]$ShaderDecoderExe,
    [ValidateSet('SV', 'ZA')]
    [string]$SourceKind = 'SV',
    [string]$EvidencePath = '',
    [string]$RegistryPath = '',
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

function Invoke-Checked([scriptblock]$Command, [string]$Description) {
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceConfiguration = if ($SourceKind -eq 'ZA') {
    [pscustomobject]@{
        label = 'Z-A'
        profile = 'pokemon-legends-za-v2.0.0'
        evidence_schema = 'pokemon-autochess-za-kanto-shader-evidence-v1'
        registry_schema = 'pokemon-autochess-za-shader-source-registry-v1'
        manifest_schema = 'pokemon-autochess-private-za-selected-programs-v1'
        evidence_file = 'docs\kanto\evidence\za_kanto_shader_inventory.json'
        registry_file = 'za_kanto_shader_families.json'
    }
} else {
    [pscustomobject]@{
        label = 'SV'
        profile = 'pokemon-scarlet-v3.0.1'
        evidence_schema = 'pokemon-autochess-sv-kanto-shader-evidence-v1'
        registry_schema = 'pokemon-autochess-sv-shader-source-registry-v1'
        manifest_schema = 'pokemon-autochess-private-sv-selected-programs-v1'
        evidence_file = 'docs\kanto\evidence\sv_kanto_shader_inventory.json'
        registry_file = 'sv_kanto_shader_families.json'
    }
}
$ShaderStudyRoot = Resolve-RequiredDirectory $ShaderStudyRoot 'Shader-study directory'
$ExporterDll = Resolve-RequiredFile $ExporterDll 'Trinity exporter'
$ShaderDecoderExe = Resolve-RequiredFile $ShaderDecoderExe 'Maxwell shader decoder'
if ([string]::IsNullOrWhiteSpace($EvidencePath)) {
    $EvidencePath = Join-Path $gameRoot $sourceConfiguration.evidence_file
}
$EvidencePath = Resolve-RequiredFile $EvidencePath "$($sourceConfiguration.label) Kanto shader evidence"
if ([string]::IsNullOrWhiteSpace($RegistryPath)) {
    $RegistryPath = Join-Path $PSScriptRoot $sourceConfiguration.registry_file
}
$RegistryPath = Resolve-RequiredFile $RegistryPath "$($sourceConfiguration.label) shader source registry"

$evidence = Get-Content -LiteralPath $EvidencePath -Raw | ConvertFrom-Json
$registry = Get-Content -LiteralPath $RegistryPath -Raw | ConvertFrom-Json
if ([string]$evidence.schema -ne $sourceConfiguration.evidence_schema -or
    [int]$evidence.summary.unresolved_permutations -ne 0) {
    throw "$($sourceConfiguration.label) Kanto shader evidence is incomplete or unsupported."
}
if ([string]$registry.schema -ne $sourceConfiguration.registry_schema) {
    throw "$($sourceConfiguration.label) shader source registry is unsupported."
}

$registryByFamily = @{}
foreach ($family in @($registry.families)) {
    $registryByFamily[[string]$family.shader_family] = $family
}
$outputRoot = Join-Path $ShaderStudyRoot 'selected-programs'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

# Both command-line tools target .NET 8 on machines that may retain a newer
# runtime only. Roll-forward changes no program bytes or translation output.
$env:DOTNET_ROLL_FORWARD = 'Major'
$records = [Collections.Generic.List[object]]::new()
foreach ($familyEvidence in @($evidence.families | Sort-Object shader_family)) {
    $familyName = [string]$familyEvidence.shader_family
    if (-not $registryByFamily.ContainsKey($familyName)) {
        throw "Shader registry is missing family: $familyName"
    }
    $familyRegistry = $registryByFamily[$familyName]
    $archivePath = Resolve-RequiredFile (
        Join-Path $ShaderStudyRoot ([string]$familyRegistry.archive.file)) (
        "$familyName BNSH archive")
    $actualArchiveHash = (
        Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualArchiveHash -ne [string]$familyEvidence.archive.sha256) {
        throw "$familyName archive SHA-256 does not match promoted evidence."
    }

    $familyOutput = Join-Path $outputRoot ([string]$familyRegistry.file_stem)
    New-Item -ItemType Directory -Path $familyOutput -Force | Out-Null
    foreach ($program in @($familyEvidence.selected_programs |
            Sort-Object variation_index)) {
        $variation = [int]$program.variation_index
        $programName = 'v{0:D4}' -f $variation
        $finalDirectory = Join-Path $familyOutput $programName
        $manifestPath = Join-Path $finalDirectory ($programName + '.json')
        $fragmentGlsl = Join-Path $finalDirectory (
            $programName + '.fsh.maxwell.glsl')
        $vertexGlsl = Join-Path $finalDirectory (
            $programName + '.vsh.maxwell.glsl')
        if (-not $Force -and
            (Test-Path -LiteralPath $manifestPath -PathType Leaf) -and
            (Test-Path -LiteralPath $fragmentGlsl -PathType Leaf) -and
            (Test-Path -LiteralPath $vertexGlsl -PathType Leaf)) {
            $existing = Get-Content -LiteralPath $manifestPath -Raw |
                ConvertFrom-Json
            if ([int]$existing.variation_index -ne $variation -or
                [int]$existing.variation_count -ne
                    [int]$familyEvidence.archive_variation_count) {
                throw "$familyName/$programName retained program identity is stale."
            }
        } else {
            $temporaryDirectory = Join-Path $familyOutput (
                $programName + '.partial-' + [Guid]::NewGuid().ToString('N'))
            try {
                New-Item -ItemType Directory -Path $temporaryDirectory -Force |
                    Out-Null
                $temporaryPrefix = Join-Path $temporaryDirectory $programName
                Invoke-Checked -Description (
                    "$familyName variation $variation extraction") -Command {
                    & dotnet $ExporterDll `
                        --bnsh $archivePath `
                        --variation $variation `
                        --output $temporaryPrefix
                }
                foreach ($stage in @('fsh', 'vsh')) {
                    $maxwell = "$temporaryPrefix.$stage.maxwell"
                    $glsl = "$maxwell.glsl"
                    Invoke-Checked -Description (
                        "$familyName variation $variation $stage decompilation") `
                        -Command { & $ShaderDecoderExe $maxwell $glsl }
                    if (-not (Test-Path -LiteralPath $glsl -PathType Leaf) -or
                        (Get-Item -LiteralPath $glsl).Length -le 0) {
                        throw "$familyName/$programName produced no $stage GLSL."
                    }
                }
                if (Test-Path -LiteralPath $finalDirectory) {
                    Remove-Item -LiteralPath $finalDirectory -Recurse -Force
                }
                Move-Item -LiteralPath $temporaryDirectory `
                    -Destination $finalDirectory
            } finally {
                Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force `
                    -ErrorAction SilentlyContinue
            }
        }

        $records.Add([pscustomobject][ordered]@{
            shader_family = $familyName
            variation_index = $variation
            material_count = [int]$program.material_count
            permutation_count = [int]$program.permutation_count
            archive_sha256 = $actualArchiveHash
            fragment_glsl_sha256 = (
                Get-FileHash -LiteralPath $fragmentGlsl -Algorithm SHA256).Hash.ToLowerInvariant()
            vertex_glsl_sha256 = (
                Get-FileHash -LiteralPath $vertexGlsl -Algorithm SHA256).Hash.ToLowerInvariant()
            directory = "$($familyRegistry.file_stem)/$programName"
        })
    }
}

if ($records.Count -ne [int]$evidence.summary.unique_selected_programs) {
    throw "Extracted $($records.Count) programs; expected $($evidence.summary.unique_selected_programs)."
}
$manifest = [ordered]@{
    schema = $sourceConfiguration.manifest_schema
    source_profile = $sourceConfiguration.profile
    runtime_execution = $false
    emulator_used = $false
    evidence_sha256 = (
        Get-FileHash -LiteralPath $EvidencePath -Algorithm SHA256).Hash.ToLowerInvariant()
    program_count = $records.Count
    programs = @($records)
}
$outputManifest = Join-Path $outputRoot 'selected_programs_manifest.json'
[IO.File]::WriteAllText(
    $outputManifest,
    ($manifest | ConvertTo-Json -Depth 8),
    (New-Object Text.UTF8Encoding($false)))

Write-Host (
    "$($sourceConfiguration.label) Kanto selected programs extracted and decompiled offline: " +
    "$($records.Count) programs -> $outputManifest")
