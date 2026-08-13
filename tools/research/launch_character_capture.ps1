[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ProgramPath,
    [string[]]$ProgramArguments = @(),
    [switch]$AllowLaunch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

$WorkspaceRoot = Resolve-FullPath $WorkspaceRoot
$ProgramPath = Resolve-FullPath $ProgramPath
$workspaceManifestPath = Join-Path $WorkspaceRoot 'workspace-manifest.json'
if (-not (Test-Path -LiteralPath $workspaceManifestPath -PathType Leaf)) {
    throw "Character capture workspace is missing: $WorkspaceRoot"
}
$workspace = Get-Content -LiteralPath $workspaceManifestPath -Raw |
    ConvertFrom-Json
if ([string]$workspace.schema -ne
    'pokemon-autochess-character-capture-workspace-v1') {
    throw 'Unsupported character capture workspace.'
}
$specPath = Join-Path $WorkspaceRoot ([string]$workspace.capture_spec)
$spec = Get-Content -LiteralPath $specPath -Raw | ConvertFrom-Json
if ([string]$spec.status -ne 'planned') {
    throw 'The source capture launcher currently accepts only planned specs.'
}
if (-not (Test-Path -LiteralPath $ProgramPath -PathType Leaf)) {
    throw "Source-game program/content is missing: $ProgramPath"
}

$renderdoc = Resolve-FullPath (
    [string]$spec.capture_environment.renderdoc.executable)
$emulator = Resolve-FullPath (
    [string]$spec.capture_environment.emulator.executable)
foreach ($required in @($renderdoc, $emulator)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Character capture tool is missing: $required"
    }
}
$renderdocExpected = [string](
    $spec.capture_environment.renderdoc.executable_sha256)
$renderdocActual = (Get-FileHash -LiteralPath $renderdoc -Algorithm SHA256).
    Hash.ToLowerInvariant()
if ($renderdocActual -ne $renderdocExpected.ToLowerInvariant()) {
    throw 'RenderDoc executable SHA-256 does not match the capture spec.'
}
$emulatorExpected = [string](
    $spec.capture_environment.emulator.executable_sha256)
$emulatorActual = (Get-FileHash -LiteralPath $emulator -Algorithm SHA256).
    Hash.ToLowerInvariant()
if ($emulatorActual -ne $emulatorExpected.ToLowerInvariant()) {
    throw 'Emulator executable SHA-256 does not match the capture spec.'
}

$programHash = (Get-FileHash -LiteralPath $ProgramPath -Algorithm SHA256).
    Hash.ToLowerInvariant()
$recordedProgramHash = [string]$spec.source.executable_sha256
if ([string]::IsNullOrWhiteSpace($recordedProgramHash)) {
    throw (
        'The source program/content SHA-256 is not recorded in the capture ' +
        'spec. Record and review this identity before launching: ' +
        $programHash)
}
if ($programHash -ne $recordedProgramHash.ToLowerInvariant()) {
    throw 'Source program/content SHA-256 does not match the capture spec.'
}
if (-not $AllowLaunch) {
    throw 'Launch requires explicit -AllowLaunch after all identities pass.'
}

$existingEmulator = Get-Process -Name (
    [IO.Path]::GetFileNameWithoutExtension($emulator)) -ErrorAction SilentlyContinue
if ($existingEmulator) {
    throw 'The configured emulator is already running. Close it normally first.'
}
$captureRoot = Join-Path $WorkspaceRoot 'captures'
New-Item -ItemType Directory -Path $captureRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$template = Join-Path $captureRoot (
    '{0}-{1}' -f [string]$spec.capture_id, $stamp)

$renderdocArguments = [Collections.Generic.List[string]]::new()
foreach ($argument in @(
        'capture',
        '--opt-disallow-fullscreen',
        '-d',
        (Split-Path -Parent $emulator),
        '-c',
        $template,
        $emulator,
        $ProgramPath)) {
    $renderdocArguments.Add([string]$argument)
}
foreach ($argument in $ProgramArguments) {
    $renderdocArguments.Add([string]$argument)
}

$process = Start-Process `
    -FilePath $renderdoc `
    -ArgumentList @($renderdocArguments) `
    -WorkingDirectory (Split-Path -Parent $emulator) `
    -WindowStyle Hidden `
    -PassThru

$launchRecord = [ordered]@{
    schema = 'pokemon-autochess-character-capture-launch-v1'
    launched_utc = [DateTime]::UtcNow.ToString('o')
    capture_id = [string]$spec.capture_id
    renderdoc_sha256 = $renderdocActual
    emulator_sha256 = $emulatorActual
    program = $ProgramPath
    program_sha256 = $programHash
    capture_template = $template
    launcher_pid = $process.Id
}
$launchPath = Join-Path $WorkspaceRoot (
    'logs\launch-' + $stamp + '.json')
[IO.File]::WriteAllText(
    $launchPath,
    ($launchRecord | ConvertTo-Json -Depth 8),
    (New-Object Text.UTF8Encoding($false)))
$launchRecord
