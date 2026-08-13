[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CapturePath,
    [Parameter(Mandatory = $true)]
    [string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)]
    [string]$StateId,
    [ValidateRange(2, 120)]
    [int]$StableChecks = 4,
    [ValidateRange(50, 5000)]
    [int]$StableIntervalMilliseconds = 500
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

if ($StateId -notmatch '^[a-z0-9][a-z0-9_-]+$') {
    throw "Invalid character-capture state id: $StateId"
}
$CapturePath = Resolve-FullPath $CapturePath
$WorkspaceRoot = Resolve-FullPath $WorkspaceRoot
if (-not (Test-Path -LiteralPath $CapturePath -PathType Leaf) -or
    [IO.Path]::GetExtension($CapturePath) -ne '.rdc') {
    throw "Capture must be an existing RDC file: $CapturePath"
}
if (-not (Test-Path -LiteralPath $WorkspaceRoot -PathType Container)) {
    throw "Character capture workspace is missing: $WorkspaceRoot"
}
$workspaceManifestPath = Join-Path $WorkspaceRoot 'workspace-manifest.json'
if (-not (Test-Path -LiteralPath $workspaceManifestPath -PathType Leaf)) {
    throw "Character capture workspace manifest is missing: $WorkspaceRoot"
}
$workspaceManifest = Get-Content -LiteralPath $workspaceManifestPath -Raw |
    ConvertFrom-Json
if ([string]$workspaceManifest.schema -ne
    'pokemon-autochess-character-capture-workspace-v1') {
    throw 'Unsupported character capture workspace.'
}
$specPath = Join-Path $WorkspaceRoot ([string]$workspaceManifest.capture_spec)
$spec = Get-Content -LiteralPath $specPath -Raw | ConvertFrom-Json
$states = @($spec.states | Where-Object id -eq $StateId)
if ($states.Count -ne 1) {
    throw "State is not uniquely defined by the capture spec: $StateId"
}
if ([string]$states[0].capture.status -ne 'planned') {
    throw "State is not planned and cannot accept a new capture: $StateId"
}

$previousSize = -1L
$stable = 0
while ($stable -lt $StableChecks) {
    $currentSize = (Get-Item -LiteralPath $CapturePath).Length
    if ($currentSize -gt 0 -and $currentSize -eq $previousSize) {
        ++$stable
    } else {
        $stable = 0
        $previousSize = $currentSize
    }
    if ($stable -lt $StableChecks) {
        Start-Sleep -Milliseconds $StableIntervalMilliseconds
    }
}

$protectedRoot = Join-Path $WorkspaceRoot 'protected'
New-Item -ItemType Directory -Path $protectedRoot -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$destinationName = '{0}-{1}-{2}.rdc' -f
    [string]$workspaceManifest.capture_id,
    $StateId,
    $timestamp
$destination = Join-Path $protectedRoot $destinationName
$staging = $destination + '.copying'
if ((Test-Path -LiteralPath $destination) -or
    (Test-Path -LiteralPath $staging)) {
    throw "Protected capture destination already exists: $destination"
}

Copy-Item -LiteralPath $CapturePath -Destination $staging
$sourceHash = (Get-FileHash -LiteralPath $CapturePath -Algorithm SHA256).
    Hash.ToLowerInvariant()
$copyHash = (Get-FileHash -LiteralPath $staging -Algorithm SHA256).
    Hash.ToLowerInvariant()
if ($sourceHash -ne $copyHash) {
    Remove-Item -LiteralPath $staging -Force
    throw "Capture copy SHA-256 mismatch: $CapturePath"
}
Move-Item -LiteralPath $staging -Destination $destination

$record = [pscustomobject][ordered]@{
    schema = 'pokemon-autochess-protected-character-capture-v1'
    capture_id = [string]$workspaceManifest.capture_id
    state_id = $StateId
    source = $CapturePath
    protected_copy = 'protected/' + $destinationName
    captured_at = (Get-Item -LiteralPath $CapturePath).LastWriteTimeUtc.
        ToString('o')
    protected_at = [DateTime]::UtcNow.ToString('o')
    bytes = (Get-Item -LiteralPath $destination).Length
    sha256 = $copyHash
}
$manifestPath = Join-Path $protectedRoot 'manifest.jsonl'
$jsonLine = $record | ConvertTo-Json -Compress
[IO.File]::AppendAllText(
    $manifestPath,
    $jsonLine + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host "Protected character capture: $destination"
$record
