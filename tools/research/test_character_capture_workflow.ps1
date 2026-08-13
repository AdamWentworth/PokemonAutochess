param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$specRelativePath = 'tools/research/captures/sv-eevee-modern-surface-v1.json'
$validator = Join-Path $PSScriptRoot 'validate_character_capture.ps1'
$scaffolder = Join-Path $PSScriptRoot 'new_character_capture_workspace.ps1'
$protector = Join-Path $PSScriptRoot 'protect_character_capture.ps1'
$launcher = Join-Path $PSScriptRoot 'launch_character_capture.ps1'
$schemaPath = Join-Path $PSScriptRoot 'character_capture_schema.json'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-character-capture-test-' + [Guid]::NewGuid().ToString('N'))

try {
    $schema = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
    Assert-Condition ([string]$schema.'$id' -eq
        'pokemon-autochess-character-capture-spec-v1') (
        'Character capture JSON schema has the wrong identity.')

    $summary = & $validator `
        -GameRoot $gameRoot `
        -SpecPath $specRelativePath
    Assert-Condition ([string]$summary.status -eq 'planned' -and
        [int]$summary.states -eq 5 -and
        [int]$summary.captured_states -eq 0) (
        'The checked-in Eevee capture plan did not validate as planned.')

    $workspaceResult = & $scaffolder `
        -GameRoot $gameRoot `
        -SpecPath $specRelativePath `
        -WorkspaceRoot $tempRoot
    $sessionRoot = [string]$workspaceResult.SessionRoot
    Assert-Condition (Test-Path -LiteralPath (
        Join-Path $sessionRoot 'workspace-manifest.json') -PathType Leaf) (
        'Workspace scaffolder did not create its manifest.')
    Assert-Condition (Test-Path -LiteralPath (
        Join-Path $sessionRoot 'protected') -PathType Container) (
        'Workspace scaffolder did not create the protected capture directory.')

    $capturePath = Join-Path $tempRoot 'fixture.rdc'
    [IO.File]::WriteAllBytes(
        $capturePath,
        [byte[]](0x52, 0x44, 0x43, 0x01, 0x02, 0x03, 0x05, 0x08))
    $record = & $protector `
        -CapturePath $capturePath `
        -WorkspaceRoot $sessionRoot `
        -StateId 'regular_neutral_front' `
        -StableChecks 2 `
        -StableIntervalMilliseconds 50
    $protectedPath = Join-Path $sessionRoot ([string]$record.protected_copy)
    Assert-Condition (Test-Path -LiteralPath $protectedPath -PathType Leaf) (
        'Capture protector did not publish the immutable copy.')
    Assert-Condition ((Get-FileHash -LiteralPath $protectedPath -Algorithm SHA256).
        Hash.ToLowerInvariant() -eq [string]$record.sha256) (
        'Protected capture does not match its recorded SHA-256.')
    $lines = @(Get-Content -LiteralPath (
        Join-Path $sessionRoot 'protected\manifest.jsonl'))
    Assert-Condition ($lines.Count -eq 1) (
        'Capture protector did not append exactly one manifest record.')

    $unrecordedProgramRejected = $false
    try {
        & $launcher `
            -WorkspaceRoot $sessionRoot `
            -ProgramPath $capturePath 2>$null | Out-Null
    } catch {
        $unrecordedProgramRejected = $_.Exception.Message -like
            '*program/content SHA-256*'
    }
    Assert-Condition $unrecordedProgramRejected (
        'Launcher did not reject an unrecorded source program identity.')

    $claimedSpecPath = Join-Path $tempRoot 'claimed-spec.json'
    $claimed = Get-Content -LiteralPath (
        Join-Path $gameRoot $specRelativePath) -Raw | ConvertFrom-Json
    $claimed.status = 'captured'
    $claimed.states[0].capture.status = 'captured'
    $claimed.states[0].capture.rdc_file = [string]$record.protected_copy
    $claimed.states[0].capture.rdc_sha256 = [string]$record.sha256
    $claimed.states[0].capture.frame_number = 123
    $claimed.states[0].capture.pokemon_event_ids = @(100, 101)
    $claimed | ConvertTo-Json -Depth 16 |
        Set-Content -LiteralPath $claimedSpecPath -Encoding utf8
    $claimedSummary = & $validator `
        -GameRoot $gameRoot `
        -SpecPath $claimedSpecPath `
        -WorkspaceRoot $sessionRoot
    Assert-Condition ([int]$claimedSummary.captured_states -eq 1) (
        'Validator did not verify the protected captured state.')

    $requiredEvidenceRejected = $false
    try {
        & $validator `
            -GameRoot $gameRoot `
            -SpecPath $claimedSpecPath `
            -WorkspaceRoot $sessionRoot `
            -RequireCapturedEvidence 2>$null | Out-Null
    } catch {
        $requiredEvidenceRejected = $_.Exception.Message -like
            '*Required evidence kind is missing*'
    }
    Assert-Condition $requiredEvidenceRejected (
        'Validator accepted an RDC claim without the required derived evidence.')

    Write-Host '[CharacterCaptureWorkflowTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
