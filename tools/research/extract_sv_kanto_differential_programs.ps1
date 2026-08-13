[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ShaderStudyRoot,
    [Parameter(Mandatory = $true)]
    [string]$PlanPath,
    [Parameter(Mandatory = $true)]
    [string]$ExporterDll,
    [Parameter(Mandatory = $true)]
    [string]$ShaderDecoderExe,
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

function Invoke-Checked([scriptblock]$Command, [string]$Description) {
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$ShaderStudyRoot = [IO.Path]::GetFullPath($ShaderStudyRoot).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $ShaderStudyRoot -PathType Container)) {
    throw "Shader-study directory is missing: $ShaderStudyRoot"
}
$PlanPath = Resolve-RequiredFile $PlanPath 'SV differential plan'
$ExporterDll = Resolve-RequiredFile $ExporterDll 'Trinity exporter'
$ShaderDecoderExe = Resolve-RequiredFile $ShaderDecoderExe (
    'Maxwell shader decoder')
if ([string]::IsNullOrWhiteSpace($EvidencePath)) {
    $EvidencePath = Join-Path $gameRoot (
        'docs\kanto\evidence\sv_kanto_shader_inventory.json')
}
$EvidencePath = Resolve-RequiredFile $EvidencePath 'SV Kanto shader evidence'
if ([string]::IsNullOrWhiteSpace($RegistryPath)) {
    $RegistryPath = Join-Path $PSScriptRoot 'sv_kanto_shader_families.json'
}
$RegistryPath = Resolve-RequiredFile $RegistryPath 'SV shader source registry'

$plan = Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
$evidence = Get-Content -LiteralPath $EvidencePath -Raw | ConvertFrom-Json
$registry = Get-Content -LiteralPath $RegistryPath -Raw | ConvertFrom-Json
if ([string]$plan.schema -ne
    'pokemon-autochess-sv-kanto-program-differential-plan-v1' -or
    @($plan.differentials).Count -eq 0) {
    throw 'SV differential plan is empty or unsupported.'
}
if ([string]$evidence.schema -ne
    'pokemon-autochess-sv-kanto-shader-evidence-v1') {
    throw 'SV Kanto shader evidence is unsupported.'
}

$registryByFamily = @{}
foreach ($family in @($registry.families)) {
    $registryByFamily[[string]$family.shader_family] = $family
}
$evidenceByFamily = @{}
foreach ($family in @($evidence.families)) {
    $evidenceByFamily[[string]$family.shader_family] = $family
}

$comparisons = @(
    $plan.differentials |
        Select-Object shader_family, comparison_variation -Unique |
        Sort-Object shader_family, comparison_variation)
$outputRoot = Join-Path $ShaderStudyRoot 'differential-programs'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$env:DOTNET_ROLL_FORWARD = 'Major'
$records = [Collections.Generic.List[object]]::new()

foreach ($comparison in $comparisons) {
    $familyName = [string]$comparison.shader_family
    $variation = [int]$comparison.comparison_variation
    if (-not $registryByFamily.ContainsKey($familyName) -or
        -not $evidenceByFamily.ContainsKey($familyName)) {
        throw "Missing source identity for differential family: $familyName"
    }
    $familyRegistry = $registryByFamily[$familyName]
    $familyEvidence = $evidenceByFamily[$familyName]
    $archivePath = Resolve-RequiredFile (
        Join-Path $ShaderStudyRoot ([string]$familyRegistry.archive.file)) (
        "$familyName BNSH archive")
    $archiveHash = (
        Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($archiveHash -ne [string]$familyEvidence.archive.sha256) {
        throw "$familyName archive SHA-256 does not match promoted evidence."
    }

    $programName = 'v{0:D4}' -f $variation
    $familyOutput = Join-Path $outputRoot ([string]$familyRegistry.file_stem)
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
            throw "$familyName/$programName retained comparison is stale."
        }
    } else {
        New-Item -ItemType Directory -Path $familyOutput -Force | Out-Null
        $temporaryDirectory = Join-Path $familyOutput (
            $programName + '.partial-' + [Guid]::NewGuid().ToString('N'))
        try {
            New-Item -ItemType Directory -Path $temporaryDirectory -Force |
                Out-Null
            $temporaryPrefix = Join-Path $temporaryDirectory $programName
            Invoke-Checked -Description (
                "$familyName comparison $variation extraction") -Command {
                & dotnet $ExporterDll `
                    --bnsh $archivePath `
                    --variation $variation `
                    --output $temporaryPrefix
            }
            foreach ($stage in @('fsh', 'vsh')) {
                $maxwell = "$temporaryPrefix.$stage.maxwell"
                $glsl = "$maxwell.glsl"
                Invoke-Checked -Description (
                    "$familyName comparison $variation $stage decompilation") `
                    -Command { & $ShaderDecoderExe $maxwell $glsl }
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
        archive_sha256 = $archiveHash
        fragment_glsl_sha256 = (
            Get-FileHash -LiteralPath $fragmentGlsl -Algorithm SHA256).Hash.ToLowerInvariant()
        vertex_glsl_sha256 = (
            Get-FileHash -LiteralPath $vertexGlsl -Algorithm SHA256).Hash.ToLowerInvariant()
        directory = "$($familyRegistry.file_stem)/$programName"
    })
}

if ($records.Count -ne [int]$plan.summary.unique_comparison_programs) {
    throw "Extracted $($records.Count) comparisons; expected $($plan.summary.unique_comparison_programs)."
}
$manifest = [ordered]@{
    schema = 'pokemon-autochess-private-sv-differential-programs-v1'
    source_profile = 'pokemon-scarlet-v3.0.1'
    runtime_execution = $false
    emulator_used = $false
    plan_sha256 = (
        Get-FileHash -LiteralPath $PlanPath -Algorithm SHA256).Hash.ToLowerInvariant()
    program_count = $records.Count
    programs = @($records)
}
$outputManifest = Join-Path $outputRoot 'differential_programs_manifest.json'
[IO.File]::WriteAllText(
    $outputManifest,
    ($manifest | ConvertTo-Json -Depth 8),
    (New-Object Text.UTF8Encoding($false)))

Write-Host (
    "SV Kanto differential programs extracted offline: " +
    "$($records.Count) comparisons -> $outputManifest")
