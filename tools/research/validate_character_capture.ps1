[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SpecPath,
    [string]$GameRoot = '',
    [string]$WorkspaceRoot = '',
    [switch]$RequireCapturedEvidence
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-RelativeSafePath([string]$PathValue, [string]$Label) {
    Assert-Condition (-not [string]::IsNullOrWhiteSpace($PathValue)) (
        "$Label cannot be empty.")
    Assert-Condition (-not [IO.Path]::IsPathRooted($PathValue)) (
        "$Label must be relative: $PathValue")
    $normalized = $PathValue.Replace('\', '/')
    Assert-Condition (
        -not (($normalized -split '/') -contains '..')) (
        "$Label escapes its root: $PathValue")
}

function Resolve-WithinRoot(
    [string]$Root,
    [string]$RelativePath,
    [string]$Label) {
    Assert-RelativeSafePath $RelativePath $Label
    $fullPath = Resolve-FullPath (Join-Path $Root $RelativePath)
    $prefix = (Resolve-FullPath $Root) + [IO.Path]::DirectorySeparatorChar
    Assert-Condition ($fullPath.StartsWith(
        $prefix,
        [StringComparison]::OrdinalIgnoreCase)) (
        "$Label escapes its root: $RelativePath")
    return $fullPath
}

function Assert-StringArrayUnique($Values, [string]$Label) {
    $strings = @($Values | ForEach-Object { [string]$_ })
    Assert-Condition ($strings.Count -gt 0) "$Label cannot be empty."
    Assert-Condition (@($strings | Where-Object {
        [string]::IsNullOrWhiteSpace($_) }).Count -eq 0) (
        "$Label contains an empty value.")
    Assert-Condition (@($strings | Sort-Object -Unique).Count -eq
        $strings.Count) "$Label contains duplicates."
    return $strings
}

function Assert-Sha256([string]$Value, [string]$Label, [bool]$Nullable) {
    if ($Nullable -and [string]::IsNullOrWhiteSpace($Value)) { return }
    Assert-Condition ($Value -match '^[a-fA-F0-9]{64}$') (
        "$Label is not a SHA-256 digest.")
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if (-not [IO.Path]::IsPathRooted($SpecPath)) {
    $SpecPath = Join-Path $GameRoot $SpecPath
}
$SpecPath = Resolve-FullPath $SpecPath
Assert-Condition (Test-Path -LiteralPath $SpecPath -PathType Leaf) (
    "Capture specification is missing: $SpecPath")

$spec = Get-Content -LiteralPath $SpecPath -Raw | ConvertFrom-Json
Assert-Condition ([string]$spec.schema -eq
    'pokemon-autochess-character-capture-spec-v1') (
    'Unsupported character-capture specification schema.')
Assert-Condition ([string]$spec.capture_id -match '^[a-z0-9][a-z0-9-]+$') (
    'capture_id must use lowercase letters, digits, and hyphens.')
$statuses = @('planned', 'captured', 'analyzed', 'qualified', 'rejected')
Assert-Condition ($statuses -contains [string]$spec.status) (
    "Unsupported capture status: $($spec.status)")

Assert-Condition ([int]$spec.subject.species_id -ge 1 -and
    [int]$spec.subject.species_id -le 151) (
    'Character capture subject must be a Kanto species from 001 through 151.')
Assert-Condition ([string]$spec.subject.appearance -in @('regular', 'shiny')) (
    'Subject appearance must be regular or shiny.')
Assert-Sha256 ([string]$spec.canonical_model.source_model_sha256) `
    'canonical_model.source_model_sha256' $false
Assert-Sha256 ([string]$spec.canonical_model.payload_sha256) `
    'canonical_model.payload_sha256' $false

$manifestRelative = [string]$spec.canonical_model.manifest
$manifestPath = Resolve-WithinRoot $GameRoot $manifestRelative (
    'canonical_model.manifest')
Assert-Condition (Test-Path -LiteralPath $manifestPath -PathType Leaf) (
    "Canonical model manifest is missing: $manifestRelative")
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Assert-Condition ([string]$manifest.schema -eq 'phlosion-native-model-ir-v1') (
    "Canonical model is not native IR: $manifestRelative")
Assert-Condition ([IO.Path]::GetFileNameWithoutExtension($manifestPath) -eq
    [string]$spec.canonical_model.stem) (
    'Canonical model stem does not match the manifest filename.')
Assert-Condition ([string]$manifest.source.profile -eq
    [string]$spec.source.content_identity) (
    'Canonical model source profile does not match source.content_identity.')
Assert-Condition ([string]$manifest.source.model_sha256 -eq
    [string]$spec.canonical_model.source_model_sha256) (
    'Canonical source-model SHA-256 does not match the manifest.')
Assert-Condition ([string]$manifest.payload.sha256 -eq
    [string]$spec.canonical_model.payload_sha256) (
    'Canonical payload SHA-256 does not match the manifest.')

$expectedFamilies = Assert-StringArrayUnique `
    $spec.canonical_model.expected_shader_families `
    'canonical_model.expected_shader_families'
$actualFamilies = @($manifest.materials.shader_family | Sort-Object -Unique)
Assert-Condition (@(Compare-Object $expectedFamilies $actualFamilies).Count -eq 0) (
    'Expected shader families do not match the canonical model.')
$expectedMaterials = Assert-StringArrayUnique `
    $spec.canonical_model.expected_materials `
    'canonical_model.expected_materials'
$actualMaterials = @($manifest.materials.name | Sort-Object -Unique)
Assert-Condition (@(Compare-Object $expectedMaterials $actualMaterials).Count -eq 0) (
    'Expected material names do not match the canonical model.')

Assert-Condition ([string]$spec.capture_environment.api -eq 'Vulkan') (
    'Character source captures currently require the Vulkan evidence path.')
Assert-Sha256 ([string]$spec.source.executable_sha256) `
    'source.executable_sha256' $true
Assert-Sha256 ([string]$spec.capture_environment.emulator.executable_sha256) `
    'capture_environment.emulator.executable_sha256' $true
Assert-Sha256 ([string]$spec.capture_environment.renderdoc.executable_sha256) `
    'capture_environment.renderdoc.executable_sha256' $true

$stateIds = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::Ordinal)
$stateStatuses = @('planned', 'captured', 'analyzed', 'rejected')
$capturedStateCount = 0
foreach ($state in @($spec.states)) {
    $stateId = [string]$state.id
    Assert-Condition ($stateId -match '^[a-z0-9][a-z0-9_-]+$') (
        "Invalid state id: $stateId")
    Assert-Condition $stateIds.Add($stateId) "Duplicate state id: $stateId"
    Assert-Condition ([string]$state.appearance -in @('regular', 'shiny')) (
        "State $stateId has an invalid appearance.")
    Assert-Condition ($stateStatuses -contains [string]$state.capture.status) (
        "State $stateId has an invalid capture status.")
    $clip = [string]$state.animation.clip
    Assert-Condition (@($manifest.animations.name) -contains $clip) (
        "State $stateId references a clip absent from the canonical model: $clip")
    if ($null -ne $state.animation.source_frame) {
        $animation = @($manifest.animations | Where-Object name -eq $clip)[0]
        Assert-Condition ([int]$state.animation.source_frame -lt
            [int]$animation.frame_count) (
            "State $stateId source frame is outside the canonical clip.")
    }

    $captureStatus = [string]$state.capture.status
    if ($captureStatus -eq 'planned') {
        Assert-Condition ($null -eq $state.capture.rdc_file -and
            $null -eq $state.capture.rdc_sha256 -and
            $null -eq $state.capture.frame_number -and
            @($state.capture.pokemon_event_ids).Count -eq 0) (
            "Planned state $stateId must not claim capture evidence.")
        continue
    }

    ++$capturedStateCount
    Assert-Sha256 ([string]$state.capture.rdc_sha256) `
        "states.$stateId.capture.rdc_sha256" $false
    Assert-Condition ($null -ne $state.capture.frame_number) (
        "Captured state $stateId is missing a frame number.")
    Assert-Condition (@($state.capture.pokemon_event_ids).Count -gt 0) (
        "Captured state $stateId is missing Pokemon event IDs.")
    if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
        $rdcPath = Resolve-WithinRoot $WorkspaceRoot `
            ([string]$state.capture.rdc_file) "states.$stateId.capture.rdc_file"
        Assert-Condition (Test-Path -LiteralPath $rdcPath -PathType Leaf) (
            "Captured RDC is missing for state $stateId.")
        $actualHash = (Get-FileHash -LiteralPath $rdcPath -Algorithm SHA256).
            Hash.ToLowerInvariant()
        Assert-Condition ($actualHash -eq
            ([string]$state.capture.rdc_sha256).ToLowerInvariant()) (
            "Captured RDC SHA-256 mismatch for state $stateId.")
    }
}
Assert-Condition ($stateIds.Count -gt 0) 'Capture specification has no states.'

$requiredEvidence = Assert-StringArrayUnique $spec.required_evidence (
    'required_evidence')
$artifactKeys = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($artifact in @($spec.evidence.artifacts)) {
    Assert-RelativeSafePath ([string]$artifact.path) 'evidence artifact path'
    Assert-Sha256 ([string]$artifact.sha256) 'evidence artifact SHA-256' $false
    if ($null -ne $artifact.state_id) {
        Assert-Condition $stateIds.Contains([string]$artifact.state_id) (
            "Evidence artifact references an unknown state: $($artifact.state_id)")
    }
    $artifactKey = '{0}|{1}' -f [string]$artifact.kind, [string]$artifact.path
    Assert-Condition $artifactKeys.Add($artifactKey) (
        "Duplicate evidence artifact: $artifactKey")
    if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
        $artifactPath = Resolve-WithinRoot $WorkspaceRoot `
            ([string]$artifact.path) 'evidence artifact path'
        Assert-Condition (Test-Path -LiteralPath $artifactPath -PathType Leaf) (
            "Evidence artifact is missing: $($artifact.path)")
        $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).
            Hash.ToLowerInvariant()
        Assert-Condition ($actualHash -eq
            ([string]$artifact.sha256).ToLowerInvariant()) (
            "Evidence artifact SHA-256 mismatch: $($artifact.path)")
    }
}

$questionIds = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::Ordinal)
foreach ($question in @($spec.evidence.open_questions)) {
    Assert-Condition $questionIds.Add([string]$question.id) (
        "Duplicate open-question id: $($question.id)")
    Assert-Condition ([string]$question.status -in @(
        'open', 'answered', 'blocked')) (
        "Open question $($question.id) has an invalid status.")
    Assert-StringArrayUnique $question.evidence_needed (
        "open_questions.$($question.id).evidence_needed") | Out-Null
    if ([string]$question.status -eq 'answered') {
        Assert-Condition (-not [string]::IsNullOrWhiteSpace(
            [string]$question.answer)) (
            "Answered question $($question.id) has no answer.")
    }
}

$findingIds = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::Ordinal)
$evidenceLevels = @(
    'gpu_capture',
    'controlled_reference_render',
    'source_parameter_mapping',
    'source_payload',
    'visual_approximation',
    'unknown')
foreach ($finding in @($spec.evidence.findings)) {
    Assert-Condition $findingIds.Add([string]$finding.id) (
        "Duplicate finding id: $($finding.id)")
    Assert-Condition ($evidenceLevels -contains
        [string]$finding.evidence_level) (
        "Finding $($finding.id) has an invalid evidence level.")
    if ([string]$finding.evidence_level -in @(
            'gpu_capture', 'controlled_reference_render')) {
        Assert-Condition (@($finding.artifact_paths).Count -gt 0) (
            "Finding $($finding.id) claims captured evidence without artifacts.")
    }
}

if ([string]$spec.status -eq 'planned') {
    Assert-Condition ($capturedStateCount -eq 0) (
        'A planned specification cannot contain captured states.')
    Assert-Condition (@($spec.evidence.artifacts).Count -eq 0) (
        'A planned specification cannot claim evidence artifacts.')
} else {
    Assert-Condition ($capturedStateCount -gt 0) (
        'A non-planned specification must contain captured state evidence.')
}

if ($RequireCapturedEvidence) {
    Assert-Condition ([string]$spec.status -in @(
        'captured', 'analyzed', 'qualified')) (
        'Captured evidence was required, but the specification is not captured.')
    Assert-Condition ($capturedStateCount -gt 0) (
        'Captured evidence was required, but no state has an RDC.')
    foreach ($kind in $requiredEvidence) {
        Assert-Condition (@($spec.evidence.artifacts |
            Where-Object kind -eq $kind).Count -gt 0) (
            "Required evidence kind is missing: $kind")
    }
}

$summary = [pscustomobject][ordered]@{
    capture_id = [string]$spec.capture_id
    status = [string]$spec.status
    subject = '{0:000} {1}' -f
        [int]$spec.subject.species_id,
        [string]$spec.subject.species_name
    states = $stateIds.Count
    captured_states = $capturedStateCount
    required_evidence_kinds = $requiredEvidence.Count
    evidence_artifacts = @($spec.evidence.artifacts).Count
    open_questions = @($spec.evidence.open_questions |
        Where-Object status -eq 'open').Count
    findings = @($spec.evidence.findings).Count
}

Write-Host (
    'Character capture specification valid: {0}, status={1}, states={2}, captured={3}, artifacts={4}.' -f
        $summary.capture_id,
        $summary.status,
        $summary.states,
        $summary.captured_states,
        $summary.evidence_artifacts)
$summary
