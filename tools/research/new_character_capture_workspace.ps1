[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SpecPath,
    [Parameter(Mandatory = $true)]
    [string]$WorkspaceRoot,
    [string]$GameRoot = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Write-Utf8Text([string]$PathValue, [string]$TextValue) {
    [IO.File]::WriteAllText(
        $PathValue,
        $TextValue,
        (New-Object Text.UTF8Encoding($false)))
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if (-not [IO.Path]::IsPathRooted($SpecPath)) {
    $SpecPath = Join-Path $GameRoot $SpecPath
}
$SpecPath = Resolve-FullPath $SpecPath
$WorkspaceRoot = Resolve-FullPath $WorkspaceRoot

& (Join-Path $PSScriptRoot 'validate_character_capture.ps1') `
    -SpecPath $SpecPath `
    -GameRoot $GameRoot | Out-Null

$spec = Get-Content -LiteralPath $SpecPath -Raw | ConvertFrom-Json
$sessionRoot = Join-Path $WorkspaceRoot ([string]$spec.capture_id)
$sessionRoot = Resolve-FullPath $sessionRoot
$workspacePrefix = $WorkspaceRoot + [IO.Path]::DirectorySeparatorChar
if (-not $sessionRoot.StartsWith(
        $workspacePrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Capture workspace escaped the requested root.'
}
if (Test-Path -LiteralPath $sessionRoot) {
    if (-not $Force) {
        throw "Character capture workspace already exists: $sessionRoot"
    }
} else {
    New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null
}

foreach ($relative in @(
        'captures',
        'protected',
        'analysis',
        'evidence',
        'shaders',
        'buffers',
        'textures',
        'targets',
        'logs')) {
    New-Item -ItemType Directory -Path (
        Join-Path $sessionRoot $relative) -Force | Out-Null
}

$specCopyPath = Join-Path $sessionRoot 'capture-spec.json'
Copy-Item -LiteralPath $SpecPath -Destination $specCopyPath -Force
$specCopyHash = (Get-FileHash -LiteralPath $specCopyPath -Algorithm SHA256).
    Hash.ToLowerInvariant()

$sourceManifest = Get-Content -LiteralPath (
    Join-Path $GameRoot ([string]$spec.canonical_model.manifest)) -Raw |
    ConvertFrom-Json
$sourceDirectory = Split-Path -Parent ([string]$sourceManifest.source.model)
$sourceDirectoryExists = Test-Path -LiteralPath $sourceDirectory -PathType Container
$emulatorPath = [string]$spec.capture_environment.emulator.executable
$renderdocPath = [string]$spec.capture_environment.renderdoc.executable

$readme = [Collections.Generic.List[string]]::new()
$readme.Add(('# Character Capture: {0}' -f [string]$spec.capture_id))
$readme.Add('')
$readme.Add(('Status: `{0}`' -f [string]$spec.status))
$readme.Add('')
$readme.Add(('Subject: `#{0:000} {1}` (`{2}`)' -f
    [int]$spec.subject.species_id,
    [string]$spec.subject.species_name,
    [string]$spec.canonical_model.stem))
$readme.Add('')
$readme.Add('This private workspace stores source-game GPU captures and derived research evidence. It must not be committed or synchronized to a public remote.')
$readme.Add('')
$readme.Add('## State Queue')
$readme.Add('')
$readme.Add('| State | Appearance | Clip | Source frame | Capture status |')
$readme.Add('| --- | --- | --- | ---: | --- |')
foreach ($state in @($spec.states)) {
    $frame = if ($null -eq $state.animation.source_frame) {
        'TBD'
    } else {
        [string]$state.animation.source_frame
    }
    $readme.Add(('| {0} | {1} | {2} | {3} | {4} |' -f
        [string]$state.id,
        [string]$state.appearance,
        [string]$state.animation.clip,
        $frame,
        [string]$state.capture.status))
}
$readme.Add('')
$readme.Add('## Evidence Rule')
$readme.Add('')
$readme.Add('Do not change the specification from `planned` until an immutable RDC has been copied into `protected/`, SHA-256 verified, and its Pokemon draw event IDs have been identified. Every derived artifact must record its own hash, state ID, and source event ID where applicable.')
Write-Utf8Text (Join-Path $sessionRoot 'README.md') (
    ($readme -join [Environment]::NewLine) + [Environment]::NewLine)

$manifest = [ordered]@{
    schema = 'pokemon-autochess-character-capture-workspace-v1'
    created_utc = [DateTime]::UtcNow.ToString('o')
    capture_id = [string]$spec.capture_id
    capture_spec = 'capture-spec.json'
    capture_spec_sha256 = $specCopyHash
    game_root = $GameRoot
    workspace_root = $sessionRoot
    source = [ordered]@{
        content_identity = [string]$spec.source.content_identity
        canonical_manifest = [string]$spec.canonical_model.manifest
        canonical_payload_sha256 = [string]$spec.canonical_model.payload_sha256
        source_resource_directory = $sourceDirectory
        source_resource_directory_exists = $sourceDirectoryExists
    }
    tools = [ordered]@{
        emulator = $emulatorPath
        emulator_exists = Test-Path -LiteralPath $emulatorPath -PathType Leaf
        renderdoc = $renderdocPath
        renderdoc_exists = Test-Path -LiteralPath $renderdocPath -PathType Leaf
    }
    source_game_launchable = $false
    blockers = @(
        'No launchable source-game program/content identity is recorded in the planned specification.',
        'No source GPU capture has been acquired or analyzed yet.'
    )
}
Write-Utf8Text (Join-Path $sessionRoot 'workspace-manifest.json') (
    ($manifest | ConvertTo-Json -Depth 10) + [Environment]::NewLine)

Write-Host "Character capture workspace ready: $sessionRoot"
[pscustomobject]@{
    SessionRoot = $sessionRoot
    SpecPath = $specCopyPath
    SourceResourceDirectoryExists = $sourceDirectoryExists
    EmulatorExists = $manifest.tools.emulator_exists
    RenderDocExists = $manifest.tools.renderdoc_exists
    SourceGameLaunchable = $false
}
